#include "benchmark/Benchmark.hpp"

#include "app/GraphicsProfiles.hpp"

#include "sim/MapRegistry.hpp"
#include "sim/WeaponCatalog.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <set>
#include <sstream>

namespace lg::benchmark {
namespace {

[[nodiscard]] bool integer(const dev::JsonValue* value, int& output, int minimum, int maximum) {
  if (value == nullptr || value->type != dev::JsonValue::Type::Number ||
      !std::isfinite(value->number) || std::floor(value->number) != value->number ||
      value->number < minimum || value->number > maximum) return false;
  output = static_cast<int>(value->number);
  return true;
}

[[nodiscard]] bool pose(const dev::JsonValue& value, CameraPose& output, std::string& error) {
  if (value.type != dev::JsonValue::Type::Object) {
    error = "camera pose must be an object";
    return false;
  }
  const dev::JsonValue* position = value.find("position");
  if (position == nullptr || position->type != dev::JsonValue::Type::Array ||
      position->array.size() != 3U) {
    error = "camera position must be a three-number array";
    return false;
  }
  float* components[] = {&output.position.x, &output.position.y, &output.position.z};
  for (std::size_t index = 0; index < 3U; ++index) {
    const dev::JsonValue& component = position->array[index];
    if (component.type != dev::JsonValue::Type::Number || !std::isfinite(component.number) ||
        std::fabs(component.number) > 1000000.0) {
      error = "camera position components must be finite";
      return false;
    }
    *components[index] = static_cast<float>(component.number);
  }
  const auto yaw = dev::numberMember(value, "yaw");
  const auto pitch = dev::numberMember(value, "pitch");
  if (!yaw || !std::isfinite(*yaw) || std::fabs(*yaw) > 1000000.0) {
    error = "camera yaw must be finite";
    return false;
  }
  if (!pitch || !std::isfinite(*pitch) || *pitch < -89.9 || *pitch > 89.9) {
    error = "camera pitch must be between -89.9 and 89.9";
    return false;
  }
  output.yawDegrees = static_cast<float>(*yaw);
  output.pitchDegrees = static_cast<float>(*pitch);
  if (const dev::JsonValue* fov = value.find("fov"); fov != nullptr) {
    if (fov->type != dev::JsonValue::Type::Number || !std::isfinite(fov->number) ||
        fov->number < 30.0 || fov->number > 140.0) {
      error = "camera fov must be between 30 and 140";
      return false;
    }
    output.fieldOfView = static_cast<float>(fov->number);
  }
  return true;
}

[[nodiscard]] bool duration(
  const dev::JsonValue& root,
  std::string_view secondsName,
  std::string_view framesName,
  std::optional<double>& seconds,
  std::optional<std::uint64_t>& frames,
  std::string& error
) {
  if (const dev::JsonValue* value = root.find(secondsName); value != nullptr) {
    if (value->type != dev::JsonValue::Type::Number || !std::isfinite(value->number) ||
        value->number <= 0.0 || value->number > 600.0) {
      error = std::string(secondsName) + " must be in (0, 600]";
      return false;
    }
    seconds = value->number;
  }
  if (const dev::JsonValue* value = root.find(framesName); value != nullptr) {
    int count = 0;
    if (!integer(value, count, 1, 1000000)) {
      error = std::string(framesName) + " must be an integer in [1, 1000000]";
      return false;
    }
    frames = static_cast<std::uint64_t>(count);
  }
  if (seconds.has_value() == frames.has_value()) {
    error = "exactly one of " + std::string(secondsName) + " or " +
      std::string(framesName) + " is required";
    return false;
  }
  return true;
}

[[nodiscard]] bool allowedCvar(std::string_view name) {
  static const std::set<std::string_view> allowed = {
    "cl_fov", "cl_late_mouse_sample", "cl_interp", "cl_interp_adaptive",
    "cl_interp_min", "cl_interp_max",
    "cl_interp_extrapolate", "cl_local_render_prediction", "r_maxfps", "r_msaa",
    "r_render_scale", "r_texture_filter", "r_texture_anisotropy", "r_texture_lod_bias",
    "r_draw_remote_players", "r_draw_remote_weapons", "r_draw_player_outlines",
    "r_player_outline_mode", "r_player_outline_style", "r_player_outline_width",
    "r_player_outline_debug_mask", "r_enemy_outline_width",
    "r_teammate_outline_width", "r_player_outline_scale", "r_show_weapon", "r_show_weapons",
    "r_frustum_cull", "r_world_frustum_cull", "r_world_gpu_indirect", "r_player_model", "r_bloom",
    "r_material_quality", "r_ambient_grounding", "r_ambient_debug",
    "r_combat_effects",
    "s_enable", "s_volume", "vid_fullscreen", "vid_width", "vid_height", "r_vsync",
    "r_present_mode"
  };
  return allowed.contains(name);
}

[[nodiscard]] std::optional<Weapon> parseScenarioWeapon(
  std::string_view token
) {
  if (const std::optional<Weapon> weapon = parseWeaponToken(token)) {
    return weapon;
  }
  std::string compact(token);
  compact.erase(
    std::remove_if(
      compact.begin(),
      compact.end(),
      [](char value) {
        return value == '_' || value == '-' || value == ' ';
      }
    ),
    compact.end()
  );
  return parseWeaponToken(compact);
}

[[nodiscard]] const GraphicsProfileDefinition* graphicsProfileByName(std::string_view name) {
  for (const GraphicsProfileDefinition& profile : kGraphicsProfiles) {
    if (profile.name == name) return &profile;
  }
  return nullptr;
}

[[nodiscard]] double nearestRank(const std::vector<double>& sorted, double fraction) {
  if (sorted.empty()) return 0.0;
  const std::size_t rank = static_cast<std::size_t>(
    std::ceil(fraction * static_cast<double>(sorted.size()))
  );
  return sorted[std::clamp<std::size_t>(rank, 1U, sorted.size()) - 1U];
}

[[nodiscard]] dev::JsonValue summaryJson(const Summary& value) {
  dev::JsonValue json = dev::JsonValue::objectValue();
  json.object["count"] = dev::JsonValue::numberValue(static_cast<double>(value.count));
  json.object["elapsed_seconds"] = dev::JsonValue::numberValue(value.elapsedSeconds);
  json.object["mean_ms"] = dev::JsonValue::numberValue(value.mean);
  json.object["median_ms"] = dev::JsonValue::numberValue(value.median);
  json.object["p90_ms"] = dev::JsonValue::numberValue(value.p90);
  json.object["p95_ms"] = dev::JsonValue::numberValue(value.p95);
  json.object["p99_ms"] = dev::JsonValue::numberValue(value.p99);
  json.object["p99_9_ms"] = value.p999
    ? dev::JsonValue::numberValue(*value.p999) : dev::JsonValue{};
  json.object["min_ms"] = dev::JsonValue::numberValue(value.minimum);
  json.object["max_ms"] = dev::JsonValue::numberValue(value.maximum);
  json.object["stddev_ms"] = dev::JsonValue::numberValue(value.standardDeviation);
  json.object["mean_fps"] = dev::JsonValue::numberValue(value.meanFps);
  return json;
}

} // namespace

ParseResult parseScenario(const dev::JsonValue& root) {
  if (root.type != dev::JsonValue::Type::Object) return {{}, false, "scenario must be an object"};
  Scenario scenario;
  if (!integer(root.find("schema_version"), scenario.schemaVersion, 1, 1))
    return {{}, false, "schema_version must be 1"};
  if (!integer(root.find("expected_benchmark_version"), scenario.expectedBenchmarkVersion, 1, 1))
    return {{}, false, "expected_benchmark_version must be 1"};
  scenario.name = dev::stringMember(root, "name").value_or("");
  scenario.map = dev::stringMember(root, "map").value_or("");
  if (!isSafeRunId(scenario.name)) return {{}, false, "name may only use letters, numbers, _ and -"};
  if (!isValidMapName(scenario.map)) return {{}, false, "map must be a safe map stem or .map filename"};
  const dev::JsonValue* resolution = root.find("resolution");
  if (resolution == nullptr || resolution->type != dev::JsonValue::Type::Array || resolution->array.size() != 2U ||
      !integer(&resolution->array[0], scenario.width, 320, 16384) ||
      !integer(&resolution->array[1], scenario.height, 200, 16384))
    return {{}, false, "resolution must be [width,height] within supported limits"};
  if (const dev::JsonValue* fullscreen = root.find("fullscreen"); fullscreen != nullptr) {
    if (fullscreen->type == dev::JsonValue::Type::Boolean) scenario.fullscreen = fullscreen->boolean ? 1 : 0;
    else if (!integer(fullscreen, scenario.fullscreen, 0, 2)) return {{}, false, "fullscreen must be boolean or 0..2"};
  }
  if (const dev::JsonValue* vsync = root.find("vsync"); vsync != nullptr) {
    if (vsync->type == dev::JsonValue::Type::Boolean) scenario.vsync = vsync->boolean ? 1 : 0;
    else if (!integer(vsync, scenario.vsync, 0, 1)) return {{}, false, "vsync must be boolean or 0/1"};
  }
  if (const dev::JsonValue* cap = root.find("frame_cap"); cap != nullptr &&
      !integer(cap, scenario.frameCap, 0, 1000)) return {{}, false, "frame_cap must be an integer in [0,1000]"};
  if (const auto fov = dev::numberMember(root, "fov")) {
    if (!std::isfinite(*fov) || *fov < 30.0 || *fov > 140.0) return {{}, false, "fov must be between 30 and 140"};
    scenario.fieldOfView = static_cast<float>(*fov);
  }
  if (const dev::JsonValue* profile = root.find("graphics_profile"); profile != nullptr) {
    if (profile->type != dev::JsonValue::Type::String || graphicsProfileByName(profile->string) == nullptr) {
      return {{}, false, "graphics_profile must be Low, Default, Competitive, or High"};
    }
    scenario.graphicsProfile = profile->string;
  }
  if (const dev::JsonValue* scale = root.find("render_scale"); scale != nullptr) {
    if (scale->type != dev::JsonValue::Type::Number || !std::isfinite(scale->number) || scale->number < 0.5 || scale->number > 1.5) {
      return {{}, false, "render_scale must be between 0.5 and 1.5"};
    }
    scenario.renderScale = static_cast<float>(scale->number);
  }
  std::string error;
  if (!duration(root, "warmup_seconds", "warmup_frames", scenario.warmupSeconds, scenario.warmupFrames, error) ||
      !duration(root, "measured_seconds", "measured_frames", scenario.measuredSeconds, scenario.measuredFrames, error))
    return {{}, false, std::move(error)};
  const dev::JsonValue* cameraStart = root.find("camera_start");
  if (cameraStart == nullptr || !pose(*cameraStart, scenario.cameraStart, error)) return {{}, false, std::move(error)};
  if (!scenario.cameraStart.fieldOfView) scenario.cameraStart.fieldOfView = scenario.fieldOfView;
  if (const dev::JsonValue* path = root.find("camera_path"); path != nullptr) {
    if (path->type != dev::JsonValue::Type::Array || path->array.size() > 1024U) return {{}, false, "camera_path must be an array of at most 1024 entries"};
    double previous = -1.0;
    for (const dev::JsonValue& value : path->array) {
      CameraKeyframe frame;
      if (!pose(value, frame, error)) return {{}, false, "camera_path: " + error};
      frame.timeSeconds = dev::numberMember(value, "time_seconds");
      frame.progress = dev::numberMember(value, "progress");
      if (frame.timeSeconds.has_value() == frame.progress.has_value()) return {{}, false, "camera keyframe needs exactly one of time_seconds or progress"};
      const double ordering = frame.progress.has_value()
        ? *frame.progress : *frame.timeSeconds;
      if (!std::isfinite(ordering) || ordering < 0.0 || (frame.progress && ordering > 1.0) || ordering < previous)
        return {{}, false, "camera keyframe positions must be finite, in range, and sorted"};
      previous = ordering;
      scenario.cameraPath.push_back(frame);
    }
  }
  if (const dev::JsonValue* actors = root.find("actors"); actors != nullptr) {
    if (actors->type != dev::JsonValue::Type::Object) return {{}, false, "actors must be an object"};
    if (const dev::JsonValue* bots = actors->find("bots"); bots && !integer(bots, scenario.actors.bots, 0, 64)) return {{}, false, "actors.bots must be in [0,64]"};
    const std::string weaponToken = dev::stringMember(*actors, "weapon").value_or("mg");
    const std::optional<Weapon> weapon = parseScenarioWeapon(weaponToken);
    if (!weapon.has_value()) return {{}, false, "actors.weapon is unsupported"};
    scenario.actors.weapon = *weapon;
    scenario.actors.attackMode = dev::stringMember(*actors, "attack_mode").value_or("off");
    if (scenario.actors.attackMode != "off" && scenario.actors.attackMode != "easy" && scenario.actors.attackMode != "medium" && scenario.actors.attackMode != "hard") return {{}, false, "actors.attack_mode is unsupported"};
    scenario.actors.stare = dev::boolMember(*actors, "stare").value_or(true);
    scenario.actors.standstill = dev::boolMember(*actors, "standstill").value_or(false);
    scenario.actors.dodge = dev::boolMember(*actors, "dodge").value_or(false);
    if (const dev::JsonValue* minimum = actors->find("dodge_min_ms"); minimum &&
        !integer(minimum, scenario.actors.dodgeMinMilliseconds, 1, 60000))
      return {{}, false, "actors.dodge_min_ms must be in [1,60000]"};
    if (const dev::JsonValue* maximum = actors->find("dodge_max_ms"); maximum &&
        !integer(maximum, scenario.actors.dodgeMaxMilliseconds, 1, 60000))
      return {{}, false, "actors.dodge_max_ms must be in [1,60000]"};
    if (scenario.actors.dodgeMinMilliseconds > scenario.actors.dodgeMaxMilliseconds)
      return {{}, false, "actors dodge interval is reversed"};
    scenario.actors.expectedCount = scenario.actors.bots;
    if (const dev::JsonValue* expected = actors->find("expected_count"); expected && !integer(expected, scenario.actors.expectedCount, 0, 64)) return {{}, false, "actors.expected_count must be in [0,64]"};
  }
  if (const dev::JsonValue* playerState = root.find("player_state");
      playerState != nullptr && playerState->type == dev::JsonValue::Type::Object) {
    scenario.hideHud = dev::boolMember(*playerState, "hide_hud").value_or(false);
    scenario.hideOverlays = dev::boolMember(*playerState, "hide_overlays").value_or(false);
    const std::string weaponToken =
      dev::stringMember(*playerState, "weapon").value_or("lg");
    const std::optional<Weapon> weapon = parseScenarioWeapon(weaponToken);
    if (!weapon.has_value()) {
      return {{}, false, "player_state.weapon is unsupported"};
    }
    scenario.playerWeapon = *weapon;
    scenario.playerAttack =
      dev::boolMember(*playerState, "attack").value_or(false);
  }
  if (const dev::JsonValue* effects = root.find("effects");
      effects != nullptr && effects->type == dev::JsonValue::Type::Object) {
    for (const auto& [name, value] : effects->object) {
      if ((name == "projectiles" || name == "tracers" || name == "explosions") &&
          value.type == dev::JsonValue::Type::String && value.string != "none") {
        scenario.unsupportedEffectFixture = true;
      }
    }
  }
  if (const dev::JsonValue* cvars = root.find("cvars"); cvars != nullptr) {
    if (cvars->type != dev::JsonValue::Type::Object) return {{}, false, "cvars must be an object"};
    for (const auto& [name, value] : cvars->object) {
      if (!allowedCvar(name)) return {{}, false, "benchmark cvar is not presentation-allowlisted: " + name};
      if (value.type == dev::JsonValue::Type::String) scenario.cvars[name] = value.string;
      else if (value.type == dev::JsonValue::Type::Boolean) scenario.cvars[name] = value.boolean ? "1" : "0";
      else if (value.type == dev::JsonValue::Type::Number && std::isfinite(value.number)) { std::ostringstream stream; stream << value.number; scenario.cvars[name] = stream.str(); }
      else return {{}, false, "benchmark cvar values must be scalar"};
    }
  }
  scenario.cvars.try_emplace("s_volume", "0");
  if (const dev::JsonValue* screenshots = root.find("screenshots"); screenshots != nullptr) {
    if (screenshots->type != dev::JsonValue::Type::Array || screenshots->array.size() > 32U) return {{}, false, "screenshots must be an array of at most 32 entries"};
    for (const dev::JsonValue& value : screenshots->array) {
      if (value.type != dev::JsonValue::Type::Object) return {{}, false, "screenshot must be an object"};
      Screenshot screenshot;
      screenshot.name = dev::stringMember(value, "name").value_or("");
      screenshot.progress = dev::numberMember(value, "progress").value_or(1.0);
      if (!isSafeRunId(screenshot.name) || !std::isfinite(screenshot.progress) || screenshot.progress < 0.0 || screenshot.progress > 1.0) return {{}, false, "screenshot name/progress is invalid"};
      scenario.screenshots.push_back(std::move(screenshot));
    }
  }
  return {std::move(scenario), true, {}};
}

std::map<std::string, std::string, std::less<>> benchmarkCvarOverrides(
  const Scenario& scenario
) {
  std::map<std::string, std::string, std::less<>> values;
  values["vid_width"] = std::to_string(scenario.width);
  values["vid_height"] = std::to_string(scenario.height);
  values["vid_fullscreen"] = std::to_string(scenario.fullscreen);
  values["r_vsync"] = std::to_string(scenario.vsync);
  values["r_present_mode"] = scenario.vsync != 0 ? "0" : "2";
  values["r_maxfps"] = std::to_string(scenario.frameCap);
  values["cl_fov"] = std::to_string(scenario.fieldOfView);
  if (const GraphicsProfileDefinition* profile =
        graphicsProfileByName(scenario.graphicsProfile);
      profile != nullptr) {
    for (const GraphicsProfileValue& value : profile->values) {
      values[std::string(value.cvar)] = std::string(value.value);
    }
  }
  for (const auto& [name, value] : scenario.cvars) {
    values[name] = value;
  }
  // The explicit descriptor field is the benchmark's single scale contract.
  values["r_render_scale"] = std::to_string(scenario.renderScale);
  return values;
}

bool isSafeRunId(std::string_view value) {
  return !value.empty() && value.size() <= 64U && std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isalnum(c) != 0 || c == '_' || c == '-'; });
}

