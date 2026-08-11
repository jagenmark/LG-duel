#include "server/ServerApp.hpp"

#include "console/ConsoleSystem.hpp"
#include "console/ConsoleConfig.hpp"
#include "net/UdpTransport.hpp"
#include "server/ServerGame.hpp"
#include "shared/Constants.hpp"
#include "sim/Arena.hpp"
#include "sim/GameplayCvars.hpp"
#include "sim/WeaponCatalog.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cctype>
#include <deque>
#include <filesystem>
#include <cmath>
#include <iostream>
#include <mutex>
#include <optional>
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
    lhs.dashTargetSpeed == rhs.dashTargetSpeed &&
    lhs.dashMaxSpeed == rhs.dashMaxSpeed &&
    lhs.dashAcceleration == rhs.dashAcceleration &&
    lhs.dashDuration == rhs.dashDuration &&
    lhs.dashCooldown == rhs.dashCooldown &&
    lhs.dashGroundHopVelocity == rhs.dashGroundHopVelocity &&
    lhs.dashAirHopVelocity == rhs.dashAirHopVelocity &&
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
    lhs.plasmaGunDamage == rhs.plasmaGunDamage &&
    lhs.freezeGunDamage == rhs.freezeGunDamage;
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
    nearlyEqualGameplayFloat(lhs.dashTargetSpeed, rhs.dashTargetSpeed) &&
    nearlyEqualGameplayFloat(lhs.dashMaxSpeed, rhs.dashMaxSpeed) &&
    nearlyEqualGameplayFloat(lhs.dashAcceleration, rhs.dashAcceleration) &&
    nearlyEqualGameplayFloat(lhs.dashDuration, rhs.dashDuration) &&
    nearlyEqualGameplayFloat(lhs.dashCooldown, rhs.dashCooldown) &&
    nearlyEqualGameplayFloat(lhs.dashGroundHopVelocity, rhs.dashGroundHopVelocity) &&
    nearlyEqualGameplayFloat(lhs.dashAirHopVelocity, rhs.dashAirHopVelocity) &&
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

[[nodiscard]] std::optional<BotAttackMode> parseBotAttackMode(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  if (value == "0" || value == "off" || value == "false") {
    return BotAttackMode::Off;
  }
  if (value == "easy") {
    return BotAttackMode::Easy;
  }
  if (value == "medium") {
    return BotAttackMode::Medium;
  }
  if (value == "hard") {
    return BotAttackMode::Hard;
  }
  return std::nullopt;
}

