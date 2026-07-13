#include "server/ServerGame.hpp"

#include "shared/Sequence.hpp"
#include "sim/BalanceConfig.hpp"
#include "sim/ClanArenaRules.hpp"
#include "sim/Collision.hpp"
#include "sim/DuelRules.hpp"
#include "sim/GameplayCvars.hpp"
#include "sim/McGuffinRules.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace lg {
namespace {

constexpr std::uint32_t kMaxLagCompensationTicks = 25;
constexpr std::uint32_t kTransientCombatEventTicks = 8;
constexpr std::uint32_t kLocalHitFeedbackEventRetentionTicks = 32;
constexpr CollisionBounds kDefaultPlayerBounds = {};
constexpr float kQ3KnockbackToInternalScale = 22.0F / 1000.0F;
constexpr float kLightningKnockbackUsefulMinimum = 682.0F;
constexpr float kProjectileCollisionEpsilon = 0.0001F;
constexpr float kHealthPickupTouchRadius = 0.7F;
constexpr float kHealthPickupTouchHalfHeight = 0.8F;
constexpr float kPi = 3.14159265359F;
constexpr float kHalfPi = kPi * 0.5F;
constexpr float kTwoPi = kPi * 2.0F;
constexpr float kMaxPitchRadians = kHalfPi - 0.01F;

[[nodiscard]] bool consumeActionEdge(
  std::uint32_t incoming,
  std::uint32_t& consumed
) {
  if (incoming == 0U || (consumed != 0U && !isSequenceNewer(incoming, consumed))) {
    return false;
  }
  consumed = incoming;
  return true;
}

[[nodiscard]] PlayerState spawnPlayer(
  const Arena& arena,
  std::size_t playerIndex,
  std::int32_t healthAmount
) {
  PlayerState player;
  player.health = healthAmount;
  player.position = arena.spawnPositions[playerIndex];
  player.position.z += player.bounds.halfHeight;
  player.viewYawRadians = std::atan2(-player.position.y, -player.position.x);
  player.onGround = true;
  player.movementMode = MovementMode::Grounded;
  return player;
}

[[nodiscard]] Arena defaultServerArena() {
  Arena arena;
  arena.min = {-15.0F, -11.0F, 0.0F};
  arena.max = {15.0F, 11.0F, 10.0F};
  arena.spawnPositions = {{
    {-8.0F, -9.0F, 2.0F},
    {8.0F, -9.0F, 2.0F},
    {-12.0F, 8.0F, 2.0F},
    {12.0F, 8.0F, 2.0F},
    {-3.0F, -9.0F, 0.0F},
    {3.0F, -9.0F, 0.0F},
  }};
  arena.walls[0] = {{-15.0F, -11.0F, 0.0F}, {-3.0F, -7.0F, 2.0F}};
  arena.walls[1] = {{3.0F, -11.0F, 0.0F}, {15.0F, -7.0F, 2.0F}};
  arena.walls[2] = {{-15.0F, 6.5F, 0.0F}, {15.0F, 11.0F, 2.0F}};
  arena.wallCount = 3;
  return arena;
}

[[nodiscard]] UserCommand commandForPlayer(
  const ServerSnapshot& snapshot,
  const std::array<UserCommand, kDuelPlayerCount>& commands,
  const std::array<bool, kDuelPlayerCount>& hasCommand,
  std::size_t playerIndex
) {
  if (hasCommand[playerIndex]) {
    return commands[playerIndex];
  }

  UserCommand command;
  command.viewYawRadians = snapshot.players[playerIndex].viewYawRadians;
  command.viewPitchRadians = snapshot.players[playerIndex].viewPitchRadians;
  return command;
}

[[nodiscard]] bool isCombatant(
  const ServerSnapshot& snapshot,
  std::size_t playerIndex
) {
  return playerIndex < kDuelPlayerCount &&
    (snapshot.connectedPlayers[playerIndex] || snapshot.botPlayers[playerIndex]) &&
    snapshot.participatingPlayers[playerIndex] &&
    snapshot.players[playerIndex].health > 0;
}

[[nodiscard]] bool isEnemyCombatant(
  const ServerSnapshot& snapshot,
  std::size_t attackerIndex,
  std::size_t targetIndex
) {
  if (
    attackerIndex >= kDuelPlayerCount ||
    targetIndex >= kDuelPlayerCount ||
    attackerIndex == targetIndex ||
    !isCombatant(snapshot, targetIndex)
  ) {
    return false;
  }
  if (snapshot.gameMode == GameMode::Duel) {
    return areDuelOpponents(attackerIndex, targetIndex);
  }
  return areClanArenaEnemies(snapshot.teams, attackerIndex, targetIndex);
}

[[nodiscard]] std::size_t firstCombatTarget(
  const ServerSnapshot& snapshot,
  std::size_t attackerIndex
) {
  for (std::size_t targetIndex = 0; targetIndex < kDuelPlayerCount; ++targetIndex) {
    if (isEnemyCombatant(snapshot, attackerIndex, targetIndex)) {
      return targetIndex;
    }
  }
  return kDuelPlayerCount;
}

[[nodiscard]] bool combatStatsPhase(MatchPhase phase) {
  return phase == MatchPhase::Live ||
    phase == MatchPhase::WaitingForPlayers ||
    phase == MatchPhase::WaitingForReady;
}

void addWeaponAccuracy(
  RoundCombatStats& stats,
  Weapon weapon,
  std::uint32_t attempts,
  std::uint32_t hits
) {
  WeaponCombatStats& weaponStats = stats.weapons[weaponIndex(weapon)];
  const std::uint32_t maxValue = std::numeric_limits<std::uint16_t>::max();
  weaponStats.attempts = static_cast<std::uint16_t>(std::min(
    maxValue,
    static_cast<std::uint32_t>(weaponStats.attempts) + attempts
  ));
  weaponStats.hits = static_cast<std::uint16_t>(std::min(
    static_cast<std::uint32_t>(weaponStats.attempts),
    static_cast<std::uint32_t>(weaponStats.hits) + hits
  ));
}

void addWeaponDamage(
  RoundCombatStats& stats,
  Weapon weapon,
  std::uint32_t damage
) {
  stats.weapons[weaponIndex(weapon)].damageDealt += damage;
}

void recordWeaponAccuracy(
  ServerSnapshot& snapshot,
  std::size_t attackerIndex,
  Weapon weapon,
  std::uint32_t attempts,
  std::uint32_t hits
) {
  if (
    attackerIndex >= kDuelPlayerCount ||
    attempts == 0 ||
    !combatStatsPhase(snapshot.matchPhase)
  ) {
    return;
  }

  if (snapshot.matchPhase == MatchPhase::Live) {
    addWeaponAccuracy(
      snapshot.roundCombatStats[attackerIndex],
      weapon,
      attempts,
      hits
    );
  }
  addWeaponAccuracy(
    snapshot.matchCombatStats[attackerIndex],
    weapon,
    attempts,
    hits
  );
}

void recordInstantWeaponAccuracy(
  ServerSnapshot& snapshot,
  std::size_t attackerIndex,
  const WeaponFireResult& fire
) {
  if (!fire.fired) {
    return;
  }

  if (fire.weapon == Weapon::Shotgun) {
    recordWeaponAccuracy(
      snapshot,
      attackerIndex,
      fire.weapon,
      fire.pelletCount,
      fire.pelletHitCount
    );
    return;
  }

  recordWeaponAccuracy(
    snapshot,
    attackerIndex,
    fire.weapon,
    1U,
    fire.hit ? 1U : 0U
  );
}

void recordProjectileHit(
  ServerSnapshot& snapshot,
  std::size_t attackerIndex,
  Weapon weapon
) {
  if (
    attackerIndex >= kDuelPlayerCount ||
    !combatStatsPhase(snapshot.matchPhase)
  ) {
    return;
  }

  if (snapshot.matchPhase == MatchPhase::Live) {
    addWeaponAccuracy(snapshot.roundCombatStats[attackerIndex], weapon, 0U, 1U);
  }
  addWeaponAccuracy(snapshot.matchCombatStats[attackerIndex], weapon, 0U, 1U);
}

[[nodiscard]] float wrapRadians(float angle) {
  while (angle <= -kPi) {
    angle += kTwoPi;
  }
  while (angle > kPi) {
    angle -= kTwoPi;
  }
  return angle;
}

[[nodiscard]] float angleDeltaRadians(float from, float to) {
  return wrapRadians(to - from);
}

[[nodiscard]] float approachAngleRadians(
  float current,
  float target,
  float maxStep
) {
  const float delta = angleDeltaRadians(current, target);
  if (std::fabs(delta) <= maxStep) {
    return wrapRadians(target);
  }
  return wrapRadians(current + std::copysign(maxStep, delta));
}

[[nodiscard]] float approachFloat(float current, float target, float maxStep) {
  const float delta = target - current;
  if (std::fabs(delta) <= maxStep) {
    return target;
  }
  return current + std::copysign(maxStep, delta);
}

[[nodiscard]] Vec3 botTargetAimPoint(const PlayerState& target) {
  return target.position + Vec3{0.0F, 0.0F, target.bounds.halfHeight * 0.45F};
}

struct BotAttackPreset {
  float reactionMinSeconds = 0.0F;
  float reactionMaxSeconds = 0.0F;
  float aimErrorRadians = 0.0F;
  float turnSpeedRadiansPerSecond = 0.0F;
  float fireToleranceRadians = 0.0F;
  float aimErrorRefreshMinSeconds = 0.0F;
  float aimErrorRefreshMaxSeconds = 0.0F;
};

[[nodiscard]] BotAttackPreset botAttackPreset(BotAttackMode mode) {
  switch (mode) {
  case BotAttackMode::Easy:
    return {0.35F, 0.50F, 0.11F, 1.35F, 0.040F, 0.45F, 0.75F};
  case BotAttackMode::Medium:
    return {0.18F, 0.28F, 0.055F, 3.25F, 0.060F, 0.35F, 0.60F};
  case BotAttackMode::Hard:
    return {0.08F, 0.14F, 0.018F, 6.25F, 0.080F, 0.25F, 0.45F};
  case BotAttackMode::Off:
    break;
  }
  return {};
}

[[nodiscard]] std::optional<std::size_t> uniqueScoreLeader(
  const std::array<std::uint16_t, kDuelPlayerCount>& scores,
  const std::array<bool, kDuelPlayerCount>& players
) {
  std::optional<std::size_t> leader;
  bool tied = false;
  for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
    if (!players[index]) {
      continue;
    }
    if (!leader.has_value() || scores[index] > scores[*leader]) {
      leader = index;
      tied = false;
    } else if (scores[index] == scores[*leader]) {
      tied = true;
    }
  }
  return tied ? std::nullopt : leader;
}

[[nodiscard]] float q3KnockbackToInternal(float knockback) {
  return knockback * kQ3KnockbackToInternalScale;
}

[[nodiscard]] float lightningKnockbackToInternal(float knockback) {
  return q3KnockbackToInternal(std::max(0.0F, knockback));
}

[[nodiscard]] bool nearlyEqualGameplayFloat(float lhs, float rhs) {
  return std::fabs(lhs - rhs) <= 0.0001F;
}

[[nodiscard]] const char* weaponSwitchingModeCommandValue(
  WeaponSwitchingMode mode
) {
  switch (mode) {
    case WeaponSwitchingMode::Ql:
      return "ql";
    case WeaponSwitchingMode::Cpma:
      return "cpma";
    case WeaponSwitchingMode::Crazy:
      return "crazy";
  }
  return "crazy";
}

[[nodiscard]] Vec3 bounceNormalForPoint(const Arena& arena, Vec3 point) {
  Vec3 bestNormal = {0.0F, 0.0F, 1.0F};
  float bestDistance = std::fabs(point.z - arena.min.z);

  const auto consider = [&bestNormal, &bestDistance](float distance, Vec3 normal) {
    if (distance < bestDistance) {
      bestDistance = distance;
      bestNormal = normal;
    }
  };

  consider(std::fabs(point.x - arena.min.x), {-1.0F, 0.0F, 0.0F});
  consider(std::fabs(point.x - arena.max.x), {1.0F, 0.0F, 0.0F});
  consider(std::fabs(point.y - arena.min.y), {0.0F, -1.0F, 0.0F});
  consider(std::fabs(point.y - arena.max.y), {0.0F, 1.0F, 0.0F});
  consider(std::fabs(point.z - arena.max.z), {0.0F, 0.0F, -1.0F});

  for (std::size_t wallIndex = 0; wallIndex < arena.wallCount; ++wallIndex) {
    const ArenaWall& wall = arena.walls[wallIndex];
    if (
      point.x < wall.min.x - kProjectileCollisionEpsilon ||
      point.x > wall.max.x + kProjectileCollisionEpsilon ||
      point.y < wall.min.y - kProjectileCollisionEpsilon ||
      point.y > wall.max.y + kProjectileCollisionEpsilon ||
      point.z < wall.min.z - kProjectileCollisionEpsilon ||
      point.z > wall.max.z + kProjectileCollisionEpsilon
    ) {
      continue;
    }

    consider(std::fabs(point.x - wall.min.x), {-1.0F, 0.0F, 0.0F});
    consider(std::fabs(point.x - wall.max.x), {1.0F, 0.0F, 0.0F});
    consider(std::fabs(point.y - wall.min.y), {0.0F, -1.0F, 0.0F});
    consider(std::fabs(point.y - wall.max.y), {0.0F, 1.0F, 0.0F});
    consider(std::fabs(point.z - wall.min.z), {0.0F, 0.0F, -1.0F});
    consider(std::fabs(point.z - wall.max.z), {0.0F, 0.0F, 1.0F});
  }

  for (std::size_t brushIndex = 0; brushIndex < arena.brushCount; ++brushIndex) {
    const ArenaBrush& brush = arena.brushes[brushIndex];
    if (
      point.x < brush.min.x - kProjectileCollisionEpsilon ||
      point.x > brush.max.x + kProjectileCollisionEpsilon ||
      point.y < brush.min.y - kProjectileCollisionEpsilon ||
      point.y > brush.max.y + kProjectileCollisionEpsilon ||
      point.z < brush.min.z - kProjectileCollisionEpsilon ||
      point.z > brush.max.z + kProjectileCollisionEpsilon
    ) {
      continue;
    }

    for (std::uint8_t faceIndex = 0; faceIndex < brush.faceCount; ++faceIndex) {
      const ArenaBrushFace& face = brush.faces[faceIndex];
      consider(std::fabs(dot(face.normal, point) - face.distance), face.normal);
    }
  }

  return bestNormal;
}

[[nodiscard]] std::filesystem::path defaultBalanceConfigPath() {
  namespace fs = std::filesystem;
  fs::path directory = fs::current_path();
  for (;;) {
    const fs::path candidate = directory / "config" / "balance.cfg";
    if (fs::exists(candidate)) {
      return candidate;
    }
    const fs::path parent = directory.parent_path();
    if (parent.empty() || parent == directory) {
      break;
    }
    directory = parent;
  }
  return {};
}

void logClientGameplayCommand(
  const std::string& playerName,
  const std::string& command,
  const std::string& value
) {
  std::cout << "player " << playerName << " executed command " <<
    command << " " << value << '\n';
}

} // namespace

ServerGame::ServerGame(NetTransport& transport, std::string balanceConfigPath)
  : transport_(transport) {
  arena_ = defaultServerArena();
  mapDescriptor_ = describeMap("custom", arena_);
  rocketLauncherTuning_.knockback = q3KnockbackToInternal(rocketKnockback_);
  if (!balanceConfigPath.empty()) {
    const BalanceConfigLoadResult loaded =
      loadBalanceConfigFromFile(balanceConfigPath);
    if (loaded.ok) {
      applyBalanceConfig(loaded.config);
    } else {
      std::cerr << "Ignoring balance config: " << loaded.error << '\n';
    }
  } else if (const std::filesystem::path fallbackPath = defaultBalanceConfigPath();
             !fallbackPath.empty()) {
    const BalanceConfigLoadResult loaded =
      loadBalanceConfigFromFile(fallbackPath.string());
    if (loaded.ok) {
      applyBalanceConfig(loaded.config);
    } else {
      std::cerr << "Ignoring balance config: " << loaded.error << '\n';
    }
  }
  resetMatch();
  snapshot_.connectedPlayers[0] = true;
  snapshot_.connectedPlayers[1] = true;
  snapshot_.readyPlayers[0] = true;
  snapshot_.readyPlayers[1] = true;
  snapshot_.matchPhase = MatchPhase::Live;
  updateParticipatingPlayers();
  publishSnapshot();
}

void ServerGame::applyBalanceConfig(const BalanceConfig& config) {
  lightningGunTuning_.range = config.lightningGun.range;
  lightningGunTuning_.eyeHeight = config.lightningGun.eyeHeight;
  freezeGunTuning_.range = config.freezeGun.range;
  freezeGunTuning_.eyeHeight = config.freezeGun.eyeHeight;
  freezeGunTuning_.freezePerSecond = config.freezeGun.freezePerSecond;
  freezeGunTuning_.decayPerSecond = config.freezeGun.decayPerSecond;
  freezeGunTuning_.maxSlowFraction = config.freezeGun.maxSlowFraction;
  icePoolTuning_ = config.icePool;
  snapshot_.icePoolTuning = icePoolTuning_;
  railgunTuning_.range = config.railgun.range;
  railgunTuning_.eyeHeight = config.railgun.eyeHeight;
  railgunTuning_.knockback = config.railgun.knockback;
  railgunCooldownDurationTicks_ = config.railgunCooldownTicks;
  machineGunTuning_.range = config.machineGun.range;
  machineGunTuning_.eyeHeight = config.machineGun.eyeHeight;
  machineGunTuning_.knockback = config.machineGun.knockback;
  machineGunTuning_.spreadRadians = config.machineGun.spreadRadians;
  machineGunCooldownDurationTicks_ = config.machineGunCooldownTicks;
  shotgunTuning_.range = config.shotgun.range;
  shotgunTuning_.pelletCount = config.shotgun.pelletCount;
  shotgunTuning_.spreadRadians = config.shotgun.spreadRadians;
  shotgunTuning_.eyeHeight = config.shotgun.eyeHeight;
  shotgunTuning_.knockback = config.shotgun.knockback;
  shotgunCooldownDurationTicks_ = config.shotgunCooldownTicks;
  rocketLauncherTuning_.speed = config.rocketLauncher.speed;
  rocketLauncherTuning_.radius = config.rocketLauncher.radius;
  rocketLauncherTuning_.directHitboxHalfExtentXY =
    config.rocketLauncher.directHitboxHalfExtentXY;
  rocketLauncherTuning_.directHitboxHalfExtentZ =
    config.rocketLauncher.directHitboxHalfExtentZ;
  rocketLauncherTuning_.eyeHeight = config.rocketLauncher.eyeHeight;
  rocketLauncherTuning_.maxLifetimeTicks = config.rocketLauncher.maxLifetimeTicks;
  rocketLauncherCooldownDurationTicks_ = config.rocketLauncherCooldownTicks;
  grenadeLauncherTuning_.speed = config.grenadeLauncher.speed;
  grenadeLauncherTuning_.verticalBoost = config.grenadeLauncher.verticalBoost;
  grenadeLauncherTuning_.gravity = config.grenadeLauncher.gravity;
  grenadeLauncherTuning_.bounceDamping = config.grenadeLauncher.bounceDamping;
  grenadeLauncherTuning_.restSpeed = config.grenadeLauncher.restSpeed;
  grenadeLauncherTuning_.bounceSoundMinSpeed =
    config.grenadeLauncher.bounceSoundMinSpeed;
  grenadeLauncherTuning_.projectileRadius =
    config.grenadeLauncher.projectileRadius;
  grenadeLauncherTuning_.projectileHitboxRadius =
    config.grenadeLauncher.projectileHitboxRadius;
  grenadeLauncherTuning_.radius = config.grenadeLauncher.radius;
  grenadeLauncherTuning_.eyeHeight = config.grenadeLauncher.eyeHeight;
  grenadeLauncherTuning_.fuseTicks = config.grenadeLauncher.fuseTicks;
  grenadeLauncherTuning_.cooldownTicks = config.grenadeLauncher.cooldownTicks;
  plasmaGunTuning_.speed = config.plasmaGun.speed;
  plasmaGunTuning_.radius = config.plasmaGun.radius;
  plasmaGunTuning_.directHitboxHalfExtentXY =
    config.plasmaGun.directHitboxHalfExtentXY;
  plasmaGunTuning_.directHitboxHalfExtentZ =
    config.plasmaGun.directHitboxHalfExtentZ;
  plasmaGunTuning_.knockback = config.plasmaGun.knockback;
  plasmaGunTuning_.eyeHeight = config.plasmaGun.eyeHeight;
  plasmaGunTuning_.maxLifetimeTicks = config.plasmaGun.maxLifetimeTicks;
  plasmaGunTuning_.cooldownTicks = config.plasmaGun.cooldownTicks;
  weaponAmmoConfig_.spawnAmmo = config.weaponAmmo.spawnAmmo;
  snapshot_.weaponAmmo.spawnAmmo = weaponAmmoConfig_.spawnAmmo;
  for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
    refillAmmo(playerIndex);
  }
  weaponPulloutDurationTicks_ = config.weaponPulloutTicks;
  jumpPadRetriggerCooldownTicks_ = config.jumpPadRetriggerCooldownTicks;
  smallHealthPickupAmount_ = config.smallHealthPickupAmount;
  largeHealthPickupAmount_ = config.largeHealthPickupAmount;
  smallHealthPickupCooldownTicks_ = config.smallHealthPickupCooldownTicks;
  largeHealthPickupCooldownTicks_ = config.largeHealthPickupCooldownTicks;
}

