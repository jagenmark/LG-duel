#include "render/CombatEffects.hpp"

#include "shared/Sequence.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace lg {
namespace {

[[nodiscard]] std::uint32_t mixBits(std::uint32_t value) {
  value ^= value >> 16U;
  value *= 0x7feb352dU;
  value ^= value >> 15U;
  value *= 0x846ca68bU;
  return value ^ (value >> 16U);
}

[[nodiscard]] float seededUnit(std::uint32_t seed, std::uint32_t lane) {
  return static_cast<float>(mixBits(seed + lane * 0x9e3779b9U) & 0x00ffffffU) /
    static_cast<float>(0x01000000U);
}

[[nodiscard]] float seededSigned(std::uint32_t seed, std::uint32_t lane) {
  return seededUnit(seed, lane) * 2.0F - 1.0F;
}

[[nodiscard]] bool finite(Vec3 value) {
  return std::isfinite(value.x) &&
    std::isfinite(value.y) &&
    std::isfinite(value.z);
}

[[nodiscard]] Vec3 safeDirection(Vec3 value, Vec3 fallback) {
  if (!finite(value)) {
    return fallback;
  }
  const Vec3 direction = normalize(value);
  return finite(direction) && length(direction) > 0.0001F
    ? direction
    : fallback;
}

[[nodiscard]] constexpr Vec3 cross(Vec3 lhs, Vec3 rhs) {
  return {
    lhs.y * rhs.z - lhs.z * rhs.y,
    lhs.z * rhs.x - lhs.x * rhs.z,
    lhs.x * rhs.y - lhs.y * rhs.x,
  };
}

[[nodiscard]] std::size_t clampedLimit(
  std::size_t requested,
  std::size_t capacity
) {
  return std::min(requested, capacity);
}

template <std::size_t Capacity>
[[nodiscard]] std::size_t activeCount(
  const std::array<CombatEffects::PoolEntry, Capacity>& pool
) {
  return static_cast<std::size_t>(std::count_if(
    pool.begin(),
    pool.end(),
    [](const CombatEffects::PoolEntry& entry) { return entry.active; }
  ));
}

template <std::size_t Capacity>
CombatEffects::PoolEntry* allocateEntry(
  std::array<CombatEffects::PoolEntry, Capacity>& pool,
  std::size_t limit,
  std::uint64_t serial
) {
  const std::size_t usable = clampedLimit(limit, Capacity);
  if (usable == 0U) {
    return nullptr;
  }
  std::size_t count = 0;
  auto freeEntry = pool.end();
  auto oldest = pool.end();
  for (auto it = pool.begin(); it != pool.end(); ++it) {
    if (!it->active) {
      if (freeEntry == pool.end()) {
        freeEntry = it;
      }
      continue;
    }
    ++count;
    if (oldest == pool.end() || it->serial < oldest->serial) {
      oldest = it;
    }
  }
  CombatEffects::PoolEntry* entry = count < usable && freeEntry != pool.end()
    ? &*freeEntry
    : &*oldest;
  entry->serial = serial;
  entry->expiryGraceState = 0;
  entry->active = true;
  return entry;
}

template <std::size_t Capacity>
void expireAndSimulate(
  std::array<CombatEffects::PoolEntry, Capacity>& pool,
  float deltaSeconds
) {
  constexpr Vec3 gravity = {0.0F, 0.0F, -18.0F};
  for (CombatEffects::PoolEntry& entry : pool) {
    if (!entry.active) {
      continue;
    }
    if (entry.expiryGraceState == 2U) {
      entry.active = false;
      continue;
    }
    TransientEffect& effect = entry.effect;
    const float ageBeforeUpdate = effect.ageSeconds;
    effect.ageSeconds += deltaSeconds;
    if (
      effect.lifetimeSeconds <= 0.0F ||
      effect.ageSeconds >= effect.lifetimeSeconds
    ) {
      if (
        effect.lifetimeSeconds > 0.0F &&
        entry.expiryGraceState == 1U &&
        ageBeforeUpdate <= 0.0001F
      ) {
        entry.expiryGraceState = 2U;
        effect.ageSeconds = effect.lifetimeSeconds * 0.35F;
        continue;
      }
      entry.active = false;
      continue;
    }
    if (entry.expiryGraceState == 1U) {
      entry.expiryGraceState = 0U;
    }
    if (
      effect.type == TransientEffectType::MachineGunCasing ||
      effect.type == TransientEffectType::BulletImpactSpark ||
      effect.type == TransientEffectType::RocketExplosionShard
    ) {
      effect.position += effect.velocity * deltaSeconds +
        gravity * (0.5F * deltaSeconds * deltaSeconds);
      effect.velocity += gravity * deltaSeconds;
      effect.rotationRadians = std::fmod(
        effect.rotationRadians + effect.angularVelocityRadiansPerSecond *
          deltaSeconds,
        2.0F * std::numbers::pi_v<float>
      );
    } else if (
      effect.type == TransientEffectType::BulletImpactDust ||
      effect.type == TransientEffectType::MachineGunMuzzleSmoke ||
      effect.type == TransientEffectType::RocketLauncherMuzzleSmoke ||
      effect.type == TransientEffectType::RocketExplosionSmoke
    ) {
      const float drag = std::exp(-2.8F * deltaSeconds);
      effect.position += effect.velocity * deltaSeconds;
      effect.velocity *= drag;
    }
  }
}

template <std::size_t Capacity>
void trimOldestToLimit(
  std::array<CombatEffects::PoolEntry, Capacity>& pool,
  std::size_t requestedLimit
) {
  const std::size_t limit = clampedLimit(requestedLimit, Capacity);
  std::size_t count = activeCount(pool);
  while (count > limit) {
    auto oldest = pool.end();
    for (auto it = pool.begin(); it != pool.end(); ++it) {
      if (!it->active) {
        continue;
      }
      if (oldest == pool.end() || it->serial < oldest->serial) {
        oldest = it;
      }
    }
    if (oldest == pool.end()) {
      return;
    }
    oldest->active = false;
    --count;
  }
}

[[nodiscard]] RenderColor impactColor(ImpactSurfaceCategory surface) {
  switch (surface) {
  case ImpactSurfaceCategory::Metal:
    return {255, 202, 102, 225};
  case ImpactSurfaceCategory::Stone:
    return {182, 168, 142, 190};
  case ImpactSurfaceCategory::WoodSoft:
    return {214, 170, 104, 190};
  case ImpactSurfaceCategory::Energy:
    return {104, 214, 255, 235};
  case ImpactSurfaceCategory::GenericHard:
    return {244, 186, 94, 215};
  }
  return {244, 186, 94, 215};
}

struct SurfaceImpactProfile {
  int baseSparkCount = 0;
  float flashLifetimeSeconds = 0.05F;
  float flashInitialScale = 0.06F;
  float flashFinalScale = 0.02F;
  float sparkLifetimeSeconds = 0.16F;
  float sparkInitialScale = 0.014F;
  float sparkSpeed = 2.0F;
  float dustInitialScale = 0.04F;
  float dustFinalScale = 0.14F;
  float decalScale = 0.035F;
  bool dust = false;
  bool decal = true;
};

[[nodiscard]] SurfaceImpactProfile surfaceImpactProfile(
  SurfaceImpactWeapon weapon
) {
  switch (weapon) {
  case SurfaceImpactWeapon::MachineGun:
    return {4, 0.065F, 0.082F, 0.024F, 0.20F, 0.014F, 2.2F,
      0.045F, 0.16F, 0.036F, true, true};
  case SurfaceImpactWeapon::Shotgun:
    // One larger response stands in for the pellet cluster. The caller sends
    // one request per accepted shot, never one request per pellet.
    return {4, 0.075F, 0.115F, 0.032F, 0.18F, 0.016F, 1.9F,
      0.070F, 0.20F, 0.058F, true, true};
  case SurfaceImpactWeapon::Precision:
    return {2, 0.045F, 0.060F, 0.016F, 0.14F, 0.012F, 2.8F,
      0.032F, 0.10F, 0.026F, false, true};
  case SurfaceImpactWeapon::Revolver:
    return {2, 0.052F, 0.070F, 0.018F, 0.15F, 0.013F, 2.5F,
      0.036F, 0.12F, 0.030F, false, true};
  case SurfaceImpactWeapon::FreezeGun:
    return {2, 0.065F, 0.092F, 0.028F, 0.17F, 0.012F, 1.35F,
      0.0F, 0.0F, 0.040F, false, true};
  }
  return {};
}

[[nodiscard]] RenderColor surfaceImpactColor(
  ImpactSurfaceCategory surface,
  SurfaceImpactWeapon weapon
) {
  if (weapon == SurfaceImpactWeapon::FreezeGun) {
    return {170, 244, 255, 235};
  }
  return impactColor(surface);
}

[[nodiscard]] RenderColor surfaceImpactDecalColor(
  ImpactSurfaceCategory surface,
  SurfaceImpactWeapon weapon
) {
  if (weapon == SurfaceImpactWeapon::FreezeGun) {
    return {94, 186, 204, 118};
  }
  switch (surface) {
  case ImpactSurfaceCategory::Metal:
    return {54, 48, 38, 190};
  case ImpactSurfaceCategory::Stone:
    return {54, 48, 42, 180};
  case ImpactSurfaceCategory::WoodSoft:
    return {70, 52, 32, 172};
  case ImpactSurfaceCategory::Energy:
    return {58, 110, 132, 120};
  case ImpactSurfaceCategory::GenericHard:
    return {48, 42, 36, 190};
  }
  return {48, 42, 36, 190};
}

} // namespace