[[nodiscard]] const char* botAttackModeCvarValue(BotAttackMode mode) {
  switch (mode) {
  case BotAttackMode::Off:
    return "0";
  case BotAttackMode::Easy:
    return "easy";
  case BotAttackMode::Medium:
    return "medium";
  case BotAttackMode::Hard:
    return "hard";
  }
  return "0";
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
    knockbackTimeMsFromCvars(console) == snapshot.knockbackTimeMs &&
    nearlyEqualGameplayFloat(console.getFloat("g_vampirism"), snapshot.vampirism) &&
    selfDamagePercentFromCvars(console) == snapshot.selfDamagePercent &&
    healthAmountFromCvars(console) == snapshot.healthAmount &&
    infiniteAmmoFromCvars(console) == snapshot.weaponAmmo.infiniteAmmo &&
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
  (void)console.execute("set g_dash_targetspeed " + std::to_string(snapshot.movementTuning.dashTargetSpeed));
  (void)console.execute("set g_dash_maxspeed " + std::to_string(snapshot.movementTuning.dashMaxSpeed));
  (void)console.execute("set g_dash_accel " + std::to_string(snapshot.movementTuning.dashAcceleration));
  (void)console.execute("set g_dash_duration " + std::to_string(snapshot.movementTuning.dashDuration));
  (void)console.execute("set g_dash_cooldown " + std::to_string(snapshot.movementTuning.dashCooldown));
  (void)console.execute("set g_dash_groundhop " + std::to_string(snapshot.movementTuning.dashGroundHopVelocity));
  (void)console.execute("set g_dash_airhop " + std::to_string(snapshot.movementTuning.dashAirHopVelocity));
  (void)console.execute("set g_flightaccel " + std::to_string(snapshot.movementTuning.flightAcceleration));
  (void)console.execute("set g_flightmaxspeed " + std::to_string(snapshot.movementTuning.maxFlightSpeed));
  (void)console.execute("set g_flightdamping " + std::to_string(snapshot.movementTuning.flightDamping));
  (void)console.execute("set g_playersize_xy " + std::to_string(snapshot.playerSizeScaleXY));
  (void)console.execute("set g_playersize_z " + std::to_string(snapshot.playerSizeScaleZ));
  (void)console.execute("set g_lg_knockback " + std::to_string(snapshot.lightningKnockback));
  (void)console.execute("set g_lg_fire_hz " + std::to_string(snapshot.lightningFireHz));
  (void)console.execute("set g_rl_knockback " + std::to_string(snapshot.rocketKnockback));
  (void)console.execute("set g_knockback_time_ms " + std::to_string(snapshot.knockbackTimeMs));
  (void)console.execute("set g_sg_damage " + std::to_string(snapshot.weaponDamage.shotgunDamagePerPellet));
  (void)console.execute("set g_mg_damage " + std::to_string(snapshot.weaponDamage.machineGunDamage));
  (void)console.execute("set g_lg_damage " + std::to_string(snapshot.weaponDamage.lightningGunDamage));
  (void)console.execute("set g_fg_damage " + std::to_string(snapshot.weaponDamage.freezeGunDamage));
  (void)console.execute("set g_rg_damage " + std::to_string(snapshot.weaponDamage.railgunDamage));
  (void)console.execute("set g_rl_damage " + std::to_string(snapshot.weaponDamage.rocketLauncherDamage));
  (void)console.execute("set g_pg_damage " + std::to_string(snapshot.weaponDamage.plasmaGunDamage));
  (void)console.execute("set g_vampirism " + std::to_string(snapshot.vampirism));
  (void)console.execute("set g_selfdamage " + std::to_string(snapshot.selfDamagePercent));
  (void)console.execute("set g_healthamount " + std::to_string(snapshot.healthAmount));
  (void)console.execute(
    std::string("set g_infiniteammo ") +
    (snapshot.weaponAmmo.infiniteAmmo ? "1" : "0")
  );
  (void)console.execute(
    std::string("set g_weaponswitching ") +
    weaponSwitchingModeCvarValue(snapshot.weaponSwitchingMode)
  );
}

} // namespace

ServerCommandLineResult parseServerCommandLine(
  int argc,
  const char* const* argv
) {
  ServerCommandLineResult parsed;
  if (argc < 0 || (argc > 0 && argv == nullptr)) {
    parsed.error = "Invalid server command line";
    return parsed;
  }
  if (argc > 0 && argv[0] != nullptr) {
    parsed.options.executablePath = argv[0];
  }

  int argument = 1;
  if (argument < argc) {
    if (argv[argument] == nullptr) {
      parsed.error = "Invalid UDP port";
      return parsed;
    }
    const std::string_view text = argv[argument];
    unsigned int port = 0;
    const auto result = std::from_chars(
      text.data(),
      text.data() + text.size(),
      port
    );
    if (
      result.ec != std::errc{} ||
      result.ptr != text.data() + text.size() ||
      port > 65535U
    ) {
      parsed.error = "Invalid UDP port: " + std::string(text);
      return parsed;
    }
    parsed.options.port = static_cast<std::uint16_t>(port);
    ++argument;
  }

  scenario::LiveScenarioOptions live;
  bool haveScenario = false;
  bool haveRunDirectory = false;
  bool haveToken = false;
  while (argument < argc) {
    if (argument + 1 >= argc || argv[argument] == nullptr ||
        argv[argument + 1] == nullptr) {
      parsed.error =
        "Live scenario options require a value and must be supplied together";
      return parsed;
    }
    const std::string_view flag = argv[argument];
    const std::string value = argv[argument + 1];
    if (flag == "--live-scenario" && !haveScenario) {
      live.scenarioPath = value;
      haveScenario = true;
    } else if (flag == "--scenario-run-dir" && !haveRunDirectory) {
      live.runDirectory = value;
      haveRunDirectory = true;
    } else if (flag == "--scenario-token" && !haveToken) {
      live.token = value;
      haveToken = true;
    } else {
      parsed.error = "Unknown or repeated server option: " +
        std::string(flag);
      return parsed;
    }
    argument += 2;
  }

  const int liveOptionCount =
    static_cast<int>(haveScenario) +
    static_cast<int>(haveRunDirectory) +
    static_cast<int>(haveToken);
  if (liveOptionCount != 0 && liveOptionCount != 3) {
    parsed.error =
      "--live-scenario, --scenario-run-dir, and --scenario-token "
      "must be supplied together";
    return parsed;
  }
  if (liveOptionCount == 3) {
    if (!scenario::validateLiveScenarioOptions(live, parsed.error)) {
      return parsed;
    }
    parsed.options.liveScenario = std::move(live);
  }
  parsed.ok = true;
  return parsed;
}

