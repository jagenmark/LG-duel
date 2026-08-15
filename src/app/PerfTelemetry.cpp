#include "app/PerfTelemetry.hpp"

#include "dev/DevJson.hpp"
#include "render/Renderer.hpp"

#include <algorithm>
#include <cmath>
#include <string>

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
  summary.lateMouseSample = summarizeMetric(
    [](const PerfSample& sample) {
      return sample.lateMouseSampleMilliseconds;
    }
  );
  summary.mouseSampleToSubmit = summarizeMetric(
    [](const PerfSample& sample) {
      return sample.mouseSampleToSubmitMilliseconds;
    }
  );
  summary.mouseSamplePhaseGain = summarizeMetric(
    [](const PerfSample& sample) {
      return sample.mouseSamplePhaseGainMilliseconds;
    }
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

dev::JsonValue benchmarkRenderPassDiagnostics(
  const RendererFrameDiagnostics& diagnostics
) {
  dev::JsonValue world = dev::JsonValue::objectValue();
  world.object["draw_calls"] =
    dev::JsonValue::numberValue(diagnostics.worldDrawCalls);
  world.object["gpu_indirect"] =
    dev::JsonValue::booleanValue(diagnostics.worldGpuIndirect);
  world.object["gpu_indirect_commands"] =
    dev::JsonValue::numberValue(diagnostics.worldGpuIndirectCommands);
  world.object["gpu_indirect_material_groups"] =
    dev::JsonValue::numberValue(diagnostics.worldGpuIndirectMaterialGroups);
  world.object["sky_draw_calls"] =
    dev::JsonValue::numberValue(diagnostics.skyDrawCalls);
  dev::JsonValue ambient = dev::JsonValue::objectValue();
  ambient.object["quality"] =
    dev::JsonValue::numberValue(diagnostics.ambientGroundingQuality);
  ambient.object["static_rays"] =
    dev::JsonValue::numberValue(diagnostics.ambientStaticRays);
  ambient.object["static_samples"] =
    dev::JsonValue::numberValue(diagnostics.ambientStaticSamples);
  ambient.object["static_cache_hits"] =
    dev::JsonValue::numberValue(diagnostics.ambientStaticCacheHits);
  ambient.object["static_minimum"] =
    dev::JsonValue::numberValue(diagnostics.ambientStaticMinimum);
  ambient.object["static_maximum"] =
    dev::JsonValue::numberValue(diagnostics.ambientStaticMaximum);
  ambient.object["probe_count"] =
    dev::JsonValue::numberValue(diagnostics.ambientProbeCount);
  ambient.object["probe_rays"] =
    dev::JsonValue::numberValue(diagnostics.ambientProbeRays);
  ambient.object["probe_bytes"] =
    dev::JsonValue::numberValue(diagnostics.ambientProbeBytes);
  ambient.object["probe_fingerprint"] = dev::JsonValue::stringValue(
    std::to_string(diagnostics.ambientProbeFingerprint)
  );
  ambient.object["probe_build_ms"] =
    dev::JsonValue::numberValue(diagnostics.ambientProbeBuildMilliseconds);
  ambient.object["dynamic_samples"] =
    dev::JsonValue::numberValue(diagnostics.ambientDynamicSamples);
  world.object["ambient"] = std::move(ambient);

  dev::JsonValue gltf = dev::JsonValue::objectValue();
  gltf.object["player_instances"] =
    dev::JsonValue::numberValue(diagnostics.gltfPlayerModelInstances);
  gltf.object["frustum_culled"] =
    dev::JsonValue::numberValue(diagnostics.gltfPlayerModelFrustumCulled);
  gltf.object["static_mesh_gpu_bytes"] =
    dev::JsonValue::numberValue(diagnostics.gltfStaticMeshGpuBytes);
  gltf.object["static_index_gpu_bytes"] =
    dev::JsonValue::numberValue(diagnostics.gltfStaticIndexGpuBytes);
  gltf.object["material_texture_gpu_bytes"] =
    dev::JsonValue::numberValue(
      static_cast<double>(diagnostics.gltfMaterialTextureGpuBytes)
    );
  gltf.object["material_texture_mip_levels"] =
    dev::JsonValue::numberValue(diagnostics.gltfMaterialTextureMipLevels);
  gltf.object["material_texture_binds"] =
    dev::JsonValue::numberValue(diagnostics.gltfMaterialTextureBinds);
  gltf.object["authored_material_textures_ready"] =
    dev::JsonValue::booleanValue(
      diagnostics.gltfAuthoredMaterialTexturesReady
    );
  gltf.object["material_fallback_used"] =
    dev::JsonValue::booleanValue(diagnostics.gltfMaterialFallbackUsed);
  gltf.object["pose_upload_bytes"] =
    dev::JsonValue::numberValue(diagnostics.gltfPoseUploadBytes);
  gltf.object["bone_palette_entries_uploaded"] =
    dev::JsonValue::numberValue(
      diagnostics.gltfBonePaletteEntriesUploaded
    );
  gltf.object["rigid_fallback_instances"] =
    dev::JsonValue::numberValue(diagnostics.gltfRigidFallbackInstances);
  gltf.object["gpu_skinned_instances"] =
    dev::JsonValue::numberValue(diagnostics.gltfGpuSkinnedInstances);
  gltf.object["body_batches"] =
    dev::JsonValue::numberValue(diagnostics.gltfBodyBatches);
  gltf.object["body_draw_calls"] =
    dev::JsonValue::numberValue(diagnostics.gltfBodyDrawCalls);
  gltf.object["shadow_caster_instances"] =
    dev::JsonValue::numberValue(diagnostics.gltfShadowCasterInstances);
  gltf.object["shadow_caster_draw_calls"] =
    dev::JsonValue::numberValue(diagnostics.gltfShadowCasterDrawCalls);
  gltf.object["outline_mask_batches"] =
    dev::JsonValue::numberValue(diagnostics.gltfOutlineMaskBatches);
  gltf.object["outline_mask_draw_calls"] =
    dev::JsonValue::numberValue(diagnostics.gltfOutlineMaskDrawCalls);
  gltf.object["legacy_cpu_skinned_vertex_upload_bytes"] =
    dev::JsonValue::numberValue(
      diagnostics.legacyCpuSkinnedGltfVertexUploadBytes
    );

  dev::JsonValue directPresent = dev::JsonValue::objectValue();
  directPresent.object["eligible"] =
    dev::JsonValue::booleanValue(diagnostics.directPresentEligible);
  directPresent.object["used"] =
    dev::JsonValue::booleanValue(diagnostics.directPresentUsed);
  directPresent.object["fallback_reason"] =
    dev::JsonValue::stringValue(diagnostics.directPresentFallbackReason);
  directPresent.object["format"] =
    dev::JsonValue::stringValue(diagnostics.directPresentFormat);

  dev::JsonValue root = dev::JsonValue::objectValue();
  root.object["world"] = std::move(world);
  root.object["gltf"] = std::move(gltf);
  root.object["direct_present"] = std::move(directPresent);
  return root;
}

} // namespace lg
