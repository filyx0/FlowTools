#pragma once

#include <string_view>

#ifdef FLOW_CORE_BUILD
    #define FLOW_API __declspec(dllexport)
#else
    #define FLOW_API __declspec(dllimport)
#endif

namespace flow::terminal
{
    FLOW_API bool supports_color();
}