void ServerGame::tick(float fixedDt) {
  receivedCommandThisTick_.fill(false);
  mcguffinThrowRequestedThisTick_.fill(false);
  jumpEdgeThisTick_.fill(false);
  dashEdgeThisTick_.fill(false);
  attackEdgeThisTick_.fill(false);
  receiveCommands();
  updateMatchState();
  updateBotCommands(fixedDt);
  snapshot_.weaponFires = {};
  snapshot_.rocketExplosions = {};
  snapshot_.footstepAudioEvents = {};
  snapshot_.grenadeBounceAudioEvents = {};
  snapshot_.fragEvents = {};
  snapshot_.localHitFeedbackEvents = {};
  snapshot_.rockets = {};
  for (std::uint32_t& cooldown : railgunCooldownTicks_) {
    if (cooldown > 0) {
      --cooldown;
    }
  }
  for (std::uint32_t& cooldown : machineGunCooldownTicks_) {
    if (cooldown > 0) {
      --cooldown;
    }
  }
  for (std::uint32_t& cooldown : shotgunCooldownTicks_) {
    if (cooldown > 0) {
      --cooldown;
    }
  }
  for (std::uint32_t& cooldown : rocketCooldownTicks_) {
    if (cooldown > 0) {
      --cooldown;
    }
  }
  for (std::uint32_t& cooldown : grenadeCooldownTicks_) {
    if (cooldown > 0) {
      --cooldown;
    }
  }
  for (std::uint32_t& cooldown : plasmaGunCooldownTicks_) {
    if (cooldown > 0) {
      --cooldown;
    }
  }
  for (std::uint32_t& pullout : weaponPulloutTicks_) {
    if (pullout > 0) {
      --pullout;
    }
  }
  decayIcePools(fixedDt);
  for (std::size_t index = 0; index < arena_.healthPickupCount; ++index) {
    std::uint32_t& cooldown = healthPickupCooldownTicks_[index];
    if (cooldown > 0) {
      --cooldown;
      if (cooldown == 0) {
        snapshot_.healthPickupAvailable[index] = true;
      }
    }
  }

  for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
    PlayerState& player = snapshot_.players[playerIndex];
    decayPlayerFreezeLevel(player, freezeGunTuning_, fixedDt);
    UserCommand command =
      commandForPlayer(snapshot_, commands_, hasCommand_, playerIndex);
    if (!hasCommand_[playerIndex]) {
      command.weapon = selectedWeapons_[playerIndex];
    }
    updateSelectedWeapon(playerIndex, command.weapon);
    if (player.health <= 0) {
      player.velocity = {};
      player.jumpHeld = false;
      player.crouched = false;
      player.sneaking = false;
      player.viewYawRadians = command.viewYawRadians;
      player.viewPitchRadians = command.viewPitchRadians;
      continue;
    }

    simulateMovement(
      snapshot_.players[playerIndex],
      command,
      arena_,
      movementTuning_,
      snapshot_.icePools,
      icePoolTuning_,
      fixedDt,
      static_cast<std::uint16_t>(
        std::min<std::uint32_t>(
          jumpPadRetriggerCooldownTicks_,
          std::numeric_limits<std::uint16_t>::max()
        )
      )
    );
  }

  snapshot_.playersColliding = false;
  for (std::size_t firstIndex = 0; firstIndex < kDuelPlayerCount; ++firstIndex) {
    if (
      !isCombatant(snapshot_, firstIndex) ||
      snapshot_.players[firstIndex].health <= 0
    ) {
      continue;
    }
    for (
      std::size_t secondIndex = firstIndex + 1U;
      secondIndex < kDuelPlayerCount;
      ++secondIndex
    ) {
      if (
        !isCombatant(snapshot_, secondIndex) ||
        snapshot_.players[secondIndex].health <= 0
      ) {
        continue;
      }
      snapshot_.playersColliding =
        resolvePlayerCollision(
          arena_,
          snapshot_.players[firstIndex],
          snapshot_.players[secondIndex]
        ) ||
        snapshot_.playersColliding;
    }
  }
  for (PlayerState& player : snapshot_.players) {
    if (player.health <= 0) {
      continue;
    }
    const CollisionResult collision = resolvePlayerArenaCollision(
      arena_,
      player,
      player.position,
      player.velocity
    );
    player.position = collision.position;
    player.velocity = collision.velocity;
  }
  updateHealthPickups();
  updateMcGuffin();
  updateFootstepAudioEvents();

  const std::array<PlayerState, kDuelPlayerCount> combatPlayers = snapshot_.players;
  std::array<std::size_t, kDuelPlayerCount> lightningTargets = {};
  std::array<std::size_t, kDuelPlayerCount> freezeTargets = {};
  std::array<std::size_t, kDuelPlayerCount> weaponTargets = {};
  lightningTargets.fill(kDuelPlayerCount);
  freezeTargets.fill(kDuelPlayerCount);
  weaponTargets.fill(kDuelPlayerCount);
  for (std::size_t attackerIndex = 0; attackerIndex < kDuelPlayerCount; ++attackerIndex) {
    UserCommand command =
      commandForPlayer(snapshot_, commands_, hasCommand_, attackerIndex);
    if (!hasCommand_[attackerIndex]) {
      command.weapon = selectedWeapons_[attackerIndex];
    }
    command.weapon = selectedWeapons_[attackerIndex];

    const bool warmupCombat =
      snapshot_.matchPhase == MatchPhase::WaitingForPlayers ||
      snapshot_.matchPhase == MatchPhase::WaitingForReady;
    const bool hasTarget =
      firstCombatTarget(snapshot_, attackerIndex) < kDuelPlayerCount;
    command.attack =
      command.attack &&
      (snapshot_.matchPhase == MatchPhase::Live || warmupCombat) &&
      isCombatant(snapshot_, attackerIndex) &&
      combatPlayers[attackerIndex].health > 0 &&
      (warmupCombat || hasTarget) &&
      canFireSelectedWeapon(attackerIndex);
    if (command.planarAim) {
      command.viewPitchRadians = 0.0F;
    }
    if (command.weapon != Weapon::LightningGun) {
      lightningGunStates_[attackerIndex] = {};
    }
    if (command.weapon != Weapon::FreezeGun) {
      freezeGunStates_[attackerIndex] = {};
    }
    if (
      command.weapon != Weapon::LightningGun &&
      command.weapon != Weapon::FreezeGun
    ) {
      snapshot_.lightningGuns[attackerIndex] = {};
    }
    const bool requestsLagCompensation =
      receivedCommandThisTick_[attackerIndex] && command.attack;
    const std::uint32_t viewedServerTick = viewedServerTicks_[attackerIndex];
    const std::uint32_t requestedRewindTicks =
      requestsLagCompensation && viewedServerTick <= snapshot_.serverTick
      ? snapshot_.serverTick - viewedServerTick
      : 0;
    const std::uint32_t clampedRewindTicks =
      std::min(requestedRewindTicks, kMaxLagCompensationTicks);
    const std::uint32_t targetTick = snapshot_.serverTick - clampedRewindTicks;
    const HistoryFrame& historyFrame = historyFrameForTick(targetTick);

    const Vec3 attackStart = weaponMuzzlePosition(
      combatPlayers[attackerIndex],
      command.weapon == Weapon::LightningGun
        ? lightningGunTuning_.eyeHeight
        : command.weapon == Weapon::FreezeGun
          ? freezeGunTuning_.eyeHeight
        : command.weapon == Weapon::MachineGun
          ? machineGunTuning_.eyeHeight
        : command.weapon == Weapon::Shotgun
          ? shotgunTuning_.eyeHeight
        : railgunTuning_.eyeHeight
    );
    const Vec3 attackDirection =
      cameraForward(command.viewYawRadians, command.viewPitchRadians);
    const float attackRange = command.weapon == Weapon::LightningGun
      ? lightningGunTuning_.range
      : command.weapon == Weapon::FreezeGun
        ? freezeGunTuning_.range
      : command.weapon == Weapon::MachineGun
        ? machineGunTuning_.range
      : command.weapon == Weapon::Shotgun
        ? shotgunTuning_.range
      : railgunTuning_.range;
    const WorldTrace worldTrace =
      traceWorld(arena_, attackStart, attackDirection, attackRange);
    std::size_t targetIndex = kDuelPlayerCount;
    float bestHitDistance = worldTrace.distance;
    for (std::size_t candidateIndex = 0; candidateIndex < kDuelPlayerCount; ++candidateIndex) {
      if (
        !isEnemyCombatant(snapshot_, attackerIndex, candidateIndex) ||
        combatPlayers[candidateIndex].health <= 0
      ) {
        continue;
      }
      const PlayerState& candidate = clampedRewindTicks == 0
        ? combatPlayers[candidateIndex]
        : historyFrame.players[candidateIndex];
      float hitDistance = 0.0F;
      if (
        tracePlayerCylinder(
          attackStart,
          attackDirection,
          candidate,
          bestHitDistance,
          hitDistance
        )
      ) {
        targetIndex = candidateIndex;
        bestHitDistance = hitDistance;
      }
    }
    if (command.weapon == Weapon::Shotgun && targetIndex >= kDuelPlayerCount) {
      targetIndex = firstCombatTarget(snapshot_, attackerIndex);
    }

    const std::size_t debugTargetIndex = targetIndex < kDuelPlayerCount
      ? targetIndex
      : firstCombatTarget(snapshot_, attackerIndex);
    PlayerState target = {};
    if (targetIndex < kDuelPlayerCount) {
      target = clampedRewindTicks == 0
        ? combatPlayers[targetIndex]
        : historyFrame.players[targetIndex];
      target.health = combatPlayers[targetIndex].health;
    }
    if (
      command.weapon == Weapon::LightningGun ||
      command.weapon == Weapon::FreezeGun
    ) {
      if (targetIndex < kDuelPlayerCount) {
        if (command.weapon == Weapon::LightningGun) {
          lightningTargets[attackerIndex] = targetIndex;
          snapshot_.lightningGuns[attackerIndex] = simulateLightningGun(
            combatPlayers[attackerIndex],
            target,
            command,
            arena_,
            lightningGunTuning_,
            lightningGunStates_[attackerIndex],
            fixedDt
          );
        } else {
          freezeTargets[attackerIndex] = targetIndex;
          snapshot_.lightningGuns[attackerIndex] = simulateFreezeGun(
            combatPlayers[attackerIndex],
            target,
            command,
            arena_,
            freezeGunTuning_,
            freezeGunStates_[attackerIndex],
            fixedDt
          );
        }
        if (snapshot_.lightningGuns[attackerIndex].hit) {
          snapshot_.lightningGuns[attackerIndex].targetPlayerIndex =
            static_cast<std::uint8_t>(targetIndex);
        }
      } else {
        snapshot_.lightningGuns[attackerIndex] = {};
        snapshot_.lightningGuns[attackerIndex].start = attackStart;
        snapshot_.lightningGuns[attackerIndex].end = worldTrace.end;
        snapshot_.lightningGuns[attackerIndex].active =
          command.attack && combatPlayers[attackerIndex].health > 0;
        if (
          command.weapon == Weapon::FreezeGun &&
          snapshot_.lightningGuns[attackerIndex].active &&
          worldTrace.hit &&
          worldTrace.normal.z >= kMinWalkNormal
        ) {
          growIcePool(worldTrace.end, normalize(worldTrace.normal), fixedDt);
        }
      }
      if (snapshot_.lightningGuns[attackerIndex].active) {
        if (command.weapon == Weapon::LightningGun) {
          consumeLightningGunAmmo(attackerIndex, fixedDt);
        } else {
          consumeFreezeGunAmmo(attackerIndex, fixedDt);
        }
      }
    } else if (
      (command.weapon == Weapon::Railgun || command.weapon == Weapon::Revolver) &&
      command.attack &&
      railgunCooldownTicks_[attackerIndex] == 0
    ) {
      if (targetIndex < kDuelPlayerCount) {
        weaponTargets[attackerIndex] = targetIndex;
        snapshot_.weaponFires[attackerIndex] = simulateRailgun(
          combatPlayers[attackerIndex],
          target,
          command,
          arena_,
          railgunTuning_,
          command.weapon
        );
      } else {
        WeaponFireResult& fire = snapshot_.weaponFires[attackerIndex];
        fire.weapon = command.weapon;
        fire.visualSeed = command.sequence;
        fire.start = attackStart;
        fire.end = worldTrace.end;
        fire.fired = command.attack && combatPlayers[attackerIndex].health > 0;
      }
      recordInstantWeaponAccuracy(
        snapshot_,
        attackerIndex,
        snapshot_.weaponFires[attackerIndex]
      );
      railgunCooldownTicks_[attackerIndex] = railgunCooldownDurationTicks_;
      (void)consumeAmmo(attackerIndex, command.weapon);
    } else if (
      command.weapon == Weapon::MachineGun &&
      command.attack &&
      machineGunCooldownTicks_[attackerIndex] == 0
    ) {
      if (targetIndex < kDuelPlayerCount) {
        weaponTargets[attackerIndex] = targetIndex;
        snapshot_.weaponFires[attackerIndex] = simulateMachineGun(
          combatPlayers[attackerIndex],
          target,
          command,
          arena_,
          machineGunTuning_
        );
      } else {
        WeaponFireResult& fire = snapshot_.weaponFires[attackerIndex];
        fire.weapon = Weapon::MachineGun;
        fire.visualSeed = command.sequence;
        fire.start = attackStart;
        fire.end = worldTrace.end;
        fire.fired = command.attack && combatPlayers[attackerIndex].health > 0;
      }
      recordInstantWeaponAccuracy(
        snapshot_,
        attackerIndex,
        snapshot_.weaponFires[attackerIndex]
      );
      machineGunCooldownTicks_[attackerIndex] = machineGunCooldownDurationTicks_;
      (void)consumeAmmo(attackerIndex, Weapon::MachineGun);
    } else if (
      command.weapon == Weapon::Shotgun &&
      command.attack &&
      shotgunCooldownTicks_[attackerIndex] == 0
    ) {
      if (targetIndex < kDuelPlayerCount) {
        weaponTargets[attackerIndex] = targetIndex;
        snapshot_.weaponFires[attackerIndex] = simulateShotgun(
          combatPlayers[attackerIndex],
          target,
          command,
          arena_,
          shotgunTuning_
        );
      } else {
        WeaponFireResult& fire = snapshot_.weaponFires[attackerIndex];
        fire.weapon = Weapon::Shotgun;
        fire.visualSeed = command.sequence;
        fire.pelletCount = shotgunTuning_.pelletCount;
        fire.start = attackStart;
        fire.end = worldTrace.end;
        fire.fired = command.attack && combatPlayers[attackerIndex].health > 0;
      }
      recordInstantWeaponAccuracy(
        snapshot_,
        attackerIndex,
        snapshot_.weaponFires[attackerIndex]
      );
      shotgunCooldownTicks_[attackerIndex] = shotgunCooldownDurationTicks_;
      (void)consumeAmmo(attackerIndex, Weapon::Shotgun);
    } else if (
      command.weapon == Weapon::RocketLauncher &&
      command.attack &&
      rocketCooldownTicks_[attackerIndex] == 0
    ) {
      if (
        spawnProjectile(
          attackerIndex,
          combatPlayers[attackerIndex],
          command,
          Weapon::RocketLauncher
        )
      ) {
        rocketCooldownTicks_[attackerIndex] = rocketLauncherCooldownDurationTicks_;
        (void)consumeAmmo(attackerIndex, Weapon::RocketLauncher);
      }
    } else if (
      command.weapon == Weapon::GrenadeLauncher &&
      command.attack &&
      grenadeCooldownTicks_[attackerIndex] == 0
    ) {
      if (
        spawnProjectile(
          attackerIndex,
          combatPlayers[attackerIndex],
          command,
          Weapon::GrenadeLauncher
        )
      ) {
        grenadeCooldownTicks_[attackerIndex] =
          grenadeLauncherTuning_.cooldownTicks;
        (void)consumeAmmo(attackerIndex, Weapon::GrenadeLauncher);
      }
    } else if (
      command.weapon == Weapon::PlasmaGun &&
      command.attack &&
      plasmaGunCooldownTicks_[attackerIndex] == 0
    ) {
      if (
        spawnProjectile(
          attackerIndex,
          combatPlayers[attackerIndex],
          command,
          Weapon::PlasmaGun
        )
      ) {
        plasmaGunCooldownTicks_[attackerIndex] =
          plasmaGunTuning_.cooldownTicks;
        (void)consumeAmmo(attackerIndex, Weapon::PlasmaGun);
      }
    }
    LightningGunResult& result = snapshot_.lightningGuns[attackerIndex];
    result.requestedRewindTicks = requestedRewindTicks;
    result.appliedRewindTicks = clampedRewindTicks == 0
      ? 0
      : snapshot_.serverTick - historyFrame.serverTick;
    result.rewindClamped = requestedRewindTicks > result.appliedRewindTicks;
    result.hasRewindDebug = result.active;
    result.rewindTargetTick = clampedRewindTicks == 0
      ? snapshot_.serverTick
      : historyFrame.serverTick;
    if (debugTargetIndex < kDuelPlayerCount) {
      const PlayerState& debugTarget = clampedRewindTicks == 0
        ? combatPlayers[debugTargetIndex]
        : historyFrame.players[debugTargetIndex];
      result.currentTargetPosition = combatPlayers[debugTargetIndex].position;
      result.rewoundTargetPosition = debugTarget.position;
      result.currentTargetBounds = combatPlayers[debugTargetIndex].bounds;
      result.rewoundTargetBounds = debugTarget.bounds;
    }
    if (
      command.weapon == Weapon::LightningGun ||
      command.weapon == Weapon::FreezeGun
    ) {
      recordWeaponAccuracy(
        snapshot_,
        attackerIndex,
        command.weapon,
        result.active ? 1U : 0U,
        result.hit ? 1U : 0U
      );
    }
  }

  for (std::size_t attackerIndex = 0; attackerIndex < kDuelPlayerCount; ++attackerIndex) {
    if (
      lightningTargets[attackerIndex] < kDuelPlayerCount &&
      !damageAllowed(attackerIndex, lightningTargets[attackerIndex])
    ) {
      snapshot_.lightningGuns[attackerIndex].damageApplied = 0;
    }
    if (
      weaponTargets[attackerIndex] < kDuelPlayerCount &&
      !damageAllowed(attackerIndex, weaponTargets[attackerIndex])
    ) {
      snapshot_.weaponFires[attackerIndex].damageApplied = 0;
    }
    if (freezeTargets[attackerIndex] < kDuelPlayerCount) {
      LightningGunResult& freezeResult = snapshot_.lightningGuns[attackerIndex];
      if (damageAllowed(attackerIndex, freezeTargets[attackerIndex])) {
        PlayerState& frozenTarget =
          snapshot_.players[freezeTargets[attackerIndex]];
        frozenTarget.freezeLevel = std::clamp(
          frozenTarget.freezeLevel + freezeResult.freezeApplied,
          0.0F,
          std::max(0.0F, freezeGunTuning_.maxLevel)
        );
      } else {
        freezeResult.freezeApplied = 0.0F;
        freezeResult.damageApplied = 0;
      }
    }
    applyDamageAndKnockback(
      attackerIndex,
      lightningTargets[attackerIndex],
      snapshot_.lightningGuns[attackerIndex].damageApplied,
      snapshot_.lightningGuns[attackerIndex].knockbackImpulse,
      Weapon::LightningGun,
      snapshot_.lightningGuns[attackerIndex].headshot
    );
    applyDamageAndKnockback(
      attackerIndex,
      weaponTargets[attackerIndex],
      snapshot_.weaponFires[attackerIndex].damageApplied,
      snapshot_.weaponFires[attackerIndex].knockbackImpulse,
      snapshot_.weaponFires[attackerIndex].weapon,
      snapshot_.weaponFires[attackerIndex].headshot
    );
    applyDamageAndKnockback(
      attackerIndex,
      freezeTargets[attackerIndex],
      snapshot_.lightningGuns[attackerIndex].damageApplied,
      {},
      Weapon::FreezeGun,
      snapshot_.lightningGuns[attackerIndex].headshot
    );
  }

  simulateRockets(fixedDt);

  if (snapshot_.matchPhase == MatchPhase::Live) {
    ++snapshot_.liveTicksElapsed;
    // McGuffin rounds end through score control and the final-hold rule. A
    // generic match timer must not bypass its best-of-three round structure.
    if (matchRules_.timeLimitMinutes > 0 && snapshot_.gameMode != GameMode::McGuffin) {
      const std::uint32_t limitTicks =
        static_cast<std::uint32_t>(matchRules_.timeLimitMinutes) * 60U * 125U;
      if (snapshot_.liveTicksElapsed >= limitTicks) {
        if (snapshot_.gameMode == GameMode::Duel) {
          const auto leader = uniqueScoreLeader(snapshot_.scores, occupiedPlayers());
          if (leader.has_value()) {
            beginMatchEnd(*leader);
          }
        } else if (snapshot_.gameMode == GameMode::ClanArena) {
          const auto leader = clanArenaScoreLeader(snapshot_.teamScores);
          if (leader.has_value()) {
            beginMatchEnd(*leader);
          }
        } else {
          const auto leader = clanArenaScoreLeader(snapshot_.mcguffinScores);
          if (leader.has_value()) {
            beginMatchEnd(*leader);
          }
        }
      }
    }
  }

  rememberTransientCombatEvents();
  restoreTransientCombatEvents();
  ++snapshot_.serverTick;
  recordHistory();
  publishSnapshot();
}

