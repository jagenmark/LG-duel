#include "server/ServerGame.hpp"

#include "shared/Sequence.hpp"
#include "sim/Collision.hpp"

#include <algorithm>
#include <cmath>

namespace lg {
namespace {

constexpr std::uint32_t kMaxLagCompensationTicks = 25;
constexpr float kPi = 3.14159265359F;
constexpr CollisionBounds kDefaultPlayerBounds = {};

[[nodiscard]] PlayerState spawnPlayer(
  const Arena& arena,
  std::size_t playerIndex
) {
  PlayerState player;
  player.position = arena.spawnPositions[playerIndex];
  player.position.z += player.bounds.halfHeight;
  player.viewYawRadians = playerIndex == 0 ? 0.0F : kPi;
  player.onGround = true;
  player.movementMode = MovementMode::Grounded;
  return player;
}

} // namespace

ServerGame::ServerGame(NetTransport& transport) : transport_(transport) {
  resetMatch();
  snapshot_.connectedPlayers.fill(true);
  snapshot_.readyPlayers.fill(true);
  snapshot_.matchPhase = MatchPhase::Live;
  publishSnapshot();
}

void ServerGame::tick(float fixedDt) {
  receivedCommandThisTick_.fill(false);
  receiveCommands();
  updateMatchState();

  for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
    if (snapshot_.players[playerIndex].health <= 0) {
      snapshot_.players[playerIndex].velocity = {};
      continue;
    }

    UserCommand command;
    if (hasCommand_[playerIndex]) {
      command = commands_[playerIndex];
    } else {
      command.viewYawRadians = snapshot_.players[playerIndex].viewYawRadians;
      command.viewPitchRadians = snapshot_.players[playerIndex].viewPitchRadians;
    }

    simulateMovement(
      snapshot_.players[playerIndex],
      command,
      arena_,
      movementTuning_,
      fixedDt
    );
  }

