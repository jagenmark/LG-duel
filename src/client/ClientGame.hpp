#pragma once

#include "client/Interpolation.hpp"
#include "client/Prediction.hpp"
#include "net/NetProtocol.hpp"
#include "net/NetTransport.hpp"
#include "sim/Arena.hpp"
#include "sim/Movement.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace lg {

class ClientGame {
public:
  ClientGame(NetTransport& transport, std::size_t localPlayerIndex);

  void sendCommand(
    const UserCommand& command,
    bool requestReset,
    bool toggleReady = false,
    bool requestMovementTuning = false,
    const MovementTuning& movementTuning = {},
    float playerSizeScaleXY = 1.0F,
    float playerSizeScaleZ = 1.0F,
    float lightningKnockback = 1000.0F,
    float rocketKnockback = 1000.0F,
    std::int32_t knockbackTimeMs = 100,
    float vampirism = 0.0F,
    std::uint8_t selfDamagePercent = 100,
    std::int32_t healthAmount = 100,
    const WeaponDamageTuning& weaponDamage = {},
    const WeaponAmmoConfig& weaponAmmo = {},
    float lightningFireHz = 20.0F,
    bool botDodgeEnabled = false,
    std::int32_t botDodgeMinIntervalMs = 250,
    std::int32_t botDodgeMaxIntervalMs = 750,
    std::string chatMessage = {},
    std::string playerName = {},
    std::string mapName = {},
    bool usePresentedServerTick = true,
    bool requestGameMode = false,
    GameMode requestedGameMode = GameMode::Duel,
    bool requestTeam = false,
    Team requestedTeam = Team::None,
    WeaponSwitchingMode weaponSwitchingMode = WeaponSwitchingMode::Crazy
  );
  void receiveSnapshots();
  void advanceInterpolation(float elapsedSeconds, float interpolationDelaySeconds);

  [[nodiscard]] bool hasSnapshot() const;
  [[nodiscard]] const ServerSnapshot& snapshot() const;
  [[nodiscard]] bool hasAcknowledgedCommand() const;
  [[nodiscard]] bool hasPendingMovementTuning() const;
  [[nodiscard]] std::uint32_t lastAcknowledgedCommand() const;
  [[nodiscard]] const PlayerState& predictedPlayer() const;
  [[nodiscard]] PlayerState interpolatedPlayer(std::size_t playerIndex) const;
  [[nodiscard]] PlayerState interpolatedPlayer(std::size_t playerIndex, float alpha) const;
  [[nodiscard]] const PredictionDiagnostics& predictionDiagnostics() const;
  [[nodiscard]] const MovementTuning& movementTuning() const;
  [[nodiscard]] const Arena& arena() const;
  [[nodiscard]] SnapshotDiagnostics snapshotDiagnostics() const;
  [[nodiscard]] bool hasConnectionError() const;
  [[nodiscard]] const std::string& connectionError() const;

private:
  NetTransport& transport_;
  std::size_t localPlayerIndex_ = 0;
  Arena arena_ = thunderstruckArena();
  std::uint32_t mapRevision_ = 1;
  MovementTuning movementTuning_ = {};
  Prediction prediction_ = {};
  SnapshotInterpolation interpolation_ = {};
  ServerSnapshot snapshot_ = {};
  MapDescriptor map_ = {};
  SnapshotDiagnostics snapshotDiagnostics_ = {};
  std::uint32_t lastSnapshotPacketsDecoded_ = 0;
  std::string connectionError_;
  std::uint32_t pendingMovementTuningCommand_ = 0;
  bool hasPendingMovementTuning_ = false;
  bool hasSnapshot_ = false;
};

} // namespace lg
