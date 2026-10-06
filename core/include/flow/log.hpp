#pragma once

#include <format>
#include <string>
#include <string_view>
#include <utility>

#ifdef FLOW_CORE_BUILD
    #define FLOW_API __declspec(dllexport)
#else
    #define FLOW_API __declspec(dllimport)
#endif

namespace flow::log
{
    FLOW_API void info(std::string_view message);
    FLOW_API void success(std::string_view message);
    FLOW_API void warning(std::string_view message);
    FLOW_API void error(std::string_view message);
    FLOW_API void debug(std::string_view message);

    template <typename... Args>
    void info(std::string_view format, Args&&... args)
    {
        info(std::vformat(format, std::make_format_args(args...)));
    }

    template <typename... Args>
    void success(std::string_view format, Args&&... args)
    {
        success(std::vformat(format, std::make_format_args(args...)));
    }

    template <typename... Args>
    void warning(std::string_view format, Args&&... args)
    {
        warning(std::vformat(format, std::make_format_args(args...)));
    }

    template <typename... Args>
    void error(std::string_view format, Args&&... args)
    {
        error(std::vformat(format, std::make_format_args(args...)));
    }

    template <typename... Args>
    void debug(std::string_view format, Args&&... args)
    {
        debug(std::vformat(format, std::make_format_args(args...)));
    }
}