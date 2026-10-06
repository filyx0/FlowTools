#pragma once

#include <string_view>

#ifdef FLOW_CORE_BUILD
    #define FLOW_API __declspec(dllexport)
#else
    #define FLOW_API __declspec(dllimport)
#endif

namespace flow
{
    FLOW_API void initialize();

    namespace core
    {
        FLOW_API std::string_view version();
    }
}