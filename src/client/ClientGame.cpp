#include "client/ClientGame.hpp"

#include "shared/Constants.hpp"
#include "shared/Sequence.hpp"
#include "sim/MapRegistry.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <utility>

namespace lg {

ClientGame::ClientGame(
  NetTransport& transport,
  std::size_t localPlayerIndex,
  std::size_t commandClientIndex
) : transport_(transport),
    localPlayerIndex_(localPlayerIndex < kDuelPlayerCount ? localPlayerIndex : 0U),
    commandClientIndex_(commandClientIndex == kNoAssignedPlayer
      ? localPlayerIndex_ : commandClientIndex),
    spectator_(localPlayerIndex == kNoAssignedPlayer) {}

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
  std::int32_t knockbackTimeMs,
  float vampirism,
  std::uint8_t selfDamagePercent,
  std::int32_t healthAmount,
  const WeaponDamageTuning& weaponDamage,
  const WeaponAmmoConfig& weaponAmmo,
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
    WeaponSwitchingMode weaponSwitchingMode,
    BotCommandType botCommand,
    std::int32_t botCommandValue,
    std::int32_t botCommandMinIntervalMs,
    std::int32_t botCommandMaxIntervalMs,
    bool requestMcGuffinThrow,
    bool wantsScoreboardStats,
    bool requestSpectator
  ) {
  if (requestMovementTuning) {
    movementTuning_ = movementTuning;
    movementTuning_.maxAirSpeed = movementTuning_.maxGroundSpeed;
    pendingMovementTuningCommand_ = command.sequence;
    hasPendingMovementTuning_ = true;
  }
  const auto advanceEdge = [](std::uint32_t& edge) {
    ++edge;
    if (edge == 0U) {
      edge = 1U;
    }
  };
  if (command.jump && !previousJumpHeld_) {
    advanceEdge(actionEdges_.jump);
  }
  if (command.dash && !previousDashHeld_) {
    advanceEdge(actionEdges_.dash);
  }
  if (command.attack && !previousAttackHeld_) {
    advanceEdge(actionEdges_.attack);
    actionEdges_.attackYawRadians = command.viewYawRadians;
    actionEdges_.attackPitchRadians = command.viewPitchRadians;
    actionEdges_.attackViewedServerTick = usePresentedServerTick
      ? interpolation_.presentationServerTick()
      : snapshot_.serverTick;
    actionEdges_.attackWeapon = command.weapon;
  }
  previousAttackHeld_ = command.attack;
  previousJumpHeld_ = command.jump;
  previousDashHeld_ = command.dash;
  if (requestReset) {
    advanceEdge(actionEdges_.reset);
  }
  if (toggleReady) {
    advanceEdge(actionEdges_.ready);
  }
  if (requestMcGuffinThrow) {
    advanceEdge(actionEdges_.mcguffinThrow);
    actionEdges_.mcguffinThrowYawRadians = command.viewYawRadians;
    actionEdges_.mcguffinThrowPitchRadians = command.viewPitchRadians;
  }
  transport_.sendCommand(
    CommandPacket{
      static_cast<std::uint8_t>(commandClientIndex_),
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
      weaponAmmo,
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
      0,
      knockbackTimeMs,
      botCommand,
      botCommandValue,
      botCommandMinIntervalMs,
      botCommandMaxIntervalMs,
      requestMcGuffinThrow,
      wantsScoreboardStats,
      0,
      requestSpectator,
      actionEdges_,
    }
  );
  if (!requestReset && !spectator_) {
    prediction_.predict(
      command,
      arena_,
      movementTuning_,
      snapshot_.icePools,
      icePoolTuning_,
      kFixedTickSeconds
    );
  }
}