CombatEffectPulseTimerAdvance advanceCombatEffectPulseTimer(
  float remainingSeconds,
  float deltaSeconds,
  float intervalSeconds
) {
  const float interval = std::isfinite(intervalSeconds)
    ? std::max(intervalSeconds, 0.001F)
    : 0.10F;
  const float remaining = std::isfinite(remainingSeconds)
    ? remainingSeconds
    : 0.0F;
  const float delta = std::isfinite(deltaSeconds)
    ? std::max(deltaSeconds, 0.0F)
    : 0.0F;
  if (remaining <= 0.0F) {
    return {true, interval};
  }
  float next = remaining - delta;
  const bool pulseDue = next <= 0.0F;
  if (pulseDue) {
    const float overdue = -next;
    next = interval - std::fmod(overdue, interval);
  }
  return {pulseDue, next};
}

void CombatEffectEventHistory::clear() {
  weaponFires_ = {};
  explosions_ = {};
  hasLastExplosionSequence_ = {};
  lastExplosionSequence_ = {};
  nextWeaponFire_ = 0;
  nextExplosion_ = 0;
}

bool CombatEffectEventHistory::acceptWeaponFire(
  std::uint8_t playerIndex,
  Weapon weapon,
  std::uint32_t visualSeed
) {
  if (playerIndex >= kDuelPlayerCount) {
    return false;
  }
  for (const WeaponFireEvent& event : weaponFires_) {
    if (
      event.active &&
      event.playerIndex == playerIndex &&
      event.weapon == weapon &&
      event.visualSeed == visualSeed
    ) {
      return false;
    }
  }
  weaponFires_[nextWeaponFire_ % weaponFires_.size()] = {
    playerIndex,
    weapon,
    visualSeed,
    true,
  };
  ++nextWeaponFire_;
  return true;
}

