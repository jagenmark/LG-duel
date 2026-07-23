#include "dev/DevJson.hpp"
#include "net/LoopbackTransport.hpp"
#include "scenario/LiveScenarioSession.hpp"
#include "server/ServerApp.hpp"
#include "server/ServerGame.hpp"
#include "shared/Constants.hpp"
#include "sim/Arena.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) {
    return 0;
  }
  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

std::filesystem::path makeRunDirectory(std::string_view name) {
  const auto stamp = std::chrono::steady_clock::now()
    .time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
    ("lg-duel-live-" + std::string(name) + "-" + std::to_string(stamp));
}

lg::scenario::ScenarioDefinition liveScenario() {
  lg::scenario::ScenarioDefinition scenario;
  scenario.name = "live_session_test";
  scenario.execution.mode =
    lg::scenario::ScenarioExecutionMode::ClientServer;
  scenario.execution.maxTicks = 1;
  scenario.execution.repeat = 1;
  scenario.world.map = "default";
  scenario.world.gameMode = lg::GameMode::Duel;
  scenario.world.seed = 12;

  lg::scenario::PlayerInitialState player;
  player.index = 0;
  player.connected = true;
  player.bot = false;
  player.position = {-4.0F, 0.0F, 0.9F};
  player.health = 100;
  player.alive = true;
  player.selectedWeapon = lg::Weapon::MachineGun;
  scenario.players.push_back(player);
  return scenario;
}

lg::dev::JsonParseResult readJson(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  const std::string text{
    std::istreambuf_iterator<char>(input),
    std::istreambuf_iterator<char>()
  };
  return lg::dev::parseJson(text);
}

void writeStartRequest(
  const std::filesystem::path& runDirectory,
  std::string_view token
) {
  std::filesystem::create_directories(runDirectory);
  std::ofstream output(runDirectory / "start.request.json");
  output << "{\"schema_version\":1,\"token\":\"" << token << "\"}\n";
}

int testCommandLine() {
  int failures = 0;
  {
    const char* argv[] = {"server"};
    const auto parsed = lg::parseServerCommandLine(1, argv);
    failures += expect(
      parsed.ok && parsed.options.port == 27960U &&
        !parsed.options.liveScenario.has_value(),
      "the normal no-option command line should keep its defaults"
    );
  }
  {
    const char* argv[] = {
      "server",
      "28000",
      "--live-scenario",
      "case.json",
      "--scenario-run-dir",
      "run",
      "--scenario-token",
      "run_7",
    };
    const auto parsed = lg::parseServerCommandLine(8, argv);
    failures += expect(
      parsed.ok && parsed.options.port == 28000U &&
        parsed.options.liveScenario.has_value() &&
        parsed.options.liveScenario->token == "run_7",
      "the full live option set should parse"
    );
  }
  {
    const char* argv[] = {
      "server",
      "28000",
      "--live-scenario",
      "case.json",
    };
    const auto parsed = lg::parseServerCommandLine(4, argv);
    failures += expect(
      !parsed.ok && parsed.error.find("supplied together") != std::string::npos,
      "a partial live option set should fail"
    );
  }
  {
    const char* argv[] = {
      "server",
      "28000",
      "--live-scenario",
      "case.json",
      "--scenario-run-dir",
      "run",
      "--scenario-token",
      "../bad",
    };
    const auto parsed = lg::parseServerCommandLine(8, argv);
    failures += expect(
      !parsed.ok && parsed.error.find("scenario-token") != std::string::npos,
      "an unsafe session token should fail"
    );
  }
  {
    const char* argv[] = {"server", "28000", "--unknown", "x"};
    const auto parsed = lg::parseServerCommandLine(4, argv);
    failures += expect(
      !parsed.ok && parsed.error.find("Unknown") != std::string::npos,
      "unknown server options should fail"
    );
  }
  return failures;
}

int testFinishRequestValidation() {
  int failures = 0;
  lg::scenario::LiveScenarioFinishRequest request;
  std::string error;
  failures += expect(
    !lg::scenario::parseLiveScenarioFinishRequest(
      R"({"schema_version":1,"token":"stale",)"
      R"("minimum_command_sequence":4})",
      "current",
      request,
      error
    ) && error.find("does not match") != std::string::npos,
    "a stale finish token should fail"
  );
  failures += expect(
    !lg::scenario::parseLiveScenarioFinishRequest(
      R"({"schema_version":1,"token":"current",)"
      R"("minimum_command_sequence":4,"extra":true})",
      "current",
      request,
      error
    ) && error.find("unknown field") != std::string::npos,
    "an unknown finish field should fail"
  );
  failures += expect(
    lg::scenario::parseLiveScenarioFinishRequest(
      R"({"schema_version":1,"token":"current",)"
      R"("minimum_command_sequence":4})",
      "current",
      request,
      error
    ) && request.minimumCommandSequence == 4U,
    "a strict matching finish request should parse"
  );
  return failures;
}

