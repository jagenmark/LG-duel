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
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct JsonValue {
  enum class Type { Null, Bool, Number, String, Array, Object };
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
    skipSpace();
    if (offset_ != text_.size()) throw std::runtime_error("trailing JSON data");
    return value;
  }

private:
  char peek() const { return offset_ < text_.size() ? text_[offset_] : '\0'; }
  char take() { return offset_ < text_.size() ? text_[offset_++] : '\0'; }
  void skipSpace() {
    while (peek() == ' ' || peek() == '\n' || peek() == '\r' || peek() == '\t') ++offset_;
  }
  void expect(char wanted) {
    skipSpace();
    if (take() != wanted) throw std::runtime_error("bad JSON token");
  }
  JsonValue parseValue() {
    skipSpace();
    if (peek() == '{') return parseObject();
    if (peek() == '[') return parseArray();
    if (peek() == '"') {
      JsonValue value;
      value.type = JsonValue::Type::String;
      value.string = parseString();
      return value;
    }
    if (text_.substr(offset_, 4) == "true") {
      offset_ += 4;
      JsonValue value;
      value.type = JsonValue::Type::Bool;
      value.boolean = true;
      return value;
    }
    if (text_.substr(offset_, 5) == "false") {
      offset_ += 5;
      JsonValue value;
      value.type = JsonValue::Type::Bool;
      return value;
    }
    if (text_.substr(offset_, 4) == "null") {
      offset_ += 4;
      return {};
    }
    return parseNumber();
  }
  JsonValue parseObject() {
    JsonValue value;
    value.type = JsonValue::Type::Object;
    expect('{');
    skipSpace();
    if (peek() == '}') { take(); return value; }
    while (true) {
      const std::string key = parseString();
      expect(':');
      value.object.emplace(key, parseValue());
      skipSpace();
      const char next = take();
      if (next == '}') break;
      if (next != ',') throw std::runtime_error("bad JSON object");
      skipSpace();
    }
    return value;
  }
  JsonValue parseArray() {
    JsonValue value;
    value.type = JsonValue::Type::Array;
    expect('[');
    skipSpace();
    if (peek() == ']') { take(); return value; }
    while (true) {
      value.array.push_back(parseValue());
      skipSpace();
      const char next = take();
      if (next == ']') break;
      if (next != ',') throw std::runtime_error("bad JSON array");
    }
    return value;
  }
  std::string parseString() {
    expect('"');
    std::string result;
    while (true) {
      const char ch = take();
      if (ch == '\0') throw std::runtime_error("unterminated JSON string");
      if (ch == '"') return result;
      if (ch != '\\') { result.push_back(ch); continue; }
      const char escaped = take();
      switch (escaped) {
        case '"': result.push_back('"'); break;
        case '\\': result.push_back('\\'); break;
        case '/': result.push_back('/'); break;
        case 'b': result.push_back('\b'); break;
        case 'f': result.push_back('\f'); break;
        case 'n': result.push_back('\n'); break;
        case 'r': result.push_back('\r'); break;
        case 't': result.push_back('\t'); break;
        default: throw std::runtime_error("unsupported JSON escape");
      }
    }
  }
  JsonValue parseNumber() {
    const std::size_t start = offset_;
    if (peek() == '-') take();
    while (peek() >= '0' && peek() <= '9') take();
    if (peek() == '.') { take(); while (peek() >= '0' && peek() <= '9') take(); }
    if (peek() == 'e' || peek() == 'E') {
      take();
      if (peek() == '+' || peek() == '-') take();
      while (peek() >= '0' && peek() <= '9') take();
    }
    if (start == offset_) throw std::runtime_error("bad JSON value");
    JsonValue value;
    value.type = JsonValue::Type::Number;
    value.number = std::stod(std::string(text_.substr(start, offset_ - start)));
    return value;
  }

  std::string_view text_;
  std::size_t offset_ = 0;
};

const JsonValue& member(const JsonValue& value, std::string_view key) {
  static const JsonValue empty;
  if (value.type != JsonValue::Type::Object) return empty;
  const auto found = value.object.find(std::string(key));
  return found == value.object.end() ? empty : found->second;
}

