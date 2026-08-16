#include "client/ClientGame.hpp"
#include "net/LoopbackTransport.hpp"
#include "net/UdpTransport.hpp"
#include "replay/KillcamClientReceiver.hpp"
#include "replay/ReplayCodec.hpp"
#include "replay/ReplayRuntime.hpp"
#include "replay/ReplayTransferServer.hpp"
#include "server/ServerGame.hpp"
#include "shared/Constants.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

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

lg::replay::ReplayDemo makeUdpReplayFixture() {
  lg::LoopbackTransport transport;
  lg::ServerGame source(transport);
  source.setMapDirectory("maps");
  if (!source.loadRequestedMap("eyetoeye")) {
    return {};
  }
  source.setConnectedPlayers({true, true});
  lg::MatchRules rules;
  rules.countdownTicks = 0U;
  source.setMatchRules(rules);

  for (std::size_t slot = 0U; slot < lg::kDuelPlayerCount; ++slot) {
    lg::CommandPacket ready;
    ready.playerIndex = static_cast<std::uint8_t>(slot);
    ready.command.sequence = static_cast<std::uint32_t>(slot + 1U);
    ready.toggleReady = true;
    transport.sendCommand(ready);
  }
  source.tick(lg::kFixedTickSeconds);
  lg::ServerSnapshot readySnapshot;
  while (transport.receiveSnapshot(readySnapshot)) {}

  std::string error;
  lg::replay::ReplayRecordingConfig recording;
  recording.checkpointIntervalTicks = 16U;
  recording.hashIntervalTicks = 1U;
  if (!source.beginReplayRecording(recording, &error)) {
    return {};
  }
  for (std::uint32_t offset = 0U; offset < 16U; ++offset) {
    for (std::size_t slot = 0U; slot < lg::kDuelPlayerCount; ++slot) {
      lg::CommandPacket command;
      command.playerIndex = static_cast<std::uint8_t>(slot);
      command.command.sequence =
        10U + offset * 2U + static_cast<std::uint32_t>(slot);
      command.command.forwardMove = slot == 0U ? 1.0F : -1.0F;
      command.command.viewYawRadians = slot == 0U ? 0.0F : 3.1415926F;
      command.viewedServerTick = source.snapshot().serverTick;
      transport.sendCommand(command);
    }
    source.tick(lg::kFixedTickSeconds);
    lg::ServerSnapshot ignored;
    while (transport.receiveSnapshot(ignored)) {}
  }

  std::optional<lg::replay::ReplayDemo> demo = source.finishReplayRecording();
  if (!demo.has_value() || demo->ticks.empty() || demo->checkpoints.empty()) {
    return {};
  }
  demo->hashes.clear();
  demo->lethalEvents.push_back({
    demo->ticks.back().tick,
    1U,
    0U,
    1U,
    lg::Weapon::Railgun,
    17U,
    lg::replay::LethalKind::Direct,
    19U,
  });
  return *demo;
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
  failures += expect(
    firstTransport.sessionId() != 0U && secondTransport.sessionId() != 0U &&
      firstTransport.sessionId() != secondTransport.sessionId(),
    "UDP clients should receive distinct authenticated sessions"
  );
  failures += expect(serverTransport.connectedClientCount() == 2, "server should track two UDP clients");
  if (failures != 0) {
    return 1;
  }

  {
    const lg::replay::ReplayDemo fixture = makeUdpReplayFixture();
    std::vector<std::uint8_t> fixtureBytes;
    std::string error;
    failures += expect(
      !fixture.ticks.empty() && !fixture.lethalEvents.empty() &&
        lg::replay::encodeDemo(fixture, fixtureBytes, &error),
      "UDP killcam fixture should encode a replay with a lethal event"
    );
    if (!fixtureBytes.empty()) {
      lg::ClientNetworkSimulationConfig simulation;
      simulation.latencyMs = 2;
      simulation.jitterMs = 1;
      simulation.lossPercent = 15;
      simulation.reorderPercent = 45;
      simulation.seed = 0xC01DCA5U;
      firstTransport.setNetworkSimulationConfig(simulation);

      lg::replay::ReplayTransferServerConfig transferConfig;
      transferConfig.maximumSegmentBytes =
        lg::replay::kReplayTransferMaxSegmentBytes;
      transferConfig.transfer.retryMilliseconds = 5U;
      transferConfig.transfer.timeoutMilliseconds = 4000U;
      transferConfig.transfer.minimumPacketIntervalMilliseconds = 1U;
      lg::replay::ReplayTransferServer transferServer(transferConfig);
      lg::replay::KillcamClientReceiver receiver({1000U, 5000U});
      receiver.bindSession(firstTransport.sessionId());
      const std::uint8_t firstClientIndex = firstTransport.clientIndex();
      const std::uint32_t firstSessionId = firstTransport.sessionId();
      const lg::replay::ReplayLethalEvent& lethal = fixture.lethalEvents.front();
      failures += expect(
        firstSessionId != 0U && firstClientIndex != lg::kNoAssignedPlayer &&
          transferServer.start(
            firstClientIndex,
            firstSessionId,
            lethal.replayGeneration,
            fixtureBytes,
            0U,
            &error,
            lethal.sequence
          ),
        "UDP killcam server should start for the authenticated client"
      );

      std::optional<std::vector<std::uint8_t>> receivedBytes;
      std::size_t outboundPacketCount = 0U;
      const auto transferStart = std::chrono::steady_clock::now();
      for (std::size_t iteration = 0U;
           iteration < 6000U && !receivedBytes.has_value();
           ++iteration) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - transferStart
        ).count();
        const std::uint64_t nowMilliseconds =
          static_cast<std::uint64_t>(std::max<std::int64_t>(0, elapsed));
        firstTransport.update();
        secondTransport.update();
        serverTransport.update();

        std::uint8_t clientIndex = 0U;
        lg::replay::ReplayTransferMessage serverMessage;
        while (serverTransport.receiveReplayTransferMessage(
                 clientIndex, serverMessage
               )) {
          transferServer.receive(
            clientIndex,
            serverTransport.clientSession(clientIndex),
            serverMessage
          );
        }

        lg::replay::ReplayTransferMessage clientMessage;
        while (firstTransport.receiveReplayTransferMessage(clientMessage)) {
          const std::optional<lg::replay::ReplayTransferMessage> response =
            receiver.receive(clientMessage, nowMilliseconds);
          if (response.has_value()) {
            (void)firstTransport.sendReplayTransferMessage(*response);
          }
        }
        if (!receiver.active() && !receiver.failed() && !receivedBytes.has_value()) {
          receivedBytes = receiver.takeCompleted();
        }
        const std::optional<lg::replay::ReplayTransferMessage> timeoutMessage =
          receiver.update(nowMilliseconds);
        if (timeoutMessage.has_value()) {
          (void)firstTransport.sendReplayTransferMessage(*timeoutMessage);
        }

        const std::vector<lg::replay::ReplayTransferOutbound> outbound =
          transferServer.poll(nowMilliseconds, 1U);
        for (const lg::replay::ReplayTransferOutbound& packet : outbound) {
          lg::WirePacket wire;
          failures += expect(
            lg::encodeReplayTransferPacket(packet.message, wire) &&
              wire.size() <= lg::replay::kReplayTransferMaxDatagramBytes,
            "UDP killcam packets should stay within the datagram bound"
          );
          ++outboundPacketCount;
          failures += expect(
            serverTransport.sendReplayTransferMessage(
              packet.clientIndex, packet.message
            ),
            "UDP server should send each authenticated killcam packet"
          );
        }
        if (!receiver.active() && !receiver.failed() && !receivedBytes.has_value()) {
          receivedBytes = receiver.takeCompleted();
        }
        if (receivedBytes.has_value()) {
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }

      failures += expect(
        receivedBytes.has_value() && *receivedBytes == fixtureBytes,
        "UDP loss and reorder should still reassemble the exact killcam"
      );
      const lg::ClientNetworkSimulationStats transferStats =
        firstTransport.networkSimulationStats();
      failures += expect(
        outboundPacketCount > 2U &&
          (transferStats.droppedIncomingPackets > 0U ||
           transferStats.droppedOutgoingPackets > 0U ||
           transferStats.reorderedIncomingPackets > 0U ||
           transferStats.reorderedOutgoingPackets > 0U),
        "UDP killcam test should exercise loss or reorder"
      );
      lg::replay::ReplayDemo decoded;
      failures += expect(
        receivedBytes.has_value() &&
          lg::replay::decodeDemo(*receivedBytes, decoded, &error) &&
          decoded.lethalEvents.size() == 1U &&
          decoded.lethalEvents.front().victim == 0U &&
          decoded.lethalEvents.front().killer == 1U &&
          decoded.lethalEvents.front().sequence == 19U,
        "UDP killcam should decode its lethal event after transfer"
      );
      if (receivedBytes.has_value()) {
        lg::replay::ReplayRuntimeConfig runtimeConfig;
        runtimeConfig.mapDirectory = "maps";
        lg::replay::ReplayRuntime runtime(std::move(decoded), runtimeConfig);
        failures += expect(
          runtime.start(&error) && runtime.active(),
          "received UDP killcam should load through ReplayRuntime"
        );
        if (runtime.active()) {
          const bool playbackAdvanced =
            runtime.resume() && runtime.advance(0.05, &error);
          failures += expect(
            playbackAdvanced,
            "received UDP killcam should advance through presentation"
          );
          runtime.stop();
        }
      }

      lg::ServerSnapshot liveSnapshot;
      bool receivedLiveSnapshot = false;
      for (std::size_t iteration = 0U; iteration < 200U; ++iteration) {
        serverTransport.sendSnapshot(server.snapshot());
        firstTransport.update();
        receivedLiveSnapshot = firstTransport.receiveSnapshot(liveSnapshot);
        if (receivedLiveSnapshot) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      failures += expect(
        firstTransport.connected() && receivedLiveSnapshot,
        "client should return to live UDP snapshots after killcam playback"
      );
      lg::replay::ReplayTransferMessage secondMessage;
      failures += expect(
        !secondTransport.receiveReplayTransferMessage(secondMessage),
        "a second client must not receive another client's killcam"
      );
      firstTransport.setNetworkSimulationConfig({});
      lg::ServerSnapshot ignored;
      for (std::size_t iteration = 0U; iteration < 32U; ++iteration) {
        firstTransport.update();
        while (firstTransport.receiveSnapshot(ignored)) {}
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    }
  }

  {
    lg::ServerSnapshot ignored;
    while (firstTransport.receiveSnapshot(ignored)) {}
    while (secondTransport.receiveSnapshot(ignored)) {}

    lg::ServerSnapshot feedbackSnapshot = server.snapshot();
    const std::size_t firstPlayerIndex = firstTransport.playerIndex();
    const std::size_t secondPlayerIndex = secondTransport.playerIndex();
    auto addVictimEvent = [&](std::size_t victim, std::size_t attacker,
                              std::uint32_t sequence) {
      lg::DamageTakenEventRing& ring =
        feedbackSnapshot.damageTakenEvents[victim];
      ring.events[0] = {
        sequence,
        64U,
        20U,
        static_cast<std::uint8_t>(
          lg::kDamageTakenDirectionValid |
          lg::kDamageTakenAttackerValid |
          (static_cast<std::uint8_t>(attacker) << 4U)
        ),
        lg::Weapon::Railgun,
      };
      (void)lg::setDamageTakenEventActive(ring, 0);
    };
    feedbackSnapshot.localHitFeedbackEvents[firstPlayerIndex][0] = {
      700U,
      20,
      static_cast<std::uint8_t>(secondPlayerIndex),
      lg::Weapon::Railgun,
      false,
      true,
    };
    feedbackSnapshot.localHitFeedbackEvents[secondPlayerIndex][0] = {
      701U,
      20,
      static_cast<std::uint8_t>(firstPlayerIndex),
      lg::Weapon::Railgun,
      false,
      true,
    };
    addVictimEvent(firstPlayerIndex, secondPlayerIndex, 800U);
    addVictimEvent(secondPlayerIndex, firstPlayerIndex, 801U);
    serverTransport.sendSnapshot(feedbackSnapshot);

    lg::ServerSnapshot firstFeedback;
    lg::ServerSnapshot secondFeedback;
    bool receivedFirstFeedback = false;
    bool receivedSecondFeedback = false;
    for (int attempt = 0; attempt < 20; ++attempt) {
      if (!receivedFirstFeedback) {
        lg::ServerSnapshot received;
        if (firstTransport.receiveSnapshot(received) &&
            lg::damageTakenEventActive(
              received.damageTakenEvents[firstPlayerIndex], 0
            ) &&
            received.damageTakenEvents[firstPlayerIndex].events[0].sequence ==
              800U) {
          firstFeedback = received;
          receivedFirstFeedback = true;
        }
      }
      if (!receivedSecondFeedback) {
        lg::ServerSnapshot received;
        if (secondTransport.receiveSnapshot(received) &&
            lg::damageTakenEventActive(
              received.damageTakenEvents[secondPlayerIndex], 0
            ) &&
            received.damageTakenEvents[secondPlayerIndex].events[0].sequence ==
              801U) {
          secondFeedback = received;
          receivedSecondFeedback = true;
        }
      }
      if (receivedFirstFeedback && receivedSecondFeedback) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    failures += expect(
      receivedFirstFeedback && receivedSecondFeedback,
      "both recipients should receive the directed feedback snapshot"
    );
    failures += expect(
      firstFeedback.localHitFeedbackEvents[firstPlayerIndex][0].active &&
        !firstFeedback.localHitFeedbackEvents[secondPlayerIndex][0].active &&
        lg::damageTakenEventActive(
          firstFeedback.damageTakenEvents[firstPlayerIndex], 0
        ) &&
        !lg::damageTakenEventActive(
          firstFeedback.damageTakenEvents[secondPlayerIndex], 0
        ),
      "a player should receive only its attacker and victim feedback rows"
    );
    failures += expect(
      secondFeedback.localHitFeedbackEvents[secondPlayerIndex][0].active &&
        !secondFeedback.localHitFeedbackEvents[firstPlayerIndex][0].active &&
        lg::damageTakenEventActive(
          secondFeedback.damageTakenEvents[secondPlayerIndex], 0
        ) &&
        !lg::damageTakenEventActive(
          secondFeedback.damageTakenEvents[firstPlayerIndex], 0
        ),
      "unrelated players should not receive another player's feedback rows"
    );
  }

  lg::ProjectileUpdatePacket projectileSource;
  projectileSource.serverTick = 77U;
  projectileSource.mapRevision = server.snapshot().mapRevision;
  projectileSource.projectileRevision =
    server.snapshot().projectileRevision;
  projectileSource.updateCount = 2U;
  projectileSource.updates[0].slot = 31U;
  projectileSource.updates[0].sequence = 101U;
  projectileSource.updates[0].kind = lg::ProjectileUpdateKind::Spawn;
  projectileSource.updates[0].weapon = lg::Weapon::RocketLauncher;
  projectileSource.updates[0].position = {1.0F, 2.0F, 3.0F};
  projectileSource.updates[0].velocity = {4.0F, 5.0F, 6.0F};
  projectileSource.updates[0].ageTicks = 8U;
  projectileSource.updates[1].slot =
    static_cast<std::uint16_t>(lg::kMaxRocketProjectiles - 1U);
  projectileSource.updates[1].sequence = 102U;
  projectileSource.updates[1].kind = lg::ProjectileUpdateKind::Remove;
  projectileSource.updates[1].weapon = lg::Weapon::GrenadeLauncher;
  projectileSource.updates[1].position = {-1.0F, -2.0F, 0.5F};
  projectileSource.updates[1].velocity = {0.0F, 0.0F, 0.0F};
  projectileSource.updates[1].radius = 0.15F;
  projectileSource.updates[1].ageTicks = 90U;
  projectileSource.updates[1].resting = true;
  serverTransport.sendProjectileUpdates(projectileSource);
  firstTransport.update();
  secondTransport.update();

  lg::ProjectileUpdatePacket firstProjectilePacket;
  lg::ProjectileUpdatePacket secondProjectilePacket;
  failures += expect(
    firstTransport.receiveProjectileUpdates(firstProjectilePacket) &&
      secondTransport.receiveProjectileUpdates(secondProjectilePacket),
    "UDP server should send projectile updates to every active client"
  );
  failures += expect(
    firstProjectilePacket.serverTick == projectileSource.serverTick &&
      firstProjectilePacket.projectileRevision ==
        projectileSource.projectileRevision &&
      firstProjectilePacket.updateCount == projectileSource.updateCount &&
      firstProjectilePacket.updates[0].slot == 31U &&
      firstProjectilePacket.updates[1].slot ==
        lg::kMaxRocketProjectiles - 1U &&
      firstProjectilePacket.updates[1].kind ==
        lg::ProjectileUpdateKind::Remove &&
      firstProjectilePacket.updates[1].resting &&
      secondProjectilePacket.updates[0].sequence == 101U,
    "UDP projectile update fields should survive server-to-client transport"
  );
  firstTransport.sendProjectileUpdates(projectileSource);
  serverTransport.update();
  failures += expect(
    !serverTransport.receiveProjectileUpdates(firstProjectilePacket),
    "UDP clients must not send projectile-authoritative state to the server"
  );

  lg::ProjectileUpdatePacket queuedProjectilePacket = projectileSource;
  queuedProjectilePacket.updateCount = 0U;
  constexpr std::uint32_t kQueueTestFirstTick = 1000U;
  for (
    std::size_t index = 0;
    index < lg::kMaxQueuedProjectileUpdatePackets + 16U;
    ++index
  ) {
    queuedProjectilePacket.serverTick =
      kQueueTestFirstTick + static_cast<std::uint32_t>(index);
    serverTransport.sendProjectileUpdates(queuedProjectilePacket);
  }
  firstTransport.update();
  secondTransport.update();
  std::size_t queuedPacketCount = 0U;
  std::uint32_t oldestQueuedTick = 0U;
  while (firstTransport.receiveProjectileUpdates(firstProjectilePacket)) {
    if (queuedPacketCount == 0U) {
      oldestQueuedTick = firstProjectilePacket.serverTick;
    }
    ++queuedPacketCount;
  }
  while (secondTransport.receiveProjectileUpdates(secondProjectilePacket)) {}
  failures += expect(
    queuedPacketCount == lg::kMaxQueuedProjectileUpdatePackets &&
      oldestQueuedTick == kQueueTestFirstTick + 16U,
    "UDP projectile receive queue should keep only its newest bounded window"
  );

  lg::ProjectileUpdatePacket staleProjectilePacket = queuedProjectilePacket;
  staleProjectilePacket.serverTick = kQueueTestFirstTick + 100U;
  staleProjectilePacket.projectileRevision =
    projectileSource.projectileRevision == 1U
      ? std::numeric_limits<std::uint32_t>::max()
      : projectileSource.projectileRevision - 1U;
  serverTransport.sendProjectileUpdates(staleProjectilePacket);
  firstTransport.update();
  secondTransport.update();
  failures += expect(
    !firstTransport.receiveProjectileUpdates(firstProjectilePacket),
    "UDP client should reject projectile packets from an old generation"
  );
  while (secondTransport.receiveProjectileUpdates(secondProjectilePacket)) {}

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
    command.playerName = "LOSS-RECOVERY";
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
  failures += expect(
    firstClient.snapshot().playerNames[firstPlayerIndex] == "LOSS-RECOVERY",
    "a name revision should recover after simulated snapshot and command loss"
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
      "the first sixteen UDP clients should fill unique player slots"
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

    {
      lg::ServerSnapshot ignored;
      for (const auto& client : clients) {
        while (client->receiveSnapshot(ignored)) {}
      }

      const std::size_t firstPlayer = clients[0]->playerIndex();
      const std::size_t secondPlayer = clients[1]->playerIndex();
      lg::ServerSnapshot feedbackSnapshot = multiServer.snapshot();
      auto addFeedback = [&](std::size_t victim, std::size_t attacker,
                             std::uint32_t sequence) {
        feedbackSnapshot.localHitFeedbackEvents[attacker][0] = {
          sequence,
          10,
          static_cast<std::uint8_t>(victim),
          lg::Weapon::Railgun,
          false,
          true,
        };
        lg::DamageTakenEventRing& ring =
          feedbackSnapshot.damageTakenEvents[victim];
        ring.events[0] = {
          sequence,
          64U,
          10U,
          static_cast<std::uint8_t>(
            lg::kDamageTakenDirectionValid |
            lg::kDamageTakenAttackerValid |
            (static_cast<std::uint8_t>(attacker) << 4U)
          ),
          lg::Weapon::Railgun,
        };
        (void)lg::setDamageTakenEventActive(ring, 0U);
      };
      addFeedback(firstPlayer, secondPlayer, 900U);
      addFeedback(secondPlayer, firstPlayer, 901U);
      multiServerTransport.sendSnapshot(feedbackSnapshot);

      lg::ServerSnapshot firstRecipient;
      lg::ServerSnapshot spectatorRecipient;
      bool gotFirstRecipient = false;
      bool gotSpectatorRecipient = false;
      for (int iteration = 0; iteration < 20; ++iteration) {
        for (const auto& client : clients) client->update();
        if (!gotFirstRecipient) {
          gotFirstRecipient = clients[0]->receiveSnapshot(firstRecipient);
        }
        if (!gotSpectatorRecipient) {
          gotSpectatorRecipient =
            clients.back()->receiveSnapshot(spectatorRecipient);
        }
        if (gotFirstRecipient && gotSpectatorRecipient) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      const auto hasVictimEvents = [](const lg::ServerSnapshot& snapshot) {
        return std::any_of(
          snapshot.damageTakenEvents.begin(),
          snapshot.damageTakenEvents.end(),
          [](const lg::DamageTakenEventRing& ring) {
            return ring.activeMask != 0U;
          }
        );
      };
      const auto hasHitFeedback = [](const lg::ServerSnapshot& snapshot) {
        return std::any_of(
          snapshot.localHitFeedbackEvents.begin(),
          snapshot.localHitFeedbackEvents.end(),
          [](const auto& row) {
            return std::any_of(
              row.begin(), row.end(),
              [](const lg::LocalHitFeedbackEvent& event) {
                return event.active;
              }
            );
          }
        );
      };
      failures += expect(
        gotFirstRecipient &&
          lg::damageTakenEventActive(
            firstRecipient.damageTakenEvents[firstPlayer], 0U
          ) &&
          !lg::damageTakenEventActive(
            firstRecipient.damageTakenEvents[secondPlayer], 0U
          ) &&
          firstRecipient.localHitFeedbackEvents[firstPlayer][0].active &&
          !firstRecipient.localHitFeedbackEvents[secondPlayer][0].active,
        "a player snapshot should contain only its own victim and attacker feedback"
      );
      failures += expect(
        gotSpectatorRecipient && !hasVictimEvents(spectatorRecipient) &&
          !hasHitFeedback(spectatorRecipient),
        "a spectator snapshot should not carry player feedback rows"
      );
    }

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

  {
    lg::UdpServerTransport capacityTransport(0);
    failures += expect(
      capacityTransport.initialize(),
      "capacity UDP server should bind an ephemeral port"
    );
    lg::ServerGame capacityServer(capacityTransport);
    std::array<
      std::unique_ptr<lg::UdpClientTransport>,
      lg::kMaxNetworkClients
    > capacityClients = {};
    for (auto& client : capacityClients) {
      client = std::make_unique<lg::UdpClientTransport>(
        "127.0.0.1",
        capacityTransport.localPort()
      );
      failures += expect(client->initialize(), "capacity client should initialize");
    }
    for (int iteration = 0; iteration < 600; ++iteration) {
      for (const auto& client : capacityClients) client->update();
      capacityTransport.update();
      syncConnectedPlayers(capacityServer, capacityTransport);
      capacityServer.tick(lg::kFixedTickSeconds);
      for (const auto& client : capacityClients) client->update();
      if (std::all_of(
            capacityClients.begin(), capacityClients.end(),
            [](const auto& client) { return client->connected(); }
          )) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    failures += expect(
      std::all_of(
        capacityClients.begin(), capacityClients.end(),
        [](const auto& client) { return client->connected(); }
      ) &&
        std::count_if(
          capacityClients.begin(), capacityClients.end(),
          [](const auto& client) { return client->spectator(); }
        ) == static_cast<std::ptrdiff_t>(lg::kMaxSpectatorClients) &&
        capacityTransport.connectedClientCount() == lg::kMaxNetworkClients,
      "UDP transport should fill sixteen player and eight spectator slots"
    );
  }

  return failures == 0 ? 0 : 1;
}
