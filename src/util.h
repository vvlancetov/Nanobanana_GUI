#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace util
{
std::wstring utf8ToWide(std::string_view text);
std::string  wideToUtf8(std::wstring_view text);

std::string              trim(std::string_view text);
std::vector<std::string> split(std::string_view text, char separator);
bool                     parseBool(std::string_view text, bool fallback);
int                      parseInt(std::string_view text, int fallback);

std::string timestampNow();
std::string sanitizeFileName(std::string_view text);

bool         ensureDirectory(const std::filesystem::path& path, std::string& error);
void         openFolder(const std::filesystem::path& path);
std::filesystem::path moduleDir();
std::filesystem::path resolvePath(const std::filesystem::path& base, const std::filesystem::path& path);

std::string detectImageExtension(const std::string& bytes, std::string_view fallbackFormat);

void logLine(const std::string& text);
} // namespace util
