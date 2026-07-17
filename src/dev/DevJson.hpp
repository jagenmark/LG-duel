#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lg::dev {

struct JsonValue {
  enum class Type {
    Null,
    Boolean,
    Number,
    String,
    Array,
    Object,
  };

  Type type = Type::Null;
  bool boolean = false;
  double number = 0.0;
  std::string string;
  std::vector<JsonValue> array;
  std::map<std::string, JsonValue, std::less<>> object;

  [[nodiscard]] static JsonValue booleanValue(bool value);
  [[nodiscard]] static JsonValue numberValue(double value);
  [[nodiscard]] static JsonValue stringValue(std::string value);
  [[nodiscard]] static JsonValue arrayValue(std::vector<JsonValue> value = {});
  [[nodiscard]] static JsonValue objectValue();

  [[nodiscard]] const JsonValue* find(std::string_view key) const;
  [[nodiscard]] JsonValue* find(std::string_view key);
};

struct JsonParseResult {
  JsonValue value;
  bool ok = false;
  std::string error;
};

[[nodiscard]] JsonParseResult parseJson(std::string_view text);
[[nodiscard]] std::string writeJson(const JsonValue& value);
[[nodiscard]] std::string escapeJson(std::string_view value);

[[nodiscard]] std::optional<std::string> stringMember(
  const JsonValue& object,
  std::string_view key
);
[[nodiscard]] std::optional<double> numberMember(
  const JsonValue& object,
  std::string_view key
);
[[nodiscard]] std::optional<bool> boolMember(
  const JsonValue& object,
  std::string_view key
);

} // namespace lg::dev
