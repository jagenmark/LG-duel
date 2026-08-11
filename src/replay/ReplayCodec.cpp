#include "replay/ReplayCodec.hpp"

#include "net/NetCodec.hpp"
#include "sim/McGuffinRules.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <string_view>

namespace lg::replay {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic = {'L', 'G', 'D', 'M'};
constexpr std::size_t kFilePreambleBytes = 16U;
constexpr std::uint32_t kNoLimit = std::numeric_limits<std::uint32_t>::max();

class Writer {
public:
  explicit Writer(std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}

  [[nodiscard]] bool ok() const { return ok_; }
  [[nodiscard]] std::size_t size() const { return bytes_.size(); }

  bool u8(std::uint8_t value) { return append(value); }
  bool boolean(bool value) { return u8(value ? 1U : 0U); }
  bool u16(std::uint16_t value) {
    return append(static_cast<std::uint8_t>(value & 0xffU)) &&
      append(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
  }
  bool u32(std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
      if (!append(static_cast<std::uint8_t>((value >> shift) & 0xffU))) return false;
    }
    return true;
  }
  bool i32(std::int32_t value) { return u32(std::bit_cast<std::uint32_t>(value)); }
  bool u64(std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) {
      if (!append(static_cast<std::uint8_t>((value >> shift) & 0xffU))) return false;
    }
    return true;
  }
  bool f32(float value) {
    return std::isfinite(value) && u32(std::bit_cast<std::uint32_t>(value));
  }
  bool f64(double value) {
    return std::isfinite(value) && u64(std::bit_cast<std::uint64_t>(value));
  }
  bool string(std::string_view value, std::size_t maximum) {
    if (value.size() > maximum || value.size() > kNoLimit) return fail();
    return u32(static_cast<std::uint32_t>(value.size())) && bytes(value);
  }
  bool bytes(std::string_view value) {
    for (const char character : value) {
      if (!append(static_cast<std::uint8_t>(character))) return false;
    }
    return true;
  }
  bool raw(const std::vector<std::uint8_t>& value) {
    if (value.size() > kMaxReplayChunkBytes) return fail();
    for (const std::uint8_t byte : value) {
      if (!append(byte)) return false;
    }
    return true;
  }

private:
  bool append(std::uint8_t value) {
    if (!ok_ || bytes_.size() >= kMaxReplayBytes) return fail();
    bytes_.push_back(value);
    return true;
  }
  bool fail() {
    ok_ = false;
    return false;
  }

  std::vector<std::uint8_t>& bytes_;
  bool ok_ = true;
};

class Reader {
public:
  explicit Reader(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}

  [[nodiscard]] bool ok() const { return ok_; }
  [[nodiscard]] bool done() const { return ok_ && cursor_ == bytes_.size(); }
  [[nodiscard]] std::size_t remaining() const { return cursor_ <= bytes_.size() ? bytes_.size() - cursor_ : 0U; }

  bool u8(std::uint8_t& value) {
    if (remaining() < 1U) return fail();
    value = bytes_[cursor_++];
    return true;
  }
  bool boolean(bool& value) {
    std::uint8_t encoded = 0;
    if (!u8(encoded) || encoded > 1U) return fail();
    value = encoded != 0U;
    return true;
  }
  bool u16(std::uint16_t& value) {
    std::uint8_t low = 0;
    std::uint8_t high = 0;
    if (!u8(low) || !u8(high)) return false;
    value = static_cast<std::uint16_t>(low) |
      (static_cast<std::uint16_t>(high) << 8U);
    return true;
  }
  bool u32(std::uint32_t& value) {
    if (remaining() < 4U) return fail();
    value = 0;
    for (unsigned shift = 0; shift < 32; shift += 8) {
      value |= static_cast<std::uint32_t>(bytes_[cursor_++]) << shift;
    }
    return true;
  }
  bool i32(std::int32_t& value) {
    std::uint32_t raw = 0;
    if (!u32(raw)) return false;
    value = std::bit_cast<std::int32_t>(raw);
    return true;
  }
  bool u64(std::uint64_t& value) {
    if (remaining() < 8U) return fail();
    value = 0;
    for (unsigned shift = 0; shift < 64; shift += 8) {
      value |= static_cast<std::uint64_t>(bytes_[cursor_++]) << shift;
    }
    return true;
  }
  bool f32(float& value) {
    std::uint32_t raw = 0;
    if (!u32(raw)) return false;
    value = std::bit_cast<float>(raw);
    return std::isfinite(value) || fail();
  }
  bool f64(double& value) {
    std::uint64_t raw = 0;
    if (!u64(raw)) return false;
    value = std::bit_cast<double>(raw);
    return std::isfinite(value) || fail();
  }
  bool string(std::string& value, std::size_t maximum) {
    std::uint32_t length = 0;
    if (!u32(length) || length > maximum || length > remaining()) return fail();
    value.assign(reinterpret_cast<const char*>(bytes_.data() + cursor_), length);
    cursor_ += length;
    return true;
  }
  bool take(std::size_t length, std::vector<std::uint8_t>& value) {
    if (length > remaining()) return fail();
    value.assign(
      bytes_.begin() + static_cast<std::ptrdiff_t>(cursor_),
      bytes_.begin() + static_cast<std::ptrdiff_t>(cursor_ + length)
    );
    cursor_ += length;
    return true;
  }

private:
  bool fail() {
    ok_ = false;
    return false;
  }

  const std::vector<std::uint8_t>& bytes_;
  std::size_t cursor_ = 0;
  bool ok_ = true;
};

[[nodiscard]] bool validWeapon(Weapon value) {
  return value <= kLastWeapon;
}

[[nodiscard]] bool validPhase(MatchPhase value) {
  return value <= MatchPhase::MatchEnd;
}

[[nodiscard]] bool validMovementMode(MovementMode value) {
  return value == MovementMode::Grounded || value == MovementMode::Airborne ||
    value == MovementMode::Flying;
}

[[nodiscard]] bool validMcGuffinState(McGuffinState value) {
  return value <= McGuffinState::InstalledBlue;
}

[[nodiscard]] bool validLethalKind(LethalKind value) {
  return value <= LethalKind::World;
}

[[nodiscard]] bool validVisibility(ReplayVisibility value) {
  return value == ReplayVisibility::DeveloperFull || value == ReplayVisibility::DuelOnly;
}

