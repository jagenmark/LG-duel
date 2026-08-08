#include "app/PerfTelemetry.hpp"

#include "dev/DevJson.hpp"
#include "render/Renderer.hpp"

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

  lg::RendererFrameDiagnostics renderer;
  renderer.worldDrawCalls = 12;
  renderer.skyDrawCalls = 2;
  renderer.ambientGroundingQuality = 2;
  renderer.ambientStaticRays = 10'662;
  renderer.ambientStaticSamples = 1'777;
  renderer.ambientStaticCacheHits = 791;
  renderer.ambientStaticMinimum = 140;
  renderer.ambientStaticMaximum = 255;
  renderer.ambientProbeCount = 405;
  renderer.ambientProbeRays = 2'430;
  renderer.ambientProbeBytes = 405;
  renderer.ambientProbeFingerprint = 3'448'240'832'750'486'961ULL;
  renderer.ambientProbeBuildMilliseconds = 0.25F;
  renderer.ambientDynamicSamples = 5;
  renderer.gltfPlayerModelInstances = 5;
  renderer.gltfMaterialTextureGpuBytes = 2'796'192;
  renderer.gltfMaterialTextureMipLevels = 10;
  renderer.gltfMaterialTextureBinds = 2;
  renderer.gltfAuthoredMaterialTexturesReady = true;
  renderer.gltfMaterialFallbackUsed = false;
  renderer.gltfPoseUploadBytes = 16'384;
  renderer.gltfBonePaletteEntriesUploaded = 256;
  renderer.gltfGpuSkinnedInstances = 5;
  renderer.gltfBodyBatches = 13;
  renderer.gltfBodyDrawCalls = 13;
  renderer.gltfOutlineMaskDrawCalls = 13;
  renderer.directPresentEligible = true;
  renderer.directPresentUsed = false;
  renderer.directPresentFallbackReason = "outline-enabled";
  renderer.directPresentFormat = "R8G8B8A8_UNORM";
  const lg::dev::JsonValue diagnostics =
    lg::benchmarkRenderPassDiagnostics(renderer);
  const lg::dev::JsonValue* world = diagnostics.find("world");
  const lg::dev::JsonValue* ambient = world != nullptr
    ? world->find("ambient")
    : nullptr;
  const lg::dev::JsonValue* gltf = diagnostics.find("gltf");
  const lg::dev::JsonValue* directPresent = diagnostics.find("direct_present");
  failures += expect(
    world != nullptr && ambient != nullptr && gltf != nullptr &&
      directPresent != nullptr &&
      lg::dev::numberMember(*world, "draw_calls") == 12.0 &&
      lg::dev::numberMember(*ambient, "quality") == 2.0 &&
      lg::dev::numberMember(*ambient, "static_rays") == 10'662.0 &&
      lg::dev::numberMember(*ambient, "probe_bytes") == 405.0 &&
      lg::dev::stringMember(*ambient, "probe_fingerprint") ==
        "3448240832750486961" &&
      lg::dev::numberMember(*ambient, "dynamic_samples") == 5.0 &&
      lg::dev::numberMember(*gltf, "player_instances") == 5.0 &&
      lg::dev::numberMember(*gltf, "material_texture_gpu_bytes") ==
        2'796'192.0 &&
      lg::dev::numberMember(*gltf, "material_texture_binds") == 2.0 &&
      lg::dev::boolMember(*gltf, "authored_material_textures_ready") == true &&
      lg::dev::boolMember(*gltf, "material_fallback_used") == false &&
      lg::dev::numberMember(*gltf, "pose_upload_bytes") == 16'384.0 &&
      lg::dev::numberMember(*gltf, "body_draw_calls") == 13.0 &&
      lg::dev::boolMember(*directPresent, "eligible") == true &&
      lg::dev::boolMember(*directPresent, "used") == false &&
      lg::dev::stringMember(*directPresent, "fallback_reason") ==
        "outline-enabled",
    "benchmark diagnostics should keep stable world, ambient, GLTF, and "
    "direct-present counters"
  );

  return failures == 0 ? 0 : 1;
}
