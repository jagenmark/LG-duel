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
  failures += expect(source.beginReplayRecording({2U, 1U}, &error),
    "server should start authoritative replay recording");
  for (std::uint32_t tick = 0U; tick < 8U; ++tick) {
    lg::CommandPacket command;
    command.playerIndex = 0U;
    command.command.sequence = 2U + tick;
    command.command.clientTick = 100U + tick;
    command.command.forwardMove = tick < 4U ? 1.0F : 0.0F;
    command.command.viewYawRadians = 0.0F;
    command.command.viewPitchRadians = 0.0F;
    command.command.planarAim = false;
    command.command.attack = (tick % 2U) == 0U;
    command.command.weapon = lg::Weapon::MachineGun;
    command.viewedServerTick = source.snapshot().serverTick;
    sourceTransport.sendCommand(command);
    source.tick(lg::kFixedTickSeconds);
    discardSnapshots(sourceTransport);
  }
  const std::optional<lg::replay::ReplayDemo> recorded = source.finishReplayRecording();
  failures += expect(recorded.has_value(), "server should finalize the replay");
  failures += expect(recorded.has_value() && recorded->ticks.size() == 8U,
    "recording should contain resolved input for every server tick");
  failures += expect(recorded.has_value() && recorded->hashes.size() >= 9U,
    "recording should contain initial and per-tick authoritative hashes");
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
  // Do not add a bot. The runner restores actor metadata but never calls the
  // bot generator; it must reproduce the recorded final bot commands.
  lg::replay::ReplayPlaybackRunner runner(playback, savedDemo);
  failures += expect(runner.initialize(&error), "playback should restore the initial checkpoint");
  while (runner.step(&error)) {}
  failures += expect(runner.finished(), "playback should consume all recorded ticks");
  failures += expect(!runner.divergence().diverged,
    "bot-AI-off playback should match every recorded gameplay hash");

  if (savedDemo.ticks.size() >= 5U) {
    const std::uint32_t target = savedDemo.ticks[4].tick + 1U;
    failures += expect(runner.seek(target, &error), "checkpoint seek should reproduce the target state");
    failures += expect(!runner.divergence().diverged,
      "checkpoint seek should retain authoritative hash agreement");
  }
  runner.stop();
  return failures == 0 ? 0 : 1;
}
