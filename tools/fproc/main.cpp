#include "flow/core.hpp"
#include "flow/log.hpp"

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cctype>
#include <iostream>
#include <string>
#include <vector>

#ifndef FPROC_VERSION
#define FPROC_VERSION "0.1.0"
#endif

namespace
{
    struct ProcessInfo
    {
        DWORD pid;
        DWORD parent_pid;
        std::string name;
    };

    struct KillOptions
    {
        DWORD pid = 0;
        bool dry_run = false;
        bool force = false;
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

    void show_help()
    {
        flow::log::info(
            "fproc - Windows process inspection and management tool"
        );

        flow::log::info("");

        flow::log::info(
            "Usage:"
        );

        flow::log::info(
            "  fproc"
        );

        flow::log::info(
            "      List running processes"
        );

        flow::log::info(
            "  fproc find <name>"
        );

        flow::log::info(
            "      Find processes by name"
        );

        flow::log::info(
            "  fproc info <pid>"
        );

        flow::log::info(
            "      Show detailed information about a process"
        );

        flow::log::info(
            "  fproc tree"
        );

        flow::log::info(
            "      Show the process hierarchy"
        );

        flow::log::info(
            "  fproc kill <pid>"
        );

        flow::log::info(
            "      Terminate a process after confirmation"
        );

        flow::log::info(
            "  fproc kill <pid> --dry-run"
        );

        flow::log::info(
            "      Show what would happen without terminating"
        );

        flow::log::info(
            "  fproc kill <pid> --force"
        );

        flow::log::info(
            "      Terminate without confirmation"
        );

        flow::log::info("");

        flow::log::info(
            "Options:"
        );

        flow::log::info(
            "  --dry-run"
        );

        flow::log::info(
            "      Do not terminate the process"
        );

        flow::log::info(
            "  --force"
        );

        flow::log::info(
            "      Skip confirmation before termination"
        );

        flow::log::info("");

        flow::log::info(
            "Other commands:"
        );

        flow::log::info(
            "  fproc --help"
        );

        flow::log::info(
            "  fproc --version"
        );
    }

