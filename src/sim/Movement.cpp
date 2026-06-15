#include "sim/Movement.hpp"

#include "shared/Math.hpp"

#include <algorithm>
#include <cmath>

namespace lg {
namespace {

[[nodiscard]] Vec3 horizontal(Vec3 value) {
  return {value.x, value.y, 0.0F};
}

void accelerate(Vec3& velocity, Vec3 wishDirection, float wishSpeed, float acceleration, float fixedDt) {
  const float currentSpeed = dot(velocity, wishDirection);
  const float addSpeed = wishSpeed - currentSpeed;
  if (addSpeed <= 0.0F) {
    return;
  }

  const float accelerationSpeed = std::min(acceleration * fixedDt * wishSpeed, addSpeed);
  velocity += wishDirection * accelerationSpeed;
}

void applyGroundFriction(Vec3& velocity, const MovementTuning& tuning, float fixedDt) {
  const Vec3 planarVelocity = horizontal(velocity);
  const float speed = length(planarVelocity);
  if (speed <= 0.0001F) {
    velocity.x = 0.0F;
    velocity.y = 0.0F;
    return;
  }

  const float control = std::max(speed, tuning.stopSpeed);
  const float drop = control * tuning.groundFriction * fixedDt;
  const float newSpeed = std::max(0.0F, speed - drop);
  const float scale = newSpeed / speed;
  velocity.x *= scale;
  velocity.y *= scale;
}

[[nodiscard]] Vec3 movementWishDirection(const UserCommand& command) {
  const Vec3 forward = yawForward(command.viewYawRadians);
  const Vec3 right = yawRight(command.viewYawRadians);
  return normalize((forward * command.forwardMove) + (right * command.rightMove));
}

void simulateGroundedOrAirborne(
  PlayerState& player,
  const UserCommand& command,
  const Arena& arena,
  const MovementTuning& tuning,
  float fixedDt
) {
  player.viewYawRadians = command.viewYawRadians;
  player.viewPitchRadians = command.viewPitchRadians;
  if (!command.jump) {
    player.jumpHeld = false;
  }

  if (player.onGround) {
    player.movementMode = MovementMode::Grounded;
    applyGroundFriction(player.velocity, tuning, fixedDt);
  } else {
    player.movementMode = MovementMode::Airborne;
  }

  const Vec3 wishDirection = movementWishDirection(command);
  if (length(wishDirection) > 0.0F) {
    const bool grounded = player.movementMode == MovementMode::Grounded;
    accelerate(
      player.velocity,
      wishDirection,
      grounded ? tuning.maxGroundSpeed : tuning.maxAirSpeed,
      grounded ? tuning.groundAcceleration : tuning.airAcceleration,
      fixedDt
    );
  }

  if (
    player.movementMode == MovementMode::Grounded &&
    command.jump &&
    !player.jumpHeld
  ) {
    player.velocity.z = tuning.jumpImpulse;
    player.jumpHeld = true;
    player.onGround = false;
    player.movementMode = MovementMode::Airborne;
  }

  if (player.movementMode != MovementMode::Flying) {
    player.velocity.z -= tuning.gravity * fixedDt;
  }

  const CollisionResult collision = resolvePlayerArenaCollision(
    arena,
    player,
    player.position + (player.velocity * fixedDt),
    player.velocity
  );

  player.position = collision.position;
  player.velocity = collision.velocity;
  player.onGround = collision.onGround;
  player.movementMode = player.onGround ? MovementMode::Grounded : MovementMode::Airborne;
}

void applyFlightDamping(
  Vec3& velocity,
  const MovementTuning& tuning,
  float fixedDt
) {
  const float dampingScale =
    std::max(0.0F, 1.0F - (tuning.flightDamping * fixedDt));
  velocity *= dampingScale;
}

void simulateFlying(
  PlayerState& player,
  const UserCommand& command,
  const Arena& arena,
  const MovementTuning& tuning,
  float fixedDt
) {
  player.viewYawRadians = command.viewYawRadians;
  player.viewPitchRadians = command.viewPitchRadians;
  player.jumpHeld = command.jump;
  player.onGround = false;

  applyFlightDamping(player.velocity, tuning, fixedDt);
  const Vec3 wishVelocity =
    (cameraForward(command.viewYawRadians, command.viewPitchRadians) *
      command.forwardMove) +
    (yawRight(command.viewYawRadians) * command.rightMove) +
    (Vec3{0.0F, 0.0F, 1.0F} * command.upMove);
  const Vec3 wishDirection = normalize(wishVelocity);
  if (length(wishDirection) > 0.0F) {
    accelerate(
      player.velocity,
      wishDirection,
      tuning.maxFlightSpeed,
      tuning.flightAcceleration,
      fixedDt
    );
  }
  player.velocity.z -=
    tuning.gravity *
    std::max(0.0F, 1.0F - tuning.flightGravityCancel) *
    fixedDt;

  const CollisionResult collision = resolvePlayerArenaCollision(
    arena,
    player,
    player.position + (player.velocity * fixedDt),
    player.velocity
  );

  player.position = collision.position;
  player.velocity = collision.velocity;
  player.onGround = collision.onGround;
  player.movementMode = MovementMode::Flying;
}

} // namespace

void simulateMovement(
  PlayerState& player,
  const UserCommand& command,
  const Arena& arena,
  const MovementTuning& tuning,
  float fixedDt
) {
  if (tuning.flightEnabled) {
    player.movementMode = MovementMode::Flying;
  } else if (player.movementMode == MovementMode::Flying) {
    player.movementMode = MovementMode::Airborne;
    player.onGround = false;
  }

  switch (player.movementMode) {
  case MovementMode::Grounded:
  case MovementMode::Airborne:
    simulateGroundedOrAirborne(player, command, arena, tuning, fixedDt);
    break;
  case MovementMode::Flying:
    simulateFlying(player, command, arena, tuning, fixedDt);
    break;
  }
}

} // namespace lg
