#include "replay/ReplayRecorder.hpp"

#include "replay/ReplayCodec.hpp"

#include <limits>

namespace lg::replay {
namespace {

bool fail(std::string* error, const char* message) {
  if (error != nullptr) *error = message;
  return false;
}

} // namespace

bool ReplayRecorder::begin(
  ReplayMetadata metadata,
  const ReplayCheckpoint& initialCheckpoint,
  ReplayRecordingConfig config,
  std::string* error
) {
  if (active_) return fail(error, "replay recorder is already active");
  if (config.checkpointIntervalTicks == 0U || config.hashIntervalTicks == 0U ||
      initialCheckpoint.serverTick != metadata.initialServerTick) {
    return fail(error, "replay recorder configuration or initial checkpoint is invalid");
  }
  config_ = config;
  demo_ = {};
  demo_.metadata = std::move(metadata);
  demo_.checkpoints.push_back(initialCheckpoint);
  demo_.hashes.push_back({initialCheckpoint.serverTick, canonicalStateHash(initialCheckpoint)});
  estimatedBytes_ = 0U;
  active_ = true;
  if (error != nullptr) error->clear();
  return true;
}

bool ReplayRecorder::recordResolvedInput(const ReplayTickInput& input, std::string* error) {
  if (!active_) return fail(error, "replay recorder is not active");
  if (demo_.ticks.size() >= kMaxReplayTicks || input.tick < demo_.metadata.initialServerTick ||
      (!demo_.ticks.empty() && input.tick <= demo_.ticks.back().tick)) {
    active_ = false;
    return fail(error, "replay input tick is invalid or capacity is exhausted");
  }
  demo_.ticks.push_back(input);
  // A small fixed estimate helps live telemetry without serializing on the
  // server's simulation thread. The final file size comes from encodeDemo.
  estimatedBytes_ += 32U + (kDuelPlayerCount * 128U);
  if (error != nullptr) error->clear();
  return true;
}

void ReplayRecorder::recordCompletedTick(const ReplayCheckpoint& checkpoint) {
  if (!active_) return;
  const std::uint64_t hash = canonicalStateHash(checkpoint);
  if (checkpoint.serverTick <= demo_.metadata.initialServerTick) {
    active_ = false;
    return;
  }
  if (checkpoint.serverTick % config_.hashIntervalTicks == 0U) {
    if (demo_.hashes.size() >= kMaxReplayTicks) {
      active_ = false;
      return;
    }
    demo_.hashes.push_back({checkpoint.serverTick, hash});
  }
  if (checkpoint.serverTick % config_.checkpointIntervalTicks == 0U) {
    if (demo_.checkpoints.size() >= kMaxReplayCheckpoints) {
      active_ = false;
      return;
    }
    demo_.checkpoints.push_back(checkpoint);
  }
}

bool ReplayRecorder::active() const {
  return active_;
}

ReplayRecorderStats ReplayRecorder::stats() const {
  return {
    static_cast<std::uint32_t>(demo_.ticks.size()),
    static_cast<std::uint32_t>(demo_.checkpoints.size()),
    static_cast<std::uint32_t>(demo_.hashes.size()),
    estimatedBytes_,
  };
}

std::optional<ReplayDemo> ReplayRecorder::finish() {
  if (!active_) return std::nullopt;
  active_ = false;
  return std::move(demo_);
}

} // namespace lg::replay