const JsonValue& at(const JsonValue& value, int index) {
  static const JsonValue empty;
  if (value.type != JsonValue::Type::Array || index < 0 ||
      static_cast<std::size_t>(index) >= value.array.size()) return empty;
  return value.array[static_cast<std::size_t>(index)];
}

int integer(const JsonValue& value, std::string_view key, int fallback = -1) {
  const JsonValue& item = member(value, key);
  return item.type == JsonValue::Type::Number ? static_cast<int>(item.number) : fallback;
}

bool boolean(const JsonValue& value, std::string_view key, bool fallback) {
  const JsonValue& item = member(value, key);
  return item.type == JsonValue::Type::Bool ? item.boolean : fallback;
}

std::vector<std::uint8_t> readBytes(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return {};
  file.seekg(0, std::ios::end);
  const std::streamoff size = file.tellg();
  if (size <= 0) return {};
  file.seekg(0, std::ios::beg);
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  file.read(reinterpret_cast<char*>(bytes.data()), size);
  return file ? bytes : std::vector<std::uint8_t>{};
}

std::uint32_t u32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
  if (offset + 4 > bytes.size()) throw std::runtime_error("short GLB data");
  return static_cast<std::uint32_t>(bytes[offset]) |
    (static_cast<std::uint32_t>(bytes[offset + 1]) << 8U) |
    (static_cast<std::uint32_t>(bytes[offset + 2]) << 16U) |
    (static_cast<std::uint32_t>(bytes[offset + 3]) << 24U);
}

struct GlbSource {
  JsonValue root;
  std::vector<std::uint8_t> binary;
};

GlbSource readGlb(const std::filesystem::path& path) {
  const std::vector<std::uint8_t> bytes = readBytes(path);
  if (bytes.size() < 20 || u32(bytes, 0) != 0x46546C67U || u32(bytes, 4) != 2U)
    throw std::runtime_error("not a glTF 2 GLB");
  std::string json;
  std::vector<std::uint8_t> binaryChunk;
  std::size_t offset = 12;
  while (offset + 8 <= bytes.size()) {
    const std::uint32_t length = u32(bytes, offset);
    const std::uint32_t type = u32(bytes, offset + 4);
    offset += 8;
    if (offset + length > bytes.size()) throw std::runtime_error("bad GLB chunk length");
    if (type == 0x4E4F534AU)
      json.assign(reinterpret_cast<const char*>(bytes.data() + offset), length);
    else if (type == 0x004E4942U)
      binaryChunk.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                         bytes.begin() + static_cast<std::ptrdiff_t>(offset + length));
    offset += length;
  }
  while (!json.empty() && (json.back() == '\0' || json.back() == ' ')) json.pop_back();
  if (json.empty() || binaryChunk.empty()) throw std::runtime_error("GLB lacks JSON or binary data");
  return {JsonParser(json).parse(), std::move(binaryChunk)};
}

std::size_t componentSize(int type) {
  switch (type) {
    case 5120: case 5121: return 1;
    case 5122: case 5123: return 2;
    case 5125: case 5126: return 4;
    default: return 0;
  }
}

std::size_t componentCount(std::string_view type) {
  if (type == "SCALAR") return 1;
  if (type == "VEC2") return 2;
  if (type == "VEC3") return 3;
  if (type == "VEC4") return 4;
  if (type == "MAT4") return 16;
  return 0;
}

double readComponent(const std::vector<std::uint8_t>& data, std::size_t offset,
                     int type, bool normalized) {
  if (offset + componentSize(type) > data.size()) throw std::runtime_error("accessor outside GLB");
  if (type == 5121) return normalized ? data[offset] / 255.0 : data[offset];
  if (type == 5120) {
    const auto value = static_cast<std::int8_t>(data[offset]);
    return normalized ? std::max(-1.0, value / 127.0) : value;
  }
  if (type == 5123) {
    const std::uint16_t value = static_cast<std::uint16_t>(data[offset]) |
      static_cast<std::uint16_t>(data[offset + 1] << 8U);
    return normalized ? value / 65535.0 : value;
  }
  if (type == 5122) {
    const std::uint16_t raw = static_cast<std::uint16_t>(data[offset]) |
      static_cast<std::uint16_t>(data[offset + 1] << 8U);
    const auto value = static_cast<std::int16_t>(raw);
    return normalized ? std::max(-1.0, value / 32767.0) : value;
  }
  if (type == 5125) return u32(data, offset);
  if (type == 5126) {
    const std::uint32_t raw = u32(data, offset);
    float value = 0.0F;
    static_assert(sizeof(value) == sizeof(raw));
    std::memcpy(&value, &raw, sizeof(value));
    return value;
  }
  throw std::runtime_error("unsupported accessor component type");
}