bool CombatEffectEventHistory::acceptExplosion(
  std::uint8_t ownerIndex,
  std::uint32_t sequence
) {
  if (ownerIndex >= kDuelPlayerCount) {
    return false;
  }
  if (
    hasLastExplosionSequence_[ownerIndex] &&
    !isSequenceNewer(sequence, lastExplosionSequence_[ownerIndex])
  ) {
    return false;
  }
  for (const ExplosionEvent& event : explosions_) {
    if (
      event.active &&
      event.ownerIndex == ownerIndex &&
      event.sequence == sequence
    ) {
      return false;
    }
  }
  explosions_[nextExplosion_ % explosions_.size()] = {
    ownerIndex,
    sequence,
    true,
  };
  ++nextExplosion_;
  lastExplosionSequence_[ownerIndex] = sequence;
  hasLastExplosionSequence_[ownerIndex] = true;
  return true;
}

void CombatEffects::clear() {
  lights_ = {};
  casings_ = {};
  particles_ = {};
  decals_ = {};
  muzzleAttachments_ = {};
  hasMuzzleAttachment_ = {};
  nextSerial_ = 1;
  shotsSpawned_ = 0;
  surfaceImpactsSpawned_ = 0;
  freezePulsesSpawned_ = 0;
  rocketShotsSpawned_ = 0;
  rocketExplosionsSpawned_ = 0;
  effectsDropped_ = 0;
  peaks_ = {};
}

