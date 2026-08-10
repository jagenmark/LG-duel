#pragma once

#include "render/BakedWeaponModels.hpp"

#include <array>

namespace lg {
namespace detail {

// Repo-native low-poly glove source. Coordinates use the baked weapon-model
// convention: +X forward, +Y right, +Z up. Each mesh has seven tapered volumes:
// short forearm, cuff, palm, three grouped fingers, and an offset thumb. The
// source is static data; it never creates geometry during a rendered frame.
template <std::size_t Count>
constexpr void appendTaperedGlovePrism(
  std::array<BakedWeaponModelTriangle, Count>& out,
  std::size_t& cursor,
  float minimumX,
  float maximumX,
  float minimumHalfWidth,
  float minimumHalfHeight,
  float maximumHalfWidth,
  float maximumHalfHeight,
  float centerY,
  float centerZ,
  RenderColor color
) {
  const std::array<Vec3, 8> points = {{
    {minimumX, centerY - minimumHalfWidth, centerZ - minimumHalfHeight},
    {maximumX, centerY - maximumHalfWidth, centerZ - maximumHalfHeight},
    {maximumX, centerY + maximumHalfWidth, centerZ - maximumHalfHeight},
    {minimumX, centerY + minimumHalfWidth, centerZ - minimumHalfHeight},
    {minimumX, centerY - minimumHalfWidth, centerZ + minimumHalfHeight},
    {maximumX, centerY - maximumHalfWidth, centerZ + maximumHalfHeight},
    {maximumX, centerY + maximumHalfWidth, centerZ + maximumHalfHeight},
    {minimumX, centerY + minimumHalfWidth, centerZ + minimumHalfHeight},
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

[[nodiscard]] constexpr std::array<BakedWeaponModelTriangle, 84> makeGlove(
  bool left
) {
  std::array<BakedWeaponModelTriangle, 84> result = {};
  std::size_t cursor = 0U;
  constexpr RenderColor forearm = {42, 56, 73, 255};
  constexpr RenderColor cuff = {78, 95, 113, 255};
  constexpr RenderColor palm = {54, 72, 92, 255};
  constexpr RenderColor fingers = {76, 99, 121, 255};
  constexpr RenderColor fingerHighlight = {96, 118, 138, 255};
  constexpr RenderColor thumb = {83, 106, 126, 255};
  const float thumbSide = left ? -1.0F : 1.0F;
  // A short, flared forearm lets its cuff end remain in the lower frame.
  appendTaperedGlovePrism(result, cursor, -0.40F, -0.16F, 0.105F, 0.078F,
    0.075F, 0.060F, 0.0F, -0.010F, forearm);
  appendTaperedGlovePrism(result, cursor, -0.18F, -0.095F, 0.090F, 0.075F,
    0.105F, 0.090F, 0.0F, -0.008F, cuff);
  // The broad base and narrow finger end give the palm a readable wedge.
  appendTaperedGlovePrism(result, cursor, -0.10F, 0.095F, 0.115F, 0.092F,
    0.135F, 0.060F, 0.0F, 0.0F, palm);
  // Three close finger wedges keep a low-poly grouped grip legible in frame.
  appendTaperedGlovePrism(result, cursor, 0.080F, 0.245F, 0.034F, 0.056F,
    0.026F, 0.041F, -0.078F, 0.008F, fingers);
  appendTaperedGlovePrism(result, cursor, 0.090F, 0.270F, 0.035F, 0.058F,
    0.027F, 0.042F, 0.0F, 0.010F, fingerHighlight);
  appendTaperedGlovePrism(result, cursor, 0.080F, 0.235F, 0.033F, 0.054F,
    0.025F, 0.039F, 0.076F, 0.008F, fingers);
  // The offset, high-contrast thumb must remain visible beside the receiver.
  appendTaperedGlovePrism(result, cursor, -0.070F, 0.155F, 0.070F, 0.058F,
    0.055F, 0.046F, thumbSide * 0.178F, -0.042F, thumb);
  return result;
}

} // namespace detail

inline constexpr auto kViewModelRightGloveModel = detail::makeGlove(false);
inline constexpr auto kViewModelLeftGloveModel = detail::makeGlove(true);

} // namespace lg
