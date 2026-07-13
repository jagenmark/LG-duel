#pragma once

#include "net/NetProtocol.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace lg {

inline constexpr float kDefaultSnapshotInterpolationDelaySeconds = 0.024F;

struct InterpolationDiagnostics {
  float effectiveDelaySeconds = kDefaultSnapshotInterpolationDelaySeconds;
  float bufferedSeconds = 0.0F;
  std::uint64_t starvationCount = 0;
  bool extrapolating = false;
};

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
  void advanceAdaptive(
    float elapsedSeconds,
    float baseDelaySeconds,
    float observedJitterSeconds,
    float minimumDelaySeconds,
    float maximumDelaySeconds,
    float maximumExtrapolationSeconds
  );

  [[nodiscard]] bool initialized() const;
  [[nodiscard]] std::uint32_t presentationServerTick() const;
  [[nodiscard]] PlayerState player(std::size_t playerIndex) const;
  [[nodiscard]] PlayerState player(std::size_t playerIndex, float alpha) const;
  [[nodiscard]] const InterpolationDiagnostics& diagnostics() const;

private:
  std::vector<Frame> snapshots_;
  double presentationTick_ = 0.0;
  bool initialized_ = false;
  bool adaptiveInitialized_ = false;
  bool starved_ = false;
  InterpolationDiagnostics diagnostics_ = {};
};

} // namespace lg
