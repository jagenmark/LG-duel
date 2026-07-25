#pragma once

#include "dev/DevJson.hpp"
#include "shared/Math.hpp"
#include "sim/UserCommand.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lg::benchmark {

inline constexpr int kBenchmarkVersion = 1;
inline constexpr int kBenchmarkSchemaVersion = 1;
inline constexpr int kFrameTimelineSchemaVersion = 1;

struct CameraPose {
  Vec3 position = {};
  float yawDegrees = 0.0F;
  float pitchDegrees = 0.0F;
  std::optional<float> fieldOfView;
};

struct CameraKeyframe : CameraPose {
  std::optional<double> timeSeconds;
  std::optional<double> progress;
};

struct Screenshot {
  std::string name;
  double progress = 1.0;
};

struct Actors {
  int bots = 0;
  Weapon weapon = Weapon::MachineGun;
  std::string attackMode = "off";
  bool stare = true;
  bool standstill = false;
  bool dodge = false;
  int dodgeMinMilliseconds = 250;
  int dodgeMaxMilliseconds = 750;
  int expectedCount = 0;
};

struct Scenario {
  int schemaVersion = 0;
  int expectedBenchmarkVersion = 0;
  std::string name;
  std::string map;
  int width = 0;
  int height = 0;
  int fullscreen = 0;
  int vsync = 0;
  int frameCap = 0;
  float fieldOfView = 90.0F;
  std::optional<double> warmupSeconds;
  std::optional<std::uint64_t> warmupFrames;
  std::optional<double> measuredSeconds;
  std::optional<std::uint64_t> measuredFrames;
  CameraPose cameraStart;
  std::vector<CameraKeyframe> cameraPath;
  Actors actors;
  std::map<std::string, std::string, std::less<>> cvars;
  std::vector<Screenshot> screenshots;
  bool hideHud = false;
  bool hideOverlays = false;
  bool unsupportedEffectFixture = false;
};

struct ParseResult {
  Scenario scenario;
  bool ok = false;
  std::string error;
};

struct FrameSample {
  std::uint64_t index = 0;
  double elapsedSeconds = 0.0;
  double frameMilliseconds = 0.0;
  double sceneBuildMilliseconds = 0.0;
  double vertexUploadMilliseconds = 0.0;
  double swapchainAcquireMilliseconds = 0.0;
  double drawIssueMilliseconds = 0.0;
  double submitMilliseconds = 0.0;
  double renderCpuMilliseconds = 0.0;
  double snapshotDecodeMilliseconds = 0.0;
  double snapshotApplyMilliseconds = 0.0;
  double networkProcessingMilliseconds = 0.0;
  double simulationMilliseconds = 0.0;
  double movementCollisionMilliseconds = 0.0;
  double tracesMilliseconds = 0.0;
  double interpolationMilliseconds = 0.0;
  double animationMilliseconds = 0.0;
  double worldVisibilityMilliseconds = 0.0;
  double renderInstanceConstructionMilliseconds = 0.0;
  double worldCommandEncodingMilliseconds = 0.0;
  double dynamicCommandEncodingMilliseconds = 0.0;
  double uiMilliseconds = 0.0;
  std::optional<double> gpuPrimaryCommandBufferMilliseconds;
  bool outlineGpuTimingApplicable = false;
  std::optional<double> outlineGpuMilliseconds;
  bool gpuTimingResultReceived = false;
  std::uint32_t gpuTimingReadbackLatencyFrames = 0;
  std::uint32_t uploadedVertices = 0;
  std::uint32_t renderedTriangles = 0;
  std::uint32_t worldDraws = 0;
  std::uint32_t worldSubmittedTriangles = 0;
  std::uint32_t worldSubmittedRanges = 0;
  std::uint32_t worldTotalChunks = 0;
  std::uint32_t worldVisibleChunks = 0;
  std::uint32_t worldCulledChunks = 0;
  std::uint32_t worldVisibilityTestedNodes = 0;
  double worldVisibilityQueryMilliseconds = 0.0;
  std::uint32_t visiblePlayers = 0;
  std::uint32_t projectileCount = 0;
  std::uint32_t effectCount = 0;
  std::uint32_t instanceUploadBytes = 0;
  std::uint32_t instanceDraws = 0;
};

struct GpuFrameTiming {
  std::uint64_t benchmarkFrameIndex = 0;
  std::optional<double> gpuPrimaryCommandBufferMilliseconds;
  bool outlineApplicable = false;
  std::optional<double> outlineGpuMilliseconds;
  std::uint32_t readbackLatencyFrames = 0;
};

struct SimulationTickSample {
  std::uint64_t index = 0;
  std::uint64_t renderFrameIndex = 0;
  double elapsedSeconds = 0.0;
  double simulationMilliseconds = 0.0;
  double networkProcessingMilliseconds = 0.0;
  double movementCollisionMilliseconds = 0.0;
  double tracesMilliseconds = 0.0;
};

struct Summary {
  std::size_t count = 0;
  double elapsedSeconds = 0.0;
  double mean = 0.0;
  double median = 0.0;
  double p90 = 0.0;
  double p95 = 0.0;
  double p99 = 0.0;
  std::optional<double> p999;
  double minimum = 0.0;
  double maximum = 0.0;
  double standardDeviation = 0.0;
  double meanFps = 0.0;
};

struct ResultContext {
  std::string runId;
  std::string runGroup;
  std::string scenarioHash;
  std::string actualMap;
  std::string renderer;
  std::uint32_t actualMapContentHash = 0;
  int actualWidth = 0;
  int actualHeight = 0;
  std::string selectedPresentMode;
  bool gpuTimingAvailable = false;
  std::string gpuTimingBackend;
  std::string gpuTimingUnavailableReason;
  std::optional<std::uint32_t> gpuTimestampValidBits;
  std::optional<double> gpuTimestampPeriodNanoseconds;
  std::optional<std::uint32_t> gpuTimingReadbackLatencyFrames;
  std::optional<double> gpuTimingMeanReadbackLatencyFrames;
  std::string gpuTimingInstrumentationVersion;
  std::string sdlBaseRevision;
  std::string sdlPatchIdentity;
  bool completed = false;
  std::uint32_t actualActorCount = 0;
  std::vector<std::string> warnings;
  std::vector<std::string> screenshotPaths;
};

[[nodiscard]] ParseResult parseScenario(const dev::JsonValue& value);
[[nodiscard]] bool isSafeRunId(std::string_view value);
[[nodiscard]] bool isSafeScenarioHash(std::string_view value);
[[nodiscard]] CameraPose cameraAt(const Scenario& scenario, double measuredSeconds);
[[nodiscard]] Summary summarize(const std::vector<FrameSample>& samples);
[[nodiscard]] bool applyGpuFrameTiming(
  std::vector<FrameSample>& samples,
  const GpuFrameTiming& timing
);
[[nodiscard]] dev::JsonValue resultJson(
  const Scenario& scenario,
  const ResultContext& context,
  const std::vector<FrameSample>& samples,
  const std::vector<SimulationTickSample>& tickSamples
);
[[nodiscard]] dev::JsonValue frameTimelineJson(
  const Scenario& scenario,
  const ResultContext& context,
  const std::vector<FrameSample>& samples,
  const std::vector<SimulationTickSample>& tickSamples
);
[[nodiscard]] bool writeArtifacts(
  const std::filesystem::path& benchmarkRoot,
  const Scenario& scenario,
  const ResultContext& context,
  const std::vector<FrameSample>& samples,
  const std::vector<SimulationTickSample>& tickSamples,
  std::filesystem::path& resultDirectory,
  std::string& error
);

} // namespace lg::benchmark
