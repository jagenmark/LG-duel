#include "app/PerfTelemetry.hpp"

#include <algorithm>
#include <cmath>

namespace lg {
namespace {

[[nodiscard]] float percentile(
  const std::array<float, PerfTelemetry::kSampleCount>& sorted,
  std::size_t sampleCount,
  float fraction
) {
  if (sampleCount == 0) {
    return 0.0F;
  }
  const std::size_t index = std::min(
    sampleCount - 1U,
    // Use the nearest indexed sample from the bounded window; interpolating would
    // report frame times that never actually occurred and can hide spikes.
    static_cast<std::size_t>(
      std::round(fraction * static_cast<float>(sampleCount - 1U))
    )
  );
  return sorted[index];
}

} // namespace

void PerfTelemetry::push(const PerfSample& sample) {
  // A fixed ring keeps telemetry allocation-free in the frame loop. Summary
  // statistics are order-independent, while nextSample_ identifies the latest.
  samples_[nextSample_] = sample;
  nextSample_ = (nextSample_ + 1U) % samples_.size();
  sampleCount_ = std::min(sampleCount_ + 1U, samples_.size());
}

void PerfTelemetry::clear() {
  nextSample_ = 0;
  sampleCount_ = 0;
}

std::size_t PerfTelemetry::sampleCount() const {
  return sampleCount_;
}

PerfWindowSummary PerfTelemetry::summarize() {
  PerfWindowSummary summary;
  summary.sampleCount = sampleCount_;
  if (sampleCount_ > 0) {
    const std::size_t latestIndex =
      (nextSample_ + samples_.size() - 1U) % samples_.size();
    summary.latest = samples_[latestIndex];
  }
  summary.frame = summarizeMetric(
    [](const PerfSample& sample) { return sample.frameMilliseconds; }
  );
  summary.sceneBuild = summarizeMetric(
    [](const PerfSample& sample) { return sample.sceneBuildMilliseconds; }
  );
  summary.gpuVertexUpload = summarizeMetric(
    [](const PerfSample& sample) { return sample.gpuVertexUploadMilliseconds; }
  );
  summary.swapchainAcquire = summarizeMetric(
    [](const PerfSample& sample) { return sample.swapchainAcquireMilliseconds; }
  );
  summary.worldDrawIssue = summarizeMetric(
    [](const PerfSample& sample) { return sample.worldDrawIssueMilliseconds; }
  );
  summary.worldVisibilityQuery = summarizeMetric(
    [](const PerfSample& sample) {
      return sample.worldVisibilityQueryMilliseconds;
    }
  );
  summary.submit = summarizeMetric(
    [](const PerfSample& sample) { return sample.submitMilliseconds; }
  );
  summary.totalRender = summarizeMetric(
    [](const PerfSample& sample) { return sample.totalRenderMilliseconds; }
  );
  summary.snapshotDecode = summarizeMetric(
    [](const PerfSample& sample) {
      return sample.snapshot.snapshotDecodeMilliseconds;
    }
  );
  summary.snapshotApply = summarizeMetric(
    [](const PerfSample& sample) {
      return sample.snapshot.snapshotApplyMilliseconds;
    }
  );
  return summary;
}

PerfMetricSummary PerfTelemetry::summarizeMetric(SampleSelector selector) {
  if (sampleCount_ == 0) {
    return {};
  }

  float sum = 0.0F;
  for (std::size_t index = 0; index < sampleCount_; ++index) {
    const float value = selector(samples_[index]);
    sorted_[index] = value;
    sum += value;
  }
  std::sort(sorted_.begin(), sorted_.begin() + sampleCount_);
  return PerfMetricSummary{
    sum / static_cast<float>(sampleCount_),
    percentile(sorted_, sampleCount_, 0.50F),
    percentile(sorted_, sampleCount_, 0.95F),
    percentile(sorted_, sampleCount_, 0.99F),
    sorted_[sampleCount_ - 1U],
  };
}

} // namespace lg
