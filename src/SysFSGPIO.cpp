 /*
 * SysFSGPIO.cpp
 *
 * A Linux SysFS GPIO abstraction for C++
 *
 * This *requires* a 2011 C++ standard compliant compiler to compile and work.
 *
 * Copyright (c) 2018 Frank C. Earl
 * All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.  You may add your
 *    own copyright notice relative to your modifications, but you cannot claim
 *    the code herein as solely your own.
 *
 * 2. Binary redistributions must reproduce the above copyright notice, either
 *    in the initial output of the derived application, a "help" screen, or in
 *    the documentation that accompanies the same.
 *
 * 3. Neither the name of the copyright holder nor the names of this software's
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.  Compliance with
 *    condition 2 does not constitute a violation of this condition.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * SPECIFICALLY DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <unistd.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/poll.h>
#include <sys/stat.h>


#include <iostream>
#include <fstream>
#include <stdexcept>

#include <string>
using std::string;

#include <SysFSGPIO.hpp>


const std::string SysFSGPIO::_sysfsPath("/sys/class/gpio/");

const ssize_t MAX_BUF = 2;


/**
 * Default constructor for the SysFSGPIO class.
 *
 * Initializes a SysFSGPIO object with default values:
 * - _id: 0
 * - _id_str: an empty string
 * - _direction: Direction::NO_DIR
 * - _edge: Edge::NONE
 * - _fd: -1 (indicating no file descriptor is open)
 * - _data: NULL (no user data associated)
 * - _activeLow: false (active-high logic level)
 * - _doTeardown: true (indicating that cleanup should be performed on destruction)
 */

SysFSGPIO::SysFSGPIO() :
		_id(0),
		_id_str(""),
		_direction(Direction::NO_DIR),
		_edge(Edge::NONE),
		_fd(-1),
		_data(NULL),
		_activeLow(false),
		_doTeardown(true)
{
}

/**
 * Constructor that sets up a SysFSGPIO object with a specific ID and direction, and
 * optionally sets the pin to active-low logic level.
 *
 * This constructor sets up a SysFSGPIO object with the specified @a id and @a direction,
 * and optionally sets the pin to active-low logic level. It also sets up the GPIO
 * for use.
 *
 * @param id The GPIO ID to use.
 * @param direction The direction of the GPIO.  May be either IN or OUT.
 * @param useActiveLow Set to true if the pin should be treated as an active-low
 *                     logic level.
 */
SysFSGPIO::SysFSGPIO(uint16_t id, Direction direction, bool useActiveLow) :
		_id(id),
		_id_str(std::to_string(id)),
		_direction(direction),
		_edge(Edge::NONE),
		_fd(-1),
		_data(NULL),
		_activeLow(useActiveLow),
		_doTeardown(true)
{
	// Simple.  Export out the GPIO with the specified direction...  There's few cleanups to be done...
	exportGPIO();
}

/**
 * Constructor that sets up a SysFSGPIO object with a specific ID, edge detection,
 * and a callback function. This constructor initializes a SysFSGPIO object with
 * the specified @a id, @a edge, @a callback, and @a data. The GPIO is configured
 * for input with the specified edge detection behavior and optionally set to
 * active-low logic level. It also starts a thread to handle edge-triggered events.
 *
 * @param id The GPIO ID to use.
 * @param edge The edge type for triggering callbacks. Must not be Edge::NONE.
 * @param callback The function to call when an edge is detected.
 * @param data User-defined data to be passed to the callback function.
 * @param useActiveLow Set to true if the pin should be treated as an active-low
 *                     logic level.
 * @throws std::runtime_error If no edge is specified, if setting the edge behavior
 *         fails, or if the GPIO value file cannot be opened.
 */

