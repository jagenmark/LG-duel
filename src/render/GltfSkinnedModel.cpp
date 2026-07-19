#include "render/GltfSkinnedModel.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
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
    while (peek() != '\0') {
      const std::string key = parseString();
      skipWhitespace();
      if (take() != ':') {
        throw std::runtime_error("invalid json object");
      }
      value.object.emplace(key, parseValue());
      skipWhitespace();
      const char separator = take();
      if (separator == '}') {
        break;
      }
      if (separator != ',') {
        throw std::runtime_error("invalid json object separator");
      }
      skipWhitespace();
    }
    return value;
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
    while (peek() != '\0') {
      value.array.push_back(parseValue());
      skipWhitespace();
      const char separator = take();
      if (separator == ']') {
        break;
      }
      if (separator != ',') {
        throw std::runtime_error("invalid json array separator");
      }
      skipWhitespace();
    }
    return value;
  }

  std::string parseString() {
    if (take() != '"') {
      throw std::runtime_error("expected json string");
    }
    std::string result;
    while (peek() != '\0') {
      const char character = take();
      if (character == '"') {
        break;
      }
      if (character == '\\') {
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
        default:
          result.push_back(escaped);
          break;
        }
      } else {
        result.push_back(character);
      }
    }
    return result;
  }

  JsonValue parseNumber() {
    const std::size_t start = offset_;
    if (peek() == '-') {
      ++offset_;
    }
    while (std::isdigit(static_cast<unsigned char>(peek()))) {
      ++offset_;
    }
    if (peek() == '.') {
      ++offset_;
      while (std::isdigit(static_cast<unsigned char>(peek()))) {
        ++offset_;
      }
    }
    if (peek() == 'e' || peek() == 'E') {
      ++offset_;
      if (peek() == '-' || peek() == '+') {
        ++offset_;
      }
      while (std::isdigit(static_cast<unsigned char>(peek()))) {
        ++offset_;
      }
    }
    JsonValue value;
    value.type = JsonValue::Type::Number;
    value.number = std::stod(std::string(text_.substr(start, offset_ - start)));
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

[[nodiscard]] RenderColor materialColor(const JsonValue& material) {
  const JsonValue& pbr = member(material, "pbrMetallicRoughness");
  const JsonValue& factor = member(pbr, "baseColorFactor");
  return {
    static_cast<std::uint8_t>(std::clamp(floatAt(factor, 0, 1.0F) * 255.0F, 0.0F, 255.0F)),
    static_cast<std::uint8_t>(std::clamp(floatAt(factor, 1, 1.0F) * 255.0F, 0.0F, 255.0F)),
    static_cast<std::uint8_t>(std::clamp(floatAt(factor, 2, 1.0F) * 255.0F, 0.0F, 255.0F)),
    static_cast<std::uint8_t>(std::clamp(floatAt(factor, 3, 1.0F) * 255.0F, 0.0F, 255.0F)),
  };
}

[[nodiscard]] bool materialTintable(const JsonValue& material) {
  const std::string name = stringMember(material, "name");
  return name == "MAT_ClothPrimary" || name == "MAT_ClothAccent";
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
        primitive.color = materialColor(material);
        primitive.tintable = materialTintable(material);
        primitive.materialIndex = materialIndex;
        const std::size_t vertexCount = positions.size() / 3U;
        primitive.vertices.reserve(vertexCount);
        bool primitiveBoundsInitialized = false;
        for (std::size_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex) {
          GpuVertex vertex;
          vertex.position = vec3FromFloats(positions, vertexIndex);
          vertex.normal = normalize(vec3FromFloatsOr(normals, vertexIndex, {0.0F, 0.0F, 1.0F}));
          const auto uv = vec2FromFloatsOr(texCoords, vertexIndex, {0.0F, 0.0F});
          vertex.u = uv[0];
          vertex.v = uv[1];
          vertex.color = primitive.color;
          vertex.tintWeight = primitive.tintable ? 255U : 0U;
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
