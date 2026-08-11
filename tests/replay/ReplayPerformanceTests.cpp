#include "net/LoopbackTransport.hpp"
#include "replay/ReplayPlayback.hpp"
#include "server/ServerGame.hpp"
#include "shared/Constants.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

constexpr std::uint32_t kMeasuredTicks = 512U;
constexpr std::size_t kTenMinuteTicks = 10U * 60U * lg::replay::kReplayTickRate;

constexpr std::size_t nextPowerOfTwo(std::size_t value) {
  std::size_t result = 1U;
  while (result < value)
    result *= 2U;
  return result;
}

int expect(bool condition, std::string_view message) {
  if (condition)
    return 0;
  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

lg::Arena arena() {
  lg::Arena value;
  value.min = {-12.0F, -12.0F, 0.0F};
  value.max = {12.0F, 12.0F, 6.0F};
  value.spawnPositions[0] = {-4.0F, 0.0F, 0.0F};
  value.spawnPositions[1] = {4.0F, 0.0F, 0.0F};
  value.spawnCount = 2U;
  return value;
}

void discardSnapshots(lg::LoopbackTransport &transport) {
  lg::ServerSnapshot snapshot;
  while (transport.receiveSnapshot(snapshot)) {
  }
}

std::chrono::microseconds tickFor(lg::ServerGame &server,
                                  lg::LoopbackTransport &transport,
                                  std::uint32_t count) {
  const auto start = std::chrono::steady_clock::now();
  for (std::uint32_t tick = 0U; tick < count; ++tick) {
    server.tick(lg::kFixedTickSeconds);
    discardSnapshots(transport);
  }
  return std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - start);
}

} // namespace

