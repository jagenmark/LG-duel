#include "render/WeaponPresentation.hpp"

#include <cmath>
#include <iostream>

namespace {

int expect(bool condition, const char* message) {
  if (condition) return 0;
  std::cerr << "FAIL: " << message << '\n';
  return 1;
}

bool nearlyEqual(float lhs, float rhs, float epsilon = 0.001F) {
  return std::fabs(lhs - rhs) <= epsilon;
}

} // namespace

int main() {
  int failures = 0;
  const lg::MachineGunBarrelSpinTuning tuning;
  lg::MachineGunBarrelSpinState state;

  for (int frame = 0; frame < 15; ++frame) {
    state.update(true, 1.0F / 60.0F, tuning);
  }
  failures += expect(
    std::fabs(state.normalizedSpeed(tuning) - 1.0F) < 0.001F,
    "barrel should reach full speed after the configured spin-up duration"
  );

  state.update(false, 0.10F, tuning);
  const float speedDuringCoast = state.normalizedSpeed(tuning);
  failures += expect(
    speedDuringCoast > 0.0F && speedDuringCoast < 1.0F,
    "barrel should retain momentum immediately after attack is released"
  );

  state.update(true, 0.05F, tuning);
  failures += expect(
    state.normalizedSpeed(tuning) > speedDuringCoast,
    "pressing attack during coast should accelerate from the current speed"
  );

  for (int frame = 0; frame < 32; ++frame) {
    state.update(false, 1.0F / 60.0F, tuning);
  }
  failures += expect(
    state.angularVelocityRadiansPerSecond > 0.0F,
    "barrel should still be coasting just before its configured stop time"
  );
  state.update(false, 1.0F / 60.0F, tuning);
  failures += expect(
    std::fabs(state.angularVelocityRadiansPerSecond) < 0.001F,
    "barrel should stop after the remaining spin-down duration"
  );

  lg::MachineGunBarrelSpinState sixtyHz;
  lg::MachineGunBarrelSpinState oneTwentyHz;
  for (int frame = 0; frame < 60; ++frame) sixtyHz.update(true, 1.0F / 60.0F, tuning);
  for (int frame = 0; frame < 120; ++frame) oneTwentyHz.update(true, 1.0F / 120.0F, tuning);
  failures += expect(
    std::fabs(sixtyHz.angularVelocityRadiansPerSecond -
      oneTwentyHz.angularVelocityRadiansPerSecond) < 0.001F,
    "spin velocity should be frame-rate independent"
  );

  const float angleBeforeInvalidStep = state.angleRadians;
  state.update(true, -1.0F, tuning);
  failures += expect(
    state.angleRadians == angleBeforeInvalidStep,
    "negative frame times should not move the presentation state"
  );

  lg::MachineGunFiringResponseState firingResponse;
  firingResponse.triggerShot(17U);
  failures += expect(
    nearlyEqual(firingResponse.kickAmount(), 1.0F) &&
      nearlyEqual(firingResponse.vibrationAmount(1.0F), 1.0F),
    "a machine-gun shot should begin full kick and vibration envelopes"
  );
  const float initialPhase = firingResponse.vibrationPhaseRadians;
  firingResponse.update(0.04F, 1.0F);
  failures += expect(
    firingResponse.kickAmount() > 0.0F &&
      firingResponse.kickAmount() < 1.0F &&
      firingResponse.vibrationPhaseRadians != initialPhase,
    "machine-gun firing response should recover while advancing vibration phase"
  );
  firingResponse.update(0.20F, 1.0F);
  failures += expect(
    nearlyEqual(firingResponse.kickAmount(), 0.0F) &&
      nearlyEqual(firingResponse.vibrationAmount(1.0F), 0.0F),
    "machine-gun firing response should settle completely after its short envelope"
  );

  const lg::RevolverTracerPresentation revolverStart =
    lg::revolverTracerPresentation(0.0F);
  const lg::RevolverTracerPresentation revolverFollowEnd =
    lg::revolverTracerPresentation(0.055F);
  const lg::RevolverTracerPresentation revolverFrozen =
    lg::revolverTracerPresentation(0.056F);
  const lg::RevolverTracerPresentation revolverExpired =
    lg::revolverTracerPresentation(0.11F);
  failures += expect(
    revolverStart.active && revolverStart.followMuzzle &&
      nearlyEqual(revolverStart.alpha, 1.0F) &&
      revolverFollowEnd.followMuzzle &&
      !revolverFrozen.followMuzzle && revolverFrozen.active &&
      !revolverExpired.active && nearlyEqual(revolverExpired.alpha, 0.0F),
    "revolver tracer should follow for 55 ms, freeze, and expire at 110 ms"
  );

  lg::RocketLauncherFiringResponseState rocketResponse;
  failures += expect(
    !rocketResponse.active() && nearlyEqual(rocketResponse.mechanicalAmount(), 0.0F),
    "rocket launcher response should begin at rest"
  );
  rocketResponse.triggerShot();
  rocketResponse.update(0.05F);
  failures += expect(
    rocketResponse.active() &&
      nearlyEqual(rocketResponse.mechanicalAmount(), 1.0F) &&
      rocketResponse.wholeWeaponRecoilAmount() > 0.0F,
    "rocket launcher should reach its authored mechanical peak after 50 ms"
  );
  rocketResponse.update(0.05F);
  failures += expect(
    rocketResponse.mechanicalAmount() < 0.0F,
    "rocket launcher mechanism should cross its small recovery overshoot"
  );
  rocketResponse.update(0.10F);
  failures += expect(
    !rocketResponse.active() &&
      nearlyEqual(rocketResponse.mechanicalAmount(), 0.0F) &&
      nearlyEqual(rocketResponse.wholeWeaponRecoilAmount(), 0.0F),
    "rocket launcher response should return exactly to rest"
  );

  lg::RocketLauncherFiringResponseState rocketSixtyHz;
  lg::RocketLauncherFiringResponseState rocketOneTwentyHz;
  rocketSixtyHz.triggerShot();
  rocketOneTwentyHz.triggerShot();
  for (int frame = 0; frame < 6; ++frame) rocketSixtyHz.update(1.0F / 60.0F);
  for (int frame = 0; frame < 12; ++frame) rocketOneTwentyHz.update(1.0F / 120.0F);
  failures += expect(
    nearlyEqual(
      rocketSixtyHz.mechanicalAmount(),
      rocketOneTwentyHz.mechanicalAmount()
    ),
    "rocket launcher mechanical response should be frame-rate independent"
  );

  return failures == 0 ? 0 : 1;
}
