#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>

#include "flow/core.hpp"
#include "flow/log.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

namespace
{
    struct TcpConnection
    {
        std::string local_address;
        std::uint16_t local_port = 0;

        std::string remote_address;
        std::uint16_t remote_port = 0;

        std::string state;
        DWORD pid = 0;
        std::string process_name;
    };

    std::string wide_to_utf8(const wchar_t* value)
    {
        if (value == nullptr || *value == L'\0')
            return {};

        const int required = WideCharToMultiByte(
            CP_UTF8,
            0,
            value,
            -1,
            nullptr,
            0,
            nullptr,
            nullptr
        );

        if (required <= 1)
            return {};

        std::string result(static_cast<std::size_t>(required - 1), '\0');

        WideCharToMultiByte(
            CP_UTF8,
            0,
            value,
            -1,
            result.data(),
            required,
            nullptr,
            nullptr
        );

        return result;
    }

    std::string get_process_name(DWORD pid)
    {
        if (pid == 0)
            return "System Idle Process";

        HANDLE process = OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION,
            FALSE,
            pid
        );

        if (process == nullptr)
            return "<access denied>";

        wchar_t buffer[MAX_PATH];
        DWORD size = static_cast<DWORD>(std::size(buffer));

        const BOOL result = QueryFullProcessImageNameW(
            process,
            0,
            buffer,
            &size
        );

        CloseHandle(process);

        if (!result)
            return "<unknown>";

        std::wstring path(buffer, size);

        const std::size_t separator = path.find_last_of(L"\\/");

        if (separator != std::wstring::npos)
            path.erase(0, separator + 1);

