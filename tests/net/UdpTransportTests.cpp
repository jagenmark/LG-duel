#include "client/ClientGame.hpp"
#include "net/UdpTransport.hpp"
#include "server/ServerGame.hpp"
#include "shared/Constants.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string_view>
#include <thread>

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
  lg::UdpServerTransport serverTransport(0);
  failures += expect(serverTransport.initialize(), "UDP server should bind an ephemeral port");
  if (failures != 0) {
    return 1;
  }

  lg::ServerGame server(serverTransport);
  server.setConnectedPlayers({false, false});
  lg::MatchRules rules;
  rules.countdownTicks = 2;
  server.setMatchRules(rules);
  lg::UdpClientTransport firstTransport("127.0.0.1", serverTransport.localPort());
  lg::UdpClientTransport secondTransport("127.0.0.1", serverTransport.localPort());
  failures += expect(firstTransport.initialize(), "first UDP client should initialize");
  failures += expect(secondTransport.initialize(), "second UDP client should initialize");
  if (failures != 0) {
    return 1;
  }

  for (int iteration = 0; iteration < 200; ++iteration) {
    firstTransport.update();
    secondTransport.update();
    serverTransport.update();
    server.setConnectedPlayers(serverTransport.connectedPlayers());
    server.tick(lg::kFixedTickSeconds);
    firstTransport.update();
    secondTransport.update();
    if (firstTransport.connected() && secondTransport.connected()) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  failures += expect(firstTransport.connected(), "first UDP client should complete handshake");
  failures += expect(secondTransport.connected(), "second UDP client should complete handshake");
  failures += expect(
    firstTransport.playerIndex() != secondTransport.playerIndex(),
    "UDP clients should receive distinct player slots"
  );
  failures += expect(serverTransport.connectedClientCount() == 2, "server should track two UDP clients");
  if (failures != 0) {
    return 1;
  }

  lg::ClientGame firstClient(firstTransport, firstTransport.playerIndex());
  lg::ClientGame secondClient(secondTransport, secondTransport.playerIndex());
  for (int iteration = 0; iteration < 100; ++iteration) {
    serverTransport.update();
    server.setConnectedPlayers(serverTransport.connectedPlayers());
    server.tick(lg::kFixedTickSeconds);
    firstTransport.update();
    secondTransport.update();
    firstClient.receiveSnapshots();
    secondClient.receiveSnapshots();
    if (
      firstClient.hasSnapshot() &&
      secondClient.hasSnapshot() &&
      firstClient.snapshot().matchPhase == lg::MatchPhase::WaitingForReady &&
      secondClient.snapshot().matchPhase == lg::MatchPhase::WaitingForReady
    ) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  failures += expect(firstClient.hasSnapshot(), "first UDP client should receive snapshot");
  failures += expect(secondClient.hasSnapshot(), "second UDP client should receive snapshot");
  failures += expect(
    firstClient.snapshot().matchPhase == lg::MatchPhase::WaitingForReady,
    "two UDP clients should enter ready-up"
  );

  const std::size_t firstPlayerIndex = firstTransport.playerIndex();
  const std::size_t firstTargetIndex = 1U - firstPlayerIndex;
  const lg::Vec3 warmupTargetStart =
    firstClient.snapshot().players[firstTargetIndex].position;
  lg::UserCommand warmupAttack;
  warmupAttack.sequence = 0;
  warmupAttack.attack = true;
  warmupAttack.viewYawRadians =
    firstPlayerIndex == 0U ? 0.0F : 3.14159265359F;
  firstClient.sendCommand(warmupAttack, false);
  for (int iteration = 0; iteration < 4; ++iteration) {
    serverTransport.update();
    server.setConnectedPlayers(serverTransport.connectedPlayers());
    server.tick(lg::kFixedTickSeconds);
    firstTransport.update();
    secondTransport.update();
    firstClient.receiveSnapshots();
    secondClient.receiveSnapshots();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  failures += expect(
    server.snapshot().hasAcknowledgedCommand[firstPlayerIndex] &&
      server.snapshot().acknowledgedCommand[firstPlayerIndex] == 0,
    "server should receive the warmup attack command over UDP"
  );
  failures += expect(
    server.snapshot().lightningGuns[firstPlayerIndex].active,
    "server should simulate the UDP warmup attack"
  );
  failures += expect(
    firstClient.snapshot().lightningGuns[firstPlayerIndex].active,
    "warmup LG activity should replicate over UDP"
  );
  failures += expect(
    firstClient.snapshot().lightningGuns[firstPlayerIndex].hit,
    "warmup LG hit state should replicate over UDP"
  );
  failures += expect(
    firstClient.snapshot().players[firstTargetIndex].health < 100,
    "warmup LG damage should replicate over UDP"
  );
  failures += expect(
    firstClient.snapshot().players[firstTargetIndex].velocity.x != 0.0F,
    "warmup LG knockback should replicate over UDP"
  );
  failures += expect(
    firstClient.snapshot().players[firstTargetIndex].position.x !=
      warmupTargetStart.x,
    "warmup LG knockback should physically move a grounded target"
  );

  lg::UserCommand warmupRelease;
  warmupRelease.sequence = 1;
  firstClient.sendCommand(warmupRelease, false);
  for (int iteration = 0; iteration < 2; ++iteration) {
    serverTransport.update();
    server.tick(lg::kFixedTickSeconds);
    firstTransport.update();
    secondTransport.update();
    firstClient.receiveSnapshots();
    secondClient.receiveSnapshots();
  }
  failures += expect(
    !firstClient.snapshot().lightningGuns[firstPlayerIndex].active,
    "a replicated attack release should stop the retained warmup LG command"
  );

  lg::UserCommand firstReady;
  firstReady.sequence = 2;
  lg::UserCommand secondReady;
  secondReady.sequence = 0;
  firstClient.sendCommand(firstReady, false, true);
  secondClient.sendCommand(secondReady, false, true);
  for (int iteration = 0; iteration < 4; ++iteration) {
    serverTransport.update();
    server.setConnectedPlayers(serverTransport.connectedPlayers());
    server.tick(lg::kFixedTickSeconds);
    firstTransport.update();
    secondTransport.update();
    firstClient.receiveSnapshots();
    secondClient.receiveSnapshots();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  failures += expect(
    firstClient.snapshot().matchPhase == lg::MatchPhase::Live,
    "UDP ready requests should advance through countdown to live play"
  );

  for (std::uint32_t sequence = 0; sequence < 40; ++sequence) {
    lg::UserCommand firstCommand;
    firstCommand.sequence = sequence + 3;
    firstCommand.clientTick = sequence;
    firstCommand.forwardMove = 1.0F;
    lg::UserCommand secondCommand;
    secondCommand.sequence = sequence + 1;
    secondCommand.clientTick = sequence;
    secondCommand.forwardMove = 1.0F;

    firstClient.sendCommand(firstCommand, false);
    secondClient.sendCommand(secondCommand, false);
    serverTransport.update();
    server.setConnectedPlayers(serverTransport.connectedPlayers());
    server.tick(lg::kFixedTickSeconds);
    firstTransport.update();
    secondTransport.update();
    firstClient.receiveSnapshots();
    secondClient.receiveSnapshots();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  for (int iteration = 0; iteration < 20; ++iteration) {
    serverTransport.update();
    server.tick(lg::kFixedTickSeconds);
    firstTransport.update();
    secondTransport.update();
    firstClient.receiveSnapshots();
    secondClient.receiveSnapshots();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  failures += expect(firstClient.hasAcknowledgedCommand(), "first UDP command should be acknowledged");
  failures += expect(secondClient.hasAcknowledgedCommand(), "second UDP command should be acknowledged");
  failures += expect(firstClient.lastAcknowledgedCommand() == 42, "first UDP ack should reach latest command");
  failures += expect(secondClient.lastAcknowledgedCommand() == 40, "second UDP ack should reach latest command");
  failures += expect(
    firstClient.snapshot().players[firstTransport.playerIndex()].position.x != -3.0F,
    "first authoritative player should move over UDP"
  );
  failures += expect(
    secondClient.snapshot().players[secondTransport.playerIndex()].position.x != 3.0F,
    "second authoritative player should move over UDP"
  );

  const auto pingDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1200);
  while (
    (firstTransport.pingMilliseconds() <= 0.0F || secondTransport.pingMilliseconds() <= 0.0F) &&
    std::chrono::steady_clock::now() < pingDeadline
  ) {
    firstTransport.update();
    secondTransport.update();
    serverTransport.update();
    server.setConnectedPlayers(serverTransport.connectedPlayers());
    server.tick(lg::kFixedTickSeconds);
    firstTransport.update();
    secondTransport.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  failures += expect(firstTransport.pingMilliseconds() > 0.0F, "first UDP client should measure ping");
  failures += expect(secondTransport.pingMilliseconds() > 0.0F, "second UDP client should measure ping");

  firstTransport.disconnect();
  serverTransport.update();
  server.setConnectedPlayers(serverTransport.connectedPlayers());
  server.tick(lg::kFixedTickSeconds);
  failures += expect(
    serverTransport.connectedClientCount() == 1 &&
      server.snapshot().matchPhase == lg::MatchPhase::WaitingForPlayers,
    "explicit disconnect should immediately return the server to lobby state"
  );

  return failures == 0 ? 0 : 1;
}
