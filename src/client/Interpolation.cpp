#include "client/Interpolation.hpp"

#include "shared/Math.hpp"

#include <cmath>

namespace lg {
namespace {

[[nodiscard]] float interpolateAngle(float previous, float current, float alpha) {
  constexpr float kTwoPi = 6.28318530718F;
  const float difference = std::remainder(current - previous, kTwoPi);
  return previous + (difference * alpha);
}

} // namespace

PlayerState interpolatePlayerState(
  const PlayerState& previous,
  const PlayerState& current,
  float alpha
) {
  const float t = clamp(alpha, 0.0F, 1.0F);
  PlayerState result = t < 1.0F ? previous : current;
  result.position = previous.position + ((current.position - previous.position) * t);
  result.velocity = previous.velocity + ((current.velocity - previous.velocity) * t);
  result.viewYawRadians = interpolateAngle(previous.viewYawRadians, current.viewYawRadians, t);
  result.viewPitchRadians = interpolateAngle(previous.viewPitchRadians, current.viewPitchRadians, t);
  return result;
}

void SnapshotInterpolation::push(const ServerSnapshot& snapshot) {
  if (!initialized_) {
    previous_ = snapshot;
    current_ = snapshot;
    initialized_ = true;
    return;
  }

  if (snapshot.serverTick < current_.serverTick) {
    return;
  }

  previous_ = current_;
  current_ = snapshot;
}

bool SnapshotInterpolation::initialized() const {
  return initialized_;
}

PlayerState SnapshotInterpolation::player(std::size_t playerIndex, float alpha) const {
  return interpolatePlayerState(
    previous_.players[playerIndex],
    current_.players[playerIndex],
    alpha
  );
}

} // namespace lg
