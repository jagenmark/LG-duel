#include "sim/Movement.hpp"

#include "shared/Math.hpp"

#include <algorithm>
#include <cmath>

namespace lg {
namespace {

[[nodiscard]] Vec3 horizontal(Vec3 value) {
  return {value.x, value.y, 0.0F};
}

[[nodiscard]] bool playerOverlapsJumpPad(
  const PlayerState& player,
  const ArenaJumpPad& jumpPad
) {
  const float closestX = clamp(player.position.x, jumpPad.min.x, jumpPad.max.x);
  const float closestY = clamp(player.position.y, jumpPad.min.y, jumpPad.max.y);
  const float deltaX = player.position.x - closestX;
  const float deltaY = player.position.y - closestY;
  const bool overlapsPlanar =
    (deltaX * deltaX) + (deltaY * deltaY) <=
      (player.bounds.radius * player.bounds.radius);
  const float playerMinZ = player.position.z - player.bounds.halfHeight;
  const float playerMaxZ = player.position.z + player.bounds.halfHeight;
  return overlapsPlanar &&
    playerMaxZ >= jumpPad.min.z &&
    playerMinZ <= jumpPad.max.z;
}

[[nodiscard]] Vec3 ballisticLaunchVelocity(
  Vec3 origin,
  Vec3 target,
  float gravity,
  float requestedHorizontalSpeed
) {
  constexpr float kMinimumFlightTime = 0.15F;
  gravity = std::max(0.001F, gravity);

  const Vec3 delta = target - origin;
  const float horizontalDistance = std::hypot(delta.x, delta.y);
  float flightTime = 0.0F;
  if (requestedHorizontalSpeed > 0.0F) {
    flightTime = horizontalDistance / requestedHorizontalSpeed;
  } else if (delta.z > 0.0F) {
    flightTime = std::sqrt((2.0F * delta.z) / gravity);
  } else {
    flightTime = horizontalDistance / kDefaultJumpPadSpeed;
  }
  flightTime = std::max(kMinimumFlightTime, flightTime);

  return {
    delta.x / flightTime,
    delta.y / flightTime,
    (delta.z + (0.5F * gravity * flightTime * flightTime)) / flightTime,
  };
}

void applyJumpPads(
  PlayerState& player,
  const Arena& arena,
  const MovementTuning& tuning,
  std::uint16_t jumpPadCooldownDurationTicks
) {
  if (player.jumpPadCooldownTicksRemaining > 0) {
    --player.jumpPadCooldownTicksRemaining;
    return;
  }

  for (std::size_t index = 0; index < arena.jumpPadCount; ++index) {
    const ArenaJumpPad& jumpPad = arena.jumpPads[index];
    if (!playerOverlapsJumpPad(player, jumpPad)) {
      continue;
    }
    player.velocity = jumpPad.hasTarget
      ? ballisticLaunchVelocity(
          player.position,
          jumpPad.targetPosition,
          tuning.gravity,
          jumpPad.hasTargetSpeed ? jumpPad.targetSpeed : 0.0F
        )
      : jumpPad.launchVelocity;
    player.onGround = false;
    player.movementMode = MovementMode::Airborne;
    player.jumpPadCooldownTicksRemaining = jumpPadCooldownDurationTicks;
    return;
  }
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

void simulateGroundedOrAirborne(
  PlayerState& player,
  const UserCommand& command,
  const Arena& arena,
  const MovementTuning& tuning,
  float fixedDt,
  std::uint16_t jumpPadCooldownDurationTicks
) {
  player.viewYawRadians = command.viewYawRadians;
  player.viewPitchRadians = command.viewPitchRadians;
  if (!command.jump) {
    player.jumpHeld = false;
  }

  const bool knockbackActive = player.knockbackTicksRemaining > 0;
  const bool useAirMovement = !player.onGround || knockbackActive;
  const bool jumpStarted = player.onGround && command.jump && !player.jumpHeld;
  if (jumpStarted) {
    player.velocity.z = tuning.jumpImpulse;
    player.jumpHeld = true;
    player.onGround = false;
    player.movementMode = MovementMode::Airborne;
  } else if (player.onGround) {
    player.movementMode = MovementMode::Grounded;
    if (!knockbackActive) {
      applyGroundFriction(player.velocity, tuning, fixedDt);
    }
  } else {
    player.movementMode = MovementMode::Airborne;
  }

  const Vec3 wishDirection = movementWishDirection(command);
  if (length(wishDirection) > 0.0F) {
    accelerate(
      player.velocity,
      wishDirection,
      useAirMovement ? tuning.maxAirSpeed : tuning.maxGroundSpeed,
      useAirMovement ? tuning.airAcceleration : tuning.groundAcceleration,
      fixedDt
    );
    if (useAirMovement && tuning.airControlEnabled) {
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
  if (player.knockbackTicksRemaining > 0) {
    --player.knockbackTicksRemaining;
  }
  applyJumpPads(player, arena, tuning, jumpPadCooldownDurationTicks);
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
  float fixedDt,
  std::uint16_t jumpPadCooldownDurationTicks
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
  applyJumpPads(player, arena, tuning, jumpPadCooldownDurationTicks);
}

} // namespace

void simulateMovement(
  PlayerState& player,
  const UserCommand& command,
  const Arena& arena,
  const MovementTuning& tuning,
  float fixedDt
) {
  simulateMovement(
    player,
    command,
    arena,
    tuning,
    fixedDt,
    kDefaultJumpPadCooldownTicks
  );
}

void simulateMovement(
  PlayerState& player,
  const UserCommand& command,
  const Arena& arena,
  const MovementTuning& tuning,
  float fixedDt,
  std::uint16_t jumpPadCooldownDurationTicks
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
    simulateGroundedOrAirborne(
      player,
      command,
      arena,
      tuning,
      fixedDt,
      jumpPadCooldownDurationTicks
    );
    break;
  case MovementMode::Flying:
    simulateFlying(
      player,
      command,
      arena,
      tuning,
      fixedDt,
      jumpPadCooldownDurationTicks
    );
    break;
  }
}

} // namespace lg
