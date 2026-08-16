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
  bool i16(std::int16_t value) { return u16(std::bit_cast<std::uint16_t>(value)); }
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
  bool i16(std::int16_t& value) {
    std::uint16_t encoded = 0;
    if (!u16(encoded)) return false;
    value = std::bit_cast<std::int16_t>(encoded);
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

[[nodiscard]] bool validStopReason(ReplayStopReason value) {
  return value <= ReplayStopReason::InvalidState;
}

[[nodiscard]] bool validWeaponSwitchingMode(WeaponSwitchingMode value) {
  return value <= WeaponSwitchingMode::Crazy;
}

[[nodiscard]] bool validFloatRange(float value, float minimum, float maximum) {
  return std::isfinite(value) && value >= minimum && value <= maximum;
}

[[nodiscard]] bool validMatchRules(const MatchRules& rules) {
  return rules.roundLimit > 0U && rules.playerLimit > 0U &&
    rules.playerLimit <= kDuelPlayerCount;
}

[[nodiscard]] bool sameMatchRules(const MatchRules& left, const MatchRules& right) {
  return left.roundLimit == right.roundLimit &&
    left.timeLimitMinutes == right.timeLimitMinutes &&
    left.playerLimit == right.playerLimit &&
    left.countdownTicks == right.countdownTicks &&
    left.roundEndTicks == right.roundEndTicks &&
    left.matchEndTicks == right.matchEndTicks &&
    left.deathRespawnTicks == right.deathRespawnTicks &&
    left.showOpponentHealth == right.showOpponentHealth;
}

[[nodiscard]] bool validMovementTuning(const MovementTuning& tuning) {
  return validFloatRange(tuning.groundAcceleration, 0.0F, 100000.0F) &&
    validFloatRange(tuning.airAcceleration, 0.0F, 100000.0F) &&
    validFloatRange(tuning.groundFriction, 0.0F, 100000.0F) &&
    validFloatRange(tuning.stopSpeed, 0.0F, 100000.0F) &&
    validFloatRange(tuning.gravity, 0.0F, 100000.0F) &&
    validFloatRange(tuning.maxGroundSpeed, 0.0F, 100000.0F) &&
    validFloatRange(tuning.maxAirSpeed, 0.0F, 100000.0F) &&
    validFloatRange(tuning.jumpImpulse, 0.0F, 100000.0F) &&
    validFloatRange(tuning.dashTargetSpeed, 0.0F, 100000.0F) &&
    validFloatRange(tuning.dashMaxSpeed, 0.0F, 100000.0F) &&
    validFloatRange(tuning.dashAcceleration, 0.0F, 100000.0F) &&
    validFloatRange(tuning.dashDuration, 0.0F, 1000.0F) &&
    validFloatRange(tuning.dashCooldown, 0.0F, 1000.0F) &&
    validFloatRange(tuning.dashGroundHopVelocity, 0.0F, 100000.0F) &&
    validFloatRange(tuning.dashAirHopVelocity, 0.0F, 100000.0F) &&
    validFloatRange(tuning.flightAcceleration, 0.0F, 100000.0F) &&
    validFloatRange(tuning.maxFlightSpeed, 0.0F, 100000.0F) &&
    validFloatRange(tuning.flightDamping, 0.0F, 100000.0F) &&
    validFloatRange(tuning.flightGravityCancel, 0.0F, 100000.0F);
}

[[nodiscard]] bool validHitscanTuning(const HitscanTuning& tuning) {
  return validFloatRange(tuning.range, 0.0F, 100000.0F) &&
    tuning.damage >= 0 && tuning.damage <= 100000 &&
    validFloatRange(tuning.eyeHeight, 0.0F, 100.0F) &&
    validFloatRange(tuning.knockback, 0.0F, 100000.0F) &&
    validFloatRange(tuning.headshotMultiplier, 0.0F, 100.0F);
}

[[nodiscard]] bool validMachineGunTuning(const MachineGunTuning& tuning) {
  return validFloatRange(tuning.range, 0.0F, 100000.0F) &&
    tuning.damage >= 0 && tuning.damage <= 100000 &&
    validFloatRange(tuning.eyeHeight, 0.0F, 100.0F) &&
    validFloatRange(tuning.knockback, 0.0F, 100000.0F) &&
    validFloatRange(tuning.spreadRadians, 0.0F, 100000.0F) &&
    validFloatRange(tuning.headshotMultiplier, 0.0F, 100.0F);
}

[[nodiscard]] bool validShotgunTuning(const ShotgunTuning& tuning) {
  return validFloatRange(tuning.range, 0.0F, 100000.0F) &&
    tuning.pelletCount > 0U && tuning.damagePerPellet >= 0 && tuning.damagePerPellet <= 100000 &&
    validFloatRange(tuning.spreadRadians, 0.0F, 100000.0F) &&
    validFloatRange(tuning.eyeHeight, 0.0F, 100.0F) &&
    validFloatRange(tuning.knockback, 0.0F, 100000.0F) &&
    validFloatRange(tuning.headshotMultiplier, 0.0F, 100.0F);
}

[[nodiscard]] bool validWeaponDamage(const WeaponDamageTuning& damage) {
  const auto valid = [](int value) { return value >= 0 && value <= 100000; };
  return valid(damage.shotgunDamagePerPellet) && valid(damage.machineGunDamage) &&
    valid(damage.lightningGunDamage) && valid(damage.railgunDamage) &&
    valid(damage.rocketLauncherDamage) && valid(damage.plasmaGunDamage) &&
    valid(damage.freezeGunDamage);
}

[[nodiscard]] bool validBalanceConfig(const BalanceConfig& config) {
  if (!validFloatRange(config.lightningGun.range, 0.0F, 100000.0F) ||
      !validFloatRange(config.lightningGun.damagePerSecond, 0.0F, 100000.0F) ||
      !validFloatRange(config.lightningGun.fireHz, 0.0F, 1000.0F) ||
      !validFloatRange(config.lightningGun.eyeHeight, 0.0F, 100.0F) ||
      !validFloatRange(config.lightningGun.knockbackPerSecond, 0.0F, 100000.0F) ||
      !validFloatRange(config.lightningGun.headshotMultiplier, 0.0F, 100.0F) ||
      !validFloatRange(config.freezeGun.range, 0.0F, 100000.0F) ||
      !validFloatRange(config.freezeGun.fireHz, 0.0F, 1000.0F) ||
      !validFloatRange(config.freezeGun.eyeHeight, 0.0F, 100.0F) ||
      !validFloatRange(config.freezeGun.damagePerSecond, 0.0F, 100000.0F) ||
      !validFloatRange(config.freezeGun.freezePerSecond, 0.0F, 100000.0F) ||
      !validFloatRange(config.freezeGun.decayPerSecond, 0.0F, 100000.0F) ||
      !validFloatRange(config.freezeGun.maxLevel, 0.0F, 100000.0F) ||
      !validFloatRange(config.freezeGun.maxSlowFraction, 0.0F, 1.0F) ||
      !validFloatRange(config.freezeGun.headshotMultiplier, 0.0F, 100.0F) ||
      !validFloatRange(config.icePool.maxRadius, 0.0F, 100000.0F) ||
      !validFloatRange(config.icePool.growthPerSecond, 0.0F, 100000.0F) ||
      !validFloatRange(config.icePool.lifetimeSeconds, 0.0F, 10000.0F) ||
      !validFloatRange(config.icePool.friction, 0.0F, 100000.0F) ||
      !validFloatRange(config.icePool.slopeGravityScale, 0.0F, 100000.0F) ||
      !validFloatRange(config.icePool.controlScale, 0.0F, 1.0F) ||
      !validFloatRange(config.icePool.mergeDistance, 0.0F, 100000.0F) ||
      !validHitscanTuning(config.railgun) || !validHitscanTuning(config.revolver) ||
      !validMachineGunTuning(config.machineGun) || !validShotgunTuning(config.shotgun) ||
      !validFloatRange(config.sniperChargeSeconds, 0.0F, 1000.0F) ||
      !validFloatRange(config.sniperMaxDamageMultiplier, 0.0F, 100.0F) ||
      !validFloatRange(config.rocketLauncher.speed, 0.0F, 100000.0F) ||
      !validFloatRange(config.rocketLauncher.radius, 0.0F, 100000.0F) ||
      !validFloatRange(config.rocketLauncher.directHitboxHalfExtentXY, 0.0F, 100000.0F) ||
      !validFloatRange(config.rocketLauncher.directHitboxHalfExtentZ, 0.0F, 100000.0F) ||
      !validFloatRange(config.rocketLauncher.knockback, 0.0F, 100000.0F) ||
      !validFloatRange(config.rocketLauncher.eyeHeight, 0.0F, 100.0F) ||
      !validFloatRange(config.grenadeLauncher.speed, 0.0F, 100000.0F) ||
      !validFloatRange(config.grenadeLauncher.verticalBoost, -100000.0F, 100000.0F) ||
      !validFloatRange(config.grenadeLauncher.gravity, 0.0F, 100000.0F) ||
      !validFloatRange(config.grenadeLauncher.bounceDamping, 0.0F, 100.0F) ||
      !validFloatRange(config.grenadeLauncher.restSpeed, 0.0F, 100000.0F) ||
      !validFloatRange(config.grenadeLauncher.bounceSoundMinSpeed, 0.0F, 100000.0F) ||
      !validFloatRange(config.grenadeLauncher.projectileRadius, 0.0F, 100000.0F) ||
      !validFloatRange(config.grenadeLauncher.projectileHitboxRadius, 0.0F, 100000.0F) ||
      !validFloatRange(config.grenadeLauncher.radius, 0.0F, 100000.0F) ||
      !validFloatRange(config.grenadeLauncher.knockback, 0.0F, 100000.0F) ||
      !validFloatRange(config.grenadeLauncher.eyeHeight, 0.0F, 100.0F) ||
      !validFloatRange(config.plasmaGun.speed, 0.0F, 100000.0F) ||
      !validFloatRange(config.plasmaGun.radius, 0.0F, 100000.0F) ||
      !validFloatRange(config.plasmaGun.directHitboxHalfExtentXY, 0.0F, 100000.0F) ||
      !validFloatRange(config.plasmaGun.directHitboxHalfExtentZ, 0.0F, 100000.0F) ||
      !validFloatRange(config.plasmaGun.knockback, 0.0F, 100000.0F) ||
      !validFloatRange(config.plasmaGun.eyeHeight, 0.0F, 100.0F)) {
    return false;
  }
  const auto validDamage = [](int value) { return value >= 0 && value <= 100000; };
  if (!validDamage(config.revolver.damage) || !validDamage(config.rocketLauncher.directDamage) ||
      !validDamage(config.rocketLauncher.splashDamage) || !validDamage(config.grenadeLauncher.directDamage) ||
      !validDamage(config.grenadeLauncher.splashDamage) || !validDamage(config.plasmaGun.damage) ||
      config.railgunCooldownTicks == 0U ||
      config.revolverCooldownTicks == 0U || config.machineGunCooldownTicks == 0U ||
      config.shotgunCooldownTicks == 0U || config.rocketLauncherCooldownTicks == 0U ||
      config.grenadeLauncher.cooldownTicks == 0U || config.plasmaGun.cooldownTicks == 0U ||
      config.rocketLauncher.maxLifetimeTicks == 0U || config.plasmaGun.maxLifetimeTicks == 0U ||
      config.weaponPulloutTicks > 100000U || config.jumpPadRetriggerCooldownTicks > 100000U ||
      config.smallHealthPickupAmount < 0 || config.largeHealthPickupAmount < 0 ||
      config.weaponAmmo.spawnAmmo[0] < 0) {
    return false;
  }
  for (const std::int32_t ammo : config.weaponAmmo.spawnAmmo) {
    if (ammo < 0 || ammo > 100000) return false;
  }
  return true;
}

bool writeMatchRules(Writer& writer, const MatchRules& rules) {
  return validMatchRules(rules) && writer.u16(rules.roundLimit) &&
    writer.u16(rules.timeLimitMinutes) && writer.u8(rules.playerLimit) &&
    writer.u16(rules.countdownTicks) && writer.u16(rules.roundEndTicks) &&
    writer.u16(rules.matchEndTicks) && writer.u16(rules.deathRespawnTicks) &&
    writer.boolean(rules.showOpponentHealth);
}

bool readMatchRules(Reader& reader, MatchRules& rules) {
  return reader.u16(rules.roundLimit) && reader.u16(rules.timeLimitMinutes) &&
    reader.u8(rules.playerLimit) && reader.u16(rules.countdownTicks) &&
    reader.u16(rules.roundEndTicks) && reader.u16(rules.matchEndTicks) &&
    reader.u16(rules.deathRespawnTicks) && reader.boolean(rules.showOpponentHealth) &&
    validMatchRules(rules);
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

bool writeMovementTuning(Writer& writer, const MovementTuning& tuning) {
  return validMovementTuning(tuning) && writer.boolean(tuning.flightEnabled) &&
    writer.f32(tuning.groundAcceleration) && writer.f32(tuning.airAcceleration) &&
    writer.f32(tuning.groundFriction) && writer.f32(tuning.stopSpeed) &&
    writer.f32(tuning.gravity) && writer.f32(tuning.maxGroundSpeed) &&
    writer.f32(tuning.maxAirSpeed) && writer.f32(tuning.jumpImpulse) &&
    writer.boolean(tuning.airControlEnabled) && writer.f32(tuning.dashTargetSpeed) &&
    writer.f32(tuning.dashMaxSpeed) && writer.f32(tuning.dashAcceleration) &&
    writer.f32(tuning.dashDuration) && writer.f32(tuning.dashCooldown) &&
    writer.f32(tuning.dashGroundHopVelocity) && writer.f32(tuning.dashAirHopVelocity) &&
    writer.f32(tuning.flightAcceleration) && writer.f32(tuning.maxFlightSpeed) &&
    writer.f32(tuning.flightDamping) && writer.f32(tuning.flightGravityCancel);
}

bool readMovementTuning(Reader& reader, MovementTuning& tuning) {
  return reader.boolean(tuning.flightEnabled) && reader.f32(tuning.groundAcceleration) &&
    reader.f32(tuning.airAcceleration) && reader.f32(tuning.groundFriction) &&
    reader.f32(tuning.stopSpeed) && reader.f32(tuning.gravity) &&
    reader.f32(tuning.maxGroundSpeed) && reader.f32(tuning.maxAirSpeed) &&
    reader.f32(tuning.jumpImpulse) && reader.boolean(tuning.airControlEnabled) &&
    reader.f32(tuning.dashTargetSpeed) && reader.f32(tuning.dashMaxSpeed) &&
    reader.f32(tuning.dashAcceleration) && reader.f32(tuning.dashDuration) &&
    reader.f32(tuning.dashCooldown) && reader.f32(tuning.dashGroundHopVelocity) &&
    reader.f32(tuning.dashAirHopVelocity) && reader.f32(tuning.flightAcceleration) &&
    reader.f32(tuning.maxFlightSpeed) && reader.f32(tuning.flightDamping) &&
    reader.f32(tuning.flightGravityCancel) && validMovementTuning(tuning);
}

bool writeHitscanTuning(Writer& writer, const HitscanTuning& tuning) {
  return writer.f32(tuning.range) && writer.i32(tuning.damage) &&
    writer.f32(tuning.eyeHeight) && writer.f32(tuning.knockback) &&
    writer.f32(tuning.headshotMultiplier);
}

bool readHitscanTuning(Reader& reader, HitscanTuning& tuning) {
  return reader.f32(tuning.range) && reader.i32(tuning.damage) &&
    reader.f32(tuning.eyeHeight) && reader.f32(tuning.knockback) &&
    reader.f32(tuning.headshotMultiplier);
}

bool writeGameplayConfig(Writer& writer, const ReplayGameplayConfig& config) {
  const BalanceConfig& balance = config.balance;
  if (!validBalanceConfig(balance) || !validMovementTuning(config.movementTuning) ||
      !validWeaponDamage(config.weaponDamage) ||
      !validFloatRange(config.playerSizeScaleXY, 0.01F, 100.0F) ||
      !validFloatRange(config.playerSizeScaleZ, 0.01F, 100.0F) ||
      !validFloatRange(config.lightningKnockback, 0.0F, 100000.0F) ||
      !validFloatRange(config.lightningFireHz, 0.0F, 1000.0F) ||
      !validFloatRange(config.rocketKnockback, 0.0F, 100000.0F) ||
      config.knockbackTimeMs < 0 || config.knockbackTimeMs > 10000 ||
      config.vampirism < 0.0F || config.vampirism > 100.0F ||
      !std::isfinite(config.vampirism) || config.healthAmount <= 0 ||
      config.healthAmount > 100000 || config.selfDamagePercent > 100U ||
      !validWeaponSwitchingMode(config.weaponSwitchingMode) ||
      !isValidMcGuffinConfig(config.mcguffinConfig) ||
      !validMatchRules(config.matchRules)) {
    return false;
  }
  const auto writeLightning = [&writer](const LightningGunTuning& tuning) {
    return writer.f32(tuning.range) && writer.f32(tuning.damagePerSecond) &&
      writer.f32(tuning.fireHz) && writer.f32(tuning.eyeHeight) &&
      writer.f32(tuning.knockbackPerSecond) && writer.f32(tuning.headshotMultiplier);
  };
  const auto writeFreeze = [&writer](const FreezeGunTuning& tuning) {
    return writer.f32(tuning.range) && writer.f32(tuning.fireHz) && writer.f32(tuning.eyeHeight) &&
      writer.f32(tuning.damagePerSecond) && writer.f32(tuning.freezePerSecond) &&
      writer.f32(tuning.decayPerSecond) && writer.f32(tuning.maxLevel) &&
      writer.f32(tuning.maxSlowFraction) && writer.f32(tuning.headshotMultiplier);
  };
  const auto writeIce = [&writer](const IcePoolTuning& tuning) {
    return writer.f32(tuning.maxRadius) && writer.f32(tuning.growthPerSecond) &&
      writer.f32(tuning.lifetimeSeconds) && writer.f32(tuning.friction) &&
      writer.f32(tuning.slopeGravityScale) && writer.f32(tuning.controlScale) &&
      writer.f32(tuning.mergeDistance);
  };
  const auto writeMachine = [&writer](const MachineGunTuning& tuning) {
    return writer.f32(tuning.range) && writer.i32(tuning.damage) && writer.f32(tuning.eyeHeight) &&
      writer.f32(tuning.knockback) && writer.f32(tuning.spreadRadians) &&
      writer.f32(tuning.headshotMultiplier);
  };
  const auto writeShotgun = [&writer](const ShotgunTuning& tuning) {
    return writer.f32(tuning.range) && writer.u8(tuning.pelletCount) &&
      writer.i32(tuning.damagePerPellet) && writer.f32(tuning.spreadRadians) &&
      writer.f32(tuning.eyeHeight) && writer.f32(tuning.knockback) &&
      writer.f32(tuning.headshotMultiplier);
  };
  const auto writeRocket = [&writer](const RocketLauncherTuning& tuning) {
    return writer.f32(tuning.speed) && writer.f32(tuning.radius) &&
      writer.f32(tuning.directHitboxHalfExtentXY) && writer.f32(tuning.directHitboxHalfExtentZ) &&
      writer.i32(tuning.directDamage) && writer.i32(tuning.splashDamage) &&
      writer.f32(tuning.knockback) && writer.f32(tuning.eyeHeight) &&
      writer.u32(tuning.maxLifetimeTicks);
  };
  const auto writeGrenade = [&writer](const GrenadeLauncherTuning& tuning) {
    return writer.f32(tuning.speed) && writer.f32(tuning.verticalBoost) &&
      writer.f32(tuning.gravity) && writer.f32(tuning.bounceDamping) &&
      writer.f32(tuning.restSpeed) && writer.f32(tuning.bounceSoundMinSpeed) &&
      writer.f32(tuning.projectileRadius) && writer.f32(tuning.projectileHitboxRadius) &&
      writer.f32(tuning.radius) && writer.i32(tuning.directDamage) &&
      writer.i32(tuning.splashDamage) && writer.f32(tuning.knockback) &&
      writer.f32(tuning.eyeHeight) && writer.u32(tuning.fuseTicks) &&
      writer.u32(tuning.cooldownTicks);
  };
  const auto writePlasma = [&writer](const PlasmaGunTuning& tuning) {
    return writer.f32(tuning.speed) && writer.f32(tuning.radius) &&
      writer.f32(tuning.directHitboxHalfExtentXY) && writer.f32(tuning.directHitboxHalfExtentZ) &&
      writer.i32(tuning.damage) && writer.f32(tuning.knockback) &&
      writer.f32(tuning.eyeHeight) && writer.u32(tuning.maxLifetimeTicks) &&
      writer.u32(tuning.cooldownTicks);
  };
  const auto writeWeaponAmmo = [&writer](const WeaponAmmoConfig& ammo) {
    if (!writer.boolean(ammo.infiniteAmmo)) return false;
    for (const std::int32_t value : ammo.spawnAmmo) if (!writer.i32(value)) return false;
    return true;
  };
  const auto writeMcGuffin = [&writer](const McGuffinConfig& value) {
    return writer.u16(value.scoreLimit) && writer.u16(value.pointsPerSecond) &&
      writer.u16(value.carryPointsPerSecond) && writer.u16(value.carryPointLimit) &&
      writer.u32(value.initialSpawnTicks) && writer.u32(value.installationDelayTicks) &&
      writer.u32(value.stealTicks) && writer.u32(value.returnTicks) &&
      writer.f32(value.throwSpeed) && writer.f32(value.throwUpSpeed) &&
      writer.f32(value.throwVelocityInheritance) && writer.f32(value.throwGravity) &&
      writer.f32(value.throwBounceDamping) && writer.u32(value.throwPickupLockoutTicks) &&
      writer.u32(value.finalHoldTicks) && writer.f32(value.pickupRadius);
  };
  return writeLightning(balance.lightningGun) && writeFreeze(balance.freezeGun) &&
    writeIce(balance.icePool) && writeHitscanTuning(writer, balance.railgun) &&
    writer.f32(balance.sniperChargeSeconds) && writer.f32(balance.sniperMaxDamageMultiplier) &&
    writer.u32(balance.railgunCooldownTicks) && writeHitscanTuning(writer, balance.revolver) &&
    writer.u32(balance.revolverCooldownTicks) && writeMachine(balance.machineGun) &&
    writer.u32(balance.machineGunCooldownTicks) && writeShotgun(balance.shotgun) &&
    writer.u32(balance.shotgunCooldownTicks) && writeRocket(balance.rocketLauncher) &&
    writer.u32(balance.rocketLauncherCooldownTicks) && writeGrenade(balance.grenadeLauncher) &&
    writePlasma(balance.plasmaGun) && writeWeaponAmmo(balance.weaponAmmo) &&
    writer.u32(balance.weaponPulloutTicks) && writer.u32(balance.jumpPadRetriggerCooldownTicks) &&
    writer.i32(balance.smallHealthPickupAmount) && writer.i32(balance.largeHealthPickupAmount) &&
    writer.u32(balance.smallHealthPickupCooldownTicks) && writer.u32(balance.largeHealthPickupCooldownTicks) &&
    writeMovementTuning(writer, config.movementTuning) &&
    writer.f32(config.playerSizeScaleXY) && writer.f32(config.playerSizeScaleZ) &&
    writer.f32(config.lightningKnockback) && writer.f32(config.lightningFireHz) &&
    writer.f32(config.rocketKnockback) && writer.i32(config.knockbackTimeMs) &&
    writer.i32(config.weaponDamage.shotgunDamagePerPellet) &&
    writer.i32(config.weaponDamage.machineGunDamage) &&
    writer.i32(config.weaponDamage.lightningGunDamage) &&
    writer.i32(config.weaponDamage.railgunDamage) &&
    writer.i32(config.weaponDamage.rocketLauncherDamage) &&
    writer.i32(config.weaponDamage.plasmaGunDamage) &&
    writer.i32(config.weaponDamage.freezeGunDamage) && writer.f32(config.vampirism) &&
    writer.u8(config.selfDamagePercent) && writer.i32(config.healthAmount) &&
    writer.u8(static_cast<std::uint8_t>(config.weaponSwitchingMode)) &&
    writeMcGuffin(config.mcguffinConfig) && writeMatchRules(writer, config.matchRules);
}

bool readGameplayConfig(Reader& reader, ReplayGameplayConfig& config) {
  BalanceConfig& balance = config.balance;
  const auto readLightning = [&reader](LightningGunTuning& tuning) {
    return reader.f32(tuning.range) && reader.f32(tuning.damagePerSecond) &&
      reader.f32(tuning.fireHz) && reader.f32(tuning.eyeHeight) &&
      reader.f32(tuning.knockbackPerSecond) && reader.f32(tuning.headshotMultiplier);
  };
  const auto readFreeze = [&reader](FreezeGunTuning& tuning) {
    return reader.f32(tuning.range) && reader.f32(tuning.fireHz) && reader.f32(tuning.eyeHeight) &&
      reader.f32(tuning.damagePerSecond) && reader.f32(tuning.freezePerSecond) &&
      reader.f32(tuning.decayPerSecond) && reader.f32(tuning.maxLevel) &&
      reader.f32(tuning.maxSlowFraction) && reader.f32(tuning.headshotMultiplier);
  };
  const auto readIce = [&reader](IcePoolTuning& tuning) {
    return reader.f32(tuning.maxRadius) && reader.f32(tuning.growthPerSecond) &&
      reader.f32(tuning.lifetimeSeconds) && reader.f32(tuning.friction) &&
      reader.f32(tuning.slopeGravityScale) && reader.f32(tuning.controlScale) &&
      reader.f32(tuning.mergeDistance);
  };
  const auto readMachine = [&reader](MachineGunTuning& tuning) {
    return reader.f32(tuning.range) && reader.i32(tuning.damage) && reader.f32(tuning.eyeHeight) &&
      reader.f32(tuning.knockback) && reader.f32(tuning.spreadRadians) &&
      reader.f32(tuning.headshotMultiplier);
  };
  const auto readShotgun = [&reader](ShotgunTuning& tuning) {
    return reader.f32(tuning.range) && reader.u8(tuning.pelletCount) &&
      reader.i32(tuning.damagePerPellet) && reader.f32(tuning.spreadRadians) &&
      reader.f32(tuning.eyeHeight) && reader.f32(tuning.knockback) &&
      reader.f32(tuning.headshotMultiplier);
  };
  const auto readRocket = [&reader](RocketLauncherTuning& tuning) {
    return reader.f32(tuning.speed) && reader.f32(tuning.radius) &&
      reader.f32(tuning.directHitboxHalfExtentXY) && reader.f32(tuning.directHitboxHalfExtentZ) &&
      reader.i32(tuning.directDamage) && reader.i32(tuning.splashDamage) &&
      reader.f32(tuning.knockback) && reader.f32(tuning.eyeHeight) &&
      reader.u32(tuning.maxLifetimeTicks);
  };
  const auto readGrenade = [&reader](GrenadeLauncherTuning& tuning) {
    return reader.f32(tuning.speed) && reader.f32(tuning.verticalBoost) &&
      reader.f32(tuning.gravity) && reader.f32(tuning.bounceDamping) &&
      reader.f32(tuning.restSpeed) && reader.f32(tuning.bounceSoundMinSpeed) &&
      reader.f32(tuning.projectileRadius) && reader.f32(tuning.projectileHitboxRadius) &&
      reader.f32(tuning.radius) && reader.i32(tuning.directDamage) &&
      reader.i32(tuning.splashDamage) && reader.f32(tuning.knockback) &&
      reader.f32(tuning.eyeHeight) && reader.u32(tuning.fuseTicks) &&
      reader.u32(tuning.cooldownTicks);
  };
  const auto readPlasma = [&reader](PlasmaGunTuning& tuning) {
    return reader.f32(tuning.speed) && reader.f32(tuning.radius) &&
      reader.f32(tuning.directHitboxHalfExtentXY) && reader.f32(tuning.directHitboxHalfExtentZ) &&
      reader.i32(tuning.damage) && reader.f32(tuning.knockback) &&
      reader.f32(tuning.eyeHeight) && reader.u32(tuning.maxLifetimeTicks) &&
      reader.u32(tuning.cooldownTicks);
  };
  const auto readWeaponAmmo = [&reader](WeaponAmmoConfig& ammo) {
    if (!reader.boolean(ammo.infiniteAmmo)) return false;
    for (std::int32_t& value : ammo.spawnAmmo) if (!reader.i32(value)) return false;
    return true;
  };
  const auto readMcGuffin = [&reader](McGuffinConfig& value) {
    return reader.u16(value.scoreLimit) && reader.u16(value.pointsPerSecond) &&
      reader.u16(value.carryPointsPerSecond) && reader.u16(value.carryPointLimit) &&
      reader.u32(value.initialSpawnTicks) && reader.u32(value.installationDelayTicks) &&
      reader.u32(value.stealTicks) && reader.u32(value.returnTicks) &&
      reader.f32(value.throwSpeed) && reader.f32(value.throwUpSpeed) &&
      reader.f32(value.throwVelocityInheritance) && reader.f32(value.throwGravity) &&
      reader.f32(value.throwBounceDamping) && reader.u32(value.throwPickupLockoutTicks) &&
      reader.u32(value.finalHoldTicks) && reader.f32(value.pickupRadius);
  };
  std::uint8_t switching = 0;
  if (!readLightning(balance.lightningGun) || !readFreeze(balance.freezeGun) ||
      !readIce(balance.icePool) || !readHitscanTuning(reader, balance.railgun) ||
      !reader.f32(balance.sniperChargeSeconds) || !reader.f32(balance.sniperMaxDamageMultiplier) ||
      !reader.u32(balance.railgunCooldownTicks) || !readHitscanTuning(reader, balance.revolver) ||
      !reader.u32(balance.revolverCooldownTicks) || !readMachine(balance.machineGun) ||
      !reader.u32(balance.machineGunCooldownTicks) || !readShotgun(balance.shotgun) ||
      !reader.u32(balance.shotgunCooldownTicks) || !readRocket(balance.rocketLauncher) ||
      !reader.u32(balance.rocketLauncherCooldownTicks) || !readGrenade(balance.grenadeLauncher) ||
      !readPlasma(balance.plasmaGun) || !readWeaponAmmo(balance.weaponAmmo) ||
      !reader.u32(balance.weaponPulloutTicks) || !reader.u32(balance.jumpPadRetriggerCooldownTicks) ||
      !reader.i32(balance.smallHealthPickupAmount) || !reader.i32(balance.largeHealthPickupAmount) ||
      !reader.u32(balance.smallHealthPickupCooldownTicks) || !reader.u32(balance.largeHealthPickupCooldownTicks) ||
      !readMovementTuning(reader, config.movementTuning) ||
      !reader.f32(config.playerSizeScaleXY) || !reader.f32(config.playerSizeScaleZ) ||
      !reader.f32(config.lightningKnockback) || !reader.f32(config.lightningFireHz) ||
      !reader.f32(config.rocketKnockback) || !reader.i32(config.knockbackTimeMs) ||
      !reader.i32(config.weaponDamage.shotgunDamagePerPellet) ||
      !reader.i32(config.weaponDamage.machineGunDamage) ||
      !reader.i32(config.weaponDamage.lightningGunDamage) ||
      !reader.i32(config.weaponDamage.railgunDamage) ||
      !reader.i32(config.weaponDamage.rocketLauncherDamage) ||
      !reader.i32(config.weaponDamage.plasmaGunDamage) ||
      !reader.i32(config.weaponDamage.freezeGunDamage) || !reader.f32(config.vampirism) ||
      !reader.u8(config.selfDamagePercent) || !reader.i32(config.healthAmount) ||
      !reader.u8(switching) || !readMcGuffin(config.mcguffinConfig) ||
      !readMatchRules(reader, config.matchRules)) {
    return false;
  }
  config.weaponSwitchingMode = static_cast<WeaponSwitchingMode>(switching);
  return validWeaponSwitchingMode(config.weaponSwitchingMode) &&
    validBalanceConfig(config.balance) && validMovementTuning(config.movementTuning) &&
    validWeaponDamage(config.weaponDamage) &&
    validFloatRange(config.playerSizeScaleXY, 0.01F, 100.0F) &&
    validFloatRange(config.playerSizeScaleZ, 0.01F, 100.0F) &&
    validFloatRange(config.lightningKnockback, 0.0F, 100000.0F) &&
    validFloatRange(config.lightningFireHz, 0.0F, 1000.0F) &&
    validFloatRange(config.rocketKnockback, 0.0F, 100000.0F) &&
    config.knockbackTimeMs >= 0 && config.knockbackTimeMs <= 10000 &&
    config.vampirism >= 0.0F && config.vampirism <= 100.0F &&
    config.healthAmount > 0 && config.healthAmount <= 100000 &&
    config.selfDamagePercent <= 100U && isValidMcGuffinConfig(config.mcguffinConfig);
}

bool writeMetadata(Writer& writer, const ReplayMetadata& metadata) {
  if (metadata.mapName.empty() || metadata.mapName.size() > kMaxReplayMapNameBytes ||
      metadata.mapContentHash == 0U || metadata.mapRevision == 0U ||
      !isValidGameMode(metadata.gameMode) || !validVisibility(metadata.visibility) ||
      !validStopReason(metadata.stopReason) ||
      metadata.protocolRevision != kReplayProtocolRevision ||
      metadata.buildFingerprint != kReplayBuildFingerprint ||
      metadata.simulationRevision != kReplaySimulationRevision ||
      metadata.configurationRevision == 0U || !validMatchRules(metadata.matchRules)) return false;
  if (!validateReplayGameplayConfig(metadata.gameplayConfig) ||
      !sameMatchRules(metadata.gameplayConfig.matchRules, metadata.matchRules) ||
      metadata.gameplayConfigHash != canonicalGameplayConfigHash(metadata.gameplayConfig)) {
    return false;
  }
  if (!writer.u32(metadata.formatFlags) || !writer.u32(metadata.protocolRevision) ||
      !writer.u64(metadata.buildFingerprint) || !writer.u64(metadata.gameplayConfigHash) ||
      !writer.u32(metadata.simulationRevision) || !writer.u32(metadata.initialServerTick) ||
      !writer.u32(metadata.mapRevision) || !writer.string(metadata.mapName, kMaxReplayMapNameBytes) ||
      !writer.u32(metadata.mapContentHash) || !writer.u8(static_cast<std::uint8_t>(metadata.gameMode)) ||
      !writeMatchRules(writer, metadata.matchRules) ||
      !writer.u8(static_cast<std::uint8_t>(metadata.visibility)) ||
      !writer.u8(static_cast<std::uint8_t>(metadata.stopReason)) ||
      !writer.u32(metadata.configurationRevision)) return false;
  if (!writeGameplayConfig(writer, metadata.gameplayConfig)) return false;
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

bool readMetadata(Reader& reader, ReplayMetadata& metadata, std::string* error) {
  std::uint8_t gameMode = 0;
  std::uint8_t visibility = 0;
  std::uint8_t stopReason = 0;
  if (!reader.u32(metadata.formatFlags) || !reader.u32(metadata.protocolRevision) ||
      !reader.u64(metadata.buildFingerprint) || !reader.u64(metadata.gameplayConfigHash) ||
      !reader.u32(metadata.simulationRevision) || !reader.u32(metadata.initialServerTick) ||
      !reader.u32(metadata.mapRevision) || !reader.string(metadata.mapName, kMaxReplayMapNameBytes) ||
      !reader.u32(metadata.mapContentHash) || !reader.u8(gameMode) ||
      !readMatchRules(reader, metadata.matchRules) || !reader.u8(visibility) ||
      !reader.u8(stopReason) || !reader.u32(metadata.configurationRevision) ||
      !readGameplayConfig(reader, metadata.gameplayConfig)) return false;
  metadata.gameMode = static_cast<GameMode>(gameMode);
  metadata.visibility = static_cast<ReplayVisibility>(visibility);
  metadata.stopReason = static_cast<ReplayStopReason>(stopReason);
  if (metadata.protocolRevision != kReplayProtocolRevision) {
    if (error != nullptr) *error = "replay protocol revision is incompatible";
    return false;
  }
  if (metadata.buildFingerprint != kReplayBuildFingerprint) {
    if (error != nullptr) *error = "replay build fingerprint is incompatible";
    return false;
  }
  if (metadata.simulationRevision != kReplaySimulationRevision) {
    if (error != nullptr) *error = "replay simulation revision is incompatible";
    return false;
  }
  if (metadata.mapName.empty() || metadata.mapRevision == 0U || metadata.mapContentHash == 0U ||
      !isValidGameMode(metadata.gameMode) || !validVisibility(metadata.visibility) ||
      !validStopReason(metadata.stopReason) || metadata.configurationRevision == 0U ||
      !sameMatchRules(metadata.gameplayConfig.matchRules, metadata.matchRules) ||
      metadata.gameplayConfigHash != canonicalGameplayConfigHash(metadata.gameplayConfig)) return false;
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
  for (const PlayerScore value : match.scores) if (!writer.i16(value)) return false;
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
      !writer.u32(checkpoint.mcguffinThrowPickupLockoutTicks) || !writer.u32(checkpoint.spawnRandomState) ||
      !writer.u32(checkpoint.lethalSequence)) return false;
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
  for (PlayerScore& value : match.scores) if (!reader.i16(value)) return false;
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
      !reader.u32(checkpoint.lethalSequence) ||
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

bool writePlayerMetadata(Writer& writer, const ReplayPlayerMetadata& player, std::size_t index) {
  return player.slot == index && isValidTeam(player.team) &&
    player.name.size() <= kMaxReplayNameBytes && (player.occupied || !player.bot) &&
    writer.u8(player.slot) && writer.boolean(player.occupied) && writer.boolean(player.bot) &&
    writer.u8(static_cast<std::uint8_t>(player.team)) && writer.string(player.name, kMaxReplayNameBytes);
}

bool readPlayerMetadata(Reader& reader, ReplayPlayerMetadata& player, std::size_t index) {
  std::uint8_t team = 0;
  if (!reader.u8(player.slot) || !reader.boolean(player.occupied) || !reader.boolean(player.bot) ||
      !reader.u8(team) || !reader.string(player.name, kMaxReplayNameBytes)) return false;
  player.team = static_cast<Team>(team);
  return player.slot == index && isValidTeam(player.team) && (player.occupied || !player.bot);
}

bool writeAuthorityBoundary(Writer& writer, const ReplayAuthorityBoundary& boundary) {
  if (boundary.tick != boundary.checkpoint.serverTick ||
      boundary.configurationRevision == 0U || !isValidGameMode(boundary.gameMode) ||
      !validMatchRules(boundary.matchRules) ||
      !sameMatchRules(boundary.gameplayConfig.matchRules, boundary.matchRules) ||
      canonicalGameplayConfigHash(boundary.gameplayConfig) != boundary.checkpoint.gameplayConfigHash ||
      !validateReplayGameplayConfig(boundary.gameplayConfig) ||
      !validateReplayCheckpoint(boundary.checkpoint)) return false;
  if (!writer.u32(boundary.tick) || !writer.u32(boundary.configurationRevision) ||
      !writer.u8(static_cast<std::uint8_t>(boundary.gameMode)) ||
      !writeMatchRules(writer, boundary.matchRules)) return false;
  for (std::size_t index = 0; index < boundary.players.size(); ++index) {
    if (!writePlayerMetadata(writer, boundary.players[index], index)) return false;
  }
  return writeGameplayConfig(writer, boundary.gameplayConfig) &&
    writeCheckpoint(writer, boundary.checkpoint);
}

bool readAuthorityBoundary(Reader& reader, ReplayAuthorityBoundary& boundary) {
  std::uint8_t mode = 0;
  if (!reader.u32(boundary.tick) || !reader.u32(boundary.configurationRevision) ||
      !reader.u8(mode) || !readMatchRules(reader, boundary.matchRules)) return false;
  boundary.gameMode = static_cast<GameMode>(mode);
  if (!isValidGameMode(boundary.gameMode) ||
      boundary.configurationRevision == 0U) return false;
  for (std::size_t index = 0; index < boundary.players.size(); ++index) {
    if (!readPlayerMetadata(reader, boundary.players[index], index)) return false;
  }
  if (!readGameplayConfig(reader, boundary.gameplayConfig) ||
      !readCheckpoint(reader, boundary.checkpoint)) return false;
  return boundary.tick == boundary.checkpoint.serverTick &&
    sameMatchRules(boundary.gameplayConfig.matchRules, boundary.matchRules) &&
    canonicalGameplayConfigHash(boundary.gameplayConfig) == boundary.checkpoint.gameplayConfigHash &&
    validateReplayCheckpoint(boundary.checkpoint);
}

bool writeHash(Writer& writer, const ReplayStateHash& hash) {
  return writer.u32(hash.tick) && writer.u64(hash.value);
}

bool readHash(Reader& reader, ReplayStateHash& hash) {
  return reader.u32(hash.tick) && reader.u64(hash.value);
}

bool writeLethal(Writer& writer, const ReplayLethalEvent& event) {
  return event.victim < kDuelPlayerCount && (event.killer < kDuelPlayerCount || event.killer == kNoReplayPlayer) &&
    event.replayGeneration != 0U && event.sequence != 0U && validWeapon(event.weapon) &&
    validLethalKind(event.kind) &&
    ((event.kind == LethalKind::Self && event.killer == event.victim) ||
      (event.kind == LethalKind::World && event.killer == kNoReplayPlayer) ||
      (event.kind == LethalKind::Direct && event.killer != kNoReplayPlayer) ||
      (event.kind == LethalKind::Splash && event.killer != kNoReplayPlayer)) && writer.u32(event.tick) &&
    writer.u32(event.replayGeneration) && writer.u8(event.victim) && writer.u8(event.killer) &&
    writer.u8(static_cast<std::uint8_t>(event.weapon)) && writer.u32(event.projectileSequence) &&
    writer.u8(static_cast<std::uint8_t>(event.kind)) && writer.u32(event.sequence);
}

bool readLethal(Reader& reader, ReplayLethalEvent& event) {
  std::uint8_t weapon = 0;
  std::uint8_t kind = 0;
  if (!reader.u32(event.tick) || !reader.u32(event.replayGeneration) || !reader.u8(event.victim) ||
      !reader.u8(event.killer) || !reader.u8(weapon) || !reader.u32(event.projectileSequence) ||
      !reader.u8(kind) || !reader.u32(event.sequence)) return false;
  event.weapon = static_cast<Weapon>(weapon);
  event.kind = static_cast<LethalKind>(kind);
  return event.victim < kDuelPlayerCount && (event.killer < kDuelPlayerCount || event.killer == kNoReplayPlayer) &&
    event.replayGeneration != 0U && event.sequence != 0U && validWeapon(event.weapon) &&
    validLethalKind(event.kind) &&
    ((event.kind == LethalKind::Self && event.killer == event.victim) ||
      (event.kind == LethalKind::World && event.killer == kNoReplayPlayer) ||
      (event.kind == LethalKind::Direct && event.killer != kNoReplayPlayer) ||
      (event.kind == LethalKind::Splash && event.killer != kNoReplayPlayer));
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

bool validateReplayGameplayConfig(
  const ReplayGameplayConfig& config,
  std::string* error
) {
  std::vector<std::uint8_t> bytes;
  Writer writer(bytes);
  if (!writeGameplayConfig(writer, config) || !writer.ok() ||
      bytes.size() > kMaxReplayConfigBytes) {
    return fail(error, "replay gameplay configuration is invalid");
  }
  if (error != nullptr) error->clear();
  return true;
}

std::uint64_t canonicalGameplayConfigHash(const ReplayGameplayConfig& config) {
  std::vector<std::uint8_t> bytes;
  Writer writer(bytes);
  if (!writeGameplayConfig(writer, config) || !writer.ok()) return 0U;
  std::uint64_t hash = 1469598103934665603ULL;
  for (const std::uint8_t byte : bytes) {
    hash ^= byte;
    hash *= 1099511628211ULL;
  }
  return hash;
}

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
      demo.hashes.size() > kMaxReplayTicks || demo.lethalEvents.size() > kMaxReplayLethalEvents ||
      demo.authorityBoundaries.size() > kMaxReplayAuthorityBoundaries) {
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
  std::uint32_t previousLethalSequence = 0;
  bool hasLethal = false;
  for (const ReplayLethalEvent& event : demo.lethalEvents) {
    if ((hasLethal && (event.tick < previousLethal ||
          (event.tick == previousLethal && event.sequence <= previousLethalSequence))) ||
        event.sequence == 0U ||
        event.tick < demo.metadata.initialServerTick) return fail(error, "lethal events are out of order");
    previousLethal = event.tick;
    previousLethalSequence = event.sequence;
    hasLethal = true;
    std::vector<std::uint8_t> payload;
    Writer payloadWriter(payload);
    if (!writeLethal(payloadWriter, event) || !writeChunk(writer, ReplayChunkType::LethalEvent, payload)) return fail(error, "lethal record is invalid");
  }
  std::uint32_t previousBoundary = 0;
  bool hasBoundary = false;
  for (const ReplayAuthorityBoundary& boundary : demo.authorityBoundaries) {
    if ((hasBoundary && boundary.tick <= previousBoundary) ||
        boundary.tick < demo.metadata.initialServerTick) {
      return fail(error, "authority boundaries are out of order");
    }
    previousBoundary = boundary.tick;
    hasBoundary = true;
    std::vector<std::uint8_t> payload;
    Writer payloadWriter(payload);
    if (!writeAuthorityBoundary(payloadWriter, boundary) || !payloadWriter.ok() ||
        !writeChunk(writer, ReplayChunkType::AuthorityBoundary, payload)) {
      return fail(error, "authority boundary is invalid or too large");
    }
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
  if (error != nullptr) error->clear();
  if (!readMetadata(metadataReader, decoded.metadata, error) || !metadataReader.done()) {
    if (error != nullptr && !error->empty()) return false;
    return fail(error, "replay metadata is invalid");
  }
  if (decoded.metadata.formatFlags != preambleFlags) return fail(error, "replay flags disagree");
  std::uint32_t previousTick = 0;
  std::uint32_t previousCheckpoint = 0;
  std::uint32_t previousHash = 0;
  std::uint32_t previousLethal = 0;
  std::uint32_t previousLethalSequence = 0;
  std::uint32_t previousBoundary = 0;
  bool hasTick = false;
  bool hasCheckpoint = false;
  bool hasHash = false;
  bool hasLethal = false;
  bool hasBoundary = false;
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
      if (decoded.lethalEvents.size() >= kMaxReplayLethalEvents) return fail(error, "replay has too many lethal events");
      ReplayLethalEvent event;
      if (!readLethal(payloadReader, event) || !payloadReader.done() ||
          event.tick < decoded.metadata.initialServerTick ||
          (hasLethal && (event.tick < previousLethal ||
            (event.tick == previousLethal && event.sequence <= previousLethalSequence)))) {
        return fail(error, "replay lethal event is invalid or out of order");
      }
      previousLethal = event.tick;
      previousLethalSequence = event.sequence;
      hasLethal = true;
      decoded.lethalEvents.push_back(event);
      break;
    }
    case ReplayChunkType::AuthorityBoundary: {
      if (decoded.authorityBoundaries.size() >= kMaxReplayAuthorityBoundaries) {
        return fail(error, "replay has too many authority boundaries");
      }
      ReplayAuthorityBoundary boundary;
      if (!readAuthorityBoundary(payloadReader, boundary) || !payloadReader.done() ||
          boundary.tick < decoded.metadata.initialServerTick ||
          (hasBoundary && boundary.tick <= previousBoundary)) {
        return fail(error, "replay authority boundary is invalid or out of order");
      }
      previousBoundary = boundary.tick;
      hasBoundary = true;
      decoded.authorityBoundaries.push_back(std::move(boundary));
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
