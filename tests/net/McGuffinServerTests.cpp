#include "net/LoopbackTransport.hpp"
#include "server/ServerGame.hpp"

#include <iostream>
#include <filesystem>
#include <string_view>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) return 0;
  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

lg::Arena objectiveArena() {
  lg::Arena arena;
  arena.min = {-10.0F, -5.0F, -1.0F};
  arena.max = {10.0F, 5.0F, 5.0F};
  arena.spawnPositions[0] = {-7.0F, 0.0F, 0.0F};
  arena.spawnPositions[1] = {7.0F, 0.0F, 0.0F};
  arena.spawnTeams[0] = lg::Team::Red;
  arena.spawnTeams[1] = lg::Team::Blue;
  arena.teamSpawnCount = 2;
  arena.teamSpawns[0] = {
    {-7.0F, 0.0F, -0.1F}, 0.0F, lg::ArenaSpawnGroup::RedBase,
  };
  arena.teamSpawns[1] = {
    {7.0F, 0.0F, -0.1F}, 3.1415927F, lg::ArenaSpawnGroup::BlueBase,
  };
  arena.mcguffin.hasNeutralSpawn = true;
  arena.mcguffin.neutralSpawn = {-7.0F, 0.0F, 0.9F};
  arena.mcguffin.hasRedBase = true;
  arena.mcguffin.redBase = {
    {-9.0F, -2.0F, 0.0F}, {-5.0F, 2.0F, 3.0F}, lg::Team::Red,
  };
  arena.mcguffin.hasBlueBase = true;
  arena.mcguffin.blueBase = {
    {5.0F, -2.0F, 0.0F}, {9.0F, 2.0F, 3.0F}, lg::Team::Blue,
  };
  return arena;
}

lg::Arena selectionArena() {
  lg::Arena arena;
  arena.min = {-20.0F, -10.0F, -1.0F};
  arena.max = {20.0F, 10.0F, 6.0F};
  arena.spawnPositions[0] = {-7.0F, 0.0F, 0.0F};
  arena.spawnPositions[1] = {7.0F, 0.0F, 0.0F};
  // Deliberately leave the old third-slot body on the best red candidate.
  // A simultaneous round reset must ignore this stale position.
  arena.spawnPositions[2] = {-15.0F, 0.0F, 0.0F};
  arena.mcguffin.hasNeutralSpawn = true;
  arena.mcguffin.neutralSpawn = {0.0F, 0.0F, 1.0F};
  arena.mcguffin.hasRedBase = true;
  arena.mcguffin.redBase = {
    {-10.0F, -2.0F, 0.0F}, {-8.0F, 2.0F, 3.0F}, lg::Team::Red,
  };
  arena.mcguffin.hasBlueBase = true;
  arena.mcguffin.blueBase = {
    {8.0F, -2.0F, 0.0F}, {10.0F, 2.0F, 3.0F}, lg::Team::Blue,
  };
  arena.teamSpawnCount = 6;
  arena.teamSpawns[0] = {
    {-15.0F, -4.0F, 0.0F}, 0.0F, lg::ArenaSpawnGroup::RedBase,
  };
  arena.teamSpawns[1] = {
    {-15.0F, 0.0F, 0.0F}, 0.25F, lg::ArenaSpawnGroup::RedBase,
  };
  arena.teamSpawns[2] = {
    {-15.0F, 4.0F, 0.0F}, 0.5F, lg::ArenaSpawnGroup::RedBase,
  };
  arena.teamSpawns[3] = {
    {15.0F, -4.0F, 0.0F}, 3.1415927F, lg::ArenaSpawnGroup::BlueBase,
  };
  arena.teamSpawns[4] = {
    {15.0F, 0.0F, 0.0F}, 3.1415927F, lg::ArenaSpawnGroup::BlueBase,
  };
  arena.teamSpawns[5] = {
    {15.0F, 4.0F, 0.0F}, 3.1415927F, lg::ArenaSpawnGroup::BlueBase,
  };
  // This narrow center wall hides the middle red candidate from the blue side
  // while leaving the flank candidates exposed.
  arena.walls[0] = {{-0.1F, -1.0F, -1.0F}, {0.1F, 1.0F, 4.0F}};
  arena.wallCount = 1;
  return arena;
}

std::filesystem::path findMapsDirectory() {
  std::filesystem::path path = std::filesystem::current_path();
  for (int depth = 0; depth < 6; ++depth) {
    if (std::filesystem::exists(path / "maps" / "eyetoeye.map")) {
      return path / "maps";
    }
    path = path.parent_path();
  }
  return {};
}

