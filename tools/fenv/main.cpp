#include "flow/core.hpp"
#include "flow/log.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include <windows.h>

#ifndef FENV_VERSION
#define FENV_VERSION "0.1.0"
#endif

namespace
{
    struct EnvironmentVariable
    {
        std::string name;
        std::string value;
    };

    std::string wide_to_utf8(
        const std::wstring& value
    )
    {
        if (value.empty())
            return {};

        const int size =
            WideCharToMultiByte(
                CP_UTF8,
                0,
                value.data(),
                static_cast<int>(value.size()),
                nullptr,
                0,
                nullptr,
                nullptr
            );

        if (size <= 0)
            return {};

        std::string result(
            size,
            '\0'
        );

        WideCharToMultiByte(
            CP_UTF8,
            0,
            value.data(),
            static_cast<int>(value.size()),
            result.data(),
            size,
            nullptr,
            nullptr
        );

        return result;
    }

    std::wstring utf8_to_wide(
        const std::string& value
    )
    {
        if (value.empty())
            return {};

        const int size =
            MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                value.data(),
                static_cast<int>(value.size()),
                nullptr,
                0
            );

        if (size <= 0)
            return {};

        std::wstring result(
            size,
            L'\0'
        );

        MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            result.data(),
            size
        );

        return result;
    }

    std::string to_lower(
        std::string value
    )
    {
        std::transform(
            value.begin(),
            value.end(),
            value.begin(),
            [](unsigned char character)
            {
                return static_cast<char>(
                    std::tolower(character)
                );
            }
        );

        return value;
    }

    std::vector<EnvironmentVariable>
    get_process_environment()
    {
        std::vector<EnvironmentVariable> variables;

        LPWCH environment =
            GetEnvironmentStringsW();

        if (!environment)
        {
            flow::log::error(
                "Unable to read process environment"
            );

            return variables;
        }

        for (
            LPWCH entry = environment;
            *entry != L'\0';
            entry += wcslen(entry) + 1
        )
        {
            const std::wstring value(entry);

            const std::size_t separator =
                value.find(L'=');

            if (
                separator == std::wstring::npos ||
                separator == 0
            )
            {
                continue;
            }

            EnvironmentVariable variable;

            variable.name =
                wide_to_utf8(
                    value.substr(
                        0,
                        separator
                    )
                );

            variable.value =
                wide_to_utf8(
                    value.substr(
                        separator + 1
                    )
                );

            variables.push_back(
                std::move(variable)
            );
        }

        FreeEnvironmentStringsW(
            environment
        );

        std::sort(
            variables.begin(),
            variables.end(),
            [](const EnvironmentVariable& a,
               const EnvironmentVariable& b)
            {
                return to_lower(a.name) <
                       to_lower(b.name);
            }
        );

        return variables;
    }

    bool read_registry_environment(
        HKEY root,
        const wchar_t* subkey,
        std::vector<EnvironmentVariable>& variables
    )
    {
        HKEY key = nullptr;

        const LONG open_result =
            RegOpenKeyExW(
                root,
                subkey,
                0,
                KEY_READ,
                &key
            );

        if (open_result != ERROR_SUCCESS)
        {
            flow::log::error(
                "Unable to open environment registry key"
            );

            return false;
        }

        DWORD value_count = 0;
        DWORD max_name_length = 0;
        DWORD max_value_length = 0;

        LONG result =
            RegQueryInfoKeyW(
                key,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                &value_count,
                &max_name_length,
                &max_value_length,
                nullptr,
                nullptr
            );

        if (result != ERROR_SUCCESS)
        {
            RegCloseKey(key);

            flow::log::error(
                "Unable to query environment registry key"
            );

            return false;
        }

        std::vector<wchar_t> name_buffer(
            max_name_length + 1
        );

        std::vector<BYTE> data_buffer(
            max_value_length + sizeof(wchar_t)
        );

        for (DWORD index = 0;
             index < value_count;
             ++index)
        {
            DWORD name_size =
                max_name_length + 1;

            DWORD data_size =
                static_cast<DWORD>(
                    data_buffer.size()
                );

            DWORD type = 0;

            result =
                RegEnumValueW(
                    key,
                    index,
                    name_buffer.data(),
                    &name_size,
                    nullptr,
                    &type,
                    data_buffer.data(),
                    &data_size
                );

            if (result != ERROR_SUCCESS)
                continue;

            if (
                type != REG_SZ &&
                type != REG_EXPAND_SZ
            )
            {
                continue;
            }

            std::wstring name(
                name_buffer.data(),
                name_size
            );

            std::wstring value;

            if (data_size >= sizeof(wchar_t))
            {
                const wchar_t* value_data =
                    reinterpret_cast<const wchar_t*>(
                        data_buffer.data()
                    );

                const std::size_t character_count =
                    data_size / sizeof(wchar_t);

                value.assign(
                    value_data,
                    character_count
                );

                while (
                    !value.empty() &&
                    value.back() == L'\0'
                )
                {
                    value.pop_back();
                }
            }

            if (type == REG_EXPAND_SZ)
            {
                std::vector<wchar_t> expanded(
                    32768
                );

                const DWORD expanded_size =
                    ExpandEnvironmentStringsW(
                        value.c_str(),
                        expanded.data(),
                        static_cast<DWORD>(
                            expanded.size()
                        )
                    );

                if (
                    expanded_size > 0 &&
                    expanded_size <= expanded.size()
                )
                {
                    value.assign(
                        expanded.data(),
                        expanded_size - 1
                    );
                }
            }

            EnvironmentVariable variable;

            variable.name =
                wide_to_utf8(name);

            variable.value =
                wide_to_utf8(value);

            variables.push_back(
                std::move(variable)
            );
        }

        RegCloseKey(key);

        std::sort(
            variables.begin(),
            variables.end(),
            [](const EnvironmentVariable& a,
               const EnvironmentVariable& b)
            {
                return to_lower(a.name) <
                       to_lower(b.name);
            }
        );

        return true;
    }

    std::vector<EnvironmentVariable>
    get_user_environment()
    {
        std::vector<EnvironmentVariable> variables;

        read_registry_environment(
            HKEY_CURRENT_USER,
            L"Environment",
            variables
        );

        return variables;
    }

    std::vector<EnvironmentVariable>
    get_system_environment()
    {
        std::vector<EnvironmentVariable> variables;

        read_registry_environment(
            HKEY_LOCAL_MACHINE,
            L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment",
            variables
        );

        return variables;
    }

    void show_help()
    {
        flow::log::info(
            "fenv - Windows environment variable utility"
        );

        flow::log::info("");

        flow::log::info(
            "Usage:"
        );

        flow::log::info(
            "  fenv"
        );

        flow::log::info(
            "      Show the current process environment"
        );

        flow::log::info(
            "  fenv get <name>"
        );

        flow::log::info(
            "      Show a specific environment variable"
        );

        flow::log::info(
            "  fenv find <term>"
        );

        flow::log::info(
            "      Find variables whose names contain the term"
        );

        flow::log::info(
            "  fenv --user"
        );

        flow::log::info(
            "      Show User environment variables"
        );

        flow::log::info(
            "  fenv --system"
        );

        flow::log::info(
            "      Show System environment variables"
        );

        flow::log::info("");

        flow::log::info(
            "Options:"
        );

        flow::log::info(
            "  --user"
        );

        flow::log::info(
            "  --system"
        );

        flow::log::info(
            "  --help"
        );

        flow::log::info(
            "  --version"
        );

        flow::log::info("");

        flow::log::info(
            "Examples:"
        );

        flow::log::info(
            "  fenv"
        );

        flow::log::info(
            "  fenv get PATH"
        );

        flow::log::info(
            "  fenv find JAVA"
        );

        flow::log::info(
            "  fenv --user"
        );

        flow::log::info(
            "  fenv --system"
        );
    }

    void show_version()
    {
        flow::log::info(
            "fenv {}",
            FENV_VERSION
        );

        flow::log::info(
            "FlowTools Core {}",
            flow::core::version()
        );

#if defined(_WIN64)
        flow::log::info(
            "Windows x64"
        );
#elif defined(_WIN32)
        flow::log::info(
            "Windows x86"
        );
#else
        flow::log::info(
            "Windows"
        );
#endif

        flow::log::success(
            "Version information displayed"
        );
    }

    void print_variables(
        const std::vector<EnvironmentVariable>& variables
    )
    {
        if (variables.empty())
        {
            flow::log::warning(
                "No environment variables found"
            );

            return;
        }

        for (const auto& variable : variables)
        {
            flow::log::info(
                "{}={}",
                variable.name,
                variable.value
            );
        }

        flow::log::success(
            "Displayed {} environment variables",
            variables.size()
        );
    }

    int show_variable(
        const std::string& name
    )
    {
        const std::wstring wide_name =
            utf8_to_wide(name);

        if (wide_name.empty())
        {
            flow::log::error(
                "Invalid variable name"
            );

            return 1;
        }

        const DWORD required_size =
            GetEnvironmentVariableW(
                wide_name.c_str(),
                nullptr,
                0
            );

        if (required_size == 0)
        {
            if (
                GetLastError() ==
                ERROR_ENVVAR_NOT_FOUND
            )
            {
                flow::log::error(
                    "Environment variable '{}' was not found",
                    name
                );

                return 1;
            }

            flow::log::error(
                "Unable to read environment variable '{}'",
                name
            );

            return 1;
        }

        std::wstring value(
            required_size,
            L'\0'
        );

        const DWORD size =
            GetEnvironmentVariableW(
                wide_name.c_str(),
                value.data(),
                required_size
            );

        if (size == 0)
        {
            flow::log::error(
                "Unable to read environment variable '{}'",
                name
            );

            return 1;
        }

        value.resize(size);

        flow::log::info(
            "{}={}",
            name,
            wide_to_utf8(value)
        );

        return 0;
    }

    int find_variables(
        const std::string& term
    )
    {
        const auto variables =
            get_process_environment();

        if (variables.empty())
            return 1;

        const std::string search =
            to_lower(term);

        std::size_t matches = 0;

        flow::log::info(
            "Searching environment variables for: {}",
            term
        );

        flow::log::info("");

        for (const auto& variable : variables)
        {
            if (
                to_lower(variable.name).find(search) ==
                std::string::npos
            )
            {
                continue;
            }

            flow::log::success(
                "{}={}",
                variable.name,
                variable.value
            );

            ++matches;
        }

        flow::log::info("");

        if (matches == 0)
        {
            flow::log::warning(
                "No matching environment variables found"
            );

            return 1;
        }

        flow::log::success(
            "Found {} matching {}",
            matches,
            matches == 1
                ? "variable"
                : "variables"
        );

        return 0;
    }
}

