#include "net/NetCodec.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

namespace lg {
namespace {

constexpr std::size_t kHeaderBytes = 12;

class Writer {
public:
  explicit Writer(WirePacket& wire) : wire_(wire) {
    wire_.clear();
    wire_.reserve(kMaxPacketBytes);
  }

  bool writeU8(std::uint8_t value) {
    if (wire_.size() + 1 > kMaxPacketBytes) {
      return false;
    }
    wire_.push_back(value);
    return true;
  }

  bool writeU16(std::uint16_t value) {
    return writeU8(static_cast<std::uint8_t>(value)) &&
      writeU8(static_cast<std::uint8_t>(value >> 8U));
  }

  bool writeU32(std::uint32_t value) {
    return writeU16(static_cast<std::uint16_t>(value)) &&
      writeU16(static_cast<std::uint16_t>(value >> 16U));
  }

  bool writeI32(std::int32_t value) {
    return writeU32(std::bit_cast<std::uint32_t>(value));
  }

  bool writeFloat(float value) {
    return std::isfinite(value) && writeU32(std::bit_cast<std::uint32_t>(value));
  }

  bool writeBool(bool value) {
    return writeU8(value ? 1U : 0U);
  }

  [[nodiscard]] std::size_t size() const {
    return wire_.size();
  }

  bool patchU16(std::size_t offset, std::uint16_t value) {
    if (offset + 2 > wire_.size()) {
      return false;
    }
    wire_[offset] = static_cast<std::uint8_t>(value);
    wire_[offset + 1] = static_cast<std::uint8_t>(value >> 8U);
    return true;
  }

private:
  WirePacket& wire_;
};

class Reader {
public:
  explicit Reader(const WirePacket& wire) : wire_(wire) {}

  bool readU8(std::uint8_t& value) {
    if (offset_ + 1 > wire_.size()) {
      return false;
    }
    value = wire_[offset_++];
    return true;
  }

  bool readU16(std::uint16_t& value) {
    std::uint8_t low = 0;
    std::uint8_t high = 0;
    if (!readU8(low) || !readU8(high)) {
      return false;
    }
    value = static_cast<std::uint16_t>(low) |
      (static_cast<std::uint16_t>(high) << 8U);
    return true;
  }

  bool readU32(std::uint32_t& value) {
    std::uint16_t low = 0;
    std::uint16_t high = 0;
    if (!readU16(low) || !readU16(high)) {
      return false;
    }
    value = static_cast<std::uint32_t>(low) |
      (static_cast<std::uint32_t>(high) << 16U);
    return true;
  }

  bool readI32(std::int32_t& value) {
    std::uint32_t bits = 0;
    if (!readU32(bits)) {
      return false;
    }
    value = std::bit_cast<std::int32_t>(bits);
    return true;
  }

  bool readFloat(float& value) {
    std::uint32_t bits = 0;
    if (!readU32(bits)) {
      return false;
    }
    value = std::bit_cast<float>(bits);
    return std::isfinite(value);
  }

  bool readBool(bool& value) {
    std::uint8_t encoded = 0;
    if (!readU8(encoded) || encoded > 1U) {
      return false;
    }
    value = encoded != 0;
    return true;
  }

