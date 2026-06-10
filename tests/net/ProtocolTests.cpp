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
    source.requestReset = true;
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
    failures += expect(decoded.requestReset, "reset bit should round trip");

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
    source.respawnTicksRemaining = {0, 88};
    source.playersColliding = true;

    lg::WirePacket wire;
    lg::ServerSnapshot decoded;
    failures += expect(lg::encodeServerSnapshot(source, wire), "snapshot should encode");
    failures += expect(wire.size() <= lg::kMaxPacketBytes, "snapshot should respect packet limit");
    failures += expect(lg::decodeServerSnapshot(wire, decoded), "snapshot should decode");
    failures += expect(decoded.serverTick == 1234, "snapshot tick should round trip");
    failures += expect(decoded.acknowledgedCommand[0] == 12, "snapshot ack should round trip");
    failures += expect(
      decoded.players[0].movementMode == lg::MovementMode::Flying,
      "movement mode should round trip"
    );
    failures += expect(nearlyEqual(decoded.players[0].position.z, 3.0F), "3D position should round trip");
    failures += expect(nearlyEqual(decoded.players[0].velocity.z, 4.0F), "3D velocity should round trip");
    failures += expect(decoded.players[1].health == 0, "death state should round trip");
    failures += expect(decoded.lightningGuns[0].hit, "beam hit should round trip");
    failures += expect(
      decoded.lightningGuns[0].requestedRewindTicks == 20 &&
        decoded.lightningGuns[0].appliedRewindTicks == 18 &&
        decoded.lightningGuns[0].rewindClamped,
      "rewind diagnostics should round trip"
    );
    failures += expect(
      nearlyEqual(decoded.lightningGuns[0].knockbackImpulse.z, 0.3F),
      "3D knockback should round trip"
    );
    failures += expect(decoded.respawnTicksRemaining[1] == 88, "respawn timer should round trip");
    failures += expect(decoded.playersColliding, "collision diagnostic should round trip");

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
