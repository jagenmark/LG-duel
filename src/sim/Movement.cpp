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

void applyAirControl(
  Vec3& velocity,
  const UserCommand& command,
  Vec3 wishDirection,
  float fixedDt
) {
  if (command.forwardMove <= 0.0F || length(wishDirection) <= 0.0F) {
    return;
  }

  const float verticalSpeed = velocity.z;
  Vec3 planarVelocity = horizontal(velocity);
  const float speed = length(planarVelocity);
  if (speed <= 0.0001F) {
    return;
  }

  planarVelocity = planarVelocity / speed;
  const float alignment = dot(planarVelocity, wishDirection);
  if (alignment <= 0.0F) {
    return;
  }

  constexpr float kQuakeWorldAirControl = 32.0F;
  const float control =
    kQuakeWorldAirControl * alignment * alignment * fixedDt;
  planarVelocity = normalize((planarVelocity * speed) + (wishDirection * control));
  planarVelocity *= speed;
  velocity.x = planarVelocity.x;
  velocity.y = planarVelocity.y;
  velocity.z = verticalSpeed;
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

void updateCrouchState(PlayerState& player, const UserCommand& command, const MovementTuning& tuning, float fixedDt) {
  const float targetAmount = command.crouch ? 1.0F : 0.0F;
  const float maxStep = std::max(0.0F, tuning.crouchTransitionSpeed) * fixedDt;
  if (player.crouchAmount < targetAmount) {
    player.crouchAmount = std::min(targetAmount, player.crouchAmount + maxStep);
  } else {
    player.crouchAmount = std::max(targetAmount, player.crouchAmount - maxStep);
  }
  player.crouched = player.crouchAmount > 0.001F;
  player.bounds = player.standingBounds;
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
  updateCrouchState(player, command, tuning, fixedDt);
  if (!command.jump) {
    player.jumpHeld = false;
  }

  const bool jumpStarted =
    player.onGround && command.jump && !command.crouch && !player.jumpHeld;
  if (jumpStarted) {
    player.velocity.z = tuning.jumpImpulse;
    player.jumpHeld = true;
    player.onGround = false;
    player.movementMode = MovementMode::Airborne;
  } else if (player.onGround) {
    player.movementMode = MovementMode::Grounded;
    applyGroundFriction(player.velocity, tuning, fixedDt);
  } else {
    player.movementMode = MovementMode::Airborne;
  }

  const Vec3 wishDirection = movementWishDirection(command);
  if (length(wishDirection) > 0.0F) {
    const bool grounded = player.movementMode == MovementMode::Grounded;
    const float groundSpeed = tuning.maxGroundSpeed *
      (player.crouched ? std::clamp(tuning.crouchSpeedScale, 0.0F, 1.0F) : 1.0F);
    accelerate(
      player.velocity,
      wishDirection,
      grounded ? groundSpeed : tuning.maxAirSpeed,
      grounded ? tuning.groundAcceleration : tuning.airAcceleration,
      fixedDt
    );
    if (!grounded && tuning.airControlEnabled) {
      applyAirControl(player.velocity, command, wishDirection, fixedDt);
    }
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
  player.crouched = false;
  player.crouchAmount = 0.0F;
  player.bounds = player.standingBounds;
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
