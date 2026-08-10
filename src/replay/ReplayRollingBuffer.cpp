#include "replay/ReplayRollingBuffer.hpp"

#include "replay/ReplayCodec.hpp"

#include <algorithm>
#include <limits>

namespace lg::replay {
namespace {

constexpr std::size_t kInputEstimateBytes = 2048U;
constexpr std::size_t kCheckpointEstimateBytes = 96U * 1024U;
constexpr std::size_t kHashEstimateBytes = 16U;
constexpr std::size_t kLethalEstimateBytes = 24U;

bool fail(std::string* error, const char* message) {
  if (error != nullptr) *error = message;
  return false;
}

} // namespace

bool ReplayRollingBuffer::begin(
  ReplayMetadata metadata,
  ReplayCheckpoint initialCheckpoint,
  std::uint32_t generation,
  ReplayRollingBufferConfig config,
  std::string* error
) {
  if (!config.enabled || config.retainedTicks == 0U || config.checkpointIntervalTicks == 0U ||
      config.hashIntervalTicks == 0U || config.maximumBytes < kCheckpointEstimateBytes + kHashEstimateBytes ||
      generation == 0U || initialCheckpoint.serverTick != metadata.initialServerTick) {
    return fail(error, "rolling replay configuration is invalid");
  }
  config_ = config;
  metadata_ = std::move(metadata);
  generation_ = generation;
  clear();
  checkpoints_.push_back(std::move(initialCheckpoint));
  hashes_.push_back({checkpoints_.front().serverTick, canonicalStateHash(checkpoints_.front())});
  estimatedBytes_ = kCheckpointEstimateBytes + kHashEstimateBytes;
  active_ = true;
  if (error != nullptr) error->clear();
  return true;
}

void ReplayRollingBuffer::reset(
  ReplayMetadata metadata,
  ReplayCheckpoint initialCheckpoint,
  std::uint32_t generation
) {
  metadata.initialServerTick = initialCheckpoint.serverTick;
  std::string ignored;
  (void)begin(std::move(metadata), std::move(initialCheckpoint), generation, config_, &ignored);
}

void ReplayRollingBuffer::recordResolvedInput(const ReplayTickInput& input) {
  if (!active_ || input.tick < metadata_.initialServerTick ||
      (!inputs_.empty() && input.tick <= inputs_.back().tick)) {
    ++droppedRecords_;
    return;
  }
  inputs_.push_back(input);
  estimatedBytes_ += kInputEstimateBytes;
  trim();
}

void ReplayRollingBuffer::recordCompletedTick(const ReplayCheckpoint& checkpoint) {
  if (!active_ || checkpoint.serverTick <= metadata_.initialServerTick ||
      (!checkpoints_.empty() && checkpoint.serverTick <= checkpoints_.back().serverTick)) {
    ++droppedRecords_;
    return;
  }
  if (checkpoint.serverTick % config_.hashIntervalTicks == 0U) {
    hashes_.push_back({checkpoint.serverTick, canonicalStateHash(checkpoint)});
    estimatedBytes_ += kHashEstimateBytes;
  }
  if (checkpoint.serverTick % config_.checkpointIntervalTicks == 0U) {
    checkpoints_.push_back(checkpoint);
    estimatedBytes_ += kCheckpointEstimateBytes;
  }
  trim();
}

void ReplayRollingBuffer::recordLethal(const ReplayLethalEvent& event) {
  if (!active_ || event.replayGeneration != generation_ || event.victim >= kDuelPlayerCount ||
      (!lethals_.empty() && event.tick < lethals_.back().tick)) {
    ++droppedRecords_;
    return;
  }
  lethals_.push_back(event);
  estimatedBytes_ += kLethalEstimateBytes;
  trim();
}

std::optional<ReplayDemo> ReplayRollingBuffer::extractSegment(
  const ReplayLethalEvent& event,
  std::uint32_t beforeTicks,
  std::uint32_t afterTicks,
  std::string* error
) const {
  if (!active_ || event.replayGeneration != generation_) {
    fail(error, "replay generation is unavailable");
    return std::nullopt;
  }
  const std::uint32_t start = event.tick > beforeTicks ? event.tick - beforeTicks : 0U;
  const std::uint32_t end = event.tick > std::numeric_limits<std::uint32_t>::max() - afterTicks
    ? std::numeric_limits<std::uint32_t>::max()
    : event.tick + afterTicks;
  const auto checkpoint = std::upper_bound(
    checkpoints_.begin(), checkpoints_.end(), start,
    [](std::uint32_t tick, const ReplayCheckpoint& candidate) {
      return tick < candidate.serverTick;
    }
  );
  if (checkpoint == checkpoints_.begin()) {
    fail(error, "rolling replay has no checkpoint before the requested segment");
    return std::nullopt;
  }
  const ReplayCheckpoint& anchor = *std::prev(checkpoint);
  if (inputs_.empty() || inputs_.front().tick > anchor.serverTick || inputs_.back().tick < end) {
    fail(error, "rolling replay segment is incomplete");
    return std::nullopt;
  }
  ReplayDemo segment;
  segment.metadata = metadata_;
  segment.metadata.initialServerTick = anchor.serverTick;
  segment.checkpoints.push_back(anchor);
  for (const ReplayTickInput& input : inputs_) {
    if (input.tick >= anchor.serverTick && input.tick <= end) segment.ticks.push_back(input);
  }
  for (const ReplayStateHash& hash : hashes_) {
    if (hash.tick >= anchor.serverTick && hash.tick <= end) segment.hashes.push_back(hash);
  }
  for (const ReplayLethalEvent& lethal : lethals_) {
    if (lethal.tick >= anchor.serverTick && lethal.tick <= end) segment.lethalEvents.push_back(lethal);
  }
  if (segment.ticks.empty() || segment.ticks.front().tick != anchor.serverTick ||
      segment.ticks.back().tick != end) {
    fail(error, "rolling replay has a gap in the requested segment");
    return std::nullopt;
  }
  if (error != nullptr) error->clear();
  return segment;
}

ReplayRollingBufferStats ReplayRollingBuffer::stats() const {
  const std::uint32_t newest = newestTick();
  const std::uint32_t oldest = inputs_.empty()
    ? (checkpoints_.empty() ? 0U : checkpoints_.front().serverTick)
    : inputs_.front().tick;
  return {
    active_, generation_, newest >= oldest ? newest - oldest : 0U,
    inputs_.size(), checkpoints_.size(), lethals_.size(), estimatedBytes_, droppedRecords_,
  };
}

bool ReplayRollingBuffer::active() const { return active_; }

void ReplayRollingBuffer::trim() {
  if (!active_) return;
  const std::uint32_t newest = newestTick();
  const std::uint32_t floor = newest > config_.retainedTicks ? newest - config_.retainedTicks : 0U;
  while (checkpoints_.size() > 1U && checkpoints_[1].serverTick <= floor) {
    checkpoints_.pop_front();
    estimatedBytes_ -= kCheckpointEstimateBytes;
    ++droppedRecords_;
  }
  const std::uint32_t anchorTick = checkpoints_.empty() ? floor : checkpoints_.front().serverTick;
  while (!inputs_.empty() && inputs_.front().tick < anchorTick) {
    inputs_.pop_front();
    estimatedBytes_ -= kInputEstimateBytes;
    ++droppedRecords_;
  }
  while (!hashes_.empty() && hashes_.front().tick < anchorTick) {
    hashes_.pop_front();
    estimatedBytes_ -= kHashEstimateBytes;
    ++droppedRecords_;
  }
  while (!lethals_.empty() && lethals_.front().tick < anchorTick) {
    lethals_.pop_front();
    estimatedBytes_ -= kLethalEstimateBytes;
    ++droppedRecords_;
  }
  while (estimatedBytes_ > config_.maximumBytes && checkpoints_.size() > 1U) {
    const std::uint32_t nextAnchor = checkpoints_[1].serverTick;
    checkpoints_.pop_front();
    estimatedBytes_ -= kCheckpointEstimateBytes;
    while (!inputs_.empty() && inputs_.front().tick < nextAnchor) {
      inputs_.pop_front();
      estimatedBytes_ -= kInputEstimateBytes;
    }
    while (!hashes_.empty() && hashes_.front().tick < nextAnchor) {
      hashes_.pop_front();
      estimatedBytes_ -= kHashEstimateBytes;
    }
    while (!lethals_.empty() && lethals_.front().tick < nextAnchor) {
      lethals_.pop_front();
      estimatedBytes_ -= kLethalEstimateBytes;
    }
    ++droppedRecords_;
  }
  // A single oversized current interval cannot be made segment-safe. Drop its
  // oldest frames while retaining its anchor rather than allow memory growth.
  while (estimatedBytes_ > config_.maximumBytes && inputs_.size() > 1U) {
    inputs_.pop_front();
    estimatedBytes_ -= kInputEstimateBytes;
    ++droppedRecords_;
  }
}

void ReplayRollingBuffer::clear() {
  inputs_.clear();
  checkpoints_.clear();
  hashes_.clear();
  lethals_.clear();
  estimatedBytes_ = 0U;
}

std::uint32_t ReplayRollingBuffer::newestTick() const {
  if (!inputs_.empty()) return inputs_.back().tick;
  if (!checkpoints_.empty()) return checkpoints_.back().serverTick;
  return 0U;
}

} // namespace lg::replay
