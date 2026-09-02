#include "render/CameraPresentation.hpp"

#include <cmath>
#include <iostream>

namespace {

int expect(bool condition, const char* message) {
  if (condition) return 0;
  std::cerr << "FAIL: " << message << '\n';
  return 1;
}

float magnitude(lg::Vec3 value) {
  return std::sqrt(lg::dot(value, value));
}

lg::CameraPresentationOutput runFor(
  lg::CameraPresentationController& controller,
  lg::CameraPresentationInput input,
  int frames
) {
  lg::CameraPresentationOutput output;
  for (int frame = 0; frame < frames; ++frame) {
    output = controller.update(input);
  }
  return output;
}

lg::CameraPresentationInput cameraInput(
  lg::Vec3 localVelocity,
  bool grounded,
  bool sliding,
  float eyeHeight,
  float deltaSeconds,
  float referenceSpeed
) {
  return {{localVelocity, grounded, sliding, deltaSeconds, referenceSpeed}, eyeHeight};
}

} // namespace

int main() {
  int failures = 0;

  lg::CameraPresentationController disabledController;
  lg::CameraPresentationTuning disabled;
  disabled.positionResponse = 0.0F;
  disabled.rollDegrees = 0.0F;
  disabled.fovBoostDegrees = 0.0F;
  const auto disabledOutput = disabledController.update(
    cameraInput({8.0F, 4.0F, 0.0F}, true, true, 0.96F, 1.0F / 60.0F, 10.5F),
    disabled
  );
  failures += expect(
    magnitude(disabledOutput.translation) == 0.0F &&
      disabledOutput.rollRadians == 0.0F &&
      disabledOutput.fovOffsetDegrees == 0.0F,
    "zero camera tuning should leave presentation neutral"
  );

  lg::CameraPresentationController movingController;
  const auto moving = runFor(
    movingController,
    cameraInput({8.0F, 5.0F, 0.0F}, true, false, 1.55F, 1.0F / 60.0F, 10.5F),
    30
  );
  failures += expect(
    moving.rollRadians < 0.0F && moving.fovOffsetDegrees > 0.0F,
    "side travel should add a small bank and fast travel should widen the view"
  );
  failures += expect(
    std::fabs(moving.rollRadians) < 0.08F && moving.fovOffsetDegrees < 4.0F,
    "camera bank and FOV gain should stay supportive rather than dominate"
  );

  lg::CameraPresentationController slideController;
  (void)slideController.update(
    cameraInput({10.5F, 0.0F, 0.0F}, true, false, 1.55F, 1.0F / 60.0F, 10.5F)
  );
  const auto slideEntry = slideController.update(
    cameraInput({10.5F, 0.0F, 0.0F}, true, true, 0.96F, 1.0F / 60.0F, 10.5F)
  );
  const auto settledSlide = runFor(
    slideController,
    cameraInput({10.5F, 0.0F, 0.0F}, true, true, 0.96F, 1.0F / 60.0F, 10.5F),
    30
  );
  failures += expect(
    slideEntry.translation.z > settledSlide.translation.z &&
      settledSlide.translation.z < -0.03F,
    "slide entry should ease the physical eye-height step then settle low"
  );

  lg::CameraPresentationController landingController;
  (void)landingController.update(
    cameraInput({10.0F, 0.0F, 5.0F}, false, false, 1.55F, 1.0F / 60.0F, 10.5F)
  );
  (void)runFor(
    landingController,
    cameraInput({10.0F, 0.0F, -8.0F}, false, false, 1.55F, 1.0F / 60.0F, 10.5F),
    20
  );
  const auto landing = landingController.update(
    cameraInput({10.0F, 0.0F, 0.0F}, true, true, 0.96F, 1.0F / 60.0F, 10.5F)
  );
  failures += expect(
    0.96F + landing.translation.z < 1.45F &&
      0.96F + landing.translation.z > 0.80F,
    "a fast landing into a slide should lower the camera without a height snap"
  );

  lg::CameraPresentationController sixtyFps;
  lg::CameraPresentationController oneTwentyFps;
  (void)sixtyFps.update(
    cameraInput({10.5F, 0.0F, 0.0F}, true, false, 1.55F, 1.0F / 60.0F, 10.5F)
  );
  (void)oneTwentyFps.update(
    cameraInput({10.5F, 0.0F, 0.0F}, true, false, 1.55F, 1.0F / 120.0F, 10.5F)
  );
  const auto sixtyResult = runFor(
    sixtyFps,
    cameraInput({10.5F, 0.0F, 0.0F}, true, true, 0.96F, 1.0F / 60.0F, 10.5F),
    30
  );
  const auto oneTwentyResult = runFor(
    oneTwentyFps,
    cameraInput({10.5F, 0.0F, 0.0F}, true, true, 0.96F, 1.0F / 120.0F, 10.5F),
    60
  );
  failures += expect(
    std::fabs(sixtyResult.translation.z - oneTwentyResult.translation.z) < 0.005F,
    "camera response should stay stable across render rates"
  );

  return failures == 0 ? 0 : 1;
}
