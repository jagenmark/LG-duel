#include "net/NetCodec.hpp"
#include "sim/MovementModes.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) {
    return 0;
  }

  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

bool nearlyEqual(float lhs, float rhs, float epsilon = 0.0001F) {
  return std::fabs(lhs - rhs) <= epsilon;
}

} // namespace

int main() {
  int failures = 0;

  {
    lg::WirePacket wire;
    lg::PacketType type;
    lg::ConnectRequest request{12345};
    lg::ConnectRequest decodedRequest;
    failures += expect(lg::encodeConnectRequest(request, wire), "connect request should encode");
    failures += expect(lg::inspectPacketType(wire, type), "connect packet type should inspect");
    failures += expect(type == lg::PacketType::ConnectRequest, "connect request type should match");
    failures += expect(lg::decodeConnectRequest(wire, decodedRequest), "connect request should decode");
    failures += expect(decodedRequest.clientNonce == 12345, "connect nonce should round trip");

    lg::ConnectAccept accept{12345, 1, 77};
    lg::ConnectAccept decodedAccept;
    failures += expect(lg::encodeConnectAccept(accept, wire), "connect accept should encode");
    failures += expect(lg::decodeConnectAccept(wire, decodedAccept), "connect accept should decode");
    failures += expect(decodedAccept.playerIndex == 1, "assigned player should round trip");
    failures += expect(decodedAccept.serverTick == 77, "accept server tick should round trip");

    lg::PingPacket ping{88};
    lg::PingPacket decodedPing;
    failures += expect(
      lg::encodePingPacket(lg::PacketType::Ping, ping, wire),
      "ping should encode"
    );
    failures += expect(
      lg::decodePingPacket(wire, lg::PacketType::Ping, decodedPing),
      "ping should decode"
    );
    failures += expect(decodedPing.token == 88, "ping token should round trip");
  }

  {
    lg::CommandPacket source;
    source.playerIndex = 1;
    source.clientNonce = 12345;
    source.command.sequence = 42;
    source.command.clientTick = 99;
    source.command.viewYawRadians = 1.25F;
    source.command.viewPitchRadians = -0.25F;
    source.command.forwardMove = 1.0F;
    source.command.rightMove = -0.5F;
    source.command.upMove = 0.75F;
    source.command.attack = true;
    source.command.jump = true;
    source.command.planarAim = false;
    source.command.weapon = lg::Weapon::PlasmaGun;
    source.requestReset = true;
    source.toggleReady = true;
    source.requestGameMode = true;
    source.requestedGameMode = lg::GameMode::ClanArena;
    source.requestTeam = true;
    source.requestedTeam = lg::Team::Blue;
    source.requestMovementTuning = true;
    source.movementTuning.flightEnabled = true;
    source.movementTuning.airControlEnabled = true;
    source.movementTuning.groundAcceleration = 120.0F;
    source.movementTuning.airAcceleration = 2.0F;
    source.movementTuning.groundFriction = 6.0F;
    source.movementTuning.stopSpeed = 2.5F;
    source.movementTuning.maxGroundSpeed = 14.0F;
    source.movementTuning.flightAcceleration = 48.0F;
    source.movementTuning.maxFlightSpeed = 16.0F;
    source.movementTuning.flightDamping = 1.5F;
    source.movementTuning.flightGravityCancel = 1.0F;
    source.playerSizeScaleXY = 1.75F;
    source.playerSizeScaleZ = 0.75F;
    source.lightningKnockback = 1500.0F;
    source.rocketKnockback = 625.0F;
    source.weaponDamage.shotgunDamagePerPellet = 7;
    source.weaponDamage.machineGunDamage = 9;
    source.weaponDamage.lightningGunDamage = 111;
    source.weaponDamage.railgunDamage = 50;
    source.weaponDamage.rocketLauncherDamage = 125;
    source.vampirism = 0.1F;
    source.selfDamagePercent = 37;
    source.healthAmount = 175;
    source.botDodgeEnabled = true;
    source.botDodgeMinIntervalMs = 250;
    source.botDodgeMaxIntervalMs = 750;
    source.chatMessage = "ready?";
    source.playerName = "yg";
    source.mapName = "thunderstruck";
    source.viewedServerTick = 88;

    lg::WirePacket wire;
    lg::CommandPacket decoded;
    failures += expect(lg::encodeCommandPacket(source, wire), "command should encode");
    failures += expect(wire.size() <= lg::kMaxPacketBytes, "command should respect packet limit");
    failures += expect(lg::decodeCommandPacket(wire, decoded), "command should decode");
    failures += expect(decoded.playerIndex == source.playerIndex, "command player should round trip");
    failures += expect(decoded.clientNonce == 12345, "command nonce should round trip");
    failures += expect(decoded.command.sequence == 42, "command sequence should round trip");
    failures += expect(decoded.command.clientTick == 99, "command tick should round trip");
    failures += expect(decoded.viewedServerTick == 88, "viewed server tick should round trip");
    failures += expect(
      nearlyEqual(decoded.command.viewPitchRadians, -0.25F),
      "command pitch should round trip"
    );
    failures += expect(decoded.command.attack && decoded.command.jump, "command bits should round trip");
    failures += expect(!decoded.command.planarAim, "command aim dimensionality should round trip");
    failures += expect(decoded.command.weapon == lg::Weapon::PlasmaGun, "weapon selection should round trip");
    failures += expect(decoded.chatMessage == "ready?", "chat message should round trip");
    failures += expect(decoded.playerName == "yg", "player name should round trip");
    failures += expect(decoded.mapName == "thunderstruck", "map name should round trip");
    failures += expect(decoded.requestReset, "reset bit should round trip");
    failures += expect(decoded.toggleReady, "ready bit should round trip");
    failures += expect(
      decoded.requestGameMode &&
        decoded.requestedGameMode == lg::GameMode::ClanArena,
      "explicit gamemode request should round trip"
    );
    failures += expect(
      decoded.requestTeam && decoded.requestedTeam == lg::Team::Blue,
      "explicit team request should round trip"
    );
    failures += expect(
      decoded.requestMovementTuning &&
        decoded.movementTuning.flightEnabled &&
        decoded.movementTuning.airControlEnabled &&
        nearlyEqual(decoded.movementTuning.groundAcceleration, 120.0F) &&
        nearlyEqual(decoded.movementTuning.airAcceleration, 2.0F) &&
        nearlyEqual(decoded.movementTuning.groundFriction, 6.0F) &&
        nearlyEqual(decoded.movementTuning.stopSpeed, 2.5F) &&
        nearlyEqual(decoded.movementTuning.maxGroundSpeed, 14.0F) &&
        nearlyEqual(decoded.movementTuning.flightAcceleration, 48.0F) &&
        nearlyEqual(decoded.movementTuning.maxFlightSpeed, 16.0F) &&
        nearlyEqual(decoded.movementTuning.flightDamping, 1.5F) &&
        nearlyEqual(decoded.movementTuning.flightGravityCancel, 1.0F) &&
        nearlyEqual(decoded.playerSizeScaleXY, 1.75F) &&
        nearlyEqual(decoded.playerSizeScaleZ, 0.75F) &&
        nearlyEqual(decoded.lightningKnockback, 1500.0F) &&
        nearlyEqual(decoded.rocketKnockback, 625.0F) &&
        decoded.weaponDamage.shotgunDamagePerPellet == 7 &&
        decoded.weaponDamage.machineGunDamage == 9 &&
        decoded.weaponDamage.lightningGunDamage == 111 &&
        decoded.weaponDamage.railgunDamage == 50 &&
        decoded.weaponDamage.rocketLauncherDamage == 125 &&
        nearlyEqual(decoded.vampirism, 0.1F) &&
        decoded.selfDamagePercent == 37 &&
        decoded.healthAmount == 175 &&
        decoded.botDodgeEnabled &&
        decoded.botDodgeMinIntervalMs == 250 &&
        decoded.botDodgeMaxIntervalMs == 750,
      "movement tuning request should round trip"
    );

    lg::WirePacket wrongVersion = wire;
    wrongVersion[4] = 1;
    failures += expect(
      !lg::decodeCommandPacket(wrongVersion, decoded),
      "wrong protocol version should be rejected"
    );

    lg::WirePacket truncated = wire;
    truncated.pop_back();
    failures += expect(!lg::decodeCommandPacket(truncated, decoded), "truncated command should be rejected");

    lg::WirePacket wrongType = wire;
    wrongType[6] = static_cast<std::uint8_t>(lg::PacketType::Snapshot);
    failures += expect(!lg::decodeCommandPacket(wrongType, decoded), "wrong packet type should be rejected");

    lg::WirePacket invalidModeWire = wire;
    invalidModeWire[invalidModeWire.size() - 3U] = 255;
    failures += expect(
      !lg::decodeCommandPacket(invalidModeWire, decoded),
      "invalid requested gamemode should be rejected while decoding"
    );

    lg::CommandPacket invalidMode = source;
    invalidMode.requestedGameMode = static_cast<lg::GameMode>(255);
    failures += expect(
      !lg::encodeCommandPacket(invalidMode, wire),
      "invalid requested gamemode should not encode"
    );

    lg::CommandPacket invalidTeam = source;
    invalidTeam.requestedTeam = static_cast<lg::Team>(255);
    failures += expect(
      !lg::encodeCommandPacket(invalidTeam, wire),
      "invalid requested team should not encode"
    );

    lg::CommandPacket invalidMovement = source;
    invalidMovement.command.forwardMove = 1.1F;
    failures += expect(lg::encodeCommandPacket(invalidMovement, wire), "finite command should encode");
    failures += expect(
      !lg::decodeCommandPacket(wire, decoded),
      "out-of-range movement input should be rejected"
    );

    lg::CommandBundle bundle;
    bundle.commandCount = 3;
    bundle.commands[0] = source;
    bundle.commands[1] = source;
    bundle.commands[1].command.sequence = 43;
    bundle.commands[2] = source;
    bundle.commands[2].command.sequence = 44;
    lg::CommandBundle decodedBundle;
    failures += expect(lg::encodeCommandBundle(bundle, wire), "command bundle should encode");
    failures += expect(lg::decodeCommandBundle(wire, decodedBundle), "command bundle should decode");
    failures += expect(decodedBundle.commandCount == 3, "bundle count should round trip");
    failures += expect(
      decodedBundle.commands[2].command.sequence == 44,
      "bundle command order should round trip"
    );
    failures += expect(
      decodedBundle.commands[0].requestedGameMode == lg::GameMode::ClanArena &&
        decodedBundle.commands[2].requestedTeam == lg::Team::Blue,
      "redundant command bundles should preserve explicit mode and team requests"
    );
    for (lg::CommandPacket& command : bundle.commands) {
      command.chatMessage.assign(lg::kMaxChatMessageBytes, 'c');
      command.playerName.assign(lg::kMaxPlayerNameBytes, 'n');
      command.mapName.assign(lg::kMaxMapNameBytes, 'm');
    }
    failures += expect(
      lg::encodeCommandBundle(bundle, wire) &&
        wire.size() <= lg::kMaxPacketBytes,
      "maximum chat and player names should fit a redundant command bundle"
    );
  }

  {
    lg::ServerSnapshot source;
    source.serverTick = 1234;
    source.mapRevision = 77;
    source.arena.min = {-20.0F, -10.0F, 0.0F};
    source.arena.max = {20.0F, 10.0F, 12.0F};
    source.arena.wallCount = 1;
    source.arena.walls[0] = {{-1.0F, -2.0F, 0.0F}, {1.0F, 2.0F, 3.0F}};
    source.arena.spawnPositions[0] = {-8.0F, 0.0F, 0.0F};
    source.arena.spawnPositions[1] = {8.0F, 0.0F, 0.0F};
    source.acknowledgedCommand = {12, 34};
    source.hasAcknowledgedCommand = {true, false};
    source.players[0].position = {1.0F, 2.0F, 3.0F};
    source.players[0].velocity = {-1.0F, 0.5F, 4.0F};
    source.players[0].viewYawRadians = 0.75F;
    source.players[0].viewPitchRadians = -0.1F;
    source.players[0].health = 81;
    source.players[0].movementMode = lg::MovementMode::Flying;
    source.players[0].onGround = false;
    source.players[0].jumpHeld = true;
    source.players[1].health = 0;
    source.lightningGuns[0].active = true;
    source.lightningGuns[0].hit = true;
    source.lightningGuns[0].targetPlayerIndex = 1;
    source.lightningGuns[0].damageApplied = 2;
    source.lightningGuns[0].start = {1.0F, 2.0F, 3.0F};
    source.lightningGuns[0].end = {5.0F, 6.0F, 7.0F};
    source.lightningGuns[0].knockbackImpulse = {0.1F, 0.2F, 0.3F};
    source.lightningGuns[0].requestedRewindTicks = 20;
    source.lightningGuns[0].appliedRewindTicks = 18;
    source.lightningGuns[0].rewindClamped = true;
    source.lightningGuns[0].hasRewindDebug = true;
    source.lightningGuns[0].rewindTargetTick = 1216;
    source.lightningGuns[0].currentTargetPosition = {8.0F, 3.0F, 4.0F};
    source.lightningGuns[0].rewoundTargetPosition = {8.0F, 0.0F, 2.0F};
    source.lightningGuns[0].currentTargetBounds = {0.5F, 1.1F};
    source.lightningGuns[0].rewoundTargetBounds = {0.4F, 0.9F};
    source.weaponFires[0].fired = true;
    source.weaponFires[0].hit = true;
    source.weaponFires[0].weapon = lg::Weapon::Railgun;
    source.weaponFires[0].damageApplied = 80;
    source.weaponFires[0].start = {1.0F, 1.5F, 2.0F};
    source.weaponFires[0].end = {9.0F, 1.5F, 2.0F};
    source.weaponFires[0].knockbackImpulse = {2.0F, 0.0F, 0.0F};
    source.weaponFires[1].fired = true;
    source.weaponFires[1].hit = true;
    source.weaponFires[1].weapon = lg::Weapon::Shotgun;
    source.weaponFires[1].damageApplied = 45;
    source.weaponFires[1].start = {1.0F, -1.5F, 2.0F};
    source.weaponFires[1].end = {7.0F, -1.5F, 2.0F};
    source.weaponFires[1].knockbackImpulse = {1.0F, 0.0F, 0.0F};
    source.weaponFires[1].pelletCount = lg::kShotgunPelletCount;
    source.weaponFires[1].pelletHitCount = 9;
    source.weaponFires[2].fired = true;
    source.weaponFires[2].hit = true;
    source.weaponFires[2].weapon = lg::Weapon::MachineGun;
    source.weaponFires[2].damageApplied = 5;
    source.weaponFires[2].start = {2.0F, 1.5F, 2.0F};
    source.weaponFires[2].end = {8.0F, 1.5F, 2.0F};
    source.weaponFires[2].knockbackImpulse = {0.11F, 0.0F, 0.0F};
    source.rocketExplosions[0].active = true;
    source.rocketExplosions[0].weapon = lg::Weapon::GrenadeLauncher;
    source.rocketExplosions[0].position = {3.0F, 4.0F, 0.0F};
    source.rocketExplosions[0].radius = 3.0F;
    source.rocketExplosions[0].ownerDamageApplied = 12;
    source.rocketExplosions[0].opponentDamageApplied = 80;
    source.fragEvents[0].active = true;
    source.fragEvents[0].targetPlayerIndex = 1;
    source.footstepAudioEvents[1].active = true;
    source.footstepAudioEvents[1].sequence = 42;
    source.footstepAudioEvents[1].position = {2.5F, -1.0F, 1.5F};
    source.grenadeBounceAudioEvents[0].active = true;
    source.grenadeBounceAudioEvents[0].sequence = 9;
    source.grenadeBounceAudioEvents[0].position = {4.5F, -2.0F, 0.75F};
    source.rockets[0].active = true;
    source.rockets[0].owner = 1;
    source.rockets[0].weapon = lg::Weapon::GrenadeLauncher;
    source.rockets[0].position = {5.0F, 6.0F, 1.2F};
    source.rockets[0].velocity = {7.0F, 8.0F, 9.0F};
    source.respawnTicksRemaining = {0, 88};
    source.scores = {7, 4};
    source.gameMode = lg::GameMode::ClanArena;
    source.teams = {
      lg::Team::Red,
      lg::Team::Blue,
      lg::Team::None,
      lg::Team::None,
      lg::Team::None,
      lg::Team::None,
    };
    source.teamScores = {8, 6};
    source.roundWinningTeam = lg::Team::Red;
    source.matchWinningTeam = lg::Team::None;
    source.connectedPlayers = {true, true};
    source.participatingPlayers = {true, true, true};
    source.readyPlayers = {true, false};
    source.roundCombatStats[0] = {250, 125, 80};
    source.roundCombatStats[1] = {200, 40, 24};
    source.matchCombatStats[0] = {500, 275, 180};
    source.matchCombatStats[1] = {450, 90, 74};
    source.playerNames = {"yg", "opponent"};
    source.matchPhase = lg::MatchPhase::Countdown;
    source.matchRules.roundLimit = 10;
    source.matchRules.timeLimitMinutes = 5;
    source.matchRules.playerLimit = 2;
    source.matchRules.countdownTicks = 625;
    source.matchRules.roundEndTicks = 125;
    source.matchRules.matchEndTicks = 625;
    source.matchRules.showOpponentHealth = true;
    source.movementTuning.flightEnabled = true;
    source.movementTuning.airControlEnabled = true;
    source.movementTuning.groundAcceleration = 120.0F;
    source.movementTuning.airAcceleration = 2.0F;
    source.movementTuning.groundFriction = 6.0F;
    source.movementTuning.stopSpeed = 2.5F;
    source.movementTuning.maxGroundSpeed = 14.0F;
    source.movementTuning.flightAcceleration = 48.0F;
    source.movementTuning.maxFlightSpeed = 16.0F;
    source.movementTuning.flightDamping = 1.5F;
    source.movementTuning.flightGravityCancel = 1.0F;
    source.playerSizeScaleXY = 1.75F;
    source.playerSizeScaleZ = 0.75F;
    source.lightningKnockback = 1500.0F;
    source.rocketKnockback = 625.0F;
    source.weaponDamage.shotgunDamagePerPellet = 11;
    source.weaponDamage.machineGunDamage = 13;
    source.weaponDamage.lightningGunDamage = 90;
    source.weaponDamage.railgunDamage = 50;
    source.weaponDamage.rocketLauncherDamage = 140;
    source.vampirism = 2.0F;
    source.selfDamagePercent = 25;
    source.healthAmount = 150;
    source.botDodgeEnabled = true;
    source.botDodgeMinIntervalMs = 300;
    source.botDodgeMaxIntervalMs = 700;
    source.phaseTicksRemaining = 321;
    source.liveTicksElapsed = 900;
    source.roundWinner = 0;
    source.chatSequence = 7;
    source.chatPlayerIndex = 1;
    source.chatMessage = "nice shot";
    source.matchWinner = 255;
    source.playersColliding = true;

    lg::WirePacket wire;
    lg::ServerSnapshot decoded;
    failures += expect(lg::encodeServerSnapshot(source, wire), "snapshot should encode");
    failures += expect(wire.size() <= lg::kMaxPacketBytes, "snapshot should respect packet limit");
    failures += expect(lg::decodeServerSnapshot(wire, decoded), "snapshot should decode");
    failures += expect(decoded.serverTick == 1234, "snapshot tick should round trip");
    failures += expect(decoded.mapRevision == 77, "snapshot map revision should round trip");
    failures += expect(
      decoded.arena.wallCount == 1 &&
        nearlyEqual(decoded.arena.max.x, 20.0F) &&
        nearlyEqual(decoded.arena.walls[0].max.z, 3.0F) &&
        nearlyEqual(decoded.arena.spawnPositions[1].x, 8.0F),
      "snapshot arena should round trip"
    );
    failures += expect(decoded.acknowledgedCommand[0] == 12, "snapshot ack should round trip");
    failures += expect(
      decoded.players[0].movementMode == lg::MovementMode::Flying &&
        decoded.players[0].jumpHeld,
      "movement mode and jump latch should round trip"
    );
    failures += expect(nearlyEqual(decoded.players[0].position.z, 3.0F), "3D position should round trip");
    failures += expect(nearlyEqual(decoded.players[0].velocity.z, 4.0F), "3D velocity should round trip");
    failures += expect(decoded.players[1].health == 0, "death state should round trip");
    failures += expect(
      decoded.lightningGuns[0].hit &&
        decoded.lightningGuns[0].targetPlayerIndex == 1,
      "beam hit target should round trip"
    );
    failures += expect(
      decoded.lightningGuns[0].requestedRewindTicks == 20 &&
        decoded.lightningGuns[0].appliedRewindTicks == 18 &&
        decoded.lightningGuns[0].rewindClamped &&
        decoded.lightningGuns[0].hasRewindDebug &&
        decoded.lightningGuns[0].rewindTargetTick == 1216 &&
        nearlyEqual(decoded.lightningGuns[0].currentTargetPosition.z, 4.0F) &&
        nearlyEqual(decoded.lightningGuns[0].rewoundTargetPosition.y, 0.0F) &&
        nearlyEqual(decoded.lightningGuns[0].currentTargetBounds.radius, 0.5F) &&
        nearlyEqual(decoded.lightningGuns[0].rewoundTargetBounds.halfHeight, 0.9F),
      "rewind diagnostics should round trip"
    );
    failures += expect(
      nearlyEqual(decoded.lightningGuns[0].knockbackImpulse.z, 0.3F),
      "3D knockback should round trip"
    );
    failures += expect(
      decoded.weaponFires[0].fired &&
        decoded.weaponFires[0].hit &&
        decoded.weaponFires[0].weapon == lg::Weapon::Railgun &&
        decoded.weaponFires[0].damageApplied == 80 &&
        nearlyEqual(decoded.weaponFires[0].end.x, 9.0F) &&
        nearlyEqual(decoded.weaponFires[0].knockbackImpulse.x, 2.0F),
      "instant weapon events should round trip"
    );
    failures += expect(
      decoded.weaponFires[1].fired &&
        decoded.weaponFires[1].hit &&
        decoded.weaponFires[1].weapon == lg::Weapon::Shotgun &&
        decoded.weaponFires[1].damageApplied == 45 &&
        decoded.weaponFires[1].pelletCount == lg::kShotgunPelletCount &&
        decoded.weaponFires[1].pelletHitCount == 9,
      "shotgun pellet event data should round trip"
    );
    failures += expect(
      decoded.weaponFires[2].fired &&
        decoded.weaponFires[2].hit &&
        decoded.weaponFires[2].weapon == lg::Weapon::MachineGun &&
        decoded.weaponFires[2].damageApplied == 5 &&
        nearlyEqual(decoded.weaponFires[2].knockbackImpulse.x, 0.11F),
      "machine gun weapon events should round trip"
    );
    failures += expect(
      decoded.rocketExplosions[0].active &&
        decoded.rocketExplosions[0].weapon == lg::Weapon::GrenadeLauncher &&
        nearlyEqual(decoded.rocketExplosions[0].position.y, 4.0F) &&
        nearlyEqual(decoded.rocketExplosions[0].radius, 3.0F) &&
        decoded.rocketExplosions[0].ownerDamageApplied == 12 &&
        decoded.rocketExplosions[0].opponentDamageApplied == 80 &&
        decoded.footstepAudioEvents[1].active &&
        decoded.footstepAudioEvents[1].sequence == 42 &&
        nearlyEqual(decoded.footstepAudioEvents[1].position.z, 1.5F) &&
        decoded.grenadeBounceAudioEvents[0].active &&
        decoded.grenadeBounceAudioEvents[0].sequence == 9 &&
        nearlyEqual(decoded.grenadeBounceAudioEvents[0].position.z, 0.75F) &&
        decoded.fragEvents[0].active &&
        decoded.fragEvents[0].targetPlayerIndex == 1 &&
        decoded.rockets[0].active &&
        decoded.rockets[0].owner == 1 &&
        decoded.rockets[0].weapon == lg::Weapon::GrenadeLauncher &&
        nearlyEqual(decoded.rockets[0].position.z, 1.2F) &&
        nearlyEqual(decoded.rockets[0].velocity.z, 9.0F),
      "rocket, explosion, footstep audio, and frag event state should round trip"
    );
    failures += expect(decoded.respawnTicksRemaining[1] == 88, "respawn timer should round trip");
    failures += expect(decoded.scores == source.scores, "scores should round trip");
    failures += expect(
      decoded.gameMode == lg::GameMode::ClanArena &&
        decoded.teams == source.teams &&
        decoded.teamScores == source.teamScores &&
        decoded.roundWinningTeam == lg::Team::Red &&
        decoded.matchWinningTeam == lg::Team::None,
      "gamemode and team match state should round trip"
    );
    failures += expect(
      decoded.chatSequence == 7 &&
        decoded.chatPlayerIndex == 1 &&
        decoded.chatMessage == "nice shot",
      "relayed chat should round trip"
    );
    failures += expect(
      decoded.matchCombatStats[0].lightningActiveTicks == 500 &&
        decoded.matchCombatStats[0].lightningHitTicks == 275 &&
        decoded.matchCombatStats[0].damageDealt == 180 &&
        decoded.playerNames[0] == "yg" &&
        decoded.playerNames[1] == "opponent",
      "scoreboard names and aggregate stats should round trip"
    );
    failures += expect(
      decoded.connectedPlayers == source.connectedPlayers &&
        decoded.participatingPlayers == source.participatingPlayers &&
        decoded.readyPlayers == source.readyPlayers,
      "lobby and participating-player state should round trip"
    );
    failures += expect(
      decoded.roundCombatStats[0].lightningActiveTicks == 250 &&
        decoded.roundCombatStats[0].lightningHitTicks == 125 &&
        decoded.roundCombatStats[0].damageDealt == 80 &&
        decoded.roundCombatStats[1].damageDealt == 24,
      "round combat stats should round trip"
    );
    failures += expect(
      decoded.matchPhase == lg::MatchPhase::Countdown &&
        decoded.matchRules.showOpponentHealth &&
        decoded.phaseTicksRemaining == 321,
      "match rules and phase should round trip"
    );
    failures += expect(
      decoded.movementTuning.flightEnabled &&
      decoded.movementTuning.airControlEnabled &&
      nearlyEqual(decoded.movementTuning.groundAcceleration, 120.0F) &&
      nearlyEqual(decoded.movementTuning.airAcceleration, 2.0F) &&
      nearlyEqual(decoded.movementTuning.groundFriction, 6.0F) &&
      nearlyEqual(decoded.movementTuning.stopSpeed, 2.5F) &&
      nearlyEqual(decoded.movementTuning.maxGroundSpeed, 14.0F) &&
      nearlyEqual(decoded.movementTuning.flightAcceleration, 48.0F) &&
      nearlyEqual(decoded.movementTuning.maxFlightSpeed, 16.0F) &&
      nearlyEqual(decoded.movementTuning.flightDamping, 1.5F) &&
      nearlyEqual(decoded.movementTuning.flightGravityCancel, 1.0F) &&
      nearlyEqual(decoded.playerSizeScaleXY, 1.75F) &&
      nearlyEqual(decoded.playerSizeScaleZ, 0.75F) &&
      nearlyEqual(decoded.lightningKnockback, 1500.0F) &&
      nearlyEqual(decoded.rocketKnockback, 625.0F) &&
      decoded.weaponDamage.shotgunDamagePerPellet == 11 &&
      decoded.weaponDamage.machineGunDamage == 13 &&
      decoded.weaponDamage.lightningGunDamage == 90 &&
      decoded.weaponDamage.railgunDamage == 50 &&
      decoded.weaponDamage.rocketLauncherDamage == 140 &&
      nearlyEqual(decoded.vampirism, 2.0F) &&
      decoded.selfDamagePercent == 25 &&
      decoded.healthAmount == 150 &&
      decoded.botDodgeEnabled &&
      decoded.botDodgeMinIntervalMs == 300 &&
      decoded.botDodgeMaxIntervalMs == 700,
      "authoritative movement tuning should round trip"
    );
    failures += expect(decoded.playersColliding, "collision diagnostic should round trip");

    lg::DisconnectPacket disconnect{12345};
    lg::DisconnectPacket decodedDisconnect;
    failures += expect(
      lg::encodeDisconnectPacket(disconnect, wire),
      "disconnect should encode"
    );
    failures += expect(
      lg::decodeDisconnectPacket(wire, decodedDisconnect) &&
        decodedDisconnect.clientNonce == 12345,
      "disconnect should round trip"
    );

    lg::WirePacket oversized(lg::kMaxPacketBytes + 1, 0);
    failures += expect(
      !lg::decodeServerSnapshot(oversized, decoded),
      "oversized snapshot should be rejected"
    );

    lg::ServerSnapshot invalid = source;
    invalid.players[0].position.x = std::numeric_limits<float>::infinity();
    failures += expect(!lg::encodeServerSnapshot(invalid, wire), "non-finite snapshot should not encode");

    invalid = source;
    invalid.teams[0] = static_cast<lg::Team>(255);
    failures += expect(
      !lg::encodeServerSnapshot(invalid, wire),
      "invalid snapshot team should not encode"
    );

    invalid = source;
    invalid.footstepAudioEvents[0].position.x =
      std::numeric_limits<float>::infinity();
    failures += expect(
      !lg::encodeServerSnapshot(invalid, wire),
      "non-finite footstep audio event should not encode"
    );

    invalid = source;
    invalid.grenadeBounceAudioEvents[0].position.x =
      std::numeric_limits<float>::infinity();
    failures += expect(
      !lg::encodeServerSnapshot(invalid, wire),
      "non-finite grenade bounce audio event should not encode"
    );

    invalid = source;
    invalid.fragEvents[0].active = true;
    invalid.fragEvents[0].targetPlayerIndex = lg::kDuelPlayerCount;
    failures += expect(
      !lg::encodeServerSnapshot(invalid, wire),
      "invalid frag target should not encode"
    );
  }

  return failures == 0 ? 0 : 1;
}
