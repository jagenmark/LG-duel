#include "render/CombatEffects.hpp"

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
    TransientEffect& effect = entry.effect;
    effect.ageSeconds += deltaSeconds;
    if (
      effect.lifetimeSeconds <= 0.0F ||
      effect.ageSeconds >= effect.lifetimeSeconds
    ) {
      entry.active = false;
      continue;
    }
    if (
      effect.type == TransientEffectType::MachineGunCasing ||
      effect.type == TransientEffectType::BulletImpactSpark
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
      effect.type == TransientEffectType::MachineGunMuzzleSmoke
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
  case ImpactSurfaceCategory::Energy:
    return {104, 214, 255, 235};
  case ImpactSurfaceCategory::GenericHard:
    return {244, 186, 94, 215};
  }
  return {244, 186, 94, 215};
}

} // namespace

void CombatEffects::clear() {
  lights_ = {};
  casings_ = {};
  particles_ = {};
  decals_ = {};
  muzzleAttachments_ = {};
  hasMuzzleAttachment_ = {};
  nextSerial_ = 1;
  shotsSpawned_ = 0;
  effectsDropped_ = 0;
  peaks_ = {};
}

void CombatEffects::update(
  float deltaSeconds,
  const CombatEffectsTuning& tuning
) {
  const float dt = std::clamp(deltaSeconds, 0.0F, 0.25F);
  if (tuning.quality <= 0) {
    lights_ = {};
    casings_ = {};
    particles_ = {};
    decals_ = {};
    return;
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
    if (
      light.active &&
      light.effect.ownerIndex < hasMuzzleAttachment_.size() &&
      hasMuzzleAttachment_[light.effect.ownerIndex]
    ) {
      // A muzzle light stays on the current socket during its short life, so
      // sway, recoil, remote pose changes, and frame rate cannot detach it.
      light.effect.position = muzzleAttachments_[light.effect.ownerIndex];
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

  const Vec3 normal = normalize(request.impactNormal);
  Vec3 tangent = normalize(cross(normal, request.incomingDirection));
  if (length(tangent) <= 0.0001F) {
    tangent = normalize(cross(normal, Vec3{0.0F, 0.0F, 1.0F}));
  }
  if (length(tangent) <= 0.0001F) {
    tangent = {1.0F, 0.0F, 0.0F};
  }
  const Vec3 bitangent = normalize(cross(normal, tangent));
  const RenderColor sparkColor = impactColor(request.surface);
  const int desiredSparks = static_cast<int>(std::clamp(
    std::round(3.0F * std::max(0.0F, tuning.particleMultiplier)),
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
      request.impactPosition + normal * 0.008F,
      0.0F,
      0.16F + seededUnit(seed, lane) * 0.12F,
      0.014F,
      0.004F,
      sparkColor,
      seed + static_cast<std::uint32_t>(index),
    };
    particle->effect.velocity =
      normal * (1.1F + seededUnit(seed, lane + 1U) * 2.2F) +
      tangent * seededSigned(seed, lane + 2U) * 1.6F +
      bitangent * seededSigned(seed, lane + 3U) * 1.6F;
  }

  PoolEntry* flash =
    allocateEntry(particles_, particleLimit, nextSerial_++);
  if (flash != nullptr) {
    flash->effect = {
      TransientEffectType::BulletImpactFlash,
      request.impactPosition + normal * 0.012F,
      0.0F,
      0.055F,
      0.07F,
      0.02F,
      sparkColor,
      seed,
    };
    flash->effect.normal = normal;
  }

  if (tuning.quality >= 2 && request.surface != ImpactSurfaceCategory::Metal) {
    PoolEntry* dust =
      allocateEntry(particles_, particleLimit, nextSerial_++);
    if (dust != nullptr) {
      dust->effect = {
        TransientEffectType::BulletImpactDust,
        request.impactPosition + normal * 0.012F,
        0.0F,
        0.34F,
        0.045F,
        0.16F,
        request.surface == ImpactSurfaceCategory::Stone
          ? RenderColor{172, 160, 138, 100}
          : RenderColor{142, 132, 118, 82},
        seed,
      };
      dust->effect.velocity = normal * 0.42F +
        tangent * seededSigned(seed, 49U) * 0.12F;
    }
  }

  PoolEntry* decal = allocateEntry(
    decals_,
    clampedLimit(tuning.maximumDecals, kDecalCapacity),
    nextSerial_++
  );
  if (decal != nullptr) {
    const float scale = 0.032F + seededUnit(seed, 52U) * 0.014F;
    decal->effect = {
      TransientEffectType::BulletDecal,
      request.impactPosition + normal * 0.0025F,
      0.0F,
      std::max(0.05F, tuning.decalLifetimeSeconds),
      scale,
      scale * 1.08F,
      {48, 42, 36, 190},
      seed,
    };
    decal->effect.normal = normal;
    decal->effect.direction = tangent;
    decal->effect.rotationRadians =
      seededUnit(seed, 53U) * 2.0F * std::numbers::pi_v<float>;
  }
}

void CombatEffects::setMuzzleAttachment(
  std::uint8_t ownerIndex,
  Vec3 position
) {
  if (ownerIndex >= muzzleAttachments_.size()) {
    return;
  }
  muzzleAttachments_[ownerIndex] = position;
  hasMuzzleAttachment_[ownerIndex] = true;
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
  result.effectsDropped = effectsDropped_;
  return result;
}

} // namespace lg