  [[nodiscard]] std::size_t remaining() const {
    return wire_.size() - offset_;
  }

private:
  const WirePacket& wire_;
  std::size_t offset_ = 0;
};

bool writeHeader(Writer& writer, PacketType type) {
  return writer.writeU32(kProtocolMagic) &&
    writer.writeU16(kProtocolVersion) &&
    writer.writeU8(static_cast<std::uint8_t>(type)) &&
    writer.writeU8(0) &&
    writer.writeU16(0) &&
    writer.writeU16(0);
}

bool finishPacket(Writer& writer) {
  if (writer.size() < kHeaderBytes) {
    return false;
  }
  const std::size_t payloadBytes = writer.size() - kHeaderBytes;
  if (payloadBytes > std::numeric_limits<std::uint16_t>::max()) {
    return false;
  }
  return writer.patchU16(8, static_cast<std::uint16_t>(payloadBytes));
}

bool readHeader(Reader& reader, PacketType expectedType, std::size_t wireSize) {
  std::uint32_t magic = 0;
  std::uint16_t version = 0;
  std::uint8_t type = 0;
  std::uint8_t flags = 0;
  std::uint16_t payloadBytes = 0;
  std::uint16_t reserved = 0;
  return wireSize <= kMaxPacketBytes &&
    wireSize >= kHeaderBytes &&
    reader.readU32(magic) &&
    reader.readU16(version) &&
    reader.readU8(type) &&
    reader.readU8(flags) &&
    reader.readU16(payloadBytes) &&
    reader.readU16(reserved) &&
    magic == kProtocolMagic &&
    version == kProtocolVersion &&
    type == static_cast<std::uint8_t>(expectedType) &&
    flags == 0 &&
    reserved == 0 &&
    payloadBytes == wireSize - kHeaderBytes;
}

bool writeCommandBody(Writer& writer, const CommandPacket& packet) {
  const UserCommand& command = packet.command;
  return packet.playerIndex < kDuelPlayerCount &&
    writer.writeU8(packet.playerIndex) &&
    writer.writeU32(command.sequence) &&
    writer.writeU32(command.clientTick) &&
    writer.writeFloat(command.viewYawRadians) &&
    writer.writeFloat(command.viewPitchRadians) &&
    writer.writeFloat(command.forwardMove) &&
    writer.writeFloat(command.rightMove) &&
    writer.writeFloat(command.upMove) &&
    writer.writeBool(command.attack) &&
    writer.writeBool(command.jump) &&
    writer.writeBool(packet.requestReset) &&
    writer.writeBool(packet.toggleReady) &&
    writer.writeBool(packet.requestMovementTuning) &&
    writer.writeFloat(packet.movementTuning.groundAcceleration) &&
    writer.writeFloat(packet.movementTuning.groundFriction) &&
    writer.writeFloat(packet.movementTuning.maxGroundSpeed) &&
    writer.writeU32(packet.viewedServerTick);
}

bool readCommandBody(Reader& reader, CommandPacket& packet) {
  if (
    !reader.readU8(packet.playerIndex) ||
    !reader.readU32(packet.command.sequence) ||
    !reader.readU32(packet.command.clientTick) ||
    !reader.readFloat(packet.command.viewYawRadians) ||
    !reader.readFloat(packet.command.viewPitchRadians) ||
    !reader.readFloat(packet.command.forwardMove) ||
    !reader.readFloat(packet.command.rightMove) ||
    !reader.readFloat(packet.command.upMove) ||
    !reader.readBool(packet.command.attack) ||
    !reader.readBool(packet.command.jump) ||
    !reader.readBool(packet.requestReset) ||
    !reader.readBool(packet.toggleReady) ||
    !reader.readBool(packet.requestMovementTuning) ||
    !reader.readFloat(packet.movementTuning.groundAcceleration) ||
    !reader.readFloat(packet.movementTuning.groundFriction) ||
    !reader.readFloat(packet.movementTuning.maxGroundSpeed) ||
    !reader.readU32(packet.viewedServerTick)
  ) {
    return false;
  }

  return packet.playerIndex < kDuelPlayerCount &&
    std::fabs(packet.command.forwardMove) <= 1.0F &&
    std::fabs(packet.command.rightMove) <= 1.0F &&
    std::fabs(packet.command.upMove) <= 1.0F &&
    packet.movementTuning.groundAcceleration >= 0.0F &&
    packet.movementTuning.groundAcceleration <= 1000.0F &&
    packet.movementTuning.groundFriction >= 0.0F &&
    packet.movementTuning.groundFriction <= 100.0F &&
    packet.movementTuning.maxGroundSpeed >= 0.1F &&
    packet.movementTuning.maxGroundSpeed <= 100.0F;
}

bool writeVec3(Writer& writer, Vec3 value) {
  return writer.writeFloat(value.x) &&
    writer.writeFloat(value.y) &&
    writer.writeFloat(value.z);
}

bool readVec3(Reader& reader, Vec3& value) {
  return reader.readFloat(value.x) &&
    reader.readFloat(value.y) &&
    reader.readFloat(value.z);
}

bool writePlayer(Writer& writer, const PlayerState& player) {
  return writeVec3(writer, player.position) &&
    writeVec3(writer, player.velocity) &&
    writer.writeFloat(player.viewYawRadians) &&
    writer.writeFloat(player.viewPitchRadians) &&
    writer.writeI32(player.health) &&
    writer.writeFloat(player.bounds.radius) &&
    writer.writeFloat(player.bounds.halfHeight) &&
    writer.writeU8(static_cast<std::uint8_t>(player.movementMode)) &&
    writer.writeBool(player.onGround);
}

bool readPlayer(Reader& reader, PlayerState& player) {
  std::int32_t health = 0;
  std::uint8_t movementMode = 0;
  if (
    !readVec3(reader, player.position) ||
    !readVec3(reader, player.velocity) ||
    !reader.readFloat(player.viewYawRadians) ||
    !reader.readFloat(player.viewPitchRadians) ||
    !reader.readI32(health) ||
    !reader.readFloat(player.bounds.radius) ||
    !reader.readFloat(player.bounds.halfHeight) ||
    !reader.readU8(movementMode) ||
    !reader.readBool(player.onGround)
  ) {
    return false;
  }

  if (
    health < 0 ||
    health > 100000 ||
    movementMode > static_cast<std::uint8_t>(MovementMode::Flying) ||
    player.bounds.radius <= 0.0F ||
    player.bounds.radius > 100.0F ||
    player.bounds.halfHeight <= 0.0F ||
    player.bounds.halfHeight > 100.0F
  ) {
    return false;
  }

  player.health = health;
  player.movementMode = static_cast<MovementMode>(movementMode);
  return true;
}

bool writeLightningGun(Writer& writer, const LightningGunResult& result) {
  return writeVec3(writer, result.start) &&
    writeVec3(writer, result.end) &&
    writer.writeBool(result.active) &&
    writer.writeBool(result.hit) &&
    writer.writeI32(result.damageApplied) &&
    writeVec3(writer, result.knockbackImpulse) &&
    writer.writeU32(result.requestedRewindTicks) &&
    writer.writeU32(result.appliedRewindTicks) &&
    writer.writeBool(result.rewindClamped);
}

bool readLightningGun(Reader& reader, LightningGunResult& result) {
  std::int32_t damageApplied = 0;
  if (
    !readVec3(reader, result.start) ||
    !readVec3(reader, result.end) ||
    !reader.readBool(result.active) ||
    !reader.readBool(result.hit) ||
    !reader.readI32(damageApplied) ||
    !readVec3(reader, result.knockbackImpulse) ||
    !reader.readU32(result.requestedRewindTicks) ||
    !reader.readU32(result.appliedRewindTicks) ||
    !reader.readBool(result.rewindClamped)
  ) {
    return false;
  }

  if (damageApplied < 0) {
    return false;
  }
  result.damageApplied = damageApplied;
  return true;
}

bool writeRoundCombatStats(
  Writer& writer,
  const RoundCombatStats& stats
) {
  return writer.writeU32(stats.lightningActiveTicks) &&
    writer.writeU32(stats.lightningHitTicks) &&
    writer.writeU32(stats.damageDealt);
}

bool readRoundCombatStats(
  Reader& reader,
  RoundCombatStats& stats
) {
  return reader.readU32(stats.lightningActiveTicks) &&
    reader.readU32(stats.lightningHitTicks) &&
    reader.readU32(stats.damageDealt) &&
    stats.lightningHitTicks <= stats.lightningActiveTicks;
}

} // namespace

bool inspectPacketType(const WirePacket& wire, PacketType& type) {
  if (wire.size() < kHeaderBytes || wire.size() > kMaxPacketBytes) {
    return false;
  }

  Reader reader(wire);
  std::uint32_t magic = 0;
  std::uint16_t version = 0;
  std::uint8_t encodedType = 0;
  std::uint8_t flags = 0;
  std::uint16_t payloadBytes = 0;
  std::uint16_t reserved = 0;
  if (
    !reader.readU32(magic) ||
    !reader.readU16(version) ||
    !reader.readU8(encodedType) ||
    !reader.readU8(flags) ||
    !reader.readU16(payloadBytes) ||
    !reader.readU16(reserved) ||
    magic != kProtocolMagic ||
    version != kProtocolVersion ||
    flags != 0 ||
    reserved != 0 ||
    payloadBytes != wire.size() - kHeaderBytes ||
    encodedType < static_cast<std::uint8_t>(PacketType::ConnectRequest) ||
    encodedType > static_cast<std::uint8_t>(PacketType::Disconnect)
  ) {
    return false;
  }

  type = static_cast<PacketType>(encodedType);
  return true;
}

bool encodeConnectRequest(const ConnectRequest& packet, WirePacket& wire) {
  Writer writer(wire);
  return writeHeader(writer, PacketType::ConnectRequest) &&
    writer.writeU32(packet.clientNonce) &&
    finishPacket(writer);
}

bool decodeConnectRequest(const WirePacket& wire, ConnectRequest& packet) {
  Reader reader(wire);
  ConnectRequest decoded;
  if (
    !readHeader(reader, PacketType::ConnectRequest, wire.size()) ||
    !reader.readU32(decoded.clientNonce) ||
    reader.remaining() != 0
  ) {
    return false;
  }
  packet = decoded;
  return true;
}

bool encodeConnectAccept(const ConnectAccept& packet, WirePacket& wire) {
  Writer writer(wire);
  return packet.playerIndex < kDuelPlayerCount &&
    writeHeader(writer, PacketType::ConnectAccept) &&
    writer.writeU32(packet.clientNonce) &&
    writer.writeU8(packet.playerIndex) &&
    writer.writeU32(packet.serverTick) &&
    finishPacket(writer);
}

bool decodeConnectAccept(const WirePacket& wire, ConnectAccept& packet) {
  Reader reader(wire);
  ConnectAccept decoded;
  if (
    !readHeader(reader, PacketType::ConnectAccept, wire.size()) ||
    !reader.readU32(decoded.clientNonce) ||
    !reader.readU8(decoded.playerIndex) ||
    !reader.readU32(decoded.serverTick) ||
    decoded.playerIndex >= kDuelPlayerCount ||
    reader.remaining() != 0
  ) {
    return false;
  }
  packet = decoded;
  return true;
}

bool encodeCommandPacket(const CommandPacket& packet, WirePacket& wire) {
  Writer writer(wire);
  return writeHeader(writer, PacketType::Command) &&
    writeCommandBody(writer, packet) &&
    finishPacket(writer);
}

bool decodeCommandPacket(const WirePacket& wire, CommandPacket& packet) {
  Reader reader(wire);
  CommandPacket decoded;
  if (
    !readHeader(reader, PacketType::Command, wire.size()) ||
    !readCommandBody(reader, decoded) ||
    reader.remaining() != 0
  ) {
    return false;
  }

  packet = decoded;
  return true;
}

bool encodeCommandBundle(const CommandBundle& bundle, WirePacket& wire) {
  if (bundle.commandCount == 0 || bundle.commandCount > kMaxBundledCommands) {
    return false;
  }

  Writer writer(wire);
  if (
    !writeHeader(writer, PacketType::CommandBundle) ||
    !writer.writeU8(bundle.commandCount)
  ) {
    return false;
  }
  for (std::size_t index = 0; index < bundle.commandCount; ++index) {
    if (!writeCommandBody(writer, bundle.commands[index])) {
      return false;
    }
  }
  return finishPacket(writer);
}

bool decodeCommandBundle(const WirePacket& wire, CommandBundle& bundle) {
  Reader reader(wire);
  CommandBundle decoded;
  if (
    !readHeader(reader, PacketType::CommandBundle, wire.size()) ||
    !reader.readU8(decoded.commandCount) ||
    decoded.commandCount == 0 ||
    decoded.commandCount > kMaxBundledCommands
  ) {
    return false;
  }
  for (std::size_t index = 0; index < decoded.commandCount; ++index) {
    if (!readCommandBody(reader, decoded.commands[index])) {
      return false;
    }
  }
  if (reader.remaining() != 0) {
    return false;
  }
  bundle = decoded;
  return true;
}

bool encodeServerSnapshot(const ServerSnapshot& snapshot, WirePacket& wire) {
  Writer writer(wire);
  if (!writeHeader(writer, PacketType::Snapshot) || !writer.writeU32(snapshot.serverTick)) {
    return false;
  }

  for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
    if (
      !writer.writeU32(snapshot.acknowledgedCommand[index]) ||
      !writer.writeBool(snapshot.hasAcknowledgedCommand[index])
    ) {
      return false;
    }
  }
  for (const PlayerState& player : snapshot.players) {
    if (!writePlayer(writer, player)) {
      return false;
    }
  }
  for (const LightningGunResult& result : snapshot.lightningGuns) {
    if (!writeLightningGun(writer, result)) {
      return false;
    }
  }
  for (std::uint32_t ticks : snapshot.respawnTicksRemaining) {
    if (!writer.writeU32(ticks)) {
      return false;
    }
  }
  for (std::uint16_t score : snapshot.scores) {
    if (!writer.writeU16(score)) {
      return false;
    }
  }
  for (bool connected : snapshot.connectedPlayers) {
    if (!writer.writeBool(connected)) {
      return false;
    }
  }
  for (bool ready : snapshot.readyPlayers) {
    if (!writer.writeBool(ready)) {
      return false;
    }
  }
  for (const RoundCombatStats& stats : snapshot.roundCombatStats) {
    if (!writeRoundCombatStats(writer, stats)) {
      return false;
    }
  }
  return writer.writeU8(static_cast<std::uint8_t>(snapshot.matchPhase)) &&
    writer.writeU16(snapshot.matchRules.roundLimit) &&
    writer.writeU16(snapshot.matchRules.timeLimitMinutes) &&
    writer.writeU8(snapshot.matchRules.playerLimit) &&
    writer.writeU16(snapshot.matchRules.countdownTicks) &&
    writer.writeU16(snapshot.matchRules.roundEndTicks) &&
    writer.writeU16(snapshot.matchRules.matchEndTicks) &&
    writer.writeBool(snapshot.matchRules.showOpponentHealth) &&
    writer.writeFloat(snapshot.movementTuning.groundAcceleration) &&
    writer.writeFloat(snapshot.movementTuning.groundFriction) &&
    writer.writeFloat(snapshot.movementTuning.maxGroundSpeed) &&
    writer.writeU32(snapshot.phaseTicksRemaining) &&
    writer.writeU32(snapshot.liveTicksElapsed) &&
    writer.writeU8(snapshot.roundWinner) &&
    writer.writeU8(snapshot.matchWinner) &&
    writer.writeBool(snapshot.playersColliding) &&
    finishPacket(writer);
}

