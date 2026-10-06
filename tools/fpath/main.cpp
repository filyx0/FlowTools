#include <flow/core.hpp>
#include <flow/log.hpp>

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <windows.h>

#ifndef FPATH_VERSION
    #define FPATH_VERSION "0.1.0"
#endif

namespace
{
    constexpr std::string_view FLOWTOOLS_VERSION = "0.1.0";

    struct FindResult
    {
        bool found = false;
        std::string path;
        std::size_t path_index = 0;
    };

    struct PathLocation
    {
        std::vector<std::size_t> system_indices;
        std::vector<std::size_t> user_indices;
    };

    struct CleanResult
    {
        std::vector<std::wstring> cleaned_entries;

        std::size_t removed_empty = 0;
        std::size_t removed_missing = 0;
        std::size_t removed_duplicates = 0;
    };

    std::wstring utf8_to_wide(std::string_view text)
    {
        if (text.empty())
            return {};

        const int size = MultiByteToWideChar(
            CP_UTF8,
            0,
            text.data(),
            static_cast<int>(text.size()),
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
            0,
            text.data(),
            static_cast<int>(text.size()),
            result.data(),
            size
        );

        return result;
    }

    std::string wide_to_utf8(std::wstring_view text)
    {
        if (text.empty())
            return {};

        const int size = WideCharToMultiByte(
            CP_UTF8,
            0,
            text.data(),
            static_cast<int>(text.size()),
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
            text.data(),
            static_cast<int>(text.size()),
            result.data(),
            size,
            nullptr,
            nullptr
        );

        return result;
    }

    std::vector<std::wstring> split_path(
        std::wstring_view path
    )
    {
        std::vector<std::wstring> entries;

        std::size_t start = 0;

        while (start <= path.size())
        {
            const std::size_t end =
                path.find(L';', start);

            if (end == std::wstring_view::npos)
            {
                entries.emplace_back(
                    path.substr(start)
                );

                break;
            }

            entries.emplace_back(
                path.substr(
                    start,
                    end - start
                )
            );

            start = end + 1;
        }

        return entries;
    }

    std::wstring join_path(
        const std::vector<std::wstring>& entries
    )
    {
        std::wstring result;

        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            if (i > 0)
                result += L';';

            result += entries[i];
        }

