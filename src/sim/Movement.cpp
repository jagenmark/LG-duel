#include "sim/Movement.hpp"

#include "benchmark/BenchmarkTiming.hpp"

#include "shared/Math.hpp"
#include "sim/Combat.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace lg {
namespace {

constexpr float kCrouchHeightScale = 0.62F;
constexpr float kCrouchGroundSpeedScale = 0.35F;
constexpr float kSneakGroundSpeedScale = 0.52F;
constexpr float kSlideMinimumSpeedScale = 0.55F;
constexpr float kSlideFrictionScale = 0.08F;
constexpr float kSlideAccelerationScale = 0.35F;
constexpr float kSlideJumpSpeedScale = 1.16F;
constexpr float kAirSteerRadiansPerSecond = 4.5F;

[[nodiscard]] Vec3 horizontal(Vec3 value) {
  return {value.x, value.y, 0.0F};
}

[[nodiscard]] Vec3 projectAlongPlane(Vec3 value, Vec3 normal) {
  return value - (normal * dot(value, normal));
}

[[nodiscard]] Vec3 clipImpactVelocityToGroundPlane(Vec3 velocity, Vec3 groundNormal) {
  constexpr float kOverclip = 1.001F;
  constexpr float kStopEpsilon = 0.0001F;
  const float backoff = dot(velocity, groundNormal) * kOverclip;
  if (backoff >= 0.0F) {
    return velocity;
  }

  Vec3 clipped = velocity - (groundNormal * backoff);
  if (std::fabs(clipped.x) < kStopEpsilon) {
    clipped.x = 0.0F;
  }
  if (std::fabs(clipped.y) < kStopEpsilon) {
    clipped.y = 0.0F;
  }
  if (std::fabs(clipped.z) < kStopEpsilon) {
    clipped.z = 0.0F;
  }
  return clipped;
}

[[nodiscard]] Vec3 clipToGroundPlanePreserveSpeed(Vec3 velocity, Vec3 groundNormal) {
  constexpr float kMinimumSpeed = 0.0001F;
  const float speed = length(velocity);
  if (speed <= kMinimumSpeed) {
    return {};
  }

  Vec3 planeDirection = horizontal(velocity);
  if (length(planeDirection) <= kMinimumSpeed) {
    return {};
  }
  if (std::fabs(groundNormal.z) <= kMinimumSpeed) {
    const Vec3 clipped = projectAlongPlane(velocity, groundNormal);
    const float clippedSpeed = length(clipped);
    if (clippedSpeed <= kMinimumSpeed) {
      return {};
    }
    return normalize(clipped) * speed;
  }

  // Build the direction that has the same x/y intent but lies exactly on the
  // ground plane. This keeps ramp movement from bleeding speed into the normal.
  planeDirection.z =
    -((planeDirection.x * groundNormal.x) + (planeDirection.y * groundNormal.y)) /
    groundNormal.z;
  return normalize(planeDirection) * speed;
}

struct GroundContact {
  Vec3 normal = {0.0F, 0.0F, 1.0F};
  bool onGround = false;
  bool groundPlane = false;
};

struct IceContact {
  bool active = false;
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
  std::span<const PlayerCollisionProxy> playerProxies,
  std::uint8_t playerIndex,
  bool preserveGroundSpeedOnLanding,
  const CollisionResult&
) {
  constexpr float kCollisionEpsilon = 0.0001F;
  PlayerState stepPlayer = player;
  stepPlayer.onGround = true;

  // Classic step move: lift by max step height, slide horizontally from that
  // raised position, then drop back down. The result is accepted only if each
  // intermediate position is non-solid.
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
  const CollisionResult stepSlide = slidePlayerMove(
    arena,
    playerProxies,
    raisedStepPlayer,
    playerIndex,
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
    // A grounded step follows terrain without losing speed. An airborne impact
    // must discard velocity into the landing plane; otherwise the raised slide's
    // restored downward component becomes horizontal speed on touchdown.
    result.velocity = preserveGroundSpeedOnLanding
      ? clipToGroundPlanePreserveSpeed(result.velocity, droppedMove.groundNormal)
      : clipImpactVelocityToGroundPlane(result.velocity, droppedMove.groundNormal);
  }
  result.onGround = startVelocity.z <= 0.0F && droppedMove.onGround;
  result.blocked = true;
  result.hitFlags |= static_cast<std::uint8_t>(MovementHitFlags::Arena);
  return {result, true};
}

[[nodiscard]] CollisionResult categorizeGroundAfterMove(
  const Arena& arena,
  const PlayerState& player,
  CollisionResult collision,
  bool preserveGroundSpeedOnLanding
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
  // After a horizontal move, do a short downward probe so slopes and tiny drops
  // keep a grounded player attached without requiring a full falling tick.
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
    // Cuboid stair tops can be missed by the trace when the player is already
    // almost exactly on the top face; keep those contacts grounded.
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
    // The probe also categorizes true airborne landings. Only a player that
    // entered the move grounded may preserve 3D speed while following terrain.
    collision.velocity = preserveGroundSpeedOnLanding
      ? clipToGroundPlanePreserveSpeed(collision.velocity, collision.groundNormal)
      : clipImpactVelocityToGroundPlane(collision.velocity, collision.groundNormal);
  }
  return collision;
}

