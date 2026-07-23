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
    // Predict with the requested tuning immediately, but keep it pending until
    // the server acknowledges this command so older snapshots cannot revert it.
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
    actionEdges_.attackZoomed = command.zoomed;
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
  CommandPacket packet;
  packet.playerIndex = spectator_
    ? kNoAssignedPlayer
    : static_cast<std::uint8_t>(localPlayerIndex_);
  packet.command = command;
  packet.requestReset = requestReset;
  packet.toggleReady = toggleReady;
  // Lag-compensated traces must rewind to what the player was shown, not
  // necessarily the newest snapshot already buffered by the client.
  packet.viewedServerTick = usePresentedServerTick
    ? interpolation_.presentationServerTick()
    : snapshot_.serverTick;
  packet.requestMovementTuning = requestMovementTuning;
  packet.movementTuning = movementTuning_;
  packet.playerSizeScaleXY = playerSizeScaleXY;
  packet.playerSizeScaleZ = playerSizeScaleZ;
  packet.lightningKnockback = lightningKnockback;
  packet.lightningFireHz = lightningFireHz;
  packet.rocketKnockback = rocketKnockback;
  packet.weaponDamage = weaponDamage;
  packet.weaponAmmo = weaponAmmo;
  packet.vampirism = vampirism;
  packet.chatMessage = std::move(chatMessage);
  packet.playerName = std::move(playerName);
  packet.mapName = std::move(mapName);
  packet.selfDamagePercent = selfDamagePercent;
  packet.healthAmount = healthAmount;
  packet.botDodgeEnabled = botDodgeEnabled;
  packet.botDodgeMinIntervalMs = botDodgeMinIntervalMs;
  packet.botDodgeMaxIntervalMs = botDodgeMaxIntervalMs;
  packet.requestGameMode = requestGameMode;
  packet.requestedGameMode = requestedGameMode;
  packet.requestTeam = requestTeam;
  packet.requestedTeam = requestedTeam;
  packet.weaponSwitchingMode = weaponSwitchingMode;
  packet.knockbackTimeMs = knockbackTimeMs;
  packet.botCommand = botCommand;
  packet.botCommandValue = botCommandValue;
  packet.botCommandMinIntervalMs = botCommandMinIntervalMs;
  packet.botCommandMaxIntervalMs = botCommandMaxIntervalMs;
  packet.requestMcGuffinThrow = requestMcGuffinThrow;
  packet.wantsScoreboardStats = wantsScoreboardStats;
  packet.requestSpectator = requestSpectator;
  packet.actionEdges = actionEdges_;
  transport_.sendCommand(packet);
  if (!spectator_ && requestReset && prediction_.initialized()) {
    // Reset replaces the local movement timeline; commands sampled against the
    // previous match state must never be replayed into the reset state.
    prediction_.initialize(prediction_.player());
  }
  if (!spectator_ && !requestReset) {
    // Reset replaces authoritative match state, so predicting the accompanying
    // movement command would create state the server deliberately discards.
    PlayerCollisionProxySet collisionProxies;
    const SnapshotInterpolation::PlayerCollisionSample localSample =
      interpolation_.collisionSample(localPlayerIndex_);
    collisionProxies.presentationServerTick = localSample.discreteServerTick;
    collisionProxies.mapRevision = localSample.mapRevision;
    for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
      if (index == localPlayerIndex_) {
        continue;
      }
      const SnapshotInterpolation::PlayerCollisionSample sample =
        interpolation_.collisionSample(index);
      if (!sample.eligible) {
        continue;
      }
      PlayerCollisionProxy& proxy =
        collisionProxies.proxies[collisionProxies.count++];
      proxy.playerIndex = static_cast<std::uint8_t>(index);
      proxy.position = sample.pose.position;
      proxy.bounds = sample.pose.bounds;
    }
    prediction_.predict(
      command,
      arena_,
      movementTuning_,
      snapshot_.icePools,
      icePoolTuning_,
      kFixedTickSeconds,
      collisionProxies,
      static_cast<std::uint8_t>(localPlayerIndex_)
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
    // Snapshot ticks are authoritative and monotonic; late or duplicated UDP
    // snapshots must not rewind prediction, interpolation, or map state.
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
        // A map revision is a new authoritative presentation timeline. Clear
        // old remote poses before accepting any state from the new map.
        interpolation_.reset();
      }
      const bool wasSpectator = spectator_;
      const std::size_t previousPlayerIndex = localPlayerIndex_;
      if (received.hasLocalClientState) {
        spectator_ = received.localSpectator;
        if (spectator_ && !wasSpectator) {
          // Releasing a body also releases its prediction history. A later
          // assignment starts from the new authoritative respawn state.
          prediction_.reset();
          hasPendingMovementTuning_ = false;
        } else if (!spectator_) {
          localPlayerIndex_ = received.localPlayerIndex;
        }
      }
      // A newly assigned body, respawn, map replacement or teleport starts a
      // fresh prediction timeline. Never compare or replay a spectator's
      // connection slot as though it were a player-body index.
      const bool localTimelineDiscontinuity = !spectator_ && (
        mapChanged || wasSpectator || previousPlayerIndex != localPlayerIndex_ ||
        (hasSnapshot_ && snapshot_.players[localPlayerIndex_].health <= 0 &&
         received.players[localPlayerIndex_].health > 0) ||
        (hasSnapshot_ && length(
          received.players[localPlayerIndex_].position -
          snapshot_.players[localPlayerIndex_].position
        ) > 32.0F)
      );
      snapshot_ = received;
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
        // Once the tuning request is acknowledged, replicated server values
        // again become the source of truth for prediction and reconciliation.
        movementTuning_ = received.movementTuning;
        movementTuning_.maxAirSpeed = movementTuning_.maxGroundSpeed;
      }
      icePoolTuning_ = received.icePoolTuning;
      hasSnapshot_ = true;
      // Buffer remote presentation first, then rebuild the local predicted state
      // from the same authoritative snapshot and its unacknowledged commands.
      interpolation_.push(received);
      if (!spectator_) {
        if (localTimelineDiscontinuity) {
          prediction_.initialize(received.players[localPlayerIndex_]);
        }
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

std::size_t ClientGame::localClientIndex() const {
  return commandClientIndex_;
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

SnapshotInterpolation::Diagnostics ClientGame::interpolationDiagnostics() const {
  return interpolation_.diagnostics();
}

SnapshotInterpolation::PlayerCollisionSample
ClientGame::interpolationCollisionSample(std::size_t playerIndex) const {
  return playerIndex < kDuelPlayerCount
    ? interpolation_.collisionSample(playerIndex)
    : SnapshotInterpolation::PlayerCollisionSample{};
}

bool ClientGame::hasConnectionError() const {
  return !connectionError_.empty();
}

const std::string& ClientGame::connectionError() const {
  return connectionError_;
}

} // namespace lg
