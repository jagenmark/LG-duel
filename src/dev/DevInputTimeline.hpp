#pragma once

#include "dev/DevControlProtocol.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace lg::dev {

// Consecutive segments share the ordinary input stream. A neutral release is
// emitted by the caller only after the final segment, never between segments.
class InputTimeline {
public:
  void reset(const ControlRequest& request) {
    segments_ = request.playerInputSequence.empty()
      ? std::vector<PlayerInput>{request.playerInput}
      : request.playerInputSequence;
    index_ = 0;
    remaining_ = segments_.front().ticks;
    total_ = 0;
    for (const auto& segment : segments_) total_ += segment.ticks;
  }

  [[nodiscard]] const PlayerInput& current() const { return segments_[index_]; }
  [[nodiscard]] std::uint32_t remainingTicks() const { return remaining_; }
  [[nodiscard]] std::uint32_t totalTicks() const { return total_; }
  [[nodiscard]] std::size_t segmentCount() const { return segments_.size(); }

  void consumeTick() {
    if (remaining_ == 0) return;
    --remaining_;
    if (remaining_ == 0 && index_ + 1 < segments_.size()) {
      remaining_ = segments_[++index_].ticks;
    }
  }

private:
  std::vector<PlayerInput> segments_;
  std::size_t index_ = 0;
  std::uint32_t remaining_ = 0;
  std::uint32_t total_ = 0;
};

} // namespace lg::dev
