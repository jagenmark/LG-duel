#include "render/GpuTimestampTiming.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string_view>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) {
    return 0;
  }
  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

bool near(double actual, double expected) {
  return std::abs(actual - expected) < 0.000001;
}

}  // namespace

int main() {
  int failures = 0;

  const auto plainDelta = lg::gpuTimestampDeltaTicks(100U, 145U, 64U);
  failures += expect(
    plainDelta.has_value() && *plainDelta == 45U,
    "64-bit timestamps should use unsigned subtraction"
  );

  const auto wrappedDelta = lg::gpuTimestampDeltaTicks(250U, 5U, 8U);
  failures += expect(
    wrappedDelta.has_value() && *wrappedDelta == 11U,
    "narrow timestamps should wrap at their valid bit count"
  );

  const auto milliseconds =
    lg::gpuTimestampMilliseconds(0U, 4000U, 64U, 2.5);
  failures += expect(
    milliseconds.has_value() && near(*milliseconds, 0.01),
    "tick conversion should apply the device period in nanoseconds"
  );
  failures += expect(
    !lg::gpuTimestampMilliseconds(0U, 1U, 0U, 1.0).has_value() &&
      !lg::gpuTimestampMilliseconds(0U, 1U, 65U, 1.0).has_value() &&
      !lg::gpuTimestampMilliseconds(0U, 1U, 64U, 0.0).has_value(),
    "invalid timestamp metadata should not produce a zero duration"
  );

  lg::GpuTimingRing ring;
  const auto slot = ring.begin(91U, true);
  failures += expect(
    slot.has_value() &&
      ring.slot(*slot).state == lg::GpuTimingSlotState::Recording,
    "begin should move a free slot to recording"
  );
  if (slot.has_value()) {
    for (std::size_t passIndex = 0;
         passIndex < lg::kGpuTimedPassCount;
         ++passIndex) {
      failures += expect(
        ring.markPassApplicable(
          *slot,
          static_cast<lg::GpuTimedPass>(passIndex)
        ),
        "each GPU stage should become applicable while recording"
      );
    }
  }
  failures += expect(
    slot.has_value() && ring.markSubmitted(*slot, 10U) &&
      ring.slot(*slot).state == lg::GpuTimingSlotState::Submitted,
    "submission should retain the frame association"
  );
  std::array<std::uint64_t, lg::GpuTimingRing::kQueriesPerSlot> stamps = {};
  for (std::size_t index = 0; index < stamps.size(); ++index) {
    stamps[index] = 100U + index * 10U;
  }
  failures += expect(
    slot.has_value() &&
      ring.markAvailable(*slot, stamps, 64U, 1000.0, 13U) &&
      ring.slot(*slot).state == lg::GpuTimingSlotState::Available,
    "readback should move a submitted slot to available"
  );
  const auto result = slot.has_value() ? ring.consume(*slot) : std::nullopt;
  failures += expect(
    result.has_value() &&
      result->benchmarkFrameIndex == 91U &&
      result->outlineApplicable &&
      result->gpuPrimaryCommandBufferMilliseconds.has_value() &&
      near(*result->gpuPrimaryCommandBufferMilliseconds, 0.21) &&
      result->outlineGpuMilliseconds.has_value() &&
      near(*result->outlineGpuMilliseconds, 0.01) &&
      result->readbackLatencyFrames == 3U,
    "delayed results should keep their original frame ID and latency"
  );
  if (result.has_value()) {
    for (std::size_t passIndex = 0;
         passIndex < lg::kGpuTimedPassCount;
         ++passIndex) {
      failures += expect(
        result->passApplicable[passIndex] &&
          result->passMilliseconds[passIndex].has_value() &&
          near(*result->passMilliseconds[passIndex], 0.01),
        "readback should retain each applicable GPU stage"
      );
    }
  }
  failures += expect(
    slot.has_value() &&
      ring.slot(*slot).state == lg::GpuTimingSlotState::Consumed &&
      ring.recycle(*slot) &&
      ring.slot(*slot).state == lg::GpuTimingSlotState::Free,
    "only a consumed slot should become reusable"
  );

  std::array<std::size_t, lg::GpuTimingRing::kSlotCount> occupied = {};
  for (std::size_t index = 0; index < occupied.size(); ++index) {
    const auto occupiedSlot = ring.begin(1000U + index, false);
    failures += expect(
      occupiedSlot.has_value(),
      "each fixed ring slot should accept one frame"
    );
    if (occupiedSlot.has_value()) {
      occupied[index] = *occupiedSlot;
      failures += expect(
        ring.markSubmitted(*occupiedSlot, index),
        "each ring slot should enter submitted state"
      );
    }
  }
  failures += expect(
    !ring.begin(2000U, false).has_value(),
    "the ring should reject a frame instead of reusing an in-flight slot"
  );
  failures += expect(
    !ring.recycle(occupied.front()),
    "a submitted query range must not be recycled"
  );

  lg::GpuTimestampTiming unavailableTiming;
  failures += expect(
    !unavailableTiming.beginFrame(nullptr, 2000U, false),
    "disabled GPU timing should not start timestamp recording"
  );
  unavailableTiming.beginPass(nullptr, lg::GpuTimedPass::MainScene);
  unavailableTiming.beginPass(nullptr, lg::GpuTimedPass::UiOverlay);
  unavailableTiming.beginOutline(nullptr);
  const auto unavailableResults = unavailableTiming.takeResults();
  failures += expect(
    unavailableResults.size() == 1U &&
      unavailableResults.front().benchmarkFrameIndex == 2000U &&
      unavailableResults.front().unavailableReason == "not_initialized" &&
      unavailableResults.front().passApplicable[
        static_cast<std::size_t>(lg::GpuTimedPass::MainScene)
      ] &&
      unavailableResults.front().passApplicable[
        static_cast<std::size_t>(lg::GpuTimedPass::UiOverlay)
      ] &&
      unavailableResults.front().passApplicable[
        static_cast<std::size_t>(lg::GpuTimedPass::OutlineTotal)
      ] &&
      !unavailableResults.front().passApplicable[
        static_cast<std::size_t>(lg::GpuTimedPass::SunShadow)
      ],
    "unavailable timing should retain which GPU passes rendered"
  );

  if (failures == 0) {
    std::cout << "GPU timestamp timing tests passed\n";
  }
  return failures == 0 ? 0 : 1;
}
