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
    console.execute("g_knockback_time_ms") ==
      "g_knockback_time_ms = 100 (default 100, Q3/QL default 100)",
    "knockback movement timer cvar should default to the Q3 100 ms value"
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
    console.execute("g_weaponswitching cpma") == "g_weaponswitching = cpma" &&
      console.getString("g_weaponswitching") == "cpma",
    "weapon switching rules should be configurable from the client console"
  );
  failures += expect(
    console.execute("r_damage_numbers_mode") ==
      "r_damage_numbers_mode = 0 (default 0)",
    "damage numbers should default to disabled"
  );
  failures += expect(
    console.execute("r_sg_weapon_model_start 1") ==
        "r_sg_weapon_model_start = 1" &&
      console.getBool("r_sg_weapon_model_start"),
    "shotgun weapon model start should be toggleable"
  );
  failures += expect(
    console.execute("r_player_model") ==
      "r_player_model = 1 (default 1)" &&
      console.execute("r_player_model 0") == "r_player_model = 0" &&
      console.execute("r_player_model 2") == "value out of range for r_player_model",
    "remote player model cvar should select legacy or animated models"
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
  failures += expect(
    !console.getBool("r_perf") &&
      !console.getBool("r_perf_detail") &&
      !console.getBool("r_perf_reset"),
    "performance HUD cvars should default disabled"
  );
  failures += expect(
    console.getBool("r_draw_remote_players") &&
      console.getBool("r_draw_remote_weapons") &&
      console.getBool("r_draw_player_outlines"),
    "render isolation cvars should preserve default remote render categories"
  );
  failures += expect(
    console.execute("r_frustum_cull") ==
      "r_frustum_cull = 1 (default 1)" &&
      console.execute("r_frustum_cull 0") == "r_frustum_cull = 0" &&
      !console.getBool("r_frustum_cull"),
    "remote frustum culling should default on and be toggleable"
  );
  failures += expect(
    console.execute("r_perf 1") == "r_perf = 1" &&
      console.execute("r_perf_detail 1") == "r_perf_detail = 1",
    "renderer performance diagnostics cvars should be toggleable"
  );
  failures += expect(
    console.execute("r_enemy_outline_width 6") == "r_enemy_outline_width = 6" &&
      console.execute("r_enemy_outline_width 7") ==
        "value out of range for r_enemy_outline_width" &&
      console.execute("r_teammate_outline_width 6") ==
        "r_teammate_outline_width = 6" &&
      console.execute("r_teammate_outline_width 7") ==
        "value out of range for r_teammate_outline_width",
    "screen-space outline width cvars should be capped at six final display pixels"
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
  failures += expect(
    console.execute("net_sim_latency_ms") ==
      "net_sim_latency_ms = 0 (default 0)" &&
      console.execute("net_sim_latency_ms 60") == "net_sim_latency_ms = 60",
    "network simulation latency cvar should default to zero and accept one-way delay"
  );
  failures += expect(
    console.execute("net_sim_jitter_ms 5001") ==
      "value out of range for net_sim_jitter_ms",
    "network simulation jitter cvar should reject absurd delay"
  );
  failures += expect(
    console.execute("net_sim_loss_percent 101") ==
      "value out of range for net_sim_loss_percent" &&
      console.execute("net_sim_reorder_percent 2") == "net_sim_reorder_percent = 2",
    "network simulation percent cvars should clamp to the registered range"
  );
  failures += expect(
    console.execute("net_sim_seed 12345") == "net_sim_seed = 12345",
    "network simulation seed should be configurable"
  );

  return failures == 0 ? 0 : 1;
}
