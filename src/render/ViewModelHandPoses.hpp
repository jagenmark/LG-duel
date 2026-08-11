#pragma once

#include "shared/Math.hpp"
#include "sim/UserCommand.hpp"

#include <array>
#include <cstdint>

namespace lg {

enum class ViewModelHandMeshPose : std::uint8_t {
  RightTriggerGrip,
  LeftClosedSupport,
  LeftOpenSupport,
};

struct ViewModelHandTransform {
  Vec3 translation = {};
  Vec3 rotationRadians = {};
  // This is a fixed viewmodel-space size. It does not inherit the weapon's
  // visual scale, which differs sharply between the rocket and rail models.
  float scale = 0.30F;
};

struct ViewModelHandPlacement {
  ViewModelHandMeshPose meshPose = ViewModelHandMeshPose::RightTriggerGrip;
  ViewModelHandTransform transform = {};
};

struct ViewModelHandPose {
  bool showRightHand = true;
  ViewModelHandPlacement right = {};
  bool showLeftHand = true;
  ViewModelHandPlacement left = {};
};

[[nodiscard]] const ViewModelHandPose& viewModelHandPose(Weapon weapon);
[[nodiscard]] bool viewModelHandPosesAreFinite();
[[nodiscard]] constexpr std::size_t viewModelHandMeshCount() { return 3U; }

} // namespace lg
