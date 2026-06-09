#include "server/ServerGame.hpp"

#include "shared/Sequence.hpp"
#include "sim/Collision.hpp"

#include <algorithm>

namespace lg {
namespace {

constexpr std::uint32_t kRespawnDelayTicks = 250;
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
  publishSnapshot();
}

void ServerGame::tick(float fixedDt) {
  receiveCommands();
  updateRespawns();

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

    PlayerState target = combatPlayers[targetIndex];
    snapshot_.lightningGuns[attackerIndex] = simulateLightningGun(
      combatPlayers[attackerIndex],
      target,
      command,
      arena_,
      lightningGunTuning_,
      lightningGunStates_[attackerIndex],
      fixedDt
    );
  }

  for (std::size_t attackerIndex = 0; attackerIndex < kDuelPlayerCount; ++attackerIndex) {
    const std::size_t targetIndex = 1U - attackerIndex;
    PlayerState& target = snapshot_.players[targetIndex];
    const bool wasAlive = target.health > 0;
    target.health = std::max(0, target.health - snapshot_.lightningGuns[attackerIndex].damageApplied);
    target.velocity += snapshot_.lightningGuns[attackerIndex].knockbackImpulse;
    if (wasAlive && target.health == 0) {
      target.velocity = {};
      snapshot_.respawnTicksRemaining[targetIndex] = kRespawnDelayTicks;
    }
  }

  hasCommand_.fill(false);
  ++snapshot_.serverTick;
  publishSnapshot();
}

void ServerGame::resetMatch() {
  const std::uint32_t serverTick = snapshot_.serverTick;
  snapshot_ = {};
  snapshot_.serverTick = serverTick;
  snapshot_.players[0] = spawnPlayer(0);
  snapshot_.players[1] = spawnPlayer(1);
  lightningGunStates_ = {};
  commands_ = {};
  hasCommand_ = {};
}

void ServerGame::updateRespawns() {
  for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
    std::uint32_t& ticksRemaining = snapshot_.respawnTicksRemaining[playerIndex];
    if (ticksRemaining == 0) {
      continue;
    }

    --ticksRemaining;
    if (ticksRemaining == 0) {
      respawnPlayer(playerIndex);
    }
  }
}

void ServerGame::respawnPlayer(std::size_t playerIndex) {
  snapshot_.players[playerIndex] = spawnPlayer(playerIndex);
  snapshot_.lightningGuns[playerIndex] = {};
  lightningGunStates_[playerIndex] = {};
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

    commands_[playerIndex] = packet.command;
    hasCommand_[playerIndex] = true;
    snapshot_.hasAcknowledgedCommand[playerIndex] = true;
    snapshot_.acknowledgedCommand[playerIndex] = packet.command.sequence;
  }
}

void ServerGame::publishSnapshot() {
  transport_.sendSnapshot(snapshot_);
}

} // namespace lg