  snapshot_.playersColliding =
    resolvePlayerCollision(arena_, snapshot_.players[0], snapshot_.players[1]);
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
  for (std::size_t attackerIndex = 0; attackerIndex < kDuelPlayerCount; ++attackerIndex) {
    const std::size_t targetIndex = 1U - attackerIndex;
    UserCommand command;
    if (hasCommand_[attackerIndex]) {
      command = commands_[attackerIndex];
    } else {
      command.viewYawRadians = snapshot_.players[attackerIndex].viewYawRadians;
      command.viewPitchRadians = snapshot_.players[attackerIndex].viewPitchRadians;
    }

    const bool warmupCombat =
      snapshot_.matchPhase == MatchPhase::WaitingForPlayers ||
      snapshot_.matchPhase == MatchPhase::WaitingForReady;
    command.attack =
      command.attack &&
      (snapshot_.matchPhase == MatchPhase::Live || warmupCombat) &&
      snapshot_.connectedPlayers[attackerIndex] &&
      (
        warmupCombat ||
        snapshot_.connectedPlayers[targetIndex]
      );
    if (command.planarAim) {
      command.viewPitchRadians = 0.0F;
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

    PlayerState target = clampedRewindTicks == 0
      ? combatPlayers[targetIndex]
      : historyFrame.players[targetIndex];
    target.health = combatPlayers[targetIndex].health;
    snapshot_.lightningGuns[attackerIndex] = simulateLightningGun(
      combatPlayers[attackerIndex],
      target,
      command,
      arena_,
      lightningGunTuning_,
      lightningGunStates_[attackerIndex],
      fixedDt
    );
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
    result.currentTargetPosition = combatPlayers[targetIndex].position;
    result.rewoundTargetPosition = target.position;
    result.currentTargetBounds = combatPlayers[targetIndex].bounds;
    result.rewoundTargetBounds = target.bounds;
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
    const std::size_t targetIndex = 1U - attackerIndex;
    PlayerState& attacker = snapshot_.players[attackerIndex];
    PlayerState& target = snapshot_.players[targetIndex];
    const bool wasAlive = target.health > 0;
    const int damageApplied =
      snapshot_.lightningGuns[attackerIndex].damageApplied;
    target.health = std::max(0, target.health - damageApplied);
    if (attacker.health > 0 && damageApplied > 0 && vampirism_ > 0.0F) {
      fractionalVampirismHealing_[attackerIndex] +=
        static_cast<double>(damageApplied) * static_cast<double>(vampirism_);
      const int healing = static_cast<int>(
        std::floor(fractionalVampirismHealing_[attackerIndex])
      );
      fractionalVampirismHealing_[attackerIndex] -=
        static_cast<double>(healing);
      attacker.health = std::min(100, attacker.health + healing);
    }
    if (snapshot_.matchPhase == MatchPhase::Live) {
      snapshot_.roundCombatStats[attackerIndex].damageDealt +=
        static_cast<std::uint32_t>(
          damageApplied
        );
      snapshot_.matchCombatStats[attackerIndex].damageDealt +=
        static_cast<std::uint32_t>(
          damageApplied
        );
    }
    target.velocity += snapshot_.lightningGuns[attackerIndex].knockbackImpulse;
    if (
      wasAlive &&
      target.health == 0 &&
      snapshot_.matchPhase == MatchPhase::Live
    ) {
      target.velocity = {};
      beginRoundEnd(attackerIndex);
      break;
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

  if (snapshot_.matchPhase == MatchPhase::Live) {
    ++snapshot_.liveTicksElapsed;
    if (matchRules_.timeLimitMinutes > 0) {
      const std::uint32_t limitTicks =
        static_cast<std::uint32_t>(matchRules_.timeLimitMinutes) * 60U * 125U;
      if (
        snapshot_.liveTicksElapsed >= limitTicks &&
        snapshot_.scores[0] != snapshot_.scores[1]
      ) {
        beginMatchEnd(snapshot_.scores[0] > snapshot_.scores[1] ? 0U : 1U);
      }
    }
  }

  ++snapshot_.serverTick;
  recordHistory();
  publishSnapshot();
}

void ServerGame::resetMatch() {
  const std::uint32_t serverTick = snapshot_.serverTick;
  const auto playerNames = snapshot_.playerNames;
  snapshot_ = {};
  snapshot_.serverTick = serverTick;
  snapshot_.players[0] = spawnPlayer(arena_, 0);
  snapshot_.players[1] = spawnPlayer(arena_, 1);
  for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
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
  snapshot_.lightningKnockback = lightningGunTuning_.knockbackPerSecond;
  snapshot_.vampirism = vampirism_;
  snapshot_.roundWinner = 255;
  snapshot_.matchWinner = 255;
  snapshot_.playerNames = playerNames;
  lightningGunStates_ = {};
  fractionalVampirismHealing_ = {};
  commands_ = {};
  viewedServerTicks_ = {};
  hasCommand_ = {};
  receivedCommandThisTick_ = {};
  history_.clear();
  recordHistory();
}

void ServerGame::respawnPlayer(std::size_t playerIndex) {
  snapshot_.players[playerIndex] = spawnPlayer(arena_, playerIndex);
  snapshot_.players[playerIndex].bounds.radius =
    kDefaultPlayerBounds.radius * playerSizeScaleXY_;
  snapshot_.players[playerIndex].bounds.halfHeight =
    kDefaultPlayerBounds.halfHeight * playerSizeScaleZ_;
  snapshot_.players[playerIndex].position.z =
    arena_.spawnPositions[playerIndex].z +
    snapshot_.players[playerIndex].bounds.halfHeight;
  snapshot_.lightningGuns[playerIndex] = {};
  lightningGunStates_[playerIndex] = {};
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
      snapshot_.playerNames[index] = "PLAYER " + std::to_string(index + 1U);
      commands_[index] = {};
      viewedServerTicks_[index] = 0;
      hasCommand_[index] = false;
      receivedCommandThisTick_[index] = false;
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

const ServerSnapshot& ServerGame::snapshot() const {
  return snapshot_;
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
      lightningGunTuning_.knockbackPerSecond = packet.lightningKnockback;
      snapshot_.lightningKnockback = lightningGunTuning_.knockbackPerSecond;
      if (vampirism_ != packet.vampirism) {
        fractionalVampirismHealing_ = {};
      }
      vampirism_ = packet.vampirism;
      snapshot_.vampirism = vampirism_;
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
