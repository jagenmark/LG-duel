#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace lg {

struct GpuTimingAvailability {
  bool available = false;
  std::string backend = "unavailable";
  std::string unavailableReason = "not_initialized";
  std::uint32_t timestampValidBits = 0;
  double timestampPeriodNanoseconds = 0.0;
  std::string instrumentationVersion = "lg_gpu_timestamp_v1";
  std::string sdlBaseRevision = "unknown";
  std::string sdlPatchIdentity = "unknown";
};

struct GpuFrameTimingResult {
  std::uint64_t benchmarkFrameIndex = 0;
  std::optional<double> gpuPrimaryCommandBufferMilliseconds;
  bool outlineApplicable = false;
  std::optional<double> outlineGpuMilliseconds;
  std::uint32_t readbackLatencyFrames = 0;
  std::string unavailableReason;
};

enum class GpuTimingSlotState : std::uint8_t {
  Free,
  Recording,
  Submitted,
  Available,
  Consumed,
};

[[nodiscard]] std::optional<std::uint64_t> gpuTimestampDeltaTicks(
  std::uint64_t start,
  std::uint64_t end,
  std::uint32_t validBits
);

[[nodiscard]] std::optional<double> gpuTimestampMilliseconds(
  std::uint64_t start,
  std::uint64_t end,
  std::uint32_t validBits,
  double periodNanoseconds
);

class GpuTimingRing {
public:
  static constexpr std::size_t kSlotCount = 8;
  static constexpr std::uint32_t kQueriesPerSlot = 4;

  struct Slot {
    GpuTimingSlotState state = GpuTimingSlotState::Free;
    std::uint64_t benchmarkFrameIndex = 0;
    bool outlineApplicable = false;
    std::uint64_t submitPollIndex = 0;
    GpuFrameTimingResult result = {};
  };

  [[nodiscard]] std::optional<std::size_t> begin(
    std::uint64_t benchmarkFrameIndex,
    bool outlineApplicable
  );
  [[nodiscard]] bool markSubmitted(
    std::size_t slotIndex,
    std::uint64_t submitPollIndex
  );
  [[nodiscard]] bool markOutlineApplicable(std::size_t slotIndex);
  [[nodiscard]] bool markAvailable(
    std::size_t slotIndex,
    const std::array<std::uint64_t, kQueriesPerSlot>& timestamps,
    std::uint32_t validBits,
    double periodNanoseconds,
    std::uint64_t currentPollIndex
  );
  [[nodiscard]] bool markUnavailable(
    std::size_t slotIndex,
    std::string reason,
    std::uint64_t currentPollIndex
  );
  [[nodiscard]] std::optional<GpuFrameTimingResult> consume(
    std::size_t slotIndex
  );
  [[nodiscard]] bool recycle(std::size_t slotIndex);
  void reset();

  [[nodiscard]] const Slot& slot(std::size_t slotIndex) const;

private:
  std::array<Slot, kSlotCount> slots_ = {};
  std::size_t nextSlot_ = 0;
};

// This adapter owns all patched SDL timestamp calls and their query pool.
// Renderer keeps it opaque so builds without the patch do not see SDL types.
class GpuTimestampTiming {
public:
  GpuTimestampTiming();
  GpuTimestampTiming(const GpuTimestampTiming&) = delete;
  GpuTimestampTiming& operator=(const GpuTimestampTiming&) = delete;
  ~GpuTimestampTiming();

  void initialize(void* device, std::string backend);
  void shutdown(void* device);

  // Poll never waits. Call it once before recording each rendered frame.
  void poll(void* device);
  [[nodiscard]] bool beginFrame(
    void* commandBuffer,
    std::uint64_t benchmarkFrameIndex,
    bool outlineApplicable
  );
  void publishUnavailableFrame(
    std::uint64_t benchmarkFrameIndex,
    bool outlineApplicable,
    std::string reason
  );
  void beginOutline(void* commandBuffer);
  void endOutline(void* commandBuffer);
  void endFrame(void* commandBuffer);
  void submitted(void* device, void* fence);
  void submissionFailed();

  void drain(void* device);
  [[nodiscard]] bool hasPending() const;
  void resetResults();
  [[nodiscard]] std::span<const GpuFrameTimingResult> takeResults();
  [[nodiscard]] const GpuFrameTimingResult* latestResult() const;
  [[nodiscard]] const GpuTimingAvailability& metadata() const;

private:
  struct Impl;
  Impl* impl_ = nullptr;
};

}  // namespace lg