int main() {
  int failures = 0;
  lg::LoopbackTransport disabledTransport;
  lg::ServerGame disabled(disabledTransport);
  disabled.setArena(arena());
  discardSnapshots(disabledTransport);
  const std::chrono::microseconds disabledTime =
      tickFor(disabled, disabledTransport, kMeasuredTicks);
  failures += expect(
      disabled.replayCheckpointCaptureStats().captures == 0U,
      "disabled replay paths must not copy a checkpoint from the tick loop");
  failures += expect(
      disabled.replayCheckpointCaptureStats().resolvedInputCaptures == 0U,
      "disabled replay paths must not copy resolved input from the tick loop");

  lg::replay::ReplayRollingBufferConfig rollingConfig;
  rollingConfig.retainedTicks = kMeasuredTicks;
  rollingConfig.checkpointIntervalTicks = 64U;
  rollingConfig.hashIntervalTicks = 32U;
  rollingConfig.maximumBytes = 2U * 1024U * 1024U;
  lg::LoopbackTransport rollingTransport;
  lg::ServerGame rolling(rollingTransport);
  rolling.setArena(arena());
  discardSnapshots(rollingTransport);
  std::string error;
  failures += expect(rolling.beginRollingReplay(rollingConfig, &error),
                     "rolling measure should begin");
  const std::chrono::microseconds rollingTime =
      tickFor(rolling, rollingTransport, kMeasuredTicks);
  const lg::replay::ReplayRollingBufferStats rollingStats =
      rolling.rollingReplayStats();
  const lg::replay::ReplayCheckpointCaptureStats rollingCaptureStats =
      rolling.replayCheckpointCaptureStats();
  failures +=
      expect(rollingStats.estimatedBytes <= rollingConfig.maximumBytes &&
                 rollingStats.residentBytes <= rollingConfig.maximumBytes &&
                 rollingStats.residentBytes >=
                     rollingStats.inputCount * sizeof(lg::replay::ReplayTickInput) &&
                 rollingStats.checkpointCount > 1U,
             "rolling measure should charge and bound native replay storage");
  failures += expect(rollingCaptureStats.captures ==
                         kMeasuredTicks / rollingConfig.hashIntervalTicks,
                     "rolling tick checkpoint copies should follow the due "
                     "interval, not every tick");
  failures += expect(rollingCaptureStats.resolvedInputCaptures == kMeasuredTicks,
                     "active rolling replay should capture one resolved input per tick");

  lg::replay::ReplayRecordingConfig recordingConfig;
  recordingConfig.checkpointIntervalTicks = 64U;
  recordingConfig.hashIntervalTicks = 32U;
  lg::LoopbackTransport sourceTransport;
  lg::ServerGame source(sourceTransport);
  source.setArena(arena());
  discardSnapshots(sourceTransport);
  failures += expect(source.beginReplayRecording(recordingConfig, &error),
                     "full recording measure should begin");
  const std::chrono::microseconds recordingTime =
      tickFor(source, sourceTransport, kMeasuredTicks);
  const lg::replay::ReplayRecorderStats recordingStats =
      source.replayRecorderStats();
  const std::optional<lg::replay::ReplayDemo> demo =
      source.finishReplayRecording();
  failures +=
      expect(recordingStats.inputTicks == kMeasuredTicks &&
                 recordingStats.estimatedBytes > 0U &&
                 recordingStats.residentBytes >=
                     recordingStats.inputTicks * sizeof(lg::replay::ReplayTickInput) &&
                 demo.has_value(),
             "full recording measure should retain every resolved input");
  failures += expect(source.replayCheckpointCaptureStats().resolvedInputCaptures == kMeasuredTicks,
                     "active full recording should capture one resolved input per tick");

  lg::LoopbackTransport encodedCappedTransport;
  lg::ServerGame encodedCapped(encodedCappedTransport);
  encodedCapped.setArena(arena());
  discardSnapshots(encodedCappedTransport);
  lg::replay::ReplayRecordingConfig encodedCappedConfig;
  encodedCappedConfig.checkpointIntervalTicks = 64U;
  encodedCappedConfig.hashIntervalTicks = 32U;
  encodedCappedConfig.maximumBytes = 600U * 1024U;
  failures += expect(encodedCapped.beginReplayRecording(encodedCappedConfig, &error),
                     "bounded full recording should reserve initial and final state");
  (void)tickFor(encodedCapped, encodedCappedTransport, 2048U);
  const lg::replay::ReplayRecorderStats encodedCappedStats = encodedCapped.replayRecorderStats();
  failures += expect(encodedCappedStats.inputTicks > 0U && encodedCappedStats.inputTicks < 2048U &&
                         encodedCappedStats.estimatedBytes <= encodedCappedConfig.maximumBytes &&
                         !encodedCapped.finishReplayRecording().has_value(),
                     "full recording should stop cleanly at its sparse-file byte cap before final encode");

  lg::LoopbackTransport residentCappedTransport;
  lg::ServerGame residentCapped(residentCappedTransport);
  residentCapped.setArena(arena());
  discardSnapshots(residentCappedTransport);
  lg::replay::ReplayRecordingConfig residentCappedConfig;
  residentCappedConfig.checkpointIntervalTicks = 4096U;
  residentCappedConfig.hashIntervalTicks = 4096U;
  residentCappedConfig.maximumResidentBytes = 256U * 1024U;
  failures += expect(residentCapped.beginReplayRecording(residentCappedConfig, &error),
                     "resident-cap recording should retain its initial checkpoint");
  (void)tickFor(residentCapped, residentCappedTransport, 2048U);
  const lg::replay::ReplayRecorderStats residentCappedStats = residentCapped.replayRecorderStats();
  failures += expect(residentCappedStats.inputTicks > 0U &&
                         residentCappedStats.inputTicks < 2048U &&
                         residentCappedStats.residentBytes <=
                             residentCappedConfig.maximumResidentBytes &&
                         residentCappedStats.residentBytes >=
                             residentCappedStats.inputTicks *
                                 sizeof(lg::replay::ReplayTickInput) &&
                         !residentCapped.finishReplayRecording().has_value(),
                     "native fixed-slot input storage should stop cleanly at its resident cap");

  const lg::replay::ReplayRecordingConfig tenMinuteConfig;
  const std::size_t tenMinuteCheckpoints =
      kTenMinuteTicks / tenMinuteConfig.checkpointIntervalTicks + 2U;
  const std::size_t tenMinuteHashes =
      kTenMinuteTicks / tenMinuteConfig.hashIntervalTicks + 2U;
  // Input storage has 16 native slots even in a two-player Duel. Model vector
  // growth to the next power of two and the largest accepted history so the
  // default resident cap has headroom for a ten-minute recording.
  const std::size_t tenMinuteResidentUpper = sizeof(lg::replay::ReplayDemo) +
      nextPowerOfTwo(kTenMinuteTicks) * sizeof(lg::replay::ReplayTickInput) +
      nextPowerOfTwo(tenMinuteCheckpoints) * sizeof(lg::replay::ReplayCheckpoint) +
      tenMinuteCheckpoints * lg::replay::kMaxReplayHistoryFrames *
          sizeof(lg::replay::ReplayHistoryFrame) +
      nextPowerOfTwo(tenMinuteHashes) * sizeof(lg::replay::ReplayStateHash);
  constexpr std::size_t kV2TickChunkBytes = 9U + 6U + 16U * 168U;
  constexpr std::size_t kV2HashChunkBytes = 9U + 12U;
  const std::size_t tenMinuteSixteenPlayerFileUpper = 4096U +
      kTenMinuteTicks * kV2TickChunkBytes + tenMinuteHashes * kV2HashChunkBytes +
      tenMinuteCheckpoints *
          (lg::replay::kReplayRecorderFinalCheckpointReserveBytes + 9U);
  failures += expect(tenMinuteResidentUpper <= lg::replay::kMaxReplayResidentBytes,
                     "512 MiB native recorder cap should cover the conservative ten-minute two-player Duel model");
  failures += expect(tenMinuteSixteenPlayerFileUpper <= lg::replay::kMaxReplayBytes,
                     "512 MiB saved-demo cap should cover the conservative ten-minute 16-player wire model");

  lg::LoopbackTransport playbackTransport;
  lg::ServerGame playback(playbackTransport);
  playback.setArena(arena());
  if (demo.has_value())
    playback.setMatchRules(demo->metadata.matchRules);
  discardSnapshots(playbackTransport);
  std::uint32_t playbackTicks = 0U;
  const auto playbackStart = std::chrono::steady_clock::now();
  if (demo.has_value()) {
    lg::replay::ReplayPlaybackRunner runner(playback, *demo);
    failures +=
        expect(runner.initialize(&error), "playback measure should initialize");
    while (runner.step(&error))
      ++playbackTicks;
    failures += expect(runner.finished() && !runner.divergence().diverged,
                       "playback measure should verify authoritative hashes");
    runner.stop();
  }
  const auto playbackTime =
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - playbackStart);
  failures += expect(playbackTicks == kMeasuredTicks,
                     "playback measure should consume all recorded ticks");

  const double playbackSeconds =
      std::max(0.000001, static_cast<double>(playbackTime.count()) / 1000000.0);
  const double playbackRate =
      static_cast<double>(playbackTicks) / playbackSeconds;
  const double realtimeMultiple =
      playbackRate / static_cast<double>(lg::replay::kReplayTickRate);
  std::cout << std::fixed << std::setprecision(2)
            << "replay-measure disabled-us-per-tick="
            << static_cast<double>(disabledTime.count()) / kMeasuredTicks
            << " rolling-us-per-tick="
            << static_cast<double>(rollingTime.count()) / kMeasuredTicks
            << " full-us-per-tick="
            << static_cast<double>(recordingTime.count()) / kMeasuredTicks
            << " rolling-bytes=" << rollingStats.estimatedBytes
            << " rolling-resident-bytes=" << rollingStats.residentBytes
            << " rolling-checkpoint-captures=" << rollingCaptureStats.captures
            << " rolling-resolved-input-captures=" << rollingCaptureStats.resolvedInputCaptures
            << " rolling-checkpoint-us="
            << static_cast<double>(rollingCaptureStats.nanoseconds) / 1000.0
            << " full-estimated-bytes=" << recordingStats.estimatedBytes
            << " full-resident-bytes=" << recordingStats.residentBytes
            << " full-resolved-input-captures="
            << source.replayCheckpointCaptureStats().resolvedInputCaptures
            << " encoded-capped-input-ticks=" << encodedCappedStats.inputTicks
            << " encoded-capped-bytes=" << encodedCappedStats.estimatedBytes
            << " resident-capped-input-ticks=" << residentCappedStats.inputTicks
            << " resident-capped-bytes=" << residentCappedStats.residentBytes
            << " sizeof-replay-tick=" << sizeof(lg::replay::ReplayTickInput)
            << " sizeof-replay-checkpoint=" << sizeof(lg::replay::ReplayCheckpoint)
            << " ten-minute-resident-upper=" << tenMinuteResidentUpper
            << " ten-minute-16-player-file-upper="
            << tenMinuteSixteenPlayerFileUpper
            << " playback-us=" << playbackTime.count()
            << " playback-ticks-per-second=" << playbackRate
            << " playback-x-realtime=" << realtimeMultiple << '\n';
  return failures == 0 ? 0 : 1;
}
