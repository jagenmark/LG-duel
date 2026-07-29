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
  result.passApplicable = slot.passApplicable;
  result.outlineApplicable =
    slot.passApplicable[static_cast<std::size_t>(GpuTimedPass::OutlineTotal)];
  result.readbackLatencyFrames =
    clampedLatency(slot.submitPollIndex, currentPollIndex);
  return result;
}

[[nodiscard]] constexpr std::uint32_t passStartQuery(GpuTimedPass pass) {
  return 1U + static_cast<std::uint32_t>(pass) * 2U;
}

[[nodiscard]] constexpr std::uint32_t passEndQuery(GpuTimedPass pass) {
  return passStartQuery(pass) + 1U;
}

[[nodiscard]] constexpr std::uint32_t frameEndQuery() {
  return GpuTimingRing::kQueriesPerSlot - 1U;
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
    candidate.passApplicable[
      static_cast<std::size_t>(GpuTimedPass::OutlineTotal)
    ] = outlineApplicable;
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
  return markPassApplicable(slotIndex, GpuTimedPass::OutlineTotal);
}

bool GpuTimingRing::markPassApplicable(
  std::size_t slotIndex,
  GpuTimedPass pass
) {
  if (
    slotIndex >= slots_.size() ||
    slots_[slotIndex].state != GpuTimingSlotState::Recording ||
    pass == GpuTimedPass::Count
  ) {
    return false;
  }
  slots_[slotIndex].passApplicable[static_cast<std::size_t>(pass)] = true;
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
    timestamps[frameEndQuery()],
    validBits,
    periodNanoseconds
  );
  for (std::size_t passIndex = 0;
       passIndex < kGpuTimedPassCount;
       ++passIndex) {
    if (!result.passApplicable[passIndex]) {
      continue;
    }
    const auto pass = static_cast<GpuTimedPass>(passIndex);
    result.passMilliseconds[passIndex] = gpuTimestampMilliseconds(
      timestamps[passStartQuery(pass)],
      timestamps[passEndQuery(pass)],
      validBits,
      periodNanoseconds
    );
  }
  result.outlineGpuMilliseconds =
    result.passMilliseconds[
      static_cast<std::size_t>(GpuTimedPass::OutlineTotal)
    ];
  if (!result.gpuPrimaryCommandBufferMilliseconds.has_value()) {
    result.unavailableReason = "invalid_timestamp_info";
  } else {
    for (std::size_t passIndex = 0;
         passIndex < kGpuTimedPassCount;
         ++passIndex) {
      if (
        result.passApplicable[passIndex] &&
        !result.passMilliseconds[passIndex].has_value()
      ) {
        result.unavailableReason =
          "invalid_" +
          std::string(gpuTimedPassName(
            static_cast<GpuTimedPass>(passIndex)
          )) +
          "_timestamp";
        break;
      }
    }
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
  std::optional<std::size_t> unavailableResultIndex;
  std::uint64_t pollIndex = 0;
  std::array<bool, kGpuTimedPassCount> passStarted = {};
  std::array<bool, kGpuTimedPassCount> passEnded = {};
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
  impl_->unavailableResultIndex.reset();
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
      queryResult = readOne(frameEndQuery());
    }
    for (std::size_t passIndex = 0;
         error.empty() &&
         queryResult == SDL_LG_GPU_TIMESTAMP_AVAILABLE &&
         passIndex < kGpuTimedPassCount;
         ++passIndex) {
      if (!impl_->ring.slot(index).passApplicable[passIndex]) {
        continue;
      }
      const auto pass = static_cast<GpuTimedPass>(passIndex);
      queryResult = readOne(passStartQuery(pass));
      if (queryResult == SDL_LG_GPU_TIMESTAMP_AVAILABLE) {
        queryResult = readOne(passEndQuery(pass));
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
  impl_->recordingSlot.reset();
  impl_->unavailableResultIndex.reset();
  impl_->passStarted = {};
  impl_->passEnded = {};
  impl_->frameEnded = false;
  const auto beginUnavailableResult = [&](std::string reason) {
    GpuFrameTimingResult result;
    result.benchmarkFrameIndex = benchmarkFrameIndex;
    result.outlineApplicable = outlineApplicable;
    result.passApplicable[
      static_cast<std::size_t>(GpuTimedPass::OutlineTotal)
    ] = outlineApplicable;
    result.unavailableReason = std::move(reason);
    impl_->results.push_back(std::move(result));
    impl_->unavailableResultIndex = impl_->results.size() - 1U;
  };
  if (!impl_->metadata.available || impl_->fatalError) {
    beginUnavailableResult(
      impl_->metadata.unavailableReason.empty()
        ? "gpu_timing_unavailable"
        : impl_->metadata.unavailableReason
    );
    return false;
  }
  impl_->recordingSlot =
    impl_->ring.begin(benchmarkFrameIndex, outlineApplicable);
  if (!impl_->recordingSlot.has_value()) {
    beginUnavailableResult("ring_full");
    return false;
  }
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
  result.passApplicable[
    static_cast<std::size_t>(GpuTimedPass::OutlineTotal)
  ] = outlineApplicable;
  result.unavailableReason = std::move(reason);
  impl_->results.push_back(std::move(result));
}

void GpuTimestampTiming::beginOutline(void* commandBufferPointer) {
  beginPass(commandBufferPointer, GpuTimedPass::OutlineTotal);
}

void GpuTimestampTiming::endOutline(void* commandBufferPointer) {
  endPass(commandBufferPointer, GpuTimedPass::OutlineTotal);
}

void GpuTimestampTiming::beginPass(
  void* commandBufferPointer,
  GpuTimedPass pass
) {
  if (pass == GpuTimedPass::Count) {
    return;
  }
  const std::size_t passIndex = static_cast<std::size_t>(pass);
  if (
    impl_->unavailableResultIndex.has_value() &&
    *impl_->unavailableResultIndex < impl_->results.size()
  ) {
    GpuFrameTimingResult& result =
      impl_->results[*impl_->unavailableResultIndex];
    result.passApplicable[passIndex] = true;
    if (pass == GpuTimedPass::OutlineTotal) {
      result.outlineApplicable = true;
    }
    return;
  }
  if (!impl_->recordingSlot.has_value()) {
    return;
  }
  if (impl_->passStarted[passIndex]) {
    return;
  }
  impl_->passStarted[passIndex] = true;
  const std::size_t slotIndex = *impl_->recordingSlot;
  (void)impl_->ring.markPassApplicable(slotIndex, pass);
#if LG_DUEL_HAS_SDL3 && LG_DUEL_SDL_GPU_TIMESTAMP_EXT
  if (
    impl_->slotErrors[slotIndex].empty() &&
    !SDL_LG_WriteGPUTimestamp(
      static_cast<SDL_GPUCommandBuffer*>(commandBufferPointer),
      impl_->pool,
      static_cast<Uint32>(
        slotIndex * GpuTimingRing::kQueriesPerSlot + passStartQuery(pass)
      ),
      SDL_LG_GPU_TIMESTAMP_TOP_OF_PIPE
    )
  ) {
    impl_->slotErrors[slotIndex] =
      std::string(gpuTimedPassName(pass)) + "_timestamp_start_failed";
    impl_->fatalError = true;
  }
#else
  (void)commandBufferPointer;
#endif
}

void GpuTimestampTiming::endPass(
  void* commandBufferPointer,
  GpuTimedPass pass
) {
  if (!impl_->recordingSlot.has_value() || pass == GpuTimedPass::Count) {
    return;
  }
  const std::size_t passIndex = static_cast<std::size_t>(pass);
  if (!impl_->passStarted[passIndex] || impl_->passEnded[passIndex]) {
    return;
  }
  impl_->passEnded[passIndex] = true;
#if LG_DUEL_HAS_SDL3 && LG_DUEL_SDL_GPU_TIMESTAMP_EXT
  const std::size_t slotIndex = *impl_->recordingSlot;
  if (
    impl_->slotErrors[slotIndex].empty() &&
    !SDL_LG_WriteGPUTimestamp(
      static_cast<SDL_GPUCommandBuffer*>(commandBufferPointer),
      impl_->pool,
      static_cast<Uint32>(
        slotIndex * GpuTimingRing::kQueriesPerSlot + passEndQuery(pass)
      ),
      SDL_LG_GPU_TIMESTAMP_BOTTOM_OF_PIPE
    )
  ) {
    impl_->slotErrors[slotIndex] =
      std::string(gpuTimedPassName(pass)) + "_timestamp_end_failed";
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
  for (std::size_t passIndex = 0;
       passIndex < kGpuTimedPassCount;
       ++passIndex) {
    if (impl_->passStarted[passIndex] && !impl_->passEnded[passIndex]) {
      endPass(commandBufferPointer, static_cast<GpuTimedPass>(passIndex));
    }
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
        slotIndex * GpuTimingRing::kQueriesPerSlot + frameEndQuery()
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
  impl_->unavailableResultIndex.reset();
}

std::span<const GpuFrameTimingResult> GpuTimestampTiming::takeResults() {
  impl_->unavailableResultIndex.reset();
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
