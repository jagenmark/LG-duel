#include "server/ServerApp.hpp"

#include "console/ConsoleSystem.hpp"
#include "console/ConsoleConfig.hpp"
#include "net/UdpTransport.hpp"
#include "server/ServerGame.hpp"
#include "shared/Constants.hpp"
#include "sim/Arena.hpp"
#include "sim/GameplayCvars.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <deque>
#include <filesystem>
#include <cmath>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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

std::filesystem::path defaultConfigPath(
  const std::string& executablePath,
  const char* filename
) {
  namespace fs = std::filesystem;
  std::vector<fs::path> starts;
  if (!executablePath.empty()) {
    starts.push_back(fs::absolute(fs::path(executablePath)).parent_path());
  }
  starts.push_back(fs::current_path());

  for (fs::path directory : starts) {
    for (;;) {
      const fs::path candidate = directory / "config" / filename;
      if (fs::exists(candidate)) {
        return candidate;
      }
      const fs::path parent = directory.parent_path();
      if (parent.empty() || parent == directory) {
        break;
      }
      directory = parent;
    }
  }
  return {};
}

void logConsoleConfigErrors(const ConsoleConfigResult& result) {
  for (const std::string& error : result.errors) {
    std::cerr << "Config warning: " << error << '\n';
  }
}

bool sameMovementTuning(const MovementTuning& lhs, const MovementTuning& rhs) {
  return lhs.flightEnabled == rhs.flightEnabled &&
    lhs.groundAcceleration == rhs.groundAcceleration &&
    lhs.airAcceleration == rhs.airAcceleration &&
    lhs.groundFriction == rhs.groundFriction &&
    lhs.stopSpeed == rhs.stopSpeed &&
    lhs.gravity == rhs.gravity &&
    lhs.maxGroundSpeed == rhs.maxGroundSpeed &&
    lhs.maxAirSpeed == rhs.maxAirSpeed &&
    lhs.jumpImpulse == rhs.jumpImpulse &&
    lhs.airControlEnabled == rhs.airControlEnabled &&
    lhs.flightAcceleration == rhs.flightAcceleration &&
    lhs.maxFlightSpeed == rhs.maxFlightSpeed &&
    lhs.flightDamping == rhs.flightDamping &&
    lhs.flightGravityCancel == rhs.flightGravityCancel;
}

bool sameWeaponDamage(
  const WeaponDamageTuning& lhs,
  const WeaponDamageTuning& rhs
) {
  return lhs.shotgunDamagePerPellet == rhs.shotgunDamagePerPellet &&
    lhs.machineGunDamage == rhs.machineGunDamage &&
    lhs.lightningGunDamage == rhs.lightningGunDamage &&
    lhs.railgunDamage == rhs.railgunDamage &&
    lhs.rocketLauncherDamage == rhs.rocketLauncherDamage &&
    lhs.plasmaGunDamage == rhs.plasmaGunDamage;
}

[[nodiscard]] bool nearlyEqualGameplayFloat(float lhs, float rhs) {
  return std::fabs(lhs - rhs) <= 0.0001F;
}

[[nodiscard]] bool nearlySameGameplayMovementTuning(
  const MovementTuning& lhs,
  const MovementTuning& rhs
) {
  return lhs.flightEnabled == rhs.flightEnabled &&
    lhs.airControlEnabled == rhs.airControlEnabled &&
    nearlyEqualGameplayFloat(lhs.groundAcceleration, rhs.groundAcceleration) &&
    nearlyEqualGameplayFloat(lhs.airAcceleration, rhs.airAcceleration) &&
    nearlyEqualGameplayFloat(lhs.groundFriction, rhs.groundFriction) &&
    nearlyEqualGameplayFloat(lhs.stopSpeed, rhs.stopSpeed) &&
    nearlyEqualGameplayFloat(lhs.maxGroundSpeed, rhs.maxGroundSpeed) &&
    nearlyEqualGameplayFloat(lhs.flightAcceleration, rhs.flightAcceleration) &&
    nearlyEqualGameplayFloat(lhs.maxFlightSpeed, rhs.maxFlightSpeed) &&
    nearlyEqualGameplayFloat(lhs.flightDamping, rhs.flightDamping);
}

[[nodiscard]] const char* weaponSwitchingModeCvarValue(
  WeaponSwitchingMode mode
) {
  switch (mode) {
    case WeaponSwitchingMode::Ql:
      return "ql";
    case WeaponSwitchingMode::Cpma:
      return "cpma";
    case WeaponSwitchingMode::Crazy:
      return "crazy";
  }
  return "crazy";
}