struct AccessorView {
  const std::vector<std::uint8_t>* data = nullptr;
  std::size_t offset = 0;
  std::size_t stride = 0;
  std::size_t count = 0;
  std::size_t components = 0;
  int componentType = 0;
  bool normalized = false;
};

AccessorView accessorView(const GlbSource& glb, int accessorIndex) {
  const JsonValue& accessor = at(member(glb.root, "accessors"), accessorIndex);
  const JsonValue& view = at(member(glb.root, "bufferViews"), integer(accessor, "bufferView"));
  AccessorView result;
  result.data = &glb.binary;
  result.componentType = integer(accessor, "componentType");
  result.components = componentCount(member(accessor, "type").string);
  result.count = static_cast<std::size_t>(std::max(0, integer(accessor, "count", 0)));
  result.normalized = boolean(accessor, "normalized", false);
  result.offset = static_cast<std::size_t>(std::max(0, integer(view, "byteOffset", 0))) +
    static_cast<std::size_t>(std::max(0, integer(accessor, "byteOffset", 0)));
  const std::size_t packed = componentSize(result.componentType) * result.components;
  result.stride = static_cast<std::size_t>(std::max(0, integer(view, "byteStride", 0)));
  if (result.stride == 0) result.stride = packed;
  if (packed == 0 || result.stride < packed) throw std::runtime_error("bad accessor layout");
  if (result.count > 0 && result.offset + (result.count - 1) * result.stride + packed > glb.binary.size())
    throw std::runtime_error("accessor exceeds GLB binary chunk");
  return result;
}

std::vector<double> accessorValues(const GlbSource& glb, int index) {
  const AccessorView view = accessorView(glb, index);
  std::vector<double> values;
  values.reserve(view.count * view.components);
  for (std::size_t item = 0; item < view.count; ++item) {
    for (std::size_t component = 0; component < view.components; ++component) {
      values.push_back(readComponent(*view.data,
        view.offset + item * view.stride + component * componentSize(view.componentType),
        view.componentType, view.normalized));
    }
  }
  return values;
}

