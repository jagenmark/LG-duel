#include "client/Interpolation.hpp"

#include "shared/Constants.hpp"
#include "shared/Math.hpp"

#include <algorithm>
#include <cmath>

namespace lg {
namespace {

constexpr std::size_t kMaxBufferedSnapshots = 64;
constexpr double kMaxPresentationClockDriftTicks = 0.5;

[[nodiscard]] float interpolateAngle(float previous, float current, float alpha) {
  constexpr float kTwoPi = 6.28318530718F;
  // remainder selects the shortest signed arc across the +/-pi wrap boundary.
  const float difference = std::remainder(current - previous, kTwoPi);
  return previous + (difference * alpha);
}

[[nodiscard]] PlayerState samplePlayerBetweenSnapshots(
  const SnapshotInterpolation::Frame& previous,
  const SnapshotInterpolation::Frame& current,
  std::size_t playerIndex,
  double presentationTick
) {
  const double tickDelta =
    static_cast<double>(current.serverTick - previous.serverTick);
  const float alpha = tickDelta > 0.0F
    ? static_cast<float>(
        (presentationTick - static_cast<double>(previous.serverTick)) / tickDelta
      )
    : 1.0F;
  return interpolatePlayerState(
    previous.players[playerIndex],
    current.players[playerIndex],
    alpha
  );
}

[[nodiscard]] double latestPresentationTick(
  const std::vector<SnapshotInterpolation::Frame>& snapshots,
  float interpolationDelaySeconds
) {
  if (snapshots.empty()) {
    return 0.0;
  }

  const double oldestTick = static_cast<double>(snapshots.front().serverTick);
  const double newestTick = static_cast<double>(snapshots.back().serverTick);
  const double delayTicks =
    static_cast<double>(std::max(0.0F, interpolationDelaySeconds)) *
    static_cast<double>(kFixedTickRate);
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
  // Interpolate continuous presentation fields only. Discrete authoritative
  // state stays on an endpoint rather than inventing impossible intermediate values.
  result.position = previous.position + ((current.position - previous.position) * t);
  result.velocity = previous.velocity + ((current.velocity - previous.velocity) * t);
  result.viewYawRadians = interpolateAngle(previous.viewYawRadians, current.viewYawRadians, t);
  result.viewPitchRadians = interpolateAngle(previous.viewPitchRadians, current.viewPitchRadians, t);
  result.bounds.radius =
    previous.bounds.radius + ((current.bounds.radius - previous.bounds.radius) * t);
  result.bounds.halfHeight =
    previous.bounds.halfHeight + ((current.bounds.halfHeight - previous.bounds.halfHeight) * t);
  if (t >= 0.5F) {
    // Switch stance at the midpoint so its discrete flag tracks the interpolated
    // bounds more closely without exposing a fractional gameplay state.
    result.crouched = current.crouched;
    result.sneaking = current.sneaking;
  }
  return result;
}

void SnapshotInterpolation::push(const ServerSnapshot& snapshot) {
  if (snapshots_.capacity() < kMaxBufferedSnapshots) {
    snapshots_.reserve(kMaxBufferedSnapshots);
  }
  Frame frame;
  frame.serverTick = snapshot.serverTick;
  frame.mapRevision = snapshot.mapRevision;
  frame.players = snapshot.players;
  for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
    frame.collisionEligible[index] = isPlayerCollisionEligible(
      snapshot.connectedPlayers[index],
      snapshot.botPlayers[index],
      snapshot.participatingPlayers[index],
      snapshot.players[index]
    );
  }
  if (!snapshots_.empty() && snapshots_.back().mapRevision != frame.mapRevision) {
    // Presentation and collision samples must never interpolate across maps.
    snapshots_.clear();
    initialized_ = false;
  }
  if (!initialized_) {
    snapshots_.push_back(frame);
    presentationTick_ = static_cast<double>(frame.serverTick);
    initialized_ = true;
    return;
  }

