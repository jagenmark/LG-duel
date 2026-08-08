#include "render/StaticAmbientProbe.hpp"

#include "sim/Combat.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>

namespace lg {
namespace {

constexpr std::array<Vec3, 8> kSurfaceRayOffsets = {{
    {0.62F, 0.0F, 0.0F},
    {-0.62F, 0.0F, 0.0F},
    {0.0F, 0.62F, 0.0F},
    {0.0F, -0.62F, 0.0F},
    {0.48F, 0.48F, 0.0F},
    {-0.48F, -0.48F, 0.0F},
    {-0.48F, 0.48F, 0.0F},
    {0.48F, -0.48F, 0.0F},
}};

constexpr std::array<Vec3, 8> kProbeRayDirections = {{
    {0.62F, 0.0F, 0.78F},
    {-0.62F, 0.0F, 0.78F},
    {0.0F, 0.62F, 0.78F},
    {0.0F, -0.62F, 0.78F},
    {0.50F, 0.50F, 0.707F},
    {-0.50F, -0.50F, 0.707F},
    {-0.50F, 0.50F, 0.707F},
    {0.50F, -0.50F, 0.707F},
}};

[[nodiscard]] std::uint64_t hashMix(std::uint64_t hash, std::uint64_t value) {
  hash ^= value;
  hash *= 1099511628211ULL;
  return hash;
}

[[nodiscard]] std::uint32_t floatBits(float value) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

[[nodiscard]] Vec3 cross(Vec3 first, Vec3 second) {
  return {
      first.y * second.z - first.z * second.y,
      first.z * second.x - first.x * second.z,
      first.x * second.y - first.y * second.x,
  };
}

[[nodiscard]] std::uint64_t sampleKey(Vec3 position, Vec3 normal) {
  const auto quantize = [](float value, float scale) {
    return static_cast<std::int32_t>(std::lround(value * scale));
  };
  std::uint64_t hash = 1469598103934665603ULL;
  hash =
      hashMix(hash, static_cast<std::uint32_t>(quantize(position.x, 256.0F)));
  hash =
      hashMix(hash, static_cast<std::uint32_t>(quantize(position.y, 256.0F)));
  hash =
      hashMix(hash, static_cast<std::uint32_t>(quantize(position.z, 256.0F)));
  hash = hashMix(hash, static_cast<std::uint32_t>(quantize(normal.x, 1024.0F)));
  hash = hashMix(hash, static_cast<std::uint32_t>(quantize(normal.y, 1024.0F)));
  hash = hashMix(hash, static_cast<std::uint32_t>(quantize(normal.z, 1024.0F)));
  return hash;
}

[[nodiscard]] float rayVisibility(const Arena &arena, Vec3 origin,
                                  Vec3 direction, float maxDistance) {
  const WorldTrace trace =
      traceStaticAmbientWorld(arena, origin, normalize(direction), maxDistance);
  if (!trace.hit) {
    return 1.0F;
  }
  const float distanceFraction =
      std::clamp(trace.distance / std::max(maxDistance, 0.001F), 0.0F, 1.0F);
  return 0.18F + 0.82F * std::sqrt(distanceFraction);
}

[[nodiscard]] std::uint8_t visibilityByte(float visibility,
                                          float minimumVisibility) {
  return static_cast<std::uint8_t>(
      std::lround(std::clamp(visibility, minimumVisibility, 1.0F) * 255.0F));
}

[[nodiscard]] std::size_t gridIndex(const StaticAmbientProbeGrid &grid,
                                    std::uint32_t x, std::uint32_t y,
                                    std::uint32_t z) {
  return (static_cast<std::size_t>(z) * grid.depth + y) * grid.width + x;
}

[[nodiscard]] float gridCoordinate(float value, float minimum, float maximum,
                                   std::uint8_t count) {
  if (count <= 1U || maximum <= minimum + 0.0001F) {
    return 0.0F;
  }
  return std::clamp((value - minimum) / (maximum - minimum), 0.0F, 1.0F) *
         static_cast<float>(count - 1U);
}

struct ProbeBounds {
  Vec3 minimum = {};
  Vec3 maximum = {};
};

[[nodiscard]] ProbeBounds
gameplayProbeBounds(const Arena &arena,
                    const StaticAmbientQualitySettings &settings) {
  Vec3 minimum = {
      std::numeric_limits<float>::max(),
      std::numeric_limits<float>::max(),
      std::numeric_limits<float>::max(),
  };
  Vec3 maximum = {
      std::numeric_limits<float>::lowest(),
      std::numeric_limits<float>::lowest(),
      std::numeric_limits<float>::lowest(),
  };
  bool found = false;
  const auto include = [&](Vec3 point) {
    minimum.x = std::min(minimum.x, point.x);
    minimum.y = std::min(minimum.y, point.y);
    minimum.z = std::min(minimum.z, point.z);
    maximum.x = std::max(maximum.x, point.x);
    maximum.y = std::max(maximum.y, point.y);
    maximum.z = std::max(maximum.z, point.z);
    found = true;
  };
  const auto includeBox = [&](Vec3 boxMinimum, Vec3 boxMaximum) {
    include(boxMinimum);
    include(boxMaximum);
  };

  for (std::size_t index = 0; index < arena.spawnCount; ++index) {
    include(arena.spawnPositions[index]);
  }
  for (std::size_t index = 0; index < arena.teamSpawnCount; ++index) {
    include(arena.teamSpawns[index].position);
  }
  for (std::size_t index = 0; index < arena.healthPickupCount; ++index) {
    include(arena.healthPickups[index].position);
  }
  for (std::size_t index = 0; index < arena.jumpPadCount; ++index) {
    includeBox(arena.jumpPads[index].min, arena.jumpPads[index].max);
    if (arena.jumpPads[index].hasTarget) {
      include(arena.jumpPads[index].targetPosition);
    }
  }
  for (std::size_t index = 0; index < arena.teleportCount; ++index) {
    includeBox(arena.teleports[index].min, arena.teleports[index].max);
    include(arena.teleports[index].destination);
  }
  if (arena.mcguffin.hasNeutralSpawn) {
    include(arena.mcguffin.neutralSpawn);
  }
  if (arena.mcguffin.hasRedBase) {
    includeBox(arena.mcguffin.redBase.min, arena.mcguffin.redBase.max);
  }
  if (arena.mcguffin.hasBlueBase) {
    includeBox(arena.mcguffin.blueBase.min, arena.mcguffin.blueBase.max);
  }

  if (!found) {
    return {arena.min, arena.max};
  }

  const float margin = std::max(settings.maxDistance * 2.0F, 4.0F);
  minimum = minimum - Vec3{margin, margin, margin};
  maximum = maximum + Vec3{margin, margin, margin};
  minimum.x = std::max(minimum.x, arena.min.x);
  minimum.y = std::max(minimum.y, arena.min.y);
  minimum.z = std::max(minimum.z, arena.min.z);
  maximum.x = std::min(maximum.x, arena.max.x);
  maximum.y = std::min(maximum.y, arena.max.y);
  maximum.z = std::min(maximum.z, arena.max.z);
  return {minimum, maximum};
}

} // namespace

StaticAmbientQualitySettings staticAmbientQualitySettings(int quality) {
  if (quality <= 0) {
    return {};
  }
  if (quality == 1) {
    return {true, 4U, 4U, 10U, 10U, 5U, 3.25F, 0.70F, 0.035F};
  }
  return {true, 6U, 6U, 13U, 13U, 7U, 5.0F, 0.55F, 0.03F};
}

float encodedAmbientVisibilityScale(std::uint8_t visibility) {
  const float linear = static_cast<float>(visibility) / 255.0F;
  return std::pow(linear, 1.0F / 2.2F);
}

std::uint64_t visualAmbientOccluderFingerprint(const Arena &arena) {
  std::uint64_t hash = 1469598103934665603ULL;
  hash = hashMix(hash, arena.visualWallCount);
  hash = hashMix(hash, arena.visualBrushCount);
  for (std::size_t index = 0; index < arena.visualWallCount; ++index) {
    const ArenaWall &wall = arena.visualWalls[index];
    for (const ArenaSurfaceKind surfaceKind : wall.faceSurfaceKinds) {
      hash = hashMix(hash, static_cast<std::uint8_t>(surfaceKind));
    }
  }
  for (std::size_t index = 0; index < arena.visualBrushCount; ++index) {
    const ArenaBrush &brush = arena.visualBrushes[index];
    hash = hashMix(hash, brush.faceCount);
    for (std::uint8_t faceIndex = 0; faceIndex < brush.faceCount; ++faceIndex) {
      const ArenaBrushFace &face = brush.faces[faceIndex];
      hash = hashMix(hash, static_cast<std::uint8_t>(face.surfaceKind));
      hash = hashMix(hash, floatBits(face.normal.x));
      hash = hashMix(hash, floatBits(face.normal.y));
      hash = hashMix(hash, floatBits(face.normal.z));
      hash = hashMix(hash, floatBits(face.distance));
    }
  }
  return hash;
}

std::uint64_t ambientProbeInputFingerprint(const Arena &arena) {
  std::uint64_t hash = 1469598103934665603ULL;
  const auto hashVec3 = [&](Vec3 value) {
    hash = hashMix(hash, floatBits(value.x));
    hash = hashMix(hash, floatBits(value.y));
    hash = hashMix(hash, floatBits(value.z));
  };
  hash = hashMix(hash, static_cast<std::uint8_t>(arena.skyId));
  hash = hashMix(hash, arena.spawnCount);
  for (std::size_t index = 0; index < arena.spawnCount; ++index) {
    hashVec3(arena.spawnPositions[index]);
  }
  hash = hashMix(hash, arena.teamSpawnCount);
  for (std::size_t index = 0; index < arena.teamSpawnCount; ++index) {
    hashVec3(arena.teamSpawns[index].position);
  }
  hash = hashMix(hash, arena.healthPickupCount);
  for (std::size_t index = 0; index < arena.healthPickupCount; ++index) {
    hashVec3(arena.healthPickups[index].position);
  }
  hash = hashMix(hash, arena.jumpPadCount);
  for (std::size_t index = 0; index < arena.jumpPadCount; ++index) {
    const ArenaJumpPad &jumpPad = arena.jumpPads[index];
    hashVec3(jumpPad.min);
    hashVec3(jumpPad.max);
    hash = hashMix(hash, jumpPad.hasTarget ? 1U : 0U);
    if (jumpPad.hasTarget) {
      hashVec3(jumpPad.targetPosition);
    }
  }
  hash = hashMix(hash, arena.teleportCount);
  for (std::size_t index = 0; index < arena.teleportCount; ++index) {
    hashVec3(arena.teleports[index].min);
    hashVec3(arena.teleports[index].max);
    hashVec3(arena.teleports[index].destination);
  }
  hash = hashMix(hash, arena.mcguffin.hasNeutralSpawn ? 1U : 0U);
  if (arena.mcguffin.hasNeutralSpawn) {
    hashVec3(arena.mcguffin.neutralSpawn);
  }
  hash = hashMix(hash, arena.mcguffin.hasRedBase ? 1U : 0U);
  if (arena.mcguffin.hasRedBase) {
    hashVec3(arena.mcguffin.redBase.min);
    hashVec3(arena.mcguffin.redBase.max);
  }
  hash = hashMix(hash, arena.mcguffin.hasBlueBase ? 1U : 0U);
  if (arena.mcguffin.hasBlueBase) {
    hashVec3(arena.mcguffin.blueBase.min);
    hashVec3(arena.mcguffin.blueBase.max);
  }
  return hash;
}

StaticAmbientBaker::StaticAmbientBaker(const Arena &arena, int quality)
    : arena_(&arena), settings_(staticAmbientQualitySettings(quality)) {
  if (!settings_.enabled) {
    stats_.maximumVisibility = 255U;
  }
  cache_.reserve(512U);
}

std::uint8_t StaticAmbientBaker::sample(Vec3 position, Vec3 normal) {
  if (!settings_.enabled || arena_ == nullptr) {
    return 255U;
  }
  normal = normalize(normal);
  if (length(normal) <= 0.0001F) {
    return 255U;
  }
  const std::uint64_t key = sampleKey(position, normal);
  if (const auto found = cache_.find(key); found != cache_.end()) {
    ++stats_.cacheHits;
    return found->second;
  }

  float total = 0.0F;
  const Vec3 origin = position + normal * settings_.surfaceBias;
  const Vec3 tangent =
      normalize(std::abs(normal.z) < 0.95F ? cross({0.0F, 0.0F, 1.0F}, normal)
                                           : cross({1.0F, 0.0F, 0.0F}, normal));
  const Vec3 bitangent = normalize(cross(normal, tangent));
  for (std::uint8_t index = 0U; index < settings_.surfaceRayCount; ++index) {
    const Vec3 offset = kSurfaceRayOffsets[index];
    const Vec3 direction =
        normalize(normal + tangent * offset.x + bitangent * offset.y);
    total += rayVisibility(*arena_, origin, direction, settings_.maxDistance);
  }
  stats_.raysCast += settings_.surfaceRayCount;
  ++stats_.uniqueSamples;
  const std::uint8_t result =
      visibilityByte(total / static_cast<float>(settings_.surfaceRayCount),
                     settings_.minimumVisibility);
  stats_.minimumVisibility = std::min(stats_.minimumVisibility, result);
  stats_.maximumVisibility = std::max(stats_.maximumVisibility, result);
  cache_.emplace(key, result);
  return result;
}

const StaticAmbientBakeStats &StaticAmbientBaker::stats() const {
  return stats_;
}

bool StaticAmbientProbeGrid::enabled() const {
  return width > 0U && depth > 0U && height > 0U && !visibility.empty();
}

std::uint32_t StaticAmbientProbeGrid::byteSize() const {
  return static_cast<std::uint32_t>(visibility.size());
}

StaticAmbientProbeGrid bakeStaticAmbientProbeGrid(const Arena &arena,
                                                  int quality) {
  const StaticAmbientQualitySettings settings =
      staticAmbientQualitySettings(quality);
  StaticAmbientProbeGrid grid;
  if (!settings.enabled) {
    return grid;
  }
  const auto start = std::chrono::steady_clock::now();
  const ProbeBounds bounds = gameplayProbeBounds(arena, settings);
  grid.minimum = bounds.minimum;
  grid.maximum = bounds.maximum;
  grid.width = settings.gridWidth;
  grid.depth = settings.gridDepth;
  grid.height = settings.gridHeight;
  grid.visibility.resize(
      static_cast<std::size_t>(grid.width) * grid.depth * grid.height, 255U);
  grid.minimumVisibility = 255U;
  grid.maximumVisibility = 0U;

  for (std::uint32_t z = 0; z < grid.height; ++z) {
    for (std::uint32_t y = 0; y < grid.depth; ++y) {
      for (std::uint32_t x = 0; x < grid.width; ++x) {
        const auto axisPosition = [](float minimum, float maximum,
                                     std::uint32_t index, std::uint8_t count) {
          if (count <= 1U) {
            return (minimum + maximum) * 0.5F;
          }
          const float amount =
              static_cast<float>(index) / static_cast<float>(count - 1U);
          return minimum + (maximum - minimum) * amount;
        };
        const Vec3 position = {
            axisPosition(grid.minimum.x, grid.maximum.x, x, grid.width),
            axisPosition(grid.minimum.y, grid.maximum.y, y, grid.depth),
            axisPosition(grid.minimum.z, grid.maximum.z, z, grid.height),
        };
        float total = 0.0F;
        for (std::uint8_t ray = 0U; ray < settings.probeRayCount; ++ray) {
          total += rayVisibility(arena, position, kProbeRayDirections[ray],
                                 settings.maxDistance);
        }
        grid.raysCast += settings.probeRayCount;
        const std::uint8_t value =
            visibilityByte(total / static_cast<float>(settings.probeRayCount),
                           settings.minimumVisibility);
        grid.visibility[gridIndex(grid, x, y, z)] = value;
        grid.minimumVisibility = std::min(grid.minimumVisibility, value);
        grid.maximumVisibility = std::max(grid.maximumVisibility, value);
      }
    }
  }

  std::uint64_t hash = 1469598103934665603ULL;
  hash = hashMix(hash, floatBits(grid.minimum.x));
  hash = hashMix(hash, floatBits(grid.minimum.y));
  hash = hashMix(hash, floatBits(grid.minimum.z));
  hash = hashMix(hash, floatBits(grid.maximum.x));
  hash = hashMix(hash, floatBits(grid.maximum.y));
  hash = hashMix(hash, floatBits(grid.maximum.z));
  hash = hashMix(hash, grid.width);
  hash = hashMix(hash, grid.depth);
  hash = hashMix(hash, grid.height);
  for (const std::uint8_t value : grid.visibility) {
    hash = hashMix(hash, value);
  }
  grid.fingerprint = hash;
  grid.buildMilliseconds = std::chrono::duration<float, std::milli>(
                               std::chrono::steady_clock::now() - start)
                               .count();
  return grid;
}

std::uint8_t sampleStaticAmbientProbe(const StaticAmbientProbeGrid &grid,
                                      Vec3 position) {
  if (!grid.enabled()) {
    return 255U;
  }
  const float x =
      gridCoordinate(position.x, grid.minimum.x, grid.maximum.x, grid.width);
  const float y =
      gridCoordinate(position.y, grid.minimum.y, grid.maximum.y, grid.depth);
  const float z =
      gridCoordinate(position.z, grid.minimum.z, grid.maximum.z, grid.height);
  const auto lower = [](float value) {
    return static_cast<std::uint32_t>(std::floor(value));
  };
  const std::uint32_t x0 = lower(x);
  const std::uint32_t y0 = lower(y);
  const std::uint32_t z0 = lower(z);
  const std::uint32_t x1 = std::min<std::uint32_t>(x0 + 1U, grid.width - 1U);
  const std::uint32_t y1 = std::min<std::uint32_t>(y0 + 1U, grid.depth - 1U);
  const std::uint32_t z1 = std::min<std::uint32_t>(z0 + 1U, grid.height - 1U);
  const float tx = x - static_cast<float>(x0);
  const float ty = y - static_cast<float>(y0);
  const float tz = z - static_cast<float>(z0);
  const auto value = [&](std::uint32_t gx, std::uint32_t gy, std::uint32_t gz) {
    return static_cast<float>(grid.visibility[gridIndex(grid, gx, gy, gz)]);
  };
  const auto mix = [](float first, float second, float amount) {
    return first + (second - first) * amount;
  };
  const float lowerNear = mix(value(x0, y0, z0), value(x1, y0, z0), tx);
  const float lowerFar = mix(value(x0, y1, z0), value(x1, y1, z0), tx);
  const float upperNear = mix(value(x0, y0, z1), value(x1, y0, z1), tx);
  const float upperFar = mix(value(x0, y1, z1), value(x1, y1, z1), tx);
  const float lowerValue = mix(lowerNear, lowerFar, ty);
  const float upperValue = mix(upperNear, upperFar, ty);
  return static_cast<std::uint8_t>(
      std::lround(std::clamp(mix(lowerValue, upperValue, tz), 0.0F, 255.0F)));
}

} // namespace lg
