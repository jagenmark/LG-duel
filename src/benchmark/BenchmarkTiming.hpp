#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace lg::benchmark {

enum class TimingSubsystem : std::size_t {
  NetworkProcessing,
  Simulation,
  MovementCollision,
  Traces,
  Interpolation,
  Animation,
  Count,
};

struct TimingValues {
  std::array<std::int64_t, static_cast<std::size_t>(TimingSubsystem::Count)>
    nanoseconds = {};

  [[nodiscard]] double milliseconds(TimingSubsystem subsystem) const {
    return static_cast<double>(nanoseconds[static_cast<std::size_t>(subsystem)]) /
      1'000'000.0;
  }
};

// These sinks are installed only while a benchmark frame or fixed tick is being
// measured. Normal play pays a predictable null check and performs no clock read.
inline thread_local TimingValues* frameTimingSink = nullptr;
inline thread_local TimingValues* tickTimingSink = nullptr;

class TimingSinkScope {
public:
  TimingSinkScope(TimingValues* frame, TimingValues* tick)
    : previousFrame_(frameTimingSink), previousTick_(tickTimingSink) {
    frameTimingSink = frame;
    tickTimingSink = tick;
  }

  ~TimingSinkScope() {
    frameTimingSink = previousFrame_;
    tickTimingSink = previousTick_;
  }

  TimingSinkScope(const TimingSinkScope&) = delete;
  TimingSinkScope& operator=(const TimingSinkScope&) = delete;

private:
  TimingValues* previousFrame_ = nullptr;
  TimingValues* previousTick_ = nullptr;
};

class ScopedTiming {
public:
  explicit ScopedTiming(TimingSubsystem subsystem)
    : subsystem_(subsystem), frame_(frameTimingSink), tick_(tickTimingSink) {
    enabled_ = frame_ != nullptr || tick_ != nullptr;
    if (enabled_) {
      start_ = std::chrono::steady_clock::now();
    }
  }

  ~ScopedTiming() {
    if (!enabled_) {
      return;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - start_
    ).count();
    const std::size_t index = static_cast<std::size_t>(subsystem_);
    if (frame_ != nullptr) {
      frame_->nanoseconds[index] += elapsed;
    }
    if (tick_ != nullptr && tick_ != frame_) {
      tick_->nanoseconds[index] += elapsed;
    }
  }

  ScopedTiming(const ScopedTiming&) = delete;
  ScopedTiming& operator=(const ScopedTiming&) = delete;

private:
  using Clock = std::chrono::steady_clock;
  TimingSubsystem subsystem_;
  TimingValues* frame_ = nullptr;
  TimingValues* tick_ = nullptr;
  Clock::time_point start_ = {};
  bool enabled_ = false;
};

} // namespace lg::benchmark
