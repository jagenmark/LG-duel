#include "client/ClientGame.hpp"
#include "client/Interpolation.hpp"
#include "client/Prediction.hpp"
#include "net/LoopbackTransport.hpp"
#include "shared/Constants.hpp"
#include "sim/Arena.hpp"
#include "sim/MapRegistry.hpp"
#include "sim/Movement.hpp"
#include "sim/MovementModes.hpp"
#include "sim/PlayerState.hpp"
#include "sim/UserCommand.hpp"

#include <cmath>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

namespace {

constexpr float kPi = 3.14159265359F;

int expect(bool condition, std::string_view message) {
  if (condition) {
    return 0;
  }

  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

bool nearlyEqual(float lhs, float rhs, float epsilon = 0.0001F) {
  return std::fabs(lhs - rhs) <= epsilon;
}

lg::SnapshotInterpolation::Clock::time_point interpolationTime(double seconds) {
  return lg::SnapshotInterpolation::Clock::time_point{} +
    std::chrono::duration_cast<lg::SnapshotInterpolation::Clock::duration>(
      std::chrono::duration<double>(seconds)
    );
}

class TestSnapshotInterpolation : public lg::SnapshotInterpolation {
public:
  void push(const lg::ServerSnapshot& snapshot) {
    pushAt(snapshot, interpolationTime(nowSeconds_));
  }

  void advance(
    float elapsedSeconds,
    float interpolationDelaySeconds = lg::kDefaultSnapshotInterpolationDelaySeconds
  ) {
    nowSeconds_ += static_cast<double>(elapsedSeconds);
    advanceTo(interpolationTime(nowSeconds_), interpolationDelaySeconds);
  }

  void elapseWithoutAdvance(double elapsedSeconds) {
    nowSeconds_ += elapsedSeconds;
  }

  void advanceAdaptive(
    float elapsedSeconds,
    float baseDelaySeconds,
    float observedJitterSeconds,
    float minimumDelaySeconds,
    float maximumDelaySeconds,
    float maximumExtrapolationSeconds
  ) {
    nowSeconds_ += static_cast<double>(elapsedSeconds);
    advanceAdaptiveTo(
      interpolationTime(nowSeconds_), elapsedSeconds, baseDelaySeconds,
      observedJitterSeconds, minimumDelaySeconds, maximumDelaySeconds,
      maximumExtrapolationSeconds
    );
  }

  void reset() {
    lg::SnapshotInterpolation::reset();
    nowSeconds_ = 0.0;
  }

private:
  double nowSeconds_ = 0.0;
};

lg::PlayerState groundedPlayer() {
  lg::PlayerState player;
  player.position = {-3.0F, 0.0F, player.bounds.halfHeight};
  player.onGround = true;
  player.movementMode = lg::MovementMode::Grounded;
  return player;
}

const lg::LocalMapLoadResult& testMap() {
  static const lg::LocalMapLoadResult loaded = lg::loadLocalMap("eyetoeye");
  return loaded;
}

void queueSnapshot(lg::LoopbackTransport& transport, lg::ServerSnapshot snapshot) {
  if (snapshot.map.mapName.empty() || snapshot.map.contentHash == 0) {
    snapshot.map = testMap().descriptor;
  }
  if (!snapshot.hasLocalClientState) {
    snapshot.localPlayerIndex = 0;
  }
  transport.sendSnapshot(snapshot);
}

} // namespace

int main() {
  int failures = 0;
  const lg::Arena arena;
  const lg::MovementTuning tuning;

  {
    lg::LoopbackTransport transport;
    lg::ClientGame client(transport, 0);
    lg::UserCommand command;
    command.sequence = 1;
    command.attack = true;
    command.viewYawRadians = 0.75F;
    command.viewPitchRadians = -0.25F;
    command.weapon = lg::Weapon::Railgun;
    client.sendCommand(command, false);
    lg::CommandPacket firstPress;
    failures += expect(
      transport.receiveCommand(firstPress) &&
        firstPress.actionEdges.attack == 1U &&
        nearlyEqual(firstPress.actionEdges.attackYawRadians, 0.75F) &&
        firstPress.actionEdges.attackWeapon == lg::Weapon::Railgun,
      "attack presses should carry a cumulative edge with original aim"
    );

    command.sequence = 2;
    client.sendCommand(command, false);
    lg::CommandPacket held;
    failures += expect(
      transport.receiveCommand(held) && held.actionEdges.attack == 1U,
      "holding attack should not create duplicate action edges"
    );
    command.sequence = 3;
    command.attack = false;
    client.sendCommand(command, false);
    lg::CommandPacket release;
    (void)transport.receiveCommand(release);
    command.sequence = 4;
    command.attack = true;
    client.sendCommand(command, false);
    lg::CommandPacket secondPress;
    failures += expect(
      transport.receiveCommand(secondPress) &&
        secondPress.actionEdges.attack == 2U,
        "a later attack press should advance the cumulative edge"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ClientGame client(transport, 0);
    lg::ServerSnapshot snapshot;
    snapshot.serverTick = 10U;
    snapshot.players[0] = groundedPlayer();
    queueSnapshot(transport, snapshot);
    client.receiveSnapshots();

    lg::UserCommand command;
    command.sequence = 1U;
    command.forwardMove = 1.0F;
    client.sendCommand(command, false);
    lg::CommandPacket predictedCommand;
    (void)transport.receiveCommand(predictedCommand);
    const lg::PlayerState beforeKeepalive = client.predictedPlayer();
    const std::size_t pendingBefore =
      client.predictionDiagnostics().pendingCommandCount;

    client.sendKeepalive(2U);
    lg::CommandPacket keepalive;
    failures += expect(
      transport.receiveCommand(keepalive) &&
        keepalive.command.sequence == 2U &&
        keepalive.command.forwardMove == 0.0F &&
        !keepalive.command.attack && keepalive.actionEdges.jump == 0U &&
        keepalive.actionEdges.dash == 0U && keepalive.actionEdges.attack == 0U,
      "keepalive should send only a neutral command packet"
    );
    const lg::PlayerState afterKeepalive = client.predictedPlayer();
    failures += expect(
      afterKeepalive.position.x == beforeKeepalive.position.x &&
        afterKeepalive.position.y == beforeKeepalive.position.y &&
        afterKeepalive.position.z == beforeKeepalive.position.z &&
        afterKeepalive.velocity.x == beforeKeepalive.velocity.x &&
        afterKeepalive.velocity.y == beforeKeepalive.velocity.y &&
        afterKeepalive.velocity.z == beforeKeepalive.velocity.z &&
        client.predictionDiagnostics().pendingCommandCount == pendingBefore,
      "keepalive should not change live prediction state"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ClientGame client(transport, 0);
    lg::ServerSnapshot current;
    current.serverTick = 10;
    current.players[0] = groundedPlayer();
    queueSnapshot(transport, current);
    client.receiveSnapshots();

    queueSnapshot(transport, current);
    lg::ServerSnapshot stale = current;
    stale.serverTick = 9;
    queueSnapshot(transport, stale);
    client.receiveSnapshots();
    const lg::SnapshotDiagnostics diagnostics = client.snapshotDiagnostics();
    failures += expect(
      diagnostics.duplicateSnapshotsIgnored == 1U &&
        diagnostics.staleSnapshotsIgnored == 1U,
      "client diagnostics should count ignored duplicate and stale snapshots"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ClientGame client(transport, 0);
    lg::ServerSnapshot snapshot;
    snapshot.serverTick = 10;
    snapshot.projectileRevision = 4;
    snapshot.players[0] = groundedPlayer();
    snapshot.projectilePresentation.plasmaLifetimeTicks = 3;
    snapshot.projectilePresentation.grenadeGravity = 0.0F;
    snapshot.projectilePresentation.grenadeBounceDamping = 0.5F;
    snapshot.projectilePresentation.grenadeRestSpeed = 0.0F;
    queueSnapshot(transport, snapshot);

    lg::ProjectileUpdatePacket updatePacket;
    updatePacket.serverTick = 10;
    updatePacket.mapRevision = snapshot.mapRevision;
    updatePacket.projectileRevision = 4;
    updatePacket.updateCount = 3;
    updatePacket.updates[0].slot =
      static_cast<std::uint16_t>(lg::kProjectileSlotsPerPlayer + 1U);
    updatePacket.updates[0].sequence = 7;
    updatePacket.updates[0].weapon = lg::Weapon::RocketLauncher;
    updatePacket.updates[0].position = {1.0F, 0.0F, 1.0F};
    updatePacket.updates[0].velocity = {10.0F, 0.0F, 0.0F};
    updatePacket.updates[1].slot = 2;
    updatePacket.updates[1].sequence = 8;
    updatePacket.updates[1].weapon = lg::Weapon::PlasmaGun;
    updatePacket.updates[1].position = {0.0F, 0.0F, 1.0F};
    updatePacket.updates[1].ageTicks = 2;
    updatePacket.updates[2].slot = 4;
    updatePacket.updates[2].sequence = 9;
    updatePacket.updates[2].weapon = lg::Weapon::GrenadeLauncher;
    updatePacket.updates[2].position = {
      0.0F,
      0.0F,
      testMap().arena.min.z + 0.001F,
    };
    updatePacket.updates[2].velocity = {0.0F, 0.0F, -1.0F};
    transport.sendProjectileUpdates(updatePacket);
    client.receiveSnapshots();

    const std::size_t rocketSlot = lg::kProjectileSlotsPerPlayer + 1U;
    failures += expect(
      client.projectiles()[rocketSlot].active &&
        client.projectiles()[rocketSlot].owner == 1U,
      "projectile slots should map to their reserved player owner"
    );
    client.advanceInterpolation(lg::kFixedTickSeconds, 0.024F);
    failures += expect(
      client.projectiles()[rocketSlot].position.x > 1.0F,
      "rocket presentation should advance between server updates"
    );
    failures += expect(
      !client.projectiles()[2].active,
      "projectile presentation should expire at its configured lifetime"
    );
    lg::ProjectileUpdatePacket expiredCorrection = updatePacket;
    expiredCorrection.serverTick = 11;
    expiredCorrection.updateCount = 1;
    expiredCorrection.updates[0] = updatePacket.updates[1];
    expiredCorrection.updates[0].kind = lg::ProjectileUpdateKind::Correct;
    expiredCorrection.updates[0].ageTicks = 1;
    transport.sendProjectileUpdates(expiredCorrection);
    client.receiveSnapshots();
    failures += expect(
      !client.projectiles()[2].active,
      "a delayed correction should not restore a locally expired projectile"
    );
    failures += expect(
      client.projectiles()[4].velocity.z > 0.0F,
      "grenade presentation should bounce against arena collision"
    );

    lg::ProjectileUpdatePacket correction = updatePacket;
    correction.serverTick = 12;
    correction.updateCount = 1;
    correction.updates[0].position.x = 5.0F;
    transport.sendProjectileUpdates(correction);
    lg::ProjectileUpdatePacket stale = correction;
    stale.serverTick = 11;
    stale.updates[0].position.x = -5.0F;
    transport.sendProjectileUpdates(stale);
    client.receiveSnapshots();
    failures += expect(
      nearlyEqual(client.projectiles()[rocketSlot].position.x, 5.0F),
      "a retained older projectile update should not undo a newer correction"
    );

    lg::ProjectileUpdatePacket wrapped = correction;
    wrapped.serverTick = UINT32_MAX - 1U;
    wrapped.updates[0].slot = 5;
    wrapped.updates[0].sequence = 10;
    wrapped.updates[0].position.x = 1.0F;
    transport.sendProjectileUpdates(wrapped);
    wrapped.serverTick = 1;
    wrapped.updates[0].position.x = 2.0F;
    transport.sendProjectileUpdates(wrapped);
    wrapped.serverTick = UINT32_MAX;
    wrapped.updates[0].position.x = -2.0F;
    transport.sendProjectileUpdates(wrapped);
    client.receiveSnapshots();
    failures += expect(
      nearlyEqual(client.projectiles()[5].position.x, 2.0F),
      "projectile correction ticks should compare safely across wrap"
    );

    lg::ProjectileUpdatePacket newRevision = correction;
    newRevision.serverTick = 13;
    newRevision.projectileRevision = 5;
    newRevision.updates[0].slot = 3;
    newRevision.updates[0].sequence = 9;
    transport.sendProjectileUpdates(newRevision);
    client.receiveSnapshots();
    failures += expect(
      client.projectiles()[rocketSlot].active &&
        !client.projectiles()[3].active,
      "a projectile packet must not reset state before its snapshot generation"
    );

    snapshot.serverTick = 13;
    snapshot.projectileRevision = 5;
    queueSnapshot(transport, snapshot);
    client.receiveSnapshots();
    failures += expect(
      !client.projectiles()[rocketSlot].active,
      "a snapshot projectile revision should clear the old presentation timeline"
    );

    correction.serverTick = 14;
    transport.sendProjectileUpdates(correction);
    newRevision.serverTick = 14;
    transport.sendProjectileUpdates(newRevision);
    client.receiveSnapshots();
    failures += expect(
      !client.projectiles()[rocketSlot].active &&
        client.projectiles()[3].active,
      "packets from an older generation should stay rejected after reset"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ClientGame client(transport, 0);
    lg::ServerSnapshot snapshot;
    snapshot.serverTick = 20;
    snapshot.projectileRevision = 2;
    snapshot.players[0] = groundedPlayer();
    queueSnapshot(transport, snapshot);

    lg::ProjectileUpdatePacket packet;
    packet.serverTick = 20;
    packet.mapRevision = snapshot.mapRevision;
    packet.projectileRevision = 2;
    packet.updateCount = 1;
    packet.updates[0].slot =
      static_cast<std::uint16_t>(2U * lg::kProjectileSlotsPerPlayer);
    packet.updates[0].sequence = 44;
    packet.updates[0].kind = lg::ProjectileUpdateKind::Spawn;
    transport.sendProjectileUpdates(packet);
    client.receiveSnapshots();

    lg::ProjectileUpdatePacket remove = packet;
    remove.serverTick = 22;
    remove.updates[0].kind = lg::ProjectileUpdateKind::Remove;
    transport.sendProjectileUpdates(remove);
    lg::ProjectileUpdatePacket olderCorrection = packet;
    olderCorrection.serverTick = 21;
    olderCorrection.updates[0].kind = lg::ProjectileUpdateKind::Correct;
    olderCorrection.updates[0].position.x = -10.0F;
    transport.sendProjectileUpdates(olderCorrection);
    client.receiveSnapshots();
    const std::size_t slot = 2U * lg::kProjectileSlotsPerPlayer;
    failures += expect(
      !client.projectiles()[slot].active,
      "an explicit remove should win over a reordered older correction"
    );

    olderCorrection.serverTick = 23;
    transport.sendProjectileUpdates(olderCorrection);
    client.receiveSnapshots();
    failures += expect(
      !client.projectiles()[slot].active,
      "a terminal record should reject later corrections for the same launch"
    );

    snapshot.serverTick = 24;
    snapshot.projectileRevision = 3;
    queueSnapshot(transport, snapshot);
    client.receiveSnapshots();
    packet.serverTick = 24;
    packet.projectileRevision = 3;
    transport.sendProjectileUpdates(packet);
    client.receiveSnapshots();
    failures += expect(
      client.projectiles()[slot].active,
      "a new projectile generation should clear prior terminal records"
    );

    packet.serverTick = 25;
    packet.updates[0].sequence = 45;
    transport.sendProjectileUpdates(packet);
    client.receiveSnapshots();
    failures += expect(
      client.projectiles()[slot].active,
      "a newer launch sequence should replace a terminal slot"
    );

    snapshot.serverTick = 26;
    snapshot.rocketExplosions[2].active = true;
    snapshot.rocketExplosions[2].sequence = 1;
    snapshot.rocketExplosions[2].projectileSequence = 45;
    queueSnapshot(transport, snapshot);
    client.receiveSnapshots();
    failures += expect(
      !client.projectiles()[slot].active,
      "an explosion should remove the exact owner and launch sequence"
    );

    packet.serverTick = 27;
    packet.updates[0].kind = lg::ProjectileUpdateKind::Correct;
    transport.sendProjectileUpdates(packet);
    client.receiveSnapshots();
    failures += expect(
      !client.projectiles()[slot].active,
      "a late update should not restore an exploded projectile"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ClientGame client(transport, 0);
    lg::ServerSnapshot initialSnapshot;
    initialSnapshot.players[0] = groundedPlayer();
    queueSnapshot(transport, initialSnapshot);
    client.receiveSnapshots();

    lg::UserCommand command;
    command.sequence = 7;
    command.forwardMove = 1.0F;
    client.sendCommand(command, false);
    const lg::PlayerState immediatePrediction = client.predictedPlayer();
    failures += expect(
      immediatePrediction.position.x > initialSnapshot.players[0].position.x,
      "ClientGame should predict before authoritative acknowledgement"
    );

    lg::ServerSnapshot delayedSnapshot = initialSnapshot;
    delayedSnapshot.serverTick = 1;
    queueSnapshot(transport, delayedSnapshot);
    client.receiveSnapshots();
    failures += expect(
      nearlyEqual(client.predictedPlayer().position.x, immediatePrediction.position.x),
      "unacknowledged command should replay over delayed snapshot"
    );
    failures += expect(
      client.predictionDiagnostics().pendingCommandCount == 1,
      "delayed acknowledgement should leave command pending"
    );

    lg::ServerSnapshot acknowledgedSnapshot = delayedSnapshot;
    acknowledgedSnapshot.serverTick = 2;
    acknowledgedSnapshot.hasAcknowledgedCommand[0] = true;
    acknowledgedSnapshot.acknowledgedCommand[0] = command.sequence;
    lg::simulateMovement(
      acknowledgedSnapshot.players[0],
      command,
      arena,
      tuning,
      lg::kFixedTickSeconds
    );
    queueSnapshot(transport, acknowledgedSnapshot);
    client.receiveSnapshots();

    failures += expect(
      client.predictionDiagnostics().pendingCommandCount == 0,
      "authoritative acknowledgement should clear ClientGame pending command"
    );
    failures += expect(
      nearlyEqual(
        client.predictedPlayer().position.x,
        acknowledgedSnapshot.players[0].position.x
      ),
      "ClientGame should converge to matching authoritative state"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ClientGame client(transport, 0);
    lg::ServerSnapshot initialSnapshot;
    initialSnapshot.players[0] = groundedPlayer();
    queueSnapshot(transport, initialSnapshot);
    client.receiveSnapshots();

    lg::MovementTuning requestedTuning;
    requestedTuning.groundAcceleration = 140.0F;
    requestedTuning.airAcceleration = 2.0F;
    requestedTuning.groundFriction = 5.0F;
    requestedTuning.stopSpeed = 2.5F;
    requestedTuning.maxGroundSpeed = 11.0F;
    requestedTuning.maxAirSpeed = 11.0F;
    requestedTuning.flightEnabled = true;
    requestedTuning.flightAcceleration = 64.0F;
    requestedTuning.maxFlightSpeed = 14.0F;
    requestedTuning.flightDamping = 0.0F;
    lg::UserCommand request;
    request.sequence = 15;
    request.forwardMove = 1.0F;
    request.viewPitchRadians = 0.5F;
    client.sendCommand(request, false, false, true, requestedTuning);

    lg::ServerSnapshot delayedSnapshot = initialSnapshot;
    delayedSnapshot.serverTick = 1;
    queueSnapshot(transport, delayedSnapshot);
    client.receiveSnapshots();
    failures += expect(
      nearlyEqual(client.movementTuning().groundAcceleration, 140.0F),
      "older snapshots should not revert pending movement tuning prediction"
    );
    failures += expect(
      client.predictedPlayer().movementMode == lg::MovementMode::Flying &&
        client.predictedPlayer().velocity.z > 0.0F,
      "flight tuning requests should affect local prediction immediately"
    );

    lg::ServerSnapshot acknowledgedSnapshot = delayedSnapshot;
    acknowledgedSnapshot.serverTick = 2;
    acknowledgedSnapshot.hasAcknowledgedCommand[0] = true;
    acknowledgedSnapshot.acknowledgedCommand[0] = request.sequence;
    acknowledgedSnapshot.movementTuning = requestedTuning;
    queueSnapshot(transport, acknowledgedSnapshot);
    client.receiveSnapshots();
    failures += expect(
      nearlyEqual(client.movementTuning().maxGroundSpeed, 11.0F) &&
        client.movementTuning().flightEnabled &&
        nearlyEqual(client.movementTuning().maxFlightSpeed, 14.0F),
      "acknowledged movement tuning should use the replicated server value"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ClientGame client(transport, 0);
    lg::ServerSnapshot initialSnapshot;
    initialSnapshot.players[0] = groundedPlayer();
    queueSnapshot(transport, initialSnapshot);
    client.receiveSnapshots();

    const lg::LocalMapLoadResult reloadedMap = lg::loadLocalMap("eyetoeye");
    failures += expect(reloadedMap.ok, "test map eyetoeye should load locally");
    const lg::Arena& reloadedArena = reloadedMap.arena;

    lg::ServerSnapshot reloadedSnapshot = initialSnapshot;
    reloadedSnapshot.serverTick = 1;
    reloadedSnapshot.mapRevision = initialSnapshot.mapRevision + 1;
    reloadedSnapshot.map = reloadedMap.descriptor;
    reloadedSnapshot.players[0].position = {
      reloadedArena.max.x - reloadedSnapshot.players[0].bounds.radius,
      0.0F,
      reloadedSnapshot.players[0].bounds.halfHeight,
    };
    queueSnapshot(transport, reloadedSnapshot);
    client.receiveSnapshots();

    failures += expect(
      nearlyEqual(client.arena().max.x, reloadedArena.max.x) &&
        client.arena().wallCount == reloadedArena.wallCount,
      "ClientGame should load reloaded maps locally from descriptors"
    );

    lg::UserCommand command;
    command.sequence = 17;
    command.forwardMove = 1.0F;
    client.sendCommand(command, false);
    failures += expect(
      lg::hashArena(client.arena()) == reloadedMap.descriptor.contentHash,
      "ClientGame prediction should use the locally loaded reloaded arena"
    );

    lg::ServerSnapshot arenaLessSnapshot = reloadedSnapshot;
    arenaLessSnapshot.serverTick = 2;
    arenaLessSnapshot.players[0].position.x = 0.0F;
    queueSnapshot(transport, arenaLessSnapshot);
    client.receiveSnapshots();
    failures += expect(
      client.snapshot().serverTick == 2 &&
        nearlyEqual(client.arena().max.x, reloadedArena.max.x) &&
        client.arena().wallCount == reloadedArena.wallCount,
      "ClientGame should retain the current arena on arena-less snapshots"
    );

    lg::ServerSnapshot descriptorOnlyReload = arenaLessSnapshot;
    descriptorOnlyReload.serverTick = 3;
    descriptorOnlyReload.mapRevision = reloadedSnapshot.mapRevision + 1;
    descriptorOnlyReload.map = {"missing_map", 12345};
    queueSnapshot(transport, descriptorOnlyReload);
    client.receiveSnapshots();
    failures += expect(
      client.snapshot().serverTick == 2 &&
        client.arena().wallCount == reloadedArena.wallCount &&
        client.hasConnectionError(),
      "ClientGame should reject unknown map descriptors before accepting a new map revision"
    );
  }

  {
    const lg::LocalMapLoadResult localMap = lg::loadLocalMap("eyetoeye");
    failures += expect(localMap.ok, "eyetoeye should load from the local map registry");
    failures += expect(
      lg::hashArena(localMap.arena) == localMap.descriptor.contentHash &&
        lg::hashArena(localMap.arena) == lg::hashArena(localMap.arena),
      "local map hash should be deterministic"
    );

    lg::LoopbackTransport transport;
    lg::ClientGame client(transport, 0);
    lg::ServerSnapshot mismatchSnapshot;
    mismatchSnapshot.serverTick = 1;
    mismatchSnapshot.mapRevision = 2;
    mismatchSnapshot.map = localMap.descriptor;
    mismatchSnapshot.map.contentHash ^= 0x1U;
    queueSnapshot(transport, mismatchSnapshot);
    client.receiveSnapshots();
    failures += expect(
      client.hasConnectionError() &&
        client.connectionError().find("Map mismatch:") != std::string::npos,
      "ClientGame should reject mismatched map hashes with a clear error"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ClientGame client(transport, 0);
    lg::ServerSnapshot initialSnapshot;
    initialSnapshot.players[0] = groundedPlayer();
    queueSnapshot(transport, initialSnapshot);
    client.receiveSnapshots();

    lg::UserCommand movement;
    movement.sequence = 20;
    movement.forwardMove = 1.0F;
    client.sendCommand(movement, false);
    lg::UserCommand reset;
    reset.sequence = 21;
    client.sendCommand(reset, true);

    lg::ServerSnapshot resetSnapshot = initialSnapshot;
    resetSnapshot.serverTick = 1;
    resetSnapshot.hasAcknowledgedCommand[0] = true;
    resetSnapshot.acknowledgedCommand[0] = reset.sequence;
    queueSnapshot(transport, resetSnapshot);
    client.receiveSnapshots();

    failures += expect(
      client.predictionDiagnostics().pendingCommandCount == 0,
      "reset acknowledgement should clear pre-reset pending commands"
    );
    failures += expect(
      nearlyEqual(client.predictedPlayer().position.x, resetSnapshot.players[0].position.x),
      "reset snapshot should restore authoritative spawn during prediction"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ClientGame client(transport, 0);
    lg::ServerSnapshot initialSnapshot;
    initialSnapshot.players[0] = groundedPlayer();
    queueSnapshot(transport, initialSnapshot);
    client.receiveSnapshots();

    lg::UserCommand acknowledged;
    acknowledged.sequence = 30;
    acknowledged.forwardMove = 1.0F;
    client.sendCommand(acknowledged, false);
    lg::UserCommand pending = acknowledged;
    pending.sequence = 31;
    client.sendCommand(pending, false);

    lg::ServerSnapshot deathSnapshot = initialSnapshot;
    deathSnapshot.serverTick = 1;
    deathSnapshot.hasAcknowledgedCommand[0] = true;
    deathSnapshot.acknowledgedCommand[0] = acknowledged.sequence;
    deathSnapshot.players[0].health = 0;
    deathSnapshot.respawnTicksRemaining[0] = 250;
    queueSnapshot(transport, deathSnapshot);
    client.receiveSnapshots();

    failures += expect(client.predictedPlayer().health == 0, "death snapshot should update prediction health");
    failures += expect(
      nearlyEqual(client.predictedPlayer().position.x, deathSnapshot.players[0].position.x),
      "pending movement should not replay while authoritative player is dead"
    );
    failures += expect(
      client.predictionDiagnostics().pendingCommandCount == 0,
      "death snapshot should discard movement that must not replay after respawn"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ClientGame client(transport, 0, 7);
    lg::ServerSnapshot playing;
    playing.hasLocalClientState = true;
    playing.localPlayerIndex = 0;
    playing.players[0] = groundedPlayer();
    queueSnapshot(transport, playing);
    client.receiveSnapshots();

    lg::UserCommand movement;
    movement.sequence = 50;
    movement.forwardMove = 1.0F;
    client.sendCommand(movement, false);
    failures += expect(
      client.predictionDiagnostics().pendingCommandCount == 1,
      "participating client should predict movement before becoming a spectator"
    );

    lg::ServerSnapshot observing = playing;
    observing.serverTick = 1;
    observing.localPlayerIndex = lg::kNoAssignedPlayer;
    observing.localSpectator = true;
    queueSnapshot(transport, observing);
    client.receiveSnapshots();
    failures += expect(
      client.spectator() && client.predictedPlayer().health == 0 &&
        client.predictionDiagnostics().pendingCommandCount == 0,
      "dedicated spectator should release the predicted body and pending movement"
    );
    movement.sequence = 51;
    client.sendCommand(movement, false);
    failures += expect(
      client.predictionDiagnostics().pendingCommandCount == 0,
      "dedicated spectator input should not create predicted movement"
    );

    lg::ServerSnapshot rejoined = observing;
    rejoined.serverTick = 2;
    rejoined.localPlayerIndex = 2;
    rejoined.localSpectator = false;
    rejoined.players[2] = groundedPlayer();
    rejoined.players[2].position.x = 9.0F;
    queueSnapshot(transport, rejoined);
    client.receiveSnapshots();
    failures += expect(
      !client.spectator() && client.localPlayerIndex() == 2 &&
        nearlyEqual(client.predictedPlayer().position.x, 9.0F),
      "body reassignment should initialize prediction from the new authoritative player index"
    );
  }

  {
    lg::PlayerState initial = groundedPlayer();
    initial.position.x = 0.0F;
    lg::Prediction prediction;
    prediction.initialize(initial);
    lg::PlayerCollisionProxySet proxies;
    proxies.count = 1;
    proxies.proxies[0].playerIndex = 1;
    proxies.proxies[0].position = {0.72F, 0.0F, initial.position.z};
    proxies.proxies[0].bounds = initial.bounds;
    lg::UserCommand command;
    command.sequence = 1;
    command.forwardMove = 1.0F;
    command.viewYawRadians = 0.0F;
    prediction.predict(
      command, arena, tuning, lg::IcePoolArray{}, lg::IcePoolTuning{},
      lg::kFixedTickSeconds, proxies, 0
    );
    failures += expect(
      prediction.player().position.x <= 0.021F,
      "local prediction should body-block against its sampled remote proxy"
    );
  }

  {
    const lg::PlayerState initial = groundedPlayer();
    lg::Prediction prediction;
    prediction.initialize(initial);

    lg::UserCommand first;
    first.sequence = 10;
    first.clientTick = 50;
    first.forwardMove = 1.0F;
    prediction.predict(first, arena, tuning, lg::kFixedTickSeconds);

    const lg::PlayerState predictedAfterFirst = prediction.player();
    failures += expect(
      predictedAfterFirst.position.x > initial.position.x,
      "prediction should apply local movement immediately"
    );
    failures += expect(
      prediction.diagnostics().pendingCommandCount == 1,
      "predicted command should remain pending before ack"
    );
    failures += expect(
      prediction.diagnostics().hasPendingCommand &&
        prediction.diagnostics().oldestPendingCommandClientTick == 50U,
      "prediction diagnostics should expose the oldest pending client tick"
    );

    lg::UserCommand second = first;
    second.sequence = 11;
    second.clientTick = 51;
    second.rightMove = 1.0F;
    prediction.predict(second, arena, tuning, lg::kFixedTickSeconds);

    lg::PlayerState authoritativeAfterFirst = initial;
    lg::simulateMovement(
      authoritativeAfterFirst,
      first,
      arena,
      tuning,
      lg::kFixedTickSeconds
    );
    lg::PlayerState expectedAfterReplay = authoritativeAfterFirst;
    lg::simulateMovement(
      expectedAfterReplay,
      second,
      arena,
      tuning,
      lg::kFixedTickSeconds
    );

    prediction.reconcile(
      authoritativeAfterFirst,
      true,
      first.sequence,
      arena,
      tuning,
      lg::kFixedTickSeconds
    );

    failures += expect(
      prediction.diagnostics().pendingCommandCount == 1,
      "reconciliation should retain unacknowledged commands"
    );
    failures += expect(
      nearlyEqual(prediction.player().position.x, expectedAfterReplay.position.x),
      "reconciliation should replay pending movement x"
    );
    failures += expect(
      nearlyEqual(prediction.player().position.y, expectedAfterReplay.position.y),
      "reconciliation should replay pending movement y"
    );

    lg::PlayerState corrected = authoritativeAfterFirst;
    corrected.position.x -= 0.5F;
    prediction.reconcile(
      corrected,
      true,
      second.sequence,
      arena,
      tuning,
      lg::kFixedTickSeconds
    );

    failures += expect(
      prediction.diagnostics().pendingCommandCount == 0,
      "latest acknowledgement should clear pending commands"
    );
    failures += expect(
      prediction.diagnostics().correctionCount == 1,
      "authoritative position difference should count as correction"
    );
    failures += expect(
      nearlyEqual(
        prediction.diagnostics().lastCorrectionVector.x,
        corrected.position.x - expectedAfterReplay.position.x
      ),
      "prediction diagnostics should expose the last correction vector"
    );
    failures += expect(
      nearlyEqual(prediction.player().position.x, corrected.position.x),
      "authoritative correction should replace predicted position"
    );
  }

  {
    lg::PlayerState dead = groundedPlayer();
    dead.health = 0;
    lg::Prediction prediction;
    prediction.initialize(dead);
    lg::UserCommand command;
    command.sequence = 3;
    command.forwardMove = 1.0F;
    command.jump = true;
    command.viewYawRadians = 1.25F;
    command.viewPitchRadians = -0.4F;
    prediction.predict(command, arena, tuning, lg::kFixedTickSeconds);
    failures += expect(
      nearlyEqual(prediction.player().position.x, dead.position.x),
      "dead local player prediction should match server movement rules"
    );
    failures += expect(
      nearlyEqual(prediction.player().viewYawRadians, 1.25F) &&
        nearlyEqual(prediction.player().viewPitchRadians, -0.4F) &&
        !prediction.player().jumpHeld,
      "dead local prediction should update look without latching jump"
    );
    failures += expect(
      prediction.diagnostics().pendingCommandCount == 0,
      "dead input should not remain pending for replay after respawn"
    );
  }

  {
    lg::Prediction prediction;
    prediction.initialize(groundedPlayer());

    lg::UserCommand beforeWrap;
    beforeWrap.sequence = UINT32_MAX;
    prediction.predict(beforeWrap, arena, tuning, lg::kFixedTickSeconds);
    lg::UserCommand afterWrap;
    afterWrap.sequence = 0;
    prediction.predict(afterWrap, arena, tuning, lg::kFixedTickSeconds);

    prediction.reconcile(
      groundedPlayer(),
      true,
      beforeWrap.sequence,
      arena,
      tuning,
      lg::kFixedTickSeconds
    );
    failures += expect(
      prediction.diagnostics().pendingCommandCount == 1,
      "acknowledgement should preserve command after sequence wrap"
    );
  }

  {
    TestSnapshotInterpolation interpolation;
    lg::ServerSnapshot previous;
    previous.serverTick = 10;
    previous.mapRevision = 4;
    previous.players[1] = groundedPlayer();
    previous.players[1].position.x = 2.0F;
    previous.connectedPlayers[1] = true;
    previous.participatingPlayers[1] = true;
    interpolation.push(previous);

    lg::ServerSnapshot current = previous;
    current.serverTick = 20;
    current.players[1].position.x = 6.0F;
    current.players[1].health = 0;
    interpolation.push(current);
    const auto sample = interpolation.collisionSample(1);
    failures += expect(
      sample.mapRevision == 4 && sample.discreteServerTick == 10 && sample.eligible,
      "collision eligibility should use the same previous discrete endpoint as health"
    );

    lg::ServerSnapshot newMap = current;
    newMap.serverTick = 21;
    newMap.mapRevision = 5;
    newMap.players[1].position.x = -7.0F;
    interpolation.push(newMap);
    const auto mapSample = interpolation.collisionSample(1);
    failures += expect(
      nearlyEqual(mapSample.pose.position.x, -7.0F) && mapSample.mapRevision == 5,
      "map revision changes should reset collision interpolation history"
    );
  }

  {
    lg::PlayerState previous = groundedPlayer();
    previous.position = {0.0F, 0.0F, 1.0F};
    previous.velocity = {2.0F, 0.0F, 0.0F};
    previous.viewYawRadians = kPi - 0.1F;
    previous.health = 100;

    lg::PlayerState current = previous;
    current.position = {10.0F, 4.0F, 3.0F};
    current.velocity = {4.0F, 2.0F, 1.0F};
    current.viewYawRadians = -kPi + 0.1F;
    current.health = 75;
    current.movementMode = lg::MovementMode::Airborne;
    current.bounds.halfHeight = previous.bounds.halfHeight * 0.5F;
    current.crouched = true;
    current.sneaking = true;

    const lg::PlayerState midpoint = lg::interpolatePlayerState(previous, current, 0.5F);
    failures += expect(nearlyEqual(midpoint.position.x, 5.0F), "interpolation should blend position x");
    failures += expect(nearlyEqual(midpoint.position.y, 2.0F), "interpolation should blend position y");
    failures += expect(nearlyEqual(midpoint.position.z, 2.0F), "interpolation should blend position z");
    failures += expect(nearlyEqual(midpoint.velocity.x, 3.0F), "interpolation should blend velocity");
    failures += expect(
      std::fabs(std::fabs(midpoint.viewYawRadians) - kPi) < 0.001F,
      "yaw interpolation should take shortest path across pi"
    );
    failures += expect(
      nearlyEqual(
        midpoint.bounds.halfHeight,
        previous.bounds.halfHeight * 0.75F
      ),
      "interpolation should blend player height for crouch transitions"
    );
    failures += expect(
      midpoint.crouched && midpoint.sneaking,
      "crouch and sneak state should switch during interpolation"
    );
    failures += expect(midpoint.health == 100, "discrete state should remain on previous snapshot");

    const lg::PlayerState endpoint = lg::interpolatePlayerState(previous, current, 1.0F);
    failures += expect(endpoint.health == 75, "interpolation endpoint should use current health");
    failures += expect(
      endpoint.movementMode == lg::MovementMode::Airborne,
      "interpolation endpoint should use current movement mode"
    );
  }

  {
    TestSnapshotInterpolation interpolation;
    lg::ServerSnapshot first;
    first.serverTick = 4;
    first.players[1].position.x = 2.0F;
    lg::ServerSnapshot second = first;
    second.serverTick = 5;
    second.players[1].position.x = 6.0F;
    interpolation.push(first);
    interpolation.push(second);

    failures += expect(interpolation.initialized(), "snapshot interpolation should initialize");
    failures += expect(
      nearlyEqual(interpolation.player(1, 0.25F).position.x, 3.0F),
      "snapshot interpolation should sample remote player between snapshots"
    );

    lg::ServerSnapshot stale = second;
    stale.serverTick = 3;
    stale.players[1].position.x = -20.0F;
    interpolation.push(stale);
    failures += expect(
      nearlyEqual(interpolation.player(1, 1.0F).position.x, 6.0F),
      "stale snapshot should not replace interpolation endpoint"
    );
  }

  {
    TestSnapshotInterpolation interpolation;
    for (std::uint32_t tick = 0; tick <= 5; ++tick) {
      lg::ServerSnapshot snapshot;
      snapshot.serverTick = tick;
      snapshot.players[1].position.x = static_cast<float>(tick);
      interpolation.push(snapshot);
    }
    float previousPosition = -1.0F;
    for (int frame = 0; frame < 6; ++frame) {
      interpolation.advance(1.0F / 360.0F, 0.024F);
      const float position = interpolation.player(1).position.x;
      failures += expect(position > previousPosition, "buffered presentation should advance every 360 Hz render frame");
      failures += expect(previousPosition < 0.0F || position - previousPosition < 1.0F, "ordinary render advancement should not jump a server tick");
      previousPosition = position;
    }
  }

  {
    TestSnapshotInterpolation interpolation;
    std::uint32_t newestTick = 3;
    for (std::uint32_t tick = 0; tick <= newestTick; ++tick) {
      lg::ServerSnapshot snapshot;
      snapshot.serverTick = tick;
      snapshot.players[1].position.x = static_cast<float>(tick);
      interpolation.push(snapshot);
    }
    double arrivalSeconds = 0.0;
    float previousPosition = -1.0F;
    int movingFrames = 0;
    for (int frame = 0; frame < 360; ++frame) {
      arrivalSeconds += 1.0 / 360.0;
      if (arrivalSeconds + 1e-9 >= lg::kFixedTickSeconds) {
        arrivalSeconds -= lg::kFixedTickSeconds;
        lg::ServerSnapshot snapshot;
        snapshot.serverTick = ++newestTick;
        snapshot.players[1].position.x = static_cast<float>(newestTick);
        interpolation.push(snapshot);
      }
      interpolation.advance(1.0F / 360.0F, 0.024F);
      const float position = interpolation.player(1).position.x;
      movingFrames += position > previousPosition + 0.0001F ? 1 : 0;
      failures += expect(position >= previousPosition, "stable delivery presentation should be monotonic");
      previousPosition = position;
    }
    const auto diagnostics = interpolation.diagnostics();
    failures += expect(movingFrames > 350, "stable 125 Hz delivery should move on nearly every 360 Hz frame");
    failures += expect(diagnostics.hardCorrectionCount == 0, "ordinary snapshot arrivals should not hard-correct");
    failures += expect(diagnostics.bufferLeadTicks > 1.25 && diagnostics.bufferLeadTicks < 4.0, "stable buffer lead should remain near the requested three ticks within render-arrival phase variation");
    failures += expect(diagnostics.playbackRate >= 0.96F && diagnostics.playbackRate <= 1.04F, "playback correction should remain bounded");
  }

  {
    TestSnapshotInterpolation interpolation;
    for (std::uint32_t tick = 0; tick <= 4; ++tick) {
      lg::ServerSnapshot snapshot;
      snapshot.serverTick = tick;
      snapshot.players[1].position.x = static_cast<float>(tick);
      interpolation.push(snapshot);
    }
    interpolation.advance(1.0F / 60.0F, 0.024F);
    interpolation.elapseWithoutAdvance((1.0 / 60.0) - 0.0005);
    lg::ServerSnapshot lateInFrame;
    lateInFrame.serverTick = 6;
    lateInFrame.players[1].position.x = 6.0F;
    interpolation.push(lateInFrame);
    interpolation.advance(0.0005F, 0.024F);
    const auto diagnostics = interpolation.diagnostics();
    const double desiredLeadTicks =
      diagnostics.bufferLeadTicks - diagnostics.timelineErrorTicks;
    failures += expect(
      desiredLeadTicks > 2.8,
      "a snapshot accepted near a 60 Hz frame end should only have its actual sub-millisecond arrival age"
    );
  }

  {
    TestSnapshotInterpolation interpolation;
    for (std::uint32_t tick = 0; tick <= 5; ++tick) {
      lg::ServerSnapshot snapshot;
      snapshot.serverTick = tick;
      snapshot.players[1].position.x = static_cast<float>(tick);
      interpolation.push(snapshot);
    }
    float previousPosition = -1.0F;
    for (int frame = 0; frame < 6; ++frame) {
      interpolation.advance(1.0F / 360.0F, 0.024F);
      previousPosition = interpolation.player(1).position.x;
    }
    interpolation.advance(0.008F, 0.024F); // one missing snapshot interval
    lg::ServerSnapshot afterLoss;
    afterLoss.serverTick = 7;
    afterLoss.players[1].position.x = 7.0F;
    interpolation.push(afterLoss);
    for (int frame = 0; frame < 6; ++frame) {
      interpolation.advance(1.0F / 360.0F, 0.024F);
      const float position = interpolation.player(1).position.x;
      failures += expect(position >= previousPosition, "one lost snapshot must not rewind presentation");
      previousPosition = position;
    }
    failures += expect(interpolation.diagnostics().hardCorrectionCount == 0, "one lost snapshot should interpolate across the gap without hard correction");
  }

  {
    TestSnapshotInterpolation interpolation;
    for (std::uint32_t tick = 0; tick <= 3; ++tick) {
      lg::ServerSnapshot snapshot;
      snapshot.serverTick = tick;
      snapshot.players[1].position.x = static_cast<float>(tick);
      interpolation.push(snapshot);
    }
    for (int frame = 0; frame < 50; ++frame) {
      interpolation.advance(1.0F / 360.0F, 0.024F);
    }
    const auto held = interpolation.diagnostics();
    failures += expect(nearlyEqual(interpolation.player(1).position.x, 3.0F), "buffer underrun should hold the newest state without extrapolation");
    failures += expect(held.bufferUnderrun && held.underrunCount == 1, "an underrun episode should be counted once");
    interpolation.advance(1.0F / 360.0F, 0.024F);
    failures += expect(interpolation.diagnostics().underrunCount == 1, "holding during underrun should not count every render frame");
    failures += expect(
      held.hardCorrectionCount == 0,
      "an uncovered underrun target must not latch a hard correction"
    );
    interpolation.elapseWithoutAdvance(0.5);
    lg::ServerSnapshot resumed;
    resumed.serverTick = 68;
    resumed.players[1].position.x = 68.0F;
    interpolation.push(resumed);
    interpolation.advance(1.0F / 360.0F, 0.024F);
    const auto recovered = interpolation.diagnostics();
    failures += expect(
      recovered.presentationTick >= 64.0 &&
        recovered.hardCorrectionCount == 1 &&
        !recovered.bufferUnderrun,
      "fresh history after a long underrun should hard-correct promptly to the buffered target"
    );
  }

  {
    TestSnapshotInterpolation interpolation;
    for (std::uint32_t tick = 10; tick <= 14; ++tick) {
      lg::ServerSnapshot snapshot;
      snapshot.serverTick = tick;
      snapshot.players[1].position.x = static_cast<float>(tick);
      interpolation.push(snapshot);
    }
    interpolation.advance(1.0F / 360.0F, 0.024F);
    const double before = interpolation.diagnostics().presentationTick;
    const std::size_t bufferedBefore = interpolation.diagnostics().bufferedSnapshotCount;
    lg::ServerSnapshot duplicate;
    duplicate.serverTick = 14;
    duplicate.players[1].position.x = -100.0F;
    interpolation.push(duplicate);
    duplicate.serverTick = 13;
    interpolation.push(duplicate);
    interpolation.advance(1.0F / 360.0F, 0.024F);
    failures += expect(interpolation.diagnostics().presentationTick > before, "duplicate and reordered snapshots must not reset arrival time or rewind");
    failures += expect(interpolation.diagnostics().bufferedSnapshotCount == bufferedBefore, "snapshot buffer should reject duplicate and reordered ticks");
  }

  {
    TestSnapshotInterpolation interpolation;
    for (std::uint32_t tick = 0; tick <= 8; ++tick) {
      lg::ServerSnapshot snapshot;
      snapshot.serverTick = tick;
      snapshot.players[1].position.x = static_cast<float>(tick);
      interpolation.push(snapshot);
    }
    interpolation.advance(1.0F / 360.0F, 0.024F);
    double previousTick = interpolation.diagnostics().presentationTick;
    interpolation.advance(1.0F / 360.0F, 0.048F);
    auto diagnostics = interpolation.diagnostics();
    failures += expect(diagnostics.presentationTick >= previousTick && diagnostics.playbackRate < 1.0F, "increasing cl_interp should build lead by slowing without rewinding");
    previousTick = diagnostics.presentationTick;
    interpolation.advance(1.0F / 360.0F, 0.008F);
    diagnostics = interpolation.diagnostics();
    failures += expect(diagnostics.presentationTick >= previousTick && diagnostics.playbackRate > 1.0F, "decreasing cl_interp should catch up at a bounded rate");
    failures += expect(diagnostics.hardCorrectionCount == 0, "normal delay changes should not be severe corrections");
  }

  {
    TestSnapshotInterpolation interpolation;
    for (std::uint32_t tick = 0; tick <= 4; ++tick) {
      lg::ServerSnapshot snapshot;
      snapshot.serverTick = tick;
      snapshot.players[1].position.x = static_cast<float>(tick);
      interpolation.push(snapshot);
    }
    interpolation.advance(1.0F / 360.0F, 0.024F);
    interpolation.reset();
    lg::ServerSnapshot newTimeline;
    newTimeline.serverTick = 100;
    newTimeline.mapRevision = 2;
    newTimeline.players[1].position.x = 1000.0F;
    interpolation.push(newTimeline);
    interpolation.advance(1.0F / 360.0F, 0.024F);
    failures += expect(nearlyEqual(interpolation.player(1).position.x, 1000.0F), "reset must discard old timeline poses");
    failures += expect(!interpolation.diagnostics().playbackStarted && interpolation.diagnostics().bufferedSnapshotCount == 1, "reset should require startup buffering again");
  }

  {
    lg::LoopbackTransport transport;
    lg::ClientGame client(transport, 0);
    for (std::uint32_t tick = 0; tick <= 5; ++tick) {
      lg::ServerSnapshot snapshot;
      snapshot.serverTick = tick;
      snapshot.players[0] = groundedPlayer();
      snapshot.players[1] = groundedPlayer();
      snapshot.players[1].position.x = static_cast<float>(tick);
      queueSnapshot(transport, snapshot);
    }
    client.receiveSnapshots();
    client.advanceInterpolation(1.0F / 360.0F, 0.024F);

    lg::UserCommand attack;
    attack.sequence = 40;
    attack.attack = true;
    client.sendCommand(
      attack,
      false,
      false,
      true,
      {},
      1.0F,
      1.0F,
      800.0F,
      625.0F,
      100,
      0.25F
    );

    lg::CommandPacket sent;
    failures += expect(
      transport.receiveCommand(sent),
      "ClientGame should emit a command packet"
    );
    failures += expect(
      sent.viewedServerTick == 2,
      "ClientGame should send the presented server tick for lag compensation"
    );
    failures += expect(
      sent.lightningKnockback == 800.0F && sent.rocketKnockback == 625.0F &&
        sent.vampirism == 0.25F,
      "ClientGame should preserve distinct LG, RL, and vampirism tuning values"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ClientGame client(transport, 0);
    constexpr std::uint32_t largeServerTick = 1U << 24U;
    for (std::uint32_t offset = 0; offset <= 5; ++offset) {
      lg::ServerSnapshot snapshot;
      snapshot.serverTick = largeServerTick + offset;
      snapshot.players[0] = groundedPlayer();
      snapshot.players[1] = groundedPlayer();
      snapshot.players[1].position.x = static_cast<float>(offset);
      queueSnapshot(transport, snapshot);
    }
    client.receiveSnapshots();
    client.advanceInterpolation(1.0F / 360.0F, 0.024F);

    lg::UserCommand attack;
    attack.sequence = 41;
    attack.attack = true;
    client.sendCommand(attack, false);

    lg::CommandPacket sent;
    failures += expect(
      transport.receiveCommand(sent),
      "ClientGame should emit a high-uptime command packet"
    );
    failures += expect(
      sent.viewedServerTick == largeServerTick + 2U,
      "ClientGame should send an exact presented server tick after long uptime"
    );
  }

  {
    TestSnapshotInterpolation interpolation;
    for (std::uint32_t tick = 0; tick <= 5; ++tick) {
      lg::ServerSnapshot snapshot;
      snapshot.serverTick = tick;
      snapshot.players[1].position.x = static_cast<float>(tick);
      snapshot.players[1].velocity.x = 125.0F;
      interpolation.push(snapshot);
    }
    interpolation.advanceAdaptive(0.0F, 0.024F, 0.0F, 0.016F, 0.064F, 0.016F);
    failures += expect(
      nearlyEqual(interpolation.player(1).position.x, 2.0F),
      "adaptive interpolation should establish its configured reserve"
    );
    interpolation.advanceAdaptive(
      lg::kFixedTickSeconds,
      0.024F,
      0.012F,
      0.016F,
      0.064F,
      0.016F
    );
    const auto jittered = interpolation.diagnostics();
    failures += expect(
      jittered.effectiveDelaySeconds > 0.024F &&
        jittered.playbackRate >= 0.96F && jittered.playbackRate <= 1.04F &&
        jittered.hardCorrectionCount == 0,
      "adaptive jitter should change only the bounded delay target used by the shared controller"
    );
    interpolation.advanceAdaptive(0.1F, 0.024F, 0.0F, 0.016F, 0.064F, 0.016F);
    failures += expect(
      interpolation.diagnostics().bufferUnderrun &&
        interpolation.diagnostics().underrunCount == 1U &&
        nearlyEqual(interpolation.player(1).position.x, 5.0F),
      "adaptive mode should use the controller's one-shot underrun hold without extrapolation"
    );
  }

  {
    TestSnapshotInterpolation networkController;
    TestSnapshotInterpolation zeroJitterAdaptive;
    for (std::uint32_t tick = 0; tick <= 6; ++tick) {
      lg::ServerSnapshot snapshot;
      snapshot.serverTick = tick;
      snapshot.players[1].position.x = static_cast<float>(tick);
      networkController.push(snapshot);
      zeroJitterAdaptive.push(snapshot);
    }
    for (int frame = 0; frame < 20; ++frame) {
      networkController.advance(1.0F / 360.0F, 0.024F);
      zeroJitterAdaptive.advanceAdaptive(
        1.0F / 360.0F, 0.024F, 0.0F, 0.016F, 0.064F, 0.05F
      );
    }
    const auto network = networkController.diagnostics();
    const auto adaptive = zeroJitterAdaptive.diagnostics();
    failures += expect(
      std::fabs(network.presentationTick - adaptive.presentationTick) < 0.000001 &&
        nearlyEqual(network.playbackRate, adaptive.playbackRate) &&
        network.underrunCount == adaptive.underrunCount &&
        network.hardCorrectionCount == adaptive.hardCorrectionCount,
      "zero-jitter adaptive mode should exactly delegate playback to the network controller"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ClientGame client(transport, 0);
    lg::ChatHistory history;
    history.messageCount = 5U;
    for (std::size_t index = 0; index < history.messageCount; ++index) {
      history.messages[index] = lg::ChatMessage{
        static_cast<std::uint32_t>(index + 10U),
        static_cast<std::uint8_t>(index % 2U),
        index % 2U == 0U ? "yg" : "Zap Witch",
        "message " + std::to_string(index + 1U),
      };
    }
    transport.publishChatHistory(history);
    client.receiveSnapshots();
    failures += expect(
      client.chatHistory().size() == 5U &&
        client.chatHistory().front().sequence == 10U &&
        client.chatHistory().back().message == "message 5",
      "ClientGame should assemble ordered chat history across chunks"
    );
  }

  return failures == 0 ? 0 : 1;
}
