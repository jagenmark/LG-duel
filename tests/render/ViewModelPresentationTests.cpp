#include "render/ViewModelPresentation.hpp"

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

bool neutral(const lg::ViewModelPresentationOutput& value) {
  return magnitude(value.translation) == 0.0F &&
    magnitude(value.rotationRadians) == 0.0F;
}

} // namespace

int main() {
  int failures = 0;
  lg::ViewModelPresentationController controller;
  const lg::ViewModelPresentationInput active{
    {8.0F, 3.0F, 0.0F}, 18.0F, -9.0F, true, false, 1.0F / 60.0F
  };
  const lg::ViewModelPresentationInput unchanged = active;

  lg::ViewModelPresentationTuning disabled;
  disabled.motionScale = 0.0F;
  failures += expect(neutral(controller.update(active, disabled)),
    "master scale zero should return an exactly neutral output");
  failures += expect(active.localVelocity.x == unchanged.localVelocity.x &&
    active.mouseDeltaX == unchanged.mouseDeltaX && active.grounded == unchanged.grounded,
    "presentation update should not mutate its input");

  controller.reset();
  lg::ViewModelPresentationTuning individual;
  individual.bobScale = 0.0F;
  individual.swayScale = 0.0F;
  individual.inertiaScale = 0.0F;
  individual.landingScale = 0.0F;
  failures += expect(neutral(controller.update(active, individual)),
    "individually disabled components should produce neutral output");

  controller.reset();
  const auto responsive = controller.update(active);
  failures += expect(magnitude(responsive.translation) > 0.0F &&
    magnitude(responsive.rotationRadians) > 0.0F,
    "movement and immediate mouse delta should move the viewmodel");
  failures += expect(magnitude(responsive.translation) < 0.25F &&
    magnitude(responsive.rotationRadians) < 0.15F,
    "viewmodel response should stay competitively subtle and bounded");
  controller.reset();
  lg::ViewModelPresentationInput airborne{
    {0.0F, 0.0F, -12.0F}, 0.0F, 0.0F, false, false, 1.0F / 60.0F
  };
  const auto airborneOutput = controller.update(airborne);
  (void)airborneOutput;
  lg::ViewModelPresentationInput landed{
    {0.0F, 0.0F, 0.0F}, 0.0F, 0.0F, true, false, 1.0F / 60.0F
  };
  const auto landing = controller.update(landed);
  failures += expect(landing.translation.z < 0.0F && landing.rotationRadians.x < 0.0F,
    "air-to-ground transition should briefly compress the viewmodel");

  lg::ViewModelPresentationTuning noLanding;
  noLanding.bobScale = 0.0F;
  noLanding.swayScale = 0.0F;
  noLanding.inertiaScale = 0.0F;
  noLanding.landingScale = 0.0F;
  controller.reset();
  const auto disabledAirborneOutput = controller.update(airborne, noLanding);
  (void)disabledAirborneOutput;
  const auto disabledLanding = controller.update(landed, noLanding);
  failures += expect(neutral(disabledLanding),
    "landing scale zero should independently remove landing response");

  controller.reset();
  lg::ViewModelPresentationInput sliding = active;
  sliding.sliding = true;
  auto slideOutput = controller.update(sliding);
  for (int frame = 0; frame < 20; ++frame) {
    slideOutput = controller.update(sliding);
  }
  failures += expect(
    slideOutput.translation.x > 0.02F &&
      slideOutput.translation.z < -0.02F &&
      slideOutput.rotationRadians.x > 0.02F,
    "sliding should push the visible weapon forward and low"
  );

  return failures == 0 ? 0 : 1;
}
