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

  const std::vector<std::string> matches = console.complete("sens");
  failures += expect(
    matches.size() == 1 && matches[0] == "sensitivity",
    "completion should find cvars"
  );
  const std::vector<std::string> config = console.archivedConfigLines();
  failures += expect(config.size() == 2, "only archived cvars should serialize");
  failures += expect(
    config[1] == "set sensitivity 2.5",
    "archive should serialize current typed values"
  );

  return failures == 0 ? 0 : 1;
}
