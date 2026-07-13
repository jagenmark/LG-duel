#include "client/ClientGame.hpp"
#include "net/UdpTransport.hpp"
#include "server/ServerGame.hpp"
#include "shared/Constants.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
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

void syncConnectedPlayers(
  lg::ServerGame& server,
  const lg::UdpServerTransport& transport
) {
  server.setConnectedPlayers(
    transport.connectedPlayers(),
    transport.connectedPlayerSessions()
  );
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
  failures += expect(server.loadRequestedMap("dev_cuboids"), "UDP test server should load dev_cuboids");
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
    syncConnectedPlayers(server, serverTransport);
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
    syncConnectedPlayers(server, serverTransport);
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
    firstClient.snapshot().hasConfiguration &&
      !firstClient.snapshot().hasCombatStats,
    "first UDP snapshot should omit unrequested combat statistics"
  );
  failures += expect(
    firstClient.snapshot().matchPhase == lg::MatchPhase::WaitingForReady,
    "two UDP clients should enter ready-up"
  );

  failures += expect(
    firstTransport.connected() &&
      secondTransport.connected() &&
      firstClient.snapshot().map.mapName == "dev_cuboids" &&
      firstClient.snapshot().map.contentHash != 0 &&
      secondClient.snapshot().map.mapName == firstClient.snapshot().map.mapName &&
      secondClient.snapshot().map.contentHash == firstClient.snapshot().map.contentHash,
    "UDP clients should receive the same map descriptor without full arena data"
  );

  const std::size_t firstPlayerIndex = firstTransport.playerIndex();
  const std::size_t firstTargetIndex = 1U - firstPlayerIndex;
  const lg::Vec3 warmupTargetStart =
    firstClient.snapshot().players[firstTargetIndex].position;
  const lg::Vec3 warmupAttackerStart =
    firstClient.snapshot().players[firstPlayerIndex].position;
  lg::UserCommand warmupAttack;
  warmupAttack.sequence = 0;
  warmupAttack.attack = true;
  warmupAttack.viewYawRadians =
    std::atan2(
      warmupTargetStart.y - warmupAttackerStart.y,
      warmupTargetStart.x - warmupAttackerStart.x
    );
  firstClient.sendCommand(warmupAttack, false);
  for (int iteration = 0; iteration < 4; ++iteration) {
    serverTransport.update();
    syncConnectedPlayers(server, serverTransport);
    server.tick(lg::kFixedTickSeconds);
    firstTransport.update();
    secondTransport.update();
    firstClient.receiveSnapshots();
    secondClient.receiveSnapshots();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  failures += expect(
    firstClient.snapshot().hasConfiguration &&
      !firstClient.snapshot().hasCombatStats,
    "gameplay snapshots should remain lean while the scoreboard is closed"
  );
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
    syncConnectedPlayers(server, serverTransport);
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
    syncConnectedPlayers(server, serverTransport);
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
    syncConnectedPlayers(server, serverTransport);
    server.tick(lg::kFixedTickSeconds);
    firstTransport.update();
    secondTransport.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  failures += expect(firstTransport.pingMilliseconds() > 0.0F, "first UDP client should measure ping");
  failures += expect(secondTransport.pingMilliseconds() > 0.0F, "second UDP client should measure ping");

  lg::CommandPacket scoreboardCommand;
  scoreboardCommand.playerIndex = static_cast<std::uint8_t>(firstTransport.playerIndex());
  scoreboardCommand.command.sequence = 43;
  scoreboardCommand.wantsScoreboardStats = true;
  firstTransport.sendCommand(scoreboardCommand);
  for (int iteration = 0; iteration < 4; ++iteration) {
    serverTransport.update();
    syncConnectedPlayers(server, serverTransport);
    server.tick(lg::kFixedTickSeconds);
    firstTransport.update();
    secondTransport.update();
    firstClient.receiveSnapshots();
    secondClient.receiveSnapshots();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  failures += expect(
    firstClient.snapshot().hasCombatStats,
    "a client with the scoreboard open should receive combat statistics"
  );
  failures += expect(
    !secondClient.snapshot().hasCombatStats,
    "one client's scoreboard should not enable combat statistics for other clients"
  );
  failures += expect(
    firstClient.snapshot().matchCombatStats[firstPlayerIndex]
      .weapons[lg::weaponIndex(lg::Weapon::LightningGun)].damageDealt ==
      server.snapshot().matchCombatStats[firstPlayerIndex]
        .weapons[lg::weaponIndex(lg::Weapon::LightningGun)].damageDealt,
    "scoreboard statistics should match the authoritative server totals"
  );

  lg::NetworkTelemetry telemetry = firstTransport.networkTelemetry();
  failures += expect(
    telemetry.valid && telemetry.historyCount > 0 &&
      telemetry.lastSnapshotBytes > 0 && telemetry.lastSnapshotBytes < 1200 &&
      telemetry.lastCommandBytes > 0 && telemetry.lastCommandBytes < 1200 &&
      telemetry.acknowledgedCommandDatagramSequence > 0,
    "UDP telemetry should measure packet sizes and command datagram acknowledgements"
  );
  failures += expect(
    telemetry.incomingLossPercent == 0.0F &&
      telemetry.outgoingLossPercent == 0.0F,
    "clean local UDP traffic should report no packet loss"
  );
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  lg::ServerSnapshot duplicateSnapshot = server.snapshot();
  serverTransport.sendSnapshot(duplicateSnapshot);
  firstTransport.update();
  failures += expect(
    firstTransport.networkTelemetry().snapshotAgeMilliseconds >= 15.0F,
    "duplicate snapshots must not refresh newest-snapshot age or adaptive timing"
  );

  lg::ClientNetworkSimulationConfig adverseNetwork;
  adverseNetwork.lossPercent = 30;
  adverseNetwork.seed = 12345;
  firstTransport.setNetworkSimulationConfig(adverseNetwork);
  for (std::uint32_t sequence = 44; sequence < 164; ++sequence) {
    lg::CommandPacket command;
    command.playerIndex = static_cast<std::uint8_t>(firstPlayerIndex);
    command.command.sequence = sequence;
    command.command.forwardMove = 1.0F;
    firstTransport.sendCommand(command);
    serverTransport.update();
    syncConnectedPlayers(server, serverTransport);
    server.tick(lg::kFixedTickSeconds);
    firstTransport.update();
    secondTransport.update();
    firstClient.receiveSnapshots();
    secondClient.receiveSnapshots();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  firstTransport.setNetworkSimulationConfig({});
  lg::CommandPacket recoveryCommand;
  recoveryCommand.playerIndex = static_cast<std::uint8_t>(firstPlayerIndex);
  recoveryCommand.command.sequence = 164;
  firstTransport.sendCommand(recoveryCommand);
  for (int iteration = 0; iteration < 30; ++iteration) {
    serverTransport.update();
    syncConnectedPlayers(server, serverTransport);
    server.tick(lg::kFixedTickSeconds);
    firstTransport.update();
    secondTransport.update();
    firstClient.receiveSnapshots();
    secondClient.receiveSnapshots();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  telemetry = firstTransport.networkTelemetry();
  failures += expect(
    telemetry.incomingLossPercent > 0.0F &&
      telemetry.outgoingLossPercent > 0.0F,
    "UDP telemetry should expose simulated incoming and outgoing packet loss"
  );

  lg::CommandPacket chatCommand;
  chatCommand.playerIndex = firstTransport.clientIndex();
  chatCommand.command.sequence = 165;
  chatCommand.chatMessage = "udp chat history";
  firstTransport.sendCommand(chatCommand);
  for (int iteration = 0; iteration < 30; ++iteration) {
    serverTransport.update();
    syncConnectedPlayers(server, serverTransport);
    server.tick(lg::kFixedTickSeconds);
    firstTransport.update();
    secondTransport.update();
    firstClient.receiveSnapshots();
    secondClient.receiveSnapshots();
    if (
      !firstClient.chatHistory().empty() &&
      !secondClient.chatHistory().empty()
    ) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  failures += expect(
    !firstClient.chatHistory().empty() &&
      firstClient.chatHistory().back().message == "udp chat history",
    "UDP chat history should return a sent message to its author"
  );
  failures += expect(
    !secondClient.chatHistory().empty() &&
      secondClient.chatHistory().back().message == "udp chat history",
    "UDP chat history should deliver a sent message to other clients"
  );

  firstTransport.disconnect();
  serverTransport.update();
  syncConnectedPlayers(server, serverTransport);
  server.tick(lg::kFixedTickSeconds);
  failures += expect(
    serverTransport.connectedClientCount() == 1 &&
      server.snapshot().matchPhase == lg::MatchPhase::WaitingForPlayers,
    "explicit disconnect should immediately return the server to lobby state"
  );

  {
    lg::UdpServerTransport multiServerTransport(0);
    failures += expect(
      multiServerTransport.initialize(),
      "multi-client UDP server should bind an ephemeral port"
    );
    if (failures != 0) {
      return 1;
    }

    lg::ServerGame multiServer(multiServerTransport);
    multiServer.setConnectedPlayers({});
    std::array<
      std::unique_ptr<lg::UdpClientTransport>,
      lg::kDuelPlayerCount + 1U
    >
      clients = {};
    for (std::size_t index = 0; index < clients.size(); ++index) {
      clients[index] = std::make_unique<lg::UdpClientTransport>(
        "127.0.0.1",
        multiServerTransport.localPort()
      );
      failures += expect(
        clients[index]->initialize(),
        "extra UDP client should initialize"
      );
    }
    if (failures != 0) {
      return 1;
    }

    for (int iteration = 0; iteration < 400; ++iteration) {
      for (const auto& client : clients) {
        client->update();
      }
      multiServerTransport.update();
      syncConnectedPlayers(multiServer, multiServerTransport);
      multiServer.tick(lg::kFixedTickSeconds);
      for (const auto& client : clients) {
        client->update();
      }
      const bool allConnected = std::all_of(
        clients.begin(),
        clients.end(),
        [](const auto& client) { return client->connected(); }
      );
      if (allConnected) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    std::array<bool, lg::kDuelPlayerCount> seenSlots = {};
    for (const auto& client : clients) {
      failures += expect(client->connected(), "all UDP clients should connect");
      if (client->connected() && client->playerIndex() < lg::kDuelPlayerCount) {
        seenSlots[client->playerIndex()] = true;
      }
    }
    failures += expect(
      std::all_of(seenSlots.begin(), seenSlots.end(), [](bool seen) { return seen; }),
      "the first six UDP clients should fill unique player slots"
    );
    failures += expect(
      clients.back()->spectator() &&
        clients.back()->clientIndex() >= lg::kDuelPlayerCount &&
        clients.back()->playerIndex() == lg::kNoAssignedPlayer,
      "an overflow client slot should become a spectator without a body"
    );
    const auto fullPlayableRoster = multiServerTransport.connectedPlayers();
    failures += expect(
      multiServerTransport.connectedClientCount() == lg::kDuelPlayerCount + 1U &&
        std::count(
          fullPlayableRoster.begin(), fullPlayableRoster.end(),
          true
        ) == lg::kDuelPlayerCount,
      "spectator capacity should be separate from playable bodies"
    );

    lg::CommandPacket spectatorChat;
    spectatorChat.command.sequence = 7;
    spectatorChat.playerName = "OBSERVER";
    spectatorChat.chatMessage = "spectator chat";
    clients.back()->sendCommand(spectatorChat);
    bool receivedSpectatorChat = false;
    for (int iteration = 0; iteration < 12; ++iteration) {
      for (const auto& client : clients) client->update();
      multiServerTransport.update();
      syncConnectedPlayers(multiServer, multiServerTransport);
      multiServer.tick(lg::kFixedTickSeconds);
      for (const auto& client : clients) client->update();
      lg::ChatHistoryChunk chunk;
      while (clients[1]->receiveChatHistory(chunk)) {
        for (std::size_t messageIndex = 0;
             messageIndex < chunk.messageCount;
             ++messageIndex) {
          const lg::ChatMessage& message = chunk.messages[messageIndex];
          receivedSpectatorChat = receivedSpectatorChat ||
            (
              message.playerIndex == lg::kNoAssignedPlayer &&
              message.speakerName == "OBSERVER" &&
              message.message == "spectator chat"
            );
        }
      }
    }
    failures += expect(
      receivedSpectatorChat,
      "an overflow spectator should send connection-authenticated chat without a body"
    );

    lg::CommandPacket selectClanArena;
    selectClanArena.command.sequence = 40;
    selectClanArena.requestGameMode = true;
    selectClanArena.requestedGameMode = lg::GameMode::ClanArena;
    clients[1]->sendCommand(selectClanArena);
    for (int iteration = 0; iteration < 8; ++iteration) {
      for (const auto& client : clients) client->update();
      multiServerTransport.update();
      syncConnectedPlayers(multiServer, multiServerTransport);
      multiServer.tick(lg::kFixedTickSeconds);
      for (const auto& client : clients) client->update();
    }
    failures += expect(
      multiServer.snapshot().gameMode == lg::GameMode::ClanArena,
      "multi-client UDP test should enter a team mode before testing team assignment"
    );

    lg::CommandPacket staleOldBodyCommand;
    staleOldBodyCommand.command.sequence = 100000;
    staleOldBodyCommand.command.forwardMove = 1.0F;
    clients[0]->sendCommand(staleOldBodyCommand);
    lg::CommandPacket becomeSpectator;
    becomeSpectator.command.sequence = 100001;
    becomeSpectator.requestSpectator = true;
    clients[0]->sendCommand(becomeSpectator);

    lg::CommandPacket joinRed;
    // The caller's body value is deliberately wrong. UDP transport stamps the
    // observed sentinel, authenticates with clientIndex, and the server assigns
    // the released body rather than trusting either client-provided body value.
    joinRed.command.sequence = 1;
    joinRed.playerIndex = 0;
    joinRed.requestTeam = true;
    joinRed.requestedTeam = lg::Team::Red;
    clients.back()->sendCommand(joinRed);
    for (int iteration = 0; iteration < 8; ++iteration) {
      for (const auto& client : clients) client->update();
      multiServerTransport.update();
      syncConnectedPlayers(multiServer, multiServerTransport);
      multiServer.tick(lg::kFixedTickSeconds);
      for (const auto& client : clients) client->update();
      lg::ServerSnapshot ignored;
      while (clients[0]->receiveSnapshot(ignored)) {}
      while (clients.back()->receiveSnapshot(ignored)) {}
    }
    failures += expect(
      clients[0]->spectator(),
      "a simultaneous spectator request should release the prior body"
    );
    failures += expect(
      !clients.back()->spectator() &&
        clients.back()->playerIndex() < lg::kDuelPlayerCount,
      "a simultaneous team request should claim the released body"
    );
    if (clients.back()->playerIndex() < lg::kDuelPlayerCount) {
      const std::size_t reassignedPlayer = clients.back()->playerIndex();
      failures += expect(
        multiServer.snapshot().teams[reassignedPlayer] == lg::Team::Red,
        "the reassigned body should use the requested team"
      );
      failures += expect(
        multiServer.snapshot().acknowledgedCommand[reassignedPlayer] ==
          joinRed.command.sequence,
        "stale commands from the prior body session must not reject the new occupant's low sequence"
      );
    }
  }

  return failures == 0 ? 0 : 1;
}