[[nodiscard]] bool gameplayConsoleMatchesSnapshot(
  const ConsoleSystem& console,
  const ServerSnapshot& snapshot
) {
  const WeaponDamageTuning weaponDamage = weaponDamageTuningFromCvars(console);
  return
    nearlySameGameplayMovementTuning(
      movementTuningFromCvars(console),
      snapshot.movementTuning
    ) &&
    nearlyEqualGameplayFloat(console.getFloat("g_playersize_xy"), snapshot.playerSizeScaleXY) &&
    nearlyEqualGameplayFloat(console.getFloat("g_playersize_z"), snapshot.playerSizeScaleZ) &&
    nearlyEqualGameplayFloat(console.getFloat("g_lg_knockback"), snapshot.lightningKnockback) &&
    nearlyEqualGameplayFloat(console.getFloat("g_lg_fire_hz"), snapshot.lightningFireHz) &&
    nearlyEqualGameplayFloat(console.getFloat("g_rl_knockback"), snapshot.rocketKnockback) &&
    nearlyEqualGameplayFloat(console.getFloat("g_vampirism"), snapshot.vampirism) &&
    selfDamagePercentFromCvars(console) == snapshot.selfDamagePercent &&
    healthAmountFromCvars(console) == snapshot.healthAmount &&
    weaponSwitchingModeFromCvars(console) == snapshot.weaponSwitchingMode &&
    sameWeaponDamage(weaponDamage, snapshot.weaponDamage);
}

