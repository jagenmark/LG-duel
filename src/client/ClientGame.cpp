#include "client/ClientGame.hpp"

#include "shared/Constants.hpp"
#include "shared/Sequence.hpp"
#include "sim/Combat.hpp"
#include "sim/MapRegistry.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <utility>

namespace lg {
namespace {

constexpr float kProjectileCollisionEpsilon = 0.0001F;

[[nodiscard]] std::uint32_t projectileLifetimeTicks(
  Weapon weapon,
  const ProjectilePresentationTuning& tuning
) {
  if (weapon == Weapon::GrenadeLauncher) {
    return tuning.grenadeFuseTicks;
  }
  if (weapon == Weapon::PlasmaGun) {
    return tuning.plasmaLifetimeTicks;
  }
  return tuning.rocketLifetimeTicks;
}

} // namespace

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

void ClientGame::sendKeepalive(
  std::uint32_t sequence,
  bool usePresentedServerTick
) {
  // UdpClientTransport already maintains the authenticated connection with
  // Ping/Pong while replay presentation suppresses fixed gameplay ticks.
  // Sending a default UserCommand here would overwrite live aim, weapon, and
  // movement state on the authoritative server.
  (void)sequence;
  (void)usePresentedServerTick;
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
      if (
        mapChanged ||
        !hasProjectileRevision_ ||
        received.projectileRevision != projectileRevision_
      ) {
        clearProjectiles();
        projectileRevision_ = received.projectileRevision;
        hasProjectileRevision_ = true;
      }
      snapshot_ = received;
      std::array<std::size_t, kDuelPlayerCount> explosionSlots = {};
      std::size_t explosionCount = 0;
      std::uint32_t newestExplosionSequence = 0;
      for (std::size_t slot = 0; slot < received.rocketExplosions.size(); ++slot) {
        if ((received.rocketExplosionActiveMask & (1U << slot)) != 0U) {
          explosionSlots[explosionCount++] = slot;
          if (
            newestExplosionSequence == 0U ||
            isSequenceNewer(
              received.rocketExplosions[slot].sequence,
              newestExplosionSequence
            )
          ) {
            newestExplosionSequence = received.rocketExplosions[slot].sequence;
          }
        }
      }
      std::sort(
        explosionSlots.begin(),
        explosionSlots.begin() + explosionCount,
        [&received, newestExplosionSequence](std::size_t left, std::size_t right) {
          return nonZeroSequenceDistance(
            newestExplosionSequence,
            received.rocketExplosions[left].sequence
          ) > nonZeroSequenceDistance(
            newestExplosionSequence,
            received.rocketExplosions[right].sequence
          );
        }
      );
      for (std::size_t index = 0; index < explosionCount; ++index) {
        removeExplodedProjectile(received.rocketExplosions[explosionSlots[index]]);
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
    } else if (received.serverTick == snapshot_.serverTick) {
      ++duplicateSnapshotsIgnored_;
    } else {
      ++staleSnapshotsIgnored_;
    }
  }
  receiveProjectileUpdates();
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
  diagnostics.duplicateSnapshotsIgnored = duplicateSnapshotsIgnored_;
  diagnostics.staleSnapshotsIgnored = staleSnapshotsIgnored_;
  diagnostics.snapshotDecodeMilliseconds =
    transportDiagnostics.snapshotDecodeMilliseconds;
  diagnostics.snapshotQueueDepth = transportDiagnostics.snapshotQueueDepth;
  snapshotDiagnostics_ = diagnostics;
}

void ClientGame::clearProjectiles() {
  projectiles_ = {};
  projectileSequences_ = {};
  projectileUpdateTicks_ = {};
  projectileAgesSeconds_ = {};
  projectileResting_ = {};
  projectileSlotsInitialized_ = {};
  projectileTerminal_ = {};
  explodedProjectileKeys_ = {};
  processedExplosionSequence_ = 0;
  nextExplodedProjectileKey_ = 0;
  projectileRevision_ = 0;
  hasProjectileRevision_ = false;
}

void ClientGame::removeExplodedProjectile(
  const RocketExplosionResult& explosion
) {
  if (
    !explosion.active ||
    explosion.sequence == 0U ||
    explosion.projectileSequence == 0U
  ) {
    return;
  }
  if (
    processedExplosionSequence_ != 0U &&
    !isSequenceNewer(
      explosion.sequence,
      processedExplosionSequence_
    )
  ) {
    return;
  }
  const std::size_t eventOwner = explosion.ownerPlayerIndex;
  if (eventOwner >= kMaxPlayers) {
    return;
  }
  processedExplosionSequence_ = explosion.sequence;
  const std::size_t firstSlot = eventOwner * kProjectileSlotsPerPlayer;
  const std::size_t lastSlot = firstSlot + kProjectileSlotsPerPlayer;
  for (std::size_t slot = firstSlot; slot < lastSlot; ++slot) {
    if (
      projectiles_[slot].active &&
      projectileSequences_[slot] == explosion.projectileSequence
    ) {
      projectiles_[slot] = {};
      projectileUpdateTicks_[slot] = snapshot_.serverTick;
      projectileResting_[slot] = false;
      projectileTerminal_[slot] = true;
    }
  }
  ExplodedProjectileKey& key =
    explodedProjectileKeys_[nextExplodedProjectileKey_];
  key.owner = static_cast<std::uint8_t>(eventOwner);
  key.sequence = explosion.projectileSequence;
  key.valid = true;
  nextExplodedProjectileKey_ =
    (nextExplodedProjectileKey_ + 1U) % explodedProjectileKeys_.size();
}

