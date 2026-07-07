#pragma once

#include "shared/Math.hpp"
#include "sim/MovementModes.hpp"

#include <cstdint>

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
  float freezeLevel = 0.0F;
  CollisionBounds bounds = {};
  MovementMode movementMode = MovementMode::Airborne;
  std::uint16_t knockbackTicksRemaining = 0;
  std::uint16_t dashCooldownTicksRemaining = 0;
  std::uint16_t dashActiveTicksRemaining = 0;
  Vec3 dashDirection = {1.0F, 0.0F, 0.0F};
  // Runtime-only trigger guard; NetCodec intentionally does not serialize it.
  std::uint16_t jumpPadCooldownTicksRemaining = 0;
  bool onGround = false;
  bool jumpHeld = false;
  bool dashHeld = false;
  bool crouched = false;
  bool sneaking = false;
};

} // namespace lg
