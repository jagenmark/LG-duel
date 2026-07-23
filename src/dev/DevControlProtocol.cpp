#include "dev/DevControlProtocol.hpp"

#include "sim/MapRegistry.hpp"
#include "sim/WeaponCatalog.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>

namespace lg::dev {
namespace {

[[nodiscard]] std::optional<Vec3> parsePosition(const JsonValue& root) {
  const JsonValue* value = root.find("position");
  if (value == nullptr || value->type != JsonValue::Type::Array || value->array.size() != 3U) {
    return std::nullopt;
  }
  Vec3 result;
  float* components[] = {&result.x, &result.y, &result.z};
  for (std::size_t index = 0; index < 3U; ++index) {
    if (value->array[index].type != JsonValue::Type::Number ||
        !std::isfinite(value->array[index].number) ||
        std::fabs(value->array[index].number) > 1000000.0) {
      return std::nullopt;
    }
    *components[index] = static_cast<float>(value->array[index].number);
  }
  return result;
}

[[nodiscard]] bool parseCamera(
  const JsonValue& root,
  CameraTransform& camera,
  std::string& error
) {
  const std::optional<Vec3> position = parsePosition(root);
  const std::optional<double> yaw = numberMember(root, "yaw");
  const std::optional<double> pitch = numberMember(root, "pitch");
  if (!position.has_value()) {
    error = "position must be a three-number array with finite values";
    return false;
  }
  if (!yaw.has_value() || !std::isfinite(*yaw) || std::fabs(*yaw) > 1000000.0) {
    error = "yaw must be a finite number";
    return false;
  }
  if (!pitch.has_value() || !std::isfinite(*pitch) || *pitch < -89.9 || *pitch > 89.9) {
    error = "pitch must be between -89.9 and 89.9 degrees";
    return false;
  }
  camera.position = *position;
  camera.yawDegrees = static_cast<float>(*yaw);
  camera.pitchDegrees = static_cast<float>(*pitch);
  if (const JsonValue* fov = root.find("fov"); fov != nullptr && fov->type != JsonValue::Type::Null) {
    if (fov->type != JsonValue::Type::Number || !std::isfinite(fov->number) ||
        fov->number < 30.0 || fov->number > 140.0) {
      error = "fov must be between 30 and 140 degrees";
      return false;
    }
    camera.fieldOfView = static_cast<float>(fov->number);
  }
  return true;
}

[[nodiscard]] std::optional<ControlOperation> parseOperation(std::string_view name) {
  if (name == "status") return ControlOperation::Status;
  if (name == "load_map") return ControlOperation::LoadMap;
  if (name == "reload_map") return ControlOperation::ReloadMap;
  if (name == "get_camera") return ControlOperation::GetCamera;
  if (name == "set_camera") return ControlOperation::SetCamera;
  if (name == "set_collision_debug") return ControlOperation::SetCollisionDebug;
  if (name == "capture_screenshot") return ControlOperation::CaptureScreenshot;
  if (name == "capture_map_views") return ControlOperation::CaptureMapViews;
  if (name == "exec_console") return ControlOperation::ExecConsole;
  if (name == "get_cvar") return ControlOperation::GetCvar;
  if (name == "set_cvar") return ControlOperation::SetCvar;
  if (name == "send_input") return ControlOperation::SendInput;
  if (name == "wait_frames") return ControlOperation::WaitFrames;
  if (name == "set_player_view") return ControlOperation::SetPlayerView;
  if (name == "set_player_weapon") return ControlOperation::SetPlayerWeapon;
  if (name == "run_benchmark") return ControlOperation::RunBenchmark;
  return std::nullopt;
}

[[nodiscard]] bool isSafeSingleLine(std::string_view value, std::size_t maximumLength) {
  if (value.empty() || value.size() > maximumLength) return false;
  return std::none_of(value.begin(), value.end(), [](unsigned char character) {
    return character == '\n' || character == '\r' || character == '\0' ||
      (character < 0x20U && character != '\t');
  });
}

[[nodiscard]] bool isSafeCvarName(std::string_view name) {
  if (name.empty() || name.size() > 64U) return false;
  return std::all_of(name.begin(), name.end(), [](unsigned char character) {
    return std::isalnum(character) != 0 || character == '_';
  });
}

[[nodiscard]] bool parseBoundedAxis(
  const JsonValue& root,
  std::string_view name,
  float& result
) {
  const JsonValue* value = root.find(name);
  if (value == nullptr) return true;
  if (value->type != JsonValue::Type::Number || !std::isfinite(value->number) ||
      value->number < -1.0 || value->number > 1.0) return false;
  result = static_cast<float>(value->number);
  return true;
}

[[nodiscard]] bool parseOptionalBool(
  const JsonValue& root,
  std::string_view name,
  bool& result
) {
  const JsonValue* value = root.find(name);
  if (value == nullptr) return true;
  if (value->type != JsonValue::Type::Boolean) return false;
  result = value->boolean;
  return true;
}

[[nodiscard]] bool parsePlayerAngles(
  const JsonValue& root,
  float& yaw,
  float& pitch,
  std::string& error
) {
  const std::optional<double> parsedYaw = numberMember(root, "yaw");
  const std::optional<double> parsedPitch = numberMember(root, "pitch");
  if (!parsedYaw.has_value() || !std::isfinite(*parsedYaw) ||
      std::fabs(*parsedYaw) > 1000000.0) {
    error = "yaw must be a finite number";
    return false;
  }
  if (!parsedPitch.has_value() || !std::isfinite(*parsedPitch) ||
      *parsedPitch < -89.9 || *parsedPitch > 89.9) {
    error = "pitch must be between -89.9 and 89.9 degrees";
    return false;
  }
  yaw = static_cast<float>(*parsedYaw);
  pitch = static_cast<float>(*parsedPitch);
  return true;
}

} // namespace

ControlRequestParseResult parseControlRequest(const JsonValue& root) {
  if (root.type != JsonValue::Type::Object) {
    return {{}, false, "request must be a JSON object"};
  }
  ControlRequest request;
  request.id = stringMember(root, "id").value_or("");
  if (const JsonValue* protocol = root.find("control_protocol"); protocol != nullptr) {
    if (protocol->type != JsonValue::Type::Number || protocol->number != 1.0) {
      return {{}, false, "unsupported control_protocol; expected 1"};
    }
  }
  const std::optional<std::string> operationName = stringMember(root, "operation");
  if (!operationName.has_value()) {
    return {{}, false, "operation must be a string"};
  }
  const std::optional<ControlOperation> operation = parseOperation(*operationName);
  if (!operation.has_value()) {
    return {{}, false, "unknown operation '" + *operationName + "'"};
  }
  request.operation = *operation;
  request.mapName = stringMember(root, "map").value_or("");
  request.presetName = stringMember(root, "preset").value_or("");
  request.captureName = stringMember(root, "name").value_or("");
  request.hideHud = boolMember(root, "hide_hud").value_or(true);
  request.hideOverlays = boolMember(root, "hide_overlays").value_or(true);

  if (request.operation == ControlOperation::ExecConsole) {
    request.consoleCommand = stringMember(root, "command").value_or("");
    if (!isSafeSingleLine(request.consoleCommand, 1024U)) {
      return {{}, false, "command must be one non-empty line of at most 1024 bytes"};
    }
  }
  if (request.operation == ControlOperation::GetCvar ||
      request.operation == ControlOperation::SetCvar) {
    request.cvarName = stringMember(root, "name").value_or("");
    if (!isSafeCvarName(request.cvarName)) {
      return {{}, false, "cvar name may only use letters, numbers, and _"};
    }
    if (request.operation == ControlOperation::SetCvar) {
      request.cvarValue = stringMember(root, "value").value_or("");
      if (!isSafeSingleLine(request.cvarValue, 256U) ||
          request.cvarValue.find('"') != std::string::npos) {
        return {{}, false, "cvar value must be one non-empty line without quotes and at most 256 bytes"};
      }
    }
  }
  if (request.operation == ControlOperation::WaitFrames) {
    const JsonValue* frames = root.find("frames");
    if (frames == nullptr || frames->type != JsonValue::Type::Number ||
        !std::isfinite(frames->number) || std::floor(frames->number) != frames->number ||
        frames->number < 1.0 || frames->number > 600.0) {
      return {{}, false, "frames must be an integer between 1 and 600"};
    }
    request.waitFrames = static_cast<std::uint32_t>(frames->number);
  }
  if (request.operation == ControlOperation::SetPlayerView) {
    std::string error;
    if (!parsePlayerAngles(
          root, request.playerYawDegrees, request.playerPitchDegrees, error)) {
      return {{}, false, std::move(error)};
    }
  }
  if (request.operation == ControlOperation::SetPlayerWeapon) {
    request.playerWeapon = stringMember(root, "weapon").value_or("");
    if (!parseWeaponToken(request.playerWeapon).has_value()) {
      return {{}, false, "weapon must name a valid LG Duel weapon"};
    }
  }
  if (request.operation == ControlOperation::SendInput) {
    PlayerInput& input = request.playerInput;
    if (!parseBoundedAxis(root, "forward", input.forward) ||
        !parseBoundedAxis(root, "right", input.right) ||
        !parseBoundedAxis(root, "up", input.up)) {
      return {{}, false, "forward, right, and up must be finite numbers between -1 and 1"};
    }
    if (!parseOptionalBool(root, "attack", input.attack) ||
        !parseOptionalBool(root, "jump", input.jump) ||
        !parseOptionalBool(root, "dash", input.dash) ||
        !parseOptionalBool(root, "crouch", input.crouch) ||
        !parseOptionalBool(root, "sneak", input.sneak) ||
        !parseOptionalBool(root, "zoom", input.zoom)) {
      return {{}, false, "attack, jump, dash, crouch, sneak, and zoom must be booleans"};
    }
    const JsonValue* ticks = root.find("ticks");
    if (ticks == nullptr || ticks->type != JsonValue::Type::Number ||
        !std::isfinite(ticks->number) || std::floor(ticks->number) != ticks->number ||
        ticks->number < 1.0 || ticks->number > 1250.0) {
      return {{}, false, "ticks must be an integer between 1 and 1250"};
    }
    input.ticks = static_cast<std::uint32_t>(ticks->number);
    const JsonValue* yaw = root.find("yaw");
    const JsonValue* pitch = root.find("pitch");
    if ((yaw == nullptr) != (pitch == nullptr)) {
      return {{}, false, "yaw and pitch must be supplied together"};
    }
    if (yaw != nullptr) {
      float parsedYaw = 0.0F;
      float parsedPitch = 0.0F;
      std::string error;
      if (!parsePlayerAngles(root, parsedYaw, parsedPitch, error)) {
        return {{}, false, std::move(error)};
      }
      input.yawDegrees = parsedYaw;
      input.pitchDegrees = parsedPitch;
    }
    const JsonValue* weapon = root.find("weapon");
    if (weapon != nullptr && weapon->type != JsonValue::Type::String) {
      return {{}, false, "weapon must be a string"};
    }
    input.weapon = weapon == nullptr ? std::string{} : weapon->string;
    if (!input.weapon.empty() && !parseWeaponToken(input.weapon).has_value()) {
      return {{}, false, "weapon must name a valid LG Duel weapon"};
    }
  }

  if (request.operation == ControlOperation::RunBenchmark) {
    request.scenarioHash = stringMember(root, "scenario_hash").value_or("");
    request.runGroup = stringMember(root, "run_group").value_or("");
    request.runId = stringMember(root, "run_id").value_or("");
    if (!benchmark::isSafeScenarioHash(request.scenarioHash)) {
      return {{}, false, "scenario_hash must contain 8 to 128 hexadecimal characters"};
    }
    if (!benchmark::isSafeRunId(request.runGroup) || !benchmark::isSafeRunId(request.runId)) {
      return {{}, false, "run_group and run_id may only use letters, numbers, _ and -"};
    }
    const JsonValue* scenario = root.find("scenario");
    if (scenario == nullptr) return {{}, false, "scenario is required"};
    benchmark::ParseResult parsed = benchmark::parseScenario(*scenario);
    if (!parsed.ok) return {{}, false, "scenario: " + parsed.error};
    request.benchmarkScenario = std::move(parsed.scenario);
  }

  if ((request.operation == ControlOperation::LoadMap ||
       (request.operation == ControlOperation::CaptureMapViews && !request.mapName.empty())) &&
      !isValidMapName(request.mapName)) {
    return {{}, false, "map must be a safe map stem or .map filename"};
  }
  if (request.operation == ControlOperation::SetCamera) {
    std::string error;
    if (!parseCamera(root, request.camera, error)) return {{}, false, std::move(error)};
  }
  if (request.operation == ControlOperation::SetCollisionDebug) {
    const JsonValue* mode = root.find("mode");
    if (mode == nullptr || mode->type != JsonValue::Type::Number ||
        !std::isfinite(mode->number) || std::floor(mode->number) != mode->number ||
        mode->number < 0.0 || mode->number > 5.0) {
      return {{}, false, "mode must be an integer between 0 and 5"};
    }
    request.collisionDebugMode = static_cast<int>(mode->number);
  }
  if (request.operation == ControlOperation::CaptureScreenshot &&
      !request.captureName.empty() && !isSafeCaptureName(request.captureName)) {
    return {{}, false, "capture name may only use letters, numbers, _ and -"};
  }
  if (request.operation == ControlOperation::CaptureMapViews) {
    const JsonValue* views = root.find("views");
    if (views == nullptr || views->type != JsonValue::Type::Array || views->array.empty()) {
      return {{}, false, "views must be a non-empty array"};
    }
    if (views->array.size() > 32U) {
      return {{}, false, "views is limited to 32 entries"};
    }
    for (const JsonValue& value : views->array) {
      if (value.type != JsonValue::Type::Object) {
        return {{}, false, "each view must be an object"};
      }
      CameraViewpoint view;
      view.name = stringMember(value, "name").value_or("");
      view.label = stringMember(value, "label").value_or("");
      view.hideHud = boolMember(value, "hide_hud").value_or(true);
      view.hideOverlays = boolMember(value, "hide_overlays").value_or(true);
      if (!isSafeCaptureName(view.name)) {
        return {{}, false, "each view name must use only letters, numbers, _ and -"};
      }
      std::string error;
      if (!parseCamera(value, view.camera, error)) {
        return {{}, false, "view '" + view.name + "': " + error};
      }
      request.viewpoints.push_back(std::move(view));
    }
  }
  return {std::move(request), true, {}};
}

bool isSafeCaptureName(std::string_view name) {
  if (name.empty() || name.size() > 64U) return false;
  return std::all_of(name.begin(), name.end(), [](unsigned char character) {
    return std::isalnum(character) != 0 || character == '_' || character == '-';
  });
}

std::string sanitizeGeneratedCaptureName(std::string_view name) {
  std::string result;
  result.reserve(std::min<std::size_t>(name.size(), 64U));
  for (const unsigned char character : name) {
    if (result.size() == 64U) break;
    result.push_back(
      std::isalnum(character) != 0 || character == '_' || character == '-'
        ? static_cast<char>(character)
        : '-'
    );
  }
  while (!result.empty() && result.back() == '-') result.pop_back();
  return result.empty() ? "capture" : result;
}

JsonValue successResponse(std::string id, JsonValue result) {
  JsonValue response = JsonValue::objectValue();
  response.object["id"] = JsonValue::stringValue(std::move(id));
  response.object["ok"] = JsonValue::booleanValue(true);
  response.object["result"] = std::move(result);
  return response;
}

JsonValue errorResponse(std::string id, std::string code, std::string message) {
  JsonValue error = JsonValue::objectValue();
  error.object["code"] = JsonValue::stringValue(std::move(code));
  error.object["message"] = JsonValue::stringValue(std::move(message));
  JsonValue response = JsonValue::objectValue();
  response.object["id"] = JsonValue::stringValue(std::move(id));
  response.object["ok"] = JsonValue::booleanValue(false);
  response.object["error"] = std::move(error);
  return response;
}

JsonValue cameraJson(const CameraTransform& camera) {
  JsonValue position = JsonValue::arrayValue({
    JsonValue::numberValue(camera.position.x),
    JsonValue::numberValue(camera.position.y),
    JsonValue::numberValue(camera.position.z),
  });
  JsonValue result = JsonValue::objectValue();
  result.object["position"] = std::move(position);
  result.object["yaw"] = JsonValue::numberValue(camera.yawDegrees);
  result.object["pitch"] = JsonValue::numberValue(camera.pitchDegrees);
  result.object["fov"] = camera.fieldOfView.has_value()
    ? JsonValue::numberValue(*camera.fieldOfView)
    : JsonValue{};
  return result;
}

} // namespace lg::dev
