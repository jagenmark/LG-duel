#include "client/ClientGame.hpp"
#include "net/LoopbackTransport.hpp"
#include "net/SimulatedTransport.hpp"
#include "server/ServerGame.hpp"
#include "shared/Constants.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string_view>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) {
    return 0;
  }

  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

} // namespace

int main() {
  int failures = 0;

  {
    lg::NetworkSimulationConfig config;
    config.commands.latencyTicks = 3;
    lg::SimulatedTransport transport(config);
    lg::CommandPacket sent;
    sent.command.sequence = 5;
    transport.sendCommand(sent);

    lg::CommandPacket received;
    failures += expect(!transport.receiveCommand(received), "latency should delay command immediately");
    transport.advanceTicks(2);
    failures += expect(!transport.receiveCommand(received), "latency should delay command before deadline");
    transport.advanceTicks();
    failures += expect(transport.receiveCommand(received), "latency should release command at deadline");
    failures += expect(received.command.sequence == 5, "delayed command should preserve payload");
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    lg::CommandPacket recoveredEdge;
    recoveredEdge.command.sequence = 1;
    recoveredEdge.actionEdges.jump = 1;
    transport.sendCommand(recoveredEdge);
    lg::CommandPacket newerRelease = recoveredEdge;
    newerRelease.command.sequence = 2;
    transport.sendCommand(newerRelease);
    server.tick(lg::kFixedTickSeconds);
    lg::ServerSnapshot snapshot;
    lg::ServerSnapshot received;
    bool receivedSnapshot = false;
    while (transport.receiveSnapshot(received)) {
      snapshot = received;
      receivedSnapshot = true;
    }
    failures += expect(
      receivedSnapshot &&
        snapshot.hasAcknowledgedCommand[0] &&
        snapshot.acknowledgedCommand[0] == 2U &&
        snapshot.players[0].velocity.z > 0.0F,
      "a recovered jump edge should execute once even when a newer release shares its bundle"
    );
  }

  {
    lg::NetworkSimulationConfig config;
    config.commands.lossRate = 1.0F;
    config.snapshots.duplicationRate = 1.0F;
    lg::SimulatedTransport transport(config);
    lg::ServerSnapshot sentSnapshot;
    sentSnapshot.map = {"testmap", 0x12345678U};
    transport.sendCommand({});
    transport.sendSnapshot(sentSnapshot);

    lg::CommandPacket command;
    failures += expect(!transport.receiveCommand(command), "full loss should drop command");
    lg::ServerSnapshot snapshot;
    failures += expect(transport.receiveSnapshot(snapshot), "duplication should retain original snapshot");
    transport.advanceTicks();
    failures += expect(transport.receiveSnapshot(snapshot), "duplication should deliver second snapshot");
    failures += expect(transport.stats().droppedPackets == 1, "loss stats should count drop");
    failures += expect(transport.stats().duplicatedPackets == 1, "duplication stats should count copy");
  }

  {
    lg::NetworkSimulationConfig config;
    config.commands.reorderRate = 1.0F;
    config.commands.reorderExtraDelayTicks = 3;
    lg::SimulatedTransport transport(config);
    lg::CommandPacket first;
    first.command.sequence = 7;
    lg::CommandPacket second;
    second.command.sequence = 8;
    transport.sendCommand(first);
    transport.sendCommand(second);
    lg::CommandPacket received;
    failures += expect(!transport.receiveCommand(received), "reordered packets should be delayed");
    transport.advanceTicks(3);
    failures += expect(transport.receiveCommand(received), "later reordered packet should arrive first");
    failures += expect(received.command.sequence == 8, "reordering should reverse delivery order");
    transport.advanceTicks();
    failures += expect(transport.receiveCommand(received), "earlier reordered packet should eventually arrive");
    failures += expect(received.command.sequence == 7, "delayed earlier packet should preserve payload");
    failures += expect(transport.stats().reorderedPackets == 2, "reorder stats should count packets");
  }

  {
    lg::NetworkSimulationConfig config;
    config.commands.latencyTicks = 2;
    config.commands.jitterTicks = 1;
    config.commands.lossRate = 0.15F;
    config.commands.duplicationRate = 0.2F;
    config.commands.reorderRate = 0.2F;
    config.commands.reorderExtraDelayTicks = 2;
    config.snapshots.latencyTicks = 3;
    config.snapshots.jitterTicks = 2;
    config.snapshots.lossRate = 0.2F;
    config.snapshots.duplicationRate = 0.15F;
    config.snapshots.reorderRate = 0.25F;
    config.snapshots.reorderExtraDelayTicks = 2;
    config.randomSeed = 12345;

    lg::SimulatedTransport transport(config);
    lg::ServerGame server(transport);
    failures += expect(server.loadRequestedMap("eyetoeye"), "simulated server should load eyetoeye");
    lg::ServerSnapshot discarded;
    for (int tick = 0; tick < 8; ++tick) {
      transport.advanceTicks();
      while (transport.receiveSnapshot(discarded)) {
      }
    }
    lg::ClientGame client(transport, 0);

    for (int tick = 0; tick < 8 && !client.hasSnapshot(); ++tick) {
      transport.advanceTicks();
      server.tick(lg::kFixedTickSeconds);
      client.receiveSnapshots();
    }
    failures += expect(client.hasSnapshot(), "client should receive initial state through adverse network");

    std::uint32_t sequence = 0;
    for (int tick = 0; tick < 600; ++tick) {
      lg::UserCommand command;
      command.sequence = sequence++;
      command.clientTick = static_cast<std::uint32_t>(tick);
      command.forwardMove = tick < 200 ? 1.0F : 0.0F;
      command.rightMove = tick >= 100 && tick < 300 ? 1.0F : 0.0F;
      command.jump = tick == 20;
      client.sendCommand(command, false);

      transport.advanceTicks();
      server.tick(lg::kFixedTickSeconds);
      client.receiveSnapshots();
    }

    for (int tick = 0; tick < 20; ++tick) {
      transport.advanceTicks();
      server.tick(lg::kFixedTickSeconds);
      client.receiveSnapshots();
    }

    failures += expect(client.hasAcknowledgedCommand(), "adverse network should still deliver command acks");
    failures += expect(client.snapshot().serverTick > 500, "client should continue receiving snapshots");
    failures += expect(
      client.predictionDiagnostics().correctionCount > 0,
      "loss and delay should exercise reconciliation corrections"
    );
    failures += expect(transport.stats().droppedPackets > 0, "adverse run should exercise packet loss");
    failures += expect(transport.stats().duplicatedPackets > 0, "adverse run should exercise duplication");
    failures += expect(transport.stats().reorderedPackets > 0, "adverse run should exercise reordering");
    failures += expect(
      std::isfinite(client.predictedPlayer().position.x),
      "prediction should remain finite under adverse delivery"
    );
  }

  return failures == 0 ? 0 : 1;
}
