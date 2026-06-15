#pragma once

#include "shared/Math.hpp"
#include "sim/Arena.hpp"
#include "sim/PlayerState.hpp"
#include "sim/UserCommand.hpp"

namespace lg {

struct LightningGunTuning {
  float range = 18.0F;
  float damagePerSecond = 80.0F;
  float eyeHeight = 0.65F;
  float knockbackPerSecond = 22.0F;
};

struct LightningGunState {
  double fractionalDamage = 0.0;
};

struct LightningGunResult {
  Vec3 start = {};
  Vec3 end = {};
  bool active = false;
  bool hit = false;
  int damageApplied = 0;
  Vec3 knockbackImpulse = {};
  std::uint32_t requestedRewindTicks = 0;
  std::uint32_t appliedRewindTicks = 0;
  bool rewindClamped = false;
  bool hasRewindDebug = false;
  std::uint32_t rewindTargetTick = 0;
  Vec3 currentTargetPosition = {};
  Vec3 rewoundTargetPosition = {};
  CollisionBounds currentTargetBounds = {};
  CollisionBounds rewoundTargetBounds = {};
};

[[nodiscard]] LightningGunResult simulateLightningGun(
  const PlayerState& attacker,
  PlayerState& target,
  const UserCommand& command,
  const Arena& arena,
  const LightningGunTuning& tuning,
  LightningGunState& state,
  float fixedDt
);

} // namespace lg
