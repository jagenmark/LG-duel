#include "client/ClientGame.hpp"

#include "shared/Constants.hpp"

namespace lg {

ClientGame::ClientGame(NetTransport& transport, std::size_t localPlayerIndex)
  : transport_(transport), localPlayerIndex_(localPlayerIndex) {}

void ClientGame::sendCommand(
  const UserCommand& command,
  bool requestReset,
  bool toggleReady
) {
  transport_.sendCommand(
    CommandPacket{
      static_cast<std::uint8_t>(localPlayerIndex_),
      command,
      requestReset,
      toggleReady,
      snapshot_.serverTick,
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

PlayerState ClientGame::interpolatedPlayer(std::size_t playerIndex, float alpha) const {
  return interpolation_.player(playerIndex, alpha);
}

const PredictionDiagnostics& ClientGame::predictionDiagnostics() const {
  return prediction_.diagnostics();
}

} // namespace lg
