#include "client/ClientGame.hpp"

#include "shared/Constants.hpp"
#include "shared/Sequence.hpp"

#include <utility>

namespace lg {

ClientGame::ClientGame(NetTransport& transport, std::size_t localPlayerIndex)
  : transport_(transport), localPlayerIndex_(localPlayerIndex) {}

void ClientGame::sendCommand(
  const UserCommand& command,
  bool requestReset,
  bool toggleReady,
  bool requestMovementTuning,
  const MovementTuning& movementTuning,
  float playerSizeScaleXY,
  float playerSizeScaleZ,
  float lightningKnockback,
  float vampirism,
  std::uint8_t selfDamagePercent,
  std::int32_t healthAmount,
  bool botDodgeEnabled,
  std::int32_t botDodgeMinIntervalMs,
  std::int32_t botDodgeMaxIntervalMs,
  std::string chatMessage,
  std::string playerName
) {
  if (requestMovementTuning) {
    movementTuning_ = movementTuning;
    movementTuning_.maxAirSpeed = movementTuning_.maxGroundSpeed;
    pendingMovementTuningCommand_ = command.sequence;
    hasPendingMovementTuning_ = true;
  }
  transport_.sendCommand(
    CommandPacket{
      static_cast<std::uint8_t>(localPlayerIndex_),
      command,
      requestReset,
      toggleReady,
      snapshot_.serverTick,
      requestMovementTuning,
      movementTuning_,
      playerSizeScaleXY,
      playerSizeScaleZ,
      lightningKnockback,
      vampirism,
      std::move(chatMessage),
      std::move(playerName),
      selfDamagePercent,
      healthAmount,
      botDodgeEnabled,
      botDodgeMinIntervalMs,
      botDodgeMaxIntervalMs,
    }
  );
  if (!requestReset) {
    prediction_.predict(command, arena_, movementTuning_, kFixedTickSeconds);
  }
}

void ClientGame::receiveSnapshots() {
  ServerSnapshot received;
  while (transport_.receiveSnapshot(received)) {
    if (!hasSnapshot_ || received.serverTick > snapshot_.serverTick) {
      snapshot_ = received;
      if (
        hasPendingMovementTuning_ &&
        received.hasAcknowledgedCommand[localPlayerIndex_] &&
        isSequenceAcknowledged(
          pendingMovementTuningCommand_,
          received.acknowledgedCommand[localPlayerIndex_]
        )
      ) {
        hasPendingMovementTuning_ = false;
      }
      if (!hasPendingMovementTuning_) {
        movementTuning_ = received.movementTuning;
        movementTuning_.maxAirSpeed = movementTuning_.maxGroundSpeed;
      }
      hasSnapshot_ = true;
      interpolation_.push(received);
      prediction_.reconcile(
        received.players[localPlayerIndex_],
        received.hasAcknowledgedCommand[localPlayerIndex_],
        received.acknowledgedCommand[localPlayerIndex_],
        arena_,
        movementTuning_,
        kFixedTickSeconds
      );
    }
  }
}

void ClientGame::advanceInterpolation(float elapsedSeconds) {
  interpolation_.advance(elapsedSeconds);
}

bool ClientGame::hasSnapshot() const {
  return hasSnapshot_;
}

const ServerSnapshot& ClientGame::snapshot() const {
  return snapshot_;
}

bool ClientGame::hasAcknowledgedCommand() const {
  return snapshot_.hasAcknowledgedCommand[localPlayerIndex_];
}

std::uint32_t ClientGame::lastAcknowledgedCommand() const {
  return snapshot_.acknowledgedCommand[localPlayerIndex_];
}

const PlayerState& ClientGame::predictedPlayer() const {
  return prediction_.player();
}

PlayerState ClientGame::interpolatedPlayer(std::size_t playerIndex) const {
  return interpolation_.player(playerIndex);
}

const PredictionDiagnostics& ClientGame::predictionDiagnostics() const {
  return prediction_.diagnostics();
}

const MovementTuning& ClientGame::movementTuning() const {
  return movementTuning_;
}

} // namespace lg
