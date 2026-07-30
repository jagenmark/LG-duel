#include "app/PerfTelemetry.hpp"

#include <cmath>
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

bool nearlyEqual(float lhs, float rhs, float epsilon = 0.001F) {
  return std::fabs(lhs - rhs) <= epsilon;
}

lg::PerfSample sample(float frameMilliseconds) {
  lg::PerfSample sample;
  sample.frameMilliseconds = frameMilliseconds;
  sample.sceneBuildMilliseconds = frameMilliseconds * 0.1F;
  sample.gpuVertexUploadMilliseconds = frameMilliseconds * 0.01F;
  sample.lateMouseSampleMilliseconds = frameMilliseconds * 0.02F;
  sample.mouseSampleToSubmitMilliseconds = frameMilliseconds * 0.03F;
  sample.mouseSamplePhaseGainMilliseconds = frameMilliseconds * 0.04F;
  sample.lateMouseSampleEnabled = true;
  sample.lateMouseSampleApplied = true;
  sample.snapshot.snapshotDecodeMilliseconds = frameMilliseconds * 0.001F;
  sample.snapshot.snapshotApplyMilliseconds = frameMilliseconds * 0.002F;
  return sample;
}

} // namespace

int main() {
  int failures = 0;

  lg::PerfTelemetry telemetry;
  lg::PerfWindowSummary empty = telemetry.summarize();
  failures += expect(
    empty.sampleCount == 0 &&
      empty.frame.average == 0.0F &&
      empty.frame.p95 == 0.0F,
    "empty performance history should summarize to zero values"
  );

  telemetry.push(sample(2.0F));
  lg::PerfWindowSummary one = telemetry.summarize();
  failures += expect(
    one.sampleCount == 1 &&
      nearlyEqual(one.frame.average, 2.0F) &&
      nearlyEqual(one.frame.p50, 2.0F) &&
      nearlyEqual(one.frame.p95, 2.0F) &&
      nearlyEqual(one.frame.p99, 2.0F) &&
      nearlyEqual(one.frame.max, 2.0F) &&
      nearlyEqual(one.lateMouseSample.average, 0.04F) &&
      nearlyEqual(one.mouseSampleToSubmit.average, 0.06F) &&
      nearlyEqual(one.mouseSamplePhaseGain.average, 0.08F) &&
      one.latest.lateMouseSampleEnabled &&
      one.latest.lateMouseSampleApplied,
    "one performance sample should produce stable percentile values"
  );

  telemetry.clear();
  for (std::size_t index = 0; index < lg::PerfTelemetry::kSampleCount; ++index) {
    telemetry.push(sample(static_cast<float>(index + 1U)));
  }
  lg::PerfWindowSummary full = telemetry.summarize();
  failures += expect(
    full.sampleCount == lg::PerfTelemetry::kSampleCount &&
      nearlyEqual(full.frame.average, 128.5F) &&
      nearlyEqual(full.frame.max, 256.0F) &&
      full.frame.p95 > full.frame.p50 &&
      full.frame.p99 >= full.frame.p95,
    "full performance history should summarize without invalid percentile indexing"
  );

  telemetry.clear();
  lg::PerfWindowSummary reset = telemetry.summarize();
  failures += expect(
    telemetry.sampleCount() == 0 &&
      reset.sampleCount == 0 &&
      reset.frame.max == 0.0F,
    "performance reset should clear history deterministically"
  );

  return failures == 0 ? 0 : 1;
}