std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
    [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

std::string escape(std::string_view text) {
  std::ostringstream out;
  for (const unsigned char ch : text) {
    switch (ch) {
      case '"': out << "\\\""; break;
      case '\\': out << "\\\\"; break;
      case '\n': out << "\\n"; break;
      case '\r': out << "\\r"; break;
      case '\t': out << "\\t"; break;
      default:
        if (ch < 0x20) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(ch);
        else out << static_cast<char>(ch);
    }
  }
  return out.str();
}

struct Check {
  std::string name;
  bool pass = false;
  std::string detail;
};

void add(std::vector<Check>& checks, std::string name, bool pass, std::string detail) {
  checks.push_back({std::move(name), pass, std::move(detail)});
}

std::vector<std::string> stringArray(const JsonValue& value, std::string_view key) {
  std::vector<std::string> result;
  for (const JsonValue& item : member(value, key).array)
    if (item.type == JsonValue::Type::String) result.push_back(item.string);
  return result;
}

std::optional<std::size_t> sizeMember(const JsonValue& value, std::string_view key) {
  const JsonValue& item = member(value, key);
  if (item.type != JsonValue::Type::Number || item.number < 0) return std::nullopt;
  return static_cast<std::size_t>(item.number);
}

void checkExpectedCount(std::vector<Check>& checks, const JsonValue& manifest,
                        std::string_view key, std::size_t actual) {
  const auto expected = sizeMember(manifest, key);
  if (!expected) return;
  add(checks, std::string("manifest.") + std::string(key), actual == *expected,
      "expected=" + std::to_string(*expected) + " actual=" + std::to_string(actual));
}

std::string makeReport(const std::filesystem::path& asset, const std::vector<Check>& checks,
                       std::size_t primitives, std::size_t vertices, std::size_t triangles,
                       std::size_t materials, std::size_t animations, std::size_t joints,
                       const lg::GltfModelBounds& bounds, std::size_t instances,
                       std::size_t poseSamples, double checksum,
                       const std::vector<std::string>& nodes,
                       const std::vector<std::string>& proxyNodes,
                       const std::vector<std::string>& weaponNodes) {
  const bool ok = std::all_of(checks.begin(), checks.end(), [](const Check& check) { return check.pass; });
  std::ostringstream out;
  out << std::setprecision(9) << "{\n"
      << "  \"schema_version\": 1,\n"
      << "  \"asset\": \"" << escape(asset.generic_string()) << "\",\n"
      << "  \"ok\": " << (ok ? "true" : "false") << ",\n"
      << "  \"headless_cpu_only\": true,\n"
      << "  \"gpu_render_checks\": \"not_run_requires_client_preview\",\n"
      << "  \"counts\": {\"primitives\": " << primitives << ", \"vertices\": " << vertices
      << ", \"triangles\": " << triangles << ", \"materials\": " << materials
      << ", \"animations\": " << animations << ", \"joints\": " << joints << "},\n"
      << "  \"bounds\": {\"min\": [" << bounds.min.x << ',' << bounds.min.y << ',' << bounds.min.z
      << "], \"max\": [" << bounds.max.x << ',' << bounds.max.y << ',' << bounds.max.z << "]},\n"
      << "  \"sampling\": {\"instances\": " << instances << ", \"pose_samples\": " << poseSamples
      << ", \"checksum\": " << checksum << ", \"timing_threshold_used\": false},\n";
  auto writeStrings = [&out](std::string_view key, const std::vector<std::string>& values, bool comma) {
    out << "  \"" << key << "\": [";
    for (std::size_t i = 0; i < values.size(); ++i) {
      if (i) out << ',';
      out << "\"" << escape(values[i]) << "\"";
    }
    out << ']' << (comma ? ",\n" : "\n");
  };
  writeStrings("nodes", nodes, true);
  writeStrings("proxy_nodes", proxyNodes, true);
  writeStrings("weapon_attachment_nodes", weaponNodes, true);
  out << "  \"checks\": [\n";
  for (std::size_t i = 0; i < checks.size(); ++i) {
    const Check& check = checks[i];
    out << "    {\"name\": \"" << escape(check.name) << "\", \"pass\": "
        << (check.pass ? "true" : "false") << ", \"detail\": \"" << escape(check.detail) << "\"}"
        << (i + 1 < checks.size() ? "," : "") << '\n';
  }
  out << "  ]\n}\n";
  return out.str();
}

void usage() {
  std::cerr << "usage: lg_duel_asset_validate <asset.glb> [--json <report.json>] "
               "[--manifest <expected.json>] [--instances <count>]\n";
}

} // namespace

