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
