#pragma once

namespace lg {

inline constexpr float kBaseMouseSensitivityRadians = 0.0025F;

[[nodiscard]] constexpr float relativeMouseYaw(
  float currentYawRadians,
  float mouseDeltaX,
  float sensitivity
) {
  return currentYawRadians -
    (mouseDeltaX * kBaseMouseSensitivityRadians * sensitivity);
}

} // namespace lg
