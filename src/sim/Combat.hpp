#pragma once

#include "shared/Math.hpp"
#include "sim/Arena.hpp"
#include "sim/PlayerState.hpp"
#include "sim/UserCommand.hpp"

#include <cstddef>
#include <cstdint>

namespace lg {

inline constexpr std::size_t kMaxRocketProjectiles = 8;
inline constexpr std::uint8_t kShotgunPelletCount = 20;

struct LightningGunTuning {
  float range = 18.0F;
  float damagePerSecond = 120.0F;
  float fireHz = 20.0F;
  float eyeHeight = 0.65F;
  float knockbackPerSecond = 22.0F;
};

struct LightningGunState {
  double fractionalDamage = 0.0;
  double shotCredit = 1.0;
};

struct LightningGunResult {
  Vec3 start = {};
  Vec3 end = {};
  bool active = false;
  bool hit = false;
  std::uint8_t targetPlayerIndex = 255;
  int damageApplied = 0;
  Vec3 knockbackImpulse = {};
  std::uint32_t requestedRewindTicks = 0;
  std::uint32_t appliedRewindTicks = 0;
  bool rewindClamped = false;
  bool hasRewindDebug = false;
  std::uint32_t rewindTargetTick = 0;
  Vec3 currentTargetPosition = {};
  Vec3 rewoundTargetPosition = {};
  CollisionBounds currentTargetBounds = {};
  CollisionBounds rewoundTargetBounds = {};
};

struct HitscanTuning {
  float range = 100.0F;
  int damage = 80;
  float eyeHeight = 0.65F;
  float knockback = 20.0F;
};

struct MachineGunTuning {
  float range = 100.0F;
  int damage = 5;
  float eyeHeight = 0.65F;
  float knockback = 0.11F;
  float spreadRadians = 0.0F;
};

struct WeaponFireResult {
  Vec3 start = {};
  Vec3 end = {};
  bool fired = false;
  bool hit = false;
  Weapon weapon = Weapon::LightningGun;
  int damageApplied = 0;
  Vec3 knockbackImpulse = {};
  std::uint8_t pelletCount = 0;
  std::uint8_t pelletHitCount = 0;
  std::uint32_t visualSeed = 0;
};

struct ShotgunTuning {
  float range = 18.0F;
  std::uint8_t pelletCount = kShotgunPelletCount;
  int damagePerPellet = 5;
  float spreadRadians = 0.0872665F;
  float eyeHeight = 0.65F;
  float knockback = 22.0F;
};

struct RocketLauncherTuning {
  float speed = 22.5F;
  float radius = 3.0F;
  int directDamage = 100;
  int splashDamage = 100;
  float knockback = 22.0F;
  float eyeHeight = 0.65F;
  std::uint32_t maxLifetimeTicks = 500;
};

struct GrenadeLauncherTuning {
  float speed = 16.0F;
  float verticalBoost = 5.0F;
  float gravity = 9.8F;
  float bounceDamping = 0.65F;
  float restSpeed = 1.5F;
  float bounceSoundMinSpeed = 1.2F;
  float projectileRadius = 0.15F;
  float projectileHitboxRadius = 0.0F;
  float radius = 3.0F;
  int directDamage = 100;
  int splashDamage = 100;
  float knockback = 22.0F;
  float eyeHeight = 0.65F;
  std::uint32_t fuseTicks = 313;
  std::uint32_t cooldownTicks = 100;
};

struct PlasmaGunTuning {
  float speed = 50.0F;
  float radius = 0.45F;
  int damage = 20;
  float knockback = 2.2F;
  float eyeHeight = 0.65F;
  std::uint32_t maxLifetimeTicks = 125;
  std::uint32_t cooldownTicks = 13;
};

struct WeaponDamageTuning {
  int shotgunDamagePerPellet = 5;
  int machineGunDamage = 5;
  int lightningGunDamage = 120;
  int railgunDamage = 80;
  int rocketLauncherDamage = 100;
  int plasmaGunDamage = 20;
};

struct RocketProjectile {
  bool active = false;
  std::uint8_t owner = 0;
  Weapon weapon = Weapon::RocketLauncher;
  Vec3 position = {};
  Vec3 previousPosition = {};
  float projectileRadius = 0.0F;
  float projectileHitboxRadius = 0.0F;
  bool ownerCollisionArmed = false;
  bool resting = false;
  Vec3 velocity = {};
  std::uint32_t ageTicks = 0;
};

struct RocketProjectileSnapshot {
  bool active = false;
  std::uint8_t owner = 0;
  Weapon weapon = Weapon::RocketLauncher;
  Vec3 position = {};
  Vec3 velocity = {};
  float radius = 0.0F;
};

struct RocketExplosionResult {
  Vec3 position = {};
  float radius = 0.0F;
  int ownerDamageApplied = 0;
  int opponentDamageApplied = 0;
  bool active = false;
  Weapon weapon = Weapon::RocketLauncher;
};

struct WorldTrace {
  Vec3 start = {};
  Vec3 end = {};
  float distance = 0.0F;
};

[[nodiscard]] Vec3 weaponMuzzlePosition(
  const PlayerState& attacker,
  float eyeHeight
);

[[nodiscard]] WorldTrace traceWorld(
  const Arena& arena,
  Vec3 origin,
  Vec3 direction,
  float maxDistance
);

[[nodiscard]] Vec3 shotgunPelletDirection(
  Vec3 forward,
  Vec3 right,
  Vec3 up,
  float spreadRadians,
  std::uint8_t pelletIndex
);

[[nodiscard]] bool tracePlayerCylinder(
  Vec3 origin,
  Vec3 direction,
  const PlayerState& target,
  float maxDistance,
  float& hitDistance
);

[[nodiscard]] LightningGunResult simulateLightningGun(
  const PlayerState& attacker,
  PlayerState& target,
  const UserCommand& command,
  const Arena& arena,
  const LightningGunTuning& tuning,
  LightningGunState& state,
  float fixedDt
);

[[nodiscard]] WeaponFireResult simulateRailgun(
  const PlayerState& attacker,
  PlayerState& target,
  const UserCommand& command,
  const Arena& arena,
  const HitscanTuning& tuning
);

[[nodiscard]] WeaponFireResult simulateMachineGun(
  const PlayerState& attacker,
  PlayerState& target,
  const UserCommand& command,
  const Arena& arena,
  const MachineGunTuning& tuning
);

[[nodiscard]] WeaponFireResult simulateShotgun(
  const PlayerState& attacker,
  PlayerState& target,
  const UserCommand& command,
  const Arena& arena,
  const ShotgunTuning& tuning
);

} // namespace lg
