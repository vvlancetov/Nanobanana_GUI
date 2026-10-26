#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace json
{
class Value
{
public:
    enum class Type
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    using Array  = std::vector<Value>;
    using Object = std::map<std::string, Value>;

    Value();
    Value(bool value);
    Value(double value);
    Value(int value);
    Value(const char* value);
    Value(std::string value);
    Value(Array value);
    Value(Object value);

    static Value makeArray();
    static Value makeObject();

    [[nodiscard]] Type type() const { return m_type; }
    [[nodiscard]] bool isNull() const { return m_type == Type::Null; }
    [[nodiscard]] bool isArray() const { return m_type == Type::Array; }
    [[nodiscard]] bool isObject() const { return m_type == Type::Object; }

    [[nodiscard]] bool               asBool(bool fallback = false) const;
    [[nodiscard]] double             asNumber(double fallback = 0.0) const;
    [[nodiscard]] const std::string& asString() const;
    [[nodiscard]] const Array&       asArray() const;
    [[nodiscard]] Array&             asArray();
    [[nodiscard]] const Object&      asObject() const;
    [[nodiscard]] Object&            asObject();

    [[nodiscard]] const Value* find(std::string_view key) const;
    [[nodiscard]] Value*       find(std::string_view key);
    [[nodiscard]] bool         contains(std::string_view key) const { return find(key) != nullptr; }

    Value&       operator[](std::string key);
    const Value& operator[](std::string_view key) const;

    [[nodiscard]] std::string dump(int indent = -1) const;

private:
    void dumpTo(std::string& out, int indent, int depth) const;

    Type                   m_type = Type::Null;
    bool                   m_bool = false;
    double                 m_number = 0.0;
    std::string            m_string;
    Array                  m_array;
    Object                 m_object;
};

bool parse(std::string_view text, Value& out, std::string& error);

std::string escape(std::string_view text);
} // namespace json
