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

  const float drop = speed * tuning.groundFriction * fixedDt;
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

  if (player.movementMode == MovementMode::Grounded && command.jump) {
    player.velocity.z = tuning.jumpImpulse;
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

void simulateFlyingPlaceholder(
  PlayerState& player,
  const UserCommand& command,
  const Arena& arena,
  float fixedDt
) {
  player.viewYawRadians = command.viewYawRadians;
  player.viewPitchRadians = command.viewPitchRadians;

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
  switch (player.movementMode) {
  case MovementMode::Grounded:
  case MovementMode::Airborne:
    simulateGroundedOrAirborne(player, command, arena, tuning, fixedDt);
    break;
  case MovementMode::Flying:
    simulateFlyingPlaceholder(player, command, arena, fixedDt);
    break;
  }
}

} // namespace lg
