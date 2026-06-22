#include "client/Interpolation.hpp"

#include "shared/Constants.hpp"
#include "shared/Math.hpp"

#include <algorithm>
#include <cmath>

namespace lg {
namespace {

constexpr std::size_t kMaxBufferedSnapshots = 64;

[[nodiscard]] float interpolateAngle(float previous, float current, float alpha) {
  constexpr float kTwoPi = 6.28318530718F;
  const float difference = std::remainder(current - previous, kTwoPi);
  return previous + (difference * alpha);
}

[[nodiscard]] PlayerState samplePlayerBetweenSnapshots(
  const ServerSnapshot& previous,
  const ServerSnapshot& current,
  std::size_t playerIndex,
  float presentationTick
) {
  const float tickDelta =
    static_cast<float>(current.serverTick - previous.serverTick);
  const float alpha = tickDelta > 0.0F
    ? (presentationTick - static_cast<float>(previous.serverTick)) / tickDelta
    : 1.0F;
  return interpolatePlayerState(
    previous.players[playerIndex],
    current.players[playerIndex],
    alpha
  );
}

[[nodiscard]] float latestPresentationTick(
  const std::deque<ServerSnapshot>& snapshots,
  float interpolationDelaySeconds
) {
  if (snapshots.empty()) {
    return 0.0F;
  }

  const float oldestTick = static_cast<float>(snapshots.front().serverTick);
  const float newestTick = static_cast<float>(snapshots.back().serverTick);
  const float delayTicks =
    std::max(0.0F, interpolationDelaySeconds) * kFixedTickRate;
  if (newestTick <= oldestTick + delayTicks) {
    return oldestTick;
  }

  return newestTick - delayTicks;
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
    snapshots_.push_back(snapshot);
    presentationTick_ = static_cast<float>(snapshot.serverTick);
    initialized_ = true;
    return;
  }

  if (!snapshots_.empty() && snapshot.serverTick <= snapshots_.back().serverTick) {
    return;
  }

  snapshots_.push_back(snapshot);
  while (snapshots_.size() > kMaxBufferedSnapshots) {
    snapshots_.pop_front();
  }

  presentationTick_ = std::min(
    presentationTick_,
    latestPresentationTick(snapshots_, kDefaultSnapshotInterpolationDelaySeconds)
  );
}

void SnapshotInterpolation::advance(
  float elapsedSeconds,
  float interpolationDelaySeconds
) {
  if (!initialized_ || snapshots_.empty()) {
    return;
  }

  const float newestPresentationTick =
    latestPresentationTick(snapshots_, interpolationDelaySeconds);
  const float oldestTick = static_cast<float>(snapshots_.front().serverTick);

  presentationTick_ += std::max(0.0F, elapsedSeconds) * kFixedTickRate;
  presentationTick_ = clamp(presentationTick_, oldestTick, newestPresentationTick);

  while (
    snapshots_.size() > 2 &&
    static_cast<float>(snapshots_[1].serverTick) < presentationTick_ - 1.0F
  ) {
    snapshots_.pop_front();
  }
}

bool SnapshotInterpolation::initialized() const {
  return initialized_;
}

PlayerState SnapshotInterpolation::player(std::size_t playerIndex, float alpha) const {
  if (snapshots_.empty()) {
    return {};
  }
  if (snapshots_.size() == 1) {
    return snapshots_.front().players[playerIndex];
  }

  return interpolatePlayerState(
    snapshots_[snapshots_.size() - 2].players[playerIndex],
    snapshots_.back().players[playerIndex],
    alpha
  );
}

PlayerState SnapshotInterpolation::player(std::size_t playerIndex) const {
  if (snapshots_.empty()) {
    return {};
  }
  if (snapshots_.size() == 1) {
    return snapshots_.front().players[playerIndex];
  }

  const auto current = std::lower_bound(
    snapshots_.begin(),
    snapshots_.end(),
    presentationTick_,
    [](const ServerSnapshot& snapshot, float tick) {
      return static_cast<float>(snapshot.serverTick) < tick;
    }
  );
  if (current == snapshots_.begin()) {
    return snapshots_.front().players[playerIndex];
  }
  if (current == snapshots_.end()) {
    return snapshots_.back().players[playerIndex];
  }

  const auto previous = current - 1;
  return samplePlayerBetweenSnapshots(
    *previous,
    *current,
    playerIndex,
    presentationTick_
  );
}

} // namespace lg