bool decodeServerSnapshot(const WirePacket& wire, ServerSnapshot& snapshot) {
  Reader reader(wire);
  ServerSnapshot decoded;
  if (!readHeader(reader, PacketType::Snapshot, wire.size()) || !reader.readU32(decoded.serverTick)) {
    return false;
  }

  for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
    if (
      !reader.readU32(decoded.acknowledgedCommand[index]) ||
      !reader.readBool(decoded.hasAcknowledgedCommand[index])
    ) {
      return false;
    }
  }
  for (PlayerState& player : decoded.players) {
    if (!readPlayer(reader, player)) {
      return false;
    }
  }
  for (LightningGunResult& result : decoded.lightningGuns) {
    if (!readLightningGun(reader, result)) {
      return false;
    }
  }
  for (std::uint32_t& ticks : decoded.respawnTicksRemaining) {
    if (!reader.readU32(ticks)) {
      return false;
    }
  }
  for (std::uint16_t& score : decoded.scores) {
    if (!reader.readU16(score)) {
      return false;
    }
  }
  for (bool& connected : decoded.connectedPlayers) {
    if (!reader.readBool(connected)) {
      return false;
    }
  }
  for (bool& ready : decoded.readyPlayers) {
    if (!reader.readBool(ready)) {
      return false;
    }
  }
  for (RoundCombatStats& stats : decoded.roundCombatStats) {
    if (!readRoundCombatStats(reader, stats)) {
      return false;
    }
  }
  std::uint8_t matchPhase = 0;
  if (
    !reader.readU8(matchPhase) ||
    matchPhase > static_cast<std::uint8_t>(MatchPhase::MatchEnd) ||
    !reader.readU16(decoded.matchRules.roundLimit) ||
    !reader.readU16(decoded.matchRules.timeLimitMinutes) ||
    !reader.readU8(decoded.matchRules.playerLimit) ||
    !reader.readU16(decoded.matchRules.countdownTicks) ||
    !reader.readU16(decoded.matchRules.roundEndTicks) ||
    !reader.readU16(decoded.matchRules.matchEndTicks) ||
    !reader.readBool(decoded.matchRules.showOpponentHealth) ||
    !reader.readFloat(decoded.movementTuning.groundAcceleration) ||
    !reader.readFloat(decoded.movementTuning.groundFriction) ||
    !reader.readFloat(decoded.movementTuning.maxGroundSpeed) ||
    !reader.readU32(decoded.phaseTicksRemaining) ||
    !reader.readU32(decoded.liveTicksElapsed) ||
    !reader.readU8(decoded.roundWinner) ||
    !reader.readU8(decoded.matchWinner) ||
    !reader.readBool(decoded.playersColliding) ||
    decoded.matchRules.roundLimit == 0 ||
    decoded.matchRules.playerLimit == 0 ||
    decoded.matchRules.playerLimit > kDuelPlayerCount ||
    decoded.movementTuning.groundAcceleration < 0.0F ||
    decoded.movementTuning.groundAcceleration > 1000.0F ||
    decoded.movementTuning.groundFriction < 0.0F ||
    decoded.movementTuning.groundFriction > 100.0F ||
    decoded.movementTuning.maxGroundSpeed < 0.1F ||
    decoded.movementTuning.maxGroundSpeed > 100.0F ||
    (decoded.roundWinner != 255 && decoded.roundWinner >= kDuelPlayerCount) ||
    (decoded.matchWinner != 255 && decoded.matchWinner >= kDuelPlayerCount) ||
    reader.remaining() != 0
  ) {
    return false;
  }
  decoded.matchPhase = static_cast<MatchPhase>(matchPhase);

  snapshot = decoded;
  return true;
}

