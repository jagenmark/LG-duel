#include "net/LoopbackTransport.hpp"
#include "net/NetCodec.hpp"
#include "server/ServerGame.hpp"
#include "shared/Constants.hpp"

#include <array>
#include <cmath>
#include <iostream>
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
    arena.walls[0].min = {-0.2F, -2.0F, 0.0F};
    arena.walls[0].max = {0.2F, 2.0F, 4.0F};
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
  }

  return failures == 0 ? 0 : 1;
}
