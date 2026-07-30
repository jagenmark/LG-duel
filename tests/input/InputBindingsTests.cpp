#include "input/InputBindings.hpp"
#include "input/MouseAim.hpp"

#include <cmath>
#include <iostream>
#include <string_view>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) {
    return 0;
  }
  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

bool nearlyEqual(float lhs, float rhs, float epsilon = 0.000001F) {
  return std::fabs(lhs - rhs) <= epsilon;
}

} // namespace

int main() {
  int failures = 0;

  failures += expect(
    nearlyEqual(
      lg::kBaseMouseSensitivityRadians,
      lg::kQuakeLiveMouseYawDegrees * lg::kMouseDegreesToRadians
    ),
    "base sensitivity should use QL m_yaw 0.022 degrees per count"
  );
  failures += expect(
    nearlyEqual(lg::kLegacyToQuakeLiveSensitivityScale, 6.510884F, 0.0001F),
    "legacy sensitivity migration should preserve the old angular speed"
  );
  failures += expect(
    lg::relativeMouseYaw(0.0F, 10.0F, 1.0F) < 0.0F,
    "moving the mouse right should turn view yaw right"
  );
  failures += expect(
    lg::relativeMouseYaw(0.0F, -10.0F, 1.0F) > 0.0F,
    "moving the mouse left should turn view yaw left"
  );
  lg::MouseAimSettings mouseAim;
  mouseAim.sensitivity = 4.0F;
  mouseAim.mouseAccel = 0.1F;
  mouseAim.mouseAccelPower = 2.0F;
  mouseAim.mouseAccelOffset = 0.0F;
  failures += expect(
    nearlyEqual(
      lg::quakeLiveMouseSensitivity(4.0F, 3.0F, 0.008F, mouseAim),
      4.0625F
    ),
    "QL accel should add pow(accel * speed, power - 1) to base sensitivity"
  );
  mouseAim.mouseAccelOffset = 1.0F;
  failures += expect(
    nearlyEqual(
      lg::quakeLiveMouseSensitivity(4.0F, 3.0F, 0.008F, mouseAim),
      4.0F
    ),
    "QL accel offset should leave sensitivity unchanged below the threshold"
  );
  mouseAim.mouseAccelOffset = 0.0F;
  mouseAim.mouseSensitivityCap = 4.02F;
  failures += expect(
    nearlyEqual(
      lg::quakeLiveMouseSensitivity(4.0F, 3.0F, 0.008F, mouseAim),
      4.02F
    ),
    "QL accel sensitivity cap should clamp the accelerated sensitivity"
  );
  const lg::MouseAimDelta delta =
    lg::quakeLiveMouseAimDelta(2.0F, 1.0F, 0.008F, mouseAim);
  failures += expect(
    nearlyEqual(delta.yawRadians, 2.0F * lg::kBaseMouseSensitivityRadians * 4.02F),
    "accelerated yaw delta should use the QL-scale effective sensitivity"
  );
  const lg::MouseAimDelta earlyDelta =
    lg::quakeLiveMouseAimDelta(4.0F, 3.0F, 0.008F, mouseAim);
  const lg::MouseAimDelta combinedDelta =
    lg::quakeLiveMouseAimDelta(6.0F, 4.0F, 0.008F, mouseAim);
  const lg::MouseAimDelta lateCorrection =
    lg::quakeLiveLateMouseAimCorrection(
      4.0F,
      3.0F,
      2.0F,
      1.0F,
      0.008F,
      mouseAim,
      0.0F,
      -1.5F,
      1.5F
    );
  failures += expect(
    nearlyEqual(
      earlyDelta.yawRadians + lateCorrection.yawRadians,
      combinedDelta.yawRadians
    ) &&
      nearlyEqual(
        earlyDelta.pitchRadians + lateCorrection.pitchRadians,
        combinedDelta.pitchRadians
      ),
    "late mouse correction should match one accelerated combined sample"
  );
  lg::MouseAimSettings unacceleratedMouseAim;
  const float maximumPitchRadians = 1.0F;
  const float pitchPerCount =
    lg::kBaseMouseSensitivityRadians * unacceleratedMouseAim.sensitivity;
  const float pitchBeforeEarlySample = maximumPitchRadians - pitchPerCount;
  const lg::MouseAimDelta clampedLateCorrection =
    lg::quakeLiveLateMouseAimCorrection(
      0.0F,
      -10.0F,
      0.0F,
      9.0F,
      0.008F,
      unacceleratedMouseAim,
      pitchBeforeEarlySample,
      -maximumPitchRadians,
      maximumPitchRadians
    );
  const lg::MouseAimDelta clampedEarlyDelta =
    lg::quakeLiveMouseAimDelta(
      0.0F,
      -10.0F,
      0.008F,
      unacceleratedMouseAim
    );
  const float clampedEarlyPitch = std::fmin(
    maximumPitchRadians,
    pitchBeforeEarlySample - clampedEarlyDelta.pitchRadians
  );
  failures += expect(
    nearlyEqual(
      clampedEarlyPitch - clampedLateCorrection.pitchRadians,
      maximumPitchRadians
    ),
    "late mouse correction should match a combined sample at the pitch limit"
  );
  lg::InputBindings bindings;

  failures += expect(bindings.bind("A", "+moveleft"), "binding should accept a key");
  failures += expect(bindings.binding("a") == "+moveleft", "keys should be case insensitive");
  failures += expect(
    bindings.handleKey("a", true) == std::vector<std::string>{"+moveleft"},
    "key down should emit the bound command"
  );
  failures += expect(
    bindings.handleKey("a", true).empty(),
    "repeated key down should not repeat button commands"
  );
  failures += expect(
    bindings.handleKey("a", false) == std::vector<std::string>{"-moveleft"},
    "key up should release plus commands"
  );

  failures += expect(bindings.bind("leftarrow", "+moveleft"), "arrow alias should bind");
  failures += expect(bindings.binding("left") == "+moveleft", "arrow alias should normalize");
  failures += expect(bindings.bind("grave", "toggleconsole"), "grave alias should bind");
  failures += expect(
    bindings.binding("section") == "toggleconsole",
    "grave should map to the section-key binding"
  );
  failures += expect(
    bindings.binding("\xC2\xA7") == "toggleconsole",
    "literal section sign should map to the section-key binding"
  );

  (void)bindings.handleKey("left", true);
  failures += expect(
    bindings.unbind("left") == std::vector<std::string>{"-moveleft"},
    "unbinding a held button should release it"
  );
  failures += expect(bindings.binding("left").empty(), "unbind should remove the mapping");

  (void)bindings.bind("d", "+moveright");
  (void)bindings.handleKey("d", true);
  failures += expect(
    bindings.releaseAll() == std::vector<std::string>{"-moveright"},
    "release all should release held button commands"
  );

  const std::vector<std::string> config = bindings.configLines();
  failures += expect(
    config.size() == 3 && config[0] == "bind a \"+moveleft\"",
    "bindings should serialize deterministically"
  );

  return failures == 0 ? 0 : 1;
}
