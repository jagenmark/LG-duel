#include "replay/ReplayRecorder.hpp"

#include "replay/ReplayCodec.hpp"

namespace lg::replay {
namespace {

constexpr std::size_t kReplayHeaderReserveBytes = 4096U;
constexpr std::size_t kReplayChunkHeaderBytes = 9U;
constexpr std::size_t kReplayStateHashBytes = kReplayChunkHeaderBytes + 12U;
constexpr std::size_t kReplaySlotInputBytes = 168U;

std::size_t inputBytes(const ReplayTickInput& input) {
  std::size_t present = 0U;
  for (const ReplaySlotInput& slot : input.slots) present += slot.present ? 1U : 0U;
  return kReplayChunkHeaderBytes + 6U + (present * kReplaySlotInputBytes);
}

bool canReserve(std::uint64_t used, std::size_t maximum, std::size_t addition) {
  return used <= maximum && addition <= maximum - static_cast<std::size_t>(used) &&
    kReplayRecorderFinalCheckpointReserveBytes <= maximum - static_cast<std::size_t>(used) - addition;
}

std::size_t stringResidentBytes(const std::string& value) {
  // String objects themselves live inside their parent struct. Count their
  // capacity as a conservative charge; short-string storage may be counted
  // twice, but the cap must never understate retained memory.
  return value.capacity();
}

std::size_t metadataResidentBytes(const ReplayMetadata& metadata) {
  std::size_t bytes = stringResidentBytes(metadata.mapName);
  for (const ReplayPlayerMetadata& player : metadata.players) {
    bytes += stringResidentBytes(player.name);
  }
  return bytes;
}

std::size_t demoResidentBytes(const ReplayDemo& demo) {
  std::size_t bytes = sizeof(ReplayDemo) + metadataResidentBytes(demo.metadata) +
    demo.ticks.capacity() * sizeof(ReplayTickInput) +
    demo.checkpoints.capacity() * sizeof(ReplayCheckpoint) +
    demo.hashes.capacity() * sizeof(ReplayStateHash) +
    demo.lethalEvents.capacity() * sizeof(ReplayLethalEvent) +
    demo.authorityBoundaries.capacity() * sizeof(ReplayAuthorityBoundary);
  for (const ReplayCheckpoint& checkpoint : demo.checkpoints) {
    bytes += checkpoint.history.capacity() * sizeof(ReplayHistoryFrame);
  }
  for (const ReplayAuthorityBoundary& boundary : demo.authorityBoundaries) {
    bytes += boundary.checkpoint.history.capacity() * sizeof(ReplayHistoryFrame);
  }
  return bytes;
}

template <typename T>
void compactVector(std::vector<T>& values) {
  std::vector<T>(values.begin(), values.end()).swap(values);
}

std::size_t doubledCapacity(std::size_t capacity) {
  return capacity == 0U ? 1U : capacity * 2U;
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
      config.maximumBytes == 0U || config.maximumBytes > kMaxReplayBytes ||
      config.maximumResidentBytes == 0U || config.maximumResidentBytes > kMaxReplayResidentBytes ||
      initialCheckpoint.serverTick != metadata.initialServerTick ||
      metadata.simulationRevision != kReplaySimulationRevision ||
      metadata.configurationRevision == 0U ||
      !validateReplayGameplayConfig(metadata.gameplayConfig) ||
      metadata.gameplayConfigHash != canonicalGameplayConfigHash(metadata.gameplayConfig) ||
      initialCheckpoint.gameplayConfigHash != metadata.gameplayConfigHash) {
    return fail(error, "replay recorder configuration or initial checkpoint is invalid");
  }
  const std::size_t initialCheckpointBytes = encodedReplayCheckpointBytes(initialCheckpoint);
  if (initialCheckpointBytes == 0U || config.maximumBytes < kReplayHeaderReserveBytes ||
      initialCheckpointBytes > config.maximumBytes - kReplayHeaderReserveBytes ||
      kReplayStateHashBytes > config.maximumBytes - kReplayHeaderReserveBytes - initialCheckpointBytes ||
      kReplayRecorderFinalCheckpointReserveBytes > config.maximumBytes - kReplayHeaderReserveBytes -
        initialCheckpointBytes - kReplayStateHashBytes) {
    return fail(error, "replay recorder capacity cannot retain initial and final checkpoints");
  }
  ReplayDemo initial;
  initial.metadata = std::move(metadata);
  initial.checkpoints.push_back(initialCheckpoint);
  initial.hashes.push_back({initialCheckpoint.serverTick, canonicalStateHash(initialCheckpoint)});
  if (demoResidentBytes(initial) > config.maximumResidentBytes) {
    return fail(error, "replay recorder resident memory cap cannot retain the initial checkpoint");
  }
  config_ = config;
  demo_ = std::move(initial);
  estimatedBytes_ = kReplayHeaderReserveBytes + initialCheckpointBytes + kReplayStateHashBytes;
  maximumBytes_ = config.maximumBytes;
  maximumResidentBytes_ = config.maximumResidentBytes;
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
  const std::size_t residentBefore = residentBytes();
  if (residentBefore > maximumResidentBytes_) {
    active_ = false;
    return fail(error, "replay recorder resident memory cap is exhausted");
  }
  if (demo_.ticks.size() == demo_.ticks.capacity()) {
    // std::vector normally grows geometrically. Charge a doubled native frame
    // block before the push so a growth step cannot persist past the cap.
    const std::size_t currentCapacity = demo_.ticks.capacity();
    const std::size_t nextCapacity = doubledCapacity(currentCapacity);
    const std::size_t withoutTickStorage =
      residentBefore - currentCapacity * sizeof(ReplayTickInput);
    if (nextCapacity * sizeof(ReplayTickInput) > maximumResidentBytes_ - withoutTickStorage) {
      active_ = false;
      return fail(error, "replay recorder resident memory cap is exhausted");
    }
  }
  demo_.ticks.push_back(input);
  if (!enforceResidentLimit()) {
    demo_.ticks.pop_back();
    compactResidentStorage();
    active_ = false;
    return fail(error, "replay recorder resident memory cap is exhausted");
  }
  estimatedBytes_ += inputBytes(input);
  if (error != nullptr) error->clear();
  return true;
}

void ReplayRecorder::recordAuthorityBoundary(const ReplayAuthorityBoundary& boundary) {
  if (!active_ || demo_.authorityBoundaries.size() >= kMaxReplayAuthorityBoundaries ||
      boundary.tick < demo_.metadata.initialServerTick ||
      (!demo_.authorityBoundaries.empty() &&
        boundary.tick <= demo_.authorityBoundaries.back().tick) ||
      boundary.checkpoint.serverTick != boundary.tick ||
      !validateReplayGameplayConfig(boundary.gameplayConfig) ||
      canonicalGameplayConfigHash(boundary.gameplayConfig) !=
        boundary.checkpoint.gameplayConfigHash) {
    return;
  }
  const std::size_t checkpointBytes = encodedReplayCheckpointBytes(boundary.checkpoint);
  if (checkpointBytes == 0U) {
    active_ = false;
    return;
  }
  // The config is bounded and small. Reserve a conservative fixed amount for
  // its explicit fields and the boundary chunk header.
  constexpr std::size_t kBoundaryConfigReserveBytes = 2048U;
  const std::size_t addition = kReplayChunkHeaderBytes + checkpointBytes +
    kBoundaryConfigReserveBytes;
  if (!canReserve(estimatedBytes_, maximumBytes_, addition)) {
    active_ = false;
    return;
  }
  const std::size_t before = demo_.authorityBoundaries.size();
  demo_.authorityBoundaries.push_back(boundary);
  estimatedBytes_ += addition;
  if (!enforceResidentLimit()) {
    demo_.authorityBoundaries.resize(before);
    compactResidentStorage();
    estimatedBytes_ -= addition;
    active_ = false;
  }
}

void ReplayRecorder::recordLethal(const ReplayLethalEvent& event) {
  if (!active_ || demo_.lethalEvents.size() >= kMaxReplayLethalEvents ||
      event.tick < demo_.metadata.initialServerTick || event.replayGeneration == 0U ||
      event.sequence == 0U ||
      (!demo_.lethalEvents.empty() &&
        (event.tick < demo_.lethalEvents.back().tick ||
          (event.tick == demo_.lethalEvents.back().tick &&
            event.sequence <= demo_.lethalEvents.back().sequence)))) {
    return;
  }
  constexpr std::size_t kLethalReserveBytes = kReplayChunkHeaderBytes + 32U;
  if (!canReserve(estimatedBytes_, maximumBytes_, kLethalReserveBytes) ||
      residentBytes() + sizeof(ReplayLethalEvent) > maximumResidentBytes_) {
    active_ = false;
    return;
  }
  demo_.lethalEvents.push_back(event);
  estimatedBytes_ += kLethalReserveBytes;
  if (!enforceResidentLimit()) {
    demo_.lethalEvents.pop_back();
    compactResidentStorage();
    estimatedBytes_ -= kLethalReserveBytes;
    active_ = false;
  }
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
  const std::uint64_t estimatedBefore = estimatedBytes_;
  const std::size_t checkpointBytes = writeCheckpoint ? encodedReplayCheckpointBytes(checkpoint) : 0U;
  const std::size_t addition = (writeHash ? kReplayStateHashBytes : 0U) +
    (writeCheckpoint ? kReplayChunkHeaderBytes + checkpointBytes : 0U);
  if ((writeCheckpoint && checkpointBytes == 0U) || !canReserve(estimatedBytes_, maximumBytes_, addition)) {
    active_ = false;
    return;
  }
  const std::uint64_t hash = writeHash ? canonicalStateHash(checkpoint) : 0U;
  const std::size_t hashesBefore = demo_.hashes.size();
  const std::size_t checkpointsBefore = demo_.checkpoints.size();
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
  if (!enforceResidentLimit()) {
    demo_.hashes.resize(hashesBefore);
    demo_.checkpoints.resize(checkpointsBefore);
    compactResidentStorage();
    estimatedBytes_ = estimatedBefore;
    active_ = false;
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
    residentBytes(),
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
  const std::uint64_t estimatedBefore = estimatedBytes_;
  const std::size_t hashesBefore = demo_.hashes.size();
  const std::size_t checkpointsBefore = demo_.checkpoints.size();
  if (needsHash) {
    demo_.hashes.push_back({finalCheckpoint.serverTick, hash});
    estimatedBytes_ += kReplayStateHashBytes;
  }
  if (needsCheckpoint) {
    demo_.checkpoints.push_back(finalCheckpoint);
    estimatedBytes_ += kReplayChunkHeaderBytes + finalCheckpointBytes;
  }
  if (!enforceResidentLimit()) {
    demo_.hashes.resize(hashesBefore);
    demo_.checkpoints.resize(checkpointsBefore);
    compactResidentStorage();
    estimatedBytes_ = estimatedBefore;
    active_ = false;
    fail(error, "replay final checkpoint exceeds resident memory capacity");
    return std::nullopt;
  }
  active_ = false;
  if (error != nullptr) error->clear();
  return std::move(demo_);
}

std::size_t ReplayRecorder::residentBytes() const {
  return demoResidentBytes(demo_);
}

bool ReplayRecorder::enforceResidentLimit() {
  if (residentBytes() <= maximumResidentBytes_) return true;
  compactResidentStorage();
  return residentBytes() <= maximumResidentBytes_;
}

void ReplayRecorder::compactResidentStorage() {
  for (ReplayCheckpoint& checkpoint : demo_.checkpoints) {
    compactVector(checkpoint.history);
  }
  compactVector(demo_.ticks);
  compactVector(demo_.checkpoints);
  compactVector(demo_.hashes);
  compactVector(demo_.lethalEvents);
  compactVector(demo_.authorityBoundaries);
  for (ReplayAuthorityBoundary& boundary : demo_.authorityBoundaries) {
    compactVector(boundary.checkpoint.history);
  }
}

} // namespace lg::replay
