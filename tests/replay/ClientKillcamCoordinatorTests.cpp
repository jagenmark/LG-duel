#include "net/LoopbackTransport.hpp"
#include "replay/ClientKillcamCoordinator.hpp"
#include "replay/ReplayCodec.hpp"
#include "replay/ReplayIoService.hpp"
#include "replay/ReplayRuntime.hpp"
#include "server/ServerGame.hpp"
#include "shared/Constants.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

constexpr std::uint32_t kSessionId = 77U;
constexpr std::uint32_t kGeneration = 9U;
constexpr std::uint32_t kLethalSequence = 4U;

int expect(bool condition, std::string_view message) {
  if (condition) return 0;
  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

void discardSnapshots(lg::LoopbackTransport& transport) {
  lg::ServerSnapshot snapshot;
  while (transport.receiveSnapshot(snapshot)) {}
}

lg::replay::ReplayDemo makeKillcamDemo(lg::ServerSnapshot& liveSnapshot) {
  lg::LoopbackTransport transport;
  lg::ServerGame source(transport);
  source.setMapDirectory("maps");
  if (!source.loadRequestedMap("eyetoeye")) return {};
  source.setConnectedPlayers({true, true});
  lg::MatchRules rules;
  rules.countdownTicks = 0U;
  source.setMatchRules(rules);
  for (std::size_t slot = 0U; slot < lg::kDuelPlayerCount; ++slot) {
    lg::CommandPacket ready;
    ready.playerIndex = static_cast<std::uint8_t>(slot);
    ready.command.sequence = static_cast<std::uint32_t>(slot + 1U);
    ready.toggleReady = true;
    ready.playerName = slot == 0U ? "VICTIM" : "KILLER";
    transport.sendCommand(ready);
  }
  source.tick(lg::kFixedTickSeconds);
  discardSnapshots(transport);

  lg::replay::ReplayRecordingConfig config;
  config.checkpointIntervalTicks = 2U;
  config.hashIntervalTicks = 1U;
  std::string error;
  if (!source.beginReplayRecording(config, &error)) return {};
  for (std::uint32_t tick = 0U; tick < 8U; ++tick) {
    source.tick(lg::kFixedTickSeconds);
    discardSnapshots(transport);
  }
  std::optional<lg::replay::ReplayDemo> demo = source.finishReplayRecording();
  if (!demo.has_value() || demo->ticks.empty()) return {};
  demo->metadata.visibility = lg::replay::ReplayVisibility::DuelOnly;
  demo->lethalEvents.push_back({
    demo->ticks.back().tick,
    kGeneration,
    0U,
    1U,
    lg::Weapon::RocketLauncher,
    0U,
    lg::replay::LethalKind::Splash,
    kLethalSequence,
  });
  liveSnapshot = source.snapshot();
  liveSnapshot.hasLocalClientState = true;
  liveSnapshot.localSpectator = false;
  liveSnapshot.localPlayerIndex = 0U;
  liveSnapshot.players[0].health = 0;
  liveSnapshot.respawnTicksRemaining[0] = 1U;
  return std::move(*demo);
}

bool queueKillcamDecode(
  lg::replay::ClientKillcamCoordinator& coordinator,
  const std::vector<std::uint8_t>& bytes,
  const lg::replay::ClientKillcamLiveView& live,
  std::uint32_t transferId,
  std::uint64_t& nowMilliseconds
) {
  lg::replay::ReplayTransferConfig config;
  config.sessionId = live.sessionId;
  config.retryMilliseconds = 1U;
  config.timeoutMilliseconds = 1000U;
  config.minimumPacketIntervalMilliseconds = 1U;
  lg::replay::ReplayTransferSender sender;
  if (!sender.begin(
        transferId,
        kGeneration,
        bytes,
        nowMilliseconds,
        config,
        kLethalSequence
      )) {
    return false;
  }

  for (std::size_t attempt = 0U;
       attempt < 1024U && !sender.complete();
       ++attempt, ++nowMilliseconds) {
    if (const auto message = sender.nextMessage(nowMilliseconds);
        message.has_value()) {
      coordinator.receiveTransfer(*message, live, nowMilliseconds);
    }
    while (auto response = coordinator.takeOutbound()) {
      if (const auto* ack = std::get_if<lg::replay::ReplayTransferAck>(
            &*response
          )) {
        sender.acknowledge(*ack);
      }
    }
  }
  return sender.complete() && coordinator.status().decodePending;
}

bool startKillcam(
  lg::replay::ClientKillcamCoordinator& coordinator,
  const std::vector<std::uint8_t>& bytes,
  const lg::replay::ClientKillcamLiveView& live,
  std::uint32_t transferId,
  std::uint64_t& nowMilliseconds
) {
  if (!queueKillcamDecode(
        coordinator,
        bytes,
        live,
        transferId,
        nowMilliseconds
      )) {
    return false;
  }

  for (std::size_t attempt = 0U;
       attempt < 1000U && !coordinator.status().active;
       ++attempt, ++nowMilliseconds) {
    coordinator.update(live, nowMilliseconds, 0.0);
    if (!coordinator.status().active) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  return coordinator.status().active;
}

} // namespace

int main() {
  int failures = 0;
  lg::ServerSnapshot liveSnapshot;
  const lg::replay::ReplayDemo demo = makeKillcamDemo(liveSnapshot);
  std::vector<std::uint8_t> bytes;
  std::string error;
  failures += expect(
    !demo.ticks.empty() && lg::replay::encodeDemo(demo, bytes, &error),
    "client coordinator fixture should encode"
  );
  if (bytes.empty()) return 1;

  lg::replay::ClientKillcamLiveView live;
  live.connected = true;
  live.spectator = false;
  live.sessionId = kSessionId;
  live.playerIndex = 0U;
  live.snapshot = &liveSnapshot;
  lg::replay::ClientKillcamCoordinator coordinator("maps");
  std::uint64_t nowMilliseconds = 1U;

  {
    lg::replay::ReplayIoService::Config blockedConfig;
    blockedConfig.maxPendingJobs = 2U;
    blockedConfig.startWorker = false;
    lg::replay::ReplayIoService blockedLocalIo(blockedConfig);
    lg::replay::ReplayIoService::JobId firstJob = 0U;
    lg::replay::ReplayIoService::JobId secondJob = 0U;
    failures += expect(
      blockedLocalIo.enqueueList(".", firstJob, &error) &&
        blockedLocalIo.enqueueList(".", secondJob, &error),
      "local replay I/O saturation fixture should fill both job slots"
    );
    lg::replay::ClientKillcamCoordinator isolatedCoordinator("maps");
    failures += expect(
      startKillcam(
        isolatedCoordinator,
        bytes,
        live,
        90U,
        nowMilliseconds
      ),
      "local demo file jobs should not block remote killcam decode"
    );
    (void)isolatedCoordinator.skip();
    blockedLocalIo.shutdown();
  }

  lg::replay::ReplayTransferConfig canceledConfig;
  canceledConfig.sessionId = live.sessionId;
  canceledConfig.retryMilliseconds = 1U;
  canceledConfig.timeoutMilliseconds = 1000U;
  canceledConfig.minimumPacketIntervalMilliseconds = 1U;
  lg::replay::ReplayTransferSender canceledSender;
  failures += expect(
    canceledSender.begin(
      99U,
      kGeneration,
      bytes,
      nowMilliseconds,
      canceledConfig,
      kLethalSequence
    ),
    "server-cancel fixture should start a transfer"
  );
  const std::optional<lg::replay::ReplayTransferMessage> canceledBegin =
    canceledSender.nextMessage(nowMilliseconds++);
  failures += expect(
    canceledBegin.has_value() &&
      std::holds_alternative<lg::replay::ReplayTransferBegin>(*canceledBegin),
    "server-cancel fixture should produce a begin"
  );
  if (canceledBegin.has_value()) {
    coordinator.receiveTransfer(*canceledBegin, live, nowMilliseconds++);
    const auto& begin = std::get<lg::replay::ReplayTransferBegin>(
      *canceledBegin
    );
    failures += expect(
      coordinator.status().transferActive &&
        coordinator.status().hasContext &&
        !coordinator.commandAllowed(
          lg::replay::ClientReplayCommand::DemoPlay
        ),
      "an active transfer should own remote context and block local playback"
    );
    coordinator.receiveTransfer(
      lg::replay::ReplayTransferCancel{
        begin.transferId,
        begin.generation,
        lg::replay::ReplayTransferCancelReason::Invalid,
        begin.sessionId,
      },
      live,
      nowMilliseconds++
    );
    failures += expect(
      !coordinator.status().transferActive &&
        !coordinator.status().hasContext &&
        coordinator.commandAllowed(
          lg::replay::ClientReplayCommand::DemoPlay
        ),
      "a matching server cancel should release all remote flow state"
    );
  }
  while (coordinator.takeOutbound().has_value()) {}

  if (canceledBegin.has_value()) {
    const auto& begin = std::get<lg::replay::ReplayTransferBegin>(
      *canceledBegin
    );
    coordinator.receiveTransfer(begin, live, nowMilliseconds++);
    (void)coordinator.takeOutbound();

    const std::vector<std::uint8_t> payload = {bytes.front()};
    lg::replay::ReplayTransferChunk malformedChunk;
    malformedChunk.transferId = begin.transferId;
    malformedChunk.generation = begin.generation;
    malformedChunk.index = 0U;
    malformedChunk.count = static_cast<std::uint16_t>(begin.chunkCount + 1U);
    malformedChunk.payload = payload;
    malformedChunk.crc32 = lg::replay::replayTransferCrc32(payload);
    malformedChunk.sessionId = begin.sessionId;
    coordinator.receiveTransfer(malformedChunk, live, nowMilliseconds++);

    const std::optional<lg::replay::ReplayTransferMessage> rejection =
      coordinator.takeOutbound();
    const auto* cancel = rejection.has_value()
      ? std::get_if<lg::replay::ReplayTransferCancel>(&*rejection)
      : nullptr;
    failures += expect(
      cancel != nullptr &&
        cancel->reason == lg::replay::ReplayTransferCancelReason::Invalid &&
        !coordinator.status().transferActive &&
        !coordinator.status().hasContext &&
        coordinator.commandAllowed(
          lg::replay::ClientReplayCommand::DemoPlay
        ),
      "a malformed chunk should reject and release remote flow at once"
    );
  }

  failures += expect(
    queueKillcamDecode(
      coordinator,
      bytes,
      live,
      100U,
      nowMilliseconds
    ),
    "post-completion cancel fixture should queue a decode"
  );
  failures += expect(
    !coordinator.status().transferActive &&
      coordinator.status().decodePending &&
      coordinator.status().hasContext,
    "completed transfer should retain context while decode is pending"
  );
  coordinator.receiveTransfer(
    lg::replay::ReplayTransferCancel{
      100U,
      kGeneration,
      lg::replay::ReplayTransferCancelReason::Invalid,
      live.sessionId,
    },
    live,
    nowMilliseconds++
  );
  failures += expect(
    !coordinator.status().decodePending &&
      !coordinator.status().hasContext &&
      coordinator.commandAllowed(
        lg::replay::ClientReplayCommand::DemoPlay
      ),
    "a cancel after transfer completion should discard the queued decode"
  );

  failures += expect(
    startKillcam(coordinator, bytes, live, 1U, nowMilliseconds),
    "the coordinator should own a trusted remote killcam"
  );
  const lg::replay::ClientKillcamStatus started = coordinator.status();
  const lg::replay::ClientKillcamHud hud = coordinator.hud();
  failures += expect(
    started.active && started.hasContext && hud.active &&
      hud.killer == "KILLER" && hud.weapon == "rl" &&
      hud.cause == "SPLASH DAMAGE",
    "active playback should retain context and expose killer and cause HUD data"
  );

  constexpr std::array blockedCommands = {
    lg::replay::ClientReplayCommand::DemoPlay,
    lg::replay::ClientReplayCommand::DemoStop,
    lg::replay::ClientReplayCommand::DemoPause,
    lg::replay::ClientReplayCommand::DemoResume,
    lg::replay::ClientReplayCommand::DemoTogglePause,
    lg::replay::ClientReplayCommand::DemoStep,
    lg::replay::ClientReplayCommand::DemoSeek,
    lg::replay::ClientReplayCommand::DemoSpeed,
    lg::replay::ClientReplayCommand::DemoCamera,
    lg::replay::ClientReplayCommand::DemoFollow,
    lg::replay::ClientReplayCommand::DemoList,
    lg::replay::ClientReplayCommand::DemoDelete,
  };
  for (const lg::replay::ClientReplayCommand command : blockedCommands) {
    failures += expect(
      !coordinator.commandAllowed(command),
      "local demo commands should not alter a remote killcam"
    );
  }
  failures += expect(
    coordinator.commandAllowed(lg::replay::ClientReplayCommand::KillcamSkip),
    "killcam_skip should remain the remote playback control"
  );

  liveSnapshot.localPlayerIndex = 1U;
  coordinator.update(live, nowMilliseconds++, 0.0);
  failures += expect(
    !coordinator.status().active && !coordinator.status().hasContext,
    "a changed local body should stop remote playback"
  );
  liveSnapshot.localPlayerIndex = 0U;

  failures += expect(
    startKillcam(coordinator, bytes, live, 2U, nowMilliseconds),
    "the coordinator should start again after a body change"
  );
  live.sessionId = kSessionId + 1U;
  coordinator.update(live, nowMilliseconds++, 0.0);
  failures += expect(
    !coordinator.status().active && !coordinator.status().hasContext,
    "a changed session should stop remote playback"
  );
  live.sessionId = kSessionId;

  failures += expect(
    startKillcam(coordinator, bytes, live, 3U, nowMilliseconds),
    "the coordinator should start again after a session change"
  );
  liveSnapshot.matchPhase = lg::MatchPhase::RoundEnd;
  coordinator.update(live, nowMilliseconds++, 0.0);
  failures += expect(
    !coordinator.status().active && !coordinator.status().hasContext,
    "a changed match state should stop remote playback"
  );
  liveSnapshot.matchPhase = lg::MatchPhase::Live;

  failures += expect(
    startKillcam(coordinator, bytes, live, 4U, nowMilliseconds) &&
      coordinator.skip() && !coordinator.status().active,
    "killcam_skip should stop and release remote playback"
  );
  return failures == 0 ? 0 : 1;
}
