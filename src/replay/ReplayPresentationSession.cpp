#include "replay/ReplayPresentationSession.hpp"

#include "shared/Constants.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace lg::replay {
namespace {

bool fail(std::string *error, const char *message) {
  if (error != nullptr)
    *error = message;
  return false;
}

bool validCameraMode(ReplayCameraMode mode) {
  return mode == ReplayCameraMode::FirstPerson ||
         mode == ReplayCameraMode::Chase || mode == ReplayCameraMode::Free;
}

} // namespace

bool ReplayPresentationSession::begin(const ReplayDemo &demo,
                                      std::uint8_t initialFollowSlot,
                                      std::string *error) {
  if (demo.ticks.empty() ||
      demo.ticks.front().tick != demo.metadata.initialServerTick ||
      demo.ticks.back().tick == std::numeric_limits<std::uint32_t>::max()) {
    state_ = {};
    state_.stopReason = ReplayPresentationStopReason::InvalidDemo;
    return fail(error, "replay presentation needs contiguous input ticks from "
                       "its initial tick");
  }
  for (std::size_t index = 1U; index < demo.ticks.size(); ++index) {
    if (demo.ticks[index - 1U].tick ==
            std::numeric_limits<std::uint32_t>::max() ||
        demo.ticks[index].tick != demo.ticks[index - 1U].tick + 1U) {
      state_ = {};
      state_.stopReason = ReplayPresentationStopReason::InvalidDemo;
      return fail(error, "replay presentation input ticks are not contiguous");
    }
  }
  followable_.fill(false);
  for (std::size_t index = 0U; index < demo.metadata.players.size(); ++index) {
    followable_[index] = demo.metadata.players[index].occupied;
  }
  if (initialFollowSlot >= followable_.size() ||
      !followable_[initialFollowSlot]) {
    state_ = {};
    state_.stopReason = ReplayPresentationStopReason::InvalidDemo;
    return fail(error, "replay presentation follow slot is not recorded");
  }
  state_ = {};
  state_.active = true;
  state_.paused = true;
  state_.followSlot = initialFollowSlot;
  state_.startTick = demo.ticks.front().tick;
  state_.endTick = demo.ticks.back().tick + 1U;
  state_.currentTick = state_.startTick;
  pendingTicks_ = 0.0;
  updateProgress();
  if (error != nullptr)
    error->clear();
  return true;
}

void ReplayPresentationSession::setPaused(bool paused) {
  if (state_.active)
    state_.paused = paused;
}

bool ReplayPresentationSession::setSpeed(float speed) {
  if (!std::isfinite(speed) || speed < 0.25F || speed > 4.0F)
    return false;
  state_.speed = speed;
  return true;
}

bool ReplayPresentationSession::setCameraMode(ReplayCameraMode mode) {
  if (!validCameraMode(mode))
    return false;
  state_.cameraMode = mode;
  return true;
}

bool ReplayPresentationSession::setFollowSlot(std::uint8_t slot) {
  if (!state_.active || slot >= followable_.size() || !followable_[slot])
    return false;
  state_.followSlot = slot;
  return true;
}

bool ReplayPresentationSession::seek(std::uint32_t tick) {
  if (!state_.active || tick < state_.startTick || tick > state_.endTick)
    return false;
  state_.currentTick = tick;
  pendingTicks_ = 0.0;
  updateProgress();
  if (state_.currentTick == state_.endTick)
    finish();
  return true;
}

bool ReplayPresentationSession::step(std::int32_t ticks) {
  if (!state_.active || ticks == 0)
    return false;
  const std::int64_t requested =
      static_cast<std::int64_t>(state_.currentTick) + ticks;
  const std::int64_t bounded =
      std::clamp(requested, static_cast<std::int64_t>(state_.startTick),
                 static_cast<std::int64_t>(state_.endTick));
  return seek(static_cast<std::uint32_t>(bounded));
}

bool ReplayPresentationSession::advance(double elapsedSeconds) {
  if (!state_.active || state_.paused || !std::isfinite(elapsedSeconds) ||
      elapsedSeconds < 0.0)
    return false;
  const double maximumPendingTicks =
      static_cast<double>(std::numeric_limits<std::int32_t>::max());
  pendingTicks_ = std::min(
      maximumPendingTicks,
      pendingTicks_ +
          elapsedSeconds * static_cast<double>(kReplayTickRate) * state_.speed);
  if (pendingTicks_ < 1.0)
    return false;
  const double boundedTicks =
      std::min(pendingTicks_,
               static_cast<double>(std::numeric_limits<std::int32_t>::max()));
  pendingTicks_ -= std::floor(boundedTicks);
  return step(static_cast<std::int32_t>(std::floor(boundedTicks)));
}

void ReplayPresentationSession::abort(ReplayPresentationStopReason reason) {
  if (!state_.active)
    return;
  if (reason == ReplayPresentationStopReason::None ||
      reason == ReplayPresentationStopReason::Complete) {
    reason = ReplayPresentationStopReason::InvalidDemo;
  }
  state_.active = false;
  state_.paused = true;
  state_.stopReason = reason;
  pendingTicks_ = 0.0;
}

const ReplayPresentationState &ReplayPresentationSession::state() const {
  return state_;
}

void ReplayPresentationSession::updateProgress() {
  const std::uint32_t duration = state_.endTick - state_.startTick;
  state_.progress =
      duration == 0U
          ? 1.0F
          : static_cast<float>(state_.currentTick - state_.startTick) /
                static_cast<float>(duration);
}

void ReplayPresentationSession::finish() {
  state_.active = false;
  state_.paused = true;
  state_.stopReason = ReplayPresentationStopReason::Complete;
  pendingTicks_ = 0.0;
}

} // namespace lg::replay
