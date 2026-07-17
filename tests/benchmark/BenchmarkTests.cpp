#include "benchmark/Benchmark.hpp"
#include "dev/DevJson.hpp"

#include <cmath>
#include <iostream>
#include <string_view>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) return 0;
  std::cerr << "FAILED: " << message << '\n';
  return 1;
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
  "actors":{"bots":1,"attack_mode":"hard","stare":false,"standstill":true,"expected_count":2},
  "cvars":{"r_frustum_cull":1,"r_player_model":"box"},
  "screenshots":[{"name":"final","progress":1}]
})";

} // namespace

int main() {
  int failures = 0;
  const lg::benchmark::ParseResult valid = parse(validScenario);
  failures += expect(valid.ok, "documented benchmark scenario should parse");
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
  }
  const lg::benchmark::Summary summary = lg::benchmark::summarize(samples);
  failures += expect(summary.count == 1000U && summary.median == 500.0, "nearest-rank median should be stable");
  failures += expect(summary.p99 == 990.0 && summary.p999 == 999.0, "high percentiles should use nearest rank");
  if (valid.ok) {
    lg::benchmark::ResultContext context;
    context.runId = "run-01"; context.runGroup = "group-01";
    context.scenarioHash = "0123456789abcdef"; context.actualMap = "eyetoeye";
    context.completed = true; context.actualActorCount = 2;
    const std::string result = lg::dev::writeJson(
      lg::benchmark::resultJson(valid.scenario, context, samples)
    );
    failures += expect(result.find("\"percentile_method\"") != std::string::npos, "result should document percentile method");
    failures += expect(result.find("\"16.67\"") != std::string::npos, "result should include frame-time thresholds");
    failures += expect(result.find("\"expected_actors\":true") != std::string::npos, "result actor validity should use snapshot actor count");
    context.actualActorCount = 3;
    const std::string extraActorResult = lg::dev::writeJson(
      lg::benchmark::resultJson(valid.scenario, context, samples)
    );
    failures += expect(
      extraActorResult.find("\"expected_actors\":false") != std::string::npos,
      "extra actors should invalidate the workload instead of being accepted"
    );
  }

  if (failures == 0) std::cout << "Benchmark tests passed\n";
  return failures == 0 ? 0 : 1;
}
