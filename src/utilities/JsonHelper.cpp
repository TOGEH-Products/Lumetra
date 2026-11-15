#include "utilities/JsonHelper.h"
#include <sstream>
#include <iomanip>
#include <cctype>
#include <windows.h>

namespace prismshift {

namespace {

std::wstring Utf8ToWide(std::string_view utf8Str) {
    if (utf8Str.empty()) return {};
    int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, utf8Str.data(), static_cast<int>(utf8Str.size()), nullptr, 0);
    if (sizeNeeded <= 0) return {};
    std::wstring wide(sizeNeeded, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8Str.data(), static_cast<int>(utf8Str.size()), wide.data(), sizeNeeded);
    return wide;
}

std::string WideToUtf8(std::wstring_view wideStr) {
    if (wideStr.empty()) return {};
    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wideStr.data(), static_cast<int>(wideStr.size()), nullptr, 0, nullptr, nullptr);
    if (sizeNeeded <= 0) return {};
    std::string utf8(sizeNeeded, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wideStr.data(), static_cast<int>(wideStr.size()), utf8.data(), sizeNeeded, nullptr, nullptr);
    return utf8;
}

class JsonParser {
public:
    explicit JsonParser(std::string_view src) : m_src(src), m_pos(0) {}

    std::optional<JsonValue> Parse() {
        SkipWhitespace();
        if (m_pos >= m_src.size()) return std::nullopt;
        auto val = ParseValue();
        SkipWhitespace();
        return val;
    }

private:
    void SkipWhitespace() {
        while (m_pos < m_src.size() && (std::isspace(static_cast<unsigned char>(m_src[m_pos])))) {
            m_pos++;
        }
    }

    char Peek() const {
        return m_pos < m_src.size() ? m_src[m_pos] : '\0';
    }

    char Get() {
        return m_pos < m_src.size() ? m_src[m_pos++] : '\0';
    }

    std::optional<JsonValue> ParseValue() {
        SkipWhitespace();
        char c = Peek();
        if (c == '{') return ParseObject();
        if (c == '[') return ParseArray();
        if (c == '"') return ParseString();
        if (c == 't' || c == 'f') return ParseBool();
        if (c == 'n') return ParseNull();
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return ParseNumber();
        return std::nullopt;
    }

    std::optional<JsonValue> ParseObject() {
        if (Get() != '{') return std::nullopt;
        std::map<std::wstring, JsonValue> obj;
        SkipWhitespace();
        if (Peek() == '}') {
            Get();
            return JsonValue(obj);
        }

        while (m_pos < m_src.size()) {
            SkipWhitespace();
            if (Peek() != '"') return std::nullopt;
            auto keyVal = ParseString();
            if (!keyVal) return std::nullopt;
            std::wstring key = keyVal->AsString();

            SkipWhitespace();
            if (Get() != ':') return std::nullopt;

            auto val = ParseValue();
            if (!val) return std::nullopt;
            obj[std::move(key)] = std::move(*val);

            SkipWhitespace();
            char next = Get();
            if (next == '}') return JsonValue(obj);
            if (next != ',') return std::nullopt;
        }
        return std::nullopt;
    }

    std::optional<JsonValue> ParseArray() {
        if (Get() != '[') return std::nullopt;
        std::vector<JsonValue> arr;
        SkipWhitespace();
        if (Peek() == ']') {
            Get();
            return JsonValue(arr);
        }

        while (m_pos < m_src.size()) {
            auto val = ParseValue();
            if (!val) return std::nullopt;
            arr.push_back(std::move(*val));

            SkipWhitespace();
            char next = Get();
            if (next == ']') return JsonValue(arr);
            if (next != ',') return std::nullopt;
        }
        return std::nullopt;
    }

    std::optional<JsonValue> ParseString() {
        if (Get() != '"') return std::nullopt;
        std::string s;
        while (m_pos < m_src.size()) {
            char c = Get();
            if (c == '"') {
                return JsonValue(Utf8ToWide(s));
            }
            if (c == '\\') {
                if (m_pos >= m_src.size()) return std::nullopt;
                char esc = Get();
                switch (esc) {
                    case '"':  s.push_back('"'); break;
                    case '\\': s.push_back('\\'); break;
                    case '/':  s.push_back('/'); break;
                    case 'b':  s.push_back('\b'); break;
                    case 'f':  s.push_back('\f'); break;
                    case 'n':  s.push_back('\n'); break;
                    case 'r':  s.push_back('\r'); break;
                    case 't':  s.push_back('\t'); break;
                    default:   s.push_back(esc); break;
                }
            } else {
                s.push_back(c);
            }
        }
        return std::nullopt;
    }

