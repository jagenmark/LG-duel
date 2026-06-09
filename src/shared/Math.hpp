#pragma once

#include <algorithm>
#include <cmath>

namespace lg {

struct Vec3 {
  float x = 0.0F;
  float y = 0.0F;
  float z = 0.0F;
};

[[nodiscard]] constexpr Vec3 operator+(Vec3 lhs, Vec3 rhs) {
  return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

[[nodiscard]] constexpr Vec3 operator-(Vec3 lhs, Vec3 rhs) {
  return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

[[nodiscard]] constexpr Vec3 operator*(Vec3 value, float scalar) {
  return {value.x * scalar, value.y * scalar, value.z * scalar};
}

[[nodiscard]] constexpr Vec3 operator*(float scalar, Vec3 value) {
  return value * scalar;
}

[[nodiscard]] constexpr Vec3 operator/(Vec3 value, float scalar) {
  return {value.x / scalar, value.y / scalar, value.z / scalar};
}

constexpr Vec3& operator+=(Vec3& lhs, Vec3 rhs) {
  lhs = lhs + rhs;
  return lhs;
}

constexpr Vec3& operator-=(Vec3& lhs, Vec3 rhs) {
  lhs = lhs - rhs;
  return lhs;
}

constexpr Vec3& operator*=(Vec3& lhs, float scalar) {
  lhs = lhs * scalar;
  return lhs;
}

[[nodiscard]] constexpr float dot(Vec3 lhs, Vec3 rhs) {
  return (lhs.x * rhs.x) + (lhs.y * rhs.y) + (lhs.z * rhs.z);
}

[[nodiscard]] inline float length(Vec3 value) {
  return std::sqrt(dot(value, value));
}

[[nodiscard]] inline Vec3 normalize(Vec3 value) {
  const float valueLength = length(value);
  if (valueLength <= 0.00001F) {
    return {};
  }

  return value / valueLength;
}

[[nodiscard]] constexpr float clamp(float value, float minValue, float maxValue) {
  return value < minValue ? minValue : (value > maxValue ? maxValue : value);
}

[[nodiscard]] inline Vec3 yawForward(float yawRadians) {
  return {std::cos(yawRadians), std::sin(yawRadians), 0.0F};
}

[[nodiscard]] inline Vec3 yawRight(float yawRadians) {
  return {-std::sin(yawRadians), std::cos(yawRadians), 0.0F};
}

[[nodiscard]] inline Vec3 cameraForward(float yawRadians, float pitchRadians) {
  const float pitchCos = std::cos(pitchRadians);
  return {
    std::cos(yawRadians) * pitchCos,
    std::sin(yawRadians) * pitchCos,
    std::sin(pitchRadians),
  };
}

} // namespace lg