SysFSGPIO::SysFSGPIO(uint16_t id, Edge edge, CallbackFunction callback, void *data, bool useActiveLow) :
		_id(id),
		_id_str(std::to_string(id)),
		_direction(Direction::IN),
		_edge(edge),
		_fd(-1),
		_callback(callback),
		_data(data),
		_activeLow(useActiveLow),
		_doTeardown(true)
{
	char buf[MAX_BUF];

	if (_edge == Edge::NONE)
	{
		// You can't exactly make a callback against NO edge...  X-D
		throw std::runtime_error("Unable to set callback without an EDGE");
	}

	// Now, init the GPIO accordingly...
	exportGPIO();

	//attempt to set edge detection
	{
		std::ofstream sysfs_edge(_sysfsPath + "gpio" + _id_str + "/edge", std::ofstream::app);
		if( !sysfs_edge.is_open() )
		{
			throw std::runtime_error("Unable to set edge behavior for GPIO " + _id_str + ".");
		}
		switch(_edge)
		{
		case Edge::RISING :
			sysfs_edge << "rising";
			break;

		case Edge::FALLING :
			sysfs_edge << "falling";
			break;

		case Edge::BOTH :
			sysfs_edge << "both";
			break;
		}
		sysfs_edge.close();
	}

	// Attempt to get our FD set up for poll();
	if (!PathExists(_sysfsPath + "gpio" + _id_str + "/value"))
	{
		throw std::runtime_error("Unable to open GPIO " + _id_str + "'s value for callback");
	}

	// Can't really gracefully get a file descriptor (REQUIRED) out of an iostream...so...
	const std::string path(_sysfsPath + "gpio" + _id_str + "/value");
	_fd = open(path.c_str(), O_RDONLY | O_NONBLOCK); // closed in destructor
	if( _fd < 0 )
	{
		perror("open");
		throw std::runtime_error("Unable to open GPIO " + _id_str + "'s value for callback");
	}

	// Now that we've got a file descriptor in hand, let's burn the initial value out of
	// it...
	ssize_t nbytes = read(_fd, buf, MAX_BUF);
	if( nbytes != MAX_BUF )
	{
		if( nbytes < 0 )
		{
			perror("SysFSGPIO()");
		}
	}

	// Now that we have GPIO-age...start the thread!
	start();
}

/*************  ✨ Codeium Command ⭐  *************/
/**
 * Destructor for the SysFSGPIO class.
 *
 * This destructor ensures that any running thread is stopped and joined,
 * closes the file descriptor if it is open, and unexports the GPIO,
 * cleaning up resources used by the SysFSGPIO object.
 */

/******  26733cba-4843-4557-86c7-8994b2c3f179  *******/
SysFSGPIO::~SysFSGPIO()
{
	if (isRunning())
	{
		// If we're running stop, join, clear our thread...
		stop();
		join();
		if (NULL != _thread) delete _thread;
	}

	if (_fd > -1)
	{
		// Have a file descriptor...close it.
		close(_fd);
	}

	// Have a GPIO we've exported...unexport...
	unexportGPIO();
}


/**
 * Export the GPIO specified in the constructor, and set its direction according to that constructor.
 * If the GPIO is already exported, do not re-export it, but do set the direction.
 *
 * If the GPIO's value is currently high, leave it that way.  If it is low, set it to low.  This is
 * useful for LEDs, etc. where we want to be able to control them with multiple SysFSGPIO objects.
 *
 * Throw exceptions if we can't export, set direction, or set active low.
 */
