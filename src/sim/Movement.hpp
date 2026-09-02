#pragma once

#include "sim/Arena.hpp"
#include "sim/IcePool.hpp"
#include "sim/PlayerState.hpp"
#include "sim/UserCommand.hpp"

#include <cstdint>
#include <span>

namespace lg {

inline constexpr std::uint16_t kDefaultJumpPadCooldownTicks = 25;

struct MovementTuning {
  bool flightEnabled = false;
  float groundAcceleration = 10.0F;
  float airAcceleration = 1.0F;
  float groundFriction = 6.0F;
  float stopSpeed = 2.5F;
  float gravity = 24.0F;
  float maxGroundSpeed = 8.0F;
  float maxAirSpeed = 8.0F;
  float jumpImpulse = 8.0F;
  bool airControlEnabled = false;
  float dashTargetSpeed = 11.5F;
  float dashMaxSpeed = 12.5F;
  float dashAcceleration = 200.0F;
  float dashDuration = 0.10F;
  float dashCooldown = 0.85F;
  float dashGroundHopVelocity = 3.25F;
  float dashAirHopVelocity = 1.875F;

  float flightAcceleration = 32.0F;
  float maxFlightSpeed = 12.0F;
  float flightDamping = 2.0F;
  float flightGravityCancel = 1.0F;
};

[[nodiscard]] bool isGlideSlideActive(
  const PlayerState& player,
  const MovementTuning& tuning
);

void simulateMovement(
  PlayerState& player,
  const UserCommand& command,
  const Arena& arena,
  const MovementTuning& tuning,
  float fixedDt
);

void simulateMovement(
  PlayerState& player,
  const UserCommand& command,
  const Arena& arena,
  const MovementTuning& tuning,
  float fixedDt,
  std::uint16_t jumpPadCooldownDurationTicks
);

void simulateMovement(
  PlayerState& player,
  const UserCommand& command,
  const Arena& arena,
  const MovementTuning& tuning,
  const IcePoolArray& icePools,
  const IcePoolTuning& icePoolTuning,
  float fixedDt,
  std::uint16_t jumpPadCooldownDurationTicks,
  std::span<const PlayerCollisionProxy> playerProxies,
  std::uint8_t playerIndex
);

void simulateMovement(
  PlayerState& player,
  const UserCommand& command,
  const Arena& arena,
  const MovementTuning& tuning,
  const IcePoolArray& icePools,
  const IcePoolTuning& icePoolTuning,
  float fixedDt
);

void simulateMovement(
  PlayerState& player,
  const UserCommand& command,
  const Arena& arena,
  const MovementTuning& tuning,
  const IcePoolArray& icePools,
  const IcePoolTuning& icePoolTuning,
  float fixedDt,
  std::uint16_t jumpPadCooldownDurationTicks
);

} // namespace lg