ServerApp::ServerApp(std::uint16_t port, std::string executablePath)
  : options_{port, std::move(executablePath), std::nullopt} {}

ServerApp::ServerApp(ServerLaunchOptions options)
  : options_(std::move(options)) {}

int ServerApp::run() const {
  std::optional<scenario::LiveScenarioSession> liveSession;
  if (options_.liveScenario.has_value()) {
    std::string liveError;
    liveSession = scenario::LiveScenarioSession::load(
      *options_.liveScenario,
      liveError
    );
    if (!liveSession.has_value()) {
      std::cerr << "Live scenario initialization failed: "
                << liveError << '\n';
      return 1;
    }
  }

  UdpServerTransport transport(options_.port);
  if (!transport.initialize()) {
    std::cerr << "UDP server initialization failed: " << transport.lastError() << '\n';
    return 1;
  }

  const std::filesystem::path balanceConfigPath =
    defaultConfigPath(options_.executablePath, "balance.cfg");
  ServerGame server(
    transport,
    balanceConfigPath.empty() ? std::string{} : balanceConfigPath.string()
  );
  server.setMapDirectory(defaultMapDirectory(options_.executablePath));
  (void)server.loadRequestedMap(kDefaultMapName);
  std::cout << "LG Duel server listening on UDP port " << transport.localPort() << '\n';

  ConsoleSystem console;
  console.registerCvar({"sv_roundlimit", "Rounds required to win.", 10, CvarFlag::None, 1.0F, 100.0F});
  console.registerCvar({"sv_timelimit", "Match time limit in minutes; zero disables.", 0, CvarFlag::None, 0.0F, 120.0F});
  console.registerCvar({"sv_playerlimit", "Players required to begin.", 2, CvarFlag::None, 1.0F, static_cast<float>(kMaxPlayers)});
  console.registerCvar({"sv_countdown", "Round countdown in seconds.", 5.0F, CvarFlag::None, 0.0F, 60.0F});
  console.registerCvar({"sv_roundend", "Round-end delay in seconds.", 5.0F, CvarFlag::None, 0.0F, 30.0F});
  console.registerCvar({"sv_matchend", "Match-end delay in seconds.", 5.0F, CvarFlag::None, 0.0F, 60.0F});
  console.registerCvar({"sv_respawn_delay", "Death respawn delay for respawning modes in seconds.", 2.0F, CvarFlag::None, 0.0F, 30.0F});
  console.registerCvar({"sv_showopponenthealth", "Show opponent health to both players.", true, CvarFlag::None, {}, {}});
  console.registerCvar({"sv_mcg_scorelimit", "McGuffin points required to win a round.", 100, CvarFlag::None, 1.0F, 1000.0F});
  console.registerCvar({"sv_mcg_points_per_second", "McGuffin installed scoring rate.", 1, CvarFlag::None, 1.0F, 20.0F});
  console.registerCvar({"sv_mcg_carry_points_per_second", "McGuffin unbanked carry-credit rate.", 1, CvarFlag::None, 1.0F, 20.0F});
  console.registerCvar({"sv_mcg_carry_limit", "Maximum unbanked McGuffin carry credit.", 10, CvarFlag::None, 1.0F, 100.0F});
  console.registerCvar({"sv_mcg_spawn_delay", "McGuffin initial spawn delay in seconds.", 30.0F, CvarFlag::None, 0.0F, 120.0F});
  console.registerCvar({"sv_mcg_install_delay", "McGuffin installation hold in seconds.", 0.0F, CvarFlag::None, 0.0F, 10.0F});
  console.registerCvar({"sv_mcg_steal_time", "McGuffin steal hold in seconds.", 1.0F, CvarFlag::None, 0.0F, 10.0F});
  console.registerCvar({"sv_mcg_return_time", "Safety return time for an uncollected ground McGuffin; zero disables.", 30.0F, CvarFlag::None, 0.0F, 120.0F});
  console.registerCvar({"sv_mcg_throw_speed", "Forward speed of a thrown McGuffin.", 12.0F, CvarFlag::None, 0.0F, 50.0F});
  console.registerCvar({"sv_mcg_throw_up_speed", "Upward arc speed added to a thrown McGuffin.", 4.0F, CvarFlag::None, 0.0F, 30.0F});
  console.registerCvar({"sv_mcg_throw_velocity_inherit", "Fraction of carrier velocity inherited by a throw.", 1.0F, CvarFlag::None, 0.0F, 2.0F});
  console.registerCvar({"sv_mcg_throw_gravity", "Gravity applied to a thrown McGuffin.", 20.0F, CvarFlag::None, 0.0F, 100.0F});
  console.registerCvar({"sv_mcg_throw_bounce", "Velocity retained when a thrown McGuffin bounces.", 0.4F, CvarFlag::None, 0.0F, 1.5F});
  console.registerCvar({"sv_mcg_throw_pickup_delay", "Pickup lockout after throwing in seconds.", 0.2F, CvarFlag::None, 0.0F, 3.0F});
  console.registerCvar({"sv_mcg_final_hold", "Uncontested hold at 99 points in seconds.", 3.0F, CvarFlag::None, 0.0F, 30.0F});
  console.registerCvar({"sv_mcg_pickup_radius", "McGuffin ground pickup radius in world units.", 0.9F, CvarFlag::None, 0.1F, 5.0F});
  console.registerCvar({
    "map_path",
    "Map file used by map_validate and map_reload.",
    defaultMapPath(options_.executablePath),
    CvarFlag::None,
  });
  registerGameplayCvars(console, CvarFlag::None);
  console.registerCvar({"bot_stare", "Passive bots face the nearest valid enemy when bot_attack is off.", true, CvarFlag::None, {}, {}});
  console.registerCvar({"bot_standstill", "Force bots to generate no movement input.", false, CvarFlag::None, {}, {}});
  console.registerCvar({"bot_dodge", "Enable deterministic random bot left/right strafing.", false, CvarFlag::None, {}, {}});
  console.registerCvar({"bot_dodge_min_ms", "Minimum bot dodge direction interval in milliseconds.", 250, CvarFlag::None, 1.0F, 10000.0F});
  console.registerCvar({"bot_dodge_max_ms", "Maximum bot dodge direction interval in milliseconds.", 750, CvarFlag::None, 1.0F, 10000.0F});
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
      std::size_t occupied = 0;
      for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
        if (snapshot.connectedPlayers[index] || snapshot.botPlayers[index]) {
          ++occupied;
        }
      }
      std::string scoreText;
      for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
        if (!scoreText.empty()) {
          scoreText += "-";
        }
        scoreText += std::to_string(snapshot.scores[index]);
      }
      return "players=" + std::to_string(occupied) +
        " phase=" + std::to_string(static_cast<int>(snapshot.matchPhase)) +
        " score=" + scoreText;
    }
  );
  console.registerCommand(
    "mcguffin_debug",
    "Show authoritative McGuffin state and timers.",
    [&server](const std::vector<std::string>&) {
      const ServerSnapshot& snapshot = server.snapshot();
      const McGuffinSnapshot& objective = snapshot.mcguffin;
      return "state=" + std::to_string(static_cast<int>(objective.state)) +
        " carrier=" + std::to_string(objective.carrierIndex) +
        " team=" + std::to_string(static_cast<int>(objective.associatedTeam)) +
        " pos=" + std::to_string(objective.position.x) + "," +
          std::to_string(objective.position.y) + "," +
          std::to_string(objective.position.z) +
        " state_ticks=" + std::to_string(objective.stateTicks) +
        " score_credit=" + std::to_string(objective.scoreSubPoints) +
        " steal_ticks=" + std::to_string(objective.interactionTicks) +
        " final_hold_ticks=" + std::to_string(objective.finalHoldTicks);
    }
  );
  console.registerCommand(
    "spawn_debug",
    "Show the most recent authoritative team-spawn candidate scores.",
    [&server](const std::vector<std::string>&) {
      return server.spawnDebugString();
    }
  );
  console.registerCommand(
    "map_validate",
    "Validate the current map_path without changing the match.",
    [&console](const std::vector<std::string>& arguments) {
      const std::string path = arguments.size() >= 2
        ? arguments[1]
        : console.getString("map_path");
      ArenaLoadResult result;
      loadArenaFromFile(path, result);
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
      ArenaLoadResult result;
      loadArenaFromFile(path, result);
      if (!result.ok) {
        return "map reload failed: " + result.error;
      }
      server.setArena(result.arena);
      return "map reloaded: " + path + " boxes=" +
        std::to_string(server.arena().wallCount);
    }
  );
  console.registerCommand(
    "bot_add",
    "Add training bots: bot_add [count].",
    [&server](const std::vector<std::string>& arguments) {
      if (arguments.size() > 2) {
        return std::string("usage: bot_add [count]");
      }
      std::optional<std::size_t> count;
      if (arguments.size() == 2) {
        int parsed = 0;
        const auto result =
          std::from_chars(arguments[1].data(), arguments[1].data() + arguments[1].size(), parsed);
        if (
          result.ec != std::errc{} ||
          result.ptr != arguments[1].data() + arguments[1].size() ||
          parsed < 0
        ) {
          return std::string("usage: bot_add [count]");
        }
        count = static_cast<std::size_t>(parsed);
      }
      return server.addBots(count).message;
    }
  );
  console.registerCommand(
    "bot_kick",
    "Remove training bots: bot_kick all|<slot>.",
    [&server](const std::vector<std::string>& arguments) {
      if (arguments.size() != 2) {
        return std::string("usage: bot_kick all|<slot>");
      }
      if (arguments[1] == "all") {
        return server.kickAllBots().message;
      }
      int slot = 0;
      const auto result =
        std::from_chars(arguments[1].data(), arguments[1].data() + arguments[1].size(), slot);
      if (
        result.ec != std::errc{} ||
        result.ptr != arguments[1].data() + arguments[1].size()
      ) {
        return std::string("usage: bot_kick all|<slot>");
      }
      if (slot <= 0) {
        return std::string("bot_kick slot must be between 1 and ") +
          std::to_string(kDuelPlayerCount);
      }
      return server.kickBotAtPlayerIndex(static_cast<std::size_t>(slot - 1)).message;
    }
  );
  console.registerCommand(
    "bot_attack",
    "Set training bot combat mode: bot_attack 0|off|easy|medium|hard.",
    [&server](const std::vector<std::string>& arguments) {
      if (arguments.size() == 1) {
        return std::string("bot_attack = ") +
          botAttackModeCvarValue(server.botAttackMode());
      }
      if (arguments.size() != 2) {
        return std::string("usage: bot_attack 0|off|easy|medium|hard");
      }
      const std::optional<BotAttackMode> mode = parseBotAttackMode(arguments[1]);
      if (!mode.has_value()) {
        return std::string("usage: bot_attack 0|off|easy|medium|hard");
      }
      server.setBotAttackMode(*mode);
      return std::string("bot_attack = ") + botAttackModeCvarValue(*mode);
    }
  );
  console.registerCommand(
    "bot_difficulty",
    "Set normal bot skill: bot_difficulty easy|medium|hard.",
    [&server](const std::vector<std::string>& arguments) {
      if (arguments.size() == 1) {
        return std::string("bot_difficulty = ") +
          botAttackModeCvarValue(server.botAttackMode());
      }
      if (arguments.size() != 2) {
        return std::string("usage: bot_difficulty easy|medium|hard");
      }
      const std::optional<BotAttackMode> mode = parseBotAttackMode(arguments[1]);
      if (!mode.has_value() || *mode == BotAttackMode::Off) {
        return std::string("usage: bot_difficulty easy|medium|hard");
      }
      server.setBotAttackMode(*mode);
      return std::string("bot_difficulty = ") + botAttackModeCvarValue(*mode);
    }
  );
  console.registerCommand(
    "bot_weapon",
    "Force a bot weapon, or return to catalog-based auto choice: bot_weapon [auto|mg|sg|gl|rl|lg|rg|pg|fg|re|1..9].",
    [&server](const std::vector<std::string>& arguments) {
      if (arguments.size() == 1) {
        return std::string("bot_weapon = ") +
          (server.botWeaponAuto() ? "auto" : std::string(weaponShortName(server.botWeapon())));
      }
      if (arguments.size() != 2) {
        return std::string("usage: bot_weapon auto|mg|sg|gl|rl|lg|rg|pg|fg|re|1..9");
      }
      if (arguments[1] == "auto") {
        server.setBotWeaponAuto();
        return std::string("bot_weapon = auto");
      }
      const std::optional<Weapon> weapon = parseWeaponToken(arguments[1]);
      if (!weapon.has_value()) {
        return std::string("usage: bot_weapon auto|mg|sg|gl|rl|lg|rg|pg|fg|re|1..9");
      }
      server.setBotWeapon(*weapon);
      return std::string("bot_weapon = ") +
        std::string(weaponShortName(*weapon));
    }
  );
  console.registerCommand(
    "bot_debug",
    "Show one bot's current filtered AI state: bot_debug <slot>.",
    [&server](const std::vector<std::string>& arguments) {
      if (arguments.size() != 2) return std::string("usage: bot_debug <slot>");
      int slot = 0;
      const auto parsed = std::from_chars(
        arguments[1].data(), arguments[1].data() + arguments[1].size(), slot
      );
      if (parsed.ec != std::errc{} || parsed.ptr != arguments[1].data() + arguments[1].size() ||
          slot < 1 || slot > static_cast<int>(kDuelPlayerCount)) {
        return std::string("usage: bot_debug <slot>");
      }
      return server.botDebugString(static_cast<std::size_t>(slot - 1));
    }
  );

  const std::filesystem::path serverCvarsPath =
    defaultConfigPath(options_.executablePath, "server_cvars.cfg");
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
  std::int32_t lastAppliedKnockbackTimeMs = 100;
  WeaponDamageTuning lastAppliedWeaponDamage;
  float lastAppliedVampirism = 0.0F;
  std::uint8_t lastAppliedSelfDamagePercent = 100;
  std::int32_t lastAppliedHealthAmount = 100;
  bool lastAppliedInfiniteAmmo = true;
  WeaponSwitchingMode lastAppliedWeaponSwitchingMode = WeaponSwitchingMode::Crazy;
  bool lastAppliedBotStareEnabled = true;
  bool lastAppliedBotStandstillEnabled = false;
  bool lastAppliedBotDodgeEnabled = false;
  int lastAppliedBotDodgeMinIntervalMs = 250;
  int lastAppliedBotDodgeMaxIntervalMs = 750;
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
    rules.deathRespawnTicks = static_cast<std::uint16_t>(
      console.getFloat("sv_respawn_delay") * kFixedTickRate
    );
    rules.showOpponentHealth = console.getBool("sv_showopponenthealth");
    server.setMatchRules(rules);
    McGuffinConfig mcguffin;
    mcguffin.scoreLimit = static_cast<std::uint16_t>(console.getInt("sv_mcg_scorelimit"));
    mcguffin.pointsPerSecond = static_cast<std::uint16_t>(console.getInt("sv_mcg_points_per_second"));
    mcguffin.carryPointsPerSecond = static_cast<std::uint16_t>(console.getInt("sv_mcg_carry_points_per_second"));
    mcguffin.carryPointLimit = static_cast<std::uint16_t>(console.getInt("sv_mcg_carry_limit"));
    mcguffin.initialSpawnTicks = static_cast<std::uint32_t>(console.getFloat("sv_mcg_spawn_delay") * kFixedTickRate);
    mcguffin.installationDelayTicks = static_cast<std::uint32_t>(console.getFloat("sv_mcg_install_delay") * kFixedTickRate);
    mcguffin.stealTicks = static_cast<std::uint32_t>(console.getFloat("sv_mcg_steal_time") * kFixedTickRate);
    mcguffin.returnTicks = static_cast<std::uint32_t>(console.getFloat("sv_mcg_return_time") * kFixedTickRate);
    mcguffin.throwSpeed = console.getFloat("sv_mcg_throw_speed");
    mcguffin.throwUpSpeed = console.getFloat("sv_mcg_throw_up_speed");
    mcguffin.throwVelocityInheritance = console.getFloat("sv_mcg_throw_velocity_inherit");
    mcguffin.throwGravity = console.getFloat("sv_mcg_throw_gravity");
    mcguffin.throwBounceDamping = console.getFloat("sv_mcg_throw_bounce");
    mcguffin.throwPickupLockoutTicks = static_cast<std::uint32_t>(
      console.getFloat("sv_mcg_throw_pickup_delay") * kFixedTickRate
    );
    mcguffin.finalHoldTicks = static_cast<std::uint32_t>(console.getFloat("sv_mcg_final_hold") * kFixedTickRate);
    mcguffin.pickupRadius = console.getFloat("sv_mcg_pickup_radius");
    server.setMcGuffinConfig(mcguffin);

    const MovementTuning movementTuning = movementTuningFromCvars(console);
    const float playerSizeScaleXY = console.getFloat("g_playersize_xy");
    const float playerSizeScaleZ = console.getFloat("g_playersize_z");
    const float lightningKnockback = console.getFloat("g_lg_knockback");
    const float lightningFireHz = console.getFloat("g_lg_fire_hz");
    const float rocketKnockback = console.getFloat("g_rl_knockback");
    const std::int32_t knockbackTimeMs = knockbackTimeMsFromCvars(console);
    const WeaponDamageTuning weaponDamage = weaponDamageTuningFromCvars(console);
    const float vampirism = console.getFloat("g_vampirism");
    const std::uint8_t selfDamagePercent = selfDamagePercentFromCvars(console);
    const std::int32_t healthAmount = healthAmountFromCvars(console);
    const bool infiniteAmmo = infiniteAmmoFromCvars(console);
    const WeaponSwitchingMode weaponSwitchingMode =
      weaponSwitchingModeFromCvars(console);
    const bool botStareEnabled = console.getBool("bot_stare");
    const bool botStandstillEnabled = console.getBool("bot_standstill");
    const bool botDodgeEnabled = console.getBool("bot_dodge");
    const int botDodgeMinIntervalMs = console.getInt("bot_dodge_min_ms");
    const int botDodgeMaxIntervalMs = console.getInt("bot_dodge_max_ms");

    const bool runtimeChanged =
      firstRuntimeCvarApply ||
      !sameMovementTuning(movementTuning, lastAppliedMovementTuning) ||
      playerSizeScaleXY != lastAppliedPlayerSizeScaleXY ||
      playerSizeScaleZ != lastAppliedPlayerSizeScaleZ ||
      lightningKnockback != lastAppliedLightningKnockback ||
      lightningFireHz != lastAppliedLightningFireHz ||
      rocketKnockback != lastAppliedRocketKnockback ||
      knockbackTimeMs != lastAppliedKnockbackTimeMs ||
      !sameWeaponDamage(weaponDamage, lastAppliedWeaponDamage) ||
      vampirism != lastAppliedVampirism ||
      selfDamagePercent != lastAppliedSelfDamagePercent ||
      healthAmount != lastAppliedHealthAmount ||
      infiniteAmmo != lastAppliedInfiniteAmmo ||
      weaponSwitchingMode != lastAppliedWeaponSwitchingMode;
    const bool botBehaviorChanged =
      firstRuntimeCvarApply ||
      botStareEnabled != lastAppliedBotStareEnabled ||
      botStandstillEnabled != lastAppliedBotStandstillEnabled ||
      botDodgeEnabled != lastAppliedBotDodgeEnabled ||
      botDodgeMinIntervalMs != lastAppliedBotDodgeMinIntervalMs ||
      botDodgeMaxIntervalMs != lastAppliedBotDodgeMaxIntervalMs;
    if (botBehaviorChanged) {
      server.setBotBehavior(
        botStareEnabled,
        botStandstillEnabled,
        botDodgeEnabled,
        botDodgeMinIntervalMs,
        botDodgeMaxIntervalMs,
        server.botAttackMode()
      );
      lastAppliedBotStareEnabled = botStareEnabled;
      lastAppliedBotStandstillEnabled = botStandstillEnabled;
      lastAppliedBotDodgeEnabled = botDodgeEnabled;
      lastAppliedBotDodgeMinIntervalMs = botDodgeMinIntervalMs;
      lastAppliedBotDodgeMaxIntervalMs = botDodgeMaxIntervalMs;
    }
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
      knockbackTimeMs,
      weaponDamage,
      vampirism,
      selfDamagePercent,
      healthAmount,
      infiniteAmmo,
      botDodgeEnabled,
      botDodgeMinIntervalMs,
      botDodgeMaxIntervalMs,
      weaponSwitchingMode
    );
    lastAppliedMovementTuning = movementTuning;
    lastAppliedPlayerSizeScaleXY = playerSizeScaleXY;
    lastAppliedPlayerSizeScaleZ = playerSizeScaleZ;
    lastAppliedLightningKnockback = lightningKnockback;
    lastAppliedLightningFireHz = lightningFireHz;
    lastAppliedRocketKnockback = rocketKnockback;
    lastAppliedKnockbackTimeMs = knockbackTimeMs;
    lastAppliedWeaponDamage = weaponDamage;
    lastAppliedVampirism = vampirism;
    lastAppliedSelfDamagePercent = selfDamagePercent;
    lastAppliedHealthAmount = healthAmount;
    lastAppliedInfiniteAmmo = infiniteAmmo;
    lastAppliedWeaponSwitchingMode = weaponSwitchingMode;
    firstRuntimeCvarApply = false;
  };
  applyConsoleCvarsToServer();
  bool consoleCvarsDirty = false;

  std::mutex inputMutex;
  std::deque<std::string> inputLines;
  if (!liveSession.has_value()) {
    std::thread([&inputMutex, &inputLines] {
      std::string line;
      while (std::getline(std::cin, line)) {
        std::lock_guard lock(inputMutex);
        inputLines.push_back(std::move(line));
      }
    }).detach();
  }

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

    bool observeLiveTick = false;
    if (liveSession.has_value()) {
      const scenario::LiveScenarioUpdate liveUpdate =
        liveSession->beforeServerTick(
          server,
          transport.connectedPlayers(),
          transport.connectedPlayerSessions()
        );
      if (liveUpdate == scenario::LiveScenarioUpdate::Failed) {
        std::cerr << "Live scenario failed: "
                  << liveSession->error() << '\n';
        return 1;
      }
      if (liveUpdate == scenario::LiveScenarioUpdate::Complete) {
        return 0;
      }
      observeLiveTick =
        liveUpdate == scenario::LiveScenarioUpdate::Running;
    }

    if (!liveSession.has_value()) {
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
    if (observeLiveTick) {
      const scenario::LiveScenarioUpdate liveUpdate =
        liveSession->afterServerTick(server);
      if (liveUpdate == scenario::LiveScenarioUpdate::Failed) {
        std::cerr << "Live scenario failed: "
                  << liveSession->error() << '\n';
        return 1;
      }
      if (liveUpdate == scenario::LiveScenarioUpdate::Complete) {
        return 0;
      }
    }
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
