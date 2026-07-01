#include "client/ClientGame.hpp"

#include "shared/Constants.hpp"
#include "shared/Sequence.hpp"
#include "sim/MapRegistry.hpp"

#include <memory>
#include <string>
#include <utility>

namespace lg {

ClientGame::ClientGame(NetTransport& transport, std::size_t localPlayerIndex)
  : transport_(transport), localPlayerIndex_(localPlayerIndex) {
  map_ = describeMap("thunderstruck", arena_);
}

void ClientGame::sendCommand(
  const UserCommand& command,
  bool requestReset,
  bool toggleReady,
  bool requestMovementTuning,
  const MovementTuning& movementTuning,
  float playerSizeScaleXY,
  float playerSizeScaleZ,
  float lightningKnockback,
  float rocketKnockback,
  float vampirism,
  std::uint8_t selfDamagePercent,
  std::int32_t healthAmount,
  const WeaponDamageTuning& weaponDamage,
  float lightningFireHz,
  bool botDodgeEnabled,
  std::int32_t botDodgeMinIntervalMs,
    std::int32_t botDodgeMaxIntervalMs,
    std::string chatMessage,
    std::string playerName,
    std::string mapName,
    bool usePresentedServerTick,
    bool requestGameMode,
    GameMode requestedGameMode,
    bool requestTeam,
    Team requestedTeam,
    WeaponSwitchingMode weaponSwitchingMode
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
      usePresentedServerTick ? interpolation_.presentationServerTick() : snapshot_.serverTick,
      requestMovementTuning,
      movementTuning_,
      playerSizeScaleXY,
      playerSizeScaleZ,
      lightningKnockback,
      lightningFireHz,
      rocketKnockback,
      weaponDamage,
      vampirism,
      std::move(chatMessage),
      std::move(playerName),
      std::move(mapName),
      selfDamagePercent,
      healthAmount,
      botDodgeEnabled,
      botDodgeMinIntervalMs,
      botDodgeMaxIntervalMs,
      requestGameMode,
      requestedGameMode,
      requestTeam,
      requestedTeam,
      weaponSwitchingMode,
    }
  );
  if (!requestReset) {
    prediction_.predict(command, arena_, movementTuning_, kFixedTickSeconds);
  }
}

void ClientGame::receiveSnapshots() {
  auto receivedStorage = std::make_unique<ServerSnapshot>();
  ServerSnapshot& received = *receivedStorage;
  while (connectionError_.empty() && transport_.receiveSnapshot(received)) {
    if (received.map.contentHash == 0 && received.map.mapName == map_.mapName) {
      received.map = map_;
    }
    if (!hasSnapshot_ || received.serverTick > snapshot_.serverTick) {
      const bool mapChanged =
        received.mapRevision != mapRevision_ ||
        received.map.mapName != map_.mapName ||
        received.map.contentHash != map_.contentHash;
      if (mapChanged) {
        LocalMapLoadResult loaded = loadAndVerifyLocalMap(received.map);
        if (!loaded.ok) {
          connectionError_ = loaded.error;
          continue;
        }
        arena_ = loaded.arena;
        map_ = loaded.descriptor;
        mapRevision_ = received.mapRevision;
      }
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

void ClientGame::advanceInterpolation(
  float elapsedSeconds,
  float interpolationDelaySeconds
) {
  interpolation_.advance(elapsedSeconds, interpolationDelaySeconds);
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

PlayerState ClientGame::interpolatedPlayer(std::size_t playerIndex, float alpha) const {
  return interpolation_.player(playerIndex, alpha);
}

const PredictionDiagnostics& ClientGame::predictionDiagnostics() const {
  return prediction_.diagnostics();
}

const MovementTuning& ClientGame::movementTuning() const {
  return movementTuning_;
}

const Arena& ClientGame::arena() const {
  return arena_;
}

bool ClientGame::hasConnectionError() const {
  return !connectionError_.empty();
}

const std::string& ClientGame::connectionError() const {
  return connectionError_;
}

} // namespace lg
