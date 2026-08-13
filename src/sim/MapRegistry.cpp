#include "sim/MapRegistry.hpp"
#include "sim/ArenaBroadphase.hpp"

#include <algorithm>
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

void hashTextureProjection(std::uint32_t& hash, const TextureProjection& projection) {
  hashVec3(hash, projection.uAxis);
  hashVec3(hash, projection.vAxis);
  hashFloat(hash, projection.uOffset);
  hashFloat(hash, projection.vOffset);
  hashFloat(hash, projection.rotationDegrees);
  hashFloat(hash, projection.uScale);
  hashFloat(hash, projection.vScale);
  hashU32(hash, projection.valid ? 1U : 0U);
}

[[nodiscard]] bool hasSkyHashExtension(const Arena& arena) {
  if (arena.skyId != SkyId::None) {
    return true;
  }
  const auto wallHasSky = [](const ArenaWall& wall) {
    return std::any_of(
      wall.faceSurfaceKinds.begin(),
      wall.faceSurfaceKinds.end(),
      [](ArenaSurfaceKind kind) { return kind != ArenaSurfaceKind::Default; }
    );
  };
  const auto brushHasSky = [](const ArenaBrush& brush) {
    for (std::uint8_t index = 0; index < brush.faceCount; ++index) {
      if (brush.faces[index].surfaceKind != ArenaSurfaceKind::Default) {
        return true;
      }
    }
    return false;
  };
  for (std::size_t index = 0; index < arena.wallCount; ++index) {
    if (wallHasSky(arena.walls[index])) {
      return true;
    }
  }
  for (std::size_t index = 0; index < arena.brushCount; ++index) {
    if (brushHasSky(arena.brushes[index])) {
      return true;
    }
  }
  for (std::size_t index = 0; index < arena.visualWallCount; ++index) {
    if (wallHasSky(arena.visualWalls[index])) {
      return true;
    }
  }
  for (std::size_t index = 0; index < arena.visualBrushCount; ++index) {
    if (brushHasSky(arena.visualBrushes[index])) {
      return true;
    }
  }
  return false;
}

