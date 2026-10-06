#include "flow/core.hpp"
#include "flow/log.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>
#include <windows.h>

#ifndef FSEARCH_VERSION
#define FSEARCH_VERSION "0.1.0"
#endif

namespace fs = std::filesystem;

namespace
{
    struct SearchOptions
    {
        std::string term;

        bool ignore_case = false;
        bool files_only = false;
        bool dirs_only = false;

        std::string extension;
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

    std::string path_to_string(
        const fs::path& path
    )
    {
        return wide_to_utf8(
            path.wstring()
        );
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

    std::string normalize_extension(
        std::string extension
    )
    {
        if (extension.empty())
            return extension;

        if (extension.front() != '.')
            extension.insert(
                extension.begin(),
                '.'
            );

        return to_lower(
            std::move(extension)
        );
    }

    bool has_extension(
        const fs::path& path,
        const std::string& extension
    )
    {
        if (extension.empty())
            return true;

        const std::string actual =
            to_lower(
                path_to_string(
                    path.extension()
                )
            );

        return actual == extension;
    }

    void show_help()
    {
        flow::log::info(
            "fsearch - File and directory search tool"
        );

        flow::log::info("");

        flow::log::info(
            "Usage:"
        );

        flow::log::info(
            "  fsearch <term>"
        );

        flow::log::info(
            "      Search recursively from the current directory"
        );

        flow::log::info(
            "  fsearch <term> --ignore-case"
        );

        flow::log::info(
            "      Search without case sensitivity"
        );

        flow::log::info(
            "  fsearch <term> --files"
        );

        flow::log::info(
            "      Search files only"
        );

        flow::log::info(
            "  fsearch <term> --dirs"
        );

        flow::log::info(
            "      Search directories only"
        );

        flow::log::info(
            "  fsearch <term> --ext <extension>"
        );

        flow::log::info(
            "      Search only files with the specified extension"
        );

        flow::log::info("");

        flow::log::info(
            "Options:"
        );

        flow::log::info(
            "  --ignore-case"
        );

        flow::log::info(
            "  --files"
        );

        flow::log::info(
            "  --dirs"
        );

        flow::log::info(
            "  --ext <extension>"
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
            "  fsearch main.cpp"
        );

        flow::log::info(
            "  fsearch main --ignore-case"
        );

        flow::log::info(
            "  fsearch .cpp --files --ext cpp"
        );

        flow::log::info(
            "  fsearch src --dirs"
        );
    }

    void show_version()
    {
        flow::log::info(
            "fsearch {}",
            FSEARCH_VERSION
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

    bool parse_options(
        int argc,
        char* argv[],
        SearchOptions& options
    )
    {
        if (argc < 2)
            return false;

        for (int i = 1; i < argc; ++i)
        {
            const std::string argument =
                argv[i];

            if (
                argument == "--ignore-case" ||
                argument == "-i"
            )
            {
                options.ignore_case = true;
                continue;
            }

            if (
                argument == "--files" ||
                argument == "-f"
            )
            {
                if (options.dirs_only)
                {
                    flow::log::error(
                        "--files and --dirs cannot be used together"
                    );

                    return false;
                }

                options.files_only = true;
                continue;
            }

            if (
                argument == "--dirs" ||
                argument == "-d"
            )
            {
                if (options.files_only)
                {
                    flow::log::error(
                        "--files and --dirs cannot be used together"
                    );

                    return false;
                }

                options.dirs_only = true;
                continue;
            }

            if (
                argument == "--ext" ||
                argument == "-e"
            )
            {
                if (i + 1 >= argc)
                {
                    flow::log::error(
                        "Missing extension after {}",
                        argument
                    );

                    return false;
                }

                options.extension =
                    normalize_extension(
                        argv[++i]
                    );

                continue;
            }

            if (
                argument.rfind(
                    "--ext=",
                    0
                ) == 0
            )
            {
                options.extension =
                    normalize_extension(
                        argument.substr(6)
                    );

                if (options.extension.empty())
                {
                    flow::log::error(
                        "Extension cannot be empty"
                    );

                    return false;
                }

                continue;
            }

            if (
                argument == "--help" ||
                argument == "-h"
            )
            {
                show_help();
                return false;
            }

            if (
                argument == "--version" ||
                argument == "-v"
            )
            {
                show_version();
                return false;
            }

            if (!options.term.empty())
            {
                flow::log::error(
                    "Unexpected argument: {}",
                    argument
                );

                return false;
            }

            options.term = argument;
        }

        if (options.term.empty())
        {
            flow::log::error(
                "Missing search term"
            );

            flow::log::info(
                "Use 'fsearch --help' to see available options"
            );

            return false;
        }

        if (
            !options.extension.empty() &&
            options.dirs_only
        )
        {
            flow::log::error(
                "--ext can only be used when searching files"
            );

            return false;
        }

        return true;
    }

    bool matches_term(
        const std::string& name,
        const SearchOptions& options
    )
    {
        if (options.ignore_case)
        {
            const std::string lower_name =
                to_lower(name);

            const std::string lower_term =
                to_lower(options.term);

            return lower_name.find(
                lower_term
            ) != std::string::npos;
        }

        return name.find(
            options.term
        ) != std::string::npos;
    }

    bool matches_entry(
        const fs::directory_entry& entry,
        const SearchOptions& options
    )
    {
        std::error_code ec;

        const bool is_directory =
            entry.is_directory(ec);

        if (ec)
            return false;

        if (
            options.files_only &&
            is_directory
        )
        {
            return false;
        }

        if (
            options.dirs_only &&
            !is_directory
        )
        {
            return false;
        }

        if (
            !is_directory &&
            !has_extension(
                entry.path(),
                options.extension
            )
        )
        {
            return false;
        }

        const std::string name =
            path_to_string(
                entry.path().filename()
            );

        return matches_term(
            name,
            options
        );
    }

    void search(
        const SearchOptions& options
    )
    {
        const fs::path root =
            fs::current_path();

        flow::log::info(
            "Searching for: {}",
            options.term
        );

        flow::log::info(
            "Root: {}",
            path_to_string(root)
        );

        if (options.ignore_case)
        {
            flow::log::info(
                "Case sensitivity: disabled"
            );
        }

        if (options.files_only)
        {
            flow::log::info(
                "Type: files only"
            );
        }
        else if (options.dirs_only)
        {
            flow::log::info(
                "Type: directories only"
            );
        }
        else
        {
            flow::log::info(
                "Type: files and directories"
            );
        }

        if (!options.extension.empty())
        {
            flow::log::info(
                "Extension: {}",
                options.extension
            );
        }

        flow::log::info("");

        std::size_t matches = 0;

        std::error_code ec;

        fs::recursive_directory_iterator iterator(
            root,
            fs::directory_options::skip_permission_denied,
            ec
        );

        fs::recursive_directory_iterator end;

        if (ec)
        {
            flow::log::error(
                "Unable to start directory search: {}",
                ec.message()
            );

            return;
        }

        for (
            ;
            iterator != end;
            iterator.increment(ec)
        )
        {
            if (ec)
            {
                ec.clear();
                continue;
            }

            const fs::directory_entry& entry =
                *iterator;

            if (!matches_entry(
                    entry,
                    options
                ))
            {
                continue;
            }

            const std::string result =
                path_to_string(
                    entry.path()
                );

            flow::log::success(
                "{}",
                result
            );

            ++matches;
        }

        flow::log::info("");

        if (matches == 0)
        {
            flow::log::warning(
                "No matches found"
            );

            return;
        }

        flow::log::success(
            "Found {} {}",
            matches,
            matches == 1
                ? "match"
                : "matches"
        );
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
        show_help();
        return 0;
    }

    const std::string first_argument =
        argv[1];

    if (
        first_argument == "help" ||
        first_argument == "--help" ||
        first_argument == "-h"
    )
    {
        show_help();
        return 0;
    }

    if (
        first_argument == "version" ||
        first_argument == "--version" ||
        first_argument == "-v"
    )
    {
        show_version();
        return 0;
    }

    SearchOptions options;

    if (!parse_options(
            argc,
            argv,
            options
        ))
    {
        return 1;
    }

    search(options);

    return 0;
}