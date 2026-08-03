#pragma once

#include "shared/Math.hpp"

#include <algorithm>
#include <cmath>

namespace lg {

// CPU reference for the common GLSL point-light diffuse term. Renderer tests
// use it as a numeric oracle for material-quality behavior.
struct PointLightResponse {
  Vec3 diffuse = {};
  Vec3 enhanced = {};
};

[[nodiscard]] constexpr Vec3 multiplyComponents(Vec3 lhs, Vec3 rhs) {
  return {lhs.x * rhs.x, lhs.y * rhs.y, lhs.z * rhs.z};
}

[[nodiscard]] inline PointLightResponse pointLightResponseReference(
  bool pointLightsEnabled,
  int materialQuality,
  Vec3 albedo,
  Vec3 radiance,
  float nDotL,
  float normalHalfDot
) {
  if (!pointLightsEnabled) {
    return {};
  }

  const float diffuseNdotL = std::max(nDotL, 0.0F);
  PointLightResponse response;
  response.diffuse = multiplyComponents(albedo, radiance) * diffuseNdotL;
  if (materialQuality == 2 && diffuseNdotL > 0.0F) {
    const float highlight = std::pow(
      std::max(normalHalfDot, 0.0F),
      24.0F
    );
    response.enhanced = radiance * (highlight * diffuseNdotL * 0.12F);
  }
  return response;
}

}  // namespace lg