void sendMode(lg::LoopbackTransport& transport, std::uint32_t sequence) {
  lg::CommandPacket packet;
  packet.playerIndex = 0;
  packet.command.sequence = sequence;
  packet.requestGameMode = true;
  packet.requestedGameMode = lg::GameMode::McGuffin;
  transport.sendCommand(packet);
}

void sendTeam(
  lg::LoopbackTransport& transport,
  std::size_t player,
  std::uint32_t sequence,
  lg::Team team
) {
  lg::CommandPacket packet;
  packet.playerIndex = static_cast<std::uint8_t>(player);
  packet.command.sequence = sequence;
  packet.requestTeam = true;
  packet.requestedTeam = team;
  transport.sendCommand(packet);
}

void sendReady(
  lg::LoopbackTransport& transport,
  std::size_t player,
  std::uint32_t sequence
) {
  lg::CommandPacket packet;
  packet.playerIndex = static_cast<std::uint8_t>(player);
  packet.command.sequence = sequence;
  packet.toggleReady = true;
  transport.sendCommand(packet);
}

} // namespace

int main() {
  int failures = 0;
  lg::LoopbackTransport transport;
  lg::ServerGame server(transport);
  const lg::Arena arena = objectiveArena();
  failures += expect(lg::hasValidMcGuffinLayout(arena),
    "test arena should be McGuffin compatible");
  server.setArena(arena);

  {
    lg::Arena invalidSpawnArena = objectiveArena();
    invalidSpawnArena.teamSpawns[0].position.z = 0.0F;
    invalidSpawnArena.teamSpawns[1].position.z = 0.0F;
    failures += expect(
      !lg::hasValidMcGuffinLayout(invalidSpawnArena),
      "physical spawn groups with only in-base candidates should be rejected"
    );
    lg::LoopbackTransport invalidTransport;
    lg::ServerGame invalidServer(invalidTransport);
    invalidServer.setArena(invalidSpawnArena);
    sendMode(invalidTransport, 1);
    invalidServer.tick(lg::kFixedTickSeconds);
    failures += expect(
      invalidServer.snapshot().gameMode != lg::GameMode::McGuffin,
      "invalid physical groups must not fall back to permanent team-tagged spawns"
    );
  }

  lg::MatchRules rules;
  rules.countdownTicks = 0;
  rules.roundEndTicks = 1;
  rules.matchEndTicks = 1;
  server.setMatchRules(rules);
  lg::McGuffinConfig config;
  config.initialSpawnTicks = 0;
  config.installationDelayTicks = 0;
  config.pointsPerSecond = 125;
  config.scoreLimit = 3;
  config.finalHoldTicks = 1;
  config.stealTicks = 1;
  server.setMcGuffinConfig(config);

  sendMode(transport, 1);
  server.tick(lg::kFixedTickSeconds);
  failures += expect(server.snapshot().gameMode == lg::GameMode::McGuffin,
    "valid arena should accept McGuffin mode request");

  const std::filesystem::path mapsDirectory = findMapsDirectory();
  failures += expect(!mapsDirectory.empty(), "test should locate runtime maps");
  if (!mapsDirectory.empty()) {
    server.setMapDirectory(mapsDirectory.string());
    failures += expect(!server.loadRequestedMap("eyetoeye"),
      "active McGuffin should reject a map without objective entities");
    failures += expect(lg::hasValidMcGuffinLayout(server.arena()),
      "rejected map load should preserve the active objective arena");
  }

  sendTeam(transport, 0, 2, lg::Team::Red);
  sendTeam(transport, 1, 1, lg::Team::Blue);
  server.tick(lg::kFixedTickSeconds);
  failures += expect(
    server.snapshot().teams[0] == lg::Team::Red &&
      server.snapshot().teams[1] == lg::Team::Blue,
    "warmup team requests should assign both McGuffin teams"
  );

  sendReady(transport, 0, 3);
  sendReady(transport, 1, 2);
  server.tick(lg::kFixedTickSeconds);
  failures += expect(server.snapshot().matchPhase == lg::MatchPhase::Live,
    "ready players on both teams should start McGuffin");
  failures += expect(
    server.snapshot().mcguffin.state == lg::McGuffinState::InstalledRed,
    "neutral objective should be picked up and installed from authoritative contacts"
  );

  server.tick(lg::kFixedTickSeconds);
  failures += expect(server.snapshot().mcguffinScores[0] == 1,
    "installed objective should score on the fixed server tick");
  server.tick(lg::kFixedTickSeconds);
  failures += expect(server.snapshot().mcguffinScores[0] == 2,
    "fixed-tick scoring should retain exact progress across ticks");
  server.tick(lg::kFixedTickSeconds);
  failures += expect(
    server.snapshot().roundWinningTeam == lg::Team::Red &&
      server.snapshot().mcguffinRoundsWon[0] == 1,
    "the final uncontested hold should end the round at the score limit"
  );
  server.tick(lg::kFixedTickSeconds);
  failures += expect(
    server.snapshot().mcguffinRound == 1 &&
      server.snapshot().mcguffinRedBaseOwner == lg::Team::Blue &&
      server.snapshot().mcguffinBlueBaseOwner == lg::Team::Red &&
      server.snapshot().players[0].position.x > 0.0F &&
      server.snapshot().players[1].position.x < 0.0F,
    "round-two respawns should follow swapped physical base ownership"
  );

  lg::WirePacket wire;
  failures += expect(lg::encodeServerSnapshot(server.snapshot(), wire),
    "in-progress McGuffin snapshots should encode");
  lg::ServerSnapshot decoded;
  failures += expect(lg::decodeServerSnapshot(wire, decoded),
    "in-progress McGuffin snapshots should decode");
  failures += expect(
    decoded.mcguffin.state == server.snapshot().mcguffin.state &&
      decoded.mcguffinScores == server.snapshot().mcguffinScores &&
      decoded.mcguffinRoundsWon == server.snapshot().mcguffinRoundsWon,
    "snapshot state should reconstruct McGuffin state without transient events"
  );

  // Let blue finish the swapped second round, then verify that merely entering
  // an unclaimed deciding-round base does not commit its ownership.
  for (int tick = 0; tick < 12 && server.snapshot().mcguffinRound < 2; ++tick) {
    server.tick(lg::kFixedTickSeconds);
  }
  failures += expect(
    server.snapshot().mcguffinRound == 2,
    "the swapped second round should advance to the deciding round"
  );
  lg::McGuffinConfig decidingConfig = config;
  decidingConfig.installationDelayTicks = 3;
  server.setMcGuffinConfig(decidingConfig);
  for (int tick = 0;
       tick < 4 && server.snapshot().mcguffin.state != lg::McGuffinState::Carried;
       ++tick) {
    server.tick(lg::kFixedTickSeconds);
  }
  failures += expect(
    server.snapshot().mcguffin.state == lg::McGuffinState::Carried &&
      server.snapshot().mcguffinRedBaseOwner == lg::Team::None &&
      server.snapshot().mcguffinBlueBaseOwner == lg::Team::None,
    "deciding-round base entry should remain provisional during the install hold"
  );
  lg::CommandPacket cancelClaim;
  cancelClaim.playerIndex = server.snapshot().mcguffin.carrierIndex;
  cancelClaim.command.sequence = 20;
  cancelClaim.requestMcGuffinThrow = true;
  transport.sendCommand(cancelClaim);
  server.tick(lg::kFixedTickSeconds);
  failures += expect(
    server.snapshot().mcguffin.state == lg::McGuffinState::Dropped &&
      server.snapshot().mcguffinRedBaseOwner == lg::Team::None &&
      server.snapshot().mcguffinBlueBaseOwner == lg::Team::None,
    "leaving the install flow must not claim either deciding-round base"
  );
  decidingConfig.returnTicks = 1;
  server.setMcGuffinConfig(decidingConfig);
  for (int tick = 0;
       tick < 40 && server.snapshot().mcguffin.state != lg::McGuffinState::InstalledRed;
       ++tick) {
    server.tick(lg::kFixedTickSeconds);
  }
  failures += expect(
    server.snapshot().mcguffin.state == lg::McGuffinState::InstalledRed &&
      server.snapshot().mcguffinRedBaseOwner == lg::Team::Red &&
      server.snapshot().mcguffinBlueBaseOwner == lg::Team::Blue,
    "a completed deciding-round installation should commit physical base ownership"
  );

  rules.deathRespawnTicks = 1;
  server.setMatchRules(rules);
  lg::WeaponDamageTuning decidingDamage;
  decidingDamage.railgunDamage = 100;
  server.setRuntimeGameplayTuning(
    {}, 1.0F, 1.0F, 1000.0F, 20.0F, 1000.0F, 100,
    decidingDamage, 0.0F, 100, 100, true, false, 250, 750,
    lg::WeaponSwitchingMode::Crazy
  );
  lg::CommandPacket decidingKill;
  decidingKill.playerIndex = 1;
  decidingKill.command.sequence = 20;
  decidingKill.command.viewYawRadians = 3.1415927F;
  decidingKill.command.weapon = lg::Weapon::Railgun;
  decidingKill.command.attack = true;
  transport.sendCommand(decidingKill);
  server.tick(lg::kFixedTickSeconds);
  server.tick(lg::kFixedTickSeconds);
  failures += expect(
    server.snapshot().players[0].health == 100 &&
      server.snapshot().players[0].position.x < 0.0F,
    "a post-claim respawn should use the physical group owned by that team"
  );

  server.resetMatch();
  failures += expect(
    server.snapshot().mcguffinScores[0] == 0 &&
      server.snapshot().mcguffinRoundsWon[0] == 0 &&
      server.snapshot().mcguffin.state == lg::McGuffinState::NeutralSpawn,
    "match reset should clear objective state, scores, and round wins"
  );

  {
    lg::LoopbackTransport selectionTransport;
    lg::ServerGame selectionServer(selectionTransport);
    selectionServer.setArena(selectionArena());
    selectionServer.setConnectedPlayers({true, true, true, false, false, false});
    lg::MatchRules selectionRules;
    selectionRules.countdownTicks = 0;
    selectionRules.playerLimit = 3;
    selectionServer.setMatchRules(selectionRules);
    sendMode(selectionTransport, 1);
    selectionServer.tick(lg::kFixedTickSeconds);
    sendTeam(selectionTransport, 0, 2, lg::Team::Red);
    sendTeam(selectionTransport, 1, 1, lg::Team::Blue);
    sendTeam(selectionTransport, 2, 1, lg::Team::Red);
    selectionServer.tick(lg::kFixedTickSeconds);
    sendReady(selectionTransport, 0, 3);
    sendReady(selectionTransport, 1, 2);
    sendReady(selectionTransport, 2, 2);
    selectionServer.tick(lg::kFixedTickSeconds);
    const lg::ServerSnapshot& selected = selectionServer.snapshot();
    failures += expect(
      selected.matchPhase == lg::MatchPhase::Live &&
        selected.players[0].position.x == -15.0F &&
        selected.players[1].position.x == 15.0F &&
        selected.players[2].position.x == -15.0F,
      "team respawns should use candidates belonging to the currently owned physical base"
    );
    failures += expect(
      selected.players[0].position.y == 0.0F &&
        selected.players[2].position.y != selected.players[0].position.y,
      "LOS safety should prefer cover and occupied/recent candidates should not stack teammates"
    );
    failures += expect(
      selected.players[0].viewYawRadians == 0.25F &&
        selectionServer.spawnDebugString().find("SELECTED") != std::string::npos,
      "spawn selection should apply authored facing and retain score diagnostics"
    );
  }

  {
    lg::LoopbackTransport throwTransport;
    lg::ServerGame throwServer(throwTransport);
    lg::Arena throwArena = objectiveArena();
    throwArena.mcguffin.redBase.min = {-4.0F, -2.0F, 0.0F};
    throwArena.mcguffin.redBase.max = {-2.0F, 2.0F, 3.0F};
    throwArena.walls[0] = {
      {-6.45F, -1.0F, 0.0F}, {-6.35F, 1.0F, 3.0F},
    };
    throwArena.wallCount = 1;
    throwServer.setArena(throwArena);
    lg::MatchRules throwRules;
    throwRules.countdownTicks = 0;
    throwServer.setMatchRules(throwRules);
    lg::McGuffinConfig throwConfig;
    throwConfig.initialSpawnTicks = 0;
    throwConfig.throwSpeed = 12.0F;
    throwConfig.throwUpSpeed = 4.0F;
    throwConfig.throwPickupLockoutTicks = 25;
    throwConfig.returnTicks = 2;
    throwServer.setMcGuffinConfig(throwConfig);
    sendMode(throwTransport, 1);
    throwServer.tick(lg::kFixedTickSeconds);
    sendTeam(throwTransport, 0, 2, lg::Team::Red);
    sendTeam(throwTransport, 1, 1, lg::Team::Blue);
    throwServer.tick(lg::kFixedTickSeconds);
    sendReady(throwTransport, 0, 3);
    sendReady(throwTransport, 1, 2);
    throwServer.tick(lg::kFixedTickSeconds);
    failures += expect(
      throwServer.snapshot().mcguffin.state == lg::McGuffinState::Carried &&
        throwServer.snapshot().mcguffin.carrierIndex == 0,
      "authoritative contact should give the test player the objective"
    );
    lg::CommandPacket throwPacket;
    throwPacket.playerIndex = 0;
    throwPacket.command.sequence = 4;
    throwPacket.command.viewYawRadians = 0.0F;
    throwPacket.command.viewPitchRadians = 0.0F;
    throwPacket.requestMcGuffinThrow = true;
    throwTransport.sendCommand(throwPacket);
    lg::CommandPacket newerAimPacket;
    newerAimPacket.playerIndex = 0;
    newerAimPacket.command.sequence = 5;
    newerAimPacket.command.viewYawRadians = 1.5707963F;
    throwTransport.sendCommand(newerAimPacket);
    throwServer.tick(lg::kFixedTickSeconds);
    failures += expect(
      throwServer.snapshot().mcguffin.state == lg::McGuffinState::Dropped &&
        throwServer.snapshot().mcguffin.lastEvent == lg::McGuffinEventType::Throw &&
        throwServer.snapshot().mcguffin.velocity.x < 0.0F &&
        throwServer.snapshot().mcguffin.velocity.y < 1.0F &&
        throwServer.snapshot().mcguffin.position.x < -6.34F,
      "throw should launch from its own accepted aim even when a newer command shares the tick"
    );
    throwServer.tick(lg::kFixedTickSeconds);
    failures += expect(
      throwServer.snapshot().mcguffin.state == lg::McGuffinState::NeutralSpawn &&
        throwServer.snapshot().mcguffin.lastEvent == lg::McGuffinEventType::Return &&
        throwServer.snapshot().mcguffin.position.x ==
          throwArena.mcguffin.neutralSpawn.x,
      "an uncollected ground objective should return to its initial spawn after the safety timer"
    );
  }

  {
    lg::LoopbackTransport respawnTransport;
    lg::ServerGame respawnServer(respawnTransport);
    respawnServer.setArena(objectiveArena());
    lg::MatchRules respawnRules;
    respawnRules.countdownTicks = 0;
    respawnRules.deathRespawnTicks = 2;
    respawnServer.setMatchRules(respawnRules);
    lg::McGuffinConfig respawnConfig;
    respawnConfig.initialSpawnTicks = 0;
    respawnServer.setMcGuffinConfig(respawnConfig);
    lg::WeaponDamageTuning damage;
    damage.railgunDamage = 100;
    respawnServer.setRuntimeGameplayTuning(
      {}, 1.0F, 1.0F, 1000.0F, 20.0F, 1000.0F, 100,
      damage, 0.0F, 100, 100, true, false, 250, 750,
      lg::WeaponSwitchingMode::Crazy
    );
    sendMode(respawnTransport, 1);
    respawnServer.tick(lg::kFixedTickSeconds);
    sendTeam(respawnTransport, 0, 2, lg::Team::Red);
    sendTeam(respawnTransport, 1, 1, lg::Team::Blue);
    respawnServer.tick(lg::kFixedTickSeconds);
    sendReady(respawnTransport, 0, 3);
    sendReady(respawnTransport, 1, 2);
    respawnServer.tick(lg::kFixedTickSeconds);
    lg::CommandPacket killPacket;
    killPacket.playerIndex = 0;
    killPacket.command.sequence = 4;
    killPacket.command.viewYawRadians = 0.0F;
    killPacket.command.weapon = lg::Weapon::Railgun;
    killPacket.command.attack = true;
    respawnTransport.sendCommand(killPacket);
    respawnServer.tick(lg::kFixedTickSeconds);
    failures += expect(
      respawnServer.snapshot().players[1].health == 0 &&
        respawnServer.snapshot().respawnTicksRemaining[1] == 2,
      "McGuffin death should start the shared respawn timer"
    );
    respawnServer.tick(lg::kFixedTickSeconds);
    failures += expect(
      respawnServer.snapshot().players[1].health == 0 &&
        respawnServer.snapshot().respawnTicksRemaining[1] == 1,
      "player should remain dead until the shared timer expires"
    );
    respawnServer.tick(lg::kFixedTickSeconds);
    failures += expect(
      respawnServer.snapshot().players[1].health == 100 &&
        respawnServer.snapshot().respawnTicksRemaining[1] == 0,
      "shared death timer should respawn exactly on its configured tick"
    );
    lg::CommandPacket releasePacket;
    releasePacket.playerIndex = 0;
    releasePacket.command.sequence = 5;
    releasePacket.command.viewYawRadians = 0.0F;
    releasePacket.command.weapon = lg::Weapon::Railgun;
    respawnTransport.sendCommand(releasePacket);
    respawnServer.tick(lg::kFixedTickSeconds);
    for (int tick = 0; tick < 187; ++tick) {
      respawnServer.tick(lg::kFixedTickSeconds);
    }
    respawnRules.deathRespawnTicks = 0;
    respawnServer.setMatchRules(respawnRules);
    lg::CommandPacket victimStatePacket;
    victimStatePacket.playerIndex = 1;
    victimStatePacket.command.sequence = 3;
    victimStatePacket.command.rightMove = 1.0F;
    victimStatePacket.command.dash = true;
    respawnTransport.sendCommand(victimStatePacket);
    killPacket.command.sequence = 6;
    respawnTransport.sendCommand(killPacket);
    respawnServer.tick(lg::kFixedTickSeconds);
    const lg::PlayerState& newLife = respawnServer.snapshot().players[1];
    failures += expect(
      respawnServer.snapshot().fragEvents[0].active &&
        newLife.health == 100 &&
        respawnServer.snapshot().respawnTicksRemaining[1] == 0 &&
        newLife.velocity.x == 0.0F && newLife.velocity.y == 0.0F &&
        newLife.velocity.z == 0.0F && newLife.freezeLevel == 0.0F &&
        newLife.knockbackTicksRemaining == 0 &&
        newLife.dashActiveTicksRemaining == 0 &&
        newLife.dashCooldownTicksRemaining == 0 &&
        respawnServer.snapshot().selectedWeapons[1] == lg::Weapon::LightningGun,
      "zero-delay respawn should create a clean new life without inherited state"
    );
    const lg::Vec3 cleanSpawn = newLife.position;
    respawnServer.tick(lg::kFixedTickSeconds);
    failures += expect(
      respawnServer.snapshot().players[1].position.x == cleanSpawn.x &&
        respawnServer.snapshot().players[1].position.y == cleanSpawn.y,
      "cached pre-death movement input should not execute on the new life"
    );
  }

  {
    lg::LoopbackTransport worldDeathTransport;
    lg::ServerGame worldDeathServer(worldDeathTransport);
    lg::Arena arena = objectiveArena();
    arena.killVolumeCount = 1;
    arena.killVolumes[0] = {{-1.0F, -1.0F, 0.0F}, {1.0F, 1.0F, 2.0F}};
    worldDeathServer.setArena(arena);
    lg::MatchRules rules;
    rules.deathRespawnTicks = 2;
    worldDeathServer.setMatchRules(rules);

    lg::ScenarioSetup setup;
    setup.match.gameMode = lg::GameMode::McGuffin;
    setup.match.phase = lg::MatchPhase::Live;
    setup.players[0].connected = true;
    setup.players[0].ready = true;
    setup.players[0].team = lg::Team::Red;
    setup.players[0].alive = true;
    setup.players[0].health = 100;
    setup.players[0].position = {-6.0F, 0.0F, 0.9F};
    setup.players[0].onGround = true;
    setup.players[1].connected = true;
    setup.players[1].ready = true;
    setup.players[1].team = lg::Team::Blue;
    setup.players[1].alive = true;
    setup.players[1].health = 100;
    setup.players[1].position = {0.0F, 0.0F, 0.9F};
    setup.players[1].onGround = true;
    std::string setupError;
    failures += expect(
      worldDeathServer.applyScenarioSetup(setup, &setupError),
      "McGuffin world-death scenario should apply"
    );

    worldDeathServer.tick(lg::kFixedTickSeconds);
    failures += expect(
      worldDeathServer.snapshot().players[1].health == 0 &&
        worldDeathServer.snapshot().respawnTicksRemaining[1] == 2,
      "McGuffin world death should keep the full respawn delay on its death tick"
    );
    worldDeathServer.tick(lg::kFixedTickSeconds);
    failures += expect(
      worldDeathServer.snapshot().players[1].health == 0 &&
        worldDeathServer.snapshot().respawnTicksRemaining[1] == 1,
      "McGuffin world-death respawn delay should start on the next tick"
    );
    worldDeathServer.tick(lg::kFixedTickSeconds);
    failures += expect(
      worldDeathServer.snapshot().players[1].health == 100 &&
        worldDeathServer.snapshot().respawnTicksRemaining[1] == 0,
      "McGuffin world death should respawn on the configured tick"
    );
  }

  return failures == 0 ? 0 : 1;
}
