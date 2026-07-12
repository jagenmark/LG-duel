#pragma once

#include "net/NetProtocol.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace lg {

inline constexpr float kDefaultSnapshotInterpolationDelaySeconds = 0.024F;

[[nodiscard]] PlayerState interpolatePlayerState(
  const PlayerState& previous,
  const PlayerState& current,
  float alpha
);

class SnapshotInterpolation {
public:
  struct PlayerCollisionSample {
    PlayerState pose = {};
    bool eligible = false;
    std::uint32_t discreteServerTick = 0;
    std::uint32_t mapRevision = 0;
  };

  struct Frame {
    std::uint32_t serverTick = 0;
    std::uint32_t mapRevision = 0;
    std::array<PlayerState, kDuelPlayerCount> players = {};
    std::array<bool, kDuelPlayerCount> collisionEligible = {};
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
  [[nodiscard]] PlayerCollisionSample collisionSample(std::size_t playerIndex) const;

private:
  std::vector<Frame> snapshots_;
  double presentationTick_ = 0.0;
  bool initialized_ = false;
};

} // namespace lg