void ServerGame::resetMatch() {
  const std::uint32_t serverTick = snapshot_.serverTick;
  const auto playerNames = snapshot_.playerNames;
  const auto connectedPlayers = snapshot_.connectedPlayers;
  const auto botPlayers = botPlayers_;
  const GameMode gameMode = snapshot_.gameMode;
  const auto teams = snapshot_.teams;
  snapshot_ = {};
  snapshot_.serverTick = serverTick;
  snapshot_.mapRevision = mapRevision_;
  snapshot_.map = mapDescriptor_;
  snapshot_.connectedPlayers = connectedPlayers;
  snapshot_.botPlayers = botPlayers;
  snapshot_.gameMode = gameMode;
  snapshot_.teams = teams;
  snapshot_.mcguffinConfig = mcguffinConfig_;
  for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
    snapshot_.players[playerIndex] = spawnPlayer(arena_, playerIndex, healthAmount_);
    PlayerState& player = snapshot_.players[playerIndex];
    player.bounds.radius =
      kDefaultPlayerBounds.radius * playerSizeScaleXY_;
    player.bounds.halfHeight =
      kDefaultPlayerBounds.halfHeight * playerSizeScaleZ_;
    player.position.z =
      arena_.spawnPositions[playerIndex].z + player.bounds.halfHeight;
  }
  snapshot_.matchRules = matchRules_;
  snapshot_.movementTuning = movementTuning_;
  snapshot_.playerSizeScaleXY = playerSizeScaleXY_;
  snapshot_.playerSizeScaleZ = playerSizeScaleZ_;
  snapshot_.lightningKnockback = lightningKnockback_;
  snapshot_.rocketKnockback = rocketKnockback_;
  snapshot_.knockbackTimeMs = knockbackTimeMs_;
  snapshot_.weaponDamage = weaponDamage_;
  snapshot_.icePoolTuning = icePoolTuning_;
  snapshot_.weaponAmmo = weaponAmmoConfig_;
  snapshot_.vampirism = vampirism_;
  snapshot_.selfDamagePercent = selfDamagePercent_;
  snapshot_.healthAmount = healthAmount_;
  snapshot_.botDodgeEnabled = botDodgeEnabled_;
  snapshot_.botDodgeMinIntervalMs = botDodgeMinIntervalMs_;
  snapshot_.botDodgeMaxIntervalMs = botDodgeMaxIntervalMs_;
  snapshot_.weaponSwitchingMode = weaponSwitchingMode_;
  resetHealthPickups();
  snapshot_.roundWinner = 255;
  snapshot_.matchWinner = 255;
  snapshot_.roundWinningTeam = Team::None;
  snapshot_.matchWinningTeam = Team::None;
  snapshot_.mcguffinRedBaseOwner = Team::Red;
  snapshot_.mcguffinBlueBaseOwner = Team::Blue;
  resetMcGuffin(mcguffinObjective_, arena_.mcguffin.neutralSpawn);
  snapshot_.playerNames = playerNames;
  snapshot_.matchPhase = enoughPlayersConnected()
    ? MatchPhase::WaitingForReady
    : MatchPhase::WaitingForPlayers;
  lightningGunStates_ = {};
  freezeGunStates_ = {};
  snapshot_.icePools = {};
  lightningAmmoCredit_.fill(1.0);
  freezeAmmoCredit_.fill(1.0);
  railgunCooldownTicks_ = {};
  machineGunCooldownTicks_ = {};
  shotgunCooldownTicks_ = {};
  rocketCooldownTicks_ = {};
  grenadeCooldownTicks_ = {};
  plasmaGunCooldownTicks_ = {};
  healthPickupCooldownTicks_ = {};
  selectedWeapons_ = {};
  selectedWeapons_.fill(Weapon::LightningGun);
  weaponPulloutTicks_ = {};
  snapshot_.selectedWeapons = selectedWeapons_;
  for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
    refillAmmo(playerIndex);
  }
  recentWeaponFires_ = {};
  recentWeaponFireTicks_ = {};
  recentRocketExplosions_ = {};
  recentRocketExplosionTicks_ = {};
  recentFootstepAudioEvents_ = {};
  recentFootstepAudioEventTicks_ = {};
  recentGrenadeBounceAudioEvents_ = {};
  recentGrenadeBounceAudioEventTicks_ = {};
  recentFragEvents_ = {};
  recentFragEventTicks_ = {};
  footstepStates_ = {};
  footstepSequences_ = {};
  rockets_ = {};
  snapshot_.icePools = {};
  grenadeBounceSequences_ = {};
  fractionalVampirismHealing_ = {};
  commands_ = {};
  viewedServerTicks_ = {};
  hasCommand_ = {};
  receivedCommandThisTick_ = {};
  playerSessions_ = {};
  botCombatStates_ = {};
  if (snapshot_.gameMode == GameMode::McGuffin) {
    resetMcGuffinRound();
  }
  updateParticipatingPlayers();
  history_.clear();
  recordHistory();
}

void ServerGame::setArena(const Arena& arena) {
  setArena(arena, describeMap("custom", arena));
}

void ServerGame::setArena(const Arena& arena, MapDescriptor descriptor) {
  arena_ = arena;
  spawnLastUsedTicks_ = {};
  spawnWasUsed_ = {};
  spawnRandomState_ = 0x51A7E123U;
  spawnDebugString_ = "no team spawn selected yet";
  mapDescriptor_ = std::move(descriptor);
  ++mapRevision_;
  if (mapRevision_ == 0) {
    mapRevision_ = 1;
  }
  resetMatch();
}

void ServerGame::setMapDirectory(std::string mapDirectory) {
  mapDirectory_ = std::move(mapDirectory);
}

void ServerGame::respawnPlayer(std::size_t playerIndex) {
  // A respawn starts a new life, including for zero-delay modes. Do not let
  // cached input or an interrupted objective interaction execute on the new
  // body before a fresh command arrives from that player.
  commands_[playerIndex] = {};
  viewedServerTicks_[playerIndex] = 0;
  hasCommand_[playerIndex] = false;
  receivedCommandThisTick_[playerIndex] = false;
  mcguffinStealTicks_[playerIndex] = 0;
  mcguffinThrowRequestedThisTick_[playerIndex] = false;
  mcguffinThrowCommands_[playerIndex] = {};
  botCombatStates_[playerIndex] = {};
  snapshot_.respawnTicksRemaining[playerIndex] = 0;
  snapshot_.players[playerIndex] = spawnPlayer(arena_, playerIndex, healthAmount_);
  snapshot_.players[playerIndex].bounds.radius =
    kDefaultPlayerBounds.radius * playerSizeScaleXY_;
  snapshot_.players[playerIndex].bounds.halfHeight =
    kDefaultPlayerBounds.halfHeight * playerSizeScaleZ_;
  snapshot_.players[playerIndex].position.z =
    arena_.spawnPositions[playerIndex].z +
    snapshot_.players[playerIndex].bounds.halfHeight;
  if (snapshot_.gameMode == GameMode::McGuffin &&
      isPlayableTeam(snapshot_.teams[playerIndex])) {
    const std::optional<std::size_t> selected = selectTeamSpawn(
      playerIndex, snapshot_.players[playerIndex]
    );
    if (selected.has_value()) {
      const ArenaTeamSpawn& spawn = arena_.teamSpawns[*selected];
      snapshot_.players[playerIndex].position = spawn.position;
      snapshot_.players[playerIndex].position.z +=
        snapshot_.players[playerIndex].bounds.halfHeight;
      snapshot_.players[playerIndex].viewYawRadians = spawn.yawRadians;
    } else if (arena_.teamSpawnCount == 0) {
      // Compatibility path for older team-tagged maps.
      for (std::size_t spawnIndex = 0; spawnIndex < arena_.spawnTeams.size(); ++spawnIndex) {
        if (arena_.spawnTeams[spawnIndex] != snapshot_.teams[playerIndex]) continue;
        snapshot_.players[playerIndex].position = arena_.spawnPositions[spawnIndex];
        snapshot_.players[playerIndex].position.z +=
          snapshot_.players[playerIndex].bounds.halfHeight;
        break;
      }
    } else {
      // Physical groups are authoritative when present. Never resurrect an
      // invalid rejected point through permanent-team legacy metadata.
      snapshot_.players[playerIndex].health = 0;
    }
  }
  snapshot_.lightningGuns[playerIndex] = {};
  snapshot_.weaponFires[playerIndex] = {};
  snapshot_.rocketExplosions[playerIndex] = {};
  snapshot_.fragEvents[playerIndex] = {};
  snapshot_.footstepAudioEvents[playerIndex] = {};
  lightningGunStates_[playerIndex] = {};
  freezeGunStates_[playerIndex] = {};
  railgunCooldownTicks_[playerIndex] = 0;
  machineGunCooldownTicks_[playerIndex] = 0;
  shotgunCooldownTicks_[playerIndex] = 0;
  rocketCooldownTicks_[playerIndex] = 0;
  grenadeCooldownTicks_[playerIndex] = 0;
  plasmaGunCooldownTicks_[playerIndex] = 0;
  selectedWeapons_[playerIndex] = Weapon::LightningGun;
  weaponPulloutTicks_[playerIndex] = 0;
  lightningAmmoCredit_[playerIndex] = 1.0;
  freezeAmmoCredit_[playerIndex] = 1.0;
  snapshot_.selectedWeapons[playerIndex] = selectedWeapons_[playerIndex];
  refillAmmo(playerIndex);
  recentFootstepAudioEvents_[playerIndex] = {};
  recentFootstepAudioEventTicks_[playerIndex] = 0;
  recentFragEvents_[playerIndex] = {};
  recentFragEventTicks_[playerIndex] = 0;
  footstepStates_[playerIndex] = {};
  footstepSequences_[playerIndex] = 0;
  fractionalVampirismHealing_[playerIndex] = 0.0;
}

void ServerGame::respawnRound() {
  commands_ = {};
  viewedServerTicks_ = {};
  hasCommand_ = {};
  receivedCommandThisTick_ = {};
  botCombatStates_ = {};
  rockets_ = {};
  snapshot_.rockets = {};
  if (snapshot_.gameMode == GameMode::McGuffin) {
    // Base ownership changes between rounds and must be established before
    // players select physical spawn groups for their new lives.
    resetMcGuffinRound();
  }
  // Round respawns are one logical reset. Mark every old body inactive first
  // so spawn safety never scores candidates against positions from the round
  // that just ended; earlier selections still reserve space for later ones.
  for (PlayerState& player : snapshot_.players) {
    player.health = 0;
  }
  for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
    respawnPlayer(playerIndex);
  }
  resetHealthPickups();
  snapshot_.playersColliding = false;
  snapshot_.respawnTicksRemaining = {};
  snapshot_.roundCombatStats = {};
  history_.clear();
  recordHistory();
}

void ServerGame::setConnectedPlayers(
  const std::array<bool, kDuelPlayerCount>& connectedPlayers
) {
  if (snapshot_.connectedPlayers == connectedPlayers) {
    return;
  }

  const bool abortActiveMatch = !warmupPhase() && snapshot_.gameMode != GameMode::McGuffin;
  const std::array<bool, kDuelPlayerCount> previousConnected =
    snapshot_.connectedPlayers;
  for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
    const bool wasHuman = previousConnected[index];
    const bool isHuman = connectedPlayers[index];
    if (isHuman && botPlayers_[index]) {
      removeBotAtPlayerIndex(index);
    }

    if (wasHuman && !isHuman) {
      dropMcGuffinCarrier(index);
      snapshot_.readyPlayers[index] = false;
      snapshot_.teams[index] = Team::None;
      snapshot_.playerNames[index] = "PLAYER " + std::to_string(index + 1U);
      resetPlayerInputState(index);
      playerSessions_[index] = 0;
      botDodgeSwitchSeconds_[index] = 0.0F;
      botCombatStates_[index] = {};
    } else if (!wasHuman && isHuman) {
      snapshot_.readyPlayers[index] = false;
      snapshot_.teams[index] = Team::None;
      snapshot_.playerNames[index] = "PLAYER " + std::to_string(index + 1U);
      resetPlayerInputState(index);
      botDodgeSwitchSeconds_[index] = 0.0F;
      botCombatStates_[index] = {};
    }
  }
  snapshot_.connectedPlayers = connectedPlayers;
  updateParticipatingPlayers();

  if (abortActiveMatch) {
    resetMatch();
    return;
  }

  if (!enoughPlayersConnected()) {
    for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
      snapshot_.readyPlayers[index] = botPlayers_[index];
    }
    snapshot_.matchPhase = MatchPhase::WaitingForPlayers;
    snapshot_.phaseTicksRemaining = 0;
    snapshot_.scores = {};
    snapshot_.teamScores = {};
    snapshot_.mcguffinRoundsWon = {};
    snapshot_.mcguffinRound = 0;
    snapshot_.matchCombatStats = {};
    snapshot_.liveTicksElapsed = 0;
    snapshot_.roundWinner = 255;
    snapshot_.matchWinner = 255;
    snapshot_.roundWinningTeam = Team::None;
    snapshot_.matchWinningTeam = Team::None;
    respawnRound();
  } else if (snapshot_.matchPhase == MatchPhase::WaitingForPlayers) {
    snapshot_.matchPhase = MatchPhase::WaitingForReady;
    snapshot_.phaseTicksRemaining = 0;
  }
}

void ServerGame::setConnectedPlayers(
  const std::array<bool, kDuelPlayerCount>& connectedPlayers,
  const std::array<std::uint32_t, kDuelPlayerCount>& playerSessions
) {
  const std::array<bool, kDuelPlayerCount> previousConnected =
    snapshot_.connectedPlayers;
  setConnectedPlayers(connectedPlayers);

  for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
    if (!connectedPlayers[index]) {
      playerSessions_[index] = 0;
      continue;
    }

    if (
      previousConnected[index] &&
      playerSessions_[index] != 0 &&
      playerSessions_[index] != playerSessions[index]
    ) {
      snapshot_.readyPlayers[index] = false;
      snapshot_.playerNames[index] = "PLAYER " + std::to_string(index + 1U);
      resetPlayerInputState(index);
      botDodgeSwitchSeconds_[index] = 0.0F;
    }
    playerSessions_[index] = playerSessions[index];
  }
}

void ServerGame::resetPlayerInputState(std::size_t playerIndex) {
  commands_[playerIndex] = {};
  viewedServerTicks_[playerIndex] = 0;
  hasCommand_[playerIndex] = false;
  receivedCommandThisTick_[playerIndex] = false;
  lastActionEdges_[playerIndex] = {};
  jumpEdgeThisTick_[playerIndex] = false;
  dashEdgeThisTick_[playerIndex] = false;
  snapshot_.acknowledgedCommand[playerIndex] = 0;
  snapshot_.hasAcknowledgedCommand[playerIndex] = false;
  lightningGunStates_[playerIndex] = {};
  freezeGunStates_[playerIndex] = {};
  lightningAmmoCredit_[playerIndex] = 1.0;
  freezeAmmoCredit_[playerIndex] = 1.0;
  selectedWeapons_[playerIndex] = Weapon::LightningGun;
  weaponPulloutTicks_[playerIndex] = 0;
  snapshot_.selectedWeapons[playerIndex] = selectedWeapons_[playerIndex];
  refillAmmo(playerIndex);
  botCombatStates_[playerIndex] = {};
}

void ServerGame::setMatchRules(const MatchRules& rules) {
  matchRules_ = rules;
  matchRules_.roundLimit = std::max<std::uint16_t>(1, matchRules_.roundLimit);
  matchRules_.playerLimit = std::clamp<std::uint8_t>(
    matchRules_.playerLimit,
    1,
    static_cast<std::uint8_t>(kDuelPlayerCount)
  );
  snapshot_.matchRules = matchRules_;
}

ArenaSpawnGroup ServerGame::spawnGroupForTeam(Team team) const {
  if (snapshot_.mcguffinRedBaseOwner == team) return ArenaSpawnGroup::RedBase;
  if (snapshot_.mcguffinBlueBaseOwner == team) return ArenaSpawnGroup::BlueBase;
  // Before the deciding round's first installation, use the authored staging
  // side. Once a base is claimed, the ownership fields take over immediately.
  return team == Team::Red
    ? ArenaSpawnGroup::RedBase
    : ArenaSpawnGroup::BlueBase;
}

std::uint32_t ServerGame::nextSpawnRandomU32() {
  std::uint32_t value = spawnRandomState_;
  value ^= value << 13U;
  value ^= value >> 17U;
  value ^= value << 5U;
  spawnRandomState_ = value == 0U ? 0x51A7E123U : value;
  return spawnRandomState_;
}

std::optional<std::size_t> ServerGame::selectTeamSpawn(
  std::size_t playerIndex,
  const PlayerState& freshPlayer
) {
  struct Candidate {
    std::size_t index = 0;
    float score = 0.0F;
    float nearestEnemy = 20.0F;
    std::uint32_t recentTicks = 625;
    int visibleEnemies = 0;
    bool occupied = false;
  };

  constexpr float kEnemyDistanceWeight = 4.0F;
  constexpr float kVisibleEnemyPenalty = 100.0F;
  constexpr float kCloseEnemyDistance = 5.0F;
  constexpr float kCloseEnemyPenalty = 60.0F;
  constexpr float kNearbyTeammateDistance = 6.0F;
  constexpr float kNearbyTeammateBonus = 10.0F;
  constexpr float kRecentUsePenalty = 30.0F;
  constexpr std::uint32_t kRecentUseTicks = 625;

  const Team team = snapshot_.teams[playerIndex];
  const ArenaSpawnGroup group = spawnGroupForTeam(team);
  std::array<Candidate, Arena::kTeamSpawnCount> candidates = {};
  std::size_t candidateCount = 0;
  for (std::size_t spawnIndex = 0;
       spawnIndex < arena_.teamSpawnCount;
       ++spawnIndex) {
    const ArenaTeamSpawn& spawn = arena_.teamSpawns[spawnIndex];
    if (spawn.group != group) continue;

    PlayerState atSpawn = freshPlayer;
    atSpawn.position = spawn.position;
    atSpawn.position.z += atSpawn.bounds.halfHeight;
    if (playerPositionSolid(arena_, atSpawn, atSpawn.position) ||
        pointInsideMcGuffinBase(spawn.position, arena_.mcguffin.redBase) ||
        pointInsideMcGuffinBase(spawn.position, arena_.mcguffin.blueBase)) {
      continue;
    }

    Candidate candidate;
    candidate.index = spawnIndex;
    bool foundEnemy = false;
    for (std::size_t other = 0; other < kDuelPlayerCount; ++other) {
      if (other == playerIndex || !isActiveCombatant(other)) continue;
      const PlayerState& otherPlayer = snapshot_.players[other];
      const Vec3 delta = otherPlayer.position - atSpawn.position;
      const float distance = length(delta);
      const float horizontalDistance = std::sqrt(
        (delta.x * delta.x) + (delta.y * delta.y)
      );
      const bool overlaps =
        horizontalDistance < atSpawn.bounds.radius + otherPlayer.bounds.radius &&
        std::fabs(delta.z) < atSpawn.bounds.halfHeight + otherPlayer.bounds.halfHeight;
      candidate.occupied = candidate.occupied || overlaps;

      if (snapshot_.teams[other] == team) {
        if (distance <= kNearbyTeammateDistance) {
          candidate.score += kNearbyTeammateBonus;
        }
        continue;
      }
      if (!isPlayableTeam(snapshot_.teams[other])) continue;
      foundEnemy = true;
      candidate.nearestEnemy = std::min(candidate.nearestEnemy, distance);
      if (distance <= kCloseEnemyDistance) candidate.score -= kCloseEnemyPenalty;
      if (distance > 0.0001F) {
        const WorldTrace trace = traceWorld(
          arena_, atSpawn.position, delta / distance, distance
        );
        if (trace.distance >= distance - 0.01F) {
          ++candidate.visibleEnemies;
          candidate.score -= kVisibleEnemyPenalty;
        }
      }
    }
    candidate.score += (foundEnemy ? candidate.nearestEnemy : 20.0F) *
      kEnemyDistanceWeight;
    if (candidate.occupied) candidate.score -= 1000.0F;
    if (spawnWasUsed_[spawnIndex]) {
      candidate.recentTicks = snapshot_.serverTick - spawnLastUsedTicks_[spawnIndex];
      if (candidate.recentTicks < kRecentUseTicks) {
        const float fraction = 1.0F -
          (static_cast<float>(candidate.recentTicks) /
            static_cast<float>(kRecentUseTicks));
        candidate.score -= kRecentUsePenalty * fraction;
      }
    }
    candidates[candidateCount++] = candidate;
  }

  if (candidateCount == 0) {
    spawnDebugString_ = "no valid physical spawn candidates";
    return std::nullopt;
  }
  const bool hasUnoccupied = std::any_of(
    candidates.begin(), candidates.begin() + candidateCount,
    [](const Candidate& candidate) { return !candidate.occupied; }
  );
  std::sort(
    candidates.begin(), candidates.begin() + candidateCount,
    [hasUnoccupied](const Candidate& left, const Candidate& right) {
      if (hasUnoccupied && left.occupied != right.occupied) return !left.occupied;
      if (left.score != right.score) return left.score > right.score;
      return left.index < right.index;
    }
  );
  const std::size_t eligibleCount = hasUnoccupied
    ? static_cast<std::size_t>(std::count_if(
        candidates.begin(), candidates.begin() + candidateCount,
        [](const Candidate& candidate) { return !candidate.occupied; }
      ))
    : candidateCount;
  const std::size_t topCount = std::min<std::size_t>(3, eligibleCount);
  const float floorScore = candidates[topCount - 1U].score;
  std::array<std::uint32_t, 3> weights = {};
  std::uint32_t totalWeight = 0;
  for (std::size_t index = 0; index < topCount; ++index) {
    weights[index] = 1U + static_cast<std::uint32_t>(std::clamp(
      (candidates[index].score - floorScore) * 10.0F,
      0.0F,
      10000.0F
    ));
    totalWeight += weights[index];
  }
  std::uint32_t roll = nextSpawnRandomU32() % totalWeight;
  std::size_t selectedRank = 0;
  for (; selectedRank + 1U < topCount; ++selectedRank) {
    if (roll < weights[selectedRank]) break;
    roll -= weights[selectedRank];
  }
  const Candidate& selected = candidates[selectedRank];
  spawnWasUsed_[selected.index] = true;
  spawnLastUsedTicks_[selected.index] = snapshot_.serverTick;

  std::ostringstream debug;
  debug << "player=" << playerIndex << " group="
        << (group == ArenaSpawnGroup::RedBase ? "red_base" : "blue_base");
  for (std::size_t index = 0; index < candidateCount; ++index) {
    const Candidate& candidate = candidates[index];
    debug << " #" << candidate.index << " score=" << candidate.score
          << " enemy=" << candidate.nearestEnemy
          << " los=" << candidate.visibleEnemies
          << " occupied=" << (candidate.occupied ? 1 : 0)
          << " recent=" << candidate.recentTicks;
    if (candidate.index == selected.index) debug << " SELECTED";
  }
  spawnDebugString_ = debug.str();
  return selected.index;
}

