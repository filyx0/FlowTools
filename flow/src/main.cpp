#include "flow/core.hpp"
#include "flow/log.hpp"

#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")

#ifndef FLOW_VERSION
#define FLOW_VERSION "unknown"
#endif

namespace
{
    namespace fs = std::filesystem;

    constexpr std::wstring_view GITHUB_OWNER = L"filyx0";
    constexpr std::wstring_view GITHUB_REPOSITORY = L"FlowTools";
    constexpr std::wstring_view GITHUB_API_HOST = L"api.github.com";

    const fs::path FLOWTOOLS_DIRECTORY = L"C:\\FlowTools";
    const fs::path BIN_DIRECTORY = FLOWTOOLS_DIRECTORY / L"bin";
    const fs::path DATA_FILE = FLOWTOOLS_DIRECTORY / L"data.json";
    const fs::path VERSIONS_FILE = FLOWTOOLS_DIRECTORY / L"versions.json";

    struct Tool
    {
        std::string name;
        std::string file;
        std::string path;
        std::string version;
        std::uint64_t size = 0;
        std::string sha256;
    };

    struct Asset
    {
        std::string name;
        std::string download_url;
        std::uint64_t size = 0;
        std::string digest;
    };

    struct InstalledTool
    {
        std::string name;
        std::string version;
    };

    enum class Key
    {
        Up,
        Down,
        Enter,
        Escape,
        Other
    };

    struct DownloadResult
    {
        bool success = false;
        std::uint64_t bytes = 0;
    };

    void clear_screen()
    {
        HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);

        if (console == INVALID_HANDLE_VALUE)
            return;

        CONSOLE_SCREEN_BUFFER_INFO info{};

        if (!GetConsoleScreenBufferInfo(console, &info))
            return;

        const DWORD cells =
            static_cast<DWORD>(info.dwSize.X) *
            static_cast<DWORD>(info.dwSize.Y);

        COORD origin{};
        origin.X = 0;
        origin.Y = 0;

        DWORD written = 0;

        FillConsoleOutputCharacterW(
            console,
            L' ',
            cells,
            origin,
            &written
        );

        FillConsoleOutputAttribute(
            console,
            info.wAttributes,
            cells,
            origin,
            &written
        );

