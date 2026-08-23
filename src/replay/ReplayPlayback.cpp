#include "replay/ReplayPlayback.hpp"

#include "replay/ReplayCodec.hpp"
#include "server/ServerGame.hpp"
#include "shared/Constants.hpp"

#include <algorithm>

namespace lg::replay {

ReplayPlaybackRunner::ReplayPlaybackRunner(ServerGame& game, const ReplayDemo& demo)
  : game_(game), demo_(demo) {}

bool ReplayPlaybackRunner::initialize(std::string* error) {
  if (demo_.checkpoints.empty() || demo_.ticks.empty()) {
    if (error != nullptr) *error = "replay needs an initial checkpoint and input ticks";
    return false;
  }
  if (!game_.restoreReplayCheckpoint(demo_.checkpoints.front(), demo_.metadata, error)) return false;
  nextInput_ = tickOffsetFor(game_.snapshot().serverTick);
  nextHash_ = 0U;
  nextBoundary_ = 0U;
  while (nextBoundary_ < demo_.authorityBoundaries.size() &&
      demo_.authorityBoundaries[nextBoundary_].tick < game_.snapshot().serverTick) {
    ++nextBoundary_;
  }
  while (nextHash_ < demo_.hashes.size() && demo_.hashes[nextHash_].tick < game_.snapshot().serverTick) {
    ++nextHash_;
  }
  initialized_ = true;
  finished_ = false;
  divergence_ = {};
  return compareHash(error);
}

bool ReplayPlaybackRunner::step(std::string* error) {
  if (!initialized_) return initialize(error);
  if (finished_ || divergence_.diverged) return false;
  const std::uint32_t tick = game_.snapshot().serverTick;
  if (!applyBoundariesForCurrentTick(error)) return false;
  if (nextInput_ >= demo_.ticks.size() || demo_.ticks[nextInput_].tick != tick) {
    finished_ = true;
    if (error != nullptr) *error = "replay has no resolved input for the next server tick";
    return false;
  }
  if (!game_.injectReplayInput(demo_.ticks[nextInput_], error)) return false;
  ++nextInput_;
  game_.tick(kFixedTickSeconds);
  if (!compareHash(error)) return false;
  if (nextInput_ == demo_.ticks.size()) finished_ = true;
  return true;
}

bool ReplayPlaybackRunner::seek(std::uint32_t tick, std::string* error) {
  if (demo_.checkpoints.empty()) {
    if (error != nullptr) *error = "replay has no checkpoints";
    return false;
  }
  const auto checkpoint = std::upper_bound(
    demo_.checkpoints.begin(), demo_.checkpoints.end(), tick,
    [](std::uint32_t requested, const ReplayCheckpoint& candidate) {
      return requested < candidate.serverTick;
    }
  );
  if (checkpoint == demo_.checkpoints.begin()) {
    if (error != nullptr) *error = "seek precedes the first replay checkpoint";
    return false;
  }
  const ReplayCheckpoint* anchor = &*std::prev(checkpoint);
  const ReplayAuthorityBoundary* authority = nullptr;
  for (const ReplayAuthorityBoundary& boundary : demo_.authorityBoundaries) {
    if (boundary.tick > tick) break;
    authority = &boundary;
    if (boundary.tick >= anchor->serverTick) anchor = &boundary.checkpoint;
  }
  const ReplayMetadata restoreMetadata = authority == nullptr
    ? demo_.metadata
    : metadataForBoundary(*authority);
  if (!game_.restoreReplayCheckpoint(*anchor, restoreMetadata, error)) return false;
  nextInput_ = tickOffsetFor(game_.snapshot().serverTick);
  nextHash_ = 0U;
  nextBoundary_ = 0U;
  while (nextBoundary_ < demo_.authorityBoundaries.size() &&
      demo_.authorityBoundaries[nextBoundary_].tick <= game_.snapshot().serverTick) {
    ++nextBoundary_;
  }
  while (nextHash_ < demo_.hashes.size() && demo_.hashes[nextHash_].tick < game_.snapshot().serverTick) ++nextHash_;
  initialized_ = true;
  finished_ = false;
  divergence_ = {};
  if (!compareHash(error)) return false;
  while (game_.snapshot().serverTick < tick && step(error)) {}
  return !divergence_.diverged && game_.snapshot().serverTick == tick;
}

bool ReplayPlaybackRunner::finished() const { return finished_; }

std::uint32_t ReplayPlaybackRunner::currentTick() const { return game_.snapshot().serverTick; }

const ReplayDivergence& ReplayPlaybackRunner::divergence() const { return divergence_; }

void ReplayPlaybackRunner::stop() {
  game_.endReplayPlayback();
  initialized_ = false;
  finished_ = true;
}

ReplayMetadata ReplayPlaybackRunner::metadataForBoundary(
  const ReplayAuthorityBoundary& boundary
) const {
  ReplayMetadata metadata = demo_.metadata;
  metadata.gameplayConfig = boundary.gameplayConfig;
  metadata.gameplayConfigHash = canonicalGameplayConfigHash(boundary.gameplayConfig);
  metadata.configurationRevision = boundary.configurationRevision;
  metadata.gameMode = boundary.gameMode;
  metadata.matchRules = boundary.matchRules;
  metadata.players = boundary.players;
  return metadata;
}

bool ReplayPlaybackRunner::applyBoundariesForCurrentTick(std::string* error) {
  const std::uint32_t tick = game_.snapshot().serverTick;
  while (nextBoundary_ < demo_.authorityBoundaries.size() &&
      demo_.authorityBoundaries[nextBoundary_].tick < tick) {
    if (error != nullptr) *error = "replay authority boundary is behind playback state";
    return false;
  }
  while (nextBoundary_ < demo_.authorityBoundaries.size() &&
      demo_.authorityBoundaries[nextBoundary_].tick == tick) {
    const ReplayAuthorityBoundary& boundary = demo_.authorityBoundaries[nextBoundary_];
    if (boundary.checkpoint.serverTick != tick ||
        canonicalGameplayConfigHash(boundary.gameplayConfig) !=
          boundary.checkpoint.gameplayConfigHash) {
      if (error != nullptr) *error = "replay authority boundary is inconsistent";
      return false;
    }
    if (!game_.restoreReplayCheckpoint(
          boundary.checkpoint,
          metadataForBoundary(boundary),
          error)) {
      return false;
    }
    ++nextBoundary_;
  }
  return true;
}

bool ReplayPlaybackRunner::compareHash(std::string* error) {
  const std::uint32_t tick = game_.snapshot().serverTick;
  while (nextHash_ < demo_.hashes.size() && demo_.hashes[nextHash_].tick < tick) ++nextHash_;
  if (nextHash_ == demo_.hashes.size() || demo_.hashes[nextHash_].tick != tick) return true;
  const std::uint64_t actual = canonicalStateHash(game_.captureReplayCheckpoint());
  const std::uint64_t expected = demo_.hashes[nextHash_].value;
  ++nextHash_;
  if (actual == expected) return true;
  divergence_ = {true, tick, expected, actual, "authoritative gameplay checkpoint"};
  if (error != nullptr) *error = "replay diverged in authoritative gameplay checkpoint";
  return false;
}

std::size_t ReplayPlaybackRunner::tickOffsetFor(std::uint32_t tick) const {
  const auto it = std::lower_bound(
    demo_.ticks.begin(), demo_.ticks.end(), tick,
    [](const ReplayTickInput& input, std::uint32_t requested) { return input.tick < requested; }
  );
  return static_cast<std::size_t>(it - demo_.ticks.begin());
}

} // namespace lg::replay
