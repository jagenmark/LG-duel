#include "net/LoopbackTransport.hpp"
#include "net/NetCodec.hpp"
#include "server/ServerGame.hpp"
#include "shared/Constants.hpp"
#include "sim/BalanceConfig.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <optional>
#include <string_view>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) {
    return 0;
  }
  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

lg::ServerSnapshot latestSnapshot(lg::LoopbackTransport& transport) {
  lg::ServerSnapshot latest;
  lg::ServerSnapshot received;
  while (transport.receiveSnapshot(received)) {
    latest = received;
  }
  return latest;
}

lg::Arena flatArena(bool blocked = false) {
  lg::Arena arena;
  arena.min = {-12.0F, -12.0F, 0.0F};
  arena.max = {12.0F, 12.0F, 6.0F};
  arena.spawnPositions[0] = {-3.0F, 0.0F, 0.0F};
  arena.spawnPositions[1] = {3.0F, 0.0F, 0.0F};
  arena.spawnPositions[2] = {0.0F, 5.0F, 0.0F};
  arena.spawnPositions[3] = {0.0F, -5.0F, 0.0F};
  arena.spawnPositions[4] = {-5.0F, 5.0F, 0.0F};
  arena.spawnPositions[5] = {5.0F, -5.0F, 0.0F};
  if (blocked) {
    arena.walls[0].min = {-0.2F, -12.0F, 0.0F};
    arena.walls[0].max = {0.2F, 12.0F, 4.0F};
    arena.wallCount = 1;
  }
  return arena;
}

void makeOneHumanWarmup(lg::ServerGame& server) {
  server.setArena(flatArena());
  server.setConnectedPlayers({true, false, false, false, false, false});
}

void readyHuman(lg::LoopbackTransport& transport, lg::ServerGame& server) {
  lg::CommandPacket ready;
  ready.playerIndex = 0;
  ready.command.sequence = 1;
  ready.toggleReady = true;
  transport.sendCommand(ready);
  server.tick(lg::kFixedTickSeconds);
}

float angleDelta(float from, float to) {
  constexpr float pi = 3.14159265359F;
  constexpr float twoPi = 2.0F * pi;
  float delta = to - from;
  while (delta <= -pi) {
    delta += twoPi;
  }
  while (delta > pi) {
    delta -= twoPi;
  }
  return delta;
}

std::array<float, 2> aimFromBotToHuman(const lg::ServerSnapshot& snapshot) {
  const lg::PlayerState& bot = snapshot.players[1];
  const lg::PlayerState& human = snapshot.players[0];
  const lg::Vec3 start =
    bot.position + lg::Vec3{0.0F, 0.0F, 0.65F};
  const lg::Vec3 target =
    human.position + lg::Vec3{0.0F, 0.0F, human.bounds.halfHeight * 0.45F};
  const lg::Vec3 delta = target - start;
  return {
    std::atan2(delta.y, delta.x),
    std::atan2(delta.z, std::hypot(delta.x, delta.y)),
  };
}

bool navPathExists(
  const lg::BotNavigationMap& map,
  std::size_t start,
  std::size_t target
) {
  if (start >= map.nodeCount || target >= map.nodeCount) return false;
  std::array<bool, lg::BotNavigationMap::kMaxNodes> visited = {};
  std::array<std::size_t, lg::BotNavigationMap::kMaxNodes> queue = {};
  std::size_t read = 0;
  std::size_t written = 0;
  queue[written++] = start;
  visited[start] = true;
  while (read < written) {
    const std::size_t current = queue[read++];
    if (current == target) return true;
    for (std::size_t index = 0; index < map.linkCount; ++index) {
      const lg::BotNavLink& link = map.links[index];
      if (link.from != current || visited[link.to]) continue;
      visited[link.to] = true;
      queue[written++] = link.to;
    }
  }
  return false;
}

bool hasNavLink(
  const lg::BotNavigationMap& map,
  lg::BotNavLinkKind kind
) {
  for (std::size_t index = 0; index < map.linkCount; ++index) {
    if (map.links[index].kind == kind) return true;
  }
  return false;
}

