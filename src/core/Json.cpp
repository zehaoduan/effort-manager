#include "Json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace effort {

namespace {

const Json& nullValue() {
    static const Json instance;
    return instance;
}

void appendUtf8(std::string& out, unsigned codePoint) {
    if (codePoint < 0x80) {
        out += static_cast<char>(codePoint);
    } else if (codePoint < 0x800) {
        out += static_cast<char>(0xC0 | (codePoint >> 6));
        out += static_cast<char>(0x80 | (codePoint & 0x3F));
    } else if (codePoint < 0x10000) {
        out += static_cast<char>(0xE0 | (codePoint >> 12));
        out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codePoint & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (codePoint >> 18));
        out += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codePoint & 0x3F));
    }
}

void dumpString(const std::string& s, std::string& out) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
}

void newline(int indent, int depth, std::string& out) {
    if (indent <= 0) return;
    out += '\n';
    out.append(static_cast<std::size_t>(indent * depth), ' ');
}

void dumpValue(const Json& value, int indent, int depth, std::string& out) {
    switch (value.type()) {
        case Json::Type::Null:
            out += "null";
            break;
        case Json::Type::Bool:
            out += value.toBool() ? "true" : "false";
            break;
        case Json::Type::Number: {
            const double n = value.toNumber();
            if (!std::isfinite(n)) {
                out += "null";
            } else if (n == std::floor(n) && std::fabs(n) < 9007199254740992.0) {
                out += std::to_string(static_cast<long long>(n));
            } else {
                char buf[32];
                std::snprintf(buf, sizeof buf, "%.17g", n);
                out += buf;
            }
            break;
        }
        case Json::Type::String:
            dumpString(value.toString(), out);
            break;
        case Json::Type::Array: {
            const auto& items = value.elements();
            if (items.empty()) {
                out += "[]";
                break;
            }
            out += '[';
            for (std::size_t i = 0; i < items.size(); ++i) {
                if (i > 0) out += ',';
                newline(indent, depth + 1, out);
                dumpValue(items[i], indent, depth + 1, out);
            }
            newline(indent, depth, out);
            out += ']';
            break;
        }
        case Json::Type::Object: {
            const auto& members = value.members();
            if (members.empty()) {
                out += "{}";
                break;
            }
            out += '{';
            bool first = true;
            for (const auto& [key, member] : members) {
                if (!first) out += ',';
                first = false;
                newline(indent, depth + 1, out);
                dumpString(key, out);
                out += indent > 0 ? ": " : ":";
                dumpValue(member, indent, depth + 1, out);
            }
            newline(indent, depth, out);
            out += '}';
            break;
        }
    }
}

class Parser {
public:
    explicit Parser(const std::string& text) : text_(text) {}

    bool parseDocument(Json& out) {
        if (!parseValue(out)) return false;
        skipWhitespace();
        if (pos_ != text_.size()) return fail("trailing characters");
        return true;
    }

    const std::string& error() const { return error_; }

private:
    bool fail(const char* message) {
        if (error_.empty()) error_ = std::string(message) + " at offset " + std::to_string(pos_);
        return false;
    }

