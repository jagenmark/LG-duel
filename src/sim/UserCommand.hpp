#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace lg {

enum class Weapon : std::uint8_t {
  LightningGun = 0,
  Railgun = 1,
  RocketLauncher = 2,
  MachineGun = 3,
  Shotgun = 4,
  GrenadeLauncher = 5,
  PlasmaGun = 6,
  FreezeGun = 7,
};

constexpr Weapon kLastWeapon = Weapon::FreezeGun;
inline constexpr std::size_t kWeaponCount = 8;

[[nodiscard]] constexpr std::size_t weaponIndex(Weapon weapon) {
  return static_cast<std::size_t>(weapon);
}

using WeaponAmmoArray = std::array<std::int32_t, kWeaponCount>;

struct UserCommand {
  std::uint32_t sequence = 0;
  std::uint32_t clientTick = 0;

  float viewYawRadians = 0.0F;
  float viewPitchRadians = 0.0F;

  float forwardMove = 0.0F;
  float rightMove = 0.0F;
  float upMove = 0.0F;

  bool attack = false;
  bool jump = false;
  bool dash = false;
  bool crouch = false;
  bool sneak = false;
  bool planarAim = true;
  Weapon weapon = Weapon::LightningGun;
};

} // namespace lg
