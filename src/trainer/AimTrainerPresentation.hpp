#pragma once

#include "render/Renderer.hpp"
#include "trainer/AimTrainer.hpp"

#include <array>
#include <cstddef>
#include <vector>

namespace lg {

struct AimTrainerPresentation {
  std::array<RocketProjectileSnapshot, kMaxRocketProjectiles> projectiles = {};
  std::vector<TransientEffect> targetEffects;
  std::size_t workerCount = 0;
  std::size_t orbCount = 0;
};

// Maps every live target to a trainer-owned render item. Worker targets do not
// use the fixed remote-player slots used by network play.
[[nodiscard]] AimTrainerPresentation buildAimTrainerPresentation(
  const AimTrainerFrame& frame
);

} // namespace lg