        SetConsoleCursorPosition(
            console,
            origin
        );
    }

    void set_cursor_visible(bool visible)
    {
        HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);

        if (console == INVALID_HANDLE_VALUE)
            return;

        CONSOLE_CURSOR_INFO info{};

        if (!GetConsoleCursorInfo(console, &info))
            return;

        info.bVisible = visible ? TRUE : FALSE;

        SetConsoleCursorInfo(
            console,
            &info
        );
    }

    Key read_key()
    {
        HANDLE input = GetStdHandle(STD_INPUT_HANDLE);

        if (input == INVALID_HANDLE_VALUE)
            return Key::Other;

        while (true)
        {
            INPUT_RECORD record{};
            DWORD count = 0;

            if (!ReadConsoleInputW(
                    input,
                    &record,
                    1,
                    &count))
            {
                return Key::Other;
            }

            if (record.EventType != KEY_EVENT)
                continue;

            const KEY_EVENT_RECORD& event =
                record.Event.KeyEvent;

            if (!event.bKeyDown)
                continue;

            switch (event.wVirtualKeyCode)
            {
            case VK_UP:
                return Key::Up;

            case VK_DOWN:
                return Key::Down;

            case VK_RETURN:
                return Key::Enter;

            case VK_ESCAPE:
                return Key::Escape;

            default:
                return Key::Other;
            }
        }
    }

    void wait_for_enter()
    {
        set_cursor_visible(true);

        std::cout
            << "\nPress ENTER to continue...";

        while (read_key() != Key::Enter)
        {
        }

        set_cursor_visible(false);
    }

    void print_header()
    {
        std::cout
            << "+----------------------------------------------------------+\n"
            << "|                       FLOWTOOLS                          |\n"
            << "|                  Package Manager "
            << FLOW_VERSION
            << "                    |\n"
            << "+----------------------------------------------------------+\n";
    }

    void print_footer()
    {
        std::cout
            << "\n"
            << "[UP/DOWN] Navigate    "
            << "[ENTER] Select    "
            << "[ESC] Back\n";
    }

    std::string wide_to_utf8(std::wstring_view value)
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
            static_cast<std::size_t>(size),
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

    std::wstring utf8_to_wide(std::string_view value)
    {
        if (value.empty())
            return {};

        const int size =
            MultiByteToWideChar(
                CP_UTF8,
                0,
                value.data(),
                static_cast<int>(value.size()),
                nullptr,
                0
            );

        if (size <= 0)
            return {};

        std::wstring result(
            static_cast<std::size_t>(size),
            L'\0'
        );

        MultiByteToWideChar(
            CP_UTF8,
            0,
            value.data(),
            static_cast<int>(value.size()),
            result.data(),
            size
        );

        return result;
    }

    std::string read_file(const fs::path& file)
    {
        std::ifstream input(
            file,
            std::ios::binary
        );

        if (!input)
            return {};

        return std::string(
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()
        );
    }

    bool write_file(
        const fs::path& file,
        const std::string& content)
    {
        std::error_code error;

        fs::create_directories(
            file.parent_path(),
            error
        );

        std::ofstream output(
            file,
            std::ios::binary
        );

        if (!output)
            return false;

        output.write(
            content.data(),
            static_cast<std::streamsize>(
                content.size()
            )
        );

        return output.good();
    }

    std::string json_string(
        const std::string& json,
        std::string_view key,
        std::size_t start)
    {
        const std::string pattern =
            "\"" + std::string(key) + "\"";

        const std::size_t key_position =
            json.find(
                pattern,
                start
            );

        if (key_position == std::string::npos)
            return {};

        const std::size_t colon =
            json.find(
                ':',
                key_position + pattern.size()
            );

        if (colon == std::string::npos)
            return {};

        const std::size_t first_quote =
            json.find(
                '"',
                colon + 1
            );

        if (first_quote == std::string::npos)
            return {};

        const std::size_t second_quote =
            json.find(
                '"',
                first_quote + 1
            );

        if (second_quote == std::string::npos)
            return {};

        return json.substr(
            first_quote + 1,
            second_quote - first_quote - 1
        );
    }

    std::uint64_t json_number(
        const std::string& json,
        std::string_view key,
        std::size_t start)
    {
        const std::string pattern =
            "\"" + std::string(key) + "\"";

        const std::size_t key_position =
            json.find(
                pattern,
                start
            );

        if (key_position == std::string::npos)
            return 0;

        const std::size_t colon =
            json.find(
                ':',
                key_position + pattern.size()
            );

        if (colon == std::string::npos)
            return 0;

        std::size_t position = colon + 1;

        while (
            position < json.size() &&
            (json[position] == ' ' ||
             json[position] == '\t' ||
             json[position] == '\r' ||
             json[position] == '\n'))
        {
            ++position;
        }

        const std::size_t end =
            json.find_first_not_of(
                "0123456789",
                position
            );

        try
        {
            return std::stoull(
                json.substr(
                    position,
                    end == std::string::npos
                        ? std::string::npos
                        : end - position
                )
            );
        }
        catch (...)
        {
            return 0;
        }
    }

    std::string normalize_digest(
        std::string value)
    {
        constexpr std::string_view prefix =
            "sha256:";

        if (value.rfind(
                prefix,
                0) == 0)
        {
            value.erase(
                0,
                prefix.size()
            );
        }

        std::transform(
            value.begin(),
            value.end(),
            value.begin(),
            [](unsigned char c)
            {
                return static_cast<char>(
                    std::tolower(c)
                );
            }
        );

        return value;
    }

    bool http_get(
        const std::wstring& host,
        const std::wstring& path,
        std::string& response)
    {
        HINTERNET session =
            WinHttpOpen(
                L"FlowTools",
                WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                WINHTTP_NO_PROXY_NAME,
                WINHTTP_NO_PROXY_BYPASS,
                0
            );

        if (!session)
            return false;

        HINTERNET connection =
            WinHttpConnect(
                session,
                host.c_str(),
                INTERNET_DEFAULT_HTTPS_PORT,
                0
            );

        if (!connection)
        {
            WinHttpCloseHandle(session);
            return false;
        }

        HINTERNET request =
            WinHttpOpenRequest(
                connection,
                L"GET",
                path.c_str(),
                nullptr,
                WINHTTP_NO_REFERER,
                WINHTTP_DEFAULT_ACCEPT_TYPES,
                WINHTTP_FLAG_SECURE
            );

        if (!request)
        {
            WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);
            return false;
        }

        const wchar_t* headers =
            L"Accept: application/vnd.github+json\r\n"
            L"User-Agent: FlowTools\r\n";

        if (!WinHttpSendRequest(
                request,
                headers,
                static_cast<DWORD>(-1L),
                WINHTTP_NO_REQUEST_DATA,
                0,
                0,
                0))
        {
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);
            return false;
        }

        if (!WinHttpReceiveResponse(
                request,
                nullptr))
        {
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);
            return false;
        }

        DWORD status = 0;
        DWORD status_size = sizeof(status);

        if (!WinHttpQueryHeaders(
                request,
                WINHTTP_QUERY_STATUS_CODE |
                WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX,
                &status,
                &status_size,
                WINHTTP_NO_HEADER_INDEX))
        {
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);
            return false;
        }

        if (status < 200 || status >= 300)
        {
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);
            return false;
        }

        response.clear();

        while (true)
        {
            DWORD available = 0;

            if (!WinHttpQueryDataAvailable(
                    request,
                    &available))
            {
                WinHttpCloseHandle(request);
                WinHttpCloseHandle(connection);
                WinHttpCloseHandle(session);
                return false;
            }

            if (available == 0)
                break;

            std::vector<char> buffer(
                static_cast<std::size_t>(available)
            );

            DWORD downloaded = 0;

            if (!WinHttpReadData(
                    request,
                    buffer.data(),
                    available,
                    &downloaded))
            {
                WinHttpCloseHandle(request);
                WinHttpCloseHandle(connection);
                WinHttpCloseHandle(session);
                return false;
            }

            response.append(
                buffer.data(),
                downloaded
            );
        }

        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);

        return true;
    }

    std::string get_release_json()
    {
        const std::wstring path =
            L"/repos/" +
            std::wstring(GITHUB_OWNER) +
            L"/" +
            std::wstring(GITHUB_REPOSITORY) +
            L"/releases/latest";

        std::string response;

        if (!http_get(
                std::wstring(GITHUB_API_HOST),
                path,
                response))
        {
            return {};
        }

        return response;
    }

    bool find_asset(
        const std::string& json,
        std::string_view wanted,
        Asset& asset)
    {
        std::size_t position = 0;

        while (true)
        {
            const std::size_t name_position =
                json.find(
                    "\"name\"",
                    position
                );

            if (name_position == std::string::npos)
                return false;

            const std::string name =
                json_string(
                    json,
                    "name",
                    name_position
                );

            const std::size_t next_name =
                json.find(
                    "\"name\"",
                    name_position + 6
                );

            const std::size_t end =
                next_name == std::string::npos
                    ? json.size()
                    : next_name;

            if (name == wanted)
            {
                const std::size_t url_position =
                    json.find(
                        "\"browser_download_url\"",
                        name_position
                    );

                if (url_position == std::string::npos ||
                    url_position >= end)
                {
                    return false;
                }

                asset.name = name;

                asset.download_url =
                    json_string(
                        json,
                        "browser_download_url",
                        url_position
                    );

                asset.size =
                    json_number(
                        json,
                        "size",
                        name_position
                    );

                const std::size_t digest_position =
                    json.find(
                        "\"digest\"",
                        name_position
                    );

                if (digest_position != std::string::npos &&
                    digest_position < end)
                {
                    asset.digest =
                        normalize_digest(
                            json_string(
                                json,
                                "digest",
                                digest_position
                            )
                        );
                }

                return !asset.download_url.empty();
            }

            position = name_position + 6;
        }
    }

    bool load_manifest(
        const fs::path& file,
        std::vector<Tool>& tools)
    {
        const std::string json =
            read_file(file);

        if (json.empty())
            return false;

        tools.clear();

        std::size_t position = 0;

        while (true)
        {
            const std::size_t name_position =
                json.find(
                    "\"name\"",
                    position
                );

            if (name_position == std::string::npos)
                break;

            const std::size_t next_name =
                json.find(
                    "\"name\"",
                    name_position + 6
                );

            const std::size_t object_end =
                next_name == std::string::npos
                    ? json.size()
                    : next_name;

            Tool tool;

            tool.name =
                json_string(
                    json,
                    "name",
                    name_position
                );

            tool.file =
                json_string(
                    json,
                    "file",
                    name_position
                );

            tool.path =
                json_string(
                    json,
                    "path",
                    name_position
                );

            tool.version =
                json_string(
                    json,
                    "version",
                    name_position
                );

            tool.size =
                json_number(
                    json,
                    "size",
                    name_position
                );

            tool.sha256 =
                normalize_digest(
                    json_string(
                        json,
                        "sha256",
                        name_position
                    )
                );

            if (!tool.name.empty() &&
                !tool.file.empty() &&
                !tool.path.empty() &&
                !tool.version.empty())
            {
                tools.push_back(
                    std::move(tool)
                );
            }

            position = object_end;
        }

        return !tools.empty();
    }

    bool save_versions(
        const std::vector<InstalledTool>& versions)
    {
        std::ostringstream output;

        output
            << "{\n"
            << "    \"tools\": [\n";

        for (std::size_t i = 0;
             i < versions.size();
             ++i)
        {
            output
                << "        {\n"
                << "            \"name\": \""
                << versions[i].name
                << "\",\n"
                << "            \"version\": \""
                << versions[i].version
                << "\"\n"
                << "        }";

            if (i + 1 < versions.size())
                output << ',';

            output << '\n';
        }

        output
            << "    ]\n"
            << "}\n";

        return write_file(
            VERSIONS_FILE,
            output.str()
        );
    }

    std::vector<InstalledTool> load_versions()
    {
        std::vector<InstalledTool> result;

        if (!fs::exists(VERSIONS_FILE))
            return result;

        const std::string json =
            read_file(VERSIONS_FILE);

        if (json.empty())
            return result;

        std::size_t position = 0;

        while (true)
        {
            const std::size_t name_position =
                json.find(
                    "\"name\"",
                    position
                );

            if (name_position == std::string::npos)
                break;

            InstalledTool tool;

            tool.name =
                json_string(
                    json,
                    "name",
                    name_position
                );

            tool.version =
                json_string(
                    json,
                    "version",
                    name_position
                );

            if (!tool.name.empty() &&
                !tool.version.empty())
            {
                result.push_back(
                    std::move(tool)
                );
            }

            position =
                name_position + 6;
        }

        return result;
    }

    void set_installed_version(
        const std::string& name,
        const std::string& version)
    {
        auto versions = load_versions();

        auto it =
            std::find_if(
                versions.begin(),
                versions.end(),
                [&](const InstalledTool& tool)
                {
                    return tool.name == name;
                }
            );

        if (it == versions.end())
        {
            versions.push_back(
                {
                    name,
                    version
                }
            );
        }
        else
        {
            it->version = version;
        }

        save_versions(versions);
    }

    void remove_installed_version(
        const std::string& name)
    {
        auto versions = load_versions();

        versions.erase(
            std::remove_if(
                versions.begin(),
                versions.end(),
                [&](const InstalledTool& tool)
                {
                    return tool.name == name;
                }
            ),
            versions.end()
        );

        save_versions(versions);
    }

    std::string installed_version(
        const std::string& name)
    {
        const auto versions =
            load_versions();

        const auto it =
            std::find_if(
                versions.begin(),
                versions.end(),
                [&](const InstalledTool& tool)
                {
                    return tool.name == name;
                }
            );

        if (it == versions.end())
            return {};

        return it->version;
    }

    std::string format_bytes(
        std::uint64_t bytes)
    {
        const double value =
            static_cast<double>(bytes);

        std::ostringstream output;
        output << std::fixed << std::setprecision(2);

        if (bytes >= 1024ULL * 1024ULL * 1024ULL)
            output << value / (1024.0 * 1024.0 * 1024.0) << " GB";
        else if (bytes >= 1024ULL * 1024ULL)
            output << value / (1024.0 * 1024.0) << " MB";
        else if (bytes >= 1024ULL)
            output << value / 1024.0 << " KB";
        else
            output << bytes << " B";

        return output.str();
    }

    void print_progress(
        std::uint64_t current,
        std::uint64_t total)
    {
        constexpr int width = 30;

        int percent = 0;

        if (total > 0)
        {
            percent = static_cast<int>(
                (current * 100) / total
            );

            if (percent > 100)
                percent = 100;
        }

        const int filled =
            (percent * width) / 100;

        std::cout
            << '\r'
            << "  Downloading [";

        for (int i = 0; i < width; ++i)
        {
            if (i < filled)
                std::cout << '#';
            else
                std::cout << '-';
        }

        std::cout
            << "] "
            << std::setw(3)
            << percent
            << "% "
            << format_bytes(current);

        if (total > 0)
        {
            std::cout
                << " / "
                << format_bytes(total);
        }

        std::cout
            << "   "
            << std::flush;
    }

    DownloadResult download_asset(
        const Asset& asset,
        const fs::path& destination)
    {
        const std::wstring url =
            utf8_to_wide(
                asset.download_url
            );

        if (url.rfind(
                L"https://",
                0) != 0)
        {
            return {};
        }

        const std::size_t host_start = 8;

        const std::size_t slash =
            url.find(
                L'/',
                host_start
            );

        if (slash == std::wstring::npos)
            return {};

        const std::wstring host =
            url.substr(
                host_start,
                slash - host_start
            );

        const std::wstring path =
            url.substr(slash);

        HINTERNET session =
            WinHttpOpen(
                L"FlowTools",
                WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                WINHTTP_NO_PROXY_NAME,
                WINHTTP_NO_PROXY_BYPASS,
                0
            );

        if (!session)
            return {};

        HINTERNET connection =
            WinHttpConnect(
                session,
                host.c_str(),
                INTERNET_DEFAULT_HTTPS_PORT,
                0
            );

        if (!connection)
        {
            WinHttpCloseHandle(session);
            return {};
        }

        HINTERNET request =
            WinHttpOpenRequest(
                connection,
                L"GET",
                path.c_str(),
                nullptr,
                WINHTTP_NO_REFERER,
                WINHTTP_DEFAULT_ACCEPT_TYPES,
                WINHTTP_FLAG_SECURE
            );

        if (!request)
        {
            WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);
            return {};
        }

        const wchar_t* headers =
            L"User-Agent: FlowTools\r\n";

        if (!WinHttpSendRequest(
                request,
                headers,
                static_cast<DWORD>(-1L),
                WINHTTP_NO_REQUEST_DATA,
                0,
                0,
                0))
        {
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);
            return {};
        }

        if (!WinHttpReceiveResponse(
                request,
                nullptr))
        {
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);
            return {};
        }

        DWORD status = 0;
        DWORD status_size = sizeof(status);

        if (!WinHttpQueryHeaders(
                request,
                WINHTTP_QUERY_STATUS_CODE |
                WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX,
                &status,
                &status_size,
                WINHTTP_NO_HEADER_INDEX))
        {
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);
            return {};
        }

        if (status < 200 || status >= 300)
        {
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);
            return {};
        }

        std::uint64_t total = asset.size;

        DWORD content_length = 0;
        DWORD content_length_size =
            sizeof(content_length);

        if (WinHttpQueryHeaders(
                request,
                WINHTTP_QUERY_CONTENT_LENGTH |
                WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX,
                &content_length,
                &content_length_size,
                WINHTTP_NO_HEADER_INDEX))
        {
            if (content_length > 0)
                total = content_length;
        }

        std::error_code error;

        fs::create_directories(
            destination.parent_path(),
            error
        );

        std::ofstream output(
            destination,
            std::ios::binary
        );

        if (!output)
        {
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);
            return {};
        }

        std::uint64_t downloaded_total = 0;

        while (true)
        {
            DWORD available = 0;

            if (!WinHttpQueryDataAvailable(
                    request,
                    &available))
            {
                output.close();

                WinHttpCloseHandle(request);
                WinHttpCloseHandle(connection);
                WinHttpCloseHandle(session);

                return {};
            }

            if (available == 0)
                break;

            std::vector<char> buffer(
                static_cast<std::size_t>(available)
            );

            DWORD downloaded = 0;

            if (!WinHttpReadData(
                    request,
                    buffer.data(),
                    available,
                    &downloaded))
            {
                output.close();

                WinHttpCloseHandle(request);
                WinHttpCloseHandle(connection);
                WinHttpCloseHandle(session);

                return {};
            }

            output.write(
                buffer.data(),
                static_cast<std::streamsize>(
                    downloaded
                )
            );

            if (!output)
            {
                output.close();

                WinHttpCloseHandle(request);
                WinHttpCloseHandle(connection);
                WinHttpCloseHandle(session);

                return {};
            }

            downloaded_total += downloaded;

            print_progress(
                downloaded_total,
                total
            );
        }

        output.close();

        std::cout << '\n';

        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);

        return {
            true,
            downloaded_total
        };
    }

    std::string sha256_file(
        const fs::path& file)
    {
        std::ifstream input(
            file,
            std::ios::binary
        );

        if (!input)
            return {};

        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;

        DWORD object_size = 0;
        DWORD result_size = 0;

        if (BCryptOpenAlgorithmProvider(
                &algorithm,
                BCRYPT_SHA256_ALGORITHM,
                nullptr,
                0) != 0)
        {
            return {};
        }

        if (BCryptGetProperty(
                algorithm,
                BCRYPT_OBJECT_LENGTH,
                reinterpret_cast<PUCHAR>(
                    &object_size
                ),
                sizeof(object_size),
                &result_size,
                0) != 0)
        {
            BCryptCloseAlgorithmProvider(
                algorithm,
                0
            );

            return {};
        }

        std::vector<UCHAR> object(
            object_size
        );

        if (BCryptCreateHash(
                algorithm,
                &hash,
                object.data(),
                object_size,
                nullptr,
                0,
                0) != 0)
        {
            BCryptCloseAlgorithmProvider(
                algorithm,
                0
            );

            return {};
        }

        std::vector<char> buffer(
            1024 * 1024
        );

        while (input)
        {
            input.read(
                buffer.data(),
                static_cast<std::streamsize>(
                    buffer.size()
                )
            );

            const std::streamsize count =
                input.gcount();

            if (count <= 0)
                break;

            if (BCryptHashData(
                    hash,
                    reinterpret_cast<PUCHAR>(
                        buffer.data()
                    ),
                    static_cast<ULONG>(count),
                    0) != 0)
            {
                BCryptDestroyHash(hash);
                BCryptCloseAlgorithmProvider(
                    algorithm,
                    0
                );

                return {};
            }
        }

        std::vector<UCHAR> digest(
            32
        );

        if (BCryptFinishHash(
                hash,
                digest.data(),
                static_cast<ULONG>(
                    digest.size()
                ),
                0) != 0)
        {
            BCryptDestroyHash(hash);
            BCryptCloseAlgorithmProvider(
                algorithm,
                0
            );

            return {};
        }

        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(
            algorithm,
            0
        );

        std::ostringstream output;

        output
            << std::hex
            << std::setfill('0');

        for (UCHAR byte : digest)
        {
            output
                << std::setw(2)
                << static_cast<unsigned int>(
                    byte
                );
        }

        return output.str();
    }

    bool install_asset(
        const Asset& asset,
        const Tool& tool)
    {
        const fs::path destination =
            FLOWTOOLS_DIRECTORY /
            utf8_to_wide(tool.path);

        const fs::path temporary =
            destination.wstring() +
            L".tmp";

        std::error_code error;

        fs::create_directories(
            destination.parent_path(),
            error
        );

        if (error)
            return false;

        std::cout
            << "\n"
            << "-> Installing "
            << tool.name
            << " "
            << tool.version
            << "\n";

        std::cout
            << "  File: "
            << tool.file
            << "\n";

        std::cout
            << "  Size: "
            << format_bytes(
                asset.size
            )
            << "\n";

        std::cout
            << "  Downloading...\n";

        const DownloadResult result =
            download_asset(
                asset,
                temporary
            );

        if (!result.success)
        {
            std::cout
                << "  [ERROR] Download failed\n";

            fs::remove(
                temporary,
                error
            );

            return false;
        }

        std::cout
            << "  [OK] Download complete\n";

        const std::string expected =
            !tool.sha256.empty()
                ? normalize_digest(tool.sha256)
                : normalize_digest(asset.digest);

        if (!expected.empty())
        {
            std::cout
                << "  Verifying SHA-256...\n";

            const std::string actual =
                sha256_file(temporary);

            if (actual.empty())
            {
                std::cout
                    << "  [ERROR] SHA-256 calculation failed\n";

                fs::remove(
                    temporary,
                    error
                );

                return false;
            }

            if (actual != expected)
            {
                std::cout
                    << "  [ERROR] SHA-256 mismatch\n";

                fs::remove(
                    temporary,
                    error
                );

                return false;
            }

            std::cout
                << "  [OK] SHA-256 verified\n";
        }
        else
        {
            std::cout
                << "  SHA-256: not provided\n";
        }

        std::cout
            << "  Installing "
            << tool.file
            << "...\n";

        fs::remove(
            destination,
            error
        );

        error.clear();

        fs::rename(
            temporary,
            destination,
            error
        );

        if (error)
        {
            std::cout
                << "  [ERROR] Installation failed\n";

            fs::remove(
                temporary,
                error
            );

            return false;
        }

        set_installed_version(
            tool.name,
            tool.version
        );

        std::cout
            << "  [OK] Installed "
            << tool.name
            << " "
            << tool.version
            << "\n";

        return true;
    }

    bool update_manifest()
    {
        std::cout
            << "-> Checking latest release...\n";

        const std::string release =
            get_release_json();

        if (release.empty())
        {
            std::cout
                << "[ERROR] Unable to contact GitHub.\n";

            return false;
        }

        Asset manifest_asset;

        if (!find_asset(
                release,
                "data.json",
                manifest_asset))
        {
            std::cout
                << "[ERROR] data.json not found in release.\n";

            return false;
        }

        const fs::path temporary =
            FLOWTOOLS_DIRECTORY /
            L"data.json.tmp";

        std::cout
            << "-> Downloading package manifest...\n";

        const DownloadResult result =
            download_asset(
                manifest_asset,
                temporary
            );

        if (!result.success)
        {
            std::cout
                << "[ERROR] Manifest download failed.\n";

            return false;
        }

        std::vector<Tool> tools;

        if (!load_manifest(
                temporary,
                tools))
        {
            std::cout
                << "[ERROR] Invalid data.json.\n";

            std::error_code error;

            fs::remove(
                temporary,
                error
            );

            return false;
        }

        std::error_code error;

        fs::remove(
            DATA_FILE,
            error
        );

        error.clear();

        fs::rename(
            temporary,
            DATA_FILE,
            error
        );

        if (error)
        {
            std::cout
                << "[ERROR] Unable to save data.json.\n";

            fs::remove(
                temporary,
                error
            );

            return false;
        }

        std::cout
            << "[OK] Manifest updated: "
            << tools.size()
            << " packages available\n";

        return true;
    }

    bool ensure_manifest()
    {
        if (fs::exists(DATA_FILE))
            return true;

        return update_manifest();
    }

    bool find_tool(
        const std::vector<Tool>& tools,
        const std::string& name,
        Tool& result)
    {
        const auto it =
            std::find_if(
                tools.begin(),
                tools.end(),
                [&](const Tool& tool)
                {
                    return tool.name == name;
                }
            );

        if (it == tools.end())
            return false;

        result = *it;
        return true;
    }

    bool install_named_tool(
        const std::string& name,
        bool install_core_dependency)
    {
        if (!ensure_manifest())
            return false;

        std::vector<Tool> tools;

        if (!load_manifest(
                DATA_FILE,
                tools))
        {
            std::cout
                << "[ERROR] Unable to read data.json.\n";

            return false;
        }

        Tool tool;

        if (!find_tool(
                tools,
                name,
                tool))
        {
            std::cout
                << "[ERROR] Package not found: "
                << name
                << "\n";

            return false;
        }

        if (install_core_dependency &&
            name != "core")
        {
            const fs::path core_file =
                BIN_DIRECTORY /
                L"flow_core.dll";

            if (!fs::exists(core_file))
            {
                std::cout
                    << "\n-> Dependency: core\n";

                if (!install_named_tool(
                        "core",
                        false))
                {
                    return false;
                }
            }
        }

        const std::string installed =
            installed_version(name);

        if (installed == tool.version)
        {
            std::cout
                << "\n[OK] "
                << name
                << " "
                << tool.version
                << " is already installed.\n";

            return true;
        }

        const std::string release =
            get_release_json();

        if (release.empty())
        {
            std::cout
                << "[ERROR] Unable to contact GitHub.\n";

            return false;
        }

        Asset asset;

        if (!find_asset(
                release,
                tool.file,
                asset))
        {
            std::cout
                << "[ERROR] Asset not found in release: "
                << tool.file
                << "\n";

            return false;
        }

        if (tool.size == 0)
            tool.size = asset.size;

        if (tool.sha256.empty())
            tool.sha256 = asset.digest;

        return install_asset(
            asset,
            tool
        );
    }

    bool uninstall_named_tool(
        const std::string& name)
    {
        if (!ensure_manifest())
            return false;

        std::vector<Tool> tools;

        if (!load_manifest(
                DATA_FILE,
                tools))
        {
            std::cout
                << "[ERROR] Unable to read data.json.\n";

            return false;
        }

        Tool tool;

        if (!find_tool(
                tools,
                name,
                tool))
        {
            std::cout
                << "[ERROR] Package not found: "
                << name
                << "\n";

            return false;
        }

        const fs::path file =
            FLOWTOOLS_DIRECTORY /
            utf8_to_wide(tool.path);

        if (!fs::exists(file))
        {
            std::cout
                << "[OK] "
                << name
                << " is not installed.\n";

            remove_installed_version(name);

            return true;
        }

        std::cout
            << "-> Removing "
            << name
            << "...\n";

        std::error_code error;

        fs::remove(
            file,
            error
        );

        if (error)
        {
            std::cout
                << "[ERROR] Unable to remove "
                << name
                << ".\n";

            return false;
        }

        remove_installed_version(name);

        std::cout
            << "[OK] Removed "
            << name
            << ".\n";

        return true;
    }

    void show_list()
    {
        clear_screen();
        print_header();

        if (!ensure_manifest())
        {
            wait_for_enter();
            return;
        }

        std::vector<Tool> tools;

        if (!load_manifest(
                DATA_FILE,
                tools))
        {
            std::cout
                << "\nUnable to read data.json.\n";

            wait_for_enter();
            return;
        }

        std::sort(
            tools.begin(),
            tools.end(),
            [](const Tool& a, const Tool& b)
            {
                return a.name < b.name;
            }
        );

        std::cout
            << "\nPackages\n\n";

        for (const Tool& tool : tools)
        {
            const std::string version =
                installed_version(
                    tool.name
                );

            std::cout
                << "  "
                << tool.name
                << "  "
                << tool.version
                << "  ";

            if (!version.empty())
                std::cout
                    << "[installed "
                    << version
                    << "]";
            else
                std::cout
                    << "[available]";

            std::cout << '\n';
        }

        wait_for_enter();
    }

    void show_info(
        const std::string& name)
    {
        clear_screen();
        print_header();

        if (!ensure_manifest())
        {
            wait_for_enter();
            return;
        }

        std::vector<Tool> tools;

        if (!load_manifest(
                DATA_FILE,
                tools))
        {
            wait_for_enter();
            return;
        }

        Tool tool;

        if (!find_tool(
                tools,
                name,
                tool))
        {
            std::cout
                << "\nPackage not found: "
                << name
                << "\n";

            wait_for_enter();
            return;
        }

        const std::string installed =
            installed_version(name);

        std::cout
            << "\nName       : "
            << tool.name
            << "\nVersion    : "
            << tool.version
            << "\nInstalled  : "
            << (installed.empty()
                    ? "no"
                    : installed)
            << "\nFile       : "
            << tool.file
            << "\nPath       : "
            << tool.path
            << "\nSize       : "
            << format_bytes(tool.size)
            << "\nSHA-256    : "
            << (
                tool.sha256.empty()
                    ? "not provided"
                    : tool.sha256
            )
            << "\n";

        wait_for_enter();
    }

    bool update_tool(
        const Tool& tool,
        const std::string& current)
    {
        if (current == tool.version)
        {
            std::cout
                << "  "
                << tool.name
                << " "
                << tool.version
                << "  up to date\n";

            return true;
        }

        std::cout
            << "\n-> Updating "
            << tool.name
            << " "
            << current
            << " -> "
            << tool.version
            << "\n";

        const std::string release =
            get_release_json();

        if (release.empty())
        {
            std::cout
                << "[ERROR] Unable to contact GitHub.\n";

            return false;
        }

        Asset asset;

        if (!find_asset(
                release,
                tool.file,
                asset))
        {
            std::cout
                << "[ERROR] Asset not found: "
                << tool.file
                << "\n";

            return false;
        }

        Tool updated = tool;

        if (updated.size == 0)
            updated.size = asset.size;

        if (updated.sha256.empty())
            updated.sha256 = asset.digest;

        return install_asset(
            asset,
            updated
        );
    }

    bool update_all()
    {
        if (!ensure_manifest())
            return false;

        std::vector<Tool> tools;

        if (!load_manifest(
                DATA_FILE,
                tools))
        {
            return false;
        }

        std::cout
            << "\n-> Checking installed tools...\n\n";

        bool success = true;

        for (const Tool& tool : tools)
        {
            const std::string current =
                installed_version(
                    tool.name
                );

            if (current.empty())
                continue;

            if (!update_tool(
                    tool,
                    current))
            {
                success = false;
            }
        }

        std::cout << '\n';

        if (success)
            std::cout
                << "[OK] Update completed.\n";
        else
            std::cout
                << "[ERROR] Some updates failed.\n";

        return success;
    }

    bool update_flow()
    {
        std::cout
            << "-> Flow update is not implemented yet.\n";

        return true;
    }

    bool update_libs()
    {
        std::cout
            << "-> Core/library update is not implemented yet.\n";

        return true;
    }

    void install_screen()
    {
        clear_screen();
        print_header();

        std::cout
            << "\nInstall a tool\n\n"
            << "Use:\n"
            << "  flow install <tool>\n";

        wait_for_enter();
    }

    void uninstall_screen()
    {
        clear_screen();
        print_header();

        std::cout
            << "\nUninstall a tool\n\n"
            << "Use:\n"
            << "  flow uninstall <tool>\n";

        wait_for_enter();
    }

    void update_screen()
    {
        const std::vector<std::string> options =
        {
            "Update everything",
            "Update Flow + Core",
            "Update installed tools",
            "Update a specific tool",
            "Back"
        };

        std::size_t selected = 0;

        while (true)
        {
            clear_screen();
            print_header();

            std::cout << '\n';

            for (std::size_t i = 0;
                 i < options.size();
                 ++i)
            {
                std::cout
                    << (i == selected
                            ? "  > "
                            : "    ")
                    << options[i]
                    << '\n';
            }

            print_footer();

            const Key key = read_key();

            if (key == Key::Up)
            {
                if (selected == 0)
                    selected = options.size() - 1;
                else
                    --selected;
            }
            else if (key == Key::Down)
            {
                selected =
                    (selected + 1) %
                    options.size();
            }
            else if (key == Key::Escape)
            {
                return;
            }
            else if (key == Key::Enter)
            {
                if (selected == 0)
                {
                    clear_screen();
                    print_header();
                    update_manifest();
                    update_all();
                    wait_for_enter();
                }
                else if (selected == 1)
                {
                    clear_screen();
                    print_header();
                    update_manifest();
                    update_flow();
                    update_libs();
                    wait_for_enter();
                }
                else if (selected == 2)
                {
                    clear_screen();
                    print_header();
                    update_manifest();
                    update_all();
                    wait_for_enter();
                }
                else if (selected == 3)
                {
                    clear_screen();
                    print_header();

                    std::cout
                        << "\nSpecific tool update is available from:\n"
                        << "  flow update <tool>\n";

                    wait_for_enter();
                }
                else
                {
                    return;
                }
            }
        }
    }

    void system_information()
    {
        clear_screen();
        print_header();

        SYSTEM_INFO info{};

        GetNativeSystemInfo(
            &info
        );

        std::cout
            << "\nSystem information\n\n"
            << "Flow version : "
            << FLOW_VERSION
            << "\n"
            << "Core version : "
            << flow::core::version()
            << "\n"
            << "Architecture : ";

        switch (info.wProcessorArchitecture)
        {
        case PROCESSOR_ARCHITECTURE_AMD64:
            std::cout << "x64";
            break;

        case PROCESSOR_ARCHITECTURE_ARM64:
            std::cout << "ARM64";
            break;

        case PROCESSOR_ARCHITECTURE_INTEL:
            std::cout << "x86";
            break;

        default:
            std::cout << "Unknown";
            break;
        }

        std::cout << '\n';

        wait_for_enter();
    }

    void main_menu()
    {
        const std::vector<std::string> options =
        {
            "Install a tool",
            "Update",
            "Installed tools",
            "Uninstall a tool",
            "System information",
            "Exit"
        };

        std::size_t selected = 0;

        set_cursor_visible(false);

        while (true)
        {
            clear_screen();
            print_header();

            std::cout << '\n';

            for (std::size_t i = 0;
                 i < options.size();
                 ++i)
            {
                std::cout
                    << (i == selected
                            ? "  > "
                            : "    ")
                    << options[i]
                    << '\n';
            }

            print_footer();

            const Key key = read_key();

            if (key == Key::Up)
            {
                if (selected == 0)
                    selected = options.size() - 1;
                else
                    --selected;
            }
            else if (key == Key::Down)
            {
                selected =
                    (selected + 1) %
                    options.size();
            }
            else if (key == Key::Escape)
            {
                break;
            }
            else if (key == Key::Enter)
            {
                switch (selected)
                {
                case 0:
                    install_screen();
                    break;

                case 1:
                    update_screen();
                    break;

                case 2:
                    show_list();
                    break;

                case 3:
                    uninstall_screen();
                    break;

                case 4:
                    system_information();
                    break;

                case 5:
                    set_cursor_visible(true);
                    clear_screen();
                    return;
                }
            }
        }

        set_cursor_visible(true);
        clear_screen();
    }

    int cli(
        int argc,
        char* argv[])
    {
        if (argc == 1)
        {
            main_menu();
            return 0;
        }

        const std::string command =
            argv[1];

        if (command == "--version" ||
            command == "-v" ||
            command == "version")
        {
            std::cout
                << "Flow "
                << FLOW_VERSION
                << '\n';

            return 0;
        }

        if (command == "--help" ||
            command == "-h" ||
            command == "help")
        {
            std::cout
                << "Flow "
                << FLOW_VERSION
                << "\n\n"
                << "Usage:\n"
                << "  flow\n"
                << "  flow install <tool>\n"
                << "  flow update <tool>\n"
                << "  flow update\n"
                << "  flow update --flow\n"
                << "  flow update --libs\n"
                << "  flow uninstall <tool>\n"
                << "  flow list\n"
                << "  flow info <tool>\n"
                << "  flow --version\n"
                << "  flow --help\n";

            return 0;
        }

        if (command == "list")
        {
            show_list();
            return 0;
        }

        if (command == "info")
        {
            if (argc < 3)
            {
                std::cout
                    << "Usage: flow info <tool>\n";

                return 1;
            }

            show_info(argv[2]);
            return 0;
        }

        if (command == "install")
        {
            if (argc < 3)
            {
                std::cout
                    << "Usage: flow install <tool>\n";

                return 1;
            }

            return install_named_tool(
                argv[2],
                true
            )
                ? 0
                : 1;
        }

        if (command == "uninstall")
        {
            if (argc < 3)
            {
                std::cout
                    << "Usage: flow uninstall <tool>\n";

                return 1;
            }

            return uninstall_named_tool(
                argv[2]
            )
                ? 0
                : 1;
        }

        if (command == "update")
        {
            if (argc == 2)
            {
                if (!update_manifest())
                    return 1;

                return update_all()
                    ? 0
                    : 1;
            }

            const std::string target =
                argv[2];

            if (target == "--flow")
            {
                return update_flow()
                    ? 0
                    : 1;
            }

            if (target == "--libs")
            {
                return update_libs()
                    ? 0
                    : 1;
            }

            if (!ensure_manifest())
                return 1;

            std::vector<Tool> tools;

            if (!load_manifest(
                    DATA_FILE,
                    tools))
            {
                return 1;
            }

            Tool tool;

            if (!find_tool(
                    tools,
                    target,
                    tool))
            {
                std::cout
                    << "[ERROR] Package not found: "
                    << target
                    << '\n';

                return 1;
            }

            const std::string current =
                installed_version(
                    target
                );

            if (current.empty())
            {
                std::cout
                    << "[ERROR] "
                    << target
                    << " is not installed.\n";

                return 1;
            }

            return update_tool(
                tool,
                current
            )
                ? 0
                : 1;
        }

        std::cout
            << "Unknown command: "
            << command
            << "\n"
            << "Use 'flow --help' for help.\n";

        return 1;
    }
}

int main(
    int argc,
    char* argv[])
{
    flow::initialize();

    return cli(
        argc,
        argv
    );
}