#include "dev/DevJson.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>

namespace lg::dev {
namespace {

class Parser {
public:
  explicit Parser(std::string_view text) : text_(text) {}

  JsonParseResult parse() {
    skipWhitespace();
    JsonValue value;
    if (!parseValue(value)) {
      return {{}, false, error_};
    }
    skipWhitespace();
    if (offset_ != text_.size()) {
      fail("unexpected trailing JSON data");
      return {{}, false, error_};
    }
    return {std::move(value), true, {}};
  }

private:
  [[nodiscard]] char peek() const {
    return offset_ < text_.size() ? text_[offset_] : '\0';
  }

  char take() {
    return offset_ < text_.size() ? text_[offset_++] : '\0';
  }

  void skipWhitespace() {
    while (peek() == ' ' || peek() == '\t' || peek() == '\r' || peek() == '\n') {
      ++offset_;
    }
  }

  bool consume(std::string_view token) {
    if (text_.substr(offset_, token.size()) != token) {
      return false;
    }
    offset_ += token.size();
    return true;
  }

  bool fail(std::string message) {
    if (error_.empty()) {
      error_ = std::move(message) + " at byte " + std::to_string(offset_);
    }
    return false;
  }

  bool parseValue(JsonValue& value) {
    skipWhitespace();
    switch (peek()) {
    case '{':
      return parseObject(value);
    case '[':
      return parseArray(value);
    case '"':
      value.type = JsonValue::Type::String;
      return parseString(value.string);
    default:
      break;
    }
    if (consume("true")) {
      value = JsonValue::booleanValue(true);
      return true;
    }
    if (consume("false")) {
      value = JsonValue::booleanValue(false);
      return true;
    }
    if (consume("null")) {
      value = {};
      return true;
    }
    return parseNumber(value);
  }

  bool parseObject(JsonValue& value) {
    (void)take();
    value = JsonValue::objectValue();
    skipWhitespace();
    if (peek() == '}') {
      (void)take();
      return true;
    }
    while (peek() != '\0') {
      std::string key;
      if (!parseString(key)) {
        return false;
      }
      skipWhitespace();
      if (take() != ':') {
        return fail("expected ':' after object key");
      }
      JsonValue member;
      if (!parseValue(member)) {
        return false;
      }
      value.object.insert_or_assign(std::move(key), std::move(member));
      skipWhitespace();
      const char separator = take();
      if (separator == '}') {
        return true;
      }
      if (separator != ',') {
        return fail("expected ',' or '}' in object");
      }
      skipWhitespace();
    }
    return fail("unterminated object");
  }

  bool parseArray(JsonValue& value) {
    (void)take();
    value = JsonValue::arrayValue();
    skipWhitespace();
    if (peek() == ']') {
      (void)take();
      return true;
    }
    while (peek() != '\0') {
      JsonValue item;
      if (!parseValue(item)) {
        return false;
      }
      value.array.push_back(std::move(item));
      skipWhitespace();
      const char separator = take();
      if (separator == ']') {
        return true;
      }
      if (separator != ',') {
        return fail("expected ',' or ']' in array");
      }
      skipWhitespace();
    }
    return fail("unterminated array");
  }

  static void appendUtf8(std::string& output, std::uint32_t codepoint) {
    if (codepoint <= 0x7FU) {
      output.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FFU) {
      output.push_back(static_cast<char>(0xC0U | (codepoint >> 6U)));
      output.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
    } else {
      output.push_back(static_cast<char>(0xE0U | (codepoint >> 12U)));
      output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
      output.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
    }
  }

  bool parseString(std::string& value) {
    if (take() != '"') {
      return fail("expected JSON string");
    }
    value.clear();
    while (peek() != '\0') {
      const unsigned char character = static_cast<unsigned char>(take());
      if (character == '"') {
        return true;
      }
      if (character < 0x20U) {
        return fail("unescaped control character in string");
      }
      if (character != '\\') {
        value.push_back(static_cast<char>(character));
        continue;
      }
      const char escaped = take();
      switch (escaped) {
      case '"': case '\\': case '/':
        value.push_back(escaped);
        break;
      case 'b': value.push_back('\b'); break;
      case 'f': value.push_back('\f'); break;
      case 'n': value.push_back('\n'); break;
      case 'r': value.push_back('\r'); break;
      case 't': value.push_back('\t'); break;
      case 'u': {
        if (offset_ + 4U > text_.size()) {
          return fail("truncated unicode escape");
        }
        std::uint32_t codepoint = 0;
        for (int index = 0; index < 4; ++index) {
          const char digit = take();
          codepoint <<= 4U;
          if (digit >= '0' && digit <= '9') codepoint += static_cast<std::uint32_t>(digit - '0');
          else if (digit >= 'a' && digit <= 'f') codepoint += static_cast<std::uint32_t>(digit - 'a' + 10);
          else if (digit >= 'A' && digit <= 'F') codepoint += static_cast<std::uint32_t>(digit - 'A' + 10);
          else return fail("invalid unicode escape");
        }
        appendUtf8(value, codepoint);
        break;
      }
      default:
        return fail("invalid string escape");
      }
    }
    return fail("unterminated string");
  }

