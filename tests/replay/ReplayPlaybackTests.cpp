#include "net/LoopbackTransport.hpp"
#include "replay/ReplayCodec.hpp"
#include "replay/ReplayPlayback.hpp"
#include "server/ServerGame.hpp"
#include "shared/Constants.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <string_view>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) return 0;
  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

lg::Arena replayArena() {
  lg::Arena arena;
  arena.min = {-12.0F, -12.0F, 0.0F};
  arena.max = {12.0F, 12.0F, 6.0F};
  arena.spawnPositions[0] = {-4.0F, 0.0F, 0.0F};
  arena.spawnPositions[1] = {4.0F, 0.0F, 0.0F};
  arena.spawnCount = 2U;
  return arena;
}

void discardSnapshots(lg::LoopbackTransport& transport) {
  lg::ServerSnapshot snapshot;
  while (transport.receiveSnapshot(snapshot)) {}
}

class CountingTransport final : public lg::NetTransport {
public:
  void sendCommand(const lg::CommandPacket&) override {}
  [[nodiscard]] bool receiveCommand(lg::CommandPacket&) override { return false; }
  void sendSnapshot(const lg::ServerSnapshot&) override { ++snapshots; }
  [[nodiscard]] bool receiveSnapshot(lg::ServerSnapshot&) override { return false; }
  void sendProjectileUpdates(const lg::ProjectileUpdatePacket&) override { ++projectileUpdates; }
  void publishChatHistory(const lg::ChatHistory&) override { ++chatPublishes; }

  std::uint32_t snapshots = 0U;
  std::uint32_t projectileUpdates = 0U;
  std::uint32_t chatPublishes = 0U;
};

void makeHumanAndBot(lg::ServerGame& game, lg::LoopbackTransport& transport) {
  game.setArena(replayArena());
  game.setConnectedPlayers({true, false, false, false, false, false, false, false,
    false, false, false, false, false, false, false, false});
  lg::MatchRules rules;
  rules.countdownTicks = 0U;
  game.setMatchRules(rules);
  (void)game.addBots(1U);
  lg::CommandPacket ready;
  ready.playerIndex = 0U;
  ready.command.sequence = 1U;
  ready.toggleReady = true;
  transport.sendCommand(ready);
  game.tick(lg::kFixedTickSeconds);
  discardSnapshots(transport);
}

} // namespace

