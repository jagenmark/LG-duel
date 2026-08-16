#include "replay/ReplayRollingBuffer.hpp"

#include "replay/ReplayCodec.hpp"

#include <algorithm>
#include <limits>

namespace lg::replay {
namespace {

std::size_t metadataResidentBytes(const ReplayMetadata& metadata) {
  std::size_t bytes = metadata.mapName.capacity();
  for (const ReplayPlayerMetadata& player : metadata.players) {
    bytes += player.name.capacity();
  }
  return bytes;
}

std::size_t checkpointResidentBytes(const ReplayCheckpoint& checkpoint) {
  return sizeof(ReplayCheckpoint) +
    checkpoint.history.capacity() * sizeof(ReplayHistoryFrame);
}

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
      config.hashIntervalTicks == 0U || config.maximumBytes == 0U ||
      generation == 0U || initialCheckpoint.serverTick != metadata.initialServerTick ||
      metadata.protocolRevision != kReplayProtocolRevision ||
      metadata.buildFingerprint != kReplayBuildFingerprint ||
      metadata.simulationRevision != kReplaySimulationRevision ||
      metadata.configurationRevision == 0U ||
      !validateReplayGameplayConfig(metadata.gameplayConfig) ||
      metadata.gameplayConfigHash != canonicalGameplayConfigHash(metadata.gameplayConfig) ||
      initialCheckpoint.gameplayConfigHash != metadata.gameplayConfigHash) {
    return fail(error, "rolling replay configuration is invalid");
  }
  config_ = config;
  metadata_ = std::move(metadata);
  generation_ = generation;
  clear();
  checkpoints_.push_back(std::move(initialCheckpoint));
  hashes_.push_back({checkpoints_.front().serverTick, canonicalStateHash(checkpoints_.front())});
  estimatedBytes_ = sizeof(ReplayRollingBuffer) + metadataResidentBytes(metadata_) +
    checkpointResidentBytes(checkpoints_.front()) + sizeof(ReplayStateHash);
  if (estimatedBytes_ > config_.maximumBytes) {
    clear();
    return fail(error, "rolling replay resident memory cap cannot retain the initial checkpoint");
  }
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
      (!inputs_.empty() && (inputs_.back().tick == std::numeric_limits<std::uint32_t>::max() ||
        input.tick != inputs_.back().tick + 1U))) {
    ++droppedRecords_;
    return;
  }
  if (estimatedBytes_ > config_.maximumBytes ||
      sizeof(ReplayTickInput) > config_.maximumBytes - estimatedBytes_) {
    ++droppedRecords_;
    return;
  }
  inputs_.push_back(input);
  estimatedBytes_ += sizeof(ReplayTickInput);
  trim();
}

void ReplayRollingBuffer::recordAuthorityBoundary(const ReplayAuthorityBoundary& boundary) {
  if (!active_) {
    ++droppedRecords_;
    return;
  }
  if (boundary.tick < metadata_.initialServerTick ||
      boundary.checkpoint.serverTick != boundary.tick ||
      boundary.configurationRevision == 0U ||
      !validateReplayGameplayConfig(boundary.gameplayConfig) ||
      canonicalGameplayConfigHash(boundary.gameplayConfig) !=
        boundary.checkpoint.gameplayConfigHash ||
      (!authorityBoundaries_.empty() &&
        boundary.tick <= authorityBoundaries_.back().tick)) {
    markAuthorityBoundaryGap(boundary.tick);
    ++droppedRecords_;
    return;
  }
  const std::size_t checkpointBytes = checkpointResidentBytes(boundary.checkpoint);
  constexpr std::size_t kBoundaryConfigReserveBytes = 2048U;
  const std::size_t addition = checkpointBytes + kBoundaryConfigReserveBytes;
  if (estimatedBytes_ > config_.maximumBytes || addition > config_.maximumBytes - estimatedBytes_) {
    markAuthorityBoundaryGap(boundary.tick);
    ++droppedRecords_;
    return;
  }
  authorityBoundaries_.push_back(boundary);
  estimatedBytes_ += addition;
  trim();
}

bool ReplayRollingBuffer::needsCompletedCheckpoint(std::uint32_t tick) const {
  return active_ && tick > metadata_.initialServerTick &&
    (tick % config_.hashIntervalTicks == 0U || tick % config_.checkpointIntervalTicks == 0U);
}

