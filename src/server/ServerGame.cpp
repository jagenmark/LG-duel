#include "server/ServerGame.hpp"

#include "net/NetCodec.hpp"
#include "replay/ReplayCodec.hpp"
#include "shared/Sequence.hpp"
#include "sim/BalanceConfig.hpp"
#include "sim/ClanArenaRules.hpp"
#include "sim/Collision.hpp"
#include "sim/DuelRules.hpp"
#include "sim/GameplayCvars.hpp"
#include "sim/McGuffinRules.hpp"
#include "sim/WeaponCatalog.hpp"

#include <algorithm>
#include <bit>
#include <cctype>
#include <chrono>
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
constexpr std::uint32_t kDamageTakenEventRetentionTicks = 32;
constexpr CollisionBounds kDefaultPlayerBounds = {};
constexpr float kQ3KnockbackToInternalScale = 22.0F / 1000.0F;
constexpr float kProjectileCollisionEpsilon = 0.0001F;
constexpr float kPi = 3.14159265359F;
constexpr float kHalfPi = kPi * 0.5F;
constexpr float kTwoPi = kPi * 2.0F;
constexpr float kMaxPitchRadians = kHalfPi - 0.01F;

[[nodiscard]] bool sameBotNavigationTuning(
  const MovementTuning& left,
  const MovementTuning& right
) {
  return left.flightEnabled == right.flightEnabled &&
    left.groundAcceleration == right.groundAcceleration &&
    left.airAcceleration == right.airAcceleration &&
    left.groundFriction == right.groundFriction &&
    left.stopSpeed == right.stopSpeed &&
    left.gravity == right.gravity &&
    left.maxGroundSpeed == right.maxGroundSpeed &&
    left.maxAirSpeed == right.maxAirSpeed &&
    left.jumpImpulse == right.jumpImpulse &&
    left.airControlEnabled == right.airControlEnabled &&
    left.dashTargetSpeed == right.dashTargetSpeed &&
    left.dashMaxSpeed == right.dashMaxSpeed &&
    left.dashAcceleration == right.dashAcceleration &&
    left.dashDuration == right.dashDuration &&
    left.dashCooldown == right.dashCooldown &&
    left.dashGroundHopVelocity == right.dashGroundHopVelocity &&
    left.dashAirHopVelocity == right.dashAirHopVelocity &&
    left.flightAcceleration == right.flightAcceleration &&
    left.maxFlightSpeed == right.maxFlightSpeed &&
    left.flightDamping == right.flightDamping &&
    left.flightGravityCancel == right.flightGravityCancel;
}

[[nodiscard]] CollisionBounds botNavigationBounds(float scaleXY, float scaleZ) {
  CollisionBounds bounds = kDefaultPlayerBounds;
  bounds.radius *= scaleXY;
  bounds.halfHeight *= scaleZ;
  return bounds;
}

[[nodiscard]] std::uint8_t quantizeDamageBearing(Vec3 victim, Vec3 source) {
  const float x = source.x - victim.x;
  const float y = source.y - victim.y;
  const float horizontalLengthSquared = (x * x) + (y * y);
  if (!std::isfinite(x) || !std::isfinite(y) ||
      horizontalLengthSquared <= 0.00000001F) {
    return 0U;
  }
  const float bearing = std::atan2(y, x);
  const float wrapped = bearing < 0.0F ? bearing + kTwoPi : bearing;
  const long rounded = std::lround(wrapped * (256.0F / kTwoPi));
  return static_cast<std::uint8_t>(rounded & 0xFFL);
}

[[nodiscard]] std::uint32_t scenarioRandomState(
  std::uint64_t seed,
  std::uint64_t stream
) {
  std::uint64_t value = seed + (0x9e3779b97f4a7c15ULL * stream);
  value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
  value ^= value >> 31U;
  const std::uint32_t folded =
    static_cast<std::uint32_t>(value) ^
    static_cast<std::uint32_t>(value >> 32U);
  return folded == 0U ? static_cast<std::uint32_t>(stream) : folded;
}

[[nodiscard]] bool validScenarioMatchPhase(MatchPhase phase) {
  return phase >= MatchPhase::WaitingForPlayers &&
    phase <= MatchPhase::MatchEnd;
}

[[nodiscard]] bool validScenarioWeapon(Weapon weapon) {
  return weaponIndex(weapon) < kWeaponCount;
}

[[nodiscard]] bool finiteScenarioVector(Vec3 value) {
  return std::isfinite(value.x) &&
    std::isfinite(value.y) &&
    std::isfinite(value.z);
}

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
  // Player slots and authored spawn capacity evolve independently. Stable
  // modulo selection preserves legacy placement while preventing slot OOB.
  const std::size_t spawnIndex = arena.spawnCount == 0
    ? 0U
    : playerIndex % arena.spawnCount;
  player.position = arena.spawnPositions[spawnIndex];
  player.position.z += player.bounds.halfHeight;
  player.viewYawRadians = std::atan2(-player.position.y, -player.position.x);
  player.onGround = true;
  player.movementMode = MovementMode::Grounded;
  return player;
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

[[nodiscard]] bool isTraceableCombatant(
  const ServerSnapshot& snapshot,
  std::size_t attackerIndex,
  std::size_t targetIndex
) {
  return attackerIndex < kDuelPlayerCount &&
    targetIndex < kDuelPlayerCount &&
    attackerIndex != targetIndex &&
    isCombatant(snapshot, targetIndex);
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

[[nodiscard]] Vec3 botTargetAimPoint(const PlayerState& target) {
  return target.position + Vec3{0.0F, 0.0F, target.bounds.halfHeight * 0.45F};
}

[[nodiscard]] bool splashCanReachPlayer(
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
    if (distance <= kProjectileCollisionEpsilon) {
      return true;
    }
    const WorldTrace trace =
      traceWorld(arena, targetPoint, segment / distance, distance);
    // Trace from the body to the blast. An impact brush may sit at the far
    // endpoint, but any nearer hit means solid world lies between them.
    if (trace.distance >= distance - kProjectileCollisionEpsilon) {
      return true;
    }
  }
  return false;
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
  arena_ = makeDefaultServerArena();
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
  rebuildBotNavigation();
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
  lightningGunTuning_.headshotMultiplier = config.lightningGun.headshotMultiplier;
  freezeGunTuning_.range = config.freezeGun.range;
  freezeGunTuning_.eyeHeight = config.freezeGun.eyeHeight;
  freezeGunTuning_.freezePerSecond = config.freezeGun.freezePerSecond;
  freezeGunTuning_.decayPerSecond = config.freezeGun.decayPerSecond;
  freezeGunTuning_.maxSlowFraction = config.freezeGun.maxSlowFraction;
  freezeGunTuning_.headshotMultiplier = config.freezeGun.headshotMultiplier;
  icePoolTuning_ = config.icePool;
  snapshot_.icePoolTuning = icePoolTuning_;
  railgunTuning_.range = config.railgun.range;
  railgunTuning_.eyeHeight = config.railgun.eyeHeight;
  railgunTuning_.knockback = config.railgun.knockback;
  railgunTuning_.headshotMultiplier = config.railgun.headshotMultiplier;
  sniperChargeSeconds_ = config.sniperChargeSeconds;
  sniperMaxDamageMultiplier_ = config.sniperMaxDamageMultiplier;
  railgunCooldownDurationTicks_ = config.railgunCooldownTicks;
  revolverTuning_ = config.revolver;
  revolverCooldownDurationTicks_ = config.revolverCooldownTicks;
  machineGunTuning_.range = config.machineGun.range;
  machineGunTuning_.eyeHeight = config.machineGun.eyeHeight;
  machineGunTuning_.knockback = config.machineGun.knockback;
  machineGunTuning_.spreadRadians = config.machineGun.spreadRadians;
  machineGunTuning_.headshotMultiplier = config.machineGun.headshotMultiplier;
  machineGunCooldownDurationTicks_ = config.machineGunCooldownTicks;
  shotgunTuning_.range = config.shotgun.range;
  shotgunTuning_.pelletCount = config.shotgun.pelletCount;
  shotgunTuning_.spreadRadians = config.shotgun.spreadRadians;
  shotgunTuning_.eyeHeight = config.shotgun.eyeHeight;
  shotgunTuning_.knockback = config.shotgun.knockback;
  shotgunTuning_.headshotMultiplier = config.shotgun.headshotMultiplier;
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
  weaponAmmoConfig_.infiniteAmmo = config.weaponAmmo.infiniteAmmo;
  snapshot_.weaponAmmo.infiniteAmmo = weaponAmmoConfig_.infiniteAmmo;
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
  rebuildBotNavigation();
}

