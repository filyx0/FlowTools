#include "flow/log.hpp"
#include "flow/terminal.hpp"

#include <iostream>
#include <windows.h>

namespace
{
    HANDLE get_output_console()
    {
        return GetStdHandle(STD_OUTPUT_HANDLE);
    }

    HANDLE get_error_console()
    {
        return GetStdHandle(STD_ERROR_HANDLE);
    }

    void set_color(HANDLE console, WORD color)
    {
        SetConsoleTextAttribute(console, color);
    }

    void reset_color(HANDLE console)
    {
        SetConsoleTextAttribute(
            console,
            FOREGROUND_RED |
            FOREGROUND_GREEN |
            FOREGROUND_BLUE
        );
    }

    void print(
        HANDLE console,
        std::ostream& stream,
        WORD color,
        std::string_view prefix,
        std::string_view message
    )
    {
        const bool use_color = flow::terminal::supports_color();

        if (use_color)
            set_color(console, color);

        stream << prefix;

        if (use_color)
            reset_color(console);

        stream << message << '\n';
    }
}

namespace flow::log
{
    void info(std::string_view message)
    {
        print(
            get_output_console(),
            std::cout,
            FOREGROUND_BLUE |
            FOREGROUND_GREEN |
            FOREGROUND_INTENSITY,
            "[ INFO ] ",
            message
        );
    }

    void success(std::string_view message)
    {
        print(
            get_output_console(),
            std::cout,
            FOREGROUND_GREEN |
            FOREGROUND_INTENSITY,
            "[  OK  ] ",
            message
        );
    }

    void warning(std::string_view message)
    {
        print(
            get_output_console(),
            std::cout,
            FOREGROUND_RED |
            FOREGROUND_GREEN |
            FOREGROUND_INTENSITY,
            "[ WARN ] ",
            message
        );
    }

    void error(std::string_view message)
    {
        print(
            get_error_console(),
            std::cerr,
            FOREGROUND_RED |
            FOREGROUND_INTENSITY,
            "[ERROR ] ",
            message
        );
    }

    void debug(std::string_view message)
    {
        print(
            get_output_console(),
            std::cout,
            FOREGROUND_RED |
            FOREGROUND_BLUE |
            FOREGROUND_INTENSITY,
            "[DEBUG ] ",
            message
        );
    }
}