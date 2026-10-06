#include "flow/terminal.hpp"

#include <windows.h>

namespace flow::terminal
{
    bool supports_color()
    {
        HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);

        if (output == INVALID_HANDLE_VALUE || output == nullptr)
            return false;

        DWORD mode = 0;

        if (!GetConsoleMode(output, &mode))
            return false;

        return true;
    }
}