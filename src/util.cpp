#include "util.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

namespace util
{
std::wstring utf8ToWide(std::string_view text)
{
    if (text.empty())
        return {};

    const int needed = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (needed <= 0)
        return {};

    std::wstring result(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), needed);
    return result;
}

std::string wideToUtf8(std::wstring_view text)
{
    if (text.empty())
        return {};

    const int needed = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0)
        return {};

    std::string result(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), needed, nullptr, nullptr);
    return result;
}

std::string trim(std::string_view text)
{
    std::size_t begin = 0;
    std::size_t end   = text.size();

    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])))
        ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])))
        --end;

    return std::string(text.substr(begin, end - begin));
}

std::vector<std::string> split(std::string_view text, char separator)
{
    std::vector<std::string> result;
    std::size_t              start = 0;

    while (start <= text.size())
    {
        const std::size_t position = text.find(separator, start);
        const std::size_t length   = (position == std::string_view::npos) ? text.size() - start : position - start;
        result.push_back(trim(text.substr(start, length)));

        if (position == std::string_view::npos)
            break;
        start = position + 1;
    }

    return result;
}

bool parseBool(std::string_view text, bool fallback)
{
    const std::string value = trim(text);
    if (value == "1" || value == "true" || value == "yes" || value == "on" || value == "True" || value == "Yes")
        return true;
    if (value == "0" || value == "false" || value == "no" || value == "off" || value == "False" || value == "No")
        return false;
    return fallback;
}

int parseInt(std::string_view text, int fallback)
{
    try
    {
        return std::stoi(trim(text));
    }
    catch (...)
    {
        return fallback;
    }
}

std::string timestampNow()
{
    const std::time_t now = std::time(nullptr);
    std::tm           local{};
#if defined(_MSC_VER)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif

    std::ostringstream out;
    out << std::put_time(&local, "%Y%m%d-%H%M%S");
    return out.str();
}

std::string sanitizeFileName(std::string_view text)
{
    std::string result;
    result.reserve(text.size());

    for (const char character : text)
    {
        const unsigned char value = static_cast<unsigned char>(character);
        if (std::isalnum(value) || character == '-' || character == '_' || character == '.')
            result.push_back(character);
        else if (character == ' ' || character == '/')
            result.push_back('_');
    }

    if (result.empty())
        result = "image";

    return result;
}

bool ensureDirectory(const std::filesystem::path& path, std::string& error)
{
    std::error_code code;
    if (std::filesystem::exists(path, code))
        return true;

    std::filesystem::create_directories(path, code);
    if (code)
    {
        error = "Не удалось создать каталог: " + path.string();
        return false;
    }

    return true;
}

void openFolder(const std::filesystem::path& path)
{
    std::error_code code;
    if (!std::filesystem::exists(path, code))
        std::filesystem::create_directories(path, code);

    ShellExecuteW(nullptr, L"open", path.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::filesystem::path moduleDir()
{
    wchar_t buffer[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return std::filesystem::path(buffer).parent_path();
}

std::filesystem::path resolvePath(const std::filesystem::path& base, const std::filesystem::path& path)
{
    if (path.is_absolute())
        return path;
    return base / path;
}

std::string detectImageExtension(const std::string& bytes, std::string_view fallbackFormat)
{
    if (bytes.size() >= 8 && static_cast<unsigned char>(bytes[0]) == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G')
        return ".png";

    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xFF && static_cast<unsigned char>(bytes[1]) == 0xD8)
        return ".jpg";

    if (bytes.size() >= 12 && bytes.compare(0, 4, "RIFF") == 0 && bytes.compare(8, 4, "WEBP") == 0)
        return ".webp";

    if (bytes.size() >= 6 && (bytes.compare(0, 3, "GIF") == 0))
        return ".gif";

    if (fallbackFormat == "jpeg" || fallbackFormat == "jpg")
        return ".jpg";
    if (fallbackFormat == "webp")
        return ".webp";
    return ".png";
}

void logLine(const std::string& text)
{
    OutputDebugStringW((utf8ToWide(text) + L"\n").c_str());

    static std::mutex     mutex;
    static std::ofstream* file = new std::ofstream(moduleDir() / "nanobanana.log", std::ios::app);

    std::lock_guard<std::mutex> lock(mutex);
    if (file->is_open())
    {
        *file << text << "\n";
        file->flush();
    }
}
} // namespace util