void SysFSGPIO::exportGPIO(void)
{
	// We're going to check for the presence of /sys/class/gpio...
    if( !PathExists(_sysfsPath) )
    {
       throw std::runtime_error(_sysfsPath + " does not exist.");
    }

    // Check to see if the GPIO entry was there to begin with.  We'll
    // set it the specified way, but if it was lit...leave it be.
    // (This may be "wrong" but it lets us do things like LEDs, etc.
    // with multiple "controllers" and do them "right" all the same...)
	if (PathExists(_sysfsPath + "gpio" + _id_str))
	{
	    // It exists before any other steps, so skip the start-up steps
	    // and move to the config and declare it not needing to be torn down.
	    _doTeardown = false;
	}
	else
	{
        // We're not going to be fancy...either this works right or we throw an exception
        // indicating that we couldn't get the GPIO generated- and we CAN ask that question
        // quicker than trying to guess whether we have a valid GPIO ID in hand....
        // attempt to export
		std::ofstream sysfs_export(_sysfsPath + "export", std::ofstream::app);
		if( !sysfs_export.is_open() )
		{
			throw std::runtime_error("Unable to export GPIO " + _id_str);
		}
		sysfs_export << _id_str;
		sysfs_export.close();

		// Now, double-check it got MADE...
		if (!PathExists(_sysfsPath + "gpio" + _id_str))
		{
			throw std::runtime_error("Unable to export GPIO(2) " + _id_str);
		}
	}

    //attempt to set direction
    {
		std::ofstream sysfs_direction(_sysfsPath + "gpio" + _id_str + "/direction", std::ofstream::app);
		if( !sysfs_direction.is_open() )
		{
			throw std::runtime_error("Unable to set direction for GPIO " + _id_str);
		}
		switch(_direction)
		{
		case Direction::IN :
			sysfs_direction << "in";
			break;

		case Direction::OUT :
			sysfs_direction << "out";
			break;
		}
		sysfs_direction.close();
    }

    // Attempt to set active low - Some things we want signal high to be "off" (Like LEDs)
    {
       std::ofstream sysfs_activelow(_sysfsPath + "gpio" + _id_str + "/active_low", std::ofstream::app);
       if( !sysfs_activelow.is_open() )
       {
          throw std::runtime_error("Unable to set active_low for GPIO " + _id_str);
       }
       if (_activeLow)
       {
    	   sysfs_activelow << "1";
       }
       else
       {
    	   sysfs_activelow << "0";
       }
       sysfs_activelow.close();
    }

    // Initially set the value to low on the line to start inbound or to clear the value on
    // outbound...set it "off"...
	{
		std::ofstream sysfs_value(_sysfsPath + "gpio" + _id_str + "/value", std::ofstream::app);
		if( !sysfs_value.is_open() )
		{
			throw std::runtime_error("Unable to initialize value for GPIO " + _id_str);
		}
		sysfs_value << "0";
		sysfs_value.close();
	}
}


/**
    * @brief Unexport GPIO
    *
    * Unexport the GPIO given that we exported it ourselves. This method is
    * idempotent, so it's safe to call it multiple times.
    *
    * @throw std::runtime_error if unable to unexport GPIO
    */
void SysFSGPIO::unexportGPIO(void)
{
    // Unexport only when we constructed it ourselves at our code level...
    if (_doTeardown)
	{
		std::ofstream sysfs_export(_sysfsPath + "unexport", std::ofstream::app);
		if( !sysfs_export.is_open() )
		{
			throw std::runtime_error("Unable to unexport GPIO " + _id_str);
		}
		sysfs_export << _id_str;
		sysfs_export.close();
	}
}

/**
    * @brief Get the value of the GPIO line
    *
    * Get the current value of the GPIO line.  If the line is set to READ
    * and/or configured for callbacks, you're not supposed to use this
    * function (The value is handed to you in the callback...)
    *
    * @return Value of the GPIO line or Value::INVALID if not set to READ
    *         or if edge is not NONE
    */
Value SysFSGPIO::getValue(void)
{
	// Presume we're operating outside of parameters...  This would be if the GPIO is set
	// to READ and for Callbacks.  If so, you're not supposed to read the GPIO line via
	// getValue() (It's handed to you in the callback...)
	Value retVal = Value::INVALID;
	if ((_edge == Edge::NONE) && (_direction != Direction::NO_DIR))
	{
		// Edge has to be NONE if it's a valid mode for us...
		std::ifstream sysfs_value(_sysfsPath + "gpio" + _id_str + "/value");
		if( !sysfs_value.is_open() )
		{
			throw std::runtime_error("Unable to get value for GPIO " + _id_str);
		}

		char value = sysfs_value.get();
		if( !sysfs_value.good() )
		{
			throw std::runtime_error("Unable to get value for GPIO " + _id_str);
		}

		switch(value)
		{
		case '0':
			retVal = Value::LOW;
			break;

		case '1':
			retVal = Value::HIGH;
			break;

		default:
			retVal = Value::INVALID;
			break;
		}
	}

	return retVal;
}

