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
  float referenceSpeed = 8.0F;
};

struct ViewModelPresentationTuning {
  float motionScale = 1.0F;
  float bobScale = 0.65F;
  float swayScale = 0.55F;
  float inertiaScale = 0.55F;
  float landingScale = 0.65F;
  float cameraPositionResponse = 0.08F;
  float cameraRollDegrees = 5.5F;
  float cameraFovBoostDegrees = 7.5F;
};

struct ViewModelPresentationOutput {
  Vec3 translation = {};
  // View-local pitch, yaw, and roll offsets. These rotate only the viewmodel.
  Vec3 rotationRadians = {};
  // Camera outputs stay in presentation and never change the aim basis.
  Vec3 cameraTranslation = {};
  float cameraRollRadians = 0.0F;
  float cameraFovOffsetDegrees = 0.0F;
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
  float cameraRollRadians_ = 0.0F;
  float cameraFovOffsetDegrees_ = 0.0F;
  float landingCompression_ = 0.0F;
  float airborneDownSpeed_ = 0.0F;
  bool wasGrounded_ = true;
};

} // namespace lg