[[nodiscard]] bool validVec3(const Vec3& value) {
  return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool writeVec3(Writer& writer, const Vec3& value) {
  return writer.f32(value.x) && writer.f32(value.y) && writer.f32(value.z);
}

bool readVec3(Reader& reader, Vec3& value) {
  return reader.f32(value.x) && reader.f32(value.y) && reader.f32(value.z);
}

bool writeCommand(Writer& writer, const UserCommand& command) {
  return validWeapon(command.weapon) &&
    writer.u32(command.sequence) && writer.u32(command.clientTick) &&
    writer.f32(command.viewYawRadians) && writer.f32(command.viewPitchRadians) &&
    writer.f32(command.forwardMove) && writer.f32(command.rightMove) && writer.f32(command.upMove) &&
    writer.boolean(command.attack) && writer.boolean(command.jump) && writer.boolean(command.dash) &&
    writer.boolean(command.crouch) && writer.boolean(command.sneak) && writer.boolean(command.zoomed) &&
    writer.boolean(command.planarAim) && writer.u8(static_cast<std::uint8_t>(command.weapon));
}

bool readCommand(Reader& reader, UserCommand& command) {
  std::uint8_t weapon = 0;
  if (!reader.u32(command.sequence) || !reader.u32(command.clientTick) ||
      !reader.f32(command.viewYawRadians) || !reader.f32(command.viewPitchRadians) ||
      !reader.f32(command.forwardMove) || !reader.f32(command.rightMove) || !reader.f32(command.upMove) ||
      !reader.boolean(command.attack) || !reader.boolean(command.jump) || !reader.boolean(command.dash) ||
      !reader.boolean(command.crouch) || !reader.boolean(command.sneak) || !reader.boolean(command.zoomed) ||
      !reader.boolean(command.planarAim) || !reader.u8(weapon)) return false;
  command.weapon = static_cast<Weapon>(weapon);
  return validWeapon(command.weapon);
}

bool writeActionEdges(Writer& writer, const ActionEdgeState& edges) {
  return validWeapon(edges.attackWeapon) &&
    writer.u32(edges.jump) && writer.u32(edges.dash) && writer.u32(edges.reset) &&
    writer.u32(edges.ready) && writer.u32(edges.mcguffinThrow) &&
    writer.f32(edges.mcguffinThrowYawRadians) && writer.f32(edges.mcguffinThrowPitchRadians) &&
    writer.u32(edges.attack) && writer.f32(edges.attackYawRadians) &&
    writer.f32(edges.attackPitchRadians) && writer.u32(edges.attackViewedServerTick) &&
    writer.u8(static_cast<std::uint8_t>(edges.attackWeapon)) && writer.boolean(edges.attackZoomed);
}

bool readActionEdges(Reader& reader, ActionEdgeState& edges) {
  std::uint8_t weapon = 0;
  if (!reader.u32(edges.jump) || !reader.u32(edges.dash) || !reader.u32(edges.reset) ||
      !reader.u32(edges.ready) || !reader.u32(edges.mcguffinThrow) ||
      !reader.f32(edges.mcguffinThrowYawRadians) || !reader.f32(edges.mcguffinThrowPitchRadians) ||
      !reader.u32(edges.attack) || !reader.f32(edges.attackYawRadians) ||
      !reader.f32(edges.attackPitchRadians) || !reader.u32(edges.attackViewedServerTick) ||
      !reader.u8(weapon) || !reader.boolean(edges.attackZoomed)) return false;
  edges.attackWeapon = static_cast<Weapon>(weapon);
  return validWeapon(edges.attackWeapon);
}

bool writePlayerState(Writer& writer, const PlayerState& player) {
  return validVec3(player.position) && validVec3(player.velocity) && validVec3(player.dashDirection) &&
    validMovementMode(player.movementMode) && writer.f32(player.position.x) && writer.f32(player.position.y) &&
    writer.f32(player.position.z) && writeVec3(writer, player.velocity) &&
    writer.f32(player.viewYawRadians) && writer.f32(player.viewPitchRadians) && writer.i32(player.health) &&
    writer.f32(player.freezeLevel) && writer.f32(player.bounds.radius) && writer.f32(player.bounds.halfHeight) &&
    writer.u8(static_cast<std::uint8_t>(player.movementMode)) &&
    writer.u16(player.knockbackTicksRemaining) && writer.u16(player.dashCooldownTicksRemaining) &&
    writer.u16(player.dashActiveTicksRemaining) && writeVec3(writer, player.dashDirection) &&
    writer.u16(player.jumpPadCooldownTicksRemaining) && writer.boolean(player.onGround) &&
    writer.boolean(player.jumpHeld) && writer.boolean(player.dashHeld) && writer.boolean(player.crouched) &&
    writer.boolean(player.sneaking);
}

bool readPlayerState(Reader& reader, PlayerState& player) {
  std::uint8_t mode = 0;
  if (!reader.f32(player.position.x) || !reader.f32(player.position.y) || !reader.f32(player.position.z) ||
      !readVec3(reader, player.velocity) || !reader.f32(player.viewYawRadians) ||
      !reader.f32(player.viewPitchRadians) || !reader.i32(player.health) ||
      !reader.f32(player.freezeLevel) || !reader.f32(player.bounds.radius) ||
      !reader.f32(player.bounds.halfHeight) || !reader.u8(mode) ||
      !reader.u16(player.knockbackTicksRemaining) || !reader.u16(player.dashCooldownTicksRemaining) ||
      !reader.u16(player.dashActiveTicksRemaining) || !readVec3(reader, player.dashDirection) ||
      !reader.u16(player.jumpPadCooldownTicksRemaining) || !reader.boolean(player.onGround) ||
      !reader.boolean(player.jumpHeld) || !reader.boolean(player.dashHeld) ||
      !reader.boolean(player.crouched) || !reader.boolean(player.sneaking)) return false;
  player.movementMode = static_cast<MovementMode>(mode);
  return validMovementMode(player.movementMode) && player.bounds.radius > 0.0F &&
    player.bounds.halfHeight > 0.0F && player.freezeLevel >= 0.0F;
}

bool writeWeaponState(Writer& writer, const ReplayWeaponState& weapon) {
  if (!validWeapon(weapon.selectedWeapon)) return false;
  if (!writer.u8(static_cast<std::uint8_t>(weapon.selectedWeapon))) return false;
  for (const std::int32_t ammo : weapon.ammo) {
    if (ammo < 0 || !writer.i32(ammo)) return false;
  }
  return writer.f64(weapon.lightningGun.fractionalDamage) && writer.f64(weapon.lightningGun.shotCredit) &&
    writer.f64(weapon.freezeGun.fractionalDamage) && writer.f64(weapon.freezeGun.shotCredit) &&
    writer.f64(weapon.lightningAmmoCredit) && writer.f64(weapon.freezeAmmoCredit) &&
    writer.f64(weapon.fractionalVampirismHealing) && writer.u32(weapon.railgunCooldownTicks) &&
    writer.u32(weapon.revolverCooldownTicks) && writer.f32(weapon.sniperAdsFraction) &&
    writer.f32(weapon.sniperChargeFraction) && writer.u32(weapon.machineGunCooldownTicks) &&
    writer.u32(weapon.shotgunCooldownTicks) && writer.u32(weapon.rocketCooldownTicks) &&
    writer.u32(weapon.grenadeCooldownTicks) && writer.u32(weapon.plasmaGunCooldownTicks) &&
    writer.u32(weapon.weaponPulloutTicks);
}

bool readWeaponState(Reader& reader, ReplayWeaponState& weapon) {
  std::uint8_t selected = 0;
  if (!reader.u8(selected)) return false;
  weapon.selectedWeapon = static_cast<Weapon>(selected);
  if (!validWeapon(weapon.selectedWeapon)) return false;
  for (std::int32_t& ammo : weapon.ammo) {
    if (!reader.i32(ammo) || ammo < 0) return false;
  }
  return reader.f64(weapon.lightningGun.fractionalDamage) && reader.f64(weapon.lightningGun.shotCredit) &&
    reader.f64(weapon.freezeGun.fractionalDamage) && reader.f64(weapon.freezeGun.shotCredit) &&
    reader.f64(weapon.lightningAmmoCredit) && reader.f64(weapon.freezeAmmoCredit) &&
    reader.f64(weapon.fractionalVampirismHealing) && reader.u32(weapon.railgunCooldownTicks) &&
    reader.u32(weapon.revolverCooldownTicks) && reader.f32(weapon.sniperAdsFraction) &&
    reader.f32(weapon.sniperChargeFraction) && reader.u32(weapon.machineGunCooldownTicks) &&
    reader.u32(weapon.shotgunCooldownTicks) && reader.u32(weapon.rocketCooldownTicks) &&
    reader.u32(weapon.grenadeCooldownTicks) && reader.u32(weapon.plasmaGunCooldownTicks) &&
    reader.u32(weapon.weaponPulloutTicks) && weapon.sniperAdsFraction >= 0.0F &&
    weapon.sniperAdsFraction <= 1.0F && weapon.sniperChargeFraction >= 0.0F &&
    weapon.sniperChargeFraction <= 1.0F;
}

bool writeRoundStats(Writer& writer, const RoundCombatStats& stats) {
  for (const WeaponCombatStats& weapon : stats.weapons) {
    if (!writer.u32(weapon.damageDealt) || !writer.u16(weapon.attempts) || !writer.u16(weapon.hits)) return false;
  }
  return true;
}

bool readRoundStats(Reader& reader, RoundCombatStats& stats) {
  for (WeaponCombatStats& weapon : stats.weapons) {
    if (!reader.u32(weapon.damageDealt) || !reader.u16(weapon.attempts) || !reader.u16(weapon.hits)) return false;
  }
  return true;
}

bool writeMetadata(Writer& writer, const ReplayMetadata& metadata) {
  if (metadata.mapName.empty() || metadata.mapName.size() > kMaxReplayMapNameBytes ||
      metadata.mapContentHash == 0U || metadata.mapRevision == 0U ||
      !isValidGameMode(metadata.gameMode) || !validVisibility(metadata.visibility)) return false;
  if (!writer.u32(metadata.formatFlags) || !writer.u32(metadata.protocolRevision) ||
      !writer.u64(metadata.buildFingerprint) || !writer.u64(metadata.gameplayConfigHash) || !writer.u32(metadata.initialServerTick) ||
      !writer.u32(metadata.mapRevision) || !writer.string(metadata.mapName, kMaxReplayMapNameBytes) ||
      !writer.u32(metadata.mapContentHash) || !writer.u8(static_cast<std::uint8_t>(metadata.gameMode)) ||
      !writer.u16(metadata.matchRules.roundLimit) || !writer.u16(metadata.matchRules.timeLimitMinutes) ||
      !writer.u8(metadata.matchRules.playerLimit) || !writer.u16(metadata.matchRules.countdownTicks) ||
      !writer.u16(metadata.matchRules.roundEndTicks) || !writer.u16(metadata.matchRules.matchEndTicks) ||
      !writer.u16(metadata.matchRules.deathRespawnTicks) || !writer.boolean(metadata.matchRules.showOpponentHealth) ||
      !writer.u8(static_cast<std::uint8_t>(metadata.visibility))) return false;
  for (std::size_t index = 0; index < metadata.players.size(); ++index) {
    const ReplayPlayerMetadata& player = metadata.players[index];
    if (player.slot != index || !isValidTeam(player.team) ||
        player.name.size() > kMaxReplayNameBytes || (!player.occupied && player.bot)) return false;
    if (!writer.u8(player.slot) || !writer.boolean(player.occupied) || !writer.boolean(player.bot) ||
        !writer.u8(static_cast<std::uint8_t>(player.team)) ||
        !writer.string(player.name, kMaxReplayNameBytes)) return false;
  }
  return true;
}

bool readMetadata(Reader& reader, ReplayMetadata& metadata) {
  std::uint8_t gameMode = 0;
  std::uint8_t visibility = 0;
  if (!reader.u32(metadata.formatFlags) || !reader.u32(metadata.protocolRevision) ||
      !reader.u64(metadata.buildFingerprint) || !reader.u64(metadata.gameplayConfigHash) || !reader.u32(metadata.initialServerTick) ||
      !reader.u32(metadata.mapRevision) || !reader.string(metadata.mapName, kMaxReplayMapNameBytes) ||
      !reader.u32(metadata.mapContentHash) || !reader.u8(gameMode) ||
      !reader.u16(metadata.matchRules.roundLimit) || !reader.u16(metadata.matchRules.timeLimitMinutes) ||
      !reader.u8(metadata.matchRules.playerLimit) || !reader.u16(metadata.matchRules.countdownTicks) ||
      !reader.u16(metadata.matchRules.roundEndTicks) || !reader.u16(metadata.matchRules.matchEndTicks) ||
      !reader.u16(metadata.matchRules.deathRespawnTicks) || !reader.boolean(metadata.matchRules.showOpponentHealth) ||
      !reader.u8(visibility)) return false;
  metadata.gameMode = static_cast<GameMode>(gameMode);
  metadata.visibility = static_cast<ReplayVisibility>(visibility);
  if (metadata.mapName.empty() || metadata.mapRevision == 0U || metadata.mapContentHash == 0U ||
      !isValidGameMode(metadata.gameMode) || !validVisibility(metadata.visibility)) return false;
  for (std::size_t index = 0; index < metadata.players.size(); ++index) {
    ReplayPlayerMetadata& player = metadata.players[index];
    std::uint8_t team = 0;
    if (!reader.u8(player.slot) || !reader.boolean(player.occupied) || !reader.boolean(player.bot) ||
        !reader.u8(team) || !reader.string(player.name, kMaxReplayNameBytes)) return false;
    player.team = static_cast<Team>(team);
    if (player.slot != index || !isValidTeam(player.team) || (!player.occupied && player.bot)) return false;
  }
  return true;
}

bool defaultCommand(const UserCommand& command) {
  return command.sequence == 0U && command.clientTick == 0U &&
    command.viewYawRadians == 0.0F && command.viewPitchRadians == 0.0F &&
    command.forwardMove == 0.0F && command.rightMove == 0.0F && command.upMove == 0.0F &&
    !command.attack && !command.jump && !command.dash && !command.crouch &&
    !command.sneak && !command.zoomed && command.planarAim &&
    command.weapon == Weapon::LightningGun;
}

bool defaultActionEdges(const ActionEdgeState& edges) {
  return edges.jump == 0U && edges.dash == 0U && edges.reset == 0U &&
    edges.ready == 0U && edges.mcguffinThrow == 0U &&
    edges.mcguffinThrowYawRadians == 0.0F && edges.mcguffinThrowPitchRadians == 0.0F &&
    edges.attack == 0U && edges.attackYawRadians == 0.0F && edges.attackPitchRadians == 0.0F &&
    edges.attackViewedServerTick == 0U && edges.attackWeapon == Weapon::LightningGun &&
    !edges.attackZoomed;
}

bool defaultAbsentReplaySlot(const ReplaySlotInput& slot) {
  return !slot.hasCommand && !slot.receivedThisTick && defaultCommand(slot.command) &&
    slot.viewedServerTick == 0U && defaultActionEdges(slot.consumedActionEdges) &&
    !slot.jumpEdgeAccepted && !slot.dashEdgeAccepted && !slot.attackEdgeAccepted &&
    defaultCommand(slot.attackEdgeCommand) && slot.attackEdgeViewedServerTick == 0U &&
    !slot.mcguffinThrowAccepted && defaultCommand(slot.mcguffinThrowCommand);
}

bool writeReplaySlotInput(Writer& writer, const ReplaySlotInput& slot) {
  return writer.boolean(slot.hasCommand) && writer.boolean(slot.receivedThisTick) &&
    writeCommand(writer, slot.command) && writer.u32(slot.viewedServerTick) &&
    writeActionEdges(writer, slot.consumedActionEdges) && writer.boolean(slot.jumpEdgeAccepted) &&
    writer.boolean(slot.dashEdgeAccepted) && writer.boolean(slot.attackEdgeAccepted) &&
    writeCommand(writer, slot.attackEdgeCommand) && writer.u32(slot.attackEdgeViewedServerTick) &&
    writer.boolean(slot.mcguffinThrowAccepted) && writeCommand(writer, slot.mcguffinThrowCommand);
}

bool readReplaySlotInput(Reader& reader, ReplaySlotInput& slot) {
  return reader.boolean(slot.hasCommand) && reader.boolean(slot.receivedThisTick) &&
    readCommand(reader, slot.command) && reader.u32(slot.viewedServerTick) &&
    readActionEdges(reader, slot.consumedActionEdges) && reader.boolean(slot.jumpEdgeAccepted) &&
    reader.boolean(slot.dashEdgeAccepted) && reader.boolean(slot.attackEdgeAccepted) &&
    readCommand(reader, slot.attackEdgeCommand) && reader.u32(slot.attackEdgeViewedServerTick) &&
    reader.boolean(slot.mcguffinThrowAccepted) && readCommand(reader, slot.mcguffinThrowCommand);
}

bool writeTickInput(Writer& writer, const ReplayTickInput& input) {
  static_assert(kDuelPlayerCount <= 16U);
  std::uint16_t presentMask = 0U;
  for (std::size_t index = 0U; index < input.slots.size(); ++index) {
    if (input.slots[index].present) {
      presentMask |= static_cast<std::uint16_t>(1U << index);
    } else if (!defaultAbsentReplaySlot(input.slots[index])) {
      return false;
    }
  }
  if (!writer.u32(input.tick) || !writer.u16(presentMask)) return false;
  for (std::size_t index = 0U; index < input.slots.size(); ++index) {
    if ((presentMask & static_cast<std::uint16_t>(1U << index)) != 0U &&
        !writeReplaySlotInput(writer, input.slots[index])) return false;
  }
  return true;
}

bool readTickInput(Reader& reader, ReplayTickInput& input) {
  static_assert(kDuelPlayerCount <= 16U);
  std::uint16_t presentMask = 0U;
  constexpr std::uint32_t kSlotMask = (1U << kDuelPlayerCount) - 1U;
  if (!reader.u32(input.tick) || !reader.u16(presentMask) ||
      (static_cast<std::uint32_t>(presentMask) & ~kSlotMask) != 0U) return false;
  for (std::size_t index = 0U; index < input.slots.size(); ++index) {
    ReplaySlotInput& slot = input.slots[index];
    if ((presentMask & static_cast<std::uint16_t>(1U << index)) == 0U) continue;
    slot.present = true;
    if (!readReplaySlotInput(reader, slot)) return false;
  }
  return true;
}

bool writeCheckpoint(Writer& writer, const ReplayCheckpoint& checkpoint) {
  if (checkpoint.mapRevision == 0U || checkpoint.projectileRevision == 0U ||
      checkpoint.history.empty() || checkpoint.history.size() > kMaxReplayHistoryFrames ||
      checkpoint.spawnRandomState == 0U || checkpoint.nextDeathmatchSpawnIndex >= Arena::kSpawnCount) return false;
  if (!writer.u32(checkpoint.serverTick) || !writer.u32(checkpoint.mapRevision) ||
      !writer.u32(checkpoint.projectileRevision) || !writer.u64(checkpoint.gameplayConfigHash)) return false;
  for (const ReplayCheckpointPlayer& player : checkpoint.players) {
    if (!isValidTeam(player.team)) return false;
    if (!writer.boolean(player.connected) || !writer.boolean(player.participating) ||
        !writer.boolean(player.ready) ||
        !writer.u8(static_cast<std::uint8_t>(player.team)) || !writePlayerState(writer, player.player) ||
        !writeWeaponState(writer, player.weapon) || !writer.u32(player.respawnTicksRemaining) ||
        !writeCommand(writer, player.command) || !writeActionEdges(writer, player.consumedActionEdges) ||
        !writer.u32(player.viewedServerTick) || !writer.boolean(player.hasCommand)) return false;
  }
  for (const ReplayProjectile& projectile : checkpoint.projectiles) {
    if (!validWeapon(projectile.weapon) || projectile.owner >= kDuelPlayerCount ||
        !validVec3(projectile.position) || !validVec3(projectile.previousPosition) ||
        !validVec3(projectile.velocity) || projectile.projectileRadius < 0.0F ||
        projectile.projectileHitboxRadius < 0.0F || (projectile.active && projectile.sequence == 0U)) return false;
    if (!writer.boolean(projectile.active) || !writer.u8(projectile.owner) || writer.u32(projectile.sequence) == false ||
        !writer.u8(static_cast<std::uint8_t>(projectile.weapon)) || !writeVec3(writer, projectile.position) ||
        !writeVec3(writer, projectile.previousPosition) || !writeVec3(writer, projectile.velocity) ||
        !writer.f32(projectile.projectileRadius) || !writer.f32(projectile.projectileHitboxRadius) ||
        !writer.boolean(projectile.ownerCollisionArmed) || !writer.boolean(projectile.resting) ||
        !writer.u32(projectile.ageTicks)) return false;
  }
  const ReplayMatchState& match = checkpoint.match;
  if (!isValidGameMode(match.gameMode) || !validPhase(match.phase) ||
      !isValidTeam(match.roundWinningTeam) || !isValidTeam(match.matchWinningTeam) ||
      (match.roundWinner != kNoReplayPlayer && match.roundWinner >= kDuelPlayerCount) ||
      (match.matchWinner != kNoReplayPlayer && match.matchWinner >= kDuelPlayerCount)) return false;
  if (!writer.u8(static_cast<std::uint8_t>(match.gameMode)) || !writer.u8(static_cast<std::uint8_t>(match.phase)) ||
      !writer.u32(match.phaseTicksRemaining) || !writer.u32(match.liveTicksElapsed) || !writer.boolean(match.overtime)) return false;
  for (const std::uint16_t value : match.scores) if (!writer.u16(value)) return false;
  for (const std::uint16_t value : match.teamScores) if (!writer.u16(value)) return false;
  for (const std::uint16_t value : match.mcguffinScores) if (!writer.u16(value)) return false;
  for (const std::uint8_t value : match.mcguffinRoundsWon) if (!writer.u8(value)) return false;
  if (!writer.u8(match.mcguffinRound) || !writer.u8(match.roundWinner) || !writer.u8(match.matchWinner) ||
      !writer.u8(static_cast<std::uint8_t>(match.roundWinningTeam)) ||
      !writer.u8(static_cast<std::uint8_t>(match.matchWinningTeam))) return false;
  for (const RoundCombatStats& stats : match.roundCombatStats) if (!writeRoundStats(writer, stats)) return false;
  for (const RoundCombatStats& stats : match.matchCombatStats) if (!writeRoundStats(writer, stats)) return false;
  for (const bool available : checkpoint.healthPickupAvailable) if (!writer.boolean(available)) return false;
  for (const std::uint32_t ticks : checkpoint.healthPickupCooldownTicks) if (!writer.u32(ticks)) return false;
  for (const IcePool& ice : checkpoint.icePools) {
    if (!validVec3(ice.center) || !validVec3(ice.normal) || ice.radius < 0.0F || ice.lifetimeSeconds < 0.0F ||
        !writer.boolean(ice.active) || !writeVec3(writer, ice.center) || !writeVec3(writer, ice.normal) ||
        !writer.f32(ice.radius) || !writer.f32(ice.lifetimeSeconds)) return false;
  }
  const McGuffinObjective& objective = checkpoint.mcguffin;
  if (!validMcGuffinState(objective.state) || !isValidMcGuffinObjective(objective) ||
      !validVec3(objective.position) || !validVec3(objective.velocity) || !validVec3(objective.spawnPosition) ||
      !isValidTeam(checkpoint.mcguffinRedBaseOwner) || !isValidTeam(checkpoint.mcguffinBlueBaseOwner)) return false;
  if (!writer.u8(static_cast<std::uint8_t>(objective.state)) || !writer.u8(static_cast<std::uint8_t>(objective.associatedTeam)) ||
      !writer.u8(static_cast<std::uint8_t>(objective.carrierTeam)) || !writer.u8(objective.carrierIndex) ||
      !writeVec3(writer, objective.position) || !writeVec3(writer, objective.velocity) ||
      !writeVec3(writer, objective.spawnPosition) || !writer.u32(objective.stateTicks) ||
      !writer.u32(objective.scoreSubPoints) || !writer.u8(static_cast<std::uint8_t>(checkpoint.mcguffinRedBaseOwner)) ||
      !writer.u8(static_cast<std::uint8_t>(checkpoint.mcguffinBlueBaseOwner))) return false;
  for (const std::uint32_t value : checkpoint.mcguffinStealTicks) if (!writer.u32(value)) return false;
  if (!writer.u32(checkpoint.mcguffinCarrySubPoints) || !writer.u16(checkpoint.mcguffinCarriedPoints) ||
      !writer.u32(checkpoint.mcguffinFinalHoldTicks) || !writer.u32(checkpoint.mcguffinRoundLiveTicks) ||
      !writer.u32(checkpoint.mcguffinThrowPickupLockoutTicks) || !writer.u32(checkpoint.spawnRandomState)) return false;
  const auto writeU32Array = [&writer](const auto& values) {
    for (const std::uint32_t value : values) if (!writer.u32(value)) return false;
    return true;
  };
  if (!writeU32Array(checkpoint.projectileSequences) || !writeU32Array(checkpoint.rocketExplosionSequences) ||
      !writeU32Array(checkpoint.fragEventSequences) || !writeU32Array(checkpoint.localHitFeedbackSequences) ||
      !writeU32Array(checkpoint.damageTakenSequences) ||
      !writeU32Array(checkpoint.footstepSequences) || !writeU32Array(checkpoint.grenadeBounceEventSequences) ||
      !writeU32Array(checkpoint.grenadeBounceSequences) || !writeU32Array(checkpoint.spawnLastUsedTicks)) return false;
  for (const ReplayFootstepState& footstep : checkpoint.footstepStates) {
    if (!validVec3(footstep.previousPosition) || !std::isfinite(footstep.distanceSinceStep) ||
        footstep.distanceSinceStep < 0.0F || !writeVec3(writer, footstep.previousPosition) ||
        !writer.f32(footstep.distanceSinceStep) || !writer.boolean(footstep.wasOnGround) ||
        !writer.boolean(footstep.initialized)) return false;
  }
  for (const bool used : checkpoint.spawnWasUsed) if (!writer.boolean(used)) return false;
  if (!writer.u32(checkpoint.nextDeathmatchSpawnIndex) || !writer.boolean(checkpoint.playersColliding) ||
      !writer.u32(static_cast<std::uint32_t>(checkpoint.history.size()))) return false;
  std::uint32_t lastHistoryTick = 0;
  for (const ReplayHistoryFrame& frame : checkpoint.history) {
    if ((&frame != checkpoint.history.data() && frame.serverTick <= lastHistoryTick) ||
        frame.serverTick > checkpoint.serverTick || !writer.u32(frame.serverTick)) return false;
    lastHistoryTick = frame.serverTick;
    for (const PlayerState& player : frame.players) if (!writePlayerState(writer, player)) return false;
  }
  return true;
}

bool readCheckpoint(Reader& reader, ReplayCheckpoint& checkpoint) {
  if (!reader.u32(checkpoint.serverTick) || !reader.u32(checkpoint.mapRevision) ||
      !reader.u32(checkpoint.projectileRevision) || !reader.u64(checkpoint.gameplayConfigHash) || checkpoint.mapRevision == 0U ||
      checkpoint.projectileRevision == 0U) return false;
  for (ReplayCheckpointPlayer& player : checkpoint.players) {
    std::uint8_t team = 0;
    if (!reader.boolean(player.connected) || !reader.boolean(player.participating) ||
        !reader.boolean(player.ready) || !reader.u8(team) || !readPlayerState(reader, player.player) ||
        !readWeaponState(reader, player.weapon) || !reader.u32(player.respawnTicksRemaining) ||
        !readCommand(reader, player.command) || !readActionEdges(reader, player.consumedActionEdges) ||
        !reader.u32(player.viewedServerTick) || !reader.boolean(player.hasCommand)) return false;
    player.team = static_cast<Team>(team);
    if (!isValidTeam(player.team)) return false;
  }
  for (ReplayProjectile& projectile : checkpoint.projectiles) {
    std::uint8_t weapon = 0;
    if (!reader.boolean(projectile.active) || !reader.u8(projectile.owner) || !reader.u32(projectile.sequence) ||
        !reader.u8(weapon) || !readVec3(reader, projectile.position) || !readVec3(reader, projectile.previousPosition) ||
        !readVec3(reader, projectile.velocity) || !reader.f32(projectile.projectileRadius) ||
        !reader.f32(projectile.projectileHitboxRadius) || !reader.boolean(projectile.ownerCollisionArmed) ||
        !reader.boolean(projectile.resting) || !reader.u32(projectile.ageTicks)) return false;
    projectile.weapon = static_cast<Weapon>(weapon);
    if (!validWeapon(projectile.weapon) || projectile.owner >= kDuelPlayerCount ||
        projectile.projectileRadius < 0.0F || projectile.projectileHitboxRadius < 0.0F ||
        (projectile.active && projectile.sequence == 0U)) return false;
  }
  ReplayMatchState& match = checkpoint.match;
  std::uint8_t gameMode = 0;
  std::uint8_t phase = 0;
  std::uint8_t roundTeam = 0;
  std::uint8_t matchTeam = 0;
  if (!reader.u8(gameMode) || !reader.u8(phase) || !reader.u32(match.phaseTicksRemaining) ||
      !reader.u32(match.liveTicksElapsed) || !reader.boolean(match.overtime)) return false;
  match.gameMode = static_cast<GameMode>(gameMode);
  match.phase = static_cast<MatchPhase>(phase);
  for (std::uint16_t& value : match.scores) if (!reader.u16(value)) return false;
  for (std::uint16_t& value : match.teamScores) if (!reader.u16(value)) return false;
  for (std::uint16_t& value : match.mcguffinScores) if (!reader.u16(value)) return false;
  for (std::uint8_t& value : match.mcguffinRoundsWon) if (!reader.u8(value)) return false;
  if (!reader.u8(match.mcguffinRound) || !reader.u8(match.roundWinner) || !reader.u8(match.matchWinner) ||
      !reader.u8(roundTeam) || !reader.u8(matchTeam)) return false;
  match.roundWinningTeam = static_cast<Team>(roundTeam);
  match.matchWinningTeam = static_cast<Team>(matchTeam);
  if (!isValidGameMode(match.gameMode) || !validPhase(match.phase) || !isValidTeam(match.roundWinningTeam) ||
      !isValidTeam(match.matchWinningTeam) || (match.roundWinner != kNoReplayPlayer && match.roundWinner >= kDuelPlayerCount) ||
      (match.matchWinner != kNoReplayPlayer && match.matchWinner >= kDuelPlayerCount)) return false;
  for (RoundCombatStats& stats : match.roundCombatStats) if (!readRoundStats(reader, stats)) return false;
  for (RoundCombatStats& stats : match.matchCombatStats) if (!readRoundStats(reader, stats)) return false;
  for (bool& available : checkpoint.healthPickupAvailable) if (!reader.boolean(available)) return false;
  for (std::uint32_t& ticks : checkpoint.healthPickupCooldownTicks) if (!reader.u32(ticks)) return false;
  for (IcePool& ice : checkpoint.icePools) {
    if (!reader.boolean(ice.active) || !readVec3(reader, ice.center) || !readVec3(reader, ice.normal) ||
        !reader.f32(ice.radius) || !reader.f32(ice.lifetimeSeconds) || ice.radius < 0.0F ||
        ice.lifetimeSeconds < 0.0F) return false;
  }
  McGuffinObjective& objective = checkpoint.mcguffin;
  std::uint8_t state = 0;
  std::uint8_t associated = 0;
  std::uint8_t carrierTeam = 0;
  std::uint8_t redOwner = 0;
  std::uint8_t blueOwner = 0;
  if (!reader.u8(state) || !reader.u8(associated) || !reader.u8(carrierTeam) || !reader.u8(objective.carrierIndex) ||
      !readVec3(reader, objective.position) || !readVec3(reader, objective.velocity) ||
      !readVec3(reader, objective.spawnPosition) || !reader.u32(objective.stateTicks) ||
      !reader.u32(objective.scoreSubPoints) || !reader.u8(redOwner) || !reader.u8(blueOwner)) return false;
  objective.state = static_cast<McGuffinState>(state);
  objective.associatedTeam = static_cast<Team>(associated);
  objective.carrierTeam = static_cast<Team>(carrierTeam);
  checkpoint.mcguffinRedBaseOwner = static_cast<Team>(redOwner);
  checkpoint.mcguffinBlueBaseOwner = static_cast<Team>(blueOwner);
  if (!validMcGuffinState(objective.state) || !isValidMcGuffinObjective(objective) ||
      !isValidTeam(checkpoint.mcguffinRedBaseOwner) || !isValidTeam(checkpoint.mcguffinBlueBaseOwner)) return false;
  for (std::uint32_t& value : checkpoint.mcguffinStealTicks) if (!reader.u32(value)) return false;
  if (!reader.u32(checkpoint.mcguffinCarrySubPoints) || !reader.u16(checkpoint.mcguffinCarriedPoints) ||
      !reader.u32(checkpoint.mcguffinFinalHoldTicks) || !reader.u32(checkpoint.mcguffinRoundLiveTicks) ||
      !reader.u32(checkpoint.mcguffinThrowPickupLockoutTicks) || !reader.u32(checkpoint.spawnRandomState) ||
      checkpoint.spawnRandomState == 0U) return false;
  const auto readU32Array = [&reader](auto& values) {
    for (std::uint32_t& value : values) if (!reader.u32(value)) return false;
    return true;
  };
  if (!readU32Array(checkpoint.projectileSequences) || !readU32Array(checkpoint.rocketExplosionSequences) ||
      !readU32Array(checkpoint.fragEventSequences) || !readU32Array(checkpoint.localHitFeedbackSequences) ||
      !readU32Array(checkpoint.damageTakenSequences) ||
      !readU32Array(checkpoint.footstepSequences) || !readU32Array(checkpoint.grenadeBounceEventSequences) ||
      !readU32Array(checkpoint.grenadeBounceSequences) || !readU32Array(checkpoint.spawnLastUsedTicks)) return false;
  for (ReplayFootstepState& footstep : checkpoint.footstepStates) {
    if (!readVec3(reader, footstep.previousPosition) || !reader.f32(footstep.distanceSinceStep) ||
        !reader.boolean(footstep.wasOnGround) || !reader.boolean(footstep.initialized) ||
        !validVec3(footstep.previousPosition) || !std::isfinite(footstep.distanceSinceStep) ||
        footstep.distanceSinceStep < 0.0F) return false;
  }
  for (bool& used : checkpoint.spawnWasUsed) if (!reader.boolean(used)) return false;
  std::uint32_t historyCount = 0;
  if (!reader.u32(checkpoint.nextDeathmatchSpawnIndex) ||
      checkpoint.nextDeathmatchSpawnIndex >= Arena::kSpawnCount || !reader.boolean(checkpoint.playersColliding) ||
      !reader.u32(historyCount) || historyCount == 0U || historyCount > kMaxReplayHistoryFrames) return false;
  checkpoint.history.clear();
  checkpoint.history.reserve(historyCount);
  std::uint32_t previousTick = 0;
  for (std::uint32_t index = 0; index < historyCount; ++index) {
    ReplayHistoryFrame frame;
    if (!reader.u32(frame.serverTick) || frame.serverTick > checkpoint.serverTick ||
        (index > 0U && frame.serverTick <= previousTick)) return false;
    previousTick = frame.serverTick;
    for (PlayerState& player : frame.players) if (!readPlayerState(reader, player)) return false;
    checkpoint.history.push_back(std::move(frame));
  }
  return true;
}

bool writeHash(Writer& writer, const ReplayStateHash& hash) {
  return writer.u32(hash.tick) && writer.u64(hash.value);
}

bool readHash(Reader& reader, ReplayStateHash& hash) {
  return reader.u32(hash.tick) && reader.u64(hash.value);
}

bool writeLethal(Writer& writer, const ReplayLethalEvent& event) {
  return event.victim < kDuelPlayerCount && (event.killer < kDuelPlayerCount || event.killer == kNoReplayPlayer) &&
    validWeapon(event.weapon) && validLethalKind(event.kind) && writer.u32(event.tick) &&
    writer.u32(event.replayGeneration) && writer.u8(event.victim) && writer.u8(event.killer) &&
    writer.u8(static_cast<std::uint8_t>(event.weapon)) && writer.u32(event.projectileSequence) &&
    writer.u8(static_cast<std::uint8_t>(event.kind));
}

bool readLethal(Reader& reader, ReplayLethalEvent& event) {
  std::uint8_t weapon = 0;
  std::uint8_t kind = 0;
  if (!reader.u32(event.tick) || !reader.u32(event.replayGeneration) || !reader.u8(event.victim) ||
      !reader.u8(event.killer) || !reader.u8(weapon) || !reader.u32(event.projectileSequence) ||
      !reader.u8(kind)) return false;
  event.weapon = static_cast<Weapon>(weapon);
  event.kind = static_cast<LethalKind>(kind);
  return event.victim < kDuelPlayerCount && (event.killer < kDuelPlayerCount || event.killer == kNoReplayPlayer) &&
    validWeapon(event.weapon) && validLethalKind(event.kind);
}

[[nodiscard]] std::uint32_t crc32(const std::vector<std::uint8_t>& payload) {
  std::uint32_t crc = 0xffffffffU;
  for (const std::uint8_t byte : payload) {
    crc ^= byte;
    for (unsigned bit = 0; bit < 8U; ++bit) {
      const std::uint32_t mask = 0U - (crc & 1U);
      crc = (crc >> 1U) ^ (0xedb88320U & mask);
    }
  }
  return ~crc;
}

bool writeChunk(Writer& writer, ReplayChunkType type, const std::vector<std::uint8_t>& payload) {
  if (payload.empty() || payload.size() > kMaxReplayChunkBytes) return false;
  return writer.u8(static_cast<std::uint8_t>(type)) && writer.u32(static_cast<std::uint32_t>(payload.size())) &&
    writer.u32(crc32(payload)) && writer.raw(payload);
}

bool fail(std::string* error, std::string_view message) {
  if (error != nullptr) *error = std::string(message);
  return false;
}

} // namespace

