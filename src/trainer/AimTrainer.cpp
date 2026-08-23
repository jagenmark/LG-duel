#include "trainer/AimTrainer.hpp"

#include "shared/Constants.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <string_view>
#include <utility>

namespace lg {
namespace {

[[nodiscard]] bool finite(Vec3 value) {
  return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool finitePositive(float value) {
  return std::isfinite(value) && value > 0.0F;
}

[[nodiscard]] std::uint64_t mix(std::uint64_t value, std::uint64_t input) {
  value ^= input + 0x9e3779b97f4a7c15ULL + (value << 6U) + (value >> 2U);
  return value;
}

[[nodiscard]] std::uint64_t mixFloat(std::uint64_t value, float input) {
  return mix(value, static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(input)));
}

[[nodiscard]] std::uint64_t mixString(std::uint64_t value, std::string_view input) {
  value = mix(value, input.size());
  for (const unsigned char byte : input) value = mix(value, byte);
  return value;
}

[[nodiscard]] Weapon firstAllowedWeapon(const AimScenario& scenario) {
  for (std::size_t index = 0; index < scenario.allowedWeapons.size(); ++index) {
    if (scenario.allowedWeapons[index]) return static_cast<Weapon>(index);
  }
  return Weapon::LightningGun;
}

} // namespace

float AimTrainerStats::accuracyPercent() const {
  return attempts == 0U ? 0.0F :
    100.0F * static_cast<float>(hits) / static_cast<float>(attempts);
}

float AimTrainerStats::scorePerMinute(std::uint32_t elapsedTicks) const {
  if (elapsedTicks == 0U) return 0.0F;
  return static_cast<float>(score) * 60.0F * kFixedTickRate /
    static_cast<float>(elapsedTicks);
}

AimTrainer::AimTrainer(Arena arena, BalanceConfig balance, MovementTuning movement)
  : arena_(std::move(arena)), balance_(std::move(balance)), movement_(movement) {
  startPlayer_.position = arena_.spawnCount > 0U
    ? arena_.spawnPositions[0]
    : Vec3{0.0F, 0.0F, startPlayer_.bounds.halfHeight};
  startPlayer_.position.z += startPlayer_.bounds.halfHeight;
  startPlayer_.onGround = true;
  startPlayer_.movementMode = MovementMode::Grounded;
  frame_.player = startPlayer_;
}

AimTrainerArmResult AimTrainer::arm(const AimScenario& scenario) {
  if (frame_.phase == AimTrainerPhase::Running) {
    return {false, "cannot change a scenario during a run"};
  }
  std::string error;
  if (!validateScenario(scenario, error)) return {false, std::move(error)};
  scenario_ = scenario;
  frame_ = {};
  frame_.phase = AimTrainerPhase::Armed;
  frame_.player = startPlayer_;
  frame_.selectedWeapon = scenario.weaponPolicy == AimWeaponPolicy::Forced
    ? scenario.forcedWeapon : firstAllowedWeapon(scenario);
  frame_.remainingTicks = scenario.durationTicks;
  frame_.message = "Scenario armed";
  return {true, {}};
}

bool AimTrainer::start() {
  if (frame_.phase != AimTrainerPhase::Armed) return false;
  resetRun();
  return true;
}

const AimTrainerFrame& AimTrainer::tick(const UserCommand& suppliedCommand) {
  frame_.naturalCompletion = false;
  if (frame_.phase != AimTrainerPhase::Running) return frame_;

  UserCommand command = suppliedCommand;
  if (scenario_.weaponPolicy == AimWeaponPolicy::Forced) {
    command.weapon = scenario_.forcedWeapon;
  } else if (!scenario_.allowedWeapons[weaponIndex(command.weapon)]) {
    command.weapon = weapons_.selectedWeapon;
  }
  decayWeaponRuntimeIcePools(weapons_.icePools, kFixedTickSeconds);
  if (scenario_.playerMovement == AimPlayerMovement::Locked) {
    frame_.player.viewYawRadians = command.viewYawRadians;
    frame_.player.viewPitchRadians = command.viewPitchRadians;
    frame_.player.position = startPlayer_.position;
    frame_.player.velocity = {};
    frame_.player.onGround = true;
    frame_.player.movementMode = MovementMode::Grounded;
    frame_.player.crouched = false;
    frame_.player.sneaking = false;
  } else {
    simulateMovement(
      frame_.player,
      command,
      arena_,
      movement_,
      weapons_.icePools,
      balance_.icePool,
      kFixedTickSeconds
    );
  }

  for (TargetRuntime& target : targets_) {
    if (!target.view.active && target.respawnTicks > 0U) {
      --target.respawnTicks;
      if (target.respawnTicks == 0U) respawnTarget(target);
    }
    if (target.view.active) {
      decayPlayerFreezeLevel(target.view.worker, balance_.freezeGun, kFixedTickSeconds);
      updateTargetMotion(target);
    }
  }

  std::vector<WeaponRuntimeTarget> targetSnapshot;
  targetSnapshot.reserve(targets_.size());
  for (const TargetRuntime& target : targets_) {
    targetSnapshot.push_back(runtimeTarget(target));
  }
  const WeaponRuntimeTick fire = tickWeaponRuntime(
    weapons_, weaponConfig_, frame_.player, command, arena_, targetSnapshot,
    kFixedTickSeconds
  );
  if (fire.fire.fired) {
    frame_.pendingFires.push_back(fire.fire);
    frame_.latestFire = fire.fire;
    frame_.fireEventPending = true;
  }
  frame_.latestBeam = fire.beam;
  frame_.selectedWeapon = weapons_.selectedWeapon;
  frame_.ammo = weapons_.ammo;
  frame_.icePools = weapons_.icePools;
  applyHitBatch(fire);
  refreshTargetViews();
  frame_.projectiles.clear();
  frame_.projectiles.reserve(weapons_.projectiles.size());
  for (const RocketProjectile& projectile : weapons_.projectiles) {
    frame_.projectiles.push_back({
      projectile.active, projectile.weapon,
      projectile.position, projectile.velocity, projectile.projectileRadius
    });
  }

  ++frame_.elapsedTicks;
  if (frame_.elapsedTicks >= scenario_.durationTicks) {
    finishNaturally();
  } else {
    frame_.remainingTicks = scenario_.durationTicks - frame_.elapsedTicks;
  }
  return frame_;
}

const AimTrainerFrame& AimTrainer::view() const { return frame_; }

void AimTrainer::consumePresentationEvents() {
  frame_.pendingFires.clear();
  frame_.latestFire = {};
  frame_.fireEventPending = false;
}

void AimTrainer::markStorageWarning(std::string message) {
  frame_.storageWarning = true;
  frame_.message = std::move(message);
}

void AimTrainer::abort() {
  if (frame_.phase == AimTrainerPhase::Running) {
    makeResult(false);
    frame_.phase = AimTrainerPhase::Results;
    frame_.message = "Run aborted (not ranked)";
    frame_.result.ranked = false;
  }
}

std::uint64_t AimTrainer::scenarioFingerprint(const AimScenario& scenario) {
  std::uint64_t result = 1469598103934665603ULL;
  result = mix(result, scenario.version);
  result = mix(result, scenario.durationTicks);
  result = mix(result, static_cast<std::uint64_t>(scenario.playerMovement));
  result = mix(result, static_cast<std::uint64_t>(scenario.weaponPolicy));
  result = mix(result, weaponIndex(scenario.forcedWeapon));
  result = mix(result, scenario.infiniteAmmo ? 1U : 0U);
  result = mix(result, static_cast<std::uint64_t>(scenario.scoreMode));
  result = mix(result, scenario.hitScore);
  result = mix(result, scenario.damageScorePerPoint);
  result = mix(result, scenario.clearScore);
  result = mix(result, scenario.seed);
  result = mixString(result, scenario.mapName);
  result = mix(result, scenario.mapIdentity);
  result = mix(result, scenario.balanceIdentity);
  for (bool allowed : scenario.allowedWeapons) result = mix(result, allowed ? 1U : 0U);
  result = mix(result, scenario.groups.size());
  for (const AimTargetGroup& group : scenario.groups) {
    result = mix(result, static_cast<std::uint64_t>(group.visual));
    result = mix(result, static_cast<std::uint64_t>(group.life));
    result = mix(result, static_cast<std::uint64_t>(group.spawnMode));
    result = mix(result, static_cast<std::uint64_t>(group.motion));
    result = mix(result, group.count);
    result = mix(result, static_cast<std::uint64_t>(std::max(0, group.health)));
    result = mix(result, group.respawnDelayTicks);
    result = mixFloat(
      result,
      group.visual == AimTargetVisual::Orb
        ? group.radius
        : CollisionBounds{}.radius
    );
    result = mix(result, group.color.red);
    result = mix(result, group.color.green);
    result = mix(result, group.color.blue);
    result = mixFloat(result, group.randomMinimum.x); result = mixFloat(result, group.randomMinimum.y); result = mixFloat(result, group.randomMinimum.z);
    result = mixFloat(result, group.randomMaximum.x); result = mixFloat(result, group.randomMaximum.y); result = mixFloat(result, group.randomMaximum.z);
    result = mixFloat(result, group.strafeDirection.x); result = mixFloat(result, group.strafeDirection.y); result = mixFloat(result, group.strafeDirection.z);
    result = mixFloat(result, group.strafeSpeed);
    result = mix(result, group.waypointTicks);
    result = mix(result, group.fixedSpawns.size());
    for (Vec3 spawn : group.fixedSpawns) {
      result = mixFloat(result, spawn.x); result = mixFloat(result, spawn.y); result = mixFloat(result, spawn.z);
    }
  }
  return result;
}

std::uint64_t AimTrainer::balanceFingerprint(
  const BalanceConfig& balance,
  const MovementTuning& movement
) {
  std::uint64_t result = 1469598103934665603ULL;
  const auto addFloat = [&result](float value) { result = mixFloat(result, value); };
  const auto addInt = [&result](auto value) {
    result = mix(result, static_cast<std::uint64_t>(value));
  };
  const auto addLightning = [&](const auto& tuning) {
    addFloat(tuning.range); addFloat(tuning.damagePerSecond); addFloat(tuning.fireHz);
    addFloat(tuning.eyeHeight); addFloat(tuning.headshotMultiplier);
  };
  addLightning(balance.lightningGun);
  addFloat(balance.lightningGun.knockbackPerSecond);
  addLightning(balance.freezeGun);
  addFloat(balance.freezeGun.freezePerSecond); addFloat(balance.freezeGun.decayPerSecond);
  addFloat(balance.freezeGun.maxLevel); addFloat(balance.freezeGun.maxSlowFraction);
  addFloat(balance.icePool.maxRadius); addFloat(balance.icePool.growthPerSecond);
  addFloat(balance.icePool.lifetimeSeconds); addFloat(balance.icePool.friction);
  addFloat(balance.icePool.slopeGravityScale); addFloat(balance.icePool.controlScale);
  addFloat(balance.icePool.mergeDistance);
  const auto addHitscan = [&](const HitscanTuning& tuning) {
    addFloat(tuning.range); addInt(tuning.damage); addFloat(tuning.eyeHeight);
    addFloat(tuning.knockback); addFloat(tuning.headshotMultiplier);
  };
  addHitscan(balance.railgun); addFloat(balance.sniperChargeSeconds);
  addFloat(balance.sniperMaxDamageMultiplier); addInt(balance.railgunCooldownTicks);
  addHitscan(balance.revolver); addInt(balance.revolverCooldownTicks);
  addFloat(balance.machineGun.range); addInt(balance.machineGun.damage);
  addFloat(balance.machineGun.eyeHeight); addFloat(balance.machineGun.knockback);
  addFloat(balance.machineGun.spreadRadians); addFloat(balance.machineGun.headshotMultiplier);
  addInt(balance.machineGunCooldownTicks);
  addFloat(balance.shotgun.range); addInt(balance.shotgun.pelletCount);
  addInt(balance.shotgun.damagePerPellet); addFloat(balance.shotgun.spreadRadians);
  addFloat(balance.shotgun.eyeHeight); addFloat(balance.shotgun.knockback);
  addFloat(balance.shotgun.headshotMultiplier); addInt(balance.shotgunCooldownTicks);
  addFloat(balance.rocketLauncher.speed); addFloat(balance.rocketLauncher.radius);
  addFloat(balance.rocketLauncher.directHitboxHalfExtentXY);
  addFloat(balance.rocketLauncher.directHitboxHalfExtentZ);
  addInt(balance.rocketLauncher.directDamage); addInt(balance.rocketLauncher.splashDamage);
  addFloat(balance.rocketLauncher.knockback); addFloat(balance.rocketLauncher.eyeHeight);
  addInt(balance.rocketLauncher.maxLifetimeTicks); addInt(balance.rocketLauncherCooldownTicks);
  addFloat(balance.grenadeLauncher.speed); addFloat(balance.grenadeLauncher.verticalBoost);
  addFloat(balance.grenadeLauncher.gravity); addFloat(balance.grenadeLauncher.bounceDamping);
  addFloat(balance.grenadeLauncher.restSpeed); addFloat(balance.grenadeLauncher.bounceSoundMinSpeed);
  addFloat(balance.grenadeLauncher.projectileRadius);
  addFloat(balance.grenadeLauncher.projectileHitboxRadius);
  addFloat(balance.grenadeLauncher.radius); addInt(balance.grenadeLauncher.directDamage);
  addInt(balance.grenadeLauncher.splashDamage); addFloat(balance.grenadeLauncher.knockback);
  addFloat(balance.grenadeLauncher.eyeHeight); addInt(balance.grenadeLauncher.fuseTicks);
  addInt(balance.grenadeLauncher.cooldownTicks);
  addFloat(balance.plasmaGun.speed); addFloat(balance.plasmaGun.radius);
  addFloat(balance.plasmaGun.directHitboxHalfExtentXY);
  addFloat(balance.plasmaGun.directHitboxHalfExtentZ); addInt(balance.plasmaGun.damage);
  addFloat(balance.plasmaGun.knockback); addFloat(balance.plasmaGun.eyeHeight);
  addInt(balance.plasmaGun.maxLifetimeTicks); addInt(balance.plasmaGun.cooldownTicks);
  addInt(balance.weaponAmmo.infiniteAmmo ? 1U : 0U);
  for (const std::int32_t ammo : balance.weaponAmmo.spawnAmmo) addInt(ammo);
  addInt(balance.weaponPulloutTicks); addInt(balance.jumpPadRetriggerCooldownTicks);
  addInt(balance.smallHealthPickupAmount); addInt(balance.largeHealthPickupAmount);
  addInt(balance.smallHealthPickupCooldownTicks); addInt(balance.largeHealthPickupCooldownTicks);
  addInt(movement.flightEnabled ? 1U : 0U); addFloat(movement.groundAcceleration);
  addFloat(movement.airAcceleration); addFloat(movement.groundFriction);
  addFloat(movement.stopSpeed); addFloat(movement.gravity); addFloat(movement.maxGroundSpeed);
  addFloat(movement.maxAirSpeed); addFloat(movement.jumpImpulse);
  addInt(movement.airControlEnabled ? 1U : 0U); addFloat(movement.dashTargetSpeed);
  addFloat(movement.dashMaxSpeed); addFloat(movement.dashAcceleration);
  addFloat(movement.dashDuration); addFloat(movement.dashCooldown);
  addFloat(movement.dashGroundHopVelocity); addFloat(movement.dashAirHopVelocity);
  addFloat(movement.flightAcceleration); addFloat(movement.maxFlightSpeed);
  addFloat(movement.flightDamping); addFloat(movement.flightGravityCancel);
  return result;
}

bool AimTrainer::validateScenario(const AimScenario& scenario, std::string& error) const {
  if (scenario.version != AimScenario::kVersion) {
    error = "unsupported scenario version";
    return false;
  }
  if (scenario.durationTicks == 0U || scenario.durationTicks > 450000U) {
    error = "duration must be between one tick and one hour";
    return false;
  }
  if (scenario.groups.empty() || scenario.groups.size() > AimScenario::kMaxGroups) {
    error = "scenario needs 1 to 64 target groups";
    return false;
  }
  if (scenario.hitScore == 0U || scenario.damageScorePerPoint == 0U ||
      scenario.clearScore == 0U) {
    error = "score values must be positive";
    return false;
  }
  if (scenario.weaponPolicy == AimWeaponPolicy::Forced &&
      weaponIndex(scenario.forcedWeapon) >= kWeaponCount) {
    error = "forced weapon is invalid";
    return false;
  }
  if (scenario.weaponPolicy == AimWeaponPolicy::All &&
      std::none_of(scenario.allowedWeapons.begin(), scenario.allowedWeapons.end(),
        [](bool allowed) { return allowed; })) {
    error = "enable at least one weapon";
    return false;
  }
  std::size_t targetCount = 0;
  for (const AimTargetGroup& group : scenario.groups) {
    if (group.name.empty()) {
      error = "target group name is required";
      return false;
    }
    if (group.count == 0U || group.count > AimScenario::kMaxTargetsPerGroup ||
        !finitePositive(group.radius)) {
      error = "target count and radius must be positive";
      return false;
    }
    targetCount += group.count;
    if (group.life == AimTargetLife::Health && group.health <= 0) {
      error = "health targets need positive health";
      return false;
    }
    if (group.spawnMode == AimSpawnMode::FixedList && group.fixedSpawns.empty()) {
      error = "fixed-list groups need at least one spawn";
      return false;
    }
    if (group.spawnMode == AimSpawnMode::BoundedRandom &&
        (!finite(group.randomMinimum) || !finite(group.randomMaximum) ||
         group.randomMinimum.x > group.randomMaximum.x ||
         group.randomMinimum.y > group.randomMaximum.y ||
         group.randomMinimum.z > group.randomMaximum.z)) {
      error = "random spawn bounds are invalid";
      return false;
    }
  }
  if (targetCount > AimScenario::kMaxTargets) {
    error = "scenario has too many targets";
    return false;
  }
  return true;
}

void AimTrainer::resetRun() {
  frame_ = {};
  frame_.phase = AimTrainerPhase::Running;
  frame_.player = startPlayer_;
  frame_.selectedWeapon = scenario_.weaponPolicy == AimWeaponPolicy::Forced
    ? scenario_.forcedWeapon : firstAllowedWeapon(scenario_);
  frame_.remainingTicks = scenario_.durationTicks;
  weaponConfig_ = {balance_, scenario_.infiniteAmmo, WeaponRuntimeSwitchingMode::Ql};
  weapons_ = makeWeaponRuntimeState(weaponConfig_, frame_.selectedWeapon);
  frame_.ammo = weapons_.ammo;
  targets_.clear();
  randomState_ = static_cast<std::uint32_t>(scenario_.seed) ^
    static_cast<std::uint32_t>(scenario_.seed >> 32U);
  if (randomState_ == 0U) randomState_ = 1U;
  std::uint32_t id = 1U;
  for (std::size_t groupIndex = 0; groupIndex < scenario_.groups.size(); ++groupIndex) {
    for (std::uint32_t index = 0; index < scenario_.groups[groupIndex].count; ++index) {
      TargetRuntime target;
      target.view.id = id++;
      target.view.groupIndex = static_cast<std::uint32_t>(groupIndex);
      target.view.visual = scenario_.groups[groupIndex].visual;
      target.view.color = scenario_.groups[groupIndex].color;
      target.view.radius = target.view.visual == AimTargetVisual::Orb
        ? scenario_.groups[groupIndex].radius
        : CollisionBounds{}.radius;
      target.spawnOrdinal = index;
      respawnTarget(target);
      targets_.push_back(target);
    }
  }
  refreshTargetViews();
  resultRecorded_ = false;
}

void AimTrainer::respawnTarget(TargetRuntime& target) {
  const AimTargetGroup& group = scenario_.groups[target.view.groupIndex];
  target.view.active = true;
  target.view.health = group.life == AimTargetLife::Health ? group.health : 1;
  if (group.spawnMode == AimSpawnMode::FixedList) {
    target.view.position = group.fixedSpawns[target.spawnOrdinal % group.fixedSpawns.size()];
  } else {
    target.view.position = {
      randomFloat(group.randomMinimum.x, group.randomMaximum.x),
      randomFloat(group.randomMinimum.y, group.randomMaximum.y),
      randomFloat(group.randomMinimum.z, group.randomMaximum.z),
    };
  }
  ++target.spawnOrdinal;
  target.waypoint = target.view.position;
  target.strafeDirectionSign = 1.0F;
  target.nextWaypointTick = frame_.elapsedTicks + std::max(1U, group.waypointTicks);
  target.view.worker = {};
  target.view.worker.position = target.view.position;
  target.view.worker.position.z = std::max(
    target.view.worker.bounds.halfHeight, target.view.position.z
  );
  target.view.worker.onGround = true;
  target.view.worker.movementMode = MovementMode::Grounded;
}

void AimTrainer::updateTargetMotion(TargetRuntime& target) {
  const AimTargetGroup& group = scenario_.groups[target.view.groupIndex];
  if (group.motion == AimTargetMotion::Stationary) return;
  if (group.motion == AimTargetMotion::RandomWaypoint &&
      frame_.elapsedTicks >= target.nextWaypointTick) {
    target.waypoint = {
      randomFloat(group.randomMinimum.x, group.randomMaximum.x),
      randomFloat(group.randomMinimum.y, group.randomMaximum.y),
      randomFloat(group.randomMinimum.z, group.randomMaximum.z),
    };
    target.nextWaypointTick = frame_.elapsedTicks + std::max(1U, group.waypointTicks);
  }
  Vec3 direction = group.motion == AimTargetMotion::Strafe
    ? normalize(group.strafeDirection) * target.strafeDirectionSign
    : normalize(target.waypoint - target.view.position);
  if (length(direction) <= 0.00001F) return;
  const float movementScale = target.view.visual == AimTargetVisual::Worker
    ? freezeMovementScale(target.view.worker, balance_.freezeGun)
    : 1.0F;
  target.view.position += direction * group.strafeSpeed * movementScale * kFixedTickSeconds;
  const Vec3 unclamped = target.view.position;
  target.view.position.x = std::clamp(target.view.position.x, group.randomMinimum.x, group.randomMaximum.x);
  target.view.position.y = std::clamp(target.view.position.y, group.randomMinimum.y, group.randomMaximum.y);
  target.view.position.z = std::clamp(target.view.position.z, group.randomMinimum.z, group.randomMaximum.z);
  if (group.motion == AimTargetMotion::Strafe &&
      (target.view.position.x != unclamped.x || target.view.position.y != unclamped.y ||
       target.view.position.z != unclamped.z)) {
    target.strafeDirectionSign = -target.strafeDirectionSign;
  }
  target.view.worker.position = target.view.position;
}

std::uint32_t AimTrainer::randomU32() {
  randomState_ ^= randomState_ << 13U;
  randomState_ ^= randomState_ >> 17U;
  randomState_ ^= randomState_ << 5U;
  return randomState_;
}

float AimTrainer::randomFloat(float minimum, float maximum) {
  const float amount = static_cast<float>(randomU32()) /
    static_cast<float>(std::numeric_limits<std::uint32_t>::max());
  return minimum + (maximum - minimum) * amount;
}

WeaponRuntimeTarget AimTrainer::runtimeTarget(const TargetRuntime& target) const {
  WeaponRuntimeTarget result;
  result.id = target.view.id;
  result.active = target.view.active;
  result.center = target.view.position;
  result.radius = target.view.radius;
  result.player = target.view.worker;
  result.player.health = target.view.active ? std::max(1, target.view.health) : 0;
  result.shape = target.view.visual == AimTargetVisual::Orb
    ? WeaponRuntimeTargetShape::Sphere : WeaponRuntimeTargetShape::Player;
  return result;
}

void AimTrainer::refreshTargetViews() {
  frame_.targets.clear();
  frame_.targets.reserve(targets_.size());
  for (const TargetRuntime& target : targets_) frame_.targets.push_back(target.view);
}

void AimTrainer::applyHitBatch(const WeaponRuntimeTick& tick) {
  frame_.stats.pelletAttempts += tick.acceptedPellets;
  frame_.stats.pelletHits += tick.hitPellets;
  frame_.stats.beamAttempts += tick.acceptedBeamPulses;
  frame_.stats.beamHits += tick.hitBeamPulses;
  frame_.stats.projectileAttempts += tick.acceptedProjectileLaunches;
  frame_.stats.projectileHits += tick.damagingProjectileHits;
  frame_.stats.attempts += tick.acceptedPellets + tick.acceptedBeamPulses +
    tick.acceptedProjectileLaunches + tick.acceptedInstantShots;
  frame_.stats.hits += tick.hitPellets + tick.hitBeamPulses +
    tick.damagingProjectileHits + tick.hitInstantShots;

  for (const WeaponRuntimeHit& hit : tick.hits) {
    auto target = std::find_if(targets_.begin(), targets_.end(),
      [&hit](const TargetRuntime& value) { return value.view.id == hit.targetId; });
    if (target == targets_.end() || !target->view.active) continue;
    const AimTargetGroup& group = scenario_.groups[target->view.groupIndex];
    const std::uint32_t damage = static_cast<std::uint32_t>(std::max(0, hit.damage));
    if (target->view.visual == AimTargetVisual::Worker && hit.freezeApplied > 0.0F) {
      target->view.worker.freezeLevel = std::clamp(
        target->view.worker.freezeLevel + hit.freezeApplied,
        0.0F,
        std::max(0.0F, balance_.freezeGun.maxLevel)
      );
    }
    frame_.stats.damage += damage;
    bool cleared = false;
    if (group.life == AimTargetLife::OneHit) {
      cleared = true;
    } else if (group.life == AimTargetLife::Health) {
      target->view.health -= static_cast<std::int32_t>(damage);
      cleared = target->view.health <= 0;
    }
    if (scenario_.scoreMode == AimScoreMode::Hit) {
      frame_.stats.score += static_cast<std::uint64_t>(scenario_.hitScore) *
        hit.scoringHits;
    }
    if (scenario_.scoreMode == AimScoreMode::Damage) {
      frame_.stats.score += static_cast<std::uint64_t>(scenario_.damageScorePerPoint) * damage;
    }
    if (cleared) {
      ++frame_.stats.clears;
      if (scenario_.scoreMode == AimScoreMode::Clear) {
        frame_.stats.score += scenario_.clearScore;
      }
      target->view.active = false;
      target->respawnTicks = group.respawnDelayTicks;
      if (target->respawnTicks == 0U) respawnTarget(*target);
    }
  }
}

void AimTrainer::finishNaturally() {
  if (resultRecorded_) return;
  resultRecorded_ = true;
  frame_.phase = AimTrainerPhase::Results;
  frame_.remainingTicks = 0;
  frame_.naturalCompletion = true;
  frame_.message = "Run complete";
  makeResult(true);
}

void AimTrainer::makeResult(bool ranked) {
  frame_.result = {
    scenarioFingerprint(scenario_), frame_.stats.score, frame_.stats.damage,
    frame_.stats.clears, frame_.stats.attempts, frame_.stats.hits,
    frame_.elapsedTicks, scenario_.seed, ranked
  };
}

} // namespace lg
