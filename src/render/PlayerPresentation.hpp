#pragma once

#include "sim/PlayerState.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace lg {

enum class PlayerLocomotionState : std::uint8_t {
  Idle,
  Starting,
  Run,
  Stopping,
  Sneak,
  Ducking,
  CrouchWalk,
  Takeoff,
  Rise,
  Fall,
  Landing,
  Death,
};

enum class PlayerMoveDirection : std::uint8_t {
  Stationary,
  Forward,
  Backward,
  Left,
  Right,
};

enum class PlayerPoseLayerMask : std::uint8_t {
  FullBody,
  UpperBody,
};

struct PlayerPresentationConfig {
  float moveEnterSpeed = 0.35F;
  float moveExitSpeed = 0.20F;
  float directionSwitchMargin = 0.12F;
  float referenceRunSpeed = 8.0F;
  float maximumPlaybackRate = 2.0F;
  float crossfadeSeconds = 0.12F;
  float takeoffSeconds = 0.10F;
  float landingSeconds = 0.16F;
  float heavyLandingSeconds = 0.22F;
  float startSeconds = 0.16F;
  float stopSeconds = 0.14F;
  float heavyLandingSpeed = 7.0F;
  float verticalDirectionDeadzone = 0.20F;
  float responseHalfLifeSeconds = 0.06F;
  float leanScale = 1.0F;
  float maximumTorsoAimRadians = 0.78539816F;
};

struct PlayerPoseLayer {
  std::string_view animationName;
  float timeSeconds = 0.0F;
  float weight = 0.0F;
  PlayerPoseLayerMask mask = PlayerPoseLayerMask::FullBody;
};

struct PlayerPresentationDiagnostics {
  PlayerLocomotionState currentState = PlayerLocomotionState::Idle;
  PlayerLocomotionState previousState = PlayerLocomotionState::Idle;
  PlayerMoveDirection moveDirection = PlayerMoveDirection::Stationary;
  float stateTimeSeconds = 0.0F;
  float stridePhase = 0.0F;
  float currentBlendWeight = 1.0F;
  float previousBlendWeight = 0.0F;
  float horizontalSpeed = 0.0F;
  float forwardSpeed = 0.0F;
  float rightSpeed = 0.0F;
  float recentAcceleration = 0.0F;
  bool airborne = false;
  bool landing = false;
  bool heavyLanding = false;
  bool crouched = false;
  bool sneaking = false;
};

struct PlayerPresentationFrame {
  static constexpr std::size_t kMaxPoseLayers = 3;

  std::array<PlayerPoseLayer, kMaxPoseLayers> poseLayers = {};
  std::uint8_t poseLayerCount = 0;
  float proceduralLean = 0.0F;
  float torsoAimPitchRadians = 0.0F;
  PlayerPresentationDiagnostics diagnostics = {};
};

// One instance belongs to one rendered player and is never shared with gameplay.
struct PlayerPresentationState {
  PlayerLocomotionState currentState = PlayerLocomotionState::Idle;
  PlayerLocomotionState previousState = PlayerLocomotionState::Idle;
  PlayerMoveDirection moveDirection = PlayerMoveDirection::Stationary;
  PlayerMoveDirection stateDirection = PlayerMoveDirection::Stationary;
  PlayerMoveDirection previousStateDirection = PlayerMoveDirection::Stationary;
  float stateTimeSeconds = 0.0F;
  float previousStateTimeSeconds = 0.0F;
  float blendTimeSeconds = 0.0F;
  float stridePhase = 0.0F;
  float previousHorizontalSpeed = 0.0F;
  float recentAcceleration = 0.0F;
  float proceduralLean = 0.0F;
  float airborneDownSpeed = 0.0F;
  std::uint32_t playerIndex = 0;
  bool initialized = false;
  bool wasGrounded = true;
  bool landingHeavy = false;
  bool previousLandingHeavy = false;
};

[[nodiscard]] PlayerPresentationFrame updatePlayerPresentation(
  PlayerPresentationState& presentation,
  const PlayerState& player,
  float deltaSeconds,
  std::uint32_t playerIndex,
  const PlayerPresentationConfig& config = {}
);

} // namespace lg