void CombatEffects::update(
  float deltaSeconds,
  const CombatEffectsTuning& tuning
) {
  const float dt = std::clamp(deltaSeconds, 0.0F, 0.25F);
  peaks_.peakLights = std::max(
    peaks_.peakLights,
    static_cast<std::uint32_t>(activeCount(lights_))
  );
  peaks_.peakCasings = std::max(
    peaks_.peakCasings,
    static_cast<std::uint32_t>(activeCount(casings_))
  );
  peaks_.peakParticles = std::max(
    peaks_.peakParticles,
    static_cast<std::uint32_t>(activeCount(particles_))
  );
  peaks_.peakDecals = std::max(
    peaks_.peakDecals,
    static_cast<std::uint32_t>(activeCount(decals_))
  );
  if (tuning.quality <= 0) {
    lights_ = {};
    casings_ = {};
    particles_ = {};
    decals_ = {};
    return;
  }
  if (tuning.quality < 2) {
    for (PoolEntry& particle : particles_) {
      if (
        particle.active &&
        (
          particle.effect.type ==
            TransientEffectType::RocketLauncherMuzzleSmoke ||
          particle.effect.type == TransientEffectType::RocketExplosionShard ||
          particle.effect.type == TransientEffectType::RocketExplosionSmoke
        )
      ) {
        particle.active = false;
      }
    }
  }
  expireAndSimulate(lights_, dt);
  expireAndSimulate(casings_, dt);
  expireAndSimulate(particles_, dt);
  expireAndSimulate(decals_, dt);
  trimOldestToLimit(
    casings_,
    tuning.casingsEnabled ? tuning.maximumCasings : 0U
  );
  trimOldestToLimit(particles_, tuning.maximumParticles);
  trimOldestToLimit(decals_, tuning.maximumDecals);

  for (PoolEntry& light : lights_) {
    const std::size_t attachment =
      static_cast<std::size_t>(light.attachment);
    if (
      light.active &&
      attachment < hasMuzzleAttachment_.size() &&
      light.effect.ownerIndex < kDuelPlayerCount &&
      hasMuzzleAttachment_[attachment][light.effect.ownerIndex]
    ) {
      // A muzzle light stays on the current socket during its short life, so
      // sway, recoil, remote pose changes, and frame rate cannot detach it.
      light.effect.position =
        muzzleAttachments_[attachment][light.effect.ownerIndex];
    }
  }

  const CombatEffectsStats current = stats();
  peaks_.peakLights = std::max(peaks_.peakLights, current.activeLights);
  peaks_.peakCasings = std::max(peaks_.peakCasings, current.activeCasings);
  peaks_.peakParticles = std::max(peaks_.peakParticles, current.activeParticles);
  peaks_.peakDecals = std::max(peaks_.peakDecals, current.activeDecals);
}

