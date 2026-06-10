#include "server/ServerGame.hpp"

#include "shared/Sequence.hpp"
#include "sim/Collision.hpp"

#include <algorithm>

namespace lg {
namespace {

constexpr std::uint32_t kMaxLagCompensationTicks = 25;
constexpr float kPi = 3.14159265359F;

[[nodiscard]] PlayerState spawnPlayer(std::size_t playerIndex) {
  PlayerState player;
  if (playerIndex == 0) {
    player.position = {-3.0F, 0.0F, player.bounds.halfHeight};
    player.viewYawRadians = 0.0F;
  } else {
    player.position = {3.0F, 0.0F, player.bounds.halfHeight};
    player.viewYawRadians = kPi;
  }
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

    command.attack = command.attack && snapshot_.matchPhase == MatchPhase::Live;
    const bool requestsLagCompensation = hasCommand_[attackerIndex] && command.attack;
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
  }

  for (std::size_t attackerIndex = 0; attackerIndex < kDuelPlayerCount; ++attackerIndex) {
    const std::size_t targetIndex = 1U - attackerIndex;
    PlayerState& target = snapshot_.players[targetIndex];
    const bool wasAlive = target.health > 0;
    target.health = std::max(0, target.health - snapshot_.lightningGuns[attackerIndex].damageApplied);
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

  hasCommand_.fill(false);
  ++snapshot_.serverTick;
  recordHistory();
  publishSnapshot();
}

void ServerGame::resetMatch() {
  const std::uint32_t serverTick = snapshot_.serverTick;
  snapshot_ = {};
  snapshot_.serverTick = serverTick;
  snapshot_.players[0] = spawnPlayer(0);
  snapshot_.players[1] = spawnPlayer(1);
  snapshot_.matchRules = matchRules_;
  snapshot_.roundWinner = 255;
  snapshot_.matchWinner = 255;
  lightningGunStates_ = {};
  commands_ = {};
  viewedServerTicks_ = {};
  hasCommand_ = {};
  history_.clear();
  recordHistory();
}

void ServerGame::respawnPlayer(std::size_t playerIndex) {
  snapshot_.players[playerIndex] = spawnPlayer(playerIndex);
  snapshot_.lightningGuns[playerIndex] = {};
  lightningGunStates_[playerIndex] = {};
}

void ServerGame::respawnRound() {
  for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
    respawnPlayer(playerIndex);
  }
  snapshot_.playersColliding = false;
  snapshot_.respawnTicksRemaining = {};
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
    }
  }
  snapshot_.connectedPlayers = connectedPlayers;

  if (!enoughPlayersConnected()) {
    snapshot_.readyPlayers = {};
    snapshot_.matchPhase = MatchPhase::WaitingForPlayers;
    snapshot_.phaseTicksRemaining = 0;
    snapshot_.scores = {};
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

    commands_[playerIndex] = packet.command;
    viewedServerTicks_[playerIndex] = packet.viewedServerTick;
    hasCommand_[playerIndex] = true;
    snapshot_.hasAcknowledgedCommand[playerIndex] = true;
    snapshot_.acknowledgedCommand[playerIndex] = packet.command.sequence;
  }
}

void ServerGame::publishSnapshot() {
  transport_.sendSnapshot(snapshot_);
}

} // namespace lg