void ServerGame::tick(float fixedDt) {
  // Tick order is authoritative: accept input and phase changes first, simulate
  // movement, resolve combat, retain events, then publish the completed state.
  if (replayPlayback_ && !pendingReplayInput_.has_value()) {
    // Playback advances only when its runner has supplied the resolved frame
    // for this server tick. This prevents accidental live or bot input leaks.
    return;
  }
  receivedCommandThisTick_.fill(false);
  mcguffinThrowRequestedThisTick_.fill(false);
  jumpEdgeThisTick_.fill(false);
  dashEdgeThisTick_.fill(false);
  attackEdgeThisTick_.fill(false);
  spawnedProjectileCount_ = 0;
  if (!replayPlayback_) {
    receiveCommands();
  }
  updateMatchState();
  const bool tickStartedLive = snapshot_.matchPhase == MatchPhase::Live;
  if (replayPlayback_) {
    applyReplayInput(*pendingReplayInput_);
    pendingReplayInput_.reset();
  } else {
    updateBotCommands(fixedDt);
    const bool recording = replayRecorder_ != nullptr && replayRecorder_->active();
    const bool rolling = rollingReplay_ != nullptr && rollingReplay_->active();
    if (recording || rolling) {
      const replay::ReplayTickInput resolvedInput = captureResolvedReplayInput();
      ++replayCheckpointCaptureStats_.resolvedInputCaptures;
      if (recording) {
        (void)replayRecorder_->recordResolvedInput(resolvedInput);
      }
      if (rolling) {
        rollingReplay_->recordResolvedInput(resolvedInput);
      }
    }
  }
  snapshot_.weaponFires = {};
  snapshot_.rocketExplosions = {};
  snapshot_.footstepAudioEvents = {};
  snapshot_.grenadeBounceAudioEvents = {};
  snapshot_.fragEvents = {};
  snapshot_.localHitFeedbackEvents = {};
  snapshot_.damageTakenEvents = {};
  // Event fields describe occurrences, not durable state. They are rebuilt for
  // this tick and restored near publication only for packet-loss tolerance.
  for (std::uint32_t& cooldown : railgunCooldownTicks_) {
    if (cooldown > 0) {
      --cooldown;
    }
  }
  for (std::uint32_t& cooldown : revolverCooldownTicks_) {
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

    PlayerCollisionProxySet collisionProxies;
    for (std::size_t otherIndex = 0; otherIndex < kDuelPlayerCount; ++otherIndex) {
      if (
        otherIndex == playerIndex ||
        !isPlayerCollisionEligible(
          snapshot_.connectedPlayers[otherIndex],
          snapshot_.botPlayers[otherIndex],
          snapshot_.participatingPlayers[otherIndex],
          snapshot_.players[otherIndex]
        )
      ) {
        continue;
      }
      PlayerCollisionProxy& proxy =
        collisionProxies.proxies[collisionProxies.count++];
      proxy.playerIndex = static_cast<std::uint8_t>(otherIndex);
      proxy.position = snapshot_.players[otherIndex].position;
      proxy.bounds = snapshot_.players[otherIndex].bounds;
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
      ),
      collisionProxies.span(),
      static_cast<std::uint8_t>(playerIndex)
    );
  }

  // Swept movement handles ordinary body blocking. This bounded symmetric
  // fallback is only an invariant repair for trapped spawn/teleport layouts.
  snapshot_.playersColliding = false;
  bool repairLimitReached = false;
  for (std::size_t pass = 0; pass < kDuelPlayerCount; ++pass) {
    bool repaired = false;
    for (std::size_t firstIndex = 0; firstIndex < kDuelPlayerCount; ++firstIndex) {
      if (!isPlayerCollisionEligible(
            snapshot_.connectedPlayers[firstIndex], snapshot_.botPlayers[firstIndex],
            snapshot_.participatingPlayers[firstIndex], snapshot_.players[firstIndex])) {
        continue;
      }
      for (std::size_t secondIndex = firstIndex + 1U;
           secondIndex < kDuelPlayerCount; ++secondIndex) {
        if (!isPlayerCollisionEligible(
              snapshot_.connectedPlayers[secondIndex], snapshot_.botPlayers[secondIndex],
              snapshot_.participatingPlayers[secondIndex], snapshot_.players[secondIndex])) {
          continue;
        }
        const bool pairRepaired = resolvePlayerCollision(
          arena_, snapshot_.players[firstIndex], snapshot_.players[secondIndex]
        );
        repaired = pairRepaired || repaired;
        emergencyPlayerCollisionRepairCount_ += pairRepaired ? 1U : 0U;
      }
    }
    snapshot_.playersColliding = snapshot_.playersColliding || repaired;
    if (!repaired) {
      break;
    }
    for (std::size_t repairedIndex = 0;
         repairedIndex < kDuelPlayerCount;
         ++repairedIndex) {
      if (!isPlayerCollisionEligible(
            snapshot_.connectedPlayers[repairedIndex],
            snapshot_.botPlayers[repairedIndex],
            snapshot_.participatingPlayers[repairedIndex],
            snapshot_.players[repairedIndex])) {
        // Collision repair must not wake or reposition dead/spectator bodies.
        continue;
      }
      PlayerState& repairedPlayer = snapshot_.players[repairedIndex];
      const CollisionResult collision = resolvePlayerArenaCollision(
        arena_, repairedPlayer, repairedPlayer.position, repairedPlayer.velocity
      );
      repairedPlayer.position = collision.position;
      repairedPlayer.velocity = collision.velocity;
    }
    repairLimitReached = pass + 1U == kDuelPlayerCount;
  }
  if (repairLimitReached) {
    ++unresolvedPlayerCollisionInvariantCount_;
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

  // Freeze post-movement poses for all aim traces so attacker iteration order
  // cannot move a target or otherwise change another player's shot result.
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
    const bool sniperCanCharge =
      command.weapon == Weapon::Railgun &&
      command.zoomed &&
      isCombatant(snapshot_, attackerIndex) &&
      combatPlayers[attackerIndex].health > 0 &&
      (snapshot_.matchPhase == MatchPhase::Live || warmupCombat);
    if (sniperCanCharge) {
      sniperAdsFractions_[attackerIndex] = std::min(
        1.0F,
        sniperAdsFractions_[attackerIndex] + fixedDt / kSniperAdsSeconds
      );
      // Charge belongs to the server so delayed or forged client HUD state can
      // never raise shot damage. Leaving ADS or this weapon clears it at once.
      if (sniperAdsFractions_[attackerIndex] >= 1.0F) {
        sniperChargeFractions_[attackerIndex] = std::min(
          1.0F,
          sniperChargeFractions_[attackerIndex] +
            fixedDt / std::max(0.05F, sniperChargeSeconds_)
        );
      }
    } else {
      sniperAdsFractions_[attackerIndex] = 0.0F;
      sniperChargeFractions_[attackerIndex] = 0.0F;
    }
    snapshot_.sniperChargePercent[attackerIndex] =
      static_cast<std::uint8_t>(std::lround(
        sniperChargeFractions_[attackerIndex] * 100.0F
      ));
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
        : command.weapon == Weapon::Revolver
          ? revolverTuning_.eyeHeight
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
      : command.weapon == Weapon::Revolver
        ? revolverTuning_.range
      : railgunTuning_.range;
    const WorldTrace worldTrace =
      traceWorld(arena_, attackStart, attackDirection, attackRange);
    std::size_t targetIndex = kDuelPlayerCount;
    float bestHitDistance = worldTrace.distance;
    for (std::size_t candidateIndex = 0; candidateIndex < kDuelPlayerCount; ++candidateIndex) {
      if (
        !isTraceableCombatant(snapshot_, attackerIndex, candidateIndex) ||
        combatPlayers[candidateIndex].health <= 0
      ) {
        continue;
      }
      // Trace every other live body. The damage policy below decides whether
      // warmup damage or team damage applies, while friendly knockback remains.
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
      command.weapon == Weapon::Railgun &&
      command.attack &&
      railgunCooldownTicks_[attackerIndex] == 0
    ) {
      HitscanTuning shotTuning = railgunTuning_;
      const float damageScale = 1.0F +
        (sniperMaxDamageMultiplier_ - 1.0F) *
        sniperChargeFractions_[attackerIndex];
      shotTuning.damage = std::max(
        1,
        static_cast<int>(std::lround(
          static_cast<float>(railgunTuning_.damage) * damageScale
        ))
      );
      if (targetIndex < kDuelPlayerCount) {
        weaponTargets[attackerIndex] = targetIndex;
        snapshot_.weaponFires[attackerIndex] = simulateRailgun(
          combatPlayers[attackerIndex],
          target,
          command,
          arena_,
          shotTuning
        );
      } else {
        WeaponFireResult& fire = snapshot_.weaponFires[attackerIndex];
        fire.weapon = Weapon::Railgun;
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
      (void)consumeAmmo(attackerIndex, Weapon::Railgun);
      // A shot spends the whole charge whether it hits or misses.
      sniperChargeFractions_[attackerIndex] = 0.0F;
      snapshot_.sniperChargePercent[attackerIndex] = 0U;
    } else if (
      command.weapon == Weapon::Revolver &&
      command.attack &&
      revolverCooldownTicks_[attackerIndex] == 0
    ) {
      if (targetIndex < kDuelPlayerCount) {
        weaponTargets[attackerIndex] = targetIndex;
        snapshot_.weaponFires[attackerIndex] = simulateRevolver(
          combatPlayers[attackerIndex],
          target,
          command,
          arena_,
          revolverTuning_
        );
      } else {
        WeaponFireResult& fire = snapshot_.weaponFires[attackerIndex];
        fire.weapon = Weapon::Revolver;
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
      revolverCooldownTicks_[attackerIndex] = revolverCooldownDurationTicks_;
      (void)consumeAmmo(attackerIndex, Weapon::Revolver);
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
    // Count an action only after the common gameplay path has accepted its
    // selected weapon, pullout, cooldown, ammo, and edge conditions. This is
    // deliberately not a requested-command counter.
    const bool acceptedWeaponFire =
      command.weapon == Weapon::LightningGun || command.weapon == Weapon::FreezeGun
      ? snapshot_.lightningGuns[attackerIndex].active
      : snapshot_.weaponFires[attackerIndex].fired;
    if (acceptedWeaponFire) {
      ++botRuntimeStats_.acceptedWeaponFires[attackerIndex];
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
      snapshot_.lightningGuns[attackerIndex].headshot,
      {true, snapshot_.lightningGuns[attackerIndex].start}
    );
    applyDamageAndKnockback(
      attackerIndex,
      weaponTargets[attackerIndex],
      snapshot_.weaponFires[attackerIndex].damageApplied,
      snapshot_.weaponFires[attackerIndex].knockbackImpulse,
      snapshot_.weaponFires[attackerIndex].weapon,
      snapshot_.weaponFires[attackerIndex].headshot,
      {true, snapshot_.weaponFires[attackerIndex].start}
    );
    applyDamageAndKnockback(
      attackerIndex,
      freezeTargets[attackerIndex],
      snapshot_.lightningGuns[attackerIndex].damageApplied,
      {},
      Weapon::FreezeGun,
      snapshot_.lightningGuns[attackerIndex].headshot,
      {true, snapshot_.lightningGuns[attackerIndex].start}
    );
  }

  simulateRockets(fixedDt);

  if (tickStartedLive) {
    ++snapshot_.liveTicksElapsed;
    // McGuffin rounds end through score control and the final-hold rule. A
    // generic match timer must not bypass its best-of-three round structure.
    if (
      matchRules_.timeLimitMinutes > 0 &&
      snapshot_.gameMode != GameMode::McGuffin &&
      !snapshot_.overtime &&
      snapshot_.matchPhase != MatchPhase::MatchEnd
    ) {
      const std::uint32_t limitTicks =
        static_cast<std::uint32_t>(matchRules_.timeLimitMinutes) * 60U * 125U;
      if (snapshot_.liveTicksElapsed >= limitTicks) {
        if (snapshot_.gameMode == GameMode::Duel) {
          const auto leader = uniqueScoreLeader(snapshot_.scores, occupiedPlayers());
          if (leader.has_value()) {
            beginMatchEnd(*leader);
          } else {
            snapshot_.overtime = true;
          }
        } else if (snapshot_.gameMode == GameMode::ClanArena) {
          const auto leader = clanArenaScoreLeader(snapshot_.teamScores);
          if (leader.has_value()) {
            beginMatchEnd(*leader);
          } else {
            snapshot_.overtime = true;
          }
        }
      }
    }
  }

  rememberTransientCombatEvents();
  restoreTransientCombatEvents();
  // History and outgoing snapshots use the incremented tick as the label for
  // the state produced above, keeping lag-compensation frames unambiguous.
  ++snapshot_.serverTick;
  recordHistory();
  const bool recorderNeedsCheckpoint = replayRecorder_ != nullptr &&
    replayRecorder_->needsCompletedCheckpoint(snapshot_.serverTick);
  const bool rollingNeedsCheckpoint = rollingReplay_ != nullptr &&
    rollingReplay_->needsCompletedCheckpoint(snapshot_.serverTick);
  if (recorderNeedsCheckpoint || rollingNeedsCheckpoint) {
    const auto captureStart = std::chrono::steady_clock::now();
    const replay::ReplayCheckpoint checkpoint = captureReplayCheckpoint();
    const auto captureElapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - captureStart
    );
    ++replayCheckpointCaptureStats_.captures;
    replayCheckpointCaptureStats_.nanoseconds += static_cast<std::uint64_t>(captureElapsed.count());
    if (recorderNeedsCheckpoint) {
      replayRecorder_->recordCompletedTick(checkpoint);
    }
    if (rollingNeedsCheckpoint) {
      rollingReplay_->recordCompletedTick(checkpoint);
    }
  }
  if (!replayPlayback_) publishSnapshot();
}

bool ServerGame::beginReplayRecording(
  replay::ReplayRecordingConfig config,
  std::string* error
) {
  if (replayPlayback_) {
    if (error != nullptr) *error = "cannot record while replay playback is active";
    return false;
  }
  replay::ReplayMetadata metadata = replayMetadata();
  auto recorder = std::make_unique<replay::ReplayRecorder>();
  if (!recorder->begin(std::move(metadata), captureReplayCheckpoint(), config, error)) {
    return false;
  }
  replayRecorder_ = std::move(recorder);
  return true;
}

std::optional<replay::ReplayDemo> ServerGame::finishReplayRecording() {
  if (replayRecorder_ == nullptr) return std::nullopt;
  // This one stop-time capture keeps the steady tick path free of extra state
  // copies while ensuring a demo always has a hash for its final live state.
  std::optional<replay::ReplayDemo> demo = replayRecorder_->finish(captureReplayCheckpoint());
  replayRecorder_.reset();
  return demo;
}

bool ServerGame::replayRecordingActive() const {
  return replayRecorder_ != nullptr && replayRecorder_->active();
}

replay::ReplayRecorderStats ServerGame::replayRecorderStats() const {
  return replayRecorder_ == nullptr ? replay::ReplayRecorderStats{} : replayRecorder_->stats();
}

replay::ReplayCheckpointCaptureStats ServerGame::replayCheckpointCaptureStats() const {
  return replayCheckpointCaptureStats_;
}

bool ServerGame::beginRollingReplay(
  replay::ReplayRollingBufferConfig config,
  std::string* error
) {
  if (replayPlayback_) {
    if (error != nullptr) *error = "cannot retain rolling replay while playback is active";
    return false;
  }
  auto rolling = std::make_unique<replay::ReplayRollingBuffer>();
  if (!rolling->begin(replayMetadata(), captureReplayCheckpoint(), replayGeneration_, config, error)) {
    return false;
  }
  rollingReplay_ = std::move(rolling);
  latestReplayLethal_.reset();
  return true;
}

void ServerGame::endRollingReplay() {
  rollingReplay_.reset();
  latestReplayLethal_.reset();
}

replay::ReplayRollingBufferStats ServerGame::rollingReplayStats() const {
  return rollingReplay_ == nullptr ? replay::ReplayRollingBufferStats{} : rollingReplay_->stats();
}

std::optional<replay::ReplayDemo> ServerGame::extractRollingReplaySegment(
  const replay::ReplayLethalEvent& event,
  std::uint32_t beforeTicks,
  std::uint32_t afterTicks,
  std::string* error
) const {
  if (rollingReplay_ == nullptr) {
    if (error != nullptr) *error = "rolling replay is disabled";
    return std::nullopt;
  }
  return rollingReplay_->extractSegment(event, beforeTicks, afterTicks, error);
}

std::optional<replay::ReplayLethalEvent> ServerGame::latestReplayLethal() const {
  return latestReplayLethal_;
}

replay::ReplayMetadata ServerGame::replayMetadata() const {
  replay::ReplayMetadata metadata;
  metadata.protocolRevision = kProtocolVersion;
  metadata.gameplayConfigHash = replayGameplayConfigHash();
  metadata.initialServerTick = snapshot_.serverTick;
  metadata.mapRevision = snapshot_.mapRevision;
  metadata.mapName = snapshot_.map.mapName;
  metadata.mapContentHash = snapshot_.map.contentHash;
  metadata.gameMode = snapshot_.gameMode;
  metadata.matchRules = matchRules_;
  metadata.visibility = snapshot_.gameMode == GameMode::Duel
    ? replay::ReplayVisibility::DuelOnly
    : replay::ReplayVisibility::DeveloperFull;
  for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
    metadata.players[index].slot = static_cast<std::uint8_t>(index);
    metadata.players[index].occupied = isOccupiedSlot(index);
    metadata.players[index].bot = botPlayers_[index];
    metadata.players[index].team = snapshot_.teams[index];
    metadata.players[index].name = snapshot_.playerNames[index];
  }
  return metadata;
}

void ServerGame::resetRollingReplay() {
  if (rollingReplay_ == nullptr || !rollingReplay_->active()) return;
  ++replayGeneration_;
  if (replayGeneration_ == 0U) replayGeneration_ = 1U;
  rollingReplay_->reset(replayMetadata(), captureReplayCheckpoint(), replayGeneration_);
  latestReplayLethal_.reset();
}

void ServerGame::recordReplayLethal(
  std::size_t attackerIndex,
  std::size_t targetIndex,
  Weapon weapon
) {
  if (rollingReplay_ == nullptr || !rollingReplay_->active() || targetIndex >= kDuelPlayerCount) return;
  replay::ReplayLethalEvent event;
  event.tick = snapshot_.serverTick;
  event.replayGeneration = replayGeneration_;
  event.victim = static_cast<std::uint8_t>(targetIndex);
  event.killer = attackerIndex < kDuelPlayerCount
    ? static_cast<std::uint8_t>(attackerIndex)
    : replay::kNoReplayPlayer;
  event.weapon = weapon;
  event.kind = attackerIndex == targetIndex
    ? replay::LethalKind::Self
    : attackerIndex < kDuelPlayerCount
      ? replay::LethalKind::Direct
      : replay::LethalKind::World;
  rollingReplay_->recordLethal(event);
  latestReplayLethal_ = event;
}

std::uint64_t ServerGame::replayGameplayConfigHash() const {
  std::uint64_t hash = 1469598103934665603ULL;
  const auto mixByte = [&hash](std::uint8_t value) {
    hash ^= value;
    hash *= 1099511628211ULL;
  };
  const auto mixU32 = [&mixByte](std::uint32_t value) {
    for (unsigned shift = 0; shift < 32U; shift += 8U) {
      mixByte(static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }
  };
  const auto mixI32 = [&mixU32](std::int32_t value) {
    mixU32(std::bit_cast<std::uint32_t>(value));
  };
  const auto mixF32 = [&mixU32](float value) {
    mixU32(std::bit_cast<std::uint32_t>(value));
  };
  const auto mixBool = [&mixByte](bool value) { mixByte(value ? 1U : 0U); };
  const auto mixMovement = [&mixBool, &mixF32](const MovementTuning& tuning) {
    mixBool(tuning.flightEnabled);
    mixF32(tuning.groundAcceleration); mixF32(tuning.airAcceleration);
    mixF32(tuning.groundFriction); mixF32(tuning.stopSpeed); mixF32(tuning.gravity);
    mixF32(tuning.maxGroundSpeed); mixF32(tuning.maxAirSpeed); mixF32(tuning.jumpImpulse);
    mixBool(tuning.airControlEnabled); mixF32(tuning.dashTargetSpeed); mixF32(tuning.dashMaxSpeed);
    mixF32(tuning.dashAcceleration); mixF32(tuning.dashDuration); mixF32(tuning.dashCooldown);
    mixF32(tuning.dashGroundHopVelocity); mixF32(tuning.dashAirHopVelocity);
    mixF32(tuning.flightAcceleration); mixF32(tuning.maxFlightSpeed); mixF32(tuning.flightDamping);
    mixF32(tuning.flightGravityCancel);
  };
  mixMovement(movementTuning_);
  mixF32(playerSizeScaleXY_); mixF32(playerSizeScaleZ_); mixF32(lightningKnockback_);
  mixF32(lightningFireHz_); mixF32(rocketKnockback_); mixI32(knockbackTimeMs_);
  mixI32(weaponDamage_.shotgunDamagePerPellet); mixI32(weaponDamage_.machineGunDamage);
  mixI32(weaponDamage_.lightningGunDamage); mixI32(weaponDamage_.railgunDamage);
  mixI32(weaponDamage_.rocketLauncherDamage); mixI32(weaponDamage_.plasmaGunDamage);
  mixI32(weaponDamage_.freezeGunDamage); mixF32(vampirism_); mixByte(selfDamagePercent_);
  mixI32(healthAmount_); mixBool(weaponAmmoConfig_.infiniteAmmo);
  for (const std::int32_t ammo : weaponAmmoConfig_.spawnAmmo) mixI32(ammo);
  const auto mixLightning = [&mixF32](const LightningGunTuning& tuning) {
    mixF32(tuning.range); mixF32(tuning.damagePerSecond); mixF32(tuning.fireHz);
    mixF32(tuning.eyeHeight); mixF32(tuning.knockbackPerSecond); mixF32(tuning.headshotMultiplier);
  };
  mixLightning(lightningGunTuning_);
  mixF32(freezeGunTuning_.range); mixF32(freezeGunTuning_.fireHz); mixF32(freezeGunTuning_.eyeHeight);
  mixF32(freezeGunTuning_.damagePerSecond); mixF32(freezeGunTuning_.freezePerSecond);
  mixF32(freezeGunTuning_.decayPerSecond); mixF32(freezeGunTuning_.maxLevel);
  mixF32(freezeGunTuning_.maxSlowFraction); mixF32(freezeGunTuning_.headshotMultiplier);
  mixF32(icePoolTuning_.maxRadius); mixF32(icePoolTuning_.growthPerSecond);
  mixF32(icePoolTuning_.lifetimeSeconds); mixF32(icePoolTuning_.friction);
  mixF32(icePoolTuning_.slopeGravityScale); mixF32(icePoolTuning_.controlScale);
  mixF32(icePoolTuning_.mergeDistance);
  const auto mixHitscan = [&mixF32, &mixI32](const HitscanTuning& tuning) {
    mixF32(tuning.range); mixI32(tuning.damage); mixF32(tuning.eyeHeight);
    mixF32(tuning.knockback); mixF32(tuning.headshotMultiplier);
  };
  mixHitscan(railgunTuning_); mixF32(sniperChargeSeconds_); mixF32(sniperMaxDamageMultiplier_);
  mixU32(railgunCooldownDurationTicks_); mixHitscan(revolverTuning_); mixU32(revolverCooldownDurationTicks_);
  mixF32(machineGunTuning_.range); mixI32(machineGunTuning_.damage); mixF32(machineGunTuning_.eyeHeight);
  mixF32(machineGunTuning_.knockback); mixF32(machineGunTuning_.spreadRadians);
  mixF32(machineGunTuning_.headshotMultiplier); mixU32(machineGunCooldownDurationTicks_);
  mixF32(shotgunTuning_.range); mixByte(shotgunTuning_.pelletCount); mixI32(shotgunTuning_.damagePerPellet);
  mixF32(shotgunTuning_.spreadRadians); mixF32(shotgunTuning_.eyeHeight); mixF32(shotgunTuning_.knockback);
  mixF32(shotgunTuning_.headshotMultiplier); mixU32(shotgunCooldownDurationTicks_);
  mixF32(rocketLauncherTuning_.speed); mixF32(rocketLauncherTuning_.radius);
  mixF32(rocketLauncherTuning_.directHitboxHalfExtentXY); mixF32(rocketLauncherTuning_.directHitboxHalfExtentZ);
  mixI32(rocketLauncherTuning_.directDamage); mixI32(rocketLauncherTuning_.splashDamage);
  mixF32(rocketLauncherTuning_.knockback); mixF32(rocketLauncherTuning_.eyeHeight);
  mixU32(rocketLauncherTuning_.maxLifetimeTicks); mixU32(rocketLauncherCooldownDurationTicks_);
  mixF32(grenadeLauncherTuning_.speed); mixF32(grenadeLauncherTuning_.verticalBoost);
  mixF32(grenadeLauncherTuning_.gravity); mixF32(grenadeLauncherTuning_.bounceDamping);
  mixF32(grenadeLauncherTuning_.restSpeed); mixF32(grenadeLauncherTuning_.bounceSoundMinSpeed);
  mixF32(grenadeLauncherTuning_.projectileRadius); mixF32(grenadeLauncherTuning_.projectileHitboxRadius);
  mixF32(grenadeLauncherTuning_.radius); mixI32(grenadeLauncherTuning_.directDamage);
  mixI32(grenadeLauncherTuning_.splashDamage); mixF32(grenadeLauncherTuning_.knockback);
  mixF32(grenadeLauncherTuning_.eyeHeight); mixU32(grenadeLauncherTuning_.fuseTicks);
  mixU32(grenadeLauncherTuning_.cooldownTicks);
  mixF32(plasmaGunTuning_.speed); mixF32(plasmaGunTuning_.radius);
  mixF32(plasmaGunTuning_.directHitboxHalfExtentXY); mixF32(plasmaGunTuning_.directHitboxHalfExtentZ);
  mixI32(plasmaGunTuning_.damage); mixF32(plasmaGunTuning_.knockback); mixF32(plasmaGunTuning_.eyeHeight);
  mixU32(plasmaGunTuning_.maxLifetimeTicks); mixU32(plasmaGunTuning_.cooldownTicks);
  mixU32(weaponPulloutDurationTicks_); mixU32(jumpPadRetriggerCooldownTicks_);
  mixI32(smallHealthPickupAmount_); mixI32(largeHealthPickupAmount_);
  mixU32(smallHealthPickupCooldownTicks_); mixU32(largeHealthPickupCooldownTicks_);
  mixU32(mcguffinConfig_.scoreLimit); mixU32(mcguffinConfig_.pointsPerSecond);
  mixU32(mcguffinConfig_.carryPointsPerSecond); mixU32(mcguffinConfig_.carryPointLimit);
  mixU32(mcguffinConfig_.initialSpawnTicks); mixU32(mcguffinConfig_.installationDelayTicks);
  mixU32(mcguffinConfig_.stealTicks); mixU32(mcguffinConfig_.returnTicks);
  mixF32(mcguffinConfig_.throwSpeed); mixF32(mcguffinConfig_.throwUpSpeed);
  mixF32(mcguffinConfig_.throwVelocityInheritance); mixF32(mcguffinConfig_.throwGravity);
  mixF32(mcguffinConfig_.throwBounceDamping); mixU32(mcguffinConfig_.throwPickupLockoutTicks);
  mixU32(mcguffinConfig_.finalHoldTicks); mixF32(mcguffinConfig_.pickupRadius);
  mixU32(matchRules_.roundLimit); mixU32(matchRules_.timeLimitMinutes); mixByte(matchRules_.playerLimit);
  mixU32(matchRules_.countdownTicks); mixU32(matchRules_.roundEndTicks); mixU32(matchRules_.matchEndTicks);
  mixU32(matchRules_.deathRespawnTicks); mixBool(matchRules_.showOpponentHealth);
  mixByte(static_cast<std::uint8_t>(weaponSwitchingMode_));
  // Bot tuning and bot RNG intentionally do not enter this fingerprint.
  return hash;
}

replay::ReplayTickInput ServerGame::captureResolvedReplayInput() const {
  replay::ReplayTickInput input;
  // This is the pre-simulation label. Output checkpoints and hashes use the
  // incremented label produced after this frame has completed.
  input.tick = snapshot_.serverTick;
  for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
    replay::ReplaySlotInput& slot = input.slots[index];
    slot.present = isOccupiedSlot(index);
    // Sparse v2 leaves absent slots fully default. In particular, do not copy
    // stale commands from a player who has just left the match.
    if (!slot.present) continue;
    slot.hasCommand = hasCommand_[index];
    slot.receivedThisTick = receivedCommandThisTick_[index];
    slot.command = commands_[index];
    slot.viewedServerTick = viewedServerTicks_[index];
    slot.consumedActionEdges = lastActionEdges_[index];
    slot.jumpEdgeAccepted = jumpEdgeThisTick_[index];
    slot.dashEdgeAccepted = dashEdgeThisTick_[index];
    slot.attackEdgeAccepted = attackEdgeThisTick_[index];
    slot.attackEdgeCommand = attackEdgeCommands_[index];
    slot.attackEdgeViewedServerTick = attackEdgeViewedServerTicks_[index];
    slot.mcguffinThrowAccepted = mcguffinThrowRequestedThisTick_[index];
    slot.mcguffinThrowCommand = mcguffinThrowCommands_[index];
  }
  return input;
}

