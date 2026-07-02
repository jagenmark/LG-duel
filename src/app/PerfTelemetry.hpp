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
  std::uint32_t normalPlayerBodyDynamicVertices = 0;
  std::uint32_t geometryOutlineDynamicVertices = 0;
  std::uint32_t outlinedPlayers = 0;
  std::uint32_t outlineMaskWidth = 0;
  std::uint32_t outlineMaskHeight = 0;
  std::uint32_t outlinePasses = 0;
  bool outlineCompositeEnabled = false;
  bool geometryOutlineFallbackUsed = false;
  std::uint32_t remoteWeaponCandidates = 0;
  std::uint32_t remoteWeaponsFrustumCulled = 0;
  std::uint32_t remoteWeaponInstances = 0;
  std::uint32_t remoteWeaponInstanceUploadBytes = 0;
  std::uint32_t remoteWeaponBatches = 0;
  std::uint32_t remoteWeaponDrawCalls = 0;
  std::uint32_t legacyRemoteWeaponDynamicVertices = 0;
  std::uint32_t firstPersonViewModelDrawCalls = 0;
  std::uint32_t firstPersonViewModelDynamicVertices = 0;
  std::uint32_t projectilesActive = 0;
  std::uint32_t projectilesFrustumCulled = 0;
  std::uint32_t projectilesRendered = 0;
  std::uint32_t plasmaInstances = 0;
  std::uint32_t rocketInstances = 0;
  std::uint32_t grenadeInstances = 0;
  std::uint32_t projectileCoreInstances = 0;
  std::uint32_t projectileGlowInstances = 0;
  std::uint32_t opaqueProjectileBatches = 0;
  std::uint32_t additiveProjectileBatches = 0;
  std::uint32_t projectileInstanceUploadBytes = 0;
  std::uint32_t projectileMeshDrawCalls = 0;
  std::uint32_t projectileGlowDrawCalls = 0;
  std::uint32_t legacyProjectileDynamicVertices = 0;
  std::uint32_t activeTransientEffects = 0;
  std::uint32_t activeMachineGunTracers = 0;
  std::uint32_t activeShotgunTracers = 0;
  std::uint32_t activeExplosionEffects = 0;
  std::uint32_t newExplosionEventsConsumed = 0;
  std::uint32_t tracerCandidates = 0;
  std::uint32_t tracerFrustumCulled = 0;
  std::uint32_t tracerInstancesSubmitted = 0;
  std::uint32_t tracerInstanceUploadBytes = 0;
  std::uint32_t tracerBatches = 0;
  std::uint32_t tracerDrawCalls = 0;
  std::uint32_t explosionCandidates = 0;
  std::uint32_t explosionFrustumCulled = 0;
  std::uint32_t explosionInstancesSubmitted = 0;
  std::uint32_t explosionInstanceUploadBytes = 0;
  std::uint32_t explosionOpaqueBatches = 0;
  std::uint32_t explosionAdditiveBatches = 0;
  std::uint32_t explosionDrawCalls = 0;
  std::uint32_t legacyWireframeExplosionDraws = 0;
  std::uint32_t legacyMachineGunShotgunVisualDraws = 0;
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
