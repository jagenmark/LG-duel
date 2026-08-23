#include "sim/WeaponRuntime.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace lg {
namespace {

[[nodiscard]] bool raySphere(
  Vec3 origin,
  Vec3 direction,
  Vec3 center,
  float radius,
  float maximumDistance,
  float& distance
) {
  const Vec3 offset = origin - center;
  const float b = dot(offset, direction);
  const float c = dot(offset, offset) - radius * radius;
  const float discriminant = b * b - c;
  if (discriminant < 0.0F) return false;
  const float root = std::sqrt(discriminant);
  const float near = -b - root;
  const float far = -b + root;
  const float candidate = near >= 0.0F ? near : far;
  if (candidate < 0.0F || candidate > maximumDistance) return false;
  distance = candidate;
  return true;
}

[[nodiscard]] bool traceTarget(
  Vec3 origin,
  Vec3 direction,
  const WeaponRuntimeTarget& target,
  float maximumDistance,
  float& distance,
  bool& headshot
) {
  headshot = false;
  if (!target.active) return false;
  if (target.shape == WeaponRuntimeTargetShape::Sphere) {
    return raySphere(origin, direction, target.center, target.radius, maximumDistance, distance);
  }
  if (!tracePlayerCylinder(origin, direction, target.player, maximumDistance, distance)) {
    return false;
  }
  float headDistance = 0.0F;
  headshot = tracePlayerHeadHitbox(origin, direction, target.player, maximumDistance, headDistance);
  return true;
}

[[nodiscard]] std::size_t nearestTarget(
  Vec3 origin,
  Vec3 direction,
  std::span<const WeaponRuntimeTarget> targets,
  float maximumDistance,
  float& hitDistance,
  bool& headshot
) {
  std::size_t result = targets.size();
  float bestDistance = maximumDistance;
  for (std::size_t index = 0; index < targets.size(); ++index) {
    float candidateDistance = 0.0F;
    bool candidateHeadshot = false;
    if (!traceTarget(
          origin, direction, targets[index], bestDistance,
          candidateDistance, candidateHeadshot
        )) {
      continue;
    }
    result = index;
    bestDistance = candidateDistance;
    headshot = candidateHeadshot;
  }
  hitDistance = bestDistance;
  return result;
}

[[nodiscard]] float eyeHeightFor(Weapon weapon, const BalanceConfig& balance) {
  switch (weapon) {
  case Weapon::LightningGun: return balance.lightningGun.eyeHeight;
  case Weapon::FreezeGun: return balance.freezeGun.eyeHeight;
  case Weapon::MachineGun: return balance.machineGun.eyeHeight;
  case Weapon::Shotgun: return balance.shotgun.eyeHeight;
  case Weapon::Revolver: return balance.revolver.eyeHeight;
  case Weapon::RocketLauncher: return balance.rocketLauncher.eyeHeight;
  case Weapon::GrenadeLauncher: return balance.grenadeLauncher.eyeHeight;
  case Weapon::PlasmaGun: return balance.plasmaGun.eyeHeight;
  case Weapon::Railgun: return balance.railgun.eyeHeight;
  }
  return 0.65F;
}

[[nodiscard]] float rangeFor(Weapon weapon, const BalanceConfig& balance) {
  switch (weapon) {
  case Weapon::LightningGun: return balance.lightningGun.range;
  case Weapon::FreezeGun: return balance.freezeGun.range;
  case Weapon::MachineGun: return balance.machineGun.range;
  case Weapon::Shotgun: return balance.shotgun.range;
  case Weapon::Revolver: return balance.revolver.range;
  case Weapon::Railgun: return balance.railgun.range;
  case Weapon::RocketLauncher:
  case Weapon::GrenadeLauncher:
  case Weapon::PlasmaGun: return 100.0F;
  }
  return 100.0F;
}

[[nodiscard]] int damageFor(Weapon weapon, const BalanceConfig& balance) {
  switch (weapon) {
  case Weapon::Railgun: return balance.railgun.damage;
  case Weapon::Revolver: return balance.revolver.damage;
  case Weapon::MachineGun: return balance.machineGun.damage;
  case Weapon::Shotgun: return balance.shotgun.damagePerPellet;
  case Weapon::RocketLauncher: return balance.rocketLauncher.directDamage;
  case Weapon::GrenadeLauncher: return balance.grenadeLauncher.directDamage;
  case Weapon::PlasmaGun: return balance.plasmaGun.damage;
  case Weapon::LightningGun:
  case Weapon::FreezeGun: return 0;
  }
  return 0;
}

[[nodiscard]] std::uint32_t cooldownFor(Weapon weapon, const BalanceConfig& balance) {
  switch (weapon) {
  case Weapon::Railgun: return balance.railgunCooldownTicks;
  case Weapon::Revolver: return balance.revolverCooldownTicks;
  case Weapon::MachineGun: return balance.machineGunCooldownTicks;
  case Weapon::Shotgun: return balance.shotgunCooldownTicks;
  case Weapon::RocketLauncher: return balance.rocketLauncherCooldownTicks;
  case Weapon::GrenadeLauncher: return balance.grenadeLauncher.cooldownTicks;
  case Weapon::PlasmaGun: return balance.plasmaGun.cooldownTicks;
  case Weapon::LightningGun:
  case Weapon::FreezeGun: return 0;
  }
  return 0;
}

[[nodiscard]] bool isBeam(Weapon weapon) {
  return weapon == Weapon::LightningGun || weapon == Weapon::FreezeGun;
}

void addHit(
  WeaponRuntimeTick& tick,
  const WeaponRuntimeTarget& target,
  int damage,
  bool headshot,
  std::uint16_t pellets = 0
) {
  for (WeaponRuntimeHit& hit : tick.hits) {
    if (hit.targetId == target.id) {
      hit.damage += damage;
      hit.pellets = static_cast<std::uint16_t>(hit.pellets + pellets);
      hit.headshot = hit.headshot || headshot;
      return;
    }
  }
  tick.hits.push_back({target.id, damage, pellets, headshot});
}

[[nodiscard]] bool hasAmmo(
  const WeaponRuntimeState& state,
  const WeaponRuntimeConfig& config,
  Weapon weapon
) {
  return hasWeaponRuntimeAmmo(state.ammo, weapon, config.infiniteAmmo);
}

void consumeAmmo(
  WeaponRuntimeState& state,
  const WeaponRuntimeConfig& config,
  Weapon weapon
) {
  (void)consumeWeaponRuntimeAmmo(state.ammo, weapon, config.infiniteAmmo);
}

void addProjectile(
  WeaponRuntimeState& state,
  const BalanceConfig& balance,
  const PlayerState& attacker,
  const UserCommand& command,
  Weapon weapon
) {
  const float eyeHeight = eyeHeightFor(weapon, balance);
  float speed = 0.0F;
  float radius = 0.0F;
  if (weapon == Weapon::RocketLauncher) {
    speed = balance.rocketLauncher.speed;
    radius = balance.rocketLauncher.radius;
  } else if (weapon == Weapon::GrenadeLauncher) {
    speed = balance.grenadeLauncher.speed;
    radius = balance.grenadeLauncher.projectileRadius;
  } else {
    speed = balance.plasmaGun.speed;
    radius = balance.plasmaGun.radius;
  }
  RocketProjectile projectile;
  projectile.active = true;
  projectile.sequence = state.nextProjectileSequence++;
  projectile.weapon = weapon;
  projectile.position = weaponMuzzlePosition(attacker, eyeHeight);
  projectile.previousPosition = projectile.position;
  projectile.velocity = cameraForward(command.viewYawRadians, command.viewPitchRadians) * speed;
  projectile.projectileRadius = radius;
  state.projectiles.push_back(projectile);
}

void advanceProjectiles(
  WeaponRuntimeState& state,
  const WeaponRuntimeConfig& config,
  const Arena& arena,
  std::span<const WeaponRuntimeTarget> targets,
  float fixedDt,
  WeaponRuntimeTick& result
) {
  for (RocketProjectile& projectile : state.projectiles) {
    if (!projectile.active) continue;
    projectile.previousPosition = projectile.position;
    if (projectile.weapon == Weapon::GrenadeLauncher) {
      projectile.velocity.z -= config.balance.grenadeLauncher.gravity * fixedDt;
    }
    const Vec3 travel = projectile.velocity * fixedDt;
    const float distance = length(travel);
    const Vec3 direction = distance > 0.00001F ? travel / distance : Vec3{};
    const WorldTrace world = traceWorld(arena, projectile.position, direction, distance);
    float hitDistance = world.distance;
    const float maximumTargetDistance = hitDistance;
    bool headshot = false;
    const std::size_t targetIndex = nearestTarget(
      projectile.position, direction, targets, maximumTargetDistance, hitDistance, headshot
    );
    if (targetIndex < targets.size()) {
      const int damage = damageFor(projectile.weapon, config.balance);
      addHit(result, targets[targetIndex], damage, headshot);
      ++result.damagingProjectileHits;
      projectile.active = false;
      continue;
    }
    if (world.hit) {
      projectile.active = false;
      continue;
    }
    projectile.position += travel;
    ++projectile.ageTicks;
    const std::uint32_t maxLife = projectile.weapon == Weapon::PlasmaGun
      ? config.balance.plasmaGun.maxLifetimeTicks
      : projectile.weapon == Weapon::RocketLauncher
        ? config.balance.rocketLauncher.maxLifetimeTicks
        : config.balance.grenadeLauncher.fuseTicks;
    if (projectile.ageTicks >= maxLife) projectile.active = false;
  }
  state.projectiles.erase(
    std::remove_if(state.projectiles.begin(), state.projectiles.end(),
      [](const RocketProjectile& projectile) { return !projectile.active; }),
    state.projectiles.end()
  );
}

} // namespace