void ServerGame::applyReplayInput(const replay::ReplayTickInput& input) {
  for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
    const replay::ReplaySlotInput& source = input.slots[index];
    commands_[index] = source.command;
    hasCommand_[index] = source.hasCommand;
    receivedCommandThisTick_[index] = source.receivedThisTick;
    viewedServerTicks_[index] = source.viewedServerTick;
    lastActionEdges_[index] = source.consumedActionEdges;
    jumpEdgeThisTick_[index] = source.jumpEdgeAccepted;
    dashEdgeThisTick_[index] = source.dashEdgeAccepted;
    attackEdgeThisTick_[index] = source.attackEdgeAccepted;
    attackEdgeCommands_[index] = source.attackEdgeCommand;
    attackEdgeViewedServerTicks_[index] = source.attackEdgeViewedServerTick;
    mcguffinThrowRequestedThisTick_[index] = source.mcguffinThrowAccepted;
    mcguffinThrowCommands_[index] = source.mcguffinThrowCommand;
  }
}

bool ServerGame::injectReplayInput(
  const replay::ReplayTickInput& input,
  std::string* error
) {
  if (!replayPlayback_) {
    if (error != nullptr) *error = "replay playback is not active";
    return false;
  }
  if (pendingReplayInput_.has_value() || input.tick != snapshot_.serverTick) {
    if (error != nullptr) *error = "replay input tick does not match server state";
    return false;
  }
  for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
    if (input.slots[index].present != isOccupiedSlot(index)) {
      if (error != nullptr) *error = "replay input occupancy does not match checkpoint";
      return false;
    }
  }
  pendingReplayInput_ = input;
  if (error != nullptr) error->clear();
  return true;
}

void ServerGame::endReplayPlayback() {
  pendingReplayInput_.reset();
  replayPlayback_ = false;
}

replay::ReplayCheckpoint ServerGame::captureReplayCheckpoint() const {
  replay::ReplayCheckpoint checkpoint;
  checkpoint.serverTick = snapshot_.serverTick;
  checkpoint.mapRevision = snapshot_.mapRevision;
  checkpoint.projectileRevision = projectileRevision_;
  checkpoint.gameplayConfigHash = replayGameplayConfigHash();
  for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
    replay::ReplayCheckpointPlayer& target = checkpoint.players[index];
    target.connected = snapshot_.connectedPlayers[index];
    target.participating = snapshot_.participatingPlayers[index];
    target.ready = snapshot_.readyPlayers[index];
    target.team = snapshot_.teams[index];
    target.player = snapshot_.players[index];
    target.weapon.selectedWeapon = selectedWeapons_[index];
    target.weapon.ammo = playerAmmo_[index];
    target.weapon.lightningGun = lightningGunStates_[index];
    target.weapon.freezeGun = freezeGunStates_[index];
    target.weapon.lightningAmmoCredit = lightningAmmoCredit_[index];
    target.weapon.freezeAmmoCredit = freezeAmmoCredit_[index];
    target.weapon.fractionalVampirismHealing = fractionalVampirismHealing_[index];
    target.weapon.railgunCooldownTicks = railgunCooldownTicks_[index];
    target.weapon.revolverCooldownTicks = revolverCooldownTicks_[index];
    target.weapon.sniperAdsFraction = sniperAdsFractions_[index];
    target.weapon.sniperChargeFraction = sniperChargeFractions_[index];
    target.weapon.machineGunCooldownTicks = machineGunCooldownTicks_[index];
    target.weapon.shotgunCooldownTicks = shotgunCooldownTicks_[index];
    target.weapon.rocketCooldownTicks = rocketCooldownTicks_[index];
    target.weapon.grenadeCooldownTicks = grenadeCooldownTicks_[index];
    target.weapon.plasmaGunCooldownTicks = plasmaGunCooldownTicks_[index];
    target.weapon.weaponPulloutTicks = weaponPulloutTicks_[index];
    target.respawnTicksRemaining = snapshot_.respawnTicksRemaining[index];
    target.command = commands_[index];
    target.consumedActionEdges = lastActionEdges_[index];
    target.viewedServerTick = viewedServerTicks_[index];
    target.hasCommand = hasCommand_[index];
  }
  for (std::size_t index = 0; index < kMaxRocketProjectiles; ++index) {
    const RocketProjectile& source = rockets_[index];
    replay::ReplayProjectile& target = checkpoint.projectiles[index];
    target.active = source.active;
    target.owner = source.owner;
    target.sequence = source.sequence;
    target.weapon = source.weapon;
    target.position = source.position;
    target.previousPosition = source.previousPosition;
    target.velocity = source.velocity;
    target.projectileRadius = source.projectileRadius;
    target.projectileHitboxRadius = source.projectileHitboxRadius;
    target.ownerCollisionArmed = source.ownerCollisionArmed;
    target.resting = source.resting;
    target.ageTicks = source.ageTicks;
  }
  checkpoint.match.gameMode = snapshot_.gameMode;
  checkpoint.match.phase = snapshot_.matchPhase;
  checkpoint.match.phaseTicksRemaining = snapshot_.phaseTicksRemaining;
  checkpoint.match.liveTicksElapsed = snapshot_.liveTicksElapsed;
  checkpoint.match.overtime = snapshot_.overtime;
  checkpoint.match.scores = snapshot_.scores;
  checkpoint.match.teamScores = snapshot_.teamScores;
  checkpoint.match.mcguffinScores = snapshot_.mcguffinScores;
  checkpoint.match.mcguffinRoundsWon = snapshot_.mcguffinRoundsWon;
  checkpoint.match.mcguffinRound = snapshot_.mcguffinRound;
  checkpoint.match.roundWinner = snapshot_.roundWinner;
  checkpoint.match.matchWinner = snapshot_.matchWinner;
  checkpoint.match.roundWinningTeam = snapshot_.roundWinningTeam;
  checkpoint.match.matchWinningTeam = snapshot_.matchWinningTeam;
  checkpoint.match.roundCombatStats = snapshot_.roundCombatStats;
  checkpoint.match.matchCombatStats = snapshot_.matchCombatStats;
  checkpoint.healthPickupAvailable = snapshot_.healthPickupAvailable;
  checkpoint.healthPickupCooldownTicks = healthPickupCooldownTicks_;
  checkpoint.icePools = snapshot_.icePools;
  checkpoint.mcguffin = mcguffinObjective_;
  checkpoint.mcguffinRedBaseOwner = snapshot_.mcguffinRedBaseOwner;
  checkpoint.mcguffinBlueBaseOwner = snapshot_.mcguffinBlueBaseOwner;
  checkpoint.mcguffinStealTicks = mcguffinStealTicks_;
  checkpoint.mcguffinCarrySubPoints = mcguffinCarrySubPoints_;
  checkpoint.mcguffinCarriedPoints = mcguffinCarriedPoints_;
  checkpoint.mcguffinFinalHoldTicks = mcguffinFinalHoldTicks_;
  checkpoint.mcguffinRoundLiveTicks = mcguffinRoundLiveTicks_;
  checkpoint.mcguffinThrowPickupLockoutTicks = mcguffinThrowPickupLockoutTicks_;
  checkpoint.spawnRandomState = spawnRandomState_;
  checkpoint.projectileSequences = projectileSequences_;
  checkpoint.rocketExplosionSequences = rocketExplosionSequences_;
  checkpoint.fragEventSequences = fragEventSequences_;
  checkpoint.localHitFeedbackSequences = localHitFeedbackSequences_;
  checkpoint.damageTakenSequences = damageTakenSequences_;
  checkpoint.footstepSequences = footstepSequences_;
  for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
    checkpoint.footstepStates[index] = {
      footstepStates_[index].previousPosition,
      footstepStates_[index].distanceSinceStep,
      footstepStates_[index].wasOnGround,
      footstepStates_[index].initialized,
    };
  }
  checkpoint.grenadeBounceEventSequences = grenadeBounceEventSequences_;
  checkpoint.grenadeBounceSequences = grenadeBounceSequences_;
  checkpoint.spawnLastUsedTicks = spawnLastUsedTicks_;
  checkpoint.spawnWasUsed = spawnWasUsed_;
  checkpoint.nextDeathmatchSpawnIndex = static_cast<std::uint32_t>(nextDeathmatchSpawnIndex_);
  checkpoint.playersColliding = snapshot_.playersColliding;
  checkpoint.history.reserve(history_.size());
  for (const HistoryFrame& frame : history_) {
    checkpoint.history.push_back({frame.serverTick, frame.players});
  }
  return checkpoint;
}