void ServerGame::setMcGuffinConfig(const McGuffinConfig& config) {
  if (!isValidMcGuffinConfig(config)) {
    return;
  }
  mcguffinConfig_ = config;
  snapshot_.mcguffinConfig = config;
}

void ServerGame::setRuntimeGameplayTuning(
  const MovementTuning& movementTuning,
  float playerSizeScaleXY,
  float playerSizeScaleZ,
  float lightningKnockback,
  float lightningFireHz,
  float rocketKnockback,
  std::int32_t knockbackTimeMs,
  const WeaponDamageTuning& weaponDamage,
  float vampirism,
  std::uint8_t selfDamagePercent,
  std::int32_t healthAmount,
  bool infiniteAmmo,
  bool botDodgeEnabled,
  int botDodgeMinIntervalMs,
  int botDodgeMaxIntervalMs,
  WeaponSwitchingMode weaponSwitchingMode
) {
  movementTuning_ = movementTuning;
  movementTuning_.maxAirSpeed = movementTuning_.maxGroundSpeed;
  snapshot_.movementTuning = movementTuning_;
  playerSizeScaleXY_ = playerSizeScaleXY;
  playerSizeScaleZ_ = playerSizeScaleZ;
  snapshot_.playerSizeScaleXY = playerSizeScaleXY_;
  snapshot_.playerSizeScaleZ = playerSizeScaleZ_;
  lightningKnockback_ = lightningKnockback;
  lightningGunTuning_.knockbackPerSecond =
    lightningKnockbackToInternal(lightningKnockback_);
  snapshot_.lightningKnockback = lightningKnockback_;
  lightningFireHz_ = lightningFireHz;
  lightningGunTuning_.fireHz = lightningFireHz_;
  freezeGunTuning_.fireHz = lightningFireHz_;
  snapshot_.lightningFireHz = lightningFireHz_;
  rocketKnockback_ = rocketKnockback;
  rocketLauncherTuning_.knockback =
    q3KnockbackToInternal(rocketKnockback_);
  grenadeLauncherTuning_.knockback =
    q3KnockbackToInternal(rocketKnockback_);
  snapshot_.rocketKnockback = rocketKnockback_;
  knockbackTimeMs_ = std::clamp(knockbackTimeMs, 0, 250);
  snapshot_.knockbackTimeMs = knockbackTimeMs_;
  weaponDamage_ = weaponDamage;
  shotgunTuning_.damagePerPellet = weaponDamage_.shotgunDamagePerPellet;
  machineGunTuning_.damage = weaponDamage_.machineGunDamage;
  lightningGunTuning_.damagePerSecond =
    static_cast<float>(weaponDamage_.lightningGunDamage);
  freezeGunTuning_.damagePerSecond =
    static_cast<float>(weaponDamage_.freezeGunDamage);
  railgunTuning_.damage = weaponDamage_.railgunDamage;
  rocketLauncherTuning_.directDamage = weaponDamage_.rocketLauncherDamage;
  rocketLauncherTuning_.splashDamage = weaponDamage_.rocketLauncherDamage;
  grenadeLauncherTuning_.directDamage = weaponDamage_.rocketLauncherDamage;
  grenadeLauncherTuning_.splashDamage = weaponDamage_.rocketLauncherDamage;
  plasmaGunTuning_.damage = weaponDamage_.plasmaGunDamage;
  snapshot_.weaponDamage = weaponDamage_;
  if (vampirism_ != vampirism) {
    fractionalVampirismHealing_ = {};
  }
  vampirism_ = vampirism;
  snapshot_.vampirism = vampirism_;
  selfDamagePercent_ = selfDamagePercent;
  snapshot_.selfDamagePercent = selfDamagePercent_;
  const bool healthAmountChanged = healthAmount_ != healthAmount;
  healthAmount_ = healthAmount;
  snapshot_.healthAmount = healthAmount_;
  const bool infiniteAmmoChanged =
    weaponAmmoConfig_.infiniteAmmo != infiniteAmmo;
  weaponAmmoConfig_.infiniteAmmo = infiniteAmmo;
  snapshot_.weaponAmmo.infiniteAmmo = weaponAmmoConfig_.infiniteAmmo;
  if (infiniteAmmoChanged) {
    for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
      refillAmmo(playerIndex);
    }
  }
  if (
    botDodgeEnabled_ != botDodgeEnabled ||
    botDodgeMinIntervalMs_ != botDodgeMinIntervalMs ||
    botDodgeMaxIntervalMs_ != botDodgeMaxIntervalMs
  ) {
    setBotDodge(botDodgeEnabled, botDodgeMinIntervalMs, botDodgeMaxIntervalMs);
  }
  if (weaponSwitchingMode_ != weaponSwitchingMode) {
    setWeaponSwitchingMode(weaponSwitchingMode);
  }
  for (PlayerState& player : snapshot_.players) {
    const float previousHalfHeight = player.bounds.halfHeight;
    player.bounds.radius =
      kDefaultPlayerBounds.radius * playerSizeScaleXY_;
    player.bounds.halfHeight =
      kDefaultPlayerBounds.halfHeight * playerSizeScaleZ_;
    player.position.z += player.bounds.halfHeight - previousHalfHeight;
    player.position.z =
      std::clamp(
        player.position.z,
        arena_.min.z + player.bounds.halfHeight,
        arena_.max.z - player.bounds.halfHeight
      );
  }
  if (
    healthAmountChanged &&
    (
      snapshot_.matchPhase == MatchPhase::WaitingForPlayers ||
      snapshot_.matchPhase == MatchPhase::WaitingForReady
    )
  ) {
    respawnRound();
  }
}

void ServerGame::setWeaponSwitchingMode(WeaponSwitchingMode mode) {
  weaponSwitchingMode_ = mode;
  snapshot_.weaponSwitchingMode = weaponSwitchingMode_;
}

void ServerGame::setBotDodge(
  bool enabled,
  int minIntervalMs,
  int maxIntervalMs
) {
  botDodgeEnabled_ = enabled;
  botDodgeMinIntervalMs_ = std::clamp(minIntervalMs, 1, 10000);
  botDodgeMaxIntervalMs_ = std::clamp(maxIntervalMs, 1, 10000);
  if (botDodgeMinIntervalMs_ > botDodgeMaxIntervalMs_) {
    std::swap(botDodgeMinIntervalMs_, botDodgeMaxIntervalMs_);
  }
  snapshot_.botDodgeEnabled = botDodgeEnabled_;
  snapshot_.botDodgeMinIntervalMs = botDodgeMinIntervalMs_;
  snapshot_.botDodgeMaxIntervalMs = botDodgeMaxIntervalMs_;
  botDodgeSwitchSeconds_ = {};
}

void ServerGame::setBotBehavior(
  bool stareEnabled,
  bool standstillEnabled,
  bool dodgeEnabled,
  int dodgeMinIntervalMs,
  int dodgeMaxIntervalMs,
  BotAttackMode attackMode
) {
  botStareEnabled_ = stareEnabled;
  botStandstillEnabled_ = standstillEnabled;
  snapshot_.botStareEnabled = botStareEnabled_;
  snapshot_.botStandstillEnabled = botStandstillEnabled_;
  setBotDodge(dodgeEnabled, dodgeMinIntervalMs, dodgeMaxIntervalMs);
  setBotAttackMode(attackMode);
}

void ServerGame::setBotAttackMode(BotAttackMode mode) {
  if (botAttackMode_ != mode) {
    botCombatStates_ = {};
  }
  botAttackMode_ = mode;
  snapshot_.botAttackMode = botAttackMode_;
  if (botAttackMode_ == BotAttackMode::Off) {
    for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
      if (botPlayers_[index]) {
        commands_[index].attack = false;
      }
    }
  }
}

BotRosterChange ServerGame::addBots(std::optional<std::size_t> count) {
  if (!warmupPhase()) {
    return {false, 0, "bot_add is only allowed during warmup"};
  }

  const std::size_t requested = count.value_or(kDuelPlayerCount);
  std::size_t added = 0;
  for (
    std::size_t playerIndex = 0;
    playerIndex < kDuelPlayerCount && added < requested;
    ++playerIndex
  ) {
    if (isOccupiedSlot(playerIndex)) {
      continue;
    }
    addBotAtPlayerIndex(playerIndex);
    ++added;
  }

  refreshWarmupRosterState();
  return {
    true,
    added,
    "added " + std::to_string(added) + (added == 1 ? " bot" : " bots"),
  };
}

BotRosterChange ServerGame::kickAllBots() {
  if (!warmupPhase()) {
    return {false, 0, "bot_kick is only allowed during warmup"};
  }

  std::size_t kicked = 0;
  for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
    if (!botPlayers_[playerIndex]) {
      continue;
    }
    removeBotAtPlayerIndex(playerIndex);
    ++kicked;
  }

  refreshWarmupRosterState();
  return {
    true,
    kicked,
    "kicked " + std::to_string(kicked) + (kicked == 1 ? " bot" : " bots"),
  };
}

BotRosterChange ServerGame::kickBotAtPlayerIndex(std::size_t playerIndex) {
  if (!warmupPhase()) {
    return {false, 0, "bot_kick is only allowed during warmup"};
  }
  if (playerIndex >= kDuelPlayerCount) {
    return {
      false,
      0,
      std::string("bot_kick slot must be between 1 and ") +
        std::to_string(kDuelPlayerCount),
    };
  }
  if (!botPlayers_[playerIndex]) {
    return {
      false,
      0,
      "slot " + std::to_string(playerIndex + 1U) + " is not a bot",
    };
  }

  removeBotAtPlayerIndex(playerIndex);
  refreshWarmupRosterState();
  return {
    true,
    1,
    "kicked bot in slot " + std::to_string(playerIndex + 1U),
  };
}

WeaponSwitchingMode ServerGame::weaponSwitchingMode() const {
  return weaponSwitchingMode_;
}

bool ServerGame::botStareEnabled() const {
  return botStareEnabled_;
}

bool ServerGame::botStandstillEnabled() const {
  return botStandstillEnabled_;
}

bool ServerGame::botDodgeEnabled() const {
  return botDodgeEnabled_;
}

int ServerGame::botDodgeMinIntervalMs() const {
  return botDodgeMinIntervalMs_;
}

int ServerGame::botDodgeMaxIntervalMs() const {
  return botDodgeMaxIntervalMs_;
}

BotAttackMode ServerGame::botAttackMode() const {
  return botAttackMode_;
}

bool ServerGame::isBotSlot(std::size_t playerIndex) const {
  return playerIndex < kDuelPlayerCount && botPlayers_[playerIndex];
}

bool ServerGame::isHumanPlayer(std::size_t playerIndex) const {
  return playerIndex < kDuelPlayerCount && snapshot_.connectedPlayers[playerIndex];
}

bool ServerGame::isOccupiedSlot(std::size_t playerIndex) const {
  return isHumanPlayer(playerIndex) || isBotSlot(playerIndex);
}

std::array<bool, kDuelPlayerCount> ServerGame::occupiedPlayers() const {
  std::array<bool, kDuelPlayerCount> occupied = {};
  for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
    occupied[index] = isOccupiedSlot(index);
  }
  return occupied;
}

const MatchRules& ServerGame::matchRules() const {
  return matchRules_;
}

void ServerGame::updateMatchState() {
  if (!enoughPlayersConnected()) {
    if (snapshot_.matchPhase != MatchPhase::WaitingForPlayers) {
      for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
        snapshot_.readyPlayers[index] = botPlayers_[index];
      }
      snapshot_.scores = {};
      snapshot_.teamScores = {};
      snapshot_.mcguffinRoundsWon = {};
      snapshot_.mcguffinRound = 0;
      snapshot_.matchCombatStats = {};
      snapshot_.liveTicksElapsed = 0;
      snapshot_.roundWinner = 255;
      snapshot_.matchWinner = 255;
      snapshot_.roundWinningTeam = Team::None;
      snapshot_.matchWinningTeam = Team::None;
      respawnRound();
    }
    snapshot_.matchPhase = MatchPhase::WaitingForPlayers;
    snapshot_.phaseTicksRemaining = 0;
    return;
  }

  switch (snapshot_.matchPhase) {
  case MatchPhase::WaitingForPlayers:
    snapshot_.matchPhase = MatchPhase::WaitingForReady;
    break;
  case MatchPhase::WaitingForReady:
    if (allConnectedPlayersReady()) {
      beginCountdown();
    }
    break;
  case MatchPhase::Countdown:
    if (snapshot_.phaseTicksRemaining > 0) {
      --snapshot_.phaseTicksRemaining;
    }
    if (snapshot_.phaseTicksRemaining == 0) {
      snapshot_.matchPhase = MatchPhase::Live;
      snapshot_.roundWinner = 255;
    }
    break;
  case MatchPhase::RoundEnd:
    if (snapshot_.phaseTicksRemaining > 0) {
      --snapshot_.phaseTicksRemaining;
    }
    if (snapshot_.phaseTicksRemaining == 0) {
      // beginCountdown owns the round respawn. Running it here as well would
      // consume two spawn selections and discard the first result.
      beginCountdown();
    }
    break;
  case MatchPhase::MatchEnd:
    if (snapshot_.phaseTicksRemaining > 0) {
      --snapshot_.phaseTicksRemaining;
    }
    if (snapshot_.phaseTicksRemaining == 0) {
      snapshot_.scores = {};
      snapshot_.teamScores = {};
      snapshot_.mcguffinRoundsWon = {};
      snapshot_.mcguffinRound = 0;
      snapshot_.matchCombatStats = {};
      snapshot_.readyPlayers = {};
      snapshot_.liveTicksElapsed = 0;
      snapshot_.roundWinner = 255;
      snapshot_.matchWinner = 255;
      snapshot_.roundWinningTeam = Team::None;
      snapshot_.matchWinningTeam = Team::None;
      respawnRound();
      snapshot_.matchPhase = MatchPhase::WaitingForReady;
    }
    break;
  case MatchPhase::Live:
    break;
  }
}

void ServerGame::beginCountdown() {
  if (warmupPhase()) {
    snapshot_.matchCombatStats = {};
  }
  snapshot_.matchPhase = MatchPhase::Countdown;
  snapshot_.phaseTicksRemaining = matchRules_.countdownTicks;
  snapshot_.roundWinner = 255;
  snapshot_.roundWinningTeam = Team::None;
  respawnRound();
  if (snapshot_.phaseTicksRemaining == 0) {
    snapshot_.matchPhase = MatchPhase::Live;
  }
}

void ServerGame::beginRoundEnd(std::size_t winnerIndex) {
  awardDuelRound(snapshot_.scores, winnerIndex);
  snapshot_.roundWinner = static_cast<std::uint8_t>(winnerIndex);
  snapshot_.phaseTicksRemaining = matchRules_.roundEndTicks;
  snapshot_.matchPhase = MatchPhase::RoundEnd;
  if (hasWonDuel(snapshot_.scores, winnerIndex, matchRules_.roundLimit)) {
    beginMatchEnd(winnerIndex);
  }
}

void ServerGame::beginRoundEnd(Team winnerTeam) {
  awardClanArenaRound(snapshot_.teamScores, winnerTeam);
  snapshot_.roundWinningTeam = winnerTeam;
  snapshot_.phaseTicksRemaining = matchRules_.roundEndTicks;
  snapshot_.matchPhase = MatchPhase::RoundEnd;
  if (hasWonClanArena(snapshot_.teamScores, winnerTeam, matchRules_.roundLimit)) {
    beginMatchEnd(winnerTeam);
  }
}

void ServerGame::beginMatchEnd(std::size_t winnerIndex) {
  snapshot_.matchWinner = static_cast<std::uint8_t>(winnerIndex);
  snapshot_.matchPhase = MatchPhase::MatchEnd;
  snapshot_.phaseTicksRemaining = matchRules_.matchEndTicks;
}

void ServerGame::beginMatchEnd(Team winnerTeam) {
  snapshot_.matchWinningTeam = winnerTeam;
  snapshot_.matchPhase = MatchPhase::MatchEnd;
  snapshot_.phaseTicksRemaining = matchRules_.matchEndTicks;
}

bool ServerGame::enoughPlayersConnected() const {
  const std::array<bool, kDuelPlayerCount> occupied = occupiedPlayers();
  const std::size_t occupiedCount = static_cast<std::size_t>(std::count(
    occupied.begin(),
    occupied.end(),
    true
  ));
  if (occupiedCount < matchRules_.playerLimit) {
    return false;
  }
  if (snapshot_.gameMode == GameMode::Duel) {
    return occupiedCount >= 1U;
  }

  bool hasRedPlayer = false;
  bool hasBluePlayer = false;
  for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
    if (!occupied[index]) {
      continue;
    }
    hasRedPlayer = hasRedPlayer || snapshot_.teams[index] == Team::Red;
    hasBluePlayer = hasBluePlayer || snapshot_.teams[index] == Team::Blue;
  }
  return hasRedPlayer && hasBluePlayer;
}

bool ServerGame::allConnectedPlayersReady() const {
  if (!enoughPlayersConnected()) {
    return false;
  }

  const std::array<bool, kDuelPlayerCount> occupied = occupiedPlayers();
  bool hasRedPlayer = false;
  bool hasBluePlayer = false;
  for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
    if (!occupied[index]) {
      continue;
    }
    if (!snapshot_.readyPlayers[index]) {
      return false;
    }
    if (snapshot_.gameMode != GameMode::Duel) {
      if (!isPlayableTeam(snapshot_.teams[index])) {
        return false;
      }
      hasRedPlayer = hasRedPlayer || snapshot_.teams[index] == Team::Red;
      hasBluePlayer = hasBluePlayer || snapshot_.teams[index] == Team::Blue;
    }
  }
  if (snapshot_.gameMode != GameMode::Duel && (!hasRedPlayer || !hasBluePlayer)) {
    return false;
  }
  return true;
}

bool ServerGame::warmupPhase() const {
  return snapshot_.matchPhase == MatchPhase::WaitingForPlayers ||
    snapshot_.matchPhase == MatchPhase::WaitingForReady;
}

bool ServerGame::isActiveCombatant(std::size_t playerIndex) const {
  return playerIndex < kDuelPlayerCount &&
    isOccupiedSlot(playerIndex) &&
    snapshot_.participatingPlayers[playerIndex] &&
    snapshot_.players[playerIndex].health > 0;
}

