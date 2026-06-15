#pragma once

#include "shared/Math.hpp"
#include "sim/PlayerState.hpp"

#include <array>
#include <cstddef>

namespace lg {

struct ArenaWall {
  Vec3 min = {};
  Vec3 max = {};
};

struct Arena {
  static constexpr std::size_t kWallCount = 24;

  Vec3 min = {-12.0F, -12.0F, 0.0F};
  Vec3 max = {12.0F, 12.0F, 8.0F};
  std::array<ArenaWall, kWallCount> walls = {};
  std::size_t wallCount = 0;
  std::array<Vec3, 2> spawnPositions = {{
    {-3.0F, 0.0F, 0.0F},
    {3.0F, 0.0F, 0.0F},
  }};
};

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