bool ServerGame::restoreReplayCheckpoint(
  const replay::ReplayCheckpoint& checkpoint,
  const replay::ReplayMetadata& metadata,
  std::string* error
) {
  const auto reject = [error](const char* message) {
    if (error != nullptr) *error = message;
    return false;
  };
  const bool validSpawnCursor = arena_.spawnCount == 0U
    ? checkpoint.nextDeathmatchSpawnIndex == 0U
    : checkpoint.nextDeathmatchSpawnIndex < arena_.spawnCount;
  if (!replay::validateReplayCheckpoint(checkpoint) || !validSpawnCursor) {
    return reject("replay checkpoint has invalid bounded state");
  }
  if (metadata.mapName != mapDescriptor_.mapName ||
      metadata.mapContentHash != mapDescriptor_.contentHash ||
      checkpoint.mapRevision != metadata.mapRevision ||
      checkpoint.gameplayConfigHash != metadata.gameplayConfigHash ||
      replayGameplayConfigHash() != metadata.gameplayConfigHash ||
      checkpoint.match.gameMode != metadata.gameMode) {
    return reject("replay checkpoint does not match the loaded map or metadata");
  }
  for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
    const replay::ReplayPlayerMetadata& player = metadata.players[index];
    if (player.slot != index || (!player.occupied && player.bot) ||
        (player.bot && checkpoint.players[index].connected) ||
        (!player.occupied && (checkpoint.players[index].connected || checkpoint.players[index].participating))) {
      return reject("replay player metadata does not match checkpoint occupancy");
    }
  }

  ++damageFeedbackRevision_;
  if (damageFeedbackRevision_ == 0U) {
    damageFeedbackRevision_ = 1U;
  }

  // Preserve the loaded arena and current gameplay tuning. A replay rejects a
  // different map above; callers must configure an equivalent server before
  // playback rather than silently simulating with changed rules.
  const ServerSnapshot configuredSnapshot = snapshot_;
  snapshot_ = {};
  mapRevision_ = checkpoint.mapRevision;
  projectileRevision_ = checkpoint.projectileRevision;
  snapshot_.serverTick = checkpoint.serverTick;
  snapshot_.mapRevision = checkpoint.mapRevision;
  snapshot_.damageFeedbackRevision = damageFeedbackRevision_;
  snapshot_.projectileRevision = checkpoint.projectileRevision;
  snapshot_.map = mapDescriptor_;
  snapshot_.gameMode = checkpoint.match.gameMode;
  matchRules_ = metadata.matchRules;
  snapshot_.matchRules = matchRules_;
  snapshot_.movementTuning = configuredSnapshot.movementTuning;
  snapshot_.playerSizeScaleXY = configuredSnapshot.playerSizeScaleXY;
  snapshot_.playerSizeScaleZ = configuredSnapshot.playerSizeScaleZ;
  snapshot_.lightningKnockback = configuredSnapshot.lightningKnockback;
  snapshot_.lightningFireHz = configuredSnapshot.lightningFireHz;
  snapshot_.rocketKnockback = configuredSnapshot.rocketKnockback;
  snapshot_.knockbackTimeMs = configuredSnapshot.knockbackTimeMs;
  snapshot_.weaponDamage = configuredSnapshot.weaponDamage;
  snapshot_.icePoolTuning = configuredSnapshot.icePoolTuning;
  snapshot_.projectilePresentation = configuredSnapshot.projectilePresentation;
  snapshot_.vampirism = configuredSnapshot.vampirism;
  snapshot_.selfDamagePercent = configuredSnapshot.selfDamagePercent;
  snapshot_.healthAmount = configuredSnapshot.healthAmount;
  snapshot_.mcguffinConfig = configuredSnapshot.mcguffinConfig;
  snapshot_.weaponSwitchingMode = configuredSnapshot.weaponSwitchingMode;
  snapshot_.weaponAmmo = configuredSnapshot.weaponAmmo;
  snapshot_.configurationRevision = configuredSnapshot.configurationRevision;
  snapshot_.hasConfiguration = configuredSnapshot.hasConfiguration;
  for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
    const replay::ReplayCheckpointPlayer& source = checkpoint.players[index];
    snapshot_.connectedPlayers[index] = source.connected;
    botPlayers_[index] = metadata.players[index].occupied && metadata.players[index].bot;
    snapshot_.botPlayers[index] = botPlayers_[index];
    snapshot_.participatingPlayers[index] = source.participating;
    snapshot_.readyPlayers[index] = source.ready;
    snapshot_.teams[index] = source.team;
    snapshot_.players[index] = source.player;
    selectedWeapons_[index] = source.weapon.selectedWeapon;
    snapshot_.selectedWeapons[index] = selectedWeapons_[index];
    playerAmmo_[index] = source.weapon.ammo;
    snapshot_.playerAmmo[index] = playerAmmo_[index];
    lightningGunStates_[index] = source.weapon.lightningGun;
    freezeGunStates_[index] = source.weapon.freezeGun;
    lightningAmmoCredit_[index] = source.weapon.lightningAmmoCredit;
    freezeAmmoCredit_[index] = source.weapon.freezeAmmoCredit;
    fractionalVampirismHealing_[index] = source.weapon.fractionalVampirismHealing;
    railgunCooldownTicks_[index] = source.weapon.railgunCooldownTicks;
    revolverCooldownTicks_[index] = source.weapon.revolverCooldownTicks;
    sniperAdsFractions_[index] = source.weapon.sniperAdsFraction;
    sniperChargeFractions_[index] = source.weapon.sniperChargeFraction;
    snapshot_.sniperChargePercent[index] = static_cast<std::uint8_t>(std::lround(
      source.weapon.sniperChargeFraction * 100.0F
    ));
    machineGunCooldownTicks_[index] = source.weapon.machineGunCooldownTicks;
    shotgunCooldownTicks_[index] = source.weapon.shotgunCooldownTicks;
    rocketCooldownTicks_[index] = source.weapon.rocketCooldownTicks;
    grenadeCooldownTicks_[index] = source.weapon.grenadeCooldownTicks;
    plasmaGunCooldownTicks_[index] = source.weapon.plasmaGunCooldownTicks;
    weaponPulloutTicks_[index] = source.weapon.weaponPulloutTicks;
    snapshot_.respawnTicksRemaining[index] = source.respawnTicksRemaining;
    commands_[index] = source.command;
    lastActionEdges_[index] = source.consumedActionEdges;
    viewedServerTicks_[index] = source.viewedServerTick;
    hasCommand_[index] = source.hasCommand;
    snapshot_.playerNames[index] = metadata.players[index].name;
  }
  for (std::size_t index = 0; index < kMaxRocketProjectiles; ++index) {
    const replay::ReplayProjectile& source = checkpoint.projectiles[index];
    rockets_[index] = {
      source.active,
      source.owner,
      source.sequence,
      source.weapon,
      source.position,
      source.previousPosition,
      source.projectileRadius,
      source.projectileHitboxRadius,
      source.ownerCollisionArmed,
      source.resting,
      source.velocity,
      source.ageTicks,
    };
  }
  snapshot_.matchPhase = checkpoint.match.phase;
  snapshot_.phaseTicksRemaining = checkpoint.match.phaseTicksRemaining;
  snapshot_.liveTicksElapsed = checkpoint.match.liveTicksElapsed;
  snapshot_.overtime = checkpoint.match.overtime;
  snapshot_.scores = checkpoint.match.scores;
  snapshot_.teamScores = checkpoint.match.teamScores;
  snapshot_.mcguffinScores = checkpoint.match.mcguffinScores;
  snapshot_.mcguffinRoundsWon = checkpoint.match.mcguffinRoundsWon;
  snapshot_.mcguffinRound = checkpoint.match.mcguffinRound;
  snapshot_.roundWinner = checkpoint.match.roundWinner;
  snapshot_.matchWinner = checkpoint.match.matchWinner;
  snapshot_.roundWinningTeam = checkpoint.match.roundWinningTeam;
  snapshot_.matchWinningTeam = checkpoint.match.matchWinningTeam;
  snapshot_.roundCombatStats = checkpoint.match.roundCombatStats;
  snapshot_.matchCombatStats = checkpoint.match.matchCombatStats;
  snapshot_.healthPickupAvailable = checkpoint.healthPickupAvailable;
  healthPickupCooldownTicks_ = checkpoint.healthPickupCooldownTicks;
  snapshot_.icePools = checkpoint.icePools;
  mcguffinObjective_ = checkpoint.mcguffin;
  snapshot_.mcguffinRedBaseOwner = checkpoint.mcguffinRedBaseOwner;
  snapshot_.mcguffinBlueBaseOwner = checkpoint.mcguffinBlueBaseOwner;
  mcguffinStealTicks_ = checkpoint.mcguffinStealTicks;
  mcguffinCarrySubPoints_ = checkpoint.mcguffinCarrySubPoints;
  mcguffinCarriedPoints_ = checkpoint.mcguffinCarriedPoints;
  mcguffinFinalHoldTicks_ = checkpoint.mcguffinFinalHoldTicks;
  mcguffinRoundLiveTicks_ = checkpoint.mcguffinRoundLiveTicks;
  mcguffinThrowPickupLockoutTicks_ = checkpoint.mcguffinThrowPickupLockoutTicks;
  spawnRandomState_ = checkpoint.spawnRandomState;
  projectileSequences_ = checkpoint.projectileSequences;
  rocketExplosionSequences_ = checkpoint.rocketExplosionSequences;
  fragEventSequences_ = checkpoint.fragEventSequences;
  localHitFeedbackSequences_ = checkpoint.localHitFeedbackSequences;
  damageTakenSequences_ = checkpoint.damageTakenSequences;
  footstepSequences_ = checkpoint.footstepSequences;
  grenadeBounceEventSequences_ = checkpoint.grenadeBounceEventSequences;
  grenadeBounceSequences_ = checkpoint.grenadeBounceSequences;
  spawnLastUsedTicks_ = checkpoint.spawnLastUsedTicks;
  spawnWasUsed_ = checkpoint.spawnWasUsed;
  nextDeathmatchSpawnIndex_ = checkpoint.nextDeathmatchSpawnIndex;
  snapshot_.playersColliding = checkpoint.playersColliding;
  history_.clear();
  for (const replay::ReplayHistoryFrame& frame : checkpoint.history) {
    history_.push_back({frame.serverTick, frame.players});
  }
  receivedCommandThisTick_ = {};
  jumpEdgeThisTick_ = {};
  dashEdgeThisTick_ = {};
  attackEdgeThisTick_ = {};
  attackEdgeCommands_ = {};
  attackEdgeViewedServerTicks_ = {};
  mcguffinThrowRequestedThisTick_ = {};
  mcguffinThrowCommands_ = {};
  botDodgeDirections_ = {};
  botDodgeSwitchSeconds_ = {};
  footstepStates_ = {};
  for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
    footstepStates_[index] = {
      checkpoint.footstepStates[index].previousPosition,
      checkpoint.footstepStates[index].distanceSinceStep,
      checkpoint.footstepStates[index].wasOnGround,
      checkpoint.footstepStates[index].initialized,
    };
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
  recentLocalHitFeedbackEvents_ = {};
  recentLocalHitFeedbackEventTicks_ = {};
  spawnedProjectileCount_ = 0;
  recentProjectileRemovals_.clear();
  projectileCorrectionCursor_ = 0;
  playerSessions_ = {};
  pendingReplayInput_.reset();
  replayPlayback_ = true;
  if (error != nullptr) error->clear();
  return true;
}

void ServerGame::resetMatch() {
  ++damageFeedbackRevision_;
  if (damageFeedbackRevision_ == 0U) {
    damageFeedbackRevision_ = 1U;
  }
  const std::uint32_t serverTick = snapshot_.serverTick;
  const auto playerNames = snapshot_.playerNames;
  const auto connectedPlayers = snapshot_.connectedPlayers;
  const auto botPlayers = botPlayers_;
  const GameMode gameMode = snapshot_.gameMode;
  const auto teams = snapshot_.teams;
  botHiddenAttackInvariantCount_ = 0;
  botRuntimeStats_ = {};
  botTargetObserved_ = {};
  botRecovering_ = {};
  snapshot_ = {};
  snapshot_.serverTick = serverTick;
  snapshot_.mapRevision = mapRevision_;
  snapshot_.damageFeedbackRevision = damageFeedbackRevision_;
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
    const std::size_t spawnIndex = arena_.spawnCount == 0
      ? 0U
      : playerIndex % arena_.spawnCount;
    player.position.z = arena_.spawnPositions[spawnIndex].z + player.bounds.halfHeight;
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
  snapshot_.botWeapon = botWeapon_;
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
  revolverCooldownTicks_ = {};
  sniperAdsFractions_ = {};
  sniperChargeFractions_ = {};
  snapshot_.sniperChargePercent = {};
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
  recentLocalHitFeedbackEvents_ = {};
  recentLocalHitFeedbackEventTicks_ = {};
  recentDamageTakenEvents_ = {};
  recentDamageTakenEventTicks_ = {};
  footstepStates_ = {};
  footstepSequences_ = {};
  clearProjectiles();
  snapshot_.icePools = {};
  grenadeBounceSequences_ = {};
  grenadeBounceEventSequences_ = {};
  fractionalVampirismHealing_ = {};
  commands_ = {};
  viewedServerTicks_ = {};
  hasCommand_ = {};
  receivedCommandThisTick_ = {};
  playerSessions_ = {};
  botMotors_ = {};
  botSenseFrames_ = {};
  botSenseFrameValid_ = {};
  for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
    botBrains_[index].reset(0xB07D0D6EU ^ static_cast<std::uint32_t>(index * 0x9e3779b9U));
  }
  if (snapshot_.gameMode == GameMode::McGuffin) {
    resetMcGuffinRound();
  }
  updateParticipatingPlayers();
  history_.clear();
  recordHistory();
  resetRollingReplay();
}

void ServerGame::setArena(const Arena& arena) {
  setArena(arena, describeMap("custom", arena));
}

void ServerGame::setArena(const Arena& arena, MapDescriptor descriptor) {
  arena_ = arena;
  spawnLastUsedTicks_ = {};
  spawnWasUsed_ = {};
  spawnRandomState_ = 0x51A7E123U;
  nextDeathmatchSpawnIndex_ = arena_.spawnCount == 0U
    ? 0U
    : kDuelPlayerCount % arena_.spawnCount;
  spawnDebugString_ = "no team spawn selected yet";
  mapDescriptor_ = std::move(descriptor);
  ++mapRevision_;
  if (mapRevision_ == 0) {
    mapRevision_ = 1;
  }
  rebuildBotNavigation();
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
  botMotors_[playerIndex] = {};
  botSenseFrames_[playerIndex] = {};
  botSenseFrameValid_[playerIndex] = false;
  botTargetObserved_[playerIndex] = false;
  botRecovering_[playerIndex] = false;
  botBrains_[playerIndex].reset(
    0xB07D0D6EU ^ static_cast<std::uint32_t>(playerIndex * 0x9e3779b9U)
  );
  snapshot_.respawnTicksRemaining[playerIndex] = 0;
  snapshot_.players[playerIndex] = spawnPlayer(arena_, playerIndex, healthAmount_);
  snapshot_.players[playerIndex].bounds.radius =
    kDefaultPlayerBounds.radius * playerSizeScaleXY_;
  snapshot_.players[playerIndex].bounds.halfHeight =
    kDefaultPlayerBounds.halfHeight * playerSizeScaleZ_;
  const std::size_t spawnIndex = arena_.spawnCount == 0
    ? 0U
    : playerIndex % arena_.spawnCount;
  snapshot_.players[playerIndex].position.z =
    arena_.spawnPositions[spawnIndex].z + snapshot_.players[playerIndex].bounds.halfHeight;
  if (snapshot_.gameMode != GameMode::McGuffin && arena_.spawnCount > 0U) {
    // Player slots are not spawn slots. Walk the complete authored pool so a
    // 16-player match can use points 17..32, skipping solid or occupied points
    // before falling back to the player's stable legacy spawn.
    std::size_t selectedSpawn = spawnIndex;
    for (std::size_t attempt = 0; attempt < arena_.spawnCount; ++attempt) {
      const std::size_t candidate = nextDeathmatchSpawnIndex_;
      nextDeathmatchSpawnIndex_ =
        (nextDeathmatchSpawnIndex_ + 1U) % arena_.spawnCount;
      PlayerState atSpawn = snapshot_.players[playerIndex];
      atSpawn.position = arena_.spawnPositions[candidate];
      atSpawn.position.z += atSpawn.bounds.halfHeight;
      if (playerPositionSolid(arena_, atSpawn, atSpawn.position)) continue;

      bool occupied = false;
      for (std::size_t other = 0; other < kDuelPlayerCount; ++other) {
        if (other == playerIndex || !isActiveCombatant(other)) continue;
        const PlayerState& body = snapshot_.players[other];
        const float dx = atSpawn.position.x - body.position.x;
        const float dy = atSpawn.position.y - body.position.y;
        const float radius = atSpawn.bounds.radius + body.bounds.radius;
        occupied = std::fabs(atSpawn.position.z - body.position.z) <
            atSpawn.bounds.halfHeight + body.bounds.halfHeight &&
          dx * dx + dy * dy < radius * radius;
        if (occupied) break;
      }
      if (!occupied) {
        selectedSpawn = candidate;
        break;
      }
    }
    snapshot_.players[playerIndex].position = arena_.spawnPositions[selectedSpawn];
    snapshot_.players[playerIndex].position.z +=
      snapshot_.players[playerIndex].bounds.halfHeight;
    snapshot_.players[playerIndex].viewYawRadians = std::atan2(
      -snapshot_.players[playerIndex].position.y,
      -snapshot_.players[playerIndex].position.x
    );
  } else if (snapshot_.gameMode == GameMode::McGuffin &&
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
      for (std::size_t spawnIndex = 0; spawnIndex < arena_.spawnCount; ++spawnIndex) {
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
  revolverCooldownTicks_[playerIndex] = 0;
  sniperAdsFractions_[playerIndex] = 0.0F;
  sniperChargeFractions_[playerIndex] = 0.0F;
  snapshot_.sniperChargePercent[playerIndex] = 0U;
  machineGunCooldownTicks_[playerIndex] = 0;
  shotgunCooldownTicks_[playerIndex] = 0;
  rocketCooldownTicks_[playerIndex] = 0;
  grenadeCooldownTicks_[playerIndex] = 0;
  plasmaGunCooldownTicks_[playerIndex] = 0;
  selectedWeapons_[playerIndex] = botPlayers_[playerIndex]
    ? botWeapon_
    : Weapon::LightningGun;
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
  botMotors_ = {};
  for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
    botBrains_[index].reset(0xB07D0D6EU ^ static_cast<std::uint32_t>(index * 0x9e3779b9U));
  }
  clearProjectiles();
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
      botMotors_[index] = {};
      botBrains_[index].reset(0xB07D0D6EU ^ static_cast<std::uint32_t>(index * 0x9e3779b9U));
    } else if (!wasHuman && isHuman) {
      snapshot_.readyPlayers[index] = false;
      snapshot_.teams[index] = Team::None;
      snapshot_.playerNames[index] = "PLAYER " + std::to_string(index + 1U);
      resetPlayerInputState(index);
      botDodgeSwitchSeconds_[index] = 0.0F;
      botMotors_[index] = {};
      botBrains_[index].reset(0xB07D0D6EU ^ static_cast<std::uint32_t>(index * 0x9e3779b9U));
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
    snapshot_.overtime = false;
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
  selectedWeapons_[playerIndex] = botPlayers_[playerIndex]
    ? botWeapon_
    : Weapon::LightningGun;
  weaponPulloutTicks_[playerIndex] = 0;
  snapshot_.selectedWeapons[playerIndex] = selectedWeapons_[playerIndex];
  refillAmmo(playerIndex);
  botMotors_[playerIndex] = {};
  botSenseFrames_[playerIndex] = {};
  botSenseFrameValid_[playerIndex] = false;
  botRuntimeStats_.acquisitions[playerIndex] = 0;
  botRuntimeStats_.losses[playerIndex] = 0;
  botRuntimeStats_.attackCommandTicks[playerIndex] = 0;
  botRuntimeStats_.acceptedWeaponFires[playerIndex] = 0;
  for (std::size_t targetIndex = 0; targetIndex < kDuelPlayerCount; ++targetIndex) {
    botRuntimeStats_.acceptedDamageEvents[playerIndex][targetIndex] = 0;
    botRuntimeStats_.acceptedDamageEvents[targetIndex][playerIndex] = 0;
  }
  botRuntimeStats_.navigationCommandTicks[playerIndex] = 0;
  botRuntimeStats_.movementIntentTicks[playerIndex] = 0;
  botRuntimeStats_.recoveryEvents[playerIndex] = 0;
  botTargetObserved_[playerIndex] = false;
  botRecovering_[playerIndex] = false;
  botBrains_[playerIndex].reset(
    0xB07D0D6EU ^ static_cast<std::uint32_t>(playerIndex * 0x9e3779b9U)
  );
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
  MovementTuning normalizedMovementTuning = movementTuning;
  normalizedMovementTuning.maxAirSpeed = normalizedMovementTuning.maxGroundSpeed;
  const CollisionBounds previousNavigationBounds =
    botNavigationBounds(playerSizeScaleXY_, playerSizeScaleZ_);
  const CollisionBounds nextNavigationBounds =
    botNavigationBounds(playerSizeScaleXY, playerSizeScaleZ);
  const bool rebuildNavigation =
    !sameBotNavigationTuning(movementTuning_, normalizedMovementTuning) ||
    previousNavigationBounds.radius != nextNavigationBounds.radius ||
    previousNavigationBounds.halfHeight != nextNavigationBounds.halfHeight;
  movementTuning_ = normalizedMovementTuning;
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
  if (rebuildNavigation) {
    rebuildBotNavigation();
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
    botMotors_ = {};
    botSenseFrames_ = {};
    botSenseFrameValid_ = {};
    botTargetObserved_ = {};
    botRecovering_ = {};
    for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
      botBrains_[index].reset(
        0xB07D0D6EU ^ static_cast<std::uint32_t>(index * 0x9e3779b9U)
      );
    }
  }
  botAttackMode_ = mode;
  snapshot_.botAttackMode = botAttackMode_;
}