bool ServerGame::isValidEnemyTarget(
  std::size_t attackerIndex,
  std::size_t targetIndex
) const {
  if (
    attackerIndex >= kDuelPlayerCount ||
    targetIndex >= kDuelPlayerCount ||
    attackerIndex == targetIndex ||
    !isActiveCombatant(attackerIndex) ||
    !isActiveCombatant(targetIndex)
  ) {
    return false;
  }
  return snapshot_.gameMode == GameMode::Duel
    ? areDuelOpponents(attackerIndex, targetIndex)
    : areClanArenaEnemies(snapshot_.teams, attackerIndex, targetIndex);
}

bool ServerGame::hasLineOfSight(
  std::size_t attackerIndex,
  std::size_t targetIndex
) const {
  if (
    attackerIndex >= kDuelPlayerCount ||
    targetIndex >= kDuelPlayerCount
  ) {
    return false;
  }
  const Vec3 start =
    weaponMuzzlePosition(snapshot_.players[attackerIndex], lightningGunTuning_.eyeHeight);
  const Vec3 target = botTargetAimPoint(snapshot_.players[targetIndex]);
  const Vec3 delta = target - start;
  const float distance = length(delta);
  if (distance <= 0.0001F) {
    return true;
  }
  const WorldTrace trace = traceWorld(arena_, start, delta / distance, distance);
  return trace.distance >= distance - 0.01F;
}

std::size_t ServerGame::nearestValidEnemy(
  std::size_t attackerIndex,
  bool requireLineOfSight
) const {
  std::size_t bestTarget = kDuelPlayerCount;
  float bestDistanceSquared = 0.0F;
  for (std::size_t targetIndex = 0; targetIndex < kDuelPlayerCount; ++targetIndex) {
    if (!isValidEnemyTarget(attackerIndex, targetIndex)) {
      continue;
    }
    if (requireLineOfSight && !hasLineOfSight(attackerIndex, targetIndex)) {
      continue;
    }
    const Vec3 delta =
      snapshot_.players[targetIndex].position - snapshot_.players[attackerIndex].position;
    const float distanceSquared = dot(delta, delta);
    if (bestTarget == kDuelPlayerCount || distanceSquared < bestDistanceSquared) {
      bestTarget = targetIndex;
      bestDistanceSquared = distanceSquared;
    }
  }
  return bestTarget;
}

bool ServerGame::damageAllowed(
  std::size_t attackerIndex,
  std::size_t targetIndex
) const {
  if (
    attackerIndex >= kDuelPlayerCount ||
    targetIndex >= kDuelPlayerCount
  ) {
    return false;
  }
  if (attackerIndex == targetIndex) {
    return true;
  }
  if (warmupPhase()) {
    return areDuelOpponents(attackerIndex, targetIndex);
  }
  return snapshot_.gameMode == GameMode::Duel
    ? areDuelOpponents(attackerIndex, targetIndex)
    : areClanArenaEnemies(snapshot_.teams, attackerIndex, targetIndex);
}

std::uint32_t ServerGame::weaponCooldownTicks(
  std::size_t playerIndex,
  Weapon weapon
) const {
  switch (weapon) {
  case Weapon::Railgun:
  case Weapon::Revolver:
    return railgunCooldownTicks_[playerIndex];
  case Weapon::MachineGun:
    return machineGunCooldownTicks_[playerIndex];
  case Weapon::Shotgun:
    return shotgunCooldownTicks_[playerIndex];
  case Weapon::RocketLauncher:
    return rocketCooldownTicks_[playerIndex];
  case Weapon::GrenadeLauncher:
    return grenadeCooldownTicks_[playerIndex];
  case Weapon::LightningGun:
  case Weapon::FreezeGun:
  case Weapon::PlasmaGun:
    return 0;
  }
  return 0;
}

bool ServerGame::canSwitchWeapon(std::size_t playerIndex) const {
  return weaponSwitchingMode_ == WeaponSwitchingMode::Crazy ||
    weaponCooldownTicks(playerIndex, selectedWeapons_[playerIndex]) == 0;
}

bool ServerGame::canFireSelectedWeapon(std::size_t playerIndex) const {
  return (
    weaponSwitchingMode_ != WeaponSwitchingMode::Ql ||
    weaponPulloutTicks_[playerIndex] == 0
  ) && hasAmmoForWeapon(playerIndex, selectedWeapons_[playerIndex]);
}

bool ServerGame::hasAmmoForWeapon(std::size_t playerIndex, Weapon weapon) const {
  return weaponAmmoConfig_.infiniteAmmo ||
    playerAmmo_[playerIndex][weaponIndex(weapon)] > 0;
}

void ServerGame::refillAmmo(std::size_t playerIndex) {
  if (playerIndex >= kDuelPlayerCount) {
    return;
  }
  playerAmmo_[playerIndex] = weaponAmmoConfig_.spawnAmmo;
  snapshot_.playerAmmo[playerIndex] = playerAmmo_[playerIndex];
}

bool ServerGame::consumeAmmo(std::size_t playerIndex, Weapon weapon) {
  if (weaponAmmoConfig_.infiniteAmmo) {
    return true;
  }
  std::int32_t& ammo = playerAmmo_[playerIndex][weaponIndex(weapon)];
  if (ammo <= 0) {
    return false;
  }
  --ammo;
  snapshot_.playerAmmo[playerIndex] = playerAmmo_[playerIndex];
  return true;
}

void ServerGame::consumeLightningGunAmmo(std::size_t playerIndex, float fixedDt) {
  if (weaponAmmoConfig_.infiniteAmmo) {
    return;
  }
  std::int32_t& ammo =
    playerAmmo_[playerIndex][weaponIndex(Weapon::LightningGun)];
  if (ammo <= 0) {
    snapshot_.playerAmmo[playerIndex] = playerAmmo_[playerIndex];
    return;
  }
  const double fireHz =
    static_cast<double>(std::max(1.0F, lightningGunTuning_.fireHz));
  double& credit = lightningAmmoCredit_[playerIndex];
  credit = std::min(credit, fireHz);
  const int shots = static_cast<int>(std::floor(credit));
  if (shots > 0) {
    const int consumed = std::min(shots, ammo);
    ammo -= consumed;
    credit -= static_cast<double>(consumed);
  }
  credit += fireHz * static_cast<double>(fixedDt);
  snapshot_.playerAmmo[playerIndex] = playerAmmo_[playerIndex];
}

void ServerGame::consumeFreezeGunAmmo(std::size_t playerIndex, float fixedDt) {
  if (weaponAmmoConfig_.infiniteAmmo) {
    return;
  }
  std::int32_t& ammo =
    playerAmmo_[playerIndex][weaponIndex(Weapon::FreezeGun)];
  if (ammo <= 0) {
    snapshot_.playerAmmo[playerIndex] = playerAmmo_[playerIndex];
    return;
  }
  const double fireHz =
    static_cast<double>(std::max(1.0F, freezeGunTuning_.fireHz));
  double& credit = freezeAmmoCredit_[playerIndex];
  credit = std::min(credit, fireHz);
  const int shots = static_cast<int>(std::floor(credit));
  if (shots > 0) {
    const int consumed = std::min(shots, ammo);
    ammo -= consumed;
    credit -= static_cast<double>(consumed);
  }
  credit += fireHz * static_cast<double>(fixedDt);
  snapshot_.playerAmmo[playerIndex] = playerAmmo_[playerIndex];
}

void ServerGame::decayIcePools(float fixedDt) {
  for (IcePool& pool : snapshot_.icePools) {
    if (!pool.active) {
      continue;
    }
    pool.lifetimeSeconds -= fixedDt;
    if (pool.lifetimeSeconds <= 0.0F || pool.radius <= 0.0F) {
      pool = {};
    }
  }
}

void ServerGame::growIcePool(Vec3 center, Vec3 normal, float fixedDt) {
  if (
    icePoolTuning_.maxRadius <= 0.0F ||
    icePoolTuning_.growthPerSecond <= 0.0F ||
    icePoolTuning_.lifetimeSeconds <= 0.0F
  ) {
    return;
  }

  IcePool* chosen = nullptr;
  IcePool* reusable = nullptr;
  for (IcePool& pool : snapshot_.icePools) {
    if (!pool.active) {
      if (reusable == nullptr) {
        reusable = &pool;
      }
      continue;
    }
    const Vec3 delta = center - pool.center;
    const float planeDistance = dot(delta, pool.normal);
    const Vec3 tangentDelta = delta - pool.normal * planeDistance;
    if (
      std::fabs(planeDistance) <= 0.5F &&
      length(tangentDelta) <= pool.radius + icePoolTuning_.mergeDistance
    ) {
      chosen = &pool;
      break;
    }
  }

  if (chosen == nullptr) {
    if (reusable == nullptr) {
      reusable = &snapshot_.icePools.front();
      for (IcePool& pool : snapshot_.icePools) {
        if (pool.lifetimeSeconds < reusable->lifetimeSeconds) {
          reusable = &pool;
        }
      }
    }
    *reusable = IcePool{
      true,
      center,
      normal,
      0.0F,
      icePoolTuning_.lifetimeSeconds,
    };
    chosen = reusable;
  }

  chosen->normal = normalize(chosen->normal + normal);
  chosen->lifetimeSeconds = icePoolTuning_.lifetimeSeconds;
  chosen->radius = std::min(
    icePoolTuning_.maxRadius,
    chosen->radius +
      (icePoolTuning_.maxRadius - chosen->radius) *
        icePoolTuning_.growthPerSecond *
        fixedDt
  );
}

void ServerGame::updateSelectedWeapon(
  std::size_t playerIndex,
  Weapon requestedWeapon
) {
  Weapon& selectedWeapon = selectedWeapons_[playerIndex];
  if (requestedWeapon == selectedWeapon) {
    snapshot_.selectedWeapons[playerIndex] = selectedWeapon;
    return;
  }
  if (!canSwitchWeapon(playerIndex)) {
    snapshot_.selectedWeapons[playerIndex] = selectedWeapon;
    return;
  }

  selectedWeapon = requestedWeapon;
  if (weaponSwitchingMode_ == WeaponSwitchingMode::Ql) {
    weaponPulloutTicks_[playerIndex] = weaponPulloutDurationTicks_;
  }
  snapshot_.selectedWeapons[playerIndex] = selectedWeapon;
}

void ServerGame::recordHistory() {
  history_.push_back(HistoryFrame{snapshot_.serverTick, snapshot_.players});
  while (history_.size() > kMaxLagCompensationTicks + 1U) {
    history_.pop_front();
  }
}

const ServerGame::HistoryFrame& ServerGame::historyFrameForTick(
  std::uint32_t serverTick
) const {
  for (auto frame = history_.rbegin(); frame != history_.rend(); ++frame) {
    if (frame->serverTick <= serverTick) {
      return *frame;
    }
  }
  return history_.front();
}

void ServerGame::applyDamageAndKnockback(
  std::size_t attackerIndex,
  std::size_t targetIndex,
  int damageApplied,
  Vec3 knockbackImpulse,
  Weapon weapon,
  bool headshot
) {
  if (targetIndex >= kDuelPlayerCount) {
    return;
  }
  const bool combatPhase =
    snapshot_.matchPhase == MatchPhase::Live ||
    snapshot_.matchPhase == MatchPhase::WaitingForPlayers ||
    snapshot_.matchPhase == MatchPhase::WaitingForReady;
  if (!combatPhase) {
    return;
  }

  PlayerState& attacker = snapshot_.players[attackerIndex];
  PlayerState& target = snapshot_.players[targetIndex];
  if (target.health <= 0) {
    return;
  }

  const bool wasAlive = target.health > 0;
  if (!damageAllowed(attackerIndex, targetIndex)) {
    damageApplied = 0;
  }
  damageApplied = std::min(damageApplied, target.health);
  target.health = std::max(0, target.health - damageApplied);
  target.velocity += knockbackImpulse;
  const std::uint16_t configuredKnockbackTicks =
    knockbackTimeMsToTicks(knockbackTimeMs_);
  if (
    configuredKnockbackTicks > 0 &&
    length(knockbackImpulse) > 0.0001F
  ) {
    target.knockbackTicksRemaining = std::max(
      target.knockbackTicksRemaining,
      configuredKnockbackTicks
    );
  }

  if (attackerIndex != targetIndex && damageApplied > 0) {
    const std::uint32_t sequence =
      ++localHitFeedbackSequences_[attackerIndex];
    const std::size_t eventSlot =
      static_cast<std::size_t>(sequence - 1U) %
      kLocalHitFeedbackEventWindow;
    LocalHitFeedbackEvent& event =
      snapshot_.localHitFeedbackEvents[attackerIndex][eventSlot];
    event.active = true;
    event.sequence = sequence;
    event.targetPlayerIndex = static_cast<std::uint8_t>(targetIndex);
    event.damageApplied = damageApplied;
    event.headshot = headshot;
    event.weapon = weapon;
  }

  if (
    attackerIndex != targetIndex &&
    attacker.health > 0 &&
    damageApplied > 0 &&
    vampirism_ > 0.0F
  ) {
    fractionalVampirismHealing_[attackerIndex] +=
      static_cast<double>(damageApplied) * static_cast<double>(vampirism_);
    const int healing = static_cast<int>(
      std::floor(fractionalVampirismHealing_[attackerIndex])
    );
    fractionalVampirismHealing_[attackerIndex] -=
      static_cast<double>(healing);
    attacker.health = std::min(healthAmount_, attacker.health + healing);
  }

  if (snapshot_.matchPhase == MatchPhase::Live) {
    addWeaponDamage(
      snapshot_.roundCombatStats[attackerIndex],
      weapon,
      static_cast<std::uint32_t>(damageApplied)
    );
  }
  if (combatStatsPhase(snapshot_.matchPhase)) {
    addWeaponDamage(
      snapshot_.matchCombatStats[attackerIndex],
      weapon,
      static_cast<std::uint32_t>(damageApplied)
    );
  }

  if (
    wasAlive &&
    target.health == 0 &&
    damageApplied > 0 &&
    (
      attackerIndex == targetIndex ||
      damageAllowed(attackerIndex, targetIndex)
    )
  ) {
    FragEvent& frag = snapshot_.fragEvents[attackerIndex];
    frag.active = true;
    frag.sequence = ++fragEventSequences_[attackerIndex];
    frag.targetPlayerIndex = static_cast<std::uint8_t>(targetIndex);
    frag.weapon = weapon;
  }

  if (
    wasAlive &&
    target.health == 0 &&
    snapshot_.matchPhase == MatchPhase::Live
  ) {
    if (
      snapshot_.gameMode == GameMode::ClanArena &&
      areClanArenaEnemies(snapshot_.teams, attackerIndex, targetIndex)
    ) {
      ++snapshot_.scores[attackerIndex];
    }
    target.knockbackTicksRemaining = 0;
    target.freezeLevel = 0.0F;
    target.velocity = {};
    lightningGunStates_[targetIndex] = {};
    freezeGunStates_[targetIndex] = {};
    snapshot_.lightningGuns[targetIndex] = {};
    snapshot_.weaponFires[targetIndex] = {};
    if (snapshot_.gameMode == GameMode::Duel) {
      std::optional<std::size_t> winner;
      std::size_t aliveCount = 0;
      for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
        if (!isOccupiedSlot(index) || snapshot_.players[index].health <= 0) {
          continue;
        }
        ++aliveCount;
        winner = index;
      }
      if (aliveCount != 1U) {
        winner = std::nullopt;
      }
      if (winner.has_value()) {
        beginRoundEnd(*winner);
      }
    } else if (snapshot_.gameMode == GameMode::ClanArena) {
      std::array<bool, kDuelPlayerCount> alivePlayers = {};
      for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
        alivePlayers[index] =
          isOccupiedSlot(index) &&
          snapshot_.players[index].health > 0;
      }
      const auto winner = clanArenaRoundWinner(
        occupiedPlayers(),
        snapshot_.teams,
        alivePlayers
      );
      if (winner.has_value()) {
        beginRoundEnd(*winner);
      }
    } else {
      dropMcGuffinCarrier(targetIndex);
      if (matchRules_.deathRespawnTicks == 0) {
        respawnPlayer(targetIndex);
      } else {
        snapshot_.respawnTicksRemaining[targetIndex] =
          matchRules_.deathRespawnTicks;
      }
    }
    return;
  }
  if (
    target.health == 0 &&
    (
      snapshot_.matchPhase == MatchPhase::WaitingForPlayers ||
      snapshot_.matchPhase == MatchPhase::WaitingForReady
    )
  ) {
    const FragEvent fragEvent = snapshot_.fragEvents[attackerIndex];
    respawnPlayer(targetIndex);
    if (fragEvent.active) {
      snapshot_.fragEvents[attackerIndex] = fragEvent;
    }
  }
}

bool ServerGame::spawnProjectile(
  std::size_t attackerIndex,
  const PlayerState& attacker,
  const UserCommand& command,
  Weapon weapon
) {
  for (RocketProjectile& rocket : rockets_) {
    if (rocket.active) {
      continue;
    }

    const bool grenade = weapon == Weapon::GrenadeLauncher;
    const bool plasma = weapon == Weapon::PlasmaGun;
    const float eyeHeight = grenade
      ? grenadeLauncherTuning_.eyeHeight
      : plasma
        ? plasmaGunTuning_.eyeHeight
        : rocketLauncherTuning_.eyeHeight;
    const float speed = grenade
      ? grenadeLauncherTuning_.speed
      : plasma
        ? plasmaGunTuning_.speed
        : rocketLauncherTuning_.speed;
    const Vec3 direction =
      cameraForward(command.viewYawRadians, command.viewPitchRadians);

    rocket.active = true;
    rocket.owner = static_cast<std::uint8_t>(attackerIndex);
    rocket.weapon = weapon;
    rocket.position = weaponMuzzlePosition(attacker, eyeHeight);
    rocket.previousPosition = rocket.position;
    rocket.projectileRadius = grenade ? grenadeLauncherTuning_.projectileRadius : 0.0F;
    rocket.projectileHitboxRadius = grenade ? grenadeLauncherTuning_.projectileHitboxRadius : 0.0F;
    rocket.velocity = direction * speed;
    if (grenade) {
      rocket.velocity.z += grenadeLauncherTuning_.verticalBoost;
    }
    rocket.ageTicks = 0;
    rocket.ownerCollisionArmed = false;
    rocket.resting = false;

    WeaponFireResult& fire = snapshot_.weaponFires[attackerIndex];
    fire.fired = true;
    fire.weapon = weapon;
    fire.visualSeed = command.sequence;
    fire.start = rocket.position;
    fire.end = rocket.position + (direction * 1.2F);
    recordWeaponAccuracy(snapshot_, attackerIndex, weapon, 1U, 0U);
    return true;
  }

  return false;
}

