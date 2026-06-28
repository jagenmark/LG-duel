#include "net/LoopbackTransport.hpp"
#include "server/ServerGame.hpp"
#include "shared/Constants.hpp"

#include <array>
#include <cmath>
#include <cstdint>
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

lg::ServerSnapshot sendAndTick(
  lg::LoopbackTransport& transport,
  lg::ServerGame& server,
  const lg::CommandPacket& packet
) {
  transport.sendCommand(packet);
  server.tick(lg::kFixedTickSeconds);
  return latestSnapshot(transport);
}

lg::CommandPacket modeRequest(
  std::uint8_t playerIndex,
  std::uint32_t sequence,
  lg::GameMode gameMode
) {
  lg::CommandPacket packet;
  packet.playerIndex = playerIndex;
  packet.command.sequence = sequence;
  packet.requestGameMode = true;
  packet.requestedGameMode = gameMode;
  return packet;
}

lg::CommandPacket teamRequest(
  std::uint8_t playerIndex,
  std::uint32_t sequence,
  lg::Team team
) {
  lg::CommandPacket packet;
  packet.playerIndex = playerIndex;
  packet.command.sequence = sequence;
  packet.requestTeam = true;
  packet.requestedTeam = team;
  return packet;
}

lg::CommandPacket readyRequest(
  std::uint8_t playerIndex,
  std::uint32_t sequence
) {
  lg::CommandPacket packet;
  packet.playerIndex = playerIndex;
  packet.command.sequence = sequence;
  packet.toggleReady = true;
  return packet;
}

lg::CommandPacket aimedAttack(
  const lg::ServerSnapshot& snapshot,
  std::uint8_t attackerIndex,
  std::uint8_t targetIndex,
  std::uint32_t sequence,
  lg::Weapon weapon
) {
  const lg::Vec3 offset =
    snapshot.players[targetIndex].position - snapshot.players[attackerIndex].position;
  lg::CommandPacket packet;
  packet.playerIndex = attackerIndex;
  packet.command.sequence = sequence;
  packet.command.attack = true;
  packet.command.weapon = weapon;
  packet.command.viewYawRadians = std::atan2(offset.y, offset.x);
  packet.command.viewPitchRadians = std::atan2(
    offset.z,
    std::hypot(offset.x, offset.y)
  );
  return packet;
}

lg::ServerSnapshot configureLiveOneVersusTwo(
  lg::LoopbackTransport& transport,
  lg::ServerGame& server,
  std::uint16_t roundLimit = 2
) {
  lg::Arena arena;
  arena.spawnPositions[0] = {-4.0F, 0.0F, 0.0F};
  arena.spawnPositions[1] = {4.0F, 0.0F, 0.0F};
  arena.spawnPositions[2] = {6.5F, 0.0F, 0.0F};
  server.setArena(arena);
  server.setConnectedPlayers({true, true, true, false, false, false});
  latestSnapshot(transport);

  lg::MatchRules rules;
  rules.roundLimit = roundLimit;
  rules.countdownTicks = 0;
  rules.roundEndTicks = 2;
  rules.matchEndTicks = 2;
  server.setMatchRules(rules);

  sendAndTick(
    transport,
    server,
    modeRequest(0, 1, lg::GameMode::ClanArena)
  );
  sendAndTick(transport, server, teamRequest(0, 2, lg::Team::Red));
  sendAndTick(transport, server, teamRequest(1, 1, lg::Team::Blue));
  sendAndTick(transport, server, teamRequest(2, 1, lg::Team::Blue));
  sendAndTick(transport, server, readyRequest(0, 3));
  sendAndTick(transport, server, readyRequest(1, 2));
  return sendAndTick(transport, server, readyRequest(2, 2));
}

lg::ServerSnapshot stopAttack(
  lg::LoopbackTransport& transport,
  lg::ServerGame& server,
  std::uint8_t playerIndex,
  std::uint32_t sequence,
  lg::Weapon weapon = lg::Weapon::LightningGun
) {
  lg::CommandPacket packet;
  packet.playerIndex = playerIndex;
  packet.command.sequence = sequence;
  packet.command.weapon = weapon;
  return sendAndTick(transport, server, packet);
}

} // namespace

