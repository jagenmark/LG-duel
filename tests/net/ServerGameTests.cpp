#include "client/ClientGame.hpp"
#include "net/LoopbackTransport.hpp"
#include "server/ServerGame.hpp"
#include "shared/Constants.hpp"
#include "sim/MovementModes.hpp"
#include "sim/UserCommand.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>

namespace {

constexpr float kPi = 3.14159265359F;

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

} // namespace

int main() {
  int failures = 0;

  {
    lg::LoopbackTransport transport;
    lg::CommandPacket first;
    first.command.sequence = 3;
    lg::CommandPacket second;
    second.command.sequence = 4;
    transport.sendCommand(first);
    transport.sendCommand(second);

    lg::CommandPacket received;
    failures += expect(transport.receiveCommand(received), "loopback should return first queued command");
    failures += expect(received.command.sequence == 3, "loopback command order should be FIFO");
    failures += expect(transport.receiveCommand(received), "loopback should return second queued command");
    failures += expect(received.command.sequence == 4, "loopback should preserve all queued commands");
    failures += expect(!transport.receiveCommand(received), "empty loopback command queue should report false");
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);

    server.setConnectedPlayers({true, false});
    server.tick(lg::kFixedTickSeconds);
    latestSnapshot(transport);
    server.setConnectedPlayers({true, true});
    server.tick(lg::kFixedTickSeconds);
    lg::ServerSnapshot snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.matchPhase == lg::MatchPhase::WaitingForReady,
      "warmup frag test should start in ready-up"
    );

    lg::CommandPacket lethalWarmupDamage;
    lethalWarmupDamage.playerIndex = 0;
    lethalWarmupDamage.command.sequence = 1;
    lethalWarmupDamage.requestMovementTuning = true;
    lethalWarmupDamage.weaponDamage.railgunDamage = 100;
    transport.sendCommand(lethalWarmupDamage);
    server.tick(lg::kFixedTickSeconds);
    latestSnapshot(transport);

    lg::UserCommand warmupAttack;
    warmupAttack.sequence = 2;
    warmupAttack.attack = true;
    warmupAttack.planarAim = true;
    warmupAttack.viewYawRadians = 0.0F;
    warmupAttack.weapon = lg::Weapon::Railgun;
    transport.sendCommand(lg::CommandPacket{0, warmupAttack, false});
    server.tick(lg::kFixedTickSeconds);
    snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.fragEvents[0].active &&
        snapshot.fragEvents[0].targetPlayerIndex == 1 &&
        snapshot.players[1].health == 100 &&
        snapshot.scores[0] == 0 &&
        snapshot.scores[1] == 0,
      "warmup kill should emit a frag event, respawn, and not affect score"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);

    bool emittedFootstep = false;
    std::uint32_t emittedSequence = 0;
    for (std::uint32_t tick = 0; tick < 90 && !emittedFootstep; ++tick) {
      lg::CommandPacket command;
      command.playerIndex = 1;
      command.command.sequence = tick + 1;
      command.command.forwardMove = 1.0F;
      transport.sendCommand(command);
      server.tick(lg::kFixedTickSeconds);
      const lg::ServerSnapshot snapshot = latestSnapshot(transport);
      emittedFootstep = snapshot.footstepAudioEvents[1].active;
      emittedSequence = snapshot.footstepAudioEvents[1].sequence;
    }

    failures += expect(
      emittedFootstep && emittedSequence > 0,
      "server should emit authoritative footstep audio events for moving players"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);

    const std::filesystem::path mapDirectory =
      std::filesystem::temp_directory_path() / "lg_duel_server_map_tests";
    std::filesystem::create_directories(mapDirectory);
    {
      std::ofstream mapFile(mapDirectory / "tiny.lgmap");
      mapFile << R"(version 1
bounds min=-6,-6,0 max=6,6,6
box floor -6,-6,0 6,6,0.5
spawn p1 -2,0,0.5 yaw=0
spawn p2 2,0,0.5 yaw=180
)";
    }
    server.setMapDirectory(mapDirectory.string());
    const std::uint32_t initialRevision = server.snapshot().mapRevision;

    lg::CommandPacket mapRequest;
    mapRequest.command.sequence = 1;
    mapRequest.mapName = "tiny";
    transport.sendCommand(mapRequest);
    server.tick(lg::kFixedTickSeconds);

    lg::ServerSnapshot mapSnapshot = latestSnapshot(transport);
    failures += expect(
      mapSnapshot.mapRevision == initialRevision + 1 &&
        mapSnapshot.arena.wallCount == 1 &&
        mapSnapshot.arena.max.x == 6.0F &&
        mapSnapshot.players[0].position.x == -2.0F &&
        mapSnapshot.players[1].position.x == 2.0F,
      "client map request should load a server-local .lgmap and reset spawns"
    );

    lg::CommandPacket invalidMapRequest;
    invalidMapRequest.command.sequence = 2;
    invalidMapRequest.mapName = "../tiny";
    transport.sendCommand(invalidMapRequest);
    server.tick(lg::kFixedTickSeconds);
    mapSnapshot = latestSnapshot(transport);
    failures += expect(
      mapSnapshot.mapRevision == initialRevision + 1,
      "invalid client map names should be ignored"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);

    lg::CommandPacket tuningRequest;
    tuningRequest.command.sequence = 1;
    tuningRequest.command.forwardMove = 1.0F;
    tuningRequest.requestMovementTuning = true;
    tuningRequest.movementTuning.airControlEnabled = true;
    tuningRequest.movementTuning.groundAcceleration = 160.0F;
    tuningRequest.movementTuning.airAcceleration = 3.0F;
    tuningRequest.movementTuning.groundFriction = 4.0F;
    tuningRequest.movementTuning.stopSpeed = 2.5F;
    tuningRequest.movementTuning.maxGroundSpeed = 12.0F;
    tuningRequest.playerSizeScaleXY = 2.0F;
    tuningRequest.playerSizeScaleZ = 0.5F;
    tuningRequest.lightningKnockback = 35.0F;
    tuningRequest.rocketKnockback = 625.0F;
    tuningRequest.vampirism = 0.1F;
    tuningRequest.selfDamagePercent = 25;
    tuningRequest.healthAmount = 150;
    transport.sendCommand(tuningRequest);
    server.tick(lg::kFixedTickSeconds);

    const lg::ServerSnapshot tuned = latestSnapshot(transport);
    failures += expect(
      tuned.movementTuning.groundAcceleration == 160.0F &&
        tuned.movementTuning.airControlEnabled &&
        tuned.movementTuning.airAcceleration == 3.0F &&
        tuned.movementTuning.groundFriction == 4.0F &&
        tuned.movementTuning.stopSpeed == 2.5F &&
        tuned.movementTuning.maxGroundSpeed == 12.0F,
      "runtime movement tuning should be authoritative and replicated"
    );
    failures += expect(
      tuned.playerSizeScaleXY == 2.0F &&
        tuned.playerSizeScaleZ == 0.5F &&
        tuned.lightningKnockback == 35.0F &&
        tuned.rocketKnockback == 625.0F &&
        tuned.vampirism == 0.1F &&
        tuned.selfDamagePercent == 25 &&
        tuned.healthAmount == 150 &&
        tuned.players[0].bounds.radius == 0.7F &&
        tuned.players[1].bounds.radius == 0.7F &&
        tuned.players[0].bounds.halfHeight == 0.45F &&
        tuned.players[1].bounds.halfHeight == 0.45F &&
        tuned.players[0].position.z == 2.45F &&
        tuned.players[1].position.z == 2.45F,
      "runtime player dimensions should apply symmetrically and independently"
    );
    failures += expect(
      tuned.players[0].velocity.x > 1.0F,
      "updated acceleration should affect the requesting simulation tick"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    server.setConnectedPlayers({true, false});
    latestSnapshot(transport);

    lg::CommandPacket customHealth;
    customHealth.command.sequence = 1;
    customHealth.requestMovementTuning = true;
    customHealth.healthAmount = 175;
    transport.sendCommand(customHealth);
    server.tick(lg::kFixedTickSeconds);

    const lg::ServerSnapshot snapshot = latestSnapshot(transport);
    failures += expect(snapshot.healthAmount == 175, "g_healthamount should replicate to warmup snapshots");
    failures += expect(snapshot.players[0].health == 175, "warmup spawn should use g_healthamount for player one");
    failures += expect(snapshot.players[1].health == 175, "warmup spawn should use g_healthamount for player two");
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);

    lg::CommandPacket enableFlight;
    enableFlight.command.sequence = 1;
    enableFlight.command.forwardMove = 1.0F;
    enableFlight.command.viewPitchRadians = 0.5F;
    enableFlight.requestMovementTuning = true;
    enableFlight.movementTuning.flightEnabled = true;
    enableFlight.movementTuning.flightAcceleration = 64.0F;
    enableFlight.movementTuning.maxFlightSpeed = 14.0F;
    enableFlight.movementTuning.flightDamping = 0.0F;
    enableFlight.movementTuning.flightGravityCancel = 1.0F;
    transport.sendCommand(enableFlight);
    server.tick(lg::kFixedTickSeconds);

    lg::ServerSnapshot snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.movementTuning.flightEnabled &&
        snapshot.players[0].movementMode == lg::MovementMode::Flying &&
        snapshot.players[1].movementMode == lg::MovementMode::Flying &&
        snapshot.players[0].velocity.z > 0.0F,
      "g_flight should enable symmetric authoritative pitch-directed flight"
    );

    lg::CommandPacket disableFlight = enableFlight;
    disableFlight.command.sequence = 2;
    disableFlight.command.forwardMove = 0.0F;
    disableFlight.requestMovementTuning = true;
    disableFlight.movementTuning.flightEnabled = false;
    transport.sendCommand(disableFlight);
    server.tick(lg::kFixedTickSeconds);

    snapshot = latestSnapshot(transport);
    failures += expect(
      !snapshot.movementTuning.flightEnabled &&
        snapshot.players[0].movementMode != lg::MovementMode::Flying &&
        snapshot.players[1].movementMode != lg::MovementMode::Flying,
      "disabling g_flight should return both players to grounded or airborne movement"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);

    lg::CommandPacket tuningRequest;
    tuningRequest.command.sequence = 1;
    tuningRequest.requestMovementTuning = true;
    tuningRequest.lightningKnockback = 1000.0F;
    transport.sendCommand(tuningRequest);
    server.tick(lg::kFixedTickSeconds);
    latestSnapshot(transport);

    lg::UserCommand attack;
    attack.sequence = 2;
    attack.attack = true;
    transport.sendCommand(lg::CommandPacket{0, attack, false});
    server.tick(lg::kFixedTickSeconds);
    const lg::ServerSnapshot snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.lightningKnockback == 1000.0F &&
        snapshot.lightningGuns[0].hit &&
        snapshot.lightningGuns[0].knockbackImpulse.x > 0.17F &&
        snapshot.lightningGuns[0].knockbackImpulse.x < 0.18F,
      "g_lg_knockback should control authoritative LG impulse magnitude"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);

    lg::CommandPacket minimumKnockback;
    minimumKnockback.command.sequence = 1;
    minimumKnockback.requestMovementTuning = true;
    minimumKnockback.lightningKnockback = 0.0F;
    transport.sendCommand(minimumKnockback);
    server.tick(lg::kFixedTickSeconds);
    latestSnapshot(transport);

    lg::UserCommand attack;
    attack.sequence = 2;
    attack.attack = true;
    transport.sendCommand(lg::CommandPacket{0, attack, false});
    server.tick(lg::kFixedTickSeconds);
    lg::ServerSnapshot snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.lightningKnockback == 0.0F &&
        snapshot.lightningGuns[0].hit &&
        snapshot.lightningGuns[0].knockbackImpulse.x > 0.119F &&
        snapshot.lightningGuns[0].knockbackImpulse.x < 0.121F,
      "g_lg_knockback 0 should map to the old 682 impulse"
    );

    lg::CommandPacket halfKnockback;
    halfKnockback.command.sequence = 3;
    halfKnockback.requestMovementTuning = true;
    halfKnockback.lightningKnockback = 500.0F;
    transport.sendCommand(halfKnockback);
    server.tick(lg::kFixedTickSeconds);
    latestSnapshot(transport);

    attack.sequence = 4;
    transport.sendCommand(lg::CommandPacket{0, attack, false});
    server.tick(lg::kFixedTickSeconds);
    snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.lightningKnockback == 500.0F &&
        snapshot.lightningGuns[0].hit &&
        snapshot.lightningGuns[0].knockbackImpulse.x > 0.147F &&
        snapshot.lightningGuns[0].knockbackImpulse.x < 0.149F,
      "g_lg_knockback 500 should map to the old 841 impulse"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);

    lg::CommandPacket tuning;
    tuning.command.sequence = 1;
    tuning.requestMovementTuning = true;
    tuning.weaponDamage.lightningGunDamage = 125;
    transport.sendCommand(tuning);
    server.tick(lg::kFixedTickSeconds);
    lg::ServerSnapshot snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.weaponDamage.lightningGunDamage == 125,
      "g_lg_damage should replicate to authoritative snapshots"
    );

    lg::UserCommand lightning;
    lightning.sequence = 2;
    lightning.attack = true;
    lg::CommandPacket lightningAttack;
    lightningAttack.command = lightning;
    transport.sendCommand(lightningAttack);
    for (int tick = 0; tick < 125; ++tick) {
      server.tick(lg::kFixedTickSeconds);
      snapshot = latestSnapshot(transport);
    }
    failures += expect(
      snapshot.players[1].health == 0,
      "g_lg_damage should control authoritative LG damage per second"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);

    lg::CommandPacket tuning;
    tuning.command.sequence = 1;
    tuning.requestMovementTuning = true;
    tuning.weaponDamage.railgunDamage = 50;
    transport.sendCommand(tuning);
    server.tick(lg::kFixedTickSeconds);
    lg::ServerSnapshot snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.weaponDamage.railgunDamage == 50,
      "g_rg_damage should replicate to authoritative snapshots"
    );

    lg::UserCommand railgun;
    railgun.sequence = 2;
    railgun.attack = true;
    railgun.weapon = lg::Weapon::Railgun;
    lg::CommandPacket railgunAttack;
    railgunAttack.command = railgun;
    transport.sendCommand(railgunAttack);
    server.tick(lg::kFixedTickSeconds);
    snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.weaponFires[0].damageApplied == 50 &&
        snapshot.players[1].health == 50,
      "g_rg_damage should control railgun damage per shot"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);

    lg::CommandPacket tuning;
    tuning.command.sequence = 1;
    tuning.requestMovementTuning = true;
    tuning.weaponDamage.machineGunDamage = 9;
    transport.sendCommand(tuning);
    server.tick(lg::kFixedTickSeconds);
    lg::ServerSnapshot snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.weaponDamage.machineGunDamage == 9,
      "g_mg_damage should replicate to authoritative snapshots"
    );

    lg::UserCommand machineGun;
    machineGun.sequence = 2;
    machineGun.attack = true;
    machineGun.weapon = lg::Weapon::MachineGun;
    lg::CommandPacket machineGunAttack;
    machineGunAttack.command = machineGun;
    transport.sendCommand(machineGunAttack);
    server.tick(lg::kFixedTickSeconds);
    snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.weaponFires[0].damageApplied == 9 &&
        snapshot.players[1].health == 91,
      "g_mg_damage should control machine gun damage per shot"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);

    lg::CommandPacket tuning;
    tuning.command.sequence = 1;
    tuning.requestMovementTuning = true;
    tuning.weaponDamage.shotgunDamagePerPellet = 3;
    transport.sendCommand(tuning);
    server.tick(lg::kFixedTickSeconds);
    lg::ServerSnapshot snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.weaponDamage.shotgunDamagePerPellet == 3,
      "g_sg_damage should replicate to authoritative snapshots"
    );

    lg::UserCommand shotgun;
    shotgun.sequence = 2;
    shotgun.attack = true;
    shotgun.weapon = lg::Weapon::Shotgun;
    lg::CommandPacket shotgunAttack;
    shotgunAttack.command = shotgun;
    transport.sendCommand(shotgunAttack);
    server.tick(lg::kFixedTickSeconds);
    snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.weaponFires[0].damageApplied ==
          static_cast<int>(snapshot.weaponFires[0].pelletHitCount) * 3 &&
        snapshot.players[1].health == 100 - snapshot.weaponFires[0].damageApplied,
      "g_sg_damage should control shotgun damage per pellet"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);

    lg::CommandPacket tuning;
    tuning.command.sequence = 1;
    tuning.requestMovementTuning = true;
    tuning.weaponDamage.rocketLauncherDamage = 50;
    transport.sendCommand(tuning);
    server.tick(lg::kFixedTickSeconds);
    lg::ServerSnapshot snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.weaponDamage.rocketLauncherDamage == 50,
      "g_rl_damage should replicate to authoritative snapshots"
    );

    lg::UserCommand rocket;
    rocket.sequence = 2;
    rocket.attack = true;
    rocket.weapon = lg::Weapon::RocketLauncher;
    lg::CommandPacket rocketAttack;
    rocketAttack.command = rocket;
    transport.sendCommand(rocketAttack);
    server.tick(lg::kFixedTickSeconds);
    for (int tick = 0; tick < 160; ++tick) {
      server.tick(lg::kFixedTickSeconds);
      snapshot = latestSnapshot(transport);
      if (snapshot.rocketExplosions[0].active) {
        break;
      }
    }
    failures += expect(
      snapshot.rocketExplosions[0].opponentDamageApplied == 50 &&
        snapshot.players[1].health == 50,
      "g_rl_damage should control rocket direct and max splash damage"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);

    lg::CommandPacket disableVampirism;
    disableVampirism.command.sequence = 1;
    disableVampirism.requestMovementTuning = true;
    disableVampirism.lightningKnockback = 0.0F;
    disableVampirism.vampirism = 0.0F;
    transport.sendCommand(disableVampirism);
    server.tick(lg::kFixedTickSeconds);
    latestSnapshot(transport);

    lg::CommandPacket damageAttacker;
    damageAttacker.playerIndex = 1;
    damageAttacker.command.sequence = 1;
    damageAttacker.command.attack = true;
    damageAttacker.command.viewYawRadians = kPi;
    transport.sendCommand(damageAttacker);
    lg::ServerSnapshot snapshot;
    for (int tick = 0; tick < 100; ++tick) {
      server.tick(lg::kFixedTickSeconds);
      snapshot = latestSnapshot(transport);
      if (snapshot.players[0].health <= 50) {
        break;
      }
    }
    damageAttacker.command.sequence = 2;
    damageAttacker.command.attack = false;
    transport.sendCommand(damageAttacker);
    server.tick(lg::kFixedTickSeconds);
    snapshot = latestSnapshot(transport);
    const int damagedHealth = snapshot.players[0].health;

    lg::CommandPacket attack;
    attack.command.sequence = 2;
    attack.command.attack = true;
    transport.sendCommand(attack);
    const int disabledTargetHealth = snapshot.players[1].health;
    for (
      int tick = 0;
      tick < 100 && snapshot.players[1].health > disabledTargetHealth - 10;
      ++tick
    ) {
      server.tick(lg::kFixedTickSeconds);
      snapshot = latestSnapshot(transport);
    }
    failures += expect(
      snapshot.vampirism == 0.0F &&
        snapshot.players[0].health == damagedHealth,
      "g_vampirism 0 should disable damage-based healing"
    );

    attack.command.sequence = 3;
    attack.command.attack = false;
    transport.sendCommand(attack);
    server.tick(lg::kFixedTickSeconds);
    latestSnapshot(transport);

    lg::CommandPacket tenPercent;
    tenPercent.command.sequence = 4;
    tenPercent.requestMovementTuning = true;
    tenPercent.lightningKnockback = 0.0F;
    tenPercent.vampirism = 0.1F;
    transport.sendCommand(tenPercent);
    server.tick(lg::kFixedTickSeconds);
    snapshot = latestSnapshot(transport);

    attack.command.sequence = 5;
    attack.command.attack = true;
    transport.sendCommand(attack);
    const int fractionalTargetHealth = snapshot.players[1].health;
    for (
      int tick = 0;
      tick < 100 && snapshot.players[1].health > fractionalTargetHealth - 10;
      ++tick
    ) {
      server.tick(lg::kFixedTickSeconds);
      snapshot = latestSnapshot(transport);
    }
    failures += expect(
      snapshot.vampirism == 0.1F &&
        snapshot.players[0].health == damagedHealth + 1,
      (
        "g_vampirism 0.1 should accumulate and heal 10 percent of damage; health=" +
        std::to_string(snapshot.players[0].health) +
        " expected=" + std::to_string(damagedHealth + 1) +
        " damage=" +
        std::to_string(fractionalTargetHealth - snapshot.players[1].health)
      )
    );

    attack.command.sequence = 6;
    attack.command.attack = false;
    transport.sendCommand(attack);
    server.tick(lg::kFixedTickSeconds);
    latestSnapshot(transport);

    lg::CommandPacket doubleHealing;
    doubleHealing.command.sequence = 7;
    doubleHealing.requestMovementTuning = true;
    doubleHealing.lightningKnockback = 0.0F;
    doubleHealing.vampirism = 2.0F;
    transport.sendCommand(doubleHealing);
    server.tick(lg::kFixedTickSeconds);
    latestSnapshot(transport);

    attack.command.sequence = 8;
    attack.command.attack = true;
    transport.sendCommand(attack);
    for (int tick = 0; tick < 100; ++tick) {
      server.tick(lg::kFixedTickSeconds);
      snapshot = latestSnapshot(transport);
      if (snapshot.players[0].health == 100) {
        break;
      }
    }
    failures += expect(
      snapshot.vampirism == 2.0F &&
        snapshot.players[0].health == 100,
      "g_vampirism 2 should heal 200 percent without exceeding 100 health"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);

    lg::UserCommand planar;
    planar.sequence = 1;
    planar.attack = true;
    planar.viewPitchRadians = 0.2F;
    planar.planarAim = true;
    transport.sendCommand(lg::CommandPacket{0, planar, false});
    server.tick(lg::kFixedTickSeconds);
    const lg::ServerSnapshot planarSnapshot = latestSnapshot(transport);
    failures += expect(
      std::fabs(planarSnapshot.lightningGuns[0].end.z - 3.55F) <= 0.01F,
      "top-down relative aim should flatten beam pitch authoritatively"
    );

    lg::UserCommand perspective = planar;
    perspective.sequence = 2;
    perspective.planarAim = false;
    transport.sendCommand(lg::CommandPacket{0, perspective, false});
    server.tick(lg::kFixedTickSeconds);
    const lg::ServerSnapshot perspectiveSnapshot = latestSnapshot(transport);
    failures += expect(
      perspectiveSnapshot.lightningGuns[0].end.z >
        perspectiveSnapshot.lightningGuns[0].start.z,
      "perspective relative aim should preserve beam pitch authoritatively"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);

    lg::CommandPacket rename;
    rename.playerIndex = 0;
    rename.command.sequence = 1;
    rename.playerName = "yg";
    transport.sendCommand(rename);
    server.tick(lg::kFixedTickSeconds);

    failures += expect(
      latestSnapshot(transport).playerNames[0] == "yg",
      "server should replicate an accepted player name"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);

    lg::CommandPacket chat;
    chat.playerIndex = 1;
    chat.command.sequence = 1;
    chat.chatMessage = "good luck";
    transport.sendCommand(chat);
    server.tick(lg::kFixedTickSeconds);

    const lg::ServerSnapshot snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.chatSequence == 1 &&
        snapshot.chatPlayerIndex == 1 &&
        snapshot.chatMessage == "good luck",
      "server should relay accepted player chat"
    );

    transport.sendCommand(chat);
    server.tick(lg::kFixedTickSeconds);
    failures += expect(
      latestSnapshot(transport).chatSequence == 1,
      "duplicate commands should not relay chat twice"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);

    lg::UserCommand beforeWrap;
    beforeWrap.sequence = std::numeric_limits<std::uint32_t>::max();
    transport.sendCommand(lg::CommandPacket{0, beforeWrap, false});
    server.tick(lg::kFixedTickSeconds);

    lg::UserCommand afterWrap;
    afterWrap.sequence = 0;
    afterWrap.viewYawRadians = kPi;
    transport.sendCommand(lg::CommandPacket{0, afterWrap, false});
    server.tick(lg::kFixedTickSeconds);

    const lg::ServerSnapshot snapshot = latestSnapshot(transport);
    failures += expect(snapshot.acknowledgedCommand[0] == 0, "sequence zero should follow uint32 wrap");
    failures += expect(snapshot.players[0].viewYawRadians == kPi, "wrapped command should be simulated");
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    lg::ClientGame client(transport, 0);
    client.receiveSnapshots();

    failures += expect(client.hasSnapshot(), "server should publish an initial snapshot");
    failures += expect(client.snapshot().serverTick == 0, "initial snapshot should start at server tick zero");
    failures += expect(!client.hasAcknowledgedCommand(), "initial snapshot should not acknowledge a command");
    failures += expect(
      client.snapshot().players[0].movementMode == lg::MovementMode::Grounded,
      "snapshot should preserve local movement mode"
    );
    failures += expect(
      client.snapshot().players[1].movementMode == lg::MovementMode::Grounded,
      "snapshot should preserve remote movement mode"
    );

    lg::UserCommand command;
    command.sequence = 10;
    command.clientTick = 20;
    command.forwardMove = 1.0F;
    command.attack = true;
    client.sendCommand(command, false);
    server.tick(lg::kFixedTickSeconds);
    client.receiveSnapshots();

    failures += expect(client.snapshot().serverTick == 1, "server tick should advance once per simulation step");
    failures += expect(client.hasAcknowledgedCommand(), "accepted command should set ack validity");
    failures += expect(client.lastAcknowledgedCommand() == 10, "snapshot should acknowledge accepted command");
    failures += expect(client.snapshot().players[0].position.x > -8.0F, "server should simulate accepted movement");
    failures += expect(client.snapshot().lightningGuns[0].hit, "server should authoritatively trace LG");

    lg::UserCommand duplicate = command;
    duplicate.viewYawRadians = kPi;
    client.sendCommand(duplicate, false);
    server.tick(lg::kFixedTickSeconds);
    client.receiveSnapshots();

    failures += expect(client.lastAcknowledgedCommand() == 10, "duplicate command should not change ack");
    failures += expect(
      client.snapshot().players[0].viewYawRadians == 0.0F,
      "duplicate command should not overwrite authoritative view"
    );

    lg::UserCommand stale = duplicate;
    stale.sequence = 9;
    client.sendCommand(stale, false);
    server.tick(lg::kFixedTickSeconds);
    client.receiveSnapshots();

    failures += expect(client.lastAcknowledgedCommand() == 10, "out-of-order command should be ignored");
    failures += expect(
      client.snapshot().players[0].viewYawRadians == 0.0F,
      "out-of-order command should not change state"
    );

    lg::UserCommand reset;
    reset.sequence = 11;
    client.sendCommand(reset, true);
    server.tick(lg::kFixedTickSeconds);
    client.receiveSnapshots();

    failures += expect(client.snapshot().serverTick == 4, "reset should preserve monotonic server ticks");
    failures += expect(client.snapshot().players[0].position.x == -8.0F, "client should receive reset spawn");
    failures += expect(client.snapshot().players[1].health == 100, "client should receive reset health");

    lg::UserCommand postResetMove;
    postResetMove.sequence = 12;
    postResetMove.forwardMove = 1.0F;
    client.sendCommand(postResetMove, false);
    server.tick(lg::kFixedTickSeconds);
    client.receiveSnapshots();
    const float movedPosition = client.snapshot().players[0].position.x;

    lg::UserCommand staleReset;
    staleReset.sequence = 11;
    client.sendCommand(staleReset, true);
    server.tick(lg::kFixedTickSeconds);
    client.receiveSnapshots();

    failures += expect(client.lastAcknowledgedCommand() == 12, "stale reset should not change ack");
    failures += expect(
      client.snapshot().players[0].position.x >= movedPosition,
      "stale reset packet should not restore spawn"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);

    std::array<bool, lg::kDuelPlayerCount> connected = {};
    connected[0] = true;
    std::array<std::uint32_t, lg::kDuelPlayerCount> sessions = {};
    sessions[0] = 1;
    server.setConnectedPlayers(connected, sessions);
    latestSnapshot(transport);

    lg::UserCommand oldSessionCommand;
    oldSessionCommand.sequence = 100;
    lg::CommandPacket oldSessionPacket;
    oldSessionPacket.playerIndex = 0;
    oldSessionPacket.command = oldSessionCommand;
    transport.sendCommand(oldSessionPacket);
    server.tick(lg::kFixedTickSeconds);
    lg::ServerSnapshot snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.hasAcknowledgedCommand[0] &&
        snapshot.acknowledgedCommand[0] == 100,
      "old session should establish a high acknowledged command"
    );
    const float beforeReconnectMove = snapshot.players[0].position.x;

    sessions[0] = 2;
    server.setConnectedPlayers(connected, sessions);
    failures += expect(
      !server.snapshot().hasAcknowledgedCommand[0],
      "new session in an occupied slot should clear stale command ack state"
    );

    lg::UserCommand freshSessionCommand;
    freshSessionCommand.sequence = 0;
    freshSessionCommand.forwardMove = 1.0F;
    lg::CommandPacket freshSessionPacket;
    freshSessionPacket.playerIndex = 0;
    freshSessionPacket.command = freshSessionCommand;
    transport.sendCommand(freshSessionPacket);
    server.tick(lg::kFixedTickSeconds);
    snapshot = latestSnapshot(transport);

    failures += expect(
      snapshot.hasAcknowledgedCommand[0] &&
        snapshot.acknowledgedCommand[0] == 0,
      "new session should accept command sequence zero after reconnect"
    );
    failures += expect(
      snapshot.players[0].position.x > beforeReconnectMove,
      "new session command should move instead of rubberbanding to stale state"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);

    for (std::uint32_t sequence = 0; sequence < 20; ++sequence) {
      lg::UserCommand targetCommand;
      targetCommand.sequence = sequence;
      targetCommand.viewYawRadians = kPi;
      targetCommand.rightMove = 1.0F;
      transport.sendCommand(lg::CommandPacket{1, targetCommand, false, false, sequence});
      server.tick(lg::kFixedTickSeconds);
    }

    const lg::ServerSnapshot beforeAttack = latestSnapshot(transport);
    failures += expect(
      std::fabs(beforeAttack.players[1].position.y) >
        beforeAttack.players[1].bounds.radius,
      "moving target should leave the uncompensated beam path"
    );

    lg::UserCommand attack;
    attack.sequence = 0;
    attack.attack = true;
    transport.sendCommand(lg::CommandPacket{0, attack, false, 0});
    server.tick(lg::kFixedTickSeconds);

    const lg::ServerSnapshot compensated = latestSnapshot(transport);
    failures += expect(compensated.lightningGuns[0].hit, "rewound LG should hit historical target");
    failures += expect(
      compensated.players[1].health == 100,
      "first fixed-tick hit should retain fractional damage against current state"
    );
    failures += expect(
      compensated.lightningGuns[0].requestedRewindTicks == 20 &&
        compensated.lightningGuns[0].appliedRewindTicks == 20 &&
        !compensated.lightningGuns[0].rewindClamped,
      "LG should report an in-range historical rewind"
    );
    failures += expect(
      compensated.lightningGuns[0].hasRewindDebug &&
        compensated.lightningGuns[0].rewindTargetTick == 0 &&
        std::fabs(
          compensated.lightningGuns[0].currentTargetPosition.y -
          compensated.lightningGuns[0].rewoundTargetPosition.y
        ) > compensated.players[1].bounds.radius,
      "LG should replicate the exact current and historical bounds used by the trace"
    );

    attack.sequence = 1;
    transport.sendCommand(lg::CommandPacket{0, attack, false, 0});
    server.tick(lg::kFixedTickSeconds);
    const lg::ServerSnapshot damaged = latestSnapshot(transport);
    failures += expect(damaged.lightningGuns[0].hit, "continuous rewound LG should remain active");
    failures += expect(
      damaged.players[1].health == 99,
      "rewound hit damage should apply to the current authoritative target"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);

    lg::CommandPacket dimensions;
    dimensions.command.sequence = 0;
    dimensions.requestMovementTuning = true;
    dimensions.playerSizeScaleZ = 0.5F;
    transport.sendCommand(dimensions);
    server.tick(lg::kFixedTickSeconds);
    const lg::ServerSnapshot historical = latestSnapshot(transport);

    for (std::uint32_t sequence = 1; sequence <= 20; ++sequence) {
      lg::UserCommand jump;
      jump.sequence = sequence;
      jump.viewYawRadians = kPi;
      jump.jump = true;
      jump.upMove = 1.0F;
      transport.sendCommand(
        lg::CommandPacket{1, jump, false, false, historical.serverTick}
      );
      server.tick(lg::kFixedTickSeconds);
    }
    const lg::ServerSnapshot airborne = latestSnapshot(transport);
    failures += expect(
      airborne.players[1].position.z -
          historical.players[1].position.z >
        airborne.players[1].bounds.halfHeight * 2.0F,
      "vertical lag-comp test target should leave the current beam height"
    );

    lg::UserCommand attack;
    attack.sequence = 21;
    attack.attack = true;
    attack.planarAim = true;
    transport.sendCommand(
      lg::CommandPacket{
        0,
        attack,
        false,
        false,
        historical.serverTick,
      }
    );
    server.tick(lg::kFixedTickSeconds);
    const lg::ServerSnapshot compensated = latestSnapshot(transport);
    failures += expect(
      compensated.lightningGuns[0].hit &&
        compensated.lightningGuns[0].rewoundTargetPosition.z <
          compensated.lightningGuns[0].currentTargetPosition.z,
      "3D lag compensation should hit a historical lower target after it jumps"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    const lg::ServerSnapshot historical = latestSnapshot(transport);

    for (std::uint32_t sequence = 0; sequence < 20; ++sequence) {
      lg::UserCommand move;
      move.sequence = sequence;
      move.viewYawRadians = kPi;
      move.rightMove = 1.0F;
      transport.sendCommand(
        lg::CommandPacket{1, move, false, false, historical.serverTick}
      );
      server.tick(lg::kFixedTickSeconds);
    }
    const lg::ServerSnapshot current = latestSnapshot(transport);
    const lg::Vec3 offset =
      current.players[1].position - current.players[0].position;

    lg::UserCommand attack;
    attack.sequence = 0;
    attack.attack = true;
    attack.planarAim = true;
    attack.viewYawRadians = std::atan2(offset.y, offset.x);
    transport.sendCommand(
      lg::CommandPacket{
        0,
        attack,
        false,
        false,
        historical.serverTick,
      }
    );
    server.tick(lg::kFixedTickSeconds);
    const lg::ServerSnapshot rewoundMiss = latestSnapshot(transport);
    failures += expect(
      !rewoundMiss.lightningGuns[0].hit &&
        rewoundMiss.lightningGuns[0].hasRewindDebug &&
        std::fabs(
          rewoundMiss.lightningGuns[0].currentTargetPosition.y -
          rewoundMiss.lightningGuns[0].rewoundTargetPosition.y
        ) > rewoundMiss.players[1].bounds.radius,
      "lag compensation should miss when the historical target was off the current aim line"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);

    for (std::uint32_t sequence = 0; sequence < 40; ++sequence) {
      lg::UserCommand targetCommand;
      targetCommand.sequence = sequence;
      targetCommand.viewYawRadians = kPi;
      transport.sendCommand(lg::CommandPacket{1, targetCommand, false, false, sequence});
      server.tick(lg::kFixedTickSeconds);
    }
    latestSnapshot(transport);

    lg::UserCommand attack;
    attack.sequence = 0;
    attack.attack = true;
    transport.sendCommand(lg::CommandPacket{0, attack, false, 0});
    server.tick(lg::kFixedTickSeconds);

    const lg::ServerSnapshot clamped = latestSnapshot(transport);
    failures += expect(
      clamped.lightningGuns[0].requestedRewindTicks == 40,
      "LG should report the full requested rewind"
    );
    failures += expect(
      clamped.lightningGuns[0].appliedRewindTicks == 25 &&
        clamped.lightningGuns[0].rewindClamped,
      "LG rewind should clamp to 200 ms"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);

    lg::UserCommand plasma;
    plasma.sequence = 77;
    plasma.attack = true;
    plasma.weapon = lg::Weapon::PlasmaGun;
    transport.sendCommand(lg::CommandPacket{0, plasma, false});
    server.tick(lg::kFixedTickSeconds);
    lg::ServerSnapshot snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.acknowledgedCommand[0] == plasma.sequence,
      "server should accept and acknowledge expanded weapon selections"
    );
    failures += expect(
      !snapshot.lightningGuns[0].active && !snapshot.weaponFires[0].fired,
      "unsupported expanded weapons should not fire implemented weapon effects"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);

    lg::UserCommand grenade;
    grenade.sequence = 1;
    grenade.attack = true;
    grenade.weapon = lg::Weapon::GrenadeLauncher;
    transport.sendCommand(lg::CommandPacket{0, grenade, false});
    server.tick(lg::kFixedTickSeconds);
    lg::ServerSnapshot snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.weaponFires[0].fired &&
        snapshot.weaponFires[0].weapon == lg::Weapon::GrenadeLauncher,
      "grenade launcher should fire a weapon event"
    );
    failures += expect(
      snapshot.rockets[0].active &&
        snapshot.rockets[0].weapon == lg::Weapon::GrenadeLauncher &&
        snapshot.rockets[0].velocity.z > 0.0F,
      "grenade launcher should replicate arcing projectile state"
    );

    grenade.sequence = 2;
    transport.sendCommand(lg::CommandPacket{0, grenade, false});
    server.tick(lg::kFixedTickSeconds);
    snapshot = latestSnapshot(transport);
    std::size_t activeGrenades = 0;
    for (const lg::RocketProjectileSnapshot& projectile : snapshot.rockets) {
      if (
        projectile.active &&
        projectile.weapon == lg::Weapon::GrenadeLauncher
      ) {
        ++activeGrenades;
      }
    }
    failures += expect(
      activeGrenades == 1,
      "grenade launcher cooldown should block immediate second grenades"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);

    lg::CommandPacket noSelfDamage;
    noSelfDamage.command.sequence = 1;
    noSelfDamage.requestMovementTuning = true;
    noSelfDamage.selfDamagePercent = 0;
    noSelfDamage.rocketKnockback = 1000.0F;
    transport.sendCommand(noSelfDamage);
    server.tick(lg::kFixedTickSeconds);
    latestSnapshot(transport);

    lg::UserCommand grenadeDown;
    grenadeDown.sequence = 2;
    grenadeDown.attack = true;
    grenadeDown.weapon = lg::Weapon::GrenadeLauncher;
    grenadeDown.planarAim = false;
    grenadeDown.viewPitchRadians = -kPi * 0.5F;
    transport.sendCommand(lg::CommandPacket{0, grenadeDown, false});
    server.tick(lg::kFixedTickSeconds);
    lg::ServerSnapshot snapshot = latestSnapshot(transport);

    bool bounced = false;
    bool exploded = false;
    bool emittedBounceAudio = false;
    for (int tick = 0; tick < 80; ++tick) {
      server.tick(lg::kFixedTickSeconds);
      snapshot = latestSnapshot(transport);
      bounced = bounced ||
        (
          snapshot.rockets[0].active &&
          snapshot.rockets[0].weapon == lg::Weapon::GrenadeLauncher &&
          snapshot.rockets[0].velocity.z > 0.0F
        );
      exploded = exploded || snapshot.rocketExplosions[0].active;
      emittedBounceAudio = emittedBounceAudio ||
        (
          snapshot.grenadeBounceAudioEvents[0].active &&
          snapshot.grenadeBounceAudioEvents[0].sequence > 0
        );
      if (bounced || exploded) {
        break;
      }
    }
    failures += expect(bounced, "downward grenade should bounce instead of exploding on first floor contact");
    failures += expect(emittedBounceAudio, "grenade bounce should emit positional audio event");
    failures += expect(!exploded, "grenade bounce should not emit an immediate explosion");
    failures += expect(snapshot.players[0].health == 100, "bounced grenade should not damage the owner before fuse");
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);

    lg::CommandPacket lighterGrenade;
    lighterGrenade.command.sequence = 1;
    lighterGrenade.requestMovementTuning = true;
    lighterGrenade.weaponDamage.rocketLauncherDamage = 50;
    transport.sendCommand(lighterGrenade);
    server.tick(lg::kFixedTickSeconds);
    latestSnapshot(transport);

    lg::UserCommand grenade;
    grenade.sequence = 2;
    grenade.attack = true;
    grenade.weapon = lg::Weapon::GrenadeLauncher;
    transport.sendCommand(lg::CommandPacket{0, grenade, false});
    bool exploded = false;
    lg::ServerSnapshot snapshot;
    for (int tick = 0; tick < 220; ++tick) {
      server.tick(lg::kFixedTickSeconds);
      snapshot = latestSnapshot(transport);
      exploded = exploded || snapshot.rocketExplosions[0].active;
      if (exploded && snapshot.players[1].health < 100) {
        break;
      }
    }
    failures += expect(exploded, "grenade should eventually explode on direct contact or fuse");
    failures += expect(
      snapshot.rocketExplosions[0].weapon == lg::Weapon::GrenadeLauncher &&
        snapshot.rocketExplosions[0].opponentDamageApplied > 0 &&
        snapshot.players[1].health < 100,
      "grenade explosion should apply and report opponent damage"
    );
    failures += expect(
      std::hypot(
        snapshot.players[1].velocity.x,
        snapshot.players[1].velocity.y,
        snapshot.players[1].velocity.z
      ) > 0.1F,
      "grenade explosion should apply knockback"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    lg::Arena arena;
    arena.min = {-120.0F, -20.0F, 0.0F};
    arena.max = {120.0F, 20.0F, 60.0F};
    arena.spawnPositions[0] = {-50.0F, 0.0F, 0.5F};
    arena.spawnPositions[1] = {50.0F, 0.0F, 0.5F};
    server.setArena(arena);
    latestSnapshot(transport);

    lg::UserCommand grenade;
    grenade.sequence = 1;
    grenade.attack = true;
    grenade.weapon = lg::Weapon::GrenadeLauncher;
    grenade.planarAim = false;
    grenade.viewPitchRadians = 0.25F;
    transport.sendCommand(lg::CommandPacket{0, grenade, false});
    bool expired = false;
    lg::ServerSnapshot snapshot;
    for (int tick = 0; tick < 340; ++tick) {
      server.tick(lg::kFixedTickSeconds);
      snapshot = latestSnapshot(transport);
      if (snapshot.rocketExplosions[0].active) {
        expired = true;
        break;
      }
    }
    failures += expect(
      expired &&
        snapshot.rocketExplosions[0].weapon == lg::Weapon::GrenadeLauncher,
      "grenade should expire through its authoritative fuse"
    );
    failures += expect(
      snapshot.players[1].health == 100,
      "long-range fuse expiration should not require direct opponent contact"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);

    for (std::uint32_t sequence = 0; sequence < 2; ++sequence) {
      lg::UserCommand firstCommand;
      firstCommand.sequence = sequence;
      firstCommand.attack = true;
      lg::UserCommand secondCommand = firstCommand;
      secondCommand.viewYawRadians = kPi;
      transport.sendCommand(lg::CommandPacket{0, firstCommand, false});
      transport.sendCommand(lg::CommandPacket{1, secondCommand, false});
      server.tick(lg::kFixedTickSeconds);
    }

    const lg::ServerSnapshot snapshot = latestSnapshot(transport);
    failures += expect(snapshot.players[0].health == 99, "player one beam should apply fixed-tick damage");
    failures += expect(snapshot.players[1].health == 99, "simultaneous beams should apply symmetrically");
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    server.setConnectedPlayers({true, true, true});
    latestSnapshot(transport);

    for (std::uint8_t playerIndex = 0; playerIndex < 3; ++playerIndex) {
      lg::CommandPacket ready;
      ready.playerIndex = playerIndex;
      ready.command.sequence = 1;
      ready.toggleReady = true;
      transport.sendCommand(ready);
      server.tick(lg::kFixedTickSeconds);
    }

    const lg::ServerSnapshot snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.matchPhase == lg::MatchPhase::WaitingForPlayers,
      "duel should not start when more than two players are connected"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);

    lg::MatchRules rules;
    rules.roundLimit = 2;
    rules.countdownTicks = 2;
    rules.roundEndTicks = 2;
    rules.matchEndTicks = 3;
    server.setMatchRules(rules);
    server.setConnectedPlayers({true, false});
    server.setBotDodge(true, 1, 1);
    const lg::Vec3 botStart = server.snapshot().players[1].position;
    server.tick(lg::kFixedTickSeconds);
    lg::ServerSnapshot snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.matchPhase == lg::MatchPhase::WaitingForPlayers,
      "one connected player should remain in the lobby"
    );
    failures += expect(
      snapshot.connectedPlayers[0] && !snapshot.connectedPlayers[1] &&
        !snapshot.connectedPlayers[2] && !snapshot.connectedPlayers[3] &&
        !snapshot.connectedPlayers[4] && !snapshot.connectedPlayers[5],
      "snapshot should replicate occupied player slots"
    );
    failures += expect(
      snapshot.participatingPlayers[0] && snapshot.participatingPlayers[1] &&
        snapshot.participatingPlayers[2] && snapshot.participatingPlayers[3] &&
        snapshot.participatingPlayers[4] && snapshot.participatingPlayers[5],
      "enabled dodge bots should join the authoritative player roster"
    );
    server.setBotDodge(false, 1, 1);
    failures += expect(
      server.snapshot().participatingPlayers ==
        std::array<bool, lg::kDuelPlayerCount>{true},
      "disabled dodge bots should leave the authoritative player roster"
    );
    server.setBotDodge(true, 1, 1);
    failures += expect(snapshot.playerNames[1] == "BOT", "empty warmup opponent should be named BOT");
    for (int tick = 0; tick < 20; ++tick) {
      server.tick(lg::kFixedTickSeconds);
      snapshot = latestSnapshot(transport);
    }
    const bool botMoved =
      snapshot.players[1].position.x != botStart.x ||
      snapshot.players[1].position.y != botStart.y;
    failures += expect(botMoved, "bot_dodge should move the empty warmup opponent");
    const int botHealthBeforeShot = snapshot.players[1].health;
    lg::UserCommand soloWarmupAttack;
    soloWarmupAttack.sequence = 0;
    soloWarmupAttack.attack = true;
    const lg::Vec3 botOffset = snapshot.players[1].position - snapshot.players[0].position;
    soloWarmupAttack.viewYawRadians = std::atan2(botOffset.y, botOffset.x);
    soloWarmupAttack.planarAim = true;
    transport.sendCommand(lg::CommandPacket{0, soloWarmupAttack, false});
    server.tick(lg::kFixedTickSeconds);
    snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.lightningGuns[0].active,
      "a connected player should be able to fire during solo warmup"
    );
    soloWarmupAttack.sequence = 1;
    transport.sendCommand(lg::CommandPacket{0, soloWarmupAttack, false});
    server.tick(lg::kFixedTickSeconds);
    snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.lightningGuns[0].hit &&
        snapshot.lightningGuns[0].targetPlayerIndex == 1 &&
        snapshot.players[1].health < botHealthBeforeShot,
      "bot_dodge targets should have authoritative hitboxes"
    );

    server.setConnectedPlayers({true, true});
    server.tick(lg::kFixedTickSeconds);
    snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.matchPhase == lg::MatchPhase::WaitingForReady,
      "two connected players should wait for ready-up"
    );

    lg::UserCommand warmupAttack;
    warmupAttack.sequence = 1;
    warmupAttack.attack = true;
    transport.sendCommand(lg::CommandPacket{0, warmupAttack, false});
    server.tick(lg::kFixedTickSeconds);
    warmupAttack.sequence = 2;
    transport.sendCommand(lg::CommandPacket{0, warmupAttack, false});
    server.tick(lg::kFixedTickSeconds);
    snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.lightningGuns[0].active &&
        snapshot.players[1].health < 100,
      "connected players should be able to shoot during warmup"
    );
    failures += expect(
      snapshot.scores[0] == 0 && snapshot.scores[1] == 0,
      "warmup combat should not affect match score"
    );

    lg::UserCommand firstReady;
    firstReady.sequence = 3;
    transport.sendCommand(lg::CommandPacket{0, firstReady, false, true, 0});
    server.tick(lg::kFixedTickSeconds);
    snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.readyPlayers[0] && !snapshot.readyPlayers[1],
      "first ready request should only ready its player"
    );

    lg::UserCommand secondReady;
    secondReady.sequence = 1;
    transport.sendCommand(lg::CommandPacket{1, secondReady, false, true, 0});
    server.tick(lg::kFixedTickSeconds);
    snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.matchPhase == lg::MatchPhase::Countdown &&
        snapshot.phaseTicksRemaining == 2,
      "all ready players should begin the configured countdown"
    );

    lg::UserCommand countdownCommand;
    countdownCommand.sequence = 4;
    countdownCommand.forwardMove = 1.0F;
    countdownCommand.attack = true;
    transport.sendCommand(lg::CommandPacket{0, countdownCommand, false, false, 0});
    server.tick(lg::kFixedTickSeconds);
    snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.players[0].position.x > -8.0F,
      "players should be able to move during countdown"
    );
    failures += expect(
      !snapshot.lightningGuns[0].active && snapshot.players[1].health == 100,
      "weapons should remain locked during countdown"
    );

    server.tick(lg::kFixedTickSeconds);
    snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.matchPhase == lg::MatchPhase::Live,
      "countdown expiry should unlock live play"
    );

    lg::CommandPacket noKnockback;
    noKnockback.command.sequence = 5;
    noKnockback.requestMovementTuning = true;
    noKnockback.lightningKnockback = 0.0F;
    transport.sendCommand(noKnockback);
    server.tick(lg::kFixedTickSeconds);
    snapshot = latestSnapshot(transport);

    std::uint32_t lastAttackSequence = 0;
    for (std::uint32_t sequence = 0; sequence < 200; ++sequence) {
      lg::UserCommand command;
      command.sequence = sequence + 6;
      command.clientTick = sequence;
      command.attack = true;
      transport.sendCommand(lg::CommandPacket{0, command, false});
      server.tick(lg::kFixedTickSeconds);
      snapshot = latestSnapshot(transport);
      lastAttackSequence = sequence;
      if (snapshot.players[1].health == 0) {
        break;
      }
    }

    failures += expect(snapshot.players[1].health == 0, "authoritative LG should kill the target");
    failures += expect(
      snapshot.acknowledgedCommand[0] == lastAttackSequence + 6,
      "server should ack latest combat command"
    );
    failures += expect(
      snapshot.scores[0] == 1 &&
        snapshot.matchPhase == lg::MatchPhase::RoundEnd &&
        snapshot.roundWinner == 0,
      "non-final kill should score and enter round end"
    );
    failures += expect(
      snapshot.fragEvents[0].active &&
        snapshot.fragEvents[0].targetPlayerIndex == 1,
      "authoritative duel kill should emit a frag event for the killer"
    );
    failures += expect(
      snapshot.roundCombatStats[0].lightningActiveTicks > 0 &&
        snapshot.roundCombatStats[0].lightningHitTicks > 0 &&
        snapshot.roundCombatStats[0].lightningHitTicks <=
          snapshot.roundCombatStats[0].lightningActiveTicks &&
        snapshot.roundCombatStats[0].damageDealt == 100,
      "round stats should record authoritative LG contact and damage"
    );
    const lg::RoundCombatStats firstRoundAggregate =
      snapshot.matchCombatStats[0];
    failures += expect(
      firstRoundAggregate.lightningActiveTicks > 0 &&
        firstRoundAggregate.lightningHitTicks > 0 &&
        firstRoundAggregate.damageDealt == 100,
      "match scoreboard stats should aggregate authoritative combat"
    );

    lg::UserCommand deadTargetCommand;
    deadTargetCommand.sequence = 1;
    deadTargetCommand.viewYawRadians = kPi;
    deadTargetCommand.attack = true;
    transport.sendCommand(lg::CommandPacket{1, deadTargetCommand, false});
    server.tick(lg::kFixedTickSeconds);
    snapshot = latestSnapshot(transport);
    failures += expect(!snapshot.lightningGuns[1].active, "weapons should be locked after round end");

    server.tick(lg::kFixedTickSeconds);
    snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.matchPhase == lg::MatchPhase::Countdown &&
        snapshot.scores[0] == 1 &&
        snapshot.scores[1] == 0 &&
        snapshot.players[0].health == 100 &&
        snapshot.players[1].health == 100 &&
        snapshot.roundCombatStats[0].lightningActiveTicks == 0 &&
        snapshot.roundCombatStats[0].lightningHitTicks == 0 &&
        snapshot.roundCombatStats[0].damageDealt == 0,
      "round-end expiry should respawn both players into a new countdown"
    );
    failures += expect(
      snapshot.matchCombatStats[0].lightningActiveTicks ==
          firstRoundAggregate.lightningActiveTicks &&
        snapshot.matchCombatStats[0].lightningHitTicks ==
          firstRoundAggregate.lightningHitTicks &&
        snapshot.matchCombatStats[0].damageDealt ==
          firstRoundAggregate.damageDealt,
      "scoreboard aggregate stats should survive round transitions"
    );

    server.tick(lg::kFixedTickSeconds);
    server.tick(lg::kFixedTickSeconds);
    snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.matchPhase == lg::MatchPhase::Live,
      "new round countdown should return to live play"
    );

    std::uint32_t secondRoundSequence = lastAttackSequence + 3;
    for (int tick = 0; tick < 200; ++tick) {
      lg::UserCommand command;
      command.sequence = secondRoundSequence++;
      command.attack = true;
      transport.sendCommand(lg::CommandPacket{0, command, false});
      server.tick(lg::kFixedTickSeconds);
      snapshot = latestSnapshot(transport);
      if (snapshot.matchPhase == lg::MatchPhase::MatchEnd) {
        break;
      }
    }
    failures += expect(
      snapshot.scores[0] == 2 &&
        snapshot.matchPhase == lg::MatchPhase::MatchEnd &&
        snapshot.matchWinner == 0,
      "configured round limit should end the match"
    );

    server.tick(lg::kFixedTickSeconds);
    server.tick(lg::kFixedTickSeconds);
    server.tick(lg::kFixedTickSeconds);
    snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.matchPhase == lg::MatchPhase::WaitingForReady &&
        snapshot.scores[0] == 0 &&
        snapshot.scores[1] == 0 &&
        snapshot.players[0].health == 100 &&
        snapshot.players[1].health == 100,
      "match-end expiry should reset scores, readiness, and both spawns"
    );

    lg::UserCommand resetCommand;
    resetCommand.sequence = secondRoundSequence;
    transport.sendCommand(lg::CommandPacket{0, resetCommand, true});
    const std::uint32_t tickBeforeReset = snapshot.serverTick;
    server.tick(lg::kFixedTickSeconds);
    snapshot = latestSnapshot(transport);

    failures += expect(snapshot.players[0].health == 100, "reset should restore local health");
    failures += expect(snapshot.players[1].health == 100, "reset should restore remote health");
    failures += expect(snapshot.players[0].position.x == -8.0F, "reset should restore local spawn");
    failures += expect(snapshot.players[1].position.x == 8.0F, "reset should restore remote spawn");
    failures += expect(
      snapshot.acknowledgedCommand[0] == resetCommand.sequence,
      "reset command should be acknowledged"
    );
    failures += expect(
      snapshot.serverTick == tickBeforeReset + 1,
      "match reset should not rewind server tick"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);

    lg::UserCommand rail;
    rail.sequence = 1;
    rail.attack = true;
    rail.weapon = lg::Weapon::Railgun;
    transport.sendCommand(lg::CommandPacket{0, rail, false});
    server.tick(lg::kFixedTickSeconds);
    lg::ServerSnapshot snapshot = latestSnapshot(transport);
    failures += expect(snapshot.weaponFires[0].fired, "railgun command should fire a weapon event");
    failures += expect(snapshot.weaponFires[0].hit, "railgun should hit the spawned opponent");
    failures += expect(snapshot.players[1].health == 20, "railgun should apply 80 damage");
    failures += expect(!snapshot.lightningGuns[0].active, "railgun should not also emit LG state");

    rail.sequence = 2;
    transport.sendCommand(lg::CommandPacket{0, rail, false});
    server.tick(lg::kFixedTickSeconds);
    snapshot = latestSnapshot(transport);
    failures += expect(snapshot.players[1].health == 20, "railgun cooldown should block immediate damage");
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);

    lg::UserCommand machineGun;
    machineGun.sequence = 1;
    machineGun.attack = true;
    machineGun.weapon = lg::Weapon::MachineGun;
    transport.sendCommand(lg::CommandPacket{0, machineGun, false});
    server.tick(lg::kFixedTickSeconds);
    lg::ServerSnapshot machineGunSnapshot = latestSnapshot(transport);
    failures += expect(machineGunSnapshot.weaponFires[0].fired, "machine gun command should fire a weapon event");
    failures += expect(machineGunSnapshot.weaponFires[0].hit, "machine gun should hit the spawned opponent");
    failures += expect(
      machineGunSnapshot.weaponFires[0].weapon == lg::Weapon::MachineGun,
      "machine gun event should replicate its selected weapon"
    );
    failures += expect(machineGunSnapshot.players[1].health == 95, "machine gun should apply 5 damage");
    failures += expect(!machineGunSnapshot.lightningGuns[0].active, "machine gun should not also emit LG state");

    machineGun.sequence = 2;
    transport.sendCommand(lg::CommandPacket{0, machineGun, false});
    server.tick(lg::kFixedTickSeconds);
    machineGunSnapshot = latestSnapshot(transport);
    failures += expect(
      machineGunSnapshot.players[1].health == 95,
      "machine gun cooldown should block immediate damage"
    );

    for (int tick = 0; tick < 12; ++tick) {
      server.tick(lg::kFixedTickSeconds);
      machineGunSnapshot = latestSnapshot(transport);
    }
    failures += expect(
      machineGunSnapshot.weaponFires[0].weapon == lg::Weapon::MachineGun &&
        machineGunSnapshot.players[1].health == 90,
      "machine gun should fire again after its fixed-tick cooldown"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);

    lg::UserCommand shotgun;
    shotgun.sequence = 1;
    shotgun.attack = true;
    shotgun.weapon = lg::Weapon::Shotgun;
    transport.sendCommand(lg::CommandPacket{0, shotgun, false});
    server.tick(lg::kFixedTickSeconds);
    lg::ServerSnapshot snapshot = latestSnapshot(transport);
    failures += expect(snapshot.weaponFires[0].fired, "shotgun command should fire a weapon event");
    failures += expect(snapshot.weaponFires[0].hit, "shotgun should hit the spawned opponent");
    failures += expect(
      snapshot.weaponFires[0].weapon == lg::Weapon::Shotgun,
      "shotgun event should replicate its selected weapon"
    );
    failures += expect(
      snapshot.weaponFires[0].pelletCount == lg::kShotgunPelletCount &&
        snapshot.weaponFires[0].pelletHitCount > 0 &&
        snapshot.weaponFires[0].pelletHitCount < snapshot.weaponFires[0].pelletCount,
      "spawn-range shotgun should replicate partial pellet hits"
    );
    failures += expect(
      snapshot.weaponFires[0].damageApplied ==
        static_cast<int>(snapshot.weaponFires[0].pelletHitCount) * 5,
      "shotgun event should report pellet-scaled damage"
    );
    const int healthAfterFirstShot = snapshot.players[1].health;
    failures += expect(
      healthAfterFirstShot == 100 - snapshot.weaponFires[0].damageApplied,
      "shotgun should apply authoritative damage"
    );
    failures += expect(!snapshot.lightningGuns[0].active, "shotgun should not also emit LG state");

    shotgun.sequence = 2;
    transport.sendCommand(lg::CommandPacket{0, shotgun, false});
    server.tick(lg::kFixedTickSeconds);
    snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.players[1].health == healthAfterFirstShot,
      "shotgun cooldown should block immediate damage"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);

    lg::UserCommand moveForward;
    moveForward.sequence = 1;
    moveForward.forwardMove = 1.0F;
    moveForward.weapon = lg::Weapon::RocketLauncher;
    transport.sendCommand(lg::CommandPacket{0, moveForward, false});
    for (int tick = 0; tick < 20; ++tick) {
      server.tick(lg::kFixedTickSeconds);
      latestSnapshot(transport);
    }

    moveForward.sequence = 2;
    moveForward.attack = true;
    transport.sendCommand(lg::CommandPacket{0, moveForward, false});
    server.tick(lg::kFixedTickSeconds);
    lg::ServerSnapshot snapshot = latestSnapshot(transport);
    failures += expect(snapshot.weaponFires[0].fired, "moving player should fire a rocket");

    bool explodedAgainstOwner = false;
    for (int tick = 0; tick < 8; ++tick) {
      server.tick(lg::kFixedTickSeconds);
      snapshot = latestSnapshot(transport);
      explodedAgainstOwner =
        explodedAgainstOwner || snapshot.rocketExplosions[0].active;
    }
    failures += expect(
      !explodedAgainstOwner,
      "forward momentum should not make a rocket collide with its owner before separating"
    );
    failures += expect(
      snapshot.players[0].health == 100,
      "a forward-moving player should not take damage from an overlapping newly fired rocket"
    );
    failures += expect(
      snapshot.rockets[0].active,
      "rocket should remain active after separating from its forward-moving owner"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);

    lg::CommandPacket noSelfDamage;
    noSelfDamage.command.sequence = 1;
    noSelfDamage.requestMovementTuning = true;
    noSelfDamage.selfDamagePercent = 0;
    noSelfDamage.rocketKnockback = 1000.0F;
    transport.sendCommand(noSelfDamage);
    server.tick(lg::kFixedTickSeconds);
    lg::ServerSnapshot snapshot = latestSnapshot(transport);
    failures += expect(snapshot.selfDamagePercent == 0, "g_selfdamage 0 should replicate to the server");

    lg::UserCommand rocketDown;
    rocketDown.sequence = 2;
    rocketDown.attack = true;
    rocketDown.weapon = lg::Weapon::RocketLauncher;
    rocketDown.planarAim = false;
    rocketDown.viewPitchRadians = -kPi * 0.5F;
    transport.sendCommand(lg::CommandPacket{0, rocketDown, false});
    bool exploded = false;
    for (int tick = 0; tick < 220; ++tick) {
      server.tick(lg::kFixedTickSeconds);
      snapshot = latestSnapshot(transport);
      if (snapshot.rocketExplosions[0].active) {
        exploded = true;
        failures += expect(
          snapshot.rocketExplosions[0].ownerDamageApplied == 0,
          "g_selfdamage 0 should report no owner damage"
        );
        failures += expect(
          snapshot.players[0].health == 100,
          "g_selfdamage 0 should prevent rocket self damage"
        );
        failures += expect(
          std::hypot(
            snapshot.players[0].velocity.x,
            snapshot.players[0].velocity.y,
            snapshot.players[0].velocity.z
          ) > 21.9F &&
            std::hypot(snapshot.players[0].velocity.x, snapshot.players[0].velocity.y) < 0.1F &&
            snapshot.players[0].velocity.z < 22.1F,
          "g_rl_knockback 1000 should use the Q3-relative internal impulse"
        );
        break;
      }
    }
    failures += expect(exploded, "downward rocket should explode near its owner");
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);

    lg::UserCommand rocket;
    rocket.sequence = 1;
    rocket.attack = true;
    rocket.weapon = lg::Weapon::RocketLauncher;
    transport.sendCommand(lg::CommandPacket{0, rocket, false});
    server.tick(lg::kFixedTickSeconds);
    lg::ServerSnapshot snapshot = latestSnapshot(transport);
    failures += expect(snapshot.weaponFires[0].fired, "rocket launcher should fire a weapon event");
    failures += expect(snapshot.rockets[0].active, "rocket projectile should replicate after firing");

    bool exploded = false;
    bool damaged = false;
    for (int tick = 0; tick < 160; ++tick) {
      server.tick(lg::kFixedTickSeconds);
      snapshot = latestSnapshot(transport);
      exploded = exploded || snapshot.rocketExplosions[0].active;
      damaged = damaged || snapshot.players[1].health < 100;
      if (exploded && damaged) {
        failures += expect(
          snapshot.rocketExplosions[0].opponentDamageApplied > 0,
          "rocket explosion should report opponent damage for audio feedback"
        );
        break;
      }
    }
    failures += expect(exploded, "rocket should eventually explode");
    failures += expect(damaged, "rocket explosion should damage the opponent");
  }

  return failures == 0 ? 0 : 1;
}
