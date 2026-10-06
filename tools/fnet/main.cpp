#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>

#include "flow/core.hpp"
#include "flow/log.hpp"

#include <format>
#include <string>
#include <vector>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

#ifndef FNET_VERSION
#define FNET_VERSION "0.1.0"
#endif

namespace
{
    struct AddressInfo
    {
        std::string address;
        bool ipv6 = false;
    };

    struct InterfaceInfo
    {
        std::string name;
        std::string description;
        std::string mac;
        bool up = false;

        std::vector<AddressInfo> addresses;
    };

    std::string wide_to_utf8(
        const wchar_t* value
    )
    {
        if (!value || *value == L'\0')
            return {};

        const int length =
            WideCharToMultiByte(
                CP_UTF8,
                0,
                value,
                -1,
                nullptr,
                0,
                nullptr,
                nullptr
            );

        if (length <= 1)
            return {};

        std::string result(
            static_cast<std::size_t>(length - 1),
            '\0'
        );

        WideCharToMultiByte(
            CP_UTF8,
            0,
            value,
            -1,
            result.data(),
            length,
            nullptr,
            nullptr
        );

        return result;
    }

    std::string mac_to_string(
        const BYTE* address,
        ULONG length
    )
    {
        if (!address || length == 0)
            return {};

        std::string result;

        for (ULONG i = 0; i < length; ++i)
        {
            if (i != 0)
                result += ':';

            result += std::format(
                "{:02X}",
                static_cast<unsigned int>(address[i])
            );
        }

        return result;
    }

    std::string sockaddr_to_string(
        const SOCKADDR* address
    )
    {
        if (!address)
            return {};

        wchar_t buffer[INET6_ADDRSTRLEN]{};

        if (address->sa_family == AF_INET)
        {
            const auto* ipv4 =
                reinterpret_cast<const SOCKADDR_IN*>(
                    address
                );

            if (!InetNtopW(
                    AF_INET,
                    const_cast<IN_ADDR*>(
                        &ipv4->sin_addr
                    ),
                    buffer,
                    INET6_ADDRSTRLEN
                ))
            {
                return {};
            }

            return wide_to_utf8(buffer);
        }

        if (address->sa_family == AF_INET6)
        {
            const auto* ipv6 =
                reinterpret_cast<const SOCKADDR_IN6*>(
                    address
                );

            if (!InetNtopW(
                    AF_INET6,
                    const_cast<IN6_ADDR*>(
                        &ipv6->sin6_addr
                    ),
                    buffer,
                    INET6_ADDRSTRLEN
                ))
            {
                return {};
            }

            return wide_to_utf8(buffer);
        }

        return {};
    }

    std::vector<InterfaceInfo> get_interfaces()
    {
        std::vector<InterfaceInfo> interfaces;

        ULONG buffer_size = 0;

        ULONG result =
            GetAdaptersAddresses(
                AF_UNSPEC,
                GAA_FLAG_INCLUDE_PREFIX,
                nullptr,
                nullptr,
                &buffer_size
            );

        if (
            result != ERROR_BUFFER_OVERFLOW &&
            result != NO_ERROR
        )
        {
            flow::log::error(
                "Unable to determine network adapter buffer size (Windows error {})",
                result
            );

            return interfaces;
        }

        std::vector<BYTE> buffer(
            buffer_size
        );

        auto* adapters =
            reinterpret_cast<IP_ADAPTER_ADDRESSES*>(
                buffer.data()
            );

        result =
            GetAdaptersAddresses(
                AF_UNSPEC,
                GAA_FLAG_INCLUDE_PREFIX,
                nullptr,
                adapters,
                &buffer_size
            );

        if (result != NO_ERROR)
        {
            flow::log::error(
                "Unable to enumerate network interfaces (Windows error {})",
                result
            );

            return interfaces;
        }

        for (
            auto* adapter = adapters;
            adapter != nullptr;
            adapter = adapter->Next
        )
        {
            InterfaceInfo info;

            info.name =
                wide_to_utf8(
                    adapter->FriendlyName
                );

            info.description =
                wide_to_utf8(
                    adapter->Description
                );

            info.mac =
                mac_to_string(
                    adapter->PhysicalAddress,
                    adapter->PhysicalAddressLength
                );

            info.up =
                adapter->OperStatus == IfOperStatusUp;

            for (
                auto* address = adapter->FirstUnicastAddress;
                address != nullptr;
                address = address->Next
            )
            {
                if (!address->Address.lpSockaddr)
                    continue;

                AddressInfo address_info;

                address_info.ipv6 =
                    address->Address.lpSockaddr->sa_family ==
                    AF_INET6;

                address_info.address =
                    sockaddr_to_string(
                        address->Address.lpSockaddr
                    );

                if (!address_info.address.empty())
                {
                    info.addresses.push_back(
                        std::move(address_info)
                    );
                }
            }

            interfaces.push_back(
                std::move(info)
            );
        }

        return interfaces;
    }

