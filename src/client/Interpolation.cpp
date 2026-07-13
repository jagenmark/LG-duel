#include "client/Interpolation.hpp"

#include "shared/Constants.hpp"
#include "shared/Math.hpp"

#include <algorithm>
#include <cmath>

namespace lg {
namespace {

constexpr std::size_t kMaxBufferedSnapshots = 64;
constexpr double kMinimumPlaybackRate = 0.96;
constexpr double kMaximumPlaybackRate = 1.04;
constexpr double kPlaybackRateCorrectionPerTick = 0.02;
constexpr double kHardCorrectionThresholdTicks = 8.0;
constexpr double kStartupTickEpsilon = 0.0001;

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
  pushAt(snapshot, Clock::now());
}

void SnapshotInterpolation::pushAt(
  const ServerSnapshot& snapshot,
  Clock::time_point acceptedAt
) {
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
    reset();
  }
  if (!initialized_) {
    snapshots_.push_back(frame);
    presentationTick_ = static_cast<double>(frame.serverTick);
    initialized_ = true;
    newestSnapshotAcceptedAt_ = acceptedAt;
    return;
  }

  if (!snapshots_.empty() && frame.serverTick <= snapshots_.back().serverTick) {
    // The buffer remains strictly ordered; late and duplicate snapshots are
    // unusable once a newer authoritative presentation frame is present.
    return;
  }

  snapshots_.push_back(frame);
  // This is an arrival-age clock, so only a genuinely newer accepted snapshot
  // may restart it. Duplicate and reordered packets cannot perturb server time.
  newestSnapshotAcceptedAt_ = acceptedAt;
  while (snapshots_.size() > kMaxBufferedSnapshots) {
    snapshots_.erase(snapshots_.begin());
  }
}

void SnapshotInterpolation::reset() {
  snapshots_.clear();
  presentationTick_ = 0.0;
  newestSnapshotAcceptedAt_ = {};
  lastAdvanceAt_ = {};
  desiredPresentationTick_ = 0.0;
  playbackRate_ = 0.0F;
  underrunCount_ = 0;
  hardCorrectionCount_ = 0;
  initialized_ = false;
  playbackStarted_ = false;
  bufferUnderrun_ = false;
  hardCorrectionActive_ = false;
  adaptiveDelayInitialized_ = false;
  effectiveDelaySeconds_ = kDefaultSnapshotInterpolationDelaySeconds;
}

void SnapshotInterpolation::advance(
  float elapsedSeconds,
  float interpolationDelaySeconds
) {
  // Packet age must start at its actual acceptance time. Charging a snapshot
  // the whole enclosing render frame can consume multiple ticks at low FPS.
  (void)elapsedSeconds;
  advanceTo(Clock::now(), interpolationDelaySeconds);
}

void SnapshotInterpolation::advanceTo(
  Clock::time_point now,
  float interpolationDelaySeconds
) {
  if (!initialized_ || snapshots_.empty()) {
    return;
  }

  const double oldestTick = static_cast<double>(snapshots_.front().serverTick);
  const double newestTick = static_cast<double>(snapshots_.back().serverTick);
  const double elapsed = lastAdvanceAt_ == Clock::time_point{}
    ? 0.0
    : std::max(
        0.0,
        std::chrono::duration<double>(now - lastAdvanceAt_).count()
      );
  lastAdvanceAt_ = now;
  const double arrivalAgeSeconds = std::max(
    0.0,
    std::chrono::duration<double>(now - newestSnapshotAcceptedAt_).count()
  );
  effectiveDelaySeconds_ = std::max(0.0F, interpolationDelaySeconds);
  const double delayTicks =
    static_cast<double>(effectiveDelaySeconds_) *
    static_cast<double>(kFixedTickRate);
  const double estimatedServerTick =
    newestTick + arrivalAgeSeconds * static_cast<double>(kFixedTickRate);
  desiredPresentationTick_ = estimatedServerTick - delayTicks;

  if (!playbackStarted_) {
    // Startup waits for real future state covering the requested delay. This
    // avoids immediately running dry while the first snapshot history arrives.
    if (newestTick - oldestTick + kStartupTickEpsilon < delayTicks) {
      presentationTick_ = oldestTick;
      return;
    }
    presentationTick_ = std::clamp(newestTick - delayTicks, oldestTick, newestTick);
    playbackStarted_ = true;
  }

  const double timelineError = desiredPresentationTick_ - presentationTick_;
  const bool desiredTickBuffered =
    desiredPresentationTick_ <= newestTick + kStartupTickEpsilon;
  if (!desiredTickBuffered) {
    hardCorrectionActive_ = false;
  }
  if (
    timelineError > kHardCorrectionThresholdTicks &&
    desiredTickBuffered &&
    !hardCorrectionActive_
  ) {
    // Large forward discontinuities are exceptional (for example a long local
    // stall). Never latch a correction while the target is beyond buffered
    // state: underrun recovery must correct as soon as fresh history arrives.
    presentationTick_ = std::min(desiredPresentationTick_, newestTick);
    ++hardCorrectionCount_;
    hardCorrectionActive_ = true;
  } else if (timelineError <= kHardCorrectionThresholdTicks) {
    hardCorrectionActive_ = false;
  }
  const double correctedError = desiredPresentationTick_ - presentationTick_;
  playbackRate_ = static_cast<float>(std::clamp(
    1.0 + correctedError * kPlaybackRateCorrectionPerTick,
    kMinimumPlaybackRate,
    kMaximumPlaybackRate
  ));
  const double nextPresentationTick = presentationTick_ +
    elapsed * static_cast<double>(kFixedTickRate) * playbackRate_;
  if (nextPresentationTick >= newestTick && desiredPresentationTick_ > newestTick) {
    presentationTick_ = newestTick;
    if (!bufferUnderrun_) {
      ++underrunCount_;
    }
    bufferUnderrun_ = true;
  } else {
    presentationTick_ = std::min(nextPresentationTick, newestTick);
    bufferUnderrun_ = false;
  }

  while (
    snapshots_.size() > 2 &&
    static_cast<double>(snapshots_[1].serverTick) < presentationTick_ - 1.0
  ) {
    // Retain the bracketing pair around the presentation clock; older frames can
    // no longer contribute to a sample and only increase search and memory cost.
    snapshots_.erase(snapshots_.begin());
  }
}