bool isSafeScenarioHash(std::string_view value) {
  return value.size() >= 8U && value.size() <= 128U && std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
}

CameraPose cameraAt(const Scenario& scenario, double measuredSeconds) {
  // Presentation time is quantized to the authoritative 125 Hz tick so camera motion is repeatable.
  const double quantized = std::floor(std::max(0.0, measuredSeconds) * 125.0) / 125.0;
  const double duration = scenario.measuredSeconds.value_or(
    static_cast<double>(scenario.measuredFrames.value_or(1U)) / 125.0
  );
  const double progress = duration > 0.0 ? std::clamp(quantized / duration, 0.0, 1.0) : 1.0;
  std::vector<std::pair<double, CameraPose>> points;
  points.reserve(scenario.cameraPath.size() + 1U);
  points.emplace_back(0.0, scenario.cameraStart);
  for (const CameraKeyframe& frame : scenario.cameraPath) {
    points.emplace_back(
      frame.progress.has_value() ? *frame.progress : *frame.timeSeconds / duration,
      frame
    );
  }
  if (points.size() == 1U || progress <= points.front().first) return points.front().second;
  for (std::size_t index = 1; index < points.size(); ++index) {
    if (progress <= points[index].first) {
      const auto& [aTime, a] = points[index - 1U]; const auto& [bTime, b] = points[index];
      const float t = bTime > aTime ? static_cast<float>((progress - aTime) / (bTime - aTime)) : 1.0F;
      CameraPose result;
      result.position = a.position + (b.position - a.position) * t;
      result.yawDegrees = a.yawDegrees + (b.yawDegrees - a.yawDegrees) * t;
      result.pitchDegrees = a.pitchDegrees + (b.pitchDegrees - a.pitchDegrees) * t;
      result.fieldOfView = a.fieldOfView.value_or(scenario.fieldOfView) + (b.fieldOfView.value_or(scenario.fieldOfView) - a.fieldOfView.value_or(scenario.fieldOfView)) * t;
      return result;
    }
  }
  return points.back().second;
}