void CombatEffects::spawnMachineGunShot(
  const MachineGunShotEffectsRequest& request,
  const CombatEffectsTuning& tuning
) {
  if (tuning.quality <= 0) {
    return;
  }
  ++shotsSpawned_;
  const std::uint32_t seed = request.visualSeed;
  const Vec3 forward = normalize(request.muzzleForward);
  const Vec3 right = normalize(request.muzzleRight);
  const Vec3 up = normalize(request.muzzleUp);

  PoolEntry* light = allocateEntry(
    lights_,
    kLightCapacity,
    nextSerial_++
  );
  if (light != nullptr) {
    light->effect = {
      TransientEffectType::MachineGunMuzzleLight,
      request.muzzlePosition,
      0.0F,
      std::max(0.001F, tuning.muzzleLightDurationSeconds),
      1.0F,
      1.0F,
      {255, 154, 62, 255},
      seed,
    };
    light->effect.intensity = std::max(0.0F, tuning.muzzleLightIntensity);
    light->effect.radius = std::max(0.0F, tuning.muzzleLightRadius);
    light->effect.ownerIndex = request.ownerIndex;
    light->attachment = MuzzleAttachment::MachineGun;
  }

  const std::size_t particleLimit = clampedLimit(
    tuning.maximumParticles,
    kParticleCapacity
  );
  const int muzzleSparkCount = tuning.quality >= 2 &&
      seededUnit(seed, 2U) > 0.42F
    ? 1
    : 0;
  for (int index = 0; index < muzzleSparkCount; ++index) {
    PoolEntry* particle =
      allocateEntry(particles_, particleLimit, nextSerial_++);
    if (particle == nullptr) {
      ++effectsDropped_;
      break;
    }
    particle->effect = {
      TransientEffectType::MachineGunMuzzleSpark,
      request.muzzlePosition + forward * 0.03F,
      0.0F,
      0.07F,
      0.016F,
      0.004F,
      {255, 210, 112, 225},
      seed + static_cast<std::uint32_t>(index),
    };
    particle->effect.velocity =
      forward * (2.2F + seededUnit(seed, 3U) * 1.8F) +
      right * seededSigned(seed, 4U) * 0.8F +
      up * seededSigned(seed, 5U) * 0.6F;
  }

  if (tuning.quality >= 2) {
    PoolEntry* smoke =
      allocateEntry(particles_, particleLimit, nextSerial_++);
    if (smoke != nullptr) {
      smoke->effect = {
        TransientEffectType::MachineGunMuzzleSmoke,
        request.muzzlePosition + forward * 0.08F,
        0.0F,
        0.18F,
        0.055F,
        0.13F,
        {118, 120, 122, 72},
        seed,
      };
      smoke->effect.velocity = forward * 0.25F + up * 0.18F;
    }
  }

  const bool spawnCasing =
    tuning.casingsEnabled &&
    tuning.casingCountMultiplier > 0.0F &&
    seededUnit(seed, 7U) <= std::min(tuning.casingCountMultiplier, 1.0F);
  if (spawnCasing) {
    PoolEntry* casing = allocateEntry(
      casings_,
      clampedLimit(tuning.maximumCasings, kCasingCapacity),
      nextSerial_++
    );
    if (casing != nullptr) {
      casing->effect = {
        TransientEffectType::MachineGunCasing,
        request.casingEjectPosition,
        0.0F,
        std::max(0.05F, tuning.casingLifetimeSeconds),
        0.022F,
        0.022F,
        {176, 128, 48, 235},
        seed,
      };
      casing->effect.velocity =
        request.inheritedVelocity * 0.35F +
        right * (2.4F + seededUnit(seed, 8U) * 1.4F) +
        up * (1.7F + seededUnit(seed, 9U) * 1.2F) -
        forward * (0.25F + seededUnit(seed, 10U) * 0.35F);
      casing->effect.rotationRadians =
        seededUnit(seed, 11U) * 2.0F * std::numbers::pi_v<float>;
      casing->effect.angularVelocityRadiansPerSecond =
        12.0F + seededUnit(seed, 12U) * 24.0F;
    } else {
      ++effectsDropped_;
    }
  }

  if (!request.hitWorld || length(request.impactNormal) <= 0.0001F) {
    return;
  }
  spawnSurfaceImpact(
    {
      request.impactPosition,
      request.impactNormal,
      request.incomingDirection,
      request.surface,
      SurfaceImpactWeapon::MachineGun,
      request.visualSeed,
    },
    tuning
  );
}