void ClientGame::receiveSnapshots() {
  auto receivedStorage = std::make_unique<ServerSnapshot>();
  ServerSnapshot& received = *receivedStorage;
  SnapshotDiagnostics diagnostics = transport_.snapshotDiagnostics();
  diagnostics.snapshotsApplied = 0;
  diagnostics.snapshotApplyMilliseconds = 0.0F;
  while (connectionError_.empty() && transport_.receiveSnapshot(received)) {
    if (!hasSnapshot_ || received.serverTick > snapshot_.serverTick) {
      const auto applyStart = std::chrono::steady_clock::now();
      const bool mapChanged =
        received.mapRevision != mapRevision_ ||
        received.map.mapName != map_.mapName ||
        received.map.contentHash != map_.contentHash;
      if (mapChanged) {
        LocalMapLoadResult loaded;
        const Arena builtInArena = makeDefaultServerArena();
        if (
          received.map.mapName == "custom" &&
          received.map.contentHash == hashArena(builtInArena)
        ) {
          loaded.arena = builtInArena;
          loaded.descriptor = received.map;
          loaded.ok = true;
        } else {
          loaded = loadAndVerifyLocalMap(received.map);
        }
        if (!loaded.ok) {
          connectionError_ = loaded.error;
          continue;
        }
        arena_ = loaded.arena;
        map_ = loaded.descriptor;
        mapRevision_ = received.mapRevision;
      }
      snapshot_ = received;
      if (received.hasLocalClientState) {
        spectator_ = received.localSpectator;
        if (!spectator_) localPlayerIndex_ = received.localPlayerIndex;
      }
      if (
        !spectator_ &&
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
      icePoolTuning_ = received.icePoolTuning;
      hasSnapshot_ = true;
      interpolation_.push(received);
      if (!spectator_) {
        prediction_.reconcile(
          received.players[localPlayerIndex_],
          received.hasAcknowledgedCommand[localPlayerIndex_],
          received.acknowledgedCommand[localPlayerIndex_],
          arena_,
          movementTuning_,
          received.icePools,
          icePoolTuning_,
          kFixedTickSeconds
        );
      }
      diagnostics.snapshotApplyMilliseconds +=
        std::chrono::duration<float, std::milli>(
          std::chrono::steady_clock::now() - applyStart
        ).count();
      ++diagnostics.snapshotsApplied;
    }
  }
  ChatHistoryChunk chatChunk;
  while (transport_.receiveChatHistory(chatChunk)) {
    while (
      !chatHistory_.empty() &&
      isSequenceNewer(
        chatChunk.oldestAvailableSequence,
        chatHistory_.front().sequence
      )
    ) {
      chatHistory_.pop_front();
    }
    for (std::size_t index = 0; index < chatChunk.messageCount; ++index) {
      const ChatMessage& message = chatChunk.messages[index];
      const bool alreadyPresent = std::any_of(
        chatHistory_.begin(),
        chatHistory_.end(),
        [&message](const ChatMessage& existing) {
          return existing.sequence == message.sequence;
        }
      );
      if (
        !alreadyPresent &&
        (chatHistory_.empty() ||
         isSequenceNewer(message.sequence, chatHistory_.back().sequence))
      ) {
        chatHistory_.push_back(message);
      }
    }
    while (chatHistory_.size() > kChatHistoryCapacity) {
      chatHistory_.pop_front();
    }
  }
  const SnapshotDiagnostics transportDiagnostics =
    transport_.snapshotDiagnostics();
  diagnostics.snapshotPacketsDecoded =
    transportDiagnostics.snapshotPacketsDecoded - lastSnapshotPacketsDecoded_;
  lastSnapshotPacketsDecoded_ = transportDiagnostics.snapshotPacketsDecoded;
  diagnostics.snapshotDecodeMilliseconds =
    transportDiagnostics.snapshotDecodeMilliseconds;
  diagnostics.snapshotQueueDepth = transportDiagnostics.snapshotQueueDepth;
  snapshotDiagnostics_ = diagnostics;
}

const std::deque<ChatMessage>& ClientGame::chatHistory() const {
  return chatHistory_;
}

void ClientGame::advanceInterpolation(
  float elapsedSeconds,
  float interpolationDelaySeconds,
  bool adaptive,
  float minimumDelaySeconds,
  float maximumDelaySeconds,
  float maximumExtrapolationSeconds
) {
  if (adaptive) {
    interpolation_.advanceAdaptive(
      elapsedSeconds,
      interpolationDelaySeconds,
      transport_.networkTelemetry().snapshotJitterMilliseconds / 1000.0F,
      minimumDelaySeconds,
      maximumDelaySeconds,
      maximumExtrapolationSeconds
    );
  } else {
    interpolation_.advance(elapsedSeconds, interpolationDelaySeconds);
  }
}

const InterpolationDiagnostics& ClientGame::interpolationDiagnostics() const {
  return interpolation_.diagnostics();
}

bool ClientGame::hasSnapshot() const {
  return hasSnapshot_;
}

const ServerSnapshot& ClientGame::snapshot() const {
  return snapshot_;
}

bool ClientGame::hasAcknowledgedCommand() const {
  return !spectator_ && snapshot_.hasAcknowledgedCommand[localPlayerIndex_];
}

bool ClientGame::hasPendingMovementTuning() const {
  return hasPendingMovementTuning_;
}

std::uint32_t ClientGame::lastAcknowledgedCommand() const {
  return spectator_ ? 0U : snapshot_.acknowledgedCommand[localPlayerIndex_];
}

const PlayerState& ClientGame::predictedPlayer() const {
  return prediction_.player();
}

std::size_t ClientGame::localPlayerIndex() const {
  return localPlayerIndex_;
}

bool ClientGame::spectator() const {
  return spectator_;
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

SnapshotDiagnostics ClientGame::snapshotDiagnostics() const {
  return snapshotDiagnostics_;
}

bool ClientGame::hasConnectionError() const {
  return !connectionError_.empty();
}

const std::string& ClientGame::connectionError() const {
  return connectionError_;
}

} // namespace lg
