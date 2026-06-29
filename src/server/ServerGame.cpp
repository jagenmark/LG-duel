#include "server/ServerGame.hpp"

#include "shared/Sequence.hpp"
#include "sim/ClanArenaRules.hpp"
#include "sim/Collision.hpp"
#include "sim/DuelRules.hpp"
#include "sim/GameplayConfig.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace lg {
namespace {

constexpr std::uint32_t kMaxLagCompensationTicks = 25;
constexpr std::uint32_t kMachineGunCooldownTicks = 13;
constexpr std::uint32_t kRailgunCooldownTicks = 188;
constexpr std::uint32_t kShotgunCooldownTicks = 125;
constexpr std::uint32_t kRocketLauncherCooldownTicks = 100;
constexpr std::uint32_t kTransientCombatEventTicks = 8;
constexpr std::uint32_t kLocalHitFeedbackEventRetentionTicks = 32;
constexpr std::uint32_t kWeaponPulloutTicks = 20;
constexpr CollisionBounds kDefaultPlayerBounds = {};
constexpr float kQ3KnockbackToInternalScale = 22.0F / 1000.0F;
constexpr float kLightningKnockbackUsefulMinimum = 682.0F;
constexpr float kProjectileCollisionEpsilon = 0.0001F;

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
  return snapshot.connectedPlayers[playerIndex] ||
    (
      snapshot.gameMode == GameMode::Duel &&
      snapshot.participatingPlayers[playerIndex] &&
      snapshot.players[playerIndex].health > 0
    );
}

[[nodiscard]] std::size_t firstCombatTarget(
  const ServerSnapshot& snapshot,
  std::size_t attackerIndex
) {
  for (std::size_t targetIndex = 0; targetIndex < kDuelPlayerCount; ++targetIndex) {
    if (
      areDuelOpponents(attackerIndex, targetIndex) &&
      isCombatant(snapshot, targetIndex) &&
      snapshot.players[targetIndex].health > 0
    ) {
      return targetIndex;
    }
  }
  return kDuelPlayerCount;
}

[[nodiscard]] float q3KnockbackToInternal(float knockback) {
  return knockback * kQ3KnockbackToInternalScale;
}

[[nodiscard]] float lightningKnockbackToInternal(float knockback) {
  const float remappedKnockback =
    kLightningKnockbackUsefulMinimum +
    (std::max(0.0F, knockback) *
      ((1000.0F - kLightningKnockbackUsefulMinimum) / 1000.0F));
  return q3KnockbackToInternal(remappedKnockback);
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

  return bestNormal;
}

