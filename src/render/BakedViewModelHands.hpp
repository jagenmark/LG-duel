#pragma once

#include "render/BakedWeaponModels.hpp"

#include <array>

namespace lg {
namespace detail {

// Repo-native low-poly glove source. Weapon-local coordinates use +X forward,
// +Y right and +Z up. Each pose has a palm, thumb, grouped fingers, wrist,
// cuff and a short forearm. Geometry is built at compile time and uploaded once.
template <std::size_t Count>
constexpr void appendTaperedGlovePrism(
  std::array<BakedWeaponModelTriangle, Count>& out,
  std::size_t& cursor,
  Vec3 start,
  Vec3 end,
  Vec3 widthAxis,
  Vec3 heightAxis,
  float startHalfWidth,
  float startHalfHeight,
  float endHalfWidth,
  float endHalfHeight,
  RenderColor color
) {
  constexpr std::array<float, 6> kRingCos = {{
    1.0F, 0.5F, -0.5F, -1.0F, -0.5F, 0.5F,
  }};
  constexpr float kSqrtThreeOverTwo = 0.8660254F;
  constexpr std::array<float, 6> kRingSin = {{
    0.0F,
    kSqrtThreeOverTwo,
    kSqrtThreeOverTwo,
    0.0F,
    -kSqrtThreeOverTwo,
    -kSqrtThreeOverTwo,
  }};
  std::array<Vec3, 6> startRing = {};
  std::array<Vec3, 6> endRing = {};
  for (std::size_t ringIndex = 0U; ringIndex < 6U; ++ringIndex) {
    startRing[ringIndex] = start +
      widthAxis * (kRingCos[ringIndex] * startHalfWidth) +
      heightAxis * (kRingSin[ringIndex] * startHalfHeight);
    endRing[ringIndex] = end +
      widthAxis * (kRingCos[ringIndex] * endHalfWidth) +
      heightAxis * (kRingSin[ringIndex] * endHalfHeight);
  }
  for (std::size_t ringIndex = 0U; ringIndex < 6U; ++ringIndex) {
    const std::size_t next = (ringIndex + 1U) % 6U;
    out[cursor++] = {{
      startRing[ringIndex], endRing[next], endRing[ringIndex]
    }, color};
    out[cursor++] = {{
      startRing[ringIndex], startRing[next], endRing[next]
    }, color};
  }
  for (std::size_t capTriangle = 0U; capTriangle < 4U; ++capTriangle) {
    out[cursor++] = {{
      startRing[0U], startRing[capTriangle + 1U], startRing[capTriangle + 2U]
    }, color};
    out[cursor++] = {{
      endRing[0U], endRing[capTriangle + 2U], endRing[capTriangle + 1U]
    }, color};
  }
}

inline constexpr RenderColor kGloveForearm = {46, 60, 78, 255};
inline constexpr RenderColor kGloveCuff = {82, 101, 122, 255};
inline constexpr RenderColor kGlovePalm = {64, 82, 105, 255};
inline constexpr RenderColor kGloveFinger = {78, 99, 121, 255};
inline constexpr RenderColor kGloveFingerLight = {105, 128, 149, 255};
inline constexpr RenderColor kGloveThumb = {94, 116, 139, 255};

[[nodiscard]] constexpr std::array<BakedWeaponModelTriangle, 140>
makeRightTriggerGrip() {
  std::array<BakedWeaponModelTriangle, 140> result = {};
  std::size_t cursor = 0U;
  // A short forearm and cuff leave the grip through the lower frame edge.
  appendTaperedGlovePrism(result, cursor,
    {-0.20F, 0.02F, -0.52F}, {-0.10F, 0.02F, -0.20F},
    {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F},
    0.075F, 0.060F, 0.065F, 0.052F, kGloveForearm);
  appendTaperedGlovePrism(result, cursor,
    {-0.12F, 0.02F, -0.24F}, {-0.06F, 0.02F, -0.14F},
    {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F},
    0.095F, 0.074F, 0.105F, 0.080F, kGloveCuff);
  // The broad palm and grouped fingers form one compact trigger-hand outline.
  appendTaperedGlovePrism(result, cursor,
    {-0.08F, 0.02F, -0.16F}, {0.10F, 0.02F, 0.00F},
    {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F},
    0.115F, 0.098F, 0.105F, 0.082F, kGlovePalm);
  appendTaperedGlovePrism(result, cursor,
    {0.03F, -0.01F, 0.00F}, {0.10F, -0.01F, -0.17F},
    {0.0F, 1.0F, 0.0F}, {1.0F, 0.0F, 0.0F},
    0.105F, 0.056F, 0.090F, 0.046F, kGloveFinger);
  // A shallow knuckle pad keeps the back of the hand clear at small size.
  appendTaperedGlovePrism(result, cursor,
    {0.00F, 0.02F, -0.01F}, {0.13F, 0.02F, 0.045F},
    {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F},
    0.098F, 0.038F, 0.085F, 0.030F, kGloveFingerLight);
  // One narrow trigger finger breaks the mitten outline without making a claw.
  appendTaperedGlovePrism(result, cursor,
    {0.04F, -0.105F, -0.01F}, {0.18F, -0.115F, -0.09F},
    {1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F},
    0.047F, 0.040F, 0.035F, 0.031F, kGloveFinger);
  // The right thumb crosses the inner side of the grip.
  appendTaperedGlovePrism(result, cursor,
    {-0.03F, -0.095F, -0.02F}, {0.08F, -0.165F, -0.085F},
    {1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F},
    0.050F, 0.047F, 0.038F, 0.036F, kGloveThumb);
  return result;
}

[[nodiscard]] constexpr std::array<BakedWeaponModelTriangle, 140>
makeLeftClosedSupport() {
  std::array<BakedWeaponModelTriangle, 140> result = {};
  std::size_t cursor = 0U;
  appendTaperedGlovePrism(result, cursor,
    {-0.28F, -0.02F, -0.50F}, {-0.12F, -0.02F, -0.20F},
    {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F},
    0.080F, 0.064F, 0.068F, 0.054F, kGloveForearm);
  appendTaperedGlovePrism(result, cursor,
    {-0.14F, -0.02F, -0.24F}, {-0.06F, -0.02F, -0.14F},
    {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F},
    0.098F, 0.076F, 0.108F, 0.082F, kGloveCuff);
  // The palm cups the underside while a grouped-finger wedge closes around it.
  appendTaperedGlovePrism(result, cursor,
    {-0.08F, -0.02F, -0.16F}, {0.11F, -0.02F, -0.01F},
    {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F},
    0.118F, 0.096F, 0.108F, 0.080F, kGlovePalm);
  appendTaperedGlovePrism(result, cursor,
    {0.03F, 0.01F, 0.00F}, {0.11F, 0.01F, -0.16F},
    {0.0F, 1.0F, 0.0F}, {1.0F, 0.0F, 0.0F},
    0.108F, 0.056F, 0.092F, 0.046F, kGloveFinger);
  appendTaperedGlovePrism(result, cursor,
    {0.00F, -0.02F, -0.01F}, {0.13F, -0.02F, 0.045F},
    {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F},
    0.100F, 0.038F, 0.086F, 0.030F, kGloveFingerLight);
  appendTaperedGlovePrism(result, cursor,
    {0.04F, 0.105F, -0.01F}, {0.18F, 0.115F, -0.09F},
    {1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F},
    0.047F, 0.040F, 0.035F, 0.031F, kGloveFinger);
  // A left thumb on the inner (+Y) side makes handedness clear.
  appendTaperedGlovePrism(result, cursor,
    {-0.03F, 0.095F, -0.02F}, {0.08F, 0.165F, -0.085F},
    {1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F},
    0.050F, 0.047F, 0.038F, 0.036F, kGloveThumb);
  return result;
}

[[nodiscard]] constexpr std::array<BakedWeaponModelTriangle, 140>
makeLeftOpenSupport() {
  std::array<BakedWeaponModelTriangle, 140> result = {};
  std::size_t cursor = 0U;
  appendTaperedGlovePrism(result, cursor,
    {-0.30F, -0.02F, -0.50F}, {-0.12F, -0.02F, -0.20F},
    {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F},
    0.082F, 0.066F, 0.070F, 0.055F, kGloveForearm);
  appendTaperedGlovePrism(result, cursor,
    {-0.14F, -0.02F, -0.24F}, {-0.06F, -0.02F, -0.14F},
    {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F},
    0.100F, 0.078F, 0.112F, 0.084F, kGloveCuff);
  appendTaperedGlovePrism(result, cursor,
    {-0.08F, -0.02F, -0.16F}, {0.12F, -0.02F, -0.02F},
    {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F},
    0.122F, 0.098F, 0.116F, 0.082F, kGlovePalm);
  // Two broad groups spread under a receiver without reading as loose rods.
  appendTaperedGlovePrism(result, cursor,
    {0.07F, -0.075F, -0.03F}, {0.27F, -0.085F, -0.01F},
    {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F},
    0.065F, 0.050F, 0.055F, 0.040F, kGloveFinger);
  appendTaperedGlovePrism(result, cursor,
    {0.07F, 0.055F, -0.03F}, {0.29F, 0.065F, -0.005F},
    {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F},
    0.062F, 0.049F, 0.052F, 0.039F, kGloveFingerLight);
  // A low knuckle pad ties the open groups back into the palm.
  appendTaperedGlovePrism(result, cursor,
    {0.02F, -0.01F, -0.02F}, {0.15F, -0.01F, 0.04F},
    {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F},
    0.102F, 0.036F, 0.088F, 0.029F, kGloveFinger);
  appendTaperedGlovePrism(result, cursor,
    {-0.03F, 0.095F, -0.02F}, {0.10F, 0.165F, -0.07F},
    {1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F},
    0.052F, 0.047F, 0.040F, 0.036F, kGloveThumb);
  return result;
}

} // namespace detail

inline constexpr auto kViewModelRightTriggerGrip = detail::makeRightTriggerGrip();
inline constexpr auto kViewModelLeftClosedSupport = detail::makeLeftClosedSupport();
inline constexpr auto kViewModelLeftOpenSupport = detail::makeLeftOpenSupport();

} // namespace lg
