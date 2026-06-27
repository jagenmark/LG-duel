#include "console/ConsoleSystem.hpp"

#include <iostream>
#include <string_view>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) {
    return 0;
  }
  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

} // namespace

int main() {
  int failures = 0;
  lg::ConsoleSystem console;
  failures += expect(
    console.registerCvar({
      "sensitivity",
      "Mouse sensitivity.",
      1.0F,
      lg::CvarFlag::Archive | lg::CvarFlag::Client,
      0.1F,
      10.0F,
      "1",
    }),
    "float cvar should register"
  );
  failures += expect(
    console.registerCvar({
      "crosshair_enable",
      "Draw the crosshair.",
      true,
      lg::CvarFlag::Archive | lg::CvarFlag::Client,
      {},
      {},
    }),
    "bool cvar should register"
  );
  failures += expect(
    console.registerCvar({
      "cl_player_name",
      "Player name.",
      std::string{},
      lg::CvarFlag::Archive | lg::CvarFlag::Client,
      {},
      {},
    }),
    "archived string cvar should register"
  );
  failures += expect(
    console.registerCvar({
      "version",
      "Build version.",
      std::string("test"),
      lg::CvarFlag::ReadOnly,
      {},
      {},
    }),
    "string cvar should register"
  );
  failures += expect(
    console.registerCvar({
      "cl_show_lagcomp",
      "Show lag compensation traces.",
      false,
      lg::CvarFlag::Client,
      {},
      {},
    }),
    "lag compensation cvar should register"
  );
  failures += expect(
    console.registerCvar({
      "r_damage_numbers_color",
      "Damage number text color.",
      std::string("white"),
      lg::CvarFlag::Client,
      {},
      {},
    }),
    "damage number color cvar should register"
  );
  failures += expect(
    console.registerCvar({
      "r_damage_numbers_size",
      "Damage number text size.",
      1.0F,
      lg::CvarFlag::Client,
      0.5F,
      4.0F,
      {},
    }),
    "damage number size cvar should register"
  );
  failures += expect(
    console.registerCommand(
      "echo",
      "Echo arguments.",
      [](const std::vector<std::string>& arguments) {
        return arguments.size() > 1 ? arguments[1] : std::string{};
      }
    ),
    "command should register"
  );
  failures += expect(
    console.registerCommand(
      "net_lag_graph",
      "Show lag graph.",
      [](const std::vector<std::string>&) { return std::string{}; }
    ),
    "lag graph command should register"
  );

  failures += expect(
    console.execute("sensitivity 2.5") == "sensitivity = 2.5",
    "direct cvar assignment should work"
  );
  failures += expect(
    console.execute("sensitivity") ==
      "sensitivity = 2.5 (default 1, Q3/QL default 1)",
    "cvar query should include current, project default, and reference default"
  );
  failures += expect(console.getFloat("sensitivity") == 2.5F, "float value should update");
  failures += expect(
    console.execute("sensitivity 20") == "value out of range for sensitivity",
    "range validation should reject invalid values"
  );
  failures += expect(
    console.execute("toggle crosshair_enable") == "crosshair_enable = 0",
    "toggle should invert bool cvars"
  );
  failures += expect(!console.getBool("crosshair_enable"), "bool value should update");
  failures += expect(
    console.execute("version changed") == "version is read-only",
    "read-only cvars should reject assignment"
  );
  failures += expect(console.execute("echo hello") == "hello", "commands should execute");
  failures += expect(
    console.execute("cl_player_name \"Zap Witch\"") == "cl_player_name = Zap Witch",
    "quoted string cvar assignment should keep spaces"
  );

  const std::vector<std::string> matches = console.complete("sens");
  failures += expect(
    matches.size() == 1 && matches[0] == "sensitivity",
    "completion should find cvars"
  );
  const std::vector<std::string> substringMatches = console.complete("lag");
  failures += expect(
    substringMatches.size() == 2 &&
      substringMatches[0] == "cl_show_lagcomp" &&
      substringMatches[1] == "net_lag_graph",
    "completion should fall back to substring search"
  );
  const std::vector<std::string> builtInSubstringMatches = console.complete("var");
  failures += expect(
    builtInSubstringMatches.size() == 1 && builtInSubstringMatches[0] == "cvarlist",
    "completion substring fallback should include built-ins"
  );
  const std::vector<std::string> prefixMatches = console.complete("set");
  failures += expect(
    prefixMatches.size() == 1 && prefixMatches[0] == "set",
    "completion should prefer prefix matches over substring matches"
  );
  const std::vector<std::string> commonPrefixMatches = console.complete("r_damage_num");
  failures += expect(
    commonPrefixMatches.size() == 1 && commonPrefixMatches[0] == "r_damage_numbers_",
    "completion should advance ambiguous matches to their shared longer prefix"
  );
  const std::vector<std::string> config = console.archivedConfigLines();
  failures += expect(config.size() == 3, "only archived cvars should serialize");
  failures += expect(
    config[0] == "set cl_player_name \"Zap Witch\"",
    "archive should quote string cvars so player names persist"
  );
  failures += expect(
    config[2] == "set sensitivity 2.5",
    "archive should serialize current typed values"
  );

  return failures == 0 ? 0 : 1;
}
