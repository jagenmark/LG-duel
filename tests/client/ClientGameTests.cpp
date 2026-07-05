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
      client.predictionDiagnostics().pendingCommandCount == 1,
      "death snapshot should retain newer pending command until acknowledged"
    );
  }

  {
    const lg::PlayerState initial = groundedPlayer();
    lg::Prediction prediction;
    prediction.initialize(initial);

    lg::UserCommand first;
    first.sequence = 10;
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

    lg::UserCommand second = first;
    second.sequence = 11;
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

    const lg::PlayerState midpoint = lg::interpolatePlayerState(previous, current, 0.5F);
    failures += expect(nearlyEqual(midpoint.position.x, 5.0F), "interpolation should blend position x");
    failures += expect(nearlyEqual(midpoint.position.y, 2.0F), "interpolation should blend position y");
    failures += expect(nearlyEqual(midpoint.position.z, 2.0F), "interpolation should blend position z");
    failures += expect(nearlyEqual(midpoint.velocity.x, 3.0F), "interpolation should blend velocity");
    failures += expect(
      std::fabs(std::fabs(midpoint.viewYawRadians) - kPi) < 0.001F,
      "yaw interpolation should take shortest path across pi"
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
    lg::SnapshotInterpolation interpolation;
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
    lg::SnapshotInterpolation interpolation;
    for (std::uint32_t tick = 0; tick <= 5; ++tick) {
      lg::ServerSnapshot snapshot;
      snapshot.serverTick = tick;
      snapshot.players[1].position.x = static_cast<float>(tick);
      interpolation.push(snapshot);
    }

    interpolation.advance(lg::kFixedTickSeconds * 20.0F);
    failures += expect(
      nearlyEqual(interpolation.player(1).position.x, 2.0F),
      "presentation interpolation should stay behind the newest snapshot"
    );

    lg::ServerSnapshot duplicate;
    duplicate.serverTick = 5;
    duplicate.players[1].position.x = 50.0F;
    interpolation.push(duplicate);
    interpolation.advance(lg::kFixedTickSeconds);
    failures += expect(
      nearlyEqual(interpolation.player(1).position.x, 2.0F),
      "duplicate snapshot ticks should not disturb presentation interpolation"
    );

    lg::ServerSnapshot next;
    next.serverTick = 6;
    next.players[1].position.x = 6.0F;
    interpolation.push(next);
    interpolation.advance(lg::kFixedTickSeconds);
    failures += expect(
      nearlyEqual(interpolation.player(1).position.x, 3.0F),
      "presentation interpolation should advance monotonically as newer snapshots arrive"
    );
  }

  {
    lg::SnapshotInterpolation interpolation;
    constexpr std::uint32_t largeServerTick = 1U << 24U;
    for (std::uint32_t offset = 0; offset <= 5; ++offset) {
      lg::ServerSnapshot snapshot;
      snapshot.serverTick = largeServerTick + offset;
      snapshot.players[1].position.x = static_cast<float>(offset);
      interpolation.push(snapshot);
    }

    interpolation.advance(lg::kFixedTickSeconds * 20.0F, 0.024F);
    failures += expect(
      interpolation.presentationServerTick() == largeServerTick + 2U,
      "presentation tick should remain exact after long server uptime"
    );
    failures += expect(
      nearlyEqual(interpolation.player(1).position.x, 2.0F),
      "presentation interpolation should not drift after long server uptime"
    );
  }

  {
    lg::SnapshotInterpolation interpolation;
    constexpr std::uint32_t tickCount = 1000;
    for (std::uint32_t tick = 0; tick <= tickCount; ++tick) {
      lg::ServerSnapshot snapshot;
      snapshot.serverTick = tick;
      snapshot.players[1].position.x = static_cast<float>(tick);
      interpolation.push(snapshot);
      interpolation.advance(lg::kFixedTickSeconds * 0.98F, 0.024F);
    }

    failures += expect(
      interpolation.presentationServerTick() == tickCount - 3U,
      "presentation interpolation should stay anchored to snapshot delay over time"
    );
    failures += expect(
      nearlyEqual(
        interpolation.player(1).position.x,
        static_cast<float>(tickCount - 3U),
        0.5F
      ),
      "remote player interpolation should not accumulate local clock drift"
    );
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
    client.advanceInterpolation(lg::kFixedTickSeconds * 20.0F, 0.024F);

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
    client.advanceInterpolation(lg::kFixedTickSeconds * 20.0F, 0.024F);

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

  return failures == 0 ? 0 : 1;
}
