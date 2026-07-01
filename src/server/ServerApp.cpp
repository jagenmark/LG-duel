#include "server/ServerApp.hpp"

#include "console/ConsoleSystem.hpp"
#include "net/UdpTransport.hpp"
#include "server/ServerGame.hpp"
#include "shared/Constants.hpp"
#include "sim/Arena.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <deque>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

namespace lg {
namespace {

constexpr const char* kDefaultMapName = "eyetoeye";

std::string defaultMapPath(const std::string& executablePath) {
  namespace fs = std::filesystem;
  constexpr const char* kRelativeMapPath = "maps/eyetoeye.map";
  if (!executablePath.empty()) {
    const fs::path executable = fs::absolute(fs::path(executablePath));
    const fs::path executableMap = executable.parent_path() / kRelativeMapPath;
    if (fs::exists(executableMap)) {
      return executableMap.string();
    }
  }
  return kRelativeMapPath;
}

std::string defaultMapDirectory(const std::string& executablePath) {
  namespace fs = std::filesystem;
  if (!executablePath.empty()) {
    const fs::path executable = fs::absolute(fs::path(executablePath));
    const fs::path executableMaps = executable.parent_path() / "maps";
    if (fs::exists(executableMaps)) {
      return executableMaps.string();
    }
  }
  return "maps";
}

} // namespace

ServerApp::ServerApp(std::uint16_t port, std::string executablePath)
  : port_(port), executablePath_(std::move(executablePath)) {}

int ServerApp::run() const {
  UdpServerTransport transport(port_);
  if (!transport.initialize()) {
    std::cerr << "UDP server initialization failed: " << transport.lastError() << '\n';
    return 1;
  }

  ServerGame server(transport);
  server.setMapDirectory(defaultMapDirectory(executablePath_));
  (void)server.loadRequestedMap(kDefaultMapName);
  std::cout << "LG Duel server listening on UDP port " << transport.localPort() << '\n';

  ConsoleSystem console;
  console.registerCvar({"sv_roundlimit", "Rounds required to win.", 10, CvarFlag::None, 1.0F, 100.0F});
  console.registerCvar({"sv_timelimit", "Match time limit in minutes; zero disables.", 0, CvarFlag::None, 0.0F, 120.0F});
  console.registerCvar({"sv_playerlimit", "Players required to begin.", 2, CvarFlag::None, 1.0F, static_cast<float>(kMaxPlayers)});
  console.registerCvar({"sv_countdown", "Round countdown in seconds.", 5.0F, CvarFlag::None, 0.0F, 60.0F});
  console.registerCvar({"sv_roundend", "Round-end delay in seconds.", 5.0F, CvarFlag::None, 0.0F, 30.0F});
  console.registerCvar({"sv_matchend", "Match-end delay in seconds.", 5.0F, CvarFlag::None, 0.0F, 60.0F});
  console.registerCvar({"sv_showopponenthealth", "Show opponent health to both players.", true, CvarFlag::None, {}, {}});
  console.registerCvar({
    "map_path",
    "Map file used by map_validate and map_reload.",
    defaultMapPath(executablePath_),
    CvarFlag::None,
  });
  bool resetRequested = false;
  console.registerCommand(
    "resetmatch",
    "Reset scores and return to ready-up.",
    [&resetRequested](const std::vector<std::string>&) {
      resetRequested = true;
      return std::string("match reset requested");
    }
  );
  console.registerCommand(
    "status",
    "Show connected players, phase, and score.",
    [&server](const std::vector<std::string>&) {
      const ServerSnapshot& snapshot = server.snapshot();
      const std::size_t connected = static_cast<std::size_t>(std::count(
        snapshot.connectedPlayers.begin(),
        snapshot.connectedPlayers.end(),
        true
      ));
      std::string scoreText;
      for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
        if (!scoreText.empty()) {
          scoreText += "-";
        }
        scoreText += std::to_string(snapshot.scores[index]);
      }
      return "players=" + std::to_string(connected) +
        " phase=" + std::to_string(static_cast<int>(snapshot.matchPhase)) +
        " score=" + scoreText;
    }
  );
  console.registerCommand(
    "map_validate",
    "Validate the current map_path without changing the match.",
    [&console](const std::vector<std::string>& arguments) {
      const std::string path = arguments.size() >= 2
        ? arguments[1]
        : console.getString("map_path");
      const ArenaLoadResult result = loadArenaFromFile(path);
      if (!result.ok) {
        return "map invalid: " + result.error;
      }
      return "map ok: " + path + " bounds=" +
        std::to_string(result.arena.min.x) + "," +
        std::to_string(result.arena.min.y) + "," +
        std::to_string(result.arena.min.z) + " to " +
        std::to_string(result.arena.max.x) + "," +
        std::to_string(result.arena.max.y) + "," +
        std::to_string(result.arena.max.z) + " boxes=" +
        std::to_string(result.arena.wallCount);
    }
  );
  console.registerCommand(
    "map_reload",
    "Reload map_path and reset the authoritative match state.",
    [&console, &server](const std::vector<std::string>& arguments) {
      const std::string path = arguments.size() >= 2
        ? arguments[1]
        : console.getString("map_path");
      const ArenaLoadResult result = loadArenaFromFile(path);
      if (!result.ok) {
        return "map reload failed: " + result.error;
      }
      server.setArena(result.arena);
      return "map reloaded: " + path + " boxes=" +
        std::to_string(server.arena().wallCount);
    }
  );
  console.registerCommand(
    "bot_dodge",
    "Toggle BOT random left/right movement: bot_dodge [0|1] [min_ms max_ms].",
    [&server](const std::vector<std::string>& arguments) {
      auto parseInt = [](const std::string& text, int& value) {
        const auto result =
          std::from_chars(text.data(), text.data() + text.size(), value);
        return result.ec == std::errc{} &&
          result.ptr == text.data() + text.size();
      };

      bool enabled = !server.botDodgeEnabled();
      std::size_t intervalArgument = 1;
      if (arguments.size() >= 2) {
        if (
          arguments[1] == "1" ||
          arguments[1] == "on" ||
          arguments[1] == "true"
        ) {
          enabled = true;
          intervalArgument = 2;
        } else if (
          arguments[1] == "0" ||
          arguments[1] == "off" ||
          arguments[1] == "false"
        ) {
          enabled = false;
          intervalArgument = 2;
        }
      }

      int minMs = server.botDodgeMinIntervalMs();
      int maxMs = server.botDodgeMaxIntervalMs();
      if (arguments.size() > intervalArgument) {
        if (arguments.size() != intervalArgument + 2) {
          return std::string("usage: bot_dodge [0|1] [min_ms max_ms]");
        }
        if (
          !parseInt(arguments[intervalArgument], minMs) ||
          !parseInt(arguments[intervalArgument + 1], maxMs)
        ) {
          return std::string("usage: bot_dodge [0|1] [min_ms max_ms]");
        }
      }

      server.setBotDodge(enabled, minMs, maxMs);
      return std::string("bot_dodge = ") +
        (server.botDodgeEnabled() ? "1" : "0") +
        " (" + std::to_string(server.botDodgeMinIntervalMs()) + "-" +
        std::to_string(server.botDodgeMaxIntervalMs()) + " ms)";
    }
  );

