#include "net/LoopbackTransport.hpp"
#include "server/ServerGame.hpp"
#include "shared/Constants.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) return 0;
  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

lg::ServerSnapshot latestSnapshot(lg::LoopbackTransport& transport) {
  lg::ServerSnapshot latest;
  lg::ServerSnapshot received;
  while (transport.receiveSnapshot(received)) latest = received;
  return latest;
}

lg::ServerSnapshot sendAndTick(
  lg::LoopbackTransport& transport,
  lg::ServerGame& server,
  const lg::CommandPacket& packet
) {
  transport.sendCommand(packet);
  server.tick(lg::kFixedTickSeconds);
  return latestSnapshot(transport);
}

lg::CommandPacket modeRequest(std::uint32_t sequence, lg::GameMode mode) {
  lg::CommandPacket packet;
  packet.playerIndex = 0;
  packet.command.sequence = sequence;
  packet.requestGameMode = true;
  packet.requestedGameMode = mode;
  return packet;
}

lg::CommandPacket readyRequest(std::uint8_t player, std::uint32_t sequence) {
  lg::CommandPacket packet;
  packet.playerIndex = player;
  packet.command.sequence = sequence;
  packet.toggleReady = true;
  return packet;
}

lg::CommandPacket aimedRail(
  const lg::ServerSnapshot& snapshot,
  std::uint8_t attacker,
  std::uint8_t target,
  std::uint32_t sequence
) {
  const lg::PlayerState& source = snapshot.players[attacker];
  const lg::Vec3 muzzle = source.position + lg::Vec3{0.0F, 0.0F, 0.65F};
  const lg::Vec3 delta = snapshot.players[target].position - muzzle;
  lg::CommandPacket packet;
  packet.playerIndex = attacker;
  packet.command.sequence = sequence;
  packet.command.attack = true;
  packet.command.weapon = lg::Weapon::Railgun;
  packet.command.planarAim = false;
  packet.command.viewYawRadians = std::atan2(delta.y, delta.x);
  packet.command.viewPitchRadians = std::atan2(
    delta.z,
    std::hypot(delta.x, delta.y)
  );
  return packet;
}

lg::CommandPacket aimedShotgun(
  const lg::ServerSnapshot& snapshot,
  std::uint8_t attacker,
  std::uint8_t target,
  std::uint32_t sequence
) {
  lg::CommandPacket packet = aimedRail(snapshot, attacker, target, sequence);
  packet.command.weapon = lg::Weapon::Shotgun;
  return packet;
}

lg::Arena testArena() {
  lg::Arena arena;
  arena.min = {-20.0F, -20.0F, 0.0F};
  arena.max = {20.0F, 20.0F, 10.0F};
  arena.spawnCount = 4;
  arena.spawnPositions[0] = {-6.0F, 0.0F, 0.0F};
  arena.spawnPositions[1] = {6.0F, 0.0F, 0.0F};
  arena.spawnPositions[2] = {0.0F, -6.0F, 0.0F};
  arena.spawnPositions[3] = {0.0F, 6.0F, 0.0F};
  return arena;
}

lg::ScenarioSetup liveSetup(
  lg::PlayerScore firstScore = 0,
  lg::PlayerScore secondScore = 0,
  int secondHealth = 80
) {
  lg::ScenarioSetup setup;
  setup.seed = 71;
  setup.match.gameMode = lg::GameMode::FreeForAll;
  setup.match.phase = lg::MatchPhase::Live;
  setup.match.scores = {firstScore, secondScore};
  setup.players[0].connected = true;
  setup.players[0].ready = true;
  setup.players[0].alive = true;
  setup.players[0].health = 100;
  setup.players[0].position = {-6.0F, 0.0F, 0.9F};
  setup.players[0].onGround = true;
  setup.players[1].connected = true;
  setup.players[1].ready = true;
  setup.players[1].alive = true;
  setup.players[1].health = secondHealth;
  setup.players[1].position = {6.0F, 0.0F, 0.9F};
  setup.players[1].onGround = true;
  return setup;
}

