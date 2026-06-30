#pragma once

#include "net/NetTransport.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace lg {

struct PerfSample {
  float frameMilliseconds = 0.0F;
  float sceneBuildMilliseconds = 0.0F;
  float gpuVertexUploadMilliseconds = 0.0F;
  float swapchainAcquireMilliseconds = 0.0F;
  float worldDrawIssueMilliseconds = 0.0F;
  float submitMilliseconds = 0.0F;
  float totalRenderMilliseconds = 0.0F;
  std::uint32_t dynamicOpaqueVertices = 0;
  std::uint32_t dynamicTranslucentVertices = 0;
  std::uint32_t totalUploadedVertices = 0;
  std::uint32_t dynamicTriangles = 0;
  std::uint32_t visibleRemotePlayers = 0;
  std::uint32_t remoteBodyModelsBuilt = 0;
  std::uint32_t remoteWeaponModelsBuilt = 0;
  std::uint32_t playerOutlinesBuilt = 0;
  SnapshotDiagnostics snapshot = {};
};

struct PerfMetricSummary {
  float average = 0.0F;
  float p50 = 0.0F;
  float p95 = 0.0F;
  float p99 = 0.0F;
  float max = 0.0F;
};

struct PerfWindowSummary {
  PerfMetricSummary frame = {};
  PerfMetricSummary sceneBuild = {};
  PerfMetricSummary gpuVertexUpload = {};
  PerfMetricSummary swapchainAcquire = {};
  PerfMetricSummary worldDrawIssue = {};
  PerfMetricSummary submit = {};
  PerfMetricSummary totalRender = {};
  PerfMetricSummary snapshotDecode = {};
  PerfMetricSummary snapshotApply = {};
  PerfSample latest = {};
  std::size_t sampleCount = 0;
};

class PerfTelemetry {
public:
  static constexpr std::size_t kSampleCount = 256;

  void push(const PerfSample& sample);
  void clear();

  [[nodiscard]] std::size_t sampleCount() const;
  [[nodiscard]] PerfWindowSummary summarize();

private:
  using SampleSelector = float (*)(const PerfSample&);

  [[nodiscard]] PerfMetricSummary summarizeMetric(SampleSelector selector);

  std::array<PerfSample, kSampleCount> samples_ = {};
  std::array<float, kSampleCount> sorted_ = {};
  std::size_t nextSample_ = 0;
  std::size_t sampleCount_ = 0;
};

} // namespace lg
