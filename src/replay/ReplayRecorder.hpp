#pragma once

#include "replay/ReplayTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace lg::replay {

// A final checkpoint must fit after the last input even when a stop falls
// between normal checkpoint intervals. The recorder reserves this much of its
// encoded-file budget while active.
inline constexpr std::size_t kReplayRecorderFinalCheckpointReserveBytes =
  512U * 1024U;

struct ReplayRecordingConfig {
  std::uint32_t checkpointIntervalTicks = 250U;
  std::uint32_t hashIntervalTicks = 125U;
  std::size_t maximumBytes = kMaxReplayBytes;
  std::size_t maximumResidentBytes = kMaxReplayResidentBytes;
};

struct ReplayRecorderStats {
  std::uint32_t inputTicks = 0;
  std::uint32_t checkpoints = 0;
  std::uint32_t hashes = 0;
  // Exact sparse-file budget estimate, not native recorder memory.
  std::uint64_t estimatedBytes = 0;
  // Native arrays, vector capacity, checkpoint/history allocations, and
  // metadata characters retained by the active recorder.
  std::uint64_t residentBytes = 0;
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
  void recordAuthorityBoundary(const ReplayAuthorityBoundary& boundary);
  void recordLethal(const ReplayLethalEvent& event);
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
  std::size_t maximumResidentBytes_ = 0;

  [[nodiscard]] std::size_t residentBytes() const;
  [[nodiscard]] bool enforceResidentLimit();
  void compactResidentStorage();
};

} // namespace lg::replay