Summary summarize(const std::vector<FrameSample>& samples) {
  Summary result;
  result.count = samples.size();
  if (samples.empty()) return result;
  std::vector<double> sorted; sorted.reserve(samples.size());
  for (const FrameSample& sample : samples) sorted.push_back(sample.frameMilliseconds);
  std::sort(sorted.begin(), sorted.end());
  const double sum = std::accumulate(sorted.begin(), sorted.end(), 0.0);
  result.mean = sum / static_cast<double>(sorted.size());
  result.elapsedSeconds = sum / 1000.0;
  result.median = nearestRank(sorted, 0.5); result.p90 = nearestRank(sorted, 0.9);
  result.p95 = nearestRank(sorted, 0.95); result.p99 = nearestRank(sorted, 0.99);
  if (sorted.size() >= 1000U) result.p999 = nearestRank(sorted, 0.999);
  result.minimum = sorted.front(); result.maximum = sorted.back();
  double squared = 0.0; for (double value : sorted) squared += (value - result.mean) * (value - result.mean);
  result.standardDeviation = std::sqrt(squared / static_cast<double>(sorted.size()));
  result.meanFps = result.mean > 0.0 ? 1000.0 / result.mean : 0.0;
  return result;
}

bool applyGpuFrameTiming(
  std::vector<FrameSample>& samples,
  const GpuFrameTiming& timing
) {
  // Measured samples stay in frame-id order, so delayed result patching has
  // bounded search cost and never guesses from arrival order.
  const auto sample = std::lower_bound(
    samples.begin(),
    samples.end(),
    timing.benchmarkFrameIndex,
    [](const FrameSample& candidate, std::uint64_t frameIndex) {
      return candidate.index < frameIndex;
    }
  );
  if (sample == samples.end() ||
      sample->index != timing.benchmarkFrameIndex) return false;
  sample->gpuPrimaryCommandBufferMilliseconds =
    timing.gpuPrimaryCommandBufferMilliseconds;
  for (std::size_t passIndex = 0;
       passIndex < kGpuTimedPassCount;
       ++passIndex) {
    sample->gpuPassTimingApplicable[passIndex] =
      sample->gpuPassTimingApplicable[passIndex] ||
      timing.passApplicable[passIndex];
    sample->gpuPassMilliseconds[passIndex] =
      sample->gpuPassTimingApplicable[passIndex]
        ? timing.passMilliseconds[passIndex]
        : std::nullopt;
  }
  sample->outlineGpuTimingApplicable =
    sample->outlineGpuTimingApplicable || timing.outlineApplicable;
  sample->outlineGpuMilliseconds = sample->outlineGpuTimingApplicable
    ? timing.outlineGpuMilliseconds : std::nullopt;
  sample->gpuTimingResultReceived = true;
  sample->gpuTimingReadbackLatencyFrames = timing.readbackLatencyFrames;
  sample->gpuTimingUnavailableReason = timing.unavailableReason;
  return true;
}

