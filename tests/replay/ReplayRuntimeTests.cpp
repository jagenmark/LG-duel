#include "net/LoopbackTransport.hpp"
#include "replay/ReplayCodec.hpp"
#include "replay/ReplayRuntime.hpp"
#include "server/ServerGame.hpp"

#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) return 0;
  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

void discardSnapshots(lg::LoopbackTransport& transport) {
  lg::ServerSnapshot snapshot;
  while (transport.receiveSnapshot(snapshot)) {}
}

void readyBoth(lg::ServerGame& game, lg::LoopbackTransport& transport) {
  for (std::size_t index = 0; index < 2U; ++index) {
    lg::CommandPacket command;
    command.playerIndex = static_cast<std::uint8_t>(index);
    command.command.sequence = static_cast<std::uint32_t>(index + 1U);
    command.toggleReady = true;
    transport.sendCommand(command);
  }
  game.tick(lg::kFixedTickSeconds);
  discardSnapshots(transport);
}

lg::replay::ReplayDemo makeRecordedDemo(lg::ServerSnapshot& finalSnapshot) {
  lg::LoopbackTransport transport;
  lg::ServerGame source(transport);
  source.setMapDirectory("maps");
  const bool mapLoaded = source.loadRequestedMap("eyetoeye");
  if (!mapLoaded) return {};
  lg::MatchRules rules;
  rules.countdownTicks = 0U;
  source.setMatchRules(rules);
  readyBoth(source, transport);
  lg::replay::ReplayGameplayConfig customConfig = source.captureReplayGameplayConfig();
  customConfig.movementTuning.maxGroundSpeed = 9.0F;
  customConfig.balance.rocketLauncher.speed = 31.0F;
  std::string error;
  if (!source.applyReplayGameplayConfig(customConfig, &error)) return {};
  lg::replay::ReplayRecordingConfig config;
  config.checkpointIntervalTicks = 2U;
  config.hashIntervalTicks = 1U;
  if (!source.beginReplayRecording(config, &error)) return {};
  for (std::uint32_t tick = 0; tick < 32U; ++tick) {
    if (tick == 16U) {
      lg::replay::ReplayGameplayConfig boundaryConfig =
        source.captureReplayGameplayConfig();
      boundaryConfig.movementTuning.maxGroundSpeed = 10.0F;
      if (!source.applyReplayGameplayConfig(boundaryConfig, &error)) return {};
    }
    for (std::size_t index = 0; index < 2U; ++index) {
      lg::CommandPacket command;
      command.playerIndex = static_cast<std::uint8_t>(index);
      command.command.sequence = 10U + tick * 2U + static_cast<std::uint32_t>(index);
      command.command.forwardMove = index == 0U ? 1.0F : -1.0F;
      command.command.viewYawRadians = index == 0U ? 0.0F : 3.1415926F;
      command.viewedServerTick = source.snapshot().serverTick;
      transport.sendCommand(command);
    }
    source.tick(lg::kFixedTickSeconds);
    discardSnapshots(transport);
  }
  finalSnapshot = source.snapshot();
  return source.finishReplayRecording().value_or(lg::replay::ReplayDemo{});
}

} // namespace

int main() {
  int failures = 0;
  lg::ServerSnapshot finalSnapshot;
  const lg::replay::ReplayDemo demo = makeRecordedDemo(finalSnapshot);
  failures += expect(!demo.ticks.empty(), "runtime fixture should record input ticks");
  failures += expect(!demo.checkpoints.empty(), "runtime fixture should record checkpoints");
  failures += expect(!demo.authorityBoundaries.empty(),
    "runtime fixture should retain a custom-config authority boundary");
  if (demo.ticks.empty()) return 1;

  lg::LoopbackTransport liveTransport;
  lg::ServerGame live(liveTransport);
  live.setMapDirectory("maps");
  const auto liveBefore = live.captureReplayCheckpoint();
  lg::replay::ReplayRuntimeConfig config;
  config.mapDirectory = "maps";
  config.maxTicksPerUpdate = 2U;
  lg::replay::ReplayRuntime runtime(demo, config);
  std::string error;
  failures += expect(runtime.start(&error), "runtime should load the recorded map and checkpoint");
  failures += expect(runtime.active() && runtime.frame().valid,
    "runtime should expose a replay presentation frame");
  failures += expect(live.captureReplayCheckpoint().serverTick == liveBefore.serverTick &&
    lg::replay::canonicalStateHash(live.captureReplayCheckpoint()) ==
      lg::replay::canonicalStateHash(liveBefore),
    "starting replay should not alter the live ServerGame");
  failures += expect(runtime.setCameraMode(lg::replay::ReplayCameraMode::Chase),
    "runtime should support chase camera control");
  failures += expect(runtime.setFollowSlot(1U), "runtime should support follow control");
  failures += expect(runtime.setSpeed(2.0F), "runtime should support speed control");
  failures += expect(runtime.resume(), "runtime should resume from its paused start state");
  failures += expect(runtime.advance(0.1, &error), "runtime should advance with bounded catch-up");
  failures += expect(runtime.catchingUp() || runtime.state().currentTick > runtime.state().startTick,
    "runtime should move the replay timeline");
  failures += expect(runtime.pause(), "runtime should pause playback");
  const std::uint32_t pausedTick = runtime.state().currentTick;
  failures += expect(runtime.advance(1.0, &error), "paused runtime should accept a no-op update");
  failures += expect(runtime.state().currentTick == pausedTick,
    "pause should hold the replay tick");
  failures += expect(runtime.step(1U, &error), "runtime should step one fixed tick");
  if (!demo.authorityBoundaries.empty()) {
    const std::uint32_t boundaryTick = demo.authorityBoundaries.front().tick + 1U;
    const bool boundarySeek = runtime.seekTick(boundaryTick, &error);
    failures += expect(boundarySeek,
      "runtime should seek across a custom-config authority boundary");
    for (int attempt = 0; attempt < 32 && runtime.catchingUp(); ++attempt) {
      failures += expect(runtime.advance(0.0, &error),
        "runtime should finish bounded boundary catch-up");
    }
    failures += expect(runtime.snapshot().serverTick == boundaryTick,
      "boundary seek should restore the requested replay tick");
  }
  failures += expect(runtime.seekTick(runtime.state().startTick, &error),
    "runtime should seek back to its start checkpoint");
  failures += expect(runtime.snapshot().serverTick == runtime.state().startTick,
    "seek should restore the requested replay tick");
  runtime.stop();
  failures += expect(!runtime.active(), "stop should end replay ownership");
  failures += expect(live.captureReplayCheckpoint().serverTick == liveBefore.serverTick &&
    lg::replay::canonicalStateHash(live.captureReplayCheckpoint()) ==
      lg::replay::canonicalStateHash(liveBefore),
    "stopping replay should leave the live ServerGame unchanged");
  (void)finalSnapshot;
  return failures == 0 ? 0 : 1;
}
