#pragma once

#include "render/Renderer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace lg {

enum class ImpactSurfaceCategory : std::uint8_t {
  GenericHard = 0,
  Metal,
  Stone,
  Energy,
};

struct CombatEffectsTuning {
  int quality = 2;
  float muzzleLightIntensity = 2.4F;
  float muzzleLightRadius = 3.2F;
  float muzzleLightDurationSeconds = 0.13F;
  bool casingsEnabled = true;
  float casingCountMultiplier = 1.0F;
  float casingLifetimeSeconds = 2.4F;
  std::size_t maximumCasings = 48;
  float particleMultiplier = 1.0F;
  std::size_t maximumParticles = 192;
  std::size_t maximumDecals = 128;
  float decalLifetimeSeconds = 24.0F;
};

struct MachineGunShotEffectsRequest {
  Vec3 muzzlePosition = {};
  Vec3 muzzleForward = {1.0F, 0.0F, 0.0F};
  Vec3 muzzleRight = {0.0F, 1.0F, 0.0F};
  Vec3 muzzleUp = {0.0F, 0.0F, 1.0F};
  Vec3 casingEjectPosition = {};
  Vec3 inheritedVelocity = {};
  Vec3 impactPosition = {};
  Vec3 impactNormal = {};
  Vec3 incomingDirection = {1.0F, 0.0F, 0.0F};
  ImpactSurfaceCategory surface = ImpactSurfaceCategory::GenericHard;
  std::uint32_t visualSeed = 0;
  std::uint8_t ownerIndex = 0;
  bool hitWorld = false;
};

struct CombatEffectsStats {
  std::uint32_t activeLights = 0;
  std::uint32_t activeCasings = 0;
  std::uint32_t activeParticles = 0;
  std::uint32_t activeDecals = 0;
  std::uint32_t peakLights = 0;
  std::uint32_t peakCasings = 0;
  std::uint32_t peakParticles = 0;
  std::uint32_t peakDecals = 0;
  std::uint64_t shotsSpawned = 0;
  std::uint64_t effectsDropped = 0;
};

// Fixed pools keep automatic fire free of per-shot heap work. This system owns
// presentation only; its data never feeds damage, traces, or player movement.
class CombatEffects {
public:
  static constexpr std::size_t kLightCapacity = 16;
  static constexpr std::size_t kCasingCapacity = 96;
  static constexpr std::size_t kParticleCapacity = 384;
  static constexpr std::size_t kDecalCapacity = 256;

  void clear();
  void update(float deltaSeconds, const CombatEffectsTuning& tuning);
  void spawnMachineGunShot(
    const MachineGunShotEffectsRequest& request,
    const CombatEffectsTuning& tuning
  );
  void setMuzzleAttachment(std::uint8_t ownerIndex, Vec3 position);
  void appendActive(std::vector<TransientEffect>& destination) const;

  [[nodiscard]] CombatEffectsStats stats() const;

  // Public only so fixed-pool helpers can stay small and allocation-free.
  // Callers still interact through the typed methods above.
  struct PoolEntry {
    TransientEffect effect = {};
    std::uint64_t serial = 0;
    bool active = false;
  };

  template <std::size_t Capacity>
  using Pool = std::array<PoolEntry, Capacity>;

private:
  Pool<kLightCapacity> lights_ = {};
  Pool<kCasingCapacity> casings_ = {};
  Pool<kParticleCapacity> particles_ = {};
  Pool<kDecalCapacity> decals_ = {};
  std::array<Vec3, kDuelPlayerCount> muzzleAttachments_ = {};
  std::array<bool, kDuelPlayerCount> hasMuzzleAttachment_ = {};
  std::uint64_t nextSerial_ = 1;
  std::uint64_t shotsSpawned_ = 0;
  std::uint64_t effectsDropped_ = 0;
  CombatEffectsStats peaks_ = {};
};

} // namespace lg
