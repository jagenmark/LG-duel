#include "replay/ReplayRecorder.hpp"

#include "replay/ReplayCodec.hpp"

#include <limits>

namespace lg::replay {
namespace {

constexpr std::size_t kReplayHeaderReserveBytes = 4096U;
constexpr std::size_t kReplayChunkHeaderBytes = 9U;
constexpr std::size_t kReplayFinalCheckpointReserveBytes = 512U * 1024U;
constexpr std::size_t kReplayStateHashBytes = kReplayChunkHeaderBytes + 12U;
constexpr std::size_t kReplaySlotInputBytes = 168U;

std::size_t inputBytes(const ReplayTickInput& input) {
  std::size_t present = 0U;
  for (const ReplaySlotInput& slot : input.slots) present += slot.present ? 1U : 0U;
  return kReplayChunkHeaderBytes + 6U + (present * kReplaySlotInputBytes);
}

bool canReserve(std::uint64_t used, std::size_t maximum, std::size_t addition) {
  return used <= maximum && addition <= maximum - static_cast<std::size_t>(used) &&
    kReplayFinalCheckpointReserveBytes <= maximum - static_cast<std::size_t>(used) - addition;
}

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
      config.maximumBytes > kMaxReplayBytes || initialCheckpoint.serverTick != metadata.initialServerTick) {
    return fail(error, "replay recorder configuration or initial checkpoint is invalid");
  }
  const std::size_t initialCheckpointBytes = encodedReplayCheckpointBytes(initialCheckpoint);
  if (initialCheckpointBytes == 0U || config.maximumBytes < kReplayHeaderReserveBytes ||
      initialCheckpointBytes > config.maximumBytes - kReplayHeaderReserveBytes ||
      kReplayStateHashBytes > config.maximumBytes - kReplayHeaderReserveBytes - initialCheckpointBytes ||
      kReplayFinalCheckpointReserveBytes > config.maximumBytes - kReplayHeaderReserveBytes -
        initialCheckpointBytes - kReplayStateHashBytes) {
    return fail(error, "replay recorder capacity cannot retain initial and final checkpoints");
  }
  config_ = config;
  demo_ = {};
  demo_.metadata = std::move(metadata);
  demo_.checkpoints.push_back(initialCheckpoint);
  demo_.hashes.push_back({initialCheckpoint.serverTick, canonicalStateHash(initialCheckpoint)});
  estimatedBytes_ = kReplayHeaderReserveBytes + initialCheckpointBytes + kReplayStateHashBytes;
  maximumBytes_ = config.maximumBytes;
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
  if (!canReserve(estimatedBytes_, maximumBytes_, inputBytes(input))) {
    active_ = false;
    return fail(error, "replay recorder capacity is exhausted");
  }
  demo_.ticks.push_back(input);
  estimatedBytes_ += inputBytes(input);
  if (error != nullptr) error->clear();
  return true;
}

bool ReplayRecorder::needsCompletedCheckpoint(std::uint32_t tick) const {
  return active_ && tick > demo_.metadata.initialServerTick &&
    (tick % config_.hashIntervalTicks == 0U || tick % config_.checkpointIntervalTicks == 0U);
}

void ReplayRecorder::recordCompletedTick(const ReplayCheckpoint& checkpoint) {
  if (!active_) return;
  if (checkpoint.serverTick <= demo_.metadata.initialServerTick) {
    active_ = false;
    return;
  }
  const bool writeHash = checkpoint.serverTick % config_.hashIntervalTicks == 0U;
  const bool writeCheckpoint = checkpoint.serverTick % config_.checkpointIntervalTicks == 0U;
  const std::size_t checkpointBytes = writeCheckpoint ? encodedReplayCheckpointBytes(checkpoint) : 0U;
  const std::size_t addition = (writeHash ? kReplayStateHashBytes : 0U) +
    (writeCheckpoint ? kReplayChunkHeaderBytes + checkpointBytes : 0U);
  if ((writeCheckpoint && checkpointBytes == 0U) || !canReserve(estimatedBytes_, maximumBytes_, addition)) {
    active_ = false;
    return;
  }
  const std::uint64_t hash = writeHash ? canonicalStateHash(checkpoint) : 0U;
  if (writeHash) {
    if (demo_.hashes.size() >= kMaxReplayTicks) {
      active_ = false;
      return;
    }
    demo_.hashes.push_back({checkpoint.serverTick, hash});
    estimatedBytes_ += kReplayStateHashBytes;
  }
  if (writeCheckpoint) {
    if (demo_.checkpoints.size() >= kMaxReplayCheckpoints) {
      active_ = false;
      return;
    }
    demo_.checkpoints.push_back(checkpoint);
    estimatedBytes_ += kReplayChunkHeaderBytes + checkpointBytes;
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

std::optional<ReplayDemo> ReplayRecorder::finish(
  const ReplayCheckpoint& finalCheckpoint,
  std::string* error
) {
  if (!active_) {
    fail(error, "replay recorder is not active");
    return std::nullopt;
  }
  const std::uint32_t expectedTick = demo_.ticks.empty()
    ? demo_.metadata.initialServerTick
    : demo_.ticks.back().tick + 1U;
  const bool needsHash = demo_.hashes.empty() || demo_.hashes.back().tick != finalCheckpoint.serverTick;
  const bool needsCheckpoint = demo_.checkpoints.empty() ||
    demo_.checkpoints.back().serverTick != finalCheckpoint.serverTick;
  const std::size_t finalCheckpointBytes = needsCheckpoint
    ? encodedReplayCheckpointBytes(finalCheckpoint)
    : 0U;
  const std::size_t finalAddition = (needsHash ? kReplayStateHashBytes : 0U) +
    (needsCheckpoint ? kReplayChunkHeaderBytes + finalCheckpointBytes : 0U);
  if (finalCheckpoint.serverTick != expectedTick || finalCheckpoint.mapRevision != demo_.metadata.mapRevision ||
      (needsCheckpoint && demo_.checkpoints.size() >= kMaxReplayCheckpoints) ||
      (needsHash && demo_.hashes.size() >= kMaxReplayTicks) ||
      (needsCheckpoint && finalCheckpointBytes == 0U) ||
      estimatedBytes_ > maximumBytes_ ||
      finalAddition > maximumBytes_ - static_cast<std::size_t>(estimatedBytes_)) {
    active_ = false;
    fail(error, "replay final checkpoint is invalid or exceeds capacity");
    return std::nullopt;
  }
  const std::uint64_t hash = canonicalStateHash(finalCheckpoint);
  if (needsHash) {
    demo_.hashes.push_back({finalCheckpoint.serverTick, hash});
    estimatedBytes_ += kReplayStateHashBytes;
  }
  if (needsCheckpoint) {
    demo_.checkpoints.push_back(finalCheckpoint);
    estimatedBytes_ += kReplayChunkHeaderBytes + finalCheckpointBytes;
  }
  active_ = false;
  if (error != nullptr) error->clear();
  return std::move(demo_);
}

} // namespace lg::replay
