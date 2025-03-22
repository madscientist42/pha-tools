
#include <string>
using std::string;

#include <iostream>
using std::cout;
using std::endl;
using std::flush;

#include <mutex>
using std::mutex;
using std::lock_guard;

// We're using the simpler (albeit only SLIGHTLY so..) C API for libgpiod
// as the C++ wrapper, especially in the 2.x api where they radically cnaged
// the API to make it, "more generic" and simply made it more painful to use.
// 
// You didn't want to provde a list of "offsets" for lines.  Most of the time
// you're dealing with only one or a couple of lines instead of the whole chip's
// worth (or major portions thereof) in your code.  Sigh...disappointing, 
//really.  VERY disappointing.
#include <gpiod.h>

#include "KernelGPIO.hpp"

#include <errno.h>

KernelGPIO::KernelGPIO(string chipname, size_t line) : 
    m_value(false), m_direction(INPUT), m_active_low(false), m_callback(nullptr), 
    m_chip(nullptr), m_line(nullptr), m_request(nullptr)
{
    // Do a small amount of sanity checking.  Range needs to be 0->chip's capacity
    if (line < 0)
    {
        cout << "Invalid line number specified.  Must be >= 0" << endl;
    }
    else
    {
        // Open the chip...
        m_chip = gpiod_chip_open(chipname.c_str());
        if (m_chip == nullptr)
        {
            cout << "Failed to open GPIO chip <" << chipname << ">" << endl;
        }
    }
}

/**
 * Destructor for the KernelGPIO class.  This will close the handles
 * we have open on the GPIO line and chip.  If the run() method  is 
 * running, this will also stop that thread.
 */
KernelGPIO::~KernelGPIO() 
{
    // Close down the thread, wait for it to finish.
    if (isRunning())
    {
        stop();
        join();
    }

    // Clean up the allocations we've made...
    close_chip();
};


/**
 * Configure the GPIO line.  This will configure the GPIO line with the
 * provided settings.  If the line is an output line, the value parameter
 * will be set.  If the line is an input line, the edge detection parameter
 * will be set.  If the edge detection parameter is set to NONE, the thread
 * for this GPIO line will not be started.
 *
 * @param direction The direction to configure the GPIO line as (INPUT or
 * OUTPUT).
 * @param active_low If true, the active state of the GPIO line will be
 * inverted.
 * @param edge The edge detection setting for the GPIO line.  This will be
 * ignored if the line is an output line.
 * @param value The value to set on the GPIO line if it is an output line.
 * This will be ignored if the line is an input line.
 *
 * @return true if the configuration was successful, false otherwise.
 */
bool KernelGPIO::configure(gpio_direction_t direction, bool active_low, gpio_edge_t edge, bool value) 
{
    bool retVal = false;        // Assume failure.
    int ret = 0;
    unsigned int offset = m_line_num;

    // Drop the current request...
    release_request();

    // Start a new one...
    struct gpiod_line_config *cfg;
    struct gpiod_line_settings *settings = gpiod_line_settings_new();

    if (!settings)
    {
        cout << " KernelGPIO : Failed to allocate line settings" << endl << flush;
    }
    else
    {
        ret = gpiod_line_settings_set_direction(settings, (gpiod_line_direction) direction);
        if (ret < 0)
        {
            cout << " KernelGPIO : Failed to set direction" << endl << flush;
        }
        else
        {
            // If we're an output line set here, do that and preserve our
            // value for readback.
            if (direction == OUTPUT)
            {
                ret = gpiod_line_settings_set_output_value(settings, (value ?  GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE));
                if (ret < 0)
                {
                    cout << " KernelGPIO : Failed to set output value" << endl << flush;
                }
                else
                {   
                    // Preserve the value...we can't read it back from the line.
                    m_value = value;
                }
            }

            // Set the edge detection.
            ret = gpiod_line_settings_set_edge_detection(settings, (gpiod_line_edge) edge);
            if (ret < 0)
            {
                cout << " KernelGPIO : Failed to set edge detection" << endl << flush;
            }
            else
            {
                // Set active low
                gpiod_line_settings_set_active_low(settings, active_low);

                // All done.  Start processing the line config out of this now.
                cfg = gpiod_line_config_new();
                if (!cfg)
                {
                    cout << " KernelGPIO : Failed to allocate line config" << endl << flush;
                }
                else
                {
                    ret = gpiod_line_config_add_line_settings(cfg, &offset, 1, settings);
                    if (ret < 0)
                    {
                        cout << " KernelGPIO : Failed to add line settings" << endl << flush;
                    }
                    else
                    {
                        // Start configuring the request/line...
                        cfg = gpiod_line_config_new();
                        if (!cfg)
                        {
                            cout << " KernelGPIO : Failed to allocate line config" << endl << flush;
                        }
                        else
                        {
                            ret = gpiod_line_config_add_line_settings(cfg, &offset, 1, settings);
                            if (ret < 0)
                            {
                                cout << " KernelGPIO : Failed to add line settings" << endl << flush;
                            }
                            else
                            {
                                struct gpiod_request_config *req_cfg = gpiod_request_config_new();
                                if (!req_cfg)
                                {
                                    cout << " KernelGPIO : Failed to allocate request config" << endl << flush;
                                }
                                else
                                {
                                    gpiod_request_config_set_consumer(req_cfg, "KerneoGPIO");
                                    m_request = gpiod_chip_request_lines(m_chip, req_cfg, cfg);
                                    if (!m_request)
                                    {
                                        cout << " KernelGPIO : Failed to request GPIO line" << endl << flush;
                                    }
                                    else
                                    {
                                        // We're a go.
                                        retVal = true;

                                        // Check to see if we were told to set edge detection.
                                        if (edge != NONE)
                                        {
                                            // Yep.  Store our cached info for the config and start the thread.
                                            m_direction = direction;
                                            m_edge = edge;
                                            start();
                                        }
                                    }
                                }

                                // Clean up after yourself
                                gpiod_request_config_free(req_cfg);
                            }
                        }
                    }

                    // Clean up after yourself
                    gpiod_line_config_free(cfg);
                }
            }
        }

        // Clean up after yourself
        gpiod_line_settings_free(settings);            
    }

    return retVal;
}

