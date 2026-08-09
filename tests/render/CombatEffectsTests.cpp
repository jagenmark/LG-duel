#include "render/CombatEffects.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) {
    return 0;
  }
  std::cerr << "FAIL: " << message << '\n';
  return 1;
}

lg::MachineGunShotEffectsRequest shot(std::uint32_t seed) {
  lg::MachineGunShotEffectsRequest request;
  request.muzzlePosition = {1.0F, 2.0F, 3.0F};
  request.muzzleForward = {1.0F, 0.0F, 0.0F};
  request.muzzleRight = {0.0F, 1.0F, 0.0F};
  request.muzzleUp = {0.0F, 0.0F, 1.0F};
  request.casingEjectPosition = {0.5F, 2.2F, 3.1F};
  request.impactPosition = {8.0F, 2.0F, 3.0F};
  request.impactNormal = {-1.0F, 0.0F, 0.0F};
  request.incomingDirection = {1.0F, 0.0F, 0.0F};
  request.visualSeed = seed;
  request.hitWorld = true;
  return request;
}

lg::RocketLauncherShotEffectsRequest rocketShot(std::uint32_t seed) {
  lg::RocketLauncherShotEffectsRequest request;
  request.muzzlePosition = {4.0F, 5.0F, 6.0F};
  request.muzzleForward = {1.0F, 0.0F, 0.0F};
  request.muzzleUp = {0.0F, 0.0F, 1.0F};
  request.visualSeed = seed;
  request.ownerIndex = 0;
  return request;
}

const lg::TransientEffect* findEffect(
  const std::vector<lg::TransientEffect>& effects,
  lg::TransientEffectType type
) {
  const auto found = std::find_if(
    effects.begin(),
    effects.end(),
    [type](const lg::TransientEffect& effect) {
      return effect.type == type;
    }
  );
  return found == effects.end() ? nullptr : &*found;
}

std::size_t countEffects(
  const std::vector<lg::TransientEffect>& effects,
  lg::TransientEffectType type
) {
  return static_cast<std::size_t>(std::count_if(
    effects.begin(),
    effects.end(),
    [type](const lg::TransientEffect& effect) {
      return effect.type == type;
    }
  ));
}

bool sameEffect(
  const lg::TransientEffect& lhs,
  const lg::TransientEffect& rhs
) {
  return lhs.type == rhs.type &&
    lhs.seed == rhs.seed &&
    lhs.position.x == rhs.position.x &&
    lhs.position.y == rhs.position.y &&
    lhs.position.z == rhs.position.z &&
    lhs.velocity.x == rhs.velocity.x &&
    lhs.velocity.y == rhs.velocity.y &&
    lhs.velocity.z == rhs.velocity.z &&
    lhs.lifetimeSeconds == rhs.lifetimeSeconds &&
    lhs.initialScale == rhs.initialScale &&
    lhs.finalScale == rhs.finalScale;
}

} // namespace