void ServerGame::simulateRockets(float fixedDt) {
  const auto cylinderDistance = [](Vec3 point, const PlayerState& player) {
    const float radial =
      std::max(
        0.0F,
        std::hypot(point.x - player.position.x, point.y - player.position.y) -
          player.bounds.radius
      );
    const float vertical =
      std::max(0.0F, std::fabs(point.z - player.position.z) - player.bounds.halfHeight);
    return std::hypot(radial, vertical);
  };
  const auto projectileDirectAabbHalfExtents = [](
    Weapon weapon,
    const PlayerState& target,
    const RocketLauncherTuning& rocketLauncherTuning,
    const PlasmaGunTuning& plasmaGunTuning
  ) {
    const float scaleXY =
      target.bounds.radius / std::max(0.0001F, kDefaultPlayerBounds.radius);
    const float scaleZ =
      target.bounds.halfHeight / std::max(0.0001F, kDefaultPlayerBounds.halfHeight);
    const float baseXY = weapon == Weapon::PlasmaGun
      ? plasmaGunTuning.directHitboxHalfExtentXY
      : rocketLauncherTuning.directHitboxHalfExtentXY;
    const float baseZ = weapon == Weapon::PlasmaGun
      ? plasmaGunTuning.directHitboxHalfExtentZ
      : rocketLauncherTuning.directHitboxHalfExtentZ;
    return Vec3{baseXY * scaleXY, baseXY * scaleXY, baseZ * scaleZ};
  };
  const auto pointInsidePlayerRelativeAabb = [](
    Vec3 point,
    const PlayerState& player,
    Vec3 halfExtents
  ) {
    const Vec3 relative = point - player.position;
    return
      std::fabs(relative.x) <= halfExtents.x + kProjectileCollisionEpsilon &&
      std::fabs(relative.y) <= halfExtents.y + kProjectileCollisionEpsilon &&
      std::fabs(relative.z) <= halfExtents.z + kProjectileCollisionEpsilon;
  };

  for (std::size_t projectileIndex = 0; projectileIndex < rockets_.size(); ++projectileIndex) {
    RocketProjectile& rocket = rockets_[projectileIndex];
    if (!rocket.active) {
      continue;
    }

    bool explode = false;
    Vec3 explosionPosition = rocket.position;
    std::size_t directTarget = kDuelPlayerCount;
    const bool grenade = rocket.weapon == Weapon::GrenadeLauncher;
    const bool plasma = rocket.weapon == Weapon::PlasmaGun;
    const bool projectileDirectAabb =
      rocket.weapon == Weapon::RocketLauncher || plasma;

    rocket.previousPosition = rocket.position;
    if (grenade && rocket.resting) {
      ++rocket.ageTicks;
      if (rocket.ageTicks >= grenadeLauncherTuning_.fuseTicks) {
        explode = true;
      } else {
        continue;
      }
    } else {
      if (grenade) {
        rocket.velocity.z -= grenadeLauncherTuning_.gravity * fixedDt;
      }
      const Vec3 nextPosition = rocket.position + (rocket.velocity * fixedDt);
      const Vec3 segment = nextPosition - rocket.position;
      const float segmentLength = length(segment);
      const Vec3 direction = segmentLength > 0.0F
        ? segment / segmentLength
        : normalize(rocket.velocity);

      if (!rocket.ownerCollisionArmed) {
        if (projectileDirectAabb) {
          const PlayerState& owner = snapshot_.players[rocket.owner];
          const Vec3 ownerHalfExtents = projectileDirectAabbHalfExtents(
            rocket.weapon,
            owner,
            rocketLauncherTuning_,
            plasmaGunTuning_
          );
          rocket.ownerCollisionArmed =
            !pointInsidePlayerRelativeAabb(rocket.position, owner, ownerHalfExtents);
        } else if (
          cylinderDistance(rocket.position, snapshot_.players[rocket.owner]) >
            rocket.projectileHitboxRadius + 0.0001F
        ) {
          rocket.ownerCollisionArmed = true;
        }
      }

      explosionPosition = nextPosition;
      if (segmentLength > 0.0F) {
        const WorldTrace worldTrace =
          traceWorld(arena_, rocket.position, direction, segmentLength);
        if (worldTrace.distance < segmentLength - 0.0001F) {
          explosionPosition = worldTrace.end;
          if (grenade) {
            Vec3 normal = bounceNormalForPoint(arena_, explosionPosition);
            if (dot(rocket.velocity, normal) > 0.0F) {
              normal *= -1.0F;
            }
            const float normalVelocity = dot(rocket.velocity, normal);
            const float impactSpeed = std::fabs(normalVelocity);
            if (normalVelocity < 0.0F) {
              rocket.velocity =
                (rocket.velocity - (normal * (2.0F * normalVelocity))) *
                grenadeLauncherTuning_.bounceDamping;
            } else {
              rocket.velocity *= grenadeLauncherTuning_.bounceDamping;
            }
            const bool restingOnFloor =
              normal.z > 0.5F && length(rocket.velocity) <= grenadeLauncherTuning_.restSpeed;
            if (restingOnFloor) {
              rocket.velocity = {};
              rocket.resting = true;
            }
            if (impactSpeed >= grenadeLauncherTuning_.bounceSoundMinSpeed) {
              GrenadeBounceAudioEvent& bounce =
                snapshot_.grenadeBounceAudioEvents[projectileIndex];
              bounce.active = true;
              bounce.sequence = ++grenadeBounceSequences_[projectileIndex];
              bounce.position = explosionPosition;
            }
            rocket.position =
              explosionPosition + (normal * (2.0F * kProjectileCollisionEpsilon));
            ++rocket.ageTicks;
            if (rocket.ageTicks >= grenadeLauncherTuning_.fuseTicks) {
              explode = true;
            } else {
              continue;
            }
          } else {
            explode = true;
          }
        }

        float bestHitDistance = segmentLength;
        const bool directHitEnabled =
          projectileDirectAabb || rocket.projectileHitboxRadius > 0.0F;
        if (!explode && directHitEnabled) {
          for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
            if (
              snapshot_.players[playerIndex].health <= 0 ||
              !isCombatant(snapshot_, playerIndex) ||
              (playerIndex == rocket.owner && !rocket.ownerCollisionArmed)
            ) {
              continue;
            }
            float hitDistance = 0.0F;
            const PlayerState& target = snapshot_.players[playerIndex];
            bool hit = false;
            if (projectileDirectAabb) {
              hit = tracePlayerProjectileDirectAabb(
                rocket.position,
                direction,
                target,
                bestHitDistance,
                projectileDirectAabbHalfExtents(
                  rocket.weapon,
                  target,
                  rocketLauncherTuning_,
                  plasmaGunTuning_
                ),
                hitDistance
              );
            } else {
              PlayerState projectileTarget = target;
              projectileTarget.bounds.radius += rocket.projectileHitboxRadius;
              projectileTarget.bounds.halfHeight += rocket.projectileHitboxRadius;
              hit = tracePlayerCylinder(
                rocket.position,
                direction,
                projectileTarget,
                bestHitDistance,
                hitDistance
              );
            }
            if (hit) {
              explode = true;
              bestHitDistance = hitDistance;
              directTarget = playerIndex;
              explosionPosition = rocket.position + (direction * hitDistance);
            }
          }
        }
      }

      ++rocket.ageTicks;
      const std::uint32_t maxLifetimeTicks = grenade
        ? grenadeLauncherTuning_.fuseTicks
        : plasma
          ? plasmaGunTuning_.maxLifetimeTicks
          : rocketLauncherTuning_.maxLifetimeTicks;
      if (!explode && rocket.ageTicks >= maxLifetimeTicks) {
        explode = true;
        explosionPosition = nextPosition;
      }

      if (!explode) {
        rocket.position = nextPosition;
        continue;
      }
    }

    rocket.active = false;
    RocketExplosionResult& explosion = snapshot_.rocketExplosions[rocket.owner];
    explosion.active = true;
    explosion.weapon = rocket.weapon;
    explosion.sequence = ++rocketExplosionSequences_[rocket.owner];
    explosion.position = explosionPosition;
    const float radius = grenade
      ? grenadeLauncherTuning_.radius
      : plasma
        ? plasmaGunTuning_.radius
        : rocketLauncherTuning_.radius;
    const int directDamage = grenade
      ? grenadeLauncherTuning_.directDamage
      : plasma
        ? plasmaGunTuning_.damage
        : rocketLauncherTuning_.directDamage;
    const int splashDamage = grenade
      ? grenadeLauncherTuning_.splashDamage
      : plasma
        ? plasmaGunTuning_.damage
        : rocketLauncherTuning_.splashDamage;
    const float knockback = grenade
      ? grenadeLauncherTuning_.knockback
      : plasma
        ? plasmaGunTuning_.knockback
        : rocketLauncherTuning_.knockback;
    explosion.radius = radius;
    bool hitOpponent = false;

    for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
      PlayerState& player = snapshot_.players[playerIndex];
      if (player.health <= 0 || !isCombatant(snapshot_, playerIndex)) {
        continue;
      }
      const float distance = cylinderDistance(explosionPosition, player);
      if (distance > radius) {
        continue;
      }
      const float falloff =
        1.0F - (distance / std::max(0.001F, radius));
      int damage = static_cast<int>(std::ceil(
        static_cast<float>(splashDamage) * falloff
      ));
      if (playerIndex == directTarget) {
        damage = std::max(damage, directDamage);
      }
      const int knockbackDamage = damage;
      int appliedDamage = playerIndex == rocket.owner
        ? (damage * static_cast<int>(selfDamagePercent_) + 50) / 100
        : damage;
      if (!damageAllowed(rocket.owner, playerIndex)) {
        appliedDamage = 0;
      }
      if (playerIndex == rocket.owner) {
        explosion.ownerDamageApplied = std::min(appliedDamage, player.health);
      } else {
        explosion.opponentDamageApplied = std::min(appliedDamage, player.health);
        if (!hitOpponent && appliedDamage > 0) {
          recordProjectileHit(snapshot_, rocket.owner, rocket.weapon);
          hitOpponent = true;
        }
      }
      Vec3 knockbackDirection = normalize(player.position - explosionPosition);
      if (length(knockbackDirection) <= 0.0001F) {
        knockbackDirection = normalize(rocket.velocity);
      }
      const float knockbackScale =
        static_cast<float>(knockbackDamage) /
        static_cast<float>(std::max(1, splashDamage));
      applyDamageAndKnockback(
        rocket.owner,
        playerIndex,
        appliedDamage,
        knockbackDirection * knockback * knockbackScale,
        rocket.weapon,
        false
      );
    }
  }

  for (std::size_t index = 0; index < rockets_.size(); ++index) {
    snapshot_.rockets[index].active = rockets_[index].active;
    snapshot_.rockets[index].owner = rockets_[index].owner;
    snapshot_.rockets[index].weapon = rockets_[index].weapon;
    snapshot_.rockets[index].position = rockets_[index].position;
    snapshot_.rockets[index].velocity = rockets_[index].velocity;
    snapshot_.rockets[index].radius = rockets_[index].projectileRadius;
  }
}

void ServerGame::updateFootstepAudioEvents() {
  constexpr float kMinimumStepSpeed = 1.15F;
  constexpr float kMinimumJumpAudioSpeed = 1.0F;
  constexpr float kBaseStrideDistance = 1.45F;
  constexpr float kMinimumStrideDistance = 0.95F;

  for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
    FootstepState& state = footstepStates_[playerIndex];
    const PlayerState& player = snapshot_.players[playerIndex];
    if (!snapshot_.participatingPlayers[playerIndex] || player.health <= 0) {
      state = {};
      continue;
    }

    if (!state.initialized) {
      state.previousPosition = player.position;
      state.wasOnGround = player.onGround;
      state.initialized = true;
      continue;
    }

    const float horizontalSpeed = std::hypot(player.velocity.x, player.velocity.y);
    const Vec3 delta = player.position - state.previousPosition;
    const float horizontalDistance = std::hypot(delta.x, delta.y);
    const bool movingOnGround =
      player.onGround && horizontalSpeed >= kMinimumStepSpeed;

    auto emitMovementSound = [&](bool jumping, bool landing) {
      FootstepAudioEvent& event = snapshot_.footstepAudioEvents[playerIndex];
      event.active = true;
      event.jumping = jumping;
      event.landing = landing;
      event.sequence = ++footstepSequences_[playerIndex];
      event.position = player.position;
    };

    if (
      !player.onGround &&
      state.wasOnGround &&
      player.velocity.z >= kMinimumJumpAudioSpeed
    ) {
      emitMovementSound(true, false);
      state.distanceSinceStep = 0.0F;
    } else if (player.onGround && !state.wasOnGround) {
      emitMovementSound(false, true);
      state.distanceSinceStep = 0.0F;
    } else if (movingOnGround && !player.crouched && !player.sneaking) {
      state.distanceSinceStep += horizontalDistance;
      const float strideDistance = std::max(
        kMinimumStrideDistance,
        kBaseStrideDistance - (horizontalSpeed * 0.045F)
      );
      if (state.distanceSinceStep >= strideDistance) {
        emitMovementSound(false, false);
        state.distanceSinceStep = std::fmod(state.distanceSinceStep, strideDistance);
      }
    } else if (player.crouched || player.sneaking) {
      state.distanceSinceStep = 0.0F;
    } else if (!player.onGround || horizontalSpeed < 0.25F) {
      state.distanceSinceStep = 0.0F;
    }

    state.previousPosition = player.position;
    state.wasOnGround = player.onGround;
  }
}

void ServerGame::resetHealthPickups() {
  healthPickupCooldownTicks_ = {};
  snapshot_.healthPickupAvailable = {};
  for (std::size_t index = 0; index < arena_.healthPickupCount; ++index) {
    snapshot_.healthPickupAvailable[index] = true;
  }
}

void ServerGame::updateHealthPickups() {
  for (std::size_t pickupIndex = 0; pickupIndex < arena_.healthPickupCount; ++pickupIndex) {
    if (!snapshot_.healthPickupAvailable[pickupIndex]) {
      continue;
    }
    const ArenaHealthPickup& pickup = arena_.healthPickups[pickupIndex];
    const std::int32_t healAmount = pickup.type == HealthPickupType::Large
      ? largeHealthPickupAmount_
      : smallHealthPickupAmount_;
    if (healAmount <= 0) {
      continue;
    }

    for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
      if (!isCombatant(snapshot_, playerIndex)) {
        continue;
      }
      PlayerState& player = snapshot_.players[playerIndex];
      if (player.health >= healthAmount_) {
        continue;
      }
      const Vec3 delta = player.position - pickup.position;
      const float touchRadius = player.bounds.radius + kHealthPickupTouchRadius;
      if (
        (delta.x * delta.x) + (delta.y * delta.y) > touchRadius * touchRadius ||
        std::fabs(delta.z) > player.bounds.halfHeight + kHealthPickupTouchHalfHeight
      ) {
        continue;
      }

      player.health = std::min(healthAmount_, player.health + healAmount);
      snapshot_.healthPickupAvailable[pickupIndex] = false;
      healthPickupCooldownTicks_[pickupIndex] =
        pickup.type == HealthPickupType::Large
          ? largeHealthPickupCooldownTicks_
          : smallHealthPickupCooldownTicks_;
      break;
    }
  }
}

void ServerGame::recordMcGuffinEvent(
  McGuffinEventType event,
  std::size_t playerIndex
) {
  ++snapshot_.mcguffin.eventSequence;
  snapshot_.mcguffin.lastEvent = event;
  snapshot_.mcguffin.eventPlayerIndex = playerIndex < kDuelPlayerCount
    ? static_cast<std::uint8_t>(playerIndex)
    : kNoMcGuffinCarrier;
}

void ServerGame::resetMcGuffinRound() {
  resetMcGuffin(mcguffinObjective_, arena_.mcguffin.neutralSpawn);
  snapshot_.mcguffinScores = {};
  mcguffinStealTicks_ = {};
  mcguffinCarrySubPoints_ = 0;
  mcguffinCarriedPoints_ = 0;
  mcguffinFinalHoldTicks_ = 0;
  mcguffinRoundLiveTicks_ = 0;
  mcguffinThrowPickupLockoutTicks_ = 0;
  if (snapshot_.mcguffinRound == 1U) {
    snapshot_.mcguffinRedBaseOwner = Team::Blue;
    snapshot_.mcguffinBlueBaseOwner = Team::Red;
  } else if (snapshot_.mcguffinRound >= 2U) {
    // The deciding round's first valid delivery claims a base.
    snapshot_.mcguffinRedBaseOwner = Team::None;
    snapshot_.mcguffinBlueBaseOwner = Team::None;
  } else {
    snapshot_.mcguffinRedBaseOwner = Team::Red;
    snapshot_.mcguffinBlueBaseOwner = Team::Blue;
  }
  snapshot_.mcguffin.lastEvent = McGuffinEventType::None;
  snapshot_.mcguffin.eventPlayerIndex = kNoMcGuffinCarrier;
  snapshot_.mcguffin.state = mcguffinObjective_.state;
  snapshot_.mcguffin.associatedTeam = Team::None;
  snapshot_.mcguffin.carrierTeam = Team::None;
  snapshot_.mcguffin.carrierIndex = kNoMcGuffinCarrier;
  snapshot_.mcguffin.position = mcguffinObjective_.position;
  snapshot_.mcguffin.velocity = {};
  snapshot_.mcguffin.stateTicks = 0;
  snapshot_.mcguffin.scoreSubPoints = 0;
  snapshot_.mcguffin.carrySubPoints = 0;
  snapshot_.mcguffin.carriedPoints = 0;
  snapshot_.mcguffin.interactionTicks = 0;
  snapshot_.mcguffin.finalHoldTicks = 0;
}

void ServerGame::dropMcGuffinCarrier(std::size_t playerIndex) {
  if (
    mcguffinObjective_.state != McGuffinState::Carried ||
    mcguffinObjective_.carrierIndex != playerIndex ||
    playerIndex >= kDuelPlayerCount
  ) {
    return;
  }
  Vec3 position = snapshot_.players[playerIndex].position;
  position.z = std::max(arena_.min.z, position.z - snapshot_.players[playerIndex].bounds.halfHeight);
  if (dropMcGuffin(mcguffinObjective_, playerIndex, position)) {
    recordMcGuffinEvent(McGuffinEventType::Drop, playerIndex);
    // Damage is resolved after the objective update in the tick. Mirror the
    // drop immediately so the snapshot published this tick never names a dead
    // or disconnected carrier.
    snapshot_.mcguffin.state = mcguffinObjective_.state;
    snapshot_.mcguffin.carrierTeam = Team::None;
    snapshot_.mcguffin.carrierIndex = kNoMcGuffinCarrier;
    snapshot_.mcguffin.position = mcguffinObjective_.position;
    snapshot_.mcguffin.velocity = {};
    snapshot_.mcguffin.stateTicks = 0;
  }
}

void ServerGame::beginMcGuffinRoundEnd(Team winnerTeam) {
  const std::size_t index = winnerTeam == Team::Red ? 0U : 1U;
  if (!isPlayableTeam(winnerTeam)) return;
  ++snapshot_.mcguffinRoundsWon[index];
  ++snapshot_.mcguffinRound;
  snapshot_.roundWinningTeam = winnerTeam;
  snapshot_.mcguffinScores[index] = mcguffinConfig_.scoreLimit;
  recordMcGuffinEvent(McGuffinEventType::RoundWin, kDuelPlayerCount);
  if (snapshot_.mcguffinRoundsWon[index] >= 2U) {
    beginMatchEnd(winnerTeam);
    return;
  }
  snapshot_.matchPhase = MatchPhase::RoundEnd;
  snapshot_.phaseTicksRemaining = matchRules_.roundEndTicks;
}

