#pragma once

#include "sim/BalanceConfig.hpp"
#include "sim/Combat.hpp"
#include "sim/UserCommand.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace lg {

enum class WeaponRuntimeSwitchingMode : std::uint8_t {
  Ql,
  Cpma,
  Crazy,
};

// Shared, transport-free weapon timing state. ServerGame keeps authority and
// publishes its own events; local modes consume the same acceptance rules.
struct WeaponRuntimeState {
  Weapon selectedWeapon = Weapon::LightningGun;
  WeaponAmmoArray ammo = {};
  std::array<std::uint32_t, kWeaponCount> cooldownTicks = {};
  std::array<double, 2> beamShotCredit = {{1.0, 1.0}};
  std::array<double, 2> beamDamageCredit = {};
  std::uint32_t nextProjectileSequence = 1;
  std::vector<RocketProjectile> projectiles;
};

enum class WeaponRuntimeTargetShape : std::uint8_t {
  Sphere,
  Player,
};

struct WeaponRuntimeTarget {
  std::uint32_t id = 0;
  WeaponRuntimeTargetShape shape = WeaponRuntimeTargetShape::Sphere;
  Vec3 center = {};
  float radius = 0.35F;
  PlayerState player = {};
  bool active = false;
};

struct WeaponRuntimeHit {
  std::uint32_t targetId = 0;
  int damage = 0;
  std::uint16_t pellets = 0;
  bool headshot = false;
};

struct WeaponRuntimeTick {
  WeaponFireResult fire = {};
  std::vector<WeaponRuntimeHit> hits;
  std::uint32_t acceptedShots = 0;
  std::uint32_t acceptedPellets = 0;
  std::uint32_t hitPellets = 0;
  std::uint32_t acceptedBeamPulses = 0;
  std::uint32_t hitBeamPulses = 0;
  std::uint32_t acceptedProjectileLaunches = 0;
  std::uint32_t damagingProjectileHits = 0;
};

struct WeaponRuntimeConfig {
  BalanceConfig balance = {};
  bool infiniteAmmo = true;
  WeaponRuntimeSwitchingMode switchingMode = WeaponRuntimeSwitchingMode::Crazy;
};

[[nodiscard]] WeaponRuntimeState makeWeaponRuntimeState(
  const WeaponRuntimeConfig& config,
  Weapon selectedWeapon = Weapon::LightningGun
);

[[nodiscard]] bool canSwitchWeaponRuntime(
  WeaponRuntimeSwitchingMode switchingMode,
  std::uint32_t currentWeaponCooldownTicks
);
[[nodiscard]] bool hasWeaponRuntimeAmmo(
  const WeaponAmmoArray& ammo,
  Weapon weapon,
  bool infiniteAmmo
);
[[nodiscard]] bool consumeWeaponRuntimeAmmo(
  WeaponAmmoArray& ammo,
  Weapon weapon,
  bool infiniteAmmo
);

// The online server and local trainer share this one-tick cooldown rule while
// keeping their distinct authority and damage-policy layers outside it.
void advanceWeaponRuntimeCooldowns(std::span<std::uint32_t> cooldowns);

// Advances cooldowns, accepts switching and firing, and advances local
// projectiles by one shared fixed tick. It does not change targets; callers
// apply the returned hit batch together after this function returns.
[[nodiscard]] WeaponRuntimeTick tickWeaponRuntime(
  WeaponRuntimeState& state,
  const WeaponRuntimeConfig& config,
  const PlayerState& attacker,
  const UserCommand& command,
  const Arena& arena,
  std::span<const WeaponRuntimeTarget> targets,
  float fixedDt
);

} // namespace lg