int main(int argc, char** argv) {
  if (argc < 2) { usage(); return 2; }
  const std::filesystem::path asset = argv[1];
  std::optional<std::filesystem::path> outputPath;
  std::optional<std::filesystem::path> manifestPath;
  std::size_t instances = 16;
  for (int index = 2; index < argc; ++index) {
    const std::string_view option = argv[index];
    if ((option == "--json" || option == "--manifest" || option == "--instances") && index + 1 >= argc) {
      usage(); return 2;
    }
    if (option == "--json") outputPath = argv[++index];
    else if (option == "--manifest") manifestPath = argv[++index];
    else if (option == "--instances") {
      try { instances = std::stoull(argv[++index]); } catch (...) { usage(); return 2; }
      if (instances == 0 || instances > 4096) { usage(); return 2; }
    } else { usage(); return 2; }
  }

  std::vector<Check> checks;
  lg::GltfSkinnedModel model;
  const bool loaded = model.load(asset.string());
  add(checks, "load", loaded, loaded ? "GltfSkinnedModel loaded the asset" : "GltfSkinnedModel rejected the asset");

  GlbSource glb;
  bool sourceRead = false;
  try {
    glb = readGlb(asset);
    sourceRead = true;
  } catch (const std::exception& error) {
    add(checks, "glb_structure", false, error.what());
  }
  if (sourceRead) add(checks, "glb_structure", true, "glTF 2 JSON and binary chunks parsed");

  std::vector<std::string> nodeNames;
  std::vector<std::string> proxyNodes;
  std::vector<std::string> weaponNodes;
  bool proxyAlignmentOk = true;
  const std::size_t materialCount = sourceRead ? member(glb.root, "materials").array.size() : 0;
  bool rawWeightsOk = true;
  bool fourInfluences = true;
  std::string weightDetail = "all encoded WEIGHTS_0 sums are within 0.002 of one";
  std::size_t rawWeightedVertices = 0;
  std::size_t rawPrimitiveCount = 0;
  std::size_t rawVertexCount = 0;
  std::size_t rawTriangleCount = 0;
  bool rawGeometryOk = true;
  if (sourceRead) {
    for (const JsonValue& node : member(glb.root, "nodes").array) {
      const JsonValue& nameValue = member(node, "name");
      const std::string name = nameValue.type == JsonValue::Type::String ? nameValue.string : std::string{};
      if (!name.empty()) nodeNames.push_back(name);
      const std::string folded = lower(name);
      if (folded.find("hitbox") != std::string::npos || folded.find("collision") != std::string::npos ||
          folded.find("collider") != std::string::npos) {
        proxyNodes.push_back(name);
        // Proxy transforms must stay finite. Shape-level fit needs the expected
        // bounds from the authoring manifest or a client preview.
        for (std::string_view transform : {"translation", "rotation", "scale", "matrix"}) {
          for (const JsonValue& component : member(node, transform).array)
            proxyAlignmentOk = proxyAlignmentOk && component.type == JsonValue::Type::Number &&
              std::isfinite(component.number);
        }
      }
      if (folded.find("weapon") != std::string::npos &&
          (folded.find("attach") != std::string::npos || folded.find("socket") != std::string::npos ||
           folded.find("mount") != std::string::npos || folded == "weapon"))
        weaponNodes.push_back(name);
    }
    for (const JsonValue& mesh : member(glb.root, "meshes").array) {
      for (const JsonValue& primitive : member(mesh, "primitives").array) {
        ++rawPrimitiveCount;
        const JsonValue& attributes = member(primitive, "attributes");
        const int positionIndex = integer(attributes, "POSITION");
        const int indexIndex = integer(primitive, "indices");
        try {
          const AccessorView positions = accessorView(glb, positionIndex);
          const AccessorView indices = accessorView(glb, indexIndex);
          rawGeometryOk = rawGeometryOk && positions.components == 3 && indices.components == 1 &&
            indices.count >= 3 && indices.count % 3 == 0;
          rawVertexCount += positions.count;
          rawTriangleCount += indices.count / 3;
        } catch (const std::exception&) {
          rawGeometryOk = false;
        }
        if (member(attributes, "JOINTS_1").type != JsonValue::Type::Null ||
            member(attributes, "WEIGHTS_1").type != JsonValue::Type::Null) fourInfluences = false;
        const int weightsIndex = integer(attributes, "WEIGHTS_0");
        if (weightsIndex < 0) continue;
        try {
          const AccessorView view = accessorView(glb, weightsIndex);
          if (view.components != 4) fourInfluences = false;
          const std::vector<double> weights = accessorValues(glb, weightsIndex);
          for (std::size_t i = 0; i + view.components <= weights.size(); i += view.components) {
            double sum = 0.0;
            for (std::size_t j = 0; j < view.components; ++j) {
              if (!std::isfinite(weights[i + j]) || weights[i + j] < -0.00001) rawWeightsOk = false;
              sum += weights[i + j];
            }
            if (sum > 0.00001) {
              ++rawWeightedVertices;
              if (std::abs(sum - 1.0) > 0.002) rawWeightsOk = false;
            }
          }
        } catch (const std::exception& error) {
          rawWeightsOk = false;
          weightDetail = error.what();
        }
      }
    }
  }

  std::size_t vertexCount = 0;
  std::size_t triangleCount = 0;
  bool indicesOk = true;
  bool runtimeWeightsOk = true;
  bool primitiveBoundsOk = true;
  for (const lg::GltfSkinnedModel::Primitive& primitive : model.primitives()) {
    vertexCount += primitive.vertices.size();
    triangleCount += primitive.indices.size() / 3;
    indicesOk = indicesOk && !primitive.vertices.empty() && primitive.indices.size() >= 3 &&
      primitive.indices.size() % 3 == 0;
    primitiveBoundsOk = primitiveBoundsOk &&
      std::isfinite(primitive.localBounds.min.x) && std::isfinite(primitive.localBounds.min.y) &&
      std::isfinite(primitive.localBounds.min.z) && std::isfinite(primitive.localBounds.max.x) &&
      std::isfinite(primitive.localBounds.max.y) && std::isfinite(primitive.localBounds.max.z) &&
      primitive.localBounds.min.x <= primitive.localBounds.max.x &&
      primitive.localBounds.min.y <= primitive.localBounds.max.y &&
      primitive.localBounds.min.z <= primitive.localBounds.max.z;
    for (const auto& vertex : primitive.vertices) {
      float sum = 0.0F;
      for (float weight : vertex.weights) {
        runtimeWeightsOk = runtimeWeightsOk && std::isfinite(weight) && weight >= 0.0F;
        sum += weight;
      }
      if (sum > 0.0F) runtimeWeightsOk = runtimeWeightsOk && std::abs(sum - 1.0F) <= 0.0001F;
    }
  }
  const lg::GltfModelBounds bounds = model.localBounds();
  const std::size_t effectivePrimitives = model.primitives().empty() ? rawPrimitiveCount : model.primitives().size();
  const std::size_t effectiveVertices = model.primitives().empty() ? rawVertexCount : vertexCount;
  const std::size_t effectiveTriangles = model.primitives().empty() ? rawTriangleCount : triangleCount;
  const bool boundsOk = loaded && primitiveBoundsOk && std::isfinite(bounds.min.x) &&
    std::isfinite(bounds.min.y) && std::isfinite(bounds.min.z) && std::isfinite(bounds.max.x) &&
    std::isfinite(bounds.max.y) && std::isfinite(bounds.max.z) && bounds.min.x <= bounds.max.x &&
    bounds.min.y <= bounds.max.y && bounds.min.z <= bounds.max.z;
  add(checks, "mesh_primitives", loaded && rawGeometryOk && effectivePrimitives > 0 && effectiveVertices > 0 && effectiveTriangles > 0 && indicesOk,
      "primitives=" + std::to_string(effectivePrimitives) + " vertices=" +
      std::to_string(effectiveVertices) + " triangles=" + std::to_string(effectiveTriangles));
  add(checks, "bounds", boundsOk, boundsOk ? "finite ordered model and primitive bounds" : "bounds are missing or invalid");
  add(checks, "skin_influences_max_four", sourceRead && fourInfluences,
      fourInfluences ? "no JOINTS_1/WEIGHTS_1 data and WEIGHTS_0 is VEC4" : "more than four encoded influences found");
  add(checks, "skin_weights_normalized", sourceRead && rawWeightsOk && runtimeWeightsOk,
      weightDetail + "; weighted_vertices=" + std::to_string(rawWeightedVertices));
  add(checks, "outline_cpu_eligibility", loaded && rawGeometryOk && effectivePrimitives > 0 && indicesOk && boundsOk,
      "triangle primitives and bounds are present; GPU outline output requires client preview");
  add(checks, "collision_proxy_alignment", proxyAlignmentOk,
      proxyNodes.empty() ? "no collision or hitbox proxy nodes encoded; manifest may require them" :
      "encoded proxy node transforms are finite; shape fit requires client preview or manifest bounds");

  std::size_t poseSamples = 0;
  double checksum = 0.0;
  bool animationsOk = true;
  const std::vector<float> sampleTimes = {0.0F, 0.173F, 0.619F};
  for (std::size_t instance = 0; instance < instances; ++instance) {
    for (const std::string& animation : model.animationNames()) {
      for (float time : sampleTimes) {
        std::vector<std::array<float, 16>> palette;
        lg::GltfSkinnedModel::PoseScratch scratch;
        const bool sampled = model.appendBonePalette({{animation, time}}, palette, scratch);
        animationsOk = animationsOk && sampled && palette.size() == model.jointCount();
        for (const auto& matrix : palette) for (float value : matrix) {
          animationsOk = animationsOk && std::isfinite(value);
          checksum += static_cast<double>(value) * (1.0 + static_cast<double>(instance % 7));
        }
        ++poseSamples;
      }
    }
  }
  if (!model.animationNames().empty())
    add(checks, "animations_sampleable", animationsOk, "sampled each clip at three fixed times");
  else
    add(checks, "animations_sampleable", true, "asset has no animations; use manifest require_animations to reject this");
  add(checks, "multiple_instance_cpu_sampling", loaded && animationsOk,
      "fixed headless samples for instances=" + std::to_string(instances) +
      "; no wall-clock pass threshold");

  JsonValue manifest;
  bool hasManifest = false;
  if (manifestPath) {
    try {
      std::ifstream file(*manifestPath);
      if (!file) throw std::runtime_error("cannot open manifest");
      std::ostringstream text;
      text << file.rdbuf();
      const std::string manifestText = text.str();
      manifest = JsonParser(manifestText).parse();
      hasManifest = manifest.type == JsonValue::Type::Object;
      add(checks, "manifest_parse", hasManifest, hasManifest ? "manifest parsed" : "manifest root must be an object");
    } catch (const std::exception& error) {
      add(checks, "manifest_parse", false, error.what());
    }
  }
  if (hasManifest) {
    const std::set<std::string> names(nodeNames.begin(), nodeNames.end());
    auto requireNodes = [&](std::string_view key) {
      for (const std::string& name : stringArray(manifest, key))
        add(checks, std::string("manifest.") + std::string(key) + "." + name,
            names.contains(name), names.contains(name) ? "node found" : "required node missing");
    };
    requireNodes("required_bones");
    requireNodes("required_attachment_nodes");
    requireNodes("required_proxy_nodes");
    requireNodes("weapon_attachment_nodes");
    if (boolean(manifest, "require_skin", false))
      add(checks, "manifest.require_skin", model.hasSkin() && model.hasSkinnedPrimitives(),
          model.hasSkin() && model.hasSkinnedPrimitives() ? "skinned primitives found" : "skin required but missing");
    if (boolean(manifest, "require_animations", false))
      add(checks, "manifest.require_animations", !model.animationNames().empty(),
          !model.animationNames().empty() ? "animations found" : "animations required but missing");
    if (boolean(manifest, "require_weapon_attachment", false))
      add(checks, "manifest.require_weapon_attachment", !weaponNodes.empty(),
          !weaponNodes.empty() ? "named weapon attachment found" : "weapon attachment node missing");
    if (boolean(manifest, "require_proxy_nodes", false))
      add(checks, "manifest.require_proxy_nodes", !proxyNodes.empty(),
          !proxyNodes.empty() ? "collision or hitbox proxy node found" : "proxy node missing");
    checkExpectedCount(checks, manifest, "expected_primitives", effectivePrimitives);
    checkExpectedCount(checks, manifest, "expected_vertices", effectiveVertices);
    checkExpectedCount(checks, manifest, "expected_triangles", effectiveTriangles);
    checkExpectedCount(checks, manifest, "expected_materials", materialCount);
    checkExpectedCount(checks, manifest, "expected_animations", model.animationNames().size());
    checkExpectedCount(checks, manifest, "expected_joints", model.jointCount());
  }

  const std::string report = makeReport(asset, checks, effectivePrimitives, effectiveVertices,
    effectiveTriangles, materialCount, model.animationNames().size(), model.jointCount(), bounds,
    instances, poseSamples, checksum, nodeNames, proxyNodes, weaponNodes);
  std::cout << report;
  if (outputPath) {
    std::ofstream file(*outputPath, std::ios::binary);
    if (!file) { std::cerr << "asset validation ERROR: cannot write " << outputPath->string() << '\n'; return 2; }
    file << report;
    if (!file) { std::cerr << "asset validation ERROR: failed to write " << outputPath->string() << '\n'; return 2; }
  }
  return std::all_of(checks.begin(), checks.end(), [](const Check& check) { return check.pass; }) ? 0 : 1;
}
