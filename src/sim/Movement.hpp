#pragma once

#include "sim/Arena.hpp"
#include "sim/PlayerState.hpp"
#include "sim/UserCommand.hpp"

namespace lg {

struct MovementTuning {
  bool flightEnabled = false;
  float groundAcceleration = 80.0F;
  float airAcceleration = 24.0F;
  float groundFriction = 8.0F;
  float stopSpeed = 2.5F;
  float gravity = 24.0F;
  float maxGroundSpeed = 8.0F;
  float maxAirSpeed = 8.0F;
  float jumpImpulse = 8.0F;
  bool airControlEnabled = false;

  float flightAcceleration = 32.0F;
  float maxFlightSpeed = 12.0F;
  float flightDamping = 2.0F;
  float flightGravityCancel = 1.0F;
};

void simulateMovement(
  PlayerState& player,
  const UserCommand& command,
  const Arena& arena,
  const MovementTuning& tuning,
  float fixedDt
);

} // namespace lg
