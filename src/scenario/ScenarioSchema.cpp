#include "scenario/ScenarioSchema.hpp"

#include "shared/Constants.hpp"
#include "sim/MapRegistry.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>

namespace lg::scenario {
namespace {

using Type = dev::JsonValue::Type;

constexpr double kMaxCoordinate = 100000.0;
constexpr double kMaxVelocity = 10000.0;
constexpr std::uint32_t kMaxScenarioTicks = 10000000U;
constexpr std::uint32_t kMaxLiveScenarioTicks = 2500U;
constexpr std::uint32_t kMaxLiveInputTicks = 1250U;
constexpr int kMaxNetworkDelayMs = 5000;
constexpr std::uint32_t kMaxScreenshotDimension = 16384U;

[[nodiscard]] std::string childPath(std::string_view path, std::string_view key) {
  return path.empty() ? std::string(key) : std::string(path) + "." + std::string(key);
}

[[nodiscard]] std::string itemPath(std::string_view path, std::size_t index) {
  return std::string(path) + "[" + std::to_string(index) + "]";
}

[[nodiscard]] bool rejectUnknown(
  const dev::JsonValue& value,
  std::initializer_list<std::string_view> allowed,
  std::string_view path,
  std::string& error
) {
  std::set<std::string_view> keys(allowed);
  for (const auto& [key, member] : value.object) {
    (void)member;
    if (!keys.contains(key)) {
      error = childPath(path, key) + ": unknown field";
      return false;
    }
  }
  return true;
}

[[nodiscard]] const dev::JsonValue* required(
  const dev::JsonValue& object,
  std::string_view key,
  Type type,
  std::string_view path,
  std::string& error
) {
  const dev::JsonValue* value = object.find(key);
  if (value == nullptr) {
    error = childPath(path, key) + ": required field is missing";
    return nullptr;
  }
  if (value->type != type) {
    error = childPath(path, key) + ": has the wrong JSON type";
    return nullptr;
  }
  return value;
}

template <typename Integer>
[[nodiscard]] bool integerValue(
  const dev::JsonValue* value,
  Integer minimum,
  Integer maximum,
  Integer& output,
  std::string_view path,
  std::string& error
) {
  if (value == nullptr || value->type != Type::Number ||
      !std::isfinite(value->number) || std::floor(value->number) != value->number ||
      value->number < static_cast<double>(minimum) ||
      value->number > static_cast<double>(maximum)) {
    error = std::string(path) + ": must be an integer in [" +
      std::to_string(minimum) + ", " + std::to_string(maximum) + "]";
    return false;
  }
  output = static_cast<Integer>(value->number);
  return true;
}

[[nodiscard]] bool numberValue(
  const dev::JsonValue* value,
  double minimum,
  double maximum,
  float& output,
  std::string_view path,
  std::string& error
) {
  if (value == nullptr || value->type != Type::Number ||
      !std::isfinite(value->number) || value->number < minimum ||
      value->number > maximum) {
    error = std::string(path) + ": must be a finite number in [" +
      std::to_string(minimum) + ", " + std::to_string(maximum) + "]";
    return false;
  }
  output = static_cast<float>(value->number);
  return true;
}

[[nodiscard]] bool boolValue(
  const dev::JsonValue* value,
  bool& output,
  std::string_view path,
  std::string& error
) {
  if (value == nullptr || value->type != Type::Boolean) {
    error = std::string(path) + ": must be a boolean";
    return false;
  }
  output = value->boolean;
  return true;
}

[[nodiscard]] bool stringValue(
  const dev::JsonValue* value,
  std::string& output,
  std::string_view path,
  std::string& error,
  bool allowEmpty = false
) {
  if (value == nullptr || value->type != Type::String ||
      (!allowEmpty && value->string.empty())) {
    error = std::string(path) + (allowEmpty
      ? ": must be a string" : ": must be a non-empty string");
    return false;
  }
  if (value->string.size() > 1024U) {
    error = std::string(path) + ": must be at most 1024 bytes";
    return false;
  }
  output = value->string;
  return true;
}

[[nodiscard]] bool vectorValue(
  const dev::JsonValue* value,
  Vec3& output,
  double magnitude,
  std::string_view path,
  std::string& error
) {
  if (value == nullptr || value->type != Type::Array || value->array.size() != 3U) {
    error = std::string(path) + ": must be a three-number array";
    return false;
  }
  float* parts[] = {&output.x, &output.y, &output.z};
  for (std::size_t index = 0; index < 3U; ++index) {
    if (!numberValue(
          &value->array[index], -magnitude, magnitude, *parts[index],
          itemPath(path, index), error)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::optional<Weapon> parseWeaponName(std::string_view name) {
  if (name == "machine_gun" || name == "mg") return Weapon::MachineGun;
  if (name == "shotgun" || name == "sg") return Weapon::Shotgun;
  if (name == "grenade_launcher" || name == "gl") return Weapon::GrenadeLauncher;
  if (name == "rocket_launcher" || name == "rl") return Weapon::RocketLauncher;
  if (name == "lightning_gun" || name == "lg") return Weapon::LightningGun;
  if (name == "railgun" || name == "rg" || name == "sr") return Weapon::Railgun;
  if (name == "plasma_gun" || name == "pg") return Weapon::PlasmaGun;
  if (name == "freeze_gun" || name == "fg") return Weapon::FreezeGun;
  if (name == "revolver" || name == "re") return Weapon::Revolver;
  return std::nullopt;
}

[[nodiscard]] bool weaponValue(
  const dev::JsonValue* value,
  Weapon& output,
  std::string_view path,
  std::string& error
) {
  if (value == nullptr || value->type != Type::String) {
    error = std::string(path) + ": must be a weapon name";
    return false;
  }
  const std::optional<Weapon> weapon = parseWeaponName(value->string);
  if (!weapon) {
    error = std::string(path) + ": unknown weapon '" + value->string + "'";
    return false;
  }
  output = *weapon;
  return true;
}

[[nodiscard]] std::optional<Team> parseTeam(std::string_view value) {
  if (value == "none") return Team::None;
  if (value == "red") return Team::Red;
  if (value == "blue") return Team::Blue;
  return std::nullopt;
}

[[nodiscard]] std::optional<GameMode> parseGameMode(std::string_view value) {
  if (value == "duel") return GameMode::Duel;
  if (value == "clan_arena") return GameMode::ClanArena;
  if (value == "mcguffin") return GameMode::McGuffin;
  return std::nullopt;
}

[[nodiscard]] std::optional<OneTickEdge> parseOneTickEdge(std::string_view value) {
  if (value == "jump") return OneTickEdge::Jump;
  if (value == "crouch") return OneTickEdge::Crouch;
  if (value == "dash") return OneTickEdge::Dash;
  if (value == "attack") return OneTickEdge::Attack;
  if (value == "sneak") return OneTickEdge::Sneak;
  if (value == "zoom") return OneTickEdge::Zoom;
  return std::nullopt;
}

[[nodiscard]] std::string_view oneTickEdgeName(OneTickEdge edge) {
  switch (edge) {
  case OneTickEdge::Jump: return "jump";
  case OneTickEdge::Crouch: return "crouch";
  case OneTickEdge::Dash: return "dash";
  case OneTickEdge::Attack: return "attack";
  case OneTickEdge::Sneak: return "sneak";
  case OneTickEdge::Zoom: return "zoom";
  }
  return "unknown";
}

[[nodiscard]] std::optional<AssertionClassification> parseClassification(
  std::string_view value
) {
  if (value == "AUTHORITATIVE_DETERMINISTIC")
    return AssertionClassification::AuthoritativeDeterministic;
  if (value == "CLIENT_BOUNDED")
    return AssertionClassification::ClientBounded;
  if (value == "RENDERER_ATTESTED")
    return AssertionClassification::RendererAttested;
  if (value == "VISUAL_REVIEW")
    return AssertionClassification::VisualReview;
  return std::nullopt;
}

[[nodiscard]] bool parseExecution(
  const dev::JsonValue& value,
  ScenarioExecution& output,
  std::string& error
) {
  const std::string path = "execution";
  if (!rejectUnknown(value, {"mode", "max_ticks", "repeat"}, path, error)) return false;
  std::string mode;
  if (!stringValue(value.find("mode"), mode, "execution.mode", error)) return false;
  if (mode == "headless") {
    output.mode = ScenarioExecutionMode::Headless;
  } else if (mode == "client_server") {
    output.mode = ScenarioExecutionMode::ClientServer;
  } else {
    error = "execution.mode: must be headless or client_server";
    return false;
  }
  if (!integerValue(
        value.find("max_ticks"), std::uint32_t{1}, kMaxScenarioTicks,
        output.maxTicks, childPath(path, "max_ticks"), error)) return false;
  if (const dev::JsonValue* repeat = value.find("repeat"); repeat != nullptr &&
      !integerValue(
        repeat, std::uint32_t{1}, std::uint32_t{100}, output.repeat,
        childPath(path, "repeat"), error)) return false;
  return true;
}

[[nodiscard]] bool parseNetwork(
  const dev::JsonValue& value,
  ScenarioNetwork& output,
  std::string& error
) {
  const std::string path = "network";
  if (!rejectUnknown(
        value,
        {"latency_ms", "jitter_ms", "packet_loss_percent", "reorder_percent", "seed"},
        path,
        error)) return false;
  auto parseOptional = [&](std::string_view name, int maximum, int& target) {
    const dev::JsonValue* member = value.find(name);
    return member == nullptr ||
      integerValue(member, 0, maximum, target, childPath(path, name), error);
  };
  if (!parseOptional("latency_ms", kMaxNetworkDelayMs, output.latencyMs) ||
      !parseOptional("jitter_ms", kMaxNetworkDelayMs, output.jitterMs) ||
      !parseOptional("packet_loss_percent", 100, output.packetLossPercent) ||
      !parseOptional("reorder_percent", 100, output.reorderPercent) ||
      !integerValue(
        value.find("seed"), std::uint32_t{0},
        std::numeric_limits<std::uint32_t>::max(), output.seed,
        "network.seed", error)) return false;
  return true;
}

[[nodiscard]] bool parseWorld(
  const dev::JsonValue& value,
  ScenarioWorld& output,
  std::string& error
) {
  const std::string path = "world";
  if (!rejectUnknown(value, {"map", "game_mode", "seed"}, path, error)) return false;
  if (!stringValue(value.find("map"), output.map, "world.map", error)) return false;
  if (!isValidMapName(output.map)) {
    error = "world.map: must be a safe map stem or .map file name";
    return false;
  }
  std::string mode;
  if (!stringValue(value.find("game_mode"), mode, "world.game_mode", error)) return false;
  const std::optional<GameMode> parsedMode = parseGameMode(mode);
  if (!parsedMode) {
    error = "world.game_mode: must be duel, clan_arena, or mcguffin";
    return false;
  }
  output.gameMode = *parsedMode;
  if (!integerValue(
        value.find("seed"), std::uint32_t{0},
        std::numeric_limits<std::uint32_t>::max(), output.seed,
        "world.seed", error)) return false;
  return true;
}

[[nodiscard]] bool parseAmmo(
  const dev::JsonValue& value,
  WeaponAmmoArray& output,
  std::string_view path,
  std::string& error
) {
  if (value.type != Type::Object) {
    error = std::string(path) + ": must be an object keyed by weapon name";
    return false;
  }
  std::set<Weapon> seen;
  for (const auto& [key, member] : value.object) {
    const std::optional<Weapon> weapon = parseWeaponName(key);
    if (!weapon) {
      error = childPath(path, key) + ": unknown weapon";
      return false;
    }
    if (!seen.insert(*weapon).second) {
      error = childPath(path, key) + ": repeats a weapon through an alias";
      return false;
    }
    std::int32_t count = 0;
    if (!integerValue(
          &member, std::int32_t{0}, std::int32_t{1000000}, count,
          childPath(path, key), error)) return false;
    output[weaponIndex(*weapon)] = count;
  }
  return true;
}

[[nodiscard]] bool parsePlayer(
  const dev::JsonValue& value,
  PlayerInitialState& output,
  std::string_view path,
  std::string& error
) {
  if (value.type != Type::Object) {
    error = std::string(path) + ": must be an object";
    return false;
  }
  if (!rejectUnknown(
        value, {"index", "connected", "bot", "team", "position", "velocity",
                "view_yaw_degrees", "view_pitch_degrees", "health", "alive",
                "selected_weapon", "ammo"}, path, error)) return false;
  if (!integerValue(
        value.find("index"), std::size_t{0}, kMaxPlayers - 1U, output.index,
        childPath(path, "index"), error)) return false;
  if (const dev::JsonValue* connected = value.find("connected"); connected != nullptr &&
      !boolValue(connected, output.connected, childPath(path, "connected"), error)) return false;
  if (const dev::JsonValue* bot = value.find("bot"); bot != nullptr &&
      !boolValue(bot, output.bot, childPath(path, "bot"), error)) return false;
  if (const dev::JsonValue* team = value.find("team"); team != nullptr) {
    if (team->type != Type::String || !parseTeam(team->string)) {
      error = childPath(path, "team") + ": must be none, red, or blue";
      return false;
    }
    output.team = *parseTeam(team->string);
  }
  if (!vectorValue(
        value.find("position"), output.position, kMaxCoordinate,
        childPath(path, "position"), error) ||
      !vectorValue(
        value.find("velocity"), output.velocity, kMaxVelocity,
        childPath(path, "velocity"), error)) return false;
  if (!numberValue(
        value.find("view_yaw_degrees"), -360000.0, 360000.0,
        output.viewYawDegrees, childPath(path, "view_yaw_degrees"), error) ||
      !numberValue(
        value.find("view_pitch_degrees"), -90.0, 90.0,
        output.viewPitchDegrees, childPath(path, "view_pitch_degrees"), error)) return false;
  if (!integerValue(
        value.find("health"), 0, 1000000, output.health,
        childPath(path, "health"), error) ||
      !boolValue(
        value.find("alive"), output.alive, childPath(path, "alive"), error) ||
      !weaponValue(
        value.find("selected_weapon"), output.selectedWeapon,
        childPath(path, "selected_weapon"), error)) return false;
  if (const dev::JsonValue* ammo = value.find("ammo"); ammo != nullptr &&
      !parseAmmo(*ammo, output.ammo, childPath(path, "ammo"), error)) return false;
  return true;
}

[[nodiscard]] bool parseInput(
  const dev::JsonValue& value,
  TimelineInput& output,
  std::string_view path,
  std::string& error
) {
  if (value.type != Type::Object) {
    error = std::string(path) + ": must be an object";
    return false;
  }
  if (!rejectUnknown(
        value, {"forward", "right", "up", "jump", "crouch", "dash", "attack",
                "sneak", "zoom", "weapon", "yaw", "pitch"}, path, error)) return false;
  if (const dev::JsonValue* forward = value.find("forward"); forward != nullptr &&
      !numberValue(forward, -1.0, 1.0, output.forward, childPath(path, "forward"), error))
    return false;
  if (const dev::JsonValue* right = value.find("right"); right != nullptr &&
      !numberValue(right, -1.0, 1.0, output.right, childPath(path, "right"), error))
    return false;
  if (const dev::JsonValue* up = value.find("up"); up != nullptr &&
      !numberValue(up, -1.0, 1.0, output.up, childPath(path, "up"), error))
    return false;
  bool* boolOutputs[] = {
    &output.jump, &output.crouch, &output.dash, &output.attack,
    &output.sneak, &output.zoom,
  };
  const std::string_view boolNames[] = {
    "jump", "crouch", "dash", "attack", "sneak", "zoom",
  };
  for (std::size_t index = 0; index < std::size(boolOutputs); ++index) {
    if (const dev::JsonValue* member = value.find(boolNames[index]); member != nullptr &&
        !boolValue(member, *boolOutputs[index], childPath(path, boolNames[index]), error))
      return false;
  }
  if (const dev::JsonValue* weapon = value.find("weapon"); weapon != nullptr) {
    Weapon parsed = {};
    if (!weaponValue(weapon, parsed, childPath(path, "weapon"), error)) return false;
    output.weapon = parsed;
  }
  if (const dev::JsonValue* yaw = value.find("yaw"); yaw != nullptr) {
    float parsed = 0.0F;
    if (!numberValue(
          yaw, -360000.0, 360000.0, parsed,
          childPath(path, "yaw"), error)) return false;
    output.yawDegrees = parsed;
  }
  if (const dev::JsonValue* pitch = value.find("pitch"); pitch != nullptr) {
    float parsed = 0.0F;
    if (!numberValue(
          pitch, -90.0, 90.0, parsed,
          childPath(path, "pitch"), error)) return false;
    output.pitchDegrees = parsed;
  }
  return true;
}

[[nodiscard]] bool parseTimelineEntry(
  const dev::JsonValue& value,
  TimelineEntry& output,
  std::string_view path,
  std::uint32_t maxTicks,
  const std::set<std::size_t>& players,
  std::string& error
) {
  if (value.type != Type::Object) {
    error = std::string(path) + ": must be an object";
    return false;
  }
  if (!rejectUnknown(
        value, {"at_tick", "player", "duration_ticks", "one_tick_edges", "input"},
        path, error)) return false;
  if (!integerValue(
        value.find("at_tick"), std::uint32_t{0}, maxTicks - 1U, output.atTick,
        childPath(path, "at_tick"), error) ||
      !integerValue(
        value.find("player"), std::size_t{0}, kMaxPlayers - 1U, output.player,
        childPath(path, "player"), error)) return false;
  if (!players.contains(output.player)) {
    error = childPath(path, "player") +
      ": must name a connected, non-bot scripted player";
    return false;
  }
  if (const dev::JsonValue* duration = value.find("duration_ticks"); duration != nullptr &&
      !integerValue(
        duration, std::uint32_t{1}, maxTicks, output.durationTicks,
        childPath(path, "duration_ticks"), error)) return false;
  if (output.durationTicks > maxTicks - output.atTick) {
    error = childPath(path, "duration_ticks") + ": extends past execution.max_ticks";
    return false;
  }
  if (const dev::JsonValue* edges = value.find("one_tick_edges"); edges != nullptr) {
    if (edges->type != Type::Array) {
      error = childPath(path, "one_tick_edges") + ": must be an array";
      return false;
    }
    std::set<std::string> seen;
    for (std::size_t index = 0; index < edges->array.size(); ++index) {
      const dev::JsonValue& edge = edges->array[index];
      const std::string edgePath = itemPath(childPath(path, "one_tick_edges"), index);
      if (edge.type != Type::String) {
        error = edgePath + ": must be a supported one-tick input edge";
        return false;
      }
      if (!seen.insert(edge.string).second) {
        error = edgePath + ": duplicate edge";
        return false;
      }
      const std::optional<OneTickEdge> parsed = parseOneTickEdge(edge.string);
      if (!parsed) {
        error = edgePath + ": must be jump, crouch, dash, attack, sneak, or zoom";
        return false;
      }
      output.oneTickEdges.push_back(*parsed);
    }
  }
  const dev::JsonValue* input = required(value, "input", Type::Object, path, error);
  return input != nullptr && parseInput(*input, output.input, childPath(path, "input"), error);
}

[[nodiscard]] bool parseSchedule(
  const dev::JsonValue& value,
  ScenarioAssertion& output,
  std::string_view path,
  std::uint32_t maxTicks,
  std::string& error
) {
  const dev::JsonValue* tick = value.find("at_tick");
  const dev::JsonValue* completion = value.find("at_completion");
  if ((tick == nullptr) == (completion == nullptr)) {
    error = std::string(path) + ": needs exactly one of at_tick or at_completion";
    return false;
  }
  if (tick != nullptr) {
    std::uint32_t parsed = 0;
    if (!integerValue(
          tick, std::uint32_t{0}, maxTicks, parsed,
          childPath(path, "at_tick"), error)) return false;
    output.atTick = parsed;
  } else if (!boolValue(
               completion, output.atCompletion,
               childPath(path, "at_completion"), error)) {
    return false;
  } else if (!output.atCompletion) {
    error = childPath(path, "at_completion") + ": only true is valid";
    return false;
  }
  return true;
}

[[nodiscard]] bool assertionPlayer(
  const dev::JsonValue& value,
  std::size_t& player,
  std::string_view path,
  const std::set<std::size_t>& players,
  std::string& error
) {
  if (!integerValue(
        value.find("player"), std::size_t{0}, kMaxPlayers - 1U, player,
        childPath(path, "player"), error)) return false;
  if (!players.contains(player)) {
    error = childPath(path, "player") + ": does not name a configured player";
    return false;
  }
  return true;
}

[[nodiscard]] bool parseEventAssertion(
  const dev::JsonValue& value,
  EventAssertion& output,
  std::string_view path,
  std::string& error
) {
  if (value.type != Type::Object) {
    error = std::string(path) + ": must be an object";
    return false;
  }
  if (!rejectUnknown(
        value, {"type", "actor", "target", "weapon", "damage", "count"},
        path, error) ||
      !stringValue(value.find("type"), output.type, childPath(path, "type"), error))
    return false;
  static const std::set<std::string> supportedTypes = {
    "weapon_fired",
    "projectile_spawned",
    "projectile_impacted",
    "explosion_created",
    "damage_applied",
    "player_killed",
    "player_respawned",
    "score_changed",
    "round_state_changed",
  };
  if (!supportedTypes.contains(output.type)) {
    error = childPath(path, "type") +
      ": unsupported event type '" + output.type + "'";
    return false;
  }
  if (const dev::JsonValue* actor = value.find("actor"); actor != nullptr) {
    std::size_t parsed = 0;
    if (!integerValue(
          actor, std::size_t{0}, kMaxPlayers - 1U, parsed,
          childPath(path, "actor"), error)) return false;
    output.actor = parsed;
  }
  if (const dev::JsonValue* target = value.find("target"); target != nullptr) {
    std::size_t parsed = 0;
    if (!integerValue(
          target, std::size_t{0}, kMaxPlayers - 1U, parsed,
          childPath(path, "target"), error)) return false;
    output.target = parsed;
  }
  if (const dev::JsonValue* weapon = value.find("weapon"); weapon != nullptr) {
    Weapon parsed = {};
    if (!weaponValue(weapon, parsed, childPath(path, "weapon"), error)) return false;
    output.weapon = parsed;
  }
  if (const dev::JsonValue* damage = value.find("damage"); damage != nullptr) {
    int parsed = 0;
    if (!integerValue(
          damage, -1000000, 1000000, parsed,
          childPath(path, "damage"), error)) return false;
    output.damage = parsed;
  }
  if (const dev::JsonValue* count = value.find("count"); count != nullptr) {
    std::uint32_t parsed = 0;
    if (!integerValue(
          count, std::uint32_t{0}, std::uint32_t{1000000}, parsed,
          childPath(path, "count"), error)) return false;
    output.count = parsed;
  }
  return true;
}

[[nodiscard]] bool parseAssertion(
  const dev::JsonValue& value,
  ScenarioAssertion& output,
  std::string_view path,
  std::uint32_t maxTicks,
  const std::set<std::size_t>& players,
  std::string& error
) {
  if (value.type != Type::Object) {
    error = std::string(path) + ": must be an object";
    return false;
  }
  std::string type;
  if (!stringValue(value.find("type"), type, childPath(path, "type"), error) ||
      !parseSchedule(value, output, path, maxTicks, error)) return false;
  if (const dev::JsonValue* classification = value.find("classification");
      classification != nullptr) {
    if (classification->type != Type::String ||
        !parseClassification(classification->string)) {
      error = childPath(path, "classification") +
        ": must be AUTHORITATIVE_DETERMINISTIC, CLIENT_BOUNDED, "
        "RENDERER_ATTESTED, or VISUAL_REVIEW";
      return false;
    }
    output.classification = *parseClassification(classification->string);
  }

  if (type == "player_position" || type == "player_velocity") {
    if (!rejectUnknown(
          value, {"type", "classification", "at_tick", "at_completion",
                  "player", "value", "tolerance"},
          path, error)) return false;
    PlayerVectorAssertion parsed;
    if (!assertionPlayer(value, parsed.player, path, players, error) ||
        !vectorValue(
          value.find("value"), parsed.value,
          type == "player_position" ? kMaxCoordinate : kMaxVelocity,
          childPath(path, "value"), error) ||
        !numberValue(
          value.find("tolerance"), 0.0, kMaxCoordinate, parsed.tolerance,
          childPath(path, "tolerance"), error)) return false;
    output.type = type == "player_position"
      ? AssertionType::PlayerPosition : AssertionType::PlayerVelocity;
    output.payload = parsed;
  } else if (type == "player_health") {
    if (!rejectUnknown(
          value, {"type", "classification", "at_tick", "at_completion",
                  "player", "health"},
          path, error)) return false;
    PlayerHealthAssertion parsed;
    if (!assertionPlayer(value, parsed.player, path, players, error) ||
        !integerValue(
          value.find("health"), 0, 1000000, parsed.health,
          childPath(path, "health"), error)) return false;
    output.type = AssertionType::PlayerHealth;
    output.payload = parsed;
  } else if (type == "player_alive") {
    if (!rejectUnknown(
          value, {"type", "classification", "at_tick", "at_completion",
                  "player", "alive"},
          path, error)) return false;
    PlayerAliveAssertion parsed;
    if (!assertionPlayer(value, parsed.player, path, players, error) ||
        !boolValue(
          value.find("alive"), parsed.alive, childPath(path, "alive"), error))
      return false;
    output.type = AssertionType::PlayerAlive;
    output.payload = parsed;
  } else if (type == "player_weapon") {
    if (!rejectUnknown(
          value, {"type", "classification", "at_tick", "at_completion",
                  "player", "weapon"},
          path, error)) return false;
    PlayerWeaponAssertion parsed;
    if (!assertionPlayer(value, parsed.player, path, players, error) ||
        !weaponValue(
          value.find("weapon"), parsed.weapon, childPath(path, "weapon"), error))
      return false;
    output.type = AssertionType::PlayerWeapon;
    output.payload = parsed;
  } else if (type == "projectile_exists" || type == "projectile_removed") {
    if (!rejectUnknown(
          value, {"type", "classification", "at_tick", "at_completion",
                  "owner", "weapon"},
          path, error)) return false;
    ProjectileAssertion parsed;
    if (const dev::JsonValue* owner = value.find("owner"); owner != nullptr) {
      std::size_t index = 0;
      if (!integerValue(
            owner, std::size_t{0}, kMaxPlayers - 1U, index,
            childPath(path, "owner"), error)) return false;
      if (!players.contains(index)) {
        error = childPath(path, "owner") + ": does not name a configured player";
        return false;
      }
      parsed.owner = index;
    }
    if (const dev::JsonValue* weapon = value.find("weapon"); weapon != nullptr) {
      Weapon parsedWeapon = {};
      if (!weaponValue(
            weapon, parsedWeapon, childPath(path, "weapon"), error)) return false;
      parsed.weapon = parsedWeapon;
    }
    output.type = type == "projectile_exists"
      ? AssertionType::ProjectileExists : AssertionType::ProjectileRemoved;
    output.payload = parsed;
  } else if (type == "event") {
    if (!rejectUnknown(
          value, {"type", "classification", "at_tick", "at_completion", "event"},
          path, error))
      return false;
    const dev::JsonValue* event = required(value, "event", Type::Object, path, error);
    EventAssertion parsed;
    if (event == nullptr ||
        !parseEventAssertion(*event, parsed, childPath(path, "event"), error))
      return false;
    if (parsed.actor && !players.contains(*parsed.actor)) {
      error = childPath(childPath(path, "event"), "actor") +
        ": does not name a configured player";
      return false;
    }
    if (parsed.target && !players.contains(*parsed.target)) {
      error = childPath(childPath(path, "event"), "target") +
        ": does not name a configured player";
      return false;
    }
    output.type = AssertionType::Event;
    output.payload = std::move(parsed);
  } else if (type == "state_hash") {
    if (!rejectUnknown(
          value, {"type", "classification", "at_tick", "at_completion", "hash"},
          path, error))
      return false;
    StateHashAssertion parsed;
    if (!stringValue(
          value.find("hash"), parsed.hash, childPath(path, "hash"), error))
      return false;
    if (parsed.hash.size() > 128U ||
        !std::all_of(parsed.hash.begin(), parsed.hash.end(), [](unsigned char c) {
          return std::isxdigit(c) != 0;
        })) {
      error = childPath(path, "hash") + ": must be 1 to 128 hexadecimal digits";
      return false;
    }
    output.type = AssertionType::StateHash;
    output.payload = std::move(parsed);
  } else if (type == "command_acknowledged") {
    if (!rejectUnknown(
          value, {"type", "classification", "at_tick", "at_completion",
                  "timeline_index", "max_ticks"}, path, error)) return false;
    CommandAcknowledgedAssertion parsed;
    if (!integerValue(
          value.find("timeline_index"), std::size_t{0}, std::size_t{99999},
          parsed.timelineIndex, childPath(path, "timeline_index"), error) ||
        !integerValue(
          value.find("max_ticks"), std::uint32_t{0}, maxTicks, parsed.maxTicks,
          childPath(path, "max_ticks"), error)) return false;
    output.type = AssertionType::CommandAcknowledged;
    output.payload = parsed;
  } else if (type == "input_edge_count") {
    if (!rejectUnknown(
          value, {"type", "classification", "at_tick", "at_completion",
                  "edge", "count"}, path, error)) return false;
    const dev::JsonValue* edge = value.find("edge");
    InputEdgeCountAssertion parsed;
    if (edge == nullptr || edge->type != Type::String ||
        !parseOneTickEdge(edge->string)) {
      error = childPath(path, "edge") +
        ": must be jump, crouch, dash, attack, sneak, or zoom";
      return false;
    }
    parsed.edge = *parseOneTickEdge(edge->string);
    if (!integerValue(
          value.find("count"), std::uint32_t{0}, std::uint32_t{1000000},
          parsed.count, childPath(path, "count"), error)) return false;
    output.type = AssertionType::InputEdgeCount;
    output.payload = parsed;
  } else if (type == "client_pending_commands_max") {
    if (!rejectUnknown(
          value, {"type", "classification", "at_tick", "at_completion", "max"},
          path, error)) return false;
    ClientPendingCommandsMaxAssertion parsed;
    if (!integerValue(
          value.find("max"), std::uint32_t{0}, kMaxScenarioTicks, parsed.max,
          childPath(path, "max"), error)) return false;
    output.type = AssertionType::ClientPendingCommandsMax;
    output.payload = parsed;
  } else if (type == "client_correction_magnitude_max") {
    if (!rejectUnknown(
          value, {"type", "classification", "at_tick", "at_completion", "max"},
          path, error)) return false;
    ClientCorrectionMagnitudeMaxAssertion parsed;
    if (!numberValue(
          value.find("max"), 0.0, kMaxCoordinate, parsed.max,
          childPath(path, "max"), error)) return false;
    output.type = AssertionType::ClientCorrectionMagnitudeMax;
    output.payload = parsed;
  } else if (type == "client_correction_count") {
    if (!rejectUnknown(
          value, {"type", "classification", "at_tick", "at_completion", "min", "max"},
          path, error)) return false;
    ClientCorrectionCountAssertion parsed;
    if (!integerValue(
          value.find("min"), std::uint32_t{0}, kMaxScenarioTicks, parsed.min,
          childPath(path, "min"), error)) return false;
    if (const dev::JsonValue* maximum = value.find("max"); maximum != nullptr) {
      std::uint32_t parsedMax = 0;
      if (!integerValue(
            maximum, parsed.min, kMaxScenarioTicks, parsedMax,
            childPath(path, "max"), error)) return false;
      parsed.max = parsedMax;
    }
    output.type = AssertionType::ClientCorrectionCount;
    output.payload = parsed;
  } else if (type == "client_converged") {
    if (!rejectUnknown(
          value, {"type", "classification", "at_tick", "at_completion",
                  "player", "tolerance", "within_ticks"}, path, error)) return false;
    ClientConvergedAssertion parsed;
    if (!assertionPlayer(value, parsed.player, path, players, error) ||
        !numberValue(
          value.find("tolerance"), 0.0, kMaxCoordinate, parsed.tolerance,
          childPath(path, "tolerance"), error) ||
        !integerValue(
          value.find("within_ticks"), std::uint32_t{0}, maxTicks,
          parsed.withinTicks, childPath(path, "within_ticks"), error)) return false;
    output.type = AssertionType::ClientConverged;
    output.payload = parsed;
  } else if (type == "client_connected") {
    if (!rejectUnknown(
          value, {"type", "classification", "at_tick", "at_completion", "expected"},
          path, error)) return false;
    ClientConnectedAssertion parsed;
    if (!boolValue(
          value.find("expected"), parsed.expected, childPath(path, "expected"), error))
      return false;
    output.type = AssertionType::ClientConnected;
    output.payload = parsed;
  } else if (type == "renderer_backend") {
    if (!rejectUnknown(
          value, {"type", "classification", "at_tick", "at_completion", "backend"},
          path, error)) return false;
    RendererBackendAssertion parsed;
    if (!stringValue(
          value.find("backend"), parsed.backend, childPath(path, "backend"), error))
      return false;
    output.type = AssertionType::RendererBackend;
    output.payload = std::move(parsed);
  } else if (type == "screenshot_checkpoint") {
    if (!rejectUnknown(
          value, {"type", "classification", "at_tick", "at_completion",
                  "capture", "width", "height"}, path, error)) return false;
    ScreenshotCheckpointAssertion parsed;
    if (!stringValue(
          value.find("capture"), parsed.capture, childPath(path, "capture"), error) ||
        !integerValue(
          value.find("width"), std::uint32_t{1}, kMaxScreenshotDimension,
          parsed.width, childPath(path, "width"), error) ||
        !integerValue(
          value.find("height"), std::uint32_t{1}, kMaxScreenshotDimension,
          parsed.height, childPath(path, "height"), error)) return false;
    output.type = AssertionType::ScreenshotCheckpoint;
    output.payload = std::move(parsed);
  } else {
    error = childPath(path, "type") + ": unknown assertion type '" + type + "'";
    return false;
  }
  return true;
}

[[nodiscard]] dev::JsonValue vectorJson(Vec3 value) {
  return dev::JsonValue::arrayValue({
    dev::JsonValue::numberValue(value.x),
    dev::JsonValue::numberValue(value.y),
    dev::JsonValue::numberValue(value.z),
  });
}

[[nodiscard]] bool parseCaptureAfterEvent(
  const dev::JsonValue& value,
  CaptureAfterEvent& output,
  std::string_view path,
  const std::set<std::size_t>& players,
  std::string& error
) {
  if (value.type != Type::Object) {
    error = std::string(path) + ": must be an object";
    return false;
  }
  if (!rejectUnknown(
        value,
        {"type", "actor", "target", "weapon", "occurrence"},
        path,
        error) ||
      !stringValue(value.find("type"), output.type, childPath(path, "type"), error))
    return false;
  static const std::set<std::string> supportedTypes = {
    "weapon_fired",
    "projectile_spawned",
    "projectile_impacted",
    "explosion_created",
    "damage_applied",
    "player_killed",
    "player_respawned",
    "score_changed",
    "round_state_changed",
  };
  if (!supportedTypes.contains(output.type)) {
    error = childPath(path, "type") +
      ": unsupported event type '" + output.type + "'";
    return false;
  }
  auto parsePlayerFilter = [&](std::string_view key, std::optional<std::size_t>& target) {
    const dev::JsonValue* member = value.find(key);
    if (member == nullptr) return true;
    std::size_t parsed = 0;
    if (!integerValue(
          member, std::size_t{0}, kMaxPlayers - 1U, parsed,
          childPath(path, key), error)) return false;
    if (!players.contains(parsed)) {
      error = childPath(path, key) + ": does not name a configured player";
      return false;
    }
    target = parsed;
    return true;
  };
  if (!parsePlayerFilter("actor", output.actor) ||
      !parsePlayerFilter("target", output.target)) return false;
  if (const dev::JsonValue* weapon = value.find("weapon"); weapon != nullptr) {
    Weapon parsed = {};
    if (!weaponValue(weapon, parsed, childPath(path, "weapon"), error)) return false;
    output.weapon = parsed;
  }
  if (const dev::JsonValue* occurrence = value.find("occurrence");
      occurrence != nullptr &&
      !integerValue(
        occurrence,
        std::uint32_t{1},
        std::uint32_t{10000},
        output.occurrence,
        childPath(path, "occurrence"),
        error)) {
    return false;
  }
  return true;
}

[[nodiscard]] bool parseCapture(
  const dev::JsonValue& value,
  ScenarioCapture& output,
  std::string_view path,
  std::uint32_t maxTicks,
  const std::set<std::size_t>& players,
  std::string& error
) {
  if (value.type != Type::Object) {
    error = std::string(path) + ": must be an object";
    return false;
  }
  if (!rejectUnknown(
        value,
        {
          "name",
          "at_server_tick",
          "after_event",
          "surface_impact_weapon",
          "wait_rendered_frames",
          "render_phase",
        },
        path, error) ||
      !stringValue(value.find("name"), output.name, childPath(path, "name"), error))
    return false;
  if (output.name.size() > 128U ||
      !std::all_of(output.name.begin(), output.name.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '_' || c == '-';
      })) {
    error = childPath(path, "name") +
      ": may only use letters, numbers, '_' and '-' (128 bytes max)";
    return false;
  }
  const dev::JsonValue* tick = value.find("at_server_tick");
  const dev::JsonValue* event = value.find("after_event");
  const dev::JsonValue* surfaceWeapon = value.find("surface_impact_weapon");
  const unsigned int triggerCount =
    (tick != nullptr ? 1U : 0U) +
    (event != nullptr ? 1U : 0U) +
    (surfaceWeapon != nullptr ? 1U : 0U);
  if (triggerCount != 1U) {
    error = std::string(path) +
      ": needs exactly one of at_server_tick, after_event, or surface_impact_weapon";
    return false;
  }
  if (tick != nullptr) {
    std::uint32_t parsed = 0;
    if (!integerValue(
          tick, std::uint32_t{0}, maxTicks, parsed,
          childPath(path, "at_server_tick"), error)) return false;
    output.atServerTick = parsed;
  } else if (event != nullptr) {
    if (event->type != Type::Object) {
      error = childPath(path, "after_event") + ": must be an object";
      return false;
    }
    CaptureAfterEvent parsed;
    if (!parseCaptureAfterEvent(
          *event, parsed, childPath(path, "after_event"), players, error))
      return false;
    output.afterEvent = std::move(parsed);
  } else {
    Weapon parsed = {};
    if (!weaponValue(
          surfaceWeapon,
          parsed,
          childPath(path, "surface_impact_weapon"),
          error)) return false;
    if (parsed != Weapon::MachineGun &&
        parsed != Weapon::Shotgun &&
        parsed != Weapon::Railgun &&
        parsed != Weapon::FreezeGun &&
        parsed != Weapon::Revolver) {
      error = childPath(path, "surface_impact_weapon") +
        ": must name machine_gun, shotgun, railgun, freeze_gun, or revolver";
      return false;
    }
    output.surfaceImpactWeapon = parsed;
  }
  if (const dev::JsonValue* wait = value.find("wait_rendered_frames");
      wait != nullptr &&
      !integerValue(
        wait, std::uint32_t{0}, std::uint32_t{600},
        output.waitRenderedFrames, childPath(path, "wait_rendered_frames"), error))
    return false;
  if (const dev::JsonValue* phase = value.find("render_phase"); phase != nullptr) {
    std::string parsed;
    if (!stringValue(
          phase, parsed, childPath(path, "render_phase"), error)) {
      return false;
    }
    static const std::set<std::string, std::less<>> kRenderPhases{
      "before_fire",
      "muzzle",
      "projectile",
      "impact",
      "surface_impact",
    };
    if (!kRenderPhases.contains(parsed)) {
      error = childPath(path, "render_phase") +
        ": must be before_fire, muzzle, projectile, impact, or surface_impact";
      return false;
    }
    output.renderPhase = std::move(parsed);
  }
  const bool surfacePhase =
    output.renderPhase && *output.renderPhase == "surface_impact";
  if (output.surfaceImpactWeapon.has_value() != surfacePhase) {
    error = std::string(path) +
      ": surface_impact_weapon requires render_phase surface_impact";
    return false;
  }
  return true;
}

void putSchedule(dev::JsonValue& value, const ScenarioAssertion& assertion) {
  if (assertion.atTick) {
    value.object["at_tick"] = dev::JsonValue::numberValue(*assertion.atTick);
  } else {
    value.object["at_completion"] = dev::JsonValue::booleanValue(true);
  }
}

} // namespace

ScenarioParseResult parseScenario(const dev::JsonValue& root) {
  if (root.type != Type::Object) {
    return {{}, false, "$: scenario must be an object"};
  }
  std::string error;
  if (!rejectUnknown(
        root, {"schema_version", "name", "description", "execution", "world",
               "network", "players", "timeline", "assertions", "captures",
               "expected_failure"}, "", error))
    return {{}, false, std::move(error)};

  ScenarioDefinition scenario;
  if (!integerValue(
        root.find("schema_version"), kScenarioSchemaVersion, kScenarioSchemaVersion,
        scenario.schemaVersion, "schema_version", error))
    return {{}, false, std::move(error)};
  if (!stringValue(root.find("name"), scenario.name, "name", error))
    return {{}, false, std::move(error)};
  if (scenario.name.size() > 128U ||
      !std::all_of(scenario.name.begin(), scenario.name.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '_' || c == '-';
      })) {
    return {{}, false, "name: may only use letters, numbers, '_' and '-' (128 bytes max)"};
  }
  if (const dev::JsonValue* description = root.find("description");
      description != nullptr &&
      !stringValue(description, scenario.description, "description", error, true))
    return {{}, false, std::move(error)};

  const dev::JsonValue* execution =
    required(root, "execution", Type::Object, "", error);
  if (execution == nullptr || !parseExecution(*execution, scenario.execution, error))
    return {{}, false, std::move(error)};
  if (
    scenario.execution.mode == ScenarioExecutionMode::ClientServer &&
    scenario.execution.maxTicks > kMaxLiveScenarioTicks
  ) {
    return {{}, false, "execution.max_ticks: client_server limit is 2500"};
  }
  if (
    scenario.execution.mode == ScenarioExecutionMode::ClientServer &&
    scenario.execution.repeat != 1U
  ) {
    return {
      {},
      false,
      "execution.repeat: client_server scenarios run once per owned process pair"
    };
  }
  const dev::JsonValue* world = required(root, "world", Type::Object, "", error);
  if (world == nullptr || !parseWorld(*world, scenario.world, error))
    return {{}, false, std::move(error)};
  if (const dev::JsonValue* network = root.find("network"); network != nullptr) {
    if (network->type != Type::Object) {
      return {{}, false, "network: must be an object"};
    }
    ScenarioNetwork parsed;
    if (!parseNetwork(*network, parsed, error))
      return {{}, false, std::move(error)};
    scenario.network = parsed;
  }

  const dev::JsonValue* players = required(root, "players", Type::Array, "", error);
  if (players == nullptr) return {{}, false, std::move(error)};
  if (players->array.empty() || players->array.size() > kMaxPlayers) {
    return {{}, false, "players: must contain 1 to " + std::to_string(kMaxPlayers) + " entries"};
  }
  std::set<std::size_t> playerIndexes;
  std::set<std::size_t> scriptedPlayerIndexes;
  for (std::size_t index = 0; index < players->array.size(); ++index) {
    PlayerInitialState player;
    const std::string path = itemPath("players", index);
    if (!parsePlayer(players->array[index], player, path, error))
      return {{}, false, std::move(error)};
    if (!playerIndexes.insert(player.index).second) {
      return {{}, false, childPath(path, "index") + ": duplicate player index"};
    }
    if (player.connected && !player.bot) {
      scriptedPlayerIndexes.insert(player.index);
    }
    scenario.players.push_back(std::move(player));
  }

  const dev::JsonValue* timeline = required(root, "timeline", Type::Array, "", error);
  if (timeline == nullptr) return {{}, false, std::move(error)};
  if (timeline->array.size() > 100000U)
    return {{}, false, "timeline: must contain at most 100000 entries"};
  for (std::size_t index = 0; index < timeline->array.size(); ++index) {
    TimelineEntry entry;
    if (!parseTimelineEntry(
          timeline->array[index], entry, itemPath("timeline", index),
          scenario.execution.maxTicks, scriptedPlayerIndexes, error))
      return {{}, false, std::move(error)};
    if (
      scenario.execution.mode == ScenarioExecutionMode::ClientServer &&
      entry.durationTicks > kMaxLiveInputTicks
    ) {
      return {
        {},
        false,
        childPath(itemPath("timeline", index), "duration_ticks") +
          ": client_server limit is 1250"
      };
    }
    scenario.timeline.push_back(std::move(entry));
  }
  std::array<std::vector<const TimelineEntry*>, kMaxPlayers> playerTimeline;
  for (const TimelineEntry& entry : scenario.timeline) {
    playerTimeline[entry.player].push_back(&entry);
  }
  for (std::size_t player = 0; player < playerTimeline.size(); ++player) {
    auto& entries = playerTimeline[player];
    std::sort(
      entries.begin(),
      entries.end(),
      [](const TimelineEntry* left, const TimelineEntry* right) {
        return left->atTick < right->atTick;
      }
    );
    for (std::size_t index = 1; index < entries.size(); ++index) {
      const TimelineEntry& previous = *entries[index - 1U];
      if (entries[index]->atTick < previous.atTick + previous.durationTicks) {
        return {
          {},
          false,
          "timeline: overlapping input ranges for player " +
            std::to_string(player)
        };
      }
    }
  }

  const dev::JsonValue* assertions = required(root, "assertions", Type::Array, "", error);
  if (assertions == nullptr) return {{}, false, std::move(error)};
  if (assertions->array.size() > 100000U)
    return {{}, false, "assertions: must contain at most 100000 entries"};
  for (std::size_t index = 0; index < assertions->array.size(); ++index) {
    ScenarioAssertion assertion;
    if (!parseAssertion(
          assertions->array[index], assertion, itemPath("assertions", index),
          scenario.execution.maxTicks, playerIndexes, error))
      return {{}, false, std::move(error)};
    if (
      scenario.execution.mode == ScenarioExecutionMode::ClientServer &&
      assertion.type == AssertionType::ClientConverged
    ) {
      const auto* converged =
        std::get_if<ClientConvergedAssertion>(&assertion.payload);
      if (
        converged == nullptr ||
        !scriptedPlayerIndexes.contains(converged->player)
      ) {
        return {
          {},
          false,
          childPath(itemPath("assertions", index), "player") +
            ": client_server convergence must name the local non-bot player"
        };
      }
    }
    if (scenario.execution.mode == ScenarioExecutionMode::ClientServer &&
        !assertion.classification) {
      return {
        {},
        false,
        childPath(itemPath("assertions", index), "classification") +
          ": required for client_server execution"
      };
    }
    if (scenario.execution.mode == ScenarioExecutionMode::ClientServer) {
      const AssertionClassification classification =
        *assertion.classification;
      const bool rendererAssertion =
        assertion.type == AssertionType::RendererBackend;
      const bool screenshotAssertion =
        assertion.type == AssertionType::ScreenshotCheckpoint;
      const bool clientAssertion =
        assertion.type >= AssertionType::CommandAcknowledged &&
        !rendererAssertion &&
        !screenshotAssertion;
      const bool validClassification =
        (
          rendererAssertion &&
          classification == AssertionClassification::RendererAttested
        ) ||
        (
          screenshotAssertion &&
          classification == AssertionClassification::VisualReview
        ) ||
        (
          clientAssertion &&
          classification == AssertionClassification::ClientBounded
        ) ||
        (
          !clientAssertion &&
          !rendererAssertion &&
          !screenshotAssertion &&
          (
            classification ==
              AssertionClassification::AuthoritativeDeterministic ||
            classification == AssertionClassification::ClientBounded
          )
        );
      if (!validClassification) {
        return {
          {},
          false,
          childPath(itemPath("assertions", index), "classification") +
            ": does not match the assertion source"
        };
      }
      if (assertion.atTick.has_value()) {
        return {
          {},
          false,
          childPath(itemPath("assertions", index), "at_tick") +
            ": client_server assertions use bounded completion checks"
        };
      }
    }
    if (const auto* acknowledged =
          std::get_if<CommandAcknowledgedAssertion>(&assertion.payload);
        acknowledged != nullptr && acknowledged->timelineIndex >= scenario.timeline.size()) {
      return {
        {},
        false,
        childPath(itemPath("assertions", index), "timeline_index") +
          ": does not name a timeline entry"
      };
    }
    scenario.assertions.push_back(std::move(assertion));
  }

  std::set<std::string> captureNames;
  if (const dev::JsonValue* captures = root.find("captures"); captures != nullptr) {
    if (captures->type != Type::Array) {
      return {{}, false, "captures: must be an array"};
    }
    if (captures->array.size() > 10000U) {
      return {{}, false, "captures: must contain at most 10000 entries"};
    }
    for (std::size_t index = 0; index < captures->array.size(); ++index) {
      ScenarioCapture capture;
      const std::string path = itemPath("captures", index);
      if (!parseCapture(
            captures->array[index], capture, path, scenario.execution.maxTicks,
            playerIndexes, error))
        return {{}, false, std::move(error)};
      if (
        scenario.execution.mode == ScenarioExecutionMode::ClientServer &&
        capture.atServerTick
      ) {
        for (const TimelineEntry& entry : scenario.timeline) {
          const std::uint32_t endTick = entry.atTick + entry.durationTicks;
          if (
            *capture.atServerTick > entry.atTick &&
            *capture.atServerTick < endTick
          ) {
            return {
              {},
              false,
              childPath(path, "at_server_tick") +
                ": must not split a live input range"
            };
          }
        }
      }
      if (!captureNames.insert(capture.name).second) {
        return {{}, false, childPath(path, "name") + ": duplicate capture name"};
      }
      scenario.captures.push_back(std::move(capture));
    }
  }
  for (std::size_t index = 0; index < scenario.assertions.size(); ++index) {
    const auto* checkpoint =
      std::get_if<ScreenshotCheckpointAssertion>(&scenario.assertions[index].payload);
    if (checkpoint != nullptr && !captureNames.contains(checkpoint->capture)) {
      return {
        {},
        false,
        childPath(itemPath("assertions", index), "capture") +
          ": does not name a capture"
      };
    }
  }

  if (const dev::JsonValue* expected = root.find("expected_failure"); expected != nullptr) {
    if (expected->type != Type::Object) {
      return {{}, false, "expected_failure: must be an object"};
    }
    if (!rejectUnknown(
          *expected,
          {"issue", "assertion_index", "reason"},
          "expected_failure",
          error))
      return {{}, false, std::move(error)};
    if (scenario.assertions.empty()) {
      return {{}, false, "expected_failure: requires at least one assertion"};
    }
    ExpectedFailure parsed;
    if (!stringValue(
          expected->find("issue"), parsed.issue,
          "expected_failure.issue", error) ||
        !integerValue(
          expected->find("assertion_index"), std::size_t{0},
          scenario.assertions.size() - 1U,
          parsed.assertionIndex,
          "expected_failure.assertion_index",
          error) ||
        !stringValue(
          expected->find("reason"), parsed.reason, "expected_failure.reason", error))
      return {{}, false, std::move(error)};
    scenario.expectedFailure = std::move(parsed);
  }
  return {std::move(scenario), true, {}};
}

ScenarioParseResult parseScenarioJson(std::string_view text) {
  dev::JsonParseResult parsed = dev::parseJson(text);
  if (!parsed.ok) return {{}, false, "JSON: " + parsed.error};
  return parseScenario(parsed.value);
}

ScenarioParseResult loadScenarioFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) return {{}, false, "could not open scenario file '" + path.string() + "'"};
  std::ostringstream text;
  text << input.rdbuf();
  if (!input.good() && !input.eof())
    return {{}, false, "could not read scenario file '" + path.string() + "'"};
  return parseScenarioJson(text.str());
}