        return wide_to_utf8(path.c_str());
    }

    std::string tcp_state_to_string(DWORD state)
    {
        switch (state)
        {
        case MIB_TCP_STATE_CLOSED:
            return "CLOSED";

        case MIB_TCP_STATE_LISTEN:
            return "LISTENING";

        case MIB_TCP_STATE_SYN_SENT:
            return "SYN_SENT";

        case MIB_TCP_STATE_SYN_RCVD:
            return "SYN_RECEIVED";

        case MIB_TCP_STATE_ESTAB:
            return "ESTABLISHED";

        case MIB_TCP_STATE_FIN_WAIT1:
            return "FIN_WAIT_1";

        case MIB_TCP_STATE_FIN_WAIT2:
            return "FIN_WAIT_2";

        case MIB_TCP_STATE_CLOSE_WAIT:
            return "CLOSE_WAIT";

        case MIB_TCP_STATE_CLOSING:
            return "CLOSING";

        case MIB_TCP_STATE_LAST_ACK:
            return "LAST_ACK";

        case MIB_TCP_STATE_TIME_WAIT:
            return "TIME_WAIT";

        case MIB_TCP_STATE_DELETE_TCB:
            return "DELETE_TCB";

        default:
            return "UNKNOWN";
        }
    }

    std::string ipv4_to_string(DWORD address)
    {
        IN_ADDR addr{};

        addr.S_un.S_addr = address;

        wchar_t buffer[INET_ADDRSTRLEN]{};

        if (InetNtopW(
                AF_INET,
                &addr,
                buffer,
                INET_ADDRSTRLEN
            ) == nullptr)
        {
            return "<unknown>";
        }

        return wide_to_utf8(buffer);
    }

    std::uint16_t network_port_to_host(DWORD port)
    {
        return ntohs(
            static_cast<u_short>(port)
        );
    }

    std::vector<TcpConnection> get_tcp_connections()
    {
        std::vector<TcpConnection> connections;

        DWORD buffer_size = 0;

        DWORD result = GetExtendedTcpTable(
            nullptr,
            &buffer_size,
            FALSE,
            AF_INET,
            TCP_TABLE_OWNER_PID_ALL,
            0
        );

        if (result != ERROR_INSUFFICIENT_BUFFER)
        {
            flow::log::error(
                "Unable to determine TCP table size (error {})",
                result
            );

            return connections;
        }

        std::vector<std::byte> buffer(buffer_size);

        auto* table =
            reinterpret_cast<MIB_TCPTABLE_OWNER_PID*>(buffer.data());

        result = GetExtendedTcpTable(
            table,
            &buffer_size,
            FALSE,
            AF_INET,
            TCP_TABLE_OWNER_PID_ALL,
            0
        );

        if (result != NO_ERROR)
        {
            flow::log::error(
                "Unable to read TCP table (error {})",
                result
            );

            return connections;
        }

        connections.reserve(table->dwNumEntries);

        for (DWORD i = 0; i < table->dwNumEntries; ++i)
        {
            const auto& row = table->table[i];

            TcpConnection connection;

            connection.local_address =
                ipv4_to_string(row.dwLocalAddr);

            connection.local_port =
                network_port_to_host(row.dwLocalPort);

            connection.remote_address =
                ipv4_to_string(row.dwRemoteAddr);

            connection.remote_port =
                network_port_to_host(row.dwRemotePort);

            connection.state =
                tcp_state_to_string(row.dwState);

            connection.pid = row.dwOwningPid;

            connection.process_name =
                get_process_name(row.dwOwningPid);

            connections.push_back(std::move(connection));
        }

        std::sort(
            connections.begin(),
            connections.end(),
            [](const TcpConnection& a, const TcpConnection& b)
            {
                if (a.local_port != b.local_port)
                    return a.local_port < b.local_port;

                if (a.local_address != b.local_address)
                    return a.local_address < b.local_address;

                return a.pid < b.pid;
            }
        );

        return connections;
    }

    void show_help()
    {
        flow::log::info("fport - Windows TCP connection utility");
        flow::log::info("");
        flow::log::info("Usage:");
        flow::log::info("  fport");
        flow::log::info("      Show TCP connections");
        flow::log::info("  fport tcp");
        flow::log::info("      Show TCP connections");
        flow::log::info("  fport listen");
        flow::log::info("      Show listening TCP ports");
        flow::log::info("  fport find <port>");
        flow::log::info("      Find connections using a local or remote port");
        flow::log::info("");
        flow::log::info("Options:");
        flow::log::info("  --help");
        flow::log::info("  --version");
        flow::log::info("");
        flow::log::info("Examples:");
        flow::log::info("  fport");
        flow::log::info("  fport listen");
        flow::log::info("  fport find 443");
    }

    void show_version()
    {
        flow::log::info("fport 0.1.0");
        flow::log::info(
            "FlowTools Core {}",
            flow::core::version()
        );

#if defined(_M_X64)
        flow::log::info("Windows x64");
#elif defined(_M_IX86)
        flow::log::info("Windows x86");
#elif defined(_M_ARM64)
        flow::log::info("Windows ARM64");
#else
        flow::log::info("Windows unknown architecture");
#endif

        flow::log::success("Version information displayed");
    }

    void print_connection(const TcpConnection& connection)
    {
        const std::string local =
            std::format(
                "{}:{}",
                connection.local_address,
                connection.local_port
            );

        const std::string remote =
            std::format(
                "{}:{}",
                connection.remote_address,
                connection.remote_port
            );

        if (connection.state == "LISTENING")
        {
            flow::log::success("{}", local);
        }
        else
        {
            flow::log::success(
                "{} -> {}",
                local,
                remote
            );
        }

        flow::log::info(
            "  State: {}",
            connection.state
        );

        flow::log::info(
            "  PID: {}",
            connection.pid
        );

        flow::log::info(
            "  Process: {}",
            connection.process_name
        );

        flow::log::info("");
    }

    void show_connections(
        const std::vector<TcpConnection>& connections
    )
    {
        flow::log::info("TCP connections");
        flow::log::info("");

        if (connections.empty())
        {
            flow::log::warning("No TCP connections found");
            return;
        }

        std::size_t listening = 0;
        std::size_t established = 0;

        for (const auto& connection : connections)
        {
            if (connection.state == "LISTENING")
                ++listening;

            if (connection.state == "ESTABLISHED")
                ++established;

            print_connection(connection);
        }

        flow::log::success(
            "Found {} TCP connections ({} listening, {} established)",
            connections.size(),
            listening,
            established
        );
    }

    void show_listening(
        const std::vector<TcpConnection>& connections
    )
    {
        flow::log::info("Listening TCP ports");
        flow::log::info("");

        std::size_t count = 0;

        for (const auto& connection : connections)
        {
            if (connection.state != "LISTENING")
                continue;

            print_connection(connection);
            ++count;
        }

        if (count == 0)
        {
            flow::log::warning(
                "No listening TCP ports found"
            );
            return;
        }

        flow::log::success(
            "Found {} listening TCP ports",
            count
        );
    }

    bool parse_port(
        std::string_view value,
        std::uint16_t& port
    )
    {
        if (value.empty())
            return false;

        unsigned long parsed = 0;

        for (const char character : value)
        {
            if (character < '0' || character > '9')
                return false;

            parsed =
                parsed * 10 +
                static_cast<unsigned long>(character - '0');

            if (parsed > 65535)
                return false;
        }

        port = static_cast<std::uint16_t>(parsed);

        return true;
    }

    void find_port(
        const std::vector<TcpConnection>& connections,
        std::string_view value
    )
    {
        std::uint16_t port = 0;

        if (!parse_port(value, port))
        {
            flow::log::error(
                "Invalid port: {}",
                value
            );

            return;
        }

        flow::log::info(
            "Searching TCP port {}",
            port
        );

        flow::log::info("");

        std::size_t found = 0;

        for (const auto& connection : connections)
        {
            if (
                connection.local_port != port &&
                connection.remote_port != port
            )
            {
                continue;
            }

            print_connection(connection);
            ++found;
        }

        if (found == 0)
        {
            flow::log::warning(
                "No TCP connections found for port {}",
                port
            );

            return;
        }

        flow::log::success(
            "Found {} TCP connections for port {}",
            found,
            port
        );
    }
}

int main(int argc, char* argv[])
{
    flow::initialize();

    if (argc == 1)
    {
        const auto connections = get_tcp_connections();

        if (connections.empty())
            return 1;

        show_connections(connections);
        return 0;
    }

    const std::string_view command = argv[1];

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

    if (command == "tcp")
    {
        const auto connections = get_tcp_connections();

        if (connections.empty())
            return 1;

        show_connections(connections);
        return 0;
    }

    if (command == "listen")
    {
        const auto connections = get_tcp_connections();

        if (connections.empty())
            return 1;

        show_listening(connections);
        return 0;
    }

    if (command == "find")
    {
        if (argc < 3)
        {
            flow::log::error(
                "Missing port. Usage: fport find <port>"
            );

            return 1;
        }

        if (argc > 3)
        {
            flow::log::error(
                "Too many arguments. Usage: fport find <port>"
            );

            return 1;
        }

        const auto connections = get_tcp_connections();

        if (connections.empty())
            return 1;

        find_port(
            connections,
            argv[2]
        );

        return 0;
    }

    flow::log::error(
        "Unknown command: {}",
        command
    );

    flow::log::info(
        "Use 'fport --help' for usage information"
    );

    return 1;
}