dev::JsonValue resultJson(
  const Scenario& scenario,
  const ResultContext& context,
  const std::vector<FrameSample>& samples,
  const std::vector<SimulationTickSample>& tickSamples
) {
  const Summary summary = summarize(samples);
  dev::JsonValue root = dev::JsonValue::objectValue();
  root.object["schema_version"] = dev::JsonValue::numberValue(kBenchmarkSchemaVersion);
  root.object["benchmark_version"] = dev::JsonValue::numberValue(kBenchmarkVersion);
  root.object["run_id"] = dev::JsonValue::stringValue(context.runId);
  root.object["run_group"] = dev::JsonValue::stringValue(context.runGroup);
  root.object["scenario_name"] = dev::JsonValue::stringValue(scenario.name);
  root.object["scenario_hash"] = dev::JsonValue::stringValue(context.scenarioHash);
  root.object["renderer"] = dev::JsonValue::stringValue(context.renderer);
  root.object["map_content_hash"] = dev::JsonValue::numberValue(context.actualMapContentHash);
  root.object["actual_resolution"] = dev::JsonValue::arrayValue({
    dev::JsonValue::numberValue(context.actualWidth), dev::JsonValue::numberValue(context.actualHeight)
  });
  root.object["selected_present_mode"] = dev::JsonValue::stringValue(context.selectedPresentMode);
  root.object["graphics_profile"] = dev::JsonValue::stringValue(context.graphicsProfile);
  root.object["render_scale"] = dev::JsonValue::numberValue(context.renderScale);
  root.object["capture_readability"] = dev::JsonValue::stringValue(
    "fixed scenario camera and explicit profile/render scale; compare only matching values"
  );
  root.object["gpu_execution_timing_available"] =
    dev::JsonValue::booleanValue(context.gpuTimingAvailable);
  root.object["gpu_timing_available"] =
    dev::JsonValue::booleanValue(context.gpuTimingAvailable);
  root.object["gpu_timing_backend"] =
    dev::JsonValue::stringValue(context.gpuTimingBackend);
  root.object["gpu_timing_unavailable_reason"] =
    dev::JsonValue::stringValue(context.gpuTimingUnavailableReason);
  root.object["gpu_timestamp_valid_bits"] =
    context.gpuTimestampValidBits
      ? dev::JsonValue::numberValue(*context.gpuTimestampValidBits)
      : dev::JsonValue{};
  root.object["gpu_timestamp_period_ns"] =
    context.gpuTimestampPeriodNanoseconds
      ? dev::JsonValue::numberValue(*context.gpuTimestampPeriodNanoseconds)
      : dev::JsonValue{};
  root.object["gpu_timing_readback_latency_frames"] =
    context.gpuTimingReadbackLatencyFrames
      ? dev::JsonValue::numberValue(*context.gpuTimingReadbackLatencyFrames)
      : dev::JsonValue{};
  root.object["gpu_timing_mean_readback_latency_frames"] =
    context.gpuTimingMeanReadbackLatencyFrames
      ? dev::JsonValue::numberValue(*context.gpuTimingMeanReadbackLatencyFrames)
      : dev::JsonValue{};
  root.object["gpu_timing_instrumentation_version"] =
    dev::JsonValue::stringValue(context.gpuTimingInstrumentationVersion);
  root.object["sdl_base_revision"] =
    dev::JsonValue::stringValue(context.sdlBaseRevision);
  root.object["sdl_patch_identity"] =
    dev::JsonValue::stringValue(context.sdlPatchIdentity);
  dev::JsonValue sdlIdentity = dev::JsonValue::objectValue();
  sdlIdentity.object["base_revision"] =
    dev::JsonValue::stringValue(context.sdlBaseRevision);
  sdlIdentity.object["patch_identity"] =
    dev::JsonValue::stringValue(context.sdlPatchIdentity);
  root.object["sdl_identity"] = std::move(sdlIdentity);
  root.object["percentile_method"] = dev::JsonValue::stringValue("nearest-rank: sort ascending and select ceil(p*N), one-based; p99.9 is null below 1000 samples");
  root.object["summary"] = summaryJson(summary);
  const auto timingMetric = [](const auto& source, auto select) {
    std::vector<double> values;
    values.reserve(source.size());
    for (const auto& sample : source) values.push_back(select(sample));
    std::sort(values.begin(), values.end());
    dev::JsonValue metric = dev::JsonValue::objectValue();
    metric.object["count"] = dev::JsonValue::numberValue(
      static_cast<double>(values.size())
    );
    metric.object["median_ms"] = dev::JsonValue::numberValue(
      nearestRank(values, 0.5)
    );
    metric.object["p95_ms"] = dev::JsonValue::numberValue(
      nearestRank(values, 0.95)
    );
    metric.object["p99_ms"] = dev::JsonValue::numberValue(
      nearestRank(values, 0.99)
    );
    return metric;
  };
  dev::JsonValue renderFrameTimings = dev::JsonValue::objectValue();
  renderFrameTimings.object["network_processing"] = timingMetric(
    samples, [](const FrameSample& s) { return s.networkProcessingMilliseconds; }
  );
  renderFrameTimings.object["simulation"] = timingMetric(
    samples, [](const FrameSample& s) { return s.simulationMilliseconds; }
  );
  renderFrameTimings.object["movement_collision"] = timingMetric(
    samples, [](const FrameSample& s) { return s.movementCollisionMilliseconds; }
  );
  renderFrameTimings.object["traces"] = timingMetric(
    samples, [](const FrameSample& s) { return s.tracesMilliseconds; }
  );
  renderFrameTimings.object["interpolation"] = timingMetric(
    samples, [](const FrameSample& s) { return s.interpolationMilliseconds; }
  );
  renderFrameTimings.object["animation"] = timingMetric(
    samples, [](const FrameSample& s) { return s.animationMilliseconds; }
  );
  renderFrameTimings.object["world_visibility"] = timingMetric(
    samples, [](const FrameSample& s) { return s.worldVisibilityMilliseconds; }
  );
  renderFrameTimings.object["render_instance_construction"] = timingMetric(
    samples,
    [](const FrameSample& s) { return s.renderInstanceConstructionMilliseconds; }
  );
  renderFrameTimings.object["world_command_encoding"] = timingMetric(
    samples, [](const FrameSample& s) { return s.worldCommandEncodingMilliseconds; }
  );
  renderFrameTimings.object["dynamic_command_encoding"] = timingMetric(
    samples, [](const FrameSample& s) { return s.dynamicCommandEncodingMilliseconds; }
  );
  renderFrameTimings.object["ui"] = timingMetric(
    samples, [](const FrameSample& s) { return s.uiMilliseconds; }
  );
  renderFrameTimings.object["swapchain_acquisition"] = timingMetric(
    samples, [](const FrameSample& s) { return s.swapchainAcquireMilliseconds; }
  );
  renderFrameTimings.object["submission"] = timingMetric(
    samples, [](const FrameSample& s) { return s.submitMilliseconds; }
  );
  dev::JsonValue simulationTickTimings = dev::JsonValue::objectValue();
  simulationTickTimings.object["simulation"] = timingMetric(
    tickSamples,
    [](const SimulationTickSample& s) { return s.simulationMilliseconds; }
  );
  simulationTickTimings.object["network_processing"] = timingMetric(
    tickSamples,
    [](const SimulationTickSample& s) { return s.networkProcessingMilliseconds; }
  );
  simulationTickTimings.object["movement_collision"] = timingMetric(
    tickSamples,
    [](const SimulationTickSample& s) { return s.movementCollisionMilliseconds; }
  );
  simulationTickTimings.object["traces"] = timingMetric(
    tickSamples,
    [](const SimulationTickSample& s) { return s.tracesMilliseconds; }
  );
  dev::JsonValue subsystemTimings = dev::JsonValue::objectValue();
  subsystemTimings.object["clock"] =
    dev::JsonValue::stringValue("std::chrono::steady_clock");
  subsystemTimings.object["nested_spans"] = dev::JsonValue::booleanValue(true);
  subsystemTimings.object["render_frame"] = std::move(renderFrameTimings);
  subsystemTimings.object["simulation_tick"] = std::move(simulationTickTimings);
  root.object["subsystem_timings"] = std::move(subsystemTimings);
  dev::JsonValue lateMouseSample = dev::JsonValue::objectValue();
  lateMouseSample.object["callback_ms"] = timingMetric(
    samples,
    [](const FrameSample& s) { return s.lateMouseSampleMilliseconds; }
  );
  lateMouseSample.object["sample_to_submit_ms"] = timingMetric(
    samples,
    [](const FrameSample& s) {
      return s.mouseSampleToSubmitMilliseconds;
    }
  );
  lateMouseSample.object["phase_gain_ms"] = timingMetric(
    samples,
    [](const FrameSample& s) {
      return s.mouseSamplePhaseGainMilliseconds;
    }
  );
  lateMouseSample.object["enabled_count"] = dev::JsonValue::numberValue(
    static_cast<double>(std::count_if(
      samples.begin(),
      samples.end(),
      [](const FrameSample& s) { return s.lateMouseSampleEnabled; }
    ))
  );
  lateMouseSample.object["applied_count"] = dev::JsonValue::numberValue(
    static_cast<double>(std::count_if(
      samples.begin(),
      samples.end(),
      [](const FrameSample& s) { return s.lateMouseSampleApplied; }
    ))
  );
  root.object["late_mouse_sample"] = std::move(lateMouseSample);
  const auto optionalTimingMetric = [](const auto& source, auto select) {
    std::vector<double> values;
    values.reserve(source.size());
    for (const auto& sample : source) {
      if (const std::optional<double> value = select(sample);
          value.has_value() && std::isfinite(*value) && *value >= 0.0) {
        values.push_back(*value);
      }
    }
    std::sort(values.begin(), values.end());
    dev::JsonValue metric = dev::JsonValue::objectValue();
    metric.object["count"] =
      dev::JsonValue::numberValue(static_cast<double>(values.size()));
    metric.object["median_ms"] = values.empty()
      ? dev::JsonValue{} : dev::JsonValue::numberValue(nearestRank(values, 0.5));
    metric.object["p95_ms"] = values.empty()
      ? dev::JsonValue{} : dev::JsonValue::numberValue(nearestRank(values, 0.95));
    metric.object["p99_ms"] = values.empty()
      ? dev::JsonValue{} : dev::JsonValue::numberValue(nearestRank(values, 0.99));
    return metric;
  };
  dev::JsonValue gpuExecutionTimings = dev::JsonValue::objectValue();
  gpuExecutionTimings.object["gpu_primary_command_buffer"] = optionalTimingMetric(
    samples,
    [](const FrameSample& sample) {
      return sample.gpuPrimaryCommandBufferMilliseconds;
    }
  );
  const auto gpuPassTiming = [&samples, &optionalTimingMetric](
    auto selectTiming,
    auto selectApplicable
  ) {
    dev::JsonValue timing = optionalTimingMetric(samples, selectTiming);
    timing.object["applicable_count"] = dev::JsonValue::numberValue(
      static_cast<double>(std::count_if(
        samples.begin(),
        samples.end(),
        selectApplicable
      ))
    );
    return timing;
  };
  for (std::size_t passIndex = 0;
       passIndex < kGpuTimedPassCount;
       ++passIndex) {
    const auto pass = static_cast<GpuTimedPass>(passIndex);
    gpuExecutionTimings.object[std::string(gpuTimedPassName(pass))] =
      gpuPassTiming(
        [passIndex](const FrameSample& sample) {
          return sample.gpuPassMilliseconds[passIndex];
        },
        [passIndex](const FrameSample& sample) {
          return sample.gpuPassTimingApplicable[passIndex];
        }
      );
  }
  dev::JsonValue outlineTiming = optionalTimingMetric(
    samples,
    [](const FrameSample& sample) { return sample.outlineGpuMilliseconds; }
  );
  outlineTiming.object["applicable_count"] = dev::JsonValue::numberValue(
    static_cast<double>(std::count_if(
      samples.begin(),
      samples.end(),
      [](const FrameSample& sample) {
        return sample.outlineGpuTimingApplicable;
      }
    ))
  );
  gpuExecutionTimings.object["outline"] = std::move(outlineTiming);
  root.object["gpu_execution_timings"] = std::move(gpuExecutionTimings);
  const auto visibilityMetric = [&samples](auto select) {
    std::vector<double> values;
    values.reserve(samples.size());
    for (const FrameSample& sample : samples) {
      values.push_back(static_cast<double>(select(sample)));
    }
    std::sort(values.begin(), values.end());
    dev::JsonValue metric = dev::JsonValue::objectValue();
    metric.object["median"] = dev::JsonValue::numberValue(nearestRank(values, 0.5));
    metric.object["p95"] = dev::JsonValue::numberValue(nearestRank(values, 0.95));
    metric.object["max"] = dev::JsonValue::numberValue(
      values.empty() ? 0.0 : values.back()
    );
    return metric;
  };
  dev::JsonValue worldVisibility = dev::JsonValue::objectValue();
  worldVisibility.object["submitted_triangles"] = visibilityMetric(
    [](const FrameSample& sample) { return sample.worldSubmittedTriangles; }
  );
  worldVisibility.object["submitted_ranges"] = visibilityMetric(
    [](const FrameSample& sample) { return sample.worldSubmittedRanges; }
  );
  worldVisibility.object["visible_chunks"] = visibilityMetric(
    [](const FrameSample& sample) { return sample.worldVisibleChunks; }
  );
  worldVisibility.object["culled_chunks"] = visibilityMetric(
    [](const FrameSample& sample) { return sample.worldCulledChunks; }
  );
  worldVisibility.object["tested_nodes"] = visibilityMetric(
    [](const FrameSample& sample) { return sample.worldVisibilityTestedNodes; }
  );
  worldVisibility.object["query_ms"] = visibilityMetric(
    [](const FrameSample& sample) {
      return sample.worldVisibilityQueryMilliseconds;
    }
  );
  worldVisibility.object["gpu_indirect_frames"] =
    dev::JsonValue::numberValue(static_cast<double>(std::count_if(
      samples.begin(),
      samples.end(),
      [](const FrameSample& sample) { return sample.worldGpuIndirect; }
    )));
  worldVisibility.object["gpu_indirect_commands"] = visibilityMetric(
    [](const FrameSample& sample) { return sample.worldGpuIndirectCommands; }
  );
  worldVisibility.object["gpu_indirect_material_groups"] = visibilityMetric(
    [](const FrameSample& sample) {
      return sample.worldGpuIndirectMaterialGroups;
    }
  );
  root.object["world_visibility"] = std::move(worldVisibility);
  dev::JsonValue thresholds = dev::JsonValue::objectValue();
  for (const double threshold : {2.63, 4.17, 6.94, 8.33, 16.67}) {
    const std::size_t over = static_cast<std::size_t>(std::count_if(samples.begin(), samples.end(), [threshold](const FrameSample& sample) { return sample.frameMilliseconds > threshold; }));
    dev::JsonValue item = dev::JsonValue::objectValue(); item.object["count_over"] = dev::JsonValue::numberValue(static_cast<double>(over));
    item.object["percent_over"] = dev::JsonValue::numberValue(samples.empty() ? 0.0 : 100.0 * static_cast<double>(over) / static_cast<double>(samples.size()));
    std::ostringstream key; key << std::fixed << std::setprecision(2) << threshold; thresholds.object[key.str()] = std::move(item);
  }
  root.object["thresholds_ms"] = std::move(thresholds);
  std::uint32_t maxGeometry = 0, maxDraws = 0, maxActors = 0;
  for (const FrameSample& sample : samples) { maxGeometry = std::max(maxGeometry, sample.uploadedVertices); maxDraws = std::max(maxDraws, sample.worldDraws + sample.instanceDraws); maxActors = std::max(maxActors, sample.visiblePlayers); }
  dev::JsonValue validity = dev::JsonValue::objectValue();
  validity.object["map"] = dev::JsonValue::booleanValue(context.actualMap == scenario.map || context.actualMap == scenario.map + ".map");
  validity.object["completed"] = dev::JsonValue::booleanValue(context.completed);
  validity.object["frame_count"] = dev::JsonValue::booleanValue(!samples.empty() && (!scenario.measuredFrames || samples.size() >= *scenario.measuredFrames));
  validity.object["nonzero_geometry_or_draws"] = dev::JsonValue::booleanValue(maxGeometry > 0U || maxDraws > 0U);
  validity.object["expected_actors"] = dev::JsonValue::booleanValue(context.actualActorCount == static_cast<std::uint32_t>(scenario.actors.expectedCount));
  validity.object["resolution"] = dev::JsonValue::booleanValue(
    context.actualWidth == scenario.width && context.actualHeight == scenario.height
  );
  validity.object["supported_workload"] = dev::JsonValue::booleanValue(!scenario.unsupportedEffectFixture);
  root.object["validity"] = std::move(validity);
  dev::JsonValue warnings = dev::JsonValue::arrayValue(); for (const std::string& warning : context.warnings) warnings.array.push_back(dev::JsonValue::stringValue(warning)); root.object["warnings"] = std::move(warnings);
  dev::JsonValue screenshots = dev::JsonValue::arrayValue(); for (const std::string& path : context.screenshotPaths) screenshots.array.push_back(dev::JsonValue::stringValue(path)); root.object["screenshots"] = std::move(screenshots);
  return root;
}

