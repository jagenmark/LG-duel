#pragma once

#include "replay/ReplayTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace lg::replay {

struct ReplayRecordingConfig {
  std::uint32_t checkpointIntervalTicks = 250U;
  std::uint32_t hashIntervalTicks = 125U;
  std::size_t maximumBytes = kMaxReplayBytes;
};

struct ReplayRecorderStats {
  std::uint32_t inputTicks = 0;
  std::uint32_t checkpoints = 0;
  std::uint32_t hashes = 0;
  std::uint64_t estimatedBytes = 0;
};

// Counts only checkpoint copies requested from ServerGame::tick. Start, stop,
// restore, and direct inspection captures do not affect this hot-path measure.
struct ReplayCheckpointCaptureStats {
  std::uint64_t resolvedInputCaptures = 0;
  std::uint64_t captures = 0;
  std::uint64_t nanoseconds = 0;
};

// The recorder is deliberately a passive owner of already-resolved data. It
// neither reads transport packets nor asks a bot for any state or decision.
class ReplayRecorder {
public:
  [[nodiscard]] bool begin(
    ReplayMetadata metadata,
    const ReplayCheckpoint& initialCheckpoint,
    ReplayRecordingConfig config = {},
    std::string* error = nullptr
  );
  [[nodiscard]] bool recordResolvedInput(const ReplayTickInput& input, std::string* error = nullptr);
  [[nodiscard]] bool needsCompletedCheckpoint(std::uint32_t tick) const;
  void recordCompletedTick(const ReplayCheckpoint& checkpoint);
  [[nodiscard]] bool active() const;
  [[nodiscard]] ReplayRecorderStats stats() const;
  // Stopping a recording captures the current state once, outside tick. This
  // guarantees a final checkpoint and hash even between normal intervals.
  [[nodiscard]] std::optional<ReplayDemo> finish(
    const ReplayCheckpoint& finalCheckpoint,
    std::string* error = nullptr
  );

private:
  ReplayRecordingConfig config_ = {};
  ReplayDemo demo_ = {};
  bool active_ = false;
  std::uint64_t estimatedBytes_ = 0;
  std::size_t maximumBytes_ = 0;
};

} // namespace lg::replay
