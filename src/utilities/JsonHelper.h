#pragma once

#include <string>
#include <vector>
#include <map>
#include <string_view>
#include <optional>

namespace prismshift {

enum class JsonType {
    Null,
    Boolean,
    Number,
    String,
    Array,
    Object
};

class JsonValue {
public:
    JsonValue() : m_type(JsonType::Null) {}
    explicit JsonValue(bool val) : m_type(JsonType::Boolean), m_boolVal(val) {}
    explicit JsonValue(double val) : m_type(JsonType::Number), m_numVal(val) {}
    explicit JsonValue(int val) : m_type(JsonType::Number), m_numVal(static_cast<double>(val)) {}
    explicit JsonValue(std::wstring val) : m_type(JsonType::String), m_strVal(std::move(val)) {}
    explicit JsonValue(std::string_view val);
    explicit JsonValue(std::vector<JsonValue> val) : m_type(JsonType::Array), m_arrVal(std::move(val)) {}
    explicit JsonValue(std::map<std::wstring, JsonValue> val) : m_type(JsonType::Object), m_objVal(std::move(val)) {}

    [[nodiscard]] JsonType GetType() const { return m_type; }
    [[nodiscard]] bool IsNull() const { return m_type == JsonType::Null; }
    [[nodiscard]] bool IsBool() const { return m_type == JsonType::Boolean; }
    [[nodiscard]] bool IsNumber() const { return m_type == JsonType::Number; }
    [[nodiscard]] bool IsString() const { return m_type == JsonType::String; }
    [[nodiscard]] bool IsArray() const { return m_type == JsonType::Array; }
    [[nodiscard]] bool IsObject() const { return m_type == JsonType::Object; }

    [[nodiscard]] bool AsBool(bool defaultVal = false) const { return m_type == JsonType::Boolean ? m_boolVal : defaultVal; }
    [[nodiscard]] double AsDouble(double defaultVal = 0.0) const { return m_type == JsonType::Number ? m_numVal : defaultVal; }
    [[nodiscard]] int AsInt(int defaultVal = 0) const { return m_type == JsonType::Number ? static_cast<int>(m_numVal) : defaultVal; }
    [[nodiscard]] const std::wstring& AsString() const { return m_strVal; }
    [[nodiscard]] const std::vector<JsonValue>& AsArray() const { return m_arrVal; }
    [[nodiscard]] const std::map<std::wstring, JsonValue>& AsObject() const { return m_objVal; }

    [[nodiscard]] bool Has(const std::wstring& key) const {
        return m_type == JsonType::Object && m_objVal.find(key) != m_objVal.end();
    }

    [[nodiscard]] const JsonValue* Get(const std::wstring& key) const {
        if (m_type != JsonType::Object) return nullptr;
        auto it = m_objVal.find(key);
        return it != m_objVal.end() ? &it->second : nullptr;
    }

    void Set(std::wstring key, JsonValue value) {
        m_type = JsonType::Object;
        m_objVal[std::move(key)] = std::move(value);
    }

    void Append(JsonValue value) {
        m_type = JsonType::Array;
        m_arrVal.push_back(std::move(value));
    }

    static std::optional<JsonValue> Parse(std::string_view json);
    [[nodiscard]] std::string Serialize(int indent = 2) const;

private:
    JsonType m_type = JsonType::Null;
    bool m_boolVal = false;
    double m_numVal = 0.0;
    std::wstring m_strVal;
    std::vector<JsonValue> m_arrVal;
    std::map<std::wstring, JsonValue> m_objVal;
};

} // namespace prismshift
