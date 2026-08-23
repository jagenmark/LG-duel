#include "sim/WeaponRuntime.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace lg {
namespace {

constexpr float kWeaponRuntimeProjectileEpsilon = 0.0001F;

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

[[nodiscard]] bool traceProjectileTarget(
  Vec3 origin,
  Vec3 direction,
  const RocketProjectile& projectile,
  const BalanceConfig& balance,
  const WeaponRuntimeTarget& target,
  float maximumDistance,
  float& distance
) {
  if (!target.active) return false;
  if (target.shape == WeaponRuntimeTargetShape::Sphere) {
    return raySphere(
      origin,
      direction,
      target.center,
      target.radius + projectile.projectileHitboxRadius,
      maximumDistance,
      distance
    );
  }
  if (
    projectile.weapon == Weapon::RocketLauncher ||
    projectile.weapon == Weapon::PlasmaGun
  ) {
    return tracePlayerProjectileDirectAabb(
      origin,
      direction,
      target.player,
      maximumDistance,
      weaponRuntimeProjectileDirectAabbHalfExtents(
        projectile.weapon,
        target.player,
        balance
      ),
      distance
    );
  }
  PlayerState expanded = target.player;
  expanded.bounds.radius += projectile.projectileHitboxRadius;
  expanded.bounds.halfHeight += projectile.projectileHitboxRadius;
  return tracePlayerCylinder(origin, direction, expanded, maximumDistance, distance);
}

[[nodiscard]] std::size_t nearestProjectileTarget(
  Vec3 origin,
  Vec3 direction,
  const RocketProjectile& projectile,
  const BalanceConfig& balance,
  std::span<const WeaponRuntimeTarget> targets,
  float maximumDistance,
  float& hitDistance
) {
  std::size_t result = targets.size();
  float bestDistance = maximumDistance;
  for (std::size_t index = 0; index < targets.size(); ++index) {
    float candidateDistance = 0.0F;
    if (!traceProjectileTarget(
          origin,
          direction,
          projectile,
          balance,
          targets[index],
          bestDistance,
          candidateDistance
        )) {
      continue;
    }
    result = index;
    bestDistance = candidateDistance;
  }
  hitDistance = bestDistance;
  return result;
}

[[nodiscard]] bool splashCanReachSphere(
  const Arena& arena,
  Vec3 explosionPosition,
  Vec3 center
) {
  const Vec3 segment = explosionPosition - center;
  const float distance = length(segment);
  if (distance <= kWeaponRuntimeProjectileEpsilon) return true;
  const WorldTrace trace = traceWorld(arena, center, segment / distance, distance);
  return trace.distance >= distance - kWeaponRuntimeProjectileEpsilon;
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

[[nodiscard]] float headshotMultiplierFor(
  Weapon weapon,
  const BalanceConfig& balance
) {
  switch (weapon) {
  case Weapon::LightningGun: return balance.lightningGun.headshotMultiplier;
  case Weapon::FreezeGun: return balance.freezeGun.headshotMultiplier;
  case Weapon::Railgun: return balance.railgun.headshotMultiplier;
  case Weapon::Revolver: return balance.revolver.headshotMultiplier;
  case Weapon::MachineGun: return balance.machineGun.headshotMultiplier;
  case Weapon::Shotgun: return balance.shotgun.headshotMultiplier;
  case Weapon::RocketLauncher:
  case Weapon::GrenadeLauncher:
  case Weapon::PlasmaGun: return 1.0F;
  }
  return 1.0F;
}

[[nodiscard]] int headshotDamage(
  int damage,
  bool headshot,
  Weapon weapon,
  const BalanceConfig& balance
) {
  return headshot
    ? std::max(0, static_cast<int>(std::lround(
        static_cast<float>(damage) * headshotMultiplierFor(weapon, balance)
      )))
    : damage;
}

[[nodiscard]] bool isBeam(Weapon weapon) {
  return weapon == Weapon::LightningGun || weapon == Weapon::FreezeGun;
}

void addHit(
  WeaponRuntimeTick& tick,
  const WeaponRuntimeTarget& target,
  Weapon weapon,
  int damage,
  bool headshot,
  float freezeApplied = 0.0F,
  std::uint16_t pellets = 0,
  bool direct = false
) {
  for (WeaponRuntimeHit& hit : tick.hits) {
    if (hit.targetId == target.id) {
      hit.damage += damage;
      hit.freezeApplied += freezeApplied;
      hit.pellets = static_cast<std::uint16_t>(hit.pellets + pellets);
      hit.scoringHits += pellets > 0U ? pellets : 1U;
      hit.headshot = hit.headshot || headshot;
      hit.direct = hit.direct || direct;
      return;
    }
  }
  tick.hits.push_back({
    target.id,
    weapon,
    damage,
    freezeApplied,
    pellets,
    pellets > 0U ? pellets : 1U,
    headshot,
    direct,
  });
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
  state.projectiles.push_back(makeWeaponRuntimeProjectile(
    0U, state.nextProjectileSequence++, weapon, attacker, command, balance
  ));
}

void advanceProjectiles(
  WeaponRuntimeState& state,
  const WeaponRuntimeConfig& config,
  PlayerState& attacker,
  const Arena& arena,
  std::span<const WeaponRuntimeTarget> targets,
  float fixedDt,
  WeaponRuntimeTick& result
) {
  const auto radiusFor = [&config](Weapon weapon) {
    if (weapon == Weapon::GrenadeLauncher) return config.balance.grenadeLauncher.radius;
    if (weapon == Weapon::PlasmaGun) return config.balance.plasmaGun.radius;
    return config.balance.rocketLauncher.radius;
  };
  const auto directDamageFor = [&config](Weapon weapon) {
    if (weapon == Weapon::GrenadeLauncher) return config.balance.grenadeLauncher.directDamage;
    if (weapon == Weapon::PlasmaGun) return config.balance.plasmaGun.damage;
    return config.balance.rocketLauncher.directDamage;
  };
  const auto splashDamageFor = [&config](Weapon weapon) {
    if (weapon == Weapon::GrenadeLauncher) return config.balance.grenadeLauncher.splashDamage;
    if (weapon == Weapon::PlasmaGun) return config.balance.plasmaGun.damage;
    return config.balance.rocketLauncher.splashDamage;
  };
  const auto knockbackFor = [&config](Weapon weapon) {
    if (weapon == Weapon::GrenadeLauncher) return config.balance.grenadeLauncher.knockback;
    if (weapon == Weapon::PlasmaGun) return config.balance.plasmaGun.knockback;
    return config.balance.rocketLauncher.knockback;
  };
  for (RocketProjectile& projectile : state.projectiles) {
    if (!projectile.active) continue;
    projectile.previousPosition = projectile.position;
    const bool grenade = projectile.weapon == Weapon::GrenadeLauncher;
    if (grenade && projectile.resting) {
      ++projectile.ageTicks;
      if (projectile.ageTicks < config.balance.grenadeLauncher.fuseTicks) continue;
    } else if (grenade) {
      projectile.velocity.z -= config.balance.grenadeLauncher.gravity * fixedDt;
    }
    const Vec3 travel = projectile.velocity * fixedDt;
    const float distance = length(travel);
    const Vec3 direction = distance > 0.00001F ? travel / distance : Vec3{};
    const WorldTrace world = traceWorld(arena, projectile.position, direction, distance);
    float hitDistance = world.distance;
    const bool directEnabled = !grenade || projectile.projectileHitboxRadius > 0.0F;
    const std::size_t directTarget = directEnabled
      ? nearestProjectileTarget(
          projectile.position,
          direction,
          projectile,
          config.balance,
          targets,
          hitDistance,
          hitDistance
        )
      : targets.size();
    bool explode = directTarget < targets.size();
    Vec3 explosionPosition = explode
      ? projectile.position + direction * hitDistance
      : projectile.position + travel;
    if (!explode && world.hit) {
      explosionPosition = world.end;
      if (grenade) {
        const WeaponRuntimeGrenadeBounce bounce = bounceWeaponRuntimeGrenade(
          projectile.velocity, world.normal, config.balance.grenadeLauncher
        );
        projectile.velocity = bounce.velocity;
        projectile.resting = bounce.resting;
        projectile.position = world.end + world.normal * 0.0002F;
        ++projectile.ageTicks;
        if (projectile.ageTicks < config.balance.grenadeLauncher.fuseTicks) continue;
      }
      explode = true;
    }
    ++projectile.ageTicks;
    explode = explode ||
      projectile.ageTicks >= weaponRuntimeProjectileMaxLifetime(projectile.weapon, config.balance);
    if (!explode) {
      projectile.position += travel;
      continue;
    }

    const float radius = radiusFor(projectile.weapon);
    const int splashDamage = splashDamageFor(projectile.weapon);
    bool damagedAnyTarget = false;
    for (std::size_t index = 0; index < targets.size(); ++index) {
      const WeaponRuntimeTarget& target = targets[index];
      if (!target.active) continue;
      const float distanceToTarget = target.shape == WeaponRuntimeTargetShape::Sphere
        ? std::max(0.0F, length(target.center - explosionPosition) - target.radius)
        : weaponRuntimePlayerCylinderDistance(explosionPosition, target.player);
      if (distanceToTarget > radius && index != directTarget) continue;
      if (
        index != directTarget &&
        !(target.shape == WeaponRuntimeTargetShape::Sphere
          ? splashCanReachSphere(arena, explosionPosition, target.center)
          : weaponRuntimeSplashCanReachPlayer(arena, explosionPosition, target.player))
      ) {
        continue;
      }
      const float falloff = 1.0F - distanceToTarget / std::max(0.001F, radius);
      int damage = static_cast<int>(std::ceil(static_cast<float>(splashDamage) * falloff));
      if (index == directTarget) damage = std::max(damage, directDamageFor(projectile.weapon));
      if (damage <= 0) continue;
      addHit(
        result,
        target,
        projectile.weapon,
        damage,
        false,
        0.0F,
        0U,
        index == directTarget
      );
      damagedAnyTarget = true;
    }
    if (damagedAnyTarget) ++result.damagingProjectileHits;

    const float selfDistance = weaponRuntimePlayerCylinderDistance(
      explosionPosition,
      attacker
    );
    if (
      selfDistance <= radius &&
      attacker.health > 0 &&
      weaponRuntimeSplashCanReachPlayer(arena, explosionPosition, attacker)
    ) {
      const float falloff = 1.0F - selfDistance / std::max(0.001F, radius);
      const int nominal = static_cast<int>(std::ceil(
        static_cast<float>(splashDamage) * falloff
      ));
      const int selfDamage =
        (nominal * static_cast<int>(config.selfDamagePercent) + 50) / 100;
      attacker.health = std::max(0, attacker.health - selfDamage);
      Vec3 knockbackDirection = normalize(attacker.position - explosionPosition);
      if (length(knockbackDirection) <= 0.0001F) {
        knockbackDirection = normalize(projectile.velocity);
      }
      attacker.velocity += knockbackDirection * knockbackFor(projectile.weapon) *
        (static_cast<float>(nominal) / static_cast<float>(std::max(1, splashDamage)));
    }
    projectile.active = false;
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

std::uint32_t weaponRuntimeSwitchBlockingCooldown(
  Weapon weapon,
  std::uint32_t cooldownTicks
) {
  // Normal play lets Plasma Gun switch at once in QL mode while its short
  // refire timer keeps running. Local modes use the same switch rule.
  return weapon == Weapon::PlasmaGun ? 0U : cooldownTicks;
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

std::uint32_t weaponRuntimeCooldownTicks(
  Weapon weapon,
  const BalanceConfig& balance
) {
  return cooldownFor(weapon, balance);
}

WeaponRuntimeSwitchResult requestWeaponRuntimeSwitch(
  Weapon selectedWeapon,
  Weapon requestedWeapon,
  WeaponRuntimeSwitchingMode switchingMode,
  std::uint32_t selectedWeaponCooldownTicks,
  std::uint32_t currentPulloutTicks,
  std::uint32_t pulloutDurationTicks
) {
  WeaponRuntimeSwitchResult result{selectedWeapon, currentPulloutTicks, false};
  if (requestedWeapon == selectedWeapon ||
      !canSwitchWeaponRuntime(switchingMode, selectedWeaponCooldownTicks)) {
    return result;
  }
  result.selectedWeapon = requestedWeapon;
  result.switched = true;
  result.pulloutTicks = switchingMode == WeaponRuntimeSwitchingMode::Ql
    ? pulloutDurationTicks : 0U;
  return result;
}

bool canFireWeaponRuntime(
  WeaponRuntimeSwitchingMode switchingMode,
  std::uint32_t pulloutTicks,
  const WeaponAmmoArray& ammo,
  Weapon weapon,
  bool infiniteAmmo
) {
  return (switchingMode != WeaponRuntimeSwitchingMode::Ql || pulloutTicks == 0U) &&
    hasWeaponRuntimeAmmo(ammo, weapon, infiniteAmmo);
}

void consumeWeaponRuntimeBeamAmmo(
  WeaponAmmoArray& ammo,
  Weapon weapon,
  bool infiniteAmmo,
  double& ammoCredit,
  float fireHz,
  float fixedDt
) {
  if (infiniteAmmo) return;
  std::int32_t& amount = ammo[weaponIndex(weapon)];
  if (amount <= 0) return;
  const double rate = static_cast<double>(std::max(1.0F, fireHz));
  ammoCredit = std::min(ammoCredit, rate);
  const int requested = static_cast<int>(std::floor(ammoCredit));
  if (requested > 0) {
    const int consumed = std::min(requested, amount);
    amount -= consumed;
    ammoCredit -= static_cast<double>(consumed);
  }
  ammoCredit += rate * static_cast<double>(fixedDt);
}

RocketProjectile makeWeaponRuntimeProjectile(
  std::uint8_t owner,
  std::uint32_t sequence,
  Weapon weapon,
  const PlayerState& attacker,
  const UserCommand& command,
  const BalanceConfig& balance
) {
  const bool grenade = weapon == Weapon::GrenadeLauncher;
  const bool plasma = weapon == Weapon::PlasmaGun;
  const float speed = grenade ? balance.grenadeLauncher.speed :
    plasma ? balance.plasmaGun.speed : balance.rocketLauncher.speed;
  RocketProjectile projectile;
  projectile.active = true;
  projectile.owner = owner;
  projectile.sequence = sequence == 0U ? 1U : sequence;
  projectile.weapon = weapon;
  projectile.position = weaponMuzzlePosition(attacker, eyeHeightFor(weapon, balance));
  projectile.previousPosition = projectile.position;
  projectile.projectileRadius = grenade ? balance.grenadeLauncher.projectileRadius : 0.0F;
  projectile.projectileHitboxRadius = grenade
    ? balance.grenadeLauncher.projectileHitboxRadius : 0.0F;
  projectile.velocity = cameraForward(
    command.viewYawRadians, command.viewPitchRadians
  ) * speed;
  if (grenade) projectile.velocity.z += balance.grenadeLauncher.verticalBoost;
  return projectile;
}

std::uint32_t weaponRuntimeProjectileMaxLifetime(
  Weapon weapon,
  const BalanceConfig& balance
) {
  if (weapon == Weapon::GrenadeLauncher) return balance.grenadeLauncher.fuseTicks;
  if (weapon == Weapon::PlasmaGun) return balance.plasmaGun.maxLifetimeTicks;
  return balance.rocketLauncher.maxLifetimeTicks;
}

WeaponRuntimeGrenadeBounce bounceWeaponRuntimeGrenade(
  Vec3 velocity,
  Vec3 normal,
  const GrenadeLauncherTuning& tuning
) {
  if (dot(velocity, normal) > 0.0F) normal *= -1.0F;
  const float normalVelocity = dot(velocity, normal);
  WeaponRuntimeGrenadeBounce result;
  result.impactSpeed = std::fabs(normalVelocity);
  result.velocity = normalVelocity < 0.0F
    ? (velocity - normal * (2.0F * normalVelocity)) * tuning.bounceDamping
    : velocity * tuning.bounceDamping;
  result.resting = normal.z > 0.5F && length(result.velocity) <= tuning.restSpeed;
  if (result.resting) result.velocity = {};
  return result;
}

float weaponRuntimePlayerCylinderDistance(Vec3 point, const PlayerState& player) {
  const float radial = std::max(
    0.0F,
    std::hypot(point.x - player.position.x, point.y - player.position.y) -
      player.bounds.radius
  );
  const float vertical = std::max(
    0.0F,
    std::fabs(point.z - player.position.z) - player.bounds.halfHeight
  );
  return std::hypot(radial, vertical);
}

Vec3 weaponRuntimeProjectileDirectAabbHalfExtents(
  Weapon weapon,
  const PlayerState& target,
  const BalanceConfig& balance
) {
  constexpr CollisionBounds defaultBounds = {};
  const float scaleXY = target.bounds.radius /
    std::max(0.0001F, defaultBounds.radius);
  const float scaleZ = target.bounds.halfHeight /
    std::max(0.0001F, defaultBounds.halfHeight);
  const float baseXY = weapon == Weapon::PlasmaGun
    ? balance.plasmaGun.directHitboxHalfExtentXY
    : balance.rocketLauncher.directHitboxHalfExtentXY;
  const float baseZ = weapon == Weapon::PlasmaGun
    ? balance.plasmaGun.directHitboxHalfExtentZ
    : balance.rocketLauncher.directHitboxHalfExtentZ;
  return {baseXY * scaleXY, baseXY * scaleXY, baseZ * scaleZ};
}

bool weaponRuntimePointInsidePlayerDirectAabb(
  Vec3 point,
  const PlayerState& player,
  Vec3 halfExtents
) {
  const Vec3 relative = point - player.position;
  return
    std::fabs(relative.x) <= halfExtents.x + kWeaponRuntimeProjectileEpsilon &&
    std::fabs(relative.y) <= halfExtents.y + kWeaponRuntimeProjectileEpsilon &&
    std::fabs(relative.z) <= halfExtents.z + kWeaponRuntimeProjectileEpsilon;
}

bool weaponRuntimeSplashCanReachPlayer(
  const Arena& arena,
  Vec3 explosionPosition,
  const PlayerState& player
) {
  const float sideOffset = player.bounds.radius * 0.75F;
  const std::array<Vec3, 5> targetPoints = {{
    player.position,
    player.position + Vec3{sideOffset, 0.0F, 0.0F},
    player.position + Vec3{-sideOffset, 0.0F, 0.0F},
    player.position + Vec3{0.0F, sideOffset, 0.0F},
    player.position + Vec3{0.0F, -sideOffset, 0.0F},
  }};
  for (const Vec3 targetPoint : targetPoints) {
    const Vec3 segment = explosionPosition - targetPoint;
    const float distance = length(segment);
    if (distance <= kWeaponRuntimeProjectileEpsilon) return true;
    const WorldTrace trace = traceWorld(arena, targetPoint, segment / distance, distance);
    if (trace.distance >= distance - kWeaponRuntimeProjectileEpsilon) return true;
  }
  return false;
}

void decayWeaponRuntimeIcePools(IcePoolArray& pools, float fixedDt) {
  for (IcePool& pool : pools) {
    if (!pool.active) continue;
    pool.lifetimeSeconds -= fixedDt;
    if (pool.lifetimeSeconds <= 0.0F || pool.radius <= 0.0F) pool = {};
  }
}

void growWeaponRuntimeIcePool(
  IcePoolArray& pools,
  Vec3 center,
  Vec3 normal,
  const IcePoolTuning& tuning,
  float fixedDt
) {
  if (tuning.maxRadius <= 0.0F || tuning.growthPerSecond <= 0.0F ||
      tuning.lifetimeSeconds <= 0.0F) {
    return;
  }
  IcePool* chosen = nullptr;
  IcePool* reusable = nullptr;
  for (IcePool& pool : pools) {
    if (!pool.active) {
      if (reusable == nullptr) reusable = &pool;
      continue;
    }
    const Vec3 delta = center - pool.center;
    const float planeDistance = dot(delta, pool.normal);
    const Vec3 tangentDelta = delta - pool.normal * planeDistance;
    if (std::fabs(planeDistance) <= 0.5F &&
        length(tangentDelta) <= pool.radius + tuning.mergeDistance) {
      chosen = &pool;
      break;
    }
  }
  if (chosen == nullptr) {
    if (reusable == nullptr) {
      reusable = &pools.front();
      for (IcePool& pool : pools) {
        if (pool.lifetimeSeconds < reusable->lifetimeSeconds) reusable = &pool;
      }
    }
    *reusable = {true, center, normal, 0.0F, tuning.lifetimeSeconds};
    chosen = reusable;
  }
  chosen->normal = normalize(chosen->normal + normal);
  chosen->lifetimeSeconds = tuning.lifetimeSeconds;
  chosen->radius = std::min(
    tuning.maxRadius,
    chosen->radius + (tuning.maxRadius - chosen->radius) *
      tuning.growthPerSecond * fixedDt
  );
}

void advanceWeaponRuntimeCooldowns(std::span<std::uint32_t> cooldowns) {
  for (std::uint32_t& cooldown : cooldowns) {
    if (cooldown > 0U) --cooldown;
  }
}

WeaponRuntimeTick tickWeaponRuntime(
  WeaponRuntimeState& state,
  const WeaponRuntimeConfig& config,
  PlayerState& attacker,
  const UserCommand& command,
  const Arena& arena,
  std::span<const WeaponRuntimeTarget> targets,
  float fixedDt
) {
  WeaponRuntimeTick result;
  advanceWeaponRuntimeCooldowns(state.cooldownTicks);
  if (state.pulloutTicks > 0U) --state.pulloutTicks;
  const Weapon previousWeapon = state.selectedWeapon;
  const WeaponRuntimeSwitchResult switchResult = requestWeaponRuntimeSwitch(
    state.selectedWeapon, command.weapon, config.switchingMode,
    weaponRuntimeSwitchBlockingCooldown(
      state.selectedWeapon,
      state.cooldownTicks[weaponIndex(state.selectedWeapon)]
    ),
    state.pulloutTicks,
    config.balance.weaponPulloutTicks
  );
  state.selectedWeapon = switchResult.selectedWeapon;
  state.pulloutTicks = switchResult.pulloutTicks;
  if (switchResult.switched && isBeam(previousWeapon)) {
    const std::size_t previousBeam = previousWeapon == Weapon::LightningGun ? 0U : 1U;
    state.beamShotCredit[previousBeam] = 1.0;
    state.beamDamageCredit[previousBeam] = 0.0;
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
  if (weapon == Weapon::Railgun && command.zoomed && attacker.health > 0) {
    state.sniperAdsFraction = std::min(
      1.0F, state.sniperAdsFraction + fixedDt / kSniperAdsSeconds
    );
    if (state.sniperAdsFraction >= 1.0F) {
      state.sniperChargeFraction = std::min(
        1.0F,
        state.sniperChargeFraction + fixedDt /
          std::max(0.05F, config.balance.sniperChargeSeconds)
      );
    }
  } else {
    state.sniperAdsFraction = 0.0F;
    state.sniperChargeFraction = 0.0F;
  }
  if (!command.attack || attacker.health <= 0 || !canFireWeaponRuntime(
        config.switchingMode, state.pulloutTicks, state.ammo, weapon, config.infiniteAmmo
      )) {
    if (isBeam(weapon)) {
      const std::size_t beamIndex = weapon == Weapon::LightningGun ? 0U : 1U;
      state.beamShotCredit[beamIndex] = 1.0;
    }
    advanceProjectiles(state, config, attacker, arena, targets, fixedDt, result);
    return result;
  }

  if (isBeam(weapon)) {
    const std::size_t beamIndex = weapon == Weapon::LightningGun ? 0U : 1U;
    const float fireHz = weapon == Weapon::LightningGun
      ? config.balance.lightningGun.fireHz
      : config.balance.freezeGun.fireHz;
    result.beam.start = result.fire.start;
    result.beam.end = result.fire.end;
    result.beam.active = true;
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
      result.beam.end = result.fire.end;
      result.beam.hit = true;
      result.beam.headshot = headshot;
    }
    ++result.acceptedBeamPulses;
    if (targetIndex < targets.size()) {
      ++result.hitBeamPulses;
      state.beamShotCredit[beamIndex] = std::min(
        state.beamShotCredit[beamIndex],
        static_cast<double>(std::max(1.0F, fireHz))
      );
      const int shotsApplied = static_cast<int>(std::floor(
        state.beamShotCredit[beamIndex]
      ));
      state.beamShotCredit[beamIndex] -= shotsApplied;
      state.beamShotCredit[beamIndex] +=
        static_cast<double>(std::max(1.0F, fireHz)) *
        static_cast<double>(fixedDt);
      int damage = 0;
      float freezeApplied = 0.0F;
      if (shotsApplied > 0) {
        const float damagePerSecond = weapon == Weapon::LightningGun
          ? config.balance.lightningGun.damagePerSecond
          : config.balance.freezeGun.damagePerSecond;
        state.beamDamageCredit[beamIndex] +=
          static_cast<double>(shotsApplied) *
          static_cast<double>(damagePerSecond) /
          static_cast<double>(std::max(1.0F, fireHz));
        damage = static_cast<int>(std::floor(state.beamDamageCredit[beamIndex]));
        state.beamDamageCredit[beamIndex] -= damage;
        damage = headshotDamage(damage, headshot, weapon, config.balance);
        freezeApplied = weapon == Weapon::FreezeGun
          ? config.balance.freezeGun.freezePerSecond *
            static_cast<float>(shotsApplied) / std::max(1.0F, fireHz)
          : 0.0F;
        result.beam.damageApplied = damage;
        result.beam.freezeApplied = freezeApplied;
      }
      // Accuracy and hit-score use the visible fixed-tick beam intersection,
      // while damage keeps the lower shared shot-credit rate.
      addHit(
        result,
        targets[targetIndex],
        weapon,
        damage,
        headshot,
        freezeApplied
      );
    } else {
      state.beamShotCredit[beamIndex] = std::min(
        1.0,
        state.beamShotCredit[beamIndex] +
          static_cast<double>(std::max(1.0F, fireHz)) *
          static_cast<double>(fixedDt)
      );
      if (
        weapon == Weapon::FreezeGun &&
        world.hit &&
        world.normal.z >= kMinWalkNormal
      ) {
        growWeaponRuntimeIcePool(
          state.icePools,
          world.end,
          normalize(world.normal),
          config.balance.icePool,
          fixedDt
        );
      }
    }
    consumeWeaponRuntimeBeamAmmo(
      state.ammo, weapon, config.infiniteAmmo, state.beamAmmoCredit[beamIndex],
      fireHz, fixedDt
    );
    advanceProjectiles(state, config, attacker, arena, targets, fixedDt, result);
    return result;
  }

  if (weapon == Weapon::RocketLauncher || weapon == Weapon::GrenadeLauncher ||
      weapon == Weapon::PlasmaGun) {
    if (state.cooldownTicks[weaponIndex(weapon)] == 0U) {
      result.fire.fired = true;
      ++result.acceptedShots;
      ++result.acceptedProjectileLaunches;
      state.cooldownTicks[weaponIndex(weapon)] = weaponRuntimeCooldownTicks(weapon, config.balance);
      consumeAmmo(state, config, weapon);
      addProjectile(state, config.balance, attacker, command, weapon);
    }
    advanceProjectiles(state, config, attacker, arena, targets, fixedDt, result);
    return result;
  }

  if (state.cooldownTicks[weaponIndex(weapon)] != 0U) {
    advanceProjectiles(state, config, attacker, arena, targets, fixedDt, result);
    return result;
  }
  result.fire.fired = true;
  ++result.acceptedShots;
  state.cooldownTicks[weaponIndex(weapon)] = weaponRuntimeCooldownTicks(weapon, config.balance);
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
      if (headshot) ++result.fire.pelletHeadshotCount;
      result.fire.hit = true;
      addHit(
        result, targets[targetIndex], weapon,
        headshotDamage(config.balance.shotgun.damagePerPellet, headshot, weapon, config.balance),
        headshot, 0.0F, 1U
      );
    }
  } else {
    ++result.acceptedInstantShots;
    const Vec3 instantDirection = weapon == Weapon::MachineGun
      ? machineGunShotDirection(
          direction,
          config.balance.machineGun.spreadRadians,
          command.sequence
        )
      : direction;
    const WorldTrace instantWorld = weapon == Weapon::MachineGun
      ? traceWorld(
          arena,
          result.fire.start,
          instantDirection,
          config.balance.machineGun.range
        )
      : world;
    result.fire.end = instantWorld.end;
    float hitDistance = instantWorld.distance;
    const float maximumTargetDistance = hitDistance;
    bool headshot = false;
    const std::size_t targetIndex = nearestTarget(
      result.fire.start,
      instantDirection,
      targets,
      maximumTargetDistance,
      hitDistance,
      headshot
    );
    if (targetIndex < targets.size()) {
      int damage = damageFor(weapon, config.balance);
      if (weapon == Weapon::Railgun) {
        damage = std::max(1, static_cast<int>(std::lround(
          static_cast<float>(damage) *
          (1.0F + (config.balance.sniperMaxDamageMultiplier - 1.0F) *
            state.sniperChargeFraction)
        )));
      }
      result.fire.hit = true;
      result.fire.headshot = headshot;
      result.fire.end = result.fire.start + instantDirection * hitDistance;
      result.fire.damageApplied = headshotDamage(damage, headshot, weapon, config.balance);
      addHit(
        result, targets[targetIndex], weapon,
        headshotDamage(damage, headshot, weapon, config.balance), headshot, 0.0F
      );
      ++result.hitInstantShots;
    }
    if (weapon == Weapon::Railgun) state.sniperChargeFraction = 0.0F;
  }
  advanceProjectiles(state, config, attacker, arena, targets, fixedDt, result);
  return result;
}

} // namespace lg
