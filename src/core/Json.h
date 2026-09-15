// Minimal JSON value with parser and serializer, sufficient for the app's state file.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace effort {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool value) : type_(Type::Bool), bool_(value) {}
    Json(double value) : type_(Type::Number), number_(value) {}
    Json(int value) : Json(static_cast<double>(value)) {}
    Json(long value) : Json(static_cast<double>(value)) {}
    Json(long long value) : Json(static_cast<double>(value)) {}
    Json(unsigned value) : Json(static_cast<double>(value)) {}
    Json(unsigned long value) : Json(static_cast<double>(value)) {}
    Json(unsigned long long value) : Json(static_cast<double>(value)) {}
    Json(const char* value) : type_(Type::String), string_(value) {}
    Json(std::string value) : type_(Type::String), string_(std::move(value)) {}

    static Json array();
    static Json object();

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    bool toBool(bool fallback = false) const { return isBool() ? bool_ : fallback; }
    double toNumber(double fallback = 0.0) const { return isNumber() ? number_ : fallback; }
    std::int64_t toInt(std::int64_t fallback = 0) const;
    std::string toString(const std::string& fallback = std::string()) const {
        return isString() ? string_ : fallback;
    }

    // Arrays
    Json& push(Json value);
    std::size_t size() const;                 // element count for arrays, member count for objects
    const Json& at(std::size_t index) const;  // null value when out of range
    const std::vector<Json>& elements() const { return array_; }

    // Objects
    Json& set(const std::string& key, Json value);
    const Json& get(const std::string& key) const;  // null value when absent
    bool contains(const std::string& key) const;
    const std::map<std::string, Json>& members() const { return object_; }

    // indent <= 0 produces a single line.
    std::string dump(int indent = 2) const;
    static std::optional<Json> parse(const std::string& text, std::string* error = nullptr);

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
    std::vector<Json> array_;
    std::map<std::string, Json> object_;
};

}  // namespace effort
