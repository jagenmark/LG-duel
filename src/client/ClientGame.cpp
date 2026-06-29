#include "client/ClientGame.hpp"

#include "shared/Constants.hpp"
#include "shared/Sequence.hpp"

#include <cstdint>
#include <iostream>
#include <memory>
#include <utility>

namespace lg {
namespace {

[[nodiscard]] std::size_t textureProjectionCount(const Arena& arena) {
  std::size_t count = 0;
  for (std::size_t wallIndex = 0; wallIndex < arena.wallCount; ++wallIndex) {
    for (const TextureProjection& projection : arena.walls[wallIndex].faceTextureProjections) {
      if (projection.valid) {
        ++count;
      }
    }
  }
  for (std::size_t brushIndex = 0; brushIndex < arena.brushCount; ++brushIndex) {
    const ArenaBrush& brush = arena.brushes[brushIndex];
    for (std::uint8_t faceIndex = 0; faceIndex < brush.faceCount; ++faceIndex) {
      if (brush.faces[faceIndex].textureProjection.valid) {
        ++count;
      }
    }
  }
  return count;
}

} // namespace

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
  while (transport_.receiveSnapshot(received)) {
    if (!hasSnapshot_ || received.serverTick > snapshot_.serverTick) {
      if (received.mapRevision != mapRevision_ && !received.hasArena) {
        continue;
      }
      if (received.hasArena) {
        arena_ = received.arena;
        mapRevision_ = received.mapRevision;
        std::cerr
          << "LG_DUEL_TEXTURE_PIPELINE_V2 client received arena revision="
          << mapRevision_
          << " walls=" << arena_.wallCount
          << " brushes=" << arena_.brushCount
          << " projectedFaces=" << textureProjectionCount(arena_)
          << '\n';
      }
      received.hasArena = false;
      received.arena = {};
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

} // namespace lg