bool applySetup(lg::ServerGame& server, const lg::ScenarioSetup& setup) {
  std::string error;
  if (server.applyScenarioSetup(setup, &error)) return true;
  std::cerr << "scenario setup failed: " << error << '\n';
  return false;
}

bool hasPlayerProjectile(const lg::ServerGame& server, std::size_t playerIndex) {
  const std::size_t firstSlot = playerIndex * lg::kProjectileSlotsPerPlayer;
  const std::size_t endSlot = firstSlot + lg::kProjectileSlotsPerPlayer;
  for (std::size_t slot = firstSlot; slot < endSlot; ++slot) {
    if (server.projectiles()[slot].active) {
      return true;
    }
  }
  return false;
}

lg::ServerSnapshot fireSelfRocket(
  lg::LoopbackTransport& transport,
  lg::ServerGame& server,
  std::uint32_t sequence
) {
  lg::CommandPacket rocket;
  rocket.playerIndex = 0;
  rocket.command.sequence = sequence;
  rocket.command.attack = true;
  rocket.command.weapon = lg::Weapon::RocketLauncher;
  rocket.command.planarAim = false;
  rocket.command.viewPitchRadians = -1.57079632679F;
  transport.sendCommand(rocket);
  lg::ServerSnapshot snapshot = server.snapshot();
  for (int tick = 0; tick < 220 && snapshot.players[0].health > 0; ++tick) {
    server.tick(lg::kFixedTickSeconds);
    snapshot = latestSnapshot(transport);
  }
  return snapshot;
}

} // namespace