int main(
    int argc,
    char* argv[]
)
{
    flow::initialize();

    if (argc <= 1)
    {
        print_variables(
            get_process_environment()
        );

        return 0;
    }

    const std::string command =
        argv[1];

    if (
        command == "--help" ||
        command == "-h" ||
        command == "help"
    )
    {
        show_help();
        return 0;
    }

    if (
        command == "--version" ||
        command == "-v" ||
        command == "version"
    )
    {
        show_version();
        return 0;
    }

    if (
        command == "--user" ||
        command == "user"
    )
    {
        flow::log::info(
            "User environment"
        );

        flow::log::info("");

        print_variables(
            get_user_environment()
        );

        return 0;
    }

    if (
        command == "--system" ||
        command == "system"
    )
    {
        flow::log::info(
            "System environment"
        );

        flow::log::info("");

        print_variables(
            get_system_environment()
        );

        return 0;
    }

    if (command == "get")
    {
        if (argc < 3)
        {
            flow::log::error(
                "Missing variable name"
            );

            flow::log::info(
                "Usage: fenv get <name>"
            );

            return 1;
        }

        return show_variable(
            argv[2]
        );
    }

    if (command == "find")
    {
        if (argc < 3)
        {
            flow::log::error(
                "Missing search term"
            );

            flow::log::info(
                "Usage: fenv find <term>"
            );

            return 1;
        }

        return find_variables(
            argv[2]
        );
    }

    flow::log::error(
        "Unknown command: {}",
        command
    );

    flow::log::info(
        "Use 'fenv --help' to see available commands"
    );

    return 1;
}