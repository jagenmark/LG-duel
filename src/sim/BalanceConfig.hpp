#pragma once

#include "sim/Combat.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace lg {

struct BalanceConfig {
  LightningGunTuning lightningGun = {};
  HitscanTuning railgun = {};
  std::uint32_t railgunCooldownTicks = 188;
  MachineGunTuning machineGun = {};
  std::uint32_t machineGunCooldownTicks = 13;
  ShotgunTuning shotgun = {};
  std::uint32_t shotgunCooldownTicks = 125;
  RocketLauncherTuning rocketLauncher = {};
  std::uint32_t rocketLauncherCooldownTicks = 100;
  GrenadeLauncherTuning grenadeLauncher = {};
  PlasmaGunTuning plasmaGun = {};
  std::uint32_t weaponPulloutTicks = 20;
};

struct BalanceConfigLoadResult {
  BalanceConfig config = {};
  bool ok = false;
  std::string error;
};

[[nodiscard]] BalanceConfigLoadResult loadBalanceConfigFromText(
  std::string_view text
);

[[nodiscard]] BalanceConfigLoadResult loadBalanceConfigFromFile(
  const std::string& path
);

} // namespace lg
