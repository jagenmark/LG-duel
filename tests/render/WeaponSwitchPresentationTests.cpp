#include "render/WeaponSwitchPresentation.hpp"

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

} // namespace

int main() {
  int failures = 0;
  lg::WeaponSwitchPresentationController controller;
  auto output = controller.update(lg::Weapon::LightningGun, 0.0F);
  failures += expect(
    !output.active && output.displayedWeapon == lg::Weapon::LightningGun,
    "first observation should establish the rest weapon without animation"
  );
  output = controller.update(lg::Weapon::LightningGun, 0.03F);
  failures += expect(!output.active, "unchanged observations must not restart a switch");

  output = controller.update(lg::Weapon::Railgun, 0.04F);
  failures += expect(
    output.active && !output.incomingHalf &&
      output.displayedWeapon == lg::Weapon::LightningGun && output.lift > 0.0F,
    "the outgoing weapon should rise before the apex"
  );
  output = controller.update(lg::Weapon::Railgun, 0.05F);
  failures += expect(
    output.incomingHalf && output.displayedWeapon == lg::Weapon::Railgun,
    "the incoming weapon should appear after the apex"
  );
  const float pausedLift = output.lift;
  output = controller.update(lg::Weapon::Railgun, 0.0F);
  failures += expect(
    near(output.lift, pausedLift), "zero delta must pause the explicit timeline"
  );

  lg::WeaponSwitchPresentationController thirtyHz;
  lg::WeaponSwitchPresentationController oneTwentyHz;
  (void)thirtyHz.update(lg::Weapon::LightningGun, 0.0F);
  (void)oneTwentyHz.update(lg::Weapon::LightningGun, 0.0F);
  for (int index = 0; index < 3; ++index) {
    (void)thirtyHz.update(lg::Weapon::RocketLauncher, 1.0F / 30.0F);
  }
  for (int index = 0; index < 12; ++index) {
    (void)oneTwentyHz.update(lg::Weapon::RocketLauncher, 1.0F / 120.0F);
  }
  const auto thirtyResult = thirtyHz.update(lg::Weapon::RocketLauncher, 0.0F);
  const auto fastResult = oneTwentyHz.update(lg::Weapon::RocketLauncher, 0.0F);
  failures += expect(
    thirtyResult.displayedWeapon == fastResult.displayedWeapon &&
      near(thirtyResult.lift, fastResult.lift),
    "equivalent explicit elapsed time should have frame-rate-independent output"
  );

  lg::WeaponSwitchPresentationController rapid;
  (void)rapid.update(lg::Weapon::MachineGun, 0.0F);
  (void)rapid.update(lg::Weapon::Shotgun, 0.02F);
  const auto retargeted = rapid.update(lg::Weapon::PlasmaGun, 0.0F);
  failures += expect(
    retargeted.outgoingWeapon == lg::Weapon::MachineGun &&
      retargeted.incomingWeapon == lg::Weapon::PlasmaGun,
    "rapid retargeting should restart from the currently displayed weapon"
  );
  rapid.observeAuthoritativeFire(lg::Weapon::PlasmaGun);
  const auto earlyFire = rapid.update(lg::Weapon::PlasmaGun, 0.0F);
  failures += expect(
    earlyFire.incomingHalf && earlyFire.displayedWeapon == lg::Weapon::PlasmaGun,
    "incoming authoritative fire should take visible priority"
  );

  rapid.reset();
  const auto reset = rapid.update(lg::Weapon::FreezeGun, 0.0F);
  failures += expect(
    !reset.active && reset.displayedWeapon == lg::Weapon::FreezeGun &&
      reset.outgoingWeapon == lg::Weapon::FreezeGun,
    "reset should remove stale outgoing state for timeline changes and slot reuse"
  );
  const auto disabled = rapid.update(lg::Weapon::Revolver, 0.0F, false);
  failures += expect(
    !disabled.active && disabled.displayedWeapon == lg::Weapon::Revolver,
    "disabled animation should preserve instant weapon changes"
  );

  const auto pure = lg::sampleWeaponSwitchPresentation(
    lg::Weapon::Shotgun,
    lg::Weapon::Railgun,
    0.50F,
    true
  );
  failures += expect(
    pure.displayedWeapon == lg::Weapon::Railgun && near(pure.lift, 1.0F),
    "pure apex sampling should swap exactly once at the raised region"
  );
  return failures == 0 ? 0 : 1;
}