std::string_view gameModeName(GameMode mode) {
  switch (mode) {
  case GameMode::Duel: return "duel";
  case GameMode::ClanArena: return "clan_arena";
  case GameMode::McGuffin: return "mcguffin";
  }
  return "unknown";
}

std::string_view teamName(Team team) {
  switch (team) {
  case Team::None: return "none";
  case Team::Red: return "red";
  case Team::Blue: return "blue";
  }
  return "unknown";
}

std::string_view weaponName(Weapon weapon) {
  switch (weapon) {
  case Weapon::MachineGun: return "machine_gun";
  case Weapon::Shotgun: return "shotgun";
  case Weapon::GrenadeLauncher: return "grenade_launcher";
  case Weapon::RocketLauncher: return "rocket_launcher";
  case Weapon::LightningGun: return "lightning_gun";
  case Weapon::Railgun: return "railgun";
  case Weapon::PlasmaGun: return "plasma_gun";
  case Weapon::FreezeGun: return "freeze_gun";
  case Weapon::Revolver: return "revolver";
  }
  return "unknown";
}

std::string_view assertionTypeName(AssertionType type) {
  switch (type) {
  case AssertionType::PlayerPosition: return "player_position";
  case AssertionType::PlayerVelocity: return "player_velocity";
  case AssertionType::PlayerHealth: return "player_health";
  case AssertionType::PlayerAlive: return "player_alive";
  case AssertionType::PlayerWeapon: return "player_weapon";
  case AssertionType::ProjectileExists: return "projectile_exists";
  case AssertionType::ProjectileRemoved: return "projectile_removed";
  case AssertionType::Event: return "event";
  case AssertionType::StateHash: return "state_hash";
  case AssertionType::CommandAcknowledged: return "command_acknowledged";
  case AssertionType::InputEdgeCount: return "input_edge_count";
  case AssertionType::ClientPendingCommandsMax: return "client_pending_commands_max";
  case AssertionType::ClientCorrectionMagnitudeMax:
    return "client_correction_magnitude_max";
  case AssertionType::ClientCorrectionCount: return "client_correction_count";
  case AssertionType::ClientConverged: return "client_converged";
  case AssertionType::ClientConnected: return "client_connected";
  case AssertionType::RendererBackend: return "renderer_backend";
  case AssertionType::ScreenshotCheckpoint: return "screenshot_checkpoint";
  }
  return "unknown";
}