/**
 * @brief Set the value of a GPIO line.
 *
 * @param value The value to set, true for high, false for low.
 * @return true on success, false on failure.  If the line is not configured for output,
 *         then the function is a no-op and returns false.
 */
bool KernelGPIO::set_value(bool value)
{
    bool retVal = false;
    int ret = 0;

    if (m_direction != OUTPUT)
    {
        cout << " KernelGPIO : Line is not configured for output" << endl << flush;
    }
    else
    {
        // Check to see if we have a request.
        if (m_request == nullptr)
        {
            cout << " KernelGPIO : Request is null??" << endl << flush;
        }
        else
        {
            // We do, presume since the preserved mode is OUTPUT that this will work now.
            ret = gpiod_line_request_set_value(m_request, m_line_num, (value ?  GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE));
            if (ret < 0)
            {
                cout << " KernelGPIO : Failed to set value - errno = " << errno << endl << flush;
            }
            else
            {
                // Success.  Preserve the value for readback.
                m_value = value;
                retVal = true;
            }
        }
    }

    return retVal;
}

/**
 * @brief Get the "value" of the GPIO line.
 *
 * This function pulls the value of the GPIO line.  If in output mode, the
 * previously set value is returned.  If in input mode, without edge detection, 
 * the value is read directly from the line and returned.  If edge detection 
 * is set, the latching behavior is as follows:
 *
 *  - gpio_edge_t::RISING: Return the value of the internal store and
 *    force the internal store false on read.  If the line has changed
 *    it will be latched active until the read.
 *  - gpio_edge_t::FALLING: Return the value of the internal store and
 *    force the internal store true on read.  If the line has changed
 *    it will be latched inactive until the read.
 *  - gpio_edge_t::BOTH: Return the value of the last event and keep the
 *    internal store as is.  It will latch to the last read event in a 
 *    current processing run for events.
 *
 * @return The value of the GPIO line, actual, or approximated as described.
 *         Also, this will return false on a failure.
 */
