#include "render/WeaponPresentation.hpp"

#include <cmath>
#include <iostream>
#include <limits>

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

  const lg::MachineGunMuzzleFlashEnvelope muzzlePeak =
    lg::machineGunMuzzleFlashEnvelope(0.0F, 17U);
  const lg::MachineGunMuzzleFlashEnvelope muzzleCarry =
    lg::machineGunMuzzleFlashEnvelope(0.080F, 17U);
  const lg::MachineGunMuzzleFlashEnvelope muzzleVariation =
    lg::machineGunMuzzleFlashEnvelope(0.080F, 25U);
  const lg::MachineGunMuzzleFlashEnvelope muzzleExpired =
    lg::machineGunMuzzleFlashEnvelope(
      lg::kMachineGunMuzzleFlashDurationSeconds,
      17U
    );
  failures += expect(
    muzzlePeak.flameAlpha > 0.95F && muzzlePeak.coreAlpha > 0.95F &&
      muzzleCarry.coreAlpha > 0.10F &&
      muzzleCarry.flameAlpha < muzzleCarry.coreAlpha &&
      muzzleCarry.coreAlpha != muzzleVariation.coreAlpha &&
      muzzleExpired.coreAlpha == 0.0F,
    "machine-gun flashes should keep a varied carried core between sharp shots"
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
    revolverStart.active &&
      nearlyEqual(revolverStart.alpha, 1.0F) &&
      revolverFollowEnd.active && revolverFollowEnd.alpha > revolverFrozen.alpha &&
      revolverFrozen.active &&
      !revolverExpired.active && nearlyEqual(revolverExpired.alpha, 0.0F),
    "revolver tracer should keep its fixed world path while fading to 110 ms"
  );

  const lg::SniperSmokeTracerPresentation sniperStart =
    lg::sniperSmokeTracerPresentation(0.0F);
  const lg::SniperSmokeTracerPresentation sniperMiddle =
    lg::sniperSmokeTracerPresentation(0.0425F);
  const lg::SniperSmokeTracerPresentation sniperExpired =
    lg::sniperSmokeTracerPresentation(lg::kSniperSmokeTracerLifetimeSeconds);
  const lg::SniperSmokeTracerPresentation sniperInvalid =
    lg::sniperSmokeTracerPresentation(std::numeric_limits<float>::quiet_NaN());
  failures += expect(
    nearlyEqual(lg::kSniperSmokeTracerLifetimeSeconds, 0.085F) &&
      nearlyEqual(lg::kSniperSmokeTracerMaximumLength, 2.40F) &&
      sniperStart.active && nearlyEqual(sniperStart.alpha, 1.0F) &&
      sniperMiddle.active && sniperMiddle.alpha > 0.0F &&
      sniperMiddle.alpha < sniperStart.alpha &&
      !sniperExpired.active && nearlyEqual(sniperExpired.alpha, 0.0F) &&
      !sniperInvalid.active && nearlyEqual(sniperInvalid.alpha, 0.0F),
    "sniper smoke tracer should have a fixed short lifetime, bounded length, and deterministic fade"
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

  lg::FreezeGunFiringResponseState freezeResponse;
  freezeResponse.update(true, 0.05F);
  failures += expect(
    freezeResponse.amount > 0.60F &&
      freezeResponse.activationFlashAmount() > 0.0F &&
      freezeResponse.coolantPulse() >= 0.0F,
    "freeze gun should rapidly focus, flash once, and begin its coolant pulse"
  );
  const float focusedAmount = freezeResponse.amount;
  freezeResponse.update(false, 0.05F);
  failures += expect(
    freezeResponse.amount > 0.0F && freezeResponse.amount < focusedAmount,
    "freeze gun should ease out instead of snapping to rest"
  );
  freezeResponse.update(false, 2.0F);
  failures += expect(
    nearlyEqual(freezeResponse.amount, 0.0F) &&
      nearlyEqual(freezeResponse.activationFlashAmount(), 0.0F),
    "freeze gun should settle exactly at rest after release"
  );

  lg::PlasmaGunFiringResponseState plasmaResponse;
  failures += expect(
    !plasmaResponse.active() &&
      nearlyEqual(plasmaResponse.containmentAmount(), 0.0F),
    "plasma gun containment should begin at rest"
  );
  plasmaResponse.triggerShot();
  failures += expect(
    plasmaResponse.active() &&
      nearlyEqual(plasmaResponse.containmentAmount(), 1.0F),
    "a plasma shot should immediately contract the contained core"
  );
  plasmaResponse.update(0.08F);
  failures += expect(
    plasmaResponse.containmentAmount() > 0.0F &&
      plasmaResponse.containmentAmount() < 1.0F,
    "plasma containment should recover smoothly during its response"
  );
  plasmaResponse.update(0.08F);
  failures += expect(
    !plasmaResponse.active() &&
      nearlyEqual(plasmaResponse.containmentAmount(), 0.0F),
    "plasma containment should return exactly to rest"
  );

  return failures == 0 ? 0 : 1;
}