  bool parseNumber(JsonValue& value) {
    const std::size_t start = offset_;
    if (peek() == '-') ++offset_;
    if (peek() == '0') {
      ++offset_;
    } else {
      if (peek() < '1' || peek() > '9') return fail("expected JSON value");
      while (peek() >= '0' && peek() <= '9') ++offset_;
    }
    if (peek() == '.') {
      ++offset_;
      if (peek() < '0' || peek() > '9') return fail("invalid JSON number");
      while (peek() >= '0' && peek() <= '9') ++offset_;
    }
    if (peek() == 'e' || peek() == 'E') {
      ++offset_;
      if (peek() == '+' || peek() == '-') ++offset_;
      if (peek() < '0' || peek() > '9') return fail("invalid JSON exponent");
      while (peek() >= '0' && peek() <= '9') ++offset_;
    }
    const std::string_view numberText = text_.substr(start, offset_ - start);
    double parsed = 0.0;
    const auto converted = std::from_chars(
      numberText.data(), numberText.data() + numberText.size(), parsed
    );
    if (converted.ec != std::errc{} || !std::isfinite(parsed)) {
      return fail("invalid or non-finite JSON number");
    }
    value = JsonValue::numberValue(parsed);
    return true;
  }

  std::string_view text_;
  std::size_t offset_ = 0;
  std::string error_;
};

void writeValue(std::ostringstream& output, const JsonValue& value) {
  switch (value.type) {
  case JsonValue::Type::Null:
    output << "null";
    break;
  case JsonValue::Type::Boolean:
    output << (value.boolean ? "true" : "false");
    break;
  case JsonValue::Type::Number:
    output << std::setprecision(std::numeric_limits<double>::max_digits10)
           << value.number;
    break;
  case JsonValue::Type::String:
    output << '"' << escapeJson(value.string) << '"';
    break;
  case JsonValue::Type::Array:
    output << '[';
    for (std::size_t index = 0; index < value.array.size(); ++index) {
      if (index > 0) output << ',';
      writeValue(output, value.array[index]);
    }
    output << ']';
    break;
  case JsonValue::Type::Object:
    output << '{';
    {
      bool first = true;
      for (const auto& [key, member] : value.object) {
        if (!first) output << ',';
        first = false;
        output << '"' << escapeJson(key) << "\":";
        writeValue(output, member);
      }
    }
    output << '}';
    break;
  }
}

} // namespace

JsonValue JsonValue::booleanValue(bool value) {
  JsonValue result;
  result.type = Type::Boolean;
  result.boolean = value;
  return result;
}

JsonValue JsonValue::numberValue(double value) {
  JsonValue result;
  result.type = Type::Number;
  result.number = value;
  return result;
}

JsonValue JsonValue::stringValue(std::string value) {
  JsonValue result;
  result.type = Type::String;
  result.string = std::move(value);
  return result;
}

JsonValue JsonValue::arrayValue(std::vector<JsonValue> value) {
  JsonValue result;
  result.type = Type::Array;
  result.array = std::move(value);
  return result;
}

JsonValue JsonValue::objectValue() {
  JsonValue result;
  result.type = Type::Object;
  return result;
}

const JsonValue* JsonValue::find(std::string_view key) const {
  if (type != Type::Object) return nullptr;
  const auto found = object.find(key);
  return found == object.end() ? nullptr : &found->second;
}

JsonValue* JsonValue::find(std::string_view key) {
  if (type != Type::Object) return nullptr;
  const auto found = object.find(key);
  return found == object.end() ? nullptr : &found->second;
}

JsonParseResult parseJson(std::string_view text) {
  return Parser(text).parse();
}

std::string escapeJson(std::string_view value) {
  std::ostringstream output;
  for (const unsigned char character : value) {
    switch (character) {
    case '"': output << "\\\""; break;
    case '\\': output << "\\\\"; break;
    case '\b': output << "\\b"; break;
    case '\f': output << "\\f"; break;
    case '\n': output << "\\n"; break;
    case '\r': output << "\\r"; break;
    case '\t': output << "\\t"; break;
    default:
      if (character < 0x20U) {
        output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
               << static_cast<unsigned int>(character) << std::dec;
      } else {
        output << static_cast<char>(character);
      }
      break;
    }
  }
  return output.str();
}

std::string writeJson(const JsonValue& value) {
  std::ostringstream output;
  writeValue(output, value);
  return output.str();
}

std::optional<std::string> stringMember(const JsonValue& object, std::string_view key) {
  const JsonValue* value = object.find(key);
  if (value == nullptr || value->type != JsonValue::Type::String) return std::nullopt;
  return value->string;
}

std::optional<double> numberMember(const JsonValue& object, std::string_view key) {
  const JsonValue* value = object.find(key);
  if (value == nullptr || value->type != JsonValue::Type::Number) return std::nullopt;
  return value->number;
}

std::optional<bool> boolMember(const JsonValue& object, std::string_view key) {
  const JsonValue* value = object.find(key);
  if (value == nullptr || value->type != JsonValue::Type::Boolean) return std::nullopt;
  return value->boolean;
}

} // namespace lg::dev
