#pragma once

#include "net/NetProtocol.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace lg {

inline constexpr float kDefaultSnapshotInterpolationDelaySeconds = 0.024F;

[[nodiscard]] PlayerState interpolatePlayerState(
  const PlayerState& previous,
  const PlayerState& current,
  float alpha
);

class SnapshotInterpolation {
public:
  using Clock = std::chrono::steady_clock;
  struct Diagnostics {
    double bufferLeadTicks = 0.0;
    double desiredBufferLeadTicks = 0.0;
    double timelineErrorTicks = 0.0;
    double presentationTick = 0.0;
    double newestSnapshotTick = 0.0;
    float playbackRate = 0.0F;
    float effectiveDelaySeconds = kDefaultSnapshotInterpolationDelaySeconds;
    std::uint32_t underrunCount = 0;
    std::uint32_t hardCorrectionCount = 0;
    std::size_t bufferedSnapshotCount = 0;
    bool playbackStarted = false;
    bool bufferUnderrun = false;
  };

  struct PlayerCollisionSample {
    PlayerState pose = {};
    bool eligible = false;
    std::uint32_t discreteServerTick = 0;
    std::uint32_t mapRevision = 0;
  };

  struct Frame {
    std::uint32_t serverTick = 0;
    std::uint32_t mapRevision = 0;
    std::array<PlayerState, kDuelPlayerCount> players = {};
    std::array<bool, kDuelPlayerCount> collisionEligible = {};
  };

  void push(const ServerSnapshot& snapshot);
  void pushAt(const ServerSnapshot& snapshot, Clock::time_point acceptedAt);
  void reset();
  void advance(
    float elapsedSeconds,
    float interpolationDelaySeconds = kDefaultSnapshotInterpolationDelaySeconds
  );
  void advanceAdaptive(
    float elapsedSeconds,
    float baseDelaySeconds,
    float observedJitterSeconds,
    float minimumDelaySeconds,
    float maximumDelaySeconds,
    float maximumExtrapolationSeconds
  );
  void advanceAdaptiveTo(
    Clock::time_point now,
    float elapsedSeconds,
    float baseDelaySeconds,
    float observedJitterSeconds,
    float minimumDelaySeconds,
    float maximumDelaySeconds,
    float maximumExtrapolationSeconds
  );
  void advanceTo(
    Clock::time_point now,
    float interpolationDelaySeconds = kDefaultSnapshotInterpolationDelaySeconds
  );

  [[nodiscard]] bool initialized() const;
  [[nodiscard]] std::uint32_t presentationServerTick() const;
  [[nodiscard]] PlayerState player(std::size_t playerIndex) const;
  [[nodiscard]] PlayerState player(std::size_t playerIndex, float alpha) const;
  [[nodiscard]] PlayerCollisionSample collisionSample(std::size_t playerIndex) const;
  [[nodiscard]] Diagnostics diagnostics() const;

private:
  std::vector<Frame> snapshots_;
  double presentationTick_ = 0.0;
  Clock::time_point newestSnapshotAcceptedAt_ = {};
  Clock::time_point lastAdvanceAt_ = {};
  double desiredPresentationTick_ = 0.0;
  float playbackRate_ = 0.0F;
  std::uint32_t underrunCount_ = 0;
  std::uint32_t hardCorrectionCount_ = 0;
  bool initialized_ = false;
  bool playbackStarted_ = false;
  bool bufferUnderrun_ = false;
  bool hardCorrectionActive_ = false;
  bool adaptiveDelayInitialized_ = false;
  float effectiveDelaySeconds_ = kDefaultSnapshotInterpolationDelaySeconds;
};

} // namespace lg