void ReplayRollingBuffer::recordCompletedTick(const ReplayCheckpoint& checkpoint) {
  if (!active_ || checkpoint.serverTick <= metadata_.initialServerTick ||
      (!checkpoints_.empty() && checkpoint.serverTick <= checkpoints_.back().serverTick)) {
    ++droppedRecords_;
    return;
  }
  const bool writeHash = checkpoint.serverTick % config_.hashIntervalTicks == 0U;
  const bool writeCheckpoint = checkpoint.serverTick % config_.checkpointIntervalTicks == 0U;
  const std::size_t addition = (writeHash ? sizeof(ReplayStateHash) : 0U) +
    (writeCheckpoint ? checkpointResidentBytes(checkpoint) : 0U);
  if (writeCheckpoint && sizeof(ReplayRollingBuffer) + metadataResidentBytes(metadata_) +
      checkpointResidentBytes(checkpoint) + (writeHash ? sizeof(ReplayStateHash) : 0U) >
        config_.maximumBytes) {
    ++droppedRecords_;
    return;
  }
  if (estimatedBytes_ > config_.maximumBytes ||
      addition > config_.maximumBytes - estimatedBytes_) {
    ++droppedRecords_;
    return;
  }
  if (writeHash) {
    hashes_.push_back({checkpoint.serverTick, canonicalStateHash(checkpoint)});
    estimatedBytes_ += sizeof(ReplayStateHash);
  }
  if (writeCheckpoint) {
    checkpoints_.push_back(checkpoint);
    estimatedBytes_ += checkpointResidentBytes(checkpoints_.back());
  }
  trim();
}

void ReplayRollingBuffer::recordLethal(const ReplayLethalEvent& event) {
  if (!active_ || event.replayGeneration != generation_ || event.victim >= kDuelPlayerCount ||
      event.replayGeneration == 0U || event.sequence == 0U ||
      (!lethals_.empty() &&
        (event.tick < lethals_.back().tick ||
          (event.tick == lethals_.back().tick &&
            event.sequence <= lethals_.back().sequence)))) {
    ++droppedRecords_;
    return;
  }
  if (estimatedBytes_ > config_.maximumBytes ||
      sizeof(ReplayLethalEvent) > config_.maximumBytes - estimatedBytes_) {
    ++droppedRecords_;
    return;
  }
  lethals_.push_back(event);
  estimatedBytes_ += sizeof(ReplayLethalEvent);
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
  if (authorityBoundaryGapTick_.has_value() && end >= *authorityBoundaryGapTick_) {
    fail(error, "rolling replay authority boundary history is incomplete");
    return std::nullopt;
  }
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
  const ReplayAuthorityBoundary* anchorBoundary = nullptr;
  for (const ReplayAuthorityBoundary& boundary : authorityBoundaries_) {
    if (boundary.tick > anchor.serverTick) break;
    anchorBoundary = &boundary;
  }
  if (anchorBoundary != nullptr) {
    segment.metadata.gameplayConfig = anchorBoundary->gameplayConfig;
    segment.metadata.gameplayConfigHash =
      canonicalGameplayConfigHash(anchorBoundary->gameplayConfig);
    segment.metadata.configurationRevision = anchorBoundary->configurationRevision;
    segment.metadata.gameMode = anchorBoundary->gameMode;
    segment.metadata.matchRules = anchorBoundary->matchRules;
    segment.metadata.players = anchorBoundary->players;
  }
  segment.checkpoints.push_back(anchor);
  for (const ReplayTickInput& input : inputs_) {
    if (input.tick >= anchor.serverTick && input.tick <= end) segment.ticks.push_back(input);
  }
  for (const ReplayStateHash& hash : hashes_) {
    if (hash.tick >= anchor.serverTick && hash.tick <= end) segment.hashes.push_back(hash);
  }
  for (const ReplayAuthorityBoundary& boundary : authorityBoundaries_) {
    if (boundary.tick >= anchor.serverTick && boundary.tick <= end) {
      segment.authorityBoundaries.push_back(boundary);
    }
  }
  for (const ReplayLethalEvent& lethal : lethals_) {
    if (lethal.tick >= anchor.serverTick && lethal.tick <= end) segment.lethalEvents.push_back(lethal);
  }
  if (segment.ticks.empty() || segment.ticks.front().tick != anchor.serverTick ||
      segment.ticks.back().tick != end) {
    fail(error, "rolling replay has a gap in the requested segment");
    return std::nullopt;
  }
  for (std::size_t index = 1U; index < segment.ticks.size(); ++index) {
    if (segment.ticks[index - 1U].tick == std::numeric_limits<std::uint32_t>::max() ||
        segment.ticks[index].tick != segment.ticks[index - 1U].tick + 1U) {
      fail(error, "rolling replay has a gap in the requested segment");
      return std::nullopt;
    }
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
    inputs_.size(), checkpoints_.size(), lethals_.size(), estimatedBytes_, estimatedBytes_, droppedRecords_,
  };
}