std::string_view assertionClassificationName(AssertionClassification classification) {
  switch (classification) {
  case AssertionClassification::AuthoritativeDeterministic:
    return "AUTHORITATIVE_DETERMINISTIC";
  case AssertionClassification::ClientBounded: return "CLIENT_BOUNDED";
  case AssertionClassification::RendererAttested: return "RENDERER_ATTESTED";
  case AssertionClassification::VisualReview: return "VISUAL_REVIEW";
  }
  return "unknown";
}

dev::JsonValue scenarioJson(const ScenarioDefinition& scenario) {
  dev::JsonValue root = dev::JsonValue::objectValue();
  root.object["schema_version"] = dev::JsonValue::numberValue(scenario.schemaVersion);
  root.object["name"] = dev::JsonValue::stringValue(scenario.name);
  root.object["description"] = dev::JsonValue::stringValue(scenario.description);
  dev::JsonValue execution = dev::JsonValue::objectValue();
  execution.object["mode"] = dev::JsonValue::stringValue(
    scenario.execution.mode == ScenarioExecutionMode::ClientServer
      ? "client_server" : "headless");
  execution.object["max_ticks"] = dev::JsonValue::numberValue(scenario.execution.maxTicks);
  execution.object["repeat"] = dev::JsonValue::numberValue(scenario.execution.repeat);
  root.object["execution"] = std::move(execution);
  dev::JsonValue world = dev::JsonValue::objectValue();
  world.object["map"] = dev::JsonValue::stringValue(scenario.world.map);
  world.object["game_mode"] =
    dev::JsonValue::stringValue(std::string(gameModeName(scenario.world.gameMode)));
  world.object["seed"] = dev::JsonValue::numberValue(scenario.world.seed);
  root.object["world"] = std::move(world);
  if (scenario.network) {
    dev::JsonValue network = dev::JsonValue::objectValue();
    network.object["latency_ms"] =
      dev::JsonValue::numberValue(scenario.network->latencyMs);
    network.object["jitter_ms"] =
      dev::JsonValue::numberValue(scenario.network->jitterMs);
    network.object["packet_loss_percent"] =
      dev::JsonValue::numberValue(scenario.network->packetLossPercent);
    network.object["reorder_percent"] =
      dev::JsonValue::numberValue(scenario.network->reorderPercent);
    network.object["seed"] = dev::JsonValue::numberValue(scenario.network->seed);
    root.object["network"] = std::move(network);
  }

  dev::JsonValue players = dev::JsonValue::arrayValue();
  for (const PlayerInitialState& player : scenario.players) {
    dev::JsonValue value = dev::JsonValue::objectValue();
    value.object["index"] = dev::JsonValue::numberValue(player.index);
    value.object["connected"] = dev::JsonValue::booleanValue(player.connected);
    value.object["bot"] = dev::JsonValue::booleanValue(player.bot);
    value.object["team"] = dev::JsonValue::stringValue(std::string(teamName(player.team)));
    value.object["position"] = vectorJson(player.position);
    value.object["velocity"] = vectorJson(player.velocity);
    value.object["view_yaw_degrees"] = dev::JsonValue::numberValue(player.viewYawDegrees);
    value.object["view_pitch_degrees"] = dev::JsonValue::numberValue(player.viewPitchDegrees);
    value.object["health"] = dev::JsonValue::numberValue(player.health);
    value.object["alive"] = dev::JsonValue::booleanValue(player.alive);
    value.object["selected_weapon"] =
      dev::JsonValue::stringValue(std::string(weaponName(player.selectedWeapon)));
    dev::JsonValue ammo = dev::JsonValue::objectValue();
    for (std::size_t index = 0; index < kWeaponCount; ++index) {
      const Weapon weapon = static_cast<Weapon>(index);
      ammo.object[std::string(weaponName(weapon))] =
        dev::JsonValue::numberValue(player.ammo[index]);
    }
    value.object["ammo"] = std::move(ammo);
    players.array.push_back(std::move(value));
  }
  root.object["players"] = std::move(players);

  dev::JsonValue timeline = dev::JsonValue::arrayValue();
  for (const TimelineEntry& entry : scenario.timeline) {
    dev::JsonValue value = dev::JsonValue::objectValue();
    value.object["at_tick"] = dev::JsonValue::numberValue(entry.atTick);
    value.object["player"] = dev::JsonValue::numberValue(entry.player);
    value.object["duration_ticks"] = dev::JsonValue::numberValue(entry.durationTicks);
    dev::JsonValue edges = dev::JsonValue::arrayValue();
    for (OneTickEdge edge : entry.oneTickEdges) {
      edges.array.push_back(
        dev::JsonValue::stringValue(std::string(oneTickEdgeName(edge))));
    }
    value.object["one_tick_edges"] = std::move(edges);
    dev::JsonValue input = dev::JsonValue::objectValue();
    input.object["forward"] = dev::JsonValue::numberValue(entry.input.forward);
    input.object["right"] = dev::JsonValue::numberValue(entry.input.right);
    if (entry.input.up != 0.0F)
      input.object["up"] = dev::JsonValue::numberValue(entry.input.up);
    input.object["jump"] = dev::JsonValue::booleanValue(entry.input.jump);
    input.object["crouch"] = dev::JsonValue::booleanValue(entry.input.crouch);
    input.object["dash"] = dev::JsonValue::booleanValue(entry.input.dash);
    input.object["attack"] = dev::JsonValue::booleanValue(entry.input.attack);
    if (entry.input.sneak)
      input.object["sneak"] = dev::JsonValue::booleanValue(true);
    if (entry.input.zoom)
      input.object["zoom"] = dev::JsonValue::booleanValue(true);
    if (entry.input.weapon)
      input.object["weapon"] =
        dev::JsonValue::stringValue(std::string(weaponName(*entry.input.weapon)));
    if (entry.input.yawDegrees)
      input.object["yaw"] = dev::JsonValue::numberValue(*entry.input.yawDegrees);
    if (entry.input.pitchDegrees)
      input.object["pitch"] = dev::JsonValue::numberValue(*entry.input.pitchDegrees);
    value.object["input"] = std::move(input);
    timeline.array.push_back(std::move(value));
  }
  root.object["timeline"] = std::move(timeline);

  dev::JsonValue assertions = dev::JsonValue::arrayValue();
  for (const ScenarioAssertion& assertion : scenario.assertions) {
    dev::JsonValue value = dev::JsonValue::objectValue();
    value.object["type"] =
      dev::JsonValue::stringValue(std::string(assertionTypeName(assertion.type)));
    if (assertion.classification) {
      value.object["classification"] = dev::JsonValue::stringValue(
        std::string(assertionClassificationName(*assertion.classification)));
    }
    putSchedule(value, assertion);
    if (const auto* parsed = std::get_if<PlayerVectorAssertion>(&assertion.payload)) {
      value.object["player"] = dev::JsonValue::numberValue(parsed->player);
      value.object["value"] = vectorJson(parsed->value);
      value.object["tolerance"] = dev::JsonValue::numberValue(parsed->tolerance);
    } else if (const auto* parsed = std::get_if<PlayerHealthAssertion>(&assertion.payload)) {
      value.object["player"] = dev::JsonValue::numberValue(parsed->player);
      value.object["health"] = dev::JsonValue::numberValue(parsed->health);
    } else if (const auto* parsed = std::get_if<PlayerAliveAssertion>(&assertion.payload)) {
      value.object["player"] = dev::JsonValue::numberValue(parsed->player);
      value.object["alive"] = dev::JsonValue::booleanValue(parsed->alive);
    } else if (const auto* parsed = std::get_if<PlayerWeaponAssertion>(&assertion.payload)) {
      value.object["player"] = dev::JsonValue::numberValue(parsed->player);
      value.object["weapon"] =
        dev::JsonValue::stringValue(std::string(weaponName(parsed->weapon)));
    } else if (const auto* parsed = std::get_if<ProjectileAssertion>(&assertion.payload)) {
      if (parsed->owner) value.object["owner"] = dev::JsonValue::numberValue(*parsed->owner);
      if (parsed->weapon)
        value.object["weapon"] =
          dev::JsonValue::stringValue(std::string(weaponName(*parsed->weapon)));
    } else if (const auto* parsed = std::get_if<EventAssertion>(&assertion.payload)) {
      dev::JsonValue event = dev::JsonValue::objectValue();
      event.object["type"] = dev::JsonValue::stringValue(parsed->type);
      if (parsed->actor) event.object["actor"] = dev::JsonValue::numberValue(*parsed->actor);
      if (parsed->target) event.object["target"] = dev::JsonValue::numberValue(*parsed->target);
      if (parsed->weapon)
        event.object["weapon"] =
          dev::JsonValue::stringValue(std::string(weaponName(*parsed->weapon)));
      if (parsed->damage) event.object["damage"] = dev::JsonValue::numberValue(*parsed->damage);
      if (parsed->count) event.object["count"] = dev::JsonValue::numberValue(*parsed->count);
      value.object["event"] = std::move(event);
    } else if (const auto* parsed = std::get_if<StateHashAssertion>(&assertion.payload)) {
      value.object["hash"] = dev::JsonValue::stringValue(parsed->hash);
    } else if (const auto* parsed =
                 std::get_if<CommandAcknowledgedAssertion>(&assertion.payload)) {
      value.object["timeline_index"] =
        dev::JsonValue::numberValue(parsed->timelineIndex);
      value.object["max_ticks"] = dev::JsonValue::numberValue(parsed->maxTicks);
    } else if (const auto* parsed =
                 std::get_if<InputEdgeCountAssertion>(&assertion.payload)) {
      value.object["edge"] =
        dev::JsonValue::stringValue(std::string(oneTickEdgeName(parsed->edge)));
      value.object["count"] = dev::JsonValue::numberValue(parsed->count);
    } else if (const auto* parsed =
                 std::get_if<ClientPendingCommandsMaxAssertion>(&assertion.payload)) {
      value.object["max"] = dev::JsonValue::numberValue(parsed->max);
    } else if (const auto* parsed =
                 std::get_if<ClientCorrectionMagnitudeMaxAssertion>(
                   &assertion.payload)) {
      value.object["max"] = dev::JsonValue::numberValue(parsed->max);
    } else if (const auto* parsed =
                 std::get_if<ClientCorrectionCountAssertion>(&assertion.payload)) {
      value.object["min"] = dev::JsonValue::numberValue(parsed->min);
      if (parsed->max)
        value.object["max"] = dev::JsonValue::numberValue(*parsed->max);
    } else if (const auto* parsed =
                 std::get_if<ClientConvergedAssertion>(&assertion.payload)) {
      value.object["player"] = dev::JsonValue::numberValue(parsed->player);
      value.object["tolerance"] =
        dev::JsonValue::numberValue(parsed->tolerance);
      value.object["within_ticks"] =
        dev::JsonValue::numberValue(parsed->withinTicks);
    } else if (const auto* parsed =
                 std::get_if<ClientConnectedAssertion>(&assertion.payload)) {
      value.object["expected"] =
        dev::JsonValue::booleanValue(parsed->expected);
    } else if (const auto* parsed =
                 std::get_if<RendererBackendAssertion>(&assertion.payload)) {
      value.object["backend"] = dev::JsonValue::stringValue(parsed->backend);
    } else if (const auto* parsed =
                 std::get_if<ScreenshotCheckpointAssertion>(&assertion.payload)) {
      value.object["capture"] = dev::JsonValue::stringValue(parsed->capture);
      value.object["width"] = dev::JsonValue::numberValue(parsed->width);
      value.object["height"] = dev::JsonValue::numberValue(parsed->height);
    }
    assertions.array.push_back(std::move(value));
  }
  root.object["assertions"] = std::move(assertions);
  if (!scenario.captures.empty()) {
    dev::JsonValue captures = dev::JsonValue::arrayValue();
    for (const ScenarioCapture& capture : scenario.captures) {
      dev::JsonValue value = dev::JsonValue::objectValue();
      value.object["name"] = dev::JsonValue::stringValue(capture.name);
      if (capture.atServerTick) {
        value.object["at_server_tick"] =
          dev::JsonValue::numberValue(*capture.atServerTick);
      } else if (capture.afterEvent) {
        dev::JsonValue event = dev::JsonValue::objectValue();
        event.object["type"] =
          dev::JsonValue::stringValue(capture.afterEvent->type);
        if (capture.afterEvent->actor)
          event.object["actor"] =
            dev::JsonValue::numberValue(*capture.afterEvent->actor);
        if (capture.afterEvent->target)
          event.object["target"] =
            dev::JsonValue::numberValue(*capture.afterEvent->target);
        if (capture.afterEvent->weapon)
          event.object["weapon"] = dev::JsonValue::stringValue(
            std::string(weaponName(*capture.afterEvent->weapon)));
        event.object["occurrence"] =
          dev::JsonValue::numberValue(capture.afterEvent->occurrence);
        value.object["after_event"] = std::move(event);
      } else if (capture.surfaceImpactWeapon) {
        value.object["surface_impact_weapon"] = dev::JsonValue::stringValue(
          std::string(weaponName(*capture.surfaceImpactWeapon))
        );
      }
      value.object["wait_rendered_frames"] =
        dev::JsonValue::numberValue(capture.waitRenderedFrames);
      if (capture.renderPhase) {
        value.object["render_phase"] =
          dev::JsonValue::stringValue(*capture.renderPhase);
      }
      captures.array.push_back(std::move(value));
    }
    root.object["captures"] = std::move(captures);
  }
  if (scenario.expectedFailure) {
    dev::JsonValue expected = dev::JsonValue::objectValue();
    expected.object["issue"] =
      dev::JsonValue::stringValue(scenario.expectedFailure->issue);
    expected.object["assertion_index"] =
      dev::JsonValue::numberValue(scenario.expectedFailure->assertionIndex);
    expected.object["reason"] =
      dev::JsonValue::stringValue(scenario.expectedFailure->reason);
    root.object["expected_failure"] = std::move(expected);
  }
  return root;
}

} // namespace lg::scenario
