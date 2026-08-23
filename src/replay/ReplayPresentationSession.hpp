#pragma once

#include "replay/ReplayTypes.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace lg::replay {

enum class ReplayCameraMode : std::uint8_t {
  FirstPerson = 0,
  Chase = 1,
  Free = 2,
};

enum class ReplayPresentationStopReason : std::uint8_t {
  None = 0,
  Complete = 1,
  UserSkipped = 2,
  TransferFailed = 3,
  PlaybackDiverged = 4,
  InvalidDemo = 5,
};

struct ReplayPresentationState {
  bool active = false;
  bool paused = true;
  ReplayCameraMode cameraMode = ReplayCameraMode::FirstPerson;
  ReplayPresentationStopReason stopReason = ReplayPresentationStopReason::None;
  std::uint8_t followSlot = 0;
  std::uint32_t startTick = 0;
  std::uint32_t endTick = 0;
  std::uint32_t currentTick = 0;
  float speed = 1.0F;
  float progress = 0.0F;
};

// This only tracks replay controls and camera choices. The app owns its live
// ClientGame; a replay presenter drives a separate playback runner from this
// session's requested tick and never mutates the live prediction state.
class ReplayPresentationSession {
public:
  [[nodiscard]] bool begin(const ReplayDemo &demo,
                           std::uint8_t initialFollowSlot,
                           std::string *error = nullptr);
  void setPaused(bool paused);
  [[nodiscard]] bool setSpeed(float speed);
  [[nodiscard]] bool setCameraMode(ReplayCameraMode mode);
  [[nodiscard]] bool setFollowSlot(std::uint8_t slot);
  [[nodiscard]] bool seek(std::uint32_t tick);
  [[nodiscard]] bool step(std::int32_t ticks);
  [[nodiscard]] bool advance(double elapsedSeconds);
  void abort(ReplayPresentationStopReason reason);
  [[nodiscard]] const ReplayPresentationState &state() const;
  // Fraction of a fixed tick accumulated by advance(). It is presentation
  // state only; the playback runner still advances on whole ticks.
  [[nodiscard]] double fractionalTick() const { return pendingTicks_; }

private:
  void updateProgress();
  void finish();

  ReplayPresentationState state_ = {};
  std::array<bool, kDuelPlayerCount> followable_ = {};
  double pendingTicks_ = 0.0;
};

} // namespace lg::replay
