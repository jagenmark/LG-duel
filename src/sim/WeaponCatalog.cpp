#include "sim/WeaponCatalog.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace lg {

std::optional<Weapon> parseWeaponToken(std::string_view token) {
  std::string value(token);
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });

  if (value.size() == 1 && value[0] >= '1' && value[0] <= '9') {
    return kWeaponSlotOrder[static_cast<std::size_t>(value[0] - '1')];
  }
  if (value == "mg" || value == "machine" || value == "machinegun") {
    return Weapon::MachineGun;
  }
  if (value == "sg" || value == "shotgun") {
    return Weapon::Shotgun;
  }
  if (value == "gl" || value == "grenade" || value == "grenadelauncher") {
    return Weapon::GrenadeLauncher;
  }
  if (value == "rl" || value == "rocket" || value == "rocketlauncher") {
    return Weapon::RocketLauncher;
  }
  if (value == "lg" || value == "lightning" || value == "lightninggun") {
    return Weapon::LightningGun;
  }
  if (value == "rg" || value == "rail" || value == "railgun") {
    return Weapon::Railgun;
  }
  if (value == "pg" || value == "plasma" || value == "plasmagun") {
    return Weapon::PlasmaGun;
  }
  if (value == "fg" || value == "freeze" || value == "freezegun") {
    return Weapon::FreezeGun;
  }
  if (value == "re" || value == "revolver") {
    return Weapon::Revolver;
  }
  return std::nullopt;
}

std::string_view weaponShortName(Weapon weapon) {
  switch (weapon) {
  case Weapon::MachineGun:
    return "mg";
  case Weapon::Shotgun:
    return "sg";
  case Weapon::GrenadeLauncher:
    return "gl";
  case Weapon::RocketLauncher:
    return "rl";
  case Weapon::LightningGun:
    return "lg";
  case Weapon::Railgun:
    return "rg";
  case Weapon::PlasmaGun:
    return "pg";
  case Weapon::FreezeGun:
    return "fg";
  case Weapon::Revolver:
    return "re";
  }
  return "??";
}

} // namespace lg
