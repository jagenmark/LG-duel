#include "map/MapToArena.hpp"

#include "map/MapParser.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace lg {
namespace {

constexpr float kPlaneEpsilon = 0.001F;
constexpr float kPlaneIntersectionEpsilon = 0.000001F;
constexpr float kBoundsPadding = 1.0F;
constexpr float kQuakeToLgScale = 1.0F / 40.0F;

[[nodiscard]] bool parseFloat(std::string_view text, float& value) {
  const char* begin = text.data();
  const char* end = begin + text.size();
  const auto result = std::from_chars(begin, end, value);
  return result.ec == std::errc{} && result.ptr == end && std::isfinite(value);
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

[[nodiscard]] bool parseOptionalYaw(const MapEntity& entity, std::string& error) {
  const std::string* yaw = entity.property("angle");
  if (yaw == nullptr) {
    yaw = entity.property("yaw");
  }
  if (yaw == nullptr) {
    return true;
  }
  float value = 0.0F;
  if (!parseFloat(*yaw, value)) {
    error = "line " + std::to_string(entity.line) + ": spawn yaw must be a finite float";
    return false;
  }
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
    const std::size_t faceIndex = static_cast<std::size_t>(axis) * 2U + side;
    wall.faceMaterialIds[faceIndex] = arenaMaterialId(face.material);
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
    if (arenaBrush.materialId == 0U && face.materialId != 0U) {
      arenaBrush.materialId = face.materialId;
    }
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
  std::vector<ArenaWall>& walls,
  std::vector<ArenaBrush>& brushes,
  std::string& error
) {
  for (std::size_t brushIndex = 0; brushIndex < entity.brushes.size(); ++brushIndex) {
    const MapBrush& brush = entity.brushes[brushIndex];
    ArenaWall wall;
    if (convertCuboidBrush(brush, wall, error)) {
      walls.push_back(wall);
      continue;
    }
    ArenaBrush arenaBrush;
    if (!convertConvexBrush(brush, ownerClass, brushIndex, arenaBrush, error)) {
      return false;
    }
    brushes.push_back(arenaBrush);
  }
  return true;
}

[[nodiscard]] bool isSpawnClass(std::string_view classname) {
  return classname == "info_player_duel" ||
    classname == "info_player_deathmatch" ||
    classname == "lg_spawn";
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
  std::vector<Vec3> spawns;
  bool hasBoundsMin = false;
  bool hasBoundsMax = false;
  Vec3 boundsMin = {};
  Vec3 boundsMax = {};

  for (const MapEntity& entity : document.entities) {
    const std::string* classname = entity.property("classname");
    if (classname == nullptr) {
      continue;
    }

    if (*classname == "worldspawn") {
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
      std::string error;
      if (!convertSolidBrushes(entity, *classname, walls, brushes, error)) {
        return {{}, false, error};
      }
    } else if (*classname == "func_group") {
      std::string error;
      if (!convertSolidBrushes(entity, *classname, walls, brushes, error)) {
        return {{}, false, error};
      }
    } else if (isSpawnClass(*classname)) {
      const std::string* origin = entity.property("origin");
      if (origin == nullptr) {
        return {{}, false, "line " + std::to_string(entity.line) + ": spawn entity is missing origin"};
      }
      Vec3 position;
      if (!parseSpaceVec3(*origin, position)) {
        return {{}, false, "line " + std::to_string(entity.line) + ": spawn origin must be 'x y z'"};
      }
      position = scaleQuakeUnits(position);
      std::string error;
      if (!parseOptionalYaw(entity, error)) {
        return {{}, false, error};
      }
      spawns.push_back(position);
    } else if (*classname == "trigger_teleport") {
      continue;
    }
  }

  if (hasBoundsMin != hasBoundsMax) {
    return {{}, false, "worldspawn must define both lg_bounds_min and lg_bounds_max"};
  }
  if (!hasBoundsMin) {
    bool initialized = false;
    for (const ArenaWall& wall : walls) {
      expandBounds(wall.min, boundsMin, boundsMax, initialized);
      expandBounds(wall.max, boundsMin, boundsMax, initialized);
    }
    for (const ArenaBrush& brush : brushes) {
      expandBounds(brush.min, boundsMin, boundsMax, initialized);
      expandBounds(brush.max, boundsMin, boundsMax, initialized);
    }
    for (Vec3 spawn : spawns) {
      expandBounds(spawn, boundsMin, boundsMax, initialized);
    }
    boundsMin = {boundsMin.x - kBoundsPadding, boundsMin.y - kBoundsPadding, boundsMin.z - kBoundsPadding};
    boundsMax = {boundsMax.x + kBoundsPadding, boundsMax.y + kBoundsPadding, boundsMax.z + kBoundsPadding};
  }

  std::ostringstream lgmap;
  lgmap << "version 1\n";
  lgmap << "bounds min=" << boundsMin.x << ',' << boundsMin.y << ',' << boundsMin.z
        << " max=" << boundsMax.x << ',' << boundsMax.y << ',' << boundsMax.z << '\n';
  const bool needsValidationPlaceholder = walls.empty() && !brushes.empty();
  const std::size_t emittedWallCount = needsValidationPlaceholder ? 1U : walls.size();
  for (std::size_t index = 0; index < emittedWallCount; ++index) {
    const ArenaWall placeholder = needsValidationPlaceholder
      ? ArenaWall{brushes[0].min, brushes[0].max}
      : ArenaWall{};
    const ArenaWall& wall = needsValidationPlaceholder ? placeholder : walls[index];
    lgmap << "box brush_" << index << ' '
          << wall.min.x << ',' << wall.min.y << ',' << wall.min.z << ' '
          << wall.max.x << ',' << wall.max.y << ',' << wall.max.z << '\n';
  }
  for (std::size_t index = 0; index < spawns.size(); ++index) {
    const Vec3 spawn = spawns[index];
    lgmap << "spawn spawn_" << index << ' '
          << spawn.x << ',' << spawn.y << ',' << spawn.z << " yaw=0\n";
  }
  ArenaLoadResult result = loadArenaFromText(lgmap.str());
  if (result.ok) {
    result.arena.wallCount = walls.size();
    for (std::size_t index = 0; index < result.arena.wallCount && index < walls.size(); ++index) {
      result.arena.walls[index].materialId = walls[index].materialId;
      result.arena.walls[index].faceMaterialIds = walls[index].faceMaterialIds;
    }
    result.arena.brushCount = std::min(brushes.size(), Arena::kBrushCount);
    for (std::size_t index = 0; index < result.arena.brushCount; ++index) {
      result.arena.brushes[index] = brushes[index];
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