  std::mutex inputMutex;
  std::deque<std::string> inputLines;
  std::thread([&inputMutex, &inputLines] {
    std::string line;
    while (std::getline(std::cin, line)) {
      std::lock_guard lock(inputMutex);
      inputLines.push_back(std::move(line));
    }
  }).detach();

  using Clock = std::chrono::steady_clock;
  const auto tickDuration = std::chrono::duration_cast<Clock::duration>(
    std::chrono::duration<float>(kFixedTickSeconds)
  );
  auto nextTick = Clock::now();
  std::size_t previousClientCount = 0;

  while (true) {
    nextTick += tickDuration;
    transport.update();
    server.setConnectedPlayers(
      transport.connectedPlayers(),
      transport.connectedPlayerSessions()
    );

    {
      std::lock_guard lock(inputMutex);
      while (!inputLines.empty()) {
        const std::string result = console.execute(inputLines.front());
        inputLines.pop_front();
        if (!result.empty()) {
          std::cout << result << '\n';
        }
      }
    }

    MatchRules rules;
    rules.roundLimit = static_cast<std::uint16_t>(console.getInt("sv_roundlimit"));
    rules.timeLimitMinutes = static_cast<std::uint16_t>(console.getInt("sv_timelimit"));
    rules.playerLimit = static_cast<std::uint8_t>(console.getInt("sv_playerlimit"));
    rules.countdownTicks = static_cast<std::uint16_t>(
      console.getFloat("sv_countdown") * kFixedTickRate
    );
    rules.roundEndTicks = static_cast<std::uint16_t>(
      console.getFloat("sv_roundend") * kFixedTickRate
    );
    rules.matchEndTicks = static_cast<std::uint16_t>(
      console.getFloat("sv_matchend") * kFixedTickRate
    );
    rules.showOpponentHealth = console.getBool("sv_showopponenthealth");
    server.setMatchRules(rules);
    if (resetRequested) {
      server.resetMatch();
      server.setConnectedPlayers(
        transport.connectedPlayers(),
        transport.connectedPlayerSessions()
      );
      resetRequested = false;
    }
    server.tick(kFixedTickSeconds);

    const std::size_t clientCount = transport.connectedClientCount();
    if (clientCount != previousClientCount) {
      std::cout << "Connected clients: " << clientCount << '\n';
      previousClientCount = clientCount;
    }

    std::this_thread::sleep_until(nextTick);
    const auto now = Clock::now();
    if (now - nextTick > tickDuration * 8) {
      nextTick = now;
    }
  }
}

} // namespace lg
