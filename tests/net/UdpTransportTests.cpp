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
    server.tick(lg::kFixedTickSeconds);
    firstTransport.update();
    secondTransport.update();
    firstClient.receiveSnapshots();
    secondClient.receiveSnapshots();
    if (firstClient.hasSnapshot() && secondClient.hasSnapshot()) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  failures += expect(firstClient.hasSnapshot(), "first UDP client should receive snapshot");
  failures += expect(secondClient.hasSnapshot(), "second UDP client should receive snapshot");

  for (std::uint32_t sequence = 0; sequence < 40; ++sequence) {
    lg::UserCommand firstCommand;
    firstCommand.sequence = sequence;
    firstCommand.clientTick = sequence;
    firstCommand.forwardMove = 1.0F;
    lg::UserCommand secondCommand;
    secondCommand.sequence = sequence;
    secondCommand.clientTick = sequence;
    secondCommand.forwardMove = 1.0F;

    firstClient.sendCommand(firstCommand, false);
    secondClient.sendCommand(secondCommand, false);
    serverTransport.update();
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
  failures += expect(firstClient.lastAcknowledgedCommand() == 39, "first UDP ack should reach latest command");
  failures += expect(secondClient.lastAcknowledgedCommand() == 39, "second UDP ack should reach latest command");
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
    server.tick(lg::kFixedTickSeconds);
    firstTransport.update();
    secondTransport.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  failures += expect(firstTransport.pingMilliseconds() > 0.0F, "first UDP client should measure ping");
  failures += expect(secondTransport.pingMilliseconds() > 0.0F, "second UDP client should measure ping");

  return failures == 0 ? 0 : 1;
}
