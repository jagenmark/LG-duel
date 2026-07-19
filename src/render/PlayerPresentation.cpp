#include "render/PlayerPresentation.hpp"

#include "shared/Math.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace lg {
namespace {

static_assert(std::is_trivially_copyable_v<PlayerPoseLayer>);
static_assert(std::is_trivially_copyable_v<PlayerPresentationFrame>);

constexpr float kRunCycleSeconds = 1.0F;
constexpr float kSneakCycleSeconds = 1.3333333F;
constexpr float kJumpClipSeconds = 0.5833333F;
constexpr float kFallClipSeconds = 0.8333333F;
constexpr float kLandClipSeconds = 0.4166667F;
constexpr float kStartStopClipSeconds = 0.5F;
constexpr float kIdlePoseSeconds = 0.8333333F;
constexpr float kDuckingPoseSeconds = 0.4166667F;
constexpr float kDeathClipSeconds = 1.1F;

float finiteOr(float value, float fallback) {
  return std::isfinite(value) ? value : fallback;
}

float positiveOr(float value, float fallback) {
  return std::isfinite(value) && value > 0.0F ? value : fallback;
}

float initialPhase(std::uint32_t playerIndex) {
  // Integer hashing makes phase stable across runs without coupling players.
  std::uint32_t value = playerIndex + 0x9e3779b9U;
  value ^= value >> 16U;
  value *= 0x7feb352dU;
  value ^= value >> 15U;
  value *= 0x846ca68bU;
  value ^= value >> 16U;
  return static_cast<float>(value & 0x00ffffffU) / 16777216.0F;
}

bool isGrounded(const PlayerState& player) {
  return player.onGround || player.movementMode == MovementMode::Grounded;
}

bool isLooping(PlayerLocomotionState state) {
  return state == PlayerLocomotionState::Run ||
    state == PlayerLocomotionState::Sneak ||
    state == PlayerLocomotionState::CrouchWalk;
}

float cycleDuration(PlayerLocomotionState state) {
  return state == PlayerLocomotionState::Sneak
    ? kSneakCycleSeconds
    : kRunCycleSeconds;
}

std::string_view clipName(
  PlayerLocomotionState state,
  PlayerMoveDirection direction,
  bool heavyLanding
) {
  switch (state) {
    case PlayerLocomotionState::Starting: return "START_FORWARD";
    case PlayerLocomotionState::Run:
      switch (direction) {
        case PlayerMoveDirection::Backward: return "RUN_BACK";
        // These two imported Worker clips carry reversed side names.
        case PlayerMoveDirection::Left: return "STRAFE_RIGHT";
        case PlayerMoveDirection::Right: return "STRAFE_LEFT";
        case PlayerMoveDirection::Forward:
        case PlayerMoveDirection::Stationary: return "RUN";
      }
      return "RUN";
    case PlayerLocomotionState::Stopping: return "STOP_FORWARD";
    case PlayerLocomotionState::Sneak: return "SNEAK";
    case PlayerLocomotionState::Ducking: return "DUCKING";
    case PlayerLocomotionState::CrouchWalk: return "CROUCH_WALK";
    case PlayerLocomotionState::Takeoff:
    case PlayerLocomotionState::Rise: return "JUMP";
    case PlayerLocomotionState::Fall: return "FALL";
    case PlayerLocomotionState::Landing:
      return heavyLanding ? "LAND_HEAVY" : "LAND_LIGHT";
    case PlayerLocomotionState::Death: return "Death";
    case PlayerLocomotionState::Idle: return "IDLE";
  }
  return "IDLE";
}

float clipTime(
  PlayerLocomotionState state,
  float stateTime,
  float stridePhase,
  float verticalVelocity,
  bool heavyLanding,
  const PlayerPresentationConfig& config
) {
  if (isLooping(state)) {
    return stridePhase * cycleDuration(state);
  }
  switch (state) {
    case PlayerLocomotionState::Starting: {
      const float duration = positiveOr(config.startSeconds, 0.16F);
      return std::clamp(stateTime / duration, 0.0F, 1.0F) * kStartStopClipSeconds;
    }
    case PlayerLocomotionState::Stopping: {
      const float duration = positiveOr(config.stopSeconds, 0.14F);
      return std::clamp(stateTime / duration, 0.0F, 1.0F) * kStartStopClipSeconds;
    }
    case PlayerLocomotionState::Ducking: return kDuckingPoseSeconds;
    case PlayerLocomotionState::Takeoff: {
      const float duration = positiveOr(config.takeoffSeconds, 0.10F);
      return std::clamp(stateTime / duration, 0.0F, 1.0F) * (kJumpClipSeconds * 0.45F);
    }
    case PlayerLocomotionState::Rise: {
      const float progress = std::clamp((8.0F - verticalVelocity) / 8.0F, 0.25F, 1.0F);
      return progress * kJumpClipSeconds;
    }
    case PlayerLocomotionState::Fall: {
      const float progress = std::clamp(-verticalVelocity / 12.0F, 0.0F, 1.0F);
      return progress * kFallClipSeconds;
    }
    case PlayerLocomotionState::Landing: {
      const float duration = heavyLanding
        ? positiveOr(config.heavyLandingSeconds, 0.22F)
        : positiveOr(config.landingSeconds, 0.16F);
      return std::clamp(stateTime / duration, 0.0F, 1.0F) * kLandClipSeconds;
    }
    case PlayerLocomotionState::Idle: return kIdlePoseSeconds;
    case PlayerLocomotionState::Death:
      return std::min(std::max(0.0F, stateTime), kDeathClipSeconds);
    case PlayerLocomotionState::Run:
    case PlayerLocomotionState::Sneak:
    case PlayerLocomotionState::CrouchWalk: break;
  }
  return 0.0F;
}

PlayerMoveDirection classifyDirection(
  PlayerMoveDirection previous,
  float forwardSpeed,
  float rightSpeed,
  float horizontalSpeed,
  bool moving,
  float switchMargin
) {
  if (!moving || horizontalSpeed <= std::numeric_limits<float>::epsilon()) {
    return PlayerMoveDirection::Stationary;
  }

  const float forwardAmount = std::fabs(forwardSpeed);
  const float rightAmount = std::fabs(rightSpeed);
  const float margin = std::max(0.0F, finiteOr(switchMargin, 0.12F)) * horizontalSpeed;
  const bool wasLongitudinal = previous == PlayerMoveDirection::Forward ||
    previous == PlayerMoveDirection::Backward;
  const bool wasLateral = previous == PlayerMoveDirection::Left ||
    previous == PlayerMoveDirection::Right;

  // Retain the previous axis near diagonals so tiny snapshot noise cannot flap it.
  const bool longitudinal = wasLongitudinal
    ? forwardAmount + margin >= rightAmount
    : wasLateral
      ? !(rightAmount + margin >= forwardAmount)
      : forwardAmount >= rightAmount;
  if (longitudinal) {
    return forwardSpeed >= 0.0F
      ? PlayerMoveDirection::Forward
      : PlayerMoveDirection::Backward;
  }
  return rightSpeed >= 0.0F
    ? PlayerMoveDirection::Right
    : PlayerMoveDirection::Left;
}

void transitionTo(
  PlayerPresentationState& presentation,
  PlayerLocomotionState next,
  PlayerMoveDirection nextDirection,
  bool nextLandingHeavy,
  float crossfadeSeconds
) {
  if (
    next == presentation.currentState &&
    nextDirection == presentation.stateDirection &&
    nextLandingHeavy == presentation.landingHeavy
  ) {
    return;
  }
  const bool interrupted =
    (
      presentation.previousState != presentation.currentState ||
      presentation.previousStateDirection != presentation.stateDirection ||
      presentation.previousLandingHeavy != presentation.landingHeavy
    ) &&
    presentation.blendTimeSeconds < crossfadeSeconds;
  const float blendAlpha = interrupted
    ? presentation.blendTimeSeconds / crossfadeSeconds
    : 1.0F;
  if (interrupted && blendAlpha < 0.5F) {
    // Fixed-capacity layers cannot snapshot an arbitrary blended skeleton. Carry
    // the dominant visible source so rapid changes do not pop to a faint state.
    presentation.currentState = presentation.previousState;
    presentation.stateDirection = presentation.previousStateDirection;
    presentation.landingHeavy = presentation.previousLandingHeavy;
    presentation.stateTimeSeconds = presentation.previousStateTimeSeconds;
  }
  presentation.previousState = presentation.currentState;
  presentation.previousStateDirection = presentation.stateDirection;
  presentation.previousLandingHeavy = presentation.landingHeavy;
  presentation.previousStateTimeSeconds = presentation.stateTimeSeconds;
  presentation.currentState = next;
  presentation.stateDirection = nextDirection;
  presentation.landingHeavy = nextLandingHeavy;
  presentation.stateTimeSeconds = 0.0F;
  presentation.blendTimeSeconds = 0.0F;
}

} // namespace