void ClientGame::receiveProjectileUpdates() {
  ProjectileUpdatePacket packet;
  while (transport_.receiveProjectileUpdates(packet)) {
    if (
      !hasProjectileRevision_ ||
      packet.mapRevision != mapRevision_ ||
      packet.projectileRevision != projectileRevision_
    ) {
      continue;
    }
    const std::size_t updateCount = std::min(
      static_cast<std::size_t>(packet.updateCount),
      packet.updates.size()
    );
    for (std::size_t index = 0; index < updateCount; ++index) {
      const ProjectileUpdate& update = packet.updates[index];
      if (update.slot >= projectiles_.size() || update.sequence == 0U) {
        continue;
      }
      const std::size_t slot = update.slot;
      const std::size_t owner = slot / kProjectileSlotsPerPlayer;
      const bool exploded = std::any_of(
        explodedProjectileKeys_.begin(),
        explodedProjectileKeys_.end(),
        [owner, &update](const ExplodedProjectileKey& key) {
          return
            key.valid &&
            key.owner == owner &&
            key.sequence == update.sequence;
        }
      );
      if (exploded) {
        continue;
      }
      if (projectileSlotsInitialized_[slot]) {
        if (
          update.sequence != projectileSequences_[slot] &&
          !isSequenceNewer(update.sequence, projectileSequences_[slot])
        ) {
          continue;
        }
        if (
          update.sequence == projectileSequences_[slot] &&
          packet.serverTick != projectileUpdateTicks_[slot] &&
          !isSequenceNewer(packet.serverTick, projectileUpdateTicks_[slot])
        ) {
          continue;
        }
      }
      const bool sameSequence =
        projectileSlotsInitialized_[slot] &&
        update.sequence == projectileSequences_[slot];
      if (
        update.kind != ProjectileUpdateKind::Remove &&
        sameSequence &&
        projectileTerminal_[slot]
      ) {
        continue;
      }
      projectileSlotsInitialized_[slot] = true;
      projectileSequences_[slot] = update.sequence;
      projectileUpdateTicks_[slot] = packet.serverTick;
      if (update.kind == ProjectileUpdateKind::Remove) {
        projectiles_[slot] = {};
        projectileAgesSeconds_[slot] = 0.0F;
        projectileResting_[slot] = false;
        projectileTerminal_[slot] = true;
        continue;
      }
      RocketProjectileSnapshot& projectile = projectiles_[slot];
      projectile.active = true;
      projectile.owner = static_cast<std::uint8_t>(owner);
      projectile.weapon = update.weapon;
      projectile.position = update.position;
      projectile.velocity = update.velocity;
      projectile.radius = update.radius;
      projectileAgesSeconds_[slot] =
        static_cast<float>(update.ageTicks) * kFixedTickSeconds;
      projectileResting_[slot] = update.resting;
      projectileTerminal_[slot] = false;
    }
  }
}

void ClientGame::advanceProjectiles(float elapsedSeconds) {
  if (!(elapsedSeconds > 0.0F)) {
    return;
  }
  const ProjectilePresentationTuning& tuning = snapshot_.projectilePresentation;
  for (std::size_t slot = 0; slot < projectiles_.size(); ++slot) {
    RocketProjectileSnapshot& projectile = projectiles_[slot];
    if (!projectile.active) {
      continue;
    }
    float remaining = elapsedSeconds;
    while (remaining > 0.0F && projectile.active) {
      const float step = std::min(remaining, kFixedTickSeconds);
      remaining -= step;
      projectileAgesSeconds_[slot] += step;
      const float lifetime =
        static_cast<float>(projectileLifetimeTicks(projectile.weapon, tuning)) *
        kFixedTickSeconds;
      if (projectileAgesSeconds_[slot] + 0.000001F >= lifetime) {
        projectile = {};
        projectileResting_[slot] = false;
        projectileTerminal_[slot] = true;
        break;
      }
      if (
        projectile.weapon == Weapon::GrenadeLauncher &&
        projectileResting_[slot]
      ) {
        continue;
      }
      if (projectile.weapon == Weapon::GrenadeLauncher) {
        projectile.velocity.z -= tuning.grenadeGravity * step;
      }
      const Vec3 displacement = projectile.velocity * step;
      const float distance = length(displacement);
      if (
        projectile.weapon != Weapon::GrenadeLauncher ||
        distance <= kProjectileCollisionEpsilon
      ) {
        projectile.position += displacement;
        continue;
      }
      const WorldTrace trace = traceWorld(
        arena_,
        projectile.position,
        displacement / distance,
        distance
      );
      if (
        !trace.hit ||
        trace.distance >= distance - kProjectileCollisionEpsilon
      ) {
        projectile.position += displacement;
        continue;
      }
      Vec3 normal = trace.normal;
      if (dot(projectile.velocity, normal) > 0.0F) {
        normal *= -1.0F;
      }
      const float normalVelocity = dot(projectile.velocity, normal);
      if (normalVelocity < 0.0F) {
        projectile.velocity =
          (projectile.velocity - normal * (2.0F * normalVelocity)) *
          tuning.grenadeBounceDamping;
      } else {
        projectile.velocity *= tuning.grenadeBounceDamping;
      }
      projectile.position =
        trace.end + normal * (2.0F * kProjectileCollisionEpsilon);
      if (
        normal.z > 0.5F &&
        length(projectile.velocity) <= tuning.grenadeRestSpeed
      ) {
        projectile.velocity = {};
        projectileResting_[slot] = true;
      }
    }
  }
}

const std::deque<ChatMessage>& ClientGame::chatHistory() const {
  return chatHistory_;
}

const std::array<
  RocketProjectileSnapshot,
  kMaxRocketProjectiles
>& ClientGame::projectiles() const {
  return projectiles_;
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
  advanceProjectiles(elapsedSeconds);
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
