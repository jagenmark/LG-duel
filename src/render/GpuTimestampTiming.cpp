#include "render/GpuTimestampTiming.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#ifndef LG_DUEL_SDL_GPU_TIMESTAMP_EXT
#define LG_DUEL_SDL_GPU_TIMESTAMP_EXT 0
#endif

#ifndef LG_DUEL_HAS_SDL3
#define LG_DUEL_HAS_SDL3 0
#endif

#ifndef LG_DUEL_SDL_BASE_REVISION
#define LG_DUEL_SDL_BASE_REVISION ""
#endif

#ifndef LG_DUEL_SDL_PATCH_IDENTITY
#define LG_DUEL_SDL_PATCH_IDENTITY ""
#endif

#if LG_DUEL_HAS_SDL3
#include <SDL3/SDL.h>
#endif

namespace lg {
namespace {

[[nodiscard]] std::uint32_t clampedLatency(
  std::uint64_t submitPollIndex,
  std::uint64_t currentPollIndex
) {
  const std::uint64_t latency =
    currentPollIndex >= submitPollIndex
      ? currentPollIndex - submitPollIndex
      : 0;
  return static_cast<std::uint32_t>(
    std::min<std::uint64_t>(latency, std::numeric_limits<std::uint32_t>::max())
  );
}

[[nodiscard]] GpuFrameTimingResult baseResult(
  const GpuTimingRing::Slot& slot,
  std::uint64_t currentPollIndex
) {
  GpuFrameTimingResult result;
  result.benchmarkFrameIndex = slot.benchmarkFrameIndex;
  result.outlineApplicable = slot.outlineApplicable;
  result.readbackLatencyFrames =
    clampedLatency(slot.submitPollIndex, currentPollIndex);
  return result;
}

}  // namespace

std::optional<std::uint64_t> gpuTimestampDeltaTicks(
  std::uint64_t start,
  std::uint64_t end,
  std::uint32_t validBits
) {
  if (validBits == 0U || validBits > 64U) {
    return std::nullopt;
  }
  if (validBits == 64U) {
    return end - start;
  }
  const std::uint64_t mask = (std::uint64_t{1} << validBits) - 1U;
  return (end - start) & mask;
}

std::optional<double> gpuTimestampMilliseconds(
  std::uint64_t start,
  std::uint64_t end,
  std::uint32_t validBits,
  double periodNanoseconds
) {
  if (!std::isfinite(periodNanoseconds) || periodNanoseconds <= 0.0) {
    return std::nullopt;
  }
  const std::optional<std::uint64_t> ticks =
    gpuTimestampDeltaTicks(start, end, validBits);
  if (!ticks.has_value()) {
    return std::nullopt;
  }
  return static_cast<double>(*ticks) * periodNanoseconds / 1'000'000.0;
}

std::optional<std::size_t> GpuTimingRing::begin(
  std::uint64_t benchmarkFrameIndex,
  bool outlineApplicable
) {
  for (std::size_t offset = 0; offset < slots_.size(); ++offset) {
    const std::size_t index = (nextSlot_ + offset) % slots_.size();
    Slot& candidate = slots_[index];
    if (candidate.state != GpuTimingSlotState::Free) {
      continue;
    }
    candidate = {};
    candidate.state = GpuTimingSlotState::Recording;
    candidate.benchmarkFrameIndex = benchmarkFrameIndex;
    candidate.outlineApplicable = outlineApplicable;
    nextSlot_ = (index + 1U) % slots_.size();
    return index;
  }
  return std::nullopt;
}

bool GpuTimingRing::markSubmitted(
  std::size_t slotIndex,
  std::uint64_t submitPollIndex
) {
  if (
    slotIndex >= slots_.size() ||
    slots_[slotIndex].state != GpuTimingSlotState::Recording
  ) {
    return false;
  }
  slots_[slotIndex].state = GpuTimingSlotState::Submitted;
  slots_[slotIndex].submitPollIndex = submitPollIndex;
  return true;
}

bool GpuTimingRing::markOutlineApplicable(std::size_t slotIndex) {
  if (
    slotIndex >= slots_.size() ||
    slots_[slotIndex].state != GpuTimingSlotState::Recording
  ) {
    return false;
  }
  slots_[slotIndex].outlineApplicable = true;
  return true;
}

bool GpuTimingRing::markAvailable(
  std::size_t slotIndex,
  const std::array<std::uint64_t, kQueriesPerSlot>& timestamps,
  std::uint32_t validBits,
  double periodNanoseconds,
  std::uint64_t currentPollIndex
) {
  if (
    slotIndex >= slots_.size() ||
    slots_[slotIndex].state != GpuTimingSlotState::Submitted
  ) {
    return false;
  }
  Slot& slot = slots_[slotIndex];
  GpuFrameTimingResult result = baseResult(slot, currentPollIndex);
  result.gpuPrimaryCommandBufferMilliseconds = gpuTimestampMilliseconds(
    timestamps[0],
    timestamps[3],
    validBits,
    periodNanoseconds
  );
  if (slot.outlineApplicable) {
    result.outlineGpuMilliseconds = gpuTimestampMilliseconds(
      timestamps[1],
      timestamps[2],
      validBits,
      periodNanoseconds
    );
  }
  if (!result.gpuPrimaryCommandBufferMilliseconds.has_value()) {
    result.unavailableReason = "invalid_timestamp_info";
  } else if (
    slot.outlineApplicable &&
    !result.outlineGpuMilliseconds.has_value()
  ) {
    result.unavailableReason = "invalid_outline_timestamp";
  }
  slot.result = std::move(result);
  slot.state = GpuTimingSlotState::Available;
  return true;
}

bool GpuTimingRing::markUnavailable(
  std::size_t slotIndex,
  std::string reason,
  std::uint64_t currentPollIndex
) {
  if (
    slotIndex >= slots_.size() ||
    slots_[slotIndex].state != GpuTimingSlotState::Submitted
  ) {
    return false;
  }
  Slot& slot = slots_[slotIndex];
  slot.result = baseResult(slot, currentPollIndex);
  slot.result.unavailableReason = std::move(reason);
  slot.state = GpuTimingSlotState::Available;
  return true;
}

std::optional<GpuFrameTimingResult> GpuTimingRing::consume(
  std::size_t slotIndex
) {
  if (
    slotIndex >= slots_.size() ||
    slots_[slotIndex].state != GpuTimingSlotState::Available
  ) {
    return std::nullopt;
  }
  slots_[slotIndex].state = GpuTimingSlotState::Consumed;
  return slots_[slotIndex].result;
}

bool GpuTimingRing::recycle(std::size_t slotIndex) {
  if (
    slotIndex >= slots_.size() ||
    slots_[slotIndex].state != GpuTimingSlotState::Consumed
  ) {
    return false;
  }
  slots_[slotIndex] = {};
  return true;
}

void GpuTimingRing::reset() {
  slots_ = {};
  nextSlot_ = 0;
}

const GpuTimingRing::Slot& GpuTimingRing::slot(std::size_t slotIndex) const {
  return slots_.at(slotIndex);
}

struct GpuTimestampTiming::Impl {
  GpuTimingAvailability metadata = {};
  GpuTimingRing ring;
  std::vector<GpuFrameTimingResult> results;
  std::vector<GpuFrameTimingResult> takenResults;
  std::optional<std::size_t> recordingSlot;
  std::uint64_t pollIndex = 0;
  bool outlineStarted = false;
  bool outlineEnded = false;
  bool frameEnded = false;
  bool fatalError = false;
#if LG_DUEL_HAS_SDL3 && LG_DUEL_SDL_GPU_TIMESTAMP_EXT
  SDL_LGGPUTimestampQueryPool* pool = nullptr;
  std::array<SDL_GPUFence*, GpuTimingRing::kSlotCount> fences = {};
  std::array<std::string, GpuTimingRing::kSlotCount> slotErrors = {};
#endif
};

GpuTimestampTiming::GpuTimestampTiming()
  : impl_(new Impl) {
  impl_->results.reserve(GpuTimingRing::kSlotCount * 2U);
  impl_->takenResults.reserve(GpuTimingRing::kSlotCount * 2U);
}

GpuTimestampTiming::~GpuTimestampTiming() {
  delete impl_;
}

void GpuTimestampTiming::initialize(void* devicePointer, std::string backend) {
  impl_->metadata = {};
  impl_->metadata.backend = std::move(backend);
  impl_->metadata.sdlBaseRevision = LG_DUEL_SDL_BASE_REVISION;
  impl_->metadata.sdlPatchIdentity = LG_DUEL_SDL_PATCH_IDENTITY;
#if LG_DUEL_HAS_SDL3 && LG_DUEL_SDL_GPU_TIMESTAMP_EXT
  auto* device = static_cast<SDL_GPUDevice*>(devicePointer);
  if (device == nullptr) {
    impl_->metadata.unavailableReason = "backend_not_sdl_gpu";
    return;
  }
  SDL_LGGPUTimestampQueryInfo info = {};
  const SDL_LGGPUTimestampResult infoResult =
    SDL_LG_GetGPUTimestampQueryInfo(device, &info);
  if (infoResult == SDL_LG_GPU_TIMESTAMP_UNSUPPORTED) {
    impl_->metadata.unavailableReason = "driver_unsupported";
    return;
  }
  if (infoResult != SDL_LG_GPU_TIMESTAMP_AVAILABLE) {
    impl_->metadata.unavailableReason = "query_info_failed";
    return;
  }
  if (
    info.timestamp_valid_bits == 0U ||
    info.timestamp_valid_bits > 64U ||
    !std::isfinite(info.timestamp_period_nanoseconds) ||
    info.timestamp_period_nanoseconds <= 0.0F
  ) {
    impl_->metadata.unavailableReason = "invalid_timestamp_info";
    return;
  }
  impl_->pool = SDL_LG_CreateGPUTimestampQueryPool(
    device,
    static_cast<Uint32>(
      GpuTimingRing::kSlotCount * GpuTimingRing::kQueriesPerSlot
    )
  );
  if (impl_->pool == nullptr) {
    impl_->metadata.unavailableReason = "query_pool_create_failed";
    return;
  }
  impl_->metadata.available = true;
  impl_->metadata.unavailableReason.clear();
  impl_->metadata.timestampValidBits = info.timestamp_valid_bits;
  impl_->metadata.timestampPeriodNanoseconds =
    info.timestamp_period_nanoseconds;
#else
  (void)devicePointer;
  impl_->metadata.unavailableReason = "sdl_gpu_timestamp_extension_not_built";
#endif
}

void GpuTimestampTiming::shutdown(void* devicePointer) {
#if LG_DUEL_HAS_SDL3 && LG_DUEL_SDL_GPU_TIMESTAMP_EXT
  auto* device = static_cast<SDL_GPUDevice*>(devicePointer);
  if (device != nullptr) {
    drain(device);
    if (!hasPending() && impl_->pool != nullptr) {
      SDL_LG_ReleaseGPUTimestampQueryPool(device, impl_->pool);
      impl_->pool = nullptr;
    }
  }
#else
  (void)devicePointer;
#endif
  impl_->recordingSlot.reset();
  impl_->ring.reset();
}

void GpuTimestampTiming::poll(void* devicePointer) {
  ++impl_->pollIndex;
#if LG_DUEL_HAS_SDL3 && LG_DUEL_SDL_GPU_TIMESTAMP_EXT
  auto* device = static_cast<SDL_GPUDevice*>(devicePointer);
  if (device == nullptr || impl_->pool == nullptr) {
    return;
  }
  for (std::size_t index = 0; index < GpuTimingRing::kSlotCount; ++index) {
    if (
      impl_->ring.slot(index).state != GpuTimingSlotState::Submitted ||
      impl_->fences[index] == nullptr ||
      !SDL_QueryGPUFence(device, impl_->fences[index])
    ) {
      continue;
    }

    std::string error = std::move(impl_->slotErrors[index]);
    std::array<std::uint64_t, GpuTimingRing::kQueriesPerSlot> values = {};
    const auto readOne = [&](std::uint32_t queryOffset) {
      return SDL_LG_GetGPUTimestampQueryResults(
        device,
        impl_->pool,
        static_cast<Uint32>(
          index * GpuTimingRing::kQueriesPerSlot + queryOffset
        ),
        1U,
        &values[queryOffset]
      );
    };
    SDL_LGGPUTimestampResult queryResult =
      error.empty() ? readOne(0U) : SDL_LG_GPU_TIMESTAMP_ERROR;
    if (error.empty() && queryResult == SDL_LG_GPU_TIMESTAMP_AVAILABLE) {
      queryResult = readOne(3U);
    }
    if (
      error.empty() &&
      queryResult == SDL_LG_GPU_TIMESTAMP_AVAILABLE &&
      impl_->ring.slot(index).outlineApplicable
    ) {
      queryResult = readOne(1U);
      if (queryResult == SDL_LG_GPU_TIMESTAMP_AVAILABLE) {
        queryResult = readOne(2U);
      }
    }
    if (error.empty() && queryResult == SDL_LG_GPU_TIMESTAMP_NOT_READY) {
      continue;
    }
    if (error.empty() && queryResult != SDL_LG_GPU_TIMESTAMP_AVAILABLE) {
      error = queryResult == SDL_LG_GPU_TIMESTAMP_UNSUPPORTED
        ? "query_read_unsupported"
        : "query_read_failed";
    }

    if (error.empty()) {
      (void)impl_->ring.markAvailable(
        index,
        values,
        impl_->metadata.timestampValidBits,
        impl_->metadata.timestampPeriodNanoseconds,
        impl_->pollIndex
      );
    } else {
      (void)impl_->ring.markUnavailable(index, error, impl_->pollIndex);
      impl_->fatalError = true;
      impl_->metadata.available = false;
      impl_->metadata.unavailableReason = error;
    }
    if (
      std::optional<GpuFrameTimingResult> result = impl_->ring.consume(index)
    ) {
      impl_->results.push_back(std::move(*result));
    }
    (void)impl_->ring.recycle(index);
    SDL_ReleaseGPUFence(device, impl_->fences[index]);
    impl_->fences[index] = nullptr;
  }
#else
  (void)devicePointer;
#endif
}

bool GpuTimestampTiming::beginFrame(
  void* commandBufferPointer,
  std::uint64_t benchmarkFrameIndex,
  bool outlineApplicable
) {
  if (!impl_->metadata.available || impl_->fatalError) {
    (void)benchmarkFrameIndex;
    (void)outlineApplicable;
    return false;
  }
  impl_->recordingSlot =
    impl_->ring.begin(benchmarkFrameIndex, outlineApplicable);
  if (!impl_->recordingSlot.has_value()) {
    GpuFrameTimingResult result;
    result.benchmarkFrameIndex = benchmarkFrameIndex;
    result.outlineApplicable = outlineApplicable;
    result.unavailableReason = "ring_full";
    impl_->results.push_back(std::move(result));
    return false;
  }
  impl_->outlineStarted = false;
  impl_->outlineEnded = false;
  impl_->frameEnded = false;
#if LG_DUEL_HAS_SDL3 && LG_DUEL_SDL_GPU_TIMESTAMP_EXT
  auto* commandBuffer =
    static_cast<SDL_GPUCommandBuffer*>(commandBufferPointer);
  const std::size_t slotIndex = *impl_->recordingSlot;
  const Uint32 firstQuery = static_cast<Uint32>(
    slotIndex * GpuTimingRing::kQueriesPerSlot
  );
  if (
    !SDL_LG_ResetGPUTimestampQueries(
      commandBuffer,
      impl_->pool,
      firstQuery,
      GpuTimingRing::kQueriesPerSlot
    ) ||
    !SDL_LG_WriteGPUTimestamp(
      commandBuffer,
      impl_->pool,
      firstQuery,
      SDL_LG_GPU_TIMESTAMP_TOP_OF_PIPE
    )
  ) {
    impl_->slotErrors[slotIndex] = "timestamp_start_failed";
    impl_->fatalError = true;
  }
#else
  (void)commandBufferPointer;
#endif
  return true;
}

void GpuTimestampTiming::publishUnavailableFrame(
  std::uint64_t benchmarkFrameIndex,
  bool outlineApplicable,
  std::string reason
) {
  GpuFrameTimingResult result;
  result.benchmarkFrameIndex = benchmarkFrameIndex;
  result.outlineApplicable = outlineApplicable;
  result.unavailableReason = std::move(reason);
  impl_->results.push_back(std::move(result));
}

void GpuTimestampTiming::beginOutline(void* commandBufferPointer) {
  if (!impl_->recordingSlot.has_value()) {
    return;
  }
  impl_->outlineStarted = true;
  const std::size_t slotIndex = *impl_->recordingSlot;
  (void)impl_->ring.markOutlineApplicable(slotIndex);
#if LG_DUEL_HAS_SDL3 && LG_DUEL_SDL_GPU_TIMESTAMP_EXT
  if (
    impl_->slotErrors[slotIndex].empty() &&
    !SDL_LG_WriteGPUTimestamp(
      static_cast<SDL_GPUCommandBuffer*>(commandBufferPointer),
      impl_->pool,
      static_cast<Uint32>(
        slotIndex * GpuTimingRing::kQueriesPerSlot + 1U
      ),
      SDL_LG_GPU_TIMESTAMP_TOP_OF_PIPE
    )
  ) {
    impl_->slotErrors[slotIndex] = "outline_timestamp_start_failed";
    impl_->fatalError = true;
  }
#else
  (void)commandBufferPointer;
#endif
}

void GpuTimestampTiming::endOutline(void* commandBufferPointer) {
  if (!impl_->outlineStarted || !impl_->recordingSlot.has_value()) {
    return;
  }
  impl_->outlineEnded = impl_->outlineStarted;
#if LG_DUEL_HAS_SDL3 && LG_DUEL_SDL_GPU_TIMESTAMP_EXT
  const std::size_t slotIndex = *impl_->recordingSlot;
  if (
    impl_->slotErrors[slotIndex].empty() &&
    !SDL_LG_WriteGPUTimestamp(
      static_cast<SDL_GPUCommandBuffer*>(commandBufferPointer),
      impl_->pool,
      static_cast<Uint32>(
        slotIndex * GpuTimingRing::kQueriesPerSlot + 2U
      ),
      SDL_LG_GPU_TIMESTAMP_BOTTOM_OF_PIPE
    )
  ) {
    impl_->slotErrors[slotIndex] = "outline_timestamp_end_failed";
    impl_->fatalError = true;
  }
#else
  (void)commandBufferPointer;
#endif
}

void GpuTimestampTiming::endFrame(void* commandBufferPointer) {
  if (!impl_->recordingSlot.has_value() || impl_->frameEnded) {
    return;
  }
  if (impl_->outlineStarted && !impl_->outlineEnded) {
    endOutline(commandBufferPointer);
  }
  impl_->frameEnded = true;
#if LG_DUEL_HAS_SDL3 && LG_DUEL_SDL_GPU_TIMESTAMP_EXT
  const std::size_t slotIndex = *impl_->recordingSlot;
  if (
    impl_->slotErrors[slotIndex].empty() &&
    !SDL_LG_WriteGPUTimestamp(
      static_cast<SDL_GPUCommandBuffer*>(commandBufferPointer),
      impl_->pool,
      static_cast<Uint32>(
        slotIndex * GpuTimingRing::kQueriesPerSlot + 3U
      ),
      SDL_LG_GPU_TIMESTAMP_BOTTOM_OF_PIPE
    )
  ) {
    impl_->slotErrors[slotIndex] = "timestamp_end_failed";
    impl_->fatalError = true;
  }
#else
  (void)commandBufferPointer;
#endif
}

void GpuTimestampTiming::submitted(void*, void* fencePointer) {
  if (!impl_->recordingSlot.has_value()) {
    return;
  }
  const std::size_t slotIndex = *impl_->recordingSlot;
  (void)impl_->ring.markSubmitted(slotIndex, impl_->pollIndex);
#if LG_DUEL_HAS_SDL3 && LG_DUEL_SDL_GPU_TIMESTAMP_EXT
  impl_->fences[slotIndex] = static_cast<SDL_GPUFence*>(fencePointer);
#else
  (void)fencePointer;
#endif
  impl_->recordingSlot.reset();
}

void GpuTimestampTiming::submissionFailed() {
  if (!impl_->recordingSlot.has_value()) {
    return;
  }
  const std::size_t slotIndex = *impl_->recordingSlot;
  (void)impl_->ring.markSubmitted(slotIndex, impl_->pollIndex);
  (void)impl_->ring.markUnavailable(
    slotIndex,
    "submission_failed",
    impl_->pollIndex
  );
  if (std::optional<GpuFrameTimingResult> result = impl_->ring.consume(slotIndex)) {
    impl_->results.push_back(std::move(*result));
  }
  (void)impl_->ring.recycle(slotIndex);
  impl_->recordingSlot.reset();
  impl_->fatalError = true;
  impl_->metadata.available = false;
  impl_->metadata.unavailableReason = "submission_failed";
}

void GpuTimestampTiming::drain(void* devicePointer) {
#if LG_DUEL_HAS_SDL3 && LG_DUEL_SDL_GPU_TIMESTAMP_EXT
  auto* device = static_cast<SDL_GPUDevice*>(devicePointer);
  if (device == nullptr) {
    return;
  }
  std::array<SDL_GPUFence*, GpuTimingRing::kSlotCount> pending = {};
  Uint32 count = 0;
  for (SDL_GPUFence* fence : impl_->fences) {
    if (fence != nullptr) {
      pending[count++] = fence;
    }
  }
  if (count == 0U) {
    return;
  }
  if (!SDL_WaitForGPUFences(device, true, pending.data(), count)) {
    impl_->fatalError = true;
    impl_->metadata.available = false;
    impl_->metadata.unavailableReason = "fence_wait_failed";
    return;
  }
  poll(device);
#else
  (void)devicePointer;
#endif
}

bool GpuTimestampTiming::hasPending() const {
  if (impl_->recordingSlot.has_value()) {
    return true;
  }
  for (std::size_t index = 0; index < GpuTimingRing::kSlotCount; ++index) {
    const GpuTimingSlotState state = impl_->ring.slot(index).state;
    if (
      state == GpuTimingSlotState::Recording ||
      state == GpuTimingSlotState::Submitted ||
      state == GpuTimingSlotState::Available
    ) {
      return true;
    }
  }
  return false;
}

void GpuTimestampTiming::resetResults() {
  impl_->results.clear();
  impl_->takenResults.clear();
}

std::span<const GpuFrameTimingResult> GpuTimestampTiming::takeResults() {
  impl_->takenResults.clear();
  impl_->takenResults.swap(impl_->results);
  impl_->results.clear();
  return impl_->takenResults;
}

const GpuFrameTimingResult* GpuTimestampTiming::latestResult() const {
  return impl_->results.empty() ? nullptr : &impl_->results.back();
}

const GpuTimingAvailability& GpuTimestampTiming::metadata() const {
  return impl_->metadata;
}

}  // namespace lg