std::uint64_t canonicalStateHash(const ReplayCheckpoint& checkpoint) {
  std::vector<std::uint8_t> bytes;
  bytes.reserve(8192U);
  Writer writer(bytes);
  if (!writeCheckpoint(writer, checkpoint) || !writer.ok()) return 0U;
  std::uint64_t value = 1469598103934665603ULL;
  for (const std::uint8_t byte : bytes) {
    value ^= byte;
    value *= 1099511628211ULL;
  }
  return value;
}

bool validateReplayCheckpoint(const ReplayCheckpoint& checkpoint, std::string* error) {
  std::vector<std::uint8_t> bytes;
  Writer writer(bytes);
  if (!writeCheckpoint(writer, checkpoint) || !writer.ok()) {
    return fail(error, "replay checkpoint fields are invalid");
  }
  if (error != nullptr) error->clear();
  return true;
}

std::size_t encodedReplayCheckpointBytes(const ReplayCheckpoint& checkpoint) {
  std::vector<std::uint8_t> bytes;
  Writer writer(bytes);
  return writeCheckpoint(writer, checkpoint) && writer.ok() ? bytes.size() : 0U;
}

bool encodeDemo(const ReplayDemo& demo, std::vector<std::uint8_t>& bytes, std::string* error) {
  bytes.clear();
  if (demo.ticks.size() > kMaxReplayTicks || demo.checkpoints.size() > kMaxReplayCheckpoints ||
      demo.hashes.size() > kMaxReplayTicks || demo.lethalEvents.size() > kMaxReplayTicks) {
    return fail(error, "replay contains too many records");
  }
  std::vector<std::uint8_t> metadataBytes;
  Writer metadataWriter(metadataBytes);
  if (!writeMetadata(metadataWriter, demo.metadata) || !metadataWriter.ok()) {
    return fail(error, "replay metadata is invalid");
  }
  std::vector<std::uint8_t> encoded;
  Writer writer(encoded);
  for (const std::uint8_t byte : kMagic) if (!writer.u8(byte)) return fail(error, "replay exceeds size limit");
  if (!writer.u16(kReplayFormatVersion) || !writer.u16(kReplayTickRate) ||
      !writer.u32(static_cast<std::uint32_t>(metadataBytes.size())) ||
      !writer.u32(demo.metadata.formatFlags) || !writer.raw(metadataBytes)) {
    return fail(error, "replay exceeds size limit");
  }
  std::uint32_t previousTick = 0;
  bool hasPreviousTick = false;
  for (const ReplayTickInput& input : demo.ticks) {
    if ((hasPreviousTick && input.tick <= previousTick) || input.tick < demo.metadata.initialServerTick) {
      return fail(error, "input ticks are out of order");
    }
    previousTick = input.tick;
    hasPreviousTick = true;
    std::vector<std::uint8_t> payload;
    Writer payloadWriter(payload);
    if (!writeTickInput(payloadWriter, input) || !payloadWriter.ok() || !writeChunk(writer, ReplayChunkType::TickInputs, payload)) {
      return fail(error, "input record is invalid or too large");
    }
  }
  std::uint32_t previousCheckpoint = 0;
  bool hasCheckpoint = false;
  for (const ReplayCheckpoint& checkpoint : demo.checkpoints) {
    if ((hasCheckpoint && checkpoint.serverTick <= previousCheckpoint) ||
        checkpoint.serverTick < demo.metadata.initialServerTick) return fail(error, "checkpoints are out of order");
    previousCheckpoint = checkpoint.serverTick;
    hasCheckpoint = true;
    std::vector<std::uint8_t> payload;
    Writer payloadWriter(payload);
    if (!writeCheckpoint(payloadWriter, checkpoint) || !payloadWriter.ok() || !writeChunk(writer, ReplayChunkType::Checkpoint, payload)) {
      return fail(error, "checkpoint is invalid or too large");
    }
  }
  std::uint32_t previousHash = 0;
  bool hasHash = false;
  for (const ReplayStateHash& hash : demo.hashes) {
    if ((hasHash && hash.tick <= previousHash) || hash.tick < demo.metadata.initialServerTick) return fail(error, "hashes are out of order");
    previousHash = hash.tick;
    hasHash = true;
    std::vector<std::uint8_t> payload;
    Writer payloadWriter(payload);
    if (!writeHash(payloadWriter, hash) || !writeChunk(writer, ReplayChunkType::StateHash, payload)) return fail(error, "hash record is invalid");
  }
  std::uint32_t previousLethal = 0;
  bool hasLethal = false;
  for (const ReplayLethalEvent& event : demo.lethalEvents) {
    if ((hasLethal && event.tick < previousLethal) || event.tick < demo.metadata.initialServerTick) return fail(error, "lethal events are out of order");
    previousLethal = event.tick;
    hasLethal = true;
    std::vector<std::uint8_t> payload;
    Writer payloadWriter(payload);
    if (!writeLethal(payloadWriter, event) || !writeChunk(writer, ReplayChunkType::LethalEvent, payload)) return fail(error, "lethal record is invalid");
  }
  if (!writer.ok() || encoded.size() < kFilePreambleBytes || encoded.size() > kMaxReplayBytes) {
    return fail(error, "replay exceeds size limit");
  }
  bytes = std::move(encoded);
  if (error != nullptr) error->clear();
  return true;
}

