#pragma once

#include "dev/DevJson.hpp"
#include "benchmark/Benchmark.hpp"
#include "shared/Math.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lg::dev {

enum class ControlOperation {
  Status,
  LoadMap,
  ReloadMap,
  GetCamera,
  SetCamera,
  SetCollisionDebug,
  CaptureScreenshot,
  CaptureMapViews,
  RunBenchmark,
};

struct CameraTransform {
  Vec3 position = {};
  float yawDegrees = 0.0F;
  float pitchDegrees = 0.0F;
  std::optional<float> fieldOfView;
};

struct CameraViewpoint {
  std::string name;
  std::string label;
  CameraTransform camera;
  bool hideHud = true;
  bool hideOverlays = true;
};

struct ControlRequest {
  std::string id;
  ControlOperation operation = ControlOperation::Status;
  std::string mapName;
  std::string presetName;
  CameraTransform camera;
  int collisionDebugMode = 0;
  std::string captureName;
  bool hideHud = true;
  bool hideOverlays = true;
  std::vector<CameraViewpoint> viewpoints;
  benchmark::Scenario benchmarkScenario;
  std::string scenarioHash;
  std::string runGroup;
  std::string runId;
};

struct ControlRequestParseResult {
  ControlRequest request;
  bool ok = false;
  std::string error;
};

[[nodiscard]] ControlRequestParseResult parseControlRequest(const JsonValue& root);
[[nodiscard]] bool isSafeCaptureName(std::string_view name);
[[nodiscard]] std::string sanitizeGeneratedCaptureName(std::string_view name);
[[nodiscard]] JsonValue successResponse(std::string id, JsonValue result);
[[nodiscard]] JsonValue errorResponse(
  std::string id,
  std::string code,
  std::string message
);
[[nodiscard]] JsonValue cameraJson(const CameraTransform& camera);

constexpr std::uint16_t kDefaultControlPort = 27961;
constexpr std::size_t kMaxControlRequestBytes = 64U * 1024U;

} // namespace lg::dev
