#include "base64.h"

namespace base64
{
namespace
{
constexpr const char* Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int decodeChar(char character)
{
    if (character >= 'A' && character <= 'Z')
        return character - 'A';
    if (character >= 'a' && character <= 'z')
        return character - 'a' + 26;
    if (character >= '0' && character <= '9')
        return character - '0' + 52;
    if (character == '+')
        return 62;
    if (character == '/')
        return 63;
    return -1;
}
} // namespace

std::string encode(std::string_view data)
{
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);

    std::size_t position = 0;
    while (position + 2 < data.size())
    {
        const unsigned int block = (static_cast<unsigned char>(data[position]) << 16) |
                                   (static_cast<unsigned char>(data[position + 1]) << 8) |
                                   static_cast<unsigned char>(data[position + 2]);
        out.push_back(Alphabet[(block >> 18) & 0x3F]);
        out.push_back(Alphabet[(block >> 12) & 0x3F]);
        out.push_back(Alphabet[(block >> 6) & 0x3F]);
        out.push_back(Alphabet[block & 0x3F]);
        position += 3;
    }

    const std::size_t remaining = data.size() - position;
    if (remaining == 1)
    {
        const unsigned int block = static_cast<unsigned char>(data[position]) << 16;
        out.push_back(Alphabet[(block >> 18) & 0x3F]);
        out.push_back(Alphabet[(block >> 12) & 0x3F]);
        out += "==";
    }
    else if (remaining == 2)
    {
        const unsigned int block = (static_cast<unsigned char>(data[position]) << 16) |
                                   (static_cast<unsigned char>(data[position + 1]) << 8);
        out.push_back(Alphabet[(block >> 18) & 0x3F]);
        out.push_back(Alphabet[(block >> 12) & 0x3F]);
        out.push_back(Alphabet[(block >> 6) & 0x3F]);
        out.push_back('=');
    }

    return out;
}

bool decode(std::string_view text, std::string& out)
{
    out.clear();
    out.reserve((text.size() / 4) * 3);

    unsigned int accumulator = 0;
    int          bits        = 0;

    for (const char character : text)
    {
        if (character == '=' || character == '\n' || character == '\r' || character == ' ' || character == '\t')
            continue;

        const int value = decodeChar(character);
        if (value < 0)
            return false;

        accumulator = (accumulator << 6) | static_cast<unsigned int>(value);
        bits += 6;

        if (bits >= 8)
        {
            bits -= 8;
            out.push_back(static_cast<char>((accumulator >> bits) & 0xFF));
        }
    }

    return true;
}
} // namespace base64