void syncGameplayConsoleFromSnapshot(
  ConsoleSystem& console,
  const ServerSnapshot& snapshot
) {
  (void)console.execute(
    std::string("set g_flight ") +
    (snapshot.movementTuning.flightEnabled ? "1" : "0")
  );
  (void)console.execute("set g_accel " + std::to_string(snapshot.movementTuning.groundAcceleration));
  (void)console.execute("set g_airaccel " + std::to_string(snapshot.movementTuning.airAcceleration));
  (void)console.execute(
    std::string("set g_aircontrol ") +
    (snapshot.movementTuning.airControlEnabled ? "1" : "0")
  );
  (void)console.execute("set g_friction " + std::to_string(snapshot.movementTuning.groundFriction));
  (void)console.execute("set g_stopspeed " + std::to_string(snapshot.movementTuning.stopSpeed));
  (void)console.execute("set g_maxspeed " + std::to_string(snapshot.movementTuning.maxGroundSpeed));
  (void)console.execute("set g_flightaccel " + std::to_string(snapshot.movementTuning.flightAcceleration));
  (void)console.execute("set g_flightmaxspeed " + std::to_string(snapshot.movementTuning.maxFlightSpeed));
  (void)console.execute("set g_flightdamping " + std::to_string(snapshot.movementTuning.flightDamping));
  (void)console.execute("set g_playersize_xy " + std::to_string(snapshot.playerSizeScaleXY));
  (void)console.execute("set g_playersize_z " + std::to_string(snapshot.playerSizeScaleZ));
  (void)console.execute("set g_lg_knockback " + std::to_string(snapshot.lightningKnockback));
  (void)console.execute("set g_lg_fire_hz " + std::to_string(snapshot.lightningFireHz));
  (void)console.execute("set g_rl_knockback " + std::to_string(snapshot.rocketKnockback));
  (void)console.execute("set g_sg_damage " + std::to_string(snapshot.weaponDamage.shotgunDamagePerPellet));
  (void)console.execute("set g_mg_damage " + std::to_string(snapshot.weaponDamage.machineGunDamage));
  (void)console.execute("set g_lg_damage " + std::to_string(snapshot.weaponDamage.lightningGunDamage));
  (void)console.execute("set g_rg_damage " + std::to_string(snapshot.weaponDamage.railgunDamage));
  (void)console.execute("set g_rl_damage " + std::to_string(snapshot.weaponDamage.rocketLauncherDamage));
  (void)console.execute("set g_pg_damage " + std::to_string(snapshot.weaponDamage.plasmaGunDamage));
  (void)console.execute("set g_vampirism " + std::to_string(snapshot.vampirism));
  (void)console.execute("set g_selfdamage " + std::to_string(snapshot.selfDamagePercent));
  (void)console.execute("set g_healthamount " + std::to_string(snapshot.healthAmount));
  (void)console.execute(
    std::string("set g_weaponswitching ") +
    weaponSwitchingModeCvarValue(snapshot.weaponSwitchingMode)
  );
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

  const std::filesystem::path balanceConfigPath =
    defaultConfigPath(executablePath_, "balance.cfg");
  ServerGame server(
    transport,
    balanceConfigPath.empty() ? std::string{} : balanceConfigPath.string()
  );
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
  registerGameplayCvars(console, CvarFlag::None);
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

  const std::filesystem::path serverCvarsPath =
    defaultConfigPath(executablePath_, "server_cvars.cfg");
  if (!serverCvarsPath.empty()) {
    logConsoleConfigErrors(
      executeConsoleConfigFile(console, serverCvarsPath.string())
    );
  } else {
    std::cerr << "Config warning: config/server_cvars.cfg not found; using code defaults\n";
  }

  MovementTuning lastAppliedMovementTuning;
  float lastAppliedPlayerSizeScaleXY = 1.0F;
  float lastAppliedPlayerSizeScaleZ = 1.0F;
  float lastAppliedLightningKnockback = 1000.0F;
  float lastAppliedLightningFireHz = 20.0F;
  float lastAppliedRocketKnockback = 1000.0F;
  WeaponDamageTuning lastAppliedWeaponDamage;
  float lastAppliedVampirism = 0.0F;
  std::uint8_t lastAppliedSelfDamagePercent = 100;
  std::int32_t lastAppliedHealthAmount = 100;
  WeaponSwitchingMode lastAppliedWeaponSwitchingMode = WeaponSwitchingMode::Crazy;
  bool firstRuntimeCvarApply = true;

  const auto applyConsoleCvarsToServer = [&] {
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

    const MovementTuning movementTuning = movementTuningFromCvars(console);
    const float playerSizeScaleXY = console.getFloat("g_playersize_xy");
    const float playerSizeScaleZ = console.getFloat("g_playersize_z");
    const float lightningKnockback = console.getFloat("g_lg_knockback");
    const float lightningFireHz = console.getFloat("g_lg_fire_hz");
    const float rocketKnockback = console.getFloat("g_rl_knockback");
    const WeaponDamageTuning weaponDamage = weaponDamageTuningFromCvars(console);
    const float vampirism = console.getFloat("g_vampirism");
    const std::uint8_t selfDamagePercent = selfDamagePercentFromCvars(console);
    const std::int32_t healthAmount = healthAmountFromCvars(console);
    const WeaponSwitchingMode weaponSwitchingMode =
      weaponSwitchingModeFromCvars(console);

    const bool runtimeChanged =
      firstRuntimeCvarApply ||
      !sameMovementTuning(movementTuning, lastAppliedMovementTuning) ||
      playerSizeScaleXY != lastAppliedPlayerSizeScaleXY ||
      playerSizeScaleZ != lastAppliedPlayerSizeScaleZ ||
      lightningKnockback != lastAppliedLightningKnockback ||
      lightningFireHz != lastAppliedLightningFireHz ||
      rocketKnockback != lastAppliedRocketKnockback ||
      !sameWeaponDamage(weaponDamage, lastAppliedWeaponDamage) ||
      vampirism != lastAppliedVampirism ||
      selfDamagePercent != lastAppliedSelfDamagePercent ||
      healthAmount != lastAppliedHealthAmount ||
      weaponSwitchingMode != lastAppliedWeaponSwitchingMode;
    if (!runtimeChanged) {
      return;
    }

    server.setRuntimeGameplayTuning(
      movementTuning,
      playerSizeScaleXY,
      playerSizeScaleZ,
      lightningKnockback,
      lightningFireHz,
      rocketKnockback,
      weaponDamage,
      vampirism,
      selfDamagePercent,
      healthAmount,
      server.botDodgeEnabled(),
      server.botDodgeMinIntervalMs(),
      server.botDodgeMaxIntervalMs(),
      weaponSwitchingMode
    );
    lastAppliedMovementTuning = movementTuning;
    lastAppliedPlayerSizeScaleXY = playerSizeScaleXY;
    lastAppliedPlayerSizeScaleZ = playerSizeScaleZ;
    lastAppliedLightningKnockback = lightningKnockback;
    lastAppliedLightningFireHz = lightningFireHz;
    lastAppliedRocketKnockback = rocketKnockback;
    lastAppliedWeaponDamage = weaponDamage;
    lastAppliedVampirism = vampirism;
    lastAppliedSelfDamagePercent = selfDamagePercent;
    lastAppliedHealthAmount = healthAmount;
    lastAppliedWeaponSwitchingMode = weaponSwitchingMode;
    firstRuntimeCvarApply = false;
  };
  applyConsoleCvarsToServer();
  bool consoleCvarsDirty = false;

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
        consoleCvarsDirty = true;
        if (!result.empty()) {
          std::cout << result << '\n';
        }
      }
    }

    if (consoleCvarsDirty) {
      applyConsoleCvarsToServer();
      consoleCvarsDirty = false;
    }
    if (resetRequested) {
      server.resetMatch();
      server.setConnectedPlayers(
        transport.connectedPlayers(),
        transport.connectedPlayerSessions()
      );
      resetRequested = false;
    }
    server.tick(kFixedTickSeconds);
    if (!gameplayConsoleMatchesSnapshot(console, server.snapshot())) {
      syncGameplayConsoleFromSnapshot(console, server.snapshot());
      applyConsoleCvarsToServer();
    }

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
