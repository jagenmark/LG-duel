#pragma once

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
};

constexpr Weapon kLastWeapon = Weapon::PlasmaGun;

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
  bool crouch = false;
  bool planarAim = true;
  Weapon weapon = Weapon::LightningGun;
};

} // namespace lg