[[nodiscard]] CollisionResult resolveMovementWithStep(
  const Arena& arena,
  const PlayerState& player,
  Vec3,
  Vec3 requestedVelocity,
  float fixedDt,
  bool allowStepMove,
  std::span<const PlayerCollisionProxy> playerProxies,
  std::uint8_t playerIndex,
  bool preserveGroundSpeedOnLanding
) {
  CollisionResult normalMove =
    slidePlayerMove(
      arena,
      playerProxies,
      player,
      playerIndex,
      player.position,
      requestedVelocity,
      fixedDt
    );
  if (allowStepMove) {
    normalMove = categorizeGroundAfterMove(
      arena,
      player,
      normalMove,
      preserveGroundSpeedOnLanding
    );
  }
  if (
    !allowStepMove ||
    !normalMove.blocked ||
    !hasMovementHitFlag(normalMove, MovementHitFlags::Arena)
  ) {
    return normalMove;
  }

  const StepMoveResult steppedMove = attemptStepMove(
    arena,
    player,
    requestedVelocity,
    fixedDt,
    playerProxies,
    playerIndex,
    preserveGroundSpeedOnLanding,
    normalMove
  );
  if (!steppedMove.valid) {
    return normalMove;
  }
  return categorizeGroundAfterMove(
    arena,
    player,
    steppedMove.collision,
    preserveGroundSpeedOnLanding
  );
}

