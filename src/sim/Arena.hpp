#pragma once

#include "shared/Constants.hpp"
#include "shared/Math.hpp"
#include "sim/PlayerState.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace lg {

struct ArenaWall {
  Vec3 min = {};
  Vec3 max = {};
  std::uint32_t materialId = 0;
  std::array<std::uint32_t, 6> faceMaterialIds = {};
};

struct Arena {
  static constexpr std::size_t kWallCount = 24;

  Vec3 min = {-12.0F, -12.0F, 0.0F};
  Vec3 max = {12.0F, 12.0F, 8.0F};
  std::array<ArenaWall, kWallCount> walls = {};
  std::size_t wallCount = 0;
  std::array<Vec3, kMaxPlayers> spawnPositions = {{
    {-3.0F, 0.0F, 0.0F},
    {3.0F, 0.0F, 0.0F},
    {0.0F, 3.0F, 0.0F},
    {0.0F, -3.0F, 0.0F},
    {-6.0F, 3.0F, 0.0F},
    {6.0F, 3.0F, 0.0F},
  }};
};

struct ArenaLoadResult {
  Arena arena = {};
  bool ok = false;
  std::string error;
};

[[nodiscard]] ArenaLoadResult loadArenaFromText(std::string_view text);
[[nodiscard]] ArenaLoadResult loadArenaFromMapText(std::string_view text);
[[nodiscard]] ArenaLoadResult loadArenaFromFile(const std::string& path);
[[nodiscard]] std::uint32_t arenaMaterialId(std::string_view material);
[[nodiscard]] Arena thunderstruckArena();

struct CollisionResult {
  Vec3 position = {};
  Vec3 velocity = {};
  bool onGround = false;
};

[[nodiscard]] CollisionResult resolvePlayerArenaCollision(
  const Arena& arena,
  const PlayerState& player,
  Vec3 requestedPosition,
  Vec3 requestedVelocity
);

} // namespace lg