int main() {
  int failures = 0;

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    server.setArena(testArena());
    latestSnapshot(transport);
    lg::MatchRules configured;
    configured.roundLimit = 7;
    configured.timeLimitMinutes = 3;
    configured.countdownTicks = 0;
    server.setMatchRules(configured);

    lg::ServerSnapshot snapshot = sendAndTick(
      transport,
      server,
      modeRequest(1, lg::GameMode::FreeForAll)
    );
    failures += expect(
      snapshot.gameMode == lg::GameMode::FreeForAll &&
        snapshot.matchRules.roundLimit == 100 &&
        snapshot.matchRules.timeLimitMinutes == 10 &&
        server.matchRules().roundLimit == 7 &&
        server.matchRules().timeLimitMinutes == 3,
      "FFA should expose fixed limits without changing configured rules"
    );

    lg::CommandPacket team;
    team.playerIndex = 0;
    team.command.sequence = 2;
    team.requestTeam = true;
    team.requestedTeam = lg::Team::Red;
    snapshot = sendAndTick(transport, server, team);
    failures += expect(
      snapshot.teams[0] == lg::Team::None &&
        snapshot.teams[1] == lg::Team::None,
      "FFA should reject team assignment"
    );
    const lg::BotRosterChange added = server.addBots(1);
    snapshot = server.snapshot();
    failures += expect(
      added.ok && added.changed == 1U && snapshot.botPlayers[2] &&
        snapshot.participatingPlayers[2] &&
        snapshot.teams[2] == lg::Team::None,
      "FFA bots should join as ready participants without a team"
    );
    sendAndTick(transport, server, readyRequest(0, 3));
    snapshot = sendAndTick(transport, server, readyRequest(1, 1));
    failures += expect(
      snapshot.matchPhase == lg::MatchPhase::Live,
      "FFA should start from ready players without a team gate"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    server.setArena(testArena());
    lg::MatchRules rules;
    rules.deathRespawnTicks = 2;
    server.setMatchRules(rules);
    failures += expect(applySetup(server, liveSetup()), "FFA combat setup should load");

    lg::ServerSnapshot snapshot = server.snapshot();
    snapshot = sendAndTick(transport, server, aimedRail(snapshot, 0, 1, 1));
    failures += expect(
      snapshot.scores[0] == 1 && snapshot.players[1].health == 0 &&
        snapshot.respawnTicksRemaining[1] == 2 &&
        snapshot.matchPhase == lg::MatchPhase::Live,
      "an FFA enemy kill should add one and start the death-respawn timer"
    );
    server.tick(lg::kFixedTickSeconds);
    snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.respawnTicksRemaining[1] == 1 &&
        snapshot.matchPhase == lg::MatchPhase::Live,
      "an FFA death should not cause a round transition"
    );
    server.tick(lg::kFixedTickSeconds);
    snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.players[1].health == 100 &&
        snapshot.respawnTicksRemaining[1] == 0 &&
        snapshot.matchPhase == lg::MatchPhase::Live,
      "the shared death timer should respawn an FFA player"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    server.setArena(testArena());
    lg::MatchRules rules;
    rules.deathRespawnTicks = 2;
    server.setMatchRules(rules);
    lg::BalanceConfig balance;
    balance.shotgun.pelletCount = 2U;
    balance.shotgun.spreadRadians = 0.12F;
    server.applyBalanceConfig(balance);
    lg::ScenarioSetup setup = liveSetup();
    setup.players[1].health = 5;
    setup.players[1].position = {0.0F, 0.0F, 0.9F};
    setup.players[2].connected = true;
    setup.players[2].ready = true;
    setup.players[2].alive = true;
    setup.players[2].health = 5;
    setup.players[2].position = {0.0F, 0.55F, 1.35F};
    failures += expect(
      applySetup(server, setup),
      "FFA shotgun multi-target setup should load"
    );
    const lg::ServerSnapshot beforeShot = server.snapshot();
    const lg::ServerSnapshot snapshot = sendAndTick(
      transport,
      server,
      aimedShotgun(beforeShot, 0, 1, 1)
    );
    const auto& shotgunStats = snapshot.matchCombatStats[0].weapons[
      lg::weaponIndex(lg::Weapon::Shotgun)
    ];
    failures += expect(
      snapshot.weaponFires[0].pelletCount == 2U &&
        snapshot.weaponFires[0].pelletHitCount == 2U &&
        snapshot.players[1].health == 0 &&
        snapshot.players[2].health == 0 &&
        snapshot.scores[0] == 2 &&
        shotgunStats.attempts == 2U &&
        shotgunStats.hits == 2U,
      "an FFA shotgun blast should split pellets, score both kills, and count pellet accuracy"
    );
    failures += expect(
      snapshot.localHitFeedbackEvents[0][0].active &&
        snapshot.localHitFeedbackEvents[0][1].active,
      "a shotgun kill on two FFA targets should retain per-target hit feedback"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    server.setArena(testArena());
    lg::MatchRules rules;
    rules.deathRespawnTicks = 2;
    server.setMatchRules(rules);
    lg::ScenarioSetup setup = liveSetup();
    setup.players[0].health = 20;
    failures += expect(applySetup(server, setup), "FFA self-kill setup should load");

    const lg::ServerSnapshot snapshot = fireSelfRocket(transport, server, 1);
    failures += expect(
      snapshot.players[0].health == 0 && snapshot.scores[0] == -1 &&
        snapshot.respawnTicksRemaining[0] > 0,
      "an FFA self-kill should subtract one and keep the signed score"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    lg::Arena arena = testArena();
    arena.killVolumeCount = 1;
    arena.killVolumes[0] = {{5.5F, -1.0F, 0.0F}, {6.5F, 1.0F, 2.0F}};
    server.setArena(arena);
    lg::MatchRules rules;
    rules.deathRespawnTicks = 2;
    server.setMatchRules(rules);
    failures += expect(
      applySetup(server, liveSetup(3, 4, 100)),
      "FFA world-death setup should load"
    );

    server.tick(lg::kFixedTickSeconds);
    lg::ServerSnapshot snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.players[1].health == 0 && snapshot.scores[1] == 3 &&
        snapshot.respawnTicksRemaining[1] == 2 &&
        snapshot.matchPhase == lg::MatchPhase::Live,
      "an FFA world death should subtract one and keep the full respawn delay"
    );
    failures += expect(
      !snapshot.fragEvents[0].active && !snapshot.fragEvents[1].active,
      "an FFA world death should not emit a player frag"
    );
    server.tick(lg::kFixedTickSeconds);
    snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.players[1].health == 0 &&
        snapshot.respawnTicksRemaining[1] == 1,
      "an FFA world-death respawn delay should start on the next tick"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    server.setArena(testArena());
    lg::ScenarioSetup setup = liveSetup(
      std::numeric_limits<lg::PlayerScore>::min(),
      0
    );
    setup.players[0].health = 20;
    failures += expect(
      applySetup(server, setup),
      "FFA minimum-score setup should load"
    );
    const lg::ServerSnapshot snapshot = fireSelfRocket(transport, server, 1);
    failures += expect(
      snapshot.players[0].health == 0 &&
        snapshot.scores[0] == std::numeric_limits<lg::PlayerScore>::min(),
      "an FFA self-kill at the minimum score should not wrap"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    server.setArena(testArena());
    failures += expect(
      applySetup(server, liveSetup(
        std::numeric_limits<lg::PlayerScore>::max(),
        0
      )),
      "FFA maximum-score setup should load"
    );
    lg::ServerSnapshot snapshot = server.snapshot();
    snapshot = sendAndTick(transport, server, aimedRail(snapshot, 0, 1, 1));
    failures += expect(
      snapshot.scores[0] == std::numeric_limits<lg::PlayerScore>::max(),
      "an FFA kill at the maximum score should not wrap"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    server.setArena(testArena());
    failures += expect(
      applySetup(server, liveSetup(99, 0)),
      "FFA score-limit setup should load"
    );
    lg::ServerSnapshot snapshot = server.snapshot();
    snapshot = sendAndTick(transport, server, aimedRail(snapshot, 0, 1, 1));
    failures += expect(
      snapshot.scores[0] == 100 &&
        snapshot.matchPhase == lg::MatchPhase::MatchEnd &&
        snapshot.matchWinner == 0,
      "the kill that reaches 100 should end FFA at once"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    lg::Arena arena = testArena();
    arena.killVolumeCount = 1;
    arena.killVolumes[0] = {{-1.0F, -6.5F, 0.0F}, {1.0F, -5.5F, 2.0F}};
    server.setArena(arena);
    lg::MatchRules rules;
    rules.deathRespawnTicks = 2;
    server.setMatchRules(rules);
    lg::ScenarioSetup setup = liveSetup(99, 0);
    setup.players[2].connected = true;
    setup.players[2].ready = true;
    setup.players[2].alive = true;
    setup.players[2].health = 100;
    setup.players[2].position = {0.0F, -6.0F, 0.9F};
    setup.players[2].onGround = true;
    failures += expect(
      applySetup(server, setup),
      "FFA match-end respawn setup should load"
    );

    server.tick(lg::kFixedTickSeconds);
    lg::ServerSnapshot snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.players[2].health == 0 &&
        snapshot.respawnTicksRemaining[2] == 2 &&
        snapshot.matchPhase == lg::MatchPhase::Live,
      "an earlier FFA world death should start its respawn timer"
    );
    snapshot = sendAndTick(transport, server, aimedRail(snapshot, 0, 1, 1));
    failures += expect(
      snapshot.scores[0] == 100 &&
        snapshot.matchPhase == lg::MatchPhase::MatchEnd &&
        snapshot.respawnTicksRemaining[2] == 1,
      "a later cap kill should end FFA with the earlier timer pending"
    );
    server.tick(lg::kFixedTickSeconds);
    snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.matchPhase == lg::MatchPhase::MatchEnd &&
        snapshot.players[2].health == 0 &&
        snapshot.respawnTicksRemaining[2] == 1,
      "FFA death timers should stop after match end"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    server.setArena(testArena());
    lg::ScenarioSetup setup = liveSetup(5, 2, 100);
    setup.match.liveTicksElapsed = 10U * 60U * 125U - 1U;
    failures += expect(applySetup(server, setup), "FFA time-limit setup should load");
    server.tick(lg::kFixedTickSeconds);
    const lg::ServerSnapshot snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.liveTicksElapsed == 10U * 60U * 125U &&
        snapshot.matchPhase == lg::MatchPhase::MatchEnd &&
        snapshot.matchWinner == 0,
      "a unique FFA leader should win at exactly ten minutes"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    server.setArena(testArena());
    lg::ScenarioSetup setup = liveSetup(4, 4);
    setup.match.liveTicksElapsed = 10U * 60U * 125U - 1U;
    failures += expect(applySetup(server, setup), "FFA overtime setup should load");
    server.tick(lg::kFixedTickSeconds);
    lg::ServerSnapshot snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.matchPhase == lg::MatchPhase::Live && snapshot.overtime,
      "a tied FFA score at ten minutes should enter overtime"
    );
    snapshot = sendAndTick(transport, server, aimedRail(snapshot, 0, 1, 1));
    failures += expect(
      snapshot.scores[0] == 5 &&
        snapshot.matchPhase == lg::MatchPhase::MatchEnd &&
        snapshot.matchWinner == 0,
      "the next FFA score in overtime should resolve the match"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    server.setArena(testArena());
    lg::MatchRules rules;
    rules.deathRespawnTicks = 20;
    server.setMatchRules(rules);
    lg::ScenarioSetup setup = liveSetup(5, 2);
    setup.players[2].connected = true;
    setup.players[2].ready = true;
    setup.players[2].alive = true;
    setup.players[2].health = 1;
    setup.players[2].position = {0.0F, -6.0F, 0.9F};
    setup.players[2].onGround = true;
    setup.match.scores[2] = 1;
    failures += expect(
      applySetup(server, setup),
      "FFA roster-change setup should load"
    );
    lg::CommandPacket rocket;
    rocket.playerIndex = 2;
    rocket.command.sequence = 1;
    rocket.command.attack = true;
    rocket.command.weapon = lg::Weapon::RocketLauncher;
    rocket.command.planarAim = false;
    rocket.command.viewYawRadians = -1.57079632679F;
    lg::ServerSnapshot snapshot = sendAndTick(transport, server, rocket);
    failures += expect(
      hasPlayerProjectile(server, 2),
      "the old FFA slot owner should have a live projectile before leaving"
    );
    snapshot = sendAndTick(transport, server, aimedRail(snapshot, 0, 2, 1));
    failures += expect(
      snapshot.players[2].health == 0 &&
        snapshot.respawnTicksRemaining[2] == 20,
      "the old FFA slot owner should have a pending death timer"
    );
    server.setConnectedPlayers({true, true});
    failures += expect(
      server.snapshot().matchPhase == lg::MatchPhase::Live &&
        server.snapshot().scores[0] == 6 &&
        server.snapshot().scores[2] == 0 &&
        server.snapshot().players[2].health == 0 &&
        server.snapshot().respawnTicksRemaining[2] == 0 &&
        !hasPlayerProjectile(server, 2) &&
        !server.snapshot().participatingPlayers[2],
      "FFA should clear a departed player's score, life, timer, and shots"
    );
    server.setConnectedPlayers({true, true, true});
    failures += expect(
      server.snapshot().matchPhase == lg::MatchPhase::Live &&
        server.snapshot().scores[0] == 6 &&
        server.snapshot().scores[2] == 0 &&
        server.snapshot().players[2].health == 100 &&
        server.snapshot().respawnTicksRemaining[2] == 0 &&
        !hasPlayerProjectile(server, 2) &&
        server.snapshot().participatingPlayers[2],
      "a new human should receive a fresh FFA slot life"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    server.setArena(testArena());
    lg::ScenarioSetup setup = liveSetup(5, 2);
    setup.players[2].bot = true;
    setup.players[2].alive = true;
    setup.players[2].health = 100;
    setup.players[2].position = {0.0F, -6.0F, 0.9F};
    setup.players[2].onGround = true;
    setup.match.scores[2] = 99;
    failures += expect(
      applySetup(server, setup),
      "FFA bot-slot reuse setup should load"
    );
    server.setConnectedPlayers({true, true, true});
    failures += expect(
      server.snapshot().matchPhase == lg::MatchPhase::Live &&
        !server.snapshot().botPlayers[2] &&
        server.snapshot().connectedPlayers[2] &&
        server.snapshot().scores[2] == 0,
      "a human replacing an FFA bot should not inherit the bot's score"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    server.setArena(testArena());
    lg::ScenarioSetup setup = liveSetup(5, 2);
    setup.players[2].connected = true;
    setup.players[2].ready = true;
    setup.players[2].alive = true;
    setup.players[2].health = 100;
    setup.players[2].position = {0.0F, -6.0F, 0.9F};
    setup.players[2].onGround = true;
    setup.match.scores[2] = 99;
    failures += expect(
      applySetup(server, setup),
      "FFA session-reuse setup should load"
    );
    server.setConnectedPlayers(
      {true, true, true},
      {10, 20, 30}
    );
    server.setConnectedPlayers(
      {true, true, true},
      {10, 20, 31}
    );
    failures += expect(
      server.snapshot().matchPhase == lg::MatchPhase::Live &&
        server.snapshot().scores[0] == 5 &&
        server.snapshot().scores[2] == 0,
      "a changed FFA session should not inherit the prior session's score"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    server.setArena(testArena());
    lg::MatchRules configured;
    configured.roundLimit = 7;
    configured.timeLimitMinutes = 3;
    server.setMatchRules(configured);
    failures += expect(
      applySetup(server, liveSetup(-4, 2)),
      "FFA replay checkpoint setup should load"
    );
    std::string error;
    failures += expect(
      server.beginReplayRecording({}, &error),
      "FFA replay recording should start"
    );
    server.tick(lg::kFixedTickSeconds);
    const std::optional<lg::replay::ReplayDemo> recording =
      server.finishReplayRecording();
    failures += expect(
      recording.has_value() && !recording->checkpoints.empty() &&
        recording->metadata.gameMode == lg::GameMode::FreeForAll &&
        recording->metadata.matchRules.roundLimit == 7 &&
        recording->metadata.matchRules.timeLimitMinutes == 3,
      "FFA replay metadata should keep the configured base rules"
    );

    lg::LoopbackTransport playbackTransport;
    lg::ServerGame playback(playbackTransport);
    playback.setArena(testArena());
    playback.setMatchRules(configured);
    const bool restored = recording.has_value() &&
      !recording->checkpoints.empty() &&
      playback.restoreReplayCheckpoint(
        recording->checkpoints.front(),
        recording->metadata,
        &error
      );
    if (!restored) {
      std::cerr << "FFA checkpoint restore failed: " << error << '\n';
    }
    failures += expect(
      restored && playback.snapshot().gameMode == lg::GameMode::FreeForAll &&
        playback.snapshot().scores[0] == -4 &&
        playback.snapshot().matchRules.roundLimit == 100 &&
        playback.snapshot().matchRules.timeLimitMinutes == 10 &&
        playback.matchRules().roundLimit == 7 &&
        playback.matchRules().timeLimitMinutes == 3,
      "FFA checkpoint restore should rebuild fixed effective limits from base rules"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    server.setArena(testArena());
    latestSnapshot(transport);
    lg::MatchRules configured;
    configured.roundLimit = 6;
    configured.timeLimitMinutes = 2;
    server.setMatchRules(configured);
    lg::ServerSnapshot snapshot = sendAndTick(
      transport,
      server,
      modeRequest(1, lg::GameMode::FreeForAll)
    );
    snapshot = sendAndTick(transport, server, modeRequest(2, lg::GameMode::Duel));
    failures += expect(
      snapshot.matchRules.roundLimit == 6 &&
        snapshot.matchRules.timeLimitMinutes == 2,
      "switching from FFA to Duel should restore configured limits"
    );
    snapshot = sendAndTick(
      transport,
      server,
      modeRequest(3, lg::GameMode::ClanArena)
    );
    failures += expect(
      snapshot.matchRules.roundLimit == 6 &&
        snapshot.matchRules.timeLimitMinutes == 2,
      "switching from FFA to Clan Arena should not leak FFA limits"
    );
  }

  return failures == 0 ? 0 : 1;
}
