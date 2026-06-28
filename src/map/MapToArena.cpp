#include "map/MapToArena.hpp"

#include "map/MapParser.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

namespace lg {
namespace {

constexpr float kPlaneEpsilon = 0.001F;
constexpr float kBoundsPadding = 1.0F;

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
        hasBoundsMin = true;
      }
      if (const std::string* value = entity.property("lg_bounds_max")) {
        if (!parseSpaceVec3(*value, boundsMax)) {
          return {{}, false, "line " + std::to_string(entity.line) + ": lg_bounds_max must be 'x y z'"};
        }
        hasBoundsMax = true;
      }
      for (const MapBrush& brush : entity.brushes) {
        ArenaWall wall;
        std::string error;
        if (!convertCuboidBrush(brush, wall, error)) {
          return {{}, false, error};
        }
        walls.push_back(wall);
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
  for (std::size_t index = 0; index < walls.size(); ++index) {
    const ArenaWall& wall = walls[index];
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
    for (std::size_t index = 0; index < result.arena.wallCount && index < walls.size(); ++index) {
      result.arena.walls[index].materialId = walls[index].materialId;
      result.arena.walls[index].faceMaterialIds = walls[index].faceMaterialIds;
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
