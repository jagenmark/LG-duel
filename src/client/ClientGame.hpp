#pragma once

#include "client/Interpolation.hpp"
#include "client/Prediction.hpp"
#include "net/NetProtocol.hpp"
#include "net/NetTransport.hpp"
#include "sim/Arena.hpp"
#include "sim/Movement.hpp"

#include <cstddef>
#include <cstdint>

namespace lg {

class ClientGame {
public:
  ClientGame(NetTransport& transport, std::size_t localPlayerIndex);

  void sendCommand(
    const UserCommand& command,
    bool requestReset,
    bool toggleReady = false,
    bool requestMovementTuning = false,
    const MovementTuning& movementTuning = {}
  );
  void receiveSnapshots();

  [[nodiscard]] bool hasSnapshot() const;
  [[nodiscard]] const ServerSnapshot& snapshot() const;
  [[nodiscard]] bool hasAcknowledgedCommand() const;
  [[nodiscard]] std::uint32_t lastAcknowledgedCommand() const;
  [[nodiscard]] const PlayerState& predictedPlayer() const;
  [[nodiscard]] PlayerState interpolatedPlayer(std::size_t playerIndex, float alpha) const;
  [[nodiscard]] const PredictionDiagnostics& predictionDiagnostics() const;
  [[nodiscard]] const MovementTuning& movementTuning() const;

private:
  NetTransport& transport_;
  std::size_t localPlayerIndex_ = 0;
  Arena arena_ = thunderstruckArena();
  MovementTuning movementTuning_ = {};
  Prediction prediction_ = {};
  SnapshotInterpolation interpolation_ = {};
  ServerSnapshot snapshot_ = {};
  std::uint32_t pendingMovementTuningCommand_ = 0;
  bool hasPendingMovementTuning_ = false;
  bool hasSnapshot_ = false;
};

} // namespace lg
