#include "net/NetCodec.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

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

  bool writeString(const std::string& value, std::size_t maxBytes) {
    if (value.size() > maxBytes || value.size() > 255U || !writeU8(
      static_cast<std::uint8_t>(value.size())
    )) {
      return false;
    }
    for (const unsigned char character : value) {
      if (character < 32U || character == 127U || !writeU8(character)) {
        return false;
      }
    }
    return true;
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

  bool readString(std::string& value, std::size_t maxBytes) {
    std::uint8_t size = 0;
    if (!readU8(size) || size > maxBytes || remaining() < size) {
      return false;
    }
    value.clear();
    value.reserve(size);
    for (std::uint8_t index = 0; index < size; ++index) {
      std::uint8_t character = 0;
      if (!readU8(character) || character < 32U || character == 127U) {
        return false;
      }
      value.push_back(static_cast<char>(character));
    }
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
    isValidGameMode(packet.requestedGameMode) &&
    isValidTeam(packet.requestedTeam) &&
    writer.writeU8(packet.playerIndex) &&
    writer.writeU32(packet.clientNonce) &&
    writer.writeU32(command.sequence) &&
    writer.writeU32(command.clientTick) &&
    writer.writeFloat(command.viewYawRadians) &&
    writer.writeFloat(command.viewPitchRadians) &&
    writer.writeFloat(command.forwardMove) &&
    writer.writeFloat(command.rightMove) &&
    writer.writeFloat(command.upMove) &&
    writer.writeBool(command.attack) &&
    writer.writeBool(command.jump) &&
    writer.writeBool(command.planarAim) &&
    writer.writeU8(static_cast<std::uint8_t>(command.weapon)) &&
    writer.writeBool(packet.requestReset) &&
    writer.writeBool(packet.toggleReady) &&
    writer.writeBool(packet.requestMovementTuning) &&
    writer.writeBool(packet.movementTuning.flightEnabled) &&
    writer.writeBool(packet.movementTuning.airControlEnabled) &&
    writer.writeFloat(packet.movementTuning.groundAcceleration) &&
    writer.writeFloat(packet.movementTuning.airAcceleration) &&
    writer.writeFloat(packet.movementTuning.groundFriction) &&
    writer.writeFloat(packet.movementTuning.stopSpeed) &&
    writer.writeFloat(packet.movementTuning.maxGroundSpeed) &&
    writer.writeFloat(packet.movementTuning.flightAcceleration) &&
    writer.writeFloat(packet.movementTuning.maxFlightSpeed) &&
    writer.writeFloat(packet.movementTuning.flightDamping) &&
    writer.writeFloat(packet.movementTuning.flightGravityCancel) &&
    writer.writeFloat(packet.playerSizeScaleXY) &&
    writer.writeFloat(packet.playerSizeScaleZ) &&
    writer.writeFloat(packet.lightningKnockback) &&
    writer.writeFloat(packet.rocketKnockback) &&
    writer.writeI32(packet.weaponDamage.shotgunDamagePerPellet) &&
    writer.writeI32(packet.weaponDamage.machineGunDamage) &&
    writer.writeI32(packet.weaponDamage.lightningGunDamage) &&
    writer.writeI32(packet.weaponDamage.railgunDamage) &&
    writer.writeI32(packet.weaponDamage.rocketLauncherDamage) &&
    writer.writeFloat(packet.vampirism) &&
    writer.writeU8(packet.selfDamagePercent) &&
    writer.writeI32(packet.healthAmount) &&
    writer.writeBool(packet.botDodgeEnabled) &&
    writer.writeI32(packet.botDodgeMinIntervalMs) &&
    writer.writeI32(packet.botDodgeMaxIntervalMs) &&
      writer.writeU32(packet.viewedServerTick) &&
      writer.writeString(packet.chatMessage, kMaxChatMessageBytes) &&
      writer.writeString(packet.playerName, kMaxPlayerNameBytes) &&
      writer.writeString(packet.mapName, kMaxMapNameBytes) &&
      writer.writeBool(packet.requestGameMode) &&
      writer.writeU8(static_cast<std::uint8_t>(packet.requestedGameMode)) &&
      writer.writeBool(packet.requestTeam) &&
      writer.writeU8(static_cast<std::uint8_t>(packet.requestedTeam));
}

bool readCommandBody(Reader& reader, CommandPacket& packet) {
  std::uint8_t weapon = 0;
  std::uint8_t requestedGameMode = 0;
  std::uint8_t requestedTeam = 0;
  if (
    !reader.readU8(packet.playerIndex) ||
    !reader.readU32(packet.clientNonce) ||
    !reader.readU32(packet.command.sequence) ||
    !reader.readU32(packet.command.clientTick) ||
    !reader.readFloat(packet.command.viewYawRadians) ||
    !reader.readFloat(packet.command.viewPitchRadians) ||
    !reader.readFloat(packet.command.forwardMove) ||
    !reader.readFloat(packet.command.rightMove) ||
    !reader.readFloat(packet.command.upMove) ||
    !reader.readBool(packet.command.attack) ||
    !reader.readBool(packet.command.jump) ||
    !reader.readBool(packet.command.planarAim) ||
    !reader.readU8(weapon) ||
    !reader.readBool(packet.requestReset) ||
    !reader.readBool(packet.toggleReady) ||
    !reader.readBool(packet.requestMovementTuning) ||
    !reader.readBool(packet.movementTuning.flightEnabled) ||
    !reader.readBool(packet.movementTuning.airControlEnabled) ||
    !reader.readFloat(packet.movementTuning.groundAcceleration) ||
    !reader.readFloat(packet.movementTuning.airAcceleration) ||
    !reader.readFloat(packet.movementTuning.groundFriction) ||
    !reader.readFloat(packet.movementTuning.stopSpeed) ||
    !reader.readFloat(packet.movementTuning.maxGroundSpeed) ||
    !reader.readFloat(packet.movementTuning.flightAcceleration) ||
    !reader.readFloat(packet.movementTuning.maxFlightSpeed) ||
    !reader.readFloat(packet.movementTuning.flightDamping) ||
    !reader.readFloat(packet.movementTuning.flightGravityCancel) ||
    !reader.readFloat(packet.playerSizeScaleXY) ||
    !reader.readFloat(packet.playerSizeScaleZ) ||
    !reader.readFloat(packet.lightningKnockback) ||
    !reader.readFloat(packet.rocketKnockback) ||
    !reader.readI32(packet.weaponDamage.shotgunDamagePerPellet) ||
    !reader.readI32(packet.weaponDamage.machineGunDamage) ||
    !reader.readI32(packet.weaponDamage.lightningGunDamage) ||
    !reader.readI32(packet.weaponDamage.railgunDamage) ||
    !reader.readI32(packet.weaponDamage.rocketLauncherDamage) ||
    !reader.readFloat(packet.vampirism) ||
    !reader.readU8(packet.selfDamagePercent) ||
    !reader.readI32(packet.healthAmount) ||
    !reader.readBool(packet.botDodgeEnabled) ||
    !reader.readI32(packet.botDodgeMinIntervalMs) ||
    !reader.readI32(packet.botDodgeMaxIntervalMs) ||
      !reader.readU32(packet.viewedServerTick) ||
      !reader.readString(packet.chatMessage, kMaxChatMessageBytes) ||
      !reader.readString(packet.playerName, kMaxPlayerNameBytes) ||
      !reader.readString(packet.mapName, kMaxMapNameBytes) ||
      !reader.readBool(packet.requestGameMode) ||
      !reader.readU8(requestedGameMode) ||
      !reader.readBool(packet.requestTeam) ||
      !reader.readU8(requestedTeam)
    ) {
    return false;
  }

  const bool valid = packet.playerIndex < kDuelPlayerCount &&
    weapon <= static_cast<std::uint8_t>(kLastWeapon) &&
    requestedGameMode <= static_cast<std::uint8_t>(GameMode::ClanArena) &&
    requestedTeam <= static_cast<std::uint8_t>(Team::Blue) &&
    std::fabs(packet.command.forwardMove) <= 1.0F &&
    std::fabs(packet.command.rightMove) <= 1.0F &&
    std::fabs(packet.command.upMove) <= 1.0F &&
    packet.movementTuning.groundAcceleration >= 0.0F &&
    packet.movementTuning.groundAcceleration <= 1000.0F &&
    packet.movementTuning.airAcceleration >= 0.0F &&
    packet.movementTuning.airAcceleration <= 1000.0F &&
    packet.movementTuning.groundFriction >= 0.0F &&
    packet.movementTuning.groundFriction <= 100.0F &&
    packet.movementTuning.stopSpeed >= 0.0F &&
    packet.movementTuning.stopSpeed <= 100.0F &&
    packet.movementTuning.maxGroundSpeed >= 0.1F &&
    packet.movementTuning.maxGroundSpeed <= 100.0F &&
    packet.movementTuning.flightAcceleration >= 0.0F &&
    packet.movementTuning.flightAcceleration <= 1000.0F &&
    packet.movementTuning.maxFlightSpeed >= 0.1F &&
    packet.movementTuning.maxFlightSpeed <= 100.0F &&
    packet.movementTuning.flightDamping >= 0.0F &&
    packet.movementTuning.flightDamping <= 100.0F &&
    packet.movementTuning.flightGravityCancel >= 0.0F &&
    packet.movementTuning.flightGravityCancel <= 1.0F &&
    packet.playerSizeScaleXY >= 0.5F &&
    packet.playerSizeScaleXY <= 3.0F &&
    packet.playerSizeScaleZ >= 0.5F &&
    packet.playerSizeScaleZ <= 3.0F &&
    packet.lightningKnockback >= 0.0F &&
    packet.lightningKnockback <= kMaxLightningKnockback &&
    packet.rocketKnockback >= 0.0F &&
    packet.rocketKnockback <= kMaxRocketKnockback &&
    packet.weaponDamage.shotgunDamagePerPellet >= 1 &&
    packet.weaponDamage.shotgunDamagePerPellet <= 500 &&
    packet.weaponDamage.machineGunDamage >= 1 &&
    packet.weaponDamage.machineGunDamage <= 500 &&
    packet.weaponDamage.lightningGunDamage >= 1 &&
    packet.weaponDamage.lightningGunDamage <= 500 &&
    packet.weaponDamage.railgunDamage >= 1 &&
    packet.weaponDamage.railgunDamage <= 500 &&
    packet.weaponDamage.rocketLauncherDamage >= 1 &&
    packet.weaponDamage.rocketLauncherDamage <= 500 &&
    packet.vampirism >= 0.0F &&
    packet.vampirism <= 2.0F &&
    packet.selfDamagePercent <= 100 &&
    packet.healthAmount >= 1 &&
    packet.healthAmount <= 100000 &&
    packet.botDodgeMinIntervalMs >= 1 &&
    packet.botDodgeMinIntervalMs <= 10000 &&
    packet.botDodgeMaxIntervalMs >= 1 &&
    packet.botDodgeMaxIntervalMs <= 10000;
  if (!valid) {
    return false;
  }
  packet.command.weapon = static_cast<Weapon>(weapon);
  packet.requestedGameMode = static_cast<GameMode>(requestedGameMode);
  packet.requestedTeam = static_cast<Team>(requestedTeam);
  return true;
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

bool writeArena(Writer& writer, const Arena& arena) {
  if (arena.wallCount > Arena::kWallCount) {
    return false;
  }
  return writeVec3(writer, arena.min) &&
    writeVec3(writer, arena.max) &&
    writer.writeU8(static_cast<std::uint8_t>(arena.wallCount)) &&
    [&]() {
      for (std::size_t index = 0; index < arena.wallCount; ++index) {
        if (
          !writeVec3(writer, arena.walls[index].min) ||
          !writeVec3(writer, arena.walls[index].max)
        ) {
          return false;
        }
      }
      for (const Vec3& spawn : arena.spawnPositions) {
        if (!writeVec3(writer, spawn)) {
          return false;
        }
      }
      return true;
    }();
}

bool readArena(Reader& reader, Arena& arena) {
  Arena decoded;
  std::uint8_t wallCount = 0;
  if (
    !readVec3(reader, decoded.min) ||
    !readVec3(reader, decoded.max) ||
    !reader.readU8(wallCount) ||
    wallCount > Arena::kWallCount
  ) {
    return false;
  }
  decoded.wallCount = wallCount;
  for (std::size_t index = 0; index < decoded.wallCount; ++index) {
    if (
      !readVec3(reader, decoded.walls[index].min) ||
      !readVec3(reader, decoded.walls[index].max)
    ) {
      return false;
    }
  }
  for (Vec3& spawn : decoded.spawnPositions) {
    if (!readVec3(reader, spawn)) {
      return false;
    }
  }

  if (
    decoded.min.x >= decoded.max.x ||
    decoded.min.y >= decoded.max.y ||
    decoded.min.z >= decoded.max.z
  ) {
    return false;
  }
  for (std::size_t index = 0; index < decoded.wallCount; ++index) {
    const ArenaWall& wall = decoded.walls[index];
    if (
      wall.min.x >= wall.max.x ||
      wall.min.y >= wall.max.y ||
      wall.min.z >= wall.max.z
    ) {
      return false;
    }
  }

  arena = decoded;
  return true;
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
    writer.writeBool(player.onGround) &&
    writer.writeBool(player.jumpHeld);
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
    !reader.readBool(player.onGround) ||
    !reader.readBool(player.jumpHeld)
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
    writer.writeU8(result.targetPlayerIndex) &&
    writer.writeI32(result.damageApplied) &&
    writeVec3(writer, result.knockbackImpulse) &&
    writer.writeU32(result.requestedRewindTicks) &&
    writer.writeU32(result.appliedRewindTicks) &&
    writer.writeBool(result.rewindClamped) &&
    writer.writeBool(result.hasRewindDebug) &&
    writer.writeU32(result.rewindTargetTick) &&
    writeVec3(writer, result.currentTargetPosition) &&
    writeVec3(writer, result.rewoundTargetPosition) &&
    writer.writeFloat(result.currentTargetBounds.radius) &&
    writer.writeFloat(result.currentTargetBounds.halfHeight) &&
    writer.writeFloat(result.rewoundTargetBounds.radius) &&
    writer.writeFloat(result.rewoundTargetBounds.halfHeight);
}

bool readLightningGun(Reader& reader, LightningGunResult& result) {
  std::int32_t damageApplied = 0;
  std::uint8_t targetPlayerIndex = 255;
  if (
    !readVec3(reader, result.start) ||
    !readVec3(reader, result.end) ||
    !reader.readBool(result.active) ||
    !reader.readBool(result.hit) ||
    !reader.readU8(targetPlayerIndex) ||
    !reader.readI32(damageApplied) ||
    !readVec3(reader, result.knockbackImpulse) ||
    !reader.readU32(result.requestedRewindTicks) ||
    !reader.readU32(result.appliedRewindTicks) ||
    !reader.readBool(result.rewindClamped) ||
    !reader.readBool(result.hasRewindDebug) ||
    !reader.readU32(result.rewindTargetTick) ||
    !readVec3(reader, result.currentTargetPosition) ||
    !readVec3(reader, result.rewoundTargetPosition) ||
    !reader.readFloat(result.currentTargetBounds.radius) ||
    !reader.readFloat(result.currentTargetBounds.halfHeight) ||
    !reader.readFloat(result.rewoundTargetBounds.radius) ||
    !reader.readFloat(result.rewoundTargetBounds.halfHeight)
  ) {
    return false;
  }

  if (
    damageApplied < 0 ||
    (targetPlayerIndex != 255 && targetPlayerIndex >= kDuelPlayerCount) ||
    result.currentTargetBounds.radius <= 0.0F ||
    result.currentTargetBounds.radius > 100.0F ||
    result.currentTargetBounds.halfHeight <= 0.0F ||
    result.currentTargetBounds.halfHeight > 100.0F ||
    result.rewoundTargetBounds.radius <= 0.0F ||
    result.rewoundTargetBounds.radius > 100.0F ||
    result.rewoundTargetBounds.halfHeight <= 0.0F ||
    result.rewoundTargetBounds.halfHeight > 100.0F
  ) {
    return false;
  }
  result.targetPlayerIndex = targetPlayerIndex;
  result.damageApplied = damageApplied;
  return true;
}

bool writeWeaponFire(Writer& writer, const WeaponFireResult& result) {
  return writeVec3(writer, result.start) &&
    writeVec3(writer, result.end) &&
    writer.writeBool(result.fired) &&
    writer.writeBool(result.hit) &&
    writer.writeU8(static_cast<std::uint8_t>(result.weapon)) &&
    writer.writeI32(result.damageApplied) &&
    writeVec3(writer, result.knockbackImpulse) &&
    writer.writeU8(result.pelletCount) &&
    writer.writeU8(result.pelletHitCount);
}

bool readWeaponFire(Reader& reader, WeaponFireResult& result) {
  std::uint8_t weapon = 0;
  std::int32_t damageApplied = 0;
  std::uint8_t pelletCount = 0;
  std::uint8_t pelletHitCount = 0;
  if (
    !readVec3(reader, result.start) ||
    !readVec3(reader, result.end) ||
    !reader.readBool(result.fired) ||
    !reader.readBool(result.hit) ||
    !reader.readU8(weapon) ||
    !reader.readI32(damageApplied) ||
    !readVec3(reader, result.knockbackImpulse) ||
    !reader.readU8(pelletCount) ||
    !reader.readU8(pelletHitCount)
  ) {
    return false;
  }
  if (
    weapon > static_cast<std::uint8_t>(kLastWeapon) ||
    damageApplied < 0 ||
    pelletHitCount > pelletCount ||
    pelletCount > kShotgunPelletCount
  ) {
    return false;
  }
  result.weapon = static_cast<Weapon>(weapon);
  result.damageApplied = damageApplied;
  result.pelletCount = pelletCount;
  result.pelletHitCount = pelletHitCount;
  return true;
}

bool writeRocketExplosion(Writer& writer, const RocketExplosionResult& result) {
  return writeVec3(writer, result.position) &&
    writer.writeFloat(result.radius) &&
    writer.writeI32(result.ownerDamageApplied) &&
    writer.writeI32(result.opponentDamageApplied) &&
    writer.writeBool(result.active);
}

bool readRocketExplosion(Reader& reader, RocketExplosionResult& result) {
  std::int32_t ownerDamageApplied = 0;
  std::int32_t opponentDamageApplied = 0;
  if (
    !readVec3(reader, result.position) ||
    !reader.readFloat(result.radius) ||
    !reader.readI32(ownerDamageApplied) ||
    !reader.readI32(opponentDamageApplied) ||
    !reader.readBool(result.active)
  ) {
    return false;
  }
  if (
    result.radius < 0.0F ||
    result.radius > 100.0F ||
    ownerDamageApplied < 0 ||
    opponentDamageApplied < 0
  ) {
    return false;
  }
  result.ownerDamageApplied = ownerDamageApplied;
  result.opponentDamageApplied = opponentDamageApplied;
  return true;
}

bool writeFootstepAudioEvent(Writer& writer, const FootstepAudioEvent& event) {
  return writer.writeBool(event.active) &&
    writer.writeU32(event.sequence) &&
    writeVec3(writer, event.position);
}

bool readFootstepAudioEvent(Reader& reader, FootstepAudioEvent& event) {
  return reader.readBool(event.active) &&
    reader.readU32(event.sequence) &&
    readVec3(reader, event.position);
}

bool writeFragEvent(Writer& writer, const FragEvent& event) {
  if (event.active && event.targetPlayerIndex >= kDuelPlayerCount) {
    return false;
  }
  return writer.writeBool(event.active) &&
    writer.writeU8(event.targetPlayerIndex);
}

bool readFragEvent(Reader& reader, FragEvent& event) {
  if (
    !reader.readBool(event.active) ||
    !reader.readU8(event.targetPlayerIndex)
  ) {
    return false;
  }
  return !event.active || event.targetPlayerIndex < kDuelPlayerCount;
}

bool writeRocketProjectile(
  Writer& writer,
  const RocketProjectileSnapshot& projectile
) {
  return writer.writeBool(projectile.active) &&
    writer.writeU8(projectile.owner) &&
    writeVec3(writer, projectile.position);
}

bool readRocketProjectile(
  Reader& reader,
  RocketProjectileSnapshot& projectile
) {
  return reader.readBool(projectile.active) &&
    reader.readU8(projectile.owner) &&
    readVec3(reader, projectile.position) &&
    projectile.owner < kDuelPlayerCount;
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
  if (
    !isValidGameMode(snapshot.gameMode) ||
    !isValidTeam(snapshot.roundWinningTeam) ||
    !isValidTeam(snapshot.matchWinningTeam) ||
    !std::all_of(
      snapshot.teams.begin(),
      snapshot.teams.end(),
      [](Team team) { return isValidTeam(team); }
    )
  ) {
    return false;
  }

  Writer writer(wire);
  if (
    !writeHeader(writer, PacketType::Snapshot) ||
    !writer.writeU32(snapshot.serverTick) ||
    !writer.writeU32(snapshot.mapRevision) ||
    !writeArena(writer, snapshot.arena)
  ) {
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
  for (const WeaponFireResult& result : snapshot.weaponFires) {
    if (!writeWeaponFire(writer, result)) {
      return false;
    }
  }
  for (const RocketExplosionResult& result : snapshot.rocketExplosions) {
    if (!writeRocketExplosion(writer, result)) {
      return false;
    }
  }
  for (const FootstepAudioEvent& event : snapshot.footstepAudioEvents) {
    if (!writeFootstepAudioEvent(writer, event)) {
      return false;
    }
  }
  for (const FragEvent& event : snapshot.fragEvents) {
    if (!writeFragEvent(writer, event)) {
      return false;
    }
  }
  for (const RocketProjectileSnapshot& projectile : snapshot.rockets) {
    if (!writeRocketProjectile(writer, projectile)) {
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
  if (!writer.writeU8(static_cast<std::uint8_t>(snapshot.gameMode))) {
    return false;
  }
  for (Team team : snapshot.teams) {
    if (!writer.writeU8(static_cast<std::uint8_t>(team))) {
      return false;
    }
  }
  for (std::uint16_t score : snapshot.teamScores) {
    if (!writer.writeU16(score)) {
      return false;
    }
  }
  if (
    !writer.writeU8(static_cast<std::uint8_t>(snapshot.roundWinningTeam)) ||
    !writer.writeU8(static_cast<std::uint8_t>(snapshot.matchWinningTeam))
  ) {
    return false;
  }
  for (bool connected : snapshot.connectedPlayers) {
    if (!writer.writeBool(connected)) {
      return false;
    }
  }
  for (bool participating : snapshot.participatingPlayers) {
    if (!writer.writeBool(participating)) {
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
  for (const RoundCombatStats& stats : snapshot.matchCombatStats) {
    if (!writeRoundCombatStats(writer, stats)) {
      return false;
    }
  }
  for (const std::string& playerName : snapshot.playerNames) {
    if (!writer.writeString(playerName, kMaxPlayerNameBytes)) {
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
    writer.writeBool(snapshot.movementTuning.flightEnabled) &&
    writer.writeBool(snapshot.movementTuning.airControlEnabled) &&
    writer.writeFloat(snapshot.movementTuning.groundAcceleration) &&
    writer.writeFloat(snapshot.movementTuning.airAcceleration) &&
    writer.writeFloat(snapshot.movementTuning.groundFriction) &&
    writer.writeFloat(snapshot.movementTuning.stopSpeed) &&
    writer.writeFloat(snapshot.movementTuning.maxGroundSpeed) &&
    writer.writeFloat(snapshot.movementTuning.flightAcceleration) &&
    writer.writeFloat(snapshot.movementTuning.maxFlightSpeed) &&
    writer.writeFloat(snapshot.movementTuning.flightDamping) &&
    writer.writeFloat(snapshot.movementTuning.flightGravityCancel) &&
    writer.writeFloat(snapshot.playerSizeScaleXY) &&
    writer.writeFloat(snapshot.playerSizeScaleZ) &&
    writer.writeFloat(snapshot.lightningKnockback) &&
    writer.writeFloat(snapshot.rocketKnockback) &&
    writer.writeI32(snapshot.weaponDamage.shotgunDamagePerPellet) &&
    writer.writeI32(snapshot.weaponDamage.machineGunDamage) &&
    writer.writeI32(snapshot.weaponDamage.lightningGunDamage) &&
    writer.writeI32(snapshot.weaponDamage.railgunDamage) &&
    writer.writeI32(snapshot.weaponDamage.rocketLauncherDamage) &&
    writer.writeFloat(snapshot.vampirism) &&
    writer.writeU8(snapshot.selfDamagePercent) &&
    writer.writeI32(snapshot.healthAmount) &&
    writer.writeBool(snapshot.botDodgeEnabled) &&
    writer.writeI32(snapshot.botDodgeMinIntervalMs) &&
    writer.writeI32(snapshot.botDodgeMaxIntervalMs) &&
    writer.writeU32(snapshot.phaseTicksRemaining) &&
    writer.writeU32(snapshot.liveTicksElapsed) &&
    writer.writeU8(snapshot.roundWinner) &&
    writer.writeU8(snapshot.matchWinner) &&
    writer.writeBool(snapshot.playersColliding) &&
    writer.writeU32(snapshot.chatSequence) &&
    writer.writeU8(snapshot.chatPlayerIndex) &&
    writer.writeString(snapshot.chatMessage, kMaxChatMessageBytes) &&
    finishPacket(writer);
}

bool decodeServerSnapshot(const WirePacket& wire, ServerSnapshot& snapshot) {
  Reader reader(wire);
  ServerSnapshot decoded;
  if (
    !readHeader(reader, PacketType::Snapshot, wire.size()) ||
    !reader.readU32(decoded.serverTick) ||
    !reader.readU32(decoded.mapRevision) ||
    !readArena(reader, decoded.arena)
  ) {
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
  for (WeaponFireResult& result : decoded.weaponFires) {
    if (!readWeaponFire(reader, result)) {
      return false;
    }
  }
  for (RocketExplosionResult& result : decoded.rocketExplosions) {
    if (!readRocketExplosion(reader, result)) {
      return false;
    }
  }
  for (FootstepAudioEvent& event : decoded.footstepAudioEvents) {
    if (!readFootstepAudioEvent(reader, event)) {
      return false;
    }
  }
  for (FragEvent& event : decoded.fragEvents) {
    if (!readFragEvent(reader, event)) {
      return false;
    }
  }
  for (RocketProjectileSnapshot& projectile : decoded.rockets) {
    if (!readRocketProjectile(reader, projectile)) {
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
  std::uint8_t gameMode = 0;
  if (!reader.readU8(gameMode)) {
    return false;
  }
  decoded.gameMode = static_cast<GameMode>(gameMode);
  for (Team& team : decoded.teams) {
    std::uint8_t encodedTeam = 0;
    if (!reader.readU8(encodedTeam)) {
      return false;
    }
    team = static_cast<Team>(encodedTeam);
  }
  for (std::uint16_t& score : decoded.teamScores) {
    if (!reader.readU16(score)) {
      return false;
    }
  }
  std::uint8_t roundWinningTeam = 0;
  std::uint8_t matchWinningTeam = 0;
  if (
    !reader.readU8(roundWinningTeam) ||
    !reader.readU8(matchWinningTeam)
  ) {
    return false;
  }
  decoded.roundWinningTeam = static_cast<Team>(roundWinningTeam);
  decoded.matchWinningTeam = static_cast<Team>(matchWinningTeam);
  if (
    !isValidGameMode(decoded.gameMode) ||
    !isValidTeam(decoded.roundWinningTeam) ||
    !isValidTeam(decoded.matchWinningTeam) ||
    !std::all_of(
      decoded.teams.begin(),
      decoded.teams.end(),
      [](Team team) { return isValidTeam(team); }
    )
  ) {
    return false;
  }
  for (bool& connected : decoded.connectedPlayers) {
    if (!reader.readBool(connected)) {
      return false;
    }
  }
  for (bool& participating : decoded.participatingPlayers) {
    if (!reader.readBool(participating)) {
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
  for (RoundCombatStats& stats : decoded.matchCombatStats) {
    if (!readRoundCombatStats(reader, stats)) {
      return false;
    }
  }
  for (std::string& playerName : decoded.playerNames) {
    if (!reader.readString(playerName, kMaxPlayerNameBytes)) {
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
    !reader.readBool(decoded.movementTuning.flightEnabled) ||
    !reader.readBool(decoded.movementTuning.airControlEnabled) ||
    !reader.readFloat(decoded.movementTuning.groundAcceleration) ||
    !reader.readFloat(decoded.movementTuning.airAcceleration) ||
    !reader.readFloat(decoded.movementTuning.groundFriction) ||
    !reader.readFloat(decoded.movementTuning.stopSpeed) ||
    !reader.readFloat(decoded.movementTuning.maxGroundSpeed) ||
    !reader.readFloat(decoded.movementTuning.flightAcceleration) ||
    !reader.readFloat(decoded.movementTuning.maxFlightSpeed) ||
    !reader.readFloat(decoded.movementTuning.flightDamping) ||
    !reader.readFloat(decoded.movementTuning.flightGravityCancel) ||
    !reader.readFloat(decoded.playerSizeScaleXY) ||
    !reader.readFloat(decoded.playerSizeScaleZ) ||
    !reader.readFloat(decoded.lightningKnockback) ||
    !reader.readFloat(decoded.rocketKnockback) ||
    !reader.readI32(decoded.weaponDamage.shotgunDamagePerPellet) ||
    !reader.readI32(decoded.weaponDamage.machineGunDamage) ||
    !reader.readI32(decoded.weaponDamage.lightningGunDamage) ||
    !reader.readI32(decoded.weaponDamage.railgunDamage) ||
    !reader.readI32(decoded.weaponDamage.rocketLauncherDamage) ||
    !reader.readFloat(decoded.vampirism) ||
    !reader.readU8(decoded.selfDamagePercent) ||
    !reader.readI32(decoded.healthAmount) ||
    !reader.readBool(decoded.botDodgeEnabled) ||
    !reader.readI32(decoded.botDodgeMinIntervalMs) ||
    !reader.readI32(decoded.botDodgeMaxIntervalMs) ||
    !reader.readU32(decoded.phaseTicksRemaining) ||
    !reader.readU32(decoded.liveTicksElapsed) ||
    !reader.readU8(decoded.roundWinner) ||
    !reader.readU8(decoded.matchWinner) ||
    !reader.readBool(decoded.playersColliding) ||
    !reader.readU32(decoded.chatSequence) ||
    !reader.readU8(decoded.chatPlayerIndex) ||
    !reader.readString(decoded.chatMessage, kMaxChatMessageBytes) ||
    decoded.matchRules.roundLimit == 0 ||
    decoded.matchRules.playerLimit == 0 ||
    decoded.matchRules.playerLimit > kDuelPlayerCount ||
    decoded.movementTuning.groundAcceleration < 0.0F ||
    decoded.movementTuning.groundAcceleration > 1000.0F ||
    decoded.movementTuning.airAcceleration < 0.0F ||
    decoded.movementTuning.airAcceleration > 1000.0F ||
    decoded.movementTuning.groundFriction < 0.0F ||
    decoded.movementTuning.groundFriction > 100.0F ||
    decoded.movementTuning.stopSpeed < 0.0F ||
    decoded.movementTuning.stopSpeed > 100.0F ||
    decoded.movementTuning.maxGroundSpeed < 0.1F ||
    decoded.movementTuning.maxGroundSpeed > 100.0F ||
    decoded.movementTuning.flightAcceleration < 0.0F ||
    decoded.movementTuning.flightAcceleration > 1000.0F ||
    decoded.movementTuning.maxFlightSpeed < 0.1F ||
    decoded.movementTuning.maxFlightSpeed > 100.0F ||
    decoded.movementTuning.flightDamping < 0.0F ||
    decoded.movementTuning.flightDamping > 100.0F ||
    decoded.movementTuning.flightGravityCancel < 0.0F ||
    decoded.movementTuning.flightGravityCancel > 1.0F ||
    decoded.playerSizeScaleXY < 0.5F ||
    decoded.playerSizeScaleXY > 3.0F ||
    decoded.playerSizeScaleZ < 0.5F ||
    decoded.playerSizeScaleZ > 3.0F ||
    decoded.lightningKnockback < 0.0F ||
    decoded.rocketKnockback < 0.0F ||
    decoded.rocketKnockback > kMaxRocketKnockback ||
    decoded.lightningKnockback > kMaxLightningKnockback ||
    decoded.weaponDamage.shotgunDamagePerPellet < 1 ||
    decoded.weaponDamage.shotgunDamagePerPellet > 500 ||
    decoded.weaponDamage.machineGunDamage < 1 ||
    decoded.weaponDamage.machineGunDamage > 500 ||
    decoded.weaponDamage.lightningGunDamage < 1 ||
    decoded.weaponDamage.lightningGunDamage > 500 ||
    decoded.weaponDamage.railgunDamage < 1 ||
    decoded.weaponDamage.railgunDamage > 500 ||
    decoded.weaponDamage.rocketLauncherDamage < 1 ||
    decoded.weaponDamage.rocketLauncherDamage > 500 ||
    decoded.vampirism < 0.0F ||
    decoded.vampirism > 2.0F ||
    decoded.selfDamagePercent > 100 ||
    decoded.healthAmount < 1 ||
    decoded.healthAmount > 100000 ||
    decoded.botDodgeMinIntervalMs < 1 ||
    decoded.botDodgeMinIntervalMs > 10000 ||
    decoded.botDodgeMaxIntervalMs < 1 ||
    decoded.botDodgeMaxIntervalMs > 10000 ||
    (decoded.roundWinner != 255 && decoded.roundWinner >= kDuelPlayerCount) ||
    (decoded.matchWinner != 255 && decoded.matchWinner >= kDuelPlayerCount) ||
    decoded.chatPlayerIndex >= kDuelPlayerCount ||
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
