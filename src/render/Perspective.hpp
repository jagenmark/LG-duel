#pragma once

#include "shared/Math.hpp"

#include <algorithm>
#include <cmath>

namespace lg {

struct PerspectiveCamera {
  Vec3 position = {};
  Vec3 forward = {1.0F, 0.0F, 0.0F};
  Vec3 right = {0.0F, -1.0F, 0.0F};
  Vec3 up = {0.0F, 0.0F, 1.0F};
  float focalLength = 1.0F;
  float aspectRatio = 1.0F;
  float nearPlane = 0.05F;
};

struct ProjectedPoint {
  float x = 0.0F;
  float y = 0.0F;
};

[[nodiscard]] inline PerspectiveCamera makePerspectiveCamera(
  Vec3 position,
  float yawRadians,
  float pitchRadians,
  float fieldOfViewDegrees,
  float aspectRatio
) {
  const Vec3 forward = cameraForward(yawRadians, pitchRadians);
  const Vec3 right = yawRight(yawRadians);
  const Vec3 up = normalize(Vec3{
    -forward.z * std::cos(yawRadians),
    -forward.z * std::sin(yawRadians),
    std::cos(pitchRadians),
  });
  constexpr float kPi = 3.14159265359F;
  const float halfFovRadians =
    fieldOfViewDegrees * (kPi / 180.0F) * 0.5F;
  return {
    position,
    forward,
    right,
    up,
    1.0F / std::tan(halfFovRadians),
    aspectRatio,
    0.05F,
  };
}

[[nodiscard]] inline Vec3 perspectiveCameraSpace(
  const PerspectiveCamera& camera,
  Vec3 worldPosition
) {
  const Vec3 offset = worldPosition - camera.position;
  return {
    dot(offset, camera.right),
    dot(offset, camera.up),
    dot(offset, camera.forward),
  };
}

[[nodiscard]] inline bool projectPerspectivePoint(
  const PerspectiveCamera& camera,
  Vec3 worldPosition,
  ProjectedPoint& projected
) {
  const Vec3 view = perspectiveCameraSpace(camera, worldPosition);
  if (view.z < camera.nearPlane) {
    return false;
  }
  projected.x =
    (view.x * camera.focalLength) / (view.z * camera.aspectRatio);
  projected.y = (view.y * camera.focalLength) / view.z;
  return true;
}

[[nodiscard]] inline bool sphereIntersectsPerspectiveFrustum(
  const PerspectiveCamera& camera,
  Vec3 center,
  float radius
) {
  const Vec3 view = perspectiveCameraSpace(camera, center);
  const float safeRadius = std::max(0.0F, radius);
  if (view.z + safeRadius < camera.nearPlane) {
    return false;
  }

  const float horizontalScale = camera.aspectRatio / camera.focalLength;
  const float verticalScale = 1.0F / camera.focalLength;
  const auto outsidePlane =
    [safeRadius](float signedDistance, float normalLength) {
      return signedDistance < -safeRadius * normalLength;
    };

  if (outsidePlane(view.x + view.z * horizontalScale, std::hypot(1.0F, horizontalScale))) {
    return false;
  }
  if (outsidePlane(-view.x + view.z * horizontalScale, std::hypot(1.0F, horizontalScale))) {
    return false;
  }
  if (outsidePlane(view.y + view.z * verticalScale, std::hypot(1.0F, verticalScale))) {
    return false;
  }
  if (outsidePlane(-view.y + view.z * verticalScale, std::hypot(1.0F, verticalScale))) {
    return false;
  }
  return true;
}

[[nodiscard]] inline bool clipPerspectiveLine(
  const PerspectiveCamera& camera,
  Vec3& start,
  Vec3& end
) {
  const float startDepth =
    dot(start - camera.position, camera.forward);
  const float endDepth =
    dot(end - camera.position, camera.forward);
  if (startDepth < camera.nearPlane && endDepth < camera.nearPlane) {
    return false;
  }
  if (startDepth >= camera.nearPlane && endDepth >= camera.nearPlane) {
    return true;
  }

  const float t =
    (camera.nearPlane - startDepth) / (endDepth - startDepth);
  const Vec3 clipped = start + ((end - start) * t);
  if (startDepth < camera.nearPlane) {
    start = clipped;
  } else {
    end = clipped;
  }
  return true;
}

} // namespace lg
