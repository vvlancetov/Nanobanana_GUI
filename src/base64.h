#pragma once

#include <string>
#include <string_view>

namespace base64
{
std::string encode(std::string_view data);
bool        decode(std::string_view text, std::string& out);
} // namespace base64
