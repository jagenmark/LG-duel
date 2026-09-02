#include "render/ViewModelPresentation.hpp"

#include <algorithm>
#include <cmath>

namespace lg {
namespace {

constexpr float kTau = 6.28318530718F;

float decayAlpha(float rate, float dt) {
  return 1.0F - std::exp(-rate * dt);
}

Vec3 lerp(Vec3 from, Vec3 to, float amount) {
  return from + (to - from) * amount;
}

float nonNegative(float value) {
  return std::max(value, 0.0F);
}

} // namespace

ViewModelPresentationOutput ViewModelPresentationController::update(
  const ViewModelPresentationInput& input,
  const ViewModelPresentationTuning& tuning
) {
  // A bounded presentation step prevents stalls from injecting a large impulse.
  const PlayerMotionPresentationInput& motion = input.motion;
  const float dt = std::clamp(motion.deltaSeconds, 0.0F, 0.05F);
  const float horizontalSpeed = std::sqrt(
    motion.localVelocity.x * motion.localVelocity.x +
    motion.localVelocity.y * motion.localVelocity.y
  );
  const float referenceSpeed = std::max(motion.referenceSpeed, 0.1F);

  if (dt > 0.0F) {
    const float velocityAlpha = decayAlpha(12.0F, dt);
    smoothedVelocity_ = lerp(smoothedVelocity_, motion.localVelocity, velocityAlpha);
    const Vec3 acceleration = (motion.localVelocity - previousVelocity_) / dt;
    previousVelocity_ = motion.localVelocity;

    if (motion.grounded) {
      // Integrating distance instead of time keeps the gait phase tied to stride.
      stridePhase_ = std::fmod(stridePhase_ + horizontalSpeed * dt * 1.55F, kTau);
    }

    const Vec3 swayTarget = {
      std::clamp(input.mouseDeltaY * 0.00045F, -0.025F, 0.025F),
      std::clamp(-input.mouseDeltaX * 0.00045F, -0.030F, 0.030F),
      std::clamp(-input.mouseDeltaX * 0.00022F, -0.015F, 0.015F),
    };
    sway_ = lerp(sway_, swayTarget, decayAlpha(24.0F, dt));

    if (!motion.grounded) {
      airborneDownSpeed_ = std::max(airborneDownSpeed_, -motion.localVelocity.z);
    } else if (!wasGrounded_) {
      landingCompression_ = std::clamp(airborneDownSpeed_ * 0.018F, 0.0F, 0.12F);
      airborneDownSpeed_ = 0.0F;
    }
    landingCompression_ *= std::exp(-14.0F * dt);
    wasGrounded_ = motion.grounded;
    slideAmount_ = std::lerp(
      slideAmount_,
      motion.sliding ? 1.0F : 0.0F,
      decayAlpha(motion.sliding ? 18.0F : 11.0F, dt)
    );

    // Acceleration is consumed below through the velocity lag; retaining this
    // calculation here documents the braking/acceleration frame boundary.
    (void)acceleration;
  }

  const float master = nonNegative(tuning.motionScale);

  const float moving = std::clamp(horizontalSpeed / referenceSpeed, 0.0F, 1.0F);
  const float bob = nonNegative(tuning.bobScale);
  const Vec3 bobTranslation = motion.grounded && !motion.sliding ? Vec3{
    -0.006F * std::cos(stridePhase_ * 2.0F) * moving,
    0.012F * std::sin(stridePhase_) * moving,
    -0.016F * std::fabs(std::sin(stridePhase_)) * moving,
  } * bob : Vec3{};
  const Vec3 bobRotation = motion.grounded && !motion.sliding ? Vec3{
    0.004F * std::sin(stridePhase_ * 2.0F) * moving,
    0.003F * std::sin(stridePhase_) * moving,
    -0.008F * std::sin(stridePhase_) * moving,
  } * bob : Vec3{};

  const float inertia = nonNegative(tuning.inertiaScale);
  const Vec3 velocityLag = motion.localVelocity - smoothedVelocity_;
  const Vec3 inertiaTranslation = Vec3{
    std::clamp(-velocityLag.x * 0.0030F, -0.025F, 0.025F),
    std::clamp(-velocityLag.y * 0.0040F, -0.035F, 0.035F),
    std::clamp(-velocityLag.z * 0.0020F, -0.025F, 0.025F),
  } * inertia;
  const Vec3 inertiaRotation = Vec3{
    std::clamp(velocityLag.z * 0.0015F, -0.012F, 0.012F),
    std::clamp(-velocityLag.y * 0.0018F, -0.016F, 0.016F),
    std::clamp(-velocityLag.y * 0.0025F, -0.020F, 0.020F),
  } * inertia;

  const float landing = nonNegative(tuning.landingScale);
  const Vec3 landingTranslation = Vec3{
    landingCompression_ * 0.125F, 0.0F, -landingCompression_
  } * landing;
  const Vec3 landingRotation =
    Vec3{-landingCompression_ * 0.25F, 0.0F, 0.0F} * landing;
  const float swayScale = nonNegative(tuning.swayScale);
  const Vec3 slideTranslation = {
    0.065F * slideAmount_,
    0.0F,
    -0.055F * slideAmount_,
  };
  const Vec3 slideRotation = {
    0.055F * slideAmount_,
    0.0F,
    0.0F,
  };

  ViewModelPresentationOutput output;
  output.translation =
    (bobTranslation + inertiaTranslation + landingTranslation + slideTranslation) * master;
  output.rotationRadians =
    (bobRotation + inertiaRotation + landingRotation + slideRotation +
      sway_ * swayScale) * master;
  return output;
}

void ViewModelPresentationController::reset() {
  *this = {};
}

} // namespace lg
