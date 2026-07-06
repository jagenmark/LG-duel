#include "sim/WeaponCatalog.hpp"

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

} // namespace

int main() {
  int failures = 0;

  failures += expect(
    lg::parseWeaponToken("1") == lg::Weapon::MachineGun,
    "slot 1 should select machine gun"
  );
  failures += expect(
    lg::parseWeaponToken("2") == lg::Weapon::Shotgun,
    "slot 2 should select shotgun"
  );
  failures += expect(
    lg::parseWeaponToken("3") == lg::Weapon::GrenadeLauncher,
    "slot 3 should select grenade launcher"
  );
  failures += expect(
    lg::parseWeaponToken("4") == lg::Weapon::RocketLauncher,
    "slot 4 should select rocket launcher"
  );
  failures += expect(
    lg::parseWeaponToken("5") == lg::Weapon::LightningGun,
    "slot 5 should select lightning gun"
  );
  failures += expect(
    lg::parseWeaponToken("6") == lg::Weapon::Railgun,
    "slot 6 should select railgun"
  );
  failures += expect(
    lg::parseWeaponToken("7") == lg::Weapon::PlasmaGun,
    "slot 7 should select plasma gun"
  );
  failures += expect(
    lg::parseWeaponToken("8") == lg::Weapon::FreezeGun,
    "slot 8 should select freeze gun"
  );
  failures += expect(
    lg::parseWeaponToken("LG") == lg::Weapon::LightningGun &&
      lg::parseWeaponToken("railgun") == lg::Weapon::Railgun &&
      lg::parseWeaponToken("pg") == lg::Weapon::PlasmaGun &&
      lg::parseWeaponToken("freezegun") == lg::Weapon::FreezeGun,
    "short and long weapon aliases should parse case-insensitively"
  );
  failures += expect(
    !lg::parseWeaponToken("9").has_value() &&
      !lg::parseWeaponToken("gauntlet").has_value(),
    "unsupported weapon tokens should be rejected"
  );
  failures += expect(
    lg::weaponShortName(lg::Weapon::GrenadeLauncher) == "gl" &&
      lg::weaponShortName(lg::Weapon::FreezeGun) == "fg",
    "weapon short names should be stable console output"
  );

  return failures == 0 ? 0 : 1;
}