void ServerGame::updateMcGuffin() {
  if (snapshot_.gameMode != GameMode::McGuffin) return;

  if (snapshot_.matchPhase == MatchPhase::Live) {
    ++mcguffinRoundLiveTicks_;
  }
  for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
    if (snapshot_.respawnTicksRemaining[index] == 0) continue;
    --snapshot_.respawnTicksRemaining[index];
    if (snapshot_.respawnTicksRemaining[index] == 0 && isOccupiedSlot(index)) {
      respawnPlayer(index);
    }
  }

  if (snapshot_.matchPhase != MatchPhase::Live) return;

  if (mcguffinThrowPickupLockoutTicks_ > 0) {
    --mcguffinThrowPickupLockoutTicks_;
  }

  // Throw requests are latched from newly accepted packets, then executed
  // after movement so origin and inherited velocity use this tick's state.
  if (mcguffinObjective_.state == McGuffinState::Carried) {
    const std::size_t carrier = mcguffinObjective_.carrierIndex;
    if (carrier < kDuelPlayerCount && mcguffinThrowRequestedThisTick_[carrier] &&
        isActiveCombatant(carrier)) {
      const UserCommand& command = mcguffinThrowCommands_[carrier];
      const Vec3 forward = cameraForward(
        command.viewYawRadians, command.viewPitchRadians
      );
      const Vec3 velocity =
        (forward * mcguffinConfig_.throwSpeed) +
        Vec3{0.0F, 0.0F, mcguffinConfig_.throwUpSpeed} +
        (snapshot_.players[carrier].velocity *
          mcguffinConfig_.throwVelocityInheritance);
      const Vec3 carrierPosition = snapshot_.players[carrier].position;
      constexpr float kThrowOriginOffset = 0.8F;
      const WorldTrace originTrace = traceWorld(
        arena_, carrierPosition, forward, kThrowOriginOffset
      );
      // Keep the launch point on the carrier side of nearby geometry. The
      // ordinary dropped-object sweep takes over from this clamped position.
      const float originDistance = std::max(
        0.0F,
        std::min(kThrowOriginOffset, originTrace.distance) -
          (2.0F * kProjectileCollisionEpsilon)
      );
      const Vec3 origin = carrierPosition + (forward * originDistance);
      if (throwMcGuffin(mcguffinObjective_, carrier, origin, velocity)) {
        mcguffinThrowPickupLockoutTicks_ =
          mcguffinConfig_.throwPickupLockoutTicks;
        // Carry credit belongs to the current objective run, not one carrier,
        // so a deliberate pass preserves the same bankable reserve as a drop.
        recordMcGuffinEvent(McGuffinEventType::Throw, carrier);
      }
    }
  }

  if (mcguffinObjective_.state == McGuffinState::Carried) {
    const std::size_t carrier = mcguffinObjective_.carrierIndex;
    if (
      carrier >= kDuelPlayerCount || !isActiveCombatant(carrier) ||
      snapshot_.teams[carrier] != mcguffinObjective_.carrierTeam
    ) {
      dropMcGuffinCarrier(carrier);
    } else {
      mcguffinObjective_.position = snapshot_.players[carrier].position;
    }
  }

  if (mcguffinObjective_.state == McGuffinState::Dropped &&
      length(mcguffinObjective_.velocity) > 0.0F) {
    mcguffinObjective_.velocity.z -=
      mcguffinConfig_.throwGravity * kFixedTickSeconds;
    const Vec3 start = mcguffinObjective_.position;
    const Vec3 next = start +
      (mcguffinObjective_.velocity * kFixedTickSeconds);
    const Vec3 segment = next - start;
    const float distance = length(segment);
    if (distance > 0.000001F) {
      const WorldTrace trace = traceWorld(arena_, start, segment / distance, distance);
      if (trace.distance < distance - kProjectileCollisionEpsilon) {
        Vec3 normal = bounceNormalForPoint(arena_, trace.end);
        if (dot(mcguffinObjective_.velocity, normal) > 0.0F) normal *= -1.0F;
        const float normalVelocity = dot(mcguffinObjective_.velocity, normal);
        if (normalVelocity < 0.0F) {
          mcguffinObjective_.velocity =
            (mcguffinObjective_.velocity - (normal * (2.0F * normalVelocity))) *
            mcguffinConfig_.throwBounceDamping;
        }
        if (normal.z > 0.5F && length(mcguffinObjective_.velocity) < 0.5F) {
          mcguffinObjective_.velocity = {};
        }
        mcguffinObjective_.position = trace.end +
          (normal * (2.0F * kProjectileCollisionEpsilon));
      } else {
        mcguffinObjective_.position = next;
      }
    }
  }

  auto baseCenter = [](const ArenaMcGuffinBase& base) {
    return (base.min + base.max) * 0.5F;
  };
  const auto ownerForPosition = [&](Vec3 position) {
    if (pointInsideMcGuffinBase(position, arena_.mcguffin.redBase)) {
      return snapshot_.mcguffinRedBaseOwner;
    }
    if (pointInsideMcGuffinBase(position, arena_.mcguffin.blueBase)) {
      return snapshot_.mcguffinBlueBaseOwner;
    }
    return Team::None;
  };

  if (
    mcguffinObjective_.state == McGuffinState::NeutralSpawn &&
    mcguffinRoundLiveTicks_ < mcguffinConfig_.initialSpawnTicks
  ) {
    mcguffinObjective_.stateTicks = mcguffinRoundLiveTicks_;
  } else if (
    mcguffinObjective_.state == McGuffinState::NeutralSpawn ||
    mcguffinObjective_.state == McGuffinState::Dropped
  ) {
    if (mcguffinObjective_.state == McGuffinState::NeutralSpawn) {
      mcguffinObjective_.stateTicks = mcguffinConfig_.initialSpawnTicks;
    }
    for (std::size_t index = 0;
         mcguffinThrowPickupLockoutTicks_ == 0 && index < kDuelPlayerCount;
         ++index) {
      if (!isActiveCombatant(index) || !isPlayableTeam(snapshot_.teams[index])) continue;
      if (tryPickupMcGuffin(
        mcguffinObjective_, mcguffinConfig_, index, snapshot_.teams[index],
        snapshot_.players[index].position
      )) {
        recordMcGuffinEvent(McGuffinEventType::Pickup, index);
        break;
      }
    }
  }

  bool carrierAtOwnBase = false;
  Team installedAtBase = Team::None;
  Vec3 installedPosition = {};
  ArenaSpawnGroup pendingClaimGroup = ArenaSpawnGroup::None;
  if (mcguffinObjective_.state == McGuffinState::Carried) {
    const std::size_t carrier = mcguffinObjective_.carrierIndex;
    if (carrier < kDuelPlayerCount) {
      const Vec3 position = snapshot_.players[carrier].position;
      const bool inRed = pointInsideMcGuffinBase(position, arena_.mcguffin.redBase);
      const bool inBlue = pointInsideMcGuffinBase(position, arena_.mcguffin.blueBase);
      const bool canClaimBase = snapshot_.mcguffinRound >= 2U && (inRed || inBlue) &&
        snapshot_.mcguffinRedBaseOwner == Team::None &&
        snapshot_.mcguffinBlueBaseOwner == Team::None;
      if (canClaimBase) {
        // Treat the carrier as provisionally owning this trigger so the normal
        // installation timer can run. Ownership is committed only after that
        // timer completes, allowing a touch-and-retreat to leave both bases free.
        pendingClaimGroup = inRed
          ? ArenaSpawnGroup::RedBase
          : ArenaSpawnGroup::BlueBase;
      }
      const Team owner = canClaimBase
        ? mcguffinObjective_.carrierTeam
        : ownerForPosition(position);
      carrierAtOwnBase = owner == mcguffinObjective_.carrierTeam;
      if (carrierAtOwnBase) {
        installedAtBase = owner;
        installedPosition = inRed
          ? baseCenter(arena_.mcguffin.redBase)
          : baseCenter(arena_.mcguffin.blueBase);
      }

      if (mcguffinCarriedPoints_ < mcguffinConfig_.carryPointLimit) {
        mcguffinCarrySubPoints_ += mcguffinConfig_.carryPointsPerSecond;
        const std::uint32_t subPointsPerPoint =
          static_cast<std::uint32_t>(kFixedTickRate);
        const std::uint32_t points = mcguffinCarrySubPoints_ / subPointsPerPoint;
        mcguffinCarrySubPoints_ %= subPointsPerPoint;
        mcguffinCarriedPoints_ = static_cast<std::uint16_t>(std::min<std::uint32_t>(
          mcguffinConfig_.carryPointLimit,
          static_cast<std::uint32_t>(mcguffinCarriedPoints_) + points
        ));
        if (mcguffinCarriedPoints_ >= mcguffinConfig_.carryPointLimit) {
          mcguffinCarrySubPoints_ = 0;
        }
      }
    }
  }

  const McGuffinState beforeTick = mcguffinObjective_.state;
  std::optional<Team> winner;
  const Team scoringTeam = beforeTick == McGuffinState::InstalledRed
    ? Team::Red : beforeTick == McGuffinState::InstalledBlue ? Team::Blue : Team::None;
  const std::size_t scoringIndex = scoringTeam == Team::Red ? 0U : 1U;
  if (isPlayableTeam(scoringTeam) &&
      snapshot_.mcguffinScores[scoringIndex] + 1U >= mcguffinConfig_.scoreLimit) {
    const ArenaMcGuffinBase& base = scoringTeam == snapshot_.mcguffinRedBaseOwner
      ? arena_.mcguffin.redBase : arena_.mcguffin.blueBase;
    bool contested = false;
    for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
      contested = contested || (isActiveCombatant(index) &&
        snapshot_.teams[index] != scoringTeam &&
        pointInsideMcGuffinBase(snapshot_.players[index].position, base));
    }
    mcguffinFinalHoldTicks_ = contested ? 0U : mcguffinFinalHoldTicks_ + 1U;
    if (mcguffinFinalHoldTicks_ >= mcguffinConfig_.finalHoldTicks) {
      winner = scoringTeam;
    }
  } else {
    winner = tickMcGuffin(
      mcguffinObjective_, mcguffinConfig_, carrierAtOwnBase,
      snapshot_.mcguffinScores
    );
  }

  if (beforeTick == McGuffinState::Dropped &&
      mcguffinObjective_.state == McGuffinState::NeutralSpawn) {
    recordMcGuffinEvent(McGuffinEventType::Return, kDuelPlayerCount);
  }

  if (beforeTick == McGuffinState::Carried &&
      mcguffinObjective_.state != McGuffinState::Carried) {
    if (pendingClaimGroup != ArenaSpawnGroup::None &&
        (mcguffinObjective_.state == McGuffinState::InstalledRed ||
         mcguffinObjective_.state == McGuffinState::InstalledBlue)) {
      const Team claimant = installedAtBase;
      const Team opponent = claimant == Team::Red ? Team::Blue : Team::Red;
      if (pendingClaimGroup == ArenaSpawnGroup::RedBase) {
        snapshot_.mcguffinRedBaseOwner = claimant;
        snapshot_.mcguffinBlueBaseOwner = opponent;
      } else {
        snapshot_.mcguffinBlueBaseOwner = claimant;
        snapshot_.mcguffinRedBaseOwner = opponent;
      }
    }
    mcguffinObjective_.position = installedPosition;
    const std::size_t index = installedAtBase == Team::Red ? 0U : 1U;
    snapshot_.mcguffinScores[index] = static_cast<std::uint16_t>(std::min<unsigned>(
      mcguffinConfig_.scoreLimit > 0 ? mcguffinConfig_.scoreLimit - 1U : 0U,
      static_cast<unsigned>(snapshot_.mcguffinScores[index]) + mcguffinCarriedPoints_
    ));
    mcguffinCarrySubPoints_ = 0;
    mcguffinCarriedPoints_ = 0;
    recordMcGuffinEvent(
      McGuffinEventType::Install,
      snapshot_.mcguffin.eventPlayerIndex
    );
  }

  if (mcguffinObjective_.state == McGuffinState::InstalledRed ||
      mcguffinObjective_.state == McGuffinState::InstalledBlue) {
    const Team owner = mcguffinObjective_.associatedTeam;
    const ArenaMcGuffinBase& base = owner == snapshot_.mcguffinRedBaseOwner
      ? arena_.mcguffin.redBase : arena_.mcguffin.blueBase;
    mcguffinObjective_.position = baseCenter(base);
    for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
      const bool stealing = isActiveCombatant(index) &&
        snapshot_.teams[index] != owner &&
        pointInsideMcGuffinBase(snapshot_.players[index].position, base);
      mcguffinStealTicks_[index] = stealing ? mcguffinStealTicks_[index] + 1U : 0U;
      if (stealing && mcguffinStealTicks_[index] >= mcguffinConfig_.stealTicks &&
          tryStealMcGuffin(
            mcguffinObjective_, mcguffinConfig_, index, snapshot_.teams[index],
            mcguffinObjective_.position
          )) {
        mcguffinStealTicks_ = {};
        mcguffinFinalHoldTicks_ = 0;
        recordMcGuffinEvent(McGuffinEventType::Steal, index);
        break;
      }
    }
  } else {
    mcguffinStealTicks_ = {};
  }

  if (winner.has_value()) beginMcGuffinRoundEnd(*winner);

  snapshot_.mcguffin.state = mcguffinObjective_.state;
  snapshot_.mcguffin.associatedTeam = mcguffinObjective_.associatedTeam;
  snapshot_.mcguffin.carrierTeam = mcguffinObjective_.carrierTeam;
  snapshot_.mcguffin.carrierIndex = mcguffinObjective_.carrierIndex;
  snapshot_.mcguffin.position = mcguffinObjective_.position;
  snapshot_.mcguffin.velocity = mcguffinObjective_.velocity;
  snapshot_.mcguffin.stateTicks = mcguffinObjective_.stateTicks;
  snapshot_.mcguffin.scoreSubPoints = mcguffinObjective_.scoreSubPoints;
  snapshot_.mcguffin.carrySubPoints = mcguffinCarrySubPoints_;
  snapshot_.mcguffin.carriedPoints = mcguffinCarriedPoints_;
  snapshot_.mcguffin.interactionTicks = *std::max_element(
    mcguffinStealTicks_.begin(), mcguffinStealTicks_.end()
  );
  snapshot_.mcguffin.finalHoldTicks = mcguffinFinalHoldTicks_;
}

void ServerGame::restoreTransientCombatEvents() {
  for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
    if (
      recentWeaponFires_[playerIndex].fired &&
      snapshot_.serverTick - recentWeaponFireTicks_[playerIndex] <=
        kTransientCombatEventTicks
    ) {
      snapshot_.weaponFires[playerIndex] = recentWeaponFires_[playerIndex];
    }
    if (
      recentRocketExplosions_[playerIndex].active &&
      snapshot_.serverTick - recentRocketExplosionTicks_[playerIndex] <=
        kTransientCombatEventTicks
    ) {
      snapshot_.rocketExplosions[playerIndex] =
        recentRocketExplosions_[playerIndex];
    }
    if (
      recentFootstepAudioEvents_[playerIndex].active &&
      snapshot_.serverTick - recentFootstepAudioEventTicks_[playerIndex] <=
        kTransientCombatEventTicks
    ) {
      const FootstepAudioEvent& recentEvent =
        recentFootstepAudioEvents_[playerIndex];
      const PlayerState& player = snapshot_.players[playerIndex];
      if (
        (player.crouched || player.sneaking) &&
        !recentEvent.jumping &&
        !recentEvent.landing
      ) {
        continue;
      }
      snapshot_.footstepAudioEvents[playerIndex] =
        recentFootstepAudioEvents_[playerIndex];
    }
    if (
      recentFragEvents_[playerIndex].active &&
      snapshot_.serverTick - recentFragEventTicks_[playerIndex] <=
        kTransientCombatEventTicks
    ) {
      snapshot_.fragEvents[playerIndex] = recentFragEvents_[playerIndex];
    }
    for (std::size_t eventSlot = 0; eventSlot < kLocalHitFeedbackEventWindow; ++eventSlot) {
      if (
        recentLocalHitFeedbackEvents_[playerIndex][eventSlot].active &&
        snapshot_.serverTick -
            recentLocalHitFeedbackEventTicks_[playerIndex][eventSlot] <=
          kLocalHitFeedbackEventRetentionTicks
      ) {
        snapshot_.localHitFeedbackEvents[playerIndex][eventSlot] =
          recentLocalHitFeedbackEvents_[playerIndex][eventSlot];
      }
    }
  }
  for (std::size_t index = 0; index < snapshot_.grenadeBounceAudioEvents.size(); ++index) {
    if (
      recentGrenadeBounceAudioEvents_[index].active &&
      snapshot_.serverTick - recentGrenadeBounceAudioEventTicks_[index] <=
        kTransientCombatEventTicks
    ) {
      snapshot_.grenadeBounceAudioEvents[index] =
        recentGrenadeBounceAudioEvents_[index];
    }
  }
}

void ServerGame::rememberTransientCombatEvents() {
  for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
    if (snapshot_.weaponFires[playerIndex].fired) {
      recentWeaponFires_[playerIndex] = snapshot_.weaponFires[playerIndex];
      recentWeaponFireTicks_[playerIndex] = snapshot_.serverTick;
    }
    if (snapshot_.rocketExplosions[playerIndex].active) {
      recentRocketExplosions_[playerIndex] =
        snapshot_.rocketExplosions[playerIndex];
      recentRocketExplosionTicks_[playerIndex] = snapshot_.serverTick;
    }
    if (snapshot_.footstepAudioEvents[playerIndex].active) {
      recentFootstepAudioEvents_[playerIndex] =
        snapshot_.footstepAudioEvents[playerIndex];
      recentFootstepAudioEventTicks_[playerIndex] = snapshot_.serverTick;
    }
    if (snapshot_.fragEvents[playerIndex].active) {
      recentFragEvents_[playerIndex] = snapshot_.fragEvents[playerIndex];
      recentFragEventTicks_[playerIndex] = snapshot_.serverTick;
    }
    for (std::size_t eventSlot = 0; eventSlot < kLocalHitFeedbackEventWindow; ++eventSlot) {
      if (
        snapshot_.localHitFeedbackEvents[playerIndex][eventSlot].active &&
        snapshot_.localHitFeedbackEvents[playerIndex][eventSlot].sequence !=
          recentLocalHitFeedbackEvents_[playerIndex][eventSlot].sequence
      ) {
        recentLocalHitFeedbackEvents_[playerIndex][eventSlot] =
          snapshot_.localHitFeedbackEvents[playerIndex][eventSlot];
        recentLocalHitFeedbackEventTicks_[playerIndex][eventSlot] =
          snapshot_.serverTick;
      }
    }
  }
  for (std::size_t index = 0; index < snapshot_.grenadeBounceAudioEvents.size(); ++index) {
    if (
      snapshot_.grenadeBounceAudioEvents[index].active &&
      snapshot_.grenadeBounceAudioEvents[index].sequence !=
        recentGrenadeBounceAudioEvents_[index].sequence
    ) {
      recentGrenadeBounceAudioEvents_[index] =
        snapshot_.grenadeBounceAudioEvents[index];
      recentGrenadeBounceAudioEventTicks_[index] = snapshot_.serverTick;
    }
  }
}

void ServerGame::updateParticipatingPlayers() {
  snapshot_.botPlayers = botPlayers_;
  for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
    snapshot_.participatingPlayers[playerIndex] = isOccupiedSlot(playerIndex);
    if (botPlayers_[playerIndex]) {
      snapshot_.readyPlayers[playerIndex] = true;
      snapshot_.playerNames[playerIndex] = "BOT " + std::to_string(playerIndex + 1U);
    } else if (!snapshot_.connectedPlayers[playerIndex]) {
      snapshot_.readyPlayers[playerIndex] = false;
    }
  }
}

void ServerGame::updateBotCommands(float fixedDt) {
  for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
    if (!botPlayers_[playerIndex]) {
      continue;
    }

    UserCommand command;
    command.viewYawRadians = snapshot_.players[playerIndex].viewYawRadians;
    command.viewPitchRadians = snapshot_.players[playerIndex].viewPitchRadians;
    command.planarAim = false;
    command.weapon = Weapon::LightningGun;

    if (botStandstillEnabled_) {
      command.forwardMove = 0.0F;
      command.rightMove = 0.0F;
      command.jump = false;
      botDodgeSwitchSeconds_[playerIndex] = 0.0F;
    } else if (botDodgeEnabled_) {
      botDodgeSwitchSeconds_[playerIndex] -= fixedDt;
      if (botDodgeSwitchSeconds_[playerIndex] <= 0.0F) {
        botDodgeDirections_[playerIndex] =
          (randomU32() & 1U) == 0U ? -1 : 1;
        const int intervalRange =
          botDodgeMaxIntervalMs_ - botDodgeMinIntervalMs_ + 1;
        const int intervalMs =
          botDodgeMinIntervalMs_ +
          static_cast<int>(randomU32() % static_cast<std::uint32_t>(intervalRange));
        botDodgeSwitchSeconds_[playerIndex] =
          static_cast<float>(intervalMs) / 1000.0F;
      }
      command.rightMove =
        botDodgeDirections_[playerIndex] < 0 ? -1.0F : 1.0F;
    } else {
      botDodgeSwitchSeconds_[playerIndex] = 0.0F;
    }

    if (!isActiveCombatant(playerIndex)) {
      command.attack = false;
      commands_[playerIndex] = command;
      hasCommand_[playerIndex] = true;
      continue;
    }

    if (botAttackMode_ != BotAttackMode::Off) {
      BotCombatState& state = botCombatStates_[playerIndex];
      const BotAttackPreset preset = botAttackPreset(botAttackMode_);
      if (!state.initialized) {
        state.initialized = true;
        state.targetPlayerIndex = kDuelPlayerCount;
        state.desiredYawRadians = command.viewYawRadians;
        state.desiredPitchRadians = command.viewPitchRadians;
        state.reactionSecondsRemaining =
          randomFloat(preset.reactionMinSeconds, preset.reactionMaxSeconds);
        state.nextAimErrorRefreshSeconds = 0.0F;
      } else {
        state.reactionSecondsRemaining -= fixedDt;
        state.nextAimErrorRefreshSeconds -= fixedDt;
      }

      if (state.reactionSecondsRemaining <= 0.0F) {
        state.targetPlayerIndex = nearestValidEnemy(playerIndex, true);
        if (state.targetPlayerIndex < kDuelPlayerCount) {
          if (state.nextAimErrorRefreshSeconds <= 0.0F) {
            const auto sampleError = [this, &preset] {
              float unit = randomFloat(-1.0F, 1.0F);
              if (std::fabs(unit) < 0.25F) {
                unit = unit < 0.0F ? -0.25F : 0.25F;
              }
              return unit * preset.aimErrorRadians;
            };
            state.aimErrorYawRadians = sampleError();
            state.aimErrorPitchRadians = sampleError() * 0.75F;
            state.nextAimErrorRefreshSeconds = randomFloat(
              preset.aimErrorRefreshMinSeconds,
              preset.aimErrorRefreshMaxSeconds
            );
          }

          const Vec3 start = weaponMuzzlePosition(
            snapshot_.players[playerIndex],
            lightningGunTuning_.eyeHeight
          );
          const Vec3 target =
            botTargetAimPoint(snapshot_.players[state.targetPlayerIndex]);
          const Vec3 delta = target - start;
          const float horizontalDistance = std::hypot(delta.x, delta.y);
          state.desiredYawRadians =
            wrapRadians(std::atan2(delta.y, delta.x) + state.aimErrorYawRadians);
          state.desiredPitchRadians = std::clamp(
            std::atan2(delta.z, horizontalDistance) + state.aimErrorPitchRadians,
            -kMaxPitchRadians,
            kMaxPitchRadians
          );
        }
        state.reactionSecondsRemaining =
          randomFloat(preset.reactionMinSeconds, preset.reactionMaxSeconds);
      }

      const float maxTurn = preset.turnSpeedRadiansPerSecond * fixedDt;
      command.viewYawRadians = approachAngleRadians(
        command.viewYawRadians,
        state.desiredYawRadians,
        maxTurn
      );
      command.viewPitchRadians = std::clamp(
        approachFloat(command.viewPitchRadians, state.desiredPitchRadians, maxTurn),
        -kMaxPitchRadians,
        kMaxPitchRadians
      );

      const bool validVisibleTarget =
        state.targetPlayerIndex < kDuelPlayerCount &&
        isValidEnemyTarget(playerIndex, state.targetPlayerIndex) &&
        hasLineOfSight(playerIndex, state.targetPlayerIndex);
      const bool combatPhase =
        snapshot_.matchPhase == MatchPhase::Live ||
        snapshot_.matchPhase == MatchPhase::WaitingForPlayers ||
        snapshot_.matchPhase == MatchPhase::WaitingForReady;
      const bool aimClose =
        std::fabs(angleDeltaRadians(command.viewYawRadians, state.desiredYawRadians)) <=
          preset.fireToleranceRadians &&
        std::fabs(command.viewPitchRadians - state.desiredPitchRadians) <=
          preset.fireToleranceRadians;
      command.attack = validVisibleTarget && combatPhase && aimClose;
    } else if (botStareEnabled_) {
      const std::size_t targetIndex = nearestValidEnemy(playerIndex, false);
      if (targetIndex < kDuelPlayerCount) {
        const Vec3 start = weaponMuzzlePosition(
          snapshot_.players[playerIndex],
          lightningGunTuning_.eyeHeight
        );
        const Vec3 target = botTargetAimPoint(snapshot_.players[targetIndex]);
        const Vec3 delta = target - start;
        command.viewYawRadians = wrapRadians(std::atan2(delta.y, delta.x));
        command.viewPitchRadians = std::clamp(
          std::atan2(delta.z, std::hypot(delta.x, delta.y)),
          -kMaxPitchRadians,
          kMaxPitchRadians
        );
      }
      command.attack = false;
    }

    commands_[playerIndex] = command;
    hasCommand_[playerIndex] = true;
  }
}

