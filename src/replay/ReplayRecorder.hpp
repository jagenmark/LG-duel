#pragma once

#include "replay/ReplayTypes.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace lg::replay {

struct ReplayRecordingConfig {
  std::uint32_t checkpointIntervalTicks = 250U;
  std::uint32_t hashIntervalTicks = 125U;
};

struct ReplayRecorderStats {
  std::uint32_t inputTicks = 0;
  std::uint32_t checkpoints = 0;
  std::uint32_t hashes = 0;
  std::uint64_t estimatedBytes = 0;
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
  void recordCompletedTick(const ReplayCheckpoint& checkpoint);
  [[nodiscard]] bool active() const;
  [[nodiscard]] ReplayRecorderStats stats() const;
  [[nodiscard]] std::optional<ReplayDemo> finish();

private:
  ReplayRecordingConfig config_ = {};
  ReplayDemo demo_ = {};
  bool active_ = false;
  std::uint64_t estimatedBytes_ = 0;
};

} // namespace lg::replay