bool sameMotorCommand(const lg::BotMotor& first, const lg::BotMotor& second) {
  const lg::UserCommand& a = first.command;
  const lg::UserCommand& b = second.command;
  return a.viewYawRadians == b.viewYawRadians &&
    a.viewPitchRadians == b.viewPitchRadians &&
    a.forwardMove == b.forwardMove && a.rightMove == b.rightMove &&
    a.attack == b.attack && a.jump == b.jump && a.dash == b.dash &&
    a.weapon == b.weapon && first.targetPlayerIndex == second.targetPlayerIndex &&
    first.goal == second.goal && first.noFireReason == second.noFireReason;
}

} // namespace

int main() {
  int failures = 0;

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);
    makeOneHumanWarmup(server);

    lg::BotRosterChange added = server.addBots();
    const lg::ServerSnapshot& snapshot = server.snapshot();
    failures += expect(added.ok && added.changed == lg::kDuelPlayerCount - 1U,
      "bot_add should fill every free non-human slot");
    failures += expect(snapshot.connectedPlayers[0] && !snapshot.connectedPlayers[1],
      "bots must not be marked as connected clients");
    failures += expect(!snapshot.botPlayers[0] && snapshot.botPlayers[1] && snapshot.botPlayers[5],
      "bot_add should replicate explicit bot slots");
    failures += expect(snapshot.participatingPlayers[1] && snapshot.readyPlayers[1],
      "bots should occupy gameplay slots and be auto-ready");
    failures += expect(snapshot.playerNames[1] == "BOT 2",
      "bots should use stable slot-based names");

    failures += expect(server.kickBotAtPlayerIndex(2).ok,
      "bot_kick <slot> should remove the requested bot");
    failures += expect(!server.snapshot().botPlayers[2] && server.snapshot().botPlayers[1],
      "bot_kick <slot> should only remove one bot");
    failures += expect(!server.kickBotAtPlayerIndex(0).ok,
      "bot_kick <slot> should reject human slots");
    failures += expect(server.kickAllBots().changed == lg::kDuelPlayerCount - 2U,
      "bot_kick all should remove all remaining bots");

    added = server.addBots(2);
    failures += expect(added.changed == 2 && server.snapshot().botPlayers[1] &&
      server.snapshot().botPlayers[2] && !server.snapshot().botPlayers[3],
      "bot_add <count> should clamp to the requested count");
    server.resetMatch();
    failures += expect(server.snapshot().botPlayers[1] && server.snapshot().botPlayers[2],
      "bots should survive resetmatch");
    server.setArena(flatArena());
    failures += expect(server.snapshot().botPlayers[1] && server.snapshot().botPlayers[2],
      "bots should survive map reloads");

    server.setConnectedPlayers({true, true, false, false, false, false});
    failures += expect(!server.snapshot().botPlayers[1] &&
      server.snapshot().connectedPlayers[1] &&
      !server.snapshot().readyPlayers[1] &&
      server.snapshot().playerNames[1] == "PLAYER 2",
      "a human connection should cleanly replace a bot slot");
  }

  {
    const lg::BotDifficultyProfile easy = lg::botDifficultyProfile(lg::BotAttackMode::Easy);
    const lg::BotDifficultyProfile medium = lg::botDifficultyProfile(lg::BotAttackMode::Medium);
    const lg::BotDifficultyProfile hard = lg::botDifficultyProfile(lg::BotAttackMode::Hard);
    failures += expect(easy.reactionMinSeconds == 0.30F && easy.reactionMaxSeconds == 0.50F &&
      medium.reactionMinSeconds == 0.18F && medium.reactionMaxSeconds == 0.30F &&
      hard.reactionMinSeconds == 0.12F && hard.reactionMaxSeconds == 0.20F,
      "difficulty profiles should retain the documented reaction ranges");
    failures += expect(easy.maxTurnRadiansPerSecond < medium.maxTurnRadiansPerSecond &&
      medium.maxTurnRadiansPerSecond < hard.maxTurnRadiansPerSecond &&
      easy.trackingErrorRadians > medium.trackingErrorRadians &&
      medium.trackingErrorRadians > hard.trackingErrorRadians,
      "difficulty should order finite turn and tracking skill without stat changes");

    lg::BotSenseFrame sense;
    sense.fixedDt = lg::kFixedTickSeconds;
    sense.self.position = {0.0F, 0.0F, 0.9F};
    sense.self.viewYawRadians = 0.0F;
    sense.self.halfHeight = 0.9F;
    sense.combatEnabled = true;
    sense.selectedWeapon = lg::Weapon::LightningGun;
    sense.weapons[lg::weaponIndex(lg::Weapon::LightningGun)] = {true, 18.0F, 0.0F, false};
    sense.visibleEnemies[0] = {1U, {6.0F, 0.0F, 0.9F}, {0.0F, 0.0F, 0.0F}};
    sense.visibleEnemyCount = 1;
    lg::BotBrain first;
    lg::BotBrain second;
    first.reset(0x1234U);
    second.reset(0x1234U);
    const lg::BotMotor visibleFirst = first.tick(sense, hard, {});
    const lg::BotMotor visibleSecond = second.tick(sense, hard, {});
    failures += expect(sameMotorCommand(visibleFirst, visibleSecond) &&
      !visibleFirst.command.attack,
      "fixed bot seeds should be deterministic and reaction should block first fire");
    failures += expect(std::fabs(visibleFirst.command.viewYawRadians) <=
      hard.maxTurnRadiansPerSecond * lg::kFixedTickSeconds + 0.0001F,
      "bot view changes should obey a finite per-tick turn bound");

    sense.visibleEnemyCount = 0;
    const lg::BotMotor hidden = first.tick(sense, hard, {});
    failures += expect(hidden.targetPlayerIndex == 1U &&
      std::fabs(hidden.lastKnownTargetPosition.x - 6.0F) < 0.0001F &&
      hidden.targetMemoryAgeSeconds > 0.0F && !hidden.command.attack,
      "LOS loss should retain only stale remembered state and never fire");

    sense.visibleEnemyCount = 1;
    const lg::BotMotor reacquired = first.tick(sense, hard, {});
    failures += expect(
      reacquired.noFireReason == lg::BotNoFireReason::Reaction && !reacquired.command.attack,
      "reappearing after occlusion should restart the full reaction delay"
    );
  }

  {
    lg::Arena aroundWall = flatArena();
    aroundWall.walls[0].min = {-0.25F, -2.0F, 0.0F};
    aroundWall.walls[0].max = {0.25F, 2.0F, 4.0F};
    aroundWall.wallCount = 1;
    const lg::BotNavigationMap map = lg::buildBotNavigationMap(
      aroundWall, lg::MovementTuning{}, lg::CollisionBounds{}
    );
    const std::size_t left = lg::nearestBotNavNode(map, {-3.0F, 0.0F, 0.9F});
    const std::size_t right = lg::nearestBotNavNode(map, {3.0F, 0.0F, 0.9F});
    failures += expect(map.nodeCount > 0U && map.linkCount > 0U &&
      navPathExists(map, left, right),
      "generated navigation should find a player-sized route around a simple wall");
  }

  {
    lg::Arena multilevel = flatArena();
    multilevel.min = {-6.0F, -6.0F, 0.0F};
    multilevel.max = {6.0F, 6.0F, 8.0F};
    multilevel.spawnCount = 2;
    multilevel.spawnPositions[0] = {-4.0F, -4.0F, 0.0F};
    multilevel.spawnPositions[1] = {4.0F, 4.0F, 0.0F};
    multilevel.walls[0].min = {-2.0F, -2.0F, 2.0F};
    multilevel.walls[0].max = {2.0F, 2.0F, 3.0F};
    multilevel.wallCount = 1;
    const lg::BotNavigationMap map = lg::buildBotNavigationMap(
      multilevel, lg::MovementTuning{}, lg::CollisionBounds{}
    );
    const std::size_t floorNode = lg::nearestBotNavNode(map, {0.0F, 0.0F, 0.9F});
    const std::size_t upperNode = lg::nearestBotNavNode(map, {0.0F, 0.0F, 3.9F});
    failures += expect(
      floorNode < map.nodeCount && upperNode < map.nodeCount && floorNode != upperNode &&
        std::fabs(map.nodes[upperNode].position.z - 3.9F) < 0.15F,
      "nav sampling should retain grounded upper walkable levels and use full 3D node matching"
    );
  }

  {
    lg::Arena special = flatArena();
    special.min = {-6.0F, -6.0F, 0.0F};
    special.max = {6.0F, 6.0F, 8.0F};
    special.spawnCount = 2;
    special.spawnPositions[0] = {-5.0F, -4.0F, 0.0F};
    special.spawnPositions[1] = {5.0F, 4.0F, 0.0F};
    special.teleports[0].min = {-4.5F, -0.5F, 0.0F};
    special.teleports[0].max = {-3.5F, 0.5F, 1.0F};
    special.teleports[0].destination = {4.0F, 0.0F, 0.9F};
    special.teleportCount = 1;
    special.jumpPads[0].min = {-1.0F, -0.6F, 0.0F};
    special.jumpPads[0].max = {1.0F, 0.6F, 0.2F};
    special.jumpPads[0].targetPosition = {0.0F, 4.0F, 0.9F};
    special.jumpPads[0].hasTarget = true;
    special.jumpPadCount = 1;
    const lg::BotNavigationMap map = lg::buildBotNavigationMap(
      special, lg::MovementTuning{}, lg::CollisionBounds{}
    );
    const std::size_t teleportExit = lg::nearestBotNavNode(
      map, special.teleports[0].destination
    );
    failures += expect(
      teleportExit < map.nodeCount &&
        std::fabs(map.nodes[teleportExit].position.z - special.teleports[0].destination.z) <
          0.01F &&
        hasNavLink(map, lg::BotNavLinkKind::Teleport) &&
        hasNavLink(map, lg::BotNavLinkKind::JumpPad),
      "nav should validate real teleport and jump-pad links while preserving exact teleport exits"
    );
  }

  {
    lg::BotNavigationMap longRoute;
    longRoute.nodeCount = 100;
    for (std::size_t index = 0; index < longRoute.nodeCount; ++index) {
      longRoute.nodes[index].position = {static_cast<float>(index), 0.0F, 0.9F};
      if (index > 0) {
        longRoute.links[longRoute.linkCount++] = {
          static_cast<std::uint16_t>(index - 1U), static_cast<std::uint16_t>(index),
          lg::BotNavLinkKind::Walk
        };
      }
    }
    lg::BotSenseFrame sense;
    sense.fixedDt = lg::kFixedTickSeconds;
    sense.self.position = longRoute.nodes[0].position;
    sense.objective = {.position = longRoute.nodes[99].position, .active = true};
    lg::BotBrain brain;
    brain.reset(0x55U);
    const lg::BotMotor motor = brain.tick(
      sense, lg::botDifficultyProfile(lg::BotAttackMode::Medium), longRoute
    );
    failures += expect(
      motor.goal == lg::BotGoalKind::Objective && motor.waypointNode == 1U,
      "path planning should retain a complete route longer than the former 64-node limit"
    );
  }

  {
    lg::Arena aroundHealthWall = flatArena();
    aroundHealthWall.walls[0].min = {-0.25F, -2.0F, 0.0F};
    aroundHealthWall.walls[0].max = {0.25F, 2.0F, 4.0F};
    aroundHealthWall.wallCount = 1;
    const lg::BotNavigationMap map = lg::buildBotNavigationMap(
      aroundHealthWall, lg::MovementTuning{}, lg::CollisionBounds{}
    );
    lg::BotSenseFrame sense;
    sense.fixedDt = lg::kFixedTickSeconds;
    sense.self.position = {-3.0F, 0.0F, 0.9F};
    sense.self.viewYawRadians = 0.0F;
    sense.self.health = 20;
    sense.selectedWeapon = lg::Weapon::LightningGun;
    sense.weapons[lg::weaponIndex(lg::Weapon::LightningGun)] = {true, 18.0F, 0.0F, false};
    sense.weapons[lg::weaponIndex(lg::Weapon::PlasmaGun)] = {true, 8.0F, 100.0F, false};
    sense.visibleEnemies[0] = {1U, {-3.0F, 3.0F, 0.9F}, {0.0F, 0.0F, 0.0F}};
    sense.visibleEnemyCount = 1;
    sense.healthResources[0] = {0U, {3.0F, 0.0F, 0.9F}, 25, true};
    sense.healthResourceCount = 1;
    lg::BotBrain brain;
    brain.reset(0x4567U);
    const lg::BotMotor motor = brain.tick(
      sense, lg::botDifficultyProfile(lg::BotAttackMode::Medium), map
    );
    failures += expect(
      motor.goal == lg::BotGoalKind::RecoverHealth &&
        motor.waypointNode < map.nodeCount &&
        motor.command.weapon == lg::Weapon::PlasmaGun && motor.command.viewYawRadians > 0.0F,
      "a low-health bot should route to a blocked seen pickup while aiming and choosing range from its enemy"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);
    lg::Arena vertical = flatArena();
    vertical.min = {-6.0F, -6.0F, 0.0F};
    vertical.max = {6.0F, 6.0F, 8.0F};
    vertical.healthPickups[0] = {{0.0F, 0.0F, 5.0F}, lg::HealthPickupType::Large};
    vertical.healthPickupCount = 1;
    server.setArena(vertical);
    lg::ScenarioSetup setup;
    setup.match.phase = lg::MatchPhase::Live;
    setup.players[0] = {
      .connected = true, .position = {0.0F, 0.0F, 5.0F}, .health = 100,
      .alive = true, .onGround = false, .ammo = std::nullopt
    };
    setup.players[1] = {
      .bot = true, .ready = true, .position = {-2.0F, 0.0F, 0.9F},
      .viewYawRadians = 0.0F, .health = 100, .alive = true, .onGround = true,
      .ammo = std::nullopt
    };
    std::string error;
    failures += expect(server.applyScenarioSetup(setup, &error),
      "vertical perception fixture should be accepted");
    server.tick(lg::kFixedTickSeconds);
    const std::string debug = server.botDebugString(1);
    failures += expect(
      debug.find("target=none") != std::string::npos &&
        debug.find("resources=0") != std::string::npos,
      "vertical FOV and wall-free hidden-height pickups must not enter filtered bot sense"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);
    lg::Arena arena = flatArena();
    arena.min = {-6.0F, -6.0F, 0.0F};
    arena.max = {6.0F, 6.0F, 6.0F};
    server.setArena(arena);
    lg::ScenarioSetup setup;
    setup.seed = 0xAA55U;
    setup.match.phase = lg::MatchPhase::Live;
    setup.players[0] = {
      .connected = true, .position = {3.0F, 0.0F, 0.9F}, .health = 100,
      .alive = true, .onGround = true, .ammo = std::nullopt
    };
    setup.players[1] = {
      .bot = true, .ready = true, .position = {-3.0F, 0.0F, 0.9F},
      .viewYawRadians = 3.14159265359F, .health = 100, .alive = true, .onGround = true,
      .ammo = std::nullopt
    };
    std::string error;
    failures += expect(server.applyScenarioSetup(setup, &error),
      "out-of-FOV exploration fixture should be accepted");
    server.setBotAttackMode(lg::BotAttackMode::Off);
    const lg::Vec3 start = server.snapshot().players[1].position;
    for (int tick = 0; tick < 90; ++tick) server.tick(lg::kFixedTickSeconds);
    const lg::Vec3 end = server.snapshot().players[1].position;
    failures += expect(
      std::hypot(end.x - start.x, end.y - start.y) > 0.10F &&
        server.botDebugString(1).find("goal=explore") != std::string::npos,
      "an out-of-FOV opponent should make a Duel bot patrol through ordinary commands"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);
    lg::Arena blocked = flatArena();
    blocked.min = {-6.0F, -6.0F, 0.0F};
    blocked.max = {6.0F, 6.0F, 6.0F};
    blocked.walls[0].min = {-0.2F, -6.0F, 0.0F};
    blocked.walls[0].max = {0.2F, 6.0F, 4.0F};
    blocked.wallCount = 1;
    server.setArena(blocked);
    lg::ScenarioSetup setup;
    setup.seed = 0xBEEFU;
    setup.match.phase = lg::MatchPhase::Live;
    setup.players[0] = {
      .connected = true, .position = {3.0F, 0.0F, 0.9F}, .health = 100,
      .alive = true, .onGround = true, .ammo = std::nullopt
    };
    setup.players[1] = {
      .bot = true, .ready = true, .position = {-3.0F, 0.0F, 0.9F},
      .viewYawRadians = 0.0F, .health = 100, .alive = true, .onGround = true,
      .ammo = std::nullopt
    };
    std::string error;
    failures += expect(server.applyScenarioSetup(setup, &error),
      "behind-wall exploration fixture should be accepted");
    server.setBotAttackMode(lg::BotAttackMode::Off);
    const lg::Vec3 start = server.snapshot().players[1].position;
    for (int tick = 0; tick < 90; ++tick) server.tick(lg::kFixedTickSeconds);
    const lg::Vec3 end = server.snapshot().players[1].position;
    failures += expect(
      std::hypot(end.x - start.x, end.y - start.y) > 0.10F &&
        server.botDebugString(1).find("target=none") != std::string::npos,
      "a behind-wall opponent should not leak into sense while the bot continues to explore"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);
    lg::Arena objectiveArena = flatArena();
    objectiveArena.min = {-8.0F, -8.0F, 0.0F};
    objectiveArena.max = {8.0F, 8.0F, 6.0F};
    objectiveArena.mcguffin.hasNeutralSpawn = true;
    objectiveArena.mcguffin.neutralSpawn = {0.0F, 0.0F, 0.9F};
    objectiveArena.mcguffin.hasRedBase = true;
    objectiveArena.mcguffin.redBase = {{-5.0F, -1.0F, 0.0F}, {-3.0F, 1.0F, 2.0F}, lg::Team::Red};
    objectiveArena.mcguffin.hasBlueBase = true;
    objectiveArena.mcguffin.blueBase = {{3.0F, -1.0F, 0.0F}, {5.0F, 1.0F, 2.0F}, lg::Team::Blue};
    server.setArena(objectiveArena);
    lg::McGuffinConfig config;
    config.initialSpawnTicks = 0;
    config.installationDelayTicks = 0;
    server.setMcGuffinConfig(config);
    lg::ScenarioSetup setup;
    setup.seed = 0xCAB1U;
    setup.match.gameMode = lg::GameMode::McGuffin;
    setup.match.phase = lg::MatchPhase::Live;
    setup.players[0] = {
      .bot = true, .ready = true, .team = lg::Team::Red,
      .position = {0.0F, 0.0F, 0.9F}, .viewYawRadians = 3.14159265359F,
      .health = 100, .alive = true, .onGround = true, .ammo = std::nullopt
    };
    setup.players[1] = {
      .bot = true, .ready = true, .team = lg::Team::Blue,
      .position = {7.0F, 7.0F, 0.9F}, .viewYawRadians = 0.0F,
      .health = 100, .alive = true, .onGround = true, .ammo = std::nullopt
    };
    std::string error;
    failures += expect(server.applyScenarioSetup(setup, &error),
      "McGuffin carrier fixture should be accepted");
    server.setBotAttackMode(lg::BotAttackMode::Off);
    for (int tick = 0; tick < 750; ++tick) server.tick(lg::kFixedTickSeconds);
    failures += expect(
      server.snapshot().mcguffin.state == lg::McGuffinState::InstalledRed &&
        server.snapshot().mcguffin.lastEvent == lg::McGuffinEventType::Install,
      "a carrying bot should seek its valid owned base and install through normal movement"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);
    makeOneHumanWarmup(server);
    (void)server.addBots(1);
    const std::uint64_t ingressBefore = server.botCommandIngressCount(1);
    const lg::Vec3 start = server.snapshot().players[1].position;
    for (int tick = 0; tick < 30; ++tick) server.tick(lg::kFixedTickSeconds);
    const lg::ServerSnapshot snapshot = latestSnapshot(transport);
    failures += expect(server.botCommandIngressCount(1) == ingressBefore + 30U &&
      (std::fabs(snapshot.players[1].position.x - start.x) > 0.001F ||
       std::fabs(snapshot.players[1].position.y - start.y) > 0.001F),
      "bot movement should enter through canonical input before normal simulation");
    failures += expect(snapshot.players[0].bounds.radius == snapshot.players[1].bounds.radius &&
      snapshot.players[0].bounds.halfHeight == snapshot.players[1].bounds.halfHeight &&
      snapshot.players[0].health == snapshot.players[1].health,
      "difficulty must not grant health, collision, or movement-stat advantages");

    lg::BalanceConfig ammoLimited;
    ammoLimited.weaponAmmo.infiniteAmmo = false;
    ammoLimited.weaponAmmo.spawnAmmo.fill(0);
    ammoLimited.weaponAmmo.spawnAmmo[lg::weaponIndex(lg::Weapon::MachineGun)] = 10;
    server.applyBalanceConfig(ammoLimited);
    server.setBotWeapon(lg::Weapon::RocketLauncher);
    server.tick(lg::kFixedTickSeconds);
    failures += expect(server.snapshot().selectedWeapons[1] == lg::Weapon::RocketLauncher,
      "forced bot_weapon should still use normal authoritative weapon switching");
    server.setBotWeaponAuto();
    server.tick(lg::kFixedTickSeconds);
    failures += expect(server.botWeaponAuto() &&
      server.snapshot().selectedWeapons[1] == lg::Weapon::MachineGun,
      "bot_weapon auto should return to ammo-aware automatic weapon choice");
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);
    makeOneHumanWarmup(server);
    (void)server.addBots(1);
    failures += expect(
      server.snapshot().botWeapon == lg::Weapon::MachineGun &&
        server.snapshot().selectedWeapons[1] == lg::Weapon::MachineGun,
      "training bots should default to the Machine Gun"
    );

    lg::CommandPacket weapon;
    weapon.playerIndex = 0;
    weapon.command.sequence = 1;
    weapon.botCommand = lg::BotCommandType::Weapon;
    weapon.botCommandValue = static_cast<std::int32_t>(lg::Weapon::RocketLauncher);
    transport.sendCommand(weapon);
    server.tick(lg::kFixedTickSeconds);
    const lg::ServerSnapshot snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.botWeapon == lg::Weapon::RocketLauncher &&
        snapshot.selectedWeapons[1] == lg::Weapon::RocketLauncher,
      "bot_weapon should authoritatively switch all bots"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);
    makeOneHumanWarmup(server);
    (void)server.addBots(1);
    readyHuman(transport, server);
    failures += expect(server.snapshot().matchPhase == lg::MatchPhase::Countdown,
      "one human plus one ready bot should enter countdown after the human readies");
    failures += expect(!server.addBots(1).ok && !server.kickAllBots().ok,
      "bot roster changes should be rejected outside warmup");
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);
    makeOneHumanWarmup(server);
    lg::CommandPacket mode;
    mode.playerIndex = 0;
    mode.command.sequence = 1;
    mode.requestGameMode = true;
    mode.requestedGameMode = lg::GameMode::ClanArena;
    transport.sendCommand(mode);
    server.tick(lg::kFixedTickSeconds);
    (void)server.addBots(3);
    const lg::ServerSnapshot& snapshot = server.snapshot();
    failures += expect(
      snapshot.botPlayers[1] && snapshot.botPlayers[2] && snapshot.botPlayers[3],
      "Clan Arena bot_add should add bots");
    failures += expect(
      lg::isPlayableTeam(snapshot.teams[1]) &&
        lg::isPlayableTeam(snapshot.teams[2]) &&
        snapshot.teams[1] != snapshot.teams[2],
      "Clan Arena bots should receive deterministic balanced playable teams");
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);
    makeOneHumanWarmup(server);
    (void)server.addBots(1);
    server.setBotBehavior(true, true, true, 1, 1, lg::BotAttackMode::Off);
    const lg::Vec3 start = server.snapshot().players[1].position;
    server.tick(lg::kFixedTickSeconds);
    lg::ServerSnapshot snapshot = latestSnapshot(transport);
    failures += expect(std::fabs(snapshot.players[1].position.x - start.x) < 0.001F &&
      std::fabs(snapshot.players[1].position.y - start.y) < 0.001F,
      "bot_standstill should suppress dodge movement input");
    const auto expectedAim = aimFromBotToHuman(snapshot);
    failures += expect(std::fabs(angleDelta(snapshot.players[1].viewYawRadians, expectedAim[0])) < 0.01F &&
      std::fabs(snapshot.players[1].viewPitchRadians - expectedAim[1]) < 0.01F,
      "bot_stare should aim passive bots toward the nearest enemy");
    failures += expect(!snapshot.lightningGuns[1].active,
      "passive bots should not attack");

    server.setBotBehavior(false, false, true, 1, 1, lg::BotAttackMode::Off);
    const lg::Vec3 dodgeStart = server.snapshot().players[1].position;
    for (int tick = 0; tick < 30; ++tick) {
      server.tick(lg::kFixedTickSeconds);
      snapshot = latestSnapshot(transport);
    }
    failures += expect(
      std::fabs(snapshot.players[1].position.x - dodgeStart.x) > 0.001F ||
        std::fabs(snapshot.players[1].position.y - dodgeStart.y) > 0.001F,
      "bot_dodge should produce deterministic strafing when standstill is off");
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);
    makeOneHumanWarmup(server);
    (void)server.addBots(1);
    server.setBotWeapon(lg::Weapon::LightningGun);
    server.setBotBehavior(false, true, false, 250, 750, lg::BotAttackMode::Easy);
    failures += expect(server.botAttackMode() == lg::BotAttackMode::Easy,
      "bot_attack easy should select easy mode");
    server.tick(lg::kFixedTickSeconds);
    lg::ServerSnapshot snapshot = latestSnapshot(transport);
    failures += expect(!snapshot.lightningGuns[1].active,
      "combat bots should not attack before their reaction delay");
    const float initialYaw = snapshot.players[1].viewYawRadians;
    bool turned = false;
    bool fired = false;
    for (int tick = 0; tick < 80; ++tick) {
      server.tick(lg::kFixedTickSeconds);
      snapshot = latestSnapshot(transport);
      turned = turned || std::fabs(angleDelta(initialYaw, snapshot.players[1].viewYawRadians)) > 0.01F;
      fired = fired || snapshot.lightningGuns[1].active;
    }
    failures += expect(snapshot.selectedWeapons[1] == lg::Weapon::LightningGun,
      "combat bots should use the Lightning Gun");
    failures += expect(turned, "combat bots should turn over multiple ticks");
    failures += expect(fired, "combat bots should eventually fire when aim is within tolerance");
    server.setBotAttackMode(lg::BotAttackMode::Off);
    server.tick(lg::kFixedTickSeconds);
    snapshot = latestSnapshot(transport);
    failures += expect(!snapshot.lightningGuns[1].active,
      "bot_attack 0/off should stop firing immediately");
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);
    server.setArena(flatArena(true));
    server.setConnectedPlayers({true, false, false, false, false, false});
    (void)server.addBots(1);
    server.setBotWeapon(lg::Weapon::LightningGun);
    server.setBotBehavior(false, true, false, 250, 750, lg::BotAttackMode::Hard);
    bool fired = false;
    for (int tick = 0; tick < 80; ++tick) {
      server.tick(lg::kFixedTickSeconds);
      const lg::ServerSnapshot snapshot = latestSnapshot(transport);
      fired = fired || snapshot.lightningGuns[1].active;
    }
    failures += expect(!fired, "a wall blocking line of sight should prevent bot attack");
  }

  {
    lg::ServerSnapshot source;
    source.map = {"testmap", 0x12345678U};
    source.botPlayers = {false, true, false, true, false, false};
    lg::WirePacket wire;
    lg::ServerSnapshot decoded;
    failures += expect(lg::encodeServerSnapshot(source, wire) &&
      lg::decodeServerSnapshot(wire, decoded) &&
      decoded.botPlayers == source.botPlayers,
      "snapshot protocol should round-trip bot identity");

    lg::CommandPacket autoWeapon;
    autoWeapon.playerIndex = 0;
    autoWeapon.command.sequence = 1;
    autoWeapon.botCommand = lg::BotCommandType::Weapon;
    autoWeapon.botCommandValue = -1;
    lg::CommandPacket decodedAutoWeapon;
    failures += expect(lg::encodeCommandPacket(autoWeapon, wire) &&
      lg::decodeCommandPacket(wire, decodedAutoWeapon) &&
      decodedAutoWeapon.botCommand == lg::BotCommandType::Weapon &&
      decodedAutoWeapon.botCommandValue == -1,
      "bot_weapon auto should use the existing validated command protocol");
  }

  return failures == 0 ? 0 : 1;
}
