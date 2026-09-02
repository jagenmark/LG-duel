#pragma once

#include "shared/Math.hpp"

namespace lg {

// Read-only render-frame motion in view-local coordinates. It never feeds
// presentation state back into gameplay.
struct PlayerMotionPresentationInput {
  Vec3 localVelocity = {};
  bool grounded = true;
  bool sliding = false;
  float deltaSeconds = 0.0F;
  float referenceSpeed = 8.0F;
  bool glideEnabled = false;
};

} // namespace lg
