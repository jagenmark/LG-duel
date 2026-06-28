#include "app/ClientCvars.hpp"
#include "console/ConsoleSystem.hpp"

#include <algorithm>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

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
    console.execute("s_lg_fire_volume 0.25") == "s_lg_fire_volume = 0.25" &&
      console.getFloat("s_lg_fire_volume") == 0.25F,
    "lightning gun fire volume should be configurable"
  );
  failures += expect(
    console.execute("s_rl_explosion_volume 0") == "s_rl_explosion_volume = 0" &&
      console.getFloat("s_rl_explosion_volume") == 0.0F,
    "rocket and grenade explosion volume should be mutable down to mute"
  );
  failures += expect(
    console.execute("s_countdown_volume 2") == "value out of range for s_countdown_volume",
    "sound mixer volumes should reject values above full volume"
  );
  const std::vector<std::string> archivedConfig = console.archivedConfigLines();
  failures += expect(
    std::find(
      archivedConfig.begin(),
      archivedConfig.end(),
      "set s_lg_fire_volume 0.25"
    ) == archivedConfig.end(),
    "sound mixer cvars should stay controlled by sound_mixer.cfg rather than client.cfg"
  );

  return failures == 0 ? 0 : 1;
}
