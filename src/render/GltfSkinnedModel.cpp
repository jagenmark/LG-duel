#include "render/GltfSkinnedModel.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace lg {
namespace {

struct JsonValue {
  enum class Type {
    Null,
    Bool,
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
  std::map<std::string, JsonValue> object;
};

class JsonParser {
public:
  explicit JsonParser(std::string_view text) : text_(text) {}

  JsonValue parse() {
    JsonValue value = parseValue();
    skipWhitespace();
    if (offset_ != text_.size()) {
      throw std::runtime_error("unexpected trailing json content");
    }
    return value;
  }

private:
  [[nodiscard]] char peek() const {
    return offset_ < text_.size() ? text_[offset_] : '\0';
  }

  char take() {
    return offset_ < text_.size() ? text_[offset_++] : '\0';
  }

  void skipWhitespace() {
    while (
      peek() == ' ' || peek() == '\n' || peek() == '\r' || peek() == '\t'
    ) {
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

  JsonValue parseValue() {
    skipWhitespace();
    if (peek() == '{') {
      return parseObject();
    }
    if (peek() == '[') {
      return parseArray();
    }
    if (peek() == '"') {
      JsonValue value;
      value.type = JsonValue::Type::String;
      value.string = parseString();
      return value;
    }
    if (consume("true")) {
      JsonValue value;
      value.type = JsonValue::Type::Bool;
      value.boolean = true;
      return value;
    }
    if (consume("false")) {
      JsonValue value;
      value.type = JsonValue::Type::Bool;
      value.boolean = false;
      return value;
    }
    if (consume("null")) {
      return {};
    }
    return parseNumber();
  }

  JsonValue parseObject() {
    JsonValue value;
    value.type = JsonValue::Type::Object;
    (void)take();
    skipWhitespace();
    if (peek() == '}') {
      (void)take();
      return value;
    }
    while (true) {
      const std::string key = parseString();
      skipWhitespace();
      if (take() != ':') {
        throw std::runtime_error("invalid json object");
      }
      value.object.emplace(key, parseValue());
      skipWhitespace();
      const char separator = take();
      if (separator == '}') {
        return value;
      }
      if (separator != ',') {
        throw std::runtime_error("invalid json object separator");
      }
      skipWhitespace();
      if (peek() == '}' || peek() == '\0') {
        throw std::runtime_error("invalid json object trailing comma");
      }
    }
  }

  JsonValue parseArray() {
    JsonValue value;
    value.type = JsonValue::Type::Array;
    (void)take();
    skipWhitespace();
    if (peek() == ']') {
      (void)take();
      return value;
    }
    while (true) {
      value.array.push_back(parseValue());
      skipWhitespace();
      const char separator = take();
      if (separator == ']') {
        return value;
      }
      if (separator != ',') {
        throw std::runtime_error("invalid json array separator");
      }
      skipWhitespace();
      if (peek() == ']' || peek() == '\0') {
        throw std::runtime_error("invalid json array trailing comma");
      }
    }
  }

  std::string parseString() {
    if (take() != '"') {
      throw std::runtime_error("expected json string");
    }
    std::string result;
    while (offset_ < text_.size()) {
      const unsigned char character = static_cast<unsigned char>(take());
      if (character == '"') {
        return result;
      }
      if (character < 0x20U) {
        throw std::runtime_error("unescaped control character in json string");
      }
      if (character == '\\') {
        if (offset_ == text_.size()) {
          throw std::runtime_error("incomplete json string escape");
        }
        const char escaped = take();
        switch (escaped) {
        case '"':
        case '\\':
        case '/':
          result.push_back(escaped);
          break;
        case 'b':
          result.push_back('\b');
          break;
        case 'f':
          result.push_back('\f');
          break;
        case 'n':
          result.push_back('\n');
          break;
        case 'r':
          result.push_back('\r');
          break;
        case 't':
          result.push_back('\t');
          break;
        case 'u':
          throw std::runtime_error("unicode json string escapes are not supported");
        default:
          throw std::runtime_error("unsupported json string escape");
        }
      } else {
        result.push_back(static_cast<char>(character));
      }
    }
    throw std::runtime_error("unterminated json string");
  }

  JsonValue parseNumber() {
    const std::size_t start = offset_;
    if (peek() == '-') {
      ++offset_;
    }

    if (peek() == '0') {
      ++offset_;
    } else if (std::isdigit(static_cast<unsigned char>(peek()))) {
      do {
        ++offset_;
      } while (std::isdigit(static_cast<unsigned char>(peek())));
    } else {
      throw std::runtime_error("invalid json number");
    }

    if (peek() == '.') {
      ++offset_;
      if (!std::isdigit(static_cast<unsigned char>(peek()))) {
        throw std::runtime_error("invalid json number fraction");
      }
      while (std::isdigit(static_cast<unsigned char>(peek()))) {
        ++offset_;
      }
    }
    if (peek() == 'e' || peek() == 'E') {
      ++offset_;
      if (peek() == '-' || peek() == '+') {
        ++offset_;
      }
      if (!std::isdigit(static_cast<unsigned char>(peek()))) {
        throw std::runtime_error("invalid json number exponent");
      }
      while (std::isdigit(static_cast<unsigned char>(peek()))) {
        ++offset_;
      }
    }
    JsonValue value;
    value.type = JsonValue::Type::Number;
    const std::string token(text_.substr(start, offset_ - start));
    std::size_t parsed = 0;
    value.number = std::stod(token, &parsed);
    if (parsed != token.size()) {
      throw std::runtime_error("invalid json number");
    }
    return value;
  }

  std::string_view text_;
  std::size_t offset_ = 0;
};

[[nodiscard]] const JsonValue& member(
  const JsonValue& object,
  std::string_view name
) {
  static const JsonValue missing;
  if (object.type != JsonValue::Type::Object) {
    return missing;
  }
  const auto found = object.object.find(std::string(name));
  return found == object.object.end() ? missing : found->second;
}

[[nodiscard]] const JsonValue& at(const JsonValue& array, int index) {
  static const JsonValue missing;
  if (
    array.type != JsonValue::Type::Array ||
    index < 0 ||
    static_cast<std::size_t>(index) >= array.array.size()
  ) {
    return missing;
  }
  return array.array[static_cast<std::size_t>(index)];
}

[[nodiscard]] int intMember(
  const JsonValue& object,
  std::string_view name,
  int fallback = -1
) {
  const JsonValue& value = member(object, name);
  return value.type == JsonValue::Type::Number
    ? static_cast<int>(value.number)
    : fallback;
}

[[nodiscard]] bool exactIntValue(const JsonValue& value, int& out) {
  if (
    value.type != JsonValue::Type::Number ||
    !std::isfinite(value.number) ||
    std::trunc(value.number) != value.number ||
    value.number < static_cast<double>(std::numeric_limits<int>::min()) ||
    value.number > static_cast<double>(std::numeric_limits<int>::max())
  ) {
    return false;
  }
  out = static_cast<int>(value.number);
  return true;
}

[[nodiscard]] bool exactIntMember(
  const JsonValue& object,
  std::string_view name,
  int& out
) {
  return exactIntValue(member(object, name), out);
}

[[nodiscard]] bool exactIntAt(
  const JsonValue& array,
  int index,
  int& out
) {
  return exactIntValue(at(array, index), out);
}

[[nodiscard]] float floatAt(
  const JsonValue& array,
  int index,
  float fallback
) {
  const JsonValue& value = at(array, index);
  return value.type == JsonValue::Type::Number
    ? static_cast<float>(value.number)
    : fallback;
}

[[nodiscard]] std::string stringMember(
  const JsonValue& object,
  std::string_view name,
  std::string fallback = {}
) {
  const JsonValue& value = member(object, name);
  return value.type == JsonValue::Type::String ? value.string : fallback;
}

[[nodiscard]] bool boolMember(
  const JsonValue& object,
  std::string_view name,
  bool fallback = false
) {
  const JsonValue& value = member(object, name);
  return value.type == JsonValue::Type::Bool ? value.boolean : fallback;
}

[[nodiscard]] float floatMember(
  const JsonValue& object,
  std::string_view name,
  float fallback = 0.0F
) {
  const JsonValue& value = member(object, name);
  return value.type == JsonValue::Type::Number
    ? static_cast<float>(value.number)
    : fallback;
}

[[nodiscard]] std::uint32_t readU32(
  const std::vector<std::uint8_t>& bytes,
  std::size_t offset
) {
  if (offset + 4U > bytes.size()) {
    return 0;
  }
  std::uint32_t value = 0;
  std::memcpy(&value, bytes.data() + offset, sizeof(value));
  return value;
}

template <typename T>
[[nodiscard]] T readScalar(
  const std::vector<std::uint8_t>& bytes,
  std::size_t offset
) {
  T value = {};
  if (offset + sizeof(T) <= bytes.size()) {
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
  }
  return value;
}

[[nodiscard]] int componentSize(int componentType) {
  switch (componentType) {
  case 5120:
  case 5121:
    return 1;
  case 5122:
  case 5123:
    return 2;
  case 5125:
  case 5126:
    return 4;
  default:
    return 0;
  }
}

[[nodiscard]] int typeCount(std::string_view type) {
  if (type == "SCALAR") {
    return 1;
  }
  if (type == "VEC2") {
    return 2;
  }
  if (type == "VEC3") {
    return 3;
  }
  if (type == "VEC4") {
    return 4;
  }
  if (type == "MAT4") {
    return 16;
  }
  return 0;
}

struct AccessorView {
  const JsonValue* accessor = nullptr;
  const JsonValue* bufferView = nullptr;
  std::size_t offset = 0;
  std::size_t stride = 0;
  int componentType = 0;
  int components = 0;
  int count = 0;
};

[[nodiscard]] AccessorView accessorView(
  const JsonValue& root,
  int accessorIndex
) {
  const JsonValue& accessors = member(root, "accessors");
  const JsonValue& bufferViews = member(root, "bufferViews");
  const JsonValue& accessor = at(accessors, accessorIndex);
  const int bufferViewIndex = intMember(accessor, "bufferView");
  const JsonValue& bufferView = at(bufferViews, bufferViewIndex);
  const int componentType = intMember(accessor, "componentType", 0);
  const int components = typeCount(stringMember(accessor, "type"));
  const std::size_t elementSize =
    static_cast<std::size_t>(componentSize(componentType) * components);
  const std::size_t stride = static_cast<std::size_t>(
    intMember(bufferView, "byteStride", static_cast<int>(elementSize))
  );
  return {
    &accessor,
    &bufferView,
    static_cast<std::size_t>(intMember(bufferView, "byteOffset", 0)) +
      static_cast<std::size_t>(intMember(accessor, "byteOffset", 0)),
    stride == 0U ? elementSize : stride,
    componentType,
    components,
    intMember(accessor, "count", 0),
  };
}

[[nodiscard]] float readComponentAsFloat(
  const std::vector<std::uint8_t>& bytes,
  int componentType,
  std::size_t offset
) {
  switch (componentType) {
  case 5120:
    return static_cast<float>(readScalar<std::int8_t>(bytes, offset));
  case 5121:
    return static_cast<float>(readScalar<std::uint8_t>(bytes, offset));
  case 5122:
    return static_cast<float>(readScalar<std::int16_t>(bytes, offset));
  case 5123:
    return static_cast<float>(readScalar<std::uint16_t>(bytes, offset));
  case 5125:
    return static_cast<float>(readScalar<std::uint32_t>(bytes, offset));
  case 5126:
    return readScalar<float>(bytes, offset);
  default:
    return 0.0F;
  }
}

[[nodiscard]] std::vector<float> readAccessorFloats(
  const JsonValue& root,
  const std::vector<std::uint8_t>& bytes,
  int accessorIndex
) {
  const AccessorView view = accessorView(root, accessorIndex);
  std::vector<float> values;
  values.reserve(static_cast<std::size_t>(view.count * view.components));
  const int componentBytes = componentSize(view.componentType);
  for (int index = 0; index < view.count; ++index) {
    const std::size_t base =
      view.offset + static_cast<std::size_t>(index) * view.stride;
    for (int component = 0; component < view.components; ++component) {
      values.push_back(readComponentAsFloat(
        bytes,
        view.componentType,
        base + static_cast<std::size_t>(component * componentBytes)
      ));
    }
  }
  return values;
}

[[nodiscard]] std::vector<std::uint32_t> readAccessorU32(
  const JsonValue& root,
  const std::vector<std::uint8_t>& bytes,
  int accessorIndex
) {
  const AccessorView view = accessorView(root, accessorIndex);
  std::vector<std::uint32_t> values;
  values.reserve(static_cast<std::size_t>(view.count * view.components));
  const int componentBytes = componentSize(view.componentType);
  for (int index = 0; index < view.count; ++index) {
    const std::size_t base =
      view.offset + static_cast<std::size_t>(index) * view.stride;
    for (int component = 0; component < view.components; ++component) {
      values.push_back(static_cast<std::uint32_t>(readComponentAsFloat(
        bytes,
        view.componentType,
        base + static_cast<std::size_t>(component * componentBytes)
      )));
    }
  }
  return values;
}

[[nodiscard]] Vec3 vec3FromFloats(
  const std::vector<float>& values,
  std::size_t index
) {
  const std::size_t offset = index * 3U;
  return {values[offset], values[offset + 1U], values[offset + 2U]};
}

[[nodiscard]] Vec3 vec3FromFloatsOr(
  const std::vector<float>& values,
  std::size_t index,
  Vec3 fallback
) {
  const std::size_t offset = index * 3U;
  return offset + 2U < values.size()
    ? Vec3{values[offset], values[offset + 1U], values[offset + 2U]}
    : fallback;
}

[[nodiscard]] std::array<float, 2> vec2FromFloatsOr(
  const std::vector<float>& values,
  std::size_t index,
  std::array<float, 2> fallback
) {
  const std::size_t offset = index * 2U;
  return offset + 1U < values.size()
    ? std::array<float, 2>{values[offset], values[offset + 1U]}
    : fallback;
}

void expandBounds(GltfModelBounds& bounds, Vec3 point, bool& initialized) {
  if (!initialized) {
    bounds.min = point;
    bounds.max = point;
    initialized = true;
    return;
  }
  bounds.min.x = std::min(bounds.min.x, point.x);
  bounds.min.y = std::min(bounds.min.y, point.y);
  bounds.min.z = std::min(bounds.min.z, point.z);
  bounds.max.x = std::max(bounds.max.x, point.x);
  bounds.max.y = std::max(bounds.max.y, point.y);
  bounds.max.z = std::max(bounds.max.z, point.z);
}

[[nodiscard]] std::array<float, 4> normalizedWeights(
  std::array<float, 4> weights
) {
  float total = 0.0F;
  for (float& weight : weights) {
    if (!std::isfinite(weight) || weight < 0.0F) {
      weight = 0.0F;
    }
    total += weight;
  }
  if (total <= 0.000001F) {
    return {};
  }
  for (float& weight : weights) {
    weight /= total;
  }
  return weights;
}

[[nodiscard]] bool safeModelRelativePath(std::string_view value) {
  if (value.empty()) {
    return false;
  }
  const std::filesystem::path path{std::string(value)};
  if (path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
    return false;
  }
  for (const std::filesystem::path& component : path) {
    if (component == ".." || component == ".") {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::uint8_t normalizedByte(float value) {
  return static_cast<std::uint8_t>(std::clamp(value * 255.0F, 0.0F, 255.0F));
}

[[nodiscard]] std::uint8_t gpuAlbedoTextureMode(
  GltfAlbedoTextureMode mode
) {
  switch (mode) {
  case GltfAlbedoTextureMode::Multiply:
    return 128U;
  case GltfAlbedoTextureMode::Replace:
    return 255U;
  case GltfAlbedoTextureMode::None:
    return 0U;
  }
  return 0U;
}

[[nodiscard]] RenderColor materialColor(
  const JsonValue& material,
  bool forceOpaque
) {
  const JsonValue& pbr = member(material, "pbrMetallicRoughness");
  const JsonValue& factor = member(pbr, "baseColorFactor");
  const bool opaque = forceOpaque ||
    stringMember(material, "alphaMode", "OPAQUE") != "BLEND";
  return {
    normalizedByte(floatAt(factor, 0, 1.0F)),
    normalizedByte(floatAt(factor, 1, 1.0F)),
    normalizedByte(floatAt(factor, 2, 1.0F)),
    opaque ? static_cast<std::uint8_t>(255U) :
      normalizedByte(floatAt(factor, 3, 1.0F)),
  };
}

[[nodiscard]] std::string readTextFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return {};
  }
  file.seekg(0, std::ios::end);
  const std::streamoff size = file.tellg();
  if (size < 0) {
    return {};
  }
  file.seekg(0, std::ios::beg);
  std::string text(static_cast<std::size_t>(size), '\0');
  file.read(text.data(), size);
  return file ? text : std::string{};
}

[[nodiscard]] std::uint32_t rotateRight(std::uint32_t value, unsigned int bits) {
  return (value >> bits) | (value << (32U - bits));
}

void hashSha256Block(
  const std::uint8_t* block,
  std::array<std::uint32_t, 8>& state
) {
  constexpr std::array<std::uint32_t, 64> constants = {
    0x428A2F98U, 0x71374491U, 0xB5C0FBCFU, 0xE9B5DBA5U,
    0x3956C25BU, 0x59F111F1U, 0x923F82A4U, 0xAB1C5ED5U,
    0xD807AA98U, 0x12835B01U, 0x243185BEU, 0x550C7DC3U,
    0x72BE5D74U, 0x80DEB1FEU, 0x9BDC06A7U, 0xC19BF174U,
    0xE49B69C1U, 0xEFBE4786U, 0x0FC19DC6U, 0x240CA1CCU,
    0x2DE92C6FU, 0x4A7484AAU, 0x5CB0A9DCU, 0x76F988DAU,
    0x983E5152U, 0xA831C66DU, 0xB00327C8U, 0xBF597FC7U,
    0xC6E00BF3U, 0xD5A79147U, 0x06CA6351U, 0x14292967U,
    0x27B70A85U, 0x2E1B2138U, 0x4D2C6DFCU, 0x53380D13U,
    0x650A7354U, 0x766A0ABBU, 0x81C2C92EU, 0x92722C85U,
    0xA2BFE8A1U, 0xA81A664BU, 0xC24B8B70U, 0xC76C51A3U,
    0xD192E819U, 0xD6990624U, 0xF40E3585U, 0x106AA070U,
    0x19A4C116U, 0x1E376C08U, 0x2748774CU, 0x34B0BCB5U,
    0x391C0CB3U, 0x4ED8AA4AU, 0x5B9CCA4FU, 0x682E6FF3U,
    0x748F82EEU, 0x78A5636FU, 0x84C87814U, 0x8CC70208U,
    0x90BEFFFAU, 0xA4506CEBU, 0xBEF9A3F7U, 0xC67178F2U,
  };
  std::array<std::uint32_t, 64> words = {};
  for (std::size_t index = 0; index < 16U; ++index) {
    const std::size_t offset = index * 4U;
    words[index] = (static_cast<std::uint32_t>(block[offset]) << 24U) |
      (static_cast<std::uint32_t>(block[offset + 1U]) << 16U) |
      (static_cast<std::uint32_t>(block[offset + 2U]) << 8U) |
      static_cast<std::uint32_t>(block[offset + 3U]);
  }
  for (std::size_t index = 16U; index < words.size(); ++index) {
    const std::uint32_t first = rotateRight(words[index - 15U], 7U) ^
      rotateRight(words[index - 15U], 18U) ^ (words[index - 15U] >> 3U);
    const std::uint32_t second = rotateRight(words[index - 2U], 17U) ^
      rotateRight(words[index - 2U], 19U) ^ (words[index - 2U] >> 10U);
    words[index] = words[index - 16U] + first + words[index - 7U] + second;
  }

  std::uint32_t a = state[0];
  std::uint32_t b = state[1];
  std::uint32_t c = state[2];
  std::uint32_t d = state[3];
  std::uint32_t e = state[4];
  std::uint32_t f = state[5];
  std::uint32_t g = state[6];
  std::uint32_t h = state[7];
  for (std::size_t index = 0; index < words.size(); ++index) {
    const std::uint32_t sigma1 = rotateRight(e, 6U) ^ rotateRight(e, 11U) ^
      rotateRight(e, 25U);
    const std::uint32_t choice = (e & f) ^ ((~e) & g);
    const std::uint32_t temporary1 = h + sigma1 + choice + constants[index] + words[index];
    const std::uint32_t sigma0 = rotateRight(a, 2U) ^ rotateRight(a, 13U) ^
      rotateRight(a, 22U);
    const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
    const std::uint32_t temporary2 = sigma0 + majority;
    h = g;
    g = f;
    f = e;
    e = d + temporary1;
    d = c;
    c = b;
    b = a;
    a = temporary1 + temporary2;
  }
  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
  state[5] += f;
  state[6] += g;
  state[7] += h;
}

[[nodiscard]] std::string sha256Hex(const std::vector<std::uint8_t>& bytes) {
  std::array<std::uint32_t, 8> state = {
    0x6A09E667U, 0xBB67AE85U, 0x3C6EF372U, 0xA54FF53AU,
    0x510E527FU, 0x9B05688CU, 0x1F83D9ABU, 0x5BE0CD19U,
  };
  constexpr std::size_t blockSize = 64U;
  const std::size_t fullBytes = bytes.size() - (bytes.size() % blockSize);
  for (std::size_t offset = 0; offset < fullBytes; offset += blockSize) {
    hashSha256Block(bytes.data() + offset, state);
  }

  std::array<std::uint8_t, blockSize> tail = {};
  const std::size_t remaining = bytes.size() - fullBytes;
  for (std::size_t index = 0; index < remaining; ++index) {
    tail[index] = bytes[fullBytes + index];
  }
  tail[remaining] = 0x80U;
  if (remaining >= 56U) {
    hashSha256Block(tail.data(), state);
    tail.fill(0U);
  }
  const std::uint64_t bitLength = static_cast<std::uint64_t>(bytes.size()) * 8U;
  for (std::size_t index = 0; index < 8U; ++index) {
    tail[63U - index] = static_cast<std::uint8_t>(bitLength >> (index * 8U));
  }
  hashSha256Block(tail.data(), state);

  constexpr std::string_view hex = "0123456789abcdef";
  std::string result(64U, '0');
  for (std::size_t index = 0; index < state.size(); ++index) {
    for (std::size_t byte = 0; byte < 4U; ++byte) {
      const std::uint32_t shift = static_cast<std::uint32_t>((3U - byte) * 8U);
      const std::uint8_t value = static_cast<std::uint8_t>(state[index] >> shift);
      result[(index * 4U + byte) * 2U] = hex[value >> 4U];
      result[(index * 4U + byte) * 2U + 1U] = hex[value & 0x0FU];
    }
  }
  return result;
}

[[nodiscard]] bool validSha256Hex(std::string_view value) {
  return value.size() == 64U && std::all_of(
    value.begin(),
    value.end(),
    [](char character) {
      return (character >= '0' && character <= '9') ||
        (character >= 'a' && character <= 'f') ||
        (character >= 'A' && character <= 'F');
    }
  );
}

[[nodiscard]] bool sha256Matches(
  std::string_view expected,
  std::string_view actual
) {
  return expected.size() == actual.size() && std::equal(
    expected.begin(), expected.end(), actual.begin(),
    [](char expectedCharacter, char actualCharacter) {
      if (expectedCharacter >= 'A' && expectedCharacter <= 'F') {
        expectedCharacter = static_cast<char>(
          expectedCharacter + ('a' - 'A')
        );
      }
      return expectedCharacter == actualCharacter;
    }
  );
}

[[nodiscard]] bool powerOfTwo(std::uint32_t value) {
  return value > 0U && (value & (value - 1U)) == 0U;
}

[[nodiscard]] GltfMaterialMetadata loadMaterialMetadata(
  std::string_view modelPath,
  const std::vector<std::string>& materialNames,
  std::string_view modelSha256,
  bool hasRenderablePrimitiveWithoutMaterial
) {
  GltfMaterialMetadata result;
  const std::filesystem::path modelFile{std::string(modelPath)};
  const std::filesystem::path modelDirectory = modelFile.parent_path();
  const std::filesystem::path manifestPath =
    modelDirectory / "material-manifest.json";
  result.manifestPath = manifestPath.lexically_normal().string();
  const std::string manifestText = readTextFile(manifestPath);
  if (manifestText.empty()) {
    result.diagnostic = "no model-local material manifest";
    return result;
  }

  const auto fail = [&result](std::string_view message) {
    result.status = GltfMaterialManifestStatus::Invalid;
    result.diagnostic = std::string(message);
    result.bindings.clear();
    result.albedo = {};
    result.packedMask = {};
    result.albedoFilePresent = false;
    result.packedMaskFilePresent = false;
    result.forceOpaque = false;
    result.materialCells = false;
    result.albedoMode = GltfAlbedoTextureMode::None;
    result.atlasColumns = 0;
    result.atlasRows = 0;
  };
  try {
    const JsonValue root = JsonParser(manifestText).parse();
    int schemaVersion = 0;
    if (
      root.type != JsonValue::Type::Object ||
      !exactIntMember(root, "schema_version", schemaVersion) ||
      schemaVersion != 1
    ) {
      fail("material manifest requires schema_version 1");
      return result;
    }
    const std::string expectedModel = stringMember(root, "model");
    if (
      !expectedModel.empty() &&
      expectedModel != modelFile.filename().string()
    ) {
      fail("material manifest model name does not match the GLB");
      return result;
    }
    const std::string expectedSha256 = stringMember(root, "model_sha256");
    if (!validSha256Hex(expectedSha256)) {
      fail("material manifest requires a valid model_sha256");
      return result;
    }
    if (!sha256Matches(expectedSha256, modelSha256)) {
      fail("material manifest model_sha256 does not match the GLB");
      return result;
    }

    result.forceOpaque = boolMember(root, "opaque", false);
    const JsonValue& textures = member(root, "textures");
    const JsonValue& albedo = member(textures, "albedo");
    const JsonValue& packedMask = member(textures, "packed_mask");
    const bool declaresAlbedo = albedo.type == JsonValue::Type::Object;
    const bool declaresMask = packedMask.type == JsonValue::Type::Object;
    if (declaresAlbedo != declaresMask) {
      fail("material manifest must declare both albedo and packed_mask");
      return result;
    }
    if (declaresAlbedo) {
      const auto parseTexture = [&modelDirectory](
                                  const JsonValue& value,
                                  GltfTextureColorSpace expectedColorSpace,
                                  GltfMaterialTexture& out
                                ) -> bool {
        const std::string relativePath = stringMember(value, "path");
        const std::string colorSpace = stringMember(value, "color_space");
        int width = 0;
        int height = 0;
        if (
          !exactIntMember(value, "width", width) ||
          !exactIntMember(value, "height", height) ||
          !safeModelRelativePath(relativePath) ||
          width <= 0 || height <= 0 || width > 2048 || height > 2048 ||
          !powerOfTwo(static_cast<std::uint32_t>(width)) ||
          !powerOfTwo(static_cast<std::uint32_t>(height))
        ) {
          return false;
        }
        const bool hasExpectedColorSpace =
          (expectedColorSpace == GltfTextureColorSpace::Srgb && colorSpace == "srgb") ||
          (expectedColorSpace == GltfTextureColorSpace::Linear && colorSpace == "linear");
        if (!hasExpectedColorSpace) {
          return false;
        }
        out.path = (modelDirectory / std::filesystem::path(relativePath))
          .lexically_normal()
          .string();
        out.colorSpace = expectedColorSpace;
        out.width = static_cast<std::uint32_t>(width);
        out.height = static_cast<std::uint32_t>(height);
        return true;
      };
      if (
        !parseTexture(albedo, GltfTextureColorSpace::Srgb, result.albedo) ||
        !parseTexture(packedMask, GltfTextureColorSpace::Linear, result.packedMask)
      ) {
        fail("material texture path, dimensions, or colour space is invalid");
        return result;
      }
      result.albedoFilePresent =
        std::filesystem::is_regular_file(result.albedo.path);
      result.packedMaskFilePresent =
        std::filesystem::is_regular_file(result.packedMask.path);
      const std::string contractR = stringMember(member(root, "packed_mask_contract"), "r");
      const std::string contractG = stringMember(member(root, "packed_mask_contract"), "g");
      const std::string contractB = stringMember(member(root, "packed_mask_contract"), "b");
      const std::string contractA = stringMember(member(root, "packed_mask_contract"), "a");
      if (
        contractR != "team_tint_weight" ||
        contractG != "perceptual_roughness" ||
        contractB != "metallic_weight" ||
        contractA != "emissive_weight_reserved_zero"
      ) {
        fail("packed mask channel contract is invalid");
        return result;
      }
      const std::string uvMode = stringMember(root, "uv_mode");
      if (uvMode == "material_cell") {
        const JsonValue& atlas = member(root, "atlas");
        int columns = 0;
        int rows = 0;
        if (
          !exactIntMember(atlas, "columns", columns) ||
          !exactIntMember(atlas, "rows", rows) ||
          columns <= 0 || rows <= 0 || columns > 32 || rows > 32
        ) {
          fail("material-cell atlas dimensions are invalid");
          return result;
        }
        result.materialCells = true;
        result.atlasColumns = static_cast<std::uint32_t>(columns);
        result.atlasRows = static_cast<std::uint32_t>(rows);
      } else if (uvMode != "texcoord0") {
        fail("textured material manifest needs uv_mode material_cell or texcoord0");
        return result;
      }
      const std::string albedoMode = stringMember(root, "albedo_mode", "multiply");
      result.albedoMode = albedoMode == "replace"
        ? GltfAlbedoTextureMode::Replace
        : albedoMode == "multiply" ? GltfAlbedoTextureMode::Multiply
        : GltfAlbedoTextureMode::None;
      if (result.albedoMode == GltfAlbedoTextureMode::None) {
        fail("textured material manifest has an invalid albedo_mode");
        return result;
      }
      if (stringMember(root, "mip_policy") != "runtime_generate") {
        fail("textured material manifest must use runtime_generate mip_policy");
        return result;
      }
    }

    // A material-cell atlas supplies every texture input to the shader, so a
    // partial map or unbound renderable primitive could sample old coordinates.
    const bool requireCoverage = result.materialCells ||
      boolMember(root, "require_material_coverage", false);
    if (result.materialCells && hasRenderablePrimitiveWithoutMaterial) {
      fail("material-cell manifest requires every renderable primitive to bind a material");
      return result;
    }
    std::vector<bool> seen(materialNames.size(), false);
    const JsonValue& bindings = member(root, "materials");
    if (bindings.type != JsonValue::Type::Array) {
      fail("material manifest needs a materials array");
      return result;
    }
    for (const JsonValue& value : bindings.array) {
      int index = -1;
      const std::string expectedName = stringMember(value, "name");
      const float tintWeight = floatMember(value, "flat_tint_weight", 0.0F);
      if (
        value.type != JsonValue::Type::Object ||
        !exactIntMember(value, "index", index) ||
        index < 0 || static_cast<std::size_t>(index) >= materialNames.size() ||
        expectedName.empty() ||
        materialNames[static_cast<std::size_t>(index)] != expectedName ||
        seen[static_cast<std::size_t>(index)] ||
        !std::isfinite(tintWeight) || tintWeight < 0.0F || tintWeight > 1.0F
      ) {
        fail("material manifest has an invalid material binding");
        return result;
      }
      GltfMaterialBinding binding;
      binding.materialIndex = index;
      binding.expectedName = expectedName;
      binding.flatTintWeight = normalizedByte(tintWeight);
      if (result.materialCells) {
        const JsonValue& cell = member(value, "cell");
        int column = -1;
        int row = -1;
        if (
          !exactIntAt(cell, 0, column) ||
          !exactIntAt(cell, 1, row) ||
          column < 0 || row < 0 ||
          static_cast<std::uint32_t>(column) >= result.atlasColumns ||
          static_cast<std::uint32_t>(row) >= result.atlasRows
        ) {
          fail("material manifest has an invalid atlas cell");
          return result;
        }
        binding.atlasU = (static_cast<float>(column) + 0.5F) /
          static_cast<float>(result.atlasColumns);
        binding.atlasV = (static_cast<float>(row) + 0.5F) /
          static_cast<float>(result.atlasRows);
      }
      seen[static_cast<std::size_t>(index)] = true;
      result.bindings.push_back(std::move(binding));
    }
    if (
      requireCoverage &&
      std::any_of(seen.begin(), seen.end(), [](bool value) { return !value; })
    ) {
      fail("material manifest does not cover every GLB material");
      return result;
    }
    result.status = GltfMaterialManifestStatus::Valid;
    result.diagnostic = "valid model-local material manifest";
  } catch (...) {
    fail("material manifest is malformed");
  }
  return result;
}

[[nodiscard]] const GltfMaterialBinding* materialBinding(
  const GltfMaterialMetadata& metadata,
  int materialIndex
) {
  const auto found = std::find_if(
    metadata.bindings.begin(),
    metadata.bindings.end(),
    [materialIndex](const GltfMaterialBinding& binding) {
      return binding.materialIndex == materialIndex;
    }
  );
  return found == metadata.bindings.end() ? nullptr : &*found;
}

[[nodiscard]] bool legacyDuelistClothMaterial(std::string_view materialName) {
  // The pre-manifest Duelist marks its flat team-tinted cloth by name.
  return materialName == "MAT_ClothPrimary" ||
    materialName == "MAT_ClothAccent";
}

[[nodiscard]] GltfSkinnedModel::Matrix4 identityMatrix() {
  return {};
}

[[nodiscard]] GltfSkinnedModel::Matrix4 multiply(
  const GltfSkinnedModel::Matrix4& lhs,
  const GltfSkinnedModel::Matrix4& rhs
) {
  GltfSkinnedModel::Matrix4 result;
  for (int row = 0; row < 4; ++row) {
    for (int col = 0; col < 4; ++col) {
      float value = 0.0F;
      for (int index = 0; index < 4; ++index) {
        value += lhs.values[static_cast<std::size_t>(row * 4 + index)] *
          rhs.values[static_cast<std::size_t>(index * 4 + col)];
      }
      result.values[static_cast<std::size_t>(row * 4 + col)] = value;
    }
  }
  return result;
}

[[nodiscard]] Vec3 transformPoint(
  const GltfSkinnedModel::Matrix4& matrix,
  Vec3 point
) {
  return {
    matrix.values[0] * point.x + matrix.values[1] * point.y +
      matrix.values[2] * point.z + matrix.values[3],
    matrix.values[4] * point.x + matrix.values[5] * point.y +
      matrix.values[6] * point.z + matrix.values[7],
    matrix.values[8] * point.x + matrix.values[9] * point.y +
      matrix.values[10] * point.z + matrix.values[11],
  };
}

[[nodiscard]] std::array<float, 4> normalizeQuat(std::array<float, 4> quat) {
  const float length = std::sqrt(
    quat[0] * quat[0] +
      quat[1] * quat[1] +
      quat[2] * quat[2] +
      quat[3] * quat[3]
  );
  if (length <= 0.00001F) {
    return {0.0F, 0.0F, 0.0F, 1.0F};
  }
  return {quat[0] / length, quat[1] / length, quat[2] / length, quat[3] / length};
}

[[nodiscard]] std::array<float, 4> multiplyQuat(
  const std::array<float, 4>& lhs,
  const std::array<float, 4>& rhs
) {
  return normalizeQuat({
    lhs[3] * rhs[0] + lhs[0] * rhs[3] + lhs[1] * rhs[2] - lhs[2] * rhs[1],
    lhs[3] * rhs[1] - lhs[0] * rhs[2] + lhs[1] * rhs[3] + lhs[2] * rhs[0],
    lhs[3] * rhs[2] + lhs[0] * rhs[1] - lhs[1] * rhs[0] + lhs[2] * rhs[3],
    lhs[3] * rhs[3] - lhs[0] * rhs[0] - lhs[1] * rhs[1] - lhs[2] * rhs[2],
  });
}

[[nodiscard]] std::array<float, 4> localXAxisRotation(float radians) {
  const float half = radians * 0.5F;
  return {std::sin(half), 0.0F, 0.0F, std::cos(half)};
}

[[nodiscard]] std::array<float, 4> slerp(
  std::array<float, 4> lhs,
  std::array<float, 4> rhs,
  float amount
) {
  lhs = normalizeQuat(lhs);
  rhs = normalizeQuat(rhs);
  float cosine =
    lhs[0] * rhs[0] + lhs[1] * rhs[1] + lhs[2] * rhs[2] + lhs[3] * rhs[3];
  if (cosine < 0.0F) {
    rhs = {-rhs[0], -rhs[1], -rhs[2], -rhs[3]};
    cosine = -cosine;
  }
  if (cosine > 0.9995F) {
    return normalizeQuat({
      lhs[0] + (rhs[0] - lhs[0]) * amount,
      lhs[1] + (rhs[1] - lhs[1]) * amount,
      lhs[2] + (rhs[2] - lhs[2]) * amount,
      lhs[3] + (rhs[3] - lhs[3]) * amount,
    });
  }
  const float angle = std::acos(std::clamp(cosine, -1.0F, 1.0F));
  const float sine = std::sin(angle);
  const float lhsScale = std::sin((1.0F - amount) * angle) / sine;
  const float rhsScale = std::sin(amount * angle) / sine;
  return {
    lhs[0] * lhsScale + rhs[0] * rhsScale,
    lhs[1] * lhsScale + rhs[1] * rhsScale,
    lhs[2] * lhsScale + rhs[2] * rhsScale,
    lhs[3] * lhsScale + rhs[3] * rhsScale,
  };
}

[[nodiscard]] Vec3 lerp(Vec3 lhs, Vec3 rhs, float amount) {
  return lhs + (rhs - lhs) * amount;
}

[[nodiscard]] GltfSkinnedModel::Matrix4 trsMatrix(
  Vec3 translation,
  std::array<float, 4> rotation,
  Vec3 scale
) {
  rotation = normalizeQuat(rotation);
  const float x = rotation[0];
  const float y = rotation[1];
  const float z = rotation[2];
  const float w = rotation[3];
  const float xx = x * x;
  const float yy = y * y;
  const float zz = z * z;
  const float xy = x * y;
  const float xz = x * z;
  const float yz = y * z;
  const float wx = w * x;
  const float wy = w * y;
  const float wz = w * z;

  GltfSkinnedModel::Matrix4 matrix;
  matrix.values = {
    (1.0F - 2.0F * (yy + zz)) * scale.x,
    (2.0F * (xy - wz)) * scale.y,
    (2.0F * (xz + wy)) * scale.z,
    translation.x,
    (2.0F * (xy + wz)) * scale.x,
    (1.0F - 2.0F * (xx + zz)) * scale.y,
    (2.0F * (yz - wx)) * scale.z,
    translation.y,
    (2.0F * (xz - wy)) * scale.x,
    (2.0F * (yz + wx)) * scale.y,
    (1.0F - 2.0F * (xx + yy)) * scale.z,
    translation.z,
    0.0F,
    0.0F,
    0.0F,
    1.0F,
  };
  return matrix;
}

[[nodiscard]] GltfSkinnedModel::Matrix4 matrixFromColumnMajor(
  const std::vector<float>& values,
  std::size_t index
) {
  const std::size_t offset = index * 16U;
  GltfSkinnedModel::Matrix4 matrix;
  for (int row = 0; row < 4; ++row) {
    for (int col = 0; col < 4; ++col) {
      matrix.values[static_cast<std::size_t>(row * 4 + col)] =
        values[offset + static_cast<std::size_t>(col * 4 + row)];
    }
  }
  return matrix;
}

template <typename Value>
[[nodiscard]] Value sampleChannelValue(
  const std::vector<float>& times,
  const std::vector<Value>& outputs,
  float time,
  GltfSkinnedModel::Interpolation interpolation,
  Value fallback
) {
  if (times.empty() || outputs.empty()) {
    return fallback;
  }
  if (time <= times.front()) {
    return outputs.front();
  }
  if (time >= times.back()) {
    return outputs.back();
  }
  auto upper = std::upper_bound(times.begin(), times.end(), time);
  const std::size_t next = static_cast<std::size_t>(upper - times.begin());
  const std::size_t previous = next == 0U ? 0U : next - 1U;
  if (interpolation == GltfSkinnedModel::Interpolation::Step) {
    return outputs[previous];
  }
  const float span = std::max(0.0001F, times[next] - times[previous]);
  const float amount = (time - times[previous]) / span;
  if constexpr (std::is_same_v<Value, Vec3>) {
    return lerp(outputs[previous], outputs[next], amount);
  } else {
    return slerp(outputs[previous], outputs[next], amount);
  }
}

[[nodiscard]] Vec3 skinVertex(
  const GltfSkinnedModel::JointVertex& vertex,
  const std::vector<GltfSkinnedModel::Matrix4>& jointMatrices
) {
  Vec3 result = {};
  float totalWeight = 0.0F;
  for (std::size_t index = 0; index < vertex.joints.size(); ++index) {
    const float weight = vertex.weights[index];
    if (weight <= 0.000001F) {
      continue;
    }
    const std::uint16_t joint = vertex.joints[index];
    if (static_cast<std::size_t>(joint) >= jointMatrices.size()) {
      continue;
    }
    result += transformPoint(jointMatrices[joint], vertex.position) * weight;
    totalWeight += weight;
  }
  // Invalid or absent influences leave the bind-pose vertex intact rather than
  // collapsing malformed optional skin data to the model origin.
  return totalWeight > 0.0F ? result : vertex.position;
}

void resolveGlobalMatrix(
  std::size_t index,
  const std::vector<GltfSkinnedModel::Node>& nodes,
  const std::vector<GltfSkinnedModel::Matrix4>& localMatrices,
  std::vector<GltfSkinnedModel::Matrix4>& globalMatrices,
  std::vector<bool>& resolved
) {
  if (index >= localMatrices.size() || resolved[index]) {
    return;
  }

  const int parent = nodes[index].parent;
  if (parent >= 0 && static_cast<std::size_t>(parent) < localMatrices.size()) {
    // Resolve parents recursively, then compose parent * local. Reversing this
    // order applies child transforms in world space and breaks the joint hierarchy.
    resolveGlobalMatrix(
      static_cast<std::size_t>(parent),
      nodes,
      localMatrices,
      globalMatrices,
      resolved
    );
    globalMatrices[index] =
      multiply(globalMatrices[static_cast<std::size_t>(parent)], localMatrices[index]);
  } else {
    globalMatrices[index] = localMatrices[index];
  }
  resolved[index] = true;
}

[[nodiscard]] std::optional<std::size_t> animationIndex(
  const std::vector<GltfSkinnedModel::Animation>& animations,
  std::string_view name
) {
  for (std::size_t index = 0; index < animations.size(); ++index) {
    if (animations[index].name == name) {
      return index;
    }
  }
  return std::nullopt;
}

[[nodiscard]] bool upperBodyPoseIncludesNode(std::string_view name) {
  // Upper-body animation layers start at the spine so locomotion remains
  // authoritative for root, pelvis, and leg motion.
  return name == "spine_01" ||
    name == "spine_02" ||
    name == "neck" ||
    name == "head" ||
    name == "upper_arm_l" ||
    name == "lower_arm_l" ||
    name == "hand_l" ||
    name == "upper_arm_r" ||
    name == "lower_arm_r" ||
    name == "hand_r" ||
    name == "weapon_socket_r" ||
    name == "weapon_socket" ||
    name == "tag_weapon";
}

[[nodiscard]] bool poseIncludesNode(
  SkinnedModelPoseMask mask,
  const std::vector<GltfSkinnedModel::Node>& nodes,
  int nodeIndex
) {
  if (
    nodeIndex < 0 ||
    static_cast<std::size_t>(nodeIndex) >= nodes.size()
  ) {
    return false;
  }
  if (mask == SkinnedModelPoseMask::FullBody) {
    return true;
  }
  return upperBodyPoseIncludesNode(nodes[static_cast<std::size_t>(nodeIndex)].name);
}

[[nodiscard]] std::vector<std::uint8_t> readFile(std::string_view path) {
  std::ifstream file(std::string(path), std::ios::binary);
  if (!file) {
    return {};
  }
  file.seekg(0, std::ios::end);
  const std::streamoff size = file.tellg();
  file.seekg(0, std::ios::beg);
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  file.read(reinterpret_cast<char*>(bytes.data()), size);
  return bytes;
}

} // namespace

bool GltfSkinnedModel::load(std::string_view path) {
  const std::vector<std::uint8_t> bytes = readFile(path);
  if (bytes.size() < 20U || readU32(bytes, 0) != 0x46546C67U) {
    return false;
  }

  std::string jsonText;
  std::vector<std::uint8_t> binaryChunk;
  std::size_t offset = 12U;
  while (offset + 8U <= bytes.size()) {
    const std::uint32_t chunkLength = readU32(bytes, offset);
    const std::uint32_t chunkType = readU32(bytes, offset + 4U);
    offset += 8U;
    if (offset + chunkLength > bytes.size()) {
      return false;
    }
    if (chunkType == 0x4E4F534AU) {
      jsonText.assign(
        reinterpret_cast<const char*>(bytes.data() + offset),
        chunkLength
      );
    } else if (chunkType == 0x004E4942U) {
      binaryChunk.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                         bytes.begin() + static_cast<std::ptrdiff_t>(offset + chunkLength));
    }
    offset += chunkLength;
  }
  if (jsonText.empty() || binaryChunk.empty()) {
    return false;
  }
  while (!jsonText.empty() && (jsonText.back() == '\0' || jsonText.back() == ' ')) {
    jsonText.pop_back();
  }

  try {
    const JsonValue root = JsonParser(jsonText).parse();
    const JsonValue& nodesJson = member(root, "nodes");
    nodes_.clear();
    nodes_.reserve(nodesJson.array.size());
    for (const JsonValue& nodeJson : nodesJson.array) {
      Node node;
      node.name = stringMember(nodeJson, "name");
      node.mesh = intMember(nodeJson, "mesh");
      node.skin = intMember(nodeJson, "skin");
      const JsonValue& translation = member(nodeJson, "translation");
      node.translation = {
        floatAt(translation, 0, 0.0F),
        floatAt(translation, 1, 0.0F),
        floatAt(translation, 2, 0.0F),
      };
      const JsonValue& rotation = member(nodeJson, "rotation");
      node.rotation = {
        floatAt(rotation, 0, 0.0F),
        floatAt(rotation, 1, 0.0F),
        floatAt(rotation, 2, 0.0F),
        floatAt(rotation, 3, 1.0F),
      };
      const JsonValue& scale = member(nodeJson, "scale");
      node.scale = {
        floatAt(scale, 0, 1.0F),
        floatAt(scale, 1, 1.0F),
        floatAt(scale, 2, 1.0F),
      };
      nodes_.push_back(node);
    }
    for (std::size_t nodeIndex = 0; nodeIndex < nodesJson.array.size(); ++nodeIndex) {
      const JsonValue& children = member(nodesJson.array[nodeIndex], "children");
      if (children.type != JsonValue::Type::Array) {
        continue;
      }
      for (const JsonValue& child : children.array) {
        if (child.type == JsonValue::Type::Number) {
          const int childIndex = static_cast<int>(child.number);
          if (childIndex >= 0 && static_cast<std::size_t>(childIndex) < nodes_.size()) {
            nodes_[static_cast<std::size_t>(childIndex)].parent =
              static_cast<int>(nodeIndex);
          }
        }
      }
    }

    const JsonValue& skins = member(root, "skins");
    const JsonValue& skin = at(skins, 0);
    joints_.clear();
    const JsonValue& jointsJson = member(skin, "joints");
    for (const JsonValue& joint : jointsJson.array) {
      if (joint.type == JsonValue::Type::Number) {
        joints_.push_back(static_cast<int>(joint.number));
      }
    }
    hasSkin_ = !joints_.empty();
    const int inverseBindAccessor = intMember(skin, "inverseBindMatrices");
    const std::vector<float> inverseBindFloats =
      readAccessorFloats(root, binaryChunk, inverseBindAccessor);
    inverseBindMatrices_.clear();
    for (std::size_t index = 0; index + 15U < inverseBindFloats.size(); index += 16U) {
      inverseBindMatrices_.push_back(
        matrixFromColumnMajor(inverseBindFloats, index / 16U)
      );
    }

    sourceTriangles_.clear();
    primitives_.clear();
    hasSkinnedPrimitives_ = false;
    localBounds_ = {};
    bool localBoundsInitialized = false;
    const JsonValue& meshes = member(root, "meshes");
    const JsonValue& materials = member(root, "materials");
    materialNames_.clear();
    materialNames_.reserve(materials.array.size());
    for (const JsonValue& material : materials.array) {
      materialNames_.push_back(stringMember(material, "name"));
    }
    std::vector<bool> meshHasSkinNode(meshes.array.size(), false);
    for (const Node& node : nodes_) {
      if (
        node.mesh >= 0 &&
        node.skin >= 0 &&
        static_cast<std::size_t>(node.mesh) < meshHasSkinNode.size()
      ) {
        meshHasSkinNode[static_cast<std::size_t>(node.mesh)] = true;
      }
    }
    bool hasRenderablePrimitiveWithoutMaterial = false;
    for (std::size_t meshIndex = 0; meshIndex < meshes.array.size(); ++meshIndex) {
      if (!meshHasSkinNode.empty() && !meshHasSkinNode[meshIndex]) {
        continue;
      }
      const JsonValue& primitives = member(meshes.array[meshIndex], "primitives");
      for (const JsonValue& primitiveJson : primitives.array) {
        const int materialIndex = intMember(primitiveJson, "material");
        if (
          intMember(primitiveJson, "mode", 4) == 4 &&
          (materialIndex < 0 ||
            static_cast<std::size_t>(materialIndex) >= materialNames_.size())
        ) {
          hasRenderablePrimitiveWithoutMaterial = true;
          break;
        }
      }
      if (hasRenderablePrimitiveWithoutMaterial) {
        break;
      }
    }
    materialMetadata_ = loadMaterialMetadata(
      path,
      materialNames_,
      sha256Hex(bytes),
      hasRenderablePrimitiveWithoutMaterial
    );
    for (std::size_t meshIndex = 0; meshIndex < meshes.array.size(); ++meshIndex) {
      if (!meshHasSkinNode.empty() && !meshHasSkinNode[meshIndex]) {
        continue;
      }
      const JsonValue& mesh = meshes.array[meshIndex];
      const JsonValue& primitives = member(mesh, "primitives");
      for (const JsonValue& primitiveJson : primitives.array) {
        if (intMember(primitiveJson, "mode", 4) != 4) {
          continue;
        }
        const JsonValue& attributes = member(primitiveJson, "attributes");
        const int positionAccessor = intMember(attributes, "POSITION");
        const int normalAccessor = intMember(attributes, "NORMAL");
        const int texCoordAccessor = intMember(attributes, "TEXCOORD_0");
        const int jointsAccessor = intMember(attributes, "JOINTS_0");
        const int weightsAccessor = intMember(attributes, "WEIGHTS_0");
        const int indicesAccessor = intMember(primitiveJson, "indices");
        const int materialIndex = intMember(primitiveJson, "material");
        const JsonValue& material = at(materials, materialIndex);
        const GltfMaterialBinding* binding = materialBinding(
          materialMetadata_,
          materialIndex
        );
        const std::uint8_t fallbackTintWeight =
          !materialMetadata_.valid() &&
            legacyDuelistClothMaterial(stringMember(material, "name"))
          ? 255U
          : 0U;
        const std::uint8_t tintWeight = binding != nullptr
          ? binding->flatTintWeight
          : fallbackTintWeight;

        const std::vector<float> positions =
          readAccessorFloats(root, binaryChunk, positionAccessor);
        if (positions.size() < 3U) {
          continue;
        }
        const std::vector<float> normals =
          readAccessorFloats(root, binaryChunk, normalAccessor);
        const std::vector<float> texCoords =
          readAccessorFloats(root, binaryChunk, texCoordAccessor);
        const std::vector<std::uint32_t> joints =
          readAccessorU32(root, binaryChunk, jointsAccessor);
        const std::vector<float> weights =
          readAccessorFloats(root, binaryChunk, weightsAccessor);
        std::vector<std::uint32_t> indices =
          readAccessorU32(root, binaryChunk, indicesAccessor);

        Primitive primitive;
        primitive.color = materialColor(material, materialMetadata_.forceOpaque);
        primitive.tintable = tintWeight > 0U;
        primitive.materialIndex = materialIndex;
        const JsonValue& pbr = member(material, "pbrMetallicRoughness");
        primitive.roughnessFactor = std::clamp(
          floatMember(pbr, "roughnessFactor", 1.0F),
          0.0F,
          1.0F
        );
        primitive.metallicFactor = std::clamp(
          floatMember(pbr, "metallicFactor", 0.0F),
          0.0F,
          1.0F
        );
        const JsonValue& emissive = member(material, "emissiveFactor");
        primitive.emissiveFactor = {
          std::clamp(floatAt(emissive, 0, 0.0F), 0.0F, 1.0F),
          std::clamp(floatAt(emissive, 1, 0.0F), 0.0F, 1.0F),
          std::clamp(floatAt(emissive, 2, 0.0F), 0.0F, 1.0F),
        };
        primitive.opaque = primitive.color.alpha == 255U;
        const std::size_t vertexCount = positions.size() / 3U;
        primitive.vertices.reserve(vertexCount);
        bool primitiveBoundsInitialized = false;
        for (std::size_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex) {
          GpuVertex vertex;
          vertex.position = vec3FromFloats(positions, vertexIndex);
          vertex.normal = normalize(vec3FromFloatsOr(normals, vertexIndex, {0.0F, 0.0F, 1.0F}));
          const auto uv = vec2FromFloatsOr(texCoords, vertexIndex, {0.0F, 0.0F});
          vertex.u = materialMetadata_.materialCells && binding != nullptr
            ? binding->atlasU
            : uv[0];
          vertex.v = materialMetadata_.materialCells && binding != nullptr
            ? binding->atlasV
            : uv[1];
          vertex.color = primitive.color;
          vertex.tintWeight = tintWeight;
          const bool samplesAuthoredTexture = materialMetadata_.hasAuthoredTextures() &&
            (!materialMetadata_.materialCells || binding != nullptr);
          vertex.albedoTextureMode = samplesAuthoredTexture
            ? gpuAlbedoTextureMode(materialMetadata_.albedoMode)
            : 0U;
          std::array<float, 4> rawWeights = {};
          for (std::size_t jointIndex = 0; jointIndex < 4U; ++jointIndex) {
            const std::size_t sourceIndex = vertexIndex * 4U + jointIndex;
            if (sourceIndex < joints.size()) {
              vertex.joints[jointIndex] = static_cast<std::uint16_t>(
                std::min<std::uint32_t>(joints[sourceIndex], 0xFFFFU)
              );
            }
            if (sourceIndex < weights.size()) {
              rawWeights[jointIndex] = weights[sourceIndex];
            }
          }
          vertex.weights = normalizedWeights(rawWeights);
          for (float weight : vertex.weights) {
            primitive.skinned = primitive.skinned || weight > 0.0F;
          }
          primitive.vertices.push_back(vertex);
          expandBounds(primitive.localBounds, vertex.position, primitiveBoundsInitialized);
          expandBounds(localBounds_, vertex.position, localBoundsInitialized);
        }
        primitive.skinned = primitive.skinned && hasSkin_;
        hasSkinnedPrimitives_ = hasSkinnedPrimitives_ || primitive.skinned;

        if (indices.empty()) {
          indices.reserve(vertexCount);
          for (std::uint32_t index = 0; index < vertexCount; ++index) {
            indices.push_back(index);
          }
        }
        for (std::uint32_t index : indices) {
          if (index < primitive.vertices.size()) {
            primitive.indices.push_back(index);
          }
        }
        if (primitive.indices.size() < 3U || primitive.vertices.empty()) {
          continue;
        }

        for (std::size_t index = 0; index + 2U < primitive.indices.size(); index += 3U) {
          SourceTriangle triangle;
          triangle.color = primitive.color;
          triangle.tintable = primitive.tintable;
          for (std::size_t corner = 0; corner < 3U; ++corner) {
            const GpuVertex& source =
              primitive.vertices[primitive.indices[index + corner]];
            triangle.vertices[corner] = {
              source.position,
              source.joints,
              source.weights,
            };
          }
          sourceTriangles_.push_back(triangle);
        }
        primitives_.push_back(std::move(primitive));
      }
    }
    if (!localBoundsInitialized) {
      localBounds_ = {};
    }

    animations_.clear();
    animationNames_.clear();
    const JsonValue& animationsJson = member(root, "animations");
    for (const JsonValue& animationJson : animationsJson.array) {
      Animation animation;
      animation.name = stringMember(animationJson, "name");
      const JsonValue& samplers = member(animationJson, "samplers");
      const JsonValue& channels = member(animationJson, "channels");
      for (const JsonValue& channelJson : channels.array) {
        const int samplerIndex = intMember(channelJson, "sampler");
        const JsonValue& samplerJson = at(samplers, samplerIndex);
        const JsonValue& target = member(channelJson, "target");
        AnimationChannel channel;
        channel.node = intMember(target, "node");
        const std::string pathName = stringMember(target, "path");
        channel.path = pathName == "rotation"
          ? ChannelPath::Rotation
          : pathName == "scale" ? ChannelPath::Scale : ChannelPath::Translation;
        channel.interpolation = stringMember(samplerJson, "interpolation", "LINEAR") == "STEP"
          ? Interpolation::Step
          : Interpolation::Linear;
        channel.inputTimes = readAccessorFloats(
          root,
          binaryChunk,
          intMember(samplerJson, "input")
        );
        if (!channel.inputTimes.empty()) {
          animation.duration = std::max(animation.duration, channel.inputTimes.back());
        }
        const std::vector<float> outputFloats = readAccessorFloats(
          root,
          binaryChunk,
          intMember(samplerJson, "output")
        );
        if (channel.path == ChannelPath::Rotation) {
          for (std::size_t index = 0; index + 3U < outputFloats.size(); index += 4U) {
            channel.rotationOutputs.push_back({
              outputFloats[index],
              outputFloats[index + 1U],
              outputFloats[index + 2U],
              outputFloats[index + 3U],
            });
          }
        } else {
          for (std::size_t index = 0; index + 2U < outputFloats.size(); index += 3U) {
            channel.vec3Outputs.push_back({
              outputFloats[index],
              outputFloats[index + 1U],
              outputFloats[index + 2U],
            });
          }
        }
        animation.channels.push_back(std::move(channel));
      }
      animationNames_.push_back(animation.name);
      animations_.push_back(std::move(animation));
    }

    restTriangles_ = triangles({});
    sourcePath_ = std::string(path);
    loaded_ = true;
  } catch (...) {
    loaded_ = false;
    return false;
  }

  return loaded_;
}

bool GltfSkinnedModel::loaded() const {
  return loaded_;
}

std::string_view GltfSkinnedModel::sourcePath() const {
  return sourcePath_;
}

const std::vector<std::string>& GltfSkinnedModel::animationNames() const {
  return animationNames_;
}

const std::vector<std::string>& GltfSkinnedModel::materialNames() const {
  return materialNames_;
}

const GltfMaterialMetadata& GltfSkinnedModel::materialMetadata() const {
  return materialMetadata_;
}

const std::vector<GltfSkinnedModel::Primitive>& GltfSkinnedModel::primitives() const {
  return primitives_;
}

GltfModelBounds GltfSkinnedModel::localBounds() const {
  return localBounds_;
}

std::uint32_t GltfSkinnedModel::jointCount() const {
  return static_cast<std::uint32_t>(joints_.size());
}

bool GltfSkinnedModel::hasSkin() const {
  return hasSkin_;
}

bool GltfSkinnedModel::hasSkinnedPrimitives() const {
  return hasSkinnedPrimitives_;
}

bool GltfSkinnedModel::appendBonePalette(
  std::span<const SkinnedModelPoseRequest> poses,
  std::vector<std::array<float, 16>>& out,
  PoseScratch& scratch,
  float upperBodyAimPitchRadians
) const {
  if (!loaded_) {
    return false;
  }
  if (joints_.empty()) {
    out.push_back(identityMatrix().values);
    return true;
  }

  scratch.sampledNodes.clear();
  scratch.sampledNodes.reserve(nodes_.size());
  for (const Node& node : nodes_) {
    scratch.sampledNodes.push_back({node.translation, node.rotation, node.scale});
  }

  for (const SkinnedModelPoseRequest& pose : poses) {
    // Pose requests are layered in caller order. Each weight blends from the
    // result accumulated so far, allowing locomotion plus masked upper-body aim.
    const std::optional<std::size_t> found =
      animationIndex(animations_, pose.animationName);
    if (!found) {
      continue;
    }
    const Animation& animation = animations_[*found];
    const float duration = std::max(0.0001F, animation.duration);
    const float time = std::clamp(pose.timeSeconds, 0.0F, duration);
    const float weight = std::clamp(pose.weight, 0.0F, 1.0F);
    if (weight <= 0.0001F) {
      continue;
    }
    for (const AnimationChannel& channel : animation.channels) {
      if (
        channel.node < 0 ||
        static_cast<std::size_t>(channel.node) >= scratch.sampledNodes.size() ||
        !poseIncludesNode(pose.mask, nodes_, channel.node)
      ) {
        continue;
      }
      NodePose& node = scratch.sampledNodes[static_cast<std::size_t>(channel.node)];
      if (channel.path == ChannelPath::Rotation) {
        node.rotation = slerp(
          node.rotation,
          sampleChannelValue(
            channel.inputTimes,
            channel.rotationOutputs,
            time,
            channel.interpolation,
            node.rotation
          ),
          weight
        );
      } else if (channel.path == ChannelPath::Scale) {
        node.scale = lerp(
          node.scale,
          sampleChannelValue(
            channel.inputTimes,
            channel.vec3Outputs,
            time,
            channel.interpolation,
            node.scale
          ),
          weight
        );
      } else {
        node.translation = lerp(
          node.translation,
          sampleChannelValue(
            channel.inputTimes,
            channel.vec3Outputs,
            time,
            channel.interpolation,
            node.translation
          ),
          weight
        );
      }
    }
  }

  const float aimPitch = std::clamp(upperBodyAimPitchRadians, -0.78539816F, 0.78539816F);
  if (std::fabs(aimPitch) > 0.0001F) {
    for (std::size_t index = 0; index < nodes_.size(); ++index) {
      float share = 0.0F;
      if (nodes_[index].name == "spine_01") share = 0.35F;
      else if (nodes_[index].name == "spine_02") share = 0.40F;
      else if (nodes_[index].name == "neck") share = 0.15F;
      else if (nodes_[index].name == "head") share = 0.10F;
      if (share <= 0.0F) continue;
      // The duelist maps local +X to model-right. Negative local-X rotation
      // pitches its +Z forward axis upward while preserving locomotion in legs.
      scratch.sampledNodes[index].rotation = multiplyQuat(
        scratch.sampledNodes[index].rotation,
        localXAxisRotation(-aimPitch * share)
      );
    }
  }

  scratch.localMatrices.clear();
  scratch.localMatrices.reserve(scratch.sampledNodes.size());
  for (const NodePose& node : scratch.sampledNodes) {
    scratch.localMatrices.push_back(trsMatrix(node.translation, node.rotation, node.scale));
  }
  scratch.globalMatrices.assign(scratch.localMatrices.size(), identityMatrix());
  scratch.resolved.assign(scratch.localMatrices.size(), false);
  for (std::size_t index = 0; index < scratch.localMatrices.size(); ++index) {
    resolveGlobalMatrix(
      index,
      nodes_,
      scratch.localMatrices,
      scratch.globalMatrices,
      scratch.resolved
    );
  }

  const std::size_t firstOut = out.size();
  out.reserve(out.size() + joints_.size());
  for (std::size_t index = 0; index < joints_.size(); ++index) {
    const int jointNode = joints_[index];
    const Matrix4 inverseBind = index < inverseBindMatrices_.size()
      ? inverseBindMatrices_[index]
      : identityMatrix();
    Matrix4 jointMatrix = identityMatrix();
    if (jointNode >= 0 && static_cast<std::size_t>(jointNode) < scratch.globalMatrices.size()) {
      // glTF palette entries are current joint global * inverse bind. Reversing
      // this product moves joints through the wrong coordinate space.
      jointMatrix = multiply(
        scratch.globalMatrices[static_cast<std::size_t>(jointNode)],
        inverseBind
      );
    }
    bool finite = true;
    for (float value : jointMatrix.values) {
      finite = finite && std::isfinite(value);
    }
    // Contain malformed asset math per joint so one NaN cannot poison the whole
    // palette, GPU vertex output, or frame.
    out.push_back(finite ? jointMatrix.values : identityMatrix().values);
  }
  return out.size() > firstOut;
}

std::vector<SkinnedModelTriangle> GltfSkinnedModel::triangles(
  const std::vector<SkinnedModelPoseRequest>& poses
) const {
  if (sourceTriangles_.empty()) {
    return {};
  }

  std::vector<NodePose> sampledNodes;
  sampledNodes.reserve(nodes_.size());
  for (const Node& node : nodes_) {
    sampledNodes.push_back({node.translation, node.rotation, node.scale});
  }

  for (const SkinnedModelPoseRequest& pose : poses) {
    const std::optional<std::size_t> found =
      animationIndex(animations_, pose.animationName);
    if (!found) {
      continue;
    }
    const Animation& animation = animations_[*found];
    const float duration = std::max(0.0001F, animation.duration);
    const float time = std::clamp(pose.timeSeconds, 0.0F, duration);
    const float weight = std::clamp(pose.weight, 0.0F, 1.0F);
    if (weight <= 0.0001F) {
      continue;
    }
    for (const AnimationChannel& channel : animation.channels) {
      if (
        channel.node < 0 ||
        static_cast<std::size_t>(channel.node) >= sampledNodes.size() ||
        !poseIncludesNode(pose.mask, nodes_, channel.node)
      ) {
        continue;
      }
      NodePose& node = sampledNodes[static_cast<std::size_t>(channel.node)];
      if (channel.path == ChannelPath::Rotation) {
        node.rotation = slerp(
          node.rotation,
          sampleChannelValue(
            channel.inputTimes,
            channel.rotationOutputs,
            time,
            channel.interpolation,
            node.rotation
          ),
          weight
        );
      } else if (channel.path == ChannelPath::Scale) {
        node.scale = lerp(
          node.scale,
          sampleChannelValue(
            channel.inputTimes,
            channel.vec3Outputs,
            time,
            channel.interpolation,
            node.scale
          ),
          weight
        );
      } else {
        node.translation = lerp(
          node.translation,
          sampleChannelValue(
            channel.inputTimes,
            channel.vec3Outputs,
            time,
            channel.interpolation,
            node.translation
          ),
          weight
        );
      }
    }
  }

  std::vector<Matrix4> localMatrices;
  localMatrices.reserve(sampledNodes.size());
  for (const NodePose& node : sampledNodes) {
    localMatrices.push_back(trsMatrix(node.translation, node.rotation, node.scale));
  }
  std::vector<Matrix4> globalMatrices(localMatrices.size(), identityMatrix());
  std::vector<bool> resolved(localMatrices.size(), false);
  for (std::size_t index = 0; index < localMatrices.size(); ++index) {
    resolveGlobalMatrix(index, nodes_, localMatrices, globalMatrices, resolved);
  }

  std::vector<Matrix4> jointMatrices;
  jointMatrices.reserve(joints_.size());
  for (std::size_t index = 0; index < joints_.size(); ++index) {
    const int jointNode = joints_[index];
    const Matrix4 inverseBind = index < inverseBindMatrices_.size()
      ? inverseBindMatrices_[index]
      : identityMatrix();
    jointMatrices.push_back(
      jointNode >= 0 && static_cast<std::size_t>(jointNode) < globalMatrices.size()
        ? multiply(globalMatrices[static_cast<std::size_t>(jointNode)], inverseBind)
        : identityMatrix()
    );
  }

  std::vector<SkinnedModelTriangle> result;
  result.reserve(sourceTriangles_.size());
  for (const SourceTriangle& source : sourceTriangles_) {
    SkinnedModelTriangle triangle;
    triangle.color = source.color;
    triangle.tintable = source.tintable;
    for (std::size_t corner = 0; corner < 3U; ++corner) {
      triangle.vertices[corner] = skinVertex(source.vertices[corner], jointMatrices);
    }
    result.push_back(triangle);
  }
  return result;
}

GltfSkinnedModel& duelistMaleModel() {
  static GltfSkinnedModel model;
  static bool attemptedLoad = false;
  if (!attemptedLoad) {
    attemptedLoad = true;
    constexpr std::array<std::string_view, 2> modelPaths = {{
      "assets/models/lg_duelist_male_v3/art/exports/lg_duelist_male.glb",
      "assets/models/lg_duelist_male_v2/art/exports/lg_duelist_male.glb",
    }};
    constexpr std::array<std::string_view, 4> prefixes = {{
      "",
      "../",
      "../../",
      "../../../",
    }};
    for (std::string_view modelPath : modelPaths) {
      for (std::string_view prefix : prefixes) {
        if (model.load(std::string(prefix) + std::string(modelPath))) {
          return model;
        }
      }
    }
  }
  return model;
}

bool GltfSkinnedModel::nodeGlobalMatrix(
  std::string_view nodeName,
  const PoseScratch& scratch,
  Matrix4& out
) const {
  for (std::size_t index = 0; index < nodes_.size(); ++index) {
    if (nodes_[index].name == nodeName && index < scratch.globalMatrices.size()) {
      out = scratch.globalMatrices[index];
      return true;
    }
  }
  return false;
}

GltfSkinnedModel& workerPlayerModel() {
  static GltfSkinnedModel model;
  static bool attemptedLoad = false;
  if (!attemptedLoad) {
    attemptedLoad = true;
    constexpr std::array<std::string_view, 4> prefixes = {{"", "../", "../../", "../../../"}};
    for (std::string_view prefix : prefixes) {
      if (model.load(std::string(prefix) + "assets/models/quaternius_worker/quaternius_worker.glb")) {
        return model;
      }
    }
  }
  return model;
}

} // namespace lg
