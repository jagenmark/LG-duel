#include "render/WeaponSwitchPresentation.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace {

int expect(bool condition, const char* message) {
  if (condition) return 0;
  std::cerr << "FAIL: " << message << '\n';
  return 1;
}

bool near(float left, float right) {
  return std::fabs(left - right) < 0.0001F;
}

lg::WeaponSwitchPresentationOutput finish(
  lg::WeaponSwitchPresentationController& controller,
  lg::Weapon weapon
) {
  return controller.update(
    weapon,
    lg::kWeaponSwitchPresentationSeconds * 2.0F
  );
}

} // namespace

int main() {
  int failures = 0;

  const auto outgoing = lg::sampleWeaponSwitchPresentation(
    lg::Weapon::MachineGun, lg::Weapon::RocketLauncher, 0.25F, true
  );
  const auto apex = lg::sampleWeaponSwitchPresentation(
    lg::Weapon::MachineGun, lg::Weapon::RocketLauncher, 0.50F, true
  );
  const auto incoming = lg::sampleWeaponSwitchPresentation(
    lg::Weapon::MachineGun, lg::Weapon::RocketLauncher, 0.75F, true
  );
  const auto complete = lg::sampleWeaponSwitchPresentation(
    lg::Weapon::MachineGun, lg::Weapon::RocketLauncher, 1.0F, false
  );
  failures += expect(
    outgoing.displayedWeapon == lg::Weapon::MachineGun &&
      !outgoing.incomingHalf && outgoing.lift > 0.0F && outgoing.lift < 1.0F &&
      apex.displayedWeapon == lg::Weapon::RocketLauncher &&
      apex.incomingHalf && near(apex.lift, 1.0F) &&
      apex.pitchRadians > 0.0F &&
      apex.upperBodyPitchRadians > 0.0F &&
      incoming.displayedWeapon == lg::Weapon::RocketLauncher &&
      incoming.incomingHalf && incoming.lift > 0.0F && incoming.lift < 1.0F &&
      !complete.active && complete.displayedWeapon == lg::Weapon::RocketLauncher &&
      near(complete.lift, 0.0F),
    "the shared phase should drop first person and raise the Worker with one weapon"
  );

  lg::WeaponSwitchPresentationController controller;
  auto output = controller.update(lg::Weapon::LightningGun, 0.0F);
  failures += expect(
    !output.active && output.displayedWeapon == lg::Weapon::LightningGun,
    "first observation should establish the rest weapon without animation"
  );
  output = controller.update(lg::Weapon::LightningGun, 0.03F);
  failures += expect(!output.active, "the selected weapon should not restart itself");
  output = controller.update(lg::Weapon::Railgun, 0.04F);
  failures += expect(
    output.active && !output.incomingHalf &&
      output.displayedWeapon == lg::Weapon::LightningGun && output.lift > 0.0F,
    "the outgoing weapon should move toward the hidden swap point"
  );
  output = controller.update(lg::Weapon::Railgun, 0.05F);
  failures += expect(
    output.incomingHalf && output.displayedWeapon == lg::Weapon::Railgun,
    "the incoming weapon should appear after the apex"
  );
  const float pausedLift = output.lift;
  output = controller.update(lg::Weapon::Railgun, 0.0F);
  failures += expect(
    near(output.lift, pausedLift), "zero delta should pause the explicit timeline"
  );

  const auto sampleAtFrameRate = [](float framesPerSecond) {
    lg::WeaponSwitchPresentationController sampled;
    (void)sampled.update(lg::Weapon::LightningGun, 0.0F);
    constexpr float totalSeconds = 0.12F;
    float elapsed = 0.0F;
    while (elapsed < totalSeconds) {
      const float delta = std::min(1.0F / framesPerSecond, totalSeconds - elapsed);
      (void)sampled.update(lg::Weapon::RocketLauncher, delta);
      elapsed += delta;
    }
    return sampled.update(lg::Weapon::RocketLauncher, 0.0F);
  };
  const auto thirtyResult = sampleAtFrameRate(30.0F);
  const auto oneTwentyFiveResult = sampleAtFrameRate(125.0F);
  const auto twoFortyResult = sampleAtFrameRate(240.0F);
  failures += expect(
    thirtyResult.displayedWeapon == oneTwentyFiveResult.displayedWeapon &&
      thirtyResult.displayedWeapon == twoFortyResult.displayedWeapon &&
      near(thirtyResult.lift, oneTwentyFiveResult.lift) &&
      near(thirtyResult.lift, twoFortyResult.lift),
    "30, 125, and 240 FPS should produce equal explicit-time output"
  );

  lg::WeaponSwitchPresentationController beforeApex;
  (void)beforeApex.update(lg::Weapon::MachineGun, 0.0F);
  const auto firstRaise = beforeApex.update(lg::Weapon::Shotgun, 0.025F);
  const auto beforeRetarget = beforeApex.update(lg::Weapon::PlasmaGun, 0.0F);
  failures += expect(
    beforeRetarget.outgoingWeapon == lg::Weapon::MachineGun &&
      beforeRetarget.incomingWeapon == lg::Weapon::PlasmaGun &&
      beforeRetarget.displayedWeapon == lg::Weapon::MachineGun &&
      near(beforeRetarget.lift, firstRaise.lift) &&
      finish(beforeApex, lg::Weapon::PlasmaGun).displayedWeapon ==
        lg::Weapon::PlasmaGun,
    "A to B to C before apex should preserve lift and settle on C without a queue"
  );

  lg::WeaponSwitchPresentationController afterApex;
  (void)afterApex.update(lg::Weapon::MachineGun, 0.0F);
  const auto firstIncoming = afterApex.update(lg::Weapon::Shotgun, 0.10F);
  const auto afterRetarget = afterApex.update(lg::Weapon::RocketLauncher, 0.0F);
  failures += expect(
    firstIncoming.displayedWeapon == lg::Weapon::Shotgun &&
      afterRetarget.outgoingWeapon == lg::Weapon::Shotgun &&
      afterRetarget.incomingWeapon == lg::Weapon::RocketLauncher &&
      near(afterRetarget.lift, firstIncoming.lift) &&
      finish(afterApex, lg::Weapon::RocketLauncher).displayedWeapon ==
        lg::Weapon::RocketLauncher,
    "A to B to C after apex should retarget from visible B without returning to rest"
  );

  lg::WeaponSwitchPresentationController spam;
  (void)spam.update(lg::Weapon::MachineGun, 0.0F);
  bool spamBounded = true;
  for (int index = 0; index < 80; ++index) {
    const lg::Weapon target = (index % 2) == 0
      ? lg::Weapon::Shotgun : lg::Weapon::MachineGun;
    const auto frame = spam.update(target, 0.003F);
    spamBounded = spamBounded && std::isfinite(frame.lift) &&
      frame.lift >= 0.0F && frame.lift <= 1.0F;
  }
  const auto spamFinished = finish(spam, lg::Weapon::MachineGun);
  failures += expect(
    spamBounded && !spamFinished.active &&
      spamFinished.displayedWeapon == lg::Weapon::MachineGun,
    "repeated A/B spam should stay bounded and settle on the newest target"
  );

  lg::WeaponSwitchPresentationController fire;
  (void)fire.update(lg::Weapon::MachineGun, 0.0F);
  (void)fire.update(lg::Weapon::RocketLauncher, 0.015F);
  const bool staleHitscanAccepted = fire.observeAuthoritativeFire(
    lg::Weapon::MachineGun, 100U
  );
  const auto afterStaleHitscan = fire.update(lg::Weapon::RocketLauncher, 0.0F);
  const bool staleProjectileAccepted = fire.observeAuthoritativeFire(
    lg::Weapon::GrenadeLauncher, 101U
  );
  const auto afterStaleProjectile = fire.update(lg::Weapon::RocketLauncher, 0.0F);
  const bool validIncomingAccepted = fire.observeAuthoritativeFire(
    lg::Weapon::RocketLauncher, 102U
  );
  const auto afterValidIncoming = fire.update(lg::Weapon::RocketLauncher, 0.0F);
  const bool duplicateAccepted = fire.observeAuthoritativeFire(
    lg::Weapon::RocketLauncher, 102U
  );
  failures += expect(
    staleHitscanAccepted && staleProjectileAccepted &&
      !afterStaleHitscan.incomingHalf && !afterStaleProjectile.incomingHalf &&
      validIncomingAccepted && afterValidIncoming.incomingHalf &&
      afterValidIncoming.displayedWeapon == lg::Weapon::RocketLauncher &&
      !duplicateAccepted,
    "stale outgoing events should not promote, while one valid incoming event should"
  );

  lg::WeaponSwitchPresentationController retained;
  (void)retained.update(lg::Weapon::MachineGun, 0.0F);
  (void)retained.observeAuthoritativeFire(lg::Weapon::MachineGun, 77U);
  (void)retained.update(lg::Weapon::Shotgun, 0.02F);
  const auto rapidBack = retained.update(lg::Weapon::MachineGun, 0.0F);
  const bool staleRetainedAccepted = retained.observeAuthoritativeFire(
    lg::Weapon::MachineGun, 77U
  );
  const auto afterRetained = retained.update(lg::Weapon::MachineGun, 0.0F);
  failures += expect(
    !rapidBack.active && !staleRetainedAccepted &&
      !afterRetained.active &&
      afterRetained.displayedWeapon == lg::Weapon::MachineGun &&
      near(afterRetained.lift, 0.0F),
    "choosing the visible weapon should cancel cleanly and ignore its retained fire event"
  );

  lg::WeaponSwitchPresentationController continuous;
  (void)continuous.update(lg::Weapon::LightningGun, 0.0F);
  (void)continuous.update(lg::Weapon::FreezeGun, 0.01F);
  continuous.observeContinuousUse(lg::Weapon::LightningGun);
  const auto staleBeam = continuous.update(lg::Weapon::FreezeGun, 0.0F);
  continuous.observeContinuousUse(lg::Weapon::FreezeGun);
  const auto activeBeam = continuous.update(lg::Weapon::FreezeGun, 0.0F);
  continuous.observeContinuousUse(lg::Weapon::FreezeGun);
  const auto repeatedBeam = continuous.update(lg::Weapon::FreezeGun, 0.0F);
  failures += expect(
    !staleBeam.incomingHalf && activeBeam.incomingHalf &&
      activeBeam.displayedWeapon == lg::Weapon::FreezeGun &&
      near(activeBeam.lift, repeatedBeam.lift),
    "continuous LG or Freeze state should promote only the selected incoming weapon"
  );

  continuous.reset();
  const auto reset = continuous.update(lg::Weapon::Revolver, 0.0F);
  failures += expect(
    !reset.active && reset.displayedWeapon == lg::Weapon::Revolver &&
      reset.outgoingWeapon == lg::Weapon::Revolver &&
      continuous.observeAuthoritativeFire(lg::Weapon::Revolver, 102U),
    "map, seek, death, or slot reset should clear old weapon and fire history"
  );
  continuous.reset();
  const auto sameMapReload = continuous.update(lg::Weapon::Railgun, 0.0F);
  failures += expect(
    !sameMapReload.active && sameMapReload.displayedWeapon == lg::Weapon::Railgun,
    "an explicit same-map reload reset should initialize directly from authority"
  );
  const auto disabled = continuous.update(lg::Weapon::Revolver, 0.0F, false);
  failures += expect(
    !disabled.active && disabled.displayedWeapon == lg::Weapon::Revolver,
    "disabled animation should preserve instant weapon changes"
  );

  return failures == 0 ? 0 : 1;
}
