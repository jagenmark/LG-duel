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
    source.requestReset = true;
    source.toggleReady = true;
    source.requestMovementTuning = true;
    source.movementTuning.flightEnabled = true;
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
    source.lightningKnockback = 35.0F;
    source.vampirism = 0.1F;
    source.chatMessage = "ready?";
    source.playerName = "yg";
    source.viewedServerTick = 88;

    lg::WirePacket wire;
    lg::CommandPacket decoded;
    failures += expect(lg::encodeCommandPacket(source, wire), "command should encode");
    failures += expect(wire.size() <= lg::kMaxPacketBytes, "command should respect packet limit");
    failures += expect(lg::decodeCommandPacket(wire, decoded), "command should decode");
    failures += expect(decoded.playerIndex == source.playerIndex, "command player should round trip");
    failures += expect(decoded.command.sequence == 42, "command sequence should round trip");
    failures += expect(decoded.command.clientTick == 99, "command tick should round trip");
    failures += expect(decoded.viewedServerTick == 88, "viewed server tick should round trip");
    failures += expect(
      nearlyEqual(decoded.command.viewPitchRadians, -0.25F),
      "command pitch should round trip"
    );
    failures += expect(decoded.command.attack && decoded.command.jump, "command bits should round trip");
    failures += expect(!decoded.command.planarAim, "command aim dimensionality should round trip");
    failures += expect(decoded.chatMessage == "ready?", "chat message should round trip");
    failures += expect(decoded.playerName == "yg", "player name should round trip");
    failures += expect(decoded.requestReset, "reset bit should round trip");
    failures += expect(decoded.toggleReady, "ready bit should round trip");
    failures += expect(
      decoded.requestMovementTuning &&
        decoded.movementTuning.flightEnabled &&
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
        nearlyEqual(decoded.lightningKnockback, 35.0F) &&
        nearlyEqual(decoded.vampirism, 0.1F),
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
    for (lg::CommandPacket& command : bundle.commands) {
      command.chatMessage.assign(lg::kMaxChatMessageBytes, 'c');
      command.playerName.assign(lg::kMaxPlayerNameBytes, 'n');
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
    source.respawnTicksRemaining = {0, 88};
    source.scores = {7, 4};
    source.connectedPlayers = {true, true};
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
    source.lightningKnockback = 35.0F;
    source.vampirism = 2.0F;
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
    failures += expect(decoded.acknowledgedCommand[0] == 12, "snapshot ack should round trip");
    failures += expect(
      decoded.players[0].movementMode == lg::MovementMode::Flying &&
        decoded.players[0].jumpHeld,
      "movement mode and jump latch should round trip"
    );
    failures += expect(nearlyEqual(decoded.players[0].position.z, 3.0F), "3D position should round trip");
    failures += expect(nearlyEqual(decoded.players[0].velocity.z, 4.0F), "3D velocity should round trip");
    failures += expect(decoded.players[1].health == 0, "death state should round trip");
    failures += expect(decoded.lightningGuns[0].hit, "beam hit should round trip");
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
    failures += expect(decoded.respawnTicksRemaining[1] == 88, "respawn timer should round trip");
    failures += expect(decoded.scores == source.scores, "scores should round trip");
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
        decoded.readyPlayers == source.readyPlayers,
      "lobby state should round trip"
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
      nearlyEqual(decoded.lightningKnockback, 35.0F) &&
      nearlyEqual(decoded.vampirism, 2.0F),
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
  }

  return failures == 0 ? 0 : 1;
}
