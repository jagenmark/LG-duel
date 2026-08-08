#include "benchmark/Benchmark.hpp"
#include "benchmark/BenchmarkTiming.hpp"
#include "dev/DevJson.hpp"

#include <cmath>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) return 0;
  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

void burnBriefly() {
  const auto end = std::chrono::steady_clock::now() +
    std::chrono::microseconds(50);
  while (std::chrono::steady_clock::now() < end) {
  }
}

lg::benchmark::ParseResult parse(std::string_view text) {
  const lg::dev::JsonParseResult json = lg::dev::parseJson(text);
  if (!json.ok) return {{}, false, json.error};
  return lg::benchmark::parseScenario(json.value);
}

constexpr std::string_view validScenario = R"({
  "schema_version":1,"expected_benchmark_version":1,
  "name":"static-baseline","map":"eyetoeye","resolution":[1280,720],
  "warmup_frames":2,"measured_frames":4,
  "camera_start":{"position":[0,0,2],"yaw":0,"pitch":0,"fov":90},
  "camera_path":[{"progress":1,"position":[10,0,2],"yaw":90,"pitch":10,"fov":100}],
  "actors":{"bots":1,"weapon":"rl","attack_mode":"hard","stare":false,"standstill":true,"expected_count":2},
  "cvars":{"r_frustum_cull":1,"r_world_frustum_cull":1,"r_player_model":"box"},
  "screenshots":[{"name":"final","progress":1}]
})";

} // namespace

