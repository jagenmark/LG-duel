#pragma once

#include "shared/Math.hpp"
#include "sim/PlayerState.hpp"

namespace lg {

struct Arena {
  Vec3 min = {-12.0F, -12.0F, 0.0F};
  Vec3 max = {12.0F, 12.0F, 8.0F};
};

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