bool encodePingPacket(PacketType type, const PingPacket& packet, WirePacket& wire) {
  if (type != PacketType::Ping && type != PacketType::Pong) {
    return false;
  }
  Writer writer(wire);
  return writeHeader(writer, type) &&
    writer.writeU32(packet.token) &&
    finishPacket(writer);
}

bool decodePingPacket(
  const WirePacket& wire,
  PacketType expectedType,
  PingPacket& packet
) {
  if (expectedType != PacketType::Ping && expectedType != PacketType::Pong) {
    return false;
  }
  Reader reader(wire);
  PingPacket decoded;
  if (
    !readHeader(reader, expectedType, wire.size()) ||
    !reader.readU32(decoded.token) ||
    reader.remaining() != 0
  ) {
    return false;
  }
  packet = decoded;
  return true;
}

bool encodeDisconnectPacket(const DisconnectPacket& packet, WirePacket& wire) {
  Writer writer(wire);
  return writeHeader(writer, PacketType::Disconnect) &&
    writer.writeU32(packet.clientNonce) &&
    finishPacket(writer);
}

bool decodeDisconnectPacket(
  const WirePacket& wire,
  DisconnectPacket& packet
) {
  Reader reader(wire);
  DisconnectPacket decoded;
  if (
    !readHeader(reader, PacketType::Disconnect, wire.size()) ||
    !reader.readU32(decoded.clientNonce) ||
    reader.remaining() != 0
  ) {
    return false;
  }
  packet = decoded;
  return true;
}

} // namespace lg
