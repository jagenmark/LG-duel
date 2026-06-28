#include "app/ClientCvars.hpp"
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
  lg::registerClientCvars(console);

  failures += expect(
    console.execute("g_lg_knockback") ==
      "g_lg_knockback = 1000 (default 1000, Q3/QL default 1000)",
    "LG knockback cvar should use the g_lg_knockback name"
  );
  failures += expect(
    console.execute("g_lg_knockback 500") == "g_lg_knockback = 500",
    "g_lg_knockback should be configurable"
  );
  failures += expect(
    console.getFloat("g_lg_knockback") == 500.0F,
    "g_lg_knockback should store the configured value"
  );
  failures += expect(
    console.execute("g_lg_knockback 100000") == "g_lg_knockback = 100000",
    "g_lg_knockback should allow the extended upper limit"
  );
  failures += expect(
    console.execute("g_knockback") == "unknown command: g_knockback",
    "legacy ambiguous g_knockback cvar should not be registered"
  );
  failures += expect(
    console.execute("g_lg_damage") == "g_lg_damage = 120 (default 120)",
    "LG damage should default to 120 DPS"
  );
  failures += expect(
    console.execute("g_lg_fire_hz") == "g_lg_fire_hz = 20 (default 20)",
    "LG fire rate should default to 20 Hz"
  );
  failures += expect(
    console.execute("g_lg_fire_hz 40") == "g_lg_fire_hz = 40" &&
      console.getFloat("g_lg_fire_hz") == 40.0F,
    "LG fire rate should be configurable"
  );
  failures += expect(
    console.execute("r_damage_numbers_mode") ==
      "r_damage_numbers_mode = 0 (default 0)",
    "damage numbers should default to disabled"
  );
  failures += expect(
    console.execute("r_damage_numbers_window") ==
      "r_damage_numbers_window = 0.4 (default 0.4)",
    "damage number burst window should default to 0.4 seconds"
  );
  failures += expect(
    console.execute("r_damage_numbers_mode 2") ==
      "r_damage_numbers_mode = 2" &&
      console.getInt("r_damage_numbers_mode") == 2,
    "damage number mode should be configurable"
  );
  failures += expect(
    console.execute("r_damage_numbers_mode 3") ==
      "r_damage_numbers_mode = 3" &&
      console.getInt("r_damage_numbers_mode") == 3,
    "damage number tally-only mode should be configurable"
  );
  failures += expect(
    console.execute("r_damage_numbers_mode 4") ==
      "r_damage_numbers_mode = 4" &&
      console.getInt("r_damage_numbers_mode") == 4,
    "world damage number tally-only mode should be configurable"
  );
  failures += expect(
    console.execute("vid_fullscreen") ==
      "vid_fullscreen = 0 (default 0)",
    "video fullscreen mode should default to windowed"
  );
  failures += expect(
    console.execute("vid_fullscreen 2") == "vid_fullscreen = 2" &&
      console.getInt("vid_fullscreen") == 2,
    "exclusive fullscreen mode should be configurable"
  );
  failures += expect(
    console.execute("vid_fullscreen 3") ==
      "value out of range for vid_fullscreen",
    "fullscreen mode should reject values outside 0-2"
  );
  failures += expect(
    console.execute("r_present_mode 1") == "r_present_mode = 1" &&
      console.getInt("r_present_mode") == 1,
    "present mode should accept mailbox"
  );
  failures += expect(
    console.execute("r_present_mode 3") ==
      "value out of range for r_present_mode",
    "present mode should reject values outside 0-2"
  );
  failures += expect(
    console.execute("r_maxfps 0") == "r_maxfps = 0" &&
      console.execute("r_maxfps -1") == "value out of range for r_maxfps",
    "frame limiter cvar should allow uncapped and reject negative caps"
  );

  return failures == 0 ? 0 : 1;
}