void ServerGame::handleBotCommandRequest(const CommandPacket& packet) {
  switch (packet.botCommand) {
    case BotCommandType::None:
      break;
    case BotCommandType::Add:
      if (packet.botCommandValue < 0) {
        (void)addBots();
      } else {
        (void)addBots(static_cast<std::size_t>(packet.botCommandValue));
      }
      break;
    case BotCommandType::KickSlot:
      if (packet.botCommandValue >= 1) {
        (void)kickBotAtPlayerIndex(static_cast<std::size_t>(packet.botCommandValue - 1));
      }
      break;
    case BotCommandType::KickAll:
      (void)kickAllBots();
      break;
    case BotCommandType::AttackMode:
      setBotAttackMode(static_cast<BotAttackMode>(packet.botCommandValue));
      break;
    case BotCommandType::Stare:
      botStareEnabled_ = packet.botCommandValue != 0;
      snapshot_.botStareEnabled = botStareEnabled_;
      break;
    case BotCommandType::Standstill:
      botStandstillEnabled_ = packet.botCommandValue != 0;
      snapshot_.botStandstillEnabled = botStandstillEnabled_;
      break;
    case BotCommandType::Dodge:
      setBotDodge(
        packet.botCommandValue != 0,
        packet.botCommandMinIntervalMs,
        packet.botCommandMaxIntervalMs
      );
      break;
  }
}

void ServerGame::updateClanArenaBotTeams() {
  if (snapshot_.gameMode == GameMode::Duel) {
    for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
      if (botPlayers_[index]) {
        snapshot_.teams[index] = Team::None;
      }
    }
    return;
  }

  for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
    if (botPlayers_[index]) {
      snapshot_.teams[index] = Team::None;
    }
  }
  for (std::size_t botIndex = 0; botIndex < kDuelPlayerCount; ++botIndex) {
    if (!botPlayers_[botIndex]) {
      continue;
    }
    std::size_t redCount = 0;
    std::size_t blueCount = 0;
    for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
      if (!isOccupiedSlot(index)) {
        continue;
      }
      if (snapshot_.teams[index] == Team::Red) {
        ++redCount;
      } else if (snapshot_.teams[index] == Team::Blue) {
        ++blueCount;
      }
    }
    snapshot_.teams[botIndex] = redCount <= blueCount ? Team::Red : Team::Blue;
  }
}

void ServerGame::refreshWarmupRosterState() {
  updateClanArenaBotTeams();
  updateParticipatingPlayers();
  if (!warmupPhase()) {
    return;
  }
  if (!enoughPlayersConnected()) {
    snapshot_.matchPhase = MatchPhase::WaitingForPlayers;
    snapshot_.phaseTicksRemaining = 0;
    snapshot_.scores = {};
    snapshot_.teamScores = {};
    snapshot_.matchCombatStats = {};
    snapshot_.liveTicksElapsed = 0;
    snapshot_.roundWinner = 255;
    snapshot_.matchWinner = 255;
    snapshot_.roundWinningTeam = Team::None;
    snapshot_.matchWinningTeam = Team::None;
    respawnRound();
  } else if (snapshot_.matchPhase == MatchPhase::WaitingForPlayers) {
    snapshot_.matchPhase = MatchPhase::WaitingForReady;
    snapshot_.phaseTicksRemaining = 0;
  }
  updateParticipatingPlayers();
}

void ServerGame::addBotAtPlayerIndex(std::size_t playerIndex) {
  if (playerIndex >= kDuelPlayerCount || snapshot_.connectedPlayers[playerIndex]) {
    return;
  }
  botPlayers_[playerIndex] = true;
  snapshot_.botPlayers[playerIndex] = true;
  snapshot_.readyPlayers[playerIndex] = true;
  snapshot_.playerNames[playerIndex] = "BOT " + std::to_string(playerIndex + 1U);
  resetPlayerInputState(playerIndex);
  botDodgeSwitchSeconds_[playerIndex] = 0.0F;
  botCombatStates_[playerIndex] = {};
  respawnPlayer(playerIndex);
  updateClanArenaBotTeams();
  updateParticipatingPlayers();
}

void ServerGame::removeBotAtPlayerIndex(std::size_t playerIndex) {
  if (playerIndex >= kDuelPlayerCount || !botPlayers_[playerIndex]) {
    return;
  }
  botPlayers_[playerIndex] = false;
  snapshot_.botPlayers[playerIndex] = false;
  snapshot_.readyPlayers[playerIndex] = false;
  snapshot_.teams[playerIndex] = Team::None;
  snapshot_.playerNames[playerIndex] = "PLAYER " + std::to_string(playerIndex + 1U);
  resetPlayerInputState(playerIndex);
  botDodgeSwitchSeconds_[playerIndex] = 0.0F;
  botCombatStates_[playerIndex] = {};
  updateParticipatingPlayers();
}

std::uint32_t ServerGame::randomU32() {
  std::uint32_t value = botRandomState_;
  value ^= value << 13U;
  value ^= value >> 17U;
  value ^= value << 5U;
  botRandomState_ = value == 0U ? 0xB07D0D6EU : value;
  return botRandomState_;
}

float ServerGame::randomFloat(float minValue, float maxValue) {
  if (minValue > maxValue) {
    std::swap(minValue, maxValue);
  }
  const float unit =
    static_cast<float>(randomU32() & 0x00ffffffU) /
    static_cast<float>(0x00ffffffU);
  return minValue + ((maxValue - minValue) * unit);
}

const ServerSnapshot& ServerGame::snapshot() const {
  return snapshot_;
}

const Arena& ServerGame::arena() const {
  return arena_;
}

const std::string& ServerGame::mapDirectory() const {
  return mapDirectory_;
}

const std::string& ServerGame::spawnDebugString() const {
  return spawnDebugString_;
}

bool ServerGame::loadRequestedMap(const std::string& mapName) {
  if (!isValidMapName(mapName)) {
    return false;
  }

  const LocalMapLoadResult result = loadLocalMap(mapName, mapDirectory_);
  if (result.ok) {
    if (snapshot_.gameMode == GameMode::McGuffin &&
        !hasValidMcGuffinLayout(result.arena)) {
      std::cerr << "map load rejected for '" << mapName
                << "': active McGuffin mode requires one neutral spawn, two bases, and team spawns\n";
      return false;
    }
    setArena(result.arena, result.descriptor);
    return true;
  }
  std::cerr << "map load failed for '" << mapName << "': " << result.error << '\n';
  return false;
}

void ServerGame::receiveCommands() {
  CommandPacket packet;
  while (transport_.receiveCommand(packet)) {
    if (packet.playerIndex >= kDuelPlayerCount) {
      continue;
    }

    const std::size_t playerIndex = static_cast<std::size_t>(packet.playerIndex);
    const bool isNewCommand =
      !snapshot_.hasAcknowledgedCommand[playerIndex] ||
      isSequenceNewer(packet.command.sequence, snapshot_.acknowledgedCommand[playerIndex]);
    if (!isNewCommand) {
      continue;
    }

    ActionEdgeState& consumedEdges = lastActionEdges_[playerIndex];
    jumpEdgeThisTick_[playerIndex] = jumpEdgeThisTick_[playerIndex] || consumeActionEdge(
      packet.actionEdges.jump,
      consumedEdges.jump
    );
    dashEdgeThisTick_[playerIndex] = dashEdgeThisTick_[playerIndex] || consumeActionEdge(
      packet.actionEdges.dash,
      consumedEdges.dash
    );
    const bool resetEdge = consumeActionEdge(
      packet.actionEdges.reset,
      consumedEdges.reset
    );
    const bool readyEdge = consumeActionEdge(
      packet.actionEdges.ready,
      consumedEdges.ready
    );
    const bool mcguffinThrowEdge = consumeActionEdge(
      packet.actionEdges.mcguffinThrow,
      consumedEdges.mcguffinThrow
    );
    const bool attackEdge = consumeActionEdge(
      packet.actionEdges.attack,
      consumedEdges.attack
    );
    if (attackEdge) {
      // Apply the original click's aim and rewind tick when a later redundant
      // command is the first datagram that delivers this attack edge.
      packet.command.attack = true;
      packet.command.viewYawRadians = packet.actionEdges.attackYawRadians;
      packet.command.viewPitchRadians = packet.actionEdges.attackPitchRadians;
      packet.command.weapon = packet.actionEdges.attackWeapon;
      packet.viewedServerTick = packet.actionEdges.attackViewedServerTick;
      attackEdgeThisTick_[playerIndex] = true;
      attackEdgeCommands_[playerIndex] = packet.command;
      attackEdgeViewedServerTicks_[playerIndex] = packet.viewedServerTick;
    }

    if (packet.requestMovementTuning) {
      const std::string playerName = packet.playerName.empty()
        ? snapshot_.playerNames[playerIndex]
        : packet.playerName;
      const auto logBool = [&](const char* command, bool current, bool next) {
        if (current != next) {
          logClientGameplayCommand(playerName, command, next ? "1" : "0");
        }
      };
      const auto logFloat = [&](const char* command, float current, float next) {
        if (!nearlyEqualGameplayFloat(current, next)) {
          logClientGameplayCommand(playerName, command, std::to_string(next));
        }
      };
      const auto logInt = [&](const char* command, int current, int next) {
        if (current != next) {
          logClientGameplayCommand(playerName, command, std::to_string(next));
        }
      };

      logBool(
        "g_flight",
        movementTuning_.flightEnabled,
        packet.movementTuning.flightEnabled
      );
      logFloat(
        "g_accel",
        movementTuning_.groundAcceleration,
        packet.movementTuning.groundAcceleration
      );
      logFloat(
        "g_airaccel",
        movementTuning_.airAcceleration,
        packet.movementTuning.airAcceleration
      );
      logBool(
        "g_aircontrol",
        movementTuning_.airControlEnabled,
        packet.movementTuning.airControlEnabled
      );
      logFloat(
        "g_friction",
        movementTuning_.groundFriction,
        packet.movementTuning.groundFriction
      );
      logFloat(
        "g_stopspeed",
        movementTuning_.stopSpeed,
        packet.movementTuning.stopSpeed
      );
      logFloat(
        "g_maxspeed",
        movementTuning_.maxGroundSpeed,
        packet.movementTuning.maxGroundSpeed
      );
      logFloat(
        "g_dash_targetspeed",
        movementTuning_.dashTargetSpeed,
        packet.movementTuning.dashTargetSpeed
      );
      logFloat(
        "g_dash_maxspeed",
        movementTuning_.dashMaxSpeed,
        packet.movementTuning.dashMaxSpeed
      );
      logFloat(
        "g_dash_accel",
        movementTuning_.dashAcceleration,
        packet.movementTuning.dashAcceleration
      );
      logFloat(
        "g_dash_duration",
        movementTuning_.dashDuration,
        packet.movementTuning.dashDuration
      );
      logFloat(
        "g_dash_cooldown",
        movementTuning_.dashCooldown,
        packet.movementTuning.dashCooldown
      );
      logFloat(
        "g_dash_groundhop",
        movementTuning_.dashGroundHopVelocity,
        packet.movementTuning.dashGroundHopVelocity
      );
      logFloat(
        "g_dash_airhop",
        movementTuning_.dashAirHopVelocity,
        packet.movementTuning.dashAirHopVelocity
      );
      logFloat(
        "g_flightaccel",
        movementTuning_.flightAcceleration,
        packet.movementTuning.flightAcceleration
      );
      logFloat(
        "g_flightmaxspeed",
        movementTuning_.maxFlightSpeed,
        packet.movementTuning.maxFlightSpeed
      );
      logFloat(
        "g_flightdamping",
        movementTuning_.flightDamping,
        packet.movementTuning.flightDamping
      );
      logFloat("g_playersize_xy", playerSizeScaleXY_, packet.playerSizeScaleXY);
      logFloat("g_playersize_z", playerSizeScaleZ_, packet.playerSizeScaleZ);
      logInt(
        "g_healthamount",
        healthAmount_,
        packet.healthAmount
      );
      logInt(
        "g_sg_damage",
        weaponDamage_.shotgunDamagePerPellet,
        packet.weaponDamage.shotgunDamagePerPellet
      );
      logInt(
        "g_mg_damage",
        weaponDamage_.machineGunDamage,
        packet.weaponDamage.machineGunDamage
      );
      logInt(
        "g_lg_damage",
        weaponDamage_.lightningGunDamage,
        packet.weaponDamage.lightningGunDamage
      );
      logInt(
        "g_fg_damage",
        weaponDamage_.freezeGunDamage,
        packet.weaponDamage.freezeGunDamage
      );
      logFloat("g_lg_fire_hz", lightningFireHz_, packet.lightningFireHz);
      logFloat("g_lg_knockback", lightningKnockback_, packet.lightningKnockback);
      logInt("g_knockback_time_ms", knockbackTimeMs_, packet.knockbackTimeMs);
      logInt(
        "g_rg_damage",
        weaponDamage_.railgunDamage,
        packet.weaponDamage.railgunDamage
      );
      logInt(
        "g_rl_damage",
        weaponDamage_.rocketLauncherDamage,
        packet.weaponDamage.rocketLauncherDamage
      );
      logFloat("g_rl_knockback", rocketKnockback_, packet.rocketKnockback);
      logInt(
        "g_pg_damage",
        weaponDamage_.plasmaGunDamage,
        packet.weaponDamage.plasmaGunDamage
      );
      logInt(
        "g_selfdamage",
        selfDamagePercent_,
        packet.selfDamagePercent
      );
      logFloat("g_vampirism", vampirism_, packet.vampirism);
      logBool(
        "g_infiniteammo",
        weaponAmmoConfig_.infiniteAmmo,
        packet.weaponAmmo.infiniteAmmo
      );
      if (weaponSwitchingMode_ != packet.weaponSwitchingMode) {
        logClientGameplayCommand(
          playerName,
          "g_weaponswitching",
          weaponSwitchingModeCommandValue(packet.weaponSwitchingMode)
        );
      }
      setRuntimeGameplayTuning(
        packet.movementTuning,
        packet.playerSizeScaleXY,
        packet.playerSizeScaleZ,
        packet.lightningKnockback,
        packet.lightningFireHz,
        packet.rocketKnockback,
        packet.knockbackTimeMs,
        packet.weaponDamage,
        packet.vampirism,
        packet.selfDamagePercent,
        packet.healthAmount,
        packet.weaponAmmo.infiniteAmmo,
        botDodgeEnabled_,
        botDodgeMinIntervalMs_,
        botDodgeMaxIntervalMs_,
        packet.weaponSwitchingMode
      );
    }

    if (
      packet.requestGameMode &&
      snapshot_.connectedPlayers[playerIndex] &&
      warmupPhase() &&
      packet.requestedGameMode != snapshot_.gameMode
    ) {
      if (
        packet.requestedGameMode == GameMode::McGuffin &&
        !hasValidMcGuffinLayout(arena_)
      ) {
        std::cerr << "Cannot start McGuffin: map requires exactly one neutral "
          "spawn, one Red base, one Blue base, and team spawns.\n";
        continue;
      }
      snapshot_.gameMode = packet.requestedGameMode;
      snapshot_.teams = {};
      resetMatch();
      updateClanArenaBotTeams();
      refreshWarmupRosterState();
    }
    if (
      packet.requestTeam &&
      snapshot_.connectedPlayers[playerIndex] &&
      snapshot_.gameMode != GameMode::Duel &&
      warmupPhase() &&
      packet.requestedTeam != snapshot_.teams[playerIndex]
    ) {
      snapshot_.teams[playerIndex] = packet.requestedTeam;
      resetMatch();
      updateClanArenaBotTeams();
      refreshWarmupRosterState();
    }

    if (packet.requestReset || resetEdge) {
      resetMatch();
      snapshot_.hasAcknowledgedCommand[playerIndex] = true;
      snapshot_.acknowledgedCommand[playerIndex] = packet.command.sequence;
      continue;
    }
    if (packet.toggleReady || readyEdge) {
      if (
        snapshot_.connectedPlayers[playerIndex] &&
        warmupPhase() &&
        (
          snapshot_.gameMode == GameMode::Duel ||
          isPlayableTeam(snapshot_.teams[playerIndex])
        )
      ) {
        snapshot_.readyPlayers[playerIndex] = !snapshot_.readyPlayers[playerIndex];
      }
    }
    if (!packet.chatMessage.empty()) {
      const std::uint32_t previousSequence = chatHistory_.messageCount == 0U
        ? 0U
        : chatHistory_.messages[chatHistory_.messageCount - 1U].sequence;
      if (chatHistory_.messageCount == kChatHistoryCapacity) {
        std::move(
          chatHistory_.messages.begin() + 1,
          chatHistory_.messages.end(),
          chatHistory_.messages.begin()
        );
        --chatHistory_.messageCount;
      }
      // Preserve the name used when the message was sent; a later occupant of
      // the same player slot must not appear to have authored old chat.
      std::uint32_t nextSequence = previousSequence + 1U;
      if (nextSequence == 0U) {
        nextSequence = 1U;
      }
      chatHistory_.messages[chatHistory_.messageCount++] = ChatMessage{
        nextSequence,
        packet.playerIndex,
        snapshot_.playerNames[packet.playerIndex],
        packet.chatMessage,
      };
    }
    if (!packet.playerName.empty()) {
      snapshot_.playerNames[playerIndex] = packet.playerName;
    }
    if (!packet.mapName.empty()) {
      (void)loadRequestedMap(packet.mapName);
    }
    if (packet.botCommand != BotCommandType::None) {
      handleBotCommandRequest(packet);
    }
    if (packet.requestMcGuffinThrow || mcguffinThrowEdge) {
      mcguffinThrowRequestedThisTick_[playerIndex] = true;
      mcguffinThrowCommands_[playerIndex] = packet.command;
      if (mcguffinThrowEdge) {
        // The edge retains the aim from the original click even when a later
        // redundant command is the first datagram that reaches the server.
        mcguffinThrowCommands_[playerIndex].viewYawRadians =
          packet.actionEdges.mcguffinThrowYawRadians;
        mcguffinThrowCommands_[playerIndex].viewPitchRadians =
          packet.actionEdges.mcguffinThrowPitchRadians;
      }
    }

    commands_[playerIndex] = packet.command;
    commands_[playerIndex].jump =
      commands_[playerIndex].jump || jumpEdgeThisTick_[playerIndex];
    commands_[playerIndex].dash =
      commands_[playerIndex].dash || dashEdgeThisTick_[playerIndex];
    viewedServerTicks_[playerIndex] = packet.viewedServerTick;
    hasCommand_[playerIndex] = true;
    receivedCommandThisTick_[playerIndex] = true;
    snapshot_.hasAcknowledgedCommand[playerIndex] = true;
    snapshot_.acknowledgedCommand[playerIndex] = packet.command.sequence;
  }

  for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
    if (!attackEdgeThisTick_[playerIndex] || !hasCommand_[playerIndex]) {
      continue;
    }
    commands_[playerIndex].attack = true;
    commands_[playerIndex].viewYawRadians =
      attackEdgeCommands_[playerIndex].viewYawRadians;
    commands_[playerIndex].viewPitchRadians =
      attackEdgeCommands_[playerIndex].viewPitchRadians;
    commands_[playerIndex].weapon = attackEdgeCommands_[playerIndex].weapon;
    viewedServerTicks_[playerIndex] = attackEdgeViewedServerTicks_[playerIndex];
  }

}

void ServerGame::publishSnapshot() {
  updateParticipatingPlayers();
  snapshot_.botStareEnabled = botStareEnabled_;
  snapshot_.botStandstillEnabled = botStandstillEnabled_;
  snapshot_.botDodgeEnabled = botDodgeEnabled_;
  snapshot_.botDodgeMinIntervalMs = botDodgeMinIntervalMs_;
  snapshot_.botDodgeMaxIntervalMs = botDodgeMaxIntervalMs_;
  snapshot_.botAttackMode = botAttackMode_;
  transport_.publishChatHistory(chatHistory_);
  transport_.sendSnapshot(snapshot_);
}

} // namespace lg