void ServerGame::setBotWeapon(Weapon weapon) {
  if (weapon > kLastWeapon) {
    return;
  }
  // Bots request this selection through normal weapon switching. They never
  // change selectedWeapons_ directly.
  botWeapon_ = weapon;
  botWeaponAuto_ = false;
  snapshot_.botWeapon = botWeapon_;
}

void ServerGame::setBotWeaponAuto() {
  botWeaponAuto_ = true;
  snapshot_.botWeapon = botWeapon_;
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

Weapon ServerGame::botWeapon() const {
  return botWeapon_;
}

bool ServerGame::botWeaponAuto() const {
  return botWeaponAuto_;
}

std::uint64_t ServerGame::botCommandIngressCount(std::size_t playerIndex) const {
  return playerIndex < kDuelPlayerCount ? botCommandIngressCounts_[playerIndex] : 0U;
}

std::uint64_t ServerGame::botDeterminismHash() const {
  std::uint64_t hash = 1469598103934665603ULL;
  const auto mix = [&](std::uint64_t value) {
    hash ^= value;
    hash *= 1099511628211ULL;
  };
  for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
    mix(botPlayers_[index]);
    if (botPlayers_[index]) {
      mix(botBrains_[index].deterministicHash());
      mix(botRuntimeStats_.acquisitions[index]);
      mix(botRuntimeStats_.losses[index]);
      mix(botRuntimeStats_.attackCommandTicks[index]);
      mix(botRuntimeStats_.acceptedWeaponFires[index]);
      for (std::size_t target = 0; target < kDuelPlayerCount; ++target) {
        mix(botRuntimeStats_.acceptedDamageEvents[index][target]);
      }
      mix(botRuntimeStats_.navigationCommandTicks[index]);
      mix(botRuntimeStats_.movementIntentTicks[index]);
      mix(botRuntimeStats_.recoveryEvents[index]);
      mix(botTargetObserved_[index]);
      mix(botRecovering_[index]);
    }
  }
  return hash;
}

std::uint32_t ServerGame::botNavigationBuildCount() const {
  return botNavigationBuildCount_;
}

std::uint64_t ServerGame::botHiddenAttackInvariantCount() const {
  return botHiddenAttackInvariantCount_;
}

const BotRuntimeStats& ServerGame::botRuntimeStats() const {
  return botRuntimeStats_;
}

std::string ServerGame::botDebugString(std::size_t playerIndex) const {
  if (playerIndex >= kDuelPlayerCount || !botPlayers_[playerIndex]) {
    return "bot_debug: slot is not a bot";
  }
  const BotMotor& motor = botMotors_[playerIndex];
  const BotTraits& traits = botBrains_[playerIndex].traits();
  const auto goalName = [](BotGoalKind goal) {
    switch (goal) {
    case BotGoalKind::Safe: return "safe";
    case BotGoalKind::Chase: return "chase";
    case BotGoalKind::RecoverHealth: return "health";
    case BotGoalKind::Objective: return "objective";
    case BotGoalKind::Explore: return "explore";
    }
    return "safe";
  };
  const auto noFireName = [](BotNoFireReason reason) {
    switch (reason) {
    case BotNoFireReason::None: return "ready";
    case BotNoFireReason::Disabled: return "disabled";
    case BotNoFireReason::Reaction: return "reaction";
    case BotNoFireReason::NoVisibleTarget: return "no-visible-target";
    case BotNoFireReason::Turning: return "turning";
    case BotNoFireReason::WeaponUnavailable: return "weapon-unavailable";
    }
    return "unknown";
  };
  const char* const difficulty = botAttackMode_ == BotAttackMode::Easy ? "easy" :
    botAttackMode_ == BotAttackMode::Medium ? "medium" :
    botAttackMode_ == BotAttackMode::Hard ? "hard" : "off";
  std::ostringstream output;
  output << "bot " << (playerIndex + 1U) << " difficulty="
    << difficulty << " target=";
  if (motor.targetPlayerIndex < kDuelPlayerCount) output << (motor.targetPlayerIndex + 1U);
  else output << "none";
  output << (motor.targetMemoryAgeSeconds == 0.0F ? " sensed=(" : " last=(")
    << motor.lastKnownTargetPosition.x << ','
    << motor.lastKnownTargetPosition.y << ',' << motor.lastKnownTargetPosition.z
    << ") age=" << motor.targetMemoryAgeSeconds
    << " goal=" << goalName(motor.goal)
    << " resources=" << motor.observedHealthResourceCount
    << " waypoint=" << (motor.waypointNode < botNavigation_.nodeCount
      ? std::to_string(motor.waypointNode) : "none")
    << " weapon=" << weaponShortName(motor.command.weapon)
    << " weapon_score=" << motor.selectedWeaponScore
    << " fire=" << noFireName(motor.noFireReason)
    << " attack=" << (motor.command.attack ? 1 : 0)
    << " recovery=" << (motor.recoveredFromStuck ? 1 : 0)
    << " traits=(agg=" << traits.aggression
    << ",risk=" << traits.risk
    << ",range=" << traits.preferredRangeBias
    << ",move=" << traits.movementCadenceBias
    << ",react=" << traits.reactionLatencyOffsetSeconds
    << ",aim=" << traits.aimBiasScale << ')';
  std::size_t topWeapon = 0U;
  for (std::size_t index = 1U; index < kWeaponCount; ++index) {
    if (motor.weaponScores[index].total > motor.weaponScores[topWeapon].total) {
      topWeapon = index;
    }
  }
  const BotWeaponScore& topScore = motor.weaponScores[topWeapon];
  if (std::isfinite(topScore.total)) {
    output << " top=" << weaponShortName(static_cast<Weapon>(topWeapon))
      << "(total=" << topScore.total
      << ",range=" << topScore.rangeFit
      << ",hit=" << topScore.hitChance
      << ",splash=" << topScore.splashValue
      << ",self=" << topScore.selfRisk
      << ",cooldown=" << topScore.cooldownPenalty
      << ",switch=" << topScore.switchCost << ')';
  }
  return output.str();
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
    // Reset only on entry. Repeating the reset every waiting tick would erase
    // warmup state and continuously respawn the remaining connected player.
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
      snapshot_.overtime = false;
      snapshot_.roundWinner = 255;
      snapshot_.matchWinner = 255;
      snapshot_.roundWinningTeam = Team::None;
      snapshot_.matchWinningTeam = Team::None;
      respawnRound();
    }
    snapshot_.matchPhase = MatchPhase::WaitingForPlayers;
    snapshot_.phaseTicksRemaining = 0;
    snapshot_.overtime = false;
    return;
  }

  switch (snapshot_.matchPhase) {
  case MatchPhase::WaitingForPlayers:
    snapshot_.matchPhase = MatchPhase::WaitingForReady;
    snapshot_.overtime = false;
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
      snapshot_.overtime = false;
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
    snapshot_.overtime = false;
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
  bool overtimeWinner = false;
  if (snapshot_.overtime) {
    const auto leader = uniqueScoreLeader(snapshot_.scores, occupiedPlayers());
    overtimeWinner = leader.has_value() && *leader == winnerIndex;
  }
  if (
    hasWonDuel(snapshot_.scores, winnerIndex, matchRules_.roundLimit) ||
    overtimeWinner
  ) {
    beginMatchEnd(winnerIndex);
  }
}