/**
    * @brief Set the value of the GPIO line
    *
    * Set the value of the GPIO line.  If the line is set to READ and/or
    * configured for callbacks, you're not supposed to use this function
    * (The value is handed to you in the callback...)
    *
    * @param value Value to set for the GPIO line
    *
    * @return Value of the GPIO line that was attempted to be set if it's
    *         valid, Value::INVALID if not set to WRITE or if edge is not
    *         NONE
    */
Value SysFSGPIO::setValue(Value value)
{
	// Presume we're operating outside of parameters...  This would be if the GPIO is set
	// to READ and for Callbacks.  If so, you're not supposed to read the GPIO line via
	// getValue() (It's handed to you in the callback...) or set the value...  We return
	// what we attempted to set if it's good...
	Value retVal = Value::INVALID;

	if ((_edge == Edge::NONE) && (_direction != Direction::NO_DIR))
	{
		// Edge has to be NONE if it's a valid mode for us...
		retVal = value;

		// While setting an Input doesn't make any sense, NORMALLY, we want to
		// allow the user to clear state and have it set itself again, so, we
		// allow this for anything other than the ABOVE condition...
		std::ofstream sysfs_value(_sysfsPath + "gpio" + _id_str + "/value", std::ofstream::app);
		if( !sysfs_value.is_open() )
		{
		  throw std::runtime_error("Unable to set value for GPIO " + _id_str);
		}
		switch(value)
		{
		case Value::LOW :
			sysfs_value << "0";
			break;

		case Value::HIGH :
			sysfs_value << "1";
			break;
		}
		sysfs_value.close();
	}

	return retVal;
}


/**
    * @brief Start the run loop for GPIO monitoring
    *
    * This member function will start a loop that will monitor the GPIO line for
    * changes.  When a change is detected, it will call the callback function
    * registered with the GPIO line.
    *
    * @note If using callbacks, you MUST call stop() to stop the loop.  If you
    *       don't, the loop will continue to run and your program will not exit
    *       properly.
    */
void SysFSGPIO::run(void)
{
	char buf[MAX_BUF];
	struct pollfd fdset[1];

	memset((void*)fdset, 0, sizeof(fdset));

	fdset[0].fd     = _fd;
	fdset[0].events = POLLPRI;

	// Presume it's all properly set up and we're going to go live with callbacks...
	while (_run)
	{
		// Set up a poll() call...
		int pollRet = poll(fdset, 1, -1);
		if( 1 == pollRet )
		{
			if(fdset[0].revents & POLLPRI)
			{
				// Got a new value to absorb...
				lseek(fdset[0].fd, 0, SEEK_SET);
				ssize_t nbytes = read(fdset[0].fd, buf, MAX_BUF);
				if( nbytes != MAX_BUF ) // See comment above
				{
					if( nbytes < 0 )
					{
						perror("SysFSGPIO::run()");
					}
					throw std::runtime_error("GPIO " + _id_str + " SysFSGPIO::run() badness...");
				}

				// Figure out what it is...
				Value val;
				switch (buf[0])
				{
				case '0' :
					val = Value::LOW;
					break;

				case '1' :
					val = Value::HIGH;
					break;

				default :
					val = Value::INVALID;
					break;
				}

				// Call our callback function with the value and possible pointer to data/object.  Call-ee MUST return.
				_callback(val, _data);
			}
		}
	}
}
// Dogsbody for the callback engine...
