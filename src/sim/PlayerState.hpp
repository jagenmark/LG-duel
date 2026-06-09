#pragma once

#include "shared/Math.hpp"
#include "sim/MovementModes.hpp"

namespace lg {

struct CollisionBounds {
  float radius = 0.35F;
  float halfHeight = 0.9F;
};

struct PlayerState {
  Vec3 position = {0.0F, 0.0F, 0.9F};
  Vec3 velocity = {};

  float viewYawRadians = 0.0F;
  float viewPitchRadians = 0.0F;

  int health = 100;
  CollisionBounds bounds = {};
  MovementMode movementMode = MovementMode::Airborne;
  bool onGround = false;
};

} // namespace lg