void hashSkyExtension(std::uint32_t& hash, const Arena& arena) {
  // This suffix preserves every legacy no-sky map hash byte for byte.
  hashU32(hash, 0x534B5931U); // SKY1
  hashU32(hash, static_cast<std::uint32_t>(arena.skyId));
  const auto hashWall = [&hash](const ArenaWall& wall) {
    for (ArenaSurfaceKind kind : wall.faceSurfaceKinds) {
      hashU32(hash, static_cast<std::uint32_t>(kind));
    }
  };
  const auto hashBrush = [&hash](const ArenaBrush& brush) {
    for (std::uint8_t index = 0; index < brush.faceCount; ++index) {
      hashU32(
        hash,
        static_cast<std::uint32_t>(brush.faces[index].surfaceKind)
      );
    }
  };
  for (std::size_t index = 0; index < arena.wallCount; ++index) {
    hashWall(arena.walls[index]);
  }
  for (std::size_t index = 0; index < arena.brushCount; ++index) {
    hashBrush(arena.brushes[index]);
  }
  for (std::size_t index = 0; index < arena.visualWallCount; ++index) {
    hashWall(arena.visualWalls[index]);
  }
  for (std::size_t index = 0; index < arena.visualBrushCount; ++index) {
    hashBrush(arena.visualBrushes[index]);
  }
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
    hashU32(hash, static_cast<std::uint32_t>(wall.collisionKind));
    hashU32(hash, wall.sourceEntityIndex);
    hashU32(hash, wall.sourceBrushIndex);
    hashU32(hash, wall.renderable ? 1U : 0U);
    for (std::size_t faceIndex = 0; faceIndex < wall.faceMaterialIds.size(); ++faceIndex) {
      // The network/capture content hash must change with rendered appearance,
      // not only collision extents.
      hashU32(hash, wall.faceMaterialIds[faceIndex]);
      hashTextureProjection(hash, wall.faceTextureProjections[faceIndex]);
    }
  }
  hashU32(hash, static_cast<std::uint32_t>(arena.brushCount));
  for (std::size_t index = 0; index < arena.brushCount; ++index) {
    const ArenaBrush& brush = arena.brushes[index];
    hashVec3(hash, brush.min);
    hashVec3(hash, brush.max);
    hashU32(hash, brush.materialId);
    hashU32(hash, static_cast<std::uint32_t>(brush.collisionKind));
    hashU32(hash, brush.sourceEntityIndex);
    hashU32(hash, brush.sourceBrushIndex);
    hashU32(hash, brush.renderable ? 1U : 0U);
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
      hashTextureProjection(hash, face.textureProjection);
      hashU32(hash, face.vertexCount);
      for (std::uint8_t vertex = 0; vertex < face.vertexCount; ++vertex) {
        hashU32(hash, face.vertices[vertex]);
      }
    }
  }
  if (arena.visualWallCount > 0 || arena.visualBrushCount > 0) {
    // Preserve legacy hashes for maps without visual-only content. Once used,
    // the domain marker and both counts make this extension unambiguous.
    hashU32(hash, 0x56495331U);
    hashU32(hash, static_cast<std::uint32_t>(arena.visualWallCount));
    for (std::size_t index = 0; index < arena.visualWallCount; ++index) {
      const ArenaWall& wall = arena.visualWalls[index];
      hashVec3(hash, wall.min);
      hashVec3(hash, wall.max);
      hashU32(hash, wall.materialId);
      hashU32(hash, wall.sourcePatchIndex);
      hashU32(hash, wall.sourcePatchPieceIndex);
      for (std::size_t faceIndex = 0; faceIndex < wall.faceMaterialIds.size(); ++faceIndex) {
        hashU32(hash, wall.faceMaterialIds[faceIndex]);
        hashTextureProjection(hash, wall.faceTextureProjections[faceIndex]);
      }
    }
    hashU32(hash, static_cast<std::uint32_t>(arena.visualBrushCount));
    for (std::size_t index = 0; index < arena.visualBrushCount; ++index) {
      const ArenaBrush& brush = arena.visualBrushes[index];
      hashVec3(hash, brush.min);
      hashVec3(hash, brush.max);
      hashU32(hash, brush.materialId);
      hashU32(hash, brush.sourcePatchIndex);
      hashU32(hash, brush.sourcePatchPieceIndex);
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
        hashTextureProjection(hash, face.textureProjection);
        hashU32(hash, face.vertexCount);
        for (std::uint8_t vertex = 0; vertex < face.vertexCount; ++vertex) {
          hashU32(hash, face.vertices[vertex]);
        }
      }
    }
  }
  hashU32(hash, static_cast<std::uint32_t>(arena.spawnCount));
  for (std::size_t index = 0; index < arena.spawnCount; ++index) {
    hashVec3(hash, arena.spawnPositions[index]);
    hashU32(hash, static_cast<std::uint32_t>(arena.spawnTeams[index]));
  }
  hashU32(hash, static_cast<std::uint32_t>(arena.teamSpawnCount));
  for (std::size_t index = 0; index < arena.teamSpawnCount; ++index) {
    const ArenaTeamSpawn& spawn = arena.teamSpawns[index];
    hashVec3(hash, spawn.position);
    hashFloat(hash, spawn.yawRadians);
    hashU32(hash, static_cast<std::uint32_t>(spawn.group));
  }
  hashU32(hash, arena.mcguffin.hasNeutralSpawn ? 1U : 0U);
  hashVec3(hash, arena.mcguffin.neutralSpawn);
  hashU32(hash, arena.mcguffin.hasRedBase ? 1U : 0U);
  hashVec3(hash, arena.mcguffin.redBase.min);
  hashVec3(hash, arena.mcguffin.redBase.max);
  hashU32(hash, arena.mcguffin.hasBlueBase ? 1U : 0U);
  hashVec3(hash, arena.mcguffin.blueBase.min);
  hashVec3(hash, arena.mcguffin.blueBase.max);
  hashU32(hash, static_cast<std::uint32_t>(arena.staticLightCount));
  for (std::size_t index = 0; index < arena.staticLightCount; ++index) {
    const ArenaStaticLight& light = arena.staticLights[index];
    hashVec3(hash, light.position);
    hashVec3(hash, light.color);
    hashFloat(hash, light.intensity);
    hashFloat(hash, light.radius);
    hashFloat(hash, light.sourceRadius);
    hashU32(hash, static_cast<std::uint32_t>(static_cast<std::int32_t>(light.priority)));
    hashU32(hash, light.castsShadows ? 1U : 0U);
    hashU32(hash, light.flickerEnabled ? 1U : 0U);
    hashU32(hash, light.flickerSeed);
    hashFloat(hash, light.flickerFrequencyHz);
    hashFloat(hash, light.flickerMinFactor);
    hashFloat(hash, light.flickerMaxFactor);
  }
  hashVec3(hash, arena.ambientLight.color);
  hashFloat(hash, arena.ambientLight.intensity);
  hashU32(hash, arena.sunLight.enabled ? 1U : 0U);
  hashVec3(hash, arena.sunLight.direction);
  hashVec3(hash, arena.sunLight.color);
  hashFloat(hash, arena.sunLight.intensity);
  hashU32(hash, static_cast<std::uint32_t>(arena.jumpPadCount));
  for (std::size_t index = 0; index < arena.jumpPadCount; ++index) {
    const ArenaJumpPad& jumpPad = arena.jumpPads[index];
    hashVec3(hash, jumpPad.min);
    hashVec3(hash, jumpPad.max);
    hashVec3(hash, jumpPad.targetPosition);
    hashVec3(hash, jumpPad.launchVelocity);
    hashFloat(hash, jumpPad.targetSpeed);
    hashU32(hash, jumpPad.hasTarget ? 1U : 0U);
    hashU32(hash, jumpPad.hasTargetSpeed ? 1U : 0U);
  }
  hashU32(hash, static_cast<std::uint32_t>(arena.teleportCount));
  for (std::size_t index = 0; index < arena.teleportCount; ++index) {
    const ArenaTeleport& teleport = arena.teleports[index];
    hashVec3(hash, teleport.min);
    hashVec3(hash, teleport.max);
    hashVec3(hash, teleport.destination);
    hashVec3(hash, teleport.exitVelocity);
  }
  if (arena.killVolumeCount > 0) {
    // Keep hashes stable for old maps, then bind maps that use world kill
    // volumes to both their count and exact bounds.
    hashU32(hash, 0x4b494c31U);
    hashU32(hash, static_cast<std::uint32_t>(arena.killVolumeCount));
    for (std::size_t index = 0; index < arena.killVolumeCount; ++index) {
      const ArenaKillVolume& volume = arena.killVolumes[index];
      hashVec3(hash, volume.min);
      hashVec3(hash, volume.max);
    }
  }
  hashU32(hash, static_cast<std::uint32_t>(arena.healthPickupCount));
  for (std::size_t index = 0; index < arena.healthPickupCount; ++index) {
    const ArenaHealthPickup& pickup = arena.healthPickups[index];
    hashVec3(hash, pickup.position);
    hashU32(hash, static_cast<std::uint32_t>(pickup.type));
  }
  if (hasSkyHashExtension(arena)) {
    hashSkyExtension(hash, arena);
  }
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

  const std::filesystem::path directory = resolveMapDirectory(mapDirectory);
  std::vector<std::filesystem::path> candidates;
  std::vector<std::string> loadErrors;
  if (extension.empty()) {
    candidates.push_back(directory / (mapName + ".map"));
  } else {
    candidates.push_back(directory / mapName);
  }

  for (const std::filesystem::path& path : candidates) {
    ArenaLoadResult result;
    loadArenaFromFile(path.string(), result);
    if (result.ok) {
      buildArenaCollisionIndex(result.arena);
      return {
        result.arena,
        describeMap(canonicalName, result.arena),
        true,
        {},
      };
    }
    if (!result.error.empty()) {
      loadErrors.push_back(std::move(result.error));
    }
  }

  std::ostringstream error;
  error << "unknown local map '" << mapName << "'; tried";
  for (const std::filesystem::path& path : candidates) {
    error << " '" << path.string() << "'";
  }
  if (!loadErrors.empty()) {
    error << "; loader error";
    for (const std::string& loadError : loadErrors) {
      error << ": " << loadError;
    }
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
