#pragma once

#include "net/NetProtocol.hpp"

#include <array>
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
  struct Frame {
    std::uint32_t serverTick = 0;
    std::array<PlayerState, kDuelPlayerCount> players = {};
  };

  void push(const ServerSnapshot& snapshot);
  void advance(
    float elapsedSeconds,
    float interpolationDelaySeconds = kDefaultSnapshotInterpolationDelaySeconds
  );

  [[nodiscard]] bool initialized() const;
  [[nodiscard]] std::uint32_t presentationServerTick() const;
  [[nodiscard]] PlayerState player(std::size_t playerIndex) const;
  [[nodiscard]] PlayerState player(std::size_t playerIndex, float alpha) const;

private:
  std::deque<Frame> snapshots_;
  double presentationTick_ = 0.0;
  bool initialized_ = false;
};

} // namespace lg
