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
  std::uint32_t pulloutTicks = 0;
  std::array<double, 2> beamShotCredit = {{1.0, 1.0}};
  std::array<double, 2> beamDamageCredit = {};
  std::array<double, 2> beamAmmoCredit = {{1.0, 1.0}};
  float sniperAdsFraction = 0.0F;
  float sniperChargeFraction = 0.0F;
  std::uint32_t nextProjectileSequence = 1;
  std::vector<RocketProjectile> projectiles;
  IcePoolArray icePools = {};
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
  Weapon weapon = Weapon::LightningGun;
  int damage = 0;
  float freezeApplied = 0.0F;
  std::uint16_t pellets = 0;
  std::uint32_t scoringHits = 0;
  bool headshot = false;
  bool direct = false;
};

struct WeaponRuntimeTick {
  WeaponFireResult fire = {};
  LightningGunResult beam = {};
  std::vector<WeaponRuntimeHit> hits;
  std::uint32_t acceptedShots = 0;
  std::uint32_t acceptedPellets = 0;
  std::uint32_t hitPellets = 0;
  std::uint32_t acceptedBeamPulses = 0;
  std::uint32_t hitBeamPulses = 0;
  std::uint32_t acceptedProjectileLaunches = 0;
  std::uint32_t damagingProjectileHits = 0;
  std::uint32_t acceptedInstantShots = 0;
  std::uint32_t hitInstantShots = 0;
};

struct WeaponRuntimeConfig {
  BalanceConfig balance = {};
  bool infiniteAmmo = true;
  WeaponRuntimeSwitchingMode switchingMode = WeaponRuntimeSwitchingMode::Crazy;
  std::uint8_t selfDamagePercent = 100;
};

struct WeaponRuntimeSwitchResult {
  Weapon selectedWeapon = Weapon::LightningGun;
  std::uint32_t pulloutTicks = 0;
  bool switched = false;
};

struct WeaponRuntimeGrenadeBounce {
  Vec3 velocity = {};
  bool resting = false;
  float impactSpeed = 0.0F;
};

[[nodiscard]] WeaponRuntimeState makeWeaponRuntimeState(
  const WeaponRuntimeConfig& config,
  Weapon selectedWeapon = Weapon::LightningGun
);

[[nodiscard]] bool canSwitchWeaponRuntime(
  WeaponRuntimeSwitchingMode switchingMode,
  std::uint32_t currentWeaponCooldownTicks
);
[[nodiscard]] std::uint32_t weaponRuntimeSwitchBlockingCooldown(
  Weapon weapon,
  std::uint32_t cooldownTicks
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

[[nodiscard]] std::uint32_t weaponRuntimeCooldownTicks(
  Weapon weapon,
  const BalanceConfig& balance
);
[[nodiscard]] WeaponRuntimeSwitchResult requestWeaponRuntimeSwitch(
  Weapon selectedWeapon,
  Weapon requestedWeapon,
  WeaponRuntimeSwitchingMode switchingMode,
  std::uint32_t selectedWeaponCooldownTicks,
  std::uint32_t currentPulloutTicks,
  std::uint32_t pulloutDurationTicks
);
[[nodiscard]] bool canFireWeaponRuntime(
  WeaponRuntimeSwitchingMode switchingMode,
  std::uint32_t pulloutTicks,
  const WeaponAmmoArray& ammo,
  Weapon weapon,
  bool infiniteAmmo
);
void consumeWeaponRuntimeBeamAmmo(
  WeaponAmmoArray& ammo,
  Weapon weapon,
  bool infiniteAmmo,
  double& ammoCredit,
  float fireHz,
  float fixedDt
);
[[nodiscard]] RocketProjectile makeWeaponRuntimeProjectile(
  std::uint8_t owner,
  std::uint32_t sequence,
  Weapon weapon,
  const PlayerState& attacker,
  const UserCommand& command,
  const BalanceConfig& balance
);
[[nodiscard]] std::uint32_t weaponRuntimeProjectileMaxLifetime(
  Weapon weapon,
  const BalanceConfig& balance
);
[[nodiscard]] WeaponRuntimeGrenadeBounce bounceWeaponRuntimeGrenade(
  Vec3 velocity,
  Vec3 normal,
  const GrenadeLauncherTuning& tuning
);
[[nodiscard]] float weaponRuntimePlayerCylinderDistance(
  Vec3 point,
  const PlayerState& player
);
[[nodiscard]] Vec3 weaponRuntimeProjectileDirectAabbHalfExtents(
  Weapon weapon,
  const PlayerState& target,
  const BalanceConfig& balance
);
[[nodiscard]] bool weaponRuntimePointInsidePlayerDirectAabb(
  Vec3 point,
  const PlayerState& player,
  Vec3 halfExtents
);
[[nodiscard]] bool weaponRuntimeSplashCanReachPlayer(
  const Arena& arena,
  Vec3 explosionPosition,
  const PlayerState& player
);
void decayWeaponRuntimeIcePools(IcePoolArray& pools, float fixedDt);
void growWeaponRuntimeIcePool(
  IcePoolArray& pools,
  Vec3 center,
  Vec3 normal,
  const IcePoolTuning& tuning,
  float fixedDt
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
  PlayerState& attacker,
  const UserCommand& command,
  const Arena& arena,
  std::span<const WeaponRuntimeTarget> targets,
  float fixedDt
);

} // namespace lg
