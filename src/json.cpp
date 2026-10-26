#include "json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace json
{
namespace
{
class Parser
{
public:
    Parser(std::string_view text) : m_text(text) {}

    bool parseValue(Value& out, std::string& error)
    {
        skipWhitespace();
        if (m_position >= m_text.size())
        {
            error = "Неожиданный конец JSON";
            return false;
        }

        const char current = m_text[m_position];
        switch (current)
        {
            case '{': return parseObject(out, error);
            case '[': return parseArray(out, error);
            case '"': return parseStringValue(out, error);
            case 't':
            case 'f': return parseBool(out, error);
            case 'n': return parseNull(out, error);
            default: return parseNumber(out, error);
        }
    }

    bool atEnd() const
    {
        std::size_t position = m_position;
        while (position < m_text.size() && std::isspace(static_cast<unsigned char>(m_text[position])))
            ++position;
        return position >= m_text.size();
    }

    std::string errorAt(std::string_view message) const
    {
        return std::string(message) + " (позиция " + std::to_string(m_position) + ")";
    }

private:
    void skipWhitespace()
    {
        while (m_position < m_text.size() && std::isspace(static_cast<unsigned char>(m_text[m_position])))
            ++m_position;
    }

    bool consume(char expected)
    {
        skipWhitespace();
        if (m_position < m_text.size() && m_text[m_position] == expected)
        {
            ++m_position;
            return true;
        }
        return false;
    }

    bool parseObject(Value& out, std::string& error)
    {
        consume('{');
        Value::Object object;

        skipWhitespace();
        if (m_position < m_text.size() && m_text[m_position] == '}')
        {
            ++m_position;
            out = Value(std::move(object));
            return true;
        }

        while (true)
        {
            skipWhitespace();
            if (m_position >= m_text.size() || m_text[m_position] != '"')
            {
                error = errorAt("Ожидалось имя ключа");
                return false;
            }

            std::string key;
            if (!parseString(key, error))
                return false;

            if (!consume(':'))
            {
                error = errorAt("Ожидалось ':'");
                return false;
            }

            Value value;
            if (!parseValue(value, error))
                return false;

            object.emplace(std::move(key), std::move(value));

            skipWhitespace();
            if (consume(','))
                continue;

            if (consume('}'))
                break;

            error = errorAt("Ожидалось ',' или '}'");
            return false;
        }

        out = Value(std::move(object));
        return true;
    }

    bool parseArray(Value& out, std::string& error)
    {
        consume('[');
        Value::Array array;

        skipWhitespace();
        if (m_position < m_text.size() && m_text[m_position] == ']')
        {
            ++m_position;
            out = Value(std::move(array));
            return true;
        }

        while (true)
        {
            Value value;
            if (!parseValue(value, error))
                return false;
            array.push_back(std::move(value));

            skipWhitespace();
            if (consume(','))
                continue;

            if (consume(']'))
                break;

            error = errorAt("Ожидалось ',' или ']'");
            return false;
        }

        out = Value(std::move(array));
        return true;
    }

    bool parseStringValue(Value& out, std::string& error)
    {
        std::string value;
        if (!parseString(value, error))
            return false;
        out = Value(std::move(value));
        return true;
    }

    static void appendUtf8(std::string& out, unsigned int codePoint)
    {
        if (codePoint <= 0x7F)
        {
            out.push_back(static_cast<char>(codePoint));
        }
        else if (codePoint <= 0x7FF)
        {
            out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
            out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        }
        else if (codePoint <= 0xFFFF)
        {
            out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
            out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        }
        else
        {
            out.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
            out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        }
    }

    bool parseHex4(unsigned int& value, std::string& error)
    {
        if (m_position + 4 > m_text.size())
        {
            error = errorAt("Незавершённый \\u escape");
            return false;
        }

        value = 0;
        for (int i = 0; i < 4; ++i)
        {
            const char digit = m_text[m_position++];
            value <<= 4;
            if (digit >= '0' && digit <= '9')
                value |= static_cast<unsigned int>(digit - '0');
            else if (digit >= 'a' && digit <= 'f')
                value |= static_cast<unsigned int>(digit - 'a' + 10);
            else if (digit >= 'A' && digit <= 'F')
                value |= static_cast<unsigned int>(digit - 'A' + 10);
            else
            {
                error = errorAt("Некорректная шестнадцатеричная цифра");
                return false;
            }
        }
        return true;
    }

    bool parseString(std::string& out, std::string& error)
    {
        if (!consume('"'))
        {
            error = errorAt("Ожидалось '\"'");
            return false;
        }

        out.clear();
        while (m_position < m_text.size())
        {
            const char current = m_text[m_position++];
            if (current == '"')
                return true;

            if (current != '\\')
            {
                out.push_back(current);
                continue;
            }

            if (m_position >= m_text.size())
            {
                error = errorAt("Незавершённый escape");
                return false;
            }

            const char escaped = m_text[m_position++];
            switch (escaped)
            {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u':
                {
                    unsigned int codePoint = 0;
                    if (!parseHex4(codePoint, error))
                        return false;

                    if (codePoint >= 0xD800 && codePoint <= 0xDBFF)
                    {
                        if (m_position + 2 <= m_text.size() && m_text[m_position] == '\\' && m_text[m_position + 1] == 'u')
                        {
                            m_position += 2;
                            unsigned int low = 0;
                            if (!parseHex4(low, error))
                                return false;

                            if (low >= 0xDC00 && low <= 0xDFFF)
                                codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (low - 0xDC00);
                            else
                                codePoint = 0xFFFD;
                        }
                        else
                        {
                            codePoint = 0xFFFD;
                        }
                    }
                    else if (codePoint >= 0xDC00 && codePoint <= 0xDFFF)
                    {
                        codePoint = 0xFFFD;
                    }

                    appendUtf8(out, codePoint);
                    break;
                }
                default:
                    error = errorAt("Неизвестный escape");
                    return false;
            }
        }

        error = errorAt("Незавершённая строка");
        return false;
    }

    bool parseNumber(Value& out, std::string& error)
    {
        const std::size_t start = m_position;
        if (m_position < m_text.size() && (m_text[m_position] == '-' || m_text[m_position] == '+'))
            ++m_position;

        bool hasDigits = false;
        while (m_position < m_text.size() && std::isdigit(static_cast<unsigned char>(m_text[m_position])))
        {
            ++m_position;
            hasDigits = true;
        }

        if (m_position < m_text.size() && m_text[m_position] == '.')
        {
            ++m_position;
            while (m_position < m_text.size() && std::isdigit(static_cast<unsigned char>(m_text[m_position])))
                ++m_position;
        }

        if (m_position < m_text.size() && (m_text[m_position] == 'e' || m_text[m_position] == 'E'))
        {
            ++m_position;
            if (m_position < m_text.size() && (m_text[m_position] == '-' || m_text[m_position] == '+'))
                ++m_position;
            while (m_position < m_text.size() && std::isdigit(static_cast<unsigned char>(m_text[m_position])))
                ++m_position;
        }

        if (!hasDigits)
        {
            error = errorAt("Некорректное число");
            return false;
        }

        out = Value(std::strtod(std::string(m_text.substr(start, m_position - start)).c_str(), nullptr));
        return true;
    }

    bool parseBool(Value& out, std::string& error)
    {
        if (m_text.compare(m_position, 4, "true") == 0)
        {
            m_position += 4;
            out = Value(true);
            return true;
        }
        if (m_text.compare(m_position, 5, "false") == 0)
        {
            m_position += 5;
            out = Value(false);
            return true;
        }

        error = errorAt("Некорректный литерал");
        return false;
    }

    bool parseNull(Value& out, std::string& error)
    {
        if (m_text.compare(m_position, 4, "null") == 0)
        {
            m_position += 4;
            out = Value();
            return true;
        }

        error = errorAt("Некорректный литерал");
        return false;
    }

    std::string_view m_text;
    std::size_t      m_position = 0;
};
} // namespace

Value::Value() = default;
Value::Value(bool value) : m_type(Type::Bool), m_bool(value) {}
Value::Value(double value) : m_type(Type::Number), m_number(value) {}
Value::Value(int value) : m_type(Type::Number), m_number(static_cast<double>(value)) {}
Value::Value(const char* value) : m_type(Type::String), m_string(value ? value : "") {}
Value::Value(std::string value) : m_type(Type::String), m_string(std::move(value)) {}
Value::Value(Array value) : m_type(Type::Array), m_array(std::move(value)) {}
Value::Value(Object value) : m_type(Type::Object), m_object(std::move(value)) {}

Value Value::makeArray()
{
    return Value(Array{});
}

Value Value::makeObject()
{
    return Value(Object{});
}

bool Value::asBool(bool fallback) const
{
    return m_type == Type::Bool ? m_bool : fallback;
}

double Value::asNumber(double fallback) const
{
    return m_type == Type::Number ? m_number : fallback;
}

const std::string& Value::asString() const
{
    static const std::string empty;
    return m_type == Type::String ? m_string : empty;
}

const Value::Array& Value::asArray() const
{
    static const Array empty;
    return m_type == Type::Array ? m_array : empty;
}

Value::Array& Value::asArray()
{
    return m_array;
}

const Value::Object& Value::asObject() const
{
    static const Object empty;
    return m_type == Type::Object ? m_object : empty;
}

Value::Object& Value::asObject()
{
    return m_object;
}

const Value* Value::find(std::string_view key) const
{
    if (m_type != Type::Object)
        return nullptr;

    const auto iterator = m_object.find(std::string(key));
    return iterator == m_object.end() ? nullptr : &iterator->second;
}

Value* Value::find(std::string_view key)
{
    if (m_type != Type::Object)
        return nullptr;

    const auto iterator = m_object.find(std::string(key));
    return iterator == m_object.end() ? nullptr : &iterator->second;
}

Value& Value::operator[](std::string key)
{
    if (m_type != Type::Object)
    {
        m_type = Type::Object;
        m_object.clear();
    }
    return m_object[std::move(key)];
}

const Value& Value::operator[](std::string_view key) const
{
    static const Value nullValue;
    const Value*       found = find(key);
    return found ? *found : nullValue;
}

std::string Value::dump(int indent) const
{
    std::string out;
    dumpTo(out, indent, 0);
    return out;
}

void Value::dumpTo(std::string& out, int indent, int depth) const
{
    const auto newline = [&](int currentDepth)
    {
        if (indent < 0)
            return;
        out.push_back('\n');
        out.append(static_cast<std::size_t>(indent * currentDepth), ' ');
    };

    switch (m_type)
    {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += m_bool ? "true" : "false"; break;
        case Type::Number:
        {
            if (std::isfinite(m_number) && std::abs(m_number - std::floor(m_number)) < 1e-9 && std::abs(m_number) < 1e15)
            {
                out += std::to_string(static_cast<long long>(m_number));
            }
            else
            {
                char buffer[64];
                std::snprintf(buffer, sizeof(buffer), "%.17g", m_number);
                out += buffer;
            }
            break;
        }
        case Type::String: out += '"'; out += escape(m_string); out += '"'; break;
        case Type::Array:
        {
            out.push_back('[');
            bool first = true;
            for (const auto& item : m_array)
            {
                if (!first)
                    out.push_back(',');
                first = false;
                newline(depth + 1);
                item.dumpTo(out, indent, depth + 1);
            }
            if (!m_array.empty())
                newline(depth);
            out.push_back(']');
            break;
        }
        case Type::Object:
        {
            out.push_back('{');
            bool first = true;
            for (const auto& [key, item] : m_object)
            {
                if (!first)
                    out.push_back(',');
                first = false;
                newline(depth + 1);
                out.push_back('"');
                out += escape(key);
                out += indent >= 0 ? "\": " : "\":";
                item.dumpTo(out, indent, depth + 1);
            }
            if (!m_object.empty())
                newline(depth);
            out.push_back('}');
            break;
        }
    }
}

std::string escape(std::string_view text)
{
    std::string out;
    out.reserve(text.size() + 8);

    for (const char character : text)
    {
        const unsigned char value = static_cast<unsigned char>(character);
        switch (character)
        {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (value < 0x20)
                {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", value);
                    out += buffer;
                }
                else
                {
                    out.push_back(character);
                }
                break;
        }
    }

    return out;
}

bool parse(std::string_view text, Value& out, std::string& error)
{
    Parser parser(text);
    if (!parser.parseValue(out, error))
        return false;

    if (!parser.atEnd())
    {
        error = "Лишние данные после JSON";
        return false;
    }

    return true;
}
} // namespace json
