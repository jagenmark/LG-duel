#pragma once

#include <cmath>

namespace lg {

inline constexpr float kMouseDegreesToRadians = 0.01745329252F;
inline constexpr float kQuakeLiveDefaultSensitivity = 5.0F;
inline constexpr float kQuakeLiveMouseYawDegrees = 0.022F;
inline constexpr float kBaseMouseSensitivityRadians =
  kQuakeLiveMouseYawDegrees * kMouseDegreesToRadians;
inline constexpr float kLegacyProjectMouseSensitivityRadians = 0.0025F;
inline constexpr float kLegacyToQuakeLiveSensitivityScale =
  kLegacyProjectMouseSensitivityRadians / kBaseMouseSensitivityRadians;

struct MouseAimSettings {
  float sensitivity = kQuakeLiveDefaultSensitivity;
  float zoomMultiplier = 1.0F;
  float mouseAccel = 0.0F;
  float mouseAccelPower = 2.0F;
  float mouseAccelOffset = 0.0F;
  float mouseSensitivityCap = 0.0F;
};

struct MouseAimDelta {
  float yawRadians = 0.0F;
  float pitchRadians = 0.0F;
};

[[nodiscard]] inline float mouseAimFrameMilliseconds(float frameSeconds) {
  if (!std::isfinite(frameSeconds)) {
    return 1.0F;
  }

  const float frameMilliseconds = frameSeconds * 1000.0F;
  if (frameMilliseconds < 1.0F) {
    return 1.0F;
  }
  if (frameMilliseconds > 200.0F) {
    return 200.0F;
  }
  return frameMilliseconds;
}

[[nodiscard]] inline float quakeLiveMouseSensitivity(
  float mouseDeltaX,
  float mouseDeltaY,
  float frameSeconds,
  const MouseAimSettings& settings
) {
  float acceleratedSensitivity = settings.sensitivity;
  if (settings.mouseAccel > 0.0F) {
    const float mouseSpeed =
      std::hypot(mouseDeltaX, mouseDeltaY) / mouseAimFrameMilliseconds(frameSeconds);
    const float offsetSpeed = mouseSpeed - settings.mouseAccelOffset;
    if (offsetSpeed > 0.0F) {
      // QL accel: B + (A * max(v - c, 0))^(P - 1), then optional sens cap.
      const float accelBase = settings.mouseAccel * offsetSpeed;
      if (accelBase > 0.0F) {
        acceleratedSensitivity +=
          static_cast<float>(std::pow(accelBase, settings.mouseAccelPower - 1.0F));
      }
    }
  }

  if (settings.mouseSensitivityCap > 0.0F) {
    acceleratedSensitivity =
      acceleratedSensitivity > settings.mouseSensitivityCap
      ? settings.mouseSensitivityCap
      : acceleratedSensitivity;
  }
  return acceleratedSensitivity * settings.zoomMultiplier;
}

[[nodiscard]] inline MouseAimDelta quakeLiveMouseAimDelta(
  float mouseDeltaX,
  float mouseDeltaY,
  float frameSeconds,
  const MouseAimSettings& settings
) {
  const float sensitivity =
    quakeLiveMouseSensitivity(mouseDeltaX, mouseDeltaY, frameSeconds, settings);
  return {
    mouseDeltaX * kBaseMouseSensitivityRadians * sensitivity,
    mouseDeltaY * kBaseMouseSensitivityRadians * sensitivity,
  };
}

[[nodiscard]] constexpr float relativeMouseYaw(
  float currentYawRadians,
  float mouseDeltaX,
  float sensitivity
) {
  return currentYawRadians -
    (mouseDeltaX * kBaseMouseSensitivityRadians * sensitivity);
}

} // namespace lg
