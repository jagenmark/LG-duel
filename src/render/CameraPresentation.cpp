#include "render/CameraPresentation.hpp"

#include <algorithm>
#include <cmath>

namespace lg {
namespace {

constexpr float kDegreesToRadians = 0.01745329252F;

[[nodiscard]] float decayAlpha(float rate, float dt) {
  return 1.0F - std::exp(-rate * dt);
}

[[nodiscard]] float smoothStep(float value) {
  value = std::clamp(value, 0.0F, 1.0F);
  return value * value * (3.0F - 2.0F * value);
}

} // namespace

CameraPresentationOutput CameraPresentationController::update(
  const CameraPresentationInput& input,
  const CameraPresentationTuning& tuning
) {
  const float dt = std::clamp(input.deltaSeconds, 0.0F, 0.05F);
  const float referenceSpeed = std::max(input.referenceSpeed, 0.1F);
  const float horizontalSpeed = std::hypot(
    input.localVelocity.x,
    input.localVelocity.y
  );

  if (!initialized_) {
    smoothedVelocity_ = input.localVelocity;
    presentedEyeHeight_ = input.eyeHeightAboveFeet;
    wasGrounded_ = input.grounded;
    initialized_ = true;
  }

  if (dt > 0.0F) {
    const float velocityAlpha = decayAlpha(9.0F, dt);
    smoothedVelocity_ +=
      (input.localVelocity - smoothedVelocity_) * velocityAlpha;
    presentedEyeHeight_ = std::lerp(
      presentedEyeHeight_,
      input.eyeHeightAboveFeet,
      decayAlpha(13.0F, dt)
    );
    slideAmount_ = std::lerp(
      slideAmount_,
      input.sliding ? 1.0F : 0.0F,
      decayAlpha(input.sliding ? 15.0F : 10.0F, dt)
    );

    if (!input.grounded) {
      airborneDownSpeed_ = std::max(
        airborneDownSpeed_,
        -input.localVelocity.z
      );
    } else if (!wasGrounded_) {
      landingCompression_ = std::clamp(
        airborneDownSpeed_ * 0.020F,
        0.0F,
        0.14F
      );
      airborneDownSpeed_ = 0.0F;
    }
    landingCompression_ *= std::exp(-12.0F * dt);
    wasGrounded_ = input.grounded;

    const float lateralAmount = std::clamp(
      input.localVelocity.y / referenceSpeed,
      -1.0F,
      1.0F
    );
    const float rollTarget =
      -lateralAmount *
      std::clamp(tuning.rollDegrees, 0.0F, 8.0F) *
      kDegreesToRadians;
    rollRadians_ = std::lerp(
      rollRadians_,
      rollTarget,
      decayAlpha(8.0F, dt)
    );

    const float speedAmount = smoothStep(
      (horizontalSpeed / referenceSpeed - 0.5F) / 0.65F
    );
    fovOffsetDegrees_ = std::lerp(
      fovOffsetDegrees_,
      speedAmount * std::clamp(tuning.fovBoostDegrees, 0.0F, 12.0F),
      decayAlpha(5.0F, dt)
    );
  }

  const float positionScale = std::clamp(
    tuning.positionResponse,
    0.0F,
    1.5F
  );
  const Vec3 velocityLag = input.localVelocity - smoothedVelocity_;
  CameraPresentationOutput output;
  output.translation = Vec3{
    std::clamp(-velocityLag.x * 0.010F, -0.055F, 0.055F),
    std::clamp(-velocityLag.y * 0.012F, -0.065F, 0.065F),
    (presentedEyeHeight_ - input.eyeHeightAboveFeet) -
      slideAmount_ * 0.07F +
      std::clamp(-input.localVelocity.z * 0.006F, -0.05F, 0.05F) -
      landingCompression_,
  } * positionScale;
  output.rollRadians = rollRadians_;
  output.fovOffsetDegrees = fovOffsetDegrees_;
  return output;
}

void CameraPresentationController::reset() {
  *this = {};
}

} // namespace lg
