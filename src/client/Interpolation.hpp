#pragma once

#include "net/NetProtocol.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>

namespace lg {

inline constexpr float kDefaultSnapshotInterpolationDelaySeconds = 0.024F;

[[nodiscard]] PlayerState interpolatePlayerState(
  const PlayerState& previous,
  const PlayerState& current,
  float alpha
);

class SnapshotInterpolation {
public:
  void push(const ServerSnapshot& snapshot);
  void advance(
    float elapsedSeconds,
    float interpolationDelaySeconds = kDefaultSnapshotInterpolationDelaySeconds
  );

  [[nodiscard]] bool initialized() const;
  [[nodiscard]] PlayerState player(std::size_t playerIndex) const;
  [[nodiscard]] PlayerState player(std::size_t playerIndex, float alpha) const;

private:
  std::deque<ServerSnapshot> snapshots_;
  float presentationTick_ = 0.0F;
  bool initialized_ = false;
};

} // namespace lg
