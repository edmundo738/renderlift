#include "renderlift/core/Json.hpp"

#include <cctype>
#include <cstdlib>
#include <utility>

namespace rl::json {

namespace {

[[noreturn]] void typeError(const char* expected) {
    throw std::runtime_error(std::string("json: expected ") + expected);
}

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    Value parseDocument() {
        Value v = parseValue();
        skipWhitespace();
        if (pos_ != text_.size()) {
            fail("unexpected trailing characters after JSON document");
        }
        return v;
    }

private:
    [[noreturn]] void fail(const char* message) const { throw ParseError(message, pos_); }

    char peek() const { return pos_ < text_.size() ? text_[pos_] : '\0'; }

    char get() {
        const char c = peek();
        if (c == '\0') {
            fail("unexpected end of input");
        }
        ++pos_;
        return c;
    }

    void skipWhitespace() {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
            ++pos_;
        }
    }

    void expectLiteral(std::string_view literal) {
        for (const char expected : literal) {
            if (pos_ >= text_.size() || text_[pos_] != expected) {
                fail("invalid literal");
            }
            ++pos_;
        }
    }

    Value parseValue() {
        skipWhitespace();
        switch (peek()) {
            case '{': {
                get();
                Value::Object obj;
                skipWhitespace();
                if (peek() == '}') {
                    get();
                    return Value(std::move(obj));
                }
                while (true) {
                    skipWhitespace();
                    if (peek() != '"') fail("expected object key");
                    std::string key = parseString();
                    skipWhitespace();
                    if (get() != ':') fail("expected ':' after object key");
                    obj.emplace(std::move(key), parseValue());
                    skipWhitespace();
                    const char c = get();
                    if (c == '}') break;
                    if (c != ',') fail("expected ',' or '}' in object");
                }
                return Value(std::move(obj));
            }
            case '[': {
                get();
                Value::Array arr;
                skipWhitespace();
                if (peek() == ']') {
                    get();
                    return Value(std::move(arr));
                }
                while (true) {
                    arr.push_back(parseValue());
                    skipWhitespace();
                    const char c = get();
                    if (c == ']') break;
                    if (c != ',') fail("expected ',' or ']' in array");
                }
                return Value(std::move(arr));
            }
            case '"': return Value(parseString());
            case 't': expectLiteral("true");  return Value(true);
            case 'f': expectLiteral("false"); return Value(false);
            case 'n': expectLiteral("null");  return Value(nullptr);
            default:
                if (peek() == '-' || std::isdigit(static_cast<unsigned char>(peek()))) {
                    return Value(parseNumber());
                }
                fail("unexpected character");
        }
    }

    double parseNumber() {
        const std::size_t start = pos_;
        if (peek() == '-') ++pos_;
        while (std::isdigit(static_cast<unsigned char>(peek()))) ++pos_;
        if (peek() == '.') {
            ++pos_;
            while (std::isdigit(static_cast<unsigned char>(peek()))) ++pos_;
        }
        if (peek() == 'e' || peek() == 'E') {
            ++pos_;
            if (peek() == '+' || peek() == '-') ++pos_;
            while (std::isdigit(static_cast<unsigned char>(peek()))) ++pos_;
        }
        const std::string token(text_.substr(start, pos_ - start));
        if (token.empty() || token == "-" || token == "." || token == "-.") {
            pos_ = start;
            fail("invalid number");
        }
        return std::strtod(token.c_str(), nullptr);
    }

    void appendUtf8(std::string& out, unsigned code) {
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            // BMP only — surrogate pairs are not needed by the profile schema.
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
    }

    std::string parseString() {
        get();  // opening quote
        std::string out;
        while (true) {
            const char c = get();
            if (c == '"') break;
            if (c != '\\') {
                out += c;
                continue;
            }
            switch (get()) {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case '/':  out += '/';  break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                case 'u': {
                    unsigned code = 0;
                    for (int i = 0; i < 4; ++i) {
                        const char h = get();
                        code <<= 4;
                        if      (h >= '0' && h <= '9') code |= static_cast<unsigned>(h - '0');
                        else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned>(h - 'A' + 10);
                        else fail("invalid \\u escape");
                    }
                    appendUtf8(out, code);
                    break;
                }
                default: fail("invalid escape sequence");
            }
        }
        return out;
    }

    std::string_view text_;
    std::size_t pos_ = 0;
};

}  // namespace

ParseError::ParseError(const std::string& message, std::size_t offset)
    : std::runtime_error("json: " + message + " (at offset " + std::to_string(offset) + ")"),
      offset_(offset) {}

bool Value::asBool() const {
    if (!isBool()) typeError("bool");
    return std::get<bool>(data_);
}

double Value::asNumber() const {
    if (!isNumber()) typeError("number");
    return std::get<double>(data_);
}

const std::string& Value::asString() const {
    if (!isString()) typeError("string");
    return std::get<std::string>(data_);
}

const Value::Array& Value::asArray() const {
    if (!isArray()) typeError("array");
    return std::get<Array>(data_);
}

const Value::Object& Value::asObject() const {
    if (!isObject()) typeError("object");
    return std::get<Object>(data_);
}

const Value* Value::find(std::string_view key) const noexcept {
    if (!isObject()) return nullptr;
    const auto& obj = std::get<Object>(data_);
    const auto it = obj.find(std::string(key));
    return it == obj.end() ? nullptr : &it->second;
}

const Value& Value::at(std::string_view key) const {
    const Value* v = find(key);
    if (!v) throw std::runtime_error("json: missing key '" + std::string(key) + "'");
    return *v;
}

std::string Value::stringOr(std::string_view key, std::string fallback) const {
    const Value* v = find(key);
    return (v && v->isString()) ? v->asString() : std::move(fallback);
}

double Value::numberOr(std::string_view key, double fallback) const {
    const Value* v = find(key);
    return (v && v->isNumber()) ? v->asNumber() : fallback;
}

bool Value::boolOr(std::string_view key, bool fallback) const {
    const Value* v = find(key);
    return (v && v->isBool()) ? v->asBool() : fallback;
}

Value parse(std::string_view text) {
    return Parser(text).parseDocument();
}

}  // namespace rl::json
