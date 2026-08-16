#include "net/LoopbackTransport.hpp"
#include "net/NetCodec.hpp"
#include "server/ServerGame.hpp"
#include "shared/Constants.hpp"
#include "sim/BalanceConfig.hpp"
#include "sim/Movement.hpp"

#include <array>
#include <chrono>
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

bool hasLongNavRoute(const lg::BotNavigationMap& map, float minimumDistance) {
  std::array<bool, lg::BotNavigationMap::kMaxNodes> visited = {};
  std::array<std::size_t, lg::BotNavigationMap::kMaxNodes> queue = {};
  for (std::size_t start = 0; start < map.nodeCount; ++start) {
    visited.fill(false);
    std::size_t read = 0;
    std::size_t written = 0;
    queue[written++] = start;
    visited[start] = true;
    while (read < written) {
      const std::size_t current = queue[read++];
      if (std::hypot(
            map.nodes[current].position.x - map.nodes[start].position.x,
            map.nodes[current].position.y - map.nodes[start].position.y
          ) >= minimumDistance) {
        return true;
      }
      for (std::size_t index = 0; index < map.linkCount; ++index) {
        const lg::BotNavLink& link = map.links[index];
        if (link.from != current || visited[link.to]) continue;
        visited[link.to] = true;
        queue[written++] = link.to;
      }
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

bool hasDirectedNavLink(
  const lg::BotNavigationMap& map,
  std::size_t from,
  std::size_t to,
  lg::BotNavLinkKind kind
) {
  for (std::size_t index = 0; index < map.linkCount; ++index) {
    const lg::BotNavLink& link = map.links[index];
    if (link.from == from && link.to == to && link.kind == kind) return true;
  }
  return false;
}

std::size_t requiredAnchorNode(
  const lg::BotNavigationMap& map,
  lg::BotNavAnchorKind kind
) {
  const std::size_t count = std::min(map.requiredAnchorCount, map.requiredAnchors.size());
  for (std::size_t index = 0; index < count; ++index) {
    const lg::BotNavRequiredAnchor& anchor = map.requiredAnchors[index];
    if (anchor.kind == kind && anchor.node < map.nodeCount) return anchor.node;
  }
  return lg::BotNavigationMap::kMaxNodes;
}

std::size_t requiredAnchorNode(
  const lg::BotNavigationMap& map,
  lg::BotNavAnchorKind kind,
  std::size_t sourceIndex
) {
  const std::size_t count = std::min(map.requiredAnchorCount, map.requiredAnchors.size());
  for (std::size_t index = 0; index < count; ++index) {
    const lg::BotNavRequiredAnchor& anchor = map.requiredAnchors[index];
    if (anchor.kind == kind && anchor.sourceIndex == sourceIndex &&
        anchor.node < map.nodeCount) {
      return anchor.node;
    }
  }
  return lg::BotNavigationMap::kMaxNodes;
}

bool playerTouchesAnyKillVolume(
  const lg::Arena& arena,
  const lg::PlayerState& player
) {
  for (std::size_t index = 0; index < arena.killVolumeCount; ++index) {
    const lg::ArenaKillVolume& volume = arena.killVolumes[index];
    if (lg::playerTouchesTriggerVolume(player.bounds, player.position,
        volume.min, volume.max)) {
      return true;
    }
  }
  return false;
}

// Replays a generated directed route with ordinary input. It does not set a
// player pose after the initial node: every waypoint, jump, pad, and teleport
// transition goes through the same movement tick used by the server.
bool executeNavRoute(
  const lg::Arena& arena,
  const lg::BotNavigationMap& map,
  std::size_t start,
  std::size_t target
) {
  if (start >= map.nodeCount || target >= map.nodeCount) return false;
  std::array<std::size_t, lg::BotNavigationMap::kMaxNodes> previous = {};
  previous.fill(lg::BotNavigationMap::kMaxNodes);
  std::array<std::size_t, lg::BotNavigationMap::kMaxNodes> queue = {};
  std::size_t read = 0;
  std::size_t written = 0;
  queue[written++] = start;
  previous[start] = start;
  while (read < written && previous[target] == lg::BotNavigationMap::kMaxNodes) {
    const std::size_t current = queue[read++];
    for (std::size_t index = 0; index < map.linkCount; ++index) {
      const lg::BotNavLink& link = map.links[index];
      if (link.from != current || previous[link.to] != lg::BotNavigationMap::kMaxNodes) continue;
      previous[link.to] = current;
      queue[written++] = link.to;
    }
  }
  if (previous[target] == lg::BotNavigationMap::kMaxNodes) return false;
  std::array<std::size_t, lg::BotNavigationMap::kMaxNodes> reverse = {};
  std::size_t routeCount = 0;
  for (std::size_t node = target; node != start; node = previous[node]) {
    reverse[routeCount++] = node;
  }
  lg::PlayerState player;
  player.position = map.nodes[start].position;
  player.bounds = {};
  player.health = 100;
  player.onGround = true;
  player.movementMode = lg::MovementMode::Grounded;
  std::size_t current = start;
  for (std::size_t reverseIndex = routeCount; reverseIndex > 0U; --reverseIndex) {
    const std::size_t next = reverse[reverseIndex - 1U];
    lg::BotNavLinkKind kind = lg::BotNavLinkKind::Walk;
    bool found = false;
    for (std::size_t index = 0; index < map.linkCount; ++index) {
      if (map.links[index].from == current && map.links[index].to == next) {
        kind = map.links[index].kind;
        found = true;
        break;
      }
    }
    if (!found) return false;
    bool reached = false;
    for (std::size_t tick = 0; tick < 512U; ++tick) {
      const lg::Vec3 delta = map.nodes[next].position - player.position;
      lg::UserCommand command;
      command.viewYawRadians = std::atan2(delta.y, delta.x);
      command.forwardMove = 1.0F;
      command.jump = kind == lg::BotNavLinkKind::Jump;
      lg::simulateMovement(player, command, arena, lg::MovementTuning{}, lg::kFixedTickSeconds);
      if (playerTouchesAnyKillVolume(arena, player)) return false;
      const lg::Vec3 remaining = map.nodes[next].position - player.position;
      if (std::sqrt(remaining.x * remaining.x + remaining.y * remaining.y +
          remaining.z * remaining.z) <= 0.55F) {
        reached = true;
        break;
      }
    }
    if (!reached) return false;
    current = next;
  }
  return true;
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
    failures += expect(easy.targetFovDegrees == 108.0F &&
      easy.targetFovDegrees == medium.targetFovDegrees &&
      medium.targetFovDegrees == hard.targetFovDegrees,
      "the same yaw-and-pitch sensory cone should apply at every difficulty");
    failures += expect(easy.fireToleranceRadians > medium.fireToleranceRadians &&
      medium.fireToleranceRadians > hard.fireToleranceRadians,
      "harder aim should not get a looser fire alignment threshold");

    lg::BotSenseFrame sense;
    sense.fixedDt = lg::kFixedTickSeconds;
    sense.self.position = {0.0F, 0.0F, 0.9F};
    sense.self.viewYawRadians = 0.0F;
    sense.self.halfHeight = 0.9F;
    sense.combatEnabled = true;
    sense.selectedWeapon = lg::Weapon::LightningGun;
    sense.weapons[lg::weaponIndex(lg::Weapon::LightningGun)] =
      {true, true, 18.0F, 6.0F, 0.05F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F};
    sense.visibleEnemies[0] = {1U, {6.0F, 0.0F, 0.9F}, 1U, true, false};
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

    lg::BotBrain gated;
    gated.reset(0x789AU);
    const int protectedTicks = static_cast<int>(std::ceil(
      hard.reactionMinSeconds / lg::kFixedTickSeconds
    ));
    bool reactionHeldViewAndMovement = true;
    for (int tick = 0; tick < protectedTicks; ++tick) {
      const lg::BotMotor motor = gated.tick(sense, hard, {});
      reactionHeldViewAndMovement = reactionHeldViewAndMovement &&
        motor.noFireReason == lg::BotNoFireReason::Reaction &&
        motor.command.viewYawRadians == sense.self.viewYawRadians &&
        motor.command.viewPitchRadians == sense.self.viewPitchRadians &&
        motor.command.forwardMove == 0.0F && motor.command.rightMove == 0.0F;
    }
    failures += expect(reactionHeldViewAndMovement,
      "reaction should gate target-driven aim and chase movement for the full sampled delay");
  }

  {
    lg::BotDifficultyProfile profile =
      lg::botDifficultyProfile(lg::BotAttackMode::Hard);
    profile.reactionMinSeconds = 0.05F;
    profile.reactionMaxSeconds = 0.05F;
    profile.maxTurnRadiansPerSecond = 100.0F;
    profile.turnAccelerationRadiansPerSecond2 = 10000.0F;
    profile.trackingErrorRadians = 0.0F;
    profile.fireToleranceRadians = 0.20F;
    profile.predictionSeconds = 0.0F;

    lg::BotSenseFrame sense;
    sense.fixedDt = 0.10F;
    sense.serverTick = 1U;
    sense.self.position = {0.0F, 0.0F, 0.9F};
    sense.self.viewYawRadians = 0.0F;
    sense.self.halfHeight = 0.9F;
    sense.combatEnabled = true;
    sense.selectedWeapon = lg::Weapon::LightningGun;
    sense.weapons[lg::weaponIndex(lg::Weapon::LightningGun)] =
      {true, true, 18.0F, 6.0F, 0.05F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F};
    sense.visibleEnemies[0] = {1U, {6.0F, 0.0F, 0.9F}, 1U, true, false};
    sense.visibleEnemies[1] = {2U, {6.4F, 0.0F, 0.9F}, 1U, true, false};
    sense.visibleEnemyCount = 2U;

    lg::BotBrain brain;
    brain.reset(0x57AB1E5U);
    const lg::BotMotor acquired = brain.tick(sense, profile, {});
    ++sense.serverTick;
    sense.visibleEnemies[0].observationServerTick = sense.serverTick;
    sense.visibleEnemies[1].observationServerTick = sense.serverTick;
    const lg::BotMotor reacted = brain.tick(sense, profile, {});
    failures += expect(acquired.targetPlayerIndex == 1U &&
      reacted.targetPlayerIndex == 1U &&
      reacted.noFireReason != lg::BotNoFireReason::Reaction,
      "a stable visible target should finish its sampled reaction delay");

    ++sense.serverTick;
    sense.visibleEnemies[0] = {1U, {6.2F, 0.0F, 0.9F},
      sense.serverTick, true, false};
    sense.visibleEnemies[1] = {2U, {5.9F, 0.0F, 0.9F},
      sense.serverTick, true, false};
    const lg::BotMotor retained = brain.tick(sense, profile, {});
    failures += expect(retained.targetPlayerIndex == 1U &&
      retained.noFireReason != lg::BotNoFireReason::Reaction,
      "a marginally closer visible enemy should not restart acquisition");

    ++sense.serverTick;
    sense.visibleEnemies[0] = {1U, {7.0F, 0.0F, 0.9F},
      sense.serverTick, true, false};
    sense.visibleEnemies[1] = {2U, {5.0F, 0.0F, 0.9F},
      sense.serverTick, true, false};
    const lg::BotMotor switched = brain.tick(sense, profile, {});
    failures += expect(switched.targetPlayerIndex == 2U &&
      switched.noFireReason == lg::BotNoFireReason::Reaction,
      "a clearly closer visible enemy should still trigger a deliberate switch");
  }

  {
    // BotObservedEnemy has no velocity. This uses only position/tick samples
    // and proves lead appears only after a later visible observation.
    lg::BotDifficultyProfile instant = lg::botDifficultyProfile(lg::BotAttackMode::Hard);
    instant.reactionMinSeconds = -0.10F;
    instant.reactionMaxSeconds = -0.10F;
    instant.maxTurnRadiansPerSecond = 100.0F;
    instant.turnAccelerationRadiansPerSecond2 = 10000.0F;
    instant.trackingErrorRadians = 0.0F;
    instant.predictionSeconds = 0.20F;
    lg::BotSenseFrame firstSense;
    firstSense.fixedDt = lg::kFixedTickSeconds;
    firstSense.serverTick = 10U;
    firstSense.self.position = {0.0F, 0.0F, 0.9F};
    firstSense.self.halfHeight = 0.9F;
    firstSense.combatEnabled = true;
    firstSense.selectedWeapon = lg::Weapon::LightningGun;
    firstSense.weapons[lg::weaponIndex(lg::Weapon::LightningGun)] =
      {true, true, 18.0F, 6.0F, 0.05F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F};
    firstSense.visibleEnemies[0] = {1U, {6.0F, 0.0F, 0.9F}, 10U, true, false};
    firstSense.visibleEnemyCount = 1U;
    lg::BotBrain first;
    lg::BotBrain second;
    first.reset(0xA1U);
    second.reset(0xA1U);
    const lg::BotMotor firstSeen = first.tick(firstSense, instant, {});
    (void)second.tick(firstSense, instant, {});
    lg::BotSenseFrame hiddenSense = firstSense;
    hiddenSense.serverTick = 11U;
    hiddenSense.visibleEnemyCount = 0U;
    hiddenSense.perceptionFresh = false;
    const lg::BotMotor hiddenFirst = first.tick(hiddenSense, instant, {});
    const lg::BotMotor hiddenSecond = second.tick(hiddenSense, instant, {});
    failures += expect(sameMotorCommand(hiddenFirst, hiddenSecond) && !hiddenFirst.command.attack,
      "hidden motion cannot change a bot lead estimate before a new visible sample");
    lg::BotSenseFrame secondSense = firstSense;
    secondSense.serverTick = 20U;
    secondSense.visibleEnemies[0] = {1U, {6.0F, 2.0F, 0.9F}, 20U, true, false};
    const lg::BotMotor secondSeen = first.tick(secondSense, instant, {});
    failures += expect(std::fabs(secondSeen.command.viewYawRadians - firstSeen.command.viewYawRadians) >
        0.05F,
      "a second visible position sample should produce bounded observation-derived lead");
  }

  {
    // Full perception runs below 125 Hz, but a beam must keep using ordinary
    // held input while the server still confirms the known target in LOS/FOV.
    lg::BotDifficultyProfile instant = lg::botDifficultyProfile(lg::BotAttackMode::Hard);
    instant.reactionMinSeconds = -0.10F;
    instant.reactionMaxSeconds = -0.10F;
    instant.maxTurnRadiansPerSecond = 100.0F;
    instant.turnAccelerationRadiansPerSecond2 = 10000.0F;
    instant.trackingErrorRadians = 0.0F;
    instant.fireToleranceRadians = 0.20F;
    lg::BotSenseFrame sense;
    sense.fixedDt = lg::kFixedTickSeconds;
    sense.serverTick = 1U;
    sense.self.position = {0.0F, 0.0F, 0.9F};
    sense.self.viewYawRadians = 0.0F;
    sense.self.halfHeight = 0.9F;
    sense.combatEnabled = true;
    sense.selectedWeapon = lg::Weapon::LightningGun;
    sense.weapons[lg::weaponIndex(lg::Weapon::LightningGun)] =
      {true, true, 18.0F, 6.0F, 0.05F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F};
    sense.visibleEnemies[0] = {1U, {6.0F, 0.0F, 0.9F}, 1U, true, false};
    sense.visibleEnemyCount = 1U;
    lg::BotBrain brain;
    brain.reset(0xB34DU);
    bool heldEveryCachedTick = brain.tick(sense, instant, {}).command.attack;
    sense.perceptionFresh = false;
    sense.attackTargetPlayerIndex = 1U;
    sense.attackTargetCurrentlyVisible = true;
    for (int tick = 0; tick < 7; ++tick) {
      ++sense.serverTick;
      heldEveryCachedTick = heldEveryCachedTick && brain.tick(sense, instant, {}).command.attack;
    }
    sense.attackTargetCurrentlyVisible = false;
    const lg::BotMotor hidden = brain.tick(sense, instant, {});
    failures += expect(heldEveryCachedTick && !hidden.command.attack &&
      hidden.noFireReason == lg::BotNoFireReason::NoVisibleTarget,
      "a current LOS/FOV latch should hold beam input between perceptions and stop next tick on loss");
  }

  {
    lg::BotWeaponSense hitscan = {true, true, 24.0F, 80.0F, 0.40F,
      0.0F, 0.0F, 0.0F, 0.0F, 0.0F};
    lg::BotWeaponSense rocket = {true, true, 20.0F, 100.0F, 0.80F,
      22.5F, 3.0F, 100.0F, 0.0F, 0.16F};
    lg::BotCombatContext farMoving;
    farMoving.targetDistance = 18.0F;
    farMoving.targetLateralSpeed = 18.0F;
    farMoving.selfHealth = 100;
    const lg::BotWeaponScore farRail = lg::scoreBotWeapon(hitscan, farMoving, 12.0F, true);
    const lg::BotWeaponScore farRocket = lg::scoreBotWeapon(rocket, farMoving, 12.0F, false);
    failures += expect(farRail.total > farRocket.total &&
      farRocket.projectileDifficulty > 0.0F,
      "utility should penalize a distant moving projectile shot");
    lg::BotCombatContext closeSplash;
    closeSplash.targetDistance = 1.0F;
    closeSplash.selfHealth = 25;
    closeSplash.nearbySplashSurface = true;
    const lg::BotWeaponScore closeRocket = lg::scoreBotWeapon(rocket, closeSplash, 8.0F, false);
    failures += expect(closeRocket.selfRisk > 0.70F,
      "close splash self-risk should sharply reduce utility at low health");
  }

  {
    // Cooldown is readiness for both current and alternate weapons. Pullout
    // cost applies only to the alternate, then hysteresis prevents small
    // score changes from weapon churn.
    lg::BotWeaponSense rail = {true, true, 30.0F, 100.0F, 1.0F,
      0.0F, 0.0F, 0.0F, 1.0F, 0.0F};
    lg::BotWeaponSense machine = {true, true, 18.0F, 10.0F, 0.10F,
      0.0F, 0.0F, 0.0F, 0.0F, 0.16F};
    lg::BotCombatContext context;
    context.targetDistance = 6.0F;
    context.selfHealth = 100;
    context.targetGrounded = true;
    const lg::BotWeaponScore coolingRail = lg::scoreBotWeapon(rail, context, 9.0F, true);
    const lg::BotWeaponScore readyMachine = lg::scoreBotWeapon(machine, context, 9.0F, false);
    failures += expect(coolingRail.cooldownPenalty > 0.90F &&
      coolingRail.switchCost == 0.0F && readyMachine.switchCost > 0.0F &&
      readyMachine.total > coolingRail.total + 0.12F,
      "a cooling current rail should yield to a ready alternative after its pullout cost");

    lg::BotDifficultyProfile instant = lg::botDifficultyProfile(lg::BotAttackMode::Hard);
    instant.reactionMinSeconds = -0.10F;
    instant.reactionMaxSeconds = -0.10F;
    instant.maxTurnRadiansPerSecond = 100.0F;
    instant.turnAccelerationRadiansPerSecond2 = 10000.0F;
    instant.trackingErrorRadians = 0.0F;
    lg::BotSenseFrame sense;
    sense.fixedDt = lg::kFixedTickSeconds;
    sense.self.position = {0.0F, 0.0F, 0.9F};
    sense.self.halfHeight = 0.9F;
    sense.combatEnabled = true;
    sense.selectedWeapon = lg::Weapon::Railgun;
    sense.weapons[lg::weaponIndex(lg::Weapon::Railgun)] = rail;
    sense.weapons[lg::weaponIndex(lg::Weapon::MachineGun)] = machine;
    sense.visibleEnemies[0] = {1U, {6.0F, 0.0F, 0.9F}, 1U, true, false};
    sense.visibleEnemyCount = 1U;
    lg::BotBrain brain;
    brain.reset(0xC001U);
    const lg::BotMotor switchMotor = brain.tick(sense, instant, {});
    rail.cooldownSeconds = 0.25F;
    sense.weapons[lg::weaponIndex(lg::Weapon::Railgun)] = rail;
    lg::BotBrain steadyBrain;
    steadyBrain.reset(0xC001U);
    const lg::BotMotor steadyMotor = steadyBrain.tick(sense, instant, {});
    failures += expect(switchMotor.command.weapon == lg::Weapon::MachineGun &&
      steadyMotor.command.weapon == lg::Weapon::Railgun,
      "cooldown utility should switch for a clear gain and retain the current rail inside hysteresis");
  }

  {
    lg::BotBrain first;
    lg::BotBrain same;
    lg::BotBrain other;
    first.reset(0xB07U);
    same.reset(0xB07U);
    other.reset(0xB08U);
    const lg::BotTraits firstTraits = first.traits();
    failures += expect(firstTraits.aggression == same.traits().aggression &&
      firstTraits.aimBiasScale == same.traits().aimBiasScale &&
      firstTraits.aggression >= 0.90F && firstTraits.aggression <= 1.10F &&
      firstTraits.reactionLatencyOffsetSeconds >= -0.025F &&
      firstTraits.reactionLatencyOffsetSeconds <= 0.025F &&
      (firstTraits.aggression != other.traits().aggression ||
       firstTraits.preferredRangeBias != other.traits().preferredRangeBias),
      "slot-seeded bot traits should persist deterministically within bounded offsets");
  }

  {
    lg::BotNavigationMap recoveryRoute;
    recoveryRoute.nodeCount = 2;
    recoveryRoute.nodes[0].position = {0.0F, 0.0F, 0.9F};
    recoveryRoute.nodes[1].position = {4.0F, 0.0F, 0.9F};
    recoveryRoute.links[0] = {0U, 1U, lg::BotNavLinkKind::Walk};
    recoveryRoute.linkCount = 1;
    lg::prepareBotNavigationMap(recoveryRoute);
    lg::BotSenseFrame sense;
    sense.fixedDt = lg::kFixedTickSeconds;
    sense.self.position = recoveryRoute.nodes[0].position;
    sense.self.onGround = true;
    sense.objective = {.position = recoveryRoute.nodes[1].position, .active = true};
    lg::BotBrain brain;
    brain.reset(0x57U);
    const lg::BotDifficultyProfile profile =
      lg::botDifficultyProfile(lg::BotAttackMode::Medium);
    bool recoveryPulse = false;
    for (int tick = 0; tick < 90; ++tick) {
      const lg::BotMotor motor = brain.tick(sense, profile, recoveryRoute);
      if (motor.recoveredFromStuck) {
        recoveryPulse = true;
        break;
      }
    }
    sense.self.position.x = 0.50F;
    const lg::BotMotor resumed = brain.tick(sense, profile, recoveryRoute);
    failures += expect(
      recoveryPulse && !resumed.recoveredFromStuck &&
        (std::fabs(resumed.command.forwardMove) > 0.01F ||
         std::fabs(resumed.command.rightMove) > 0.01F),
      "stuck recovery should stop for one tick, replan, and resume graph movement"
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
      navPathExists(map, left, right) && executeNavRoute(aroundWall, map, left, right),
      "generated navigation should execute a player-command route around a simple wall");
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
    lg::Arena tall = flatArena();
    tall.min = {-6.0F, -6.0F, 0.0F};
    tall.max = {6.0F, 6.0F, 24.0F};
    tall.spawnCount = 2;
    tall.spawnPositions[0] = {-5.0F, -5.0F, 0.0F};
    tall.spawnPositions[1] = {5.0F, 5.0F, 0.0F};
    for (std::size_t index = 0; index < 10U; ++index) {
      const float bottom = 2.2F * static_cast<float>(index + 1U);
      tall.walls[index].min = {-1.0F, -1.0F, bottom};
      tall.walls[index].max = {1.0F, 1.0F, bottom + 0.20F};
    }
    tall.wallCount = 10;
    const lg::BotNavigationMap map = lg::buildBotNavigationMap(
      tall, lg::MovementTuning{}, lg::CollisionBounds{}
    );
    std::array<float, 16> levels = {};
    std::size_t levelCount = 0;
    for (std::size_t node = 0; node < map.nodeCount; ++node) {
      const float level = map.nodes[node].position.z;
      bool known = false;
      for (std::size_t seen = 0; seen < levelCount; ++seen) {
        known = known || std::fabs(levels[seen] - level) < 0.10F;
      }
      if (!known && levelCount < levels.size()) levels[levelCount++] = level;
    }
    failures += expect(levelCount > 8U,
      "nav should retain more than eight collision-checked walkable levels within its node budget");
  }

  {
    lg::Arena large = flatArena();
    large.min = {-50.0F, -50.0F, 0.0F};
    large.max = {50.0F, 50.0F, 6.0F};
    large.spawnCount = 2;
    large.spawnPositions[0] = {-45.0F, 0.0F, 0.0F};
    large.spawnPositions[1] = {45.0F, 0.0F, 0.0F};
    const lg::BotNavigationMap map = lg::buildBotNavigationMap(
      large, lg::MovementTuning{}, lg::CollisionBounds{}
    );
    const lg::BotNavigationMap repeated = lg::buildBotNavigationMap(
      large, lg::MovementTuning{}, lg::CollisionBounds{}
    );
    const std::size_t left = lg::nearestBotNavNode(map, {-45.0F, 0.0F, 0.9F});
    const std::size_t right = lg::nearestBotNavNode(map, {45.0F, 0.0F, 0.9F});
    failures += expect(
      map.nodeCount > 100U && map.nodeCount < lg::BotNavigationMap::kMaxNodes &&
        !map.nodeCapacityRejects && !map.linkCapacityRejects &&
        !map.regionWorkExhausted && !map.regionTaskCapacityReached &&
        navPathExists(map, left, right) &&
        map.nodeCount == repeated.nodeCount && map.linkCount == repeated.linkCount &&
        map.regionExpansionWork == repeated.regionExpansionWork,
      "large-map sampling should retain a deterministic route without exhausting fixed bounds"
    );
  }

  {
    lg::Arena vertical = flatArena();
    vertical.spawnCount = 2;
    vertical.spawnPositions[0] = {-5.0F, 0.0F, 0.0F};
    vertical.spawnPositions[1] = {-4.0F, 3.0F, 0.0F};
    vertical.walls[0].min = {-1.0F, -4.0F, 0.0F};
    vertical.walls[0].max = {5.0F, 4.0F, 0.40F};
    vertical.wallCount = 1;
    vertical.healthPickups[0].position = {2.0F, 0.0F, 0.40F};
    vertical.healthPickupCount = 1;
    const lg::BotNavigationMap map = lg::buildBotNavigationMap(
      vertical, lg::MovementTuning{}, lg::CollisionBounds{}
    );
    const std::size_t spawn = requiredAnchorNode(map, lg::BotNavAnchorKind::Spawn);
    const std::size_t health = requiredAnchorNode(map, lg::BotNavAnchorKind::Health);
    failures += expect(
      navPathExists(map, spawn, health) && executeNavRoute(vertical, map, spawn, health),
      "generated navigation should execute a normal-command route up a vertical step to health"
    );
  }

  {
    lg::Arena lethal = flatArena();
    lethal.min = {-6.0F, -6.0F, 0.0F};
    lethal.max = {6.0F, 6.0F, 6.0F};
    lethal.spawnCount = 2;
    lethal.spawnPositions[0] = {-3.0F, 0.0F, 0.0F};
    lethal.spawnPositions[1] = {3.0F, 0.0F, 0.0F};
    lethal.killVolumes[0] = {{-1.0F, -6.0F, -1.0F}, {1.0F, 6.0F, 8.0F}};
    lethal.killVolumeCount = 1;
    const lg::BotNavigationMap map = lg::buildBotNavigationMap(
      lethal, lg::MovementTuning{}, lg::CollisionBounds{}
    );
    const std::size_t left = requiredAnchorNode(map, lg::BotNavAnchorKind::Spawn, 0U);
    const std::size_t right = requiredAnchorNode(map, lg::BotNavAnchorKind::Spawn, 1U);
    failures += expect(
      map.requiredAnchorsComplete && left < map.nodeCount && right < map.nodeCount &&
        !navPathExists(map, left, right) && !executeNavRoute(lethal, map, left, right),
      "lethal kill-volume routes should stay out of the movement graph"
    );
  }

  {
    lg::Arena safeDrop = flatArena();
    safeDrop.min = {-4.0F, -2.0F, 0.0F};
    safeDrop.max = {4.0F, 2.0F, 6.0F};
    safeDrop.spawnCount = 2;
    safeDrop.spawnPositions[0] = {0.69F, 0.0F, 3.0F};
    safeDrop.spawnPositions[1] = {1.94F, 0.0F, 0.0F};
    safeDrop.walls[0].min = {-3.0F, -2.0F, 0.0F};
    safeDrop.walls[0].max = {0.35F, 2.0F, 3.0F};
    safeDrop.wallCount = 1;
    // The straight chord cuts through this volume. A real drop leaves the
    // upper surface first, stays above it, and reaches the lower surface.
    safeDrop.killVolumes[0] = {{1.0F, -2.0F, 1.2F}, {1.3F, 2.0F, 2.0F}};
    safeDrop.killVolumeCount = 1;
    const lg::BotNavigationMap map = lg::buildBotNavigationMap(
      safeDrop, lg::MovementTuning{}, lg::CollisionBounds{}
    );
    const std::size_t upper = requiredAnchorNode(map, lg::BotNavAnchorKind::Spawn, 0U);
    const std::size_t lower = requiredAnchorNode(map, lg::BotNavAnchorKind::Spawn, 1U);
    bool hasSafeDropLink = false;
    if (upper < map.nodeCount) {
      for (std::size_t index = 0; index < map.linkCount; ++index) {
        const lg::BotNavLink& link = map.links[index];
        if (link.from != upper || link.to >= map.nodeCount ||
            (link.kind != lg::BotNavLinkKind::Step &&
             link.kind != lg::BotNavLinkKind::Jump)) {
          continue;
        }
        hasSafeDropLink = map.nodes[link.to].position.z <
          map.nodes[upper].position.z - 0.50F;
        if (hasSafeDropLink) break;
      }
    }
    failures += expect(
      upper < map.nodeCount && lower < map.nodeCount && hasSafeDropLink &&
        navPathExists(map, upper, lower) && executeNavRoute(safeDrop, map, upper, lower),
      "a simulated safe jump or drop should clear a kill volume crossed by its straight chord"
    );
  }

  {
    const lg::Arena realArena = lg::makeDefaultServerArena();
    const lg::BotNavigationMap map = lg::buildBotNavigationMap(
      realArena, lg::MovementTuning{}, lg::CollisionBounds{}
    );
    failures += expect(
      map.nodeCount > 0U && hasLongNavRoute(map, 4.0F),
      "the packaged default arena should retain a generated player-valid route"
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
        hasNavLink(map, lg::BotNavLinkKind::JumpPad) &&
        map.teleportRoutes[0].verified && map.jumpPadRoutes[0].verified &&
        hasDirectedNavLink(map, map.teleportRoutes[0].entryNode,
          map.teleportRoutes[0].exitNode, lg::BotNavLinkKind::Teleport) &&
        !hasDirectedNavLink(map, map.teleportRoutes[0].exitNode,
          map.teleportRoutes[0].entryNode, lg::BotNavLinkKind::Teleport) &&
        hasDirectedNavLink(map, map.jumpPadRoutes[0].entryNode,
          map.jumpPadRoutes[0].exitNode, lg::BotNavLinkKind::JumpPad) &&
        !hasDirectedNavLink(map, map.jumpPadRoutes[0].exitNode,
          map.jumpPadRoutes[0].entryNode, lg::BotNavLinkKind::JumpPad) &&
        executeNavRoute(special, map, map.teleportRoutes[0].entryNode,
          map.teleportRoutes[0].exitNode) &&
        executeNavRoute(special, map, map.jumpPadRoutes[0].entryNode,
          map.jumpPadRoutes[0].exitNode),
      "nav should execute real teleport and jump-pad routes while preserving exact teleport exits"
    );
  }

  {
    lg::Arena partlyBlockedTrigger = flatArena();
    partlyBlockedTrigger.spawnCount = 2;
    partlyBlockedTrigger.teleports[0].min = {-1.0F, -0.20F, 0.0F};
    partlyBlockedTrigger.teleports[0].max = {1.0F, 0.20F, 0.20F};
    partlyBlockedTrigger.teleports[0].destination = {5.0F, 0.0F, 0.9F};
    partlyBlockedTrigger.teleportCount = 1;
    // The trigger center is solid, but either end still has a legal player
    // center that overlaps the trigger. This catches center-only entry search.
    partlyBlockedTrigger.walls[0].min = {-0.60F, -0.35F, 0.0F};
    partlyBlockedTrigger.walls[0].max = {0.60F, 0.35F, 4.0F};
    partlyBlockedTrigger.wallCount = 1;
    const lg::BotNavigationMap map = lg::buildBotNavigationMap(
      partlyBlockedTrigger, lg::MovementTuning{}, lg::CollisionBounds{}
    );
    failures += expect(
      map.teleportRoutes[0].verified &&
        executeNavRoute(partlyBlockedTrigger, map, map.teleportRoutes[0].entryNode,
          map.teleportRoutes[0].exitNode),
      "trigger-entry search should use a valid overlapping side position when its center is blocked"
    );
  }

  {
    lg::ArenaHealthPickup pickup;
    pickup.position = {0.0F, 0.0F, 0.0F};
    const lg::CollisionBounds bounds = {};
    const float touchRadius = bounds.radius + lg::kHealthPickupTouchRadius;
    const float touchHalfHeight = bounds.halfHeight + lg::kHealthPickupTouchHalfHeight;
    failures += expect(
      lg::playerTouchesHealthPickup(bounds, {touchRadius, 0.0F, 0.0F}, pickup) &&
        lg::playerTouchesHealthPickup(bounds, {0.0F, 0.0F, touchHalfHeight}, pickup) &&
        !lg::playerTouchesHealthPickup(bounds, {touchRadius + 0.001F, 0.0F, 0.0F}, pickup) &&
        !lg::playerTouchesHealthPickup(bounds, {0.0F, 0.0F, touchHalfHeight + 0.001F}, pickup),
      "health touch checks should include the exact server boundary and reject points outside it"
    );
  }

  {
    lg::Arena healthAnchor = flatArena();
    healthAnchor.healthPickups[0].position = {0.137F, -0.083F, 0.0F};
    healthAnchor.healthPickupCount = 1;
    const lg::BotNavigationMap map = lg::buildBotNavigationMap(
      healthAnchor, lg::MovementTuning{}, lg::CollisionBounds{}
    );
    const std::size_t healthNode = map.healthAnchorNodes[0];
    failures += expect(
      healthNode < map.nodeCount &&
        lg::playerTouchesHealthPickup(
          lg::CollisionBounds{}, map.nodes[healthNode].position,
          healthAnchor.healthPickups[0]
        ),
      "a health resource anchor must remain at its proven touch center after node insertion"
    );
  }

  {
    lg::Arena occludedHealth = flatArena();
    occludedHealth.min = {-5.0F, -5.0F, -4.0F};
    occludedHealth.max = {5.0F, 5.0F, 4.0F};
    occludedHealth.healthPickups[0].position = {0.0F, 0.0F, 0.0F};
    occludedHealth.healthPickupCount = 1;
    // This one physical wall contains the full legal center volume for the
    // pickup. It is an authored-data failure, not a missing nav sample.
    occludedHealth.walls[0].min = {-3.0F, -3.0F, -4.0F};
    occludedHealth.walls[0].max = {3.0F, 3.0F, 4.0F};
    occludedHealth.wallCount = 1;
    const lg::BotNavigationMap map = lg::buildBotNavigationMap(
      occludedHealth, lg::MovementTuning{}, lg::CollisionBounds{}
    );
    failures += expect(
      !map.requiredAnchorsComplete && map.healthTouchVolumeOccluded[0] &&
        map.healthTouchVolumeProofs[0] > 0U,
      "health validation should diagnose a pickup whose whole touch volume is physically occluded"
    );
  }

  {
    lg::Arena mcgRoute = flatArena();
    mcgRoute.spawnCount = 2;
    mcgRoute.spawnPositions[0] = {-6.0F, -4.0F, 0.0F};
    mcgRoute.spawnPositions[1] = {6.0F, 4.0F, 0.0F};
    mcgRoute.mcguffin.hasNeutralSpawn = true;
    mcgRoute.mcguffin.neutralSpawn = {0.0F, 0.0F, 0.0F};
    mcgRoute.mcguffin.hasRedBase = true;
    mcgRoute.mcguffin.redBase = {{-8.0F, 5.0F, 0.0F}, {-6.0F, 7.0F, 1.0F}, lg::Team::Red};
    mcgRoute.mcguffin.hasBlueBase = true;
    mcgRoute.mcguffin.blueBase = {{6.0F, -7.0F, 0.0F}, {8.0F, -5.0F, 1.0F}, lg::Team::Blue};
    const lg::BotNavigationMap map = lg::buildBotNavigationMap(
      mcgRoute, lg::MovementTuning{}, lg::CollisionBounds{}
    );
    const std::size_t spawn = requiredAnchorNode(map, lg::BotNavAnchorKind::Spawn);
    const std::size_t objective = requiredAnchorNode(map, lg::BotNavAnchorKind::NeutralObjective);
    const std::size_t redBase = requiredAnchorNode(map, lg::BotNavAnchorKind::RedBase);
    const std::size_t blueBase = requiredAnchorNode(map, lg::BotNavAnchorKind::BlueBase);
    failures += expect(
      executeNavRoute(mcgRoute, map, spawn, objective) &&
        executeNavRoute(mcgRoute, map, objective, redBase) &&
        executeNavRoute(mcgRoute, map, objective, blueBase),
      "generated navigation should execute normal-command McGuffin objective and base routes"
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
    lg::BotNavigationMap largeRoute;
    largeRoute.nodeCount = lg::BotNavigationMap::kMaxNodes;
    for (std::size_t index = 0; index < largeRoute.nodeCount; ++index) {
      largeRoute.nodes[index].position = {static_cast<float>(index), 0.0F, 0.9F};
      if (index > 0U) {
        largeRoute.links[largeRoute.linkCount++] = {
          static_cast<std::uint16_t>(index - 1U), static_cast<std::uint16_t>(index),
          lg::BotNavLinkKind::Walk
        };
      }
    }
    lg::prepareBotNavigationMap(largeRoute);
    lg::BotSenseFrame sense;
    sense.fixedDt = lg::kFixedTickSeconds;
    sense.self.position = largeRoute.nodes[0].position;
    sense.objective = {.position = largeRoute.nodes[largeRoute.nodeCount - 1U].position,
      .active = true};
    const auto start = std::chrono::steady_clock::now();
    bool everyPlanStarted = true;
    constexpr std::size_t kReplanBenchmarkIterations = 128U;
    for (std::size_t repeat = 0; repeat < kReplanBenchmarkIterations; ++repeat) {
      lg::BotBrain brain;
      brain.reset(0x600DU + static_cast<std::uint32_t>(repeat));
      const lg::BotMotor motor = brain.tick(
        sense, lg::botDifficultyProfile(lg::BotAttackMode::Medium), largeRoute
      );
      everyPlanStarted = everyPlanStarted && motor.goal == lg::BotGoalKind::Objective &&
        motor.waypointNode < largeRoute.nodeCount &&
        largeRoute.nodes[motor.waypointNode].position.x > 0.0F &&
        largeRoute.nodes[motor.waypointNode].position.x < 3.0F;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - start
    ).count();
    std::cout << "bot nav benchmark: " << kReplanBenchmarkIterations << " max-graph replans="
      << elapsed << "us\n";
    failures += expect(
      largeRoute.outgoingLinksPrepared && everyPlanStarted && elapsed < 250000,
      "indexed fixed-heap planning should replan a max-size graph within a bot tick budget"
    );
  }

  {
    lg::BotNavigationMap healthRoute;
    healthRoute.nodeCount = 3;
    healthRoute.nodes[0].position = {0.0F, 0.0F, 0.9F};
    healthRoute.nodes[1].position = {1.0F, 0.0F, 0.9F};
    healthRoute.nodes[2].position = {8.0F, 0.0F, 0.9F};
    healthRoute.links[healthRoute.linkCount++] = {0U, 1U, lg::BotNavLinkKind::Walk};
    healthRoute.healthAnchorNodes.fill(UINT16_MAX);
    healthRoute.healthAnchorNodes[3] = 1U;
    lg::prepareBotNavigationMap(healthRoute);
    lg::BotSenseFrame sense;
    sense.fixedDt = lg::kFixedTickSeconds;
    sense.self.position = healthRoute.nodes[0].position;
    sense.self.health = 20;
    sense.healthResources[0] = {3U, healthRoute.nodes[2].position, 25, true};
    sense.healthResourceCount = 1U;
    lg::BotBrain brain;
    brain.reset(0xA11CU);
    const lg::BotMotor motor = brain.tick(
      sense, lg::botDifficultyProfile(lg::BotAttackMode::Medium), healthRoute
    );
    failures += expect(
      motor.goal == lg::BotGoalKind::RecoverHealth && motor.waypointNode < healthRoute.nodeCount &&
        std::fabs(healthRoute.nodes[motor.waypointNode].position.x - 1.0F) < 0.01F,
      "health routing should use the resource-indexed collision-settled anchor, not its authored point"
    );
  }

  {
    lg::BotNavigationMap disconnected;
    disconnected.nodeCount = 2;
    disconnected.nodes[0].position = {0.0F, 0.0F, 0.9F};
    disconnected.nodes[1].position = {6.0F, 0.0F, 0.9F};
    lg::BotSenseFrame sense;
    sense.fixedDt = lg::kFixedTickSeconds;
    sense.self.position = disconnected.nodes[0].position;
    sense.objective = {.position = disconnected.nodes[1].position, .active = true};
    lg::BotBrain brain;
    brain.reset(0xD15CU);
    const lg::BotMotor motor = brain.tick(
      sense, lg::botDifficultyProfile(lg::BotAttackMode::Medium), disconnected
    );
    failures += expect(
      motor.goal == lg::BotGoalKind::Objective &&
        motor.waypointNode == lg::BotNavigationMap::kMaxNodes &&
        motor.command.forwardMove == 0.0F && motor.command.rightMove == 0.0F,
      "a disconnected route should fail safe instead of steering directly into its blocked goal"
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
    sense.weapons[lg::weaponIndex(lg::Weapon::LightningGun)] =
      {true, true, 18.0F, 6.0F, 0.05F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F};
    sense.weapons[lg::weaponIndex(lg::Weapon::PlasmaGun)] =
      {true, true, 8.0F, 20.0F, 0.10F, 100.0F, 0.0F, 0.0F, 0.0F, 0.0F};
    sense.visibleEnemies[0] = {1U, {-3.0F, 3.0F, 0.9F}, 1U, true, false};
    sense.visibleEnemyCount = 1;
    sense.healthResources[0] = {0U, {3.0F, 0.0F, 0.9F}, 25, true};
    sense.healthResourceCount = 1;
    lg::BotBrain brain;
    brain.reset(0x4567U);
    const lg::BotMotor routingMotor = brain.tick(
      sense, lg::botDifficultyProfile(lg::BotAttackMode::Medium), map
    );
    lg::BotMotor combatMotor = routingMotor;
    for (int tick = 0; tick < 50; ++tick) {
      combatMotor = brain.tick(
        sense, lg::botDifficultyProfile(lg::BotAttackMode::Medium), map
      );
    }
    failures += expect(
      routingMotor.goal == lg::BotGoalKind::RecoverHealth &&
        routingMotor.waypointNode < map.nodeCount &&
        combatMotor.command.weapon != lg::Weapon::RocketLauncher &&
        combatMotor.command.viewYawRadians > 0.0F,
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
    const lg::ServerSnapshot tuning = server.snapshot();
    const std::uint32_t initialBuilds = server.botNavigationBuildCount();
    lg::WeaponDamageTuning damage = tuning.weaponDamage;
    ++damage.machineGunDamage;
    const auto applyRuntimeTuning = [&](const lg::MovementTuning& movement, float scaleXY) {
      server.setRuntimeGameplayTuning(
        movement,
        scaleXY,
        tuning.playerSizeScaleZ,
        tuning.lightningKnockback,
        tuning.lightningFireHz,
        tuning.rocketKnockback,
        tuning.knockbackTimeMs,
        damage,
        tuning.vampirism,
        tuning.selfDamagePercent,
        tuning.healthAmount + 1,
        !tuning.weaponAmmo.infiniteAmmo,
        tuning.botDodgeEnabled,
        tuning.botDodgeMinIntervalMs,
        tuning.botDodgeMaxIntervalMs,
        tuning.weaponSwitchingMode
      );
    };
    applyRuntimeTuning(tuning.movementTuning, tuning.playerSizeScaleXY);
    const std::uint32_t nonNavigationBuilds = server.botNavigationBuildCount();
    lg::MovementTuning movement = tuning.movementTuning;
    movement.groundAcceleration += 1.0F;
    applyRuntimeTuning(movement, tuning.playerSizeScaleXY);
    applyRuntimeTuning(movement, tuning.playerSizeScaleXY * 1.1F);
    failures += expect(
      nonNavigationBuilds == initialBuilds &&
        server.botNavigationBuildCount() == initialBuilds + 2U,
      "runtime damage, health, and ammo changes must not rebuild nav, while movement and bounds changes must"
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
    for (int tick = 0; tick < 180; ++tick) server.tick(lg::kFixedTickSeconds);
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
    for (int tick = 0; tick < 180; ++tick) server.tick(lg::kFixedTickSeconds);
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
      .position = {0.0F, 0.0F, 0.9F}, .viewYawRadians = 1.7681919F,
      .health = 100, .alive = true, .onGround = true, .ammo = std::nullopt
    };
    setup.players[1] = {
      .connected = true, .ready = true, .team = lg::Team::Blue,
      .position = {-1.0F, 5.0F, 0.9F}, .viewYawRadians = -1.3734008F,
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
      "a carrying bot should install at its base even while a visible enemy is present"
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
    int heldBeamTicks = 0;
    int longestHeldBeamRun = 0;
    for (int tick = 0; tick < 450; ++tick) {
      server.tick(lg::kFixedTickSeconds);
      snapshot = latestSnapshot(transport);
      turned = turned || std::fabs(angleDelta(initialYaw, snapshot.players[1].viewYawRadians)) > 0.01F;
      fired = fired || snapshot.lightningGuns[1].active;
      heldBeamTicks = snapshot.lightningGuns[1].active ? heldBeamTicks + 1 : 0;
      longestHeldBeamRun = std::max(longestHeldBeamRun, heldBeamTicks);
    }
    failures += expect(snapshot.selectedWeapons[1] == lg::Weapon::LightningGun,
      "combat bots should use the Lightning Gun");
    failures += expect(turned, "combat bots should turn over multiple ticks");
    failures += expect(fired, "combat bots should eventually fire when aim is within tolerance");
    failures += expect(longestHeldBeamRun >= 16,
      "beam attack should remain held through every motor tick between easy perceptions");
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