void SnapshotInterpolation::advanceAdaptive(
  float elapsedSeconds,
  float baseDelaySeconds,
  float observedJitterSeconds,
  float minimumDelaySeconds,
  float maximumDelaySeconds,
  float maximumExtrapolationSeconds
) {
  advanceAdaptiveTo(
    Clock::now(), elapsedSeconds, baseDelaySeconds, observedJitterSeconds,
    minimumDelaySeconds, maximumDelaySeconds, maximumExtrapolationSeconds
  );
}

void SnapshotInterpolation::advanceAdaptiveTo(
  Clock::time_point now,
  float elapsedSeconds,
  float baseDelaySeconds,
  float observedJitterSeconds,
  float minimumDelaySeconds,
  float maximumDelaySeconds,
  float maximumExtrapolationSeconds
) {
  const float elapsed = std::max(0.0F, elapsedSeconds);
  const float minimumDelay = std::max(0.0F, minimumDelaySeconds);
  const float maximumDelay = std::max(minimumDelay, maximumDelaySeconds);
  // Two jitter deviations cover ordinary arrival variation without making the
  // presentation clock chase every individual packet.
  const float desiredDelay = std::clamp(
    std::max(baseDelaySeconds, minimumDelay) +
      (2.0F * std::max(0.0F, observedJitterSeconds)),
    minimumDelay,
    maximumDelay
  );
  if (!adaptiveDelayInitialized_) {
    effectiveDelaySeconds_ = desiredDelay;
    adaptiveDelayInitialized_ = true;
  } else if (desiredDelay > effectiveDelaySeconds_) {
    // Add safety margin promptly when conditions worsen; remove it slowly so
    // short calm periods cannot make the buffer oscillate.
    effectiveDelaySeconds_ = desiredDelay;
  } else {
    effectiveDelaySeconds_ = std::max(
      desiredDelay,
      effectiveDelaySeconds_ - (elapsed * 0.004F)
    );
  }
  // Adaptive mode contributes only the smoothed delay target. The shared
  // controller remains the sole owner of startup, playback and corrections.
  (void)maximumExtrapolationSeconds;
  advanceTo(now, effectiveDelaySeconds_);
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
    // Buffer underrun is owned by the controller, which holds presentation at
    // the newest authoritative pose until a future sample arrives.
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

SnapshotInterpolation::Diagnostics SnapshotInterpolation::diagnostics() const {
  Diagnostics result;
  result.presentationTick = presentationTick_;
  result.playbackRate = playbackRate_;
  result.effectiveDelaySeconds = effectiveDelaySeconds_;
  result.desiredBufferLeadTicks =
    static_cast<double>(effectiveDelaySeconds_) * static_cast<double>(kFixedTickRate);
  result.underrunCount = underrunCount_;
  result.hardCorrectionCount = hardCorrectionCount_;
  result.bufferedSnapshotCount = snapshots_.size();
  result.playbackStarted = playbackStarted_;
  result.bufferUnderrun = bufferUnderrun_;
  if (!snapshots_.empty()) {
    result.newestSnapshotTick = static_cast<double>(snapshots_.back().serverTick);
    result.bufferLeadTicks = result.newestSnapshotTick - presentationTick_;
    result.timelineErrorTicks = desiredPresentationTick_ - presentationTick_;
    result.desiredBufferLeadTicks =
      result.bufferLeadTicks - result.timelineErrorTicks;
  }
  return result;
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
