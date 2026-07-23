#include "render/PlayerPresentation.hpp"

#include <cmath>
#include <iostream>
#include <string_view>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) {
    return 0;
  }
  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

bool nearlyEqual(float lhs, float rhs, float epsilon = 0.001F) {
  return std::fabs(lhs - rhs) <= epsilon;
}

lg::PlayerState groundedPlayer() {
  lg::PlayerState player;
  player.onGround = true;
  player.movementMode = lg::MovementMode::Grounded;
  return player;
}

std::string_view activeBaseClip(const lg::PlayerPresentationFrame& frame) {
  for (std::size_t index = frame.poseLayerCount; index > 0U; --index) {
    const lg::PlayerPoseLayer& layer = frame.poseLayers[index - 1U];
    if (layer.mask == lg::PlayerPoseLayerMask::FullBody) return layer.animationName;
  }
  return {};
}

} // namespace

int main() {
  int failures = 0;
  const lg::PlayerPresentationConfig config;

  lg::PlayerPresentationState thresholdState;
  lg::PlayerState player = groundedPlayer();
  player.velocity.x = config.moveEnterSpeed - 0.01F;
  auto frame = lg::updatePlayerPresentation(thresholdState, player, 0.016F, 2U, config);

  lg::PlayerPresentationState deathState;
  lg::PlayerState deadPlayer = groundedPlayer();
  deadPlayer.health = 0;
  auto deathFrame = lg::updatePlayerPresentation(deathState, deadPlayer, 0.25F, 0U, config);
  failures += expect(
    deathFrame.diagnostics.currentState == lg::PlayerLocomotionState::Death &&
      activeBaseClip(deathFrame) == "Death",
    "a dead player should play and hold a death clip"
  );
  deadPlayer.health = 100;
  (void)lg::updatePlayerPresentation(deathState, deadPlayer, 0.016F, 0U, config);
  deadPlayer.health = 0;
  deathFrame = lg::updatePlayerPresentation(deathState, deadPlayer, 0.016F, 0U, config);
  failures += expect(
    activeBaseClip(deathFrame) == "Death",
    "each later death should restart the full death clip"
  );
  failures += expect(
    frame.diagnostics.currentState == lg::PlayerLocomotionState::Idle &&
      frame.diagnostics.moveDirection == lg::PlayerMoveDirection::Stationary,
    "speed below the enter threshold should remain idle"
  );
  player.velocity.x = config.moveEnterSpeed + 0.01F;
  frame = lg::updatePlayerPresentation(thresholdState, player, 0.016F, 2U, config);
  failures += expect(
    frame.diagnostics.currentState == lg::PlayerLocomotionState::Starting &&
      frame.diagnostics.moveDirection == lg::PlayerMoveDirection::Forward &&
      activeBaseClip(frame) == "START_FORWARD",
    "forward speed above the enter threshold should play the authored start"
  );
  player.velocity.x = (config.moveEnterSpeed + config.moveExitSpeed) * 0.5F;
  frame = lg::updatePlayerPresentation(thresholdState, player, 0.016F, 2U, config);
  failures += expect(
    frame.diagnostics.currentState == lg::PlayerLocomotionState::Starting,
    "movement hysteresis should retain the start while speed stays above exit"
  );
  player.velocity.x = config.moveExitSpeed - 0.01F;
  frame = lg::updatePlayerPresentation(thresholdState, player, 0.016F, 2U, config);
  failures += expect(
    frame.diagnostics.currentState == lg::PlayerLocomotionState::Stopping &&
      activeBaseClip(frame) == "STOP_FORWARD",
    "dropping below the exit threshold should play the authored stop"
  );
  frame = lg::updatePlayerPresentation(
    thresholdState, player, config.stopSeconds, 2U, config
  );
  frame = lg::updatePlayerPresentation(thresholdState, player, 0.001F, 2U, config);
  failures += expect(
    frame.diagnostics.currentState == lg::PlayerLocomotionState::Idle,
    "the stop clip should settle to idle without restarting"
  );

  lg::PlayerPresentationState directionState;
  player = groundedPlayer();
  player.viewYawRadians = 0.0F;
  player.velocity = {2.0F, 0.0F, 0.0F};
  frame = lg::updatePlayerPresentation(directionState, player, 0.016F, 3U, config);
  failures += expect(
    frame.diagnostics.moveDirection == lg::PlayerMoveDirection::Forward,
    "positive local forward velocity should classify forward"
  );
  frame = lg::updatePlayerPresentation(
    directionState, player, config.startSeconds, 3U, config
  );
  frame = lg::updatePlayerPresentation(directionState, player, 0.001F, 3U, config);
  failures += expect(
    frame.diagnostics.currentState == lg::PlayerLocomotionState::Run &&
      activeBaseClip(frame) == "RUN",
    "completed forward start should enter the forward run loop"
  );
  player.velocity = {-2.0F, 0.0F, 0.0F};
  frame = lg::updatePlayerPresentation(directionState, player, 0.016F, 3U, config);
  failures += expect(
    frame.diagnostics.moveDirection == lg::PlayerMoveDirection::Backward &&
      activeBaseClip(frame) == "RUN_BACK" &&
      frame.diagnostics.previousBlendWeight > 0.0F,
    "backward movement should select and crossfade to the authored backpedal"
  );
  player.velocity = {0.0F, -2.0F, 0.0F};
  frame = lg::updatePlayerPresentation(directionState, player, 0.016F, 3U, config);
  failures += expect(
    frame.diagnostics.moveDirection == lg::PlayerMoveDirection::Right &&
      activeBaseClip(frame) == "STRAFE_LEFT",
    "right movement should select the imported clip that moves visually right"
  );
  player.velocity = {0.0F, 2.0F, 0.0F};
  frame = lg::updatePlayerPresentation(directionState, player, 0.016F, 3U, config);
  failures += expect(
    frame.diagnostics.moveDirection == lg::PlayerMoveDirection::Left &&
      activeBaseClip(frame) == "STRAFE_RIGHT",
    "left movement should select the imported clip that moves visually left"
  );
  player.viewYawRadians = 1.57079632679F;
  player.velocity = {0.0F, 2.0F, 0.0F};
  frame = lg::updatePlayerPresentation(directionState, player, 0.016F, 3U, config);
  failures += expect(
    frame.diagnostics.moveDirection == lg::PlayerMoveDirection::Forward,
    "direction classification should rotate into the player's local frame"
  );

  lg::PlayerPresentationState directionNoiseState;
  player = groundedPlayer();
  player.velocity = {1.0F, -0.8F, 0.0F};
  frame = lg::updatePlayerPresentation(directionNoiseState, player, 0.016F, 4U, config);
  player.velocity = {0.96F, -1.0F, 0.0F};
  frame = lg::updatePlayerPresentation(directionNoiseState, player, 0.016F, 4U, config);
  failures += expect(
    frame.diagnostics.moveDirection == lg::PlayerMoveDirection::Forward,
    "small diagonal noise should not switch the retained direction axis"
  );
  player.velocity = {0.6F, -1.4F, 0.0F};
  frame = lg::updatePlayerPresentation(directionNoiseState, player, 0.016F, 4U, config);
  failures += expect(
    frame.diagnostics.moveDirection == lg::PlayerMoveDirection::Right,
    "a decisive diagonal change should switch direction axis"
  );

  lg::PlayerPresentationState postureState;
  player = groundedPlayer();
  player.crouched = true;
  frame = lg::updatePlayerPresentation(postureState, player, 0.20F, 5U, config);
  failures += expect(
    frame.diagnostics.currentState == lg::PlayerLocomotionState::Ducking &&
      frame.poseLayers[frame.poseLayerCount - 1U].animationName == "DUCKING",
    "stationary crouch should select the ducking pose"
  );
  player.velocity.x = 2.0F;
  frame = lg::updatePlayerPresentation(postureState, player, 0.016F, 5U, config);
  failures += expect(
    frame.diagnostics.currentState == lg::PlayerLocomotionState::CrouchWalk,
    "moving crouch should select crouch walk"
  );
  player.crouched = false;
  player.sneaking = true;
  frame = lg::updatePlayerPresentation(postureState, player, 0.016F, 5U, config);
  failures += expect(
    frame.diagnostics.currentState == lg::PlayerLocomotionState::Sneak &&
      frame.diagnostics.sneaking,
    "moving sneak facts should select the sneak cycle"
  );

  lg::PlayerPresentationState airborneState;
  player = groundedPlayer();
  frame = lg::updatePlayerPresentation(airborneState, player, 0.016F, 6U, config);
  player.onGround = false;
  player.movementMode = lg::MovementMode::Airborne;
  player.velocity.z = 5.0F;
  frame = lg::updatePlayerPresentation(airborneState, player, 0.016F, 6U, config);
  failures += expect(
    frame.diagnostics.currentState == lg::PlayerLocomotionState::Takeoff &&
      frame.diagnostics.airborne,
    "leaving ground should enter a short takeoff state"
  );
  frame = lg::updatePlayerPresentation(airborneState, player, config.takeoffSeconds, 6U, config);
  frame = lg::updatePlayerPresentation(airborneState, player, 0.001F, 6U, config);
  failures += expect(
    frame.diagnostics.currentState == lg::PlayerLocomotionState::Rise,
    "positive vertical speed after takeoff should select rise"
  );
  player.velocity.z = -4.0F;
  frame = lg::updatePlayerPresentation(airborneState, player, 0.016F, 6U, config);
  failures += expect(
    frame.diagnostics.currentState == lg::PlayerLocomotionState::Fall &&
      frame.poseLayers[frame.poseLayerCount - 1U].animationName == "FALL",
    "negative vertical speed should select fall"
  );
  player.onGround = true;
  player.movementMode = lg::MovementMode::Grounded;
  player.velocity.z = 0.0F;
  frame = lg::updatePlayerPresentation(airborneState, player, 0.016F, 6U, config);
  failures += expect(
    frame.diagnostics.currentState == lg::PlayerLocomotionState::Landing &&
      frame.diagnostics.landing &&
      !frame.diagnostics.heavyLanding &&
      activeBaseClip(frame) == "LAND_LIGHT" &&
      frame.poseLayers[frame.poseLayerCount - 1U].timeSeconds > 0.0F,
    "a modest fall should advance the authored light landing clip"
  );
  frame = lg::updatePlayerPresentation(airborneState, player, config.landingSeconds, 6U, config);
  frame = lg::updatePlayerPresentation(airborneState, player, 0.001F, 6U, config);
  failures += expect(
    frame.diagnostics.currentState == lg::PlayerLocomotionState::Idle,
    "landing should settle back to grounded locomotion"
  );

  lg::PlayerPresentationState heavyLandingState;
  player = groundedPlayer();
  (void)lg::updatePlayerPresentation(heavyLandingState, player, 0.016F, 60U, config);
  player.onGround = false;
  player.movementMode = lg::MovementMode::Airborne;
  player.velocity.z = -(config.heavyLandingSpeed + 1.0F);
  (void)lg::updatePlayerPresentation(heavyLandingState, player, 0.10F, 60U, config);
  player.onGround = true;
  player.movementMode = lg::MovementMode::Grounded;
  player.velocity.z = 0.0F;
  frame = lg::updatePlayerPresentation(heavyLandingState, player, 0.016F, 60U, config);
  failures += expect(
    frame.diagnostics.heavyLanding && activeBaseClip(frame) == "LAND_HEAVY",
    "a fast downward impact should select the authored heavy landing clip"
  );

  lg::PlayerPresentationState blendState;
  player = groundedPlayer();
  frame = lg::updatePlayerPresentation(blendState, player, 0.20F, 7U, config);
  player.velocity.x = 4.0F;
  frame = lg::updatePlayerPresentation(blendState, player, config.crossfadeSeconds * 0.25F, 7U, config);
  failures += expect(
    frame.poseLayerCount >= 2U &&
      frame.poseLayers[0].animationName == "IDLE" &&
      frame.poseLayers[1].animationName == "START_FORWARD" &&
      nearlyEqual(
        frame.diagnostics.currentBlendWeight + frame.diagnostics.previousBlendWeight,
        1.0F
      ) &&
      frame.diagnostics.currentBlendWeight > 0.0F &&
      frame.diagnostics.currentBlendWeight < 1.0F,
    "state changes should emit complementary previous and current base layers"
  );
  const float stateTimeBefore = frame.diagnostics.stateTimeSeconds;
  frame = lg::updatePlayerPresentation(blendState, player, 0.010F, 7U, config);
  failures += expect(
    frame.diagnostics.stateTimeSeconds > stateTimeBefore,
    "stable locomotion should not repeatedly reset the current state"
  );
  frame = lg::updatePlayerPresentation(blendState, player, config.crossfadeSeconds, 7U, config);
  failures += expect(
    nearlyEqual(frame.diagnostics.currentBlendWeight, 1.0F) &&
      nearlyEqual(frame.diagnostics.previousBlendWeight, 0.0F),
    "crossfade should complete at full current-state weight"
  );

  lg::PlayerPresentationState interruptedBlendState;
  player = groundedPlayer();
  (void)lg::updatePlayerPresentation(interruptedBlendState, player, 0.20F, 8U, config);
  player.velocity.x = 4.0F;
  frame = lg::updatePlayerPresentation(
    interruptedBlendState, player, config.crossfadeSeconds * 0.25F, 8U, config
  );
  player.onGround = false;
  player.movementMode = lg::MovementMode::Airborne;
  player.velocity.z = 5.0F;
  frame = lg::updatePlayerPresentation(
    interruptedBlendState, player, 0.016F, 8U, config
  );
  failures += expect(
    frame.poseLayerCount >= 2U &&
      frame.poseLayers[0].animationName == "IDLE" &&
      frame.poseLayers[1].animationName == "JUMP",
    "an interrupted early crossfade should carry its dominant visible source pose"
  );

  lg::PlayerPresentationState lifecycleState;
  player = groundedPlayer();
  (void)lg::updatePlayerPresentation(lifecycleState, player, 0.016F, 9U, config);
  player.onGround = false;
  player.movementMode = lg::MovementMode::Airborne;
  player.velocity.z = -3.0F;
  (void)lg::updatePlayerPresentation(lifecycleState, player, 0.20F, 9U, config);
  lifecycleState = {};
  player = groundedPlayer();
  frame = lg::updatePlayerPresentation(lifecycleState, player, 0.016F, 9U, config);
  failures += expect(
    frame.diagnostics.currentState == lg::PlayerLocomotionState::Idle &&
      !frame.diagnostics.landing,
    "resetting a hidden or reused slot should not synthesize a landing"
  );

  lg::PlayerPresentationState phaseA;
  lg::PlayerPresentationState phaseB;
  player = groundedPlayer();
  player.velocity.x = config.referenceRunSpeed;
  const auto phaseFrameA = lg::updatePlayerPresentation(phaseA, player, 0.0F, 10U, config);
  const auto phaseFrameARepeat = lg::updatePlayerPresentation(
    phaseB, player, 0.0F, 10U, config
  );
  lg::PlayerPresentationState phaseC;
  const auto phaseFrameC = lg::updatePlayerPresentation(phaseC, player, 0.0F, 11U, config);
  failures += expect(
    nearlyEqual(phaseFrameA.diagnostics.stridePhase, phaseFrameARepeat.diagnostics.stridePhase) &&
      !nearlyEqual(phaseFrameA.diagnostics.stridePhase, phaseFrameC.diagnostics.stridePhase),
    "stride phase should be deterministic per index and independent across players"
  );

  lg::PlayerPresentationState responseState;
  player = groundedPlayer();
  frame = lg::updatePlayerPresentation(responseState, player, 0.016F, 12U, config);
  player.velocity = {6.0F, -4.0F, 0.0F};
  frame = lg::updatePlayerPresentation(responseState, player, 0.05F, 12U, config);
  failures += expect(
    frame.diagnostics.recentAcceleration > 0.0F &&
      frame.proceduralLean > 0.0F &&
      frame.poseLayerCount <= lg::PlayerPresentationFrame::kMaxPoseLayers,
    "acceleration and restrained lean should respond without exceeding fixed capacity"
  );
  player.velocity = {};
  frame = lg::updatePlayerPresentation(responseState, player, 0.05F, 12U, config);
  failures += expect(
    frame.diagnostics.recentAcceleration < 0.0F,
    "recent acceleration should report deceleration"
  );

  player = groundedPlayer();
  player.position = {4.0F, 5.0F, 6.0F};
  player.velocity = {1.0F, 2.0F, 3.0F};
  player.viewYawRadians = 0.7F;
  player.viewPitchRadians = 2.0F;
  player.health = 73;
  player.crouched = true;
  player.sneaking = true;
  const lg::PlayerState unchanged = player;
  lg::PlayerPresentationState nonMutationState;
  frame = lg::updatePlayerPresentation(nonMutationState, player, 0.016F, 13U, config);
  failures += expect(
    player.position.x == unchanged.position.x &&
      player.position.y == unchanged.position.y &&
      player.position.z == unchanged.position.z &&
      player.velocity.x == unchanged.velocity.x &&
      player.velocity.y == unchanged.velocity.y &&
      player.velocity.z == unchanged.velocity.z &&
      player.viewYawRadians == unchanged.viewYawRadians &&
      player.viewPitchRadians == unchanged.viewPitchRadians &&
      player.health == unchanged.health &&
      player.crouched == unchanged.crouched &&
      player.sneaking == unchanged.sneaking &&
      frame.torsoAimPitchRadians <= config.maximumTorsoAimRadians,
    "presentation update should not mutate gameplay facts and should clamp torso aim"
  );

  return failures == 0 ? 0 : 1;
}
