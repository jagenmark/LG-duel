#include "render/ViewModelHandPoses.hpp"

#include <cmath>

namespace lg {
namespace {

// Values are relative to the final, animated weapon frame. They deliberately
// contain no gameplay grip data and cover each selectable weapon once.
constexpr std::array<ViewModelHandPose, kWeaponCount> kPoses = {{
  // Lightning gun
  {true, {{-0.06F, 0.00F, -0.11F}, {0.00F, 0.00F, 0.00F}}, true, {{0.28F, -0.16F, 0.04F}, {0.10F, 0.00F, 0.08F}}},
  // Railgun
  {true, {{-0.06F, 0.01F, -0.11F}, {0.00F, 0.00F, 0.00F}}, true, {{0.42F, -0.18F, 0.02F}, {0.06F, 0.00F, 0.10F}}},
  // Rocket launcher
  {true, {{0.03F, 0.00F, -0.08F}, {0.00F, 0.00F, 0.00F}}, true, {{0.36F, -0.20F, 0.02F}, {0.08F, 0.00F, 0.11F}}},
  // Machine gun
  {true, {{-0.04F, 0.00F, -0.10F}, {0.00F, 0.00F, 0.00F}}, true, {{0.24F, -0.16F, 0.01F}, {0.06F, 0.00F, 0.08F}}},
  // Shotgun
  {true, {{-0.05F, 0.00F, -0.10F}, {0.00F, 0.00F, 0.00F}}, true, {{0.30F, -0.18F, 0.01F}, {0.07F, 0.00F, 0.10F}}},
  // Grenade launcher
  {true, {{-0.04F, 0.00F, -0.10F}, {0.00F, 0.00F, 0.00F}}, true, {{0.23F, -0.17F, 0.02F}, {0.08F, 0.00F, 0.08F}}},
  // Plasma gun
  {true, {{-0.04F, 0.00F, -0.10F}, {0.00F, 0.00F, 0.00F}}, true, {{0.22F, -0.17F, 0.02F}, {0.08F, 0.00F, 0.08F}}},
  // Freeze gun
  {true, {{-0.05F, 0.00F, -0.11F}, {0.00F, 0.00F, 0.00F}}, true, {{0.34F, -0.18F, 0.03F}, {0.08F, 0.00F, 0.10F}}},
  // Revolver: a one-handed first pass keeps the silhouette intentional.
  {true, {{-0.03F, 0.00F, -0.09F}, {0.00F, 0.00F, 0.00F}}, false, {}},
}};

[[nodiscard]] bool finiteTransform(const ViewModelHandTransform& transform) {
  return std::isfinite(transform.translation.x) &&
    std::isfinite(transform.translation.y) &&
    std::isfinite(transform.translation.z) &&
    std::isfinite(transform.rotationRadians.x) &&
    std::isfinite(transform.rotationRadians.y) &&
    std::isfinite(transform.rotationRadians.z);
}

} // namespace

const ViewModelHandPose& viewModelHandPose(Weapon weapon) {
  return kPoses[weaponIndex(weapon)];
}

bool viewModelHandPosesAreFinite() {
  for (const ViewModelHandPose& pose : kPoses) {
    if (!finiteTransform(pose.right) || !finiteTransform(pose.left)) return false;
  }
  return true;
}

} // namespace lg
