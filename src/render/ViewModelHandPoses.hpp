#pragma once

#include "shared/Math.hpp"
#include "sim/UserCommand.hpp"

#include <array>

namespace lg {

struct ViewModelHandTransform {
  Vec3 translation = {};
  Vec3 rotationRadians = {};
};

struct ViewModelHandPose {
  bool showRightHand = true;
  ViewModelHandTransform right = {};
  bool showLeftHand = true;
  ViewModelHandTransform left = {};
};

[[nodiscard]] const ViewModelHandPose& viewModelHandPose(Weapon weapon);
[[nodiscard]] bool viewModelHandPosesAreFinite();
[[nodiscard]] constexpr std::size_t viewModelHandMeshCount() { return 2U; }

} // namespace lg