  if (!snapshots_.empty() && frame.serverTick <= snapshots_.back().serverTick) {
    // The buffer remains strictly ordered; late and duplicate snapshots are
    // unusable once a newer authoritative presentation frame is present.
    return;
  }

  snapshots_.push_back(frame);
  while (snapshots_.size() > kMaxBufferedSnapshots) {
    snapshots_.erase(snapshots_.begin());
  }

  presentationTick_ = std::min(
    // New arrivals may move the target forward but must never pull the running
    // presentation clock backward and create a visible remote-player rewind.
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

  const double newestPresentationTick =
    latestPresentationTick(snapshots_, interpolationDelaySeconds);
  const double oldestTick = static_cast<double>(snapshots_.front().serverTick);

  presentationTick_ +=
    static_cast<double>(std::max(0.0F, elapsedSeconds)) *
    static_cast<double>(kFixedTickRate);
  if (presentationTick_ < newestPresentationTick - kMaxPresentationClockDriftTicks) {
    // Catch up after stalls or sparse delivery rather than permanently adding
    // extra latency beyond the configured interpolation delay.
    presentationTick_ = newestPresentationTick;
  }
  presentationTick_ = std::clamp(presentationTick_, oldestTick, newestPresentationTick);

  while (
    snapshots_.size() > 2 &&
    static_cast<double>(snapshots_[1].serverTick) < presentationTick_ - 1.0
  ) {
    // Retain the bracketing pair around the presentation clock; older frames can
    // no longer contribute to a sample and only increase search and memory cost.
    snapshots_.erase(snapshots_.begin());
  }
}

bool SnapshotInterpolation::initialized() const {
  return initialized_;
}

std::uint32_t SnapshotInterpolation::presentationServerTick() const {
  if (snapshots_.empty()) {
    return 0;
  }
  return static_cast<std::uint32_t>(std::lround(
    std::clamp(
      presentationTick_,
      static_cast<double>(snapshots_.front().serverTick),
      static_cast<double>(snapshots_.back().serverTick)
    )
  ));
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
    [](const SnapshotInterpolation::Frame& snapshot, double tick) {
      return static_cast<double>(snapshot.serverTick) < tick;
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

SnapshotInterpolation::PlayerCollisionSample
SnapshotInterpolation::collisionSample(std::size_t playerIndex) const {
  if (snapshots_.empty()) {
    return {};
  }
  if (snapshots_.size() == 1) {
    const Frame& frame = snapshots_.front();
    return {
      frame.players[playerIndex], frame.collisionEligible[playerIndex],
      frame.serverTick, frame.mapRevision
    };
  }

  const auto current = std::lower_bound(
    snapshots_.begin(), snapshots_.end(), presentationTick_,
    [](const Frame& frame, double tick) {
      return static_cast<double>(frame.serverTick) < tick;
    }
  );
  if (current == snapshots_.begin()) {
    const Frame& frame = snapshots_.front();
    return {frame.players[playerIndex], frame.collisionEligible[playerIndex],
            frame.serverTick, frame.mapRevision};
  }
  if (current == snapshots_.end()) {
    const Frame& frame = snapshots_.back();
    return {frame.players[playerIndex], frame.collisionEligible[playerIndex],
            frame.serverTick, frame.mapRevision};
  }

  const Frame& previous = *(current - 1);
  const double tickDelta = static_cast<double>(current->serverTick - previous.serverTick);
  const float alpha = tickDelta > 0.0
    ? static_cast<float>((presentationTick_ - previous.serverTick) / tickDelta)
    : 1.0F;
  // Eligibility follows the same endpoint policy as health and the other base
  // discrete PlayerState fields: it changes only when presentation reaches the
  // newer authoritative snapshot.
  const Frame& discrete = alpha < 1.0F ? previous : *current;
  return {
    interpolatePlayerState(previous.players[playerIndex], current->players[playerIndex], alpha),
    discrete.collisionEligible[playerIndex],
    discrete.serverTick,
    discrete.mapRevision,
  };
}

} // namespace lg