    std::optional<JsonValue> ParseNumber() {
        size_t start = m_pos;
        if (Peek() == '-') m_pos++;
        while (m_pos < m_src.size() && (std::isdigit(static_cast<unsigned char>(m_src[m_pos])) || m_src[m_pos] == '.' || m_src[m_pos] == 'e' || m_src[m_pos] == 'E' || m_src[m_pos] == '+' || m_src[m_pos] == '-')) {
            m_pos++;
        }
        std::string numStr(m_src.substr(start, m_pos - start));
        try {
            double d = std::stod(numStr);
            return JsonValue(d);
        } catch (...) {
            return std::nullopt;
        }
    }

    std::optional<JsonValue> ParseBool() {
        if (m_src.substr(m_pos, 4) == "true") {
            m_pos += 4;
            return JsonValue(true);
        }
        if (m_src.substr(m_pos, 5) == "false") {
            m_pos += 5;
            return JsonValue(false);
        }
        return std::nullopt;
    }

    std::optional<JsonValue> ParseNull() {
        if (m_src.substr(m_pos, 4) == "null") {
            m_pos += 4;
            return JsonValue();
        }
        return std::nullopt;
    }

    std::string_view m_src;
    size_t m_pos;
};

void EscapeString(std::string_view in, std::string& out) {
    out.push_back('"');
    for (char c : in) {
        switch (c) {
            case '"':  out.append("\\\""); break;
            case '\\': out.append("\\\\"); break;
            case '\b': out.append("\\b"); break;
            case '\f': out.append("\\f"); break;
            case '\n': out.append("\\n"); break;
            case '\r': out.append("\\r"); break;
            case '\t': out.append("\\t"); break;
            default:   out.push_back(c); break;
        }
    }
    out.push_back('"');
}

void SerializeValue(const JsonValue& val, std::string& out, int indent, int level) {
    std::string ind(level * indent, ' ');
    std::string nextInd((level + 1) * indent, ' ');

    switch (val.GetType()) {
        case JsonType::Null:
            out.append("null");
            break;
        case JsonType::Boolean:
            out.append(val.AsBool() ? "true" : "false");
            break;
        case JsonType::Number: {
            std::ostringstream ss;
            ss << std::setprecision(4) << val.AsDouble();
            out.append(ss.str());
            break;
        }
        case JsonType::String: {
            std::string utf8 = WideToUtf8(val.AsString());
            EscapeString(utf8, out);
            break;
        }
        case JsonType::Array: {
            const auto& arr = val.AsArray();
            if (arr.empty()) {
                out.append("[]");
                break;
            }
            out.append("[\n");
            for (size_t i = 0; i < arr.size(); ++i) {
                out.append(nextInd);
                SerializeValue(arr[i], out, indent, level + 1);
                if (i + 1 < arr.size()) out.push_back(',');
                out.push_back('\n');
            }
            out.append(ind).append("]");
            break;
        }
        case JsonType::Object: {
            const auto& obj = val.AsObject();
            if (obj.empty()) {
                out.append("{}");
                break;
            }
            out.append("{\n");
            size_t idx = 0;
            for (const auto& [k, v] : obj) {
                out.append(nextInd);
                std::string kUtf8 = WideToUtf8(k);
                EscapeString(kUtf8, out);
                out.append(": ");
                SerializeValue(v, out, indent, level + 1);
                if (++idx < obj.size()) out.push_back(',');
                out.push_back('\n');
            }
            out.append(ind).append("}");
            break;
        }
    }
}

} // namespace

JsonValue::JsonValue(std::string_view val)
    : m_type(JsonType::String), m_strVal(Utf8ToWide(val)) {
}

std::optional<JsonValue> JsonValue::Parse(std::string_view json) {
    JsonParser parser(json);
    return parser.Parse();
}

std::string JsonValue::Serialize(int indent) const {
    std::string out;
    SerializeValue(*this, out, indent, 0);
    return out;
}

} // namespace prismshift
