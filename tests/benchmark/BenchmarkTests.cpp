#include "benchmark/Benchmark.hpp"
#include "benchmark/BenchmarkTiming.hpp"
#include "dev/DevJson.hpp"

#include <cmath>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
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
    valid.ok && valid.scenario.actors.weapon == lg::Weapon::RocketLauncher,
    "benchmark bot weapon should parse through the shared weapon catalog"
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
    samples[index].frameMilliseconds = static_cast<double>(index + 1U);
    samples[index].networkProcessingMilliseconds =
      static_cast<double>(index + 1U);
  }
  std::vector<lg::benchmark::SimulationTickSample> tickSamples(100);
  for (std::size_t index = 0; index < tickSamples.size(); ++index) {
    tickSamples[index].simulationMilliseconds =
      static_cast<double>(index + 1U);
  }
  const lg::benchmark::Summary summary = lg::benchmark::summarize(samples);
  failures += expect(summary.count == 1000U && summary.median == 500.0, "nearest-rank median should be stable");
  failures += expect(summary.p99 == 990.0 && summary.p999 == 999.0, "high percentiles should use nearest rank");
  if (valid.ok) {
    lg::benchmark::ResultContext context;
    context.runId = "run-01"; context.runGroup = "group-01";
    context.scenarioHash = "0123456789abcdef"; context.actualMap = "eyetoeye";
    context.completed = true; context.actualActorCount = 2;
    const lg::dev::JsonValue resultValue = lg::benchmark::resultJson(
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
    failures += expect(
      frameTelemetry.find("network_processing_ms") != std::string::npos &&
        frameTelemetry.find("dynamic_command_encoding_ms") != std::string::npos,
      "per-render-frame CSV should expose subsystem timing columns"
    );
    failures += expect(
      tickTelemetry.find("simulation_ms,network_processing_ms") !=
        std::string::npos,
      "simulation tick CSV should expose its independent timing stream"
    );
    std::error_code cleanupError;
    std::filesystem::remove_all(artifactRoot, cleanupError);
  }

  if (failures == 0) std::cout << "Benchmark tests passed\n";
  return failures == 0 ? 0 : 1;
}
