#include "sim/Movement.hpp"

#include "shared/Math.hpp"

#include <algorithm>
#include <cmath>

namespace lg {
namespace {

[[nodiscard]] Vec3 horizontal(Vec3 value) {
  return {value.x, value.y, 0.0F};
}

[[nodiscard]] Vec3 projectAlongPlane(Vec3 value, Vec3 normal) {
  return value - (normal * dot(value, normal));
}

[[nodiscard]] Vec3 clipToGroundPlanePreserveSpeed(Vec3 velocity, Vec3 groundNormal) {
  constexpr float kMinimumSpeed = 0.0001F;
  const float speed = length(velocity);
  if (speed <= kMinimumSpeed) {
    return {};
  }

  const Vec3 clipped = projectAlongPlane(velocity, groundNormal);
  const float clippedSpeed = length(clipped);
  if (clippedSpeed <= kMinimumSpeed) {
    return {};
  }

  return normalize(clipped) * speed;
}

struct GroundContact {
  Vec3 normal = {0.0F, 0.0F, 1.0F};
  bool onGround = false;
  bool groundPlane = false;
};

struct StepMoveResult {
  CollisionResult collision = {};
  bool valid = false;
};

[[nodiscard]] StepMoveResult attemptStepMove(
  const Arena& arena,
  const PlayerState& player,
  Vec3 startVelocity,
  float fixedDt,
  const CollisionResult&
) {
  constexpr float kCollisionEpsilon = 0.0001F;
  PlayerState stepPlayer = player;
  stepPlayer.onGround = true;

  const CollisionResult upMove = slidePlayerArenaMove(
    arena,
    stepPlayer,
    player.position,
    {0.0F, 0.0F, kPlayerStepHeight},
    1.0F
  );
  const float stepSize = upMove.position.z - player.position.z;
  if (stepSize <= kCollisionEpsilon || playerPositionSolid(arena, stepPlayer, upMove.position)) {
    return {};
  }

  PlayerState raisedStepPlayer = stepPlayer;
  raisedStepPlayer.position = upMove.position;
  const CollisionResult stepSlide = slidePlayerArenaMove(
    arena,
    raisedStepPlayer,
    upMove.position,
    startVelocity,
    fixedDt
  );
  if (playerPositionSolid(arena, stepPlayer, stepSlide.position)) {
    return {};
  }

  raisedStepPlayer.position = stepSlide.position;
  const CollisionResult droppedMove = slidePlayerArenaMove(
    arena,
    raisedStepPlayer,
    stepSlide.position,
    {0.0F, 0.0F, -stepSize},
    1.0F
  );
  if (playerPositionSolid(arena, stepPlayer, droppedMove.position)) {
    return {};
  }

  CollisionResult result = droppedMove;
  result.velocity = stepSlide.velocity;
  if (!player.onGround && startVelocity.z > 0.0F) {
    result.velocity.x = startVelocity.x;
    result.velocity.y = startVelocity.y;
  }
  result.groundNormal = droppedMove.groundNormal;
  result.groundPlane = droppedMove.groundPlane;
  if (startVelocity.z <= 0.0F && droppedMove.onGround) {
    result.velocity = clipToGroundPlanePreserveSpeed(result.velocity, droppedMove.groundNormal);
  }
  result.onGround = startVelocity.z <= 0.0F && droppedMove.onGround;
  result.blocked = true;
  return {result, true};
}

[[nodiscard]] CollisionResult categorizeGroundAfterMove(
  const Arena& arena,
  const PlayerState& player,
  CollisionResult collision
) {
  constexpr float kGroundTraceDistance = 0.25F / 40.0F;
  constexpr float kGroundFollowDistance = 0.08F;
  constexpr float kCollisionEpsilon = 0.0001F;
  if (collision.onGround || collision.groundPlane) {
    return collision;
  }
  if (
    collision.velocity.z > 0.0F &&
    (!player.onGround || player.knockbackTicksRemaining > 0)
  ) {
    return collision;
  }

  const float groundTraceDistance =
    player.onGround ? kGroundFollowDistance : kGroundTraceDistance;
  PlayerState probePlayer = player;
  probePlayer.position = collision.position;
  const CollisionResult groundProbe = slidePlayerArenaMove(
    arena,
    probePlayer,
    collision.position,
    {0.0F, 0.0F, -groundTraceDistance},
    1.0F
  );
  if (!groundProbe.groundPlane) {
    const float feetZ = collision.position.z - player.bounds.halfHeight;
    bool standingOnLowStep = false;
    for (std::size_t index = 0; index < arena.wallCount; ++index) {
      const ArenaWall& wall = arena.walls[index];
      if (
        collision.position.x < wall.min.x - player.bounds.radius ||
        collision.position.x > wall.max.x + player.bounds.radius ||
        collision.position.y < wall.min.y - player.bounds.radius ||
        collision.position.y > wall.max.y + player.bounds.radius
      ) {
        continue;
      }
      if (
        std::fabs(wall.max.z - feetZ) <= groundTraceDistance + kCollisionEpsilon
      ) {
        standingOnLowStep = true;
        break;
      }
    }
    if (!standingOnLowStep) {
      return collision;
    }

    collision.velocity.z = 0.0F;
    collision.onGround = true;
    collision.groundPlane = true;
    collision.groundNormal = {0.0F, 0.0F, 1.0F};
    return collision;
  }

  collision.position = groundProbe.position;
  collision.groundPlane = true;
  collision.groundNormal = groundProbe.groundNormal;
  collision.onGround = groundProbe.onGround;
  if (collision.onGround) {
    collision.velocity = clipToGroundPlanePreserveSpeed(
      collision.velocity,
      collision.groundNormal
    );
  }
  return collision;
}

[[nodiscard]] CollisionResult resolveMovementWithStep(
  const Arena& arena,
  const PlayerState& player,
  Vec3,
  Vec3 requestedVelocity,
  float fixedDt,
  bool allowStepMove
) {
  CollisionResult normalMove =
    slidePlayerArenaMove(arena, player, player.position, requestedVelocity, fixedDt);
  if (allowStepMove) {
    normalMove = categorizeGroundAfterMove(arena, player, normalMove);
  }
  if (!allowStepMove || !normalMove.blocked) {
    return normalMove;
  }

  const StepMoveResult steppedMove = attemptStepMove(
    arena,
    player,
    requestedVelocity,
    fixedDt,
    normalMove
  );
  if (!steppedMove.valid) {
    return normalMove;
  }
  return categorizeGroundAfterMove(arena, player, steppedMove.collision);
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
  const float speed = length(velocity);
  if (speed <= 0.0001F) {
    velocity = {};
    return;
  }

  const float control = std::max(speed, tuning.stopSpeed);
  const float drop = control * tuning.groundFriction * fixedDt;
  const float newSpeed = std::max(0.0F, speed - drop);
  const float scale = newSpeed / speed;
  velocity *= scale;
}

[[nodiscard]] Vec3 movementWishDirection(const UserCommand& command) {
  const Vec3 forward = yawForward(command.viewYawRadians);
  const Vec3 right = yawRight(command.viewYawRadians);
  return normalize((forward * command.forwardMove) + (right * command.rightMove));
}

[[nodiscard]] Vec3 movementWishDirectionGrounded(
  const UserCommand& command,
  Vec3 groundNormal
) {
  constexpr float kInputEpsilon = 0.0001F;
  Vec3 forward = yawForward(command.viewYawRadians);
  Vec3 right = yawRight(command.viewYawRadians);
  forward.z = 0.0F;
  right.z = 0.0F;

  if (
    std::fabs(command.forwardMove) <= kInputEpsilon &&
    std::fabs(command.rightMove) > kInputEpsilon
  ) {
    Vec3 slopeAxis = horizontal(groundNormal);
    if (length(slopeAxis) > kInputEpsilon) {
      slopeAxis = normalize(slopeAxis);
      Vec3 contourWish = right * command.rightMove;
      contourWish -= slopeAxis * dot(contourWish, slopeAxis);
      if (length(contourWish) > kInputEpsilon) {
        return normalize(contourWish);
      }
    }
  }

  forward = normalize(projectAlongPlane(forward, groundNormal));
  right = normalize(projectAlongPlane(right, groundNormal));
  return normalize((forward * command.forwardMove) + (right * command.rightMove));
}

[[nodiscard]] GroundContact traceGround(
  const Arena& arena,
  const PlayerState& player
) {
  constexpr float kGroundTraceDistance = 0.25F / 40.0F;
  constexpr float kGroundFollowDistance = 0.08F;
  if (!player.onGround && player.velocity.z > 0.0F) {
    return {};
  }

  const float traceDistance = player.onGround ? kGroundFollowDistance : kGroundTraceDistance;
  const CollisionResult trace = slidePlayerArenaMove(
    arena,
    player,
    player.position,
    {0.0F, 0.0F, -traceDistance},
    1.0F
  );

  if (!trace.groundPlane) {
    return {};
  }

  return {trace.groundNormal, trace.onGround, true};
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

  const bool wasOnGround = player.onGround;
  const GroundContact groundContact = traceGround(arena, player);
  player.onGround = groundContact.onGround;
  player.movementMode = player.onGround ? MovementMode::Grounded : MovementMode::Airborne;

  const bool knockbackActive = player.knockbackTicksRemaining > 0;
  const bool useAirMovement = !player.onGround || knockbackActive;
  const bool jumpStarted =
    (player.onGround || wasOnGround) && command.jump && !player.jumpHeld;
  if (jumpStarted) {
    player.velocity.z = tuning.jumpImpulse;
    player.jumpHeld = true;
    player.onGround = false;
    player.movementMode = MovementMode::Airborne;
  } else if (player.onGround) {
    player.movementMode = MovementMode::Grounded;
    if (!knockbackActive) {
      player.velocity = clipToGroundPlanePreserveSpeed(
        player.velocity,
        groundContact.normal
      );
      applyGroundFriction(player.velocity, tuning, fixedDt);
    }
  } else {
    player.movementMode = MovementMode::Airborne;
  }

  const Vec3 wishDirection = useAirMovement
    ? movementWishDirection(command)
    : movementWishDirectionGrounded(command, groundContact.normal);
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

  if (!useAirMovement && !jumpStarted) {
    player.velocity = clipToGroundPlanePreserveSpeed(
      player.velocity,
      groundContact.normal
    );
  }

  if (useAirMovement || jumpStarted) {
    player.velocity.z -= tuning.gravity * fixedDt;
  }

  const CollisionResult collision = resolveMovementWithStep(
    arena,
    player,
    player.position + (player.velocity * fixedDt),
    player.velocity,
    fixedDt,
    true
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

  const CollisionResult collision = slidePlayerArenaMove(
    arena,
    player,
    player.position,
    player.velocity,
    fixedDt
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
