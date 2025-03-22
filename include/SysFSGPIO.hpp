/*
 * SysFSGPIO.hpp
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


#ifndef INCLUDE_SYSFSGPIO_H_
#define INCLUDE_SYSFSGPIO_H_

// This is LINUX-only...so while the toolset's cross-platform, until we can
// devise a similar solution set for Windows and OSX, we'll block this include
// which will get installed on other targets, if it's not Linux...
#if defined(__linux__)

#include <NONCOPY.hpp>
#include <Runable.hpp>
#include <unistd.h>

#include <functional>
using std::function;

#include <string>
using std::string;
using std::to_string;

// Helpful typedefs...

//  Direction, used to help indicate IN/OUT for code.
typedef enum
{
	IN,
	OUT,
	NO_DIR
} Direction;

// Value, used to help code specify signal HIGH/LOW...
typedef enum
{
	LOW,
	HIGH,
	INVALID
} Value;

// Edge, used to help code specify edge(s) for event callback...
typedef enum
{
	NONE,
	RISING,
	FALLING,
	BOTH
} Edge;

typedef function<void(Value, void*)> CallbackFunction;


class SysFSGPIO: public Runable
{
public:
	// Having to make a default constructor- if you want to use SharedReference,
	// you HAVE to.  (SIGH...)
	SysFSGPIO();

	// In/Out constructor without an event callback for input...
	SysFSGPIO(uint16_t id, Direction direction, bool useActiveLow = false);

	// In constructor with an event callback for input...  Destructor **MUST** unwind the
	// callback thread when it's this case...
	SysFSGPIO(uint16_t id, Edge edge, CallbackFunction callback, void *data = NULL, bool useActiveLow = false);

	// Destructor...should correctly unwind unless something catastrophic happens...
	virtual ~SysFSGPIO();

	// Get my value...if a callback is set on us, we return INVALID.
	// (You're supposed to use the callback's return... )
	Value getValue(void);

	// Set my value...returns value or invalid...
	Value setValue(Value value);

	// Get my ID...
	uint16_t getID(void) { return _id; }

	// Check for seeing if a designated chip entry for our GPIOs is even THERE.
	static bool checkForGPIOChip(uint16_t _id)
	{
		string gpiochipPath = _sysfsPath + "gpiochip" + to_string(_id);
		return PathExists(gpiochipPath);
	};

protected:
	virtual void run(void);

private:
	// The generic path into the sysfs GPIO class edge...
	static const string  _sysfsPath;

	// MY GPIO's internal params...
	uint16_t 				_id;			// What's the GPIO number for this object?
	string					_id_str;		// String version of the GPIO number...
	Direction   			_direction;		// What direction was set on init?
	Edge					_edge;			// What (optional) edge was set on init?
	CallbackFunction		_callback;		// Did we have a callback set on us?
	int						_fd;			// Is there an FD opened against this GPIO?
	void *					_data;			// Generic pointer to data that can be passed to the callback.
	bool					_activeLow;		// Are we set active low?
	bool                    _doTeardown;    // Was the GPIO config there before we came into existence?

	// Export out GPIO...
	void exportGPIO(void);

	// Unexport the GPIO
	void unexportGPIO(void);

	// Quick, NASTY way to check if a path exists...
	static bool PathExists( const std::string &Pathname ) { return access( Pathname.c_str(), 0 ) == 0;	};
};

#endif // #if defined(__linux__)

#endif /* INCLUDE_SYSFSGPIO_H_ */