    void skipWhitespace() {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
            ++pos_;
        }
    }

    bool parseValue(Json& out) {
        skipWhitespace();
        if (pos_ >= text_.size()) return fail("unexpected end of input");
        const char c = text_[pos_];
        if (c == '{') return parseObject(out);
        if (c == '[') return parseArray(out);
        if (c == '"') {
            std::string s;
            if (!parseString(s)) return false;
            out = Json(std::move(s));
            return true;
        }
        if (c == 't') return parseLiteral("true", Json(true), out);
        if (c == 'f') return parseLiteral("false", Json(false), out);
        if (c == 'n') return parseLiteral("null", Json(nullptr), out);
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumber(out);
        return fail("unexpected character");
    }

    bool parseLiteral(const char* literal, Json value, Json& out) {
        const std::size_t n = std::strlen(literal);
        if (text_.compare(pos_, n, literal) != 0) return fail("invalid literal");
        pos_ += n;
        out = std::move(value);
        return true;
    }

    bool parseNumber(Json& out) {
        const std::size_t start = pos_;
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            const bool numberChar = (c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E';
            if (!numberChar) break;
            ++pos_;
        }
        const std::string token = text_.substr(start, pos_ - start);
        char* end = nullptr;
        const double value = std::strtod(token.c_str(), &end);
        if (token.empty() || end != token.c_str() + token.size()) return fail("invalid number");
        out = Json(value);
        return true;
    }

    bool parseHex4(unsigned& out) {
        if (pos_ + 4 > text_.size()) return fail("truncated \\u escape");
        out = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[pos_++];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') out |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= static_cast<unsigned>(c - 'A' + 10);
            else return fail("invalid hex digit in \\u escape");
        }
        return true;
    }

    bool parseString(std::string& out) {
        ++pos_;  // opening quote
        while (true) {
            if (pos_ >= text_.size()) return fail("unterminated string");
            const char c = text_[pos_++];
            if (c == '"') return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (pos_ >= text_.size()) return fail("unterminated escape");
            const char e = text_[pos_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned codePoint = 0;
                    if (!parseHex4(codePoint)) return false;
                    if (codePoint >= 0xD800 && codePoint <= 0xDBFF && pos_ + 6 <= text_.size() &&
                        text_[pos_] == '\\' && text_[pos_ + 1] == 'u') {
                        const std::size_t save = pos_;
                        pos_ += 2;
                        unsigned low = 0;
                        if (parseHex4(low) && low >= 0xDC00 && low <= 0xDFFF) {
                            codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (low - 0xDC00);
                        } else {
                            pos_ = save;
                            error_.clear();
                        }
                    }
                    appendUtf8(out, codePoint);
                    break;
                }
                default:
                    return fail("invalid escape");
            }
        }
    }

    bool parseArray(Json& out) {
        ++pos_;  // '['
        out = Json::array();
        skipWhitespace();
        if (pos_ < text_.size() && text_[pos_] == ']') {
            ++pos_;
            return true;
        }
        while (true) {
            Json item;
            if (!parseValue(item)) return false;
            out.push(std::move(item));
            skipWhitespace();
            if (pos_ >= text_.size()) return fail("unterminated array");
            const char c = text_[pos_++];
            if (c == ',') continue;
            if (c == ']') return true;
            return fail("expected ',' or ']'");
        }
    }

    bool parseObject(Json& out) {
        ++pos_;  // '{'
        out = Json::object();
        skipWhitespace();
        if (pos_ < text_.size() && text_[pos_] == '}') {
            ++pos_;
            return true;
        }
        while (true) {
            skipWhitespace();
            if (pos_ >= text_.size() || text_[pos_] != '"') return fail("expected object key");
            std::string key;
            if (!parseString(key)) return false;
            skipWhitespace();
            if (pos_ >= text_.size() || text_[pos_] != ':') return fail("expected ':'");
            ++pos_;
            Json value;
            if (!parseValue(value)) return false;
            out.set(key, std::move(value));
            skipWhitespace();
            if (pos_ >= text_.size()) return fail("unterminated object");
            const char c = text_[pos_++];
            if (c == ',') continue;
            if (c == '}') return true;
            return fail("expected ',' or '}'");
        }
    }

    const std::string& text_;
    std::size_t pos_ = 0;
    std::string error_;
};

}  // namespace

Json Json::array() {
    Json j;
    j.type_ = Type::Array;
    return j;
}

Json Json::object() {
    Json j;
    j.type_ = Type::Object;
    return j;
}

std::int64_t Json::toInt(std::int64_t fallback) const {
    if (!isNumber() || !std::isfinite(number_)) return fallback;
    return static_cast<std::int64_t>(std::llround(number_));
}

Json& Json::push(Json value) {
    if (!isArray()) {
        *this = array();
    }
    array_.push_back(std::move(value));
    return *this;
}

std::size_t Json::size() const {
    if (isArray()) return array_.size();
    if (isObject()) return object_.size();
    return 0;
}

const Json& Json::at(std::size_t index) const {
    if (!isArray() || index >= array_.size()) return nullValue();
    return array_[index];
}

Json& Json::set(const std::string& key, Json value) {
    if (!isObject()) {
        *this = object();
    }
    object_[key] = std::move(value);
    return *this;
}

const Json& Json::get(const std::string& key) const {
    if (!isObject()) return nullValue();
    const auto it = object_.find(key);
    return it == object_.end() ? nullValue() : it->second;
}

bool Json::contains(const std::string& key) const {
    return isObject() && object_.count(key) > 0;
}

std::string Json::dump(int indent) const {
    std::string out;
    dumpValue(*this, indent, 0, out);
    return out;
}

std::optional<Json> Json::parse(const std::string& text, std::string* error) {
    Parser parser(text);
    Json result;
    if (!parser.parseDocument(result)) {
        if (error) *error = parser.error();
        return std::nullopt;
    }
    return result;
}

}  // namespace effort