int testTypedSetupRejection() {
  int failures = 0;
  auto scenario = liveScenario();
  scenario.players[0].team = static_cast<lg::Team>(255);
  const std::filesystem::path runDirectory =
    makeRunDirectory("setup-reject");
  lg::scenario::LiveScenarioSession session(
    {{"unused.json"}, runDirectory, "setup_reject"},
    std::move(scenario)
  );
  lg::LoopbackTransport transport;
  lg::ServerGame game(transport);
  game.setArena(lg::makeDefaultServerArena());
  std::array<bool, lg::kDuelPlayerCount> roster = {};
  std::array<std::uint32_t, lg::kDuelPlayerCount> sessions = {};
  roster[0] = true;
  sessions[0] = 90;
  const auto update = session.beforeServerTick(game, roster, sessions);
  failures += expect(
    update == lg::scenario::LiveScenarioUpdate::Failed &&
      session.error().find("scenario setup failed") != std::string::npos,
    "the live session should keep ServerGame typed setup validation"
  );
  std::error_code cleanupError;
  std::filesystem::remove_all(runDirectory, cleanupError);
  return failures;
}

int testReadyCheckpointAndResult() {
  int failures = 0;
  const std::filesystem::path runDirectory =
    makeRunDirectory("artifacts");
  lg::scenario::LiveScenarioSession session(
    {{"unused.json"}, runDirectory, "artifact_run"},
    liveScenario()
  );
  lg::LoopbackTransport transport;
  lg::ServerGame game(transport);
  game.setArena(lg::makeDefaultServerArena());

  std::array<bool, lg::kDuelPlayerCount> roster = {};
  std::array<std::uint32_t, lg::kDuelPlayerCount> sessions = {};
  roster[0] = true;
  sessions[0] = 17;
  failures += expect(
    session.beforeServerTick(game, roster, sessions) ==
      lg::scenario::LiveScenarioUpdate::WaitingForStart,
    "a matching one-human roster should wait for the start token"
  );
  failures += expect(
    std::filesystem::exists(runDirectory / "ready.json") &&
      !std::filesystem::exists(runDirectory / "checkpoint-0.json"),
    "setup should publish ready before checkpoint zero"
  );
  writeStartRequest(runDirectory, "artifact_run");
  failures += expect(
    session.beforeServerTick(game, roster, sessions) ==
      lg::scenario::LiveScenarioUpdate::Running &&
      std::filesystem::exists(runDirectory / "checkpoint-0.json"),
    "the matching start token should reset setup and publish checkpoint zero"
  );

  game.tick(lg::kFixedTickSeconds);
  failures += expect(
    session.afterServerTick(game) ==
      lg::scenario::LiveScenarioUpdate::Complete,
    "max_ticks should complete the live session"
  );
  failures += expect(
    std::filesystem::exists(runDirectory / "checkpoint-1.json") &&
      std::filesystem::exists(runDirectory / "result.json"),
    "the observed tick should publish a checkpoint and final result"
  );

  const auto ready = readJson(runDirectory / "ready.json");
  const auto checkpoint = readJson(runDirectory / "checkpoint-1.json");
  const auto result = readJson(runDirectory / "result.json");
  failures += expect(
    ready.ok && ready.value.find("token") != nullptr &&
      ready.value.find("token")->string == "artifact_run",
    "ready output should carry the launcher token"
  );
  failures += expect(
    checkpoint.ok &&
      checkpoint.value.find("relative_tick") != nullptr &&
      checkpoint.value.find("relative_tick")->number == 1.0 &&
      checkpoint.value.find("latest_consumed_command") != nullptr,
    "checkpoint output should carry tick and command facts"
  );
  failures += expect(
    result.ok && result.value.find("passed") != nullptr &&
      result.value.find("passed")->boolean &&
      result.value.find("completion_reason") != nullptr &&
      result.value.find("completion_reason")->string == "max_ticks",
    "result output should record clean max-tick completion"
  );

  std::error_code cleanupError;
  std::filesystem::remove_all(runDirectory, cleanupError);
  return failures;
}

} // namespace

int main() {
  int failures = 0;
  failures += testCommandLine();
  failures += testFinishRequestValidation();
  failures += testTypedSetupRejection();
  failures += testReadyCheckpointAndResult();
  if (failures == 0) {
    std::cout << "Live scenario session tests passed\n";
  }
  return failures == 0 ? 0 : 1;
}
