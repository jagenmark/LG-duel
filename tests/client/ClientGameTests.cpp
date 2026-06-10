#include "client/ClientGame.hpp"
#include "client/Interpolation.hpp"
#include "client/Prediction.hpp"
#include "net/LoopbackTransport.hpp"
#include "shared/Constants.hpp"
#include "sim/Arena.hpp"
#include "sim/Movement.hpp"
#include "sim/MovementModes.hpp"
#include "sim/PlayerState.hpp"
#include "sim/UserCommand.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
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
    transport.sendSnapshot(initialSnapshot);
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
    transport.sendSnapshot(delayedSnapshot);
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
    transport.sendSnapshot(acknowledgedSnapshot);
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
    transport.sendSnapshot(initialSnapshot);
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
    transport.sendSnapshot(resetSnapshot);
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
    transport.sendSnapshot(initialSnapshot);
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
    transport.sendSnapshot(deathSnapshot);
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
    prediction.predict(command, arena, tuning, lg::kFixedTickSeconds);
    failures += expect(
      nearlyEqual(prediction.player().position.x, dead.position.x),
      "dead local player prediction should match server movement rules"
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

  return failures == 0 ? 0 : 1;
}