        return result;
    }

    std::wstring normalize_path(
        std::wstring_view path
    )
    {
        if (path.empty())
            return {};

        std::wstring value(path);

        for (auto& character : value)
        {
            if (character == L'/')
                character = L'\\';
        }

        while (
            value.size() > 3 &&
            !value.empty() &&
            value.back() == L'\\'
        )
        {
            value.pop_back();
        }

        for (auto& character : value)
        {
            if (
                character >= L'A' &&
                character <= L'Z'
            )
            {
                character =
                    static_cast<wchar_t>(
                        character - L'A' + L'a'
                    );
            }
        }

        return value;
    }

    bool file_exists(
        const std::wstring& path
    )
    {
        const DWORD attributes =
            GetFileAttributesW(
                path.c_str()
            );

        return attributes !=
               INVALID_FILE_ATTRIBUTES;
    }

    bool has_extension(
        std::wstring_view name
    )
    {
        return name.find_last_of(L'.') !=
               std::wstring_view::npos;
    }

    bool get_process_path(
        std::wstring& path
    )
    {
        DWORD size = 32768;

        std::vector<wchar_t> buffer(size);

        const DWORD length =
            GetEnvironmentVariableW(
                L"PATH",
                buffer.data(),
                size
            );

        if (length == 0)
            return false;

        if (length >= size)
            return false;

        path.assign(
            buffer.data(),
            length
        );

        return true;
    }

    bool get_user_path(
        std::wstring& path
    )
    {
        HKEY key = nullptr;

        const LONG open_result =
            RegOpenKeyExW(
                HKEY_CURRENT_USER,
                L"Environment",
                0,
                KEY_QUERY_VALUE,
                &key
            );

        if (open_result != ERROR_SUCCESS)
            return false;

        DWORD type = 0;
        DWORD size = 0;

        LONG query_result =
            RegQueryValueExW(
                key,
                L"Path",
                nullptr,
                &type,
                nullptr,
                &size
            );

        if (
            query_result != ERROR_SUCCESS ||
            (type != REG_SZ &&
             type != REG_EXPAND_SZ)
        )
        {
            RegCloseKey(key);
            return false;
        }

        std::vector<wchar_t> buffer(
            (size / sizeof(wchar_t)) + 1,
            L'\0'
        );

        query_result =
            RegQueryValueExW(
                key,
                L"Path",
                nullptr,
                &type,
                reinterpret_cast<LPBYTE>(
                    buffer.data()
                ),
                &size
            );

        RegCloseKey(key);

        if (query_result != ERROR_SUCCESS)
            return false;

        path.assign(
            buffer.data()
        );

        return true;
    }

    bool get_system_path(
        std::wstring& path
    )
    {
        HKEY key = nullptr;

        const LONG open_result =
            RegOpenKeyExW(
                HKEY_LOCAL_MACHINE,
                L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment",
                0,
                KEY_QUERY_VALUE,
                &key
            );

        if (open_result != ERROR_SUCCESS)
            return false;

        DWORD type = 0;
        DWORD size = 0;

        LONG query_result =
            RegQueryValueExW(
                key,
                L"Path",
                nullptr,
                &type,
                nullptr,
                &size
            );

        if (
            query_result != ERROR_SUCCESS ||
            (type != REG_SZ &&
             type != REG_EXPAND_SZ)
        )
        {
            RegCloseKey(key);
            return false;
        }

        std::vector<wchar_t> buffer(
            (size / sizeof(wchar_t)) + 1,
            L'\0'
        );

        query_result =
            RegQueryValueExW(
                key,
                L"Path",
                nullptr,
                &type,
                reinterpret_cast<LPBYTE>(
                    buffer.data()
                ),
                &size
            );

        RegCloseKey(key);

        if (query_result != ERROR_SUCCESS)
            return false;

        path.assign(
            buffer.data()
        );

        return true;
    }

    bool set_user_path(
        const std::wstring& path
    )
    {
        HKEY key = nullptr;

        const LONG open_result =
            RegOpenKeyExW(
                HKEY_CURRENT_USER,
                L"Environment",
                0,
                KEY_SET_VALUE,
                &key
            );

        if (open_result != ERROR_SUCCESS)
            return false;

        const DWORD size =
            static_cast<DWORD>(
                (path.size() + 1) *
                sizeof(wchar_t)
            );

        const LONG set_result =
            RegSetValueExW(
                key,
                L"Path",
                0,
                REG_EXPAND_SZ,
                reinterpret_cast<const BYTE*>(
                    path.c_str()
                ),
                size
            );

        RegCloseKey(key);

        if (set_result != ERROR_SUCCESS)
            return false;

        SendMessageTimeoutW(
            HWND_BROADCAST,
            WM_SETTINGCHANGE,
            0,
            reinterpret_cast<LPARAM>(
                L"Environment"
            ),
            SMTO_ABORTIFHUNG,
            5000,
            nullptr
        );

        return true;
    }

    void show_named_path(
        std::string_view name,
        const std::vector<std::wstring>& entries
    )
    {
        flow::log::info(
            "{} PATH contains {} entries",
            name,
            entries.size()
        );

        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            flow::log::info(
                "[{}] {}",
                i + 1,
                wide_to_utf8(entries[i])
            );
        }

        flow::log::success(
            "{} PATH scan completed",
            name
        );
    }

    void show_path(
        const std::vector<std::wstring>& entries
    )
    {
        flow::log::info(
            "PATH contains {} entries",
            entries.size()
        );

        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            flow::log::info(
                "[{}] {}",
                i + 1,
                wide_to_utf8(entries[i])
            );
        }

        flow::log::success(
            "PATH scan completed"
        );
    }

    FindResult find_executable(
        std::string_view name,
        const std::vector<std::wstring>& entries
    )
    {
        FindResult result;

        const std::wstring program =
            utf8_to_wide(name);

        std::vector<std::wstring> candidates;

        if (has_extension(program))
        {
            candidates.push_back(program);
        }
        else
        {
            candidates.push_back(
                program + L".exe"
            );

            candidates.push_back(
                program + L".com"
            );

            candidates.push_back(
                program + L".cmd"
            );

            candidates.push_back(
                program + L".bat"
            );
        }

        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            const auto& entry = entries[i];

            if (entry.empty())
                continue;

            for (const auto& candidate_name : candidates)
            {
                const std::wstring candidate =
                    entry +
                    L"\\" +
                    candidate_name;

                if (file_exists(candidate))
                {
                    result.found = true;

                    result.path =
                        wide_to_utf8(candidate);

                    result.path_index =
                        i + 1;

                    return result;
                }
            }
        }

        return result;
    }

    void check_scope(
        std::string_view scope_name,
        const std::vector<std::wstring>& entries,
        std::size_t& missing,
        std::size_t& duplicates,
        std::size_t& empty
    )
    {
        std::unordered_set<std::wstring> seen;

        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            const auto& entry = entries[i];

            if (entry.empty())
            {
                flow::log::warning(
                    "Empty PATH entry"
                );

                flow::log::info(
                    "        source: {} #{}",
                    scope_name,
                    i + 1
                );

                ++empty;
                continue;
            }

            const std::wstring normalized =
                normalize_path(entry);

            if (!seen.insert(normalized).second)
            {
                flow::log::warning(
                    "Duplicate within {} PATH",
                    scope_name
                );

                flow::log::info(
                    "        {}",
                    wide_to_utf8(entry)
                );

                flow::log::info(
                    "        source: {} #{}",
                    scope_name,
                    i + 1
                );

                ++duplicates;
            }

            const DWORD attributes =
                GetFileAttributesW(
                    entry.c_str()
                );

            if (
                attributes ==
                INVALID_FILE_ATTRIBUTES
            )
            {
                flow::log::warning(
                    "Missing PATH entry"
                );

                flow::log::info(
                    "        {}",
                    wide_to_utf8(entry)
                );

                flow::log::info(
                    "        source: {} #{}",
                    scope_name,
                    i + 1
                );

                ++missing;
            }
        }
    }

    void check_path()
    {
        flow::log::info(
            "Checking PATH"
        );

        std::wstring system_path;
        std::wstring user_path;

        if (!get_system_path(system_path))
        {
            flow::log::error(
                "Unable to read system PATH"
            );

            return;
        }

        if (!get_user_path(user_path))
        {
            flow::log::error(
                "Unable to read user PATH"
            );

            return;
        }

        const auto system_entries =
            split_path(system_path);

        const auto user_entries =
            split_path(user_path);

        std::unordered_map<
            std::wstring,
            PathLocation
        > locations;

        for (std::size_t i = 0; i < system_entries.size(); ++i)
        {
            if (system_entries[i].empty())
                continue;

            locations[
                normalize_path(system_entries[i])
            ].system_indices.push_back(i + 1);
        }

        for (std::size_t i = 0; i < user_entries.size(); ++i)
        {
            if (user_entries[i].empty())
                continue;

            locations[
                normalize_path(user_entries[i])
            ].user_indices.push_back(i + 1);
        }

        std::size_t missing = 0;
        std::size_t internal_duplicates = 0;
        std::size_t cross_scope_duplicates = 0;
        std::size_t empty = 0;

        for (const auto& [normalized, location] : locations)
        {
            const bool has_system =
                !location.system_indices.empty();

            const bool has_user =
                !location.user_indices.empty();

            const std::size_t total =
                location.system_indices.size() +
                location.user_indices.size();

            if (total <= 1)
                continue;

            flow::log::warning(
                "Duplicate PATH entry"
            );

            flow::log::info(
                "        {}",
                wide_to_utf8(normalized)
            );

            if (has_system)
            {
                std::string indices;

                for (
                    std::size_t i = 0;
                    i < location.system_indices.size();
                    ++i
                )
                {
                    if (i > 0)
                        indices += ", ";

                    indices += "#";
                    indices += std::to_string(
                        location.system_indices[i]
                    );
                }

                flow::log::info(
                    "        system: {}",
                    indices
                );
            }

            if (has_user)
            {
                std::string indices;

                for (
                    std::size_t i = 0;
                    i < location.user_indices.size();
                    ++i
                )
                {
                    if (i > 0)
                        indices += ", ";

                    indices += "#";
                    indices += std::to_string(
                        location.user_indices[i]
                    );
                }

                flow::log::info(
                    "        user: {}",
                    indices
                );
            }

            if (has_system && has_user)
            {
                std::string type =
                    "system + user";

                for (
                    std::size_t i = 1;
                    i < location.user_indices.size();
                    ++i
                )
                {
                    type += " + user";
                }

                flow::log::info(
                    "        type: {}",
                    type
                );

                ++cross_scope_duplicates;
            }

            if (location.system_indices.size() > 1)
            {
                internal_duplicates +=
                    location.system_indices.size() - 1;
            }

            if (location.user_indices.size() > 1)
            {
                internal_duplicates +=
                    location.user_indices.size() - 1;
            }
        }

        check_scope(
            "system",
            system_entries,
            missing,
            internal_duplicates,
            empty
        );

        check_scope(
            "user",
            user_entries,
            missing,
            internal_duplicates,
            empty
        );

        flow::log::info(
            "Check completed: {} missing, {} cross-scope duplicates, {} internal duplicates, {} empty",
            missing,
            cross_scope_duplicates,
            internal_duplicates,
            empty
        );

        if (
            missing == 0 &&
            cross_scope_duplicates == 0 &&
            internal_duplicates == 0 &&
            empty == 0
        )
        {
            flow::log::success(
                "PATH looks clean"
            );
        }
        else
        {
            flow::log::warning(
                "PATH contains problems"
            );
        }
    }

    bool add_to_user_path(
        std::string_view directory
    )
    {
        std::wstring path;

        if (!get_user_path(path))
        {
            flow::log::error(
                "Unable to read user PATH"
            );

            return false;
        }

        const std::wstring new_entry =
            utf8_to_wide(directory);

        const std::wstring normalized_new =
            normalize_path(new_entry);

        const auto entries =
            split_path(path);

        for (const auto& entry : entries)
        {
            if (
                normalize_path(entry) ==
                normalized_new
            )
            {
                flow::log::warning(
                    "PATH entry already exists: {}",
                    directory
                );

                return true;
            }
        }

        if (
            !path.empty() &&
            path.back() != L';'
        )
        {
            path += L';';
        }

        path += new_entry;

        return set_user_path(path);
    }

    bool remove_from_user_path(
        std::string_view directory
    )
    {
        std::wstring path;

        if (!get_user_path(path))
        {
            flow::log::error(
                "Unable to read user PATH"
            );

            return false;
        }

        const std::wstring target =
            utf8_to_wide(directory);

        const std::wstring normalized_target =
            normalize_path(target);

        const auto entries =
            split_path(path);

        bool removed = false;

        std::vector<std::wstring> filtered;

        for (const auto& entry : entries)
        {
            if (
                normalize_path(entry) ==
                normalized_target
            )
            {
                removed = true;
                continue;
            }

            filtered.push_back(entry);
        }

        if (!removed)
        {
            flow::log::warning(
                "PATH entry not found: {}",
                directory
            );

            return true;
        }

        const std::wstring new_path =
            join_path(filtered);

        return set_user_path(new_path);
    }

    CleanResult build_cleaned_user_path(
        const std::vector<std::wstring>& entries
    )
    {
        CleanResult result;

        std::unordered_set<std::wstring> seen;

        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            const auto& entry = entries[i];

            if (entry.empty())
            {
                ++result.removed_empty;

                flow::log::warning(
                    "Would remove empty entry: user #{}",
                    i + 1
                );

                continue;
            }

            const std::wstring normalized =
                normalize_path(entry);

            if (!seen.insert(normalized).second)
            {
                ++result.removed_duplicates;

                flow::log::warning(
                    "Would remove duplicate: user #{}",
                    i + 1
                );

                flow::log::info(
                    "        {}",
                    wide_to_utf8(entry)
                );

                continue;
            }

            const DWORD attributes =
                GetFileAttributesW(
                    entry.c_str()
                );

            if (
                attributes ==
                INVALID_FILE_ATTRIBUTES
            )
            {
                ++result.removed_missing;

                flow::log::warning(
                    "Would remove missing entry: user #{}",
                    i + 1
                );

                flow::log::info(
                    "        {}",
                    wide_to_utf8(entry)
                );

                continue;
            }

            result.cleaned_entries.push_back(entry);
        }

        return result;
    }

    bool clean_user_path(
        bool apply
    )
    {
        std::wstring user_path;

        if (!get_user_path(user_path))
        {
            flow::log::error(
                "Unable to read user PATH"
            );

            return false;
        }

        const auto entries =
            split_path(user_path);

        flow::log::info(
            "Analyzing user PATH"
        );

        const CleanResult result =
            build_cleaned_user_path(entries);

        const std::size_t total_removed =
            result.removed_empty +
            result.removed_missing +
            result.removed_duplicates;

        if (total_removed == 0)
        {
            flow::log::success(
                "User PATH does not need cleaning"
            );

            return true;
        }

        flow::log::info(
            "Clean plan: remove {} entries",
            total_removed
        );

        flow::log::info(
            "        empty: {}",
            result.removed_empty
        );

        flow::log::info(
            "        missing: {}",
            result.removed_missing
        );

        flow::log::info(
            "        duplicates: {}",
            result.removed_duplicates
        );

        if (!apply)
        {
            flow::log::warning(
                "Dry run only - no changes made"
            );

            flow::log::info(
                "Use: fpath clean --apply"
            );

            return true;
        }

        const std::wstring cleaned_path =
            join_path(
                result.cleaned_entries
            );

        if (!set_user_path(cleaned_path))
        {
            flow::log::error(
                "Unable to update user PATH"
            );

            return false;
        }

        flow::log::success(
            "User PATH cleaned"
        );

        flow::log::info(
            "Removed {} entries",
            total_removed
        );

        flow::log::warning(
            "Existing terminals may still have the old PATH"
        );

        return true;
    }

    void show_version()
    {
        flow::log::info(
            "fpath {}",
            FPATH_VERSION
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

    void show_help()
    {
        flow::log::info(
            "fpath - Windows PATH diagnostic and management tool"
        );

        flow::log::info("");

        flow::log::info(
            "Usage:"
        );

        flow::log::info(
            "  fpath"
        );

        flow::log::info(
            "      Show the current process PATH"
        );

        flow::log::info(
            "  fpath user"
        );

        flow::log::info(
            "      Show the User PATH"
        );

        flow::log::info(
            "  fpath system"
        );

        flow::log::info(
            "      Show the System PATH"
        );

        flow::log::info(
            "  fpath check"
        );

        flow::log::info(
            "      Diagnose PATH problems"
        );

        flow::log::info(
            "  fpath find <program>"
        );

        flow::log::info(
            "      Find an executable in PATH"
        );

        flow::log::info(
            "  fpath add <directory>"
        );

        flow::log::info(
            "      Add a directory to User PATH"
        );

        flow::log::info(
            "  fpath remove <directory>"
        );

        flow::log::info(
            "      Remove a directory from User PATH"
        );

        flow::log::info(
            "  fpath clean"
        );

        flow::log::info(
            "      Preview safe User PATH cleanup"
        );

        flow::log::info(
            "  fpath clean --apply"
        );

        flow::log::info(
            "      Apply safe User PATH cleanup"
        );

        flow::log::info(
            "  fpath version"
        );

        flow::log::info(
            "      Show version information"
        );

        flow::log::info(
            "  fpath --version"
        );

        flow::log::info(
            "      Show version information"
        );

        flow::log::info(
            "  fpath --help"
        );

        flow::log::info(
            "      Show this help"
        );

        flow::log::info("");

        flow::log::info(
            "Notes:"
        );

        flow::log::info(
            "  clean never modifies the System PATH"
        );

        flow::log::info(
            "  clean does not remove System + User duplicates"
        );

        flow::log::info(
            "  clean removes empty, missing and internal User entries"
        );
    }
}

int main(
    int argc,
    char* argv[]
)
{
    flow::initialize();

    if (argc >= 2)
    {
        const std::string_view command =
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
            command == "version" ||
            command == "--version" ||
            command == "-v"
        )
        {
            show_version();
            return 0;
        }
    }

    std::wstring environment_path;

    if (!get_process_path(environment_path))
    {
        flow::log::error(
            "Unable to read PATH"
        );

        return 1;
    }

    const auto entries =
        split_path(environment_path);

    if (argc == 1)
    {
        show_path(entries);
        return 0;
    }

    const std::string_view command =
        argv[1];

    if (command == "user")
    {
        std::wstring user_path;

        if (!get_user_path(user_path))
        {
            flow::log::error(
                "Unable to read user PATH"
            );

            return 1;
        }

        const auto user_entries =
            split_path(user_path);

        show_named_path(
            "User",
            user_entries
        );

        return 0;
    }

    if (command == "system")
    {
        std::wstring system_path;

        if (!get_system_path(system_path))
        {
            flow::log::error(
                "Unable to read system PATH"
            );

            return 1;
        }

        const auto system_entries =
            split_path(system_path);

        show_named_path(
            "System",
            system_entries
        );

        return 0;
    }

    if (command == "check")
    {
        check_path();
        return 0;
    }

    if (command == "find")
    {
        if (argc < 3)
        {
            flow::log::error(
                "Missing executable name"
            );

            flow::log::info(
                "Usage: fpath find <program>"
            );

            return 1;
        }

        const std::string_view program =
            argv[2];

        flow::log::info(
            "Searching for: {}",
            program
        );

        const FindResult result =
            find_executable(
                program,
                entries
            );

        if (result.found)
        {
            flow::log::success(
                "Found: {}",
                result.path
            );

            flow::log::info(
                "PATH entry: {}",
                result.path_index
            );

            return 0;
        }

        flow::log::error(
            "Executable not found: {}",
            program
        );

        return 1;
    }

    if (command == "add")
    {
        if (argc < 3)
        {
            flow::log::error(
                "Missing directory"
            );

            flow::log::info(
                "Usage: fpath add <directory>"
            );

            return 1;
        }

        const std::string_view directory =
            argv[2];

        flow::log::info(
            "Adding PATH entry: {}",
            directory
        );

        if (add_to_user_path(directory))
        {
            flow::log::success(
                "PATH entry added"
            );

            return 0;
        }

        flow::log::error(
            "Unable to modify user PATH"
        );

        return 1;
    }

    if (command == "remove")
    {
        if (argc < 3)
        {
            flow::log::error(
                "Missing directory"
            );

            flow::log::info(
                "Usage: fpath remove <directory>"
            );

            return 1;
        }

        const std::string_view directory =
            argv[2];

        flow::log::info(
            "Removing PATH entry: {}",
            directory
        );

        if (remove_from_user_path(directory))
        {
            flow::log::success(
                "PATH removal completed"
            );

            return 0;
        }

        flow::log::error(
            "Unable to modify user PATH"
        );

        return 1;
    }

    if (command == "clean")
    {
        bool apply = false;

        if (argc >= 3)
        {
            const std::string_view option =
                argv[2];

            if (option == "--apply")
            {
                apply = true;
            }
            else if (
                option == "--help" ||
                option == "-h"
            )
            {
                flow::log::info(
                    "Usage: fpath clean [--apply]"
                );

                flow::log::info(
                    "Without --apply, clean performs a dry run."
                );

                flow::log::info(
                    "With --apply, safe User PATH changes are written."
                );

                return 0;
            }
            else
            {
                flow::log::error(
                    "Unknown clean option: {}",
                    option
                );

                flow::log::info(
                    "Usage: fpath clean [--apply]"
                );

                return 1;
            }
        }

        if (clean_user_path(apply))
            return 0;

        return 1;
    }

    flow::log::error(
        "Unknown command: {}",
        command
    );

    flow::log::info(
        "Use 'fpath --help' to see available commands"
    );

    return 1;
}