bool KernelGPIO::get_value()
{
    bool retVal = false;
    int ret = 0;

    // This is somewhat complex, depending on what our settings have, we pull from
    // the internal store value or from the actual line setting.
    if (m_chip != nullptr)    
    {
        if (m_request == nullptr)
        {
            if (m_direction == gpio_direction_t::OUTPUT)
            {
                // Can't read the value from the line so we report what was previously set.
                retVal = m_value;
            }
            else
            {
                // Input mode.  Now we need to see what settings we have.  Edge detection modes
                // set on eventing will dictate some of our behaviors.
                if (m_edge == gpio_edge_t::NONE)
                {
                    // No edge detection.  Just read the value.
                    if (m_request == nullptr)
                    {
                        cout << " KernelGPIO : Request is null??" << endl << flush;
                    }
                    else 
                    {
                        ret = gpiod_line_request_get_value(m_request, m_line_num);
                        if (ret < 0)
                        {
                            cout << " KernelGPIO : Failed to get value - errno = " << errno << endl << flush;
                        }
                        {
                            retVal = ret;
                        }
                    }
                    
                }

                switch (m_edge)
                {
                    case gpio_edge_t::NONE:
                        // No edge detection.  Just read the value.
                        if (m_request == nullptr)
                        {
                            cout << " KernelGPIO : Request is null??" << endl << flush;
                        }
                        else 
                        {
                            ret = gpiod_line_request_get_value(m_request, m_line_num);
                            if (ret < 0)
                            {
                                cout << " KernelGPIO : Failed to get value - errno = " << errno << endl << flush;
                            }
                            {
                                retVal = ret;
                            }
                        }
                        break;

                    case gpio_edge_t::BOTH:
                        // Just return the internal store.  Last event in a string up to this point is the value
                        retVal = m_value;
                        break;

                    case gpio_edge_t::RISING:
                        // Latching behavior- you will see rising edges meaning line goes active
                        // but we won't see returns to inactive on the line. Return our internal
                        // store's value and force it to false.  If you need to know each rising
                        // value, set a callback.
                        retVal = m_value;
                        m_value = false;
                        break;

                    case gpio_edge_t::FALLING:
                        // Latching behavior- you will see falling edges meaning line goes inactive
                        // but we won't see returns to active on the line. Return our internal
                        // store's value and force it to true.  If you need to know each falling
                        // value, set a callback.
                        retVal = m_value;
                        m_value = true;
                        break;
                }                    
            }
        }
        else
        {
            cout << "KernelGPIO : No request open" << endl << flush;
        }
    }
    else
    {
        cout << "KernelGPIO : No chip open" << endl << flush;
    }

    return retVal;
}

void KernelGPIO::run()
{
    int ret = 0;
    bool value = false;

    // Allocate out a buffer for events- ONE event per each pass.
    struct gpiod_edge_event_buffer *buf = gpiod_edge_event_buffer_new(1);
    if (buf == nullptr)
    {
        cout << " KernelGPIO : Failed to allocate event buffer" << endl << flush;        
    }
    else
    {
        // This loop only runs in the right modes...
        while (_run && m_direction == gpio_direction_t::INPUT && m_edge != gpio_edge_t::NONE) 
        {
            // Wait for an event to happen...indefinitely...
            ret = gpiod_line_request_wait_edge_events(m_request, -1);
            if (ret < 0)
            {
                cout << " KernelGPIO : Failed to wait for event - errno = " << errno << endl << flush;
            }
            ret = 0;
            while (ret == 0)
            {
                // Read events...ONE AT A TIME for a reason.
                ret = gpiod_line_request_read_edge_events(m_request, buf, 1);
                if (ret < 0)
                {
                    // We're done either way.  Last set of events have been read...
                    // We're just checking to see if this needs an error logged or not
                    if (errno != EAGAIN)
                    {
                        cout << " KernelGPIO : Failed to read event - errno = " << errno << endl << flush;
                    }
                }
                else if (ret == 0)
                {
                    struct gpiod_edge_event *ev = gpiod_edge_event_buffer_get_event(buf, 0);

                    // We got the event.  Process it.
                    switch (gpiod_edge_event_get_event_type(ev))
                    {
                        case GPIOD_LINE_EDGE_RISING:
                            m_value = true;
                            break;
                        case GPIOD_LINE_EDGE_FALLING:
                            m_value = false;
                            break;
                        default:
                            cout << " KernelGPIO : Unknown event type - " << gpiod_edge_event_get_event_type(ev) << endl << flush;
                            break;
                    }
                }
            }
        }

        // Clean up after yourself...
        gpiod_edge_event_buffer_free(buf);
    }
}


/**
 * @brief Release the line_request and free all associated resources.
 *
 * If the class is not currently using a request, this call is a no-op.
 * Otherwise, this will free up the resources associated with the current
 * chip for immediate reuse.
 */
void KernelGPIO::release_request()
{
    // Double free is not possible with the C API, but we don't need to 
    // do things if it was already closed...       
    if (m_request != nullptr)
    {
        gpiod_line_request_release(m_request);
        // Not strictly needed, but we use nullptr as a barrier for not opened/closed...
        m_request = nullptr;
    }
}

/**
 * @brief Close the current chip and release all associated resources.
 *
 * If the class is not currently using a chip, this call is a no-op.
 * Otherwise, this will free up the resources associated with the current
 * chip for immediate reuse.
 */
void KernelGPIO::close_chip()
{
    // Double close is not possible with the C API, but we don't need to 
    // do things if it was already closed...       
    if (m_chip != nullptr)
    {
        gpiod_chip_close(m_chip);
        // Not strictly needed, but we use nullptr as a barrier for not opened/closed...
        m_chip = nullptr;
    }
}   