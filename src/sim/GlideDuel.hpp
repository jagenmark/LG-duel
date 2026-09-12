#pragma once

#include "sim/GameMode.hpp"
#include "sim/Movement.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace lg {

enum class GlideCondition : std::uint8_t { Float, Blast, Rush };
enum class GlidePowerup : std::uint8_t { None, Surge, Guard, Repair };
inline constexpr std::uint16_t kGlideBuffTicks = 6U * 125U;
inline constexpr std::array<std::uint16_t, 3> kGlidePickupSpawnTicks = {8U * 125U, 20U * 125U, 32U * 125U};

// Public, authoritative state. Timings and round conditions derive from the
// round clock; checkpoints preserve ownership and layout across replay seeks.
struct GlideDuelState {
  std::uint32_t roundNumber = 0;
  std::array<Vec3, 3> pickupPositions = {};
  std::uint8_t claimedMask = 0;
  std::array<GlidePowerup, 2> buffs = {};
  std::array<std::uint16_t, 2> buffTicks = {};
  std::uint32_t pickupSequence = 0;
  std::uint8_t lastCollector = 255;
  GlidePowerup lastPickup = GlidePowerup::None;
  std::uint16_t announcementTicks = 0;
};

[[nodiscard]] inline bool validGlideDuelState(const GlideDuelState& state) {
  if (state.claimedMask > 7 || (state.lastCollector > 1 && state.lastCollector != 255) ||
      static_cast<unsigned>(state.lastPickup) > 3 || state.announcementTicks > 250) return false;
  for (const auto& p : state.pickupPositions)
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return false;
  for (std::size_t i = 0; i < 2; ++i) {
    if (static_cast<unsigned>(state.buffs[i]) > 2 || state.buffTicks[i] > kGlideBuffTicks ||
        ((state.buffs[i] == GlidePowerup::None) != (state.buffTicks[i] == 0))) return false;
  }
  return true;
}

[[nodiscard]] inline GlideCondition glideCondition(std::uint32_t round) {
  return static_cast<GlideCondition>((std::max(1U, round) - 1U) % 3U);
}
[[nodiscard]] inline std::string_view glideConditionName(GlideCondition condition) {
  switch (condition) {
  case GlideCondition::Float: return "FLOAT";
  case GlideCondition::Blast: return "BLAST";
  case GlideCondition::Rush: return "RUSH";
  }
  return "";
}
[[nodiscard]] inline std::string_view glidePowerupName(GlidePowerup powerup) {
  switch (powerup) {
  case GlidePowerup::Surge: return "SURGE";
  case GlidePowerup::Guard: return "GUARD";
  case GlidePowerup::Repair: return "REPAIR";
  case GlidePowerup::None: return "";
  }
  return "";
}
[[nodiscard]] inline GlidePowerup glideWavePowerup(std::uint32_t round, std::size_t wave) {
  return static_cast<GlidePowerup>(1U + (std::max(1U, round) - 1U + wave) % 3U);
}
[[nodiscard]] inline std::uint32_t glideElapsedTicks(std::uint32_t remaining) {
  return kGlideRoundTicks - std::min<std::uint32_t>(remaining, kGlideRoundTicks);
}
// -1 means no active/announced pickup. includePreview adds the three-second cue.
[[nodiscard]] inline int glidePickupWave(const GlideDuelState& state, std::uint32_t remaining, bool includePreview = false) {
  const auto elapsed = glideElapsedTicks(remaining);
  for (std::size_t i = 0; i < 3; ++i) {
    if (!(state.claimedMask & (1U << i)) && elapsed >= kGlidePickupSpawnTicks[i] - (includePreview ? 375U : 0U) &&
        elapsed < kGlidePickupSpawnTicks[i] + 1000U) return static_cast<int>(i);
  }
  return -1;
}
[[nodiscard]] inline float glideZoneRadius(std::uint32_t remaining) {
  return 4.0F + 20.0F * std::clamp(static_cast<float>(remaining) / kGlideOverdriveTicks, 0.0F, 1.0F);
}
[[nodiscard]] inline bool glideOutsideZone(const GlideDuelState& state, std::uint32_t remaining, const Vec3& position) {
  const Vec3 delta = position - state.pickupPositions[2];
  const float radius = glideZoneRadius(remaining);
  return delta.x * delta.x + delta.y * delta.y > radius * radius;
}
[[nodiscard]] inline MovementTuning glideRoundMovement(MovementTuning base, std::uint32_t round) {
  base.glideMovementEnabled = true;
  switch (glideCondition(round)) {
  case GlideCondition::Float: base.gravity *= 0.7F; break;
  case GlideCondition::Blast: break;
  case GlideCondition::Rush: base.dashCooldown *= 0.6F; break;
  }
  return base;
}
} // namespace lg
