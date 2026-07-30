#include "map/MapToArena.hpp"

#include "map/MapParser.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace lg {
namespace {

constexpr float kPlaneEpsilon = 0.001F;
constexpr float kPlaneIntersectionEpsilon = 0.000001F;
constexpr float kBoundsPadding = 1.0F;
constexpr float kQuakeToLgScale = 1.0F / 40.0F;
constexpr float kDefaultLightIntensity = 1.0F;
constexpr float kDefaultLightRadiusQuakeUnits = 320.0F;
constexpr float kMaxLightIntensity = 16.0F;
constexpr float kMaxLightRadiusQuakeUnits = 4096.0F;
constexpr float kMaxLightSourceRadiusQuakeUnits = 1024.0F;
constexpr std::int32_t kMinLightPriority = -1000;
constexpr std::int32_t kMaxLightPriority = 1000;
constexpr float kDefaultLightFlickerFrequencyHz = 8.0F;
constexpr float kMaxLightFlickerFrequencyHz = 30.0F;
constexpr float kMaxLightFlickerFactor = 4.0F;
constexpr float kDegreesToRadians = 0.01745329252F;
constexpr Vec3 kDefaultSunDirection = {0.25916052F, -0.43193421F, -0.86386842F};
constexpr Vec3 kDefaultSunColor = {1.0F, 0.94117647F, 0.78431374F};
constexpr float kDefaultSunIntensity = 0.7F;
constexpr float kDefaultTeleportExitSpeed = 10.0F;

struct TargetPosition {
  std::string targetname;
  Vec3 position = {};
  float yawRadians = 0.0F;
  int line = 0;
};

[[nodiscard]] const MapProperty* findProperty(
  const MapEntity& entity,
  std::string_view key
) {
  for (const MapProperty& property : entity.properties) {
    if (property.key == key) {
      return &property;
    }
  }
  return nullptr;
}

[[nodiscard]] bool parseFloat(std::string_view text, float& value) {
  const char* begin = text.data();
  const char* end = begin + text.size();
  const auto result = std::from_chars(begin, end, value);
  return result.ec == std::errc{} && result.ptr == end && std::isfinite(value);
}

[[nodiscard]] bool parseUint32(std::string_view text, std::uint32_t& value) {
  const char* begin = text.data();
  const char* end = begin + text.size();
  const auto result = std::from_chars(begin, end, value);
  return result.ec == std::errc{} && result.ptr == end;
}

[[nodiscard]] bool parseInt32(std::string_view text, std::int32_t& value) {
  const char* begin = text.data();
  const char* end = begin + text.size();
  const auto result = std::from_chars(begin, end, value);
  return result.ec == std::errc{} && result.ptr == end;
}

[[nodiscard]] bool parseBool(std::string_view text, bool& value) {
  std::string normalized(text);
  std::transform(
    normalized.begin(),
    normalized.end(),
    normalized.begin(),
    [](unsigned char character) {
      return static_cast<char>(std::tolower(character));
    }
  );
  if (
    normalized == "1" || normalized == "true" || normalized == "yes" ||
    normalized == "on"
  ) {
    value = true;
    return true;
  }
  if (
    normalized == "0" || normalized == "false" || normalized == "no" ||
    normalized == "off"
  ) {
    value = false;
    return true;
  }
  return false;
}

[[nodiscard]] bool isSha256Hex(std::string_view value) {
  return value.size() == 64U && std::all_of(
    value.begin(),
    value.end(),
    [](unsigned char character) { return std::isxdigit(character) != 0; }
  );
}

[[nodiscard]] bool parseSpaceVec3(std::string_view text, Vec3& value) {
  std::istringstream input{std::string(text)};
  std::string x;
  std::string y;
  std::string z;
  std::string extra;
  if (!(input >> x >> y >> z) || (input >> extra)) {
    return false;
  }
  return parseFloat(x, value.x) && parseFloat(y, value.y) && parseFloat(z, value.z);
}

[[nodiscard]] Vec3 scaleQuakeUnits(Vec3 value) {
  return value * kQuakeToLgScale;
}

[[nodiscard]] std::string normalizedMaterialName(std::string_view material) {
  std::string normalized(material);
  std::replace(normalized.begin(), normalized.end(), '\\', '/');
  while (!normalized.empty() && normalized.front() == '/') {
    normalized.erase(normalized.begin());
  }
  std::transform(
    normalized.begin(),
    normalized.end(),
    normalized.begin(),
    [](unsigned char character) {
      return static_cast<char>(std::tolower(character));
    }
  );
  constexpr std::string_view prefix = "textures/";
  if (normalized.rfind(prefix, 0) == 0) {
    normalized.erase(0, prefix.size());
  }
  return normalized;
}

[[nodiscard]] bool isPlayerClipMaterial(std::string_view material) {
  const std::string normalized = normalizedMaterialName(material);
  return normalized == "common/playerclip" || normalized == "common/clip";
}

[[nodiscard]] bool isWeaponClipMaterial(std::string_view material) {
  return normalizedMaterialName(material) == "common/weapclip";
}

[[nodiscard]] bool isCollisionOnlyMaterial(std::string_view material) {
  return isPlayerClipMaterial(material) || isWeaponClipMaterial(material);
}

[[nodiscard]] bool brushHasCollisionOnlyMaterial(const MapBrush& brush) {
  return std::any_of(
    brush.faces.begin(),
    brush.faces.end(),
    [](const MapFace& face) {
      return isCollisionOnlyMaterial(face.material);
    }
  );
}

[[nodiscard]] bool brushUsesOnlyMaterialClass(
  const MapBrush& brush,
  bool (*predicate)(std::string_view)
) {
  return !brush.faces.empty() &&
    std::all_of(
      brush.faces.begin(),
      brush.faces.end(),
      [predicate](const MapFace& face) {
        return predicate(face.material);
      }
    );
}

[[nodiscard]] ArenaCollisionKind inferredCollisionKind(const MapBrush& brush) {
  if (brushUsesOnlyMaterialClass(brush, isPlayerClipMaterial)) {
    return ArenaCollisionKind::PlayerClip;
  }
  if (brushUsesOnlyMaterialClass(brush, isWeaponClipMaterial)) {
    return ArenaCollisionKind::WeaponClip;
  }
  return ArenaCollisionKind::VisibleSolid;
}

[[nodiscard]] std::string_view collisionKindName(ArenaCollisionKind kind) {
  switch (kind) {
    case ArenaCollisionKind::VisibleSolid: return "visible_solid";
    case ArenaCollisionKind::PlayerClip: return "playerclip";
    case ArenaCollisionKind::WeaponClip: return "weapclip";
  }
  return "visible_solid";
}

[[nodiscard]] bool parseCollisionKind(
  std::string_view value,
  ArenaCollisionKind& kind
) {
  if (value == "visible_solid") {
    kind = ArenaCollisionKind::VisibleSolid;
    return true;
  }
  if (value == "playerclip") {
    kind = ArenaCollisionKind::PlayerClip;
    return true;
  }
  if (value == "weapclip") {
    kind = ArenaCollisionKind::WeaponClip;
    return true;
  }
  return false;
}

struct SourceBrushMetadata {
  ArenaCollisionKind collisionKind = ArenaCollisionKind::VisibleSolid;
  std::uint32_t entityIndex = kInvalidSourceGeometryIndex;
  std::uint32_t brushIndex = kInvalidSourceGeometryIndex;
  std::uint32_t patchIndex = kInvalidSourceGeometryIndex;
  std::uint32_t patchPieceIndex = kInvalidSourceGeometryIndex;
  bool hasCollisionKind = false;
  bool renderOnly = false;
};

[[nodiscard]] bool parseSourceBrushMetadata(
  const MapEntity& entity,
  SourceBrushMetadata& metadata,
  std::string& error
) {
  const MapProperty* geometryRole = findProperty(entity, "lg_geometry_role");
  const MapProperty* sourcePatch = findProperty(entity, "lg_source_patch_index");
  const MapProperty* sourcePatchPiece =
    findProperty(entity, "lg_source_patch_piece_index");
  const MapProperty* sourceEntity = findProperty(entity, "lg_source_entity_index");
  const MapProperty* sourceBrush = findProperty(entity, "lg_source_brush_index");
  const MapProperty* adaptationVisualId =
    findProperty(entity, "lg_adaptation_visual_id");
  if (geometryRole != nullptr) {
    if (geometryRole->value != "render_only") {
      error = "line " + std::to_string(geometryRole->line) +
        ": lg_geometry_role must be render_only";
      return false;
    }
    metadata.renderOnly = true;
  }
  if (geometryRole == nullptr && sourcePatch != nullptr) {
    error = "line " + std::to_string(entity.line) +
      ": lg_source_patch_index requires lg_geometry_role render_only";
    return false;
  }
  if (sourcePatchPiece != nullptr && sourcePatch == nullptr) {
    error = "line " + std::to_string(sourcePatchPiece->line) +
      ": lg_source_patch_piece_index requires render-only source patch metadata";
    return false;
  }
  if (sourcePatch != nullptr) {
    if (
      entity.brushes.size() != 1U ||
      !parseUint32(sourcePatch->value, metadata.patchIndex) ||
      metadata.patchIndex == kInvalidSourceGeometryIndex
    ) {
      error = "line " + std::to_string(entity.line) +
        ": render-only source patch metadata requires one brush and a "
        "non-negative 32-bit index";
      return false;
    }
    metadata.patchPieceIndex = 0U;
    if (
      sourcePatchPiece != nullptr &&
      (!parseUint32(sourcePatchPiece->value, metadata.patchPieceIndex) ||
       metadata.patchPieceIndex == kInvalidSourceGeometryIndex)
    ) {
      error = "line " + std::to_string(sourcePatchPiece->line) +
        ": lg_source_patch_piece_index must be a non-negative 32-bit index";
      return false;
    }
  }

  const MapProperty* collision = findProperty(entity, "lg_collision_class");
  if (collision != nullptr) {
    if (!parseCollisionKind(collision->value, metadata.collisionKind)) {
      error = "line " + std::to_string(collision->line) +
        ": lg_collision_class must be visible_solid, playerclip, or weapclip";
      return false;
    }
    metadata.hasCollisionKind = true;
  }

  if ((sourceEntity == nullptr) != (sourceBrush == nullptr)) {
    error = "line " + std::to_string(entity.line) +
      ": source brush metadata requires both lg_source_entity_index and lg_source_brush_index";
    return false;
  }
  if (sourceEntity != nullptr) {
    if (
      entity.brushes.size() != 1U ||
      !parseUint32(sourceEntity->value, metadata.entityIndex) ||
      !parseUint32(sourceBrush->value, metadata.brushIndex) ||
      metadata.entityIndex == kInvalidSourceGeometryIndex ||
      metadata.brushIndex == kInvalidSourceGeometryIndex
    ) {
      error = "line " + std::to_string(entity.line) +
        ": source brush metadata requires one brush and non-negative 32-bit indices";
      return false;
    }
  }
  if (metadata.renderOnly) {
    const bool hasPatchSource = sourcePatch != nullptr;
    const bool hasBrushSource = sourceEntity != nullptr;
    if (collision != nullptr || hasPatchSource == hasBrushSource) {
      error = "line " + std::to_string(entity.line) +
        ": render-only geometry requires exactly one patch or brush source and no collision class";
      return false;
    }
    if (hasPatchSource && adaptationVisualId != nullptr) {
      error = "line " + std::to_string(adaptationVisualId->line) +
        ": source patch geometry cannot use lg_adaptation_visual_id";
      return false;
    }
    if (hasBrushSource) {
      const bool validId = adaptationVisualId != nullptr &&
        !adaptationVisualId->value.empty() &&
        std::isalnum(
          static_cast<unsigned char>(adaptationVisualId->value.front())
        ) != 0 &&
        std::all_of(
          adaptationVisualId->value.begin(), adaptationVisualId->value.end(),
          [](unsigned char character) {
            return std::isalnum(character) != 0 || character == '_' ||
              character == '-' || character == '.';
          }
        );
      if (!validId) {
        error = "line " + std::to_string(entity.line) +
          ": render-only source brush metadata requires a stable lg_adaptation_visual_id";
        return false;
      }
    }
  } else if (adaptationVisualId != nullptr) {
    error = "line " + std::to_string(adaptationVisualId->line) +
      ": lg_adaptation_visual_id requires render-only source brush metadata";
    return false;
  }
  return true;
}

void clearRenderableMaterial(ArenaWall& wall) {
  wall.materialId = 0;
  wall.faceMaterialIds = {};
  wall.faceTextureProjections = {};
}

void clearRenderableMaterial(ArenaBrush& brush) {
  brush.materialId = 0;
  for (std::uint8_t faceIndex = 0; faceIndex < brush.faceCount; ++faceIndex) {
    brush.faces[faceIndex].materialId = 0;
    brush.faces[faceIndex].textureProjection = {};
  }
}

[[nodiscard]] bool brushPointBounds(
  const MapBrush& brush,
  Vec3& minimum,
  Vec3& maximum,
  std::string_view ownerClass,
  std::string& error
) {
  bool initialized = false;
  for (const MapFace& face : brush.faces) {
    for (Vec3 point : face.points) {
      point = scaleQuakeUnits(point);
      if (!initialized) {
        minimum = point;
        maximum = point;
        initialized = true;
        continue;
      }
      minimum.x = std::min(minimum.x, point.x);
      minimum.y = std::min(minimum.y, point.y);
      minimum.z = std::min(minimum.z, point.z);
      maximum.x = std::max(maximum.x, point.x);
      maximum.y = std::max(maximum.y, point.y);
      maximum.z = std::max(maximum.z, point.z);
    }
  }
  if (
    !initialized ||
    !(minimum.x < maximum.x && minimum.y < maximum.y && minimum.z < maximum.z)
  ) {
    error = "line " + std::to_string(brush.line) +
      ": " + std::string(ownerClass) + " brush has degenerate or inverted bounds";
    return false;
  }
  return true;
}

[[nodiscard]] bool parsePositiveFloat(
  const MapEntity& entity,
  std::string_view key,
  float& value,
  std::string& error
) {
  const std::string* text = entity.property(key);
  if (text == nullptr) {
    return true;
  }
  if (!parseFloat(*text, value) || value <= 0.0F) {
    error = "line " + std::to_string(entity.line) + ": light " +
      std::string(key) + " must be a positive finite float";
    return false;
  }
  return true;
}

[[nodiscard]] bool parseNonNegativeFloat(
  const MapEntity& entity,
  std::string_view key,
  float& value,
  std::string& error
) {
  const std::string* text = entity.property(key);
  if (text == nullptr) {
    return true;
  }
  if (!parseFloat(*text, value) || value < 0.0F) {
    error = "line " + std::to_string(entity.line) + ": light_sun " +
      std::string(key) + " must be a non-negative finite float";
    return false;
  }
  return true;
}

[[nodiscard]] TextureProjection textureProjectionForFace(
  Vec3 normal,
  const MapFace& face
) {
  TextureProjection projection;
  Vec3 baseU = {};
  Vec3 baseV = {};
  const Vec3 absolute = {
    std::fabs(normal.x),
    std::fabs(normal.y),
    std::fabs(normal.z),
  };
  if (absolute.z >= absolute.x && absolute.z >= absolute.y) {
    baseU = {1.0F, 0.0F, 0.0F};
    baseV = {0.0F, -1.0F, 0.0F};
  } else if (absolute.x >= absolute.y) {
    baseU = {0.0F, 1.0F, 0.0F};
    baseV = {0.0F, 0.0F, -1.0F};
  } else {
    baseU = {1.0F, 0.0F, 0.0F};
    baseV = {0.0F, 0.0F, -1.0F};
  }

  constexpr float kDegreesToRadians = 0.01745329252F;
  const float radians = face.rotationDegrees * kDegreesToRadians;
  const float sine = std::sin(radians);
  const float cosine = std::cos(radians);
  const Vec3 rotatedU = baseU * cosine - baseV * sine;
  const Vec3 rotatedV = baseU * sine + baseV * cosine;
  const float xScale = std::fabs(face.xScale) <= 0.0001F ? 1.0F : face.xScale;
  const float yScale = std::fabs(face.yScale) <= 0.0001F ? 1.0F : face.yScale;
  projection.uAxis = rotatedU / xScale;
  projection.vAxis = rotatedV / yScale;
  projection.uOffset = face.xOffset;
  projection.vOffset = face.yOffset;
  projection.rotationDegrees = face.rotationDegrees;
  projection.uScale = xScale;
  projection.vScale = yScale;
  projection.valid = true;
  return projection;
}

[[nodiscard]] bool parseOptionalYaw(
  const MapEntity& entity,
  std::string_view owner,
  float& yawRadians,
  std::string& error
) {
  yawRadians = 0.0F;
  const std::string* yaw = entity.property("angle");
  if (yaw == nullptr) {
    yaw = entity.property("yaw");
  }
  if (yaw == nullptr) {
    return true;
  }
  float value = 0.0F;
  if (!parseFloat(*yaw, value)) {
    error = "line " + std::to_string(entity.line) + ": " +
      std::string(owner) + " yaw must be a finite float";
    return false;
  }
  constexpr float kDegreesToRadians = 0.01745329252F;
  yawRadians = value * kDegreesToRadians;
  return true;
}

[[nodiscard]] bool parseOptionalPositivePropertyFloat(
  const MapEntity& entity,
  std::string_view key,
  std::string_view ownerClass,
  float& value,
  bool& present,
  std::string& error
) {
  const MapProperty* property = findProperty(entity, key);
  if (property == nullptr) {
    present = false;
    return true;
  }
  present = true;
  if (!parseFloat(property->value, value) || value <= 0.0F) {
    error = "line " + std::to_string(property->line) + ": " +
      std::string(ownerClass) + " " + std::string(key) +
      " must be a positive finite float";
    return false;
  }
  return true;
}

[[nodiscard]] bool parseOptionalFinitePropertyFloat(
  const MapEntity& entity,
  std::string_view key,
  std::string_view ownerClass,
  float& value,
  bool& present,
  std::string& error
) {
  const MapProperty* property = findProperty(entity, key);
  if (property == nullptr) {
    present = false;
    return true;
  }
  present = true;
  if (!parseFloat(property->value, value)) {
    error = "line " + std::to_string(property->line) + ": " +
      std::string(ownerClass) + " " + std::string(key) +
      " must be a finite float";
    return false;
  }
  return true;
}

[[nodiscard]] bool fallbackJumpPadVelocity(
  const MapEntity& entity,
  float speed,
  ArenaJumpPad& jumpPad,
  std::string& error
) {
  if (const MapProperty* direction = findProperty(entity, "direction")) {
    Vec3 parsed = {};
    if (!parseSpaceVec3(direction->value, parsed)) {
      error = "line " + std::to_string(direction->line) +
        ": trigger_jumppad direction must be 'x y z'";
      return false;
    }
    parsed = normalize(parsed);
    if (length(parsed) <= 0.0001F) {
      error = "line " + std::to_string(direction->line) +
        ": trigger_jumppad direction must be non-zero";
      return false;
    }
    jumpPad.launchVelocity = parsed * speed;
    return true;
  }

  float angle = 0.0F;
  float pitch = 90.0F;
  bool hasAngle = false;
  bool hasPitch = false;
  if (
    !parseOptionalFinitePropertyFloat(entity, "angle", "trigger_jumppad", angle, hasAngle, error) ||
    !parseOptionalFinitePropertyFloat(entity, "pitch", "trigger_jumppad", pitch, hasPitch, error)
  ) {
    return false;
  }
  if (hasAngle || hasPitch) {
    const float yawRadians = angle * kDegreesToRadians;
    const float pitchRadians = pitch * kDegreesToRadians;
    jumpPad.launchVelocity = cameraForward(yawRadians, pitchRadians) * speed;
    return true;
  }

  jumpPad.launchVelocity = {0.0F, 0.0F, speed};
  return true;
}

[[nodiscard]] Vec3 subtract(Vec3 lhs, Vec3 rhs) {
  return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

[[nodiscard]] Vec3 cross(Vec3 lhs, Vec3 rhs) {
  return {
    lhs.y * rhs.z - lhs.z * rhs.y,
    lhs.z * rhs.x - lhs.x * rhs.z,
    lhs.x * rhs.y - lhs.y * rhs.x,
  };
}

[[nodiscard]] bool nearlyEqual(float lhs, float rhs) {
  return std::fabs(lhs - rhs) <= kPlaneEpsilon;
}

[[nodiscard]] bool classifyFace(const MapFace& face, int& axis, float& coordinate) {
  const Vec3 normal = cross(
    subtract(face.points[1], face.points[0]),
    subtract(face.points[2], face.points[0])
  );
  const std::array<float, 3> components = {normal.x, normal.y, normal.z};
  int normalAxis = -1;
  for (int index = 0; index < 3; ++index) {
    if (std::fabs(components[index]) > kPlaneEpsilon) {
      if (normalAxis != -1) {
        return false;
      }
      normalAxis = index;
    }
  }
  if (normalAxis == -1) {
    return false;
  }

  for (int index = 0; index < 3; ++index) {
    if (index != normalAxis && std::fabs(components[index]) > kPlaneEpsilon) {
      return false;
    }
  }

  axis = -1;
  for (int index = 0; index < 3; ++index) {
    const float first = index == 0 ? face.points[0].x : index == 1 ? face.points[0].y : face.points[0].z;
    const float second = index == 0 ? face.points[1].x : index == 1 ? face.points[1].y : face.points[1].z;
    const float third = index == 0 ? face.points[2].x : index == 1 ? face.points[2].y : face.points[2].z;
    if (nearlyEqual(first, second) && nearlyEqual(first, third)) {
      if (axis != -1) {
        return false;
      }
      axis = index;
      coordinate = first;
    }
  }
  return axis == normalAxis;
}

[[nodiscard]] std::size_t cuboidSceneFaceIndex(int axis, std::size_t side) {
  if (axis == 2) {
    return side == 0U ? 0U : 1U;
  }
  if (axis == 1) {
    return side == 0U ? 2U : 4U;
  }
  return side == 0U ? 5U : 3U;
}

[[nodiscard]] bool convertCuboidBrush(
  const MapBrush& brush,
  ArenaWall& wall,
  std::string& error
) {
  if (brush.faces.size() != 6) {
    error = "line " + std::to_string(brush.line) + ": worldspawn brush must have exactly six faces";
    return false;
  }

  std::array<std::vector<float>, 3> planes = {};
  for (const MapFace& face : brush.faces) {
    int axis = -1;
    float coordinate = 0.0F;
    if (!classifyFace(face, axis, coordinate)) {
      error = "line " + std::to_string(face.line) + ": brush face is not axis-aligned";
      return false;
    }
    std::vector<float>& axisPlanes = planes[static_cast<std::size_t>(axis)];
    if (std::any_of(axisPlanes.begin(), axisPlanes.end(), [&](float existing) {
      return nearlyEqual(existing, coordinate);
    })) {
      error = "line " + std::to_string(face.line) + ": duplicate cuboid plane";
      return false;
    }
    axisPlanes.push_back(coordinate);
  }

  for (const std::vector<float>& axisPlanes : planes) {
    if (axisPlanes.size() != 2) {
      error = "line " + std::to_string(brush.line) + ": brush is missing a cuboid plane";
      return false;
    }
  }
  for (std::vector<float>& axisPlanes : planes) {
    std::sort(axisPlanes.begin(), axisPlanes.end());
  }

  wall.min = {planes[0][0], planes[1][0], planes[2][0]};
  wall.max = {planes[0][1], planes[1][1], planes[2][1]};
  wall.min = scaleQuakeUnits(wall.min);
  wall.max = scaleQuakeUnits(wall.max);
  for (const MapFace& face : brush.faces) {
    if (!face.material.empty()) {
      wall.materialId = arenaMaterialId(face.material);
      break;
    }
  }
  for (const MapFace& face : brush.faces) {
    int axis = -1;
    float coordinate = 0.0F;
    if (!classifyFace(face, axis, coordinate)) {
      continue;
    }
    const std::size_t side = nearlyEqual(coordinate, planes[static_cast<std::size_t>(axis)][0])
      ? 0U
      : 1U;
    const std::size_t faceIndex = cuboidSceneFaceIndex(axis, side);
    wall.faceMaterialIds[faceIndex] = arenaMaterialId(face.material);
    Vec3 normal = {};
    if (axis == 0) {
      normal.x = side == 0U ? -1.0F : 1.0F;
    } else if (axis == 1) {
      normal.y = side == 0U ? -1.0F : 1.0F;
    } else {
      normal.z = side == 0U ? -1.0F : 1.0F;
    }
    wall.faceTextureProjections[faceIndex] =
      textureProjectionForFace(normal, face);
  }
  if (!(wall.min.x < wall.max.x && wall.min.y < wall.max.y && wall.min.z < wall.max.z)) {
    error = "line " + std::to_string(brush.line) + ": cuboid brush has degenerate or inverted bounds";
    return false;
  }
  return true;
}

[[nodiscard]] bool intersectPlanes(
  const ArenaBrushFace& first,
  const ArenaBrushFace& second,
  const ArenaBrushFace& third,
  Vec3& point
) {
  const Vec3 secondThird = cross(second.normal, third.normal);
  const float denominator = dot(first.normal, secondThird);
  if (std::fabs(denominator) <= kPlaneIntersectionEpsilon) {
    return false;
  }
  point =
    (
      secondThird * first.distance +
      cross(third.normal, first.normal) * second.distance +
      cross(first.normal, second.normal) * third.distance
    ) / denominator;
  return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

[[nodiscard]] bool addUniqueVertex(ArenaBrush& brush, Vec3 point, std::uint8_t& index) {
  constexpr float kVertexEpsilon = 0.0005F;
  // Plane triples that meet at one geometric corner produce slightly different
  // floats; merge them so topology and fixed-capacity counts remain stable.
  for (std::uint8_t existing = 0; existing < brush.vertexCount; ++existing) {
    const Vec3 delta = brush.vertices[existing] - point;
    if (length(delta) <= kVertexEpsilon) {
      index = existing;
      return true;
    }
  }
  if (brush.vertexCount >= ArenaBrush::kMaxVertices) {
    return false;
  }
  index = brush.vertexCount;
  brush.vertices[brush.vertexCount++] = point;
  return true;
}

[[nodiscard]] std::string brushContext(
  const MapBrush& brush,
  std::string_view ownerClass,
  std::size_t brushIndex
) {
  bool initialized = false;
  Vec3 minimum = {};
  Vec3 maximum = {};
  for (const MapFace& face : brush.faces) {
    for (Vec3 point : face.points) {
      point = scaleQuakeUnits(point);
      if (!initialized) {
        minimum = point;
        maximum = point;
        initialized = true;
        continue;
      }
      minimum.x = std::min(minimum.x, point.x);
      minimum.y = std::min(minimum.y, point.y);
      minimum.z = std::min(minimum.z, point.z);
      maximum.x = std::max(maximum.x, point.x);
      maximum.y = std::max(maximum.y, point.y);
      maximum.z = std::max(maximum.z, point.z);
    }
  }

  std::ostringstream output;
  output << "line " << brush.line << ": " << ownerClass << " brush " << brushIndex;
  if (initialized) {
    const Vec3 center = (minimum + maximum) * 0.5F;
    output << " near " << center.x << ',' << center.y << ',' << center.z;
  }
  output << ": ";
  return output.str();
}

void sortFaceVertices(ArenaBrush& brush, ArenaBrushFace& face) {
  if (face.vertexCount < 3) {
    return;
  }
  Vec3 center = {};
  for (std::uint8_t index = 0; index < face.vertexCount; ++index) {
    center += brush.vertices[face.vertices[index]];
  }
  center = center / static_cast<float>(face.vertexCount);

  Vec3 tangent = normalize(cross(face.normal, Vec3{0.0F, 0.0F, 1.0F}));
  // Build a local 2D basis on the face and sort by polar angle. Nearly horizontal
  // faces need an explicit tangent because crossing parallel axes degenerates.
  if (length(tangent) <= kPlaneEpsilon) {
    tangent = Vec3{1.0F, 0.0F, 0.0F};
  }
  const Vec3 bitangent = normalize(cross(face.normal, tangent));

  std::sort(
    face.vertices.begin(),
    face.vertices.begin() + face.vertexCount,
    [&](std::uint8_t lhs, std::uint8_t rhs) {
      const Vec3 lhsDelta = brush.vertices[lhs] - center;
      const Vec3 rhsDelta = brush.vertices[rhs] - center;
      const float lhsAngle = std::atan2(dot(lhsDelta, bitangent), dot(lhsDelta, tangent));
      const float rhsAngle = std::atan2(dot(rhsDelta, bitangent), dot(rhsDelta, tangent));
      return lhsAngle < rhsAngle;
    }
  );
}

[[nodiscard]] bool buildBrushHull(
  ArenaBrush& arenaBrush,
  const std::string& context,
  std::string& error
) {
  arenaBrush.vertexCount = 0;
  for (std::uint8_t faceIndex = 0; faceIndex < arenaBrush.faceCount; ++faceIndex) {
    arenaBrush.faces[faceIndex].vertexCount = 0;
    arenaBrush.faces[faceIndex].vertices = {};
  }

  for (std::size_t first = 0; first < arenaBrush.faceCount; ++first) {
    // A convex brush vertex is the intersection of three boundary planes that
    // lies inside every half-space; no source polygon winding is required.
    for (std::size_t second = first + 1; second < arenaBrush.faceCount; ++second) {
      for (std::size_t third = second + 1; third < arenaBrush.faceCount; ++third) {
        Vec3 point;
        if (!intersectPlanes(
          arenaBrush.faces[first],
          arenaBrush.faces[second],
          arenaBrush.faces[third],
          point
        )) {
          continue;
        }
        bool inside = true;
        for (std::size_t faceIndex = 0; faceIndex < arenaBrush.faceCount; ++faceIndex) {
          const ArenaBrushFace& face = arenaBrush.faces[faceIndex];
          if (dot(face.normal, point) > face.distance + kPlaneEpsilon) {
            inside = false;
            break;
          }
        }
        if (!inside) {
          continue;
        }
        std::uint8_t vertexIndex = 0;
        if (!addUniqueVertex(arenaBrush, point, vertexIndex)) {
          error = context + "convex brush has too many vertices";
          return false;
        }
      }
    }
  }

  if (arenaBrush.vertexCount < 4) {
    error = context + "convex brush has no closed volume";
    return false;
  }

  for (std::uint8_t faceIndex = 0; faceIndex < arenaBrush.faceCount; ++faceIndex) {
    ArenaBrushFace& face = arenaBrush.faces[faceIndex];
    for (std::uint8_t vertexIndex = 0; vertexIndex < arenaBrush.vertexCount; ++vertexIndex) {
      if (std::fabs(dot(face.normal, arenaBrush.vertices[vertexIndex]) - face.distance) <= 0.002F) {
        if (face.vertexCount >= ArenaBrushFace::kMaxVertices) {
          error = context + "convex brush face has too many vertices";
          return false;
        }
        face.vertices[face.vertexCount++] = vertexIndex;
      }
    }
    if (face.vertexCount < 3) {
      error = context + "convex brush face has too few vertices";
      return false;
    }
    sortFaceVertices(arenaBrush, face);
  }

  arenaBrush.min = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
  arenaBrush.max = {-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()};
  for (std::uint8_t index = 0; index < arenaBrush.vertexCount; ++index) {
    const Vec3 point = arenaBrush.vertices[index];
    arenaBrush.min.x = std::min(arenaBrush.min.x, point.x);
    arenaBrush.min.y = std::min(arenaBrush.min.y, point.y);
    arenaBrush.min.z = std::min(arenaBrush.min.z, point.z);
    arenaBrush.max.x = std::max(arenaBrush.max.x, point.x);
    arenaBrush.max.y = std::max(arenaBrush.max.y, point.y);
    arenaBrush.max.z = std::max(arenaBrush.max.z, point.z);
  }
  return true;
}

[[nodiscard]] bool convertConvexBrush(
  const MapBrush& brush,
  std::string_view ownerClass,
  std::size_t brushIndex,
  ArenaBrush& arenaBrush,
  std::string& error
) {
  const std::string context = brushContext(brush, ownerClass, brushIndex);
  if (brush.faces.size() < 4 || brush.faces.size() > ArenaBrush::kMaxFaces) {
    error = context + "convex brush has unsupported face count";
    return false;
  }

  std::vector<Vec3> scaledPoints;
  for (const MapFace& face : brush.faces) {
    for (Vec3 point : face.points) {
      scaledPoints.push_back(scaleQuakeUnits(point));
    }
  }
  if (scaledPoints.empty()) {
    error = context + "worldspawn brush has no points";
    return false;
  }

  arenaBrush = {};
  arenaBrush.faceCount = static_cast<std::uint8_t>(brush.faces.size());
  std::array<Vec3, ArenaBrush::kMaxFaces> rawNormals = {};
  std::array<float, ArenaBrush::kMaxFaces> rawDistances = {};
  for (std::size_t index = 0; index < brush.faces.size(); ++index) {
    const MapFace& mapFace = brush.faces[index];
    Vec3 first = scaleQuakeUnits(mapFace.points[0]);
    Vec3 second = scaleQuakeUnits(mapFace.points[1]);
    Vec3 third = scaleQuakeUnits(mapFace.points[2]);
    Vec3 normal = normalize(cross(second - first, third - first));
    if (length(normal) <= kPlaneEpsilon) {
      error = context + "face at line " + std::to_string(mapFace.line) + " is degenerate";
      return false;
    }
    float distance = dot(normal, first);
    rawNormals[index] = normal;
    rawDistances[index] = distance;
    float maxPositiveDistance = 0.0F;
    float maxNegativeDistance = 0.0F;
    int positivePoints = 0;
    int negativePoints = 0;
    for (Vec3 point : scaledPoints) {
      const float side = dot(normal, point) - distance;
      if (side > kPlaneEpsilon) {
        ++positivePoints;
        maxPositiveDistance = std::max(maxPositiveDistance, side);
      }
      if (side < -kPlaneEpsilon) {
        ++negativePoints;
        maxNegativeDistance = std::max(maxNegativeDistance, -side);
      }
    }
    if (
      positivePoints < negativePoints ||
      (positivePoints == negativePoints && maxPositiveDistance < maxNegativeDistance)
    ) {
      normal *= -1.0F;
      distance *= -1.0F;
    }

    ArenaBrushFace& face = arenaBrush.faces[index];
    face.normal = normal;
    face.distance = distance;
    face.materialId = arenaMaterialId(mapFace.material);
    face.textureProjection = textureProjectionForFace(normal, mapFace);
    if (arenaBrush.materialId == 0U && face.materialId != 0U) {
      arenaBrush.materialId = face.materialId;
    }
  }

  if (buildBrushHull(arenaBrush, context, error)) {
    return true;
  }

  for (std::uint8_t faceIndex = 0; faceIndex < arenaBrush.faceCount; ++faceIndex) {
    arenaBrush.faces[faceIndex].normal = rawNormals[faceIndex] * -1.0F;
    arenaBrush.faces[faceIndex].distance = rawDistances[faceIndex] * -1.0F;
  }
  if (buildBrushHull(arenaBrush, context, error)) {
    return true;
  }

  if (arenaBrush.faceCount > 12) {
    return false;
  }

  const std::uint32_t combinations = 1U << arenaBrush.faceCount;
  for (std::uint32_t mask = 0; mask < combinations; ++mask) {
    for (std::uint8_t faceIndex = 0; faceIndex < arenaBrush.faceCount; ++faceIndex) {
      const float sign = (mask & (1U << faceIndex)) == 0U ? 1.0F : -1.0F;
      arenaBrush.faces[faceIndex].normal = rawNormals[faceIndex] * sign;
      arenaBrush.faces[faceIndex].distance = rawDistances[faceIndex] * sign;
    }
    if (buildBrushHull(arenaBrush, context, error)) {
      return true;
    }
  }
  error = context + "convex brush has no closed volume";
  return false;
}

[[nodiscard]] bool convertSolidBrushes(
  const MapEntity& entity,
  std::string_view ownerClass,
  bool requireSourceLocator,
  std::vector<ArenaWall>& walls,
  std::vector<ArenaBrush>& brushes,
  std::vector<ArenaWall>& visualWalls,
  std::vector<ArenaBrush>& visualBrushes,
  std::string& error
) {
  SourceBrushMetadata metadata;
  if (!parseSourceBrushMetadata(entity, metadata, error)) {
    return false;
  }
  if (metadata.renderOnly && ownerClass != "func_group") {
    error = "line " + std::to_string(entity.line) +
      ": lg_geometry_role is supported only on func_group";
    return false;
  }
  if (
    requireSourceLocator && !metadata.renderOnly &&
    metadata.entityIndex == kInvalidSourceGeometryIndex
  ) {
    error = "line " + std::to_string(entity.line) +
      ": source-bound imported func_group requires lg_source_entity_index and "
      "lg_source_brush_index";
    return false;
  }
  for (std::size_t brushIndex = 0; brushIndex < entity.brushes.size(); ++brushIndex) {
    const MapBrush& brush = entity.brushes[brushIndex];
    const ArenaCollisionKind collisionKind = inferredCollisionKind(brush);
    const bool collisionOnly = collisionKind != ArenaCollisionKind::VisibleSolid;
    if (metadata.renderOnly && collisionOnly) {
      error = "line " + std::to_string(brush.line) +
        ": render-only geometry must use visible materials";
      return false;
    }
    if (!collisionOnly && brushHasCollisionOnlyMaterial(brush)) {
      error = "line " + std::to_string(brush.line) +
        ": collision-only brushes must use one clip class on every face";
      return false;
    }
    if (metadata.hasCollisionKind && metadata.collisionKind != collisionKind) {
      error = "line " + std::to_string(brush.line) +
        ": lg_collision_class " + std::string(collisionKindName(metadata.collisionKind)) +
        " does not match brush materials classified as " +
        std::string(collisionKindName(collisionKind));
      return false;
    }
    ArenaWall wall;
    if (convertCuboidBrush(brush, wall, error)) {
      wall.collisionKind = collisionKind;
      wall.sourceEntityIndex = metadata.entityIndex;
      wall.sourceBrushIndex = metadata.brushIndex;
      wall.sourcePatchIndex = metadata.patchIndex;
      wall.sourcePatchPieceIndex = metadata.patchPieceIndex;
      wall.renderable = !collisionOnly;
      if (collisionOnly) {
        clearRenderableMaterial(wall);
      }
      if (metadata.renderOnly) {
        visualWalls.push_back(wall);
      } else {
        walls.push_back(wall);
      }
      continue;
    }
    ArenaBrush arenaBrush;
    if (!convertConvexBrush(brush, ownerClass, brushIndex, arenaBrush, error)) {
      return false;
    }
    arenaBrush.collisionKind = collisionKind;
    arenaBrush.sourceEntityIndex = metadata.entityIndex;
    arenaBrush.sourceBrushIndex = metadata.brushIndex;
    arenaBrush.sourcePatchIndex = metadata.patchIndex;
    arenaBrush.sourcePatchPieceIndex = metadata.patchPieceIndex;
    arenaBrush.renderable = !collisionOnly;
    if (collisionOnly) {
      clearRenderableMaterial(arenaBrush);
    }
    if (metadata.renderOnly) {
      visualBrushes.push_back(arenaBrush);
    } else {
      brushes.push_back(arenaBrush);
    }
  }
  return true;
}

[[nodiscard]] bool isSpawnClass(std::string_view classname) {
  return classname == "lg_spawn" || classname == "info_player_team";
}

[[nodiscard]] bool parseTeamProperty(
  const MapEntity& entity,
  bool required,
  Team& team,
  std::string& error
) {
  const std::string* value = entity.property("team");
  if (value == nullptr) {
    if (!required) {
      team = Team::None;
      return true;
    }
    error = "line " + std::to_string(entity.line) + ": entity is missing team 'red' or 'blue'";
    return false;
  }
  if (*value == "red") {
    team = Team::Red;
    return true;
  }
  if (*value == "blue") {
    team = Team::Blue;
    return true;
  }
  error = "line " + std::to_string(entity.line) + ": team must be 'red' or 'blue'";
  return false;
}

[[nodiscard]] bool parseSpawnGroupProperty(
  const MapEntity& entity,
  Team legacyTeam,
  ArenaSpawnGroup& group,
  std::string& error
) {
  const std::string* value = entity.property("spawn_group");
  if (value == nullptr) {
    group = legacyTeam == Team::Red
      ? ArenaSpawnGroup::RedBase
      : legacyTeam == Team::Blue
        ? ArenaSpawnGroup::BlueBase
        : ArenaSpawnGroup::None;
    return true;
  }
  if (*value == "red_base") {
    group = ArenaSpawnGroup::RedBase;
    return true;
  }
  if (*value == "blue_base") {
    group = ArenaSpawnGroup::BlueBase;
    return true;
  }
  error = "line " + std::to_string(entity.line) +
    ": spawn_group must be 'red_base' or 'blue_base'";
  return false;
}

[[nodiscard]] bool convertMcGuffinBaseEntity(
  const MapEntity& entity,
  ArenaMcGuffinBase& base,
  std::string& error
) {
  if (!parseTeamProperty(entity, true, base.team, error)) {
    return false;
  }
  if (entity.brushes.size() != 1U) {
    error = "line " + std::to_string(entity.line) +
      ": trigger_mcguffin_base must contain exactly one cuboid brush";
    return false;
  }
  ArenaWall volume;
  if (!convertCuboidBrush(entity.brushes[0], volume, error)) {
    error = "line " + std::to_string(entity.line) +
      ": trigger_mcguffin_base requires a non-degenerate cuboid brush";
    return false;
  }
  base.min = volume.min;
  base.max = volume.max;
  return true;
}

[[nodiscard]] bool isLightClass(std::string_view classname) {
  return classname == "light" || classname == "light_point";
}

[[nodiscard]] bool isSunLightClass(std::string_view classname) {
  return classname == "light_sun";
}

[[nodiscard]] bool healthPickupTypeForClass(
  std::string_view classname,
  HealthPickupType& type
) {
  if (classname == "item_health_small") {
    type = HealthPickupType::Small;
    return true;
  }
  if (classname == "item_health_large") {
    type = HealthPickupType::Large;
    return true;
  }
  return false;
}

[[nodiscard]] bool normalizeColor(Vec3& color, std::string& error, const MapEntity& entity) {
  if (color.x > 1.0F || color.y > 1.0F || color.z > 1.0F) {
    color = color / 255.0F;
  }
  if (
    color.x < 0.0F || color.y < 0.0F || color.z < 0.0F ||
    color.x > 1.0F || color.y > 1.0F || color.z > 1.0F
  ) {
    error = "line " + std::to_string(entity.line) +
      ": light color channels must be normalized or 0..255";
    return false;
  }
  return true;
}

[[nodiscard]] bool parseLightColor(
  const MapEntity& entity,
  ArenaStaticLight& light,
  std::string& error
) {
  const std::string* color = entity.property("_color");
  if (color == nullptr) {
    color = entity.property("color");
  }
  if (color == nullptr) {
    return true;
  }
  if (!parseSpaceVec3(*color, light.color)) {
    error = "line " + std::to_string(entity.line) + ": light color must be 'r g b'";
    return false;
  }
  return normalizeColor(light.color, error, entity);
}

[[nodiscard]] bool parseQuakeLightTuple(
  const MapEntity& entity,
  ArenaStaticLight& light,
  std::string& error
) {
  const std::string* value = entity.property("_light");
  if (value == nullptr) {
    return true;
  }

  std::istringstream input{*value};
  std::vector<float> values;
  std::string token;
  while (input >> token) {
    float parsed = 0.0F;
    if (!parseFloat(token, parsed)) {
      error = "line " + std::to_string(entity.line) +
        ": _light values must be finite floats";
      return false;
    }
    values.push_back(parsed);
  }

  if (values.size() == 1U) {
    if (values[0] <= 0.0F) {
      error = "line " + std::to_string(entity.line) + ": _light intensity must be positive";
      return false;
    }
    light.intensity = values[0] / 300.0F;
    return true;
  }
  if (values.size() == 4U) {
    if (
      values[0] < 0.0F || values[1] < 0.0F || values[2] < 0.0F ||
      values[0] > 1.0F || values[1] > 1.0F || values[2] > 1.0F ||
      values[3] <= 0.0F
    ) {
      error = "line " + std::to_string(entity.line) +
        ": _light must be intensity or 'r g b intensity'";
      return false;
    }
    light.color = {values[0], values[1], values[2]};
    light.intensity = values[3] / 300.0F;
    return true;
  }

  error = "line " + std::to_string(entity.line) +
    ": _light must be intensity or 'r g b intensity'";
  return false;
}

[[nodiscard]] bool parseLightBool(
  const MapEntity& entity,
  std::string_view key,
  bool& value,
  std::string& error
) {
  const std::string* text = entity.property(key);
  if (text == nullptr) {
    return true;
  }
  if (!parseBool(*text, value)) {
    error = "line " + std::to_string(entity.line) + ": light " +
      std::string(key) + " must be one of 0, 1, false, true, no, yes, off, or on";
    return false;
  }
  return true;
}

[[nodiscard]] bool parseStaticLightOptions(
  const MapEntity& entity,
  ArenaStaticLight& light,
  float radiusQuakeUnits,
  std::string& error
) {
  if (!parseLightBool(entity, "casts_shadows", light.castsShadows, error)) {
    return false;
  }

  float sourceRadiusQuakeUnits = 0.0F;
  if (const std::string* text = entity.property("source_radius")) {
    if (!parseFloat(*text, sourceRadiusQuakeUnits) ||
      sourceRadiusQuakeUnits < 0.0F ||
      sourceRadiusQuakeUnits > kMaxLightSourceRadiusQuakeUnits) {
      error = "line " + std::to_string(entity.line) +
        ": light source_radius must be a finite float from 0 to " +
        std::to_string(static_cast<int>(kMaxLightSourceRadiusQuakeUnits));
      return false;
    }
    if (sourceRadiusQuakeUnits > radiusQuakeUnits) {
      error = "line " + std::to_string(entity.line) +
        ": light source_radius must not exceed radius";
      return false;
    }
  }
  light.sourceRadius = sourceRadiusQuakeUnits * kQuakeToLgScale;

  if (const std::string* text = entity.property("priority")) {
    std::int32_t priority = 0;
    if (!parseInt32(*text, priority) ||
      priority < kMinLightPriority || priority > kMaxLightPriority) {
      error = "line " + std::to_string(entity.line) + ": light priority must be an integer from " +
        std::to_string(kMinLightPriority) + " to " + std::to_string(kMaxLightPriority);
      return false;
    }
    light.priority = static_cast<std::int16_t>(priority);
  }

  if (!parseLightBool(entity, "flicker", light.flickerEnabled, error)) {
    return false;
  }
  if (const std::string* text = entity.property("flicker_seed")) {
    if (!parseUint32(*text, light.flickerSeed)) {
      error = "line " + std::to_string(entity.line) +
        ": light flicker_seed must be an unsigned 32-bit integer";
      return false;
    }
  }

  const std::string* frequencyText = entity.property("flicker_frequency");
  if (light.flickerEnabled) {
    light.flickerFrequencyHz = kDefaultLightFlickerFrequencyHz;
  }
  if (frequencyText != nullptr) {
    if (!parseFloat(*frequencyText, light.flickerFrequencyHz) ||
      light.flickerFrequencyHz < 0.0F ||
      light.flickerFrequencyHz > kMaxLightFlickerFrequencyHz) {
      error = "line " + std::to_string(entity.line) +
        ": light flicker_frequency must be a finite float from 0 to " +
        std::to_string(static_cast<int>(kMaxLightFlickerFrequencyHz));
      return false;
    }
  }
  if (light.flickerEnabled && light.flickerFrequencyHz <= 0.0F) {
    error = "line " + std::to_string(entity.line) +
      ": light flicker_frequency must be positive when flicker is enabled";
    return false;
  }
  if (!light.flickerEnabled && light.flickerFrequencyHz > 0.0F) {
    error = "line " + std::to_string(entity.line) +
      ": light flicker_frequency may be positive only when flicker is enabled";
    return false;
  }

  const auto parseFlickerFactor = [&](std::string_view key, float& value) {
    const std::string* text = entity.property(key);
    if (text == nullptr) {
      return true;
    }
    if (!parseFloat(*text, value) || value < 0.0F || value > kMaxLightFlickerFactor) {
      error = "line " + std::to_string(entity.line) + ": light " + std::string(key) +
        " must be a finite float from 0 to " +
        std::to_string(static_cast<int>(kMaxLightFlickerFactor));
      return false;
    }
    return true;
  };
  if (
    !parseFlickerFactor("flicker_min", light.flickerMinFactor) ||
    !parseFlickerFactor("flicker_max", light.flickerMaxFactor)
  ) {
    return false;
  }
  if (light.flickerMinFactor > light.flickerMaxFactor) {
    error = "line " + std::to_string(entity.line) +
      ": light flicker_min must not exceed flicker_max";
    return false;
  }
  return true;
}

[[nodiscard]] bool convertLightEntity(
  const MapEntity& entity,
  ArenaStaticLight& light,
  std::string& error
) {
  const std::string* origin = entity.property("origin");
  if (origin == nullptr) {
    error = "line " + std::to_string(entity.line) + ": light entity is missing origin";
    return false;
  }
  if (!parseSpaceVec3(*origin, light.position)) {
    error = "line " + std::to_string(entity.line) + ": light origin must be 'x y z'";
    return false;
  }
  light.position = scaleQuakeUnits(light.position);
  light.intensity = kDefaultLightIntensity;
  light.radius = scaleQuakeUnits({kDefaultLightRadiusQuakeUnits, 0.0F, 0.0F}).x;

  if (
    !parseLightColor(entity, light, error) ||
    !parseQuakeLightTuple(entity, light, error)
  ) {
    return false;
  }
  if (const std::string* value = entity.property("light")) {
    if (!parseFloat(*value, light.intensity) || light.intensity <= 0.0F) {
      error = "line " + std::to_string(entity.line) +
        ": light intensity must be a positive finite float";
      return false;
    }
    light.intensity /= 300.0F;
  }
  if (!parsePositiveFloat(entity, "intensity", light.intensity, error)) {
    return false;
  }
  float radiusQuakeUnits = light.radius / kQuakeToLgScale;
  if (!parsePositiveFloat(entity, "radius", radiusQuakeUnits, error)) {
    return false;
  }
  if (light.intensity > kMaxLightIntensity) {
    error = "line " + std::to_string(entity.line) + ": light intensity must not exceed " +
      std::to_string(static_cast<int>(kMaxLightIntensity));
    return false;
  }
  if (radiusQuakeUnits > kMaxLightRadiusQuakeUnits) {
    error = "line " + std::to_string(entity.line) + ": light radius must not exceed " +
      std::to_string(static_cast<int>(kMaxLightRadiusQuakeUnits));
    return false;
  }
  if (!parseStaticLightOptions(entity, light, radiusQuakeUnits, error)) {
    return false;
  }
  light.radius = radiusQuakeUnits * kQuakeToLgScale;
  return true;
}

[[nodiscard]] bool parseSunColor(
  const MapEntity& entity,
  ArenaSunLight& light,
  std::string& error
) {
  const std::string* color = entity.property("color");
  if (color == nullptr) {
    color = entity.property("_color");
  }
  if (color == nullptr) {
    return true;
  }
  if (!parseSpaceVec3(*color, light.color)) {
    error = "line " + std::to_string(entity.line) + ": light_sun color must be 'r g b'";
    return false;
  }
  return normalizeColor(light.color, error, entity);
}

[[nodiscard]] bool parseSunAngleFallback(
  const MapEntity& entity,
  ArenaSunLight& light,
  std::string& error
) {
  const std::string* angleText = entity.property("angle");
  const std::string* pitchText = entity.property("pitch");
  if (angleText == nullptr && pitchText == nullptr) {
    light.direction = kDefaultSunDirection;
    return true;
  }

  float angle = 0.0F;
  float pitch = -45.0F;
  if (angleText != nullptr && !parseFloat(*angleText, angle)) {
    error = "line " + std::to_string(entity.line) + ": light_sun angle must be a finite float";
    return false;
  }
  if (pitchText != nullptr && !parseFloat(*pitchText, pitch)) {
    error = "line " + std::to_string(entity.line) + ": light_sun pitch must be a finite float";
    return false;
  }

  const float yawRadians = angle * kDegreesToRadians;
  const float pitchRadians = pitch * kDegreesToRadians;
  const float pitchCos = std::cos(pitchRadians);
  light.direction = normalize({
    std::cos(yawRadians) * pitchCos,
    std::sin(yawRadians) * pitchCos,
    std::sin(pitchRadians),
  });
  return true;
}

[[nodiscard]] bool convertSunLightEntity(
  const MapEntity& entity,
  ArenaSunLight& light,
  std::string& error
) {
  light.enabled = true;
  light.intensity = kDefaultSunIntensity;
  light.color = kDefaultSunColor;
  light.direction = kDefaultSunDirection;

  if (!parseSunColor(entity, light, error) ||
      !parseNonNegativeFloat(entity, "intensity", light.intensity, error)) {
    return false;
  }

  if (const std::string* direction = entity.property("direction")) {
    if (!parseSpaceVec3(*direction, light.direction)) {
      error = "line " + std::to_string(entity.line) +
        ": light_sun direction must be 'x y z'";
      return false;
    }
    light.direction = normalize(light.direction);
  } else if (!parseSunAngleFallback(entity, light, error)) {
    return false;
  }

  if (
    length(light.direction) <= 0.0001F ||
    !std::isfinite(light.direction.x) ||
    !std::isfinite(light.direction.y) ||
    !std::isfinite(light.direction.z)
  ) {
    error = "line " + std::to_string(entity.line) +
      ": light_sun direction must be non-zero finite vector";
    return false;
  }
  return true;
}

[[nodiscard]] bool convertTargetPositionEntity(
  const MapEntity& entity,
  TargetPosition& target,
  std::string& error
) {
  const MapProperty* targetname = findProperty(entity, "targetname");
  if (targetname == nullptr || targetname->value.empty()) {
    error = "line " + std::to_string(entity.line) +
      ": target_position is missing targetname";
    return false;
  }
  const MapProperty* origin = findProperty(entity, "origin");
  if (origin == nullptr) {
    error = "line " + std::to_string(entity.line) +
      ": target_position is missing origin";
    return false;
  }
  Vec3 position = {};
  if (!parseSpaceVec3(origin->value, position)) {
    error = "line " + std::to_string(origin->line) +
      ": target_position origin must be 'x y z'";
    return false;
  }
  target.targetname = targetname->value;
  target.position = scaleQuakeUnits(position);
  if (!parseOptionalYaw(entity, "target_position", target.yawRadians, error)) {
    return false;
  }
  target.line = entity.line;
  return true;
}

[[nodiscard]] bool convertHealthPickupEntity(
  const MapEntity& entity,
  HealthPickupType type,
  ArenaHealthPickup& pickup,
  std::string& error
) {
  const MapProperty* origin = findProperty(entity, "origin");
  if (origin == nullptr) {
    error = "line " + std::to_string(entity.line) +
      ": health pickup is missing origin";
    return false;
  }
  Vec3 position = {};
  if (!parseSpaceVec3(origin->value, position)) {
    error = "line " + std::to_string(origin->line) +
      ": health pickup origin must be 'x y z'";
    return false;
  }
  pickup.position = scaleQuakeUnits(position);
  pickup.type = type;
  return true;
}

[[nodiscard]] const TargetPosition* findTargetPosition(
  const std::vector<TargetPosition>& targets,
  std::string_view targetname
) {
  for (const TargetPosition& target : targets) {
    if (target.targetname == targetname) {
      return &target;
    }
  }
  return nullptr;
}

[[nodiscard]] bool convertJumpPadEntity(
  const MapEntity& entity,
  const std::vector<TargetPosition>& targets,
  std::vector<ArenaJumpPad>& jumpPads,
  std::string& error
) {
  if (entity.brushes.empty()) {
    error = "line " + std::to_string(entity.line) +
      ": trigger_jumppad requires at least one brush";
    return false;
  }

  float speed = kDefaultJumpPadSpeed;
  bool hasSpeed = false;
  if (!parseOptionalPositivePropertyFloat(
        entity,
        "speed",
        "trigger_jumppad",
        speed,
        hasSpeed,
        error
      )) {
    return false;
  }

  ArenaJumpPad templatePad;
  templatePad.targetSpeed = speed;
  templatePad.hasTargetSpeed = hasSpeed;
  if (const MapProperty* target = findProperty(entity, "target")) {
    const TargetPosition* targetPosition = findTargetPosition(targets, target->value);
    if (targetPosition == nullptr) {
      error = "line " + std::to_string(target->line) +
        ": trigger_jumppad target '" + target->value +
        "' does not match a target_position";
      return false;
    }
    templatePad.hasTarget = true;
    templatePad.targetPosition = targetPosition->position;
  } else if (!fallbackJumpPadVelocity(entity, speed, templatePad, error)) {
    return false;
  }

  for (const MapBrush& brush : entity.brushes) {
    if (jumpPads.size() >= Arena::kJumpPadCount) {
      error = "line " + std::to_string(entity.line) +
        ": too many trigger_jumppad brushes";
      return false;
    }
    ArenaJumpPad jumpPad = templatePad;
    if (!brushPointBounds(brush, jumpPad.min, jumpPad.max, "trigger_jumppad", error)) {
      return false;
    }
    jumpPads.push_back(jumpPad);
  }
  return true;
}

[[nodiscard]] bool convertTeleportEntity(
  const MapEntity& entity,
  const std::vector<TargetPosition>& targets,
  std::vector<ArenaTeleport>& teleports,
  std::string& error
) {
  const MapProperty* target = findProperty(entity, "target");
  if (target == nullptr || target->value.empty()) {
    error = "line " + std::to_string(entity.line) + ": trigger_teleport is missing target";
    return false;
  }
  const TargetPosition* destination = findTargetPosition(targets, target->value);
  if (destination == nullptr) {
    error = "line " + std::to_string(target->line) + ": trigger_teleport target '" +
      target->value + "' does not match a target_position";
    return false;
  }
  if (entity.brushes.empty()) {
    error = "line " + std::to_string(entity.line) + ": trigger_teleport requires at least one brush";
    return false;
  }
  for (const MapBrush& brush : entity.brushes) {
    if (teleports.size() >= Arena::kTeleportCount) {
      error = "line " + std::to_string(entity.line) + ": too many trigger_teleport brushes";
      return false;
    }
    ArenaTeleport teleport;
    if (!brushPointBounds(brush, teleport.min, teleport.max, "trigger_teleport", error)) {
      return false;
    }
    teleport.destination = destination->position;
    teleport.exitVelocity = {
      std::cos(destination->yawRadians) * kDefaultTeleportExitSpeed,
      std::sin(destination->yawRadians) * kDefaultTeleportExitSpeed,
      0.0F,
    };
    teleports.push_back(teleport);
  }
  return true;
}

void expandBounds(Vec3 point, Vec3& minimum, Vec3& maximum, bool& initialized) {
  if (!initialized) {
    minimum = point;
    maximum = point;
    initialized = true;
    return;
  }
  minimum.x = std::min(minimum.x, point.x);
  minimum.y = std::min(minimum.y, point.y);
  minimum.z = std::min(minimum.z, point.z);
  maximum.x = std::max(maximum.x, point.x);
  maximum.y = std::max(maximum.y, point.y);
  maximum.z = std::max(maximum.z, point.z);
}

} // namespace

ArenaLoadResult convertMapDocumentToArena(const MapDocument& document) {
  std::vector<ArenaWall> walls;
  std::vector<ArenaBrush> brushes;
  std::vector<ArenaWall> visualWalls;
  std::vector<ArenaBrush> visualBrushes;
  std::vector<ArenaStaticLight> staticLights;
  std::vector<ArenaJumpPad> jumpPads;
  std::vector<ArenaTeleport> teleports;
  std::vector<ArenaHealthPickup> healthPickups;
  std::vector<TargetPosition> targetPositions;
  ArenaAmbientLight ambientLight;
  ArenaSunLight sunLight;
  ArenaMcGuffinLayout mcguffin;
  bool hasSunLight = false;
  std::vector<Vec3> spawns;
  std::vector<Team> spawnTeams;
  std::vector<ArenaTeamSpawn> teamSpawns;
  bool hasBoundsMin = false;
  bool hasBoundsMax = false;
  Vec3 boundsMin = {};
  Vec3 boundsMax = {};
  bool sourceBoundImport = false;

  // A source-bound import is auditable only if every emitted source brush can
  // be traced back to the exact decompile. Ordinary hand-authored maps remain
  // free to use func_group without importer metadata.
  for (const MapEntity& entity : document.entities) {
    const std::string* classname = entity.property("classname");
    if (classname == nullptr || *classname != "worldspawn") {
      continue;
    }
    const MapProperty* sourceBsp = findProperty(entity, "lg_source_bsp_sha256");
    const MapProperty* rawDecompile = findProperty(entity, "lg_raw_decompile_sha256");
    if ((sourceBsp == nullptr) != (rawDecompile == nullptr)) {
      return {{}, false, "line " + std::to_string(entity.line) +
        ": imported world metadata requires both lg_source_bsp_sha256 and "
        "lg_raw_decompile_sha256"};
    }
    if (sourceBsp != nullptr) {
      if (!isSha256Hex(sourceBsp->value) || !isSha256Hex(rawDecompile->value)) {
        return {{}, false, "line " + std::to_string(entity.line) +
          ": imported source hashes must be 64 hexadecimal characters"};
      }
      sourceBoundImport = true;
    }
  }

  // Resolve named targets first so triggers may reference entities that appear
  // later in the map file without making entity order semantically significant.
  for (const MapEntity& entity : document.entities) {
    const std::string* classname = entity.property("classname");
    if (classname == nullptr || *classname != "target_position") {
      continue;
    }
    TargetPosition target;
    std::string error;
    if (!convertTargetPositionEntity(entity, target, error)) {
      return {{}, false, error};
    }
    if (findTargetPosition(targetPositions, target.targetname) != nullptr) {
      return {{}, false, "line " + std::to_string(entity.line) +
        ": duplicate target_position targetname '" + target.targetname + "'"};
    }
    targetPositions.push_back(std::move(target));
  }

  for (const MapEntity& entity : document.entities) {
    const std::string* classname = entity.property("classname");
    if (classname == nullptr) {
      continue;
    }
    HealthPickupType healthPickupType = HealthPickupType::Small;

    if (*classname == "worldspawn") {
      std::string error;
      if (const std::string* value = entity.property("lg_bounds_min")) {
        if (!parseSpaceVec3(*value, boundsMin)) {
          return {{}, false, "line " + std::to_string(entity.line) + ": lg_bounds_min must be 'x y z'"};
        }
        boundsMin = scaleQuakeUnits(boundsMin);
        hasBoundsMin = true;
      }
      if (const std::string* value = entity.property("lg_bounds_max")) {
        if (!parseSpaceVec3(*value, boundsMax)) {
          return {{}, false, "line " + std::to_string(entity.line) + ": lg_bounds_max must be 'x y z'"};
        }
        boundsMax = scaleQuakeUnits(boundsMax);
        hasBoundsMax = true;
      }
      if (const std::string* value = entity.property("lg_ambient_intensity")) {
        if (!parseFloat(*value, ambientLight.intensity) ||
            ambientLight.intensity < 0.0F) {
          return {{}, false, "line " + std::to_string(entity.line) +
            ": lg_ambient_intensity must be a non-negative finite float"};
        }
      }
      if (const std::string* value = entity.property("lg_ambient_color")) {
        if (!parseSpaceVec3(*value, ambientLight.color)) {
          return {{}, false, "line " + std::to_string(entity.line) +
            ": lg_ambient_color must be 'r g b'"};
        }
        if (!normalizeColor(ambientLight.color, error, entity)) {
          return {{}, false, error};
        }
      }
      if (!convertSolidBrushes(
            entity, *classname, false, walls, brushes, visualWalls, visualBrushes, error
          )) {
        return {{}, false, error};
      }
    } else if (*classname == "func_group") {
      std::string error;
      if (!convertSolidBrushes(
            entity,
            *classname,
            sourceBoundImport,
            walls,
            brushes,
            visualWalls,
            visualBrushes,
            error
          )) {
        return {{}, false, error};
      }
    } else if (isSpawnClass(*classname)) {
      if (spawns.size() >= Arena::kSpawnCount) {
        return {{}, false, "line " + std::to_string(entity.line) +
          ": too many spawn points"};
      }
      const std::string* origin = entity.property("origin");
      if (origin == nullptr) {
        return {{}, false, "line " + std::to_string(entity.line) + ": spawn entity is missing origin"};
      }
      Vec3 position;
      if (!parseSpaceVec3(*origin, position)) {
        return {{}, false, "line " + std::to_string(entity.line) + ": spawn origin must be 'x y z'"};
      }
      position = scaleQuakeUnits(position);
      float yawRadians = 0.0F;
      std::string error;
      if (!parseOptionalYaw(entity, "spawn", yawRadians, error)) {
        return {{}, false, error};
      }
      spawns.push_back(position);
      Team team = Team::None;
      std::string teamError;
      const bool hasSpawnGroup = entity.property("spawn_group") != nullptr;
      const bool teamRequired = *classname == "info_player_team" && !hasSpawnGroup;
      if (!parseTeamProperty(entity, teamRequired, team, teamError)) {
        return {{}, false, teamError};
      }
      spawnTeams.push_back(team);
      ArenaSpawnGroup group = ArenaSpawnGroup::None;
      if (!parseSpawnGroupProperty(entity, team, group, teamError)) {
        return {{}, false, teamError};
      }
      if (group != ArenaSpawnGroup::None) {
        if (teamSpawns.size() >= Arena::kTeamSpawnCount) {
          return {{}, false, "line " + std::to_string(entity.line) +
            ": too many team spawn points"};
        }
        teamSpawns.push_back({position, yawRadians, group});
      }
    } else if (*classname == "info_mcguffin_spawn") {
      if (mcguffin.hasNeutralSpawn) {
        return {{}, false, "line " + std::to_string(entity.line) +
          ": duplicate info_mcguffin_spawn"};
      }
      const std::string* origin = entity.property("origin");
      if (origin == nullptr || !parseSpaceVec3(*origin, mcguffin.neutralSpawn)) {
        return {{}, false, "line " + std::to_string(entity.line) +
          ": info_mcguffin_spawn requires origin 'x y z'"};
      }
      mcguffin.neutralSpawn = scaleQuakeUnits(mcguffin.neutralSpawn);
      mcguffin.hasNeutralSpawn = true;
    } else if (*classname == "trigger_mcguffin_base") {
      ArenaMcGuffinBase base;
      std::string error;
      if (!convertMcGuffinBaseEntity(entity, base, error)) {
        return {{}, false, error};
      }
      if (base.team == Team::Red) {
        if (mcguffin.hasRedBase) {
          return {{}, false, "line " + std::to_string(entity.line) +
            ": duplicate Red McGuffin base"};
        }
        mcguffin.redBase = base;
        mcguffin.hasRedBase = true;
      } else {
        if (mcguffin.hasBlueBase) {
          return {{}, false, "line " + std::to_string(entity.line) +
            ": duplicate Blue McGuffin base"};
        }
        mcguffin.blueBase = base;
        mcguffin.hasBlueBase = true;
      }
    } else if (isLightClass(*classname)) {
      if (staticLights.size() >= Arena::kStaticLightCount) {
        return {{}, false, "line " + std::to_string(entity.line) + ": too many light entities"};
      }
      ArenaStaticLight light;
      std::string error;
      if (!convertLightEntity(entity, light, error)) {
        return {{}, false, error};
      }
      staticLights.push_back(light);
    } else if (isSunLightClass(*classname)) {
      if (hasSunLight) {
        return {{}, false, "line " + std::to_string(entity.line) + ": multiple light_sun entities are not supported"};
      }
      std::string error;
      if (!convertSunLightEntity(entity, sunLight, error)) {
        return {{}, false, error};
      }
      hasSunLight = true;
    } else if (*classname == "trigger_jumppad") {
      std::string error;
      if (!convertJumpPadEntity(entity, targetPositions, jumpPads, error)) {
        return {{}, false, error};
      }
    } else if (healthPickupTypeForClass(*classname, healthPickupType)) {
      if (healthPickups.size() >= Arena::kHealthPickupCount) {
        return {{}, false, "line " + std::to_string(entity.line) + ": too many health pickups"};
      }
      ArenaHealthPickup pickup;
      std::string error;
      if (!convertHealthPickupEntity(entity, healthPickupType, pickup, error)) {
        return {{}, false, error};
      }
      healthPickups.push_back(pickup);
    } else if (*classname == "target_position") {
      continue;
    } else if (*classname == "trigger_teleport") {
      std::string error;
      if (!convertTeleportEntity(entity, targetPositions, teleports, error)) {
        return {{}, false, error};
      }
    }
  }

  std::vector<std::uint64_t> sourceBrushLocators;
  sourceBrushLocators.reserve(walls.size() + brushes.size());
  const auto recordSourceLocator = [&sourceBrushLocators](
    std::uint32_t entityIndex,
    std::uint32_t brushIndex
  ) {
    if (
      entityIndex == kInvalidSourceGeometryIndex ||
      brushIndex == kInvalidSourceGeometryIndex
    ) {
      return;
    }
    sourceBrushLocators.push_back(
      (static_cast<std::uint64_t>(entityIndex) << 32U) |
      static_cast<std::uint64_t>(brushIndex)
    );
  };
  for (const ArenaWall& wall : walls) {
    recordSourceLocator(wall.sourceEntityIndex, wall.sourceBrushIndex);
  }
  for (const ArenaBrush& brush : brushes) {
    recordSourceLocator(brush.sourceEntityIndex, brush.sourceBrushIndex);
  }
  std::sort(sourceBrushLocators.begin(), sourceBrushLocators.end());
  if (
    std::adjacent_find(sourceBrushLocators.begin(), sourceBrushLocators.end()) !=
    sourceBrushLocators.end()
  ) {
    return {{}, false, "duplicate imported source brush locator"};
  }

  std::vector<std::uint64_t> visualSourceBrushLocators;
  visualSourceBrushLocators.reserve(visualWalls.size() + visualBrushes.size());
  const auto recordVisualSourceBrush = [&visualSourceBrushLocators](
    std::uint32_t entityIndex,
    std::uint32_t brushIndex
  ) {
    if (
      entityIndex == kInvalidSourceGeometryIndex ||
      brushIndex == kInvalidSourceGeometryIndex
    ) {
      return;
    }
    visualSourceBrushLocators.push_back(
      (static_cast<std::uint64_t>(entityIndex) << 32U) |
      static_cast<std::uint64_t>(brushIndex)
    );
  };
  for (const ArenaWall& wall : visualWalls) {
    recordVisualSourceBrush(wall.sourceEntityIndex, wall.sourceBrushIndex);
  }
  for (const ArenaBrush& brush : visualBrushes) {
    recordVisualSourceBrush(brush.sourceEntityIndex, brush.sourceBrushIndex);
  }
  std::sort(visualSourceBrushLocators.begin(), visualSourceBrushLocators.end());
  if (
    std::adjacent_find(visualSourceBrushLocators.begin(), visualSourceBrushLocators.end()) !=
    visualSourceBrushLocators.end()
  ) {
    return {{}, false, "duplicate render-only source brush locator"};
  }

  std::vector<std::uint64_t> sourcePatchLocators;
  sourcePatchLocators.reserve(visualWalls.size() + visualBrushes.size());
  for (const ArenaWall& wall : visualWalls) {
    if (wall.sourcePatchIndex != kInvalidSourceGeometryIndex) {
      sourcePatchLocators.push_back(
        (static_cast<std::uint64_t>(wall.sourcePatchIndex) << 32U) |
        static_cast<std::uint64_t>(wall.sourcePatchPieceIndex)
      );
    }
  }
  for (const ArenaBrush& brush : visualBrushes) {
    if (brush.sourcePatchIndex != kInvalidSourceGeometryIndex) {
      sourcePatchLocators.push_back(
        (static_cast<std::uint64_t>(brush.sourcePatchIndex) << 32U) |
        static_cast<std::uint64_t>(brush.sourcePatchPieceIndex)
      );
    }
  }
  std::sort(sourcePatchLocators.begin(), sourcePatchLocators.end());
  if (
    std::adjacent_find(sourcePatchLocators.begin(), sourcePatchLocators.end()) !=
    sourcePatchLocators.end()
  ) {
    return {{}, false, "duplicate imported source patch locator"};
  }

  if (hasBoundsMin != hasBoundsMax) {
    return {{}, false, "worldspawn must define both lg_bounds_min and lg_bounds_max"};
  }
  if (!hasBoundsMin) {
    // Derived bounds include gameplay entities as well as solids, then add room
    // for player extents so edge spawns and targets are not immediately clamped.
    bool initialized = false;
    for (const ArenaWall& wall : walls) {
      expandBounds(wall.min, boundsMin, boundsMax, initialized);
      expandBounds(wall.max, boundsMin, boundsMax, initialized);
    }
    for (const ArenaBrush& brush : brushes) {
      expandBounds(brush.min, boundsMin, boundsMax, initialized);
      expandBounds(brush.max, boundsMin, boundsMax, initialized);
    }
    for (const ArenaWall& wall : visualWalls) {
      expandBounds(wall.min, boundsMin, boundsMax, initialized);
      expandBounds(wall.max, boundsMin, boundsMax, initialized);
    }
    for (const ArenaBrush& brush : visualBrushes) {
      expandBounds(brush.min, boundsMin, boundsMax, initialized);
      expandBounds(brush.max, boundsMin, boundsMax, initialized);
    }
    for (const ArenaJumpPad& jumpPad : jumpPads) {
      expandBounds(jumpPad.min, boundsMin, boundsMax, initialized);
      expandBounds(jumpPad.max, boundsMin, boundsMax, initialized);
      if (jumpPad.hasTarget) {
        expandBounds(jumpPad.targetPosition, boundsMin, boundsMax, initialized);
      }
    }
    for (const ArenaTeleport& teleport : teleports) {
      expandBounds(teleport.min, boundsMin, boundsMax, initialized);
      expandBounds(teleport.max, boundsMin, boundsMax, initialized);
      expandBounds(teleport.destination, boundsMin, boundsMax, initialized);
    }
    for (const ArenaHealthPickup& pickup : healthPickups) {
      expandBounds(pickup.position, boundsMin, boundsMax, initialized);
    }
    for (Vec3 spawn : spawns) {
      expandBounds(spawn, boundsMin, boundsMax, initialized);
    }
    if (mcguffin.hasNeutralSpawn) {
      expandBounds(mcguffin.neutralSpawn, boundsMin, boundsMax, initialized);
    }
    if (mcguffin.hasRedBase) {
      expandBounds(mcguffin.redBase.min, boundsMin, boundsMax, initialized);
      expandBounds(mcguffin.redBase.max, boundsMin, boundsMax, initialized);
    }
    if (mcguffin.hasBlueBase) {
      expandBounds(mcguffin.blueBase.min, boundsMin, boundsMax, initialized);
      expandBounds(mcguffin.blueBase.max, boundsMin, boundsMax, initialized);
    }
    boundsMin = {boundsMin.x - kBoundsPadding, boundsMin.y - kBoundsPadding, boundsMin.z - kBoundsPadding};
    boundsMax = {boundsMax.x + kBoundsPadding, boundsMax.y + kBoundsPadding, boundsMax.z + kBoundsPadding};
  }

  std::ostringstream arenaText;
  arenaText << "version 1\n";
  arenaText << "bounds min=" << boundsMin.x << ',' << boundsMin.y << ',' << boundsMin.z
        << " max=" << boundsMax.x << ',' << boundsMax.y << ',' << boundsMax.z << '\n';
  const bool needsValidationPlaceholder = walls.empty() &&
    (!brushes.empty() || !visualWalls.empty() || !visualBrushes.empty());
  // Reuse the arena text loader for shared bounds/spawn validation. Brush-only
  // maps need one temporary box because that legacy validator requires a wall.
  const std::size_t emittedWallCount = needsValidationPlaceholder ? 1U : walls.size();
  for (std::size_t index = 0; index < emittedWallCount; ++index) {
    ArenaWall placeholder;
    if (needsValidationPlaceholder) {
      if (!brushes.empty()) {
        placeholder = {brushes[0].min, brushes[0].max};
      } else if (!visualWalls.empty()) {
        placeholder = visualWalls[0];
      } else {
        placeholder = {visualBrushes[0].min, visualBrushes[0].max};
      }
    }
    const ArenaWall& wall = needsValidationPlaceholder ? placeholder : walls[index];
    arenaText << "box brush_" << index << ' '
          << wall.min.x << ',' << wall.min.y << ',' << wall.min.z << ' '
          << wall.max.x << ',' << wall.max.y << ',' << wall.max.z << '\n';
  }
  for (std::size_t index = 0; index < spawns.size(); ++index) {
    const Vec3 spawn = spawns[index];
    arenaText << "spawn spawn_" << index << ' '
          << spawn.x << ',' << spawn.y << ',' << spawn.z << " yaw=0\n";
  }
  ArenaLoadResult result = loadArenaFromText(arenaText.str());
  if (result.ok) {
    result.arena.renderDefaultFloor = !sourceBoundImport;
    if (brushes.size() > Arena::kBrushCount) {
      return {{}, false, "map has too many convex brushes"};
    }
    if (visualWalls.size() > Arena::kVisualWallCount) {
      return {{}, false, "map has too many visual-only walls"};
    }
    if (visualBrushes.size() > Arena::kVisualBrushCount) {
      return {{}, false, "map has too many visual-only convex brushes"};
    }
    result.arena.wallCount = walls.size();
    // Remove the validation placeholder and install the exact convex geometry;
    // collision and rendering must use brush planes, not their broad-phase AABB.
    for (std::size_t index = 0; index < result.arena.wallCount && index < walls.size(); ++index) {
      result.arena.walls[index].materialId = walls[index].materialId;
      result.arena.walls[index].faceMaterialIds = walls[index].faceMaterialIds;
      result.arena.walls[index].faceTextureProjections =
        walls[index].faceTextureProjections;
      result.arena.walls[index].collisionKind = walls[index].collisionKind;
      result.arena.walls[index].sourceEntityIndex = walls[index].sourceEntityIndex;
      result.arena.walls[index].sourceBrushIndex = walls[index].sourceBrushIndex;
      result.arena.walls[index].renderable = walls[index].renderable;
    }
    result.arena.brushCount = brushes.size();
    for (std::size_t index = 0; index < result.arena.brushCount; ++index) {
      result.arena.brushes[index] = brushes[index];
    }
    result.arena.visualWallCount = visualWalls.size();
    for (std::size_t index = 0; index < result.arena.visualWallCount; ++index) {
      result.arena.visualWalls[index] = visualWalls[index];
    }
    result.arena.visualBrushCount = visualBrushes.size();
    for (std::size_t index = 0; index < result.arena.visualBrushCount; ++index) {
      result.arena.visualBrushes[index] = visualBrushes[index];
    }
    result.arena.staticLightCount = staticLights.size();
    for (std::size_t index = 0; index < result.arena.staticLightCount; ++index) {
      result.arena.staticLights[index] = staticLights[index];
    }
    result.arena.ambientLight = ambientLight;
    result.arena.sunLight = sunLight;
    result.arena.jumpPadCount = jumpPads.size();
    for (std::size_t index = 0; index < result.arena.jumpPadCount; ++index) {
      result.arena.jumpPads[index] = jumpPads[index];
    }
    result.arena.teleportCount = teleports.size();
    for (std::size_t index = 0; index < result.arena.teleportCount; ++index) {
      result.arena.teleports[index] = teleports[index];
    }
    result.arena.healthPickupCount = healthPickups.size();
    for (std::size_t index = 0; index < result.arena.healthPickupCount; ++index) {
      result.arena.healthPickups[index] = healthPickups[index];
    }
    result.arena.mcguffin = mcguffin;
    for (std::size_t index = 0;
         index < spawnTeams.size() && index < result.arena.spawnTeams.size();
         ++index) {
      result.arena.spawnTeams[index] = spawnTeams[index];
    }
    result.arena.teamSpawnCount = teamSpawns.size();
    for (std::size_t index = 0; index < teamSpawns.size(); ++index) {
      result.arena.teamSpawns[index] = teamSpawns[index];
    }
  }
  return result;
}

ArenaLoadResult loadArenaFromMapText(std::string_view text) {
  MapParseResult parsed = parseMapDocument(text);
  if (!parsed.ok) {
    return {{}, false, parsed.error};
  }
  return convertMapDocumentToArena(parsed.document);
}

} // namespace lg
