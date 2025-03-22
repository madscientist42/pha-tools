#pragma once

#include <string>
using std::string;

#include <iostream>
using std::cout;
using std::endl;
using std::flush;

#include <mutex>
using std::mutex;
using std::lock_guard;

#include <atomic>
using std::atomic;
using std::memory_order;

#include <Runable.hpp>

// We're using the simpler (albeit only SLIGHTLY so..) C API for libgpiod
// as the C++ wrapper, especially in the 2.x api where they radically cnaged
// the API to make it, "more generic" and simply made it more painful to use.
//
// You didn't want to provde a list of "offsets" for lines.  Most of the time
// you're dealing with only one or a couple of lines instead of the whole chip's
// worth (or major portions thereof) in your code.  Sigh...disappointing,
//really.  VERY disappointing.
#include <gpiod.h>

/*
    Provide for easy-ish use of libgpiod without having to deal with the C++ wrapper
    as it's kind of painful to use compared to the C API.  This works generically
    as a single interface to each GPIO line you're driving to simplify the code
    and interface.  The main problem with the C++ wrapper is that it tries to handle
    each and every kind of situation with pins...this one just cares whether
*/
class KernelGPIO : public Runable
{
    public:
        // Declare out a cleaner, simpler direction typedef
        typedef enum gpio_direction_t
        {
            INPUT = GPIOD_LINE_DIRECTION_INPUT,
            OUTPUT = GPIOD_LINE_DIRECTION_OUTPUT
        } gpio_direction_t;

        // Declare out a cleaner, simpler edge detection typedef
        typedef enum gpio_edge_t
        {
            NONE = GPIOD_LINE_EDGE_NONE,
            RISING = GPIOD_LINE_EDGE_RISING,
            FALLING = GPIOD_LINE_EDGE_FALLING,
            BOTH = GPIOD_LINE_EDGE_BOTH
        } gpio_edge_t;

        // Declare out a typedef that is explicitly tied to this abstraction's
        // callback implementation for use in the interrupt processing. If it's
        // passed in as a nullptr, we don't handle callbacks.
        typedef void (*gpio_callback_t)(bool value);

        // We open to the chip and line number we're interested in, failure blocks other calls.
        KernelGPIO(string chipname, size_t line);
        ~KernelGPIO();

        // This can (re-)configure the GPIO line to the right mode, behavior, etc.
        // An error will leave the object in a non-configured state...
        bool configure(gpio_direction_t direction = INPUT, bool active_low = false, gpio_edge_t edge = NONE, bool value = false);

        // Set the callback to be called when the line changes state when we're in edge detection mode
        // Ignored if we're not in edge detection mode, can be set to NULL to turn this off.
        void set_callback(gpio_callback_t callback) { m_callback = callback; }

        // Line value getter/setter.
        bool get_value();
        bool set_value(bool value);

        // Info about the GPIO chip and line defined by this object and config.
        string get_chipname() { return chipname; }
        size_t get_line() { return m_line_num; }
        gpio_direction_t get_direction() { return m_direction; }
        gpio_edge_t get_edge() { return m_edge; }
        bool get_active_low() { return m_active_low; }

    protected:
        void run();

    private:
        string                      chipname;
        atomic<bool>                m_value;
        atomic<gpio_direction_t>    m_direction;
        atomic<gpio_edge_t>         m_edge;
        atomic<bool>                m_active_low;
        atomic<gpio_callback_t>     m_callback;
        unsigned int                m_line_num;
        struct gpiod_chip           *m_chip;
        struct gpiod_line           *m_line;
        struct gpiod_line_request   *m_request;

        // Helper functions
        void release_request();
        void close_chip();
};