[[nodiscard]] bool playerOverlapsJumpPad(
  const PlayerState& player,
  const ArenaJumpPad& jumpPad
) {
  return playerTouchesTriggerVolume(
    player.bounds,
    player.position,
    jumpPad.min,
    jumpPad.max
  );
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

void applyTeleports(PlayerState& player, const Arena& arena) {
  for (std::size_t index = 0; index < arena.teleportCount; ++index) {
    const ArenaTeleport& teleport = arena.teleports[index];
    if (!playerTouchesTriggerVolume(
          player.bounds,
          player.position,
          teleport.min,
          teleport.max
        )) {
      continue;
    }
    // Trigger volumes are evaluated after collision movement on both the
    // authoritative server and predicted client. Preserve that shared order so
    // crossing the boundary resolves on the same fixed tick.
    player.position = teleport.destination;
    player.velocity = teleport.exitVelocity;
    player.onGround = false;
    player.movementMode = MovementMode::Airborne;
    return;
  }
}

[[nodiscard]] float standingHalfHeight(const PlayerState& player) {
  return player.crouched
    ? player.bounds.halfHeight / kCrouchHeightScale
    : player.bounds.halfHeight;
}

void applyCrouchState(
  PlayerState& player,
  const UserCommand& command,
  const Arena& arena,
  std::span<const PlayerCollisionProxy> playerProxies
) {
  const float targetStandingHalfHeight = standingHalfHeight(player);
  const float targetHalfHeight = command.crouch
    ? targetStandingHalfHeight * kCrouchHeightScale
    : targetStandingHalfHeight;
  if (std::fabs(player.bounds.halfHeight - targetHalfHeight) <= 0.0001F) {
    player.crouched = command.crouch;
    player.sneaking = command.sneak;
    return;
  }

  const float feetZ = player.position.z - player.bounds.halfHeight;
  PlayerState resizedPlayer = player;
  resizedPlayer.bounds.halfHeight = targetHalfHeight;
  resizedPlayer.position.z = feetZ + targetHalfHeight;
  resizedPlayer.crouched = command.crouch;
  resizedPlayer.sneaking = command.sneak;
  bool blockedByPlayer = false;
  if (!command.crouch) {
    for (const PlayerCollisionProxy& proxy : playerProxies) {
      blockedByPlayer = blockedByPlayer ||
        playerPositionOverlapsProxy(resizedPlayer, resizedPlayer.position, proxy);
    }
  }
  if (
    !command.crouch &&
    (playerPositionSolid(arena, resizedPlayer, resizedPlayer.position) || blockedByPlayer)
  ) {
    player.crouched = true;
    player.sneaking = command.sneak;
    return;
  }
  player.bounds.halfHeight = resizedPlayer.bounds.halfHeight;
  player.position.z = resizedPlayer.position.z;
  player.crouched = resizedPlayer.crouched;
  player.sneaking = resizedPlayer.sneaking;
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

[[nodiscard]] std::uint16_t secondsToTicks(float seconds, float fixedDt) {
  if (seconds <= 0.0F || fixedDt <= 0.0F) {
    return 0;
  }
  return static_cast<std::uint16_t>(std::clamp(
    static_cast<int>(std::ceil(seconds / fixedDt)),
    0,
    static_cast<int>(std::numeric_limits<std::uint16_t>::max())
  ));
}

[[nodiscard]] Vec3 dashWishDirection(const UserCommand& command) {
  Vec3 local = {
    command.rightMove,
    command.forwardMove,
    0.0F,
  };
  if (length(local) <= 0.001F) {
    local = {0.0F, 1.0F, 0.0F};
  }
  local = normalize(local);

  const Vec3 world =
    (yawForward(command.viewYawRadians) * local.y) +
    (yawRight(command.viewYawRadians) * local.x);
  return normalize(horizontal(world));
}

void startDash(
  PlayerState& player,
  const UserCommand& command,
  const MovementTuning& tuning,
  float fixedDt
) {
  player.dashDirection = dashWishDirection(command);
  if (length(player.dashDirection) <= 0.001F) {
    player.dashDirection = yawForward(command.viewYawRadians);
  }
  player.dashActiveTicksRemaining = secondsToTicks(tuning.dashDuration, fixedDt);
  player.dashCooldownTicksRemaining = secondsToTicks(tuning.dashCooldown, fixedDt);

  // Dash hop uses max instead of addition so repeated redirects cannot stack
  // into a vertical movement exploit.
  if (player.onGround) {
    player.velocity.z = std::max(player.velocity.z, tuning.dashGroundHopVelocity);
    player.onGround = false;
    player.movementMode = MovementMode::Airborne;
  } else {
    player.velocity.z = std::max(player.velocity.z, tuning.dashAirHopVelocity);
  }
}

void updateDashState(
  PlayerState& player,
  const UserCommand& command,
  const MovementTuning& tuning,
  float fixedDt
) {
  const bool dashPressed = command.dash && !player.dashHeld;
  if (dashPressed && player.dashCooldownTicksRemaining == 0) {
    startDash(player, command, tuning, fixedDt);
  }
  player.dashHeld = command.dash;
}

void applyDashAcceleration(PlayerState& player, const MovementTuning& tuning, float fixedDt) {
  if (player.dashActiveTicksRemaining == 0) {
    return;
  }

  Vec3 velocity = horizontal(player.velocity);
  Vec3 direction = horizontal(player.dashDirection);
  if (length(direction) <= 0.001F) {
    --player.dashActiveTicksRemaining;
    return;
  }
  direction = normalize(direction);

  const float oldSpeed = length(velocity);
  const float currentSpeedInDashDir = dot(velocity, direction);
  const float speedToAdd = tuning.dashTargetSpeed - currentSpeedInDashDir;
  if (speedToAdd > 0.0F) {
    const float addSpeed = std::min(speedToAdd, tuning.dashAcceleration * fixedDt);
    velocity += direction * addSpeed;
  }

  // Cap only speed created by dash. Skilled speed above the dash cap is kept,
  // so using dash as a redirect never punishes existing movement.
  const float newSpeed = length(velocity);
  const float cap = std::max(tuning.dashMaxSpeed, oldSpeed);
  if (newSpeed > cap && newSpeed > 0.0F) {
    velocity *= cap / newSpeed;
  }

  player.velocity.x = velocity.x;
  player.velocity.y = velocity.y;
  --player.dashActiveTicksRemaining;
}

void decrementDashCooldown(PlayerState& player) {
  if (player.dashCooldownTicksRemaining > 0) {
    --player.dashCooldownTicksRemaining;
  }
}

void applyClassicAirControl(
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

void applyGlideAirControl(
  Vec3& velocity,
  Vec3 wishDirection,
  float fixedDt
) {
  wishDirection = horizontal(wishDirection);
  if (length(wishDirection) <= 0.0001F) {
    return;
  }

  const float verticalSpeed = velocity.z;
  Vec3 planarVelocity = horizontal(velocity);
  const float speed = length(planarVelocity);
  if (speed <= 0.0001F) {
    return;
  }

  wishDirection = normalize(wishDirection);
  const float currentYaw = std::atan2(planarVelocity.y, planarVelocity.x);
  const float wishYaw = std::atan2(wishDirection.y, wishDirection.x);
  const float yawDelta = std::atan2(
    std::sin(wishYaw - currentYaw),
    std::cos(wishYaw - currentYaw)
  );
  const float maxTurn = kAirSteerRadiansPerSecond * fixedDt;
  const float steeredYaw = currentYaw +
    std::clamp(yawDelta, -maxTurn, maxTurn);
  planarVelocity = {
    std::cos(steeredYaw) * speed,
    std::sin(steeredYaw) * speed,
    0.0F,
  };
  velocity.x = planarVelocity.x;
  velocity.y = planarVelocity.y;
  velocity.z = verticalSpeed;
}

void applyGroundFriction(Vec3& velocity, const MovementTuning& tuning, float fixedDt) {
  const float speed = length(horizontal(velocity));
  if (speed <= 0.0001F) {
    velocity.x = 0.0F;
    velocity.y = 0.0F;
    return;
  }

  const float control = std::max(speed, tuning.stopSpeed);
  // Quake-style stop speed keeps braking decisive below stopSpeed instead of
  // letting friction fade proportionally as horizontal speed approaches zero.
  const float drop = control * tuning.groundFriction * fixedDt;
  const float newSpeed = std::max(0.0F, speed - drop);
  const float scale = newSpeed / speed;
  velocity *= scale;
}

[[nodiscard]] IceContact findIceContact(
  const PlayerState& player,
  const GroundContact& groundContact,
  const IcePoolArray& icePools
) {
  if (!groundContact.onGround) {
    return {};
  }

  const Vec3 footPoint =
    player.position - Vec3{0.0F, 0.0F, player.bounds.halfHeight};
  for (const IcePool& pool : icePools) {
    if (!pool.active || pool.radius <= 0.0F || pool.lifetimeSeconds <= 0.0F) {
      continue;
    }
    // Require the pool and contacted floor to describe nearly the same plane;
    // this prevents nearby ice on a wall or adjoining slope from taking effect.
    if (dot(pool.normal, groundContact.normal) < 0.95F) {
      continue;
    }
    const Vec3 delta = footPoint - pool.center;
    const float planeDistance = dot(delta, pool.normal);
    // Allow modest map/projection error normal to the plane, then test the
    // actual footprint in-plane and expand it by the player's cylinder radius.
    if (std::fabs(planeDistance) > 0.5F) {
      continue;
    }
    const Vec3 tangentDelta = delta - (pool.normal * planeDistance);
    if (length(tangentDelta) <= pool.radius + player.bounds.radius) {
      return {true};
    }
  }
  return {};
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
  constexpr float kPlaneEpsilon = 0.0001F;
  const Vec3 forward = yawForward(command.viewYawRadians);
  const Vec3 right = yawRight(command.viewYawRadians);
  Vec3 wishDirection =
    (forward * command.forwardMove) + (right * command.rightMove);
  wishDirection.z = 0.0F;
  if (length(wishDirection) <= kPlaneEpsilon) {
    return {};
  }
  if (std::fabs(groundNormal.z) <= kPlaneEpsilon) {
    return normalize(wishDirection);
  }

  // Project input onto the current ground plane. On ramps this turns pure
  // forward/right input into movement along the ramp surface instead of into it.
  wishDirection.z =
    -((wishDirection.x * groundNormal.x) + (wishDirection.y * groundNormal.y)) /
    groundNormal.z;
  return normalize(wishDirection);
}

[[nodiscard]] GroundContact traceGround(
  const Arena& arena,
  const PlayerState& player
) {
  constexpr float kGroundTraceDistance = 0.25F / 40.0F;
  constexpr float kGroundFollowDistance = 0.08F;
  constexpr float kGroundKickoffSpeed = 10.0F / 40.0F;

  const float traceDistance = player.onGround ? kGroundFollowDistance : kGroundTraceDistance;
  // Ground tracing is deliberately short: long traces would snap players down
  // ledges, while this only preserves contact with ramps, stairs, and tiny gaps.
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
  if (
    (!player.onGround || player.knockbackTicksRemaining > 0) &&
    player.velocity.z > 0.0F &&
    dot(player.velocity, trace.groundNormal) > kGroundKickoffSpeed
  ) {
    // A jump or knockback moving away from the plane should not be re-grounded
    // by the same short trace that keeps walking players attached to ramps.
    return {};
  }

  return {trace.groundNormal, trace.onGround, true};
}

void simulateGroundedOrAirborne(
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
) {
  player.viewYawRadians = command.viewYawRadians;
  player.viewPitchRadians = command.viewPitchRadians;
  const bool wasSlideActive = isGlideSlideActive(player, tuning);
  applyCrouchState(player, command, arena, playerProxies);
  if (!command.jump) {
    player.jumpHeld = false;
  }

  const bool wasOnGround = player.onGround;
  const GroundContact groundContact = traceGround(arena, player);
  player.onGround = groundContact.onGround;
  player.movementMode = player.onGround ? MovementMode::Grounded : MovementMode::Airborne;
  updateDashState(player, command, tuning, fixedDt);
  const IceContact iceContact = findIceContact(player, groundContact, icePools);
  MovementTuning localTuning = tuning;
  if (iceContact.active) {
    localTuning.groundFriction = icePoolTuning.friction;
    localTuning.groundAcceleration *= std::clamp(icePoolTuning.controlScale, 0.0F, 1.0F);
  }

  const bool slideActive = isGlideSlideActive(player, tuning);
  if (slideActive) {
    localTuning.groundFriction *= kSlideFrictionScale;
    localTuning.groundAcceleration *= kSlideAccelerationScale;
  }

  const bool knockbackActive = player.knockbackTicksRemaining > 0;
  // Knockback uses air acceleration and skips ground friction while retaining
  // physical ground contact for collision, landing, and snapshot semantics.
  const bool useAirMovement = !player.onGround || knockbackActive;
  // The previous contact also authorizes this edge-triggered jump so a tiny
  // seam or ledge miss in the fresh ground trace does not eat a valid input.
  const bool jumpStarted =
    (player.onGround || wasOnGround) && command.jump && !player.jumpHeld;
  if (jumpStarted) {
    if (wasSlideActive) {
      Vec3 planarVelocity = horizontal(player.velocity);
      const float planarSpeed = length(planarVelocity);
      const float slideJumpSpeed =
        tuning.maxGroundSpeed * kSlideJumpSpeedScale;
      if (planarSpeed > 0.0001F && planarSpeed < slideJumpSpeed) {
        planarVelocity *= slideJumpSpeed / planarSpeed;
        player.velocity.x = planarVelocity.x;
        player.velocity.y = planarVelocity.y;
      }
    }
    player.velocity.z = tuning.jumpImpulse;
    player.jumpHeld = true;
    player.onGround = false;
    player.movementMode = MovementMode::Airborne;
  } else if (player.onGround) {
    player.movementMode = MovementMode::Grounded;
    if (!wasOnGround) {
      // A short pre-move trace can acquire ground for an airborne player. Treat
      // that as an impact even though player.onGround is now true for this tick.
      player.velocity = clipImpactVelocityToGroundPlane(
        player.velocity,
        groundContact.normal
      );
    } else if (!knockbackActive) {
      player.velocity = clipToGroundPlanePreserveSpeed(player.velocity, groundContact.normal);
    }
    if (!knockbackActive) {
      applyGroundFriction(player.velocity, localTuning, fixedDt);
      if (iceContact.active && groundContact.normal.z < 0.999F) {
        // Ice on ramps uses grounded gravity along the floor plane. This makes
        // icy ramps a terrain hazard while preserving normal non-icy ramp feel.
        const Vec3 gravity = {0.0F, 0.0F, -localTuning.gravity};
        const Vec3 slopeGravity = projectAlongPlane(gravity, groundContact.normal);
        player.velocity +=
          slopeGravity * std::max(0.0F, icePoolTuning.slopeGravityScale) * fixedDt;
      }
    }
  } else {
    player.movementMode = MovementMode::Airborne;
  }

  // This choice intentionally uses the pre-jump contact state: a jump that
  // starts this tick receives its grounded acceleration before becoming airborne.
  const Vec3 wishDirection = useAirMovement
    ? movementWishDirection(command)
    : movementWishDirectionGrounded(command, groundContact.normal);
  if (length(wishDirection) > 0.0F) {
    float maxGroundSpeed = tuning.maxGroundSpeed;
    if (player.crouched) {
      maxGroundSpeed *= kCrouchGroundSpeedScale;
    } else if (player.sneaking) {
      maxGroundSpeed *= kSneakGroundSpeedScale;
    }
    accelerate(
      player.velocity,
      wishDirection,
      useAirMovement ? tuning.maxAirSpeed : maxGroundSpeed,
      useAirMovement ? tuning.airAcceleration : localTuning.groundAcceleration,
      fixedDt
    );
    if (useAirMovement && tuning.airControlEnabled) {
      if (tuning.glideMovementEnabled) {
        applyGlideAirControl(player.velocity, wishDirection, fixedDt);
      } else {
        applyClassicAirControl(player.velocity, command, wishDirection, fixedDt);
      }
    }
  }
  applyDashAcceleration(player, tuning, fixedDt);

  if (!useAirMovement && !jumpStarted) {
    // Acceleration can reintroduce a component into the floor normal, so clip a
    // second time to leave the final grounded velocity exactly tangent to it.
    player.velocity = clipToGroundPlanePreserveSpeed(
      player.velocity,
      groundContact.normal
    );
  }

  if (useAirMovement || jumpStarted) {
    player.velocity.z -= tuning.gravity * fixedDt;
  }

  CollisionResult collision = resolveMovementWithStep(
    arena,
    player,
    player.position + (player.velocity * fixedDt),
    player.velocity,
    fixedDt,
    true,
    playerProxies,
    playerIndex,
    wasOnGround
  );
  if (
    !useAirMovement &&
    !jumpStarted &&
    !collision.onGround &&
    groundContact.groundPlane &&
    groundContact.onGround &&
    collision.velocity.z > 0.0F &&
    length(horizontal(groundContact.normal)) > 0.0001F &&
    dot(collision.velocity, groundContact.normal) <= (10.0F / 40.0F)
  ) {
    // Ramp seams can produce a tiny upward component even when the player is
    // still moving along the same ground plane. Preserve the previous ground
    // contact instead of briefly popping airborne.
    collision.groundPlane = true;
    collision.onGround = true;
    collision.groundNormal = groundContact.normal;
    collision.velocity = clipToGroundPlanePreserveSpeed(
      collision.velocity,
      groundContact.normal
    );
  }
  player.position = collision.position;
  player.velocity = collision.velocity;
  player.onGround = collision.onGround;
  player.movementMode = player.onGround ? MovementMode::Grounded : MovementMode::Airborne;
  // Timers expire after the tick they affect; changing this order shifts
  // authoritative and predicted movement behavior by one simulation tick.
  if (player.knockbackTicksRemaining > 0) {
    --player.knockbackTicksRemaining;
  }
  decrementDashCooldown(player);
  applyJumpPads(player, arena, tuning, jumpPadCooldownDurationTicks);
  applyTeleports(player, arena);
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
  std::uint16_t jumpPadCooldownDurationTicks,
  std::span<const PlayerCollisionProxy> playerProxies,
  std::uint8_t playerIndex
) {
  player.viewYawRadians = command.viewYawRadians;
  player.viewPitchRadians = command.viewPitchRadians;
  player.crouched = false;
  player.sneaking = false;
  player.jumpHeld = command.jump;
  player.onGround = false;
  updateDashState(player, command, tuning, fixedDt);

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
  applyDashAcceleration(player, tuning, fixedDt);
  player.velocity.z -=
    tuning.gravity *
    std::max(0.0F, 1.0F - tuning.flightGravityCancel) *
    fixedDt;

  const CollisionResult collision = slidePlayerMove(
    arena,
    playerProxies,
    player,
    playerIndex,
    player.position,
    player.velocity,
    fixedDt
  );

  player.position = collision.position;
  player.velocity = collision.velocity;
  player.onGround = collision.onGround;
  player.movementMode = MovementMode::Flying;
  decrementDashCooldown(player);
  applyJumpPads(player, arena, tuning, jumpPadCooldownDurationTicks);
  applyTeleports(player, arena);
}

} // namespace

MovementTuning movementTuningWithGlideProfile(MovementTuning tuning) {
  if (!tuning.glideMovementEnabled) {
    return tuning;
  }

  tuning.groundAcceleration = 7.5F;
  tuning.airAcceleration = 3.0F;
  tuning.airControlEnabled = true;
  tuning.groundFriction = 1.35F;
  tuning.stopSpeed = 1.0F;
  tuning.maxGroundSpeed = 10.5F;
  tuning.maxAirSpeed = 10.5F;
  return tuning;
}

bool isGlideSlideActive(
  const PlayerState& player,
  const MovementTuning& tuning
) {
  if (!tuning.glideMovementEnabled) {
    return false;
  }
  const MovementTuning effectiveTuning =
    movementTuningWithGlideProfile(tuning);
  const float planarSpeed = std::hypot(player.velocity.x, player.velocity.y);
  return player.onGround &&
    player.crouched &&
    planarSpeed >= effectiveTuning.maxGroundSpeed * kSlideMinimumSpeedScale;
}

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
    IcePoolArray{},
    IcePoolTuning{},
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
  simulateMovement(
    player,
    command,
    arena,
    tuning,
    IcePoolArray{},
    IcePoolTuning{},
    fixedDt,
    jumpPadCooldownDurationTicks
  );
}

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
) {
  benchmark::ScopedTiming timing(
    benchmark::TimingSubsystem::MovementCollision
  );
  const MovementTuning effectiveTuning =
    movementTuningWithGlideProfile(tuning);
  if (effectiveTuning.flightEnabled) {
    player.movementMode = MovementMode::Flying;
  } else if (player.movementMode == MovementMode::Flying) {
    player.movementMode = MovementMode::Airborne;
    player.onGround = false;
  }

  switch (player.movementMode) {
  case MovementMode::Grounded:
  case MovementMode::Airborne:
    simulateGroundedOrAirborne(
      player, command, arena, effectiveTuning, icePools, icePoolTuning,
      fixedDt * freezeMovementScale(player), jumpPadCooldownDurationTicks,
      playerProxies, playerIndex
    );
    break;
  case MovementMode::Flying:
    simulateFlying(
      player, command, arena, effectiveTuning,
      fixedDt * freezeMovementScale(player), jumpPadCooldownDurationTicks,
      playerProxies, playerIndex
    );
    break;
  }
}

void simulateMovement(
  PlayerState& player,
  const UserCommand& command,
  const Arena& arena,
  const MovementTuning& tuning,
  const IcePoolArray& icePools,
  const IcePoolTuning& icePoolTuning,
  float fixedDt
) {
  simulateMovement(
    player,
    command,
    arena,
    tuning,
    icePools,
    icePoolTuning,
    fixedDt,
    kDefaultJumpPadCooldownTicks
  );
}

void simulateMovement(
  PlayerState& player,
  const UserCommand& command,
  const Arena& arena,
  const MovementTuning& tuning,
  const IcePoolArray& icePools,
  const IcePoolTuning& icePoolTuning,
  float fixedDt,
  std::uint16_t jumpPadCooldownDurationTicks
) {
  simulateMovement(
    player, command, arena, tuning, icePools, icePoolTuning, fixedDt,
    jumpPadCooldownDurationTicks, {}, 0
  );
}

} // namespace lg