dev::JsonValue frameTimelineJson(
  const Scenario& scenario,
  const ResultContext& context,
  const std::vector<FrameSample>& samples,
  const std::vector<SimulationTickSample>& tickSamples
) {
  dev::JsonValue root = dev::JsonValue::objectValue();
  root.object["format"] = dev::JsonValue::stringValue("lg-duel-frame-timeline");
  root.object["schema_version"] =
    dev::JsonValue::numberValue(kFrameTimelineSchemaVersion);

  dev::JsonValue schema = dev::JsonValue::objectValue();
  schema.object["frame_index"] =
    dev::JsonValue::stringValue("zero-based measured render-frame index");
  schema.object["elapsed_time_seconds"] = dev::JsonValue::stringValue(
    "steady-clock seconds since the measured phase began"
  );
  schema.object["total_cpu_ms"] = dev::JsonValue::stringValue(
    "CPU-observed wall-clock frame duration; subsystem spans may overlap"
  );
  schema.object["total_gpu_ms"] = dev::JsonValue::stringValue(
    "GPU execution duration in milliseconds, or null when unavailable"
  );
  schema.object["cpu_subsystems_ms"] = dev::JsonValue::stringValue(
    "named CPU spans in milliseconds; values are not required to sum to total_cpu_ms"
  );
  schema.object["late_mouse_sample"] = dev::JsonValue::stringValue(
    "late mouse callback cost, sample-to-submit span, phase gain, and enabled/applied flags; spans are not additive CPU subsystems"
  );
  schema.object["gpu_subsystems_ms"] = dev::JsonValue::stringValue(
    "named GPU stage spans in milliseconds; values may overlap and do not sum to total_gpu_ms"
  );
  schema.object["gpu_subsystem_states"] = dev::JsonValue::stringValue(
    "available, unavailable, or not_applicable state for every named GPU stage"
  );
  schema.object["gpu_timing_readback_latency_frames"] =
    dev::JsonValue::stringValue(
      "nonblocking timestamp readback delay, or null when no result arrived"
    );
  schema.object["gpu_timing_unavailable_reason"] =
    dev::JsonValue::stringValue(
      "per-frame query failure reason, or null when no failure was reported"
  );
  schema.object["workload_counters"] = dev::JsonValue::stringValue(
    "raw per-frame work and visibility counters"
  );
  schema.object["event_markers"] = dev::JsonValue::stringValue(
    "events assigned to this frame; simulation tick markers include source tick indices"
  );
  root.object["schema"] = std::move(schema);

  dev::JsonValue metadata = dev::JsonValue::objectValue();
  metadata.object["benchmark_version"] =
    dev::JsonValue::numberValue(kBenchmarkVersion);
  metadata.object["benchmark_schema_version"] =
    dev::JsonValue::numberValue(kBenchmarkSchemaVersion);
  metadata.object["run_id"] = dev::JsonValue::stringValue(context.runId);
  metadata.object["run_group"] = dev::JsonValue::stringValue(context.runGroup);
  metadata.object["scenario_name"] = dev::JsonValue::stringValue(scenario.name);
  metadata.object["scenario_hash"] =
    dev::JsonValue::stringValue(context.scenarioHash);
  metadata.object["graphics_profile"] =
    dev::JsonValue::stringValue(context.graphicsProfile);
  metadata.object["render_scale"] =
    dev::JsonValue::numberValue(context.renderScale);
  metadata.object["label"] = dev::JsonValue::stringValue(
    scenario.name + " - " + context.graphicsProfile
  );
  metadata.object["renderer"] = dev::JsonValue::stringValue(context.renderer);
  metadata.object["map_content_hash"] =
    dev::JsonValue::numberValue(context.actualMapContentHash);
  metadata.object["actual_resolution"] = dev::JsonValue::arrayValue({
    dev::JsonValue::numberValue(context.actualWidth),
    dev::JsonValue::numberValue(context.actualHeight),
  });
  metadata.object["selected_present_mode"] =
    dev::JsonValue::stringValue(context.selectedPresentMode);
  metadata.object["clock"] =
    dev::JsonValue::stringValue("std::chrono::steady_clock");
  root.object["metadata"] = std::move(metadata);

  dev::JsonValue gpuTiming = dev::JsonValue::objectValue();
  gpuTiming.object["available"] =
    dev::JsonValue::booleanValue(context.gpuTimingAvailable);
  gpuTiming.object["backend"] =
    dev::JsonValue::stringValue(context.gpuTimingBackend);
  gpuTiming.object["unavailable_reason"] =
    context.gpuTimingUnavailableReason.empty()
      ? dev::JsonValue{}
      : dev::JsonValue::stringValue(context.gpuTimingUnavailableReason);
  gpuTiming.object["timestamp_valid_bits"] =
    context.gpuTimestampValidBits
      ? dev::JsonValue::numberValue(*context.gpuTimestampValidBits)
      : dev::JsonValue{};
  gpuTiming.object["timestamp_period_ns"] =
    context.gpuTimestampPeriodNanoseconds
      ? dev::JsonValue::numberValue(*context.gpuTimestampPeriodNanoseconds)
      : dev::JsonValue{};
  gpuTiming.object["instrumentation_version"] =
    dev::JsonValue::stringValue(context.gpuTimingInstrumentationVersion);
  const std::size_t gpuSampleCount = static_cast<std::size_t>(std::count_if(
    samples.begin(),
    samples.end(),
    [](const FrameSample& sample) {
      return sample.gpuPrimaryCommandBufferMilliseconds.has_value();
    }
  ));
  gpuTiming.object["sample_count"] =
    dev::JsonValue::numberValue(static_cast<double>(gpuSampleCount));
  gpuTiming.object["coverage_percent"] = dev::JsonValue::numberValue(
    samples.empty()
      ? 0.0
      : 100.0 * static_cast<double>(gpuSampleCount) /
          static_cast<double>(samples.size())
  );
  root.object["gpu_timing"] = std::move(gpuTiming);
  root.object["gpu_execution_timing_available"] =
    dev::JsonValue::booleanValue(gpuSampleCount > 0U);
  root.object["frame_count"] =
    dev::JsonValue::numberValue(static_cast<double>(samples.size()));
  root.object["simulation_tick_count"] =
    dev::JsonValue::numberValue(static_cast<double>(tickSamples.size()));

  std::map<std::uint64_t, std::vector<const SimulationTickSample*>> ticksByFrame;
  for (const SimulationTickSample& tick : tickSamples) {
    ticksByFrame[tick.renderFrameIndex].push_back(&tick);
  }

  dev::JsonValue frames = dev::JsonValue::arrayValue();
  frames.array.reserve(samples.size());
  for (const FrameSample& sample : samples) {
    dev::JsonValue frame = dev::JsonValue::objectValue();
    frame.object["frame_index"] =
      dev::JsonValue::numberValue(static_cast<double>(sample.index));
    frame.object["elapsed_time_seconds"] =
      dev::JsonValue::numberValue(sample.elapsedSeconds);
    frame.object["total_cpu_ms"] =
      dev::JsonValue::numberValue(sample.frameMilliseconds);
    frame.object["total_gpu_ms"] =
      sample.gpuPrimaryCommandBufferMilliseconds
        ? dev::JsonValue::numberValue(
            *sample.gpuPrimaryCommandBufferMilliseconds
          )
        : dev::JsonValue{};
    frame.object["gpu_timing_available"] =
      dev::JsonValue::booleanValue(
        sample.gpuPrimaryCommandBufferMilliseconds.has_value()
      );
    frame.object["gpu_timing_result_received"] =
      dev::JsonValue::booleanValue(sample.gpuTimingResultReceived);
    frame.object["gpu_timing_readback_latency_frames"] =
      sample.gpuTimingResultReceived
        ? dev::JsonValue::numberValue(
            sample.gpuTimingReadbackLatencyFrames
          )
        : dev::JsonValue{};
    frame.object["gpu_timing_unavailable_reason"] =
      sample.gpuTimingUnavailableReason.empty()
        ? dev::JsonValue{}
        : dev::JsonValue::stringValue(
            sample.gpuTimingUnavailableReason
          );

    dev::JsonValue cpu = dev::JsonValue::objectValue();
    cpu.object["scene_build"] =
      dev::JsonValue::numberValue(sample.sceneBuildMilliseconds);
    cpu.object["vertex_upload"] =
      dev::JsonValue::numberValue(sample.vertexUploadMilliseconds);
    cpu.object["swapchain_acquisition"] =
      dev::JsonValue::numberValue(sample.swapchainAcquireMilliseconds);
    cpu.object["draw_issue"] =
      dev::JsonValue::numberValue(sample.drawIssueMilliseconds);
    cpu.object["submission"] =
      dev::JsonValue::numberValue(sample.submitMilliseconds);
    cpu.object["renderer_total"] =
      dev::JsonValue::numberValue(sample.renderCpuMilliseconds);
    cpu.object["snapshot_decode"] =
      dev::JsonValue::numberValue(sample.snapshotDecodeMilliseconds);
    cpu.object["snapshot_apply"] =
      dev::JsonValue::numberValue(sample.snapshotApplyMilliseconds);
    cpu.object["network_processing"] =
      dev::JsonValue::numberValue(sample.networkProcessingMilliseconds);
    cpu.object["simulation"] =
      dev::JsonValue::numberValue(sample.simulationMilliseconds);
    cpu.object["movement_collision"] =
      dev::JsonValue::numberValue(sample.movementCollisionMilliseconds);
    cpu.object["traces"] =
      dev::JsonValue::numberValue(sample.tracesMilliseconds);
    cpu.object["interpolation"] =
      dev::JsonValue::numberValue(sample.interpolationMilliseconds);
    cpu.object["animation"] =
      dev::JsonValue::numberValue(sample.animationMilliseconds);
    cpu.object["world_visibility"] =
      dev::JsonValue::numberValue(sample.worldVisibilityMilliseconds);
    cpu.object["render_instance_construction"] = dev::JsonValue::numberValue(
      sample.renderInstanceConstructionMilliseconds
    );
    cpu.object["world_command_encoding"] =
      dev::JsonValue::numberValue(sample.worldCommandEncodingMilliseconds);
    cpu.object["dynamic_command_encoding"] =
      dev::JsonValue::numberValue(sample.dynamicCommandEncodingMilliseconds);
    cpu.object["ui"] = dev::JsonValue::numberValue(sample.uiMilliseconds);
    frame.object["cpu_subsystems_ms"] = std::move(cpu);
    dev::JsonValue lateMouseSample = dev::JsonValue::objectValue();
    lateMouseSample.object["callback_ms"] =
      dev::JsonValue::numberValue(sample.lateMouseSampleMilliseconds);
    lateMouseSample.object["sample_to_submit_ms"] =
      dev::JsonValue::numberValue(sample.mouseSampleToSubmitMilliseconds);
    lateMouseSample.object["phase_gain_ms"] =
      dev::JsonValue::numberValue(sample.mouseSamplePhaseGainMilliseconds);
    lateMouseSample.object["enabled"] =
      dev::JsonValue::booleanValue(sample.lateMouseSampleEnabled);
    lateMouseSample.object["applied"] =
      dev::JsonValue::booleanValue(sample.lateMouseSampleApplied);
    frame.object["late_mouse_sample"] = std::move(lateMouseSample);
    dev::JsonValue gpu = dev::JsonValue::objectValue();
    dev::JsonValue gpuStates = dev::JsonValue::objectValue();
    for (std::size_t passIndex = 0;
         passIndex < kGpuTimedPassCount;
         ++passIndex) {
      const std::string passName = std::string(gpuTimedPassName(
        static_cast<GpuTimedPass>(passIndex)
      ));
      const bool applicable =
        sample.gpuPassTimingApplicable[passIndex];
      const std::optional<double>& milliseconds =
        sample.gpuPassMilliseconds[passIndex];
      gpu.object[passName] = milliseconds
        ? dev::JsonValue::numberValue(*milliseconds)
        : dev::JsonValue{};
      gpuStates.object[passName] = dev::JsonValue::stringValue(
        !applicable
          ? "not_applicable"
          : milliseconds ? "available" : "unavailable"
      );
    }
    frame.object["gpu_subsystems_ms"] = std::move(gpu);
    frame.object["gpu_subsystem_states"] = std::move(gpuStates);

    dev::JsonValue workload = dev::JsonValue::objectValue();
    workload.object["uploaded_vertices"] =
      dev::JsonValue::numberValue(sample.uploadedVertices);
    workload.object["rendered_triangles"] =
      dev::JsonValue::numberValue(sample.renderedTriangles);
    workload.object["world_draws"] =
      dev::JsonValue::numberValue(sample.worldDraws);
    workload.object["world_submitted_triangles"] =
      dev::JsonValue::numberValue(sample.worldSubmittedTriangles);
    workload.object["world_submitted_ranges"] =
      dev::JsonValue::numberValue(sample.worldSubmittedRanges);
    workload.object["world_total_chunks"] =
      dev::JsonValue::numberValue(sample.worldTotalChunks);
    workload.object["world_visible_chunks"] =
      dev::JsonValue::numberValue(sample.worldVisibleChunks);
    workload.object["world_culled_chunks"] =
      dev::JsonValue::numberValue(sample.worldCulledChunks);
    workload.object["world_visibility_tested_nodes"] =
      dev::JsonValue::numberValue(sample.worldVisibilityTestedNodes);
    workload.object["world_visibility_query_ms"] =
      dev::JsonValue::numberValue(sample.worldVisibilityQueryMilliseconds);
    workload.object["world_gpu_indirect"] =
      dev::JsonValue::booleanValue(sample.worldGpuIndirect);
    workload.object["world_gpu_indirect_commands"] =
      dev::JsonValue::numberValue(sample.worldGpuIndirectCommands);
    workload.object["world_gpu_indirect_material_groups"] =
      dev::JsonValue::numberValue(sample.worldGpuIndirectMaterialGroups);
    workload.object["visible_players"] =
      dev::JsonValue::numberValue(sample.visiblePlayers);
    workload.object["projectiles"] =
      dev::JsonValue::numberValue(sample.projectileCount);
    workload.object["effects"] =
      dev::JsonValue::numberValue(sample.effectCount);
    workload.object["lights"] =
      dev::JsonValue::numberValue(sample.lightCount);
    workload.object["particles"] =
      dev::JsonValue::numberValue(sample.particleCount);
    workload.object["transparent_effects"] =
      dev::JsonValue::numberValue(sample.transparentEffectCount);
    workload.object["instance_upload_bytes"] =
      dev::JsonValue::numberValue(sample.instanceUploadBytes);
    workload.object["instance_draws"] =
      dev::JsonValue::numberValue(sample.instanceDraws);
    frame.object["workload_counters"] = std::move(workload);

    dev::JsonValue markers = dev::JsonValue::arrayValue();
    if (const auto found = ticksByFrame.find(sample.index);
        found != ticksByFrame.end()) {
      dev::JsonValue marker = dev::JsonValue::objectValue();
      marker.object["type"] =
        dev::JsonValue::stringValue("simulation_ticks");
      marker.object["count"] =
        dev::JsonValue::numberValue(static_cast<double>(found->second.size()));
      dev::JsonValue indices = dev::JsonValue::arrayValue();
      indices.array.reserve(found->second.size());
      for (const SimulationTickSample* tick : found->second) {
        indices.array.push_back(
          dev::JsonValue::numberValue(static_cast<double>(tick->index))
        );
      }
      marker.object["tick_indices"] = std::move(indices);
      markers.array.push_back(std::move(marker));
    }
    frame.object["event_markers"] = std::move(markers);
    frames.array.push_back(std::move(frame));
  }
  root.object["frames"] = std::move(frames);
  return root;
}

