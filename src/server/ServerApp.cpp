#include "server/ServerApp.hpp"

#include "console/ConsoleSystem.hpp"
#include "net/UdpTransport.hpp"
#include "server/ServerGame.hpp"
#include "shared/Constants.hpp"

#include <algorithm>
#include <chrono>
#include <deque>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

namespace lg {

ServerApp::ServerApp(std::uint16_t port) : port_(port) {}

int ServerApp::run() const {
  UdpServerTransport transport(port_);
  if (!transport.initialize()) {
    std::cerr << "UDP server initialization failed: " << transport.lastError() << '\n';
    return 1;
  }

  ServerGame server(transport);
  std::cout << "LG Duel server listening on UDP port " << transport.localPort() << '\n';

  ConsoleSystem console;
  console.registerCvar({"sv_roundlimit", "Rounds required to win.", 10, CvarFlag::None, 1.0F, 100.0F});
  console.registerCvar({"sv_timelimit", "Match time limit in minutes; zero disables.", 0, CvarFlag::None, 0.0F, 120.0F});
  console.registerCvar({"sv_playerlimit", "Players required to begin.", 2, CvarFlag::None, 1.0F, 2.0F});
  console.registerCvar({"sv_countdown", "Round countdown in seconds.", 5.0F, CvarFlag::None, 0.0F, 60.0F});
  console.registerCvar({"sv_roundend", "Round-end delay in seconds.", 1.0F, CvarFlag::None, 0.0F, 30.0F});
  console.registerCvar({"sv_matchend", "Match-end delay in seconds.", 5.0F, CvarFlag::None, 0.0F, 60.0F});
  console.registerCvar({"sv_showopponenthealth", "Show opponent health to both players.", true, CvarFlag::None, {}, {}});
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
      return "players=" + std::to_string(connected) +
        " phase=" + std::to_string(static_cast<int>(snapshot.matchPhase)) +
        " score=" + std::to_string(snapshot.scores[0]) +
        "-" + std::to_string(snapshot.scores[1]);
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
    server.setConnectedPlayers(transport.connectedPlayers());

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
      server.setConnectedPlayers(transport.connectedPlayers());
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
