#include "net/NetCodec.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <tuple>
#include <utility>

namespace lg {
namespace {

constexpr std::size_t kHeaderBytes = 12;

[[nodiscard]] bool isValidWeapon(Weapon weapon) {
  return weapon <= kLastWeapon;
}

[[nodiscard]] bool isProjectileWeapon(Weapon weapon) {
  return weapon == Weapon::RocketLauncher ||
    weapon == Weapon::GrenadeLauncher ||
    weapon == Weapon::PlasmaGun;
}

constexpr std::uint32_t kMaxProjectilePresentationTicks = 7500U;

[[nodiscard]] bool isValidProjectilePresentationTuning(
  const ProjectilePresentationTuning& tuning
) {
  return
    tuning.rocketLifetimeTicks > 0U &&
    tuning.rocketLifetimeTicks <= kMaxProjectilePresentationTicks &&
    tuning.grenadeFuseTicks > 0U &&
    tuning.grenadeFuseTicks <= kMaxProjectilePresentationTicks &&
    tuning.plasmaLifetimeTicks > 0U &&
    tuning.plasmaLifetimeTicks <= kMaxProjectilePresentationTicks &&
    std::isfinite(tuning.grenadeGravity) &&
    tuning.grenadeGravity >= 0.0F &&
    tuning.grenadeGravity <= 500.0F &&
    std::isfinite(tuning.grenadeBounceDamping) &&
    tuning.grenadeBounceDamping >= 0.0F &&
    tuning.grenadeBounceDamping <= 1.5F &&
    std::isfinite(tuning.grenadeRestSpeed) &&
    tuning.grenadeRestSpeed >= 0.0F &&
    tuning.grenadeRestSpeed <= 20.0F;
}

[[nodiscard]] bool isValidProjectileUpdate(
  const ProjectileUpdate& update
) {
  return
    update.slot < kMaxRocketProjectiles &&
    update.sequence != 0U &&
    update.kind <= ProjectileUpdateKind::Remove &&
    isProjectileWeapon(update.weapon) &&
    std::isfinite(update.position.x) &&
    std::isfinite(update.position.y) &&
    std::isfinite(update.position.z) &&
    std::isfinite(update.velocity.x) &&
    std::isfinite(update.velocity.y) &&
    std::isfinite(update.velocity.z) &&
    std::isfinite(update.radius) &&
    update.radius >= 0.0F &&
    update.radius <= 5.0F &&
    update.ageTicks <= kMaxProjectilePresentationTicks &&
    (!update.resting || update.weapon == Weapon::GrenadeLauncher);
}

[[nodiscard]] bool isValidWeaponSwitchingMode(WeaponSwitchingMode mode) {
  return mode <= WeaponSwitchingMode::Crazy;
}

[[nodiscard]] bool isValidBotAttackMode(BotAttackMode mode) {
  return mode <= BotAttackMode::Hard;
}

[[nodiscard]] bool isValidBotCommandType(BotCommandType type) {
  return type <= BotCommandType::Weapon;
}

[[nodiscard]] bool isValidMcGuffinStateValue(McGuffinState state) {
  return state <= McGuffinState::InstalledBlue;
}

[[nodiscard]] bool isValidMcGuffinEventType(McGuffinEventType event) {
  return event <= McGuffinEventType::Throw;
}

[[nodiscard]] bool isValidMcGuffinSnapshot(const McGuffinSnapshot& objective) {
  if (!isValidMcGuffinStateValue(objective.state) ||
      !isValidTeam(objective.associatedTeam) ||
      !isValidTeam(objective.carrierTeam) ||
      !isValidMcGuffinEventType(objective.lastEvent) ||
      !std::isfinite(objective.position.x) ||
      !std::isfinite(objective.position.y) ||
      !std::isfinite(objective.position.z) ||
      !std::isfinite(objective.velocity.x) ||
      !std::isfinite(objective.velocity.y) ||
      !std::isfinite(objective.velocity.z)) {
    return false;
  }
  if (objective.state == McGuffinState::Carried) {
    return objective.carrierIndex < kDuelPlayerCount &&
      isPlayableTeam(objective.carrierTeam);
  }
  if (objective.carrierIndex != kNoMcGuffinCarrier ||
      objective.carrierTeam != Team::None) {
    return false;
  }
  if (objective.state == McGuffinState::InstalledRed) {
    return objective.associatedTeam == Team::Red;
  }
  if (objective.state == McGuffinState::InstalledBlue) {
    return objective.associatedTeam == Team::Blue;
  }
  return objective.state != McGuffinState::NeutralSpawn ||
    objective.associatedTeam == Team::None;
}

[[nodiscard]] bool isValidBotCommandRequest(
  BotCommandType type,
  std::int32_t value,
  std::int32_t minIntervalMs,
  std::int32_t maxIntervalMs
) {
  if (minIntervalMs < 1 || minIntervalMs > 10000 || maxIntervalMs < 1 || maxIntervalMs > 10000) {
    return false;
  }
  switch (type) {
    case BotCommandType::None:
      return value == 0;
    case BotCommandType::Add:
      return value >= -1 && value <= static_cast<std::int32_t>(kDuelPlayerCount);
    case BotCommandType::KickSlot:
      return value >= 1 && value <= static_cast<std::int32_t>(kDuelPlayerCount);
    case BotCommandType::KickAll:
      return value == 0;
    case BotCommandType::AttackMode:
      return value >= 0 && value <= static_cast<std::int32_t>(BotAttackMode::Hard);
    case BotCommandType::Stare:
    case BotCommandType::Standstill:
    case BotCommandType::Dodge:
      return value == 0 || value == 1;
    case BotCommandType::Weapon:
      return value >= -1 &&
        value <= static_cast<std::int32_t>(kLastWeapon);
  }
  return false;
}

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
    // The wire format is explicitly little-endian; never serialize host memory
    // directly, because peers may differ in byte order, padding, or alignment.
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
    // Non-finite gameplay values are rejected at the serialization boundary so
    // NaNs cannot enter authoritative simulation or client presentation state.
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
  // Payload size is patched only after the body succeeds, keeping the header
  // layout fixed while allowing variable-length strings and command bundles.
  return writer.patchU16(8, static_cast<std::uint16_t>(payloadBytes));
}

bool readHeader(Reader& reader, PacketType expectedType, std::size_t wireSize) {
  std::uint32_t magic = 0;
  std::uint16_t version = 0;
  std::uint8_t type = 0;
  std::uint8_t flags = 0;
  std::uint16_t payloadBytes = 0;
  std::uint16_t reserved = 0;
  // Exact version, reserved fields, and payload length make incompatible or
  // extended layouts fail closed instead of being partially misinterpreted.
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

constexpr std::uint8_t kCompressedPayloadFlag = 1U;
constexpr std::size_t kSnapshotMatchWindow = 4096U;
constexpr std::size_t kSnapshotMinMatch = 3U;
constexpr std::size_t kSnapshotMaxMatch = 18U;

[[nodiscard]] std::uint16_t readWireU16(
  const WirePacket& wire,
  std::size_t offset
) {
  return static_cast<std::uint16_t>(wire[offset]) |
    (static_cast<std::uint16_t>(wire[offset + 1U]) << 8U);
}

[[nodiscard]] std::uint32_t snapshotMatchHash(
  const WirePacket& input,
  std::size_t offset
) {
  const std::uint32_t value =
    static_cast<std::uint32_t>(input[offset]) |
    (static_cast<std::uint32_t>(input[offset + 1U]) << 8U) |
    (static_cast<std::uint32_t>(input[offset + 2U]) << 16U);
  return (value * 2654435761U) >> 20U;
}

// Player snapshots and combat-stat arrays contain repeated fixed-capacity
// records. A small deterministic LZ window preserves every authoritative bit
// while removing that repetition; there is no stateful cross-packet dictionary.
[[nodiscard]] bool compactSnapshotWire(WirePacket& wire) {
  if (wire.size() < kHeaderBytes || wire.size() > kMaxPacketBytes) return false;
  const std::size_t payloadBytes = wire.size() - kHeaderBytes;
  if (payloadBytes > std::numeric_limits<std::uint16_t>::max()) return false;

  WirePacket compressed;
  compressed.reserve(payloadBytes);
  compressed.push_back(static_cast<std::uint8_t>(payloadBytes));
  compressed.push_back(static_cast<std::uint8_t>(payloadBytes >> 8U));
  std::array<std::int32_t, 4096> latest = {};
  latest.fill(-1);
  std::size_t inputOffset = kHeaderBytes;
  while (inputOffset < wire.size()) {
    const std::size_t controlOffset = compressed.size();
    compressed.push_back(0U);
    std::uint8_t control = 0U;
    for (std::size_t token = 0; token < 8U && inputOffset < wire.size(); ++token) {
      std::size_t matchLength = 0U;
      std::size_t matchDistance = 0U;
      if (inputOffset + kSnapshotMinMatch <= wire.size()) {
        const std::size_t hash = snapshotMatchHash(wire, inputOffset);
        const std::int32_t candidate = latest[hash];
        latest[hash] = static_cast<std::int32_t>(inputOffset);
        if (candidate >= static_cast<std::int32_t>(kHeaderBytes)) {
          const std::size_t candidateOffset = static_cast<std::size_t>(candidate);
          const std::size_t distance = inputOffset - candidateOffset;
          if (distance <= kSnapshotMatchWindow) {
            const std::size_t limit = std::min(
              kSnapshotMaxMatch,
              wire.size() - inputOffset
            );
            while (matchLength < limit &&
                   wire[candidateOffset + matchLength] ==
                     wire[inputOffset + matchLength]) {
              ++matchLength;
            }
            if (matchLength >= kSnapshotMinMatch) matchDistance = distance;
          }
        }
      }
      if (matchLength >= kSnapshotMinMatch) {
        control |= static_cast<std::uint8_t>(1U << token);
        const std::uint16_t encoded = static_cast<std::uint16_t>(
          (matchDistance - 1U) |
          ((matchLength - kSnapshotMinMatch) << 12U)
        );
        compressed.push_back(static_cast<std::uint8_t>(encoded));
        compressed.push_back(static_cast<std::uint8_t>(encoded >> 8U));
        inputOffset += matchLength;
      } else {
        compressed.push_back(wire[inputOffset++]);
      }
    }
    compressed[controlOffset] = control;
  }

  if (compressed.size() < payloadBytes) {
    WirePacket compacted(wire.begin(), wire.begin() + kHeaderBytes);
    compacted[7] = kCompressedPayloadFlag;
    compacted.insert(compacted.end(), compressed.begin(), compressed.end());
    const std::size_t compactedPayloadBytes = compacted.size() - kHeaderBytes;
    compacted[8] = static_cast<std::uint8_t>(compactedPayloadBytes);
    compacted[9] = static_cast<std::uint8_t>(compactedPayloadBytes >> 8U);
    wire = std::move(compacted);
  }
  // Encoding failure is deliberate: a caller must never hand an oversized
  // snapshot to UDP and accidentally rely on IP fragmentation.
  if (wire.size() > kMaxUdpApplicationDatagramBytes) {
    wire.clear();
    return false;
  }
  return true;
}

[[nodiscard]] bool expandSnapshotWire(
  const WirePacket& wire,
  WirePacket& expanded
) {
  if (wire.size() < kHeaderBytes ||
      wire.size() > kMaxUdpApplicationDatagramBytes ||
      readWireU16(wire, 8U) != wire.size() - kHeaderBytes) {
    return false;
  }
  const std::uint8_t flags = wire[7];
  if (flags == 0U) {
    expanded = wire;
    return true;
  }
  if (flags != kCompressedPayloadFlag || wire.size() < kHeaderBytes + 2U) {
    return false;
  }
  const std::size_t expandedPayloadBytes = readWireU16(wire, kHeaderBytes);
  if (expandedPayloadBytes >
      std::numeric_limits<std::uint16_t>::max()) return false;

  WirePacket payload;
  payload.reserve(expandedPayloadBytes);
  std::size_t inputOffset = kHeaderBytes + 2U;
  while (inputOffset < wire.size() && payload.size() < expandedPayloadBytes) {
    const std::uint8_t control = wire[inputOffset++];
    for (std::size_t token = 0;
         token < 8U && payload.size() < expandedPayloadBytes;
         ++token) {
      if ((control & (1U << token)) == 0U) {
        if (inputOffset >= wire.size()) return false;
        payload.push_back(wire[inputOffset++]);
        continue;
      }
      if (inputOffset + 2U > wire.size()) return false;
      const std::uint16_t encoded = readWireU16(wire, inputOffset);
      inputOffset += 2U;
      const std::size_t distance = (encoded & 0x0FFFU) + 1U;
      const std::size_t length = (encoded >> 12U) + kSnapshotMinMatch;
      if (distance > payload.size() ||
          length > expandedPayloadBytes - payload.size()) return false;
      const std::size_t matchOffset = payload.size() - distance;
      for (std::size_t index = 0; index < length; ++index) {
        payload.push_back(payload[matchOffset + index]);
      }
    }
  }
  if (inputOffset != wire.size() || payload.size() != expandedPayloadBytes) {
    return false;
  }
  expanded.assign(wire.begin(), wire.begin() + kHeaderBytes);
  expanded[7] = 0U;
  expanded[8] = static_cast<std::uint8_t>(expandedPayloadBytes);
  expanded[9] = static_cast<std::uint8_t>(expandedPayloadBytes >> 8U);
  expanded.insert(expanded.end(), payload.begin(), payload.end());
  return true;
}

bool writeActionEdgeState(Writer& writer, const ActionEdgeState& edges) {
  return std::isfinite(edges.mcguffinThrowYawRadians) &&
    std::isfinite(edges.mcguffinThrowPitchRadians) &&
    std::isfinite(edges.attackYawRadians) &&
    std::isfinite(edges.attackPitchRadians) &&
    static_cast<std::uint8_t>(edges.attackWeapon) <=
      static_cast<std::uint8_t>(kLastWeapon) &&
    writer.writeU32(edges.jump) &&
    writer.writeU32(edges.dash) &&
    writer.writeU32(edges.reset) &&
    writer.writeU32(edges.ready) &&
    writer.writeU32(edges.mcguffinThrow) &&
    writer.writeFloat(edges.mcguffinThrowYawRadians) &&
    writer.writeFloat(edges.mcguffinThrowPitchRadians) &&
    writer.writeU32(edges.attack) &&
    writer.writeFloat(edges.attackYawRadians) &&
    writer.writeFloat(edges.attackPitchRadians) &&
    writer.writeU32(edges.attackViewedServerTick) &&
    writer.writeU8(static_cast<std::uint8_t>(edges.attackWeapon)) &&
    writer.writeBool(edges.attackZoomed);
}

bool readActionEdgeState(Reader& reader, ActionEdgeState& edges) {
  std::uint8_t attackWeapon = 0;
  return reader.readU32(edges.jump) &&
    reader.readU32(edges.dash) &&
    reader.readU32(edges.reset) &&
    reader.readU32(edges.ready) &&
    reader.readU32(edges.mcguffinThrow) &&
    reader.readFloat(edges.mcguffinThrowYawRadians) &&
    reader.readFloat(edges.mcguffinThrowPitchRadians) &&
    reader.readU32(edges.attack) &&
    reader.readFloat(edges.attackYawRadians) &&
    reader.readFloat(edges.attackPitchRadians) &&
    reader.readU32(edges.attackViewedServerTick) &&
    reader.readU8(attackWeapon) &&
    reader.readBool(edges.attackZoomed) &&
    std::isfinite(edges.mcguffinThrowYawRadians) &&
    std::isfinite(edges.mcguffinThrowPitchRadians) &&
    std::isfinite(edges.attackYawRadians) &&
    std::isfinite(edges.attackPitchRadians) &&
    attackWeapon <= static_cast<std::uint8_t>(kLastWeapon) &&
    ((edges.attackWeapon = static_cast<Weapon>(attackWeapon)), true);
}

bool writeCommandBody(Writer& writer, const CommandPacket& packet) {
  const UserCommand& command = packet.command;
  return packet.clientIndex < kMaxNetworkClients &&
    (packet.playerIndex < kDuelPlayerCount ||
     packet.playerIndex == kNoAssignedPlayer) &&
    isValidGameMode(packet.requestedGameMode) &&
    isValidTeam(packet.requestedTeam) &&
    isValidWeaponSwitchingMode(packet.weaponSwitchingMode) &&
    isValidBotCommandType(packet.botCommand) &&
    isValidBotCommandRequest(
      packet.botCommand,
      packet.botCommandValue,
      packet.botCommandMinIntervalMs,
      packet.botCommandMaxIntervalMs
    ) &&
    std::all_of(
      packet.weaponAmmo.spawnAmmo.begin(),
      packet.weaponAmmo.spawnAmmo.end(),
      [](std::int32_t ammo) { return ammo >= 0 && ammo <= 999; }
    ) &&
    writer.writeU8(packet.clientIndex) &&
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
    writer.writeBool(command.dash) &&
    writer.writeBool(command.crouch) &&
    writer.writeBool(command.sneak) &&
    writer.writeBool(command.zoomed) &&
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
    writer.writeFloat(packet.movementTuning.dashTargetSpeed) &&
    writer.writeFloat(packet.movementTuning.dashMaxSpeed) &&
    writer.writeFloat(packet.movementTuning.dashAcceleration) &&
    writer.writeFloat(packet.movementTuning.dashDuration) &&
    writer.writeFloat(packet.movementTuning.dashCooldown) &&
    writer.writeFloat(packet.movementTuning.dashGroundHopVelocity) &&
    writer.writeFloat(packet.movementTuning.dashAirHopVelocity) &&
    writer.writeFloat(packet.movementTuning.flightAcceleration) &&
    writer.writeFloat(packet.movementTuning.maxFlightSpeed) &&
    writer.writeFloat(packet.movementTuning.flightDamping) &&
    writer.writeFloat(packet.movementTuning.flightGravityCancel) &&
    writer.writeFloat(packet.playerSizeScaleXY) &&
    writer.writeFloat(packet.playerSizeScaleZ) &&
    writer.writeFloat(packet.lightningKnockback) &&
    writer.writeFloat(packet.lightningFireHz) &&
    writer.writeFloat(packet.rocketKnockback) &&
    writer.writeI32(packet.knockbackTimeMs) &&
    writer.writeI32(packet.weaponDamage.shotgunDamagePerPellet) &&
    writer.writeI32(packet.weaponDamage.machineGunDamage) &&
    writer.writeI32(packet.weaponDamage.lightningGunDamage) &&
    writer.writeI32(packet.weaponDamage.railgunDamage) &&
    writer.writeI32(packet.weaponDamage.rocketLauncherDamage) &&
    writer.writeI32(packet.weaponDamage.plasmaGunDamage) &&
    writer.writeI32(packet.weaponDamage.freezeGunDamage) &&
    writer.writeBool(packet.weaponAmmo.infiniteAmmo) &&
    writer.writeI32(packet.weaponAmmo.spawnAmmo[weaponIndex(Weapon::LightningGun)]) &&
    writer.writeI32(packet.weaponAmmo.spawnAmmo[weaponIndex(Weapon::Railgun)]) &&
    writer.writeI32(packet.weaponAmmo.spawnAmmo[weaponIndex(Weapon::RocketLauncher)]) &&
    writer.writeI32(packet.weaponAmmo.spawnAmmo[weaponIndex(Weapon::MachineGun)]) &&
    writer.writeI32(packet.weaponAmmo.spawnAmmo[weaponIndex(Weapon::Shotgun)]) &&
    writer.writeI32(packet.weaponAmmo.spawnAmmo[weaponIndex(Weapon::GrenadeLauncher)]) &&
    writer.writeI32(packet.weaponAmmo.spawnAmmo[weaponIndex(Weapon::PlasmaGun)]) &&
    writer.writeI32(packet.weaponAmmo.spawnAmmo[weaponIndex(Weapon::FreezeGun)]) &&
    writer.writeI32(packet.weaponAmmo.spawnAmmo[weaponIndex(Weapon::Revolver)]) &&
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
      writer.writeU8(static_cast<std::uint8_t>(packet.requestedTeam)) &&
      writer.writeBool(packet.requestSpectator) &&
      writer.writeU8(static_cast<std::uint8_t>(packet.weaponSwitchingMode)) &&
      writer.writeU8(static_cast<std::uint8_t>(packet.botCommand)) &&
      writer.writeI32(packet.botCommandValue) &&
      writer.writeI32(packet.botCommandMinIntervalMs) &&
      writer.writeI32(packet.botCommandMaxIntervalMs) &&
      writer.writeBool(packet.requestMcGuffinThrow) &&
      writer.writeBool(packet.wantsScoreboardStats) &&
      writer.writeU32(packet.acknowledgedConfigurationRevision) &&
      writeActionEdgeState(writer, packet.actionEdges);
}

bool readCommandBody(Reader& reader, CommandPacket& packet) {
  std::uint8_t weapon = 0;
  std::uint8_t requestedGameMode = 0;
  std::uint8_t requestedTeam = 0;
  std::uint8_t weaponSwitchingMode = 0;
  std::uint8_t botCommand = 0;
  if (
    !reader.readU8(packet.clientIndex) ||
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
    !reader.readBool(packet.command.dash) ||
    !reader.readBool(packet.command.crouch) ||
    !reader.readBool(packet.command.sneak) ||
    !reader.readBool(packet.command.zoomed) ||
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
    !reader.readFloat(packet.movementTuning.dashTargetSpeed) ||
    !reader.readFloat(packet.movementTuning.dashMaxSpeed) ||
    !reader.readFloat(packet.movementTuning.dashAcceleration) ||
    !reader.readFloat(packet.movementTuning.dashDuration) ||
    !reader.readFloat(packet.movementTuning.dashCooldown) ||
    !reader.readFloat(packet.movementTuning.dashGroundHopVelocity) ||
    !reader.readFloat(packet.movementTuning.dashAirHopVelocity) ||
    !reader.readFloat(packet.movementTuning.flightAcceleration) ||
    !reader.readFloat(packet.movementTuning.maxFlightSpeed) ||
    !reader.readFloat(packet.movementTuning.flightDamping) ||
    !reader.readFloat(packet.movementTuning.flightGravityCancel) ||
    !reader.readFloat(packet.playerSizeScaleXY) ||
    !reader.readFloat(packet.playerSizeScaleZ) ||
    !reader.readFloat(packet.lightningKnockback) ||
    !reader.readFloat(packet.lightningFireHz) ||
    !reader.readFloat(packet.rocketKnockback) ||
    !reader.readI32(packet.knockbackTimeMs) ||
    !reader.readI32(packet.weaponDamage.shotgunDamagePerPellet) ||
    !reader.readI32(packet.weaponDamage.machineGunDamage) ||
    !reader.readI32(packet.weaponDamage.lightningGunDamage) ||
    !reader.readI32(packet.weaponDamage.railgunDamage) ||
    !reader.readI32(packet.weaponDamage.rocketLauncherDamage) ||
    !reader.readI32(packet.weaponDamage.plasmaGunDamage) ||
    !reader.readI32(packet.weaponDamage.freezeGunDamage) ||
    !reader.readBool(packet.weaponAmmo.infiniteAmmo) ||
    !reader.readI32(packet.weaponAmmo.spawnAmmo[weaponIndex(Weapon::LightningGun)]) ||
    !reader.readI32(packet.weaponAmmo.spawnAmmo[weaponIndex(Weapon::Railgun)]) ||
    !reader.readI32(packet.weaponAmmo.spawnAmmo[weaponIndex(Weapon::RocketLauncher)]) ||
    !reader.readI32(packet.weaponAmmo.spawnAmmo[weaponIndex(Weapon::MachineGun)]) ||
    !reader.readI32(packet.weaponAmmo.spawnAmmo[weaponIndex(Weapon::Shotgun)]) ||
    !reader.readI32(packet.weaponAmmo.spawnAmmo[weaponIndex(Weapon::GrenadeLauncher)]) ||
    !reader.readI32(packet.weaponAmmo.spawnAmmo[weaponIndex(Weapon::PlasmaGun)]) ||
    !reader.readI32(packet.weaponAmmo.spawnAmmo[weaponIndex(Weapon::FreezeGun)]) ||
    !reader.readI32(packet.weaponAmmo.spawnAmmo[weaponIndex(Weapon::Revolver)]) ||
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
      !reader.readU8(requestedTeam) ||
      !reader.readBool(packet.requestSpectator) ||
      !reader.readU8(weaponSwitchingMode) ||
      !reader.readU8(botCommand) ||
      !reader.readI32(packet.botCommandValue) ||
      !reader.readI32(packet.botCommandMinIntervalMs) ||
      !reader.readI32(packet.botCommandMaxIntervalMs) ||
      !reader.readBool(packet.requestMcGuffinThrow) ||
      !reader.readBool(packet.wantsScoreboardStats) ||
      !reader.readU32(packet.acknowledgedConfigurationRevision) ||
      !readActionEdgeState(reader, packet.actionEdges)
    ) {
    return false;
  }

  const bool valid = packet.clientIndex < kMaxNetworkClients &&
    (packet.playerIndex < kDuelPlayerCount ||
     packet.playerIndex == kNoAssignedPlayer) &&
    weapon <= static_cast<std::uint8_t>(kLastWeapon) &&
    requestedGameMode <= static_cast<std::uint8_t>(GameMode::McGuffin) &&
    requestedTeam <= static_cast<std::uint8_t>(Team::Blue) &&
    weaponSwitchingMode <= static_cast<std::uint8_t>(WeaponSwitchingMode::Crazy) &&
    botCommand <= static_cast<std::uint8_t>(BotCommandType::Weapon) &&
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
    packet.movementTuning.dashTargetSpeed >= 0.0F &&
    packet.movementTuning.dashTargetSpeed <= 100.0F &&
    packet.movementTuning.dashMaxSpeed >= 0.0F &&
    packet.movementTuning.dashMaxSpeed <= 100.0F &&
    packet.movementTuning.dashAcceleration >= 0.0F &&
    packet.movementTuning.dashAcceleration <= 1000.0F &&
    packet.movementTuning.dashDuration >= 0.0F &&
    packet.movementTuning.dashDuration <= 2.0F &&
    packet.movementTuning.dashCooldown >= 0.0F &&
    packet.movementTuning.dashCooldown <= 10.0F &&
    packet.movementTuning.dashGroundHopVelocity >= 0.0F &&
    packet.movementTuning.dashGroundHopVelocity <= 100.0F &&
    packet.movementTuning.dashAirHopVelocity >= 0.0F &&
    packet.movementTuning.dashAirHopVelocity <= 100.0F &&
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
    packet.lightningFireHz >= kMinLightningFireHz &&
    packet.lightningFireHz <= kMaxLightningFireHz &&
    packet.rocketKnockback >= 0.0F &&
    packet.rocketKnockback <= kMaxRocketKnockback &&
    packet.knockbackTimeMs >= 0 &&
    packet.knockbackTimeMs <= 250 &&
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
    packet.weaponDamage.plasmaGunDamage >= 1 &&
    packet.weaponDamage.plasmaGunDamage <= 500 &&
    packet.weaponDamage.freezeGunDamage >= 1 &&
    packet.weaponDamage.freezeGunDamage <= 500 &&
    std::all_of(
      packet.weaponAmmo.spawnAmmo.begin(),
      packet.weaponAmmo.spawnAmmo.end(),
      [](std::int32_t ammo) { return ammo >= 0 && ammo <= 999; }
    ) &&
    packet.vampirism >= 0.0F &&
    packet.vampirism <= 2.0F &&
    packet.selfDamagePercent <= 100 &&
    packet.healthAmount >= 1 &&
    packet.healthAmount <= 100000 &&
    packet.botDodgeMinIntervalMs >= 1 &&
    packet.botDodgeMinIntervalMs <= 10000 &&
    packet.botDodgeMaxIntervalMs >= 1 &&
    packet.botDodgeMaxIntervalMs <= 10000 &&
    isValidBotCommandRequest(
      static_cast<BotCommandType>(botCommand),
      packet.botCommandValue,
      packet.botCommandMinIntervalMs,
      packet.botCommandMaxIntervalMs
    );
  if (!valid) {
    return false;
  }
  packet.command.weapon = static_cast<Weapon>(weapon);
  packet.requestedGameMode = static_cast<GameMode>(requestedGameMode);
  packet.requestedTeam = static_cast<Team>(requestedTeam);
  packet.weaponSwitchingMode =
    static_cast<WeaponSwitchingMode>(weaponSwitchingMode);
  packet.botCommand = static_cast<BotCommandType>(botCommand);
  return true;
}

[[nodiscard]] bool commandHasControl(const CommandPacket& packet) {
  return packet.requestReset || packet.toggleReady ||
    packet.requestMovementTuning || !packet.chatMessage.empty() ||
    !packet.playerName.empty() || !packet.mapName.empty() ||
    packet.requestGameMode || packet.requestTeam ||
    packet.botCommand != BotCommandType::None ||
    packet.requestMcGuffinThrow || packet.requestSpectator;
}

[[nodiscard]] bool sameUserCommand(
  const UserCommand& lhs,
  const UserCommand& rhs
) {
  return lhs.sequence == rhs.sequence &&
    lhs.clientTick == rhs.clientTick &&
    lhs.viewYawRadians == rhs.viewYawRadians &&
    lhs.viewPitchRadians == rhs.viewPitchRadians &&
    lhs.forwardMove == rhs.forwardMove &&
    lhs.rightMove == rhs.rightMove &&
    lhs.upMove == rhs.upMove &&
    lhs.attack == rhs.attack &&
    lhs.jump == rhs.jump &&
    lhs.dash == rhs.dash &&
    lhs.crouch == rhs.crouch &&
    lhs.sneak == rhs.sneak &&
    lhs.zoomed == rhs.zoomed &&
    lhs.planarAim == rhs.planarAim &&
    lhs.weapon == rhs.weapon;
}

bool writeCompactCommand(Writer& writer, const CommandPacket& packet) {
  const UserCommand& command = packet.command;
  if (
    packet.clientIndex >= kMaxNetworkClients ||
    (packet.playerIndex >= kDuelPlayerCount &&
     packet.playerIndex != kNoAssignedPlayer) ||
    static_cast<std::uint8_t>(command.weapon) >
      static_cast<std::uint8_t>(kLastWeapon) ||
    std::fabs(command.forwardMove) > 1.0F ||
    std::fabs(command.rightMove) > 1.0F ||
    std::fabs(command.upMove) > 1.0F
  ) {
    return false;
  }
  std::uint16_t inputBits = 0;
  inputBits |= command.attack ? 1U << 0U : 0U;
  inputBits |= command.jump ? 1U << 1U : 0U;
  inputBits |= command.dash ? 1U << 2U : 0U;
  inputBits |= command.crouch ? 1U << 3U : 0U;
  inputBits |= command.sneak ? 1U << 4U : 0U;
  inputBits |= command.planarAim ? 1U << 5U : 0U;
  inputBits |= packet.wantsScoreboardStats ? 1U << 6U : 0U;
  inputBits |= command.zoomed ? 1U << 7U : 0U;
  const bool hasControl = commandHasControl(packet);
  return writer.writeU32(command.sequence) &&
    writer.writeU32(command.clientTick) &&
    writer.writeFloat(command.viewYawRadians) &&
    writer.writeFloat(command.viewPitchRadians) &&
    writer.writeFloat(command.forwardMove) &&
    writer.writeFloat(command.rightMove) &&
    writer.writeFloat(command.upMove) &&
    writer.writeU16(inputBits) &&
    writer.writeU8(static_cast<std::uint8_t>(command.weapon)) &&
    writer.writeU32(packet.viewedServerTick) &&
    writer.writeU32(packet.acknowledgedConfigurationRevision) &&
    writer.writeBool(hasControl) &&
    (!hasControl || writeCommandBody(writer, packet));
}

bool readCompactCommand(
  Reader& reader,
  std::uint8_t clientIndex,
  std::uint8_t playerIndex,
  std::uint32_t clientNonce,
  const ActionEdgeState& actionEdges,
  CommandPacket& packet
) {
  CommandPacket compact;
  compact.clientIndex = clientIndex;
  compact.playerIndex = playerIndex;
  compact.clientNonce = clientNonce;
  compact.actionEdges = actionEdges;
  std::uint16_t inputBits = 0;
  std::uint8_t weapon = 0;
  bool hasControl = false;
  if (
    !reader.readU32(compact.command.sequence) ||
    !reader.readU32(compact.command.clientTick) ||
    !reader.readFloat(compact.command.viewYawRadians) ||
    !reader.readFloat(compact.command.viewPitchRadians) ||
    !reader.readFloat(compact.command.forwardMove) ||
    !reader.readFloat(compact.command.rightMove) ||
    !reader.readFloat(compact.command.upMove) ||
    !reader.readU16(inputBits) ||
    !reader.readU8(weapon) ||
    !reader.readU32(compact.viewedServerTick) ||
    !reader.readU32(compact.acknowledgedConfigurationRevision) ||
    !reader.readBool(hasControl)
  ) {
    return false;
  }
  if (
    (inputBits & ~0xFFU) != 0U ||
    weapon > static_cast<std::uint8_t>(kLastWeapon) ||
    std::fabs(compact.command.forwardMove) > 1.0F ||
    std::fabs(compact.command.rightMove) > 1.0F ||
    std::fabs(compact.command.upMove) > 1.0F
  ) {
    return false;
  }
  compact.command.attack = (inputBits & (1U << 0U)) != 0U;
  compact.command.jump = (inputBits & (1U << 1U)) != 0U;
  compact.command.dash = (inputBits & (1U << 2U)) != 0U;
  compact.command.crouch = (inputBits & (1U << 3U)) != 0U;
  compact.command.sneak = (inputBits & (1U << 4U)) != 0U;
  compact.command.planarAim = (inputBits & (1U << 5U)) != 0U;
  compact.wantsScoreboardStats = (inputBits & (1U << 6U)) != 0U;
  compact.command.zoomed = (inputBits & (1U << 7U)) != 0U;
  compact.command.weapon = static_cast<Weapon>(weapon);
  if (!hasControl) {
    packet = compact;
    return true;
  }

  CommandPacket full;
  if (
    !readCommandBody(reader, full) ||
    full.clientIndex != compact.clientIndex ||
    full.playerIndex != compact.playerIndex ||
    full.clientNonce != compact.clientNonce ||
    !sameUserCommand(full.command, compact.command) ||
    full.viewedServerTick != compact.viewedServerTick ||
    full.wantsScoreboardStats != compact.wantsScoreboardStats ||
    full.acknowledgedConfigurationRevision !=
      compact.acknowledgedConfigurationRevision
  ) {
    return false;
  }
  full.actionEdges = actionEdges;
  packet = std::move(full);
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

bool writePlayer(Writer& writer, const PlayerState& player) {
  return writeVec3(writer, player.position) &&
    writeVec3(writer, player.velocity) &&
    writer.writeFloat(player.viewYawRadians) &&
    writer.writeFloat(player.viewPitchRadians) &&
    writer.writeI32(player.health) &&
    writer.writeFloat(player.freezeLevel) &&
    writer.writeFloat(player.bounds.radius) &&
    writer.writeFloat(player.bounds.halfHeight) &&
    writer.writeU8(static_cast<std::uint8_t>(player.movementMode)) &&
    writer.writeU16(player.knockbackTicksRemaining) &&
    writer.writeU16(player.dashCooldownTicksRemaining) &&
    writer.writeU16(player.dashActiveTicksRemaining) &&
    writeVec3(writer, player.dashDirection) &&
    writer.writeBool(player.onGround) &&
    writer.writeBool(player.jumpHeld) &&
    writer.writeBool(player.dashHeld) &&
    writer.writeBool(player.crouched) &&
    writer.writeBool(player.sneaking);
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
    !reader.readFloat(player.freezeLevel) ||
    !reader.readFloat(player.bounds.radius) ||
    !reader.readFloat(player.bounds.halfHeight) ||
    !reader.readU8(movementMode) ||
    !reader.readU16(player.knockbackTicksRemaining) ||
    !reader.readU16(player.dashCooldownTicksRemaining) ||
    !reader.readU16(player.dashActiveTicksRemaining) ||
    !readVec3(reader, player.dashDirection) ||
    !reader.readBool(player.onGround) ||
    !reader.readBool(player.jumpHeld) ||
    !reader.readBool(player.dashHeld) ||
    !reader.readBool(player.crouched) ||
    !reader.readBool(player.sneaking)
  ) {
    return false;
  }

  if (
    health < 0 ||
    health > 100000 ||
    player.freezeLevel < 0.0F ||
    player.freezeLevel > 1000.0F ||
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
  if (!writer.writeBool(result.active)) {
    return false;
  }
  if (!result.active) {
    return true;
  }
  if (!(writeVec3(writer, result.start) &&
    writeVec3(writer, result.end) &&
    writer.writeBool(result.hit) &&
    writer.writeBool(result.headshot) &&
    writer.writeU8(result.targetPlayerIndex) &&
    writer.writeI32(result.damageApplied) &&
    writeVec3(writer, result.knockbackImpulse) &&
    writer.writeFloat(result.freezeApplied) &&
    writer.writeU32(result.requestedRewindTicks) &&
    writer.writeU32(result.appliedRewindTicks) &&
    writer.writeBool(result.rewindClamped) &&
    writer.writeBool(result.hasRewindDebug))) {
    return false;
  }
  // Rewind geometry is diagnostic-only and must not tax ordinary beam updates.
  return !result.hasRewindDebug || (
    writer.writeU32(result.rewindTargetTick) &&
    writeVec3(writer, result.currentTargetPosition) &&
    writeVec3(writer, result.rewoundTargetPosition) &&
    writer.writeFloat(result.currentTargetBounds.radius) &&
    writer.writeFloat(result.currentTargetBounds.halfHeight) &&
    writer.writeFloat(result.rewoundTargetBounds.radius) &&
    writer.writeFloat(result.rewoundTargetBounds.halfHeight));
}

bool readLightningGun(Reader& reader, LightningGunResult& result) {
  std::int32_t damageApplied = 0;
  std::uint8_t targetPlayerIndex = 255;
  if (!reader.readBool(result.active)) {
    return false;
  }
  if (!result.active) {
    return true;
  }
  if (
    !readVec3(reader, result.start) ||
    !readVec3(reader, result.end) ||
    !reader.readBool(result.hit) ||
    !reader.readBool(result.headshot) ||
    !reader.readU8(targetPlayerIndex) ||
    !reader.readI32(damageApplied) ||
    !readVec3(reader, result.knockbackImpulse) ||
    !reader.readFloat(result.freezeApplied) ||
    !reader.readU32(result.requestedRewindTicks) ||
    !reader.readU32(result.appliedRewindTicks) ||
    !reader.readBool(result.rewindClamped) ||
    !reader.readBool(result.hasRewindDebug)
  ) {
    return false;
  }
  if (result.hasRewindDebug && (
    !reader.readU32(result.rewindTargetTick) ||
    !readVec3(reader, result.currentTargetPosition) ||
    !readVec3(reader, result.rewoundTargetPosition) ||
    !reader.readFloat(result.currentTargetBounds.radius) ||
    !reader.readFloat(result.currentTargetBounds.halfHeight) ||
    !reader.readFloat(result.rewoundTargetBounds.radius) ||
    !reader.readFloat(result.rewoundTargetBounds.halfHeight)
  )) {
    return false;
  }

  if (
    damageApplied < 0 ||
    (result.headshot && !result.hit) ||
    result.freezeApplied < 0.0F ||
    result.freezeApplied > 1000.0F ||
    (targetPlayerIndex != 255 && targetPlayerIndex >= kDuelPlayerCount) ||
    (result.hasRewindDebug && (result.currentTargetBounds.radius <= 0.0F ||
    result.currentTargetBounds.radius > 100.0F ||
    result.currentTargetBounds.halfHeight <= 0.0F ||
    result.currentTargetBounds.halfHeight > 100.0F ||
    result.rewoundTargetBounds.radius <= 0.0F ||
    result.rewoundTargetBounds.radius > 100.0F ||
    result.rewoundTargetBounds.halfHeight <= 0.0F ||
    result.rewoundTargetBounds.halfHeight > 100.0F))
  ) {
    return false;
  }
  result.targetPlayerIndex = targetPlayerIndex;
  result.damageApplied = damageApplied;
  return true;
}

bool writeWeaponFire(Writer& writer, const WeaponFireResult& result) {
  if (!writer.writeBool(result.fired)) {
    return false;
  }
  return !result.fired || (writeVec3(writer, result.start) &&
    writeVec3(writer, result.end) &&
    writer.writeBool(result.hit) &&
    writer.writeBool(result.headshot) &&
    writer.writeU8(static_cast<std::uint8_t>(result.weapon)) &&
    writer.writeI32(result.damageApplied) &&
    writeVec3(writer, result.knockbackImpulse) &&
    writer.writeU8(result.pelletCount) &&
    writer.writeU8(result.pelletHitCount) &&
    writer.writeU8(result.pelletHeadshotCount) &&
    writer.writeU32(result.visualSeed));
}

bool readWeaponFire(Reader& reader, WeaponFireResult& result) {
  std::uint8_t weapon = 0;
  std::int32_t damageApplied = 0;
  std::uint8_t pelletCount = 0;
  std::uint8_t pelletHitCount = 0;
  std::uint8_t pelletHeadshotCount = 0;
  std::uint32_t visualSeed = 0;
  if (!reader.readBool(result.fired)) {
    return false;
  }
  if (!result.fired) {
    return true;
  }
  if (
    !readVec3(reader, result.start) ||
    !readVec3(reader, result.end) ||
    !reader.readBool(result.hit) ||
    !reader.readBool(result.headshot) ||
    !reader.readU8(weapon) ||
    !reader.readI32(damageApplied) ||
    !readVec3(reader, result.knockbackImpulse) ||
    !reader.readU8(pelletCount) ||
    !reader.readU8(pelletHitCount) ||
    !reader.readU8(pelletHeadshotCount) ||
    !reader.readU32(visualSeed)
  ) {
    return false;
  }
  if (
    weapon > static_cast<std::uint8_t>(kLastWeapon) ||
    damageApplied < 0 ||
    (result.headshot && !result.hit) ||
    pelletHitCount > pelletCount ||
    pelletHeadshotCount > pelletHitCount ||
    pelletCount > kShotgunPelletCount
  ) {
    return false;
  }
  result.weapon = static_cast<Weapon>(weapon);
  result.damageApplied = damageApplied;
  result.pelletCount = pelletCount;
  result.pelletHitCount = pelletHitCount;
  result.pelletHeadshotCount = pelletHeadshotCount;
  result.visualSeed = visualSeed;
  return true;
}

bool writeRocketExplosion(Writer& writer, const RocketExplosionResult& result) {
  if (result.weapon > kLastWeapon) {
    return false;
  }
  if (!writer.writeBool(result.active)) {
    return false;
  }
  return !result.active || (writeVec3(writer, result.position) &&
    writer.writeFloat(result.radius) &&
    writer.writeI32(result.ownerDamageApplied) &&
    writer.writeI32(result.opponentDamageApplied) &&
    writer.writeU32(result.sequence) &&
    writer.writeU32(result.projectileSequence) &&
    writer.writeU8(static_cast<std::uint8_t>(result.weapon)));
}

bool readRocketExplosion(Reader& reader, RocketExplosionResult& result) {
  std::int32_t ownerDamageApplied = 0;
  std::int32_t opponentDamageApplied = 0;
  std::uint8_t weapon = 0;
  if (!reader.readBool(result.active)) {
    return false;
  }
  if (!result.active) {
    return true;
  }
  if (
    !readVec3(reader, result.position) ||
    !reader.readFloat(result.radius) ||
    !reader.readI32(ownerDamageApplied) ||
    !reader.readI32(opponentDamageApplied) ||
    !reader.readU32(result.sequence) ||
    !reader.readU32(result.projectileSequence) ||
    !reader.readU8(weapon)
  ) {
    return false;
  }
  if (
    result.radius < 0.0F ||
    result.radius > 100.0F ||
    ownerDamageApplied < 0 ||
    opponentDamageApplied < 0 ||
    weapon > static_cast<std::uint8_t>(kLastWeapon)
  ) {
    return false;
  }
  result.ownerDamageApplied = ownerDamageApplied;
  result.opponentDamageApplied = opponentDamageApplied;
  result.weapon = static_cast<Weapon>(weapon);
  return true;
}

bool writeFootstepAudioEvent(Writer& writer, const FootstepAudioEvent& event) {
  return writer.writeBool(event.active) && (!event.active || (
    writer.writeBool(event.jumping) &&
    writer.writeBool(event.landing) &&
    writer.writeU32(event.sequence) &&
    writeVec3(writer, event.position)));
}

bool readFootstepAudioEvent(Reader& reader, FootstepAudioEvent& event) {
  return reader.readBool(event.active) && (!event.active || (
    reader.readBool(event.jumping) &&
    reader.readBool(event.landing) &&
    reader.readU32(event.sequence) &&
    readVec3(reader, event.position)));
}

bool writeGrenadeBounceAudioEvent(
  Writer& writer,
  const GrenadeBounceAudioEvent& event
) {
  return writer.writeBool(event.active) && (!event.active || (
    writer.writeU32(event.sequence) &&
    writeVec3(writer, event.position)));
}

bool readGrenadeBounceAudioEvent(
  Reader& reader,
  GrenadeBounceAudioEvent& event
) {
  return reader.readBool(event.active) && (!event.active || (
    reader.readU32(event.sequence) &&
    readVec3(reader, event.position)));
}

bool writeFragEvent(Writer& writer, const FragEvent& event) {
  if (
    (event.active && event.targetPlayerIndex >= kDuelPlayerCount) ||
    event.weapon > kLastWeapon
  ) {
    return false;
  }
  return writer.writeBool(event.active) && (!event.active || (
    writer.writeU32(event.sequence) &&
    writer.writeU8(event.targetPlayerIndex) &&
    writer.writeU8(static_cast<std::uint8_t>(event.weapon))));
}

bool readFragEvent(Reader& reader, FragEvent& event) {
  std::uint8_t weapon = 0;
  if (
    !reader.readBool(event.active) ||
    (!event.active ? false :
    !reader.readU32(event.sequence) ||
    !reader.readU8(event.targetPlayerIndex) ||
    !reader.readU8(weapon))
  ) {
    return false;
  }
  if (weapon > static_cast<std::uint8_t>(kLastWeapon)) {
    return false;
  }
  event.weapon = static_cast<Weapon>(weapon);
  return !event.active || event.targetPlayerIndex < kDuelPlayerCount;
}

bool writeLocalHitFeedbackEvent(
  Writer& writer,
  const LocalHitFeedbackEvent& event
) {
  if (
    (event.active && event.targetPlayerIndex >= kDuelPlayerCount) ||
    event.damageApplied < 0 ||
    event.weapon > kLastWeapon
  ) {
    return false;
  }
  return writer.writeBool(event.active) && (!event.active || (
    writer.writeU32(event.sequence) &&
    writer.writeU8(event.targetPlayerIndex) &&
    writer.writeI32(event.damageApplied) &&
    writer.writeBool(event.headshot) &&
    writer.writeU8(static_cast<std::uint8_t>(event.weapon))));
}

bool readLocalHitFeedbackEvent(
  Reader& reader,
  LocalHitFeedbackEvent& event
) {
  std::uint8_t weapon = 0;
  std::int32_t damageApplied = 0;
  if (
    !reader.readBool(event.active) ||
    (!event.active ? false :
    !reader.readU32(event.sequence) ||
    !reader.readU8(event.targetPlayerIndex) ||
    !reader.readI32(damageApplied) ||
    !reader.readBool(event.headshot) ||
    !reader.readU8(weapon))
  ) {
    return false;
  }
  if (
    (event.active && event.targetPlayerIndex >= kDuelPlayerCount) ||
    damageApplied < 0 ||
    weapon > static_cast<std::uint8_t>(kLastWeapon)
  ) {
    return false;
  }
  event.damageApplied = damageApplied;
  event.weapon = static_cast<Weapon>(weapon);
  return true;
}

bool writeIcePool(Writer& writer, const IcePool& pool) {
  return writer.writeBool(pool.active) && (!pool.active || (
    writeVec3(writer, pool.center) &&
    writeVec3(writer, pool.normal) &&
    writer.writeFloat(pool.radius) &&
    writer.writeFloat(pool.lifetimeSeconds)));
}

bool readIcePool(Reader& reader, IcePool& pool) {
  if (
    !reader.readBool(pool.active) ||
    (!pool.active ? false :
    !readVec3(reader, pool.center) ||
    !readVec3(reader, pool.normal) ||
    !reader.readFloat(pool.radius) ||
    !reader.readFloat(pool.lifetimeSeconds))
  ) {
    return false;
  }
  return !pool.active || (
    pool.radius >= 0.0F &&
    pool.radius <= 100.0F &&
    pool.lifetimeSeconds >= 0.0F &&
    pool.lifetimeSeconds <= 60.0F &&
    pool.normal.z >= 0.0F &&
    length(pool.normal) > 0.5F &&
    length(pool.normal) < 1.5F);
}

bool writeRoundCombatStats(
  Writer& writer,
  const RoundCombatStats& stats
) {
  for (const WeaponCombatStats& weaponStats : stats.weapons) {
    if (
      !writer.writeU32(weaponStats.damageDealt) ||
      !writer.writeU16(weaponStats.attempts) ||
      !writer.writeU16(weaponStats.hits)
    ) {
      return false;
    }
  }
  return true;
}

bool readRoundCombatStats(
  Reader& reader,
  RoundCombatStats& stats
) {
  for (WeaponCombatStats& weaponStats : stats.weapons) {
    if (
      !reader.readU32(weaponStats.damageDealt) ||
      !reader.readU16(weaponStats.attempts) ||
      !reader.readU16(weaponStats.hits) ||
      weaponStats.hits > weaponStats.attempts
    ) {
      return false;
    }
  }
  return true;
}

template <typename Array, typename IsActive, typename WriteValue>
bool writeSparseArray(
  Writer& writer,
  const Array& values,
  IsActive isActive,
  WriteValue writeValue
) {
  constexpr std::size_t elementCount = std::tuple_size_v<Array>;
  constexpr std::size_t wordCount = (elementCount + 31U) / 32U;
  static_assert(elementCount > 0);
  // Fixed slot indices remain authoritative; the mask only omits inactive bodies.
  std::array<std::uint32_t, wordCount> activeMasks = {};
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (isActive(values[index])) {
      activeMasks[index / 32U] |= std::uint32_t{1} << (index % 32U);
    }
  }
  for (const std::uint32_t activeMask : activeMasks) {
    if (!writer.writeU32(activeMask)) {
      return false;
    }
  }
  for (std::size_t index = 0; index < values.size(); ++index) {
    if ((activeMasks[index / 32U] &
         (std::uint32_t{1} << (index % 32U))) != 0 &&
        !writeValue(writer, values[index])) {
      return false;
    }
  }
  return true;
}

template <typename Array, typename ReadValue>
bool readSparseArray(Reader& reader, Array& values, ReadValue readValue) {
  constexpr std::size_t elementCount = std::tuple_size_v<Array>;
  constexpr std::size_t wordCount = (elementCount + 31U) / 32U;
  static_assert(elementCount > 0);
  std::array<std::uint32_t, wordCount> activeMasks = {};
  for (std::uint32_t& activeMask : activeMasks) {
    if (!reader.readU32(activeMask)) {
      return false;
    }
  }
  constexpr std::size_t lastWordBits = elementCount % 32U;
  if constexpr (lastWordBits != 0U) {
    constexpr std::uint32_t validLastMask =
      (std::uint32_t{1} << lastWordBits) - 1U;
    // Unused high bits are never aliases for future authoritative slots.
    if ((activeMasks.back() & ~validLastMask) != 0U) {
      return false;
    }
  }
  for (std::size_t index = 0; index < values.size(); ++index) {
    if ((activeMasks[index / 32U] &
         (std::uint32_t{1} << (index % 32U))) != 0 &&
        !readValue(reader, values[index])) {
      return false;
    }
  }
  return true;
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
    (flags != 0 &&
     !((encodedType == static_cast<std::uint8_t>(PacketType::Snapshot) ||
        encodedType == static_cast<std::uint8_t>(PacketType::CombatStats)) &&
       flags == kCompressedPayloadFlag)) ||
    reserved != 0 ||
    payloadBytes != wire.size() - kHeaderBytes ||
    encodedType < static_cast<std::uint8_t>(PacketType::ConnectRequest) ||
    encodedType > static_cast<std::uint8_t>(PacketType::ProjectileUpdates)
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
  return packet.clientIndex < kMaxNetworkClients &&
    (packet.playerIndex < kDuelPlayerCount ||
     packet.playerIndex == kNoAssignedPlayer) &&
    writeHeader(writer, PacketType::ConnectAccept) &&
    writer.writeU32(packet.clientNonce) &&
    writer.writeU8(packet.clientIndex) &&
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
    !reader.readU8(decoded.clientIndex) ||
    !reader.readU8(decoded.playerIndex) ||
    !reader.readU32(decoded.serverTick) ||
    decoded.clientIndex >= kMaxNetworkClients ||
    (decoded.playerIndex >= kDuelPlayerCount &&
     decoded.playerIndex != kNoAssignedPlayer) ||
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
    finishPacket(writer) &&
    writer.size() <= kMaxUdpApplicationDatagramBytes;
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

  // Decode into a temporary and commit only after full validation and exact
  // consumption, so a malformed packet cannot partially mutate caller state.
  packet = decoded;
  return true;
}

bool encodeCommandBundle(const CommandBundle& bundle, WirePacket& wire) {
  if (bundle.datagramSequence == 0 || bundle.commandCount == 0 ||
      bundle.commandCount > kMaxBundledCommands) {
    return false;
  }
  const CommandPacket& newest = bundle.commands[bundle.commandCount - 1U];
  for (std::size_t index = 0; index < bundle.commandCount; ++index) {
    if (
      bundle.commands[index].clientIndex != newest.clientIndex ||
      bundle.commands[index].playerIndex != newest.playerIndex ||
      bundle.commands[index].clientNonce != newest.clientNonce
    ) {
      return false;
    }
  }

  Writer writer(wire);
  if (
    !writeHeader(writer, PacketType::CommandBundle) ||
    !writer.writeU32(bundle.datagramSequence) ||
    !writer.writeU8(newest.clientIndex) ||
    !writer.writeU8(newest.playerIndex) ||
    !writer.writeU32(newest.clientNonce) ||
    !writeActionEdgeState(writer, bundle.actionEdges) ||
    !writer.writeU8(bundle.commandCount)
  ) {
    return false;
  }
  for (std::size_t index = 0; index < bundle.commandCount; ++index) {
    if (!writeCompactCommand(writer, bundle.commands[index])) {
      return false;
    }
  }
  return finishPacket(writer) &&
    writer.size() <= kMaxUdpApplicationDatagramBytes;
}

bool decodeCommandBundle(const WirePacket& wire, CommandBundle& bundle) {
  Reader reader(wire);
  CommandBundle decoded;
  std::uint8_t clientIndex = 0;
  std::uint8_t playerIndex = 0;
  std::uint32_t clientNonce = 0;
  if (
    !readHeader(reader, PacketType::CommandBundle, wire.size()) ||
    !reader.readU32(decoded.datagramSequence) ||
    !reader.readU8(clientIndex) ||
    !reader.readU8(playerIndex) ||
    !reader.readU32(clientNonce) ||
    !readActionEdgeState(reader, decoded.actionEdges) ||
    !reader.readU8(decoded.commandCount) ||
    decoded.datagramSequence == 0 ||
    clientIndex >= kMaxNetworkClients ||
    (playerIndex >= kDuelPlayerCount && playerIndex != kNoAssignedPlayer) ||
    decoded.commandCount == 0 ||
    decoded.commandCount > kMaxBundledCommands
  ) {
    return false;
  }
  for (std::size_t index = 0; index < decoded.commandCount; ++index) {
    if (!readCompactCommand(
          reader,
          clientIndex,
          playerIndex,
          clientNonce,
          decoded.actionEdges,
          decoded.commands[index]
        )) {
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
    !isValidMapName(snapshot.map.mapName) ||
    snapshot.map.contentHash == 0 ||
    snapshot.mapRevision == 0U ||
    snapshot.projectileRevision == 0U ||
    !isValidWeaponSwitchingMode(snapshot.weaponSwitchingMode) ||
    !isValidBotAttackMode(snapshot.botAttackMode) ||
    !isValidWeapon(snapshot.botWeapon) ||
    !isValidTeam(snapshot.roundWinningTeam) ||
    !isValidTeam(snapshot.matchWinningTeam) ||
    !isValidMcGuffinSnapshot(snapshot.mcguffin) ||
    !isValidTeam(snapshot.mcguffinRedBaseOwner) ||
    !isValidTeam(snapshot.mcguffinBlueBaseOwner) ||
    !isValidMcGuffinConfig(snapshot.mcguffinConfig) ||
    !isValidProjectilePresentationTuning(snapshot.projectilePresentation) ||
    !std::all_of(
      snapshot.teams.begin(),
      snapshot.teams.end(),
      [](Team team) { return isValidTeam(team); }
    ) ||
    !std::all_of(
      snapshot.selectedWeapons.begin(),
      snapshot.selectedWeapons.end(),
      [](Weapon weapon) { return isValidWeapon(weapon); }
    ) ||
    !std::all_of(
      snapshot.weaponAmmo.spawnAmmo.begin(),
      snapshot.weaponAmmo.spawnAmmo.end(),
      [](std::int32_t ammo) { return ammo >= 0 && ammo <= 999; }
    ) ||
    !std::all_of(
      snapshot.playerAmmo.begin(),
      snapshot.playerAmmo.end(),
      [](const WeaponAmmoArray& ammo) {
        return std::all_of(
          ammo.begin(),
          ammo.end(),
          [](std::int32_t value) { return value >= 0 && value <= 999; }
        );
      }
    )
  ) {
    return false;
  }

  Writer writer(wire);
  if (
    !writeHeader(writer, PacketType::Snapshot) ||
    !writer.writeU32(snapshot.serverTick) ||
    !writer.writeBool(snapshot.hasLocalClientState) ||
    !writer.writeU8(snapshot.localPlayerIndex) ||
    !writer.writeBool(snapshot.localSpectator) ||
    !writer.writeU8(snapshot.spectatorCount) ||
    !writer.writeU32(snapshot.acknowledgedCommandDatagramSequence) ||
    !writer.writeU32(snapshot.commandDatagramAckBits) ||
    !writer.writeU32(snapshot.mapRevision) ||
    !writer.writeU32(snapshot.projectileRevision) ||
    !writer.writeString(snapshot.map.mapName, kMaxMapNameBytes) ||
    !writer.writeU32(snapshot.map.contentHash)
  ) {
    return false;
  }

  if (snapshot.hasLocalClientState && !snapshot.localSpectator &&
      snapshot.localPlayerIndex >= kDuelPlayerCount) {
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
  for (Weapon weapon : snapshot.selectedWeapons) {
    if (!writer.writeU8(static_cast<std::uint8_t>(weapon))) {
      return false;
    }
  }
  for (std::uint8_t charge : snapshot.sniperChargePercent) {
    if (charge > 100U || !writer.writeU8(charge)) {
      return false;
    }
  }
  for (const WeaponAmmoArray& ammo : snapshot.playerAmmo) {
    for (std::int32_t value : ammo) {
      if (!writer.writeI32(value)) {
        return false;
      }
    }
  }
  if (!writeSparseArray(writer, snapshot.lightningGuns,
        [](const auto& value) { return value.active; }, writeLightningGun) ||
      !writeSparseArray(writer, snapshot.weaponFires,
        [](const auto& value) { return value.fired; }, writeWeaponFire) ||
      !writeSparseArray(writer, snapshot.rocketExplosions,
        [](const auto& value) { return value.active; }, writeRocketExplosion) ||
      !writeSparseArray(writer, snapshot.footstepAudioEvents,
        [](const auto& value) { return value.active; }, writeFootstepAudioEvent) ||
      !writeSparseArray(writer, snapshot.grenadeBounceAudioEvents,
        [](const auto& value) { return value.active; }, writeGrenadeBounceAudioEvent) ||
      !writeSparseArray(writer, snapshot.fragEvents,
        [](const auto& value) { return value.active; }, writeFragEvent)) {
    return false;
  }
  constexpr std::size_t hitFeedbackBitCount =
    kDuelPlayerCount * kLocalHitFeedbackEventWindow;
  constexpr std::size_t hitFeedbackWordCount =
    (hitFeedbackBitCount + 31U) / 32U;
  std::array<std::uint32_t, hitFeedbackWordCount> hitFeedbackMasks = {};
  for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
    for (std::size_t eventIndex = 0; eventIndex < kLocalHitFeedbackEventWindow; ++eventIndex) {
      const std::size_t bit = playerIndex * kLocalHitFeedbackEventWindow + eventIndex;
      if (snapshot.localHitFeedbackEvents[playerIndex][eventIndex].active) {
        hitFeedbackMasks[bit / 32U] |= std::uint32_t{1} << (bit % 32U);
      }
    }
  }
  for (const std::uint32_t mask : hitFeedbackMasks) {
    if (!writer.writeU32(mask)) return false;
  }
  for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
    for (std::size_t eventIndex = 0; eventIndex < kLocalHitFeedbackEventWindow; ++eventIndex) {
      const std::size_t bit = playerIndex * kLocalHitFeedbackEventWindow + eventIndex;
      if ((hitFeedbackMasks[bit / 32U] &
           (std::uint32_t{1} << (bit % 32U))) != 0 &&
          !writeLocalHitFeedbackEvent(
            writer,
            snapshot.localHitFeedbackEvents[playerIndex][eventIndex]
          )) {
        return false;
      }
    }
  }
  if (!writeSparseArray(writer, snapshot.icePools,
        [](const auto& value) { return value.active; }, writeIcePool)) {
    return false;
  }
  for (bool available : snapshot.healthPickupAvailable) {
    if (!writer.writeBool(available)) {
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
  for (std::uint16_t score : snapshot.mcguffinScores) {
    if (!writer.writeU16(score)) return false;
  }
  for (std::uint8_t rounds : snapshot.mcguffinRoundsWon) {
    if (!writer.writeU8(rounds)) return false;
  }
  if (
    !writer.writeU8(snapshot.mcguffinRound) ||
    !writer.writeU8(static_cast<std::uint8_t>(snapshot.mcguffinRedBaseOwner)) ||
    !writer.writeU8(static_cast<std::uint8_t>(snapshot.mcguffinBlueBaseOwner)) ||
    !writer.writeU8(static_cast<std::uint8_t>(snapshot.mcguffin.state)) ||
    !writer.writeU8(static_cast<std::uint8_t>(snapshot.mcguffin.associatedTeam)) ||
    !writer.writeU8(static_cast<std::uint8_t>(snapshot.mcguffin.carrierTeam)) ||
    !writer.writeU8(snapshot.mcguffin.carrierIndex) ||
    !writeVec3(writer, snapshot.mcguffin.position) ||
    !writeVec3(writer, snapshot.mcguffin.velocity) ||
    !writer.writeU32(snapshot.mcguffin.stateTicks) ||
    !writer.writeU32(snapshot.mcguffin.scoreSubPoints) ||
    !writer.writeU32(snapshot.mcguffin.carrySubPoints) ||
    !writer.writeU16(snapshot.mcguffin.carriedPoints) ||
    !writer.writeU32(snapshot.mcguffin.interactionTicks) ||
    !writer.writeU32(snapshot.mcguffin.finalHoldTicks) ||
    !writer.writeU32(snapshot.mcguffin.eventSequence) ||
    !writer.writeU8(static_cast<std::uint8_t>(snapshot.mcguffin.lastEvent)) ||
    !writer.writeU8(snapshot.mcguffin.eventPlayerIndex)
  ) return false;
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
  for (bool bot : snapshot.botPlayers) {
    if (!writer.writeBool(bot)) {
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
  if (!writer.writeBool(snapshot.hasCombatStats)) {
    return false;
  }
  if (snapshot.hasCombatStats) {
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
  }
  for (const std::string& playerName : snapshot.playerNames) {
    if (!writer.writeString(playerName, kMaxPlayerNameBytes)) {
      return false;
    }
  }
  if (!writer.writeU8(static_cast<std::uint8_t>(snapshot.matchPhase)) ||
      !writer.writeU32(snapshot.configurationRevision) ||
      !writer.writeBool(snapshot.hasConfiguration)) {
    return false;
  }
  if (snapshot.hasConfiguration && !(
    writer.writeU16(snapshot.matchRules.roundLimit) &&
    writer.writeU16(snapshot.matchRules.timeLimitMinutes) &&
    writer.writeU8(snapshot.matchRules.playerLimit) &&
    writer.writeU16(snapshot.matchRules.countdownTicks) &&
    writer.writeU16(snapshot.matchRules.roundEndTicks) &&
    writer.writeU16(snapshot.matchRules.matchEndTicks) &&
    writer.writeU16(snapshot.matchRules.deathRespawnTicks) &&
    writer.writeBool(snapshot.matchRules.showOpponentHealth) &&
    writer.writeBool(snapshot.movementTuning.flightEnabled) &&
    writer.writeBool(snapshot.movementTuning.airControlEnabled) &&
    writer.writeFloat(snapshot.movementTuning.groundAcceleration) &&
    writer.writeFloat(snapshot.movementTuning.airAcceleration) &&
    writer.writeFloat(snapshot.movementTuning.groundFriction) &&
    writer.writeFloat(snapshot.movementTuning.stopSpeed) &&
    writer.writeFloat(snapshot.movementTuning.maxGroundSpeed) &&
    writer.writeFloat(snapshot.movementTuning.dashTargetSpeed) &&
    writer.writeFloat(snapshot.movementTuning.dashMaxSpeed) &&
    writer.writeFloat(snapshot.movementTuning.dashAcceleration) &&
    writer.writeFloat(snapshot.movementTuning.dashDuration) &&
    writer.writeFloat(snapshot.movementTuning.dashCooldown) &&
    writer.writeFloat(snapshot.movementTuning.dashGroundHopVelocity) &&
    writer.writeFloat(snapshot.movementTuning.dashAirHopVelocity) &&
    writer.writeFloat(snapshot.movementTuning.flightAcceleration) &&
    writer.writeFloat(snapshot.movementTuning.maxFlightSpeed) &&
    writer.writeFloat(snapshot.movementTuning.flightDamping) &&
    writer.writeFloat(snapshot.movementTuning.flightGravityCancel) &&
    writer.writeFloat(snapshot.playerSizeScaleXY) &&
    writer.writeFloat(snapshot.playerSizeScaleZ) &&
    writer.writeFloat(snapshot.lightningKnockback) &&
    writer.writeFloat(snapshot.lightningFireHz) &&
    writer.writeFloat(snapshot.rocketKnockback) &&
    writer.writeI32(snapshot.knockbackTimeMs) &&
    writer.writeI32(snapshot.weaponDamage.shotgunDamagePerPellet) &&
    writer.writeI32(snapshot.weaponDamage.machineGunDamage) &&
    writer.writeI32(snapshot.weaponDamage.lightningGunDamage) &&
    writer.writeI32(snapshot.weaponDamage.railgunDamage) &&
    writer.writeI32(snapshot.weaponDamage.rocketLauncherDamage) &&
    writer.writeI32(snapshot.weaponDamage.plasmaGunDamage) &&
    writer.writeI32(snapshot.weaponDamage.freezeGunDamage) &&
    writer.writeFloat(snapshot.icePoolTuning.maxRadius) &&
    writer.writeFloat(snapshot.icePoolTuning.growthPerSecond) &&
    writer.writeFloat(snapshot.icePoolTuning.lifetimeSeconds) &&
    writer.writeFloat(snapshot.icePoolTuning.friction) &&
    writer.writeFloat(snapshot.icePoolTuning.slopeGravityScale) &&
    writer.writeFloat(snapshot.icePoolTuning.controlScale) &&
    writer.writeFloat(snapshot.icePoolTuning.mergeDistance) &&
    writer.writeU32(snapshot.projectilePresentation.rocketLifetimeTicks) &&
    writer.writeU32(snapshot.projectilePresentation.grenadeFuseTicks) &&
    writer.writeU32(snapshot.projectilePresentation.plasmaLifetimeTicks) &&
    writer.writeFloat(snapshot.projectilePresentation.grenadeGravity) &&
    writer.writeFloat(snapshot.projectilePresentation.grenadeBounceDamping) &&
    writer.writeFloat(snapshot.projectilePresentation.grenadeRestSpeed) &&
    writer.writeBool(snapshot.weaponAmmo.infiniteAmmo) &&
    writer.writeI32(snapshot.weaponAmmo.spawnAmmo[weaponIndex(Weapon::LightningGun)]) &&
    writer.writeI32(snapshot.weaponAmmo.spawnAmmo[weaponIndex(Weapon::Railgun)]) &&
    writer.writeI32(snapshot.weaponAmmo.spawnAmmo[weaponIndex(Weapon::RocketLauncher)]) &&
    writer.writeI32(snapshot.weaponAmmo.spawnAmmo[weaponIndex(Weapon::MachineGun)]) &&
    writer.writeI32(snapshot.weaponAmmo.spawnAmmo[weaponIndex(Weapon::Shotgun)]) &&
    writer.writeI32(snapshot.weaponAmmo.spawnAmmo[weaponIndex(Weapon::GrenadeLauncher)]) &&
    writer.writeI32(snapshot.weaponAmmo.spawnAmmo[weaponIndex(Weapon::PlasmaGun)]) &&
    writer.writeI32(snapshot.weaponAmmo.spawnAmmo[weaponIndex(Weapon::FreezeGun)]) &&
    writer.writeI32(snapshot.weaponAmmo.spawnAmmo[weaponIndex(Weapon::Revolver)]) &&
    writer.writeFloat(snapshot.vampirism) &&
    writer.writeU8(snapshot.selfDamagePercent) &&
    writer.writeI32(snapshot.healthAmount) &&
    writer.writeU16(snapshot.mcguffinConfig.scoreLimit) &&
    writer.writeU16(snapshot.mcguffinConfig.pointsPerSecond) &&
    writer.writeU16(snapshot.mcguffinConfig.carryPointsPerSecond) &&
    writer.writeU16(snapshot.mcguffinConfig.carryPointLimit) &&
    writer.writeU32(snapshot.mcguffinConfig.initialSpawnTicks) &&
    writer.writeU32(snapshot.mcguffinConfig.installationDelayTicks) &&
    writer.writeU32(snapshot.mcguffinConfig.stealTicks) &&
    writer.writeU32(snapshot.mcguffinConfig.returnTicks) &&
    writer.writeFloat(snapshot.mcguffinConfig.throwSpeed) &&
    writer.writeFloat(snapshot.mcguffinConfig.throwUpSpeed) &&
    writer.writeFloat(snapshot.mcguffinConfig.throwVelocityInheritance) &&
    writer.writeFloat(snapshot.mcguffinConfig.throwGravity) &&
    writer.writeFloat(snapshot.mcguffinConfig.throwBounceDamping) &&
    writer.writeU32(snapshot.mcguffinConfig.throwPickupLockoutTicks) &&
    writer.writeU32(snapshot.mcguffinConfig.finalHoldTicks) &&
    writer.writeFloat(snapshot.mcguffinConfig.pickupRadius) &&
    writer.writeU8(static_cast<std::uint8_t>(snapshot.weaponSwitchingMode)))) {
    return false;
  }
  if (!(writer.writeBool(snapshot.botDodgeEnabled) &&
    writer.writeI32(snapshot.botDodgeMinIntervalMs) &&
    writer.writeI32(snapshot.botDodgeMaxIntervalMs) &&
    writer.writeBool(snapshot.botStareEnabled) &&
    writer.writeBool(snapshot.botStandstillEnabled) &&
    writer.writeU8(static_cast<std::uint8_t>(snapshot.botAttackMode)) &&
    writer.writeU8(static_cast<std::uint8_t>(snapshot.botWeapon)) &&
    writer.writeU32(snapshot.phaseTicksRemaining) &&
    writer.writeU32(snapshot.liveTicksElapsed) &&
    writer.writeBool(snapshot.overtime) &&
    writer.writeU8(snapshot.roundWinner) &&
    writer.writeU8(snapshot.matchWinner) &&
    writer.writeBool(snapshot.playersColliding) &&
    finishPacket(writer))) {
    return false;
  }
  return compactSnapshotWire(wire);
}

bool encodeBoundedGameplaySnapshot(
  const ServerSnapshot& snapshot,
  WirePacket& wire
) {
  if (encodeServerSnapshot(snapshot, wire)) return true;

  ServerSnapshot bounded = snapshot;

  // Rewind geometry and movement sounds only affect presentation. Drop them
  // first so player health, damage, and other authoritative state still reach
  // every gameplay transport after a large same-tick event burst.
  for (LightningGunResult& result : bounded.lightningGuns) {
    result.hasRewindDebug = false;
  }
  bounded.footstepAudioEvents.fill({});
  bounded.grenadeBounceAudioEvents.fill({});
  if (encodeServerSnapshot(bounded, wire)) return true;

  // Hit feedback is recipient-specific only when the transport tags its
  // recipient. Loopback and simulated snapshots have no such tag, so preserve
  // every window there and move to the next fallback tier.
  if (bounded.hasLocalClientState) {
    for (std::size_t player = 0; player < kDuelPlayerCount; ++player) {
      if (
        bounded.localSpectator ||
        player != bounded.localPlayerIndex
      ) {
        bounded.localHitFeedbackEvents[player].fill({});
      }
    }
    if (encodeServerSnapshot(bounded, wire)) return true;
  }

  // Recurring beams cost less to lose than one-shot feedback. Keep hit
  // confirmations and frag events until lower-priority visuals are gone.
  bounded.lightningGuns.fill({});
  if (encodeServerSnapshot(bounded, wire)) return true;

  // The next snapshot is self-contained, so transient fire and blast visuals
  // may be cut before player, projectile, objective, or score state.
  bounded.weaponFires.fill({});
  bounded.rocketExplosions.fill({});
  if (encodeServerSnapshot(bounded, wire)) return true;

  bounded.fragEvents.fill({});
  for (auto& events : bounded.localHitFeedbackEvents) events.fill({});
  return encodeServerSnapshot(bounded, wire);
}

bool decodeServerSnapshot(const WirePacket& wire, ServerSnapshot& snapshot) {
  WirePacket expandedWire;
  if (!expandSnapshotWire(wire, expandedWire)) return false;
  Reader reader(expandedWire);
  auto decodedStorage = std::make_unique<ServerSnapshot>();
  ServerSnapshot& decoded = *decodedStorage;
  if (
    !readHeader(reader, PacketType::Snapshot, expandedWire.size()) ||
    !reader.readU32(decoded.serverTick) ||
    !reader.readBool(decoded.hasLocalClientState) ||
    !reader.readU8(decoded.localPlayerIndex) ||
    !reader.readBool(decoded.localSpectator) ||
    !reader.readU8(decoded.spectatorCount) ||
    !reader.readU32(decoded.acknowledgedCommandDatagramSequence) ||
    !reader.readU32(decoded.commandDatagramAckBits) ||
    !reader.readU32(decoded.mapRevision) ||
    !reader.readU32(decoded.projectileRevision) ||
    !reader.readString(decoded.map.mapName, kMaxMapNameBytes) ||
    !reader.readU32(decoded.map.contentHash) ||
    !isValidMapName(decoded.map.mapName) ||
    decoded.map.contentHash == 0 ||
    decoded.mapRevision == 0U ||
    decoded.projectileRevision == 0U ||
    (decoded.hasLocalClientState && !decoded.localSpectator &&
     decoded.localPlayerIndex >= kDuelPlayerCount) ||
    decoded.spectatorCount > kMaxSpectatorClients
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
  for (Weapon& weapon : decoded.selectedWeapons) {
    std::uint8_t encodedWeapon = 0;
    if (!reader.readU8(encodedWeapon)) {
      return false;
    }
    if (encodedWeapon > static_cast<std::uint8_t>(kLastWeapon)) {
      return false;
    }
    weapon = static_cast<Weapon>(encodedWeapon);
  }
  for (std::uint8_t& charge : decoded.sniperChargePercent) {
    if (!reader.readU8(charge) || charge > 100U) {
      return false;
    }
  }
  for (WeaponAmmoArray& ammo : decoded.playerAmmo) {
    for (std::int32_t& value : ammo) {
      if (!reader.readI32(value) || value < 0 || value > 999) {
        return false;
      }
    }
  }
  if (!readSparseArray(reader, decoded.lightningGuns, readLightningGun) ||
      !readSparseArray(reader, decoded.weaponFires, readWeaponFire) ||
      !readSparseArray(reader, decoded.rocketExplosions, readRocketExplosion) ||
      !readSparseArray(reader, decoded.footstepAudioEvents, readFootstepAudioEvent) ||
      !readSparseArray(reader, decoded.grenadeBounceAudioEvents, readGrenadeBounceAudioEvent) ||
      !readSparseArray(reader, decoded.fragEvents, readFragEvent)) {
    return false;
  }
  constexpr std::size_t hitFeedbackBitCount =
    kDuelPlayerCount * kLocalHitFeedbackEventWindow;
  constexpr std::size_t hitFeedbackWordCount =
    (hitFeedbackBitCount + 31U) / 32U;
  std::array<std::uint32_t, hitFeedbackWordCount> hitFeedbackMasks = {};
  for (std::uint32_t& mask : hitFeedbackMasks) {
    if (!reader.readU32(mask)) return false;
  }
  constexpr std::size_t hitFeedbackLastWordBits = hitFeedbackBitCount % 32U;
  if constexpr (hitFeedbackLastWordBits != 0U) {
    constexpr std::uint32_t validLastMask =
      (std::uint32_t{1} << hitFeedbackLastWordBits) - 1U;
    if ((hitFeedbackMasks.back() & ~validLastMask) != 0U) return false;
  }
  for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
    for (std::size_t eventIndex = 0; eventIndex < kLocalHitFeedbackEventWindow; ++eventIndex) {
      const std::size_t bit = playerIndex * kLocalHitFeedbackEventWindow + eventIndex;
      if ((hitFeedbackMasks[bit / 32U] &
           (std::uint32_t{1} << (bit % 32U))) != 0 &&
          !readLocalHitFeedbackEvent(
            reader,
            decoded.localHitFeedbackEvents[playerIndex][eventIndex]
          )) {
        return false;
      }
    }
  }
  if (!readSparseArray(reader, decoded.icePools, readIcePool)) {
    return false;
  }
  for (bool& available : decoded.healthPickupAvailable) {
    if (!reader.readBool(available)) {
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
  for (std::uint16_t& score : decoded.mcguffinScores) {
    if (!reader.readU16(score)) return false;
  }
  for (std::uint8_t& rounds : decoded.mcguffinRoundsWon) {
    if (!reader.readU8(rounds)) return false;
  }
  std::uint8_t redBaseOwner = 0;
  std::uint8_t blueBaseOwner = 0;
  std::uint8_t mcguffinState = 0;
  std::uint8_t associatedTeam = 0;
  std::uint8_t carrierTeam = 0;
  std::uint8_t mcguffinEvent = 0;
  if (
    !reader.readU8(decoded.mcguffinRound) ||
    !reader.readU8(redBaseOwner) ||
    !reader.readU8(blueBaseOwner) ||
    !reader.readU8(mcguffinState) ||
    !reader.readU8(associatedTeam) ||
    !reader.readU8(carrierTeam) ||
    !reader.readU8(decoded.mcguffin.carrierIndex) ||
    !readVec3(reader, decoded.mcguffin.position) ||
    !readVec3(reader, decoded.mcguffin.velocity) ||
    !reader.readU32(decoded.mcguffin.stateTicks) ||
    !reader.readU32(decoded.mcguffin.scoreSubPoints) ||
    !reader.readU32(decoded.mcguffin.carrySubPoints) ||
    !reader.readU16(decoded.mcguffin.carriedPoints) ||
    !reader.readU32(decoded.mcguffin.interactionTicks) ||
    !reader.readU32(decoded.mcguffin.finalHoldTicks) ||
    !reader.readU32(decoded.mcguffin.eventSequence) ||
    !reader.readU8(mcguffinEvent) ||
    !reader.readU8(decoded.mcguffin.eventPlayerIndex)
  ) return false;
  decoded.mcguffinRedBaseOwner = static_cast<Team>(redBaseOwner);
  decoded.mcguffinBlueBaseOwner = static_cast<Team>(blueBaseOwner);
  decoded.mcguffin.state = static_cast<McGuffinState>(mcguffinState);
  decoded.mcguffin.associatedTeam = static_cast<Team>(associatedTeam);
  decoded.mcguffin.carrierTeam = static_cast<Team>(carrierTeam);
  decoded.mcguffin.lastEvent = static_cast<McGuffinEventType>(mcguffinEvent);
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
    !isValidTeam(decoded.mcguffinRedBaseOwner) ||
    !isValidTeam(decoded.mcguffinBlueBaseOwner) ||
    !isValidMcGuffinSnapshot(decoded.mcguffin) ||
    !isValidMcGuffinConfig(decoded.mcguffinConfig) ||
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
  for (bool& bot : decoded.botPlayers) {
    if (!reader.readBool(bot)) {
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
  if (!reader.readBool(decoded.hasCombatStats)) {
    return false;
  }
  if (decoded.hasCombatStats) {
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
  }
  for (std::string& playerName : decoded.playerNames) {
    if (!reader.readString(playerName, kMaxPlayerNameBytes)) {
      return false;
    }
  }
  std::uint8_t matchPhase = 0;
  std::uint8_t weaponSwitchingMode = 0;
  std::uint8_t botAttackMode = 0;
  std::uint8_t botWeapon = 0;
  if (
    !reader.readU8(matchPhase) ||
    matchPhase > static_cast<std::uint8_t>(MatchPhase::MatchEnd) ||
    !reader.readU32(decoded.configurationRevision) ||
    decoded.configurationRevision == 0 ||
    !reader.readBool(decoded.hasConfiguration) ||
    (decoded.hasConfiguration && (
    !reader.readU16(decoded.matchRules.roundLimit) ||
    !reader.readU16(decoded.matchRules.timeLimitMinutes) ||
    !reader.readU8(decoded.matchRules.playerLimit) ||
    !reader.readU16(decoded.matchRules.countdownTicks) ||
    !reader.readU16(decoded.matchRules.roundEndTicks) ||
    !reader.readU16(decoded.matchRules.matchEndTicks) ||
    !reader.readU16(decoded.matchRules.deathRespawnTicks) ||
    !reader.readBool(decoded.matchRules.showOpponentHealth) ||
    !reader.readBool(decoded.movementTuning.flightEnabled) ||
    !reader.readBool(decoded.movementTuning.airControlEnabled) ||
    !reader.readFloat(decoded.movementTuning.groundAcceleration) ||
    !reader.readFloat(decoded.movementTuning.airAcceleration) ||
    !reader.readFloat(decoded.movementTuning.groundFriction) ||
    !reader.readFloat(decoded.movementTuning.stopSpeed) ||
    !reader.readFloat(decoded.movementTuning.maxGroundSpeed) ||
    !reader.readFloat(decoded.movementTuning.dashTargetSpeed) ||
    !reader.readFloat(decoded.movementTuning.dashMaxSpeed) ||
    !reader.readFloat(decoded.movementTuning.dashAcceleration) ||
    !reader.readFloat(decoded.movementTuning.dashDuration) ||
    !reader.readFloat(decoded.movementTuning.dashCooldown) ||
    !reader.readFloat(decoded.movementTuning.dashGroundHopVelocity) ||
    !reader.readFloat(decoded.movementTuning.dashAirHopVelocity) ||
    !reader.readFloat(decoded.movementTuning.flightAcceleration) ||
    !reader.readFloat(decoded.movementTuning.maxFlightSpeed) ||
    !reader.readFloat(decoded.movementTuning.flightDamping) ||
    !reader.readFloat(decoded.movementTuning.flightGravityCancel) ||
    !reader.readFloat(decoded.playerSizeScaleXY) ||
    !reader.readFloat(decoded.playerSizeScaleZ) ||
    !reader.readFloat(decoded.lightningKnockback) ||
    !reader.readFloat(decoded.lightningFireHz) ||
    !reader.readFloat(decoded.rocketKnockback) ||
    !reader.readI32(decoded.knockbackTimeMs) ||
    !reader.readI32(decoded.weaponDamage.shotgunDamagePerPellet) ||
    !reader.readI32(decoded.weaponDamage.machineGunDamage) ||
    !reader.readI32(decoded.weaponDamage.lightningGunDamage) ||
    !reader.readI32(decoded.weaponDamage.railgunDamage) ||
    !reader.readI32(decoded.weaponDamage.rocketLauncherDamage) ||
    !reader.readI32(decoded.weaponDamage.plasmaGunDamage) ||
    !reader.readI32(decoded.weaponDamage.freezeGunDamage) ||
    !reader.readFloat(decoded.icePoolTuning.maxRadius) ||
    !reader.readFloat(decoded.icePoolTuning.growthPerSecond) ||
    !reader.readFloat(decoded.icePoolTuning.lifetimeSeconds) ||
    !reader.readFloat(decoded.icePoolTuning.friction) ||
    !reader.readFloat(decoded.icePoolTuning.slopeGravityScale) ||
    !reader.readFloat(decoded.icePoolTuning.controlScale) ||
    !reader.readFloat(decoded.icePoolTuning.mergeDistance) ||
    !reader.readU32(decoded.projectilePresentation.rocketLifetimeTicks) ||
    !reader.readU32(decoded.projectilePresentation.grenadeFuseTicks) ||
    !reader.readU32(decoded.projectilePresentation.plasmaLifetimeTicks) ||
    !reader.readFloat(decoded.projectilePresentation.grenadeGravity) ||
    !reader.readFloat(decoded.projectilePresentation.grenadeBounceDamping) ||
    !reader.readFloat(decoded.projectilePresentation.grenadeRestSpeed) ||
    !reader.readBool(decoded.weaponAmmo.infiniteAmmo) ||
    !reader.readI32(decoded.weaponAmmo.spawnAmmo[weaponIndex(Weapon::LightningGun)]) ||
    !reader.readI32(decoded.weaponAmmo.spawnAmmo[weaponIndex(Weapon::Railgun)]) ||
    !reader.readI32(decoded.weaponAmmo.spawnAmmo[weaponIndex(Weapon::RocketLauncher)]) ||
    !reader.readI32(decoded.weaponAmmo.spawnAmmo[weaponIndex(Weapon::MachineGun)]) ||
    !reader.readI32(decoded.weaponAmmo.spawnAmmo[weaponIndex(Weapon::Shotgun)]) ||
    !reader.readI32(decoded.weaponAmmo.spawnAmmo[weaponIndex(Weapon::GrenadeLauncher)]) ||
    !reader.readI32(decoded.weaponAmmo.spawnAmmo[weaponIndex(Weapon::PlasmaGun)]) ||
    !reader.readI32(decoded.weaponAmmo.spawnAmmo[weaponIndex(Weapon::FreezeGun)]) ||
    !reader.readI32(decoded.weaponAmmo.spawnAmmo[weaponIndex(Weapon::Revolver)]) ||
    !reader.readFloat(decoded.vampirism) ||
    !reader.readU8(decoded.selfDamagePercent) ||
    !reader.readI32(decoded.healthAmount) ||
    !reader.readU16(decoded.mcguffinConfig.scoreLimit) ||
    !reader.readU16(decoded.mcguffinConfig.pointsPerSecond) ||
    !reader.readU16(decoded.mcguffinConfig.carryPointsPerSecond) ||
    !reader.readU16(decoded.mcguffinConfig.carryPointLimit) ||
    !reader.readU32(decoded.mcguffinConfig.initialSpawnTicks) ||
    !reader.readU32(decoded.mcguffinConfig.installationDelayTicks) ||
    !reader.readU32(decoded.mcguffinConfig.stealTicks) ||
    !reader.readU32(decoded.mcguffinConfig.returnTicks) ||
    !reader.readFloat(decoded.mcguffinConfig.throwSpeed) ||
    !reader.readFloat(decoded.mcguffinConfig.throwUpSpeed) ||
    !reader.readFloat(decoded.mcguffinConfig.throwVelocityInheritance) ||
    !reader.readFloat(decoded.mcguffinConfig.throwGravity) ||
    !reader.readFloat(decoded.mcguffinConfig.throwBounceDamping) ||
    !reader.readU32(decoded.mcguffinConfig.throwPickupLockoutTicks) ||
    !reader.readU32(decoded.mcguffinConfig.finalHoldTicks) ||
    !reader.readFloat(decoded.mcguffinConfig.pickupRadius) ||
    !reader.readU8(weaponSwitchingMode))) ||
    !reader.readBool(decoded.botDodgeEnabled) ||
    !reader.readI32(decoded.botDodgeMinIntervalMs) ||
    !reader.readI32(decoded.botDodgeMaxIntervalMs) ||
    !reader.readBool(decoded.botStareEnabled) ||
    !reader.readBool(decoded.botStandstillEnabled) ||
    !reader.readU8(botAttackMode) ||
    !reader.readU8(botWeapon) ||
    !reader.readU32(decoded.phaseTicksRemaining) ||
    !reader.readU32(decoded.liveTicksElapsed) ||
    !reader.readBool(decoded.overtime) ||
    !reader.readU8(decoded.roundWinner) ||
    !reader.readU8(decoded.matchWinner) ||
    !reader.readBool(decoded.playersColliding) ||
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
    decoded.movementTuning.dashTargetSpeed < 0.0F ||
    decoded.movementTuning.dashTargetSpeed > 100.0F ||
    decoded.movementTuning.dashMaxSpeed < 0.0F ||
    decoded.movementTuning.dashMaxSpeed > 100.0F ||
    decoded.movementTuning.dashAcceleration < 0.0F ||
    decoded.movementTuning.dashAcceleration > 1000.0F ||
    decoded.movementTuning.dashDuration < 0.0F ||
    decoded.movementTuning.dashDuration > 2.0F ||
    decoded.movementTuning.dashCooldown < 0.0F ||
    decoded.movementTuning.dashCooldown > 10.0F ||
    decoded.movementTuning.dashGroundHopVelocity < 0.0F ||
    decoded.movementTuning.dashGroundHopVelocity > 100.0F ||
    decoded.movementTuning.dashAirHopVelocity < 0.0F ||
    decoded.movementTuning.dashAirHopVelocity > 100.0F ||
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
    decoded.lightningFireHz < kMinLightningFireHz ||
    decoded.lightningFireHz > kMaxLightningFireHz ||
    decoded.rocketKnockback < 0.0F ||
    decoded.rocketKnockback > kMaxRocketKnockback ||
    decoded.knockbackTimeMs < 0 ||
    decoded.knockbackTimeMs > 250 ||
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
    decoded.weaponDamage.plasmaGunDamage < 1 ||
    decoded.weaponDamage.plasmaGunDamage > 500 ||
    decoded.weaponDamage.freezeGunDamage < 1 ||
    decoded.weaponDamage.freezeGunDamage > 500 ||
    decoded.icePoolTuning.maxRadius < 0.0F ||
    decoded.icePoolTuning.maxRadius > 100.0F ||
    decoded.icePoolTuning.growthPerSecond < 0.0F ||
    decoded.icePoolTuning.growthPerSecond > 1000.0F ||
    decoded.icePoolTuning.lifetimeSeconds < 0.0F ||
    decoded.icePoolTuning.lifetimeSeconds > 60.0F ||
    decoded.icePoolTuning.friction < 0.0F ||
    decoded.icePoolTuning.friction > 100.0F ||
    decoded.icePoolTuning.slopeGravityScale < 0.0F ||
    decoded.icePoolTuning.slopeGravityScale > 10.0F ||
    decoded.icePoolTuning.controlScale < 0.0F ||
    decoded.icePoolTuning.controlScale > 1.0F ||
    decoded.icePoolTuning.mergeDistance < 0.0F ||
    decoded.icePoolTuning.mergeDistance > 100.0F ||
    !isValidProjectilePresentationTuning(decoded.projectilePresentation) ||
    !std::all_of(
      decoded.weaponAmmo.spawnAmmo.begin(),
      decoded.weaponAmmo.spawnAmmo.end(),
      [](std::int32_t ammo) { return ammo >= 0 && ammo <= 999; }
    ) ||
    decoded.vampirism < 0.0F ||
    decoded.vampirism > 2.0F ||
    decoded.selfDamagePercent > 100 ||
    decoded.healthAmount < 1 ||
    decoded.healthAmount > 100000 ||
    !isValidMcGuffinConfig(decoded.mcguffinConfig) ||
    decoded.botDodgeMinIntervalMs < 1 ||
    decoded.botDodgeMinIntervalMs > 10000 ||
    decoded.botDodgeMaxIntervalMs < 1 ||
    decoded.botDodgeMaxIntervalMs > 10000 ||
    botAttackMode > static_cast<std::uint8_t>(BotAttackMode::Hard) ||
    botWeapon > static_cast<std::uint8_t>(kLastWeapon) ||
    weaponSwitchingMode > static_cast<std::uint8_t>(WeaponSwitchingMode::Crazy) ||
    (decoded.roundWinner != 255 && decoded.roundWinner >= kDuelPlayerCount) ||
    (decoded.matchWinner != 255 && decoded.matchWinner >= kDuelPlayerCount) ||
    reader.remaining() != 0
  ) {
    return false;
  }
  decoded.matchPhase = static_cast<MatchPhase>(matchPhase);
  decoded.weaponSwitchingMode =
    static_cast<WeaponSwitchingMode>(weaponSwitchingMode);
  decoded.botAttackMode = static_cast<BotAttackMode>(botAttackMode);
  decoded.botWeapon = static_cast<Weapon>(botWeapon);

  snapshot = std::move(decoded);
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

bool encodeChatHistoryChunk(
  const ChatHistoryChunk& packet,
  WirePacket& wire
) {
  if (
    packet.messageCount == 0U ||
    packet.messageCount > kChatHistoryChunkCapacity ||
    packet.oldestAvailableSequence == 0U ||
    packet.latestSequence == 0U
  ) {
    return false;
  }
  Writer writer(wire);
  if (
    !writeHeader(writer, PacketType::ChatHistory) ||
    !writer.writeU32(packet.oldestAvailableSequence) ||
    !writer.writeU32(packet.latestSequence) ||
    !writer.writeU8(packet.messageCount)
  ) {
    return false;
  }
  for (std::size_t index = 0; index < packet.messageCount; ++index) {
    const ChatMessage& message = packet.messages[index];
    if (
      message.sequence == 0U ||
      (message.playerIndex >= kDuelPlayerCount &&
       message.playerIndex != kNoAssignedPlayer) ||
      !writer.writeU32(message.sequence) ||
      !writer.writeU8(message.playerIndex) ||
      !writer.writeString(message.speakerName, kMaxPlayerNameBytes) ||
      !writer.writeString(message.message, kMaxChatMessageBytes)
    ) {
      return false;
    }
  }
  return finishPacket(writer) &&
    writer.size() <= kMaxUdpApplicationDatagramBytes;
}

bool decodeChatHistoryChunk(
  const WirePacket& wire,
  ChatHistoryChunk& packet
) {
  Reader reader(wire);
  ChatHistoryChunk decoded;
  if (
    !readHeader(reader, PacketType::ChatHistory, wire.size()) ||
    !reader.readU32(decoded.oldestAvailableSequence) ||
    !reader.readU32(decoded.latestSequence) ||
    !reader.readU8(decoded.messageCount) ||
    decoded.oldestAvailableSequence == 0U ||
    decoded.latestSequence == 0U ||
    decoded.messageCount == 0U ||
    decoded.messageCount > kChatHistoryChunkCapacity
  ) {
    return false;
  }
  for (std::size_t index = 0; index < decoded.messageCount; ++index) {
    ChatMessage& message = decoded.messages[index];
    if (
      !reader.readU32(message.sequence) ||
      !reader.readU8(message.playerIndex) ||
      !reader.readString(message.speakerName, kMaxPlayerNameBytes) ||
      !reader.readString(message.message, kMaxChatMessageBytes) ||
      message.sequence == 0U ||
      (message.playerIndex >= kDuelPlayerCount &&
       message.playerIndex != kNoAssignedPlayer)
    ) {
      return false;
    }
  }
  if (reader.remaining() != 0U) {
    return false;
  }
  packet = std::move(decoded);
  return true;
}

bool encodeChatHistoryAck(
  const ChatHistoryAck& packet,
  WirePacket& wire
) {
  if (packet.sequence == 0U) {
    return false;
  }
  Writer writer(wire);
  return writeHeader(writer, PacketType::ChatHistoryAck) &&
    writer.writeU32(packet.sequence) &&
    finishPacket(writer);
}

bool decodeChatHistoryAck(
  const WirePacket& wire,
  ChatHistoryAck& packet
) {
  Reader reader(wire);
  ChatHistoryAck decoded;
  if (
    !readHeader(reader, PacketType::ChatHistoryAck, wire.size()) ||
    !reader.readU32(decoded.sequence) ||
    decoded.sequence == 0U ||
    reader.remaining() != 0U
  ) {
    return false;
  }
  packet = decoded;
  return true;
}

bool encodeCombatStatsPacket(
  const CombatStatsPacket& packet,
  WirePacket& wire
) {
  const std::size_t first = packet.firstPlayerIndex;
  const std::size_t count = packet.playerCount;
  if (count == 0U || first >= kDuelPlayerCount ||
      count > kDuelPlayerCount - first) {
    return false;
  }
  Writer writer(wire);
  if (!writeHeader(writer, PacketType::CombatStats) ||
      !writer.writeU32(packet.serverTick) ||
      !writer.writeU8(packet.firstPlayerIndex) ||
      !writer.writeU8(packet.playerCount)) return false;
  for (std::size_t index = first; index < first + count; ++index) {
    if (!writeRoundCombatStats(writer, packet.round[index])) return false;
  }
  for (std::size_t index = first; index < first + count; ++index) {
    if (!writeRoundCombatStats(writer, packet.match[index])) return false;
  }
  if (!finishPacket(writer)) return false;
  return compactSnapshotWire(wire);
}

bool decodeCombatStatsPacket(
  const WirePacket& wire,
  CombatStatsPacket& packet
) {
  WirePacket expandedWire;
  if (!expandSnapshotWire(wire, expandedWire)) return false;
  Reader reader(expandedWire);
  CombatStatsPacket decoded;
  if (!readHeader(reader, PacketType::CombatStats, expandedWire.size()) ||
      !reader.readU32(decoded.serverTick) ||
      !reader.readU8(decoded.firstPlayerIndex) ||
      !reader.readU8(decoded.playerCount) ||
      decoded.playerCount == 0U ||
      decoded.firstPlayerIndex >= kDuelPlayerCount ||
      decoded.playerCount > kDuelPlayerCount - decoded.firstPlayerIndex) {
    return false;
  }
  const std::size_t first = decoded.firstPlayerIndex;
  const std::size_t count = decoded.playerCount;
  for (std::size_t index = first; index < first + count; ++index) {
    if (!readRoundCombatStats(reader, decoded.round[index])) return false;
  }
  for (std::size_t index = first; index < first + count; ++index) {
    if (!readRoundCombatStats(reader, decoded.match[index])) return false;
  }
  if (reader.remaining() != 0) return false;
  packet = decoded;
  return true;
}

bool encodeProjectileUpdatePacket(
  const ProjectileUpdatePacket& packet,
  WirePacket& wire
) {
  if (
    packet.mapRevision == 0U ||
    packet.projectileRevision == 0U ||
    packet.updateCount > kMaxProjectileUpdatesPerPacket
  ) {
    wire.clear();
    return false;
  }

  std::array<bool, kMaxRocketProjectiles> seenSlots = {};
  Writer writer(wire);
  if (
    !writeHeader(writer, PacketType::ProjectileUpdates) ||
    !writer.writeU32(packet.serverTick) ||
    !writer.writeU32(packet.mapRevision) ||
    !writer.writeU32(packet.projectileRevision) ||
    !writer.writeU8(packet.updateCount)
  ) {
    wire.clear();
    return false;
  }
  for (std::size_t index = 0; index < packet.updateCount; ++index) {
    const ProjectileUpdate& update = packet.updates[index];
    if (
      !isValidProjectileUpdate(update) ||
      seenSlots[update.slot] ||
      !writer.writeU16(update.slot) ||
      !writer.writeU32(update.sequence) ||
      !writer.writeU8(static_cast<std::uint8_t>(update.kind)) ||
      !writer.writeU8(static_cast<std::uint8_t>(update.weapon)) ||
      !writeVec3(writer, update.position) ||
      !writeVec3(writer, update.velocity) ||
      !writer.writeFloat(update.radius) ||
      !writer.writeU32(update.ageTicks) ||
      !writer.writeBool(update.resting)
    ) {
      wire.clear();
      return false;
    }
    seenSlots[update.slot] = true;
  }
  if (
    !finishPacket(writer) ||
    writer.size() > kMaxUdpApplicationDatagramBytes
  ) {
    wire.clear();
    return false;
  }
  return true;
}

bool decodeProjectileUpdatePacket(
  const WirePacket& wire,
  ProjectileUpdatePacket& packet
) {
  if (wire.size() > kMaxUdpApplicationDatagramBytes) {
    return false;
  }

  Reader reader(wire);
  ProjectileUpdatePacket decoded;
  if (
    !readHeader(reader, PacketType::ProjectileUpdates, wire.size()) ||
    !reader.readU32(decoded.serverTick) ||
    !reader.readU32(decoded.mapRevision) ||
    !reader.readU32(decoded.projectileRevision) ||
    !reader.readU8(decoded.updateCount) ||
    decoded.mapRevision == 0U ||
    decoded.projectileRevision == 0U ||
    decoded.updateCount > kMaxProjectileUpdatesPerPacket
  ) {
    return false;
  }

  std::array<bool, kMaxRocketProjectiles> seenSlots = {};
  for (std::size_t index = 0; index < decoded.updateCount; ++index) {
    ProjectileUpdate& update = decoded.updates[index];
    std::uint8_t kind = 0;
    std::uint8_t weapon = 0;
    if (
      !reader.readU16(update.slot) ||
      !reader.readU32(update.sequence) ||
      !reader.readU8(kind) ||
      !reader.readU8(weapon) ||
      !readVec3(reader, update.position) ||
      !readVec3(reader, update.velocity) ||
      !reader.readFloat(update.radius) ||
      !reader.readU32(update.ageTicks) ||
      !reader.readBool(update.resting)
    ) {
      return false;
    }
    update.kind = static_cast<ProjectileUpdateKind>(kind);
    update.weapon = static_cast<Weapon>(weapon);
    if (
      !isValidProjectileUpdate(update) ||
      seenSlots[update.slot]
    ) {
      return false;
    }
    seenSlots[update.slot] = true;
  }
  if (reader.remaining() != 0U) {
    return false;
  }
  packet = decoded;
  return true;
}

} // namespace lg
