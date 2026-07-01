#include "render/Perspective.hpp"

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

bool nearlyEqual(float lhs, float rhs, float epsilon = 0.0001F) {
  return std::fabs(lhs - rhs) <= epsilon;
}

} // namespace

int main() {
  int failures = 0;

  {
    const lg::PerspectiveCamera camera =
      lg::makePerspectiveCamera({}, 0.0F, 0.0F, 90.0F, 1.0F);
    lg::ProjectedPoint projected;
    failures += expect(
      lg::projectPerspectivePoint(camera, {10.0F, 0.0F, 0.0F}, projected) &&
        nearlyEqual(projected.x, 0.0F) &&
        nearlyEqual(projected.y, 0.0F),
      "forward point should project to screen center"
    );
    failures += expect(
      lg::projectPerspectivePoint(camera, {10.0F, -5.0F, 0.0F}, projected) &&
        projected.x > 0.0F,
      "camera-right point should project right"
    );
    failures += expect(
      !lg::projectPerspectivePoint(camera, {-1.0F, 0.0F, 0.0F}, projected),
      "point behind camera should be rejected"
    );
  }

  {
    constexpr float kHalfPi = 1.57079632679F;
    const lg::PerspectiveCamera camera =
      lg::makePerspectiveCamera({}, kHalfPi, 0.0F, 90.0F, 1.0F);
    lg::ProjectedPoint projected;
    failures += expect(
      lg::projectPerspectivePoint(camera, {0.0F, 10.0F, 0.0F}, projected) &&
        nearlyEqual(projected.x, 0.0F),
      "yaw should rotate the forward projection"
    );
  }

  {
    const lg::PerspectiveCamera camera =
      lg::makePerspectiveCamera({}, 0.0F, 0.0F, 90.0F, 2.0F);
    lg::ProjectedPoint projected;
    failures += expect(
      lg::projectPerspectivePoint(camera, {10.0F, -5.0F, 0.0F}, projected) &&
        nearlyEqual(projected.x, 0.25F),
      "aspect ratio should narrow horizontal normalized projection"
    );
  }

  {
    const lg::PerspectiveCamera camera =
      lg::makePerspectiveCamera({}, 0.0F, 0.0F, 90.0F, 1.0F);
    lg::Vec3 start = {-1.0F, 0.0F, 0.0F};
    lg::Vec3 end = {1.0F, 0.0F, 0.0F};
    failures += expect(
      lg::clipPerspectiveLine(camera, start, end) &&
        nearlyEqual(start.x, camera.nearPlane),
      "line crossing near plane should be clipped"
    );
    start = {-2.0F, 0.0F, 0.0F};
    end = {-1.0F, 0.0F, 0.0F};
    failures += expect(
      !lg::clipPerspectiveLine(camera, start, end),
      "line behind near plane should be rejected"
    );
  }

  {
    const lg::PerspectiveCamera camera =
      lg::makePerspectiveCamera({}, 0.0F, 0.0F, 90.0F, 1.0F);
    failures += expect(
      lg::sphereIntersectsPerspectiveFrustum(camera, {10.0F, 0.0F, 0.0F}, 0.5F),
      "sphere directly in front of camera should be visible"
    );
    failures += expect(
      !lg::sphereIntersectsPerspectiveFrustum(camera, {-1.0F, 0.0F, 0.0F}, 0.25F),
      "sphere completely behind camera should be culled"
    );
    failures += expect(
      !lg::sphereIntersectsPerspectiveFrustum(camera, {10.0F, 12.0F, 0.0F}, 0.5F) &&
        !lg::sphereIntersectsPerspectiveFrustum(camera, {10.0F, -12.0F, 0.0F}, 0.5F),
      "spheres beyond the left and right frustum planes should be culled"
    );
    failures += expect(
      !lg::sphereIntersectsPerspectiveFrustum(camera, {10.0F, 0.0F, 12.0F}, 0.5F) &&
        !lg::sphereIntersectsPerspectiveFrustum(camera, {10.0F, 0.0F, -12.0F}, 0.5F),
      "spheres beyond the top and bottom frustum planes should be culled"
    );
    failures += expect(
      lg::sphereIntersectsPerspectiveFrustum(camera, {10.0F, 10.4F, 0.0F}, 0.5F),
      "sphere intersecting an edge plane should be kept"
    );
    failures += expect(
      lg::sphereIntersectsPerspectiveFrustum(camera, {0.02F, 0.0F, 0.0F}, 0.05F),
      "sphere intersecting the near plane should be kept"
    );
    failures += expect(
      lg::sphereIntersectsPerspectiveFrustum(camera, {10.0F, -11.5F, 0.0F}, 2.0F),
      "larger sphere near an edge should not be falsely culled"
    );
  }

  return failures == 0 ? 0 : 1;
}