WeaponRuntimeState makeWeaponRuntimeState(
  const WeaponRuntimeConfig& config,
  Weapon selectedWeapon
) {
  WeaponRuntimeState state;
  state.selectedWeapon = selectedWeapon;
  state.ammo = config.balance.weaponAmmo.spawnAmmo;
  return state;
}

bool canSwitchWeaponRuntime(
  WeaponRuntimeSwitchingMode switchingMode,
  std::uint32_t currentWeaponCooldownTicks
) {
  return switchingMode == WeaponRuntimeSwitchingMode::Crazy ||
    currentWeaponCooldownTicks == 0U;
}

bool hasWeaponRuntimeAmmo(
  const WeaponAmmoArray& ammo,
  Weapon weapon,
  bool infiniteAmmo
) {
  return infiniteAmmo || ammo[weaponIndex(weapon)] > 0;
}

bool consumeWeaponRuntimeAmmo(
  WeaponAmmoArray& ammo,
  Weapon weapon,
  bool infiniteAmmo
) {
  if (infiniteAmmo) return true;
  std::int32_t& amount = ammo[weaponIndex(weapon)];
  if (amount <= 0) return false;
  --amount;
  return true;
}

void advanceWeaponRuntimeCooldowns(std::span<std::uint32_t> cooldowns) {
  for (std::uint32_t& cooldown : cooldowns) {
    if (cooldown > 0U) --cooldown;
  }
}

