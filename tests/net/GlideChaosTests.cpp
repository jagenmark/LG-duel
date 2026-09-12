#include "net/LoopbackTransport.hpp"
#include "replay/ReplayCodec.hpp"
#include "replay/ReplayPlayback.hpp"
#include "server/ServerGame.hpp"
#include "shared/Constants.hpp"
#include <cmath>
#include <functional>
#include <iostream>
#include <memory>

namespace {
int failures = 0;
void check(bool ok, const char* message) {
  if (!ok) { ++failures; std::cerr << "FAILED: " << message << '\n'; }
}
struct Fixture {
  lg::LoopbackTransport transport;
  std::unique_ptr<lg::ServerGame> game = std::make_unique<lg::ServerGame>(transport);
  lg::Arena arena;
  Fixture() {
    arena.min = {-20, -20, 0}; arena.max = {20, 20, 10}; arena.wallCount = 0;
    arena.spawnCount = 2; arena.spawnPositions[0] = {-6, 0, 0}; arena.spawnPositions[1] = {6, 0, 0};
    game->setArena(arena);
  }
  void reset(lg::GameMode mode = lg::GameMode::GlideDuel, lg::MatchPhase phase = lg::MatchPhase::Live) {
    lg::ScenarioSetup setup;
    setup.match.gameMode = mode; setup.match.phase = phase;
    setup.match.phaseTicksRemaining = lg::kGlideRoundTicks;
    for (std::size_t i = 0; i < 2; ++i) {
      auto& player = setup.players[i];
      player.connected = player.ready = player.alive = player.onGround = true;
      player.health = 100; player.position = arena.spawnPositions[i] + lg::Vec3{0, 0, .9F};
    }
    std::string error;
    check(game->applyScenarioSetup(setup, &error), error.c_str());
  }
  void tick(int count = 1) {
    for (int i = 0; i < count; ++i) {
      game->tick(lg::kFixedTickSeconds);
      lg::ServerSnapshot ignored;
      while (transport.receiveSnapshot(ignored)) {}
    }
  }
  // Use the real validated checkpoint boundary to isolate particular game
  // moments without adding a production cheat path for power-up ownership.
  void change(const std::function<void(lg::replay::ReplayCheckpoint&)>& edit) {
    std::string error;
    check(game->beginReplayRecording({}, &error), error.c_str());
    auto demo = game->finishReplayRecording();
    check(demo.has_value(), "fixture captures checkpoint metadata");
    if (!demo) return;
    auto checkpoint = game->captureReplayCheckpoint(); edit(checkpoint);
    check(game->restoreReplayCheckpoint(checkpoint, demo->metadata, &error), error.c_str());
    game->endReplayPlayback();
  }
  void shoot(std::size_t attacker, std::size_t target) {
    const auto& snapshot = game->snapshot();
    const lg::Vec3 delta = snapshot.players[target].position -
      (snapshot.players[attacker].position + lg::Vec3{0, 0, .65F});
    lg::CommandPacket packet;
    packet.playerIndex = static_cast<std::uint8_t>(attacker);
    packet.command.sequence = snapshot.acknowledgedCommand[attacker] + 1;
    packet.command.weapon = lg::Weapon::Railgun;
    packet.command.attack = true; packet.command.planarAim = false;
    packet.command.viewYawRadians = std::atan2(delta.y, delta.x);
    packet.command.viewPitchRadians = std::atan2(delta.z, std::hypot(delta.x, delta.y));
    transport.sendCommand(packet); tick();
  }
};
}
int main() {
  auto owner = std::make_unique<Fixture>(); auto& f = *owner;
  f.reset();
  check(f.game->snapshot().glide.arenaReady, "open arena has a shared contest area");
  check(std::abs(f.game->snapshot().movementTuning.gravity - 16.8F) < .001F,
    "first FLOAT round uses reduced gravity");
  f.change([](auto& c) { c.glide.roundNumber = 2; });
  check(std::abs(f.game->snapshot().movementTuning.gravity - 24.0F) < .001F,
    "BLAST restores base gravity after checkpoint restore");
  f.change([](auto& c) { c.glide.roundNumber = 3; });
  check(std::abs(f.game->snapshot().movementTuning.dashCooldown - .51F) < .001F,
    "RUSH derives shorter dash cooldown from base tuning");
  {
    lg::CommandPacket tuning; tuning.playerIndex = 0; tuning.command.sequence = 1;
    tuning.requestMovementTuning = true; tuning.movementTuning = f.game->snapshot().movementTuning;
    f.transport.sendCommand(tuning); f.tick();
    check(std::abs(f.game->snapshot().movementTuning.dashCooldown - .51F) < .001F,
      "echoed client configuration does not compound the RUSH multiplier");
  }
  f.reset(lg::GameMode::Duel);
  check(!f.game->snapshot().movementTuning.glideMovementEnabled &&
      f.game->snapshot().movementTuning.gravity == 24.0F &&
      f.game->snapshot().movementTuning.dashCooldown == .85F,
    "leaving Glide restores configured classic movement");
  f.reset(lg::GameMode::GlideDuel, lg::MatchPhase::WaitingForReady);
  check(f.game->snapshot().glide.roundNumber == 0, "waiting does not consume a condition");
  for (std::size_t slot = 0; slot < 2; ++slot) {
    if (f.game->snapshot().readyPlayers[slot]) continue;
    lg::CommandPacket ready; ready.playerIndex = static_cast<std::uint8_t>(slot);
    ready.command.sequence = 1; ready.toggleReady = true; f.transport.sendCommand(ready);
  }
  f.tick();
  check(f.game->snapshot().glide.roundNumber == 1 &&
    lg::glideCondition(f.game->snapshot().glide.roundNumber) == lg::GlideCondition::Float,
    "first ready countdown announces FLOAT");

  f.reset();
  f.change([](auto& c) {
    c.match.phaseTicksRemaining = lg::kGlideRoundTicks - 8 * 125 + 1;
    c.players[0].player.position = c.glide.pickupPositions[0] + lg::Vec3{0, 0, .9F};
  });
  f.tick();
  check(f.game->snapshot().glide.claimedMask == 0, "preview cannot be collected early");
  f.tick();
  check(f.game->snapshot().glide.buffs[0] == lg::GlidePowerup::Surge &&
    f.game->snapshot().glide.buffTicks[0] == lg::kGlideBuffTicks &&
    f.game->snapshot().glide.claimedMask == 1 && f.game->snapshot().glide.pickupSequence == 1,
    "SURGE grants exactly one timed ownership at spawn");
  f.tick(lg::kGlideBuffTicks);
  check(f.game->snapshot().glide.buffs[0] == lg::GlidePowerup::None &&
    f.game->snapshot().glide.buffTicks[0] == 0 && f.game->snapshot().glide.pickupSequence == 1,
    "buff expires at six seconds without recollecting");

  const auto damage = [&](lg::GlidePowerup attackerBuff, lg::GlidePowerup defenderBuff, int health = 100) {
    f.reset();
    f.change([&](auto& c) {
      c.players[1].player.health = health;
      c.glide.buffs = {attackerBuff, defenderBuff};
      c.glide.buffTicks = {static_cast<std::uint16_t>(attackerBuff == lg::GlidePowerup::None ? 0 : 750),
        static_cast<std::uint16_t>(defenderBuff == lg::GlidePowerup::None ? 0 : 750)};
    });
    f.shoot(0, 1);
    const int removed = health - f.game->snapshot().players[1].health;
    check(f.game->snapshot().weaponFires[0].damageApplied == removed,
      "published weapon damage matches modified health removed");
    return removed;
  };
  const int normal = damage(lg::GlidePowerup::None, lg::GlidePowerup::None);
  const int surge = damage(lg::GlidePowerup::Surge, lg::GlidePowerup::None);
  const int guard = damage(lg::GlidePowerup::None, lg::GlidePowerup::Guard);
  check(normal > 0 && surge == std::min(100, static_cast<int>(std::lround(normal * 1.35F))) &&
    guard == static_cast<int>(std::lround(normal * .65F)), "power-ups scale actual ordinary weapon hits");
  check(damage(lg::GlidePowerup::None, lg::GlidePowerup::Guard, 5) == 5,
    "GUARD does not reduce overkill to a fraction of remaining health");

  f.reset();
  f.change([](auto& c) {
    c.match.phaseTicksRemaining = lg::kGlideRoundTicks - 20 * 125;
    c.players[1].player.position = c.glide.pickupPositions[1] + lg::Vec3{0, 0, .9F};
  });
  f.tick();
  check(f.game->snapshot().glide.buffs[1] == lg::GlidePowerup::Guard &&
    f.game->snapshot().glide.claimedMask == 2, "second contested wave grants GUARD");
  f.reset();
  f.change([](auto& c) {
    c.match.phaseTicksRemaining = lg::kGlideRoundTicks - 32 * 125;
    c.players[0].player.position = c.glide.pickupPositions[2] + lg::Vec3{0, 0, .9F};
  });
  f.tick();
  check(f.game->snapshot().glide.claimedMask == 0, "full health leaves REPAIR available for a comeback");
  f.change([](auto& c) { c.players[0].player.health = 85; });
  f.tick();
  check(f.game->snapshot().players[0].health == 100 && f.game->snapshot().glide.claimedMask == 4 &&
    f.game->snapshot().glide.buffs[0] == lg::GlidePowerup::None,
    "REPAIR heals up to spawn health without replacing a buff");

  f.reset();
  f.change([](auto& c) {
    c.match.phaseTicksRemaining = lg::kGlideRoundTicks - 8 * 125;
    for (std::size_t i = 0; i < 2; ++i)
      c.players[i].player.position = c.glide.pickupPositions[0] + lg::Vec3{i == 0 ? -.5F : .5F, 0, .9F};
  });
  f.tick();
  check(f.game->snapshot().glide.pickupSequence == 1 &&
    ((f.game->snapshot().glide.buffTicks[0] > 0) != (f.game->snapshot().glide.buffTicks[1] > 0)),
    "simultaneous pickup touches grant a single owner");

  f.reset();
  f.change([](auto& c) {
    c.match.phaseTicksRemaining = 125;
    c.players[0].player.position = c.glide.pickupPositions[2] + lg::Vec3{0, 0, .9F};
    c.players[1].player.position = {18, 18, .9F};
    c.glide.buffs[1] = lg::GlidePowerup::Guard; c.glide.buffTicks[1] = 750;
  });
  f.tick(125);
  check(f.game->snapshot().players[0].health == 100 && f.game->snapshot().players[1].health == 92,
    "closing zone deals eight HP per second outside, unaffected by GUARD or Overdrive");
  f.reset();
  f.change([](auto& c) {
    c.match.phaseTicksRemaining = 125;
    c.players[0].player.position = {-18, -18, .9F}; c.players[1].player.position = {18, 18, .9F};
    c.players[0].player.health = c.players[1].player.health = 1;
  });
  f.tick(16);
  check(f.game->snapshot().matchPhase == lg::MatchPhase::RoundEnd &&
    f.game->snapshot().roundWinner == 255 && f.game->snapshot().scores[0] == 0 &&
    f.game->snapshot().scores[1] == 0, "simultaneous zone deaths resolve as a draw");
  while (f.game->snapshot().glide.roundNumber == 1) f.tick();
  check(f.game->snapshot().glide.roundNumber == 2 && f.game->snapshot().glide.claimedMask == 0 &&
    f.game->snapshot().glide.buffTicks[0] == 0, "draw still rotates condition and clears pickup state");

  // Real recorded inputs exercise collection, expiry, world damage, a round
  // boundary and the next gravity profile. Seek must restore the active buff.
  f.reset();
  f.change([](auto& c) {
    c.match.phaseTicksRemaining = lg::kGlideRoundTicks - 8 * 125;
    c.players[0].player.position = c.glide.pickupPositions[0] + lg::Vec3{0, 0, .9F};
  });
  std::string error;
  lg::replay::ReplayRecordingConfig config; config.checkpointIntervalTicks = 125; config.hashIntervalTicks = 125;
  check(f.game->beginReplayRecording(config, &error), error.c_str());
  f.tick(125); const auto buffState = f.game->captureReplayCheckpoint();
  f.tick(5000);
  const auto finalState = f.game->captureReplayCheckpoint();
  auto demo = f.game->finishReplayRecording();
  check(demo.has_value() && finalState.glide.roundNumber >= 2, "recorded fixture crosses a round condition change");
  if (demo) {
    lg::LoopbackTransport playbackTransport;
    auto playback = std::make_unique<lg::ServerGame>(playbackTransport); playback->setArena(f.arena);
    lg::replay::ReplayPlaybackRunner runner(*playback, *demo);
    check(runner.initialize(&error), error.c_str());
    while (runner.step(&error)) {}
    if (!error.empty()) std::cerr << error << '\n';
    check(runner.finished() && !runner.divergence().diverged &&
      lg::replay::canonicalStateHash(playback->captureReplayCheckpoint()) == lg::replay::canonicalStateHash(finalState),
      "Glide replay matches every authoritative hash across power-ups and rounds");
    check(runner.seek(buffState.serverTick, &error) &&
      lg::replay::canonicalStateHash(playback->captureReplayCheckpoint()) == lg::replay::canonicalStateHash(buffState) &&
      playback->snapshot().glide.buffs[0] == lg::GlidePowerup::Surge,
      "seeking into an active power-up restores exact ownership and remaining duration");
    runner.stop();
  }
  std::cout << "Glide chaos checks: " << (failures == 0 ? "PASS" : "FAIL") << '\n';
  return failures == 0 ? 0 : 1;
}