void CombatEffects::spawnSurfaceImpact(
  const SurfaceImpactEffectsRequest& request,
  const CombatEffectsTuning& tuning
) {
  if (
    tuning.quality <= 0 ||
    !finite(request.position) ||
    !finite(request.normal) ||
    length(request.normal) <= 0.0001F
  ) {
    return;
  }
  ++surfaceImpactsSpawned_;
  const SurfaceImpactProfile profile = surfaceImpactProfile(request.weapon);
  const std::uint32_t seed = request.visualSeed;
  const Vec3 normal = safeDirection(request.normal, Vec3{0.0F, 0.0F, 1.0F});
  Vec3 tangent = normalize(cross(normal, request.incomingDirection));
  if (length(tangent) <= 0.0001F) {
    tangent = normalize(cross(normal, Vec3{0.0F, 0.0F, 1.0F}));
  }
  if (length(tangent) <= 0.0001F) {
    tangent = {1.0F, 0.0F, 0.0F};
  }
  const Vec3 bitangent = normalize(cross(normal, tangent));
  const RenderColor impact = surfaceImpactColor(request.surface, request.weapon);
  const std::size_t particleLimit = clampedLimit(
    tuning.maximumParticles,
    kParticleCapacity
  );
  const int desiredSparks = static_cast<int>(std::clamp(
    std::round(
      static_cast<float>(profile.baseSparkCount) *
      std::max(0.0F, tuning.particleMultiplier)
    ),
    0.0F,
    8.0F
  ));
  for (int index = 0; index < desiredSparks; ++index) {
    PoolEntry* particle =
      allocateEntry(particles_, particleLimit, nextSerial_++);
    if (particle == nullptr) {
      ++effectsDropped_;
      break;
    }
    const std::uint32_t lane = 20U + static_cast<std::uint32_t>(index) * 4U;
    particle->effect = {
      TransientEffectType::BulletImpactSpark,
      request.position + normal * 0.008F,
      0.0F,
      profile.sparkLifetimeSeconds + seededUnit(seed, lane) * 0.08F,
      profile.sparkInitialScale,
      profile.sparkInitialScale * 0.30F,
      impact,
      seed + static_cast<std::uint32_t>(index),
    };
    particle->effect.velocity =
      normal * (0.8F + seededUnit(seed, lane + 1U) * profile.sparkSpeed) +
      tangent * seededSigned(seed, lane + 2U) * profile.sparkSpeed * 0.62F +
      bitangent * seededSigned(seed, lane + 3U) * profile.sparkSpeed * 0.62F;
  }

  PoolEntry* flash = allocateEntry(particles_, particleLimit, nextSerial_++);
  if (flash != nullptr) {
    flash->effect = {
      TransientEffectType::BulletImpactFlash,
      request.position + normal * 0.012F,
      0.0F,
      profile.flashLifetimeSeconds,
      profile.flashInitialScale,
      profile.flashFinalScale,
      impact,
      seed,
    };
    flash->effect.normal = normal;
  } else {
    ++effectsDropped_;
  }

  const bool useDust =
    tuning.quality >= 2 &&
    profile.dust &&
    request.surface != ImpactSurfaceCategory::Metal &&
    request.surface != ImpactSurfaceCategory::Energy;
  if (useDust) {
    PoolEntry* dust = allocateEntry(particles_, particleLimit, nextSerial_++);
    if (dust != nullptr) {
      const RenderColor dustColor = request.surface == ImpactSurfaceCategory::Stone
        ? RenderColor{172, 160, 138, 100}
        : request.surface == ImpactSurfaceCategory::WoodSoft
        ? RenderColor{154, 124, 82, 82}
        : RenderColor{142, 132, 118, 82};
      dust->effect = {
        TransientEffectType::BulletImpactDust,
        request.position + normal * 0.012F,
        0.0F,
        0.32F,
        profile.dustInitialScale,
        profile.dustFinalScale,
        dustColor,
        seed,
      };
      dust->effect.velocity = normal * 0.42F +
        tangent * seededSigned(seed, 49U) * 0.12F;
    } else {
      ++effectsDropped_;
    }
  }

  if (!profile.decal) {
    return;
  }
  PoolEntry* decal = allocateEntry(
    decals_,
    clampedLimit(tuning.maximumDecals, kDecalCapacity),
    nextSerial_++
  );
  if (decal == nullptr) {
    ++effectsDropped_;
    return;
  }
  const float scale = profile.decalScale * (0.88F + seededUnit(seed, 52U) * 0.34F);
  decal->effect = {
    TransientEffectType::BulletDecal,
    request.position + normal * 0.0025F,
    0.0F,
    std::max(0.05F, tuning.decalLifetimeSeconds),
    scale,
    scale * 1.08F,
    surfaceImpactDecalColor(request.surface, request.weapon),
    seed,
  };
  decal->effect.normal = normal;
  decal->effect.direction = tangent;
  decal->effect.rotationRadians =
    seededUnit(seed, 53U) * 2.0F * std::numbers::pi_v<float>;
}

