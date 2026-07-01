#include "sim/MapRegistry.hpp"

#include <bit>
#include <cctype>
#include <filesystem>
#include <sstream>
#include <utility>
#include <vector>

namespace lg {
namespace {

constexpr std::size_t kMaxLocalMapNameBytes = 32;

void hashByte(std::uint32_t& hash, std::uint8_t value) {
  hash ^= value;
  hash *= 16777619U;
}

void hashU32(std::uint32_t& hash, std::uint32_t value) {
  hashByte(hash, static_cast<std::uint8_t>(value));
  hashByte(hash, static_cast<std::uint8_t>(value >> 8U));
  hashByte(hash, static_cast<std::uint8_t>(value >> 16U));
  hashByte(hash, static_cast<std::uint8_t>(value >> 24U));
}

void hashFloat(std::uint32_t& hash, float value) {
  hashU32(hash, std::bit_cast<std::uint32_t>(value));
}

void hashVec3(std::uint32_t& hash, Vec3 value) {
  hashFloat(hash, value.x);
  hashFloat(hash, value.y);
  hashFloat(hash, value.z);
}

std::filesystem::path resolveMapDirectory(const std::string& mapDirectory) {
  const std::filesystem::path requested(
    mapDirectory.empty() ? "maps" : mapDirectory
  );
  if (requested.is_absolute() || std::filesystem::exists(requested)) {
    return requested;
  }

  std::filesystem::path directory = std::filesystem::current_path();
  for (;;) {
    const std::filesystem::path candidate = directory / requested;
    if (std::filesystem::exists(candidate)) {
      return candidate;
    }
    const std::filesystem::path parent = directory.parent_path();
    if (parent.empty() || parent == directory) {
      break;
    }
    directory = parent;
  }
  return requested;
}

} // namespace

bool isValidMapName(std::string_view mapName) {
  if (mapName.empty() || mapName.size() > kMaxLocalMapNameBytes) {
    return false;
  }

  const std::filesystem::path requested(mapName);
  if (requested.has_parent_path() || requested.filename().string() != mapName) {
    return false;
  }

  const std::string extension = requested.extension().string();
  const std::string stem = extension.empty()
    ? std::string(mapName)
    : requested.stem().string();
  if (!extension.empty() && extension != ".map") {
    return false;
  }
  if (stem.empty()) {
    return false;
  }
  for (const unsigned char character : stem) {
    if (!std::isalnum(character) && character != '_' && character != '-') {
      return false;
    }
  }
  return true;
}

std::uint32_t hashArena(const Arena& arena) {
  std::uint32_t hash = 2166136261U;
  hashVec3(hash, arena.min);
  hashVec3(hash, arena.max);
  hashU32(hash, static_cast<std::uint32_t>(arena.wallCount));
  for (std::size_t index = 0; index < arena.wallCount; ++index) {
    const ArenaWall& wall = arena.walls[index];
    hashVec3(hash, wall.min);
    hashVec3(hash, wall.max);
    hashU32(hash, wall.materialId);
  }
  hashU32(hash, static_cast<std::uint32_t>(arena.brushCount));
  for (std::size_t index = 0; index < arena.brushCount; ++index) {
    const ArenaBrush& brush = arena.brushes[index];
    hashVec3(hash, brush.min);
    hashVec3(hash, brush.max);
    hashU32(hash, brush.materialId);
    hashU32(hash, brush.vertexCount);
    hashU32(hash, brush.faceCount);
    for (std::uint8_t vertex = 0; vertex < brush.vertexCount; ++vertex) {
      hashVec3(hash, brush.vertices[vertex]);
    }
    for (std::uint8_t faceIndex = 0; faceIndex < brush.faceCount; ++faceIndex) {
      const ArenaBrushFace& face = brush.faces[faceIndex];
      hashVec3(hash, face.normal);
      hashFloat(hash, face.distance);
      hashU32(hash, face.materialId);
      hashU32(hash, face.vertexCount);
      for (std::uint8_t vertex = 0; vertex < face.vertexCount; ++vertex) {
        hashU32(hash, face.vertices[vertex]);
      }
    }
  }
  for (const Vec3& spawn : arena.spawnPositions) {
    hashVec3(hash, spawn);
  }
  hashU32(hash, static_cast<std::uint32_t>(arena.staticLightCount));
  for (std::size_t index = 0; index < arena.staticLightCount; ++index) {
    const ArenaStaticLight& light = arena.staticLights[index];
    hashVec3(hash, light.position);
    hashVec3(hash, light.color);
    hashFloat(hash, light.intensity);
    hashFloat(hash, light.radius);
  }
  hashU32(hash, arena.sunLight.enabled ? 1U : 0U);
  hashVec3(hash, arena.sunLight.direction);
  hashVec3(hash, arena.sunLight.color);
  hashFloat(hash, arena.sunLight.intensity);
  return hash == 0U ? 1U : hash;
}

MapDescriptor describeMap(std::string mapName, const Arena& arena) {
  return {std::move(mapName), hashArena(arena)};
}

LocalMapLoadResult loadLocalMap(
  const std::string& mapName,
  const std::string& mapDirectory
) {
  if (!isValidMapName(mapName)) {
    return {{}, {}, false, "invalid map name '" + mapName + "'"};
  }

  const std::filesystem::path requested(mapName);
  const std::string extension = requested.extension().string();
  const std::string canonicalName = extension.empty()
    ? mapName
    : requested.stem().string();

  if (canonicalName == "thunderstruck") {
    const Arena arena = thunderstruckArena();
    return {arena, describeMap(canonicalName, arena), true, {}};
  }

  const std::filesystem::path directory = resolveMapDirectory(mapDirectory);
  std::vector<std::filesystem::path> candidates;
  if (extension.empty()) {
    candidates.push_back(directory / (mapName + ".map"));
  } else {
    candidates.push_back(directory / mapName);
  }

  for (const std::filesystem::path& path : candidates) {
    const ArenaLoadResult result = loadArenaFromFile(path.string());
    if (result.ok) {
      return {
        result.arena,
        describeMap(canonicalName, result.arena),
        true,
        {},
      };
    }
  }

  std::ostringstream error;
  error << "unknown local map '" << mapName << "'; tried";
  for (const std::filesystem::path& path : candidates) {
    error << " '" << path.string() << "'";
  }
  return {{}, {}, false, error.str()};
}

LocalMapLoadResult loadAndVerifyLocalMap(
  const MapDescriptor& descriptor,
  const std::string& mapDirectory
) {
  LocalMapLoadResult loaded = loadLocalMap(descriptor.mapName, mapDirectory);
  if (!loaded.ok) {
    return loaded;
  }
  if (loaded.descriptor.contentHash != descriptor.contentHash) {
    return {
      {},
      loaded.descriptor,
      false,
      "Map mismatch: server requires " + descriptor.mapName +
        " hash " + std::to_string(descriptor.contentHash) +
        ", local hash is " + std::to_string(loaded.descriptor.contentHash) + ".",
    };
  }
  return loaded;
}

} // namespace lg
