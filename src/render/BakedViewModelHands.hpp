#pragma once

#include "render/BakedWeaponModels.hpp"

#include <array>

namespace lg {
namespace detail {

// Repo-native low-poly glove source. Coordinates use the baked weapon-model
// convention: +X forward, +Y right, +Z up. Each mesh has five deliberately
// chunky boxes (forearm, cuff, palm, fingers, thumb) and no runtime geometry.
constexpr void appendGloveBox(
  std::array<BakedWeaponModelTriangle, 60>& out,
  std::size_t& cursor,
  Vec3 minimum,
  Vec3 maximum,
  RenderColor color
) {
  const std::array<Vec3, 8> points = {{
    {minimum.x, minimum.y, minimum.z}, {maximum.x, minimum.y, minimum.z},
    {maximum.x, maximum.y, minimum.z}, {minimum.x, maximum.y, minimum.z},
    {minimum.x, minimum.y, maximum.z}, {maximum.x, minimum.y, maximum.z},
    {maximum.x, maximum.y, maximum.z}, {minimum.x, maximum.y, maximum.z},
  }};
  constexpr std::array<std::array<std::size_t, 3>, 12> faces = {{
    {{0U, 2U, 1U}}, {{0U, 3U, 2U}}, {{4U, 5U, 6U}}, {{4U, 6U, 7U}},
    {{0U, 1U, 5U}}, {{0U, 5U, 4U}}, {{1U, 2U, 6U}}, {{1U, 6U, 5U}},
    {{2U, 3U, 7U}}, {{2U, 7U, 6U}}, {{3U, 0U, 4U}}, {{3U, 4U, 7U}},
  }};
  for (const auto& face : faces) {
    out[cursor++] = {{points[face[0]], points[face[1]], points[face[2]]}, color};
  }
}

[[nodiscard]] constexpr std::array<BakedWeaponModelTriangle, 60> makeGlove(
  bool left
) {
  std::array<BakedWeaponModelTriangle, 60> result = {};
  std::size_t cursor = 0U;
  constexpr RenderColor glove = {30, 35, 42, 255};
  constexpr RenderColor cuff = {53, 61, 70, 255};
  constexpr RenderColor knuckle = {42, 48, 56, 255};
  const float thumbSide = left ? -1.0F : 1.0F;
  appendGloveBox(result, cursor, {-0.56F, -0.070F, -0.095F}, {-0.19F, 0.070F, 0.070F}, glove);
  appendGloveBox(result, cursor, {-0.23F, -0.092F, -0.110F}, {-0.14F, 0.092F, 0.085F}, cuff);
  appendGloveBox(result, cursor, {-0.17F, -0.105F, -0.105F}, {0.05F, 0.105F, 0.100F}, glove);
  appendGloveBox(result, cursor, {0.03F, -0.098F, -0.074F}, {0.20F, 0.098F, 0.075F}, knuckle);
  appendGloveBox(
    result,
    cursor,
    {-0.06F, thumbSide * 0.072F - 0.050F, -0.145F},
    {0.12F, thumbSide * 0.072F + 0.050F, -0.015F},
    glove
  );
  return result;
}

} // namespace detail

inline constexpr auto kViewModelRightGloveModel = detail::makeGlove(false);
inline constexpr auto kViewModelLeftGloveModel = detail::makeGlove(true);

} // namespace lg
