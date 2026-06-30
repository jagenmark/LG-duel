#include "sim/Arena.hpp"

#include "map/MapToArena.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace lg {
namespace {

constexpr std::size_t kMaxSpawnCount = kMaxPlayers;
constexpr std::size_t kMinimumDuelSpawnCount = 2;
constexpr float kMaxCoordinateMagnitude = 1000.0F;

struct ParsedSpawn {
  Vec3 position = {};
};

struct ParsedArena {
  Arena arena = {};
  std::size_t spawnCount = 0;
};

[[nodiscard]] std::string trimComment(std::string line) {
  const std::size_t comment = line.find('#');
  if (comment != std::string::npos) {
    line.erase(comment);
  }
  return line;
}

[[nodiscard]] bool parseFloat(std::string_view text, float& value) {
  const char* begin = text.data();
  const char* end = begin + text.size();
  const auto result = std::from_chars(begin, end, value);
  return result.ec == std::errc{} && result.ptr == end && std::isfinite(value);
}

[[nodiscard]] bool parseVec3(std::string_view text, Vec3& value) {
  const std::size_t firstComma = text.find(',');
  const std::size_t secondComma =
    firstComma == std::string_view::npos
      ? std::string_view::npos
      : text.find(',', firstComma + 1);
  if (
    firstComma == std::string_view::npos ||
    secondComma == std::string_view::npos ||
    text.find(',', secondComma + 1) != std::string_view::npos
  ) {
    return false;
  }

  return parseFloat(text.substr(0, firstComma), value.x) &&
    parseFloat(
      text.substr(firstComma + 1, secondComma - firstComma - 1),
      value.y
    ) &&
    parseFloat(text.substr(secondComma + 1), value.z);
}

[[nodiscard]] bool parseNamedVec3(
  const std::string& token,
  std::string_view name,
  Vec3& value
) {
  const std::string prefix = std::string(name) + "=";
  if (token.rfind(prefix, 0) != 0) {
    return false;
  }
  return parseVec3(std::string_view(token).substr(prefix.size()), value);
}

[[nodiscard]] bool lessThanAll(Vec3 minimum, Vec3 maximum) {
  return minimum.x < maximum.x &&
    minimum.y < maximum.y &&
    minimum.z < maximum.z;
}

[[nodiscard]] bool finiteAndReasonable(Vec3 value) {
  return std::isfinite(value.x) &&
    std::isfinite(value.y) &&
    std::isfinite(value.z) &&
    std::fabs(value.x) <= kMaxCoordinateMagnitude &&
    std::fabs(value.y) <= kMaxCoordinateMagnitude &&
    std::fabs(value.z) <= kMaxCoordinateMagnitude;
}

[[nodiscard]] bool contains(Vec3 minimum, Vec3 maximum, Vec3 point) {
  return point.x >= minimum.x &&
    point.x <= maximum.x &&
    point.y >= minimum.y &&
    point.y <= maximum.y &&
    point.z >= minimum.z &&
    point.z <= maximum.z;
}

[[nodiscard]] bool containsBox(
  Vec3 boundsMin,
  Vec3 boundsMax,
  Vec3 boxMin,
  Vec3 boxMax
) {
  return contains(boundsMin, boundsMax, boxMin) &&
    contains(boundsMin, boundsMax, boxMax);
}

[[nodiscard]] bool containsBrush(Vec3 boundsMin, Vec3 boundsMax, const ArenaBrush& brush) {
  for (std::uint8_t index = 0; index < brush.vertexCount; ++index) {
    if (!contains(boundsMin, boundsMax, brush.vertices[index])) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::string lineError(int lineNumber, const std::string& message) {
  return "line " + std::to_string(lineNumber) + ": " + message;
}

[[nodiscard]] ArenaLoadResult validateArena(const ParsedArena& parsed) {
  const Arena& arena = parsed.arena;
  if (!lessThanAll(arena.min, arena.max)) {
    return {{}, false, "bounds min must be lower than bounds max on every axis"};
  }
  if (!finiteAndReasonable(arena.min) || !finiteAndReasonable(arena.max)) {
    return {{}, false, "bounds coordinates must be finite and within +/-1000"};
  }
  if (arena.wallCount == 0 && arena.brushCount == 0) {
    return {{}, false, "map must define at least one solid"};
  }
  if (parsed.spawnCount < kMinimumDuelSpawnCount) {
    return {{}, false, "map must define at least two spawn points"};
  }

  for (std::size_t index = 0; index < arena.wallCount; ++index) {
    const ArenaWall& wall = arena.walls[index];
    if (!lessThanAll(wall.min, wall.max)) {
      return {{}, false, "box " + std::to_string(index) + " has inverted bounds"};
    }
    if (!finiteAndReasonable(wall.min) || !finiteAndReasonable(wall.max)) {
      return {{}, false, "box " + std::to_string(index) + " coordinates are out of range"};
    }
    if (!containsBox(arena.min, arena.max, wall.min, wall.max)) {
      return {{}, false, "box " + std::to_string(index) + " is outside arena bounds"};
    }
  }
  for (std::size_t index = 0; index < arena.brushCount; ++index) {
    const ArenaBrush& brush = arena.brushes[index];
    if (
      brush.faceCount < 4 ||
      brush.vertexCount < 4 ||
      brush.faceCount > ArenaBrush::kMaxFaces ||
      brush.vertexCount > ArenaBrush::kMaxVertices
    ) {
      return {{}, false, "brush " + std::to_string(index) + " has invalid geometry"};
    }
    if (!finiteAndReasonable(brush.min) || !finiteAndReasonable(brush.max)) {
      return {{}, false, "brush " + std::to_string(index) + " coordinates are out of range"};
    }
    if (!containsBrush(arena.min, arena.max, brush)) {
      return {{}, false, "brush " + std::to_string(index) + " is outside arena bounds"};
    }
  }

  for (std::size_t index = 0; index < parsed.spawnCount; ++index) {
    if (!finiteAndReasonable(arena.spawnPositions[index])) {
      return {{}, false, "spawn " + std::to_string(index) + " coordinates are out of range"};
    }
    if (!contains(arena.min, arena.max, arena.spawnPositions[index])) {
      return {{}, false, "spawn " + std::to_string(index) + " is outside arena bounds"};
    }
  }

  return {arena, true, {}};
}

} // namespace

std::uint32_t arenaMaterialId(std::string_view material) {
  std::uint32_t hash = 2166136261U;
  for (char character : material) {
    char normalized = character;
    if (normalized == '\\') {
      normalized = '/';
    }
    if (normalized >= 'A' && normalized <= 'Z') {
      normalized = static_cast<char>(normalized - 'A' + 'a');
    }
    hash ^= static_cast<unsigned char>(normalized);
    hash *= 16777619U;
  }
  return hash == 0U ? 1U : hash;
}

ArenaLoadResult loadArenaFromText(std::string_view text) {
  ParsedArena parsed;
  parsed.arena.walls = {};
  parsed.arena.wallCount = 0;
  parsed.arena.brushes = {};
  parsed.arena.brushCount = 0;
  parsed.arena.staticLights = {};
  parsed.arena.staticLightCount = 0;
  parsed.arena.spawnPositions = {};

  bool hasVersion = false;
  bool hasBounds = false;
  std::istringstream input{std::string(text)};
  std::string line;
  int lineNumber = 0;
  while (std::getline(input, line)) {
    ++lineNumber;
    line = trimComment(std::move(line));

    std::istringstream lineStream(line);
    std::vector<std::string> tokens;
    std::string token;
    while (lineStream >> token) {
      tokens.push_back(std::move(token));
    }
    if (tokens.empty()) {
      continue;
    }

    if (tokens[0] == "version") {
      int version = 0;
      if (tokens.size() != 2) {
        return {{}, false, lineError(lineNumber, "version expects one integer")};
      }
      const auto result =
        std::from_chars(tokens[1].data(), tokens[1].data() + tokens[1].size(), version);
      if (result.ec != std::errc{} || result.ptr != tokens[1].data() + tokens[1].size()) {
        return {{}, false, lineError(lineNumber, "version must be an integer")};
      }
      if (version != 1) {
        return {{}, false, lineError(lineNumber, "only map version 1 is supported")};
      }
      hasVersion = true;
    } else if (tokens[0] == "bounds") {
      if (tokens.size() != 3) {
        return {{}, false, lineError(lineNumber, "bounds expects min= and max= vectors")};
      }
      if (
        !parseNamedVec3(tokens[1], "min", parsed.arena.min) ||
        !parseNamedVec3(tokens[2], "max", parsed.arena.max)
      ) {
        return {{}, false, lineError(lineNumber, "bounds vectors must be min=x,y,z max=x,y,z")};
      }
      hasBounds = true;
    } else if (tokens[0] == "box") {
      if (tokens.size() != 4) {
        return {{}, false, lineError(lineNumber, "box expects id min max")};
      }
      if (parsed.arena.wallCount >= Arena::kWallCount) {
        return {{}, false, lineError(lineNumber, "too many boxes")};
      }
      ArenaWall wall;
      if (!parseVec3(tokens[2], wall.min) || !parseVec3(tokens[3], wall.max)) {
        return {{}, false, lineError(lineNumber, "box vectors must be x,y,z")};
      }
      parsed.arena.walls[parsed.arena.wallCount++] = wall;
    } else if (tokens[0] == "spawn") {
      if (tokens.size() < 3 || tokens.size() > 4) {
        return {{}, false, lineError(lineNumber, "spawn expects id position [yaw=degrees]")};
      }
      if (parsed.spawnCount >= kMaxSpawnCount) {
        return {{}, false, lineError(lineNumber, "too many spawn points")};
      }
      Vec3 position;
      if (!parseVec3(tokens[2], position)) {
        return {{}, false, lineError(lineNumber, "spawn position must be x,y,z")};
      }
      if (tokens.size() == 4 && tokens[3].rfind("yaw=", 0) != 0) {
        return {{}, false, lineError(lineNumber, "spawn option must be yaw=degrees")};
      }
      parsed.arena.spawnPositions[parsed.spawnCount++] = position;
    } else {
      return {{}, false, lineError(lineNumber, "unknown directive '" + tokens[0] + "'")};
    }
  }

  if (!hasVersion) {
    return {{}, false, "map is missing version"};
  }
  if (!hasBounds) {
    return {{}, false, "map is missing bounds"};
  }
  return validateArena(parsed);
}

ArenaLoadResult loadArenaFromFile(const std::string& path) {
  std::ifstream file(path);
  if (!file) {
    return {{}, false, "could not open map file '" + path + "'"};
  }

  std::ostringstream text;
  text << file.rdbuf();
  const std::filesystem::path mapPath(path);
  const std::string extension = mapPath.extension().string();
  ArenaLoadResult result = extension == ".map"
    ? loadArenaFromMapText(text.str())
    : loadArenaFromText(text.str());
  if (!result.ok) {
    result.error = path + ": " + result.error;
  }
  return result;
}

} // namespace lg