int main() {
  int failures = 0;
  lg::CombatEffects effects;
  lg::CombatEffectsTuning tuning;
  tuning.maximumCasings = 3;
  tuning.maximumParticles = 7;
  tuning.maximumDecals = 4;
  tuning.casingLifetimeSeconds = 0.2F;
  tuning.decalLifetimeSeconds = 0.3F;

  const lg::CombatEffectPulseTimerAdvance firstFreezePulse =
    lg::advanceCombatEffectPulseTimer(0.0F, 0.03F, 0.10F);
  const lg::CombatEffectPulseTimerAdvance slowFreezePulse =
    lg::advanceCombatEffectPulseTimer(0.02F, 0.25F, 0.10F);
  const lg::CombatEffectPulseTimerAdvance heldFreezePulse =
    lg::advanceCombatEffectPulseTimer(
      slowFreezePulse.remainingSeconds,
      0.06F,
      0.10F
    );
  const lg::CombatEffectPulseTimerAdvance nextFreezePulse =
    lg::advanceCombatEffectPulseTimer(
      heldFreezePulse.remainingSeconds,
      0.02F,
      0.10F
    );
  failures += expect(
    firstFreezePulse.pulseDue &&
      std::fabs(firstFreezePulse.remainingSeconds - 0.10F) < 0.0001F &&
      slowFreezePulse.pulseDue &&
      std::fabs(slowFreezePulse.remainingSeconds - 0.07F) < 0.0001F &&
      !heldFreezePulse.pulseDue &&
      std::fabs(heldFreezePulse.remainingSeconds - 0.01F) < 0.0001F &&
      nextFreezePulse.pulseDue &&
      std::fabs(nextFreezePulse.remainingSeconds - 0.09F) < 0.0001F,
    "freeze pulse cadence should preserve slow-frame elapsed time with one pulse per frame"
  );

  for (std::uint32_t seed = 1; seed <= 64; ++seed) {
    effects.spawnMachineGunShot(shot(seed), tuning);
  }
  lg::CombatEffectsStats stats = effects.stats();
  failures += expect(
    stats.activeLights <= lg::CombatEffects::kLightCapacity &&
      stats.activeCasings <= tuning.maximumCasings &&
      stats.activeParticles <= tuning.maximumParticles &&
      stats.activeDecals <= tuning.maximumDecals,
    "fixed effect pools must honor each configured limit"
  );
  failures += expect(
    stats.activeDecals == tuning.maximumDecals,
    "decal budget should recycle predictably under sustained fire"
  );

  lg::CombatEffects metalEffects;
  lg::MachineGunShotEffectsRequest metalShot = shot(101U);
  metalShot.surface = lg::ImpactSurfaceCategory::Metal;
  metalEffects.spawnMachineGunShot(metalShot, tuning);
  std::vector<lg::TransientEffect> metalActive;
  metalEffects.appendActive(metalActive);
  lg::CombatEffects stoneEffects;
  lg::MachineGunShotEffectsRequest stoneShot = shot(101U);
  stoneShot.surface = lg::ImpactSurfaceCategory::Stone;
  stoneEffects.spawnMachineGunShot(stoneShot, tuning);
  std::vector<lg::TransientEffect> stoneActive;
  stoneEffects.appendActive(stoneActive);
  const auto hasDust = [](const std::vector<lg::TransientEffect>& active) {
    return std::any_of(
      active.begin(),
      active.end(),
      [](const lg::TransientEffect& effect) {
        return effect.type == lg::TransientEffectType::BulletImpactDust;
      }
    );
  };
  failures += expect(
    !hasDust(metalActive) && hasDust(stoneActive),
    "metal and stone impacts should keep their distinct effect variants"
  );
  lg::CombatEffects variantPoolEffects;
  for (lg::ImpactSurfaceCategory category : {
    lg::ImpactSurfaceCategory::Metal,
    lg::ImpactSurfaceCategory::Stone,
    lg::ImpactSurfaceCategory::WoodSoft,
    lg::ImpactSurfaceCategory::Energy,
    lg::ImpactSurfaceCategory::GenericHard,
  }) {
    lg::MachineGunShotEffectsRequest categoryShot = shot(
      200U + static_cast<std::uint32_t>(category)
    );
    categoryShot.surface = category;
    variantPoolEffects.spawnMachineGunShot(categoryShot, tuning);
  }
  stats = variantPoolEffects.stats();
  failures += expect(
    stats.activeParticles <= tuning.maximumParticles &&
      stats.activeDecals <= tuning.maximumDecals,
    "all impact surface variants should honor the shared pool caps"
  );

  lg::SurfaceImpactEffectsRequest shotgunImpact;
  shotgunImpact.position = {8.0F, 2.0F, 3.0F};
  shotgunImpact.normal = {-1.0F, 0.0F, 0.0F};
  shotgunImpact.incomingDirection = {1.0F, 0.0F, 0.0F};
  shotgunImpact.surface = lg::ImpactSurfaceCategory::WoodSoft;
  shotgunImpact.weapon = lg::SurfaceImpactWeapon::Shotgun;
  shotgunImpact.visualSeed = 301U;
  lg::CombatEffects shotgunImpactEffects;
  shotgunImpactEffects.spawnSurfaceImpact(shotgunImpact, tuning);
  std::vector<lg::TransientEffect> shotgunImpactActive;
  shotgunImpactEffects.appendActive(shotgunImpactActive);
  failures += expect(
    shotgunImpactEffects.stats().surfaceImpactsSpawned == 1U &&
      shotgunImpactEffects.stats().activeParticles <= tuning.maximumParticles &&
      shotgunImpactEffects.stats().activeDecals == 1U,
    "one shotgun request should create one bounded shared surface response"
  );

  lg::SurfaceImpactEffectsRequest tierImpact = shotgunImpact;
  tierImpact.weapon = lg::SurfaceImpactWeapon::MachineGun;
  tierImpact.surface = lg::ImpactSurfaceCategory::Stone;
  lg::CombatEffectsTuning impactQualityZero = tuning;
  impactQualityZero.quality = 0;
  lg::CombatEffects qualityZeroEffects;
  qualityZeroEffects.spawnSurfaceImpact(tierImpact, impactQualityZero);
  lg::CombatEffectsTuning impactQualityOne = tuning;
  impactQualityOne.quality = 1;
  lg::CombatEffects qualityOneEffects;
  qualityOneEffects.spawnSurfaceImpact(tierImpact, impactQualityOne);
  std::vector<lg::TransientEffect> qualityOneActive;
  qualityOneEffects.appendActive(qualityOneActive);
  lg::CombatEffects qualityTwoEffects;
  qualityTwoEffects.spawnSurfaceImpact(tierImpact, tuning);
  std::vector<lg::TransientEffect> qualityTwoActive;
  qualityTwoEffects.appendActive(qualityTwoActive);
  failures += expect(
    qualityZeroEffects.stats().surfaceImpactsSpawned == 0U &&
      qualityZeroEffects.stats().activeParticles == 0U &&
      qualityZeroEffects.stats().activeDecals == 0U &&
      countEffects(
        qualityOneActive,
        lg::TransientEffectType::BulletImpactFlash
      ) == 1U &&
      countEffects(
        qualityOneActive,
        lg::TransientEffectType::BulletImpactSpark
      ) == 0U &&
      countEffects(
        qualityOneActive,
        lg::TransientEffectType::BulletImpactDust
      ) == 0U &&
      qualityOneEffects.stats().activeDecals == 1U &&
      countEffects(
        qualityTwoActive,
        lg::TransientEffectType::BulletImpactSpark
      ) > 0U &&
      countEffects(
        qualityTwoActive,
        lg::TransientEffectType::BulletImpactDust
      ) == 1U,
    "surface tiers should progress from no work to core and decal to particles"
  );

  lg::CombatEffects energyImpactEffects;
  tierImpact.surface = lg::ImpactSurfaceCategory::Energy;
  energyImpactEffects.spawnSurfaceImpact(tierImpact, tuning);
  std::vector<lg::TransientEffect> energyImpactActive;
  energyImpactEffects.appendActive(energyImpactActive);
  failures += expect(
    countEffects(
      energyImpactActive,
      lg::TransientEffectType::BulletImpactDust
    ) == 0U,
    "energy surfaces should not create opaque impact dust"
  );

  lg::CombatEffects machineGunContactEffects;
  lg::CombatEffects revolverContactEffects;
  tierImpact.surface = lg::ImpactSurfaceCategory::GenericHard;
  tierImpact.weapon = lg::SurfaceImpactWeapon::MachineGun;
  machineGunContactEffects.spawnSurfaceImpact(tierImpact, impactQualityOne);
  tierImpact.weapon = lg::SurfaceImpactWeapon::Revolver;
  revolverContactEffects.spawnSurfaceImpact(tierImpact, impactQualityOne);
  std::vector<lg::TransientEffect> machineGunContactActive;
  std::vector<lg::TransientEffect> revolverContactActive;
  machineGunContactEffects.appendActive(machineGunContactActive);
  revolverContactEffects.appendActive(revolverContactActive);
  const lg::TransientEffect* machineGunFlash = findEffect(
    machineGunContactActive,
    lg::TransientEffectType::BulletImpactFlash
  );
  const lg::TransientEffect* revolverFlash = findEffect(
    revolverContactActive,
    lg::TransientEffectType::BulletImpactFlash
  );
  const lg::TransientEffect* machineGunDecal = findEffect(
    machineGunContactActive,
    lg::TransientEffectType::BulletDecal
  );
  const lg::TransientEffect* revolverDecal = findEffect(
    revolverContactActive,
    lg::TransientEffectType::BulletDecal
  );
  failures += expect(
    machineGunFlash != nullptr &&
      revolverFlash != nullptr &&
      machineGunDecal != nullptr &&
      revolverDecal != nullptr &&
      revolverFlash->initialScale > machineGunFlash->initialScale &&
      revolverFlash->lifetimeSeconds > machineGunFlash->lifetimeSeconds &&
      revolverDecal->initialScale > machineGunDecal->initialScale,
    "revolver contacts should read heavier than machine-gun contacts"
  );

  lg::CombatEffects precisionFirstEffects;
  lg::CombatEffects precisionRepeatEffects;
  shotgunImpact.weapon = lg::SurfaceImpactWeapon::Precision;
  shotgunImpact.visualSeed = 302U;
  precisionFirstEffects.spawnSurfaceImpact(shotgunImpact, tuning);
  precisionRepeatEffects.spawnSurfaceImpact(shotgunImpact, tuning);
  std::vector<lg::TransientEffect> precisionFirst;
  std::vector<lg::TransientEffect> precisionRepeat;
  precisionFirstEffects.appendActive(precisionFirst);
  precisionRepeatEffects.appendActive(precisionRepeat);
  failures += expect(
    precisionFirst.size() == precisionRepeat.size() &&
      std::equal(
        precisionFirst.begin(),
        precisionFirst.end(),
        precisionRepeat.begin(),
        [](const lg::TransientEffect& lhs, const lg::TransientEffect& rhs) {
          return sameEffect(lhs, rhs);
        }
      ),
    "precision impacts should remain exact for the same world hit and seed"
  );

  lg::CombatEffects freezeEffects;
  lg::FreezeGunPulseEffectsRequest freezePulse;
  freezePulse.muzzlePosition = {1.0F, 2.0F, 3.0F};
  freezePulse.muzzleForward = {1.0F, 0.0F, 0.0F};
  freezePulse.impactPosition = {8.0F, 2.0F, 3.0F};
  freezePulse.impactNormal = {-1.0F, 0.0F, 0.0F};
  freezePulse.incomingDirection = {1.0F, 0.0F, 0.0F};
  freezePulse.surface = lg::ImpactSurfaceCategory::Stone;
  freezePulse.visualSeed = 303U;
  freezePulse.ownerIndex = 0;
  freezePulse.hitWorld = true;
  lg::CombatEffects freezeQualityOneEffects;
  freezeQualityOneEffects.spawnFreezeGunPulse(
    freezePulse,
    impactQualityOne
  );
  std::vector<lg::TransientEffect> freezeQualityOneActive;
  freezeQualityOneEffects.appendActive(freezeQualityOneActive);
  freezeEffects.spawnFreezeGunPulse(freezePulse, tuning);
  std::vector<lg::TransientEffect> freezeActive;
  freezeEffects.appendActive(freezeActive);
  const lg::TransientEffect* freezeLight = findEffect(
    freezeActive,
    lg::TransientEffectType::MachineGunMuzzleLight
  );
  const bool freezeHasSmoke = std::any_of(
    freezeActive.begin(),
    freezeActive.end(),
    [](const lg::TransientEffect& effect) {
      return effect.type == lg::TransientEffectType::MachineGunMuzzleSmoke ||
        effect.type == lg::TransientEffectType::BulletImpactDust;
    }
  );
  failures += expect(
    freezeQualityOneEffects.stats().activeLights == 0U &&
      countEffects(
        freezeQualityOneActive,
        lg::TransientEffectType::BulletImpactFlash
      ) == 2U &&
      countEffects(
        freezeQualityOneActive,
        lg::TransientEffectType::BulletImpactSpark
      ) == 0U &&
      freezeQualityOneEffects.stats().activeDecals == 1U &&
    freezeEffects.stats().freezePulsesSpawned == 1U &&
      freezeEffects.stats().surfaceImpactsSpawned == 1U &&
      freezeLight != nullptr &&
      freezeLight->color.blue > freezeLight->color.red &&
      countEffects(
        freezeActive,
        lg::TransientEffectType::BulletImpactSpark
      ) > 0U &&
      !freezeHasSmoke,
    "freeze uses cyan core and frost mark at medium quality, then adds light and particles"
  );

  lg::CombatEffectsTuning cappedFreeze = tuning;
  cappedFreeze.maximumParticles = 2;
  cappedFreeze.maximumDecals = 1;
  lg::CombatEffects cappedFreezeEffects;
  for (std::uint32_t seed = 0; seed < 12U; ++seed) {
    freezePulse.visualSeed = seed;
    cappedFreezeEffects.spawnFreezeGunPulse(freezePulse, cappedFreeze);
  }
  failures += expect(
    cappedFreezeEffects.stats().activeLights <= lg::CombatEffects::kLightCapacity &&
      cappedFreezeEffects.stats().activeParticles <= cappedFreeze.maximumParticles &&
      cappedFreezeEffects.stats().activeDecals <= cappedFreeze.maximumDecals,
    "repeated freeze pulses should stay inside the shared fixed-pool limits"
  );

  lg::CombatEffects disabledSurfaceEffects;
  lg::CombatEffectsTuning disabledSurfaceTuning = tuning;
  disabledSurfaceTuning.quality = 0;
  disabledSurfaceEffects.spawnSurfaceImpact(shotgunImpact, disabledSurfaceTuning);
  disabledSurfaceEffects.spawnFreezeGunPulse(
    freezePulse,
    disabledSurfaceTuning
  );
  failures += expect(
    disabledSurfaceEffects.stats().surfaceImpactsSpawned == 0U &&
      disabledSurfaceEffects.stats().freezePulsesSpawned == 0U &&
      disabledSurfaceEffects.stats().activeLights == 0U &&
      disabledSurfaceEffects.stats().activeParticles == 0U &&
      disabledSurfaceEffects.stats().activeDecals == 0U,
    "disabled combat effects should skip surface and freeze pool work"
  );

  std::vector<lg::TransientEffect> first;
  std::vector<lg::TransientEffect> second;
  effects.appendActive(first);
  lg::CombatEffects repeat;
  for (std::uint32_t seed = 1; seed <= 64; ++seed) {
    repeat.spawnMachineGunShot(shot(seed), tuning);
  }
  repeat.appendActive(second);
  failures += expect(
    first.size() == second.size(),
    "seeded effect creation should be reproducible"
  );
  if (!first.empty() && first.size() == second.size()) {
    failures += expect(
      first.back().seed == second.back().seed &&
        first.back().position.x == second.back().position.x,
      "repeated seeds should produce the same recycled result"
    );
  }

  effects.setMuzzleAttachment(0, {9.0F, 8.0F, 7.0F});
  effects.update(0.01F, tuning);
  std::vector<lg::TransientEffect> followed;
  effects.appendActive(followed);
  bool lightFollowed = false;
  for (const lg::TransientEffect& effect : followed) {
    if (effect.type == lg::TransientEffectType::MachineGunMuzzleLight) {
      lightFollowed = effect.position.x == 9.0F &&
        effect.position.y == 8.0F &&
        effect.position.z == 7.0F;
      break;
    }
  }
  failures += expect(
    lightFollowed,
    "temporary muzzle lights should follow the current authored socket"
  );

  tuning.maximumCasings = 1;
  tuning.maximumParticles = 2;
  tuning.maximumDecals = 1;
  effects.update(0.0F, tuning);
  stats = effects.stats();
  failures += expect(
    stats.activeCasings <= tuning.maximumCasings &&
      stats.activeParticles <= tuning.maximumParticles &&
      stats.activeDecals <= tuning.maximumDecals,
    "lowered live budgets should remove the oldest active effects"
  );
  effects.spawnMachineGunShot(shot(65), tuning);
  stats = effects.stats();
  failures += expect(
    stats.activeCasings <= tuning.maximumCasings &&
      stats.activeParticles <= tuning.maximumParticles &&
      stats.activeDecals <= tuning.maximumDecals,
    "new shots after a live budget cut must stay within each active limit"
  );
  tuning.casingsEnabled = false;
  effects.update(0.0F, tuning);
  failures += expect(
    effects.stats().activeCasings == 0,
    "disabling casings should clear active casing work"
  );

  effects.update(0.25F, tuning);
  effects.update(0.25F, tuning);
  stats = effects.stats();
  failures += expect(
    stats.activeLights == 0 &&
      stats.activeCasings == 0 &&
      stats.activeParticles == 0 &&
      stats.activeDecals == 0,
    "expired effects should leave every pool"
  );

  effects.spawnMachineGunShot(shot(77), tuning);
  effects.clear();
  stats = effects.stats();
  failures += expect(
    stats.activeLights == 0 &&
      stats.activeCasings == 0 &&
      stats.activeParticles == 0 &&
      stats.activeDecals == 0,
    "map or session cleanup should clear all transient effects"
  );

  lg::CombatEffectsTuning disabled = tuning;
  disabled.quality = 0;
  effects.spawnMachineGunShot(shot(88), disabled);
  failures += expect(
    effects.stats().shotsSpawned == 0,
    "disabled combat effects should submit no work"
  );

  lg::CombatEffectEventHistory eventHistory;
  failures += expect(
    eventHistory.acceptWeaponFire(0, lg::Weapon::RocketLauncher, 1001U) &&
      !eventHistory.acceptWeaponFire(0, lg::Weapon::RocketLauncher, 1001U) &&
      eventHistory.acceptWeaponFire(1, lg::Weapon::RocketLauncher, 1001U) &&
      eventHistory.acceptWeaponFire(0, lg::Weapon::MachineGun, 1001U) &&
      !eventHistory.acceptWeaponFire(
        static_cast<std::uint8_t>(lg::kDuelPlayerCount),
        lg::Weapon::RocketLauncher,
        1002U
      ),
    "fire history should de-duplicate exact events without merging owners or weapons"
  );
  failures += expect(
    eventHistory.acceptExplosion(0, 40U) &&
      !eventHistory.acceptExplosion(0, 40U) &&
      !eventHistory.acceptExplosion(0, 39U) &&
      eventHistory.acceptExplosion(0, 41U) &&
      eventHistory.acceptExplosion(1, 40U) &&
      !eventHistory.acceptExplosion(
        static_cast<std::uint8_t>(lg::kDuelPlayerCount),
        42U
      ),
    "explosion history should reject repeats and stale owner sequences"
  );
  eventHistory.clear();
  failures += expect(
    eventHistory.acceptWeaponFire(0, lg::Weapon::RocketLauncher, 1001U) &&
      eventHistory.acceptExplosion(0, 40U),
    "clearing event history should permit new session events"
  );
  lg::CombatEffectEventHistory boundedHistory;
  bool boundedHistoryAccepted = true;
  for (
    std::uint32_t seed = 0;
    seed <= lg::CombatEffectEventHistory::kWeaponFireCapacity;
    ++seed
  ) {
    boundedHistoryAccepted =
      boundedHistory.acceptWeaponFire(
        0,
        lg::Weapon::RocketLauncher,
        seed
      ) &&
      boundedHistoryAccepted;
  }
  failures += expect(
    boundedHistoryAccepted &&
      boundedHistory.acceptWeaponFire(0, lg::Weapon::RocketLauncher, 0U),
    "fixed fire history should recycle its oldest key after reaching capacity"
  );

  lg::CombatEffectsTuning rocketLow = tuning;
  rocketLow.quality = 0;
  rocketLow.maximumParticles = 8;
  lg::CombatEffects rocketLowEffects;
  rocketLowEffects.spawnRocketLauncherShot(rocketShot(501U), rocketLow);
  rocketLowEffects.spawnRocketExplosion(
    {{7.0F, 0.0F, 1.0F}, 3.0F, 502U},
    rocketLow
  );
  failures += expect(
    rocketLowEffects.stats().activeLights == 0 &&
      rocketLowEffects.stats().activeParticles == 0 &&
      rocketLowEffects.stats().rocketShotsSpawned == 0 &&
      rocketLowEffects.stats().rocketExplosionsSpawned == 0,
    "quality zero should omit rocket secondary particles and lights"
  );

  lg::CombatEffectsTuning rocketMedium = rocketLow;
  rocketMedium.quality = 1;
  lg::CombatEffects rocketMediumEffects;
  rocketMediumEffects.spawnRocketLauncherShot(rocketShot(503U), rocketMedium);
  rocketMediumEffects.spawnRocketExplosion(
    {{7.0F, 0.0F, 1.0F}, 3.0F, 504U},
    rocketMedium
  );
  failures += expect(
    rocketMediumEffects.stats().activeLights == 1 &&
      rocketMediumEffects.stats().activeParticles == 0 &&
      rocketMediumEffects.stats().rocketShotsSpawned == 1 &&
      rocketMediumEffects.stats().rocketExplosionsSpawned == 0,
    "medium quality should retain the short rocket light without full smoke or shards"
  );

  lg::CombatEffectsTuning rocketFull = rocketMedium;
  rocketFull.quality = 2;
  lg::CombatEffects rocketFullEffects;
  rocketFullEffects.spawnRocketLauncherShot(rocketShot(505U), rocketFull);
  rocketFullEffects.spawnRocketExplosion(
    {{7.0F, 0.0F, 1.0F}, 3.0F, 506U},
    rocketFull
  );
  failures += expect(
    rocketFullEffects.stats().activeLights == 1 &&
      rocketFullEffects.stats().activeParticles == 5 &&
      rocketFullEffects.stats().rocketShotsSpawned == 1 &&
      rocketFullEffects.stats().rocketExplosionsSpawned == 1,
    "full quality should add one muzzle smoke, three shards, and one blast smoke"
  );

  lg::CombatEffects shortRocketLightEffects;
  lg::CombatEffectsTuning shortRocketLight = rocketMedium;
  shortRocketLight.muzzleLightDurationSeconds = 0.01F;
  shortRocketLightEffects.spawnRocketLauncherShot(
    rocketShot(507U),
    shortRocketLight
  );
  shortRocketLightEffects.update(0.02F, shortRocketLight);
  failures += expect(
    shortRocketLightEffects.stats().activeLights == 1 &&
      shortRocketLightEffects.stats().peakLights == 1,
    "a first-frame hitch should keep one readable rocket light frame and record its peak"
  );
  shortRocketLightEffects.update(0.0F, shortRocketLight);
  failures += expect(
    shortRocketLightEffects.stats().activeLights == 0,
    "the rocket light hitch grace should expire on the next update"
  );

  lg::CombatEffects liveQualityEffects;
  liveQualityEffects.spawnRocketLauncherShot(rocketShot(508U), rocketFull);
  liveQualityEffects.spawnRocketExplosion(
    {{7.0F, 0.0F, 1.0F}, 3.0F, 509U},
    rocketFull
  );
  liveQualityEffects.update(0.0F, rocketMedium);
  std::vector<lg::TransientEffect> mediumQualityActive;
  liveQualityEffects.appendActive(mediumQualityActive);
  const bool hasFullQualityRocketParticle = std::any_of(
    mediumQualityActive.begin(),
    mediumQualityActive.end(),
    [](const lg::TransientEffect& effect) {
      return
        effect.type ==
          lg::TransientEffectType::RocketLauncherMuzzleSmoke ||
        effect.type == lg::TransientEffectType::RocketExplosionShard ||
        effect.type == lg::TransientEffectType::RocketExplosionSmoke;
    }
  );
  failures += expect(
    liveQualityEffects.stats().activeLights == 1 &&
      !hasFullQualityRocketParticle,
    "lowering live quality to medium should remove full-quality rocket particles"
  );
  liveQualityEffects.update(0.0F, rocketLow);
  failures += expect(
    liveQualityEffects.stats().activeLights == 0 &&
      liveQualityEffects.stats().activeParticles == 0,
    "lowering live quality to zero should clear all rocket effects"
  );

  std::vector<lg::TransientEffect> rocketFirst;
  rocketFullEffects.appendActive(rocketFirst);
  lg::CombatEffects rocketRepeatEffects;
  rocketRepeatEffects.spawnRocketLauncherShot(rocketShot(505U), rocketFull);
  rocketRepeatEffects.spawnRocketExplosion(
    {{7.0F, 0.0F, 1.0F}, 3.0F, 506U},
    rocketFull
  );
  std::vector<lg::TransientEffect> rocketSecond;
  rocketRepeatEffects.appendActive(rocketSecond);
  const lg::TransientEffect* firstSmoke = findEffect(
    rocketFirst,
    lg::TransientEffectType::RocketLauncherMuzzleSmoke
  );
  const lg::TransientEffect* secondSmoke = findEffect(
    rocketSecond,
    lg::TransientEffectType::RocketLauncherMuzzleSmoke
  );
  const lg::TransientEffect* firstShard = findEffect(
    rocketFirst,
    lg::TransientEffectType::RocketExplosionShard
  );
  const lg::TransientEffect* secondShard = findEffect(
    rocketSecond,
    lg::TransientEffectType::RocketExplosionShard
  );
  failures += expect(
    firstSmoke != nullptr &&
      secondSmoke != nullptr &&
      firstShard != nullptr &&
      secondShard != nullptr &&
      sameEffect(*firstSmoke, *secondSmoke) &&
      sameEffect(*firstShard, *secondShard),
    "rocket smoke and shards should repeat exactly for a visual seed"
  );

  lg::CombatEffects attachedEffects;
  lg::MachineGunShotEffectsRequest attachedMachineGun = shot(601U);
  attachedMachineGun.ownerIndex = 0;
  attachedEffects.spawnMachineGunShot(attachedMachineGun, rocketMedium);
  attachedEffects.spawnRocketLauncherShot(rocketShot(602U), rocketMedium);
  const lg::Vec3 machineGunSocket = {9.0F, 8.0F, 7.0F};
  const lg::Vec3 rocketSocket = {-3.0F, 4.0F, 2.0F};
  attachedEffects.setMuzzleAttachment(
    0,
    lg::MuzzleAttachment::MachineGun,
    machineGunSocket
  );
  attachedEffects.setMuzzleAttachment(
    0,
    lg::MuzzleAttachment::RocketLauncher,
    rocketSocket
  );
  attachedEffects.update(0.01F, rocketMedium);
  std::vector<lg::TransientEffect> attachedActive;
  attachedEffects.appendActive(attachedActive);
  const lg::TransientEffect* machineGunLight = findEffect(
    attachedActive,
    lg::TransientEffectType::MachineGunMuzzleLight
  );
  const lg::TransientEffect* rocketLight = findEffect(
    attachedActive,
    lg::TransientEffectType::RocketLauncherMuzzleLight
  );
  failures += expect(
    machineGunLight != nullptr &&
      rocketLight != nullptr &&
      machineGunLight->position.x == machineGunSocket.x &&
      machineGunLight->position.y == machineGunSocket.y &&
      machineGunLight->position.z == machineGunSocket.z &&
      rocketLight->position.x == rocketSocket.x &&
      rocketLight->position.y == rocketSocket.y &&
      rocketLight->position.z == rocketSocket.z,
    "machine-gun and rocket lights should follow their own moving sockets"
  );

  lg::CombatEffectsTuning cappedRocket = rocketFull;
  cappedRocket.maximumParticles = 2;
  lg::CombatEffects cappedRocketEffects;
  for (std::uint32_t seed = 0; seed < 16U; ++seed) {
    cappedRocketEffects.spawnRocketExplosion(
      {{6.0F, 1.0F, 1.0F}, 3.0F, seed},
      cappedRocket
    );
  }
  failures += expect(
    cappedRocketEffects.stats().activeParticles == 2 &&
      cappedRocketEffects.stats().activeParticles <=
        cappedRocket.maximumParticles,
    "rocket-heavy bursts should recycle inside the fixed particle cap"
  );
  cappedRocketEffects.update(0.25F, cappedRocket);
  cappedRocketEffects.update(0.25F, cappedRocket);
  failures += expect(
    cappedRocketEffects.stats().activeParticles == 0,
    "rocket secondary particles should expire from the fixed pool"
  );

  lg::CombatEffects invalidRocketEffects;
  lg::RocketLauncherShotEffectsRequest invalidShot = rocketShot(701U);
  invalidShot.muzzlePosition.x = std::numeric_limits<float>::infinity();
  invalidRocketEffects.spawnRocketLauncherShot(invalidShot, rocketFull);
  invalidRocketEffects.spawnRocketExplosion(
    {
      {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F},
      3.0F,
      702U,
    },
    rocketFull
  );
  invalidRocketEffects.spawnRocketExplosion(
    {
      {3.0F, 0.0F, 1.0F},
      std::numeric_limits<float>::infinity(),
      703U,
    },
    rocketFull
  );
  std::vector<lg::TransientEffect> finiteRocketEffects;
  invalidRocketEffects.appendActive(finiteRocketEffects);
  failures += expect(
    invalidRocketEffects.stats().rocketShotsSpawned == 0 &&
      invalidRocketEffects.stats().rocketExplosionsSpawned == 1 &&
      !finiteRocketEffects.empty() &&
      std::all_of(
        finiteRocketEffects.begin(),
        finiteRocketEffects.end(),
        [](const lg::TransientEffect& effect) {
          return std::isfinite(effect.position.x) &&
            std::isfinite(effect.position.y) &&
            std::isfinite(effect.position.z) &&
            std::isfinite(effect.velocity.x) &&
            std::isfinite(effect.velocity.y) &&
            std::isfinite(effect.velocity.z);
        }
      ),
    "rocket requests should reject bad positions and clamp a bad blast radius"
  );

  return failures == 0 ? 0 : 1;
}