bool ReplayRollingBuffer::active() const { return active_; }

void ReplayRollingBuffer::trim() {
  if (!active_) return;
  const std::uint32_t newest = newestTick();
  const std::uint32_t floor = newest > config_.retainedTicks ? newest - config_.retainedTicks : 0U;
  while (checkpoints_.size() > 1U && checkpoints_[1].serverTick <= floor) {
    estimatedBytes_ -= checkpointResidentBytes(checkpoints_.front());
    checkpoints_.pop_front();
    ++droppedRecords_;
  }
  const std::uint32_t anchorTick = checkpoints_.empty() ? floor : checkpoints_.front().serverTick;
  while (!inputs_.empty() && inputs_.front().tick < anchorTick) {
    inputs_.pop_front();
    estimatedBytes_ -= sizeof(ReplayTickInput);
    ++droppedRecords_;
  }
  while (!hashes_.empty() && hashes_.front().tick < anchorTick) {
    hashes_.pop_front();
    estimatedBytes_ -= sizeof(ReplayStateHash);
    ++droppedRecords_;
  }
  while (!lethals_.empty() && lethals_.front().tick < anchorTick) {
    lethals_.pop_front();
    estimatedBytes_ -= sizeof(ReplayLethalEvent);
    ++droppedRecords_;
  }
  while (authorityBoundaries_.size() > 1U &&
      authorityBoundaries_[1].tick < anchorTick) {
    markAuthorityBoundaryGap(authorityBoundaries_.front().tick);
    estimatedBytes_ -= checkpointResidentBytes(authorityBoundaries_.front().checkpoint) + 2048U;
    authorityBoundaries_.pop_front();
    ++droppedRecords_;
  }
  while (estimatedBytes_ > config_.maximumBytes && checkpoints_.size() > 1U) {
    const std::uint32_t nextAnchor = checkpoints_[1].serverTick;
    estimatedBytes_ -= checkpointResidentBytes(checkpoints_.front());
    checkpoints_.pop_front();
    while (!inputs_.empty() && inputs_.front().tick < nextAnchor) {
      inputs_.pop_front();
      estimatedBytes_ -= sizeof(ReplayTickInput);
    }
    while (!hashes_.empty() && hashes_.front().tick < nextAnchor) {
      hashes_.pop_front();
      estimatedBytes_ -= sizeof(ReplayStateHash);
    }
    while (!lethals_.empty() && lethals_.front().tick < nextAnchor) {
      lethals_.pop_front();
      estimatedBytes_ -= sizeof(ReplayLethalEvent);
    }
    while (authorityBoundaries_.size() > 1U &&
        authorityBoundaries_[1].tick < nextAnchor) {
      markAuthorityBoundaryGap(authorityBoundaries_.front().tick);
      estimatedBytes_ -= checkpointResidentBytes(authorityBoundaries_.front().checkpoint) + 2048U;
      authorityBoundaries_.pop_front();
    }
    ++droppedRecords_;
  }
  // A single oversized current interval cannot be made segment-safe. Drop its
  // oldest frames while retaining its anchor rather than allow memory growth.
  while (estimatedBytes_ > config_.maximumBytes && inputs_.size() > 1U) {
    inputs_.pop_front();
    estimatedBytes_ -= sizeof(ReplayTickInput);
    ++droppedRecords_;
  }
  while (estimatedBytes_ > config_.maximumBytes && lethals_.size() > 1U) {
    lethals_.pop_front();
    estimatedBytes_ -= sizeof(ReplayLethalEvent);
    ++droppedRecords_;
  }
}

void ReplayRollingBuffer::clear() {
  std::deque<ReplayTickInput>().swap(inputs_);
  std::deque<ReplayCheckpoint>().swap(checkpoints_);
  std::deque<ReplayAuthorityBoundary>().swap(authorityBoundaries_);
  std::deque<ReplayStateHash>().swap(hashes_);
  std::deque<ReplayLethalEvent>().swap(lethals_);
  authorityBoundaryGapTick_.reset();
  estimatedBytes_ = 0U;
}

void ReplayRollingBuffer::markAuthorityBoundaryGap(std::uint32_t tick) {
  const std::uint32_t safeTick = std::max(tick, metadata_.initialServerTick);
  if (!authorityBoundaryGapTick_.has_value() || safeTick < *authorityBoundaryGapTick_) {
    authorityBoundaryGapTick_ = safeTick;
  }
}

std::uint32_t ReplayRollingBuffer::newestTick() const {
  if (!inputs_.empty()) return inputs_.back().tick;
  if (!checkpoints_.empty()) return checkpoints_.back().serverTick;
  return 0U;
}

} // namespace lg::replay