bool writeArtifacts(
  const std::filesystem::path& benchmarkRoot,
  const Scenario& scenario,
  const ResultContext& context,
  const std::vector<FrameSample>& samples,
  const std::vector<SimulationTickSample>& tickSamples,
  std::filesystem::path& resultDirectory,
  std::string& error
) {
  if (!isSafeRunId(context.runGroup) || !isSafeRunId(context.runId) || !isSafeRunId(scenario.name)) { error = "unsafe scenario, run group, or run id"; return false; }
  resultDirectory = benchmarkRoot / scenario.name / context.runGroup / context.runId;
  std::error_code ec; std::filesystem::create_directories(resultDirectory / "screenshots", ec);
  if (ec) { error = "could not create benchmark result directory: " + ec.message(); return false; }
  std::ofstream frames(resultDirectory / "frame-times.csv", std::ios::trunc);
  frames << "frame,elapsed_seconds,frame_ms\n";
  std::ofstream telemetry(resultDirectory / "telemetry.csv", std::ios::trunc);
  telemetry << "frame,elapsed_seconds,frame_ms,scene_build_ms,vertex_upload_ms,"
    "swapchain_acquire_ms,draw_issue_ms,submit_ms,total_render_cpu_ms,"
    "snapshot_decode_ms,snapshot_apply_ms,network_processing_ms,simulation_ms,"
    "movement_collision_ms,traces_ms,interpolation_ms,animation_ms,"
    "world_visibility_ms,render_instance_construction_ms,"
    "world_command_encoding_ms,dynamic_command_encoding_ms,ui_ms,"
    "late_mouse_sample_ms,mouse_sample_to_submit_ms,"
    "mouse_sample_phase_gain_ms,late_mouse_sample_enabled,"
    "late_mouse_sample_applied,"
    "gpu_primary_command_buffer_ms,outline_gpu_ms,outline_gpu_state,"
    "uploaded_vertices,rendered_triangles,world_draws,world_submitted_triangles,"
    "world_submitted_ranges,world_total_chunks,world_visible_chunks,"
    "world_culled_chunks,world_visibility_tested_nodes,world_visibility_query_ms,"
    "world_gpu_indirect,world_gpu_indirect_commands,"
    "world_gpu_indirect_material_groups,"
    "visible_players,projectiles,effects,lights,particles,transparent_effects,"
    "instance_upload_bytes,instance_draws";
  for (std::size_t passIndex = 0;
       passIndex < kGpuTimedPassCount;
       ++passIndex) {
    const std::string name = std::string(gpuTimedPassName(
      static_cast<GpuTimedPass>(passIndex)
    ));
    telemetry << ',' << name << "_gpu_ms," << name << "_gpu_state";
  }
  telemetry << ",gpu_timing_result_received,"
    "gpu_timing_readback_latency_frames,gpu_timing_unavailable_reason\n";
  std::ofstream ticks(resultDirectory / "simulation-ticks.csv", std::ios::trunc);
  ticks << "tick,render_frame,elapsed_seconds,simulation_ms,"
    "network_processing_ms,movement_collision_ms,traces_ms\n";
  frames << std::setprecision(10);
  telemetry << std::setprecision(10);
  ticks << std::setprecision(10);
  for (const FrameSample& s : samples) {
    frames << s.index << ',' << s.elapsedSeconds << ',' << s.frameMilliseconds << '\n';
    telemetry << s.index << ',' << s.elapsedSeconds << ',' << s.frameMilliseconds
      << ',' << s.sceneBuildMilliseconds << ',' << s.vertexUploadMilliseconds
      << ',' << s.swapchainAcquireMilliseconds << ',' << s.drawIssueMilliseconds
      << ',' << s.submitMilliseconds << ',' << s.renderCpuMilliseconds
      << ',' << s.snapshotDecodeMilliseconds << ',' << s.snapshotApplyMilliseconds
      << ',' << s.networkProcessingMilliseconds << ',' << s.simulationMilliseconds
      << ',' << s.movementCollisionMilliseconds << ',' << s.tracesMilliseconds
      << ',' << s.interpolationMilliseconds << ',' << s.animationMilliseconds
      << ',' << s.worldVisibilityMilliseconds
      << ',' << s.renderInstanceConstructionMilliseconds
      << ',' << s.worldCommandEncodingMilliseconds
      << ',' << s.dynamicCommandEncodingMilliseconds << ',' << s.uiMilliseconds
      << ',' << s.lateMouseSampleMilliseconds
      << ',' << s.mouseSampleToSubmitMilliseconds
      << ',' << s.mouseSamplePhaseGainMilliseconds
      << ',' << (s.lateMouseSampleEnabled ? 1 : 0)
      << ',' << (s.lateMouseSampleApplied ? 1 : 0)
      << ',';
    if (s.gpuPrimaryCommandBufferMilliseconds) {
      telemetry << *s.gpuPrimaryCommandBufferMilliseconds;
    }
    telemetry << ',';
    if (s.outlineGpuMilliseconds) telemetry << *s.outlineGpuMilliseconds;
    telemetry << ',' << (
      !s.outlineGpuTimingApplicable
        ? "not_applicable"
        : (s.outlineGpuMilliseconds ? "available" : "unavailable")
    )
      << ',' << s.uploadedVertices << ',' << s.renderedTriangles
      << ',' << s.worldDraws << ',' << s.worldSubmittedTriangles
      << ',' << s.worldSubmittedRanges << ',' << s.worldTotalChunks
      << ',' << s.worldVisibleChunks << ',' << s.worldCulledChunks
      << ',' << s.worldVisibilityTestedNodes
      << ',' << s.worldVisibilityQueryMilliseconds
      << ',' << (s.worldGpuIndirect ? 1 : 0)
      << ',' << s.worldGpuIndirectCommands
      << ',' << s.worldGpuIndirectMaterialGroups
      << ',' << s.visiblePlayers
      << ',' << s.projectileCount << ',' << s.effectCount
      << ',' << s.lightCount << ',' << s.particleCount
      << ',' << s.transparentEffectCount
      << ',' << s.instanceUploadBytes << ',' << s.instanceDraws;
    for (std::size_t passIndex = 0;
         passIndex < kGpuTimedPassCount;
         ++passIndex) {
      telemetry << ',';
      if (s.gpuPassMilliseconds[passIndex]) {
        telemetry << *s.gpuPassMilliseconds[passIndex];
      }
      telemetry << ',' << (
        !s.gpuPassTimingApplicable[passIndex]
          ? "not_applicable"
          : (
              s.gpuPassMilliseconds[passIndex]
                ? "available"
                : "unavailable"
            )
      );
    }
    telemetry << ',' << (s.gpuTimingResultReceived ? 1 : 0) << ',';
    if (s.gpuTimingResultReceived) {
      telemetry << s.gpuTimingReadbackLatencyFrames;
    }
    telemetry << ',' << s.gpuTimingUnavailableReason << '\n';
  }
  for (const SimulationTickSample& s : tickSamples) {
    ticks << s.index << ',' << s.renderFrameIndex << ',' << s.elapsedSeconds
      << ',' << s.simulationMilliseconds << ',' << s.networkProcessingMilliseconds
      << ',' << s.movementCollisionMilliseconds << ',' << s.tracesMilliseconds
      << '\n';
  }
  std::ofstream result(resultDirectory / "result.json", std::ios::trunc);
  result << dev::writeJson(resultJson(scenario, context, samples, tickSamples))
    << '\n';
  std::ofstream timeline(
    resultDirectory / "frame-timeline.json",
    std::ios::trunc
  );
  timeline << dev::writeJson(
    frameTimelineJson(scenario, context, samples, tickSamples)
  ) << '\n';
  if (!frames || !telemetry || !ticks || !result || !timeline) {
    error = "could not write benchmark artifacts";
    return false;
  }
  return true;
}

} // namespace lg::benchmark