void CombatEffects::spawnFreezeGunPulse(
  const FreezeGunPulseEffectsRequest& request,
  const CombatEffectsTuning& tuning
) {
  if (
    tuning.quality <= 0 ||
    request.ownerIndex >= kDuelPlayerCount ||
    !finite(request.muzzlePosition)
  ) {
    return;
  }
  ++freezePulsesSpawned_;
  const std::uint32_t seed = request.visualSeed;
  const Vec3 forward = safeDirection(
    request.muzzleForward,
    Vec3{1.0F, 0.0F, 0.0F}
  );
  PoolEntry* light = allocateEntry(lights_, kLightCapacity, nextSerial_++);
  if (light != nullptr) {
    light->effect = {
      TransientEffectType::MachineGunMuzzleLight,
      request.muzzlePosition,
      0.0F,
      std::clamp(tuning.muzzleLightDurationSeconds * 0.58F, 0.035F, 0.085F),
      1.0F,
      1.0F,
      {158, 238, 255, 255},
      seed,
    };
    light->effect.intensity =
      std::max(0.0F, tuning.muzzleLightIntensity) * 0.58F;
    light->effect.radius = std::max(0.0F, tuning.muzzleLightRadius) * 0.62F;
    light->effect.ownerIndex = request.ownerIndex;
    light->attachment = MuzzleAttachment::FreezeGun;
  } else {
    ++effectsDropped_;
  }
  PoolEntry* core = allocateEntry(
    particles_,
    clampedLimit(tuning.maximumParticles, kParticleCapacity),
    nextSerial_++
  );
  if (core != nullptr) {
    core->effect = {
      TransientEffectType::BulletImpactFlash,
      request.muzzlePosition + forward * 0.035F,
      0.0F,
      0.070F,
      0.055F,
      0.020F,
      {214, 251, 255, 245},
      seed,
    };
    core->effect.direction = forward;
  } else {
    ++effectsDropped_;
  }
  if (!request.hitWorld) {
    return;
  }
  spawnSurfaceImpact(
    {
      request.impactPosition,
      request.impactNormal,
      request.incomingDirection,
      request.surface,
      SurfaceImpactWeapon::FreezeGun,
      seed,
    },
    tuning
  );
}

void CombatEffects::spawnRocketLauncherShot(
  const RocketLauncherShotEffectsRequest& request,
  const CombatEffectsTuning& tuning
) {
  if (
    tuning.quality <= 0 ||
    request.ownerIndex >= kDuelPlayerCount ||
    !finite(request.muzzlePosition)
  ) {
    return;
  }
  ++shotsSpawned_;
  ++rocketShotsSpawned_;
  const std::uint32_t seed = request.visualSeed;
  const Vec3 forward = safeDirection(
    request.muzzleForward,
    Vec3{1.0F, 0.0F, 0.0F}
  );
  const Vec3 up = safeDirection(request.muzzleUp, Vec3{0.0F, 0.0F, 1.0F});

  PoolEntry* light = allocateEntry(
    lights_,
    kLightCapacity,
    nextSerial_++
  );
  if (light != nullptr) {
    const float qualityScale = tuning.quality >= 2 ? 0.82F : 0.62F;
    light->effect = {
      TransientEffectType::RocketLauncherMuzzleLight,
      request.muzzlePosition,
      0.0F,
      std::clamp(tuning.muzzleLightDurationSeconds * 0.52F, 0.001F, 0.070F),
      1.0F,
      1.0F,
      {255, 194, 96, 255},
      seed,
    };
    light->effect.intensity =
      std::max(0.0F, tuning.muzzleLightIntensity) * qualityScale;
    light->effect.radius =
      std::max(0.0F, tuning.muzzleLightRadius) * 0.72F;
    light->effect.ownerIndex = request.ownerIndex;
    light->attachment = MuzzleAttachment::RocketLauncher;
    light->expiryGraceState = 1U;
  }

  if (tuning.quality < 2) {
    return;
  }
  PoolEntry* smoke = allocateEntry(
    particles_,
    clampedLimit(tuning.maximumParticles, kParticleCapacity),
    nextSerial_++
  );
  if (smoke == nullptr) {
    ++effectsDropped_;
    return;
  }
  smoke->effect = {
    TransientEffectType::RocketLauncherMuzzleSmoke,
    request.muzzlePosition + forward * 0.055F,
    0.0F,
    0.17F,
    0.045F,
    0.115F,
    {112, 116, 118, 62},
    seed,
  };
  smoke->effect.velocity =
    forward * (0.30F + seededUnit(seed, 61U) * 0.12F) +
    up * (0.08F + seededUnit(seed, 62U) * 0.08F);
  smoke->expiryGraceState = 1U;
}