int main() {
  int failures = 0;

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    lg::Arena arena;
    server.setArena(arena);
    server.setConnectedPlayers({true, true, true, false, false, false});
    latestSnapshot(transport);

    lg::ServerSnapshot snapshot = sendAndTick(
      transport,
      server,
      modeRequest(0, 1, lg::GameMode::ClanArena)
    );
    failures += expect(
      snapshot.gameMode == lg::GameMode::ClanArena,
      "a connected player should be able to select Clan Arena during warmup"
    );

    snapshot = sendAndTick(transport, server, readyRequest(0, 2));
    failures += expect(
      !snapshot.readyPlayers[0],
      "an unassigned Clan Arena player should not be able to ready up"
    );

    sendAndTick(transport, server, teamRequest(0, 3, lg::Team::Red));
    sendAndTick(transport, server, teamRequest(1, 1, lg::Team::Blue));
    snapshot = sendAndTick(
      transport,
      server,
      teamRequest(2, 1, lg::Team::Blue)
    );
    failures += expect(
      snapshot.teams[0] == lg::Team::Red &&
        snapshot.teams[1] == lg::Team::Blue &&
        snapshot.teams[2] == lg::Team::Blue,
      "Clan Arena should accept an unequal 1v2 team assignment"
    );

    snapshot = sendAndTick(transport, server, readyRequest(0, 4));
    failures += expect(snapshot.readyPlayers[0], "assigned player should ready up");
    snapshot = sendAndTick(
      transport,
      server,
      teamRequest(1, 2, lg::Team::Blue)
    );
    failures += expect(
      snapshot.readyPlayers[0],
      "retransmitting the same explicit team should not clear readiness"
    );
    snapshot = sendAndTick(
      transport,
      server,
      teamRequest(1, 3, lg::Team::Red)
    );
    failures += expect(
      !snapshot.readyPlayers[0] && snapshot.teams[1] == lg::Team::Red,
      "changing a team during warmup should clear all readiness"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    lg::Arena arena;
    arena.spawnPositions[0] = {-4.0F, 0.0F, 0.0F};
    arena.spawnPositions[1] = {4.0F, 0.0F, 0.0F};
    server.setArena(arena);
    server.setConnectedPlayers({true, true, false, false, false, false});
    latestSnapshot(transport);

    lg::ServerSnapshot snapshot = sendAndTick(
      transport,
      server,
      modeRequest(0, 1, lg::GameMode::ClanArena)
    );
    const int unassignedTargetHealth = snapshot.players[1].health;
    snapshot = sendAndTick(
      transport,
      server,
      aimedAttack(snapshot, 0, 1, 2, lg::Weapon::Railgun)
    );
    failures += expect(
      snapshot.weaponFires[0].hit &&
        snapshot.players[1].health < unassignedTargetHealth &&
        snapshot.scores[0] == 0 &&
        snapshot.scores[1] == 0 &&
        snapshot.teamScores[0] == 0 &&
        snapshot.teamScores[1] == 0,
      "unassigned Clan Arena warmup players should damage each other without scoring"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    lg::Arena arena;
    arena.spawnPositions[0] = {-4.0F, 0.0F, 0.0F};
    arena.spawnPositions[1] = {4.0F, 0.0F, 0.0F};
    server.setArena(arena);
    server.setConnectedPlayers({true, true, false, false, false, false});
    latestSnapshot(transport);

    lg::ServerSnapshot snapshot = sendAndTick(
      transport,
      server,
      modeRequest(0, 1, lg::GameMode::ClanArena)
    );
    snapshot = sendAndTick(transport, server, teamRequest(0, 2, lg::Team::Red));
    snapshot = sendAndTick(transport, server, teamRequest(1, 1, lg::Team::Red));
    const int teammateHealth = snapshot.players[1].health;
    snapshot = sendAndTick(
      transport,
      server,
      aimedAttack(snapshot, 0, 1, 3, lg::Weapon::Railgun)
    );
    failures += expect(
      snapshot.weaponFires[0].hit &&
        snapshot.players[1].health < teammateHealth &&
        snapshot.scores[0] == 0 &&
        snapshot.scores[1] == 0 &&
        snapshot.teamScores[0] == 0 &&
        snapshot.teamScores[1] == 0,
      "same-team Clan Arena warmup players should damage each other without scoring"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    lg::ServerSnapshot snapshot = configureLiveOneVersusTwo(transport, server);
    failures += expect(
      snapshot.matchPhase == lg::MatchPhase::Live,
      "ready 1v2 teams should enter live play"
    );

    snapshot = sendAndTick(
      transport,
      server,
      modeRequest(0, 4, lg::GameMode::Duel)
    );
    failures += expect(
      snapshot.gameMode == lg::GameMode::ClanArena,
      "gamemode changes should be ignored outside warmup"
    );
    snapshot = sendAndTick(
      transport,
      server,
      teamRequest(1, 3, lg::Team::Red)
    );
    failures += expect(
      snapshot.teams[1] == lg::Team::Blue,
      "team changes should be ignored outside warmup"
    );

    server.setConnectedPlayers({true, true, true, true, false, false});
    snapshot = server.snapshot();
    failures += expect(
      snapshot.matchPhase == lg::MatchPhase::WaitingForReady &&
        snapshot.gameMode == lg::GameMode::ClanArena &&
        snapshot.teams[3] == lg::Team::None &&
        snapshot.teamScores[0] == 0 &&
        snapshot.teamScores[1] == 0,
      "joining during a match should abort to Clan Arena warmup"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    lg::ServerSnapshot snapshot = configureLiveOneVersusTwo(transport, server);
    server.setConnectedPlayers({true, true, false, false, false, false});
    snapshot = server.snapshot();
    failures += expect(
      snapshot.matchPhase == lg::MatchPhase::WaitingForReady &&
        snapshot.gameMode == lg::GameMode::ClanArena &&
        snapshot.teams[0] == lg::Team::Red &&
        snapshot.teams[1] == lg::Team::Blue &&
        snapshot.teams[2] == lg::Team::None,
      "leaving during a match should abort to Clan Arena warmup"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    lg::ServerSnapshot snapshot = configureLiveOneVersusTwo(transport, server);
    const int teammateHealth = snapshot.players[2].health;
    snapshot = sendAndTick(
      transport,
      server,
      aimedAttack(snapshot, 1, 2, 3, lg::Weapon::LightningGun)
    );
    failures += expect(
      snapshot.lightningGuns[1].hit &&
        snapshot.lightningGuns[1].damageApplied == 0 &&
        snapshot.players[2].health == teammateHealth &&
        std::hypot(
          snapshot.players[2].velocity.x,
          snapshot.players[2].velocity.y,
          snapshot.players[2].velocity.z
        ) > 0.0F,
      "friendly LG should apply knockback without damage"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    lg::ServerSnapshot snapshot = configureLiveOneVersusTwo(transport, server);
    const int ownerHealth = snapshot.players[1].health;
    const int teammateHealth = snapshot.players[2].health;
    snapshot = sendAndTick(
      transport,
      server,
      aimedAttack(snapshot, 1, 2, 3, lg::Weapon::RocketLauncher)
    );
    snapshot = stopAttack(
      transport,
      server,
      1,
      4,
      lg::Weapon::RocketLauncher
    );

    bool exploded = false;
    for (int tick = 0; tick < 80; ++tick) {
      server.tick(lg::kFixedTickSeconds);
      snapshot = latestSnapshot(transport);
      if (snapshot.rocketExplosions[1].active) {
        exploded = true;
        if (snapshot.players[1].health < ownerHealth) {
          break;
        }
      }
    }
    failures += expect(exploded, "friendly-fire test rocket should explode");
    failures += expect(
      snapshot.players[2].health == teammateHealth &&
        std::hypot(
          snapshot.players[2].velocity.x,
          snapshot.players[2].velocity.y,
          snapshot.players[2].velocity.z
        ) > 0.0F,
      "friendly RL splash should apply knockback without damage"
    );
    failures += expect(
      snapshot.players[1].health < ownerHealth,
      "rocket self-damage should remain enabled in Clan Arena"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    lg::ServerSnapshot snapshot = configureLiveOneVersusTwo(transport, server);

    lg::CommandPacket noLightningKnockback;
    noLightningKnockback.playerIndex = 0;
    noLightningKnockback.command.sequence = 4;
    noLightningKnockback.requestMovementTuning = true;
    noLightningKnockback.lightningKnockback = 0.0F;
    snapshot = sendAndTick(transport, server, noLightningKnockback);

    std::uint32_t redSequence = 5;
    for (int tick = 0; tick < 2000 && snapshot.players[1].health > 0; ++tick) {
      snapshot = sendAndTick(
        transport,
        server,
        aimedAttack(
          snapshot,
          0,
          1,
          redSequence++,
          lg::Weapon::LightningGun
        )
      );
    }
    failures += expect(
      snapshot.players[1].health == 0 &&
        snapshot.players[2].health > 0 &&
        snapshot.matchPhase == lg::MatchPhase::Live &&
        snapshot.teamScores[0] == 0 &&
        snapshot.scores[0] == 1,
      "eliminating one enemy should award an individual kill without ending the round"
    );
    failures += expect(
      snapshot.fragEvents[0].active &&
        snapshot.fragEvents[0].targetPlayerIndex == 1,
      "authoritative Clan Arena elimination should emit a frag event for the killer"
    );

    snapshot = stopAttack(transport, server, 0, redSequence++);
    const lg::Vec3 deadPosition = snapshot.players[1].position;
    lg::CommandPacket deadCommand;
    deadCommand.playerIndex = 1;
    deadCommand.command.sequence = 3;
    deadCommand.command.forwardMove = 1.0F;
    deadCommand.command.jump = true;
    deadCommand.command.attack = true;
    deadCommand.command.viewYawRadians = 1.234F;
    snapshot = sendAndTick(transport, server, deadCommand);
    failures += expect(
      snapshot.players[1].position.x == deadPosition.x &&
        snapshot.players[1].position.y == deadPosition.y &&
        snapshot.players[1].position.z == deadPosition.z &&
        snapshot.players[1].viewYawRadians == 1.234F &&
        !snapshot.lightningGuns[1].active,
      "dead players should look around without moving, jumping, or firing"
    );
    snapshot = stopAttack(transport, server, 1, 4);
    snapshot = sendAndTick(
      transport,
      server,
      aimedAttack(
        snapshot,
        0,
        2,
        redSequence++,
        lg::Weapon::LightningGun
      )
    );
    failures += expect(
      snapshot.lightningGuns[0].hit &&
        snapshot.lightningGuns[0].targetPlayerIndex == 2,
      "dead players should not block traces or retain a hitbox"
    );

    for (int tick = 0; tick < 2000 && snapshot.players[2].health > 0; ++tick) {
      snapshot = sendAndTick(
        transport,
        server,
        aimedAttack(
          snapshot,
          0,
          2,
          redSequence++,
          lg::Weapon::LightningGun
        )
      );
    }
    failures += expect(
      snapshot.matchPhase == lg::MatchPhase::RoundEnd &&
        snapshot.roundWinningTeam == lg::Team::Red &&
        snapshot.teamScores[0] == 1 &&
        snapshot.scores[0] == 2,
      "eliminating the opposing team should award one round and preserve individual kills"
    );

    server.tick(lg::kFixedTickSeconds);
    latestSnapshot(transport);
    server.tick(lg::kFixedTickSeconds);
    snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.matchPhase == lg::MatchPhase::Live &&
        snapshot.players[1].health == 100 &&
        snapshot.players[2].health == 100 &&
        snapshot.teamScores[0] == 1 &&
        snapshot.scores[0] == 2,
      "the next Clan Arena round should preserve team score and individual kills"
    );

    for (int tick = 0; tick < 2000 && snapshot.players[1].health > 0; ++tick) {
      snapshot = sendAndTick(
        transport,
        server,
        aimedAttack(
          snapshot,
          0,
          1,
          redSequence++,
          lg::Weapon::LightningGun
        )
      );
    }
    for (int tick = 0; tick < 2000 && snapshot.players[2].health > 0; ++tick) {
      snapshot = sendAndTick(
        transport,
        server,
        aimedAttack(
          snapshot,
          0,
          2,
          redSequence++,
          lg::Weapon::LightningGun
        )
      );
    }



    failures += expect(
      snapshot.matchPhase == lg::MatchPhase::MatchEnd &&
        snapshot.matchWinningTeam == lg::Team::Red &&
        snapshot.teamScores[0] == 2 &&
        snapshot.scores[0] == 4,
      "the team reaching the round limit should win while retaining individual kills"
    );

    lg::CommandPacket reset;
    reset.playerIndex = 2;
    reset.command.sequence = 4;
    reset.requestReset = true;
    snapshot = sendAndTick(transport, server, reset);
    failures += expect(
      snapshot.matchPhase == lg::MatchPhase::WaitingForReady &&
        snapshot.gameMode == lg::GameMode::ClanArena &&
        snapshot.teams[0] == lg::Team::Red &&
        snapshot.teams[1] == lg::Team::Blue &&
        snapshot.teams[2] == lg::Team::Blue &&
        snapshot.teamScores[0] == 0 &&
        snapshot.teamScores[1] == 0 &&
        snapshot.scores[0] == 0,
      "resetting Clan Arena should clear team scores and individual kills"
    );
  }

  return failures == 0 ? 0 : 1;
}
