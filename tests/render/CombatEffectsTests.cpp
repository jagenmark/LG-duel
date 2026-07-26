#include "render/CombatEffects.hpp"

#include <iostream>
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

  return failures == 0 ? 0 : 1;
}