WeaponRuntimeTick tickWeaponRuntime(
  WeaponRuntimeState& state,
  const WeaponRuntimeConfig& config,
  const PlayerState& attacker,
  const UserCommand& command,
  const Arena& arena,
  std::span<const WeaponRuntimeTarget> targets,
  float fixedDt
) {
  WeaponRuntimeTick result;
  advanceWeaponRuntimeCooldowns(state.cooldownTicks);
  if (command.weapon != state.selectedWeapon && canSwitchWeaponRuntime(
        config.switchingMode,
        state.cooldownTicks[weaponIndex(state.selectedWeapon)]
      )) {
    state.selectedWeapon = command.weapon;
  }
  const Weapon weapon = state.selectedWeapon;
  result.fire.weapon = weapon;
  result.fire.visualSeed = command.sequence;
  result.fire.start = weaponMuzzlePosition(attacker, eyeHeightFor(weapon, config.balance));
  const Vec3 direction = cameraForward(command.viewYawRadians, command.viewPitchRadians);
  const WorldTrace world = traceWorld(
    arena, result.fire.start, direction, rangeFor(weapon, config.balance)
  );
  result.fire.end = world.end;
  if (!command.attack || attacker.health <= 0 || !hasAmmo(state, config, weapon)) {
    advanceProjectiles(state, config, arena, targets, fixedDt, result);
    return result;
  }

  if (isBeam(weapon)) {
    const std::size_t beamIndex = weapon == Weapon::LightningGun ? 0U : 1U;
    const float fireHz = weapon == Weapon::LightningGun
      ? config.balance.lightningGun.fireHz
      : config.balance.freezeGun.fireHz;
    state.beamShotCredit[beamIndex] = std::min(
      state.beamShotCredit[beamIndex] + std::max(1.0F, fireHz) * fixedDt,
      static_cast<double>(std::max(1.0F, fireHz))
    );
    result.fire.fired = true;
    float hitDistance = world.distance;
    const float maximumTargetDistance = hitDistance;
    bool headshot = false;
    const std::size_t targetIndex = nearestTarget(
      result.fire.start, direction, targets, maximumTargetDistance, hitDistance, headshot
    );
    if (targetIndex < targets.size()) {
      result.fire.hit = true;
      result.fire.end = result.fire.start + direction * hitDistance;
      result.fire.headshot = headshot;
    }
    if (state.beamShotCredit[beamIndex] >= 1.0) {
      state.beamShotCredit[beamIndex] -= 1.0;
      ++result.acceptedBeamPulses;
      if (targetIndex < targets.size()) {
        const float damagePerSecond = weapon == Weapon::LightningGun
          ? config.balance.lightningGun.damagePerSecond
          : config.balance.freezeGun.damagePerSecond;
        state.beamDamageCredit[beamIndex] += damagePerSecond / std::max(1.0F, fireHz);
        const int damage = static_cast<int>(std::floor(state.beamDamageCredit[beamIndex]));
        state.beamDamageCredit[beamIndex] -= damage;
        addHit(result, targets[targetIndex], damage, headshot);
        ++result.hitBeamPulses;
      }
    }
    advanceProjectiles(state, config, arena, targets, fixedDt, result);
    return result;
  }

  if (weapon == Weapon::RocketLauncher || weapon == Weapon::GrenadeLauncher ||
      weapon == Weapon::PlasmaGun) {
    if (state.cooldownTicks[weaponIndex(weapon)] == 0U) {
      result.fire.fired = true;
      ++result.acceptedShots;
      ++result.acceptedProjectileLaunches;
      state.cooldownTicks[weaponIndex(weapon)] = cooldownFor(weapon, config.balance);
      consumeAmmo(state, config, weapon);
      addProjectile(state, config.balance, attacker, command, weapon);
    }
    advanceProjectiles(state, config, arena, targets, fixedDt, result);
    return result;
  }

  if (state.cooldownTicks[weaponIndex(weapon)] != 0U) {
    advanceProjectiles(state, config, arena, targets, fixedDt, result);
    return result;
  }
  result.fire.fired = true;
  ++result.acceptedShots;
  state.cooldownTicks[weaponIndex(weapon)] = cooldownFor(weapon, config.balance);
  consumeAmmo(state, config, weapon);

  if (weapon == Weapon::Shotgun) {
    const std::uint8_t pelletCount = config.balance.shotgun.pelletCount;
    result.acceptedPellets = pelletCount;
    result.fire.pelletCount = pelletCount;
    const Vec3 forward = direction;
    Vec3 right = normalize(Vec3{-forward.y, forward.x, 0.0F});
    if (length(right) <= 0.00001F) right = {1.0F, 0.0F, 0.0F};
    const Vec3 up = normalize(Vec3{
      right.y * forward.z - right.z * forward.y,
      right.z * forward.x - right.x * forward.z,
      right.x * forward.y - right.y * forward.x,
    });
    for (std::uint8_t pellet = 0; pellet < pelletCount; ++pellet) {
      const Vec3 pelletDirection = shotgunPelletDirection(
        forward, right, up, config.balance.shotgun.spreadRadians, pellet, pelletCount
      );
      const WorldTrace pelletWorld = traceWorld(
        arena, result.fire.start, pelletDirection, config.balance.shotgun.range
      );
      float hitDistance = pelletWorld.distance;
      const float maximumTargetDistance = hitDistance;
      bool headshot = false;
      const std::size_t targetIndex = nearestTarget(
        result.fire.start, pelletDirection, targets, maximumTargetDistance, hitDistance, headshot
      );
      if (targetIndex >= targets.size()) continue;
      ++result.hitPellets;
      ++result.fire.pelletHitCount;
      result.fire.hit = true;
      addHit(result, targets[targetIndex], config.balance.shotgun.damagePerPellet, headshot, 1U);
    }
  } else {
    float hitDistance = world.distance;
    const float maximumTargetDistance = hitDistance;
    bool headshot = false;
    const std::size_t targetIndex = nearestTarget(
      result.fire.start, direction, targets, maximumTargetDistance, hitDistance, headshot
    );
    if (targetIndex < targets.size()) {
      const int damage = damageFor(weapon, config.balance);
      result.fire.hit = true;
      result.fire.headshot = headshot;
      result.fire.end = result.fire.start + direction * hitDistance;
      result.fire.damageApplied = damage;
      addHit(result, targets[targetIndex], damage, headshot);
    }
  }
  advanceProjectiles(state, config, arena, targets, fixedDt, result);
  return result;
}

} // namespace lg