void ServerGame::beginRoundEnd(Team winnerTeam) {
  awardClanArenaRound(snapshot_.teamScores, winnerTeam);
  snapshot_.roundWinningTeam = winnerTeam;
  snapshot_.phaseTicksRemaining = matchRules_.roundEndTicks;
  snapshot_.matchPhase = MatchPhase::RoundEnd;
  bool overtimeWinner = false;
  if (snapshot_.overtime) {
    const auto leader = clanArenaScoreLeader(snapshot_.teamScores);
    overtimeWinner = leader.has_value() && *leader == winnerTeam;
  }
  if (
    hasWonClanArena(snapshot_.teamScores, winnerTeam, matchRules_.roundLimit) ||
    overtimeWinner
  ) {
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
    return railgunCooldownTicks_[playerIndex];
  case Weapon::Revolver:
    return revolverCooldownTicks_[playerIndex];
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
  // Keep one extra frame so a rewind at the maximum age still has a stable
  // boundary sample when commands reference the oldest permitted server tick.
  history_.push_back(HistoryFrame{snapshot_.serverTick, snapshot_.players});
  while (history_.size() > kMaxLagCompensationTicks + 1U) {
    history_.pop_front();
  }
}

const ServerGame::HistoryFrame& ServerGame::historyFrameForTick(
  std::uint32_t serverTick
) const {
  // Select the newest frame not newer than the requested tick. If the request
  // predates retained history, clamp to the oldest authoritative pose available.
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
  bool headshot,
  DamageContext context
) {
  if (attackerIndex >= kDuelPlayerCount || targetIndex >= kDuelPlayerCount) {
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
  // Clamp before recording feedback and statistics so every downstream system
  // observes actual health removed rather than the weapon's nominal damage.
  damageApplied = std::clamp(damageApplied, 0, target.health);
  if (damageApplied > 0 && attackerIndex < kDuelPlayerCount) {
    ++botRuntimeStats_.acceptedDamageEvents[attackerIndex][targetIndex];
  }
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
    // A sequenced ring retains several rapid hits in one snapshot window while
    // allowing clients to deduplicate events repeated for packet-loss tolerance.
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

  if (damageApplied > 0) {
    const std::uint32_t sequence =
      nextNonZeroSequence(damageTakenSequences_[targetIndex]);
    damageTakenSequences_[targetIndex] = sequence;
    const std::size_t eventSlot =
      static_cast<std::size_t>(sequence - 1U) % kDamageTakenEventWindow;
    DamageTakenEventRing& ring = snapshot_.damageTakenEvents[targetIndex];
    DamageTakenEvent& event = ring.events[eventSlot];
    event.sequence = sequence;
    event.direction256 = context.hasSourcePosition
      ? quantizeDamageBearing(target.position, context.sourcePosition)
      : 0U;
    event.presentationDamage = static_cast<std::uint8_t>(
      std::min(damageApplied, 255)
    );
    event.metadata = 0U;
    if (context.hasSourcePosition &&
        std::isfinite(context.sourcePosition.x) &&
        std::isfinite(context.sourcePosition.y) &&
        std::isfinite(context.sourcePosition.z) &&
        ((context.sourcePosition.x - target.position.x) *
           (context.sourcePosition.x - target.position.x) +
         (context.sourcePosition.y - target.position.y) *
           (context.sourcePosition.y - target.position.y)) > 0.00000001F) {
      event.metadata |= kDamageTakenDirectionValid;
    }
    if (attackerIndex == targetIndex) {
      event.metadata |= kDamageTakenSelfDamage;
    }
    event.metadata |= kDamageTakenAttackerValid;
    event.metadata |= static_cast<std::uint8_t>(attackerIndex << 4U);
    event.weapon = weapon;
    (void)setDamageTakenEventActive(ring, eventSlot);
  }

  if (
    attackerIndex != targetIndex &&
    attacker.health > 0 &&
    damageApplied > 0 &&
    vampirism_ > 0.0F
  ) {
    // Preserve fractional healing between hits so low damage and fractional
    // vampirism remain deterministic instead of losing value to per-hit rounding.
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
    recordReplayLethal(attackerIndex, targetIndex, weapon);
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
    // Warmup deaths respawn immediately, but the respawn reset must not erase
    // the frag event before clients have had a chance to present it.
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
  // Each player owns one fixed partition. A busy attacker can fill only their
  // own slots and cannot reject another player's shot.
  const std::size_t firstSlot = attackerIndex * kProjectileSlotsPerPlayer;
  const std::size_t endSlot = firstSlot + kProjectileSlotsPerPlayer;
  for (std::size_t slot = firstSlot; slot < endSlot; ++slot) {
    RocketProjectile& rocket = rockets_[slot];
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
    std::uint32_t& ownerSequence = projectileSequences_[attackerIndex];
    ++ownerSequence;
    if (ownerSequence == 0U) {
      ++ownerSequence;
    }
    rocket.sequence = ownerSequence;
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
    // Ignore the owner only until the projectile has fully left its spawn
    // hitbox; once armed, later self-intersection must behave like any other hit.
    rocket.ownerCollisionArmed = false;
    rocket.resting = false;

    WeaponFireResult& fire = snapshot_.weaponFires[attackerIndex];
    fire.fired = true;
    fire.weapon = weapon;
    fire.visualSeed = rocket.sequence;
    fire.start = rocket.position;
    fire.end = rocket.position + (direction * 1.2F);
    ProjectileUpdate& spawned =
      spawnedProjectileUpdates_[spawnedProjectileCount_++];
    spawned.kind = ProjectileUpdateKind::Spawn;
    spawned.slot = static_cast<std::uint16_t>(slot);
    spawned.sequence = rocket.sequence;
    spawned.weapon = rocket.weapon;
    spawned.position = rocket.position;
    spawned.velocity = rocket.velocity;
    spawned.radius = rocket.projectileRadius;
    spawned.ageTicks = rocket.ageTicks;
    spawned.resting = rocket.resting;
    recordWeaponAccuracy(snapshot_, attackerIndex, weapon, 1U, 0U);
    return true;
  }

  return false;
}

void ServerGame::simulateRockets(float fixedDt) {
  const auto cylinderDistance = [](Vec3 point, const PlayerState& player) {
    // Splash distance is measured to the finite player cylinder surface, not its
    // center, so player size and vertical overlap affect falloff consistently.
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
    // Direct-hit tuning is authored for default bounds and scales with runtime
    // player-size changes so hit registration follows the authoritative body.
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
                snapshot_.grenadeBounceAudioEvents[rocket.owner];
              bounce.active = true;
              ++grenadeBounceSequences_[projectileIndex];
              bounce.sequence = ++grenadeBounceEventSequences_[rocket.owner];
              if (bounce.sequence == 0U) {
                bounce.sequence = ++grenadeBounceEventSequences_[rocket.owner];
              }
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
              // Tighten the remaining trace distance after every hit so the final
              // target is the nearest along the swept segment; exact ties keep the first slot.
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
    ProjectileUpdate removed;
    removed.kind = ProjectileUpdateKind::Remove;
    removed.slot = static_cast<std::uint16_t>(projectileIndex);
    removed.sequence = rocket.sequence;
    removed.weapon = rocket.weapon;
    removed.position = explosionPosition;
    removed.velocity = rocket.velocity;
    removed.radius = rocket.projectileRadius;
    removed.ageTicks = rocket.ageTicks;
    removed.resting = rocket.resting;
    recentProjectileRemovals_.push_back({
      removed,
      snapshot_.serverTick + 1U,
      false,
      false,
    });
    RocketExplosionResult& explosion = snapshot_.rocketExplosions[rocket.owner];
    explosion.active = true;
    explosion.weapon = rocket.weapon;
    rocketExplosionSequences_[rocket.owner] =
      nextNonZeroSequence(rocketExplosionSequences_[rocket.owner]);
    explosion.sequence = rocketExplosionSequences_[rocket.owner];
    explosion.projectileSequence = rocket.sequence;
    explosion.position = explosionPosition;
    const Vec3 directImpactPosition = explosionPosition;
    const Vec3 splashExplosionPosition = explosion.position;
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
      // A direct hit already reached the body during the projectile sweep.
      // Splash-only damage needs at least one clear path around solid world.
      if (
        playerIndex != directTarget &&
        !splashCanReachPlayer(arena_, explosionPosition, player)
      ) {
        continue;
      }
      const float falloff =
        1.0F - (distance / std::max(0.001F, radius));
      // Ceil preserves one point of damage just inside the radius instead of
      // creating a zero-damage shell through integer truncation.
      int damage = static_cast<int>(std::ceil(
        static_cast<float>(splashDamage) * falloff
      ));
      if (playerIndex == directTarget) {
        damage = std::max(damage, directDamage);
      }
      const int knockbackDamage = damage;
      // Self-damage percentage affects health only. Knockback retains nominal
      // blast strength so movement techniques do not weaken with self-damage rules.
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
        // At the exact blast center there is no radial direction; projectile
        // travel provides a deterministic fallback instead of a zero impulse.
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
        false,
        playerIndex == directTarget
          ? DamageContext{true, directImpactPosition}
          : DamageContext{true, splashExplosionPosition}
      );
    }
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
      if (!playerTouchesHealthPickup(player.bounds, player.position, pickup)) {
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
  // Repeat short-lived presentation events across a small snapshot window.
  // Sequence numbers make repetition idempotent for clients that saw them once.
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
    for (std::size_t eventSlot = 0; eventSlot < kDamageTakenEventWindow; ++eventSlot) {
      if (
        damageTakenEventActive(recentDamageTakenEvents_[playerIndex], eventSlot) &&
        snapshot_.serverTick -
            recentDamageTakenEventTicks_[playerIndex][eventSlot] <=
          kDamageTakenEventRetentionTicks
      ) {
        snapshot_.damageTakenEvents[playerIndex].events[eventSlot] =
          recentDamageTakenEvents_[playerIndex].events[eventSlot];
        (void)setDamageTakenEventActive(
          snapshot_.damageTakenEvents[playerIndex],
          eventSlot
        );
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
    for (std::size_t eventSlot = 0; eventSlot < kDamageTakenEventWindow; ++eventSlot) {
      if (
        damageTakenEventActive(snapshot_.damageTakenEvents[playerIndex], eventSlot) &&
        snapshot_.damageTakenEvents[playerIndex].events[eventSlot].sequence !=
          recentDamageTakenEvents_[playerIndex].events[eventSlot].sequence
      ) {
        recentDamageTakenEvents_[playerIndex].events[eventSlot] =
          snapshot_.damageTakenEvents[playerIndex].events[eventSlot];
        (void)setDamageTakenEventActive(
          recentDamageTakenEvents_[playerIndex],
          eventSlot
        );
        recentDamageTakenEventTicks_[playerIndex][eventSlot] =
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

void ServerGame::rebuildBotNavigation() {
  const CollisionBounds bounds =
    botNavigationBounds(playerSizeScaleXY_, playerSizeScaleZ_);
  botNavigation_ = buildBotNavigationMap(arena_, movementTuning_, bounds);
  ++botNavigationBuildCount_;
}

BotSenseFrame ServerGame::buildBotSenseFrame(
  std::size_t playerIndex,
  float fixedDt
) const {
  BotSenseFrame sense;
  sense.serverTick = snapshot_.serverTick;
  sense.fixedDt = fixedDt;
  const PlayerState& self = snapshot_.players[playerIndex];
  sense.self.position = self.position;
  sense.self.velocity = self.velocity;
  sense.self.viewYawRadians = self.viewYawRadians;
  sense.self.viewPitchRadians = self.viewPitchRadians;
  sense.self.radius = self.bounds.radius;
  sense.self.halfHeight = self.bounds.halfHeight;
  sense.self.health = self.health;
  sense.self.onGround = self.onGround;
  sense.self.dashReady = self.dashCooldownTicksRemaining == 0U;
  sense.selectedWeapon = selectedWeapons_[playerIndex];
  sense.forceWeapon = !botWeaponAuto_;
  sense.forcedWeapon = botWeapon_;
  sense.combatEnabled = botAttackMode_ != BotAttackMode::Off &&
    isActiveCombatant(playerIndex);
  sense.standstill = botStandstillEnabled_;
  sense.dodgeOverride = botDodgeEnabled_;
  sense.dodgeMinIntervalMs = botDodgeMinIntervalMs_;
  sense.dodgeMaxIntervalMs = botDodgeMaxIntervalMs_;

  const auto setWeapon = [&](Weapon weapon, float range, float damage,
                             float intervalSeconds, float projectileSpeed,
                             float splashRadius, float splashDamage) {
    BotWeaponSense& output = sense.weapons[weaponIndex(weapon)];
    // A weapon remains a legal held input while its cooldown expires. The
    // shared fire path, not the bot, decides the exact fire tick.
    output.usable = hasAmmoForWeapon(playerIndex, weapon);
    output.infiniteAmmo = weaponAmmoConfig_.infiniteAmmo;
    output.effectiveRange = range;
    output.damagePerShot = damage;
    output.fireIntervalSeconds = std::max(0.025F, intervalSeconds);
    output.projectileSpeed = projectileSpeed;
    output.splashRadius = splashRadius;
    output.splashDamage = splashDamage;
    output.cooldownSeconds = static_cast<float>(weaponCooldownTicks(playerIndex, weapon)) *
      fixedDt;
    output.switchCostSeconds = weapon == selectedWeapons_[playerIndex] ? 0.0F :
      static_cast<float>(weaponPulloutDurationTicks_) * fixedDt;
  };
  setWeapon(Weapon::LightningGun, lightningGunTuning_.range,
    lightningGunTuning_.damagePerSecond / std::max(1.0F, lightningGunTuning_.fireHz),
    1.0F / std::max(1.0F, lightningGunTuning_.fireHz), 0.0F, 0.0F, 0.0F);
  setWeapon(Weapon::Railgun, railgunTuning_.range, static_cast<float>(railgunTuning_.damage),
    static_cast<float>(railgunCooldownDurationTicks_) * fixedDt, 0.0F, 0.0F, 0.0F);
  setWeapon(Weapon::RocketLauncher, 20.0F, static_cast<float>(rocketLauncherTuning_.directDamage),
    static_cast<float>(rocketLauncherCooldownDurationTicks_) * fixedDt, rocketLauncherTuning_.speed,
    rocketLauncherTuning_.radius, static_cast<float>(rocketLauncherTuning_.splashDamage));
  setWeapon(Weapon::MachineGun, machineGunTuning_.range, static_cast<float>(machineGunTuning_.damage),
    static_cast<float>(machineGunCooldownDurationTicks_) * fixedDt, 0.0F, 0.0F, 0.0F);
  setWeapon(Weapon::Shotgun, shotgunTuning_.range,
    static_cast<float>(shotgunTuning_.damagePerPellet * shotgunTuning_.pelletCount),
    static_cast<float>(shotgunCooldownDurationTicks_) * fixedDt, 0.0F, 0.0F, 0.0F);
  setWeapon(Weapon::GrenadeLauncher, 16.0F, static_cast<float>(grenadeLauncherTuning_.directDamage),
    static_cast<float>(grenadeLauncherTuning_.cooldownTicks) * fixedDt,
    grenadeLauncherTuning_.speed, grenadeLauncherTuning_.radius,
    static_cast<float>(grenadeLauncherTuning_.splashDamage));
  setWeapon(Weapon::PlasmaGun, 24.0F, static_cast<float>(plasmaGunTuning_.damage),
    static_cast<float>(plasmaGunTuning_.cooldownTicks) * fixedDt, plasmaGunTuning_.speed,
    0.0F, 0.0F);
  setWeapon(Weapon::FreezeGun, freezeGunTuning_.range,
    freezeGunTuning_.damagePerSecond / std::max(1.0F, freezeGunTuning_.fireHz),
    1.0F / std::max(1.0F, freezeGunTuning_.fireHz), 0.0F, 0.0F, 0.0F);
  setWeapon(Weapon::Revolver, revolverTuning_.range, static_cast<float>(revolverTuning_.damage),
    static_cast<float>(revolverCooldownDurationTicks_) * fixedDt, 0.0F, 0.0F, 0.0F);

  // This is the authoritative-to-filtered boundary. Dynamic facts enter the
  // brain only after both world LOS and the complete yaw/pitch view cone agree.
  // 108 degrees is the common physical yaw+pitch cone for easy, medium, and
  // hard. This is intentionally not a difficulty reward.
  const float halfFovRadians = botDifficultyProfile(botAttackMode_).targetFovDegrees *
    kPi / 360.0F;
  const float minimumViewDot = std::cos(halfFovRadians);
  const Vec3 viewStart = weaponMuzzlePosition(self, lightningGunTuning_.eyeHeight);
  const Vec3 forward = cameraForward(self.viewYawRadians, self.viewPitchRadians);
  const auto currentlyVisiblePoint = [&](Vec3 point) {
    const Vec3 delta = point - viewStart;
    const float distance = length(delta);
    if (distance <= 0.001F) return true;
    if (dot(delta / distance, forward) < minimumViewDot) return false;
    const WorldTrace trace = traceWorld(arena_, viewStart, delta / distance, distance);
    return trace.distance >= distance - 0.01F;
  };
  for (std::size_t targetIndex = 0; targetIndex < kDuelPlayerCount; ++targetIndex) {
    if (!isValidEnemyTarget(playerIndex, targetIndex) ||
        !hasLineOfSight(playerIndex, targetIndex) ||
        !currentlyVisiblePoint(botTargetAimPoint(snapshot_.players[targetIndex]))) {
      continue;
    }
    if (sense.visibleEnemyCount == sense.visibleEnemies.size()) break;
    BotObservedEnemy& observed = sense.visibleEnemies[sense.visibleEnemyCount++];
    observed.playerIndex = static_cast<std::uint8_t>(targetIndex);
    observed.position = snapshot_.players[targetIndex].position;
    observed.observationServerTick = snapshot_.serverTick;
    observed.onGround = snapshot_.players[targetIndex].onGround;
    const Vec3 targetPoint = botTargetAimPoint(snapshot_.players[targetIndex]);
    const Vec3 towardTarget = normalize(targetPoint - viewStart);
    // The trace sees only static world geometry just beyond a target already
    // inside LOS and FOV. It never asks for hidden entities or player state.
    observed.nearbySplashSurface = traceWorld(arena_, targetPoint, towardTarget, 2.0F).hit;
  }
  for (std::size_t index = 0; index < arena_.healthPickupCount; ++index) {
    const ArenaHealthPickup& pickup = arena_.healthPickups[index];
    // Availability is an authoritative dynamic fact. Do not put it into the
    // filtered frame until the pickup itself is in sight. The brain may keep
    // this seen state briefly as fallible memory after it leaves sight.
    if (!currentlyVisiblePoint(pickup.position)) continue;
    if (sense.healthResourceCount == sense.healthResources.size()) break;
    BotHealthResourceSense& resource = sense.healthResources[sense.healthResourceCount++];
    resource.resourceIndex = static_cast<std::uint8_t>(index);
    resource.position = pickup.position;
    resource.value = pickup.type == HealthPickupType::Large
      ? largeHealthPickupAmount_ : smallHealthPickupAmount_;
    resource.available = snapshot_.healthPickupAvailable[index];
  }
  if (snapshot_.gameMode == GameMode::McGuffin) {
    // McGuffinSnapshot is encoded in every human ServerSnapshot (state,
    // carrier, exact position, and base ownership). It is public HUD/objective
    // data, so this uses the replicated snapshot rather than hidden server
    // objective state. Pickups do not receive this exception.
    sense.objective.position = snapshot_.mcguffin.position;
    sense.objective.active = true;
    sense.objective.carrying = snapshot_.mcguffin.state == McGuffinState::Carried &&
      snapshot_.mcguffin.carrierIndex == playerIndex;
    if (sense.objective.carrying) {
      const Team team = snapshot_.teams[playerIndex];
      const auto baseCenter = [](const ArenaMcGuffinBase& base) {
        return (base.min + base.max) * 0.5F;
      };
      // Only supply an installation point this carrier may use under the
      // current public ownership rules. The decision code never receives the
      // other team's target or hidden carrier data.
      if (snapshot_.mcguffinRedBaseOwner == team && arena_.mcguffin.hasRedBase) {
        sense.objective.scoringPosition = baseCenter(arena_.mcguffin.redBase);
        sense.objective.hasScoringPosition = true;
      } else if (snapshot_.mcguffinBlueBaseOwner == team && arena_.mcguffin.hasBlueBase) {
        sense.objective.scoringPosition = baseCenter(arena_.mcguffin.blueBase);
        sense.objective.hasScoringPosition = true;
      } else if (snapshot_.mcguffinRound >= 2U &&
                 snapshot_.mcguffinRedBaseOwner == Team::None &&
                 snapshot_.mcguffinBlueBaseOwner == Team::None &&
                 arena_.mcguffin.hasRedBase && arena_.mcguffin.hasBlueBase) {
        // In a deciding round either public unclaimed base is legal. Pick one
        // from static geometry only; no enemy state enters this tie break.
        const Vec3 red = baseCenter(arena_.mcguffin.redBase);
        const Vec3 blue = baseCenter(arena_.mcguffin.blueBase);
        sense.objective.scoringPosition = length(red - self.position) <=
          length(blue - self.position) ? red : blue;
        sense.objective.hasScoringPosition = true;
      }
    }
  }
  return sense;
}

void ServerGame::updateBotCommands(float fixedDt) {
  const auto perceptionIntervalTicks = [&](BotAttackMode mode) {
    // Motor commands remain 125 Hz. LOS/FOV is deterministic, slot-phased,
    // and slow enough that 16 bots do not trace every opponent every tick.
    switch (mode) {
    case BotAttackMode::Easy: return 16U;
    case BotAttackMode::Medium: return 12U;
    case BotAttackMode::Hard: return 8U;
    case BotAttackMode::Off: return 16U;
    }
    return 16U;
  };
  const auto refreshMotorSense = [&](BotSenseFrame& sense, std::size_t playerIndex,
                                      bool perceptionFresh) {
    const PlayerState& self = snapshot_.players[playerIndex];
    sense.serverTick = snapshot_.serverTick;
    sense.fixedDt = fixedDt;
    sense.perceptionFresh = perceptionFresh;
    sense.self.position = self.position;
    sense.self.velocity = self.velocity;
    sense.self.viewYawRadians = self.viewYawRadians;
    sense.self.viewPitchRadians = self.viewPitchRadians;
    sense.self.radius = self.bounds.radius;
    sense.self.halfHeight = self.bounds.halfHeight;
    sense.self.health = self.health;
    sense.self.onGround = self.onGround;
    sense.self.dashReady = self.dashCooldownTicksRemaining == 0U;
    sense.selectedWeapon = selectedWeapons_[playerIndex];
    sense.forceWeapon = !botWeaponAuto_;
    sense.forcedWeapon = botWeapon_;
    sense.combatEnabled = botAttackMode_ != BotAttackMode::Off &&
      isActiveCombatant(playerIndex);
    sense.standstill = botStandstillEnabled_;
    sense.dodgeOverride = botDodgeEnabled_;
    sense.dodgeMinIntervalMs = botDodgeMinIntervalMs_;
    sense.dodgeMaxIntervalMs = botDodgeMaxIntervalMs_;
  };
  const auto currentTargetVisible = [&](std::size_t playerIndex,
                                        std::uint8_t targetIndex) {
    if (targetIndex >= kDuelPlayerCount ||
        !isValidEnemyTarget(playerIndex, targetIndex) ||
        !hasLineOfSight(playerIndex, targetIndex)) {
      return false;
    }
    const PlayerState& self = snapshot_.players[playerIndex];
    const Vec3 viewStart = weaponMuzzlePosition(self, lightningGunTuning_.eyeHeight);
    const Vec3 delta = botTargetAimPoint(snapshot_.players[targetIndex]) - viewStart;
    const float distance = length(delta);
    if (distance <= 0.001F) return true;
    // All difficulties share the same physical yaw-and-pitch cone. This
    // revalidation exposes only a yes/no result for a target already known to
    // the motor, never a fresh position or velocity sample.
    const float minimumDot = std::cos(
      botDifficultyProfile(botAttackMode_).targetFovDegrees * kPi / 360.0F
    );
    if (dot(delta / distance,
        cameraForward(self.viewYawRadians, self.viewPitchRadians)) < minimumDot) {
      return false;
    }
    const WorldTrace trace = traceWorld(arena_, viewStart, delta / distance, distance);
    return trace.distance >= distance - 0.01F;
  };
  const std::uint32_t perceptionInterval = perceptionIntervalTicks(botAttackMode_);
  for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
    if (!botPlayers_[playerIndex]) {
      continue;
    }
    const bool freshPerception = !botSenseFrameValid_[playerIndex] ||
      ((snapshot_.serverTick + static_cast<std::uint32_t>(playerIndex * 5U)) %
        perceptionInterval == 0U);
    if (freshPerception) {
      botSenseFrames_[playerIndex] = buildBotSenseFrame(playerIndex, fixedDt);
      botSenseFrameValid_[playerIndex] = true;
    }
    BotSenseFrame sense = botSenseFrames_[playerIndex];
    refreshMotorSense(sense, playerIndex, freshPerception);
    sense.attackTargetPlayerIndex = botMotors_[playerIndex].targetPlayerIndex;
    sense.attackTargetCurrentlyVisible = currentTargetVisible(playerIndex,
      sense.attackTargetPlayerIndex);
    botMotors_[playerIndex] = botBrains_[playerIndex].tick(
      sense, botDifficultyProfile(botAttackMode_), botNavigation_);
    UserCommand command = botMotors_[playerIndex].command;
    if (freshPerception) {
      bool targetObserved = false;
      for (std::size_t seen = 0; seen < sense.visibleEnemyCount; ++seen) {
        targetObserved = targetObserved ||
          sense.visibleEnemies[seen].playerIndex == botMotors_[playerIndex].targetPlayerIndex;
      }
      if (targetObserved && !botTargetObserved_[playerIndex]) {
        ++botRuntimeStats_.acquisitions[playerIndex];
      } else if (!targetObserved && botTargetObserved_[playerIndex]) {
        ++botRuntimeStats_.losses[playerIndex];
      }
      botTargetObserved_[playerIndex] = targetObserved;
    }
    if (botMotors_[playerIndex].waypointNode < botNavigation_.nodeCount) {
      ++botRuntimeStats_.navigationCommandTicks[playerIndex];
    }
    if (std::fabs(command.forwardMove) > 0.01F || std::fabs(command.rightMove) > 0.01F ||
        command.jump || command.dash) {
      ++botRuntimeStats_.movementIntentTicks[playerIndex];
    }
    if (botMotors_[playerIndex].recoveredFromStuck && !botRecovering_[playerIndex]) {
      ++botRuntimeStats_.recoveryEvents[playerIndex];
    }
    botRecovering_[playerIndex] = botMotors_[playerIndex].recoveredFromStuck;
    // Do not treat a cached visible list as permission to fire. Revalidate the
    // target selected this motor tick so walls and FOV loss cancel a held beam
    // on the next canonical command, while a stable beam stays held between
    // slower full perception frames.
    const bool attackTargetVisible = currentTargetVisible(playerIndex,
      botMotors_[playerIndex].targetPlayerIndex);
    if (command.attack && !attackTargetVisible) {
      ++botHiddenAttackInvariantCount_;
      command.attack = false;
    }
    // bot_stare is a legacy training override. It can face a sensed target,
    // but still cannot see through a wall or outside its FOV.
    if (botAttackMode_ == BotAttackMode::Off && botStareEnabled_ && sense.perceptionFresh &&
        sense.visibleEnemyCount > 0U) {
      const BotObservedEnemy& target = sense.visibleEnemies.front();
      const Vec3 delta = botTargetAimPoint(PlayerState{
        .position = target.position, .bounds = snapshot_.players[playerIndex].bounds
      }) - weaponMuzzlePosition(snapshot_.players[playerIndex], lightningGunTuning_.eyeHeight);
      command.viewYawRadians = std::atan2(delta.y, delta.x);
      command.viewPitchRadians = std::clamp(std::atan2(delta.z, std::hypot(delta.x, delta.y)),
        -kMaxPitchRadians, kMaxPitchRadians);
      command.attack = false;
    }
    if (!isActiveCombatant(playerIndex)) command.attack = false;
    if (command.attack) ++botRuntimeStats_.attackCommandTicks[playerIndex];
    ingestGameplayCommand(playerIndex, command, snapshot_.serverTick, false, true);
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
    case BotCommandType::Weapon:
      if (packet.botCommandValue == -1) {
        setBotWeaponAuto();
      } else {
        setBotWeapon(static_cast<Weapon>(packet.botCommandValue));
      }
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
  botMotors_[playerIndex] = {};
  botSenseFrames_[playerIndex] = {};
  botSenseFrameValid_[playerIndex] = false;
  botBrains_[playerIndex].reset(
    0xB07D0D6EU ^ static_cast<std::uint32_t>(playerIndex * 0x9e3779b9U)
  );
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
  botMotors_[playerIndex] = {};
  botSenseFrames_[playerIndex] = {};
  botSenseFrameValid_[playerIndex] = false;
  botBrains_[playerIndex].reset(
    0xB07D0D6EU ^ static_cast<std::uint32_t>(playerIndex * 0x9e3779b9U)
  );
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

bool ServerGame::applyScenarioSetup(
  const ScenarioSetup& setup,
  std::string* error
) {
  const auto reject = [error](std::string message) {
    if (error != nullptr) {
      *error = std::move(message);
    }
    return false;
  };

  if (!isValidGameMode(setup.match.gameMode)) {
    return reject("scenario game mode is invalid");
  }
  if (!validScenarioMatchPhase(setup.match.phase)) {
    return reject("scenario match phase is invalid");
  }
  if (
    (setup.match.roundWinner != 255 &&
      setup.match.roundWinner >= kDuelPlayerCount) ||
    (setup.match.matchWinner != 255 &&
      setup.match.matchWinner >= kDuelPlayerCount)
  ) {
    return reject("scenario winner slot is invalid");
  }
  if (
    !isValidTeam(setup.match.roundWinningTeam) ||
    !isValidTeam(setup.match.matchWinningTeam)
  ) {
    return reject("scenario winning team is invalid");
  }

  for (std::size_t index = 0; index < setup.players.size(); ++index) {
    const ScenarioPlayerSetup& player = setup.players[index];
    const bool occupied = player.connected || player.bot;
    if (player.connected && player.bot) {
      return reject(
        "scenario player " + std::to_string(index) +
        " cannot be both connected and a bot"
      );
    }
    if (!isValidTeam(player.team)) {
      return reject("scenario player team is invalid");
    }
    if (setup.match.gameMode == GameMode::Duel && player.team != Team::None) {
      return reject("duel scenario players cannot have a team");
    }
    if (!occupied && (player.alive || player.ready || player.team != Team::None)) {
      return reject(
        "scenario player " + std::to_string(index) +
        " has active state in an empty slot"
      );
    }
    if (occupied && player.alive != (player.health > 0)) {
      return reject(
        "scenario player " + std::to_string(index) +
        " has conflicting alive and health state"
      );
    }
    if (
      !finiteScenarioVector(player.position) ||
      !finiteScenarioVector(player.velocity) ||
      !std::isfinite(player.viewYawRadians) ||
      !std::isfinite(player.viewPitchRadians)
    ) {
      return reject("scenario player pose must contain finite values");
    }
    if (!validScenarioWeapon(player.selectedWeapon)) {
      return reject("scenario player weapon is invalid");
    }
    if (player.ammo.has_value()) {
      for (const std::int32_t ammo : *player.ammo) {
        if (ammo < 0) {
          return reject("scenario player ammo cannot be negative");
        }
      }
    }
  }

  if (error != nullptr) {
    error->clear();
  }

  // Scenario setup writes the server's source of truth, then updates each
  // snapshot mirror. It never sends a packet or changes protocol state.
  snapshot_.gameMode = setup.match.gameMode;
  snapshot_.connectedPlayers = {};
  snapshot_.teams = {};
  snapshot_.readyPlayers = {};
  botPlayers_ = {};
  for (std::size_t index = 0; index < setup.players.size(); ++index) {
    const ScenarioPlayerSetup& player = setup.players[index];
    snapshot_.connectedPlayers[index] = player.connected;
    botPlayers_[index] = player.bot;
    snapshot_.teams[index] = player.team;
    snapshot_.readyPlayers[index] = player.bot || player.ready;
  }
  resetMatch();

  snapshot_.serverTick = setup.serverTick;
  snapshot_.matchPhase = setup.match.phase;
  snapshot_.phaseTicksRemaining = setup.match.phaseTicksRemaining;
  snapshot_.liveTicksElapsed = setup.match.liveTicksElapsed;
  snapshot_.overtime = setup.match.overtime;
  snapshot_.scores = setup.match.scores;
  snapshot_.teamScores = setup.match.teamScores;
  snapshot_.mcguffinScores = setup.match.mcguffinScores;
  snapshot_.mcguffinRoundsWon = setup.match.mcguffinRoundsWon;
  snapshot_.mcguffinRound = setup.match.mcguffinRound;
  snapshot_.roundWinner = setup.match.roundWinner;
  snapshot_.matchWinner = setup.match.matchWinner;
  snapshot_.roundWinningTeam = setup.match.roundWinningTeam;
  snapshot_.matchWinningTeam = setup.match.matchWinningTeam;
  snapshot_.roundCombatStats = {};
  snapshot_.matchCombatStats = {};
  snapshot_.respawnTicksRemaining = {};
  snapshot_.playersColliding = false;

  for (std::size_t index = 0; index < setup.players.size(); ++index) {
    const ScenarioPlayerSetup& source = setup.players[index];
    PlayerState& player = snapshot_.players[index];
    player.position = source.position;
    player.velocity = source.velocity;
    player.viewYawRadians = source.viewYawRadians;
    player.viewPitchRadians = source.viewPitchRadians;
    player.health = source.alive ? source.health : 0;
    player.freezeLevel = 0.0F;
    player.bounds.radius = kDefaultPlayerBounds.radius * playerSizeScaleXY_;
    player.bounds.halfHeight =
      kDefaultPlayerBounds.halfHeight * playerSizeScaleZ_;
    player.movementMode = source.onGround
      ? MovementMode::Grounded
      : MovementMode::Airborne;
    player.knockbackTicksRemaining = 0;
    player.dashCooldownTicksRemaining = 0;
    player.dashActiveTicksRemaining = 0;
    player.dashDirection = {1.0F, 0.0F, 0.0F};
    player.jumpPadCooldownTicksRemaining = 0;
    player.onGround = source.onGround;
    player.jumpHeld = false;
    player.dashHeld = false;
    player.crouched = false;
    player.sneaking = false;

    selectedWeapons_[index] = source.selectedWeapon;
    snapshot_.selectedWeapons[index] = source.selectedWeapon;
    playerAmmo_[index] = source.ammo.value_or(weaponAmmoConfig_.spawnAmmo);
    snapshot_.playerAmmo[index] = playerAmmo_[index];
  }

  lightningGunStates_ = {};
  freezeGunStates_ = {};
  lightningAmmoCredit_.fill(1.0);
  freezeAmmoCredit_.fill(1.0);
  fractionalVampirismHealing_ = {};
  railgunCooldownTicks_ = {};
  revolverCooldownTicks_ = {};
  sniperAdsFractions_ = {};
  sniperChargeFractions_ = {};
  snapshot_.sniperChargePercent = {};
  machineGunCooldownTicks_ = {};
  shotgunCooldownTicks_ = {};
  rocketCooldownTicks_ = {};
  grenadeCooldownTicks_ = {};
  plasmaGunCooldownTicks_ = {};
  weaponPulloutTicks_ = {};

  clearProjectiles();
  snapshot_.icePools = {};
  resetHealthPickups();

  snapshot_.lightningGuns = {};
  snapshot_.weaponFires = {};
  snapshot_.rocketExplosions = {};
  snapshot_.footstepAudioEvents = {};
  snapshot_.grenadeBounceAudioEvents = {};
  snapshot_.fragEvents = {};
  snapshot_.localHitFeedbackEvents = {};
  snapshot_.damageTakenEvents = {};
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
  recentLocalHitFeedbackEvents_ = {};
  recentLocalHitFeedbackEventTicks_ = {};
  recentDamageTakenEvents_ = {};
  recentDamageTakenEventTicks_ = {};
  projectileSequences_ = {};
  rocketExplosionSequences_ = {};
  grenadeBounceSequences_ = {};
  grenadeBounceEventSequences_ = {};
  fragEventSequences_ = {};
  localHitFeedbackSequences_ = {};
  damageTakenSequences_ = {};
  footstepSequences_ = {};
  footstepStates_ = {};

  commands_ = {};
  viewedServerTicks_ = {};
  hasCommand_ = {};
  receivedCommandThisTick_ = {};
  lastActionEdges_ = {};
  jumpEdgeThisTick_ = {};
  dashEdgeThisTick_ = {};
  attackEdgeThisTick_ = {};
  attackEdgeCommands_ = {};
  attackEdgeViewedServerTicks_ = {};
  mcguffinThrowRequestedThisTick_ = {};
  mcguffinThrowCommands_ = {};
  snapshot_.acknowledgedCommand = {};
  snapshot_.hasAcknowledgedCommand = {};
  playerSessions_ = {};

  botDodgeDirections_ = {};
  botDodgeSwitchSeconds_ = {};
  botMotors_ = {};
  botRuntimeStats_ = {};
  botTargetObserved_ = {};
  botRecovering_ = {};
  // Separate non-zero streams keep bot choices and spawn choices stable even
  // when one system draws more random values than the other.
  botRandomState_ = scenarioRandomState(setup.seed, 1U);
  for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
    botBrains_[index].reset(scenarioRandomState(setup.seed, 20U + index));
  }
  spawnRandomState_ = scenarioRandomState(setup.seed, 2U);
  spawnLastUsedTicks_ = {};
  spawnWasUsed_ = {};
  nextDeathmatchSpawnIndex_ = arena_.spawnCount == 0U
    ? 0U
    : kDuelPlayerCount % arena_.spawnCount;

  // Objective state is durable even outside its mode, so start every scenario
  // from the same clean state before its first authoritative tick.
  resetMcGuffin(mcguffinObjective_, arena_.mcguffin.neutralSpawn);
  mcguffinStealTicks_ = {};
  mcguffinCarrySubPoints_ = 0;
  mcguffinCarriedPoints_ = 0;
  mcguffinFinalHoldTicks_ = 0;
  mcguffinRoundLiveTicks_ = 0;
  mcguffinThrowPickupLockoutTicks_ = 0;
  snapshot_.mcguffin = {};
  snapshot_.mcguffin.position = mcguffinObjective_.position;
  snapshot_.mcguffin.eventPlayerIndex = kNoMcGuffinCarrier;
  if (snapshot_.mcguffinRound == 1U) {
    snapshot_.mcguffinRedBaseOwner = Team::Blue;
    snapshot_.mcguffinBlueBaseOwner = Team::Red;
  } else if (snapshot_.mcguffinRound >= 2U) {
    snapshot_.mcguffinRedBaseOwner = Team::None;
    snapshot_.mcguffinBlueBaseOwner = Team::None;
  } else {
    snapshot_.mcguffinRedBaseOwner = Team::Red;
    snapshot_.mcguffinBlueBaseOwner = Team::Blue;
  }

  updateParticipatingPlayers();
  history_.clear();
  recordHistory();
  return true;
}

ScenarioState ServerGame::captureScenarioState() const {
  ScenarioState state;
  state.serverTick = snapshot_.serverTick;
  state.mapRevision = snapshot_.mapRevision;
  state.projectileRevision = projectileRevision_;
  state.mapName = snapshot_.map.mapName;
  state.mapContentHash = snapshot_.map.contentHash;
  for (std::size_t index = 0; index < state.players.size(); ++index) {
    ScenarioPlayerState& target = state.players[index];
    target.slot = static_cast<std::uint8_t>(index);
    target.connected = snapshot_.connectedPlayers[index];
    target.bot = botPlayers_[index];
    target.participating = snapshot_.participatingPlayers[index];
    target.ready = snapshot_.readyPlayers[index];
    target.team = snapshot_.teams[index];
    target.alive =
      target.participating && snapshot_.players[index].health > 0;
    target.player = snapshot_.players[index];
    target.weapon.selectedWeapon = selectedWeapons_[index];
    target.weapon.ammo = playerAmmo_[index];
    target.weapon.lightningGun = lightningGunStates_[index];
    target.weapon.freezeGun = freezeGunStates_[index];
    target.weapon.lightningAmmoCredit = lightningAmmoCredit_[index];
    target.weapon.freezeAmmoCredit = freezeAmmoCredit_[index];
    target.weapon.fractionalVampirismHealing =
      fractionalVampirismHealing_[index];
    target.weapon.railgunCooldownTicks = railgunCooldownTicks_[index];
    target.weapon.revolverCooldownTicks = revolverCooldownTicks_[index];
    target.weapon.sniperAdsFraction = sniperAdsFractions_[index];
    target.weapon.sniperChargeFraction = sniperChargeFractions_[index];
    target.weapon.machineGunCooldownTicks = machineGunCooldownTicks_[index];
    target.weapon.shotgunCooldownTicks = shotgunCooldownTicks_[index];
    target.weapon.rocketCooldownTicks = rocketCooldownTicks_[index];
    target.weapon.grenadeCooldownTicks = grenadeCooldownTicks_[index];
    target.weapon.plasmaGunCooldownTicks = plasmaGunCooldownTicks_[index];
    target.weapon.weaponPulloutTicks = weaponPulloutTicks_[index];
    target.respawnTicksRemaining =
      snapshot_.respawnTicksRemaining[index];
    target.command = commands_[index];
    target.consumedActionEdges = lastActionEdges_[index];
    target.viewedServerTick = viewedServerTicks_[index];
    target.hasCommand = hasCommand_[index];
    target.botState.dodgeDirection = botDodgeDirections_[index];
    target.botState.dodgeSwitchSeconds = botDodgeSwitchSeconds_[index];
    const BotMotor& motor = botMotors_[index];
    target.botState.targetPlayerIndex = motor.targetPlayerIndex;
    target.botState.desiredYawRadians = motor.command.viewYawRadians;
    target.botState.desiredPitchRadians = motor.command.viewPitchRadians;
    target.botState.initialized = motor.targetPlayerIndex < kDuelPlayerCount;
  }
  for (std::size_t index = 0; index < state.projectiles.size(); ++index) {
    const RocketProjectile& source = rockets_[index];
    ScenarioProjectileState& target = state.projectiles[index];
    target.slot = static_cast<std::uint16_t>(index);
    target.active = source.active;
    target.owner = source.owner;
    target.sequence = source.sequence;
    target.weapon = source.weapon;
    target.position = source.position;
    target.previousPosition = source.previousPosition;
    target.velocity = source.velocity;
    target.projectileRadius = source.projectileRadius;
    target.projectileHitboxRadius = source.projectileHitboxRadius;
    target.ownerCollisionArmed = source.ownerCollisionArmed;
    target.resting = source.resting;
    target.ageTicks = source.ageTicks;
  }

  state.match.gameMode = snapshot_.gameMode;
  state.match.phase = snapshot_.matchPhase;
  state.match.phaseTicksRemaining = snapshot_.phaseTicksRemaining;
  state.match.liveTicksElapsed = snapshot_.liveTicksElapsed;
  state.match.overtime = snapshot_.overtime;
  state.match.scores = snapshot_.scores;
  state.match.teamScores = snapshot_.teamScores;
  state.match.mcguffinScores = snapshot_.mcguffinScores;
  state.match.mcguffinRoundsWon = snapshot_.mcguffinRoundsWon;
  state.match.mcguffinRound = snapshot_.mcguffinRound;
  state.match.roundWinner = snapshot_.roundWinner;
  state.match.matchWinner = snapshot_.matchWinner;
  state.match.roundWinningTeam = snapshot_.roundWinningTeam;
  state.match.matchWinningTeam = snapshot_.matchWinningTeam;
  state.match.roundCombatStats = snapshot_.roundCombatStats;
  state.match.matchCombatStats = snapshot_.matchCombatStats;
  state.healthPickupAvailable = snapshot_.healthPickupAvailable;
  state.healthPickupCooldownTicks = healthPickupCooldownTicks_;
  state.icePools = snapshot_.icePools;
  state.mcguffin = mcguffinObjective_;
  state.mcguffinRedBaseOwner = snapshot_.mcguffinRedBaseOwner;
  state.mcguffinBlueBaseOwner = snapshot_.mcguffinBlueBaseOwner;
  state.mcguffinStealTicks = mcguffinStealTicks_;
  state.mcguffinCarrySubPoints = mcguffinCarrySubPoints_;
  state.mcguffinCarriedPoints = mcguffinCarriedPoints_;
  state.mcguffinFinalHoldTicks = mcguffinFinalHoldTicks_;
  state.mcguffinRoundLiveTicks = mcguffinRoundLiveTicks_;
  state.mcguffinThrowPickupLockoutTicks =
    mcguffinThrowPickupLockoutTicks_;
  state.botRandomState = botRandomState_;
  state.spawnRandomState = spawnRandomState_;
  state.projectileSequences = projectileSequences_;
  state.rocketExplosionSequences = rocketExplosionSequences_;
  state.fragEventSequences = fragEventSequences_;
  state.localHitFeedbackSequences = localHitFeedbackSequences_;
  state.footstepSequences = footstepSequences_;
  state.grenadeBounceEventSequences = grenadeBounceEventSequences_;
  state.grenadeBounceSequences = grenadeBounceSequences_;
  state.spawnLastUsedTicks = spawnLastUsedTicks_;
  state.spawnWasUsed = spawnWasUsed_;
  state.nextDeathmatchSpawnIndex = nextDeathmatchSpawnIndex_;
  state.playersColliding = snapshot_.playersColliding;
  state.history.reserve(history_.size());
  for (const HistoryFrame& frame : history_) {
    state.history.push_back({frame.serverTick, frame.players});
  }
  return state;
}

const ServerSnapshot& ServerGame::snapshot() const {
  return snapshot_;
}

const std::array<RocketProjectile, kMaxRocketProjectiles>&
ServerGame::projectiles() const {
  return rockets_;
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

void ServerGame::ingestGameplayCommand(
  std::size_t playerIndex,
  UserCommand command,
  std::uint32_t viewedServerTick,
  bool receivedFromNetwork,
  bool generatedByBot
) {
  if (playerIndex >= kDuelPlayerCount) return;
  // Human packets and local bot motors meet here before movement or combat.
  // This keeps weapon selection, held actions, and one-shot action edges on
  // one gameplay path; bot code has no direct simulation-state access.
  command.jump = command.jump || jumpEdgeThisTick_[playerIndex];
  command.dash = command.dash || dashEdgeThisTick_[playerIndex];
  commands_[playerIndex] = command;
  viewedServerTicks_[playerIndex] = viewedServerTick;
  hasCommand_[playerIndex] = true;
  receivedCommandThisTick_[playerIndex] =
    receivedCommandThisTick_[playerIndex] || receivedFromNetwork;
  if (generatedByBot) ++botCommandIngressCounts_[playerIndex];
}

void ServerGame::applyAttackEdges() {
  for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
    if (!attackEdgeThisTick_[playerIndex] || !hasCommand_[playerIndex]) continue;
    UserCommand command = commands_[playerIndex];
    command.attack = true;
    command.viewYawRadians = attackEdgeCommands_[playerIndex].viewYawRadians;
    command.viewPitchRadians = attackEdgeCommands_[playerIndex].viewPitchRadians;
    command.weapon = attackEdgeCommands_[playerIndex].weapon;
    ingestGameplayCommand(playerIndex, command, attackEdgeViewedServerTicks_[playerIndex],
      receivedCommandThisTick_[playerIndex], false);
  }
}

void ServerGame::receiveCommands() {
  const auto appendChatMessage = [this](
    std::uint8_t playerIndex,
    std::string speakerName,
    const std::string& message
  ) {
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
    std::uint32_t nextSequence = previousSequence + 1U;
    if (nextSequence == 0U) nextSequence = 1U;
    chatHistory_.messages[chatHistory_.messageCount++] = ChatMessage{
      nextSequence,
      playerIndex,
      std::move(speakerName),
      message,
    };
  };

  CommandPacket packet;
  while (transport_.receiveCommand(packet)) {
    if (packet.playerIndex >= kDuelPlayerCount) {
      if (
        packet.playerIndex == kNoAssignedPlayer &&
        packet.clientIndex < kMaxNetworkClients &&
        !packet.chatMessage.empty()
      ) {
        const std::size_t clientIndex = packet.clientIndex;
        if (chatClientNonces_[clientIndex] != packet.clientNonce) {
          chatClientNonces_[clientIndex] = packet.clientNonce;
          hasAcknowledgedChatCommand_[clientIndex] = false;
        }
        const bool isNewChat =
          !hasAcknowledgedChatCommand_[clientIndex] ||
          isSequenceNewer(
            packet.command.sequence,
            acknowledgedChatCommands_[clientIndex]
          );
        if (isNewChat) {
          appendChatMessage(
            kNoAssignedPlayer,
            packet.playerName.empty()
              ? "SPECTATOR " + std::to_string(clientIndex + 1U)
              : packet.playerName,
            packet.chatMessage
          );
          acknowledgedChatCommands_[clientIndex] = packet.command.sequence;
          hasAcknowledgedChatCommand_[clientIndex] = true;
        }
      }
      continue;
    }

    const std::size_t playerIndex = static_cast<std::size_t>(packet.playerIndex);
    const bool isNewCommand =
      !snapshot_.hasAcknowledgedCommand[playerIndex] ||
      isSequenceNewer(packet.command.sequence, snapshot_.acknowledgedCommand[playerIndex]);
    // UDP may duplicate or reorder packets. Only a wrap-safe newer sequence may
    // mutate authoritative input, tuning, roster, chat, or match state.
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
      packet.command.zoomed = packet.actionEdges.attackZoomed;
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
      // Preserve the name used when the message was sent; a later occupant of
      // the same player slot must not appear to have authored old chat.
      appendChatMessage(
        packet.playerIndex,
        snapshot_.playerNames[packet.playerIndex],
        packet.chatMessage
      );
      if (packet.clientIndex < kMaxNetworkClients) {
        const std::size_t clientIndex = packet.clientIndex;
        chatClientNonces_[clientIndex] = packet.clientNonce;
        acknowledgedChatCommands_[clientIndex] = packet.command.sequence;
        hasAcknowledgedChatCommand_[clientIndex] = true;
      }
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

    ingestGameplayCommand(
      playerIndex, packet.command, packet.viewedServerTick, true, false
    );
    snapshot_.hasAcknowledgedCommand[playerIndex] = true;
    // Acknowledgement is published only after every side effect in this packet
    // has been accepted, so client prediction may safely retire the command.
    snapshot_.acknowledgedCommand[playerIndex] = packet.command.sequence;
  }

  applyAttackEdges();

}

void ServerGame::publishSnapshot() {
  updateParticipatingPlayers();
  snapshot_.botStareEnabled = botStareEnabled_;
  snapshot_.botStandstillEnabled = botStandstillEnabled_;
  snapshot_.botDodgeEnabled = botDodgeEnabled_;
  snapshot_.botDodgeMinIntervalMs = botDodgeMinIntervalMs_;
  snapshot_.botDodgeMaxIntervalMs = botDodgeMaxIntervalMs_;
  snapshot_.botAttackMode = botAttackMode_;
  snapshot_.botWeapon = botWeapon_;
  snapshot_.projectileRevision = projectileRevision_;
  snapshot_.projectilePresentation.rocketLifetimeTicks =
    rocketLauncherTuning_.maxLifetimeTicks;
  snapshot_.projectilePresentation.grenadeFuseTicks =
    grenadeLauncherTuning_.fuseTicks;
  snapshot_.projectilePresentation.plasmaLifetimeTicks =
    plasmaGunTuning_.maxLifetimeTicks;
  snapshot_.projectilePresentation.grenadeGravity =
    grenadeLauncherTuning_.gravity;
  snapshot_.projectilePresentation.grenadeBounceDamping =
    grenadeLauncherTuning_.bounceDamping;
  snapshot_.projectilePresentation.grenadeRestSpeed =
    grenadeLauncherTuning_.restSpeed;
  transport_.publishChatHistory(chatHistory_);
  transport_.sendSnapshot(snapshot_);
  publishProjectileUpdates();
}

void ServerGame::clearProjectiles() {
  rockets_ = {};
  spawnedProjectileCount_ = 0;
  recentProjectileRemovals_.clear();
  projectileCorrectionCursor_ = 0;
  ++projectileRevision_;
  if (projectileRevision_ == 0U) {
    projectileRevision_ = 1U;
  }
}

void ServerGame::publishProjectileUpdates() {
  while (
    !recentProjectileRemovals_.empty() &&
    snapshot_.serverTick - recentProjectileRemovals_.front().serverTick >
      kTransientCombatEventTicks
  ) {
    recentProjectileRemovals_.pop_front();
  }

  ProjectileUpdatePacket packet;
  packet.serverTick = snapshot_.serverTick;
  packet.mapRevision = snapshot_.mapRevision;
  packet.projectileRevision = projectileRevision_;
  std::array<bool, kMaxRocketProjectiles> packetSlots = {};
  std::array<bool, kMaxRocketProjectiles> eventSlots = {};

  const auto sendPacket = [&]() {
    if (packet.updateCount == 0U) {
      return;
    }
    transport_.sendProjectileUpdates(packet);
    packet.updateCount = 0;
    packet.updates = {};
    packetSlots = {};
  };
  const auto appendPriority = [&](const ProjectileUpdate& update) {
    if (
      packet.updateCount >= kMaxProjectileUpdatesPerPacket ||
      packetSlots[update.slot]
    ) {
      sendPacket();
    }
    packet.updates[packet.updateCount++] = update;
    packetSlots[update.slot] = true;
    eventSlots[update.slot] = true;
    return true;
  };
  const auto appendCorrection = [&](const ProjectileUpdate& update) {
    if (
      packet.updateCount >= kMaxProjectileUpdatesPerPacket ||
      packetSlots[update.slot]
    ) {
      return false;
    }
    packet.updates[packet.updateCount++] = update;
    packetSlots[update.slot] = true;
    return true;
  };
  const auto correctionForSlot = [this](std::size_t slot) {
    const RocketProjectile& source = rockets_[slot];
    ProjectileUpdate target;
    target.kind = ProjectileUpdateKind::Correct;
    target.slot = static_cast<std::uint16_t>(slot);
    target.sequence = source.sequence;
    target.weapon = source.weapon;
    target.position = source.position;
    target.velocity = source.velocity;
    target.radius = source.projectileRadius;
    target.ageTicks = source.ageTicks;
    target.resting = source.resting;
    return target;
  };

  for (std::size_t index = 0; index < spawnedProjectileCount_; ++index) {
    const ProjectileUpdate& update = spawnedProjectileUpdates_[index];
    const bool removedThisTick = std::any_of(
      recentProjectileRemovals_.begin(),
      recentProjectileRemovals_.end(),
      [&](const RecentProjectileRemoval& removal) {
        return
          removal.serverTick == snapshot_.serverTick &&
          removal.update.slot == update.slot &&
          removal.update.sequence == update.sequence;
      }
    );
    if (!removedThisTick) {
      appendPriority(update);
    }
  }

  // New terminal records must all go out once. Flush bounded packets as needed
  // rather than letting an early burst hide later removals until they expire.
  for (RecentProjectileRemoval& removal : recentProjectileRemovals_) {
    if (!removal.sentOnce) {
      appendPriority(removal.update);
      removal.sentOnce = true;
    }
  }

  // Give every terminal record a second send before its retention deadline.
  // Oldest records go first. As the deadline nears, the budget grows and may
  // flush more bounded packets rather than dropping late records unseen.
  std::size_t unreplayedCount = 0;
  std::uint32_t minimumTicksRemaining = kTransientCombatEventTicks;
  for (const RecentProjectileRemoval& removal : recentProjectileRemovals_) {
    if (
      removal.replayedOnce ||
      removal.serverTick >= snapshot_.serverTick
    ) {
      continue;
    }
    ++unreplayedCount;
    const std::uint32_t age = snapshot_.serverTick - removal.serverTick;
    minimumTicksRemaining = std::min(
      minimumTicksRemaining,
      kTransientCombatEventTicks - age + 1U
    );
  }
  const std::size_t removalReplayBudget = unreplayedCount == 0U
    ? 0U
    : (unreplayedCount + minimumTicksRemaining - 1U) /
      minimumTicksRemaining;
  std::size_t removalsReplayed = 0;
  for (RecentProjectileRemoval& removal : recentProjectileRemovals_) {
    if (
      removalsReplayed >= removalReplayBudget ||
      removal.replayedOnce ||
      removal.serverTick >= snapshot_.serverTick
    ) {
      continue;
    }
    appendPriority(removal.update);
    removal.replayedOnce = true;
    ++removalsReplayed;
  }

  const std::size_t activeCount = static_cast<std::size_t>(std::count_if(
    rockets_.begin(),
    rockets_.end(),
    [](const RocketProjectile& projectile) { return projectile.active; }
  ));
  const std::size_t correctionBudget =
    activeCount == 0U ? 0U : (activeCount + 23U) / 24U;
  std::size_t correctionsAdded = 0;
  if (correctionBudget > 0U &&
      packet.updateCount < kMaxProjectileUpdatesPerPacket) {
    const std::size_t correctionStart = projectileCorrectionCursor_;
    do {
      const std::size_t slot = projectileCorrectionCursor_;
      projectileCorrectionCursor_ =
        (projectileCorrectionCursor_ + 1U) % rockets_.size();
      if (rockets_[slot].active && !eventSlots[slot]) {
        if (!appendCorrection(correctionForSlot(slot))) {
          break;
        }
        ++correctionsAdded;
      }
    } while (
      correctionsAdded < correctionBudget &&
      packet.updateCount < kMaxProjectileUpdatesPerPacket &&
      projectileCorrectionCursor_ != correctionStart
    );
  }

  sendPacket();
}

} // namespace lg
