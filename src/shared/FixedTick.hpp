#pragma once

#include <algorithm>
#include <cmath>

namespace lg {

struct FixedTickFrame {
  int tickCount = 0;
  float droppedSeconds = 0.0F;
};

[[nodiscard]] inline FixedTickFrame planFixedTicks(
  float& accumulatorSeconds,
  float elapsedSeconds,
  float fixedDt,
  int maxTicksPerFrame
) {
  accumulatorSeconds += std::max(0.0F, elapsedSeconds);

  const int availableTicks = static_cast<int>(std::floor(accumulatorSeconds / fixedDt));
  const int tickCount = std::min(availableTicks, maxTicksPerFrame);
  const int droppedTicks = availableTicks - tickCount;

  accumulatorSeconds -= static_cast<float>(availableTicks) * fixedDt;
  return {
    tickCount,
    static_cast<float>(droppedTicks) * fixedDt,
  };
}

} // namespace lg