[[nodiscard]] std::filesystem::path defaultGameplayConfigPath() {
  namespace fs = std::filesystem;
  fs::path directory = fs::current_path();
  for (;;) {
    const fs::path candidate = directory / "config" / "gameplay.cfg";
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

} // namespace

ServerGame::ServerGame(NetTransport& transport) : transport_(transport) {
  rocketLauncherTuning_.knockback = q3KnockbackToInternal(rocketKnockback_);
  const std::filesystem::path gameplayConfigPath = defaultGameplayConfigPath();
  if (!gameplayConfigPath.empty()) {
    const GameplayConfigLoadResult loaded =
      loadGameplayConfigFromFile(gameplayConfigPath.string());
    if (loaded.ok) {
      grenadeLauncherTuning_ = loaded.config.grenadeLauncher;
    } else {
      std::cerr << "Ignoring gameplay config: " << loaded.error << '\n';
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

void ServerGame::tick(float fixedDt) {
  receivedCommandThisTick_.fill(false);
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

  for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
    PlayerState& player = snapshot_.players[playerIndex];
    UserCommand command =
      commandForPlayer(snapshot_, commands_, hasCommand_, playerIndex);
    if (!hasCommand_[playerIndex]) {
      command.weapon = selectedWeapons_[playerIndex];
    }
    updateSelectedWeapon(playerIndex, command.weapon);
    if (player.health <= 0) {
      player.velocity = {};
      player.jumpHeld = false;
      player.viewYawRadians = command.viewYawRadians;
      player.viewPitchRadians = command.viewPitchRadians;
      continue;
    }

    simulateMovement(
      snapshot_.players[playerIndex],
      command,
      arena_,
      movementTuning_,
      fixedDt
    );
  }

  snapshot_.playersColliding = false;
  for (std::size_t firstIndex = 0; firstIndex < kDuelPlayerCount; ++firstIndex) {
    if (
      !snapshot_.connectedPlayers[firstIndex] ||
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
        !snapshot_.connectedPlayers[secondIndex] ||
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
  updateFootstepAudioEvents();

  const std::array<PlayerState, kDuelPlayerCount> combatPlayers = snapshot_.players;
  std::array<std::size_t, kDuelPlayerCount> lightningTargets = {};
  std::array<std::size_t, kDuelPlayerCount> weaponTargets = {};
  lightningTargets.fill(kDuelPlayerCount);
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
      snapshot_.connectedPlayers[attackerIndex] &&
      combatPlayers[attackerIndex].health > 0 &&
      (warmupCombat || hasTarget) &&
      canFireSelectedWeapon(attackerIndex);
    if (command.planarAim) {
      command.viewPitchRadians = 0.0F;
    }
    if (command.weapon != Weapon::LightningGun) {
      lightningGunStates_[attackerIndex] = {};
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
        !areDuelOpponents(attackerIndex, candidateIndex) ||
        !isCombatant(snapshot_, candidateIndex) ||
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
    if (command.weapon == Weapon::LightningGun) {
      if (targetIndex < kDuelPlayerCount) {
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
      }
    } else if (
      command.weapon == Weapon::Railgun &&
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
          railgunTuning_
        );
      } else {
        WeaponFireResult& fire = snapshot_.weaponFires[attackerIndex];
        fire.weapon = Weapon::Railgun;
        fire.visualSeed = command.sequence;
        fire.start = attackStart;
        fire.end = worldTrace.end;
        fire.fired = command.attack && combatPlayers[attackerIndex].health > 0;
      }
      railgunCooldownTicks_[attackerIndex] = kRailgunCooldownTicks;
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
      machineGunCooldownTicks_[attackerIndex] = kMachineGunCooldownTicks;
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
      shotgunCooldownTicks_[attackerIndex] = kShotgunCooldownTicks;
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
        rocketCooldownTicks_[attackerIndex] = kRocketLauncherCooldownTicks;
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
    if (snapshot_.matchPhase == MatchPhase::Live) {
      RoundCombatStats& stats = snapshot_.roundCombatStats[attackerIndex];
      if (result.active) {
        ++stats.lightningActiveTicks;
        ++snapshot_.matchCombatStats[attackerIndex].lightningActiveTicks;
      }
      if (result.hit) {
        ++stats.lightningHitTicks;
        ++snapshot_.matchCombatStats[attackerIndex].lightningHitTicks;
      }
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
    applyDamageAndKnockback(
      attackerIndex,
      lightningTargets[attackerIndex],
      snapshot_.lightningGuns[attackerIndex].damageApplied,
      snapshot_.lightningGuns[attackerIndex].knockbackImpulse,
      Weapon::LightningGun
    );
    applyDamageAndKnockback(
      attackerIndex,
      weaponTargets[attackerIndex],
      snapshot_.weaponFires[attackerIndex].damageApplied,
      snapshot_.weaponFires[attackerIndex].knockbackImpulse,
      snapshot_.weaponFires[attackerIndex].weapon
    );
  }

  simulateRockets(fixedDt);

  if (snapshot_.matchPhase == MatchPhase::Live) {
    ++snapshot_.liveTicksElapsed;
    if (matchRules_.timeLimitMinutes > 0) {
      const std::uint32_t limitTicks =
        static_cast<std::uint32_t>(matchRules_.timeLimitMinutes) * 60U * 125U;
      if (snapshot_.liveTicksElapsed >= limitTicks) {
        if (snapshot_.gameMode == GameMode::Duel) {
          const auto leader = duelScoreLeader(
            snapshot_.scores,
            snapshot_.connectedPlayers
          );
          if (leader.has_value()) {
            beginMatchEnd(*leader);
          }
        } else {
          const auto leader = clanArenaScoreLeader(snapshot_.teamScores);
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
  const GameMode gameMode = snapshot_.gameMode;
  const auto teams = snapshot_.teams;
  snapshot_ = {};
  snapshot_.serverTick = serverTick;
  snapshot_.mapRevision = mapRevision_;
  snapshot_.arena = arena_;
  snapshot_.hasArena = true;
  snapshot_.connectedPlayers = connectedPlayers;
  snapshot_.gameMode = gameMode;
  snapshot_.teams = teams;
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
  snapshot_.weaponDamage = weaponDamage_;
  snapshot_.vampirism = vampirism_;
  snapshot_.selfDamagePercent = selfDamagePercent_;
  snapshot_.healthAmount = healthAmount_;
  snapshot_.botDodgeEnabled = botDodgeEnabled_;
  snapshot_.botDodgeMinIntervalMs = botDodgeMinIntervalMs_;
  snapshot_.botDodgeMaxIntervalMs = botDodgeMaxIntervalMs_;
  snapshot_.weaponSwitchingMode = weaponSwitchingMode_;
  snapshot_.roundWinner = 255;
  snapshot_.matchWinner = 255;
  snapshot_.roundWinningTeam = Team::None;
  snapshot_.matchWinningTeam = Team::None;
  snapshot_.playerNames = playerNames;
  snapshot_.matchPhase = enoughPlayersConnected()
    ? MatchPhase::WaitingForReady
    : MatchPhase::WaitingForPlayers;
  lightningGunStates_ = {};
  railgunCooldownTicks_ = {};
  machineGunCooldownTicks_ = {};
  shotgunCooldownTicks_ = {};
  rocketCooldownTicks_ = {};
  grenadeCooldownTicks_ = {};
  plasmaGunCooldownTicks_ = {};
  selectedWeapons_ = {};
  selectedWeapons_.fill(Weapon::LightningGun);
  weaponPulloutTicks_ = {};
  snapshot_.selectedWeapons = selectedWeapons_;
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
  grenadeBounceSequences_ = {};
  fractionalVampirismHealing_ = {};
  commands_ = {};
  viewedServerTicks_ = {};
  hasCommand_ = {};
  receivedCommandThisTick_ = {};
  playerSessions_ = {};
  updateParticipatingPlayers();
  history_.clear();
  recordHistory();
}

void ServerGame::setArena(const Arena& arena) {
  arena_ = arena;
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
  snapshot_.players[playerIndex] = spawnPlayer(arena_, playerIndex, healthAmount_);
  snapshot_.players[playerIndex].bounds.radius =
    kDefaultPlayerBounds.radius * playerSizeScaleXY_;
  snapshot_.players[playerIndex].bounds.halfHeight =
    kDefaultPlayerBounds.halfHeight * playerSizeScaleZ_;
  snapshot_.players[playerIndex].position.z =
    arena_.spawnPositions[playerIndex].z +
    snapshot_.players[playerIndex].bounds.halfHeight;
  snapshot_.lightningGuns[playerIndex] = {};
  snapshot_.weaponFires[playerIndex] = {};
  snapshot_.rocketExplosions[playerIndex] = {};
  snapshot_.fragEvents[playerIndex] = {};
  snapshot_.footstepAudioEvents[playerIndex] = {};
  lightningGunStates_[playerIndex] = {};
  railgunCooldownTicks_[playerIndex] = 0;
  machineGunCooldownTicks_[playerIndex] = 0;
  shotgunCooldownTicks_[playerIndex] = 0;
  rocketCooldownTicks_[playerIndex] = 0;
  grenadeCooldownTicks_[playerIndex] = 0;
  plasmaGunCooldownTicks_[playerIndex] = 0;
  selectedWeapons_[playerIndex] = Weapon::LightningGun;
  weaponPulloutTicks_[playerIndex] = 0;
  snapshot_.selectedWeapons[playerIndex] = selectedWeapons_[playerIndex];
  recentFootstepAudioEvents_[playerIndex] = {};
  recentFootstepAudioEventTicks_[playerIndex] = 0;
  recentFragEvents_[playerIndex] = {};
  recentFragEventTicks_[playerIndex] = 0;
  footstepStates_[playerIndex] = {};
  footstepSequences_[playerIndex] = 0;
  fractionalVampirismHealing_[playerIndex] = 0.0;
}

void ServerGame::respawnRound() {
  for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
    respawnPlayer(playerIndex);
  }
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

  const bool abortActiveMatch = !warmupPhase();
  for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
    if (!connectedPlayers[index]) {
      snapshot_.readyPlayers[index] = false;
      snapshot_.teams[index] = Team::None;
      snapshot_.playerNames[index] = "BOT";
      resetPlayerInputState(index);
      playerSessions_[index] = 0;
      botDodgeSwitchSeconds_[index] = 0.0F;
    } else if (!snapshot_.connectedPlayers[index]) {
      snapshot_.readyPlayers[index] = false;
      snapshot_.teams[index] = Team::None;
      snapshot_.playerNames[index] = "PLAYER " + std::to_string(index + 1U);
      resetPlayerInputState(index);
      botDodgeSwitchSeconds_[index] = 0.0F;
    }
  }
  snapshot_.connectedPlayers = connectedPlayers;
  updateParticipatingPlayers();

  if (abortActiveMatch) {
    resetMatch();
    return;
  }

  if (!enoughPlayersConnected()) {
    snapshot_.readyPlayers = {};
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
  snapshot_.acknowledgedCommand[playerIndex] = 0;
  snapshot_.hasAcknowledgedCommand[playerIndex] = false;
  lightningGunStates_[playerIndex] = {};
  selectedWeapons_[playerIndex] = Weapon::LightningGun;
  weaponPulloutTicks_[playerIndex] = 0;
  snapshot_.selectedWeapons[playerIndex] = selectedWeapons_[playerIndex];
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
  updateParticipatingPlayers();
}

WeaponSwitchingMode ServerGame::weaponSwitchingMode() const {
  return weaponSwitchingMode_;
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

const MatchRules& ServerGame::matchRules() const {
  return matchRules_;
}

void ServerGame::updateMatchState() {
  if (!enoughPlayersConnected()) {
    if (snapshot_.matchPhase != MatchPhase::WaitingForPlayers) {
      snapshot_.readyPlayers = {};
      snapshot_.scores = {};
      snapshot_.teamScores = {};
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
      respawnRound();
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
  return snapshot_.gameMode == GameMode::Duel
    ? hasRequiredDuelPlayers(snapshot_.connectedPlayers)
    : hasRequiredClanArenaPlayers(snapshot_.connectedPlayers);
}

bool ServerGame::allConnectedPlayersReady() const {
  return snapshot_.gameMode == GameMode::Duel
    ? canStartDuel(snapshot_.connectedPlayers, snapshot_.readyPlayers)
    : canStartClanArena(
        snapshot_.connectedPlayers,
        snapshot_.readyPlayers,
        snapshot_.teams
      );
}

bool ServerGame::warmupPhase() const {
  return snapshot_.matchPhase == MatchPhase::WaitingForPlayers ||
    snapshot_.matchPhase == MatchPhase::WaitingForReady;
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
  case Weapon::MachineGun:
    return machineGunCooldownTicks_[playerIndex];
  case Weapon::Shotgun:
    return shotgunCooldownTicks_[playerIndex];
  case Weapon::RocketLauncher:
    return rocketCooldownTicks_[playerIndex];
  case Weapon::GrenadeLauncher:
    return grenadeCooldownTicks_[playerIndex];
  case Weapon::LightningGun:
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
  return weaponSwitchingMode_ != WeaponSwitchingMode::Ql ||
    weaponPulloutTicks_[playerIndex] == 0;
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
    weaponPulloutTicks_[playerIndex] = kWeaponPulloutTicks;
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
  Weapon weapon
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
    snapshot_.roundCombatStats[attackerIndex].damageDealt +=
      static_cast<std::uint32_t>(damageApplied);
    snapshot_.matchCombatStats[attackerIndex].damageDealt +=
      static_cast<std::uint32_t>(damageApplied);
  }

  if (
    wasAlive &&
    target.health == 0 &&
    attackerIndex != targetIndex &&
    damageApplied > 0 &&
    damageAllowed(attackerIndex, targetIndex)
  ) {
    FragEvent& frag = snapshot_.fragEvents[attackerIndex];
    frag.active = true;
    frag.targetPlayerIndex = static_cast<std::uint8_t>(targetIndex);
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
    target.velocity = {};
    lightningGunStates_[targetIndex] = {};
    snapshot_.lightningGuns[targetIndex] = {};
    snapshot_.weaponFires[targetIndex] = {};
    if (snapshot_.gameMode == GameMode::Duel) {
      const auto winner = duelRoundWinner(
        snapshot_.connectedPlayers,
        targetIndex
      );
      if (winner.has_value()) {
        beginRoundEnd(*winner);
      }
    } else {
      std::array<bool, kDuelPlayerCount> alivePlayers = {};
      for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
        alivePlayers[index] =
          snapshot_.connectedPlayers[index] &&
          snapshot_.players[index].health > 0;
      }
      const auto winner = clanArenaRoundWinner(
        snapshot_.connectedPlayers,
        snapshot_.teams,
        alivePlayers
      );
      if (winner.has_value()) {
        beginRoundEnd(*winner);
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
    respawnPlayer(targetIndex);
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
    fire.start = rocket.position;
    fire.end = rocket.position + (direction * 1.2F);
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

      if (
        !rocket.ownerCollisionArmed &&
        cylinderDistance(rocket.position, snapshot_.players[rocket.owner]) >
          rocket.projectileHitboxRadius + 0.0001F
      ) {
        rocket.ownerCollisionArmed = true;
      }

      explosionPosition = nextPosition;
      if (segmentLength > 0.0F) {
        const WorldTrace worldTrace =
          traceWorld(arena_, rocket.position, direction, segmentLength);
        if (worldTrace.distance < segmentLength - 0.0001F) {
          explosionPosition = worldTrace.end;
          if (grenade) {
            const Vec3 normal = bounceNormalForPoint(arena_, explosionPosition);
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
          !grenade || rocket.projectileHitboxRadius > 0.0F;
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
            PlayerState projectileTarget = snapshot_.players[playerIndex];
            projectileTarget.bounds.radius += rocket.projectileHitboxRadius;
            projectileTarget.bounds.halfHeight += rocket.projectileHitboxRadius;
            if (
              tracePlayerCylinder(
                rocket.position,
                direction,
                projectileTarget,
                bestHitDistance,
                hitDistance
              )
            ) {
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
        rocket.weapon
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
    } else if (movingOnGround) {
      state.distanceSinceStep += horizontalDistance;
      const float strideDistance = std::max(
        kMinimumStrideDistance,
        kBaseStrideDistance - (horizontalSpeed * 0.045F)
      );
      if (state.distanceSinceStep >= strideDistance) {
        emitMovementSound(false, false);
        state.distanceSinceStep = std::fmod(state.distanceSinceStep, strideDistance);
      }
    } else if (!player.onGround || horizontalSpeed < 0.25F) {
      state.distanceSinceStep = 0.0F;
    }

    state.previousPosition = player.position;
    state.wasOnGround = player.onGround;
  }
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
  const bool anyPlayerConnected = std::any_of(
    snapshot_.connectedPlayers.begin(),
    snapshot_.connectedPlayers.end(),
    [](bool connected) { return connected; }
  );
  for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
    snapshot_.participatingPlayers[playerIndex] =
      snapshot_.connectedPlayers[playerIndex] ||
      (botDodgeEnabled_ && anyPlayerConnected);
  }
}

void ServerGame::updateBotCommands(float fixedDt) {
  for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
    if (snapshot_.connectedPlayers[playerIndex]) {
      continue;
    }
    snapshot_.playerNames[playerIndex] = "BOT";
    if (!snapshot_.participatingPlayers[playerIndex]) {
      commands_[playerIndex] = {};
      commands_[playerIndex].viewYawRadians =
        snapshot_.players[playerIndex].viewYawRadians;
      commands_[playerIndex].viewPitchRadians =
        snapshot_.players[playerIndex].viewPitchRadians;
      hasCommand_[playerIndex] = false;
      botDodgeSwitchSeconds_[playerIndex] = 0.0F;
      continue;
    }

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

    UserCommand command;
    command.viewYawRadians = snapshot_.players[playerIndex].viewYawRadians;
    command.viewPitchRadians = snapshot_.players[playerIndex].viewPitchRadians;
    command.rightMove =
      botDodgeDirections_[playerIndex] < 0 ? -1.0F : 1.0F;
    command.weapon = Weapon::LightningGun;
    commands_[playerIndex] = command;
    hasCommand_[playerIndex] = true;
  }
}

std::uint32_t ServerGame::randomU32() {
  std::uint32_t value = botRandomState_;
  value ^= value << 13U;
  value ^= value >> 17U;
  value ^= value << 5U;
  botRandomState_ = value == 0U ? 0xB07D0D6EU : value;
  return botRandomState_;
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

bool ServerGame::loadRequestedMap(const std::string& mapName) {
  if (mapName.empty() || mapName.size() > kMaxMapNameBytes) {
    return false;
  }

  namespace fs = std::filesystem;
  const fs::path requested(mapName);
  if (requested.has_parent_path() || requested.filename().string() != mapName) {
    return false;
  }

  const std::string extension = requested.extension().string();
  const std::string stem = extension.empty()
    ? mapName
    : requested.stem().string();
  if (!extension.empty() && extension != ".lgmap" && extension != ".map") {
    return false;
  }
  if (stem.empty()) {
    return false;
  }
  for (const unsigned char character : stem) {
    if (
      !std::isalnum(character) &&
      character != '_' &&
      character != '-'
    ) {
      return false;
    }
  }

  const fs::path directory = fs::path(mapDirectory_.empty() ? "maps" : mapDirectory_);
  std::vector<fs::path> candidates;
  if (extension.empty()) {
    candidates.push_back(directory / (mapName + ".lgmap"));
    candidates.push_back(directory / (mapName + ".map"));
  } else {
    candidates.push_back(directory / mapName);
  }

  for (const fs::path& path : candidates) {
    const ArenaLoadResult result = loadArenaFromFile(path.string());
    if (result.ok) {
      setArena(result.arena);
      return true;
    }
  }
  std::cerr << "map load failed for '" << mapName << "'; tried";
  for (const fs::path& path : candidates) {
    std::cerr << " '" << path.string() << "'";
  }
  std::cerr << '\n';
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

    if (packet.requestMovementTuning) {
      movementTuning_.flightEnabled = packet.movementTuning.flightEnabled;
      movementTuning_.airControlEnabled = packet.movementTuning.airControlEnabled;
      movementTuning_.groundAcceleration = packet.movementTuning.groundAcceleration;
      movementTuning_.airAcceleration = packet.movementTuning.airAcceleration;
      movementTuning_.groundFriction = packet.movementTuning.groundFriction;
      movementTuning_.stopSpeed = packet.movementTuning.stopSpeed;
      movementTuning_.maxGroundSpeed = packet.movementTuning.maxGroundSpeed;
      movementTuning_.maxAirSpeed = packet.movementTuning.maxGroundSpeed;
      movementTuning_.flightAcceleration =
        packet.movementTuning.flightAcceleration;
      movementTuning_.maxFlightSpeed =
        packet.movementTuning.maxFlightSpeed;
      movementTuning_.flightDamping =
        packet.movementTuning.flightDamping;
      movementTuning_.flightGravityCancel =
        packet.movementTuning.flightGravityCancel;
      snapshot_.movementTuning = movementTuning_;
      playerSizeScaleXY_ = packet.playerSizeScaleXY;
      playerSizeScaleZ_ = packet.playerSizeScaleZ;
      snapshot_.playerSizeScaleXY = playerSizeScaleXY_;
      snapshot_.playerSizeScaleZ = playerSizeScaleZ_;
      lightningKnockback_ = packet.lightningKnockback;
      lightningGunTuning_.knockbackPerSecond =
        lightningKnockbackToInternal(lightningKnockback_);
      snapshot_.lightningKnockback = lightningKnockback_;
      lightningFireHz_ = packet.lightningFireHz;
      lightningGunTuning_.fireHz = lightningFireHz_;
      snapshot_.lightningFireHz = lightningFireHz_;
      rocketKnockback_ = packet.rocketKnockback;
      rocketLauncherTuning_.knockback =
        q3KnockbackToInternal(rocketKnockback_);
      grenadeLauncherTuning_.knockback =
        q3KnockbackToInternal(rocketKnockback_);
      snapshot_.rocketKnockback = rocketKnockback_;
      weaponDamage_ = packet.weaponDamage;
      shotgunTuning_.damagePerPellet = weaponDamage_.shotgunDamagePerPellet;
      machineGunTuning_.damage = weaponDamage_.machineGunDamage;
      lightningGunTuning_.damagePerSecond =
        static_cast<float>(weaponDamage_.lightningGunDamage);
      railgunTuning_.damage = weaponDamage_.railgunDamage;
      rocketLauncherTuning_.directDamage =
        weaponDamage_.rocketLauncherDamage;
      rocketLauncherTuning_.splashDamage =
        weaponDamage_.rocketLauncherDamage;
      grenadeLauncherTuning_.directDamage =
        weaponDamage_.rocketLauncherDamage;
      grenadeLauncherTuning_.splashDamage =
        weaponDamage_.rocketLauncherDamage;
      plasmaGunTuning_.damage = weaponDamage_.plasmaGunDamage;
      snapshot_.weaponDamage = weaponDamage_;
      if (vampirism_ != packet.vampirism) {
        fractionalVampirismHealing_ = {};
      }
      vampirism_ = packet.vampirism;
      snapshot_.vampirism = vampirism_;
      selfDamagePercent_ = packet.selfDamagePercent;
      snapshot_.selfDamagePercent = selfDamagePercent_;
      const bool healthAmountChanged = healthAmount_ != packet.healthAmount;
      healthAmount_ = packet.healthAmount;
      snapshot_.healthAmount = healthAmount_;
      setBotDodge(
        packet.botDodgeEnabled,
        packet.botDodgeMinIntervalMs,
        packet.botDodgeMaxIntervalMs
      );
      setWeaponSwitchingMode(packet.weaponSwitchingMode);
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

    if (
      packet.requestGameMode &&
      snapshot_.connectedPlayers[playerIndex] &&
      warmupPhase() &&
      packet.requestedGameMode != snapshot_.gameMode
    ) {
      snapshot_.gameMode = packet.requestedGameMode;
      snapshot_.teams = {};
      resetMatch();
    }
    if (
      packet.requestTeam &&
      snapshot_.connectedPlayers[playerIndex] &&
      snapshot_.gameMode == GameMode::ClanArena &&
      warmupPhase() &&
      packet.requestedTeam != snapshot_.teams[playerIndex]
    ) {
      snapshot_.teams[playerIndex] = packet.requestedTeam;
      resetMatch();
    }

    if (packet.requestReset) {
      resetMatch();
      snapshot_.hasAcknowledgedCommand[playerIndex] = true;
      snapshot_.acknowledgedCommand[playerIndex] = packet.command.sequence;
      continue;
    }
    if (packet.toggleReady) {
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
      ++snapshot_.chatSequence;
      snapshot_.chatPlayerIndex = packet.playerIndex;
      snapshot_.chatMessage = packet.chatMessage;
    }
    if (!packet.playerName.empty()) {
      snapshot_.playerNames[playerIndex] = packet.playerName;
    }
    if (!packet.mapName.empty()) {
      (void)loadRequestedMap(packet.mapName);
    }

    commands_[playerIndex] = packet.command;
    viewedServerTicks_[playerIndex] = packet.viewedServerTick;
    hasCommand_[playerIndex] = true;
    receivedCommandThisTick_[playerIndex] = true;
    snapshot_.hasAcknowledgedCommand[playerIndex] = true;
    snapshot_.acknowledgedCommand[playerIndex] = packet.command.sequence;
  }
}

void ServerGame::publishSnapshot() {
  updateParticipatingPlayers();
  transport_.sendSnapshot(snapshot_);
}

} // namespace lg
