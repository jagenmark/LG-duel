#pragma once

#include "shared/Math.hpp"

namespace lg {

// Inputs are read-only render-frame observations in view-local coordinates:
// x is forward, y is right, and z is up. They never feed gameplay state back.
struct ViewModelPresentationInput {
  Vec3 localVelocity = {};
  float mouseDeltaX = 0.0F;
  float mouseDeltaY = 0.0F;
  bool grounded = true;
  float deltaSeconds = 0.0F;
};

struct ViewModelPresentationTuning {
  float motionScale = 1.0F;
  float bobScale = 0.65F;
  float swayScale = 0.55F;
  float inertiaScale = 0.55F;
  float landingScale = 0.65F;
  float cameraPositionResponse = 0.0F;
};

struct ViewModelPresentationOutput {
  Vec3 translation = {};
  // View-local pitch, yaw, and roll offsets. These rotate only the viewmodel.
  Vec3 rotationRadians = {};
  // Optional translation-only camera response; its shipped default is exactly zero.
  Vec3 cameraTranslation = {};
};

class ViewModelPresentationController {
public:
  [[nodiscard]] ViewModelPresentationOutput update(
    const ViewModelPresentationInput& input,
    const ViewModelPresentationTuning& tuning = {}
  );
  void reset();

private:
  float stridePhase_ = 0.0F;
  Vec3 smoothedVelocity_ = {};
  Vec3 previousVelocity_ = {};
  Vec3 sway_ = {};
  float landingCompression_ = 0.0F;
  float airborneDownSpeed_ = 0.0F;
  bool wasGrounded_ = true;
};

} // namespace lg
