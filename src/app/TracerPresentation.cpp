#include "app/TracerPresentation.hpp"

#include "render/WeaponPresentation.hpp"

#include <algorithm>
#include <cmath>

namespace lg {

RenderColor tracerColor(Weapon weapon, std::uint32_t seed) {
  const std::uint8_t variation =
    static_cast<std::uint8_t>((seed * 17U + 31U) & 23U);
  if (weapon == Weapon::Shotgun) {
    return {
      static_cast<std::uint8_t>(218U + variation),
      static_cast<std::uint8_t>(166U + variation),
      92,
      150,
    };
  }
  return {
    255,
    static_cast<std::uint8_t>(210U + variation),
    118,
    185,
  };
}

float localTracerVisualRange(const WeaponFireResult& fire) {
  const float fireDistance = length(fire.end - fire.start);
  if (std::isfinite(fireDistance) && fireDistance > 0.001F) {
    // Server cameraForward endpoints can rebuild a fraction short after client normalization.
    return fireDistance + 0.001F;
  }
  return fire.weapon == Weapon::Shotgun ? 18.0F : 100.0F;
}

void LocalTracerAimHistory::remember(const UserCommand& command) {
  entries_[next_] = {
    command.sequence,
    command.viewYawRadians,
    command.viewPitchRadians,
    true,
  };
  next_ = (next_ + 1U) % entries_.size();
}

bool LocalTracerAimHistory::find(
  std::uint32_t sequence,
  float& yawRadians,
  float& pitchRadians
) const {
  for (const LocalTracerAim& entry : entries_) {
    if (entry.active && entry.sequence == sequence) {
      yawRadians = entry.yawRadians;
      pitchRadians = entry.pitchRadians;
      return true;
    }
  }
  return false;
}

WeaponFireResult localPerspectiveTracerFire(
  const Arena& arena,
  const WeaponFireResult& fire,
  Vec3 visualStart,
  const PlayerState& localPlayer,
  const LocalTracerAimHistory& localAimHistory
) {
  float yawRadians = localPlayer.viewYawRadians;
  float pitchRadians = localPlayer.viewPitchRadians;
  (void)localAimHistory.find(fire.visualSeed, yawRadians, pitchRadians);

  WeaponFireResult visualFire = fire;
  const Vec3 direction = cameraForward(yawRadians, pitchRadians);
  const WorldTrace trace =
    traceWorld(arena, fire.start, direction, localTracerVisualRange(fire));
  visualFire.start = visualStart;
  visualFire.end = trace.end;
  return visualFire;
}

CapturedMachineGunTracerPresentation captureMachineGunTracerPresentation(
  const WeaponFireResult& fire,
  Vec3 visualStart
) {
  const Vec3 direction = normalize(fire.end - fire.start);
  if (length(direction) <= 0.0001F) {
    return {};
  }
  const float width =
    0.010F + static_cast<float>(fire.visualSeed & 3U) * 0.0015F;
  return {
    true,
    {
      visualStart + direction * 0.22F,
      fire.end,
      0.0F,
      0.036F,
      width,
      tracerColor(Weapon::MachineGun, fire.visualSeed),
      fire.visualSeed,
      TracerStyle::MachineGun,
    },
    {
      visualStart,
      visualStart + direction * 0.16F,
      0.0F,
      kMachineGunMuzzleFlashDurationSeconds,
      0.045F,
      {255, 188, 76, 235},
      fire.visualSeed,
      TracerStyle::MachineGunMuzzleFlash,
    },
  };
}

void TransientTracerPool::update(float dt) {
  const float elapsed = std::max(0.0F, dt);
  for (std::size_t index = 0; index < tracers_.size(); ++index) {
    if (!active_[index]) {
      continue;
    }
    if (expiryGraceState_[index] == 2U) {
      active_[index] = false;
      continue;
    }
    const float ageBeforeUpdate = tracers_[index].ageSeconds;
    tracers_[index].ageSeconds += elapsed;
    if (tracers_[index].ageSeconds >= tracers_[index].lifetimeSeconds) {
      if (
        expiryGraceState_[index] == 1U &&
        ageBeforeUpdate <= 0.0001F
      ) {
        expiryGraceState_[index] = 2U;
        tracers_[index].ageSeconds = tracers_[index].lifetimeSeconds * 0.35F;
        continue;
      }
      active_[index] = false;
    } else if (expiryGraceState_[index] == 1U) {
      expiryGraceState_[index] = 0U;
    }
  }
}

void TransientTracerPool::add(
  const TransientTracer& tracer,
  bool followMuzzle,
  Weapon weapon,
  std::uint32_t seed,
  std::uint8_t playerIndex
) {
  std::size_t slot = tracers_.size();
  for (std::size_t index = 0; index < active_.size(); ++index) {
    if (!active_[index]) {
      slot = index;
      break;
    }
  }
  if (slot == tracers_.size()) {
    slot = 0;
    for (std::size_t index = 1; index < active_.size(); ++index) {
      if (tracers_[index].ageSeconds > tracers_[slot].ageSeconds) {
        slot = index;
      }
    }
  }
  tracers_[slot] = tracer;
  active_[slot] = true;
  attachments_[slot] = {followMuzzle, weapon, seed, playerIndex};
  // Keep the compact Rocket flash for one extra submitted frame if a long
  // frame crosses its whole lifetime.
  expiryGraceState_[slot] =
    tracer.style == TracerStyle::RocketLauncherMuzzleFlash ? 1U : 0U;
}

void TransientTracerPool::addCapturedMachineGunPresentation(
  const CapturedMachineGunTracerPresentation& presentation,
  std::uint32_t seed,
  std::uint8_t playerIndex
) {
  if (!presentation.active) {
    return;
  }
  add(
    presentation.longTracer,
    false,
    Weapon::MachineGun,
    seed,
    playerIndex
  );
  add(
    presentation.muzzleCue,
    false,
    Weapon::MachineGun,
    seed,
    playerIndex
  );
}

} // namespace lg
