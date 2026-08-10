#include "render/ViewModelHandPoses.hpp"

#include <cmath>

namespace lg {
namespace {

// Values are relative to the final, animated weapon frame. Pitch turns the
// glove's forearm toward the lower screen edge; each weapon then gets its own
// wrist angle and grip/support placement. They contain no gameplay grip data.
constexpr std::array<ViewModelHandPose, kWeaponCount> kPoses = {{
  // Lightning gun
  {true, {{-0.10F, 0.14F, 0.14F}, {-1.48F, 0.03F, 0.12F}}, true, {{0.18F, -0.16F, 0.18F}, {-1.43F, -0.05F, -0.16F}}},
  // Railgun
  {true, {{-0.10F, 0.14F, 0.13F}, {-1.46F, 0.06F, 0.16F}}, true, {{0.29F, -0.16F, 0.16F}, {-1.40F, -0.08F, -0.20F}}},
  // Rocket launcher
  {true, {{-0.05F, 0.15F, 0.14F}, {-1.44F, 0.09F, 0.10F}}, true, {{0.24F, -0.18F, 0.16F}, {-1.38F, -0.10F, -0.22F}}},
  // Machine gun
  {true, {{-0.09F, 0.14F, 0.15F}, {-1.47F, 0.04F, 0.14F}}, true, {{0.17F, -0.17F, 0.18F}, {-1.41F, -0.06F, -0.18F}}},
  // Shotgun
  {true, {{-0.10F, 0.15F, 0.14F}, {-1.45F, 0.05F, 0.18F}}, true, {{0.25F, -0.18F, 0.17F}, {-1.39F, -0.09F, -0.22F}}},
  // Grenade launcher
  {true, {{-0.08F, 0.14F, 0.14F}, {-1.42F, 0.10F, 0.11F}}, true, {{0.19F, -0.17F, 0.17F}, {-1.37F, -0.08F, -0.17F}}},
  // Plasma gun
  {true, {{-0.09F, 0.14F, 0.15F}, {-1.46F, 0.06F, 0.15F}}, true, {{0.19F, -0.17F, 0.17F}, {-1.39F, -0.07F, -0.19F}}},
  // Freeze gun
  {true, {{-0.10F, 0.14F, 0.14F}, {-1.49F, 0.03F, 0.18F}}, true, {{0.25F, -0.18F, 0.17F}, {-1.37F, -0.10F, -0.23F}}},
  // Revolver: a one-handed first pass keeps the silhouette intentional.
  {true, {{-0.07F, 0.15F, 0.11F}, {-1.50F, 0.10F, 0.24F}}, false, {}},
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
