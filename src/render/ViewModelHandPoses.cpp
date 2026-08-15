#include "render/ViewModelHandPoses.hpp"

#include <cmath>

namespace lg {
namespace {

constexpr ViewModelHandMeshPose kRight =
  ViewModelHandMeshPose::RightTriggerGrip;
constexpr ViewModelHandMeshPose kClosed =
  ViewModelHandMeshPose::LeftClosedSupport;
constexpr ViewModelHandMeshPose kOpen =
  ViewModelHandMeshPose::LeftOpenSupport;

// Every row uses grip and support points in the final weapon frame. The mesh
// origin sits at that point, so these values describe the intended contact on
// each model instead of serving as a common screen-space offset.
constexpr std::array<ViewModelHandPose, kWeaponCount> kPoses = {{
  // Lightning Gun: trigger grip and open palm under the front receiver.
  {true, {kRight, {{0.12F, 0.070F, -0.04F}, {0.02F, 0.00F, 0.04F}, 0.25F}},
   true, {kOpen, {{0.50F, -0.080F, 0.03F}, {-0.03F, 0.02F, -0.05F}, 0.25F}}},
  // Railgun: rear trigger grip and closed support below the fore-end.
  {true, {kRight, {{0.00F, 0.060F, -0.01F}, {0.00F, 0.02F, 0.03F}, 0.23F}},
   true, {kClosed, {{0.42F, -0.070F, -0.04F}, {-0.02F, 0.01F, -0.04F}, 0.25F}}},
  // Rocket Launcher: authored rear and forward underside sockets.
  {true, {kRight, {{-0.58F, 0.070F, 0.00F}, {0.02F, 0.00F, -0.16F}, 0.24F}},
   true, {kClosed, {{0.43F, -0.080F, -0.10F}, {-0.02F, 0.01F, -0.03F}, 0.26F}}},
  // Machine Gun: rear grip and open support beneath the receiver, behind barrels.
  {true, {kRight, {{-0.18F, 0.070F, -0.02F}, {0.01F, 0.00F, 0.04F}, 0.24F}},
   true, {kOpen, {{0.27F, -0.080F, -0.02F}, {-0.03F, 0.02F, -0.04F}, 0.25F}}},
  // Shotgun: rear trigger grip and closed hand on the fore-end.
  {true, {kRight, {{-0.20F, 0.070F, -0.02F}, {0.01F, 0.00F, 0.03F}, 0.24F}},
   true, {kClosed, {{0.32F, -0.080F, -0.02F}, {-0.02F, 0.01F, -0.04F}, 0.25F}}},
  // Grenade Launcher: firing grip and closed support beneath the barrel body.
  {true, {kRight, {{0.12F, 0.070F, -0.05F}, {0.02F, 0.00F, 0.04F}, 0.25F}},
   true, {kClosed, {{0.45F, -0.080F, 0.02F}, {-0.03F, 0.02F, -0.04F}, 0.25F}}},
  // Plasma Gun: primary grip and open support clear of prongs and core.
  {true, {kRight, {{-0.42F, 0.070F, -0.02F}, {0.02F, 0.00F, -0.16F}, 0.24F}},
   true, {kOpen, {{0.04F, -0.080F, -0.03F}, {-0.03F, 0.01F, -0.04F}, 0.25F}}},
  // Freeze Gun: rear grip and open support below its optical core.
  {true, {kRight, {{-0.39F, 0.070F, -0.07F}, {0.02F, 0.00F, -0.16F}, 0.24F}},
   true, {kOpen, {{0.28F, -0.080F, 0.03F}, {-0.03F, 0.02F, -0.05F}, 0.25F}}},
  // Revolver: one clean trigger hand at the grip.
  {true, {kRight, {{-0.23F, 0.060F, -0.10F}, {0.01F, 0.00F, 0.02F}, 0.24F}},
   false, {kClosed, {}}},
}};

[[nodiscard]] bool finiteTransform(const ViewModelHandTransform& transform) {
  return std::isfinite(transform.translation.x) &&
    std::isfinite(transform.translation.y) &&
    std::isfinite(transform.translation.z) &&
    std::isfinite(transform.rotationRadians.x) &&
    std::isfinite(transform.rotationRadians.y) &&
    std::isfinite(transform.rotationRadians.z) &&
    std::isfinite(transform.scale) &&
    transform.scale > 0.0F;
}

} // namespace

const ViewModelHandPose& viewModelHandPose(Weapon weapon) {
  return kPoses[weaponIndex(weapon)];
}

bool viewModelHandPosesAreFinite() {
  for (const ViewModelHandPose& pose : kPoses) {
    if (!finiteTransform(pose.right.transform) ||
        !finiteTransform(pose.left.transform)) {
      return false;
    }
  }
  return true;
}

} // namespace lg