int main() {
  int failures = 0;
  failures += expect(
    lg::benchmark::frameTimingSink == nullptr &&
      lg::benchmark::tickTimingSink == nullptr,
    "benchmark timing should be disabled by default"
  );
  lg::benchmark::TimingValues frameTiming;
  lg::benchmark::TimingValues firstTickTiming;
  lg::benchmark::TimingValues secondTickTiming;
  {
    lg::benchmark::TimingSinkScope frameScope(&frameTiming, nullptr);
    {
      lg::benchmark::ScopedTiming frameOnly(
        lg::benchmark::TimingSubsystem::NetworkProcessing
      );
      burnBriefly();
    }
    {
      lg::benchmark::TimingSinkScope tickScope(
        &frameTiming, &firstTickTiming
      );
      lg::benchmark::ScopedTiming tick(
        lg::benchmark::TimingSubsystem::Simulation
      );
      burnBriefly();
    }
    {
      lg::benchmark::TimingSinkScope tickScope(
        &frameTiming, &secondTickTiming
      );
      lg::benchmark::ScopedTiming tick(
        lg::benchmark::TimingSubsystem::Simulation
      );
      burnBriefly();
    }
  }
  const auto timingIndex = [](lg::benchmark::TimingSubsystem subsystem) {
    return static_cast<std::size_t>(subsystem);
  };
  failures += expect(
    frameTiming.nanoseconds[timingIndex(
      lg::benchmark::TimingSubsystem::NetworkProcessing
    )] > 0 &&
      firstTickTiming.nanoseconds[timingIndex(
        lg::benchmark::TimingSubsystem::NetworkProcessing
      )] == 0,
    "frame-only spans should not leak into simulation-tick samples"
  );
  failures += expect(
    frameTiming.nanoseconds[timingIndex(
      lg::benchmark::TimingSubsystem::Simulation
    )] ==
      firstTickTiming.nanoseconds[timingIndex(
        lg::benchmark::TimingSubsystem::Simulation
      )] +
      secondTickTiming.nanoseconds[timingIndex(
        lg::benchmark::TimingSubsystem::Simulation
      )],
    "multiple fixed ticks should accumulate exactly once into their render frame"
  );
  failures += expect(
    lg::benchmark::frameTimingSink == nullptr &&
      lg::benchmark::tickTimingSink == nullptr,
    "timing sink scopes should restore disabled mode"
  );
  const lg::benchmark::ParseResult valid = parse(validScenario);
  failures += expect(valid.ok, "documented benchmark scenario should parse");
  failures += expect(
    valid.ok && valid.scenario.cvars.at("s_volume") == "0",
    "benchmark scenarios should mute master sound by default"
  );
  failures += expect(
    valid.ok && valid.scenario.actors.weapon == lg::Weapon::RocketLauncher,
    "benchmark bot weapon should parse through the shared weapon catalog"
  );
  const lg::benchmark::ParseResult firingPlayer = parse(R"({
    "schema_version":1,"expected_benchmark_version":1,
    "name":"firing-player","map":"eyetoeye","resolution":[1280,720],
    "warmup_frames":2,"measured_frames":4,
    "camera_start":{"position":[0,0,2],"yaw":0,"pitch":0},
    "player_state":{"alive":true,"weapon":"machine_gun","attack":true}
  })");
  failures += expect(
    firingPlayer.ok &&
      firingPlayer.scenario.playerWeapon == lg::Weapon::MachineGun &&
    firingPlayer.scenario.playerAttack,
    "benchmark player weapon and held attack should parse"
  );

  const lg::benchmark::ParseResult graphicsProfile = parse(R"({
    "schema_version":1,"expected_benchmark_version":1,
    "name":"profile","map":"eyetoeye","resolution":[1280,720],
    "graphics_profile":"Competitive","render_scale":1.25,
    "warmup_frames":2,"measured_frames":4,
    "camera_start":{"position":[0,0,2],"yaw":0,"pitch":0}
  })");
  failures += expect(
    graphicsProfile.ok && graphicsProfile.scenario.graphicsProfile == "Competitive" &&
      std::fabs(graphicsProfile.scenario.renderScale - 1.25F) < 0.001F,
    "benchmark graphics profile and render scale should parse"
  );
  for (int materialQuality = 0; materialQuality <= 2; ++materialQuality) {
    lg::benchmark::Scenario materialQualityScenario;
    materialQualityScenario.graphicsProfile = "Default";
    materialQualityScenario.renderScale = 1.25F;
    materialQualityScenario.cvars["r_material_quality"] =
      std::to_string(materialQuality);
    materialQualityScenario.cvars["r_bloom"] = "0";
    materialQualityScenario.cvars["r_render_scale"] = "0.5";
    const auto overrides =
      lg::benchmark::benchmarkCvarOverrides(materialQualityScenario);
    failures += expect(
      overrides.at("r_material_quality") == std::to_string(materialQuality) &&
        overrides.at("r_bloom") == "0" &&
        overrides.at("r_render_scale") == "1.250000",
      "benchmark descriptor cvars should override a profile except render scale"
    );
  }
  lg::benchmark::Scenario profileOnlyScenario;
  profileOnlyScenario.graphicsProfile = "High";
  const auto profileOnlyOverrides =
    lg::benchmark::benchmarkCvarOverrides(profileOnlyScenario);
  failures += expect(
    profileOnlyOverrides.at("r_material_quality") == "2" &&
      profileOnlyOverrides.at("r_bloom") == "1" &&
      profileOnlyOverrides.at("r_render_scale") == "1.000000",
    "profile-only benchmark scenarios should retain graphics profile values"
  );
  const lg::benchmark::ParseResult bloomControl = parse(R"({
    "schema_version":1,"expected_benchmark_version":1,
    "name":"bloom-control","map":"eyetoeye","resolution":[1280,720],
    "warmup_frames":2,"measured_frames":4,
    "camera_start":{"position":[0,0,2],"yaw":0,"pitch":0},
    "cvars":{"r_bloom":0}
  })");
  failures += expect(
    bloomControl.ok && bloomControl.scenario.cvars.contains("r_bloom"),
    "benchmark scenarios should allow a fixed bloom setting"
  );
  const lg::benchmark::ParseResult ambientControl = parse(R"({
    "schema_version":1,"expected_benchmark_version":1,
    "name":"ambient-control","map":"eyetoeye","resolution":[1280,720],
    "warmup_frames":2,"measured_frames":4,
    "camera_start":{"position":[0,0,2],"yaw":0,"pitch":0},
    "cvars":{"r_ambient_grounding":2,"r_ambient_debug":0}
  })");
  failures += expect(
    ambientControl.ok &&
      ambientControl.scenario.cvars.at("r_ambient_grounding") == "2" &&
      ambientControl.scenario.cvars.at("r_ambient_debug") == "0",
    "benchmark scenarios should allow ambient controls"
  );
  for (int materialQuality = 0; materialQuality <= 2; ++materialQuality) {
    const lg::benchmark::ParseResult materialQualityControl = parse(
      R"({"schema_version":1,"expected_benchmark_version":1,
      "name":"material-quality-control","map":"eyetoeye","resolution":[1280,720],
      "warmup_frames":2,"measured_frames":4,
      "camera_start":{"position":[0,0,2],"yaw":0,"pitch":0},
      "cvars":{"r_material_quality":)" + std::to_string(materialQuality) + R"(}})"
    );
    failures += expect(
      materialQualityControl.ok &&
        materialQualityControl.scenario.cvars.at("r_material_quality") ==
          std::to_string(materialQuality),
      "benchmark scenarios should allow material quality values from zero through two"
    );
  }
  const lg::benchmark::ParseResult gameplayCvar = parse(R"({
    "schema_version":1,"expected_benchmark_version":1,
    "name":"gameplay-cvar","map":"eyetoeye","resolution":[1280,720],
    "warmup_frames":2,"measured_frames":4,
    "camera_start":{"position":[0,0,2],"yaw":0,"pitch":0},
    "cvars":{"g_gravity":1}
  })");
  failures += expect(
    !gameplayCvar.ok &&
      gameplayCvar.error ==
        "benchmark cvar is not presentation-allowlisted: g_gravity",
    "benchmark scenarios should reject unrelated gameplay cvars"
  );
  const lg::benchmark::ParseResult lateMouseControl = parse(R"({
    "schema_version":1,"expected_benchmark_version":1,
    "name":"late-mouse-control","map":"eyetoeye","resolution":[1280,720],
    "warmup_frames":2,"measured_frames":4,
    "camera_start":{"position":[0,0,2],"yaw":0,"pitch":0},
    "cvars":{"cl_late_mouse_sample":0}
  })");
  failures += expect(
    lateMouseControl.ok &&
      lateMouseControl.scenario.cvars.at("cl_late_mouse_sample") == "0",
    "benchmark scenarios should allow a late mouse sampling A/B setting"
  );
  const lg::benchmark::ParseResult mutedAudio = parse(R"({
    "schema_version":1,"expected_benchmark_version":1,
    "name":"muted-audio","map":"eyetoeye","resolution":[1280,720],
    "warmup_frames":2,"measured_frames":4,
    "camera_start":{"position":[0,0,2],"yaw":0,"pitch":0},
    "cvars":{"s_volume":0}
  })");
  failures += expect(
    mutedAudio.ok && mutedAudio.scenario.cvars.at("s_volume") == "0",
    "benchmark scenarios should allow a fixed master sound volume"
  );
  const lg::benchmark::ParseResult unsupportedFixture = parse(R"({
    "schema_version":1,"expected_benchmark_version":1,
    "name":"effects","map":"eyetoeye","resolution":[1280,720],
    "warmup_frames":2,"measured_frames":4,
    "camera_start":{"position":[0,0,2],"yaw":0,"pitch":0},
    "effects":{"projectiles":"fixed_cycle","tracers":"none","explosions":"none"}
  })");
  failures += expect(
    unsupportedFixture.ok && unsupportedFixture.scenario.unsupportedEffectFixture,
    "unimplemented synthetic effects should be retained as an invalidating workload request"
  );
  failures += expect(
    !parse(R"({"schema_version":1,"expected_benchmark_version":1,"name":"x","map":"eyetoeye","resolution":[1280,720],"warmup_seconds":0,"measured_frames":2,"camera_start":{"position":[0,0,0],"yaw":0,"pitch":0}})").ok,
    "zero duration should be rejected"
  );
  failures += expect(
    !parse(R"({"schema_version":1,"expected_benchmark_version":1,"name":"x","map":"eyetoeye","resolution":[0,720],"warmup_frames":2,"measured_frames":2,"camera_start":{"position":[0,0,0],"yaw":0,"pitch":0}})").ok,
    "invalid resolution should be rejected"
  );
  failures += expect(
    !parse(R"({"schema_version":1,"expected_benchmark_version":1,"name":"x","map":"eyetoeye","resolution":[1280,720],"warmup_seconds":1,"warmup_frames":2,"measured_frames":2,"camera_start":{"position":[0,0,0],"yaw":0,"pitch":0}})").ok,
    "ambiguous duration units should be rejected"
  );
  failures += expect(
    !parse(R"({"schema_version":1,"expected_benchmark_version":1,"name":"x","map":"../evil","resolution":[1280,720],"warmup_frames":2,"measured_frames":2,"camera_start":{"position":[0,0,0],"yaw":0,"pitch":0}})").ok,
    "map traversal should be rejected"
  );
  failures += expect(
    !parse(R"({"schema_version":1,"expected_benchmark_version":1,"name":"x","map":"eyetoeye","resolution":[1280,720],"warmup_frames":2,"measured_frames":2,"camera_start":{"position":[0,0,0],"yaw":0,"pitch":0},"actors":{"weapon":"banana"}})").ok,
    "unsupported benchmark bot weapons should be rejected"
  );
  failures += expect(!lg::benchmark::isSafeRunId("../../run"), "run id traversal should be rejected");
  failures += expect(!lg::benchmark::isSafeScenarioHash("not-a-hash"), "non-hex hash should be rejected");
  failures += expect(lg::benchmark::isSafeScenarioHash("0123456789abcdef"), "hex hash should be accepted");

  if (valid.ok) {
    lg::benchmark::Scenario cameraScenario = valid.scenario;
    cameraScenario.measuredFrames.reset();
    cameraScenario.measuredSeconds = 1.0;
    const lg::benchmark::CameraPose halfway = lg::benchmark::cameraAt(cameraScenario, 0.503);
    failures += expect(
      std::fabs(halfway.position.x - 4.96F) < 0.001F,
      "camera interpolation should quantize presentation time to 125 Hz"
    );
  }

  std::vector<lg::benchmark::FrameSample> samples(1000);
  for (std::size_t index = 0; index < samples.size(); ++index) {
    samples[index].index = index;
    samples[index].elapsedSeconds = static_cast<double>(index) / 100.0;
    samples[index].frameMilliseconds = static_cast<double>(index + 1U);
    samples[index].networkProcessingMilliseconds =
      static_cast<double>(index + 1U);
  }
  samples[1].worldVisibleChunks = 17;
  samples[1].renderCpuMilliseconds = 3.5;
  samples[1].lightCount = 2;
  samples[1].particleCount = 4;
  samples[1].transparentEffectCount = 7;
  samples[1].lateMouseSampleMilliseconds = 0.25;
  samples[1].mouseSampleToSubmitMilliseconds = 0.5;
  samples[1].mouseSamplePhaseGainMilliseconds = 1.5;
  samples[1].lateMouseSampleEnabled = true;
  samples[1].lateMouseSampleApplied = true;
  std::vector<lg::benchmark::SimulationTickSample> tickSamples(100);
  for (std::size_t index = 0; index < tickSamples.size(); ++index) {
    tickSamples[index].index = index;
    tickSamples[index].renderFrameIndex = index / 2U;
    tickSamples[index].simulationMilliseconds =
      static_cast<double>(index + 1U);
  }
  const lg::benchmark::Summary summary = lg::benchmark::summarize(samples);
  failures += expect(summary.count == 1000U && summary.median == 500.0, "nearest-rank median should be stable");
  failures += expect(summary.p99 == 990.0 && summary.p999 == 999.0, "high percentiles should use nearest rank");
  std::vector<lg::benchmark::FrameSample> measuredOnly(3);
  measuredOnly[0].index = 4;
  measuredOnly[1].index = 9;
  measuredOnly[2].index = 15;
  failures += expect(
    lg::benchmark::applyGpuFrameTiming(
      measuredOnly,
      {
        .benchmarkFrameIndex = 9,
        .gpuPrimaryCommandBufferMilliseconds = 2.5,
        .outlineApplicable = true,
        .outlineGpuMilliseconds = 0.4,
        .readbackLatencyFrames = 3,
        .unavailableReason = "",
      }
    ) &&
      !measuredOnly[0].gpuPrimaryCommandBufferMilliseconds.has_value() &&
      measuredOnly[1].gpuPrimaryCommandBufferMilliseconds == 2.5 &&
      measuredOnly[1].outlineGpuMilliseconds == 0.4 &&
      measuredOnly[1].gpuTimingReadbackLatencyFrames == 3 &&
      !measuredOnly[2].gpuPrimaryCommandBufferMilliseconds.has_value(),
    "delayed GPU results should patch the sample with the exact frame id"
  );
  failures += expect(
    !lg::benchmark::applyGpuFrameTiming(
      measuredOnly,
      {
        .benchmarkFrameIndex = 10,
        .gpuPrimaryCommandBufferMilliseconds = 7.0,
        .outlineApplicable = false,
        .outlineGpuMilliseconds = std::nullopt,
        .readbackLatencyFrames = 0,
        .unavailableReason = "",
      }
    ),
    "a GPU result for an unknown frame should not attach to another sample"
  );
  measuredOnly[2].outlineGpuTimingApplicable = true;
  measuredOnly[2].gpuPassTimingApplicable[
    static_cast<std::size_t>(lg::GpuTimedPass::MainScene)
  ] = true;
  failures += expect(
    lg::benchmark::applyGpuFrameTiming(
      measuredOnly,
      {
        .benchmarkFrameIndex = 15,
        .gpuPrimaryCommandBufferMilliseconds = std::nullopt,
        .outlineApplicable = false,
        .outlineGpuMilliseconds = std::nullopt,
        .readbackLatencyFrames = 0,
        .unavailableReason = "ring_full",
      }
    ) &&
      measuredOnly[2].outlineGpuTimingApplicable &&
      measuredOnly[2].gpuPassTimingApplicable[
        static_cast<std::size_t>(lg::GpuTimedPass::MainScene)
      ] &&
      !measuredOnly[2].outlineGpuMilliseconds.has_value(),
    "missing GPU timing should preserve all known pass applicability"
  );
  failures += expect(
    lg::benchmark::summarize(measuredOnly).count == 3U,
    "only the supplied measured samples should enter benchmark aggregates"
  );
  if (valid.ok) {
    lg::benchmark::ResultContext context;
    context.runId = "run-01"; context.runGroup = "group-01";
    context.scenarioHash = "0123456789abcdef"; context.actualMap = "eyetoeye";
    context.completed = true; context.actualActorCount = 2;
    context.gpuTimingBackend = "unsupported";
    context.gpuTimingUnavailableReason = "patched Vulkan timestamp support is not active";
    const lg::dev::JsonValue resultValue = lg::benchmark::resultJson(
      valid.scenario, context, samples, tickSamples
    );
    const lg::dev::JsonValue timelineValue =
      lg::benchmark::frameTimelineJson(
        valid.scenario, context, samples, tickSamples
      );
    const std::string result = lg::dev::writeJson(resultValue);
    failures += expect(result.find("\"percentile_method\"") != std::string::npos, "result should document percentile method");
    failures += expect(result.find("\"16.67\"") != std::string::npos, "result should include frame-time thresholds");
    failures += expect(
      result.find("\"world_visibility\"") != std::string::npos &&
        result.find("\"submitted_triangles\"") != std::string::npos,
      "result should summarize static-world BVH visibility telemetry"
    );
    const lg::dev::JsonValue* subsystem = resultValue.find("subsystem_timings");
    const lg::dev::JsonValue* renderFrame = subsystem != nullptr
      ? subsystem->find("render_frame") : nullptr;
    const lg::dev::JsonValue* network = renderFrame != nullptr
      ? renderFrame->find("network_processing") : nullptr;
    const lg::dev::JsonValue* simulationTick = subsystem != nullptr
      ? subsystem->find("simulation_tick") : nullptr;
    const lg::dev::JsonValue* simulation = simulationTick != nullptr
      ? simulationTick->find("simulation") : nullptr;
    const lg::dev::JsonValue* lateMouseSample =
      resultValue.find("late_mouse_sample");
    failures += expect(
      network != nullptr && network->find("median_ms") != nullptr &&
        network->find("median_ms")->number == 500.0 &&
        network->find("p95_ms")->number == 950.0 &&
        network->find("p99_ms")->number == 990.0,
      "render-frame subsystem timings should aggregate nearest-rank percentiles"
    );
    failures += expect(
      simulation != nullptr && simulation->find("median_ms") != nullptr &&
        simulation->find("median_ms")->number == 50.0 &&
        simulation->find("p95_ms")->number == 95.0 &&
        simulation->find("p99_ms")->number == 99.0,
      "simulation-tick timings should aggregate independently from render frames"
    );
    failures += expect(
      lateMouseSample != nullptr &&
        lateMouseSample->find("callback_ms") != nullptr &&
        lateMouseSample->find("callback_ms")->find("count")->number == 1000.0 &&
        lateMouseSample->find("sample_to_submit_ms") != nullptr &&
        lateMouseSample->find("phase_gain_ms") != nullptr &&
        lateMouseSample->find("enabled_count")->number == 1.0 &&
        lateMouseSample->find("applied_count")->number == 1.0 &&
        renderFrame != nullptr &&
        renderFrame->find("phase_gain") == nullptr,
      "late mouse values should have their own summaries and frame counts"
    );
    const lg::dev::JsonValue* timelineFrames = timelineValue.find("frames");
    const lg::dev::JsonValue* secondTimelineFrame =
      timelineFrames != nullptr &&
        timelineFrames->type == lg::dev::JsonValue::Type::Array &&
        timelineFrames->array.size() > 1U
      ? &timelineFrames->array[1] : nullptr;
    const lg::dev::JsonValue* totalGpu = secondTimelineFrame != nullptr
      ? secondTimelineFrame->find("total_gpu_ms") : nullptr;
    const lg::dev::JsonValue* gpuSubsystems = secondTimelineFrame != nullptr
      ? secondTimelineFrame->find("gpu_subsystems_ms") : nullptr;
    const lg::dev::JsonValue* gpuSubsystemStates =
      secondTimelineFrame != nullptr
        ? secondTimelineFrame->find("gpu_subsystem_states")
        : nullptr;
    const lg::dev::JsonValue* cpuSubsystems = secondTimelineFrame != nullptr
      ? secondTimelineFrame->find("cpu_subsystems_ms") : nullptr;
    const lg::dev::JsonValue* workload = secondTimelineFrame != nullptr
      ? secondTimelineFrame->find("workload_counters") : nullptr;
    const lg::dev::JsonValue* markers = secondTimelineFrame != nullptr
      ? secondTimelineFrame->find("event_markers") : nullptr;
    const lg::dev::JsonValue* timelineLateMouse =
      secondTimelineFrame != nullptr
        ? secondTimelineFrame->find("late_mouse_sample")
        : nullptr;
    failures += expect(
      timelineValue.find("format") != nullptr &&
        timelineValue.find("format")->string == "lg-duel-frame-timeline" &&
        timelineValue.find("schema_version") != nullptr &&
        timelineValue.find("schema_version")->number == 1.0 &&
        timelineValue.find("schema") != nullptr &&
        timelineValue.find("metadata") != nullptr,
      "frame timeline should identify and document its stable schema"
    );
    failures += expect(
      secondTimelineFrame != nullptr &&
        secondTimelineFrame->find("frame_index")->number == 1.0 &&
        secondTimelineFrame->find("total_cpu_ms")->number == 2.0 &&
        totalGpu != nullptr &&
        totalGpu->type == lg::dev::JsonValue::Type::Null &&
        secondTimelineFrame->find("gpu_timing_available") != nullptr &&
        !secondTimelineFrame->find("gpu_timing_available")->boolean &&
        gpuSubsystems != nullptr &&
        gpuSubsystems->type == lg::dev::JsonValue::Type::Object &&
        gpuSubsystems->find("main_scene")->type ==
          lg::dev::JsonValue::Type::Null &&
        gpuSubsystemStates != nullptr &&
        gpuSubsystemStates->find("main_scene")->string ==
          "not_applicable",
      "frame timeline should keep CPU totals and explicit GPU states"
    );
    failures += expect(
      cpuSubsystems != nullptr &&
        cpuSubsystems->find("renderer_total") != nullptr &&
        cpuSubsystems->find("renderer_total")->number == 3.5 &&
        workload != nullptr &&
        workload->find("world_visible_chunks") != nullptr &&
        workload->find("world_visible_chunks")->number == 17.0 &&
        workload->find("lights") != nullptr &&
        workload->find("lights")->number == 2.0 &&
        workload->find("particles") != nullptr &&
        workload->find("particles")->number == 4.0 &&
        workload->find("transparent_effects") != nullptr &&
        workload->find("transparent_effects")->number == 7.0,
      "frame timeline should retain named CPU timings and workload counters"
    );
    failures += expect(
      timelineLateMouse != nullptr &&
        timelineLateMouse->find("callback_ms")->number == 0.25 &&
        timelineLateMouse->find("sample_to_submit_ms")->number == 0.5 &&
        timelineLateMouse->find("phase_gain_ms")->number == 1.5 &&
        timelineLateMouse->find("enabled")->boolean &&
        timelineLateMouse->find("applied")->boolean &&
        cpuSubsystems != nullptr &&
        cpuSubsystems->find("phase_gain") == nullptr,
      "frame timeline should keep late mouse timing outside CPU subsystems"
    );
    failures += expect(
      markers != nullptr &&
        markers->type == lg::dev::JsonValue::Type::Array &&
        markers->array.size() == 1U &&
        markers->array[0].find("count") != nullptr &&
        markers->array[0].find("count")->number == 2.0 &&
        markers->array[0].find("tick_indices") != nullptr &&
        markers->array[0].find("tick_indices")->array[0].number == 2.0,
      "frame timeline should link simulation ticks to their render frame"
    );
    failures += expect(
      result.find("\"gpu_timing_available\":false") != std::string::npos &&
        result.find(
          "\"gpu_timing_unavailable_reason\":"
          "\"patched Vulkan timestamp support is not active\""
        ) != std::string::npos,
      "unsupported GPU timing metadata should give an explicit reason"
    );
    const lg::dev::JsonValue* unavailableGpuTimings =
      resultValue.find("gpu_execution_timings");
    const lg::dev::JsonValue* unavailablePrimary =
      unavailableGpuTimings != nullptr
        ? unavailableGpuTimings->find("gpu_primary_command_buffer") : nullptr;
    failures += expect(
      unavailablePrimary != nullptr &&
        unavailablePrimary->find("count")->number == 0.0 &&
        unavailablePrimary->find("median_ms")->type ==
          lg::dev::JsonValue::Type::Null &&
        resultValue.find("gpu_timestamp_valid_bits")->type ==
          lg::dev::JsonValue::Type::Null &&
        resultValue.find("gpu_timestamp_period_ns")->type ==
          lg::dev::JsonValue::Type::Null &&
        resultValue.find("gpu_timing_readback_latency_frames")->type ==
          lg::dev::JsonValue::Type::Null,
      "unsupported GPU timing metadata and summaries should be null, not zero"
    );
    failures += expect(
      result.find("\"summary\":{\"count\":1000") != std::string::npos &&
        result.find("\"subsystem_timings\"") != std::string::npos,
      "adding GPU data should keep the existing CPU result fields"
    );
    std::vector<lg::benchmark::FrameSample> gpuSamples(4);
    gpuSamples[0].gpuPrimaryCommandBufferMilliseconds = 1.0;
    gpuSamples[1].gpuPrimaryCommandBufferMilliseconds = 9.0;
    gpuSamples[3].gpuPrimaryCommandBufferMilliseconds = 3.0;
    gpuSamples[0].outlineGpuTimingApplicable = true;
    gpuSamples[0].outlineGpuMilliseconds = 0.25;
    gpuSamples[0].gpuPassTimingApplicable[
      static_cast<std::size_t>(lg::GpuTimedPass::MainScene)
    ] = true;
    gpuSamples[0].gpuPassMilliseconds[
      static_cast<std::size_t>(lg::GpuTimedPass::MainScene)
    ] = 0.6;
    gpuSamples[1].outlineGpuTimingApplicable = true;
    gpuSamples[1].gpuPassTimingApplicable[
      static_cast<std::size_t>(lg::GpuTimedPass::MainScene)
    ] = true;
    gpuSamples[1].gpuPassMilliseconds[
      static_cast<std::size_t>(lg::GpuTimedPass::MainScene)
    ] = 0.9;
    gpuSamples[2].outlineGpuTimingApplicable = false;
    gpuSamples[3].outlineGpuTimingApplicable = true;
    gpuSamples[3].outlineGpuMilliseconds = 0.75;
    const lg::dev::JsonValue gpuResult = lg::benchmark::resultJson(
      valid.scenario, context, gpuSamples, {}
    );
    const lg::dev::JsonValue* gpuTimings =
      gpuResult.find("gpu_execution_timings");
    const lg::dev::JsonValue* primary = gpuTimings != nullptr
      ? gpuTimings->find("gpu_primary_command_buffer") : nullptr;
    const lg::dev::JsonValue* outline = gpuTimings != nullptr
      ? gpuTimings->find("outline") : nullptr;
    const lg::dev::JsonValue* mainScene = gpuTimings != nullptr
      ? gpuTimings->find("main_scene") : nullptr;
    failures += expect(
      primary != nullptr && primary->find("count")->number == 3.0 &&
        primary->find("median_ms")->number == 3.0 &&
        outline != nullptr && outline->find("count")->number == 2.0 &&
        outline->find("median_ms")->number == 0.25 &&
        mainScene != nullptr &&
        mainScene->find("applicable_count")->number == 2.0 &&
        mainScene->find("count")->number == 2.0 &&
        mainScene->find("median_ms")->number == 0.6,
      "GPU aggregates should retain total, compatibility, and stage timings"
    );
    context.gpuTimingAvailable = true;
    context.gpuTimingBackend = "vulkan";
    context.gpuTimingUnavailableReason.clear();
    const lg::dev::JsonValue gpuTimeline = lg::benchmark::frameTimelineJson(
      valid.scenario,
      context,
      gpuSamples,
      {}
    );
    const lg::dev::JsonValue* gpuTimelineFrames = gpuTimeline.find("frames");
    const lg::dev::JsonValue* firstGpuFrame =
      gpuTimelineFrames != nullptr && !gpuTimelineFrames->array.empty()
        ? &gpuTimelineFrames->array[0]
        : nullptr;
    const lg::dev::JsonValue* firstGpuSubsystems =
      firstGpuFrame != nullptr
        ? firstGpuFrame->find("gpu_subsystems_ms")
        : nullptr;
    failures += expect(
      gpuTimeline.find("gpu_execution_timing_available") != nullptr &&
        gpuTimeline.find("gpu_execution_timing_available")->boolean &&
        gpuTimeline.find("gpu_timing")->find("sample_count")->number == 3.0 &&
        firstGpuFrame != nullptr &&
        firstGpuFrame->find("total_gpu_ms")->number == 1.0 &&
        firstGpuFrame->find("gpu_timing_available")->boolean &&
        firstGpuSubsystems != nullptr &&
        firstGpuSubsystems->find("main_scene")->number == 0.6,
      "frame timeline should serialize matched total and stage GPU timing"
    );
    failures += expect(result.find("\"expected_actors\":true") != std::string::npos, "result actor validity should use snapshot actor count");
    context.actualActorCount = 3;
    const std::string extraActorResult = lg::dev::writeJson(
      lg::benchmark::resultJson(valid.scenario, context, samples, tickSamples)
    );
    failures += expect(
      extraActorResult.find("\"expected_actors\":false") != std::string::npos,
      "extra actors should invalidate the workload instead of being accepted"
    );
    const std::filesystem::path artifactRoot =
      std::filesystem::temp_directory_path() /
      ("lg-duel-benchmark-telemetry-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()
      ));
    samples[0].gpuPrimaryCommandBufferMilliseconds = 1.25;
    samples[0].outlineGpuTimingApplicable = true;
    samples[0].gpuPassTimingApplicable[
      static_cast<std::size_t>(lg::GpuTimedPass::MainScene)
    ] = true;
    samples[0].gpuPassMilliseconds[
      static_cast<std::size_t>(lg::GpuTimedPass::MainScene)
    ] = 0.75;
    samples[0].gpuTimingResultReceived = true;
    samples[0].gpuTimingReadbackLatencyFrames = 2;
    std::filesystem::path resultDirectory;
    std::string artifactError;
    failures += expect(
      lg::benchmark::writeArtifacts(
        artifactRoot,
        valid.scenario,
        context,
        samples,
        tickSamples,
        resultDirectory,
        artifactError
      ),
      "benchmark artifacts should include frame and tick telemetry"
    );
    const auto readFile = [](const std::filesystem::path& path) {
      std::ifstream stream(path);
      return std::string(
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>()
      );
    };
    const std::string frameTelemetry = readFile(resultDirectory / "telemetry.csv");
    const std::string tickTelemetry = readFile(
      resultDirectory / "simulation-ticks.csv"
    );
    const std::string frameTimeline = readFile(
      resultDirectory / "frame-timeline.json"
    );
    const lg::dev::JsonParseResult parsedTimeline =
      lg::dev::parseJson(frameTimeline);
    failures += expect(
      frameTelemetry.find("network_processing_ms") != std::string::npos &&
        frameTelemetry.find("dynamic_command_encoding_ms") != std::string::npos &&
        frameTelemetry.find(
          "late_mouse_sample_ms,mouse_sample_to_submit_ms,"
          "mouse_sample_phase_gain_ms,late_mouse_sample_enabled,"
          "late_mouse_sample_applied"
        ) != std::string::npos &&
        frameTelemetry.find(
          "gpu_primary_command_buffer_ms,outline_gpu_ms,outline_gpu_state"
        ) != std::string::npos &&
        frameTelemetry.find(
          "main_scene_gpu_ms,main_scene_gpu_state"
        ) != std::string::npos &&
        frameTelemetry.find("gpu_timing_readback_latency_frames") !=
          std::string::npos &&
        frameTelemetry.find(
          "effects,lights,particles,transparent_effects,"
        ) != std::string::npos &&
        frameTelemetry.find(",,not_applicable,") != std::string::npos &&
        frameTelemetry.find(",,unavailable,") != std::string::npos,
      "per-render-frame CSV should expose subsystem timing columns"
    );
    failures += expect(
      tickTelemetry.find("simulation_ms,network_processing_ms") !=
        std::string::npos,
      "simulation tick CSV should expose its independent timing stream"
    );
    failures += expect(
      parsedTimeline.ok &&
        parsedTimeline.value.find("frame_count") != nullptr &&
        parsedTimeline.value.find("frame_count")->number == 1000.0,
      "benchmark artifacts should include a valid frame-timeline.json"
    );
    std::error_code cleanupError;
    std::filesystem::remove_all(artifactRoot, cleanupError);
  }

  if (failures == 0) std::cout << "Benchmark tests passed\n";
  return failures == 0 ? 0 : 1;
}