PlayerPresentationFrame updatePlayerPresentation(
  PlayerPresentationState& presentation,
  const PlayerState& player,
  float deltaSeconds,
  std::uint32_t playerIndex,
  const PlayerPresentationConfig& config
) {
  const float dt = std::isfinite(deltaSeconds) && deltaSeconds > 0.0F
    ? std::min(deltaSeconds, 0.25F)
    : 0.0F;
  const bool grounded = isGrounded(player);
  const float horizontalSpeed = std::hypot(player.velocity.x, player.velocity.y);
  const float safeHorizontalSpeed = finiteOr(horizontalSpeed, 0.0F);
  const float enterSpeed = std::max(0.0F, finiteOr(config.moveEnterSpeed, 0.35F));
  const float exitSpeed = std::clamp(
    finiteOr(config.moveExitSpeed, 0.20F),
    0.0F,
    enterSpeed
  );

  if (!presentation.initialized || presentation.playerIndex != playerIndex) {
    presentation = {};
    presentation.initialized = true;
    presentation.playerIndex = playerIndex;
    presentation.stridePhase = initialPhase(playerIndex);
    presentation.previousHorizontalSpeed = safeHorizontalSpeed;
    presentation.wasGrounded = grounded;
  }

  const PlayerMoveDirection priorMoveDirection = presentation.moveDirection;
  const bool wasMoving = priorMoveDirection != PlayerMoveDirection::Stationary;
  const bool moving = safeHorizontalSpeed > (wasMoving ? exitSpeed : enterSpeed);
  const float forwardSpeed = finiteOr(dot(player.velocity, yawForward(player.viewYawRadians)), 0.0F);
  const float rightSpeed = finiteOr(dot(player.velocity, yawRight(player.viewYawRadians)), 0.0F);
  presentation.moveDirection = classifyDirection(
    priorMoveDirection,
    forwardSpeed,
    rightSpeed,
    safeHorizontalSpeed,
    moving,
    config.directionSwitchMargin
  );
  const bool justStarted =
    priorMoveDirection == PlayerMoveDirection::Stationary &&
    presentation.moveDirection != PlayerMoveDirection::Stationary;
  const bool justStopped =
    priorMoveDirection != PlayerMoveDirection::Stationary &&
    presentation.moveDirection == PlayerMoveDirection::Stationary;
  if (!grounded) {
    presentation.airborneDownSpeed = std::max(
      presentation.airborneDownSpeed,
      std::max(0.0F, -finiteOr(player.velocity.z, 0.0F))
    );
  }

  PlayerLocomotionState desired = presentation.currentState;
  PlayerMoveDirection desiredDirection = PlayerMoveDirection::Stationary;
  bool desiredLandingHeavy = false;
  const bool alive = player.health > 0;
  if (!alive) {
    desired = PlayerLocomotionState::Death;
  } else if (!grounded) {
    const bool justTookOff = presentation.wasGrounded;
    const float takeoffSeconds = positiveOr(config.takeoffSeconds, 0.10F);
    if (justTookOff ||
        (presentation.currentState == PlayerLocomotionState::Takeoff &&
         presentation.stateTimeSeconds < takeoffSeconds)) {
      desired = PlayerLocomotionState::Takeoff;
    } else if (player.velocity.z > finiteOr(config.verticalDirectionDeadzone, 0.20F)) {
      desired = PlayerLocomotionState::Rise;
    } else {
      desired = PlayerLocomotionState::Fall;
    }
  } else {
    const bool newLanding = !presentation.wasGrounded;
    const bool heavyLanding = newLanding
      ? presentation.airborneDownSpeed >=
          std::max(0.0F, finiteOr(config.heavyLandingSpeed, 7.0F))
      : presentation.landingHeavy;
    const float landingSeconds = heavyLanding
      ? positiveOr(config.heavyLandingSeconds, 0.22F)
      : positiveOr(config.landingSeconds, 0.16F);
    if (!presentation.wasGrounded ||
        (presentation.currentState == PlayerLocomotionState::Landing &&
         presentation.stateTimeSeconds < landingSeconds)) {
      desired = PlayerLocomotionState::Landing;
      desiredLandingHeavy = heavyLanding;
    } else if (player.crouched) {
      desired = moving
        ? PlayerLocomotionState::CrouchWalk
        : PlayerLocomotionState::Ducking;
    } else if (player.sneaking && moving) {
      desired = PlayerLocomotionState::Sneak;
    } else if (
      presentation.currentState == PlayerLocomotionState::Starting &&
      moving &&
      presentation.stateTimeSeconds < positiveOr(config.startSeconds, 0.16F)
    ) {
      desired = PlayerLocomotionState::Starting;
      desiredDirection = PlayerMoveDirection::Forward;
    } else if (
      presentation.currentState == PlayerLocomotionState::Stopping &&
      !moving &&
      presentation.stateTimeSeconds < positiveOr(config.stopSeconds, 0.14F)
    ) {
      desired = PlayerLocomotionState::Stopping;
      desiredDirection = PlayerMoveDirection::Forward;
    } else if (justStarted && presentation.moveDirection == PlayerMoveDirection::Forward) {
      desired = PlayerLocomotionState::Starting;
      desiredDirection = PlayerMoveDirection::Forward;
    } else if (justStopped && priorMoveDirection == PlayerMoveDirection::Forward) {
      desired = PlayerLocomotionState::Stopping;
      desiredDirection = PlayerMoveDirection::Forward;
    } else {
      desired = moving
        ? PlayerLocomotionState::Run
        : PlayerLocomotionState::Idle;
      desiredDirection = moving
        ? presentation.moveDirection
        : PlayerMoveDirection::Stationary;
    }
  }

  if (desired == PlayerLocomotionState::Run) {
    desiredDirection = presentation.moveDirection;
  }

  const float crossfadeSeconds = positiveOr(config.crossfadeSeconds, 0.12F);
  transitionTo(
    presentation,
    desired,
    desiredDirection,
    desiredLandingHeavy,
    crossfadeSeconds
  );
  presentation.stateTimeSeconds += dt;
  presentation.previousStateTimeSeconds += dt;
  presentation.blendTimeSeconds += dt;

  const float referenceSpeed = positiveOr(config.referenceRunSpeed, 8.0F);
  const float maximumRate = std::max(0.0F, finiteOr(config.maximumPlaybackRate, 2.0F));
  if (moving && grounded && dt > 0.0F) {
    const float playbackRate = std::clamp(safeHorizontalSpeed / referenceSpeed, 0.0F, maximumRate);
    const float cycle = isLooping(presentation.currentState)
      ? cycleDuration(presentation.currentState)
      : kRunCycleSeconds;
    presentation.stridePhase = std::fmod(
      presentation.stridePhase + dt * playbackRate / cycle,
      1.0F
    );
  }

  const float halfLife = positiveOr(config.responseHalfLifeSeconds, 0.06F);
  const float response = dt > 0.0F ? 1.0F - std::exp2(-dt / halfLife) : 0.0F;
  const float acceleration = dt > 0.0F
    ? std::clamp(
        (safeHorizontalSpeed - presentation.previousHorizontalSpeed) / dt,
        -100.0F,
        100.0F
      )
    : 0.0F;
  presentation.recentAcceleration +=
    (acceleration - presentation.recentAcceleration) * response;
  presentation.previousHorizontalSpeed = safeHorizontalSpeed;
  const float leanTarget = std::clamp(
    rightSpeed / referenceSpeed * finiteOr(config.leanScale, 1.0F),
    -1.0F,
    1.0F
  );
  presentation.proceduralLean +=
    (leanTarget - presentation.proceduralLean) * response;

  const bool blending =
    (
      presentation.previousState != presentation.currentState ||
      presentation.previousStateDirection != presentation.stateDirection ||
      presentation.previousLandingHeavy != presentation.landingHeavy
    ) &&
    presentation.blendTimeSeconds < crossfadeSeconds;
  const float currentWeight = blending
    ? std::clamp(presentation.blendTimeSeconds / crossfadeSeconds, 0.0F, 1.0F)
    : 1.0F;
  const float previousWeight = blending ? 1.0F - currentWeight : 0.0F;

  PlayerPresentationFrame frame;
  if (previousWeight > 0.0F) {
    frame.poseLayers[frame.poseLayerCount++] = {
      clipName(
        presentation.previousState,
        presentation.previousStateDirection,
        presentation.previousLandingHeavy
      ),
      clipTime(
        presentation.previousState,
        presentation.previousStateTimeSeconds,
        presentation.stridePhase,
        player.velocity.z,
        presentation.previousLandingHeavy,
        config
      ),
      previousWeight,
      PlayerPoseLayerMask::FullBody,
    };
  }
  frame.poseLayers[frame.poseLayerCount++] = {
    clipName(
      presentation.currentState,
      presentation.stateDirection,
      presentation.landingHeavy
    ),
    clipTime(
      presentation.currentState,
      presentation.stateTimeSeconds,
      presentation.stridePhase,
      player.velocity.z,
      presentation.landingHeavy,
      config
    ),
    currentWeight,
    PlayerPoseLayerMask::FullBody,
  };
  if (std::fabs(presentation.proceduralLean) > 0.02F &&
      frame.poseLayerCount < PlayerPresentationFrame::kMaxPoseLayers) {
    frame.poseLayers[frame.poseLayerCount++] = {
      presentation.proceduralLean > 0.0F ? "LEAN_LEFT" : "LEAN_RIGHT",
      kJumpClipSeconds,
      std::fabs(presentation.proceduralLean),
      PlayerPoseLayerMask::UpperBody,
    };
  }

  frame.proceduralLean = presentation.proceduralLean;
  const float maximumAim = std::max(0.0F, finiteOr(config.maximumTorsoAimRadians, 0.78539816F));
  frame.torsoAimPitchRadians = std::clamp(
    finiteOr(player.viewPitchRadians, 0.0F),
    -maximumAim,
    maximumAim
  );
  frame.diagnostics = {
    presentation.currentState,
    presentation.previousState,
    presentation.moveDirection,
    presentation.stateTimeSeconds,
    presentation.stridePhase,
    currentWeight,
    previousWeight,
    safeHorizontalSpeed,
    forwardSpeed,
    rightSpeed,
    presentation.recentAcceleration,
    !grounded,
    presentation.currentState == PlayerLocomotionState::Landing,
    presentation.currentState == PlayerLocomotionState::Landing &&
      presentation.landingHeavy,
    player.crouched,
    player.sneaking,
  };
  presentation.wasGrounded = grounded;
  if (grounded) {
    presentation.airborneDownSpeed = 0.0F;
  }
  return frame;
}

} // namespace lg