    void show_help()
    {
        flow::log::info(
            "fnet - Windows network information utility"
        );

        flow::log::info("");

        flow::log::info(
            "Usage:"
        );

        flow::log::info(
            "  fnet"
        );

        flow::log::info(
            "      Show network interfaces"
        );

        flow::log::info(
            "  fnet interfaces"
        );

        flow::log::info(
            "      Show network interfaces"
        );

        flow::log::info(
            "  fnet ip"
        );

        flow::log::info(
            "      Show IPv4 and IPv6 addresses"
        );

        flow::log::info("");

        flow::log::info(
            "Options:"
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
            "  fnet"
        );

        flow::log::info(
            "  fnet interfaces"
        );

        flow::log::info(
            "  fnet ip"
        );
    }

    void show_version()
    {
        flow::log::info(
            "fnet {}",
            FNET_VERSION
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

    void show_interfaces(
        bool addresses_only
    )
    {
        const auto interfaces =
            get_interfaces();

        if (interfaces.empty())
        {
            flow::log::warning(
                "No network interfaces found"
            );

            return;
        }

        flow::log::info(
            "Network interfaces"
        );

        flow::log::info("");

        std::size_t active = 0;

        for (const auto& interface : interfaces)
        {
            if (interface.up)
                ++active;

            if (addresses_only)
            {
                if (interface.addresses.empty())
                    continue;

                flow::log::success(
                    "{}",
                    interface.name
                );

                for (const auto& address :
                     interface.addresses)
                {
                    flow::log::info(
                        "  {} {}",
                        address.ipv6
                            ? "IPv6:"
                            : "IPv4:",
                        address.address
                    );
                }

                flow::log::info("");

                continue;
            }

            flow::log::success(
                "{}",
                interface.name
            );

            flow::log::info(
                "  Status: {}",
                interface.up
                    ? "up"
                    : "down"
            );

            if (!interface.description.empty())
            {
                flow::log::info(
                    "  Description: {}",
                    interface.description
                );
            }

            if (!interface.mac.empty())
            {
                flow::log::info(
                    "  MAC: {}",
                    interface.mac
                );
            }

            if (interface.addresses.empty())
            {
                flow::log::info(
                    "  Addresses: none"
                );
            }
            else
            {
                flow::log::info(
                    "  Addresses:"
                );

                for (const auto& address :
                     interface.addresses)
                {
                    flow::log::info(
                        "    {} {}",
                        address.ipv6
                            ? "IPv6:"
                            : "IPv4:",
                        address.address
                    );
                }
            }

            flow::log::info("");
        }

        flow::log::success(
            "Found {} network interfaces ({} up)",
            interfaces.size(),
            active
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
        show_interfaces(false);
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

    if (command == "interfaces")
    {
        show_interfaces(false);
        return 0;
    }

    if (command == "ip")
    {
        show_interfaces(true);
        return 0;
    }

    flow::log::error(
        "Unknown command: {}",
        command
    );

    flow::log::info(
        "Use 'fnet --help' to see available commands"
    );

    return 1;
}