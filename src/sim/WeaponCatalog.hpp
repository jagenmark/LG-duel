#pragma once

#include "sim/UserCommand.hpp"

#include <array>
#include <optional>
#include <string_view>

namespace lg {

constexpr std::array<Weapon, 9> kWeaponSlotOrder = {{
  Weapon::MachineGun,
  Weapon::Shotgun,
  Weapon::GrenadeLauncher,
  Weapon::RocketLauncher,
  Weapon::LightningGun,
  Weapon::Railgun,
  Weapon::PlasmaGun,
  Weapon::FreezeGun,
  Weapon::Revolver,
}};

[[nodiscard]] std::optional<Weapon> parseWeaponToken(std::string_view token);
[[nodiscard]] std::string_view weaponShortName(Weapon weapon);

} // namespace lg
