// RenderLift — ALRR Core: minimal JSON parser.
//
// Intentionally small and dependency-free: game/hardware profiles are plain
// JSON files and we don't want a third-party dependency in the kernel of the
// engine before third_party/ has its review process. Supports objects,
// arrays, strings (with common escapes + \uXXXX BMP), numbers, booleans and
// null — everything the profile schema needs.
#pragma once

#include <cstddef>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace rl::json {

class ParseError : public std::runtime_error {
public:
    ParseError(const std::string& message, std::size_t offset);
    [[nodiscard]] std::size_t offset() const noexcept { return offset_; }

private:
    std::size_t offset_;
};

class Value {
public:
    using Array  = std::vector<Value>;
    using Object = std::map<std::string, Value>;

    Value() : data_(nullptr) {}
    Value(std::nullptr_t) : data_(nullptr) {}
    Value(bool v) : data_(v) {}
    Value(double v) : data_(v) {}
    Value(std::string v) : data_(std::move(v)) {}
    Value(Array v) : data_(std::move(v)) {}
    Value(Object v) : data_(std::move(v)) {}

    [[nodiscard]] bool isNull()   const noexcept { return std::holds_alternative<std::nullptr_t>(data_); }
    [[nodiscard]] bool isBool()   const noexcept { return std::holds_alternative<bool>(data_); }
    [[nodiscard]] bool isNumber() const noexcept { return std::holds_alternative<double>(data_); }
    [[nodiscard]] bool isString() const noexcept { return std::holds_alternative<std::string>(data_); }
    [[nodiscard]] bool isArray()  const noexcept { return std::holds_alternative<Array>(data_); }
    [[nodiscard]] bool isObject() const noexcept { return std::holds_alternative<Object>(data_); }

    [[nodiscard]] bool              asBool()   const;
    [[nodiscard]] double            asNumber() const;
    [[nodiscard]] const std::string& asString() const;
    [[nodiscard]] const Array&      asArray()  const;
    [[nodiscard]] const Object&     asObject() const;

    // Object helpers. find() returns nullptr when the key is absent (or when
    // this value is not an object); at() throws on a missing key.
    [[nodiscard]] const Value* find(std::string_view key) const noexcept;
    [[nodiscard]] const Value& at(std::string_view key) const;

    [[nodiscard]] std::string stringOr(std::string_view key, std::string fallback) const;
    [[nodiscard]] double      numberOr(std::string_view key, double fallback) const;
    [[nodiscard]] bool        boolOr(std::string_view key, bool fallback) const;

private:
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> data_;
};

// Parses a complete JSON document. Throws ParseError on malformed input
// (including trailing characters after the document).
[[nodiscard]] Value parse(std::string_view text);

}  // namespace rl::json
