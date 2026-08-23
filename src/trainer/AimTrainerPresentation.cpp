#include "trainer/AimTrainerPresentation.hpp"

namespace lg {

AimTrainerPresentation buildAimTrainerPresentation(const AimTrainerFrame& frame) {
  AimTrainerPresentation result;
  result.targetEffects.reserve(frame.targets.size());
  for (const AimTargetView& target : frame.targets) {
    if (!target.active) continue;
    if (target.visual == AimTargetVisual::Worker) {
      ++result.workerCount;
      result.targetEffects.push_back({
        TransientEffectType::TrainerWorkerTarget,
        target.position,
        0.0F,
        0.0F,
        target.radius,
        target.radius,
        {target.color.red, target.color.green, target.color.blue, 255U},
        target.id,
      });
      continue;
    }
    ++result.orbCount;
    result.targetEffects.push_back({
      TransientEffectType::TrainerOrbTarget,
      target.position,
      0.0F,
      0.0F,
      target.radius,
      target.radius,
      {target.color.red, target.color.green, target.color.blue, 255U},
      target.id,
    });
  }

  std::size_t projectileIndex = 0;
  for (const AimTrainerProjectileView& projectile : frame.projectiles) {
    if (!projectile.active || projectileIndex >= result.projectiles.size()) continue;
    result.projectiles[projectileIndex++] = {
      true,
      0U,
      projectile.weapon,
      projectile.position,
      projectile.velocity,
      projectile.radius,
    };
  }
  return result;
}

} // namespace lg
