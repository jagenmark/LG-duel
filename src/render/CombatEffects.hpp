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
  WoodSoft,
  Energy,
};

// This stays deliberately small. Weapon code sends a weapon intent and a
// preclassified broad surface, while this fixed-pool system owns the compact
// response. It never reads texture names or changes combat data.
enum class SurfaceImpactWeapon : std::uint8_t {
  MachineGun = 0,
  Shotgun,
  Precision,
  Revolver,
  FreezeGun,
};

enum class MuzzleAttachment : std::uint8_t {
  MachineGun = 0,
  RocketLauncher,
  FreezeGun,
  Count,
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

struct CombatEffectPulseTimerAdvance {
  bool pulseDue = false;
  float remainingSeconds = 0.0F;
};

// Advances a short presentation cadence without losing elapsed time. One call
// yields at most one pulse, even when a slow frame crosses several intervals.
[[nodiscard]] CombatEffectPulseTimerAdvance advanceCombatEffectPulseTimer(
  float remainingSeconds,
  float deltaSeconds,
  float intervalSeconds
);

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

struct SurfaceImpactEffectsRequest {
  Vec3 position = {};
  Vec3 normal = {};
  Vec3 incomingDirection = {1.0F, 0.0F, 0.0F};
  ImpactSurfaceCategory surface = ImpactSurfaceCategory::GenericHard;
  SurfaceImpactWeapon weapon = SurfaceImpactWeapon::MachineGun;
  std::uint32_t visualSeed = 0;
};

struct FreezeGunPulseEffectsRequest {
  Vec3 muzzlePosition = {};
  Vec3 muzzleForward = {1.0F, 0.0F, 0.0F};
  Vec3 impactPosition = {};
  Vec3 impactNormal = {};
  Vec3 incomingDirection = {1.0F, 0.0F, 0.0F};
  ImpactSurfaceCategory surface = ImpactSurfaceCategory::GenericHard;
  std::uint32_t visualSeed = 0;
  std::uint8_t ownerIndex = 0;
  bool hitWorld = false;
};

struct RocketLauncherShotEffectsRequest {
  Vec3 muzzlePosition = {};
  Vec3 muzzleForward = {1.0F, 0.0F, 0.0F};
  Vec3 muzzleUp = {0.0F, 0.0F, 1.0F};
  std::uint32_t visualSeed = 0;
  std::uint8_t ownerIndex = 0;
};

struct RocketExplosionEffectsRequest {
  Vec3 position = {};
  float radius = 3.0F;
  std::uint32_t visualSeed = 0;
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
  std::uint64_t surfaceImpactsSpawned = 0;
  std::uint64_t freezePulsesSpawned = 0;
  std::uint64_t rocketShotsSpawned = 0;
  std::uint64_t rocketExplosionsSpawned = 0;
  std::uint64_t effectsDropped = 0;
};

// Accepted combat events repeat across snapshot frames. This fixed history
// keeps presentation work at one spawn per event without growing containers.
class CombatEffectEventHistory {
public:
  static constexpr std::size_t kWeaponFireCapacity = 64;
  static constexpr std::size_t kExplosionCapacity = 64;

  void clear();
  [[nodiscard]] bool acceptWeaponFire(
    std::uint8_t playerIndex,
    Weapon weapon,
    std::uint32_t visualSeed
  );
  [[nodiscard]] bool acceptExplosion(
    std::uint8_t ownerIndex,
    std::uint32_t sequence
  );

private:
  struct WeaponFireEvent {
    std::uint8_t playerIndex = 0;
    Weapon weapon = Weapon::LightningGun;
    std::uint32_t visualSeed = 0;
    bool active = false;
  };

  struct ExplosionEvent {
    std::uint8_t ownerIndex = 0;
    std::uint32_t sequence = 0;
    bool active = false;
  };

  std::array<WeaponFireEvent, kWeaponFireCapacity> weaponFires_ = {};
  std::array<ExplosionEvent, kExplosionCapacity> explosions_ = {};
  std::array<bool, kDuelPlayerCount> hasLastExplosionSequence_ = {};
  std::array<std::uint32_t, kDuelPlayerCount> lastExplosionSequence_ = {};
  std::size_t nextWeaponFire_ = 0;
  std::size_t nextExplosion_ = 0;
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
  void spawnSurfaceImpact(
    const SurfaceImpactEffectsRequest& request,
    const CombatEffectsTuning& tuning
  );
  void spawnFreezeGunPulse(
    const FreezeGunPulseEffectsRequest& request,
    const CombatEffectsTuning& tuning
  );
  void spawnRocketLauncherShot(
    const RocketLauncherShotEffectsRequest& request,
    const CombatEffectsTuning& tuning
  );
  void spawnRocketExplosion(
    const RocketExplosionEffectsRequest& request,
    const CombatEffectsTuning& tuning
  );
  void setMuzzleAttachment(std::uint8_t ownerIndex, Vec3 position);
  void setMuzzleAttachment(
    std::uint8_t ownerIndex,
    MuzzleAttachment attachment,
    Vec3 position
  );
  void appendActive(std::vector<TransientEffect>& destination) const;

  [[nodiscard]] CombatEffectsStats stats() const;

  // Public only so fixed-pool helpers can stay small and allocation-free.
  // Callers still interact through the typed methods above.
  struct PoolEntry {
    TransientEffect effect = {};
    std::uint64_t serial = 0;
    MuzzleAttachment attachment = MuzzleAttachment::MachineGun;
    std::uint8_t expiryGraceState = 0;
    bool active = false;
  };

  template <std::size_t Capacity>
  using Pool = std::array<PoolEntry, Capacity>;

private:
  Pool<kLightCapacity> lights_ = {};
  Pool<kCasingCapacity> casings_ = {};
  Pool<kParticleCapacity> particles_ = {};
  Pool<kDecalCapacity> decals_ = {};
  std::array<
    std::array<Vec3, kDuelPlayerCount>,
    static_cast<std::size_t>(MuzzleAttachment::Count)
  > muzzleAttachments_ = {};
  std::array<
    std::array<bool, kDuelPlayerCount>,
    static_cast<std::size_t>(MuzzleAttachment::Count)
  > hasMuzzleAttachment_ = {};
  std::uint64_t nextSerial_ = 1;
  std::uint64_t shotsSpawned_ = 0;
  std::uint64_t surfaceImpactsSpawned_ = 0;
  std::uint64_t freezePulsesSpawned_ = 0;
  std::uint64_t rocketShotsSpawned_ = 0;
  std::uint64_t rocketExplosionsSpawned_ = 0;
  std::uint64_t effectsDropped_ = 0;
  CombatEffectsStats peaks_ = {};
};

} // namespace lg
