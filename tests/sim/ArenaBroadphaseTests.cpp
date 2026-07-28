#include "sim/Arena.hpp"
#include "sim/Combat.hpp"
#include "sim/MapRegistry.hpp"

#include <bit>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

[[nodiscard]] std::uint32_t nextRandom(std::uint32_t& state) {
  state = state * 1664525U + 1013904223U;
  return state;
}

[[nodiscard]] float unitRandom(std::uint32_t& state) {
  return static_cast<float>(nextRandom(state) >> 8U) / static_cast<float>(1U << 24U);
}

[[nodiscard]] bool sameFloat(float lhs, float rhs) {
  return std::bit_cast<std::uint32_t>(lhs) == std::bit_cast<std::uint32_t>(rhs);
}

[[nodiscard]] bool sameVec(lg::Vec3 lhs, lg::Vec3 rhs) {
  return sameFloat(lhs.x, rhs.x) && sameFloat(lhs.y, rhs.y) && sameFloat(lhs.z, rhs.z);
}

[[nodiscard]] bool sameTrace(const lg::WorldTrace& lhs, const lg::WorldTrace& rhs) {
  return lhs.hit == rhs.hit && sameFloat(lhs.distance, rhs.distance) &&
    sameVec(lhs.start, rhs.start) && sameVec(lhs.end, rhs.end) &&
    sameVec(lhs.normal, rhs.normal) && lhs.materialId == rhs.materialId &&
    lhs.sourceIndex == rhs.sourceIndex && lhs.faceIndex == rhs.faceIndex &&
    lhs.source == rhs.source;
}

[[nodiscard]] bool sameCollision(const lg::CollisionResult& lhs, const lg::CollisionResult& rhs) {
  return sameVec(lhs.position, rhs.position) && sameVec(lhs.velocity, rhs.velocity) &&
    sameVec(lhs.groundNormal, rhs.groundNormal) && lhs.onGround == rhs.onGround &&
    lhs.groundPlane == rhs.groundPlane && lhs.blocked == rhs.blocked && lhs.hitFlags == rhs.hitFlags;
}

} // namespace

int main() {
  std::size_t checkedMaps = 0;
  for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator("maps")) {
    if (!entry.is_regular_file() || entry.path().extension() != ".map") continue;
    const std::string mapName = entry.path().stem().string();
    const lg::LocalMapLoadResult loaded = lg::loadLocalMap(mapName);
    if (!loaded.ok || !loaded.arena.collisionIndex) {
      std::cerr << "FAILED: indexed map load for " << mapName << '\n';
      return 1;
    }
    lg::Arena linear = loaded.arena;
    linear.collisionIndex.reset();
    std::uint32_t random = loaded.descriptor.contentHash ^ 0x7f4a7c15U;
    for (std::size_t sample = 0; sample < 1000U; ++sample) {
      const lg::Vec3 origin = {
        loaded.arena.min.x + unitRandom(random) * (loaded.arena.max.x - loaded.arena.min.x),
        loaded.arena.min.y + unitRandom(random) * (loaded.arena.max.y - loaded.arena.min.y),
        loaded.arena.min.z + unitRandom(random) * (loaded.arena.max.z - loaded.arena.min.z),
      };
      const lg::Vec3 direction = lg::normalize(lg::Vec3{
        unitRandom(random) * 2.0F - 1.0F,
        unitRandom(random) * 2.0F - 1.0F,
        unitRandom(random) * 2.0F - 1.0F,
      });
      const float distance = 0.1F + unitRandom(random) * 200.0F;
      const lg::WorldTrace indexedTrace = lg::traceWorld(loaded.arena, origin, direction, distance);
      const lg::WorldTrace linearTrace = lg::traceWorld(linear, origin, direction, distance);
      if (!sameTrace(indexedTrace, linearTrace)) {
        std::cerr << "FAILED: trace mismatch for " << mapName << " sample " << sample << '\n';
        return 2;
      }
    }
    for (std::size_t sample = 0; sample < 500U; ++sample) {
      lg::PlayerState player;
      player.position = loaded.arena.spawnPositions[sample % lg::kMaxPlayers];
      player.position.z += player.bounds.halfHeight + 0.05F;
      player.velocity = {
        unitRandom(random) * 30.0F - 15.0F,
        unitRandom(random) * 30.0F - 15.0F,
        unitRandom(random) * 20.0F - 10.0F,
      };
      player.onGround = (sample % 3U) == 0U;
      const float fixedDt = 0.001F + unitRandom(random) * 0.03F;
      const lg::CollisionResult indexedMove = lg::slidePlayerArenaMove(
        loaded.arena, player, player.position, player.velocity, fixedDt
      );
      const lg::CollisionResult linearMove = lg::slidePlayerArenaMove(
        linear, player, player.position, player.velocity, fixedDt
      );
      if (!sameCollision(indexedMove, linearMove)) {
        std::cerr << "FAILED: movement mismatch for " << mapName << " sample " << sample << '\n';
        return 3;
      }
    }
    ++checkedMaps;
  }
  if (checkedMaps == 0U) {
    std::cerr << "FAILED: no packaged maps were checked\n";
    return 4;
  }
  std::cout << "Broadphase matched linear authored-order replay across " << checkedMaps << " maps.\n";
  return 0;
}