    void show_version()
    {
        flow::log::info(
            "fproc {}",
            FPROC_VERSION
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

    std::vector<ProcessInfo> get_processes()
    {
        std::vector<ProcessInfo> processes;

        HANDLE snapshot =
            CreateToolhelp32Snapshot(
                TH32CS_SNAPPROCESS,
                0
            );

        if (snapshot == INVALID_HANDLE_VALUE)
        {
            flow::log::error(
                "Unable to create process snapshot"
            );

            return processes;
        }

        PROCESSENTRY32W entry{};
        entry.dwSize =
            sizeof(PROCESSENTRY32W);

        if (!Process32FirstW(
                snapshot,
                &entry
            ))
        {
            CloseHandle(snapshot);

            flow::log::error(
                "Unable to enumerate processes"
            );

            return processes;
        }

        do
        {
            ProcessInfo process;

            process.pid =
                entry.th32ProcessID;

            process.parent_pid =
                entry.th32ParentProcessID;

            process.name =
                wide_to_utf8(
                    entry.szExeFile
                );

            processes.push_back(
                std::move(process)
            );

        } while (
            Process32NextW(
                snapshot,
                &entry
            )
        );

        CloseHandle(snapshot);

        return processes;
    }

    void list_processes()
    {
        const auto processes =
            get_processes();

        if (processes.empty())
            return;

        std::vector<ProcessInfo> sorted =
            processes;

        std::sort(
            sorted.begin(),
            sorted.end(),
            [](const ProcessInfo& a,
               const ProcessInfo& b)
            {
                return a.pid < b.pid;
            }
        );

        flow::log::info(
            "{:<8} {:<8} {}",
            "PID",
            "PPID",
            "NAME"
        );

        flow::log::info(
            "{:<8} {:<8} {}",
            "--------",
            "--------",
            "------------------------------"
        );

        for (const auto& process : sorted)
        {
            flow::log::info(
                "{:<8} {:<8} {}",
                process.pid,
                process.parent_pid,
                process.name
            );
        }

        flow::log::success(
            "Found {} running processes",
            sorted.size()
        );
    }

    void find_processes(
        const std::string& term
    )
    {
        const auto processes =
            get_processes();

        if (processes.empty())
            return;

        const std::string search =
            to_lower(term);

        std::size_t matches = 0;

        flow::log::info(
            "Searching processes for: {}",
            term
        );

        flow::log::info("");

        for (const auto& process : processes)
        {
            const std::string name =
                to_lower(process.name);

            if (
                name.find(search) ==
                std::string::npos
            )
            {
                continue;
            }

            flow::log::success(
                "PID {:<8} {}",
                process.pid,
                process.name
            );

            ++matches;
        }

        flow::log::info("");

        if (matches == 0)
        {
            flow::log::warning(
                "No matching processes found"
            );

            return;
        }

        flow::log::success(
            "Found {} matching {}",
            matches,
            matches == 1
                ? "process"
                : "processes"
        );
    }

    bool parse_pid(
        const std::string& value,
        DWORD& pid
    )
    {
        if (value.empty())
            return false;

        unsigned long result = 0;

        for (const char character : value)
        {
            if (
                character < '0' ||
                character > '9'
            )
            {
                return false;
            }

            result =
                result * 10 +
                static_cast<unsigned long>(
                    character - '0'
                );

            if (
                result >
                static_cast<unsigned long>(
                    MAXDWORD
                )
            )
            {
                return false;
            }
        }

        pid =
            static_cast<DWORD>(result);

        return true;
    }

    bool get_process_by_pid(
        DWORD pid,
        ProcessInfo& result
    )
    {
        const auto processes =
            get_processes();

        for (const auto& process : processes)
        {
            if (process.pid == pid)
            {
                result = process;
                return true;
            }
        }

        return false;
    }

    std::size_t get_thread_count(
        DWORD pid
    )
    {
        HANDLE snapshot =
            CreateToolhelp32Snapshot(
                TH32CS_SNAPTHREAD,
                0
            );

        if (snapshot == INVALID_HANDLE_VALUE)
            return 0;

        THREADENTRY32 entry{};
        entry.dwSize =
            sizeof(THREADENTRY32);

        if (!Thread32First(
                snapshot,
                &entry
            ))
        {
            CloseHandle(snapshot);
            return 0;
        }

        std::size_t count = 0;

        do
        {
            if (
                entry.th32OwnerProcessID ==
                pid
            )
            {
                ++count;
            }

        } while (
            Thread32Next(
                snapshot,
                &entry
            )
        );

        CloseHandle(snapshot);

        return count;
    }

    std::string get_executable_path(
        DWORD pid
    )
    {
        HANDLE process =
            OpenProcess(
                PROCESS_QUERY_LIMITED_INFORMATION,
                FALSE,
                pid
            );

        if (!process)
            return {};

        std::wstring buffer(
            32768,
            L'\0'
        );

        DWORD size =
            static_cast<DWORD>(
                buffer.size()
            );

        const BOOL success =
            QueryFullProcessImageNameW(
                process,
                0,
                buffer.data(),
                &size
            );

        CloseHandle(process);

        if (!success)
            return {};

        buffer.resize(size);

        return wide_to_utf8(buffer);
    }

    void show_process_info(
        DWORD pid
    )
    {
        ProcessInfo process;

        if (!get_process_by_pid(
                pid,
                process
            ))
        {
            flow::log::error(
                "Process with PID {} was not found",
                pid
            );

            return;
        }

        flow::log::info(
            "Process information"
        );

        flow::log::info("");

        flow::log::info(
            "PID: {}",
            process.pid
        );

        flow::log::info(
            "Name: {}",
            process.name
        );

        flow::log::info(
            "Parent PID: {}",
            process.parent_pid
        );

        flow::log::info(
            "Threads: {}",
            get_thread_count(pid)
        );

        const std::string path =
            get_executable_path(pid);

        if (path.empty())
        {
            flow::log::warning(
                "Executable path: unavailable"
            );
        }
        else
        {
            flow::log::info(
                "Executable path: {}",
                path
            );
        }
    }

    void print_tree_node(
        DWORD pid,
        const std::vector<ProcessInfo>& processes,
        const std::string& prefix,
        bool is_last
    )
    {
        auto iterator =
            std::find_if(
                processes.begin(),
                processes.end(),
                [pid](const ProcessInfo& process)
                {
                    return process.pid == pid;
                }
            );

        if (iterator == processes.end())
            return;

        const std::string branch =
            prefix.empty()
                ? ""
                : (is_last ? "`-- " : "|-- ");

        flow::log::info(
            "{}{}{} ({})",
            prefix,
            branch,
            iterator->name,
            iterator->pid
        );

        std::vector<DWORD> children;

        for (const auto& process : processes)
        {
            if (
                process.parent_pid == pid &&
                process.pid != pid
            )
            {
                children.push_back(
                    process.pid
                );
            }
        }

        std::sort(
            children.begin(),
            children.end()
        );

        for (
            std::size_t i = 0;
            i < children.size();
            ++i
        )
        {
            const bool child_is_last =
                i + 1 == children.size();

            std::string child_prefix =
                prefix;

            if (!prefix.empty())
            {
                child_prefix +=
                    is_last
                        ? "    "
                        : "|   ";
            }

            print_tree_node(
                children[i],
                processes,
                child_prefix,
                child_is_last
            );
        }
    }

    void show_process_tree()
    {
        const auto processes =
            get_processes();

        if (processes.empty())
            return;

        std::vector<DWORD> roots;

        for (const auto& process : processes)
        {
            const auto parent =
                std::find_if(
                    processes.begin(),
                    processes.end(),
                    [&process](const ProcessInfo& candidate)
                    {
                        return candidate.pid ==
                            process.parent_pid;
                    }
                );

            if (parent == processes.end())
            {
                roots.push_back(
                    process.pid
                );
            }
        }

        std::sort(
            roots.begin(),
            roots.end()
        );

        flow::log::info(
            "Process tree"
        );

        flow::log::info("");

        for (
            std::size_t i = 0;
            i < roots.size();
            ++i
        )
        {
            print_tree_node(
                roots[i],
                processes,
                "",
                i + 1 == roots.size()
            );
        }

        flow::log::info("");

        flow::log::success(
            "Displayed process tree"
        );
    }

    bool ask_confirmation(
        const ProcessInfo& process
    )
    {
        flow::log::warning(
            "You are about to terminate:"
        );

        flow::log::info(
            "  PID: {}",
            process.pid
        );

        flow::log::info(
            "  Name: {}",
            process.name
        );

        flow::log::info("");

        std::cout
            << "Terminate this process? [y/N]: "
            << std::flush;

        std::string answer;

        if (!std::getline(
                std::cin,
                answer
            ))
        {
            return false;
        }

        if (answer.empty())
            return false;

        std::transform(
            answer.begin(),
            answer.end(),
            answer.begin(),
            [](unsigned char character)
            {
                return static_cast<char>(
                    std::tolower(character)
                );
            }
        );

        return answer == "y" ||
               answer == "yes";
    }

    int kill_process(
        const KillOptions& options
    )
    {
        ProcessInfo process;

        if (!get_process_by_pid(
                options.pid,
                process
            ))
        {
            flow::log::error(
                "Process with PID {} was not found",
                options.pid
            );

            return 1;
        }

        if (options.dry_run)
        {
            flow::log::info(
                "Dry run enabled"
            );

            flow::log::info(
                "Process: {}",
                process.name
            );

            flow::log::info(
                "PID: {}",
                process.pid
            );

            flow::log::info(
                "Action: terminate process"
            );

            flow::log::success(
                "Dry run completed - no process was terminated"
            );

            return 0;
        }

        if (!options.force)
        {
            if (!ask_confirmation(process))
            {
                flow::log::info(
                    "Process termination cancelled"
                );

                return 0;
            }
        }
        else
        {
            flow::log::warning(
                "Force mode enabled"
            );

            flow::log::warning(
                "Terminating PID {} ({}) without confirmation",
                process.pid,
                process.name
            );
        }

        HANDLE handle =
            OpenProcess(
                PROCESS_TERMINATE,
                FALSE,
                options.pid
            );

        if (!handle)
        {
            const DWORD error =
                GetLastError();

            flow::log::error(
                "Unable to open process {} (Windows error {})",
                options.pid,
                error
            );

            return 1;
        }

        const BOOL success =
            TerminateProcess(
                handle,
                1
            );

        CloseHandle(handle);

        if (!success)
        {
            const DWORD error =
                GetLastError();

            flow::log::error(
                "Unable to terminate process {} (Windows error {})",
                options.pid,
                error
            );

            return 1;
        }

        flow::log::success(
            "Process {} ({}) terminated",
            options.pid,
            process.name
        );

        return 0;
    }

    bool parse_kill_options(
        int argc,
        char* argv[],
        KillOptions& options
    )
    {
        if (argc < 3)
        {
            flow::log::error(
                "Missing PID"
            );

            flow::log::info(
                "Usage: fproc kill <pid> [--dry-run] [--force]"
            );

            return false;
        }

        if (!parse_pid(
                argv[2],
                options.pid
            ))
        {
            flow::log::error(
                "Invalid PID: {}",
                argv[2]
            );

            return false;
        }

        for (int i = 3; i < argc; ++i)
        {
            const std::string argument =
                argv[i];

            if (
                argument == "--dry-run"
            )
            {
                options.dry_run = true;
                continue;
            }

            if (
                argument == "--force"
            )
            {
                options.force = true;
                continue;
            }

            flow::log::error(
                "Unknown kill option: {}",
                argument
            );

            flow::log::info(
                "Use 'fproc kill --help' to see available options"
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
        list_processes();
        return 0;
    }

    const std::string command =
        argv[1];

    if (
        command == "help" ||
        command == "--help" ||
        command == "-h"
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

    if (command == "find")
    {
        if (argc < 3)
        {
            flow::log::error(
                "Missing process name"
            );

            flow::log::info(
                "Usage: fproc find <name>"
            );

            return 1;
        }

        find_processes(
            argv[2]
        );

        return 0;
    }

    if (command == "info")
    {
        if (argc < 3)
        {
            flow::log::error(
                "Missing PID"
            );

            flow::log::info(
                "Usage: fproc info <pid>"
            );

            return 1;
        }

        DWORD pid = 0;

        if (!parse_pid(
                argv[2],
                pid
            ))
        {
            flow::log::error(
                "Invalid PID: {}",
                argv[2]
            );

            return 1;
        }

        show_process_info(pid);

        return 0;
    }

    if (command == "tree")
    {
        show_process_tree();
        return 0;
    }

    if (command == "kill")
    {
        if (
            argc >= 3 &&
            (
                std::string(argv[2]) == "--help" ||
                std::string(argv[2]) == "-h"
            )
        )
        {
            flow::log::info(
                "Usage:"
            );

            flow::log::info(
                "  fproc kill <pid>"
            );

            flow::log::info(
                "      Terminate after confirmation"
            );

            flow::log::info(
                "  fproc kill <pid> --dry-run"
            );

            flow::log::info(
                "      Show what would happen without terminating"
            );

            flow::log::info(
                "  fproc kill <pid> --force"
            );

            flow::log::info(
                "      Terminate without confirmation"
            );

            return 0;
        }

        KillOptions options;

        if (!parse_kill_options(
                argc,
                argv,
                options
            ))
        {
            return 1;
        }

        return kill_process(options);
    }

    flow::log::error(
        "Unknown command: {}",
        command
    );

    flow::log::info(
        "Use 'fproc --help' to see available commands"
    );

    return 1;
}