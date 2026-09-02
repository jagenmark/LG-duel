#pragma once

#include "shared/Math.hpp"

namespace lg {

struct CameraPresentationInput {
  Vec3 localVelocity = {};
  bool grounded = true;
  bool sliding = false;
  float eyeHeightAboveFeet = 1.55F;
  float deltaSeconds = 0.0F;
  float referenceSpeed = 8.0F;
};

struct CameraPresentationTuning {
  float positionResponse = 0.7F;
  float rollDegrees = 2.5F;
  float fovBoostDegrees = 3.5F;
};

struct CameraPresentationOutput {
  Vec3 translation = {};
  float rollRadians = 0.0F;
  float fovOffsetDegrees = 0.0F;
};

class CameraPresentationController {
public:
  [[nodiscard]] CameraPresentationOutput update(
    const CameraPresentationInput& input,
    const CameraPresentationTuning& tuning = {}
  );
  void reset();

private:
  Vec3 smoothedVelocity_ = {};
  float presentedEyeHeight_ = 1.55F;
  float slideAmount_ = 0.0F;
  float landingCompression_ = 0.0F;
  float airborneDownSpeed_ = 0.0F;
  float rollRadians_ = 0.0F;
  float fovOffsetDegrees_ = 0.0F;
  bool wasGrounded_ = true;
  bool initialized_ = false;
};

} // namespace lg
