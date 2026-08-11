#include "dev/DevControlProtocol.hpp"
#include "dev/DevControlServer.hpp"
#include "dev/DevJson.hpp"
#include "dev/PngWriter.hpp"

#include <array>
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

lg::dev::ControlRequestParseResult parseRequest(std::string_view text) {
  const lg::dev::JsonParseResult json = lg::dev::parseJson(text);
  if (!json.ok) return {{}, false, json.error};
  return lg::dev::parseControlRequest(json.value);
}

} // namespace

int main() {
  int failures = 0;

  const lg::dev::JsonParseResult json = lg::dev::parseJson(
    R"({"operation":"status","control_protocol":1,"escaped":"a\nb"})"
  );
  failures += expect(json.ok, "valid JSON should parse");
  failures += expect(
    lg::dev::stringMember(json.value, "escaped") == std::optional<std::string>("a\nb"),
    "JSON string escapes should decode"
  );
  failures += expect(
    lg::dev::parseJson("{\"operation\":]").ok == false,
    "malformed JSON should be rejected"
  );

  failures += expect(
    !parseRequest(R"({"operation":"load_map","map":"../eyetoeye"})").ok,
    "map traversal should be rejected"
  );
  failures += expect(
    !parseRequest(R"({"operation":"load_map","map":"C:\\temp\\evil.map"})").ok,
    "absolute or separated map paths should be rejected"
  );
  failures += expect(
    parseRequest(R"({"operation":"load_map","map":"eyetoeye"})").ok,
    "safe map stems should be accepted"
  );
  failures += expect(
    !parseRequest(R"({"operation":"set_camera","position":[1,2,3],"yaw":0,"pitch":90})").ok,
    "camera pitch outside the renderable range should be rejected"
  );
  failures += expect(
    parseRequest(R"({"operation":"set_camera","position":[1,2,3],"yaw":140,"pitch":-20,"fov":100})").ok,
    "valid deterministic camera parameters should be accepted"
  );
  failures += expect(
    !parseRequest(R"({"operation":"set_collision_debug"})").ok,
    "collision debug mode should be required"
  );
  failures += expect(
    !parseRequest(R"({"operation":"set_collision_debug","mode":"3"})").ok &&
      !parseRequest(R"({"operation":"set_collision_debug","mode":2.5})").ok &&
      !parseRequest(R"({"operation":"set_collision_debug","mode":6})").ok,
    "collision debug mode should reject wrong types, fractions, and values outside 0 through 5"
  );
  const lg::dev::ControlRequestParseResult collisionDebug =
    parseRequest(R"({"operation":"set_collision_debug","mode":3})");
  failures += expect(
    collisionDebug.ok &&
      collisionDebug.request.operation == lg::dev::ControlOperation::SetCollisionDebug &&
      collisionDebug.request.collisionDebugMode == 3,
    "typed collision debug requests should retain their bounded mode"
  );
  failures += expect(
    !parseRequest(R"({"operation":"capture_screenshot","name":"../../bad"})").ok,
    "unsafe capture filenames should be rejected"
  );
  failures += expect(
    parseRequest(R"({"operation":"capture_screenshot","name":"central-overview"})").ok,
    "safe capture filenames should be accepted"
  );
  const auto armedPhaseCapture = parseRequest(
    R"({"operation":"arm_phase_capture","name":"rocket-muzzle","phase":"local_rocket_launcher_muzzle","hide_hud":true,"hide_overlays":true})"
  );
  failures += expect(
    armedPhaseCapture.ok &&
      armedPhaseCapture.request.operation ==
        lg::dev::ControlOperation::ArmPhaseCapture &&
      armedPhaseCapture.request.captureName == "rocket-muzzle" &&
      armedPhaseCapture.request.capturePhase ==
        "local_rocket_launcher_muzzle",
    "phase capture arms only a named bounded renderer phase"
  );
  failures += expect(
    parseRequest(
      R"({"operation":"arm_phase_capture","name":"rocket-impact","phase":"local_rocket_launcher_impact"})"
    ).ok,
    "phase capture should accept the exact local Rocket impact frame"
  );
  failures += expect(
    parseRequest(
      R"({"operation":"arm_phase_capture","name":"rocket-flight","phase":"local_rocket_launcher_projectile"})"
    ).ok,
    "phase capture should accept an exact local Rocket flight frame"
  );
  failures += expect(
    parseRequest(
      R"({"operation":"arm_phase_capture","name":"freeze-contact","phase":"local_surface_impact"})"
    ).ok,
    "phase capture should accept an exact local surface-impact frame"
  );
  failures += expect(
    parseRequest(
      R"({"operation":"arm_phase_capture","name":"local-out","phase":"local_weapon_switch_outgoing"})"
    ).ok &&
      parseRequest(
        R"({"operation":"arm_phase_capture","name":"local-apex","phase":"local_weapon_switch_apex"})"
      ).ok &&
      parseRequest(
        R"({"operation":"arm_phase_capture","name":"local-in","phase":"local_weapon_switch_incoming"})"
      ).ok &&
      parseRequest(
        R"({"operation":"arm_phase_capture","name":"remote-out","phase":"remote_weapon_switch_outgoing"})"
      ).ok &&
      parseRequest(
        R"({"operation":"arm_phase_capture","name":"remote-apex","phase":"remote_weapon_switch_apex"})"
      ).ok &&
      parseRequest(
        R"({"operation":"arm_phase_capture","name":"remote-in","phase":"remote_weapon_switch_incoming"})"
      ).ok,
    "phase capture should accept bounded local and remote switch frames"
  );
  failures += expect(
    !parseRequest(
      R"({"operation":"arm_phase_capture","name":"rocket-muzzle","phase":"idle"})"
    ).ok &&
      !parseRequest(
        R"({"operation":"arm_phase_capture","name":"../bad","phase":"local_rocket_launcher_muzzle"})"
      ).ok &&
      !parseRequest(
        R"({"operation":"arm_phase_capture","name":"rocket-muzzle","phase":"local_rocket_launcher_muzzle","extra":true})"
      ).ok &&
      !parseRequest(
        R"({"operation":"arm_phase_capture","name":"rocket-muzzle","phase":"local_rocket_launcher_muzzle","hide_hud":"false"})"
      ).ok &&
      !parseRequest(
        R"({"operation":"arm_phase_capture","name":"rocket-muzzle","phase":"local_rocket_launcher_muzzle","hide_overlays":1})"
      ).ok,
    "phase capture should reject unknown phases, unsafe names, extra fields, and non-boolean hide flags"
  );
  const auto collectedPhaseCapture = parseRequest(
    R"({"operation":"collect_phase_capture","name":"rocket-muzzle"})"
  );
  failures += expect(
    collectedPhaseCapture.ok &&
      collectedPhaseCapture.request.operation ==
        lg::dev::ControlOperation::CollectPhaseCapture &&
      !parseRequest(
        R"({"operation":"collect_phase_capture","name":"rocket-muzzle","phase":"local_rocket_launcher_muzzle"})"
      ).ok,
    "phase capture collection should bind to one safe armed name"
  );
  failures += expect(
    parseRequest(R"({"operation":"exec_console","command":"r_show_fps"})").ok &&
      !parseRequest("{\"operation\":\"exec_console\",\"command\":\"one\\ntwo\"}").ok,
    "console commands should be bounded to one line"
  );
  failures += expect(
    parseRequest(R"({"operation":"get_cvar","name":"cl_fov"})").ok &&
      parseRequest(R"({"operation":"set_cvar","name":"cl_fov","value":"110"})").ok &&
      !parseRequest(R"({"operation":"set_cvar","name":"../bad","value":"1"})").ok,
    "cvar requests should keep names and values bounded"
  );
  const lg::dev::ControlRequestParseResult playerInput = parseRequest(
    R"({"operation":"send_input","ticks":20,"forward":1,"right":-0.5,"attack":true,"yaw":90,"pitch":-10,"weapon":"sniper","one_tick_edges":["attack"]})"
  );
  failures += expect(
    playerInput.ok && playerInput.request.playerInput.ticks == 20U &&
      playerInput.request.playerInput.attack &&
      playerInput.request.playerInput.attackOneTick &&
      playerInput.request.playerInput.yawDegrees == std::optional<float>(90.0F),
    "typed player input should retain bounded controls"
  );
  const auto edgeOnly = parseRequest(
    R"({"operation":"send_input","ticks":4,"one_tick_edges":["jump"]})"
  );
  failures += expect(
    edgeOnly.ok &&
      edgeOnly.request.playerInput.jump &&
      edgeOnly.request.playerInput.jumpOneTick,
    "a named one-tick edge should activate its first command"
  );
  failures += expect(
      !parseRequest(R"({"operation":"send_input","ticks":0})").ok &&
      !parseRequest(R"({"operation":"send_input","ticks":1,"forward":2})").ok &&
      !parseRequest(R"({"operation":"send_input","ticks":1,"yaw":10})").ok &&
      !parseRequest(R"({"operation":"send_input","ticks":1,"attack":"true"})").ok &&
      !parseRequest(R"({"operation":"send_input","ticks":1,"weapon":7})").ok &&
      !parseRequest(R"({"operation":"send_input","ticks":1,"one_tick_edges":["bad"]})").ok &&
      !parseRequest(R"({"operation":"send_input","ticks":1,"extra":1})").ok,
    "player input should reject bad spans, axes, types, and partial view angles"
  );
  failures += expect(
    parseRequest(R"({"operation":"wait_frames","frames":5})").ok &&
      !parseRequest(R"({"operation":"wait_frames","frames":601})").ok,
    "frame waits should have a fixed bound"
  );
  const lg::dev::ControlRequestParseResult networkSimulation = parseRequest(
    R"({"operation":"set_network_simulation","latency_ms":120,"jitter_ms":15,"packet_loss_percent":3,"reorder_percent":4,"seed":4294967295})"
  );
  failures += expect(
    networkSimulation.ok &&
      networkSimulation.request.operation ==
        lg::dev::ControlOperation::SetNetworkSimulation &&
      networkSimulation.request.networkSimulation.latencyMs == 120 &&
      networkSimulation.request.networkSimulation.jitterMs == 15 &&
      networkSimulation.request.networkSimulation.lossPercent == 3 &&
      networkSimulation.request.networkSimulation.reorderPercent == 4 &&
      networkSimulation.request.networkSimulation.seed == 4294967295U,
    "typed network simulation should retain its full bounded config"
  );
  failures += expect(
    !parseRequest(
      R"({"operation":"set_network_simulation","latency_ms":5001,"jitter_ms":0,"packet_loss_percent":0,"reorder_percent":0,"seed":1})"
    ).ok &&
      !parseRequest(
        R"({"operation":"set_network_simulation","latency_ms":0,"jitter_ms":0,"packet_loss_percent":101,"reorder_percent":0,"seed":1})"
      ).ok &&
      !parseRequest(
        R"({"operation":"set_network_simulation","latency_ms":0,"jitter_ms":0,"packet_loss_percent":0,"reorder_percent":0})"
      ).ok &&
      !parseRequest(
        R"({"operation":"set_network_simulation","latency_ms":0.5,"jitter_ms":0,"packet_loss_percent":0,"reorder_percent":0,"seed":1})"
      ).ok &&
      !parseRequest(
        R"({"operation":"set_network_simulation","latency_ms":0,"jitter_ms":0,"packet_loss_percent":0,"reorder_percent":0,"seed":4294967296})"
      ).ok &&
      !parseRequest(
        R"({"operation":"set_network_simulation","latency_ms":0,"jitter_ms":0,"packet_loss_percent":0,"reorder_percent":0,"seed":1,"extra":true})"
      ).ok,
    "network simulation should reject missing, fractional, out-of-range, and unknown fields"
  );
  const lg::dev::ControlRequestParseResult clientTickWait =
    parseRequest(R"({"operation":"wait_client_tick","min_tick":123})");
  const lg::dev::ControlRequestParseResult snapshotTickWait =
    parseRequest(R"({"operation":"wait_snapshot_tick","min_tick":456})");
  const lg::dev::ControlRequestParseResult commandAckWait =
    parseRequest(R"({"operation":"wait_command_ack","sequence":789})");
  failures += expect(
    clientTickWait.ok &&
      clientTickWait.request.operation == lg::dev::ControlOperation::WaitClientTick &&
      clientTickWait.request.minimumTick == 123U &&
      snapshotTickWait.ok &&
      snapshotTickWait.request.operation == lg::dev::ControlOperation::WaitSnapshotTick &&
      snapshotTickWait.request.minimumTick == 456U &&
      commandAckWait.ok &&
      commandAckWait.request.operation == lg::dev::ControlOperation::WaitCommandAck &&
      commandAckWait.request.commandSequence == 789U,
    "typed tick and command waits should retain their thresholds"
  );
  failures += expect(
    !parseRequest(R"({"operation":"wait_client_tick"})").ok &&
      !parseRequest(R"({"operation":"wait_snapshot_tick","min_tick":-1})").ok &&
      !parseRequest(R"({"operation":"wait_snapshot_tick","min_tick":1.5})").ok &&
      !parseRequest(R"({"operation":"wait_command_ack","sequence":"1"})").ok &&
      !parseRequest(R"({"operation":"wait_command_ack","sequence":1,"extra":0})").ok,
    "typed waits should reject missing, wrong-type, fractional, and unknown fields"
  );
  failures += expect(
    parseRequest(R"({"operation":"get_client_state"})").ok &&
      !parseRequest(R"({"operation":"get_client_state","extra":true})").ok,
    "client state requests should reject operation parameters"
  );
  failures += expect(
    parseRequest(R"({"operation":"set_player_view","yaw":180,"pitch":20})").ok &&
      parseRequest(R"({"operation":"set_player_weapon","weapon":"sr"})").ok &&
      !parseRequest(R"({"operation":"set_player_weapon","weapon":"not-a-weapon"})").ok,
    "player view and weapon requests should validate their values"
  );
  failures += expect(
    !parseRequest(R"({"control_protocol":99,"operation":"status"})").ok,
    "incompatible control protocol versions should be rejected"
  );
  failures += expect(
    !parseRequest(R"({"operation":"run_benchmark","run_group":"group","run_id":"../bad","scenario_hash":"0123456789abcdef","scenario":{}})").ok,
    "unsafe benchmark run paths should be rejected"
  );
  failures += expect(
    parseRequest(R"({"operation":"run_benchmark","run_group":"group-01","run_id":"run-01","scenario_hash":"0123456789abcdef","scenario":{"schema_version":1,"expected_benchmark_version":1,"name":"static","map":"eyetoeye","resolution":[1280,720],"warmup_frames":1,"measured_frames":1,"camera_start":{"position":[0,0,2],"yaw":0,"pitch":0}}})").ok,
    "typed benchmark control requests should parse"
  );

  lg::dev::DevControlServer disabledServer;
  failures += expect(
    !disabledServer.running() && disabledServer.port() == 0,
    "developer control should be disabled until explicitly started"
  );

  const std::filesystem::path pngPath =
    std::filesystem::temp_directory_path() / "lg-duel-dev-control-test.png";
  const std::array<std::uint8_t, 16> pixels = {
    255, 0, 0, 255, 0, 255, 0, 255,
    0, 0, 255, 255, 255, 255, 255, 255,
  };
  std::string pngError;
  failures += expect(
    lg::dev::writeRgbaPng(pngPath.string(), 2, 2, pixels, pngError),
    "PNG writer should create a small RGBA image"
  );
  std::ifstream pngFile(pngPath, std::ios::binary);
  const std::array<unsigned char, 8> expectedSignature = {137, 80, 78, 71, 13, 10, 26, 10};
  std::array<unsigned char, 8> signature = {};
  pngFile.read(reinterpret_cast<char*>(signature.data()), signature.size());
  failures += expect(signature == expectedSignature, "PNG output should have the PNG signature");
  std::error_code removeError;
  std::filesystem::remove(pngPath, removeError);

  lg::dev::JsonValue result = lg::dev::JsonValue::objectValue();
  result.object["map"] = lg::dev::JsonValue::stringValue("eyetoeye");
  const std::string response = lg::dev::writeJson(
    lg::dev::successResponse("request-1", std::move(result))
  );
  failures += expect(
    response.find("\"ok\":true") != std::string::npos &&
      response.find("\"request-1\"") != std::string::npos,
    "structured success responses should retain the request id and state"
  );

  if (failures == 0) std::cout << "Developer control tests passed\n";
  return failures == 0 ? 0 : 1;
}
