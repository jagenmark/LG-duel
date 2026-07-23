#pragma once

#include "sim/Combat.hpp"
#include "sim/IcePool.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace lg {

struct BalanceConfig {
  LightningGunTuning lightningGun = {};
  FreezeGunTuning freezeGun = {};
  IcePoolTuning icePool = {};
  HitscanTuning railgun = {};
  float sniperChargeSeconds = 3.3F;
  float sniperMaxDamageMultiplier = 3.0F;
  std::uint32_t railgunCooldownTicks = 188;
  HitscanTuning revolver = {};
  std::uint32_t revolverCooldownTicks = 188;
  MachineGunTuning machineGun = {};
  std::uint32_t machineGunCooldownTicks = 13;
  ShotgunTuning shotgun = {};
  std::uint32_t shotgunCooldownTicks = 125;
  RocketLauncherTuning rocketLauncher = {};
  std::uint32_t rocketLauncherCooldownTicks = 100;
  GrenadeLauncherTuning grenadeLauncher = {};
  PlasmaGunTuning plasmaGun = {};
  WeaponAmmoConfig weaponAmmo = {};
  std::uint32_t weaponPulloutTicks = 20;
  std::uint32_t jumpPadRetriggerCooldownTicks = 25;
  std::int32_t smallHealthPickupAmount = 25;
  std::int32_t largeHealthPickupAmount = 50;
  std::uint32_t smallHealthPickupCooldownTicks = 1250;
  std::uint32_t largeHealthPickupCooldownTicks = 4375;
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
