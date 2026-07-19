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

lg::MapDescriptor testMapDescriptor() {
  return {"testmap", 0x12345678U};
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

    lg::ConnectAccept accept{12345, 3, 1, 77};
    lg::ConnectAccept decodedAccept;
    failures += expect(lg::encodeConnectAccept(accept, wire), "connect accept should encode");
    failures += expect(lg::decodeConnectAccept(wire, decodedAccept), "connect accept should decode");
    failures += expect(decodedAccept.playerIndex == 1, "assigned player should round trip");
    failures += expect(decodedAccept.clientIndex == 3, "client slot should round trip");
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
    source.clientIndex = 7;
    source.clientNonce = 12345;
    source.command.sequence = 42;
    source.acknowledgedConfigurationRevision = 77;
    source.wantsScoreboardStats = true;
    source.command.clientTick = 99;
    source.command.viewYawRadians = 1.25F;
    source.command.viewPitchRadians = -0.25F;
    source.command.forwardMove = 1.0F;
    source.command.rightMove = -0.5F;
    source.command.upMove = 0.75F;
    source.command.attack = true;
    source.command.jump = true;
    source.command.dash = true;
    source.command.crouch = true;
    source.command.sneak = true;
    source.command.planarAim = false;
    source.command.weapon = lg::Weapon::PlasmaGun;
    source.requestReset = true;
    source.toggleReady = true;
    source.requestGameMode = true;
    source.requestedGameMode = lg::GameMode::ClanArena;
    source.requestTeam = true;
    source.requestedTeam = lg::Team::Blue;
    source.requestSpectator = true;
    source.weaponSwitchingMode = lg::WeaponSwitchingMode::Cpma;
    source.requestMovementTuning = true;
    source.movementTuning.flightEnabled = true;
    source.movementTuning.airControlEnabled = true;
    source.movementTuning.groundAcceleration = 120.0F;
    source.movementTuning.airAcceleration = 2.0F;
    source.movementTuning.groundFriction = 6.0F;
    source.movementTuning.stopSpeed = 2.5F;
    source.movementTuning.maxGroundSpeed = 14.0F;
    source.movementTuning.dashTargetSpeed = 13.0F;
    source.movementTuning.dashMaxSpeed = 14.5F;
    source.movementTuning.dashAcceleration = 240.0F;
    source.movementTuning.dashDuration = 0.12F;
    source.movementTuning.dashCooldown = 0.7F;
    source.movementTuning.dashGroundHopVelocity = 3.6F;
    source.movementTuning.dashAirHopVelocity = 2.1F;
    source.movementTuning.flightAcceleration = 48.0F;
    source.movementTuning.maxFlightSpeed = 16.0F;
    source.movementTuning.flightDamping = 1.5F;
    source.movementTuning.flightGravityCancel = 1.0F;
    source.playerSizeScaleXY = 1.75F;
    source.playerSizeScaleZ = 0.75F;
    source.lightningKnockback = 1500.0F;
    source.lightningFireHz = 40.0F;
    source.rocketKnockback = 625.0F;
    source.knockbackTimeMs = 125;
    source.weaponDamage.shotgunDamagePerPellet = 7;
    source.weaponDamage.machineGunDamage = 9;
    source.weaponDamage.lightningGunDamage = 111;
    source.weaponDamage.railgunDamage = 50;
    source.weaponDamage.rocketLauncherDamage = 125;
    source.weaponDamage.plasmaGunDamage = 20;
    source.weaponDamage.freezeGunDamage = 112;
    source.weaponAmmo.infiniteAmmo = false;
    source.weaponAmmo.spawnAmmo[lg::weaponIndex(lg::Weapon::LightningGun)] = 151;
    source.weaponAmmo.spawnAmmo[lg::weaponIndex(lg::Weapon::Railgun)] = 11;
    source.weaponAmmo.spawnAmmo[lg::weaponIndex(lg::Weapon::RocketLauncher)] = 12;
    source.weaponAmmo.spawnAmmo[lg::weaponIndex(lg::Weapon::MachineGun)] = 101;
    source.weaponAmmo.spawnAmmo[lg::weaponIndex(lg::Weapon::Shotgun)] = 13;
    source.weaponAmmo.spawnAmmo[lg::weaponIndex(lg::Weapon::GrenadeLauncher)] = 14;
    source.weaponAmmo.spawnAmmo[lg::weaponIndex(lg::Weapon::PlasmaGun)] = 51;
    source.weaponAmmo.spawnAmmo[lg::weaponIndex(lg::Weapon::FreezeGun)] = 152;
    source.weaponAmmo.spawnAmmo[lg::weaponIndex(lg::Weapon::Revolver)] = 15;
    source.vampirism = 0.1F;
    source.selfDamagePercent = 37;
    source.healthAmount = 175;
    source.botDodgeEnabled = true;
    source.botDodgeMinIntervalMs = 250;
    source.botDodgeMaxIntervalMs = 750;
    source.chatMessage = "åäöÅÄÖ";
    source.playerName = "yg";
    source.mapName = "testmap";
    source.botCommand = lg::BotCommandType::Add;
    source.botCommandValue = 1;
    source.requestMcGuffinThrow = true;
    source.viewedServerTick = 88;
    source.actionEdges.jump = 4;
    source.actionEdges.dash = 5;
    source.actionEdges.reset = 6;
    source.actionEdges.ready = 7;
    source.actionEdges.mcguffinThrow = 8;
    source.actionEdges.mcguffinThrowYawRadians = 0.75F;
    source.actionEdges.mcguffinThrowPitchRadians = -0.2F;
    source.actionEdges.attack = 9;
    source.actionEdges.attackYawRadians = 1.25F;
    source.actionEdges.attackPitchRadians = -0.15F;
    source.actionEdges.attackViewedServerTick = 86;
    source.actionEdges.attackWeapon = lg::Weapon::Railgun;

    lg::WirePacket wire;
    lg::CommandPacket decoded;
    failures += expect(lg::encodeCommandPacket(source, wire), "command should encode");
    failures += expect(wire.size() <= lg::kMaxPacketBytes, "command should respect packet limit");
    failures += expect(lg::decodeCommandPacket(wire, decoded), "command should decode");
    failures += expect(decoded.playerIndex == source.playerIndex, "command player should round trip");
    failures += expect(
      decoded.clientIndex == source.clientIndex,
      "command connection identity should round trip separately from its body"
    );
    failures += expect(decoded.clientNonce == 12345, "command nonce should round trip");
    failures += expect(decoded.command.sequence == 42, "command sequence should round trip");
    failures += expect(
      decoded.acknowledgedConfigurationRevision == 77,
      "configuration acknowledgement should round trip"
    );
    failures += expect(decoded.wantsScoreboardStats,
                       "scoreboard statistics interest should round trip");
    failures += expect(decoded.command.clientTick == 99, "command tick should round trip");
    failures += expect(decoded.viewedServerTick == 88, "viewed server tick should round trip");
    failures += expect(
      nearlyEqual(decoded.command.viewPitchRadians, -0.25F),
      "command pitch should round trip"
    );
    failures += expect(
      decoded.command.attack &&
        decoded.command.jump &&
        decoded.command.dash &&
        decoded.command.crouch &&
        decoded.command.sneak,
      "command bits should round trip"
    );
    failures += expect(!decoded.command.planarAim, "command aim dimensionality should round trip");
    failures += expect(decoded.command.weapon == lg::Weapon::PlasmaGun, "weapon selection should round trip");
    failures += expect(decoded.chatMessage == "åäöÅÄÖ", "Swedish chat message should round trip");
    failures += expect(decoded.playerName == "yg", "player name should round trip");
    failures += expect(decoded.mapName == "testmap", "map name should round trip");
    failures += expect(
      decoded.botCommand == lg::BotCommandType::Add &&
        decoded.botCommandValue == 1,
      "bot command request should round trip"
    );
    failures += expect(decoded.requestReset, "reset bit should round trip");
    failures += expect(decoded.toggleReady, "ready bit should round trip");
    failures += expect(decoded.requestMcGuffinThrow,
      "McGuffin throw request should round trip");
    failures += expect(
      decoded.actionEdges.jump == 4U &&
        decoded.actionEdges.mcguffinThrow == 8U &&
        nearlyEqual(decoded.actionEdges.mcguffinThrowYawRadians, 0.75F) &&
        decoded.actionEdges.attack == 9U &&
        decoded.actionEdges.attackWeapon == lg::Weapon::Railgun,
      "cumulative action edges should round trip"
    );
    failures += expect(
      decoded.requestGameMode &&
        decoded.requestedGameMode == lg::GameMode::ClanArena,
      "explicit gamemode request should round trip"
    );
    failures += expect(
      decoded.requestTeam && decoded.requestedTeam == lg::Team::Blue,
      "explicit team request should round trip"
    );
    failures += expect(decoded.requestSpectator,
      "spectator role request should round trip");
    failures += expect(
      decoded.weaponSwitchingMode == lg::WeaponSwitchingMode::Cpma,
      "weapon switching mode request should round trip"
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
        nearlyEqual(decoded.movementTuning.dashTargetSpeed, 13.0F) &&
        nearlyEqual(decoded.movementTuning.dashMaxSpeed, 14.5F) &&
        nearlyEqual(decoded.movementTuning.dashAcceleration, 240.0F) &&
        nearlyEqual(decoded.movementTuning.dashDuration, 0.12F) &&
        nearlyEqual(decoded.movementTuning.dashCooldown, 0.7F) &&
        nearlyEqual(decoded.movementTuning.dashGroundHopVelocity, 3.6F) &&
        nearlyEqual(decoded.movementTuning.dashAirHopVelocity, 2.1F) &&
        nearlyEqual(decoded.movementTuning.flightAcceleration, 48.0F) &&
        nearlyEqual(decoded.movementTuning.maxFlightSpeed, 16.0F) &&
        nearlyEqual(decoded.movementTuning.flightDamping, 1.5F) &&
        nearlyEqual(decoded.movementTuning.flightGravityCancel, 1.0F) &&
        nearlyEqual(decoded.playerSizeScaleXY, 1.75F) &&
        nearlyEqual(decoded.playerSizeScaleZ, 0.75F) &&
        nearlyEqual(decoded.lightningKnockback, 1500.0F) &&
        nearlyEqual(decoded.lightningFireHz, 40.0F) &&
        nearlyEqual(decoded.rocketKnockback, 625.0F) &&
        decoded.knockbackTimeMs == 125 &&
        decoded.weaponDamage.shotgunDamagePerPellet == 7 &&
        decoded.weaponDamage.machineGunDamage == 9 &&
        decoded.weaponDamage.lightningGunDamage == 111 &&
        decoded.weaponDamage.railgunDamage == 50 &&
        decoded.weaponDamage.rocketLauncherDamage == 125 &&
        decoded.weaponDamage.plasmaGunDamage == 20 &&
        decoded.weaponDamage.freezeGunDamage == 112 &&
        !decoded.weaponAmmo.infiniteAmmo &&
        decoded.weaponAmmo.spawnAmmo == source.weaponAmmo.spawnAmmo &&
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
    bundle.datagramSequence = 91;
    bundle.actionEdges = source.actionEdges;
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
      decodedBundle.datagramSequence == 91,
      "command datagram sequence should round trip"
    );
    failures += expect(
      decodedBundle.commands[2].command.sequence == 44,
      "bundle command order should round trip"
    );
    failures += expect(
      decodedBundle.commands[0].requestedGameMode == lg::GameMode::ClanArena &&
        decodedBundle.commands[2].requestedTeam == lg::Team::Blue,
      "redundant command bundles should preserve explicit mode and team requests"
    );
    std::cout << "command control bundle bytes=" << wire.size() << '\n';
    failures += expect(
      wire.size() < 1200,
      "ordinary redundant command bundles should stay below the datagram budget"
    );

    lg::CommandBundle gameplayBundle;
    gameplayBundle.datagramSequence = 92;
    gameplayBundle.actionEdges = source.actionEdges;
    gameplayBundle.commandCount = lg::kMaxBundledCommands;
    for (std::size_t index = 0; index < gameplayBundle.commandCount; ++index) {
      lg::CommandPacket& command = gameplayBundle.commands[index];
      command.clientIndex = source.clientIndex;
      command.playerIndex = source.playerIndex;
      command.clientNonce = source.clientNonce;
      command.command = source.command;
      command.command.sequence = static_cast<std::uint32_t>(100U + index);
      command.viewedServerTick = static_cast<std::uint32_t>(80U + index);
      command.acknowledgedConfigurationRevision = 77;
    }
    failures += expect(
      lg::encodeCommandBundle(gameplayBundle, wire) &&
        lg::decodeCommandBundle(wire, decodedBundle),
      "maximum gameplay command history should round trip"
    );
    failures += expect(
      wire.size() <= lg::kMaxCommandDatagramBytes,
      "maximum gameplay command history should stay below the datagram budget"
    );
    std::cout << "command gameplay bundle bytes=" << wire.size() << '\n';

    lg::CommandBundle spectatorBundle = gameplayBundle;
    spectatorBundle.datagramSequence = 93;
    spectatorBundle.commandCount = 2;
    for (std::size_t index = 0; index < spectatorBundle.commandCount; ++index) {
      spectatorBundle.commands[index].clientIndex = 9;
      spectatorBundle.commands[index].playerIndex = lg::kNoAssignedPlayer;
    }
    failures += expect(
      lg::encodeCommandBundle(spectatorBundle, wire) &&
        lg::decodeCommandBundle(wire, decodedBundle) &&
        decodedBundle.commands[1].clientIndex == 9 &&
        decodedBundle.commands[1].playerIndex == lg::kNoAssignedPlayer,
      "spectator client slots should retain bundled command redundancy"
    );

    spectatorBundle.commands[1].clientIndex = 8;
    failures += expect(
      !lg::encodeCommandBundle(spectatorBundle, wire),
      "a command bundle should have one authenticated connection identity"
    );
    bundle.datagramSequence = 0;
    failures += expect(
      !lg::encodeCommandBundle(bundle, wire),
      "command bundles should reject the reserved zero datagram sequence"
    );
    bundle.datagramSequence = 91;
    for (lg::CommandPacket& command : bundle.commands) {
      command.chatMessage.assign(lg::kMaxChatMessageBytes, 'c');
      command.playerName.assign(lg::kMaxPlayerNameBytes, 'n');
      command.mapName.assign(lg::kMaxMapNameBytes, 'm');
    }
    failures += expect(
      !lg::encodeCommandBundle(bundle, wire),
      "an oversized redundant command bundle should fail encoding"
    );
  }

  {
    lg::CommandPacket source;
    source.playerIndex = 0;
    source.chatMessage.assign(180U, 'l');

    lg::WirePacket wire;
    lg::CommandPacket decoded;
    failures += expect(
      lg::encodeCommandPacket(source, wire),
      "longer chat command should encode"
    );
    failures += expect(
      lg::decodeCommandPacket(wire, decoded),
      "longer chat command should decode"
    );
    failures += expect(
      decoded.chatMessage == source.chatMessage &&
        decoded.chatMessage.size() > 64U,
      "chat command should allow messages beyond the old 64-byte cap"
    );
  }

  {
    const std::array states = {
      lg::McGuffinState::NeutralSpawn,
      lg::McGuffinState::Carried,
      lg::McGuffinState::Dropped,
      lg::McGuffinState::InstalledRed,
      lg::McGuffinState::InstalledBlue,
    };
    for (lg::McGuffinState state : states) {
      lg::ServerSnapshot source;
      source.map = testMapDescriptor();
      source.gameMode = lg::GameMode::McGuffin;
      source.mcguffin.state = state;
      source.mcguffin.position = {1.0F, 2.0F, 3.0F};
      source.mcguffin.velocity = {4.0F, 5.0F, 6.0F};
      source.mcguffinConfig.throwSpeed = 13.0F;
      source.mcguffinConfig.throwUpSpeed = 5.0F;
      source.mcguffin.stateTicks = 41;
      source.mcguffin.eventSequence = 9;
      source.mcguffin.lastEvent = lg::McGuffinEventType::Pickup;
      source.mcguffinScores = {42, 37};
      source.mcguffinRoundsWon = {1, 0};
      if (state == lg::McGuffinState::Carried) {
        source.mcguffin.carrierIndex = 1;
        source.mcguffin.carrierTeam = lg::Team::Blue;
      }
      if (state == lg::McGuffinState::InstalledRed) {
        source.mcguffin.associatedTeam = lg::Team::Red;
      } else if (state == lg::McGuffinState::InstalledBlue) {
        source.mcguffin.associatedTeam = lg::Team::Blue;
      }
      lg::WirePacket wire;
      lg::ServerSnapshot decoded;
      failures += expect(lg::encodeServerSnapshot(source, wire),
        "every McGuffin objective state should encode");
      failures += expect(lg::decodeServerSnapshot(wire, decoded),
        "every McGuffin objective state should decode");
      failures += expect(
        decoded.gameMode == lg::GameMode::McGuffin &&
          decoded.mcguffin.state == state &&
          decoded.mcguffin.position.z == 3.0F &&
          decoded.mcguffin.velocity.z == 6.0F &&
          decoded.mcguffinConfig.throwSpeed == 13.0F &&
          decoded.mcguffinConfig.throwUpSpeed == 5.0F &&
          decoded.mcguffinScores == source.mcguffinScores,
        "McGuffin snapshot should round trip authoritatively"
      );
    }

    lg::ServerSnapshot invalid;
    invalid.map = testMapDescriptor();
    invalid.gameMode = static_cast<lg::GameMode>(255);
    lg::WirePacket wire;
    failures += expect(!lg::encodeServerSnapshot(invalid, wire),
      "invalid game mode enum should be rejected");
    invalid.gameMode = lg::GameMode::McGuffin;
    invalid.mcguffin.state = static_cast<lg::McGuffinState>(255);
    failures += expect(!lg::encodeServerSnapshot(invalid, wire),
      "invalid McGuffin state enum should be rejected");
  }

  {
    lg::ServerSnapshot source;
    source.serverTick = 1234;
    source.acknowledgedCommandDatagramSequence = 88;
    source.commandDatagramAckBits = 0xA5A55A5AU;
    source.mapRevision = 77;
    source.map = testMapDescriptor();
    source.acknowledgedCommand = {12, 34};
    source.hasAcknowledgedCommand = {true, false};
    source.players[0].position = {1.0F, 2.0F, 3.0F};
    source.players[0].velocity = {-1.0F, 0.5F, 4.0F};
    source.players[0].viewYawRadians = 0.75F;
    source.players[0].viewPitchRadians = -0.1F;
    source.players[0].health = 81;
    source.players[0].freezeLevel = 37.5F;
    source.players[0].movementMode = lg::MovementMode::Flying;
    source.players[0].onGround = false;
    source.players[0].jumpHeld = true;
    source.players[0].dashHeld = true;
    source.players[0].dashCooldownTicksRemaining = 77;
    source.players[0].dashActiveTicksRemaining = 4;
    source.players[0].dashDirection = {0.0F, -1.0F, 0.0F};
    source.players[0].crouched = true;
    source.players[0].sneaking = true;
    source.players[0].knockbackTicksRemaining = 9;
    source.players[1].health = 0;
    source.selectedWeapons[0] = lg::Weapon::Revolver;
    source.selectedWeapons[1] = lg::Weapon::Railgun;
    source.playerAmmo[0][lg::weaponIndex(lg::Weapon::LightningGun)] = 149;
    source.playerAmmo[0][lg::weaponIndex(lg::Weapon::Railgun)] = 9;
    source.playerAmmo[0][lg::weaponIndex(lg::Weapon::RocketLauncher)] = 8;
    source.playerAmmo[0][lg::weaponIndex(lg::Weapon::MachineGun)] = 99;
    source.playerAmmo[0][lg::weaponIndex(lg::Weapon::Shotgun)] = 7;
    source.playerAmmo[0][lg::weaponIndex(lg::Weapon::GrenadeLauncher)] = 6;
    source.playerAmmo[0][lg::weaponIndex(lg::Weapon::PlasmaGun)] = 49;
    source.playerAmmo[0][lg::weaponIndex(lg::Weapon::FreezeGun)] = 148;
    source.playerAmmo[0][lg::weaponIndex(lg::Weapon::Revolver)] = 13;
    source.playerAmmo[1][lg::weaponIndex(lg::Weapon::LightningGun)] = 120;
    source.playerAmmo[1][lg::weaponIndex(lg::Weapon::Railgun)] = 5;
    source.playerAmmo[1][lg::weaponIndex(lg::Weapon::RocketLauncher)] = 4;
    source.playerAmmo[1][lg::weaponIndex(lg::Weapon::MachineGun)] = 88;
    source.playerAmmo[1][lg::weaponIndex(lg::Weapon::Shotgun)] = 3;
    source.playerAmmo[1][lg::weaponIndex(lg::Weapon::GrenadeLauncher)] = 2;
    source.playerAmmo[1][lg::weaponIndex(lg::Weapon::PlasmaGun)] = 48;
    source.playerAmmo[1][lg::weaponIndex(lg::Weapon::FreezeGun)] = 147;
    source.playerAmmo[1][lg::weaponIndex(lg::Weapon::Revolver)] = 12;
    source.lightningGuns[0].active = true;
    source.lightningGuns[0].hit = true;
    source.lightningGuns[0].headshot = true;
    source.lightningGuns[0].targetPlayerIndex = 1;
    source.lightningGuns[0].damageApplied = 2;
    source.lightningGuns[0].start = {1.0F, 2.0F, 3.0F};
    source.lightningGuns[0].end = {5.0F, 6.0F, 7.0F};
    source.lightningGuns[0].knockbackImpulse = {0.1F, 0.2F, 0.3F};
    source.lightningGuns[0].freezeApplied = 2.5F;
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
    source.weaponFires[0].headshot = true;
    source.weaponFires[0].weapon = lg::Weapon::Revolver;
    source.weaponFires[0].damageApplied = 80;
    source.weaponFires[0].start = {1.0F, 1.5F, 2.0F};
    source.weaponFires[0].end = {9.0F, 1.5F, 2.0F};
    source.weaponFires[0].knockbackImpulse = {2.0F, 0.0F, 0.0F};
    source.weaponFires[1].fired = true;
    source.weaponFires[1].hit = true;
    source.weaponFires[1].headshot = true;
    source.weaponFires[1].weapon = lg::Weapon::Shotgun;
    source.weaponFires[1].damageApplied = 45;
    source.weaponFires[1].start = {1.0F, -1.5F, 2.0F};
    source.weaponFires[1].end = {7.0F, -1.5F, 2.0F};
    source.weaponFires[1].knockbackImpulse = {1.0F, 0.0F, 0.0F};
    source.weaponFires[1].pelletCount = lg::kShotgunPelletCount;
    source.weaponFires[1].pelletHitCount = 9;
    source.weaponFires[1].pelletHeadshotCount = 3;
    source.weaponFires[1].visualSeed = 12;
    source.weaponFires[2].fired = true;
    source.weaponFires[2].hit = true;
    source.weaponFires[2].weapon = lg::Weapon::MachineGun;
    source.weaponFires[2].damageApplied = 5;
    source.weaponFires[2].start = {2.0F, 1.5F, 2.0F};
    source.weaponFires[2].visualSeed = 77;
    source.weaponFires[2].end = {8.0F, 1.5F, 2.0F};
    source.weaponFires[2].knockbackImpulse = {0.11F, 0.0F, 0.0F};
    source.rocketExplosions[0].active = true;
    source.rocketExplosions[0].weapon = lg::Weapon::GrenadeLauncher;
    source.rocketExplosions[0].position = {3.0F, 4.0F, 0.0F};
    source.rocketExplosions[0].radius = 3.0F;
    source.rocketExplosions[0].ownerDamageApplied = 12;
    source.rocketExplosions[0].opponentDamageApplied = 80;
    source.rocketExplosions[0].sequence = 42;
    source.fragEvents[0].active = true;
    source.fragEvents[0].sequence = 55;
    source.fragEvents[0].targetPlayerIndex = 1;
    source.fragEvents[0].weapon = lg::Weapon::Railgun;
    source.localHitFeedbackEvents[0][0].active = true;
    source.localHitFeedbackEvents[0][0].sequence = 17;
    source.localHitFeedbackEvents[0][0].targetPlayerIndex = 1;
    source.localHitFeedbackEvents[0][0].damageApplied = 45;
    source.localHitFeedbackEvents[0][0].headshot = true;
    source.localHitFeedbackEvents[0][0].weapon = lg::Weapon::Shotgun;
    source.localHitFeedbackEvents[0][1].active = true;
    source.localHitFeedbackEvents[0][1].sequence = 18;
    source.localHitFeedbackEvents[0][1].targetPlayerIndex = 2;
    source.localHitFeedbackEvents[0][1].damageApplied = 80;
    source.localHitFeedbackEvents[0][1].weapon = lg::Weapon::RocketLauncher;
    source.footstepAudioEvents[1].active = true;
    source.footstepAudioEvents[1].jumping = true;
    source.footstepAudioEvents[1].landing = true;
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
    source.rockets[0].radius = 0.25F;
    source.icePools[0].active = true;
    source.icePools[0].center = {1.0F, 2.0F, 0.0F};
    source.icePools[0].normal = {0.0F, 0.0F, 1.0F};
    source.icePools[0].radius = 1.25F;
    source.icePools[0].lifetimeSeconds = 2.5F;
    source.healthPickupAvailable[0] = true;
    source.healthPickupAvailable[3] = true;
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
    source.botPlayers = {false, false, true, false};
    source.participatingPlayers = {true, true, true};
    source.readyPlayers = {true, false};
    source.roundCombatStats[0].weapons[lg::weaponIndex(lg::Weapon::LightningGun)] =
      {80, 250, 125};
    source.roundCombatStats[1].weapons[lg::weaponIndex(lg::Weapon::LightningGun)] =
      {24, 200, 40};
    source.matchCombatStats[0].weapons[lg::weaponIndex(lg::Weapon::LightningGun)] =
      {180, 500, 275};
    source.matchCombatStats[1].weapons[lg::weaponIndex(lg::Weapon::LightningGun)] =
      {74, 450, 90};
    source.playerNames = {"yg", "opponent"};
    source.matchPhase = lg::MatchPhase::Countdown;
    source.matchRules.roundLimit = 10;
    source.matchRules.timeLimitMinutes = 5;
    source.matchRules.playerLimit = 2;
    source.matchRules.countdownTicks = 625;
    source.matchRules.roundEndTicks = 125;
    source.matchRules.matchEndTicks = 625;
    source.matchRules.deathRespawnTicks = 250;
    source.matchRules.showOpponentHealth = true;
    source.movementTuning.flightEnabled = true;
    source.movementTuning.airControlEnabled = true;
    source.movementTuning.groundAcceleration = 120.0F;
    source.movementTuning.airAcceleration = 2.0F;
    source.movementTuning.groundFriction = 6.0F;
    source.movementTuning.stopSpeed = 2.5F;
    source.movementTuning.maxGroundSpeed = 14.0F;
    source.movementTuning.dashTargetSpeed = 13.0F;
    source.movementTuning.dashMaxSpeed = 14.5F;
    source.movementTuning.dashAcceleration = 240.0F;
    source.movementTuning.dashDuration = 0.12F;
    source.movementTuning.dashCooldown = 0.7F;
    source.movementTuning.dashGroundHopVelocity = 3.6F;
    source.movementTuning.dashAirHopVelocity = 2.1F;
    source.movementTuning.flightAcceleration = 48.0F;
    source.movementTuning.maxFlightSpeed = 16.0F;
    source.movementTuning.flightDamping = 1.5F;
    source.movementTuning.flightGravityCancel = 1.0F;
    source.playerSizeScaleXY = 1.75F;
    source.playerSizeScaleZ = 0.75F;
    source.lightningKnockback = 1500.0F;
    source.lightningFireHz = 25.0F;
    source.rocketKnockback = 625.0F;
    source.knockbackTimeMs = 125;
    source.weaponDamage.shotgunDamagePerPellet = 11;
    source.weaponDamage.machineGunDamage = 13;
    source.weaponDamage.lightningGunDamage = 90;
    source.weaponDamage.railgunDamage = 50;
    source.weaponDamage.rocketLauncherDamage = 140;
    source.weaponDamage.plasmaGunDamage = 25;
    source.weaponDamage.freezeGunDamage = 95;
    source.icePoolTuning.maxRadius = 3.0F;
    source.icePoolTuning.growthPerSecond = 11.0F;
    source.icePoolTuning.lifetimeSeconds = 4.0F;
    source.icePoolTuning.friction = 0.75F;
    source.icePoolTuning.slopeGravityScale = 1.25F;
    source.icePoolTuning.controlScale = 0.25F;
    source.icePoolTuning.mergeDistance = 1.4F;
    source.weaponAmmo.infiniteAmmo = false;
    source.weaponAmmo.spawnAmmo[lg::weaponIndex(lg::Weapon::LightningGun)] = 150;
    source.weaponAmmo.spawnAmmo[lg::weaponIndex(lg::Weapon::Railgun)] = 10;
    source.weaponAmmo.spawnAmmo[lg::weaponIndex(lg::Weapon::RocketLauncher)] = 11;
    source.weaponAmmo.spawnAmmo[lg::weaponIndex(lg::Weapon::MachineGun)] = 100;
    source.weaponAmmo.spawnAmmo[lg::weaponIndex(lg::Weapon::Shotgun)] = 12;
    source.weaponAmmo.spawnAmmo[lg::weaponIndex(lg::Weapon::GrenadeLauncher)] = 13;
    source.weaponAmmo.spawnAmmo[lg::weaponIndex(lg::Weapon::PlasmaGun)] = 50;
    source.weaponAmmo.spawnAmmo[lg::weaponIndex(lg::Weapon::FreezeGun)] = 149;
    source.weaponAmmo.spawnAmmo[lg::weaponIndex(lg::Weapon::Revolver)] = 14;
    source.vampirism = 2.0F;
    source.selfDamagePercent = 25;
    source.healthAmount = 150;
    source.botDodgeEnabled = true;
    source.botDodgeMinIntervalMs = 300;
    source.botDodgeMaxIntervalMs = 700;
    source.weaponSwitchingMode = lg::WeaponSwitchingMode::Ql;
    source.phaseTicksRemaining = 321;
    source.liveTicksElapsed = 900;
    source.roundWinner = 0;
    source.matchWinner = 255;
    source.playersColliding = true;
    // High-frequency gameplay snapshots never embed scoreboard aggregates;
    // those use the independently bounded CombatStats packet below.
    source.hasCombatStats = false;

    lg::WirePacket wire;
    lg::ServerSnapshot decoded;
    failures += expect(lg::encodeServerSnapshot(source, wire), "snapshot should encode");
    failures += expect(wire.size() <= lg::kMaxPacketBytes, "snapshot should respect packet limit");
    const std::size_t activeCombatSnapshotBytes = wire.size();
    failures += expect(
      activeCombatSnapshotBytes < 2800,
      "representative active-combat snapshot should remain below 2800 bytes"
    );
    failures += expect(lg::decodeServerSnapshot(wire, decoded), "snapshot should decode");
    failures += expect(decoded.serverTick == 1234, "snapshot tick should round trip");
    failures += expect(decoded.mapRevision == 77, "snapshot map revision should round trip");
    failures += expect(
      decoded.map.mapName == source.map.mapName &&
        decoded.map.contentHash == source.map.contentHash,
      "snapshot map descriptor should round trip without arena geometry"
    );
    failures += expect(decoded.acknowledgedCommand[0] == 12, "snapshot ack should round trip");
    failures += expect(
      decoded.acknowledgedCommandDatagramSequence == 88 &&
        decoded.commandDatagramAckBits == 0xA5A55A5AU,
      "command datagram acknowledgement window should round trip"
    );
    failures += expect(
        decoded.players[0].movementMode == lg::MovementMode::Flying &&
        decoded.players[0].jumpHeld &&
        decoded.players[0].dashHeld &&
        decoded.players[0].crouched &&
        decoded.players[0].sneaking &&
        decoded.players[0].knockbackTicksRemaining == 9 &&
        decoded.players[0].dashCooldownTicksRemaining == 77 &&
        decoded.players[0].dashActiveTicksRemaining == 4 &&
        nearlyEqual(decoded.players[0].dashDirection.y, -1.0F) &&
        nearlyEqual(decoded.players[0].freezeLevel, 37.5F),
      "movement mode, movement latches, dash state, knockback timer, and freeze level should round trip"
    );
    failures += expect(nearlyEqual(decoded.players[0].position.z, 3.0F), "3D position should round trip");
    failures += expect(nearlyEqual(decoded.players[0].velocity.z, 4.0F), "3D velocity should round trip");
    failures += expect(decoded.players[1].health == 0, "death state should round trip");
    failures += expect(
      decoded.selectedWeapons[0] == lg::Weapon::Revolver &&
        decoded.selectedWeapons[1] == lg::Weapon::Railgun,
      "selected weapons should round trip"
    );
    failures += expect(
      decoded.playerAmmo == source.playerAmmo,
      "per-player ammo should round trip"
    );
    failures += expect(
      decoded.lightningGuns[0].hit &&
        decoded.lightningGuns[0].headshot &&
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
      nearlyEqual(decoded.lightningGuns[0].freezeApplied, 2.5F),
      "beam freeze amount should round trip"
    );
    failures += expect(
      decoded.weaponFires[0].fired &&
        decoded.weaponFires[0].hit &&
        decoded.weaponFires[0].headshot &&
        decoded.weaponFires[0].weapon == lg::Weapon::Revolver &&
        decoded.weaponFires[0].damageApplied == 80 &&
        nearlyEqual(decoded.weaponFires[0].end.x, 9.0F) &&
        nearlyEqual(decoded.weaponFires[0].knockbackImpulse.x, 2.0F),
      "instant weapon events should round trip"
    );
    failures += expect(
      decoded.weaponFires[1].fired &&
        decoded.weaponFires[1].hit &&
        decoded.weaponFires[1].headshot &&
        decoded.weaponFires[1].weapon == lg::Weapon::Shotgun &&
        decoded.weaponFires[1].damageApplied == 45 &&
        decoded.weaponFires[1].pelletCount == lg::kShotgunPelletCount &&
        decoded.weaponFires[1].pelletHitCount == 9 &&
        decoded.weaponFires[1].pelletHeadshotCount == 3 &&
        decoded.weaponFires[1].visualSeed == 12 &&
        decoded.weaponFires[2].visualSeed == 77,
      "shotgun pellet event data should round trip"
    );
    failures += expect(
      decoded.localHitFeedbackEvents[0][0].active &&
        decoded.localHitFeedbackEvents[0][0].sequence == 17 &&
        decoded.localHitFeedbackEvents[0][0].targetPlayerIndex == 1 &&
        decoded.localHitFeedbackEvents[0][0].damageApplied == 45 &&
        decoded.localHitFeedbackEvents[0][0].headshot &&
        decoded.localHitFeedbackEvents[0][0].weapon == lg::Weapon::Shotgun &&
        decoded.localHitFeedbackEvents[0][1].active &&
        decoded.localHitFeedbackEvents[0][1].sequence == 18 &&
        decoded.localHitFeedbackEvents[0][1].targetPlayerIndex == 2 &&
        decoded.localHitFeedbackEvents[0][1].damageApplied == 80 &&
        decoded.localHitFeedbackEvents[0][1].weapon == lg::Weapon::RocketLauncher,
      "local hit feedback event window should round trip"
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
        decoded.rocketExplosions[0].sequence == 42 &&
        decoded.footstepAudioEvents[1].active &&
        decoded.footstepAudioEvents[1].jumping &&
        decoded.footstepAudioEvents[1].landing &&
        decoded.footstepAudioEvents[1].sequence == 42 &&
        nearlyEqual(decoded.footstepAudioEvents[1].position.z, 1.5F) &&
        decoded.grenadeBounceAudioEvents[0].active &&
        decoded.grenadeBounceAudioEvents[0].sequence == 9 &&
        nearlyEqual(decoded.grenadeBounceAudioEvents[0].position.z, 0.75F) &&
        decoded.fragEvents[0].active &&
        decoded.fragEvents[0].sequence == 55 &&
        decoded.fragEvents[0].targetPlayerIndex == 1 &&
        decoded.fragEvents[0].weapon == lg::Weapon::Railgun &&
        decoded.rockets[0].active &&
        decoded.rockets[0].owner == 1 &&
        decoded.rockets[0].weapon == lg::Weapon::GrenadeLauncher &&
        nearlyEqual(decoded.rockets[0].position.z, 1.2F) &&
        nearlyEqual(decoded.rockets[0].velocity.z, 9.0F) &&
        nearlyEqual(decoded.rockets[0].radius, 0.25F),
      "rocket, explosion, footstep audio, and frag event state should round trip"
    );
    failures += expect(
      decoded.icePools[0].active &&
        nearlyEqual(decoded.icePools[0].center.x, 1.0F) &&
        nearlyEqual(decoded.icePools[0].center.y, 2.0F) &&
        nearlyEqual(decoded.icePools[0].normal.z, 1.0F) &&
        nearlyEqual(decoded.icePools[0].radius, 1.25F) &&
        nearlyEqual(decoded.icePools[0].lifetimeSeconds, 2.5F),
      "ice pool snapshot should round trip"
    );
    failures += expect(
      decoded.healthPickupAvailable == source.healthPickupAvailable,
      "health pickup availability bits should round trip"
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
      !decoded.hasCombatStats && decoded.playerNames[0] == "yg" &&
        decoded.playerNames[1] == "opponent",
      "gameplay names should round trip without embedded combat aggregates"
    );
    failures += expect(
      decoded.connectedPlayers == source.connectedPlayers &&
        decoded.botPlayers == source.botPlayers &&
        decoded.participatingPlayers == source.participatingPlayers &&
        decoded.readyPlayers == source.readyPlayers,
      "lobby, bot, and participating-player state should round trip"
    );
    failures += expect(
      !decoded.hasCombatStats,
      "round combat stats should stay out of gameplay snapshots"
    );
    failures += expect(
      decoded.matchPhase == lg::MatchPhase::Countdown &&
        decoded.matchRules.showOpponentHealth &&
        decoded.matchRules.deathRespawnTicks == 250 &&
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
      nearlyEqual(decoded.movementTuning.dashTargetSpeed, 13.0F) &&
      nearlyEqual(decoded.movementTuning.dashMaxSpeed, 14.5F) &&
      nearlyEqual(decoded.movementTuning.dashAcceleration, 240.0F) &&
      nearlyEqual(decoded.movementTuning.dashDuration, 0.12F) &&
      nearlyEqual(decoded.movementTuning.dashCooldown, 0.7F) &&
      nearlyEqual(decoded.movementTuning.dashGroundHopVelocity, 3.6F) &&
      nearlyEqual(decoded.movementTuning.dashAirHopVelocity, 2.1F) &&
      nearlyEqual(decoded.movementTuning.flightAcceleration, 48.0F) &&
      nearlyEqual(decoded.movementTuning.maxFlightSpeed, 16.0F) &&
      nearlyEqual(decoded.movementTuning.flightDamping, 1.5F) &&
      nearlyEqual(decoded.movementTuning.flightGravityCancel, 1.0F) &&
      nearlyEqual(decoded.playerSizeScaleXY, 1.75F) &&
      nearlyEqual(decoded.playerSizeScaleZ, 0.75F) &&
      nearlyEqual(decoded.lightningKnockback, 1500.0F) &&
      nearlyEqual(decoded.lightningFireHz, 25.0F) &&
      nearlyEqual(decoded.rocketKnockback, 625.0F) &&
      decoded.knockbackTimeMs == 125 &&
      decoded.weaponDamage.shotgunDamagePerPellet == 11 &&
      decoded.weaponDamage.machineGunDamage == 13 &&
      decoded.weaponDamage.lightningGunDamage == 90 &&
      decoded.weaponDamage.railgunDamage == 50 &&
      decoded.weaponDamage.rocketLauncherDamage == 140 &&
      decoded.weaponDamage.plasmaGunDamage == 25 &&
      decoded.weaponDamage.freezeGunDamage == 95 &&
      nearlyEqual(decoded.icePoolTuning.maxRadius, 3.0F) &&
      nearlyEqual(decoded.icePoolTuning.growthPerSecond, 11.0F) &&
      nearlyEqual(decoded.icePoolTuning.lifetimeSeconds, 4.0F) &&
      nearlyEqual(decoded.icePoolTuning.friction, 0.75F) &&
      nearlyEqual(decoded.icePoolTuning.slopeGravityScale, 1.25F) &&
      nearlyEqual(decoded.icePoolTuning.controlScale, 0.25F) &&
      nearlyEqual(decoded.icePoolTuning.mergeDistance, 1.4F) &&
      !decoded.weaponAmmo.infiniteAmmo &&
      decoded.weaponAmmo.spawnAmmo == source.weaponAmmo.spawnAmmo &&
      nearlyEqual(decoded.vampirism, 2.0F) &&
      decoded.selfDamagePercent == 25 &&
      decoded.healthAmount == 150 &&
      decoded.botDodgeEnabled &&
      decoded.botDodgeMinIntervalMs == 300 &&
      decoded.botDodgeMaxIntervalMs == 700 &&
      decoded.weaponSwitchingMode == lg::WeaponSwitchingMode::Ql,
      "authoritative movement tuning should round trip"
    );
    failures += expect(decoded.playersColliding, "collision diagnostic should round trip");

    // Ordinary gameplay snapshots omit both recoverable blocks. Configuration
    // is retransmitted until acknowledged; scoreboard aggregates use their own
    // packet and never inflate the latency-sensitive snapshot transport path.
    lg::ServerSnapshot leanSnapshot;
    leanSnapshot.map = testMapDescriptor();
    leanSnapshot.connectedPlayers[0] = true;
    leanSnapshot.connectedPlayers[1] = true;
    leanSnapshot.participatingPlayers[0] = true;
    leanSnapshot.participatingPlayers[1] = true;
    leanSnapshot.hasCombatStats = false;
    leanSnapshot.hasConfiguration = false;
    failures += expect(lg::encodeServerSnapshot(leanSnapshot, wire),
                       "lean snapshot should encode");
    const std::size_t leanBytes = wire.size();
    lg::ServerSnapshot decodedLean;
    failures += expect(
      lg::decodeServerSnapshot(wire, decodedLean) &&
        !decodedLean.hasCombatStats && !decodedLean.hasConfiguration,
      "lean snapshot should advertise omitted recoverable blocks"
    );

    lg::ServerSnapshot fullConfigSnapshot = leanSnapshot;
    fullConfigSnapshot.hasConfiguration = true;
    failures += expect(lg::encodeServerSnapshot(fullConfigSnapshot, wire),
                       "configuration refresh snapshot should encode");
    const std::size_t configurationBytes = wire.size();

    lg::ServerSnapshot fullSnapshot = fullConfigSnapshot;
    fullSnapshot.hasCombatStats = true;
    failures += expect(lg::encodeServerSnapshot(fullSnapshot, wire),
                       "full refresh snapshot should encode");
    const std::size_t fullBytes = wire.size();
    failures += expect(
      fullBytes <= lg::kMaxUdpApplicationDatagramBytes,
      "compressible combat-statistics fixture should stay within one datagram"
    );
    failures += expect(leanBytes < 2500 && configurationBytes < 2500,
                       "normal and configuration refresh snapshots should stay below budget");
    std::cout << "snapshot bytes: gameplay=" << leanBytes
              << " configuration-retry=" << configurationBytes
              << " embedded-stats-fixture=" << fullBytes << '\n';

    lg::CombatStatsPacket statsPacket;
    statsPacket.serverTick = 1234;
    statsPacket.round = source.roundCombatStats;
    statsPacket.match = source.matchCombatStats;
    failures += expect(lg::encodeCombatStatsPacket(statsPacket, wire),
                       "combat statistics packet should encode");
    const std::size_t statsBytes = wire.size();
    lg::CombatStatsPacket decodedStats;
    failures += expect(
      statsBytes < 1200 &&
        lg::decodeCombatStatsPacket(wire, decodedStats) &&
        decodedStats.serverTick == statsPacket.serverTick &&
        decodedStats.match[0].weapons[0].damageDealt ==
          statsPacket.match[0].weapons[0].damageDealt,
      "combat statistics should round trip below the datagram budget"
    );
    std::cout << "combat-stats packet bytes=" << statsBytes << '\n';

    lg::CombatStatsPacket diverseStats;
    diverseStats.serverTick = 1235;
    diverseStats.firstPlayerIndex = 8;
    diverseStats.playerCount = 4;
    for (std::size_t player = 8; player < 12; ++player) {
      for (std::size_t weapon = 0; weapon < lg::kWeaponCount; ++weapon) {
        const std::uint32_t seed = static_cast<std::uint32_t>(
          (player + 1U) * 2654435761U + weapon * 2246822519U
        );
        diverseStats.round[player].weapons[weapon] = {
          seed,
          static_cast<std::uint16_t>(seed >> 8U),
          static_cast<std::uint16_t>(static_cast<std::uint16_t>(seed >> 8U) / 2U),
        };
        diverseStats.match[player].weapons[weapon] = {
          seed ^ 0xA5A5A5A5U,
          static_cast<std::uint16_t>(seed >> 3U),
          static_cast<std::uint16_t>(static_cast<std::uint16_t>(seed >> 3U) / 2U),
        };
      }
    }
    failures += expect(
      lg::encodeCombatStatsPacket(diverseStats, wire) &&
        wire.size() <= lg::kMaxUdpApplicationDatagramBytes &&
        lg::decodeCombatStatsPacket(wire, decodedStats) &&
        decodedStats.firstPlayerIndex == 8U &&
        decodedStats.playerCount == 4U &&
        decodedStats.match[11].weapons[8].damageDealt ==
          diverseStats.match[11].weapons[8].damageDealt,
      "diverse combat statistics pages should remain below the datagram ceiling"
    );
    diverseStats.firstPlayerIndex = 15;
    diverseStats.playerCount = 2;
    failures += expect(
      !lg::encodeCombatStatsPacket(diverseStats, wire),
      "combat statistics pages should reject ranges beyond player capacity"
    );
    lg::ServerSnapshot sixPlayerSnapshot = leanSnapshot;
    sixPlayerSnapshot.gameMode = lg::GameMode::ClanArena;
    sixPlayerSnapshot.matchRules.playerLimit =
      static_cast<std::uint8_t>(lg::kDuelPlayerCount);
    sixPlayerSnapshot.connectedPlayers.fill(true);
    sixPlayerSnapshot.participatingPlayers.fill(true);
    sixPlayerSnapshot.teams = {
      lg::Team::Red,
      lg::Team::Blue,
      lg::Team::Red,
      lg::Team::Blue,
      lg::Team::Red,
      lg::Team::Blue,
    };
    failures += expect(
      lg::encodeServerSnapshot(sixPlayerSnapshot, wire),
      "typical six-player snapshot should encode"
    );
    const std::size_t sixPlayerSnapshotBytes = wire.size();
    failures += expect(
      sixPlayerSnapshotBytes < 2500,
      "typical six-player snapshot should remain below 2500 bytes"
    );
    std::cout << "snapshot bytes: duel=" << leanBytes
               << " duel-full=" << fullBytes
               << " six-player=" << sixPlayerSnapshotBytes
               << " active-combat=" << activeCombatSnapshotBytes << '\n';

    lg::ServerSnapshot sixteenPlayerSnapshot = leanSnapshot;
    sixteenPlayerSnapshot.gameMode = lg::GameMode::ClanArena;
    sixteenPlayerSnapshot.matchRules.playerLimit =
      static_cast<std::uint8_t>(lg::kDuelPlayerCount);
    sixteenPlayerSnapshot.connectedPlayers.fill(true);
    sixteenPlayerSnapshot.participatingPlayers.fill(true);
    sixteenPlayerSnapshot.hasAcknowledgedCommand.fill(true);
    for (std::size_t index = 0; index < lg::kDuelPlayerCount; ++index) {
      sixteenPlayerSnapshot.acknowledgedCommand[index] =
        static_cast<std::uint32_t>(100U + index);
      sixteenPlayerSnapshot.players[index].position = {
        static_cast<float>(index),
        -static_cast<float>(index) * 0.5F,
        0.9F + static_cast<float>(index) * 0.01F,
      };
      sixteenPlayerSnapshot.selectedWeapons[index] =
        static_cast<lg::Weapon>(index % lg::kWeaponCount);
      sixteenPlayerSnapshot.teams[index] =
        (index % 2U) == 0U ? lg::Team::Red : lg::Team::Blue;
    }
    lg::LocalHitFeedbackEvent& lastWindowEvent =
      sixteenPlayerSnapshot.localHitFeedbackEvents[15][3];
    lastWindowEvent.active = true;
    lastWindowEvent.sequence = 0x12345678U;
    lastWindowEvent.targetPlayerIndex = 0;
    lastWindowEvent.damageApplied = 77;
    lastWindowEvent.headshot = true;
    lastWindowEvent.weapon = lg::Weapon::Railgun;
    failures += expect(
      lg::encodeServerSnapshot(sixteenPlayerSnapshot, wire) &&
        wire.size() <= lg::kMaxUdpApplicationDatagramBytes,
      "typical sixteen-player snapshot should fit one UDP datagram"
    );
    const std::size_t sixteenPlayerSnapshotBytes = wire.size();
    lg::ServerSnapshot decodedSixteenPlayers;
    failures += expect(
      lg::decodeServerSnapshot(wire, decodedSixteenPlayers) &&
        decodedSixteenPlayers.connectedPlayers ==
          sixteenPlayerSnapshot.connectedPlayers &&
        nearlyEqual(decodedSixteenPlayers.players[15].position.x,
                    sixteenPlayerSnapshot.players[15].position.x) &&
        nearlyEqual(decodedSixteenPlayers.players[15].position.z,
                    sixteenPlayerSnapshot.players[15].position.z) &&
        decodedSixteenPlayers.acknowledgedCommand ==
          sixteenPlayerSnapshot.acknowledgedCommand &&
        decodedSixteenPlayers.playerNames[15] == "PLAYER 16" &&
        decodedSixteenPlayers.localHitFeedbackEvents[15][3].active &&
        decodedSixteenPlayers.localHitFeedbackEvents[15][3].sequence ==
          lastWindowEvent.sequence,
      "all sixteen slots and the final hit-feedback window should round trip"
    );
    std::cout << "snapshot bytes: sixteen-player="
              << sixteenPlayerSnapshotBytes << '\n';

    lg::ServerSnapshot unboundedBurst = sixteenPlayerSnapshot;
    for (std::size_t player = 0; player < lg::kDuelPlayerCount; ++player) {
      auto& beam = unboundedBurst.lightningGuns[player];
      beam.active = true;
      beam.hit = true;
      beam.targetPlayerIndex = static_cast<std::uint8_t>((player + 1U) %
        lg::kDuelPlayerCount);
      beam.damageApplied = static_cast<int>(player + 1U);
      beam.start = {static_cast<float>(player * 17U + 1U),
                    static_cast<float>(player * 19U + 2U),
                    static_cast<float>(player * 23U + 3U)};
      beam.end = {beam.start.x + 101.25F, beam.start.y + 203.5F,
                  beam.start.z + 307.75F};
      beam.hasRewindDebug = true;
      beam.rewindTargetTick = static_cast<std::uint32_t>(500U + player);
      beam.currentTargetPosition = beam.start;
      beam.rewoundTargetPosition = beam.end;
      for (std::size_t event = 0;
           event < lg::kLocalHitFeedbackEventWindow;
           ++event) {
        auto& feedback = unboundedBurst.localHitFeedbackEvents[player][event];
        feedback.active = true;
        feedback.sequence = static_cast<std::uint32_t>(player * 101U + event);
        feedback.targetPlayerIndex = beam.targetPlayerIndex;
        feedback.damageApplied = static_cast<int>(player * 7U + event + 1U);
        feedback.weapon = lg::Weapon::LightningGun;
      }
    }
    failures += expect(
      !lg::encodeServerSnapshot(unboundedBurst, wire) && wire.empty(),
      "an incompressible event burst should fail instead of exceeding 1200 bytes"
    );

    failures += expect(
      lg::encodeServerSnapshot(sixteenPlayerSnapshot, wire) && wire[7] == 1U,
      "sixteen-player fixture should exercise compressed snapshot framing"
    );
    lg::WirePacket malformedCompressed = wire;
    malformedCompressed.pop_back();
    malformedCompressed[8] = static_cast<std::uint8_t>(
      malformedCompressed.size() - 12U
    );
    malformedCompressed[9] = static_cast<std::uint8_t>(
      (malformedCompressed.size() - 12U) >> 8U
    );
    failures += expect(
      !lg::decodeServerSnapshot(malformedCompressed, decoded),
      "truncated compressed snapshots should be rejected"
    );
    malformedCompressed = wire;
    malformedCompressed[14] |= 1U;
    malformedCompressed[15] = 0xFFU;
    malformedCompressed[16] = 0x0FU;
    failures += expect(
      !lg::decodeServerSnapshot(malformedCompressed, decoded),
      "compressed snapshots should reject back-references before output"
    );
    malformedCompressed = wire;
    malformedCompressed[12] = 0xFFU;
    malformedCompressed[13] = 0xFFU;
    failures += expect(
      !lg::decodeServerSnapshot(malformedCompressed, decoded),
      "compressed snapshot expansion must match its bounded declared size"
    );

    lg::Arena smallArena;
    lg::Arena largeArena = smallArena;
    largeArena.wallCount = 160;
    for (std::size_t wallIndex = 0; wallIndex < largeArena.wallCount; ++wallIndex) {
      const float x = static_cast<float>(wallIndex) * 2.0F;
      largeArena.walls[wallIndex] = {
        {x, 0.0F, 0.0F},
        {x + 1.0F, 1.0F, 1.0F},
      };
    }

    lg::ServerSnapshot smallMapSnapshot;
    smallMapSnapshot.serverTick = 55;
    smallMapSnapshot.mapRevision = 3;
    smallMapSnapshot.map = {"small_map", lg::hashArena(smallArena)};
    failures += expect(
      lg::encodeServerSnapshot(smallMapSnapshot, wire),
      "small map descriptor snapshot should encode"
    );
    failures += expect(
      wire.size() <= lg::kMaxPacketBytes,
      "small map descriptor snapshot should respect packet limit"
    );
    const std::size_t smallMapSnapshotBytes = wire.size();
    lg::ServerSnapshot decodedLargeMap;

    lg::ServerSnapshot largeMapSnapshot = smallMapSnapshot;
    largeMapSnapshot.map = {"large_map", lg::hashArena(largeArena)};
    failures += expect(
      lg::encodeServerSnapshot(largeMapSnapshot, wire),
      "large map descriptor snapshot should encode"
    );
    failures += expect(
      wire.size() == smallMapSnapshotBytes,
      "snapshot wire size should not scale with static map complexity"
    );
    failures += expect(
      lg::decodeServerSnapshot(wire, decodedLargeMap),
      "large map descriptor snapshot should decode"
    );
    failures += expect(
      decodedLargeMap.map.mapName == "large_map" &&
        decodedLargeMap.map.contentHash == largeMapSnapshot.map.contentHash &&
        decodedLargeMap.mapRevision == largeMapSnapshot.mapRevision,
      "large map descriptor snapshot should preserve revision without arena payload"
    );

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
    invalid.footstepAudioEvents[0].active = true;
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

    invalid = source;
    invalid.fragEvents[0].active = true;
    invalid.fragEvents[0].weapon = static_cast<lg::Weapon>(255);
    failures += expect(
      !lg::encodeServerSnapshot(invalid, wire),
      "invalid frag weapon should not encode"
    );

    lg::ServerSnapshot emptySparseSnapshot;
    emptySparseSnapshot.map = testMapDescriptor();
    emptySparseSnapshot.hasCombatStats = false;
    lg::WirePacket emptySparseWire;
    failures += expect(
      lg::encodeServerSnapshot(emptySparseSnapshot, emptySparseWire),
      "empty sparse snapshot should encode"
    );
    lg::ServerSnapshot oneLightningSnapshot = emptySparseSnapshot;
    oneLightningSnapshot.lightningGuns[0].active = true;
    lg::WirePacket oneLightningWire;
    failures += expect(
      lg::encodeServerSnapshot(oneLightningSnapshot, oneLightningWire),
      "single sparse event snapshot should encode"
    );
    std::size_t sparseMaskOffset = 12U;
    while (sparseMaskOffset < emptySparseWire.size() &&
           sparseMaskOffset < oneLightningWire.size() &&
           emptySparseWire[sparseMaskOffset] == oneLightningWire[sparseMaskOffset]) {
      ++sparseMaskOffset;
    }
    failures += expect(
      sparseMaskOffset < emptySparseWire.size(),
      "sparse mask offset should be observable from a single active slot"
    );
    if (sparseMaskOffset + 3U < emptySparseWire.size()) {
      // The first differing payload byte is the little-endian lightning mask.
      // Bit 31 is outside the six-player array and must be rejected.
      emptySparseWire[sparseMaskOffset + 3U] |= 0x80U;
      failures += expect(
        !lg::decodeServerSnapshot(emptySparseWire, decoded),
        "sparse masks should reject unused high slot bits"
      );
    }
  }

  {
    lg::ServerSnapshot source;
    source.map = testMapDescriptor();

    lg::WirePacket wire;
    lg::ServerSnapshot decoded;
    failures += expect(
      lg::encodeServerSnapshot(source, wire),
      "snapshot without embedded chat should encode"
    );
    failures += expect(
      lg::decodeServerSnapshot(wire, decoded),
      "snapshot without embedded chat should decode"
    );
    failures += expect(
      decoded.serverTick == source.serverTick,
      "snapshot without embedded chat should still round trip"
    );
  }

  {
    lg::ChatHistoryChunk source;
    source.oldestAvailableSequence = 7U;
    source.latestSequence = 10U;
    source.messageCount = 4U;
    source.messages[0] = {7U, 1U, "Zap Witch", "snyggt åäöÅÄÖ"};
    source.messages[1] = {8U, 0U, "yg", std::string(lg::kMaxChatMessageBytes, 's')};
    source.messages[2] = {9U, 1U, "Zap Witch", std::string(lg::kMaxChatMessageBytes, 't')};
    source.messages[3] = {
      10U,
      lg::kNoAssignedPlayer,
      "Observer",
      std::string(lg::kMaxChatMessageBytes, 'u')
    };
    lg::WirePacket wire;
    lg::ChatHistoryChunk decoded;
    failures += expect(
      lg::encodeChatHistoryChunk(source, wire) && wire.size() < 1200U,
      "maximum chat-history chunks should remain MTU-friendly"
    );
    failures += expect(
      lg::decodeChatHistoryChunk(wire, decoded) &&
        decoded.oldestAvailableSequence == 7U &&
        decoded.latestSequence == 10U &&
        decoded.messageCount == 4U &&
        decoded.messages[0].speakerName == "Zap Witch" &&
        decoded.messages[0].message == "snyggt åäöÅÄÖ" &&
        decoded.messages[1].message == std::string(lg::kMaxChatMessageBytes, 's') &&
        decoded.messages[3].playerIndex == lg::kNoAssignedPlayer,
      "chat history should round trip names, UTF-8, spectators, and long messages"
    );

    lg::ChatHistoryAck ack;
    failures += expect(
      lg::encodeChatHistoryAck({8U}, wire) &&
        lg::decodeChatHistoryAck(wire, ack) &&
        ack.sequence == 8U,
      "chat history acknowledgements should round trip"
    );
  }

  return failures == 0 ? 0 : 1;
}