void CombatEffects::spawnRocketExplosion(
  const RocketExplosionEffectsRequest& request,
  const CombatEffectsTuning& tuning
) {
  if (tuning.quality < 2 || !finite(request.position)) {
    return;
  }
  ++rocketExplosionsSpawned_;
  const std::uint32_t seed = request.visualSeed;
  const float radius = std::isfinite(request.radius)
    ? std::clamp(request.radius, 0.25F, 3.6F)
    : 3.0F;
  const std::size_t particleLimit = clampedLimit(
    tuning.maximumParticles,
    kParticleCapacity
  );
  const int shardCount = static_cast<int>(std::clamp(
    std::round(3.0F * std::max(0.0F, tuning.particleMultiplier)),
    0.0F,
    4.0F
  ));
  for (int index = 0; index < shardCount; ++index) {
    PoolEntry* shard =
      allocateEntry(particles_, particleLimit, nextSerial_++);
    if (shard == nullptr) {
      ++effectsDropped_;
      break;
    }
    const std::uint32_t lane = 80U + static_cast<std::uint32_t>(index) * 4U;
    const float azimuth = seededUnit(seed, lane) *
      2.0F * std::numbers::pi_v<float>;
    const float vertical = std::clamp(
      0.28F + seededSigned(seed, lane + 1U) * 0.52F,
      -0.24F,
      0.82F
    );
    const float horizontal =
      std::sqrt(std::max(0.0F, 1.0F - vertical * vertical));
    const Vec3 direction = {
      std::cos(azimuth) * horizontal,
      std::sin(azimuth) * horizontal,
      vertical,
    };
    shard->effect = {
      TransientEffectType::RocketExplosionShard,
      request.position + direction * (radius * 0.035F),
      0.0F,
      0.13F + seededUnit(seed, lane + 2U) * 0.07F,
      0.020F,
      0.005F,
      {248, 126, 72, 190},
      seed + static_cast<std::uint32_t>(index),
    };
    shard->effect.velocity =
      direction * (2.0F + seededUnit(seed, lane + 3U) * 2.4F);
    shard->expiryGraceState = 1U;
  }

  PoolEntry* smoke =
    allocateEntry(particles_, particleLimit, nextSerial_++);
  if (smoke == nullptr) {
    ++effectsDropped_;
    return;
  }
  const float smokeScale = std::clamp(radius * 0.035F, 0.06F, 0.13F);
  smoke->effect = {
    TransientEffectType::RocketExplosionSmoke,
    request.position + Vec3{0.0F, 0.0F, radius * 0.025F},
    0.0F,
    0.27F,
    smokeScale,
    smokeScale * 2.05F,
    {104, 108, 110, 58},
    seed + 9U,
  };
  smoke->effect.velocity = {
    seededSigned(seed, 101U) * 0.12F,
    seededSigned(seed, 102U) * 0.12F,
    0.30F + seededUnit(seed, 103U) * 0.12F,
  };
  smoke->expiryGraceState = 1U;
}

void CombatEffects::setMuzzleAttachment(
  std::uint8_t ownerIndex,
  Vec3 position
) {
  setMuzzleAttachment(ownerIndex, MuzzleAttachment::MachineGun, position);
}

void CombatEffects::setMuzzleAttachment(
  std::uint8_t ownerIndex,
  MuzzleAttachment attachment,
  Vec3 position
) {
  const std::size_t attachmentIndex =
    static_cast<std::size_t>(attachment);
  if (
    ownerIndex >= kDuelPlayerCount ||
    attachmentIndex >= muzzleAttachments_.size() ||
    !finite(position)
  ) {
    return;
  }
  muzzleAttachments_[attachmentIndex][ownerIndex] = position;
  hasMuzzleAttachment_[attachmentIndex][ownerIndex] = true;
}

void CombatEffects::appendActive(
  std::vector<TransientEffect>& destination
) const {
  const auto appendPool = [&destination](const auto& pool) {
    for (const PoolEntry& entry : pool) {
      if (entry.active) {
        destination.push_back(entry.effect);
      }
    }
  };
  appendPool(lights_);
  appendPool(casings_);
  appendPool(particles_);
  appendPool(decals_);
}

CombatEffectsStats CombatEffects::stats() const {
  CombatEffectsStats result = peaks_;
  result.activeLights = static_cast<std::uint32_t>(activeCount(lights_));
  result.activeCasings = static_cast<std::uint32_t>(activeCount(casings_));
  result.activeParticles = static_cast<std::uint32_t>(activeCount(particles_));
  result.activeDecals = static_cast<std::uint32_t>(activeCount(decals_));
  result.shotsSpawned = shotsSpawned_;
  result.surfaceImpactsSpawned = surfaceImpactsSpawned_;
  result.freezePulsesSpawned = freezePulsesSpawned_;
  result.rocketShotsSpawned = rocketShotsSpawned_;
  result.rocketExplosionsSpawned = rocketExplosionsSpawned_;
  result.effectsDropped = effectsDropped_;
  return result;
}

} // namespace lg
