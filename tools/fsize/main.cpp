#include "flow/core.hpp"
#include "flow/log.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>

#ifndef FSIZE_VERSION
#define FSIZE_VERSION "0.1.0"
#endif

namespace fs = std::filesystem;

namespace
{
    struct SizeInfo
    {
        std::uintmax_t bytes = 0;
        std::size_t files = 0;
        std::size_t directories = 0;
    };

    std::string path_to_string(
        const fs::path& path
    )
    {
        return path.string();
    }

    std::string format_size(
        std::uintmax_t bytes
    )
    {
        constexpr double units = 1024.0;

        const char* suffixes[] =
        {
            "B",
            "KB",
            "MB",
            "GB",
            "TB",
            "PB"
        };

        double size =
            static_cast<double>(bytes);

        std::size_t index = 0;

        while (
            size >= units &&
            index < 5
        )
        {
            size /= units;
            ++index;
        }

        if (index == 0)
        {
            return std::to_string(bytes) + " B";
        }

        return std::format(
            "{:.2f} {}",
            size,
            suffixes[index]
        );
    }

    bool calculate_directory_size(
        const fs::path& root,
        SizeInfo& result
    )
    {
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
                "Unable to scan directory: {}",
                ec.message()
            );

            return false;
        }

        for (
            ;
            iterator != end;
            iterator.increment(ec)
        )
        {
            if (ec)
            {
                flow::log::warning(
                    "Unable to access an entry: {}",
                    ec.message()
                );

                ec.clear();
                continue;
            }

            const fs::directory_entry& entry =
                *iterator;

            std::error_code type_error;

            if (entry.is_directory(type_error))
            {
                if (!type_error)
                    ++result.directories;

                continue;
            }

            if (entry.is_regular_file(type_error))
            {
                if (type_error)
                    continue;

                std::error_code size_error;

                const std::uintmax_t size =
                    entry.file_size(size_error);

                if (size_error)
                {
                    flow::log::warning(
                        "Unable to read size of: {}",
                        path_to_string(entry.path())
                    );

                    continue;
                }

                result.bytes += size;
                ++result.files;
            }
        }

        return true;
    }

    bool calculate_size(
        const fs::path& path,
        SizeInfo& result,
        bool& is_directory
    )
    {
        std::error_code ec;

        const fs::file_status status =
            fs::status(path, ec);

        if (ec)
        {
            flow::log::error(
                "Unable to access '{}': {}",
                path_to_string(path),
                ec.message()
            );

            return false;
        }

        if (fs::is_regular_file(status))
        {
            std::error_code size_error;

            result.bytes =
                fs::file_size(
                    path,
                    size_error
                );

            if (size_error)
            {
                flow::log::error(
                    "Unable to read file size: {}",
                    size_error.message()
                );

                return false;
            }

            result.files = 1;
            is_directory = false;

            return true;
        }

        if (fs::is_directory(status))
        {
            is_directory = true;
            ++result.directories;

            return calculate_directory_size(
                path,
                result
            );
        }

        flow::log::error(
            "Path is not a regular file or directory: {}",
            path_to_string(path)
        );

        return false;
    }

    void show_help()
    {
        flow::log::info(
            "fsize - File and directory size utility"
        );

        flow::log::info("");

        flow::log::info(
            "Usage:"
        );

        flow::log::info(
            "  fsize <path>"
        );

        flow::log::info(
            "      Calculate the size of a file or directory"
        );

        flow::log::info(
            "  fsize <path> --human"
        );

        flow::log::info(
            "      Show the size using readable units"
        );

        flow::log::info("");

        flow::log::info(
            "Options:"
        );

        flow::log::info(
            "  --human"
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
            "  fsize file.txt"
        );

        flow::log::info(
            "  fsize C:\\Projects\\FlowTools"
        );

        flow::log::info(
            "  fsize . --human"
        );
    }

    void show_version()
    {
        flow::log::info(
            "fsize {}",
            FSIZE_VERSION
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

    int run(
        const fs::path& path,
        bool human
    )
    {
        SizeInfo info;
        bool is_directory = false;

        flow::log::info(
            "Calculating size..."
        );

        flow::log::info(
            "Path: {}",
            path_to_string(path)
        );

        if (!calculate_size(
                path,
                info,
                is_directory
            ))
        {
            return 1;
        }

        flow::log::info("");

        flow::log::info(
            "Type: {}",
            is_directory
                ? "directory"
                : "file"
        );

        if (is_directory)
        {
            flow::log::info(
                "Files: {}",
                info.files
            );

            flow::log::info(
                "Directories: {}",
                info.directories
            );
        }

        if (human)
        {
            flow::log::info(
                "Size: {}",
                format_size(info.bytes)
            );
        }
        else
        {
            flow::log::info(
                "Size: {} bytes",
                info.bytes
            );
        }

        flow::log::success(
            "Size calculated successfully"
        );

        return 0;
    }

    bool parse_arguments(
        int argc,
        char* argv[],
        fs::path& path,
        bool& human
    )
    {
        for (int i = 1; i < argc; ++i)
        {
            const std::string argument =
                argv[i];

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

            if (
                argument == "--human" ||
                argument == "-H"
            )
            {
                human = true;
                continue;
            }

            if (!path.empty())
            {
                flow::log::error(
                    "Unexpected argument: {}",
                    argument
                );

                return false;
            }

            path = argument;
        }

        if (path.empty())
        {
            flow::log::error(
                "Missing path"
            );

            flow::log::info(
                "Use 'fsize --help' to see available options"
            );

            return false;
        }

        return true;
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

    fs::path path;
    bool human = false;

    if (!parse_arguments(
            argc,
            argv,
            path,
            human
        ))
    {
        const std::string first_argument =
            argc > 1
                ? argv[1]
                : "";

        if (
            first_argument == "--help" ||
            first_argument == "-h" ||
            first_argument == "--version" ||
            first_argument == "-v"
        )
        {
            return 0;
        }

        return 1;
    }

    return run(
        path,
        human
    );
}