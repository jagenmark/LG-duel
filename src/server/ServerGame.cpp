#include "server/ServerGame.hpp"

#include "shared/Sequence.hpp"
#include "sim/Collision.hpp"

#include <algorithm>
#include <cmath>

namespace lg {
namespace {

constexpr std::uint32_t kMaxLagCompensationTicks = 25;
constexpr std::uint32_t kRailgunCooldownTicks = 188;
constexpr std::uint32_t kRocketLauncherCooldownTicks = 100;
constexpr std::uint32_t kTransientCombatEventTicks = 8;
constexpr CollisionBounds kDefaultPlayerBounds = {};
constexpr float kQ3KnockbackToInternalScale = 22.0F / 1000.0F;

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
      snapshot.botDodgeEnabled &&
      snapshot.players[playerIndex].health > 0
    );
}

[[nodiscard]] std::size_t firstCombatTarget(
  const ServerSnapshot& snapshot,
  std::size_t attackerIndex
) {
  for (std::size_t targetIndex = 0; targetIndex < kDuelPlayerCount; ++targetIndex) {
    if (
      targetIndex != attackerIndex &&
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

} // namespace

ServerGame::ServerGame(NetTransport& transport) : transport_(transport) {
  rocketLauncherTuning_.knockback = q3KnockbackToInternal(rocketKnockback_);
  resetMatch();
  snapshot_.connectedPlayers[0] = true;
  snapshot_.connectedPlayers[1] = true;
  snapshot_.readyPlayers[0] = true;
  snapshot_.readyPlayers[1] = true;
  snapshot_.matchPhase = MatchPhase::Live;
  publishSnapshot();
}

void ServerGame::tick(float fixedDt) {
  receivedCommandThisTick_.fill(false);
  receiveCommands();
  updateMatchState();
  updateBotCommands(fixedDt);
  snapshot_.weaponFires = {};
  snapshot_.rocketExplosions = {};
  snapshot_.rockets = {};
  for (std::uint32_t& cooldown : railgunCooldownTicks_) {
    if (cooldown > 0) {
      --cooldown;
    }
  }
  for (std::uint32_t& cooldown : rocketCooldownTicks_) {
    if (cooldown > 0) {
      --cooldown;
    }
  }

  for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
    if (snapshot_.players[playerIndex].health <= 0) {
      snapshot_.players[playerIndex].velocity = {};
      continue;
    }

    const UserCommand command =
      commandForPlayer(snapshot_, commands_, hasCommand_, playerIndex);

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
    if (!snapshot_.connectedPlayers[firstIndex]) {
      continue;
    }
    for (
      std::size_t secondIndex = firstIndex + 1U;
      secondIndex < kDuelPlayerCount;
      ++secondIndex
    ) {
      if (!snapshot_.connectedPlayers[secondIndex]) {
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
    const CollisionResult collision = resolvePlayerArenaCollision(
      arena_,
      player,
      player.position,
      player.velocity
    );
    player.position = collision.position;
    player.velocity = collision.velocity;
  }

  const std::array<PlayerState, kDuelPlayerCount> combatPlayers = snapshot_.players;
  std::array<std::size_t, kDuelPlayerCount> lightningTargets = {};
  std::array<std::size_t, kDuelPlayerCount> weaponTargets = {};
  lightningTargets.fill(kDuelPlayerCount);
  weaponTargets.fill(kDuelPlayerCount);
  for (std::size_t attackerIndex = 0; attackerIndex < kDuelPlayerCount; ++attackerIndex) {
    UserCommand command =
      commandForPlayer(snapshot_, commands_, hasCommand_, attackerIndex);

    const bool warmupCombat =
      snapshot_.matchPhase == MatchPhase::WaitingForPlayers ||
      snapshot_.matchPhase == MatchPhase::WaitingForReady;
    const bool hasTarget =
      firstCombatTarget(snapshot_, attackerIndex) < kDuelPlayerCount;
    command.attack =
      command.attack &&
      (snapshot_.matchPhase == MatchPhase::Live || warmupCombat) &&
      snapshot_.connectedPlayers[attackerIndex] &&
      (warmupCombat || hasTarget);
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
        : railgunTuning_.eyeHeight
    );
    const Vec3 attackDirection =
      cameraForward(command.viewYawRadians, command.viewPitchRadians);
    const float attackRange = command.weapon == Weapon::LightningGun
      ? lightningGunTuning_.range
      : railgunTuning_.range;
    const WorldTrace worldTrace =
      traceWorld(arena_, attackStart, attackDirection, attackRange);
    std::size_t targetIndex = kDuelPlayerCount;
    float bestHitDistance = worldTrace.distance;
    for (std::size_t candidateIndex = 0; candidateIndex < kDuelPlayerCount; ++candidateIndex) {
      if (
        candidateIndex == attackerIndex ||
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
        fire.start = attackStart;
        fire.end = worldTrace.end;
        fire.fired = command.attack && combatPlayers[attackerIndex].health > 0;
      }
      railgunCooldownTicks_[attackerIndex] = kRailgunCooldownTicks;
    } else if (
      command.weapon == Weapon::RocketLauncher &&
      command.attack &&
      rocketCooldownTicks_[attackerIndex] == 0
    ) {
      for (RocketProjectile& rocket : rockets_) {
        if (rocket.active) {
          continue;
        }
        const Vec3 direction =
          cameraForward(command.viewYawRadians, command.viewPitchRadians);
        rocket.active = true;
        rocket.owner = static_cast<std::uint8_t>(attackerIndex);
        rocket.position =
          weaponMuzzlePosition(combatPlayers[attackerIndex], rocketLauncherTuning_.eyeHeight);
        rocket.previousPosition = rocket.position;
        rocket.velocity = direction * rocketLauncherTuning_.speed;
        rocket.ageTicks = 0;
        rocket.ownerCollisionArmed = false;
        WeaponFireResult& fire = snapshot_.weaponFires[attackerIndex];
        fire.fired = true;
        fire.weapon = Weapon::RocketLauncher;
        fire.start = rocket.position;
        fire.end = rocket.position + (direction * 1.2F);
        rocketCooldownTicks_[attackerIndex] = kRocketLauncherCooldownTicks;
        break;
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
    applyDamageAndKnockback(
      attackerIndex,
      lightningTargets[attackerIndex],
      snapshot_.lightningGuns[attackerIndex].damageApplied,
      snapshot_.lightningGuns[attackerIndex].knockbackImpulse
    );
    applyDamageAndKnockback(
      attackerIndex,
      weaponTargets[attackerIndex],
      snapshot_.weaponFires[attackerIndex].damageApplied,
      snapshot_.weaponFires[attackerIndex].knockbackImpulse
    );
  }

  simulateRockets(fixedDt);

  if (snapshot_.matchPhase == MatchPhase::Live) {
    ++snapshot_.liveTicksElapsed;
    if (matchRules_.timeLimitMinutes > 0) {
      const std::uint32_t limitTicks =
        static_cast<std::uint32_t>(matchRules_.timeLimitMinutes) * 60U * 125U;
      if (snapshot_.liveTicksElapsed >= limitTicks) {
        std::size_t leaderIndex = 0;
        bool tied = false;
        for (std::size_t index = 1; index < kDuelPlayerCount; ++index) {
          if (snapshot_.scores[index] > snapshot_.scores[leaderIndex]) {
            leaderIndex = index;
            tied = false;
          } else if (snapshot_.scores[index] == snapshot_.scores[leaderIndex]) {
            tied = true;
          }
        }
        if (!tied) {
          beginMatchEnd(leaderIndex);
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
  snapshot_ = {};
  snapshot_.serverTick = serverTick;
  snapshot_.mapRevision = mapRevision_;
  snapshot_.arena = arena_;
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
  snapshot_.vampirism = vampirism_;
  snapshot_.selfDamagePercent = selfDamagePercent_;
  snapshot_.healthAmount = healthAmount_;
  snapshot_.botDodgeEnabled = botDodgeEnabled_;
  snapshot_.botDodgeMinIntervalMs = botDodgeMinIntervalMs_;
  snapshot_.botDodgeMaxIntervalMs = botDodgeMaxIntervalMs_;
  snapshot_.roundWinner = 255;
  snapshot_.matchWinner = 255;
  snapshot_.playerNames = playerNames;
  lightningGunStates_ = {};
  railgunCooldownTicks_ = {};
  rocketCooldownTicks_ = {};
  recentWeaponFires_ = {};
  recentWeaponFireTicks_ = {};
  recentRocketExplosions_ = {};
  recentRocketExplosionTicks_ = {};
  rockets_ = {};
  fractionalVampirismHealing_ = {};
  commands_ = {};
  viewedServerTicks_ = {};
  hasCommand_ = {};
  receivedCommandThisTick_ = {};
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
  lightningGunStates_[playerIndex] = {};
  railgunCooldownTicks_[playerIndex] = 0;
  rocketCooldownTicks_[playerIndex] = 0;
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

  for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
    if (!connectedPlayers[index]) {
      snapshot_.readyPlayers[index] = false;
      snapshot_.playerNames[index] = "BOT";
      commands_[index] = {};
      viewedServerTicks_[index] = 0;
      hasCommand_[index] = false;
      receivedCommandThisTick_[index] = false;
      botDodgeSwitchSeconds_[index] = 0.0F;
    } else if (!snapshot_.connectedPlayers[index]) {
      snapshot_.playerNames[index] = "PLAYER " + std::to_string(index + 1U);
      commands_[index] = {};
      viewedServerTicks_[index] = 0;
      hasCommand_[index] = false;
      receivedCommandThisTick_[index] = false;
      botDodgeSwitchSeconds_[index] = 0.0F;
    }
  }
  snapshot_.connectedPlayers = connectedPlayers;

  if (!enoughPlayersConnected()) {
    snapshot_.readyPlayers = {};
    snapshot_.matchPhase = MatchPhase::WaitingForPlayers;
    snapshot_.phaseTicksRemaining = 0;
    snapshot_.scores = {};
    snapshot_.matchCombatStats = {};
    snapshot_.liveTicksElapsed = 0;
    snapshot_.roundWinner = 255;
    snapshot_.matchWinner = 255;
    respawnRound();
  } else if (
    snapshot_.matchPhase == MatchPhase::WaitingForPlayers ||
    snapshot_.matchPhase == MatchPhase::MatchEnd
  ) {
    snapshot_.matchPhase = MatchPhase::WaitingForReady;
    snapshot_.phaseTicksRemaining = 0;
  }
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
      snapshot_.matchCombatStats = {};
      snapshot_.liveTicksElapsed = 0;
      snapshot_.roundWinner = 255;
      snapshot_.matchWinner = 255;
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
      snapshot_.matchCombatStats = {};
      snapshot_.readyPlayers = {};
      snapshot_.liveTicksElapsed = 0;
      snapshot_.roundWinner = 255;
      snapshot_.matchWinner = 255;
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
  respawnRound();
  if (snapshot_.phaseTicksRemaining == 0) {
    snapshot_.matchPhase = MatchPhase::Live;
  }
}

void ServerGame::beginRoundEnd(std::size_t winnerIndex) {
  ++snapshot_.scores[winnerIndex];
  snapshot_.roundWinner = static_cast<std::uint8_t>(winnerIndex);
  snapshot_.phaseTicksRemaining = matchRules_.roundEndTicks;
  snapshot_.matchPhase = MatchPhase::RoundEnd;
  if (snapshot_.scores[winnerIndex] >= matchRules_.roundLimit) {
    beginMatchEnd(winnerIndex);
  }
}

void ServerGame::beginMatchEnd(std::size_t winnerIndex) {
  snapshot_.matchWinner = static_cast<std::uint8_t>(winnerIndex);
  snapshot_.matchPhase = MatchPhase::MatchEnd;
  snapshot_.phaseTicksRemaining = matchRules_.matchEndTicks;
}

bool ServerGame::enoughPlayersConnected() const {
  return static_cast<std::size_t>(std::count(
    snapshot_.connectedPlayers.begin(),
    snapshot_.connectedPlayers.end(),
    true
  )) >= matchRules_.playerLimit;
}

bool ServerGame::allConnectedPlayersReady() const {
  for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
    if (snapshot_.connectedPlayers[index] && !snapshot_.readyPlayers[index]) {
      return false;
    }
  }
  return true;
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
  Vec3 knockbackImpulse
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
  damageApplied = std::min(damageApplied, target.health);
  target.health = std::max(0, target.health - damageApplied);
  target.velocity += knockbackImpulse;

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
    snapshot_.matchPhase == MatchPhase::Live
  ) {
    target.velocity = {};
    beginRoundEnd(attackerIndex);
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

  for (RocketProjectile& rocket : rockets_) {
    if (!rocket.active) {
      continue;
    }

    rocket.previousPosition = rocket.position;
    const Vec3 nextPosition = rocket.position + (rocket.velocity * fixedDt);
    const Vec3 segment = nextPosition - rocket.position;
    const float segmentLength = length(segment);
    const Vec3 direction = segmentLength > 0.0F
      ? segment / segmentLength
      : normalize(rocket.velocity);

    if (
      !rocket.ownerCollisionArmed &&
      cylinderDistance(rocket.position, snapshot_.players[rocket.owner]) > 0.0001F
    ) {
      rocket.ownerCollisionArmed = true;
    }

    bool explode = false;
    Vec3 explosionPosition = nextPosition;
    std::size_t directTarget = kDuelPlayerCount;

    if (segmentLength > 0.0F) {
      const WorldTrace worldTrace =
        traceWorld(arena_, rocket.position, direction, segmentLength);
      if (worldTrace.distance < segmentLength - 0.0001F) {
        explode = true;
        explosionPosition = worldTrace.end;
      }

      float bestHitDistance = segmentLength;
      for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
        if (
          snapshot_.players[playerIndex].health <= 0 ||
          !isCombatant(snapshot_, playerIndex) ||
          (playerIndex == rocket.owner && !rocket.ownerCollisionArmed)
        ) {
          continue;
        }
        float hitDistance = 0.0F;
        if (
          tracePlayerCylinder(
            rocket.position,
            direction,
            snapshot_.players[playerIndex],
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

    ++rocket.ageTicks;
    if (!explode && rocket.ageTicks >= rocketLauncherTuning_.maxLifetimeTicks) {
      explode = true;
      explosionPosition = nextPosition;
    }

    if (!explode) {
      rocket.position = nextPosition;
      continue;
    }

    rocket.active = false;
    RocketExplosionResult& explosion = snapshot_.rocketExplosions[rocket.owner];
    explosion.active = true;
    explosion.position = explosionPosition;
    explosion.radius = rocketLauncherTuning_.radius;

    for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
      PlayerState& player = snapshot_.players[playerIndex];
      if (player.health <= 0 || !isCombatant(snapshot_, playerIndex)) {
        continue;
      }
      const float distance = cylinderDistance(explosionPosition, player);
      if (distance > rocketLauncherTuning_.radius) {
        continue;
      }
      const float falloff =
        1.0F - (distance / std::max(0.001F, rocketLauncherTuning_.radius));
      int damage = static_cast<int>(std::ceil(
        static_cast<float>(rocketLauncherTuning_.splashDamage) * falloff
      ));
      if (playerIndex == directTarget) {
        damage = std::max(damage, rocketLauncherTuning_.directDamage);
      }
      const int knockbackDamage = damage;
      const int appliedDamage = playerIndex == rocket.owner
        ? (damage * static_cast<int>(selfDamagePercent_) + 50) / 100
        : damage;
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
        static_cast<float>(std::max(1, rocketLauncherTuning_.splashDamage));
      applyDamageAndKnockback(
        rocket.owner,
        playerIndex,
        appliedDamage,
        knockbackDirection * rocketLauncherTuning_.knockback * knockbackScale
      );
    }
  }

  for (std::size_t index = 0; index < rockets_.size(); ++index) {
    snapshot_.rockets[index].active = rockets_[index].active;
    snapshot_.rockets[index].owner = rockets_[index].owner;
    snapshot_.rockets[index].position = rockets_[index].position;
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
  }
}

void ServerGame::updateBotCommands(float fixedDt) {
  const bool anyPlayerConnected = std::any_of(
    snapshot_.connectedPlayers.begin(),
    snapshot_.connectedPlayers.end(),
    [](bool connected) { return connected; }
  );
  for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
    if (snapshot_.connectedPlayers[playerIndex]) {
      continue;
    }
    snapshot_.playerNames[playerIndex] = "BOT";
    if (!botDodgeEnabled_ || !anyPlayerConnected) {
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
        q3KnockbackToInternal(lightningKnockback_);
      snapshot_.lightningKnockback = lightningKnockback_;
      rocketKnockback_ = packet.rocketKnockback;
      rocketLauncherTuning_.knockback =
        q3KnockbackToInternal(rocketKnockback_);
      snapshot_.rocketKnockback = rocketKnockback_;
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

    if (packet.requestReset) {
      resetMatch();
      snapshot_.hasAcknowledgedCommand[playerIndex] = true;
      snapshot_.acknowledgedCommand[playerIndex] = packet.command.sequence;
      continue;
    }
    if (packet.toggleReady) {
      if (
        snapshot_.connectedPlayers[playerIndex] &&
        (snapshot_.matchPhase == MatchPhase::WaitingForPlayers ||
         snapshot_.matchPhase == MatchPhase::WaitingForReady)
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

    commands_[playerIndex] = packet.command;
    viewedServerTicks_[playerIndex] = packet.viewedServerTick;
    hasCommand_[playerIndex] = true;
    receivedCommandThisTick_[playerIndex] = true;
    snapshot_.hasAcknowledgedCommand[playerIndex] = true;
    snapshot_.acknowledgedCommand[playerIndex] = packet.command.sequence;
  }
}

void ServerGame::publishSnapshot() {
  transport_.sendSnapshot(snapshot_);
}

} // namespace lg