bool decodeDemo(const std::vector<std::uint8_t>& bytes, ReplayDemo& demo, std::string* error) {
  if (bytes.size() < kFilePreambleBytes || bytes.size() > kMaxReplayBytes) return fail(error, "replay size is invalid");
  Reader reader(bytes);
  for (const std::uint8_t expected : kMagic) {
    std::uint8_t actual = 0;
    if (!reader.u8(actual) || actual != expected) return fail(error, "replay magic is invalid");
  }
  std::uint16_t version = 0;
  std::uint16_t tickRate = 0;
  std::uint32_t metadataSize = 0;
  std::uint32_t preambleFlags = 0;
  if (!reader.u16(version) || !reader.u16(tickRate) || !reader.u32(metadataSize) || !reader.u32(preambleFlags)) {
    return fail(error, "replay preamble is truncated");
  }
  if (version != kReplayFormatVersion) return fail(error, "replay version is incompatible");
  if (tickRate != kReplayTickRate) return fail(error, "replay tick rate is incompatible");
  if (metadataSize == 0U || metadataSize > kMaxReplayChunkBytes || metadataSize > reader.remaining()) {
    return fail(error, "replay metadata length is invalid");
  }
  ReplayDemo decoded;
  std::vector<std::uint8_t> metadataBytes;
  if (!reader.take(metadataSize, metadataBytes)) return fail(error, "replay metadata is truncated");
  Reader metadataReader(metadataBytes);
  if (!readMetadata(metadataReader, decoded.metadata) || !metadataReader.done()) {
    return fail(error, "replay metadata is invalid");
  }
  if (decoded.metadata.formatFlags != preambleFlags) return fail(error, "replay flags disagree");
  std::uint32_t previousTick = 0;
  std::uint32_t previousCheckpoint = 0;
  std::uint32_t previousHash = 0;
  std::uint32_t previousLethal = 0;
  bool hasTick = false;
  bool hasCheckpoint = false;
  bool hasHash = false;
  bool hasLethal = false;
  while (reader.remaining() > 0U) {
    std::uint8_t typeValue = 0;
    std::uint32_t payloadSize = 0;
    std::uint32_t checksum = 0;
    if (!reader.u8(typeValue) || !reader.u32(payloadSize) || !reader.u32(checksum) ||
        payloadSize == 0U || payloadSize > kMaxReplayChunkBytes || payloadSize > reader.remaining()) {
      return fail(error, "replay chunk length is invalid");
    }
    std::vector<std::uint8_t> payload;
    if (!reader.take(payloadSize, payload)) return fail(error, "replay chunk is truncated");
    if (crc32(payload) != checksum) return fail(error, "replay chunk checksum is invalid");
    Reader payloadReader(payload);
    const ReplayChunkType type = static_cast<ReplayChunkType>(typeValue);
    switch (type) {
    case ReplayChunkType::TickInputs: {
      if (decoded.ticks.size() >= kMaxReplayTicks) return fail(error, "replay has too many input ticks");
      ReplayTickInput input;
      if (!readTickInput(payloadReader, input) || !payloadReader.done() ||
          input.tick < decoded.metadata.initialServerTick || (hasTick && input.tick <= previousTick)) {
        return fail(error, "replay input tick is invalid or out of order");
      }
      previousTick = input.tick;
      hasTick = true;
      decoded.ticks.push_back(std::move(input));
      break;
    }
    case ReplayChunkType::Checkpoint: {
      if (decoded.checkpoints.size() >= kMaxReplayCheckpoints) return fail(error, "replay has too many checkpoints");
      ReplayCheckpoint checkpoint;
      if (!readCheckpoint(payloadReader, checkpoint) || !payloadReader.done() ||
          checkpoint.serverTick < decoded.metadata.initialServerTick ||
          (hasCheckpoint && checkpoint.serverTick <= previousCheckpoint)) {
        return fail(error, "replay checkpoint is invalid or out of order");
      }
      previousCheckpoint = checkpoint.serverTick;
      hasCheckpoint = true;
      decoded.checkpoints.push_back(std::move(checkpoint));
      break;
    }
    case ReplayChunkType::StateHash: {
      if (decoded.hashes.size() >= kMaxReplayTicks) return fail(error, "replay has too many hashes");
      ReplayStateHash hash;
      if (!readHash(payloadReader, hash) || !payloadReader.done() ||
          hash.tick < decoded.metadata.initialServerTick || (hasHash && hash.tick <= previousHash)) {
        return fail(error, "replay hash is invalid or out of order");
      }
      previousHash = hash.tick;
      hasHash = true;
      decoded.hashes.push_back(hash);
      break;
    }
    case ReplayChunkType::LethalEvent: {
      if (decoded.lethalEvents.size() >= kMaxReplayTicks) return fail(error, "replay has too many lethal events");
      ReplayLethalEvent event;
      if (!readLethal(payloadReader, event) || !payloadReader.done() ||
          event.tick < decoded.metadata.initialServerTick || (hasLethal && event.tick < previousLethal)) {
        return fail(error, "replay lethal event is invalid or out of order");
      }
      previousLethal = event.tick;
      hasLethal = true;
      decoded.lethalEvents.push_back(event);
      break;
    }
    default:
      return fail(error, "replay chunk type is unknown");
    }
  }
  if (!reader.done()) return fail(error, "replay has trailing data");
  demo = std::move(decoded);
  if (error != nullptr) error->clear();
  return true;
}

} // namespace lg::replay
