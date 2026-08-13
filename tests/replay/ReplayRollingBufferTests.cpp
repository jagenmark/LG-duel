#include "replay/ReplayRollingBuffer.hpp"

#include "net/LoopbackTransport.hpp"
#include "server/ServerGame.hpp"
#include "shared/Constants.hpp"

#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) return 0;
  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

lg::replay::ReplayMetadata metadata() {
  lg::replay::ReplayMetadata value;
  value.initialServerTick = 0U;
  value.mapRevision = 1U;
  value.mapName = "rolling_test";
  value.mapContentHash = 1U;
  for (std::size_t index = 0; index < value.players.size(); ++index) {
    value.players[index].slot = static_cast<std::uint8_t>(index);
  }
  return value;
}

lg::replay::ReplayCheckpoint checkpoint(std::uint32_t tick) {
  lg::replay::ReplayCheckpoint value;
  value.serverTick = tick;
  value.mapRevision = 1U;
  value.projectileRevision = 1U;
  value.spawnRandomState = 1U;
  value.history.push_back({tick, {}});
  return value;
}

lg::replay::ReplayTickInput input(std::uint32_t tick) {
  lg::replay::ReplayTickInput value;
  value.tick = tick;
  return value;
}

} // namespace

int main() {
  int failures = 0;
  lg::replay::ReplayRollingBuffer buffer;
  lg::replay::ReplayRollingBufferConfig config;
  config.retainedTicks = 8U;
  config.checkpointIntervalTicks = 5U;
  config.hashIntervalTicks = 3U;
  config.maximumBytes = 1024U * 1024U;
  std::string error;
  failures += expect(buffer.begin(metadata(), checkpoint(0U), 5U, config, &error),
    "rolling replay should begin with a checkpoint");
  failures += expect(!buffer.needsCompletedCheckpoint(1U) &&
    buffer.needsCompletedCheckpoint(3U) && buffer.needsCompletedCheckpoint(5U),
    "rolling replay should request full checkpoint capture only at hash or checkpoint intervals");

  for (std::uint32_t tick = 0U; tick <= 16U; ++tick) {
    buffer.recordResolvedInput(input(tick));
    buffer.recordCompletedTick(checkpoint(tick + 1U));
  }
  const lg::replay::ReplayRollingBufferStats stats = buffer.stats();
  failures += expect(stats.enabled && stats.generation == 5U, "rolling replay should report its active generation");
  failures += expect(stats.estimatedBytes <= config.maximumBytes, "rolling replay must stay within its byte bound");
  failures += expect(stats.retainedTicks <= config.retainedTicks + config.checkpointIntervalTicks,
    "checkpoint anchoring may add at most one checkpoint interval to retention");
  failures += expect(stats.droppedRecords > 0U, "ring trimming should account for discarded records");

  const lg::replay::ReplayLethalEvent lethal = {
    9U, 5U, 1U, 0U, lg::Weapon::RocketLauncher, 7U, lg::replay::LethalKind::Direct,
  };
  buffer.recordLethal(lethal);
  const std::optional<lg::replay::ReplayDemo> segment = buffer.extractSegment(lethal, 2U, 2U, &error);
  failures += expect(segment.has_value(), "checkpoint-anchored recent lethal segment should extract");
  failures += expect(segment.has_value() && segment->checkpoints.size() == 1U,
    "a segment should carry one starting checkpoint");
  failures += expect(segment.has_value() && segment->ticks.front().tick == segment->metadata.initialServerTick &&
    segment->ticks.back().tick == 11U, "segment should contain a continuous command range through its end");
  failures += expect(segment.has_value() && segment->lethalEvents.size() == 1U,
    "segment should retain the lethal selector metadata");
  failures += expect(!buffer.extractSegment(lethal, 2U, 40U, &error).has_value(),
    "incomplete future replay segment should reject cleanly");

  {
    lg::replay::ReplayRollingBuffer gapBuffer;
    failures += expect(gapBuffer.begin(metadata(), checkpoint(0U), 7U, config, &error),
      "gap test should begin a rolling replay");
    gapBuffer.recordResolvedInput(input(0U));
    gapBuffer.recordResolvedInput(input(1U));
    gapBuffer.recordResolvedInput(input(3U));
    const lg::replay::ReplayLethalEvent gapLethal = {
      1U, 7U, 1U, 0U, lg::Weapon::RocketLauncher, 1U, lg::replay::LethalKind::Direct,
    };
    failures += expect(gapBuffer.stats().droppedRecords == 1U,
      "rolling recording must reject a non-adjacent resolved-input tick");
    failures += expect(!gapBuffer.extractSegment(gapLethal, 1U, 2U, &error).has_value(),
      "segment extraction must not bridge a rejected middle input gap");
  }

  {
    lg::replay::ReplayRollingBuffer cappedBuffer;
    lg::replay::ReplayRollingBufferConfig cappedConfig;
    cappedConfig.retainedTicks = 4096U;
    cappedConfig.checkpointIntervalTicks = 4096U;
    cappedConfig.hashIntervalTicks = 4096U;
    cappedConfig.maximumBytes = 128U * 1024U;
    failures += expect(cappedBuffer.begin(metadata(), checkpoint(0U), 8U, cappedConfig, &error),
      "native rolling cap should retain its initial checkpoint");
    for (std::uint32_t tick = 0U; tick < 256U; ++tick) {
      cappedBuffer.recordResolvedInput(input(tick));
    }
    const lg::replay::ReplayRollingBufferStats cappedStats = cappedBuffer.stats();
    failures += expect(cappedStats.inputCount > 0U && cappedStats.inputCount < 256U &&
      cappedStats.residentBytes <= cappedConfig.maximumBytes &&
      cappedStats.residentBytes >= cappedStats.inputCount * sizeof(lg::replay::ReplayTickInput) &&
      cappedStats.droppedRecords > 0U,
      "rolling storage should charge native frames and stop before its resident cap");
  }

  buffer.reset(metadata(), checkpoint(20U), 6U);
  failures += expect(buffer.stats().generation == 6U, "reset should advance to the new replay generation");
  failures += expect(!buffer.extractSegment(lethal, 1U, 1U, &error).has_value(),
    "prior-generation lethal events must not cross a reset boundary");

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    lg::Arena arena;
    arena.min = {-12.0F, -12.0F, 0.0F};
    arena.max = {12.0F, 12.0F, 6.0F};
    arena.spawnPositions[0] = {-3.0F, 0.0F, 0.0F};
    arena.spawnPositions[1] = {3.0F, 0.0F, 0.0F};
    arena.spawnCount = 2U;
    server.setArena(arena);
    lg::ScenarioSetup setup;
    setup.match.gameMode = lg::GameMode::Duel;
    setup.match.phase = lg::MatchPhase::Live;
    setup.players[0].connected = true;
    setup.players[0].alive = true;
    setup.players[0].health = 100;
    setup.players[0].position = {-3.0F, 0.0F, 0.9F};
    setup.players[0].selectedWeapon = lg::Weapon::Railgun;
    setup.players[0].ammo = lg::WeaponAmmoArray{{150, 10, 10, 100, 10, 10, 50, 150, 10}};
    setup.players[1].connected = true;
    setup.players[1].alive = true;
    setup.players[1].health = 1;
    setup.players[1].position = {3.0F, 0.0F, 0.9F};
    failures += expect(server.applyScenarioSetup(setup, &error), "death test setup should apply");
    lg::replay::ReplayRollingBufferConfig rollingConfig;
    rollingConfig.checkpointIntervalTicks = 1U;
    rollingConfig.hashIntervalTicks = 1U;
    rollingConfig.maximumBytes = 1024U * 1024U;
    failures += expect(server.beginRollingReplay(rollingConfig, &error),
      "server should enable the rolling replay archive");
    lg::CommandPacket rail;
    rail.playerIndex = 0U;
    rail.command.sequence = 1U;
    rail.command.attack = true;
    rail.command.planarAim = false;
    rail.command.viewYawRadians = 0.0F;
    rail.command.viewPitchRadians = -0.041F;
    rail.command.weapon = lg::Weapon::Railgun;
    rail.viewedServerTick = server.snapshot().serverTick;
    transport.sendCommand(rail);
    server.tick(lg::kFixedTickSeconds);
    server.tick(lg::kFixedTickSeconds);
    server.tick(lg::kFixedTickSeconds);
    const std::optional<lg::replay::ReplayLethalEvent> serverLethal = server.latestReplayLethal();
    failures += expect(serverLethal.has_value() && serverLethal->victim == 1U &&
      serverLethal->killer == 0U && serverLethal->weapon == lg::Weapon::Railgun,
      "authoritative death should add rolling lethal metadata at the damage seam");
    if (serverLethal.has_value()) {
      failures += expect(server.extractRollingReplaySegment(*serverLethal, 0U, 2U, &error).has_value(),
        "authoritative lethal metadata should select a self-contained replay segment");
    }
  }
  return failures == 0 ? 0 : 1;
}