int main() {
  int failures = 0;
  lg::LoopbackTransport sourceTransport;
  lg::ServerGame source(sourceTransport);
  discardSnapshots(sourceTransport);
  makeHumanAndBot(source, sourceTransport);
  source.setBotAttackMode(lg::BotAttackMode::Hard);
  failures += expect(source.snapshot().botPlayers[1], "test should contain a bot actor");
  failures += expect(source.snapshot().matchPhase == lg::MatchPhase::Live,
    "test match should enter live play before recording");

  std::string error;
  lg::replay::ReplayRecordingConfig recordingConfig;
  recordingConfig.checkpointIntervalTicks = 24U;
  recordingConfig.hashIntervalTicks = 12U;
  failures += expect(source.beginReplayRecording(recordingConfig, &error),
    "server should start authoritative replay recording");
  for (std::uint32_t tick = 0U; tick < 96U; ++tick) {
    lg::CommandPacket command;
    command.playerIndex = 0U;
    command.command.sequence = 2U + tick;
    command.command.clientTick = 100U + tick;
    command.command.forwardMove = 1.0F;
    command.command.viewYawRadians = 0.0F;
    command.command.viewPitchRadians = 0.0F;
    command.command.planarAim = false;
    command.command.attack = false;
    command.command.weapon = lg::Weapon::MachineGun;
    command.viewedServerTick = source.snapshot().serverTick;
    sourceTransport.sendCommand(command);
    source.tick(lg::kFixedTickSeconds);
    if (tick == 0U) {
      lg::ServerSnapshot published;
      failures += expect(sourceTransport.receiveSnapshot(published),
        "live authoritative recording must still publish snapshots");
    }
    discardSnapshots(sourceTransport);
  }
  const lg::replay::ReplayCheckpoint sourceFinalCheckpoint = source.captureReplayCheckpoint();
  const std::optional<lg::replay::ReplayDemo> recorded = source.finishReplayRecording();
  failures += expect(recorded.has_value(), "server should finalize the replay");
  failures += expect(recorded.has_value() && recorded->ticks.size() == 96U,
    "recording should contain resolved input for every server tick");
  failures += expect(recorded.has_value() && recorded->hashes.size() > 2U &&
    recorded->hashes.back().tick == sourceFinalCheckpoint.serverTick &&
    recorded->hashes.back().value == lg::replay::canonicalStateHash(sourceFinalCheckpoint),
    "recording should append an exact final authoritative hash off interval");
  failures += expect(recorded.has_value() && recorded->checkpoints.size() > 2U &&
    recorded->checkpoints.back().serverTick == sourceFinalCheckpoint.serverTick,
    "recording should append an exact final checkpoint off interval");
  failures += expect(recorded.has_value() && recorded->metadata.players[1].bot,
    "bot identity belongs in replay metadata");

  std::vector<std::uint8_t> bytes;
  lg::replay::ReplayDemo savedDemo;
  if (recorded.has_value()) {
    failures += expect(lg::replay::encodeDemo(*recorded, bytes, &error),
      "saved demo should encode");
    failures += expect(lg::replay::decodeDemo(bytes, savedDemo, &error),
      "saved demo should decode");
  }

  lg::LoopbackTransport playbackTransport;
  lg::ServerGame playback(playbackTransport);
  discardSnapshots(playbackTransport);
  playback.setArena(replayArena());
  playback.setMatchRules(savedDemo.metadata.matchRules);
  const lg::replay::ReplayCheckpoint beforeRejectedRestore = playback.captureReplayCheckpoint();
  lg::replay::ReplayCheckpoint invalidRestore = savedDemo.checkpoints.front();
  invalidRestore.history.clear();
  failures += expect(!playback.restoreReplayCheckpoint(invalidRestore, savedDemo.metadata, &error) &&
    lg::replay::canonicalStateHash(playback.captureReplayCheckpoint()) ==
      lg::replay::canonicalStateHash(beforeRejectedRestore),
    "invalid empty lag history must reject before changing any playback state");
  invalidRestore = savedDemo.checkpoints.front();
  invalidRestore.nextDeathmatchSpawnIndex = playback.arena().spawnCount;
  failures += expect(!playback.restoreReplayCheckpoint(invalidRestore, savedDemo.metadata, &error) &&
    lg::replay::canonicalStateHash(playback.captureReplayCheckpoint()) ==
      lg::replay::canonicalStateHash(beforeRejectedRestore),
    "runtime-invalid spawn cursor must reject before an out-of-bounds spawn path");
  // Do not add a bot. The runner restores actor metadata but never calls the
  // bot generator; it must reproduce the recorded final bot commands.
  lg::replay::ReplayPlaybackRunner runner(playback, savedDemo);
  failures += expect(runner.initialize(&error), "playback should restore the initial checkpoint");
  while (runner.step(&error)) {}
  failures += expect(runner.finished(), "playback should consume all recorded ticks");
  failures += expect(!runner.divergence().diverged,
    "bot-AI-off playback should match every recorded gameplay hash");
  failures += expect(lg::replay::canonicalStateHash(playback.captureReplayCheckpoint()) ==
    lg::replay::canonicalStateHash(sourceFinalCheckpoint),
    "final playback state should match the original stop-time checkpoint");

  if (savedDemo.ticks.size() >= 49U) {
    const std::uint32_t target = savedDemo.ticks[48].tick + 1U;
    failures += expect(runner.seek(target, &error), "checkpoint seek should reproduce the target state");
    while (runner.step(&error)) {}
    failures += expect(!runner.divergence().diverged,
      "periodic checkpoint seek should retain footstep and gameplay hash agreement");
  }
  runner.stop();

  CountingTransport headlessTransport;
  lg::ServerGame headlessPlayback(headlessTransport);
  headlessPlayback.setArena(replayArena());
  headlessPlayback.setMatchRules(savedDemo.metadata.matchRules);
  headlessTransport.snapshots = 0U;
  headlessTransport.projectileUpdates = 0U;
  headlessTransport.chatPublishes = 0U;
  lg::replay::ReplayPlaybackRunner headlessRunner(headlessPlayback, savedDemo);
  failures += expect(headlessRunner.initialize(&error), "headless playback should initialize");
  while (headlessRunner.step(&error)) {}
  failures += expect(headlessRunner.finished() && !headlessRunner.divergence().diverged &&
    headlessTransport.snapshots == 0U && headlessTransport.projectileUpdates == 0U &&
    headlessTransport.chatPublishes == 0U,
    "headless replay must not publish snapshot, projectile, or chat transport output");
  headlessRunner.stop();
  return failures == 0 ? 0 : 1;
}
