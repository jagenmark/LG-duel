#pragma once

#include "replay/ReplayTypes.hpp"

#include <cstdint>
#include <deque>
#include <optional>
#include <string>

namespace lg::replay {

struct ReplayRollingBufferConfig {
  bool enabled = true;
  std::uint32_t retainedTicks = 1500U; // 12 seconds at the fixed 125 Hz rate.
  std::uint32_t checkpointIntervalTicks = 250U;
  std::uint32_t hashIntervalTicks = 125U;
  std::size_t maximumBytes = 16U * 1024U * 1024U;
};

struct ReplayRollingBufferStats {
  bool enabled = false;
  std::uint32_t generation = 0;
  std::uint32_t retainedTicks = 0;
  std::size_t inputCount = 0;
  std::size_t checkpointCount = 0;
  std::size_t lethalCount = 0;
  std::size_t estimatedBytes = 0;
  std::uint64_t droppedRecords = 0;
};

// A server-owned bounded archive. Callers provide the same resolved input and
// post-tick checkpoints used by full demos; this class never reads packets or
// actor internals. A segment always starts with a retained checkpoint.
class ReplayRollingBuffer {
public:
  [[nodiscard]] bool begin(
    ReplayMetadata metadata,
    ReplayCheckpoint initialCheckpoint,
    std::uint32_t generation,
    ReplayRollingBufferConfig config = {},
    std::string* error = nullptr
  );
  void reset(ReplayMetadata metadata, ReplayCheckpoint initialCheckpoint, std::uint32_t generation);
  void recordResolvedInput(const ReplayTickInput& input);
  [[nodiscard]] bool needsCompletedCheckpoint(std::uint32_t tick) const;
  void recordCompletedTick(const ReplayCheckpoint& checkpoint);
  void recordLethal(const ReplayLethalEvent& event);
  [[nodiscard]] std::optional<ReplayDemo> extractSegment(
    const ReplayLethalEvent& event,
    std::uint32_t beforeTicks,
    std::uint32_t afterTicks,
    std::string* error = nullptr
  ) const;
  [[nodiscard]] ReplayRollingBufferStats stats() const;
  [[nodiscard]] bool active() const;

private:
  void trim();
  void clear();
  [[nodiscard]] std::uint32_t newestTick() const;

  ReplayRollingBufferConfig config_ = {};
  ReplayMetadata metadata_ = {};
  std::uint32_t generation_ = 0;
  std::deque<ReplayTickInput> inputs_;
  std::deque<ReplayCheckpoint> checkpoints_;
  std::deque<ReplayStateHash> hashes_;
  std::deque<ReplayLethalEvent> lethals_;
  std::size_t estimatedBytes_ = 0;
  std::uint64_t droppedRecords_ = 0;
  bool active_ = false;
};

} // namespace lg::replay
