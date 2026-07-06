#pragma once

#include "shared/Math.hpp"

#include <array>
#include <cstddef>

namespace lg {

inline constexpr std::size_t kMaxIcePools = 16;

struct IcePool {
  bool active = false;
  Vec3 center = {};
  Vec3 normal = {0.0F, 0.0F, 1.0F};
  float radius = 0.0F;
  float lifetimeSeconds = 0.0F;
};

using IcePoolArray = std::array<IcePool, kMaxIcePools>;

struct IcePoolTuning {
  float maxRadius = 2.4F;
  float growthPerSecond = 10.0F;
  float lifetimeSeconds = 3.0F;
  float friction = 1.0F;
  float slopeGravityScale = 1.0F;
  float controlScale = 0.35F;
  float mergeDistance = 1.0F;
};

} // namespace lg
