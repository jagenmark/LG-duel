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
    console.execute("g_fg_damage") == "g_fg_damage = 120 (default 120)",
    "FG damage should default to 120 DPS"
  );
  failures += expect(
    console.execute("g_infiniteammo") == "g_infiniteammo = 1 (default 1)" &&
      console.execute("g_infiniteammo 0") == "g_infiniteammo = 0" &&
      !console.getBool("g_infiniteammo"),
    "infinite ammo should default on and remain toggleable"
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
    console.execute("g_dash_targetspeed 12") == "g_dash_targetspeed = 12" &&
      console.getFloat("g_dash_targetspeed") == 12.0F &&
      console.execute("g_dash_cooldown 0.7") == "g_dash_cooldown = 0.7" &&
      console.getFloat("g_dash_cooldown") == 0.7F,
    "dash movement cvars should be configurable"
  );
  failures += expect(
    console.execute("r_damage_numbers_mode") ==
      "r_damage_numbers_mode = 0 (default 0)",
    "damage numbers should default to disabled"
  );
  failures += expect(
    console.execute("cl_health_size 20") == "cl_health_size = 20" &&
      console.execute("cl_health_size 20.5") ==
        "value out of range for cl_health_size",
    "health HUD size cvar should allow large values up to twenty"
  );
  failures += expect(
    console.execute("cl_speed_size") ==
        "cl_speed_size = 1.5 (default 1.5)" &&
      console.execute("cl_speed_size 0.25") ==
        "value out of range for cl_speed_size" &&
      console.execute("cl_speed_size 2") == "cl_speed_size = 2",
    "speed HUD size cvar should be separate from health size and bounded"
  );
  failures += expect(
    console.execute("cl_weapon_bar_size") ==
        "cl_weapon_bar_size = 1.75 (default 1.75)" &&
      console.execute("cl_weapon_bar_size 0.25") ==
        "value out of range for cl_weapon_bar_size" &&
      console.execute("cl_weapon_bar_size 2.5") == "cl_weapon_bar_size = 2.5",
    "weapon bar size cvar should scale the left weapon HUD"
  );
  failures += expect(
    console.execute("cl_showfps_size") ==
        "cl_showfps_size = 1.6 (default 1.6)" &&
      console.execute("cl_showfps_size 0.25") ==
        "value out of range for cl_showfps_size" &&
      console.execute("cl_showfps_size 2") == "cl_showfps_size = 2",
    "HUD FPS counter size cvar should be configurable"
  );
  failures += expect(
    console.execute("cg_ground_debug") ==
        "cg_ground_debug = 0 (default 0)" &&
      console.execute("cg_ground_debug 3") ==
        "value out of range for cg_ground_debug" &&
      console.execute("cg_ground_debug 2") == "cg_ground_debug = 2" &&
      console.getInt("cg_ground_debug") == 2,
    "ground debug HUD cvar should expose off, summary, and detailed modes"
  );
  failures += expect(
    console.execute("cl_health_style") ==
        "cl_health_style = 0 (default 0)" &&
      console.execute("cl_health_style 3") ==
        "value out of range for cl_health_style" &&
      console.execute("cl_health_style 2") == "cl_health_style = 2",
    "health HUD style cvar should expose the three supported layouts"
  );
  failures += expect(
    console.execute("crosshair_style") ==
        "crosshair_style = 0 (default 0)" &&
      console.execute("crosshair_style 4") ==
        "value out of range for crosshair_style" &&
      console.execute("crosshair_style 3") == "crosshair_style = 3",
    "crosshair style cvar should include the ring style"
  );
  failures += expect(
    console.execute("crosshair_width") ==
        "crosshair_width = 2 (default 2)" &&
      console.execute("crosshair_width 0.5") ==
        "value out of range for crosshair_width" &&
      console.execute("crosshair_width 4") ==
        "crosshair_width = 4",
    "crosshair width should be configurable"
  );
  failures += expect(
    console.execute("crosshair_dot") ==
        "crosshair_dot = 0 (default 0)" &&
      console.execute("crosshair_dot 1") ==
        "crosshair_dot = 1" &&
      console.getBool("crosshair_dot"),
    "crosshair dot should be independently toggleable"
  );
  failures += expect(
    console.execute("crosshair_dot_width") ==
        "crosshair_dot_width = 2 (default 2)" &&
      console.execute("crosshair_dot_width 0.5") ==
        "value out of range for crosshair_dot_width" &&
      console.execute("crosshair_dot_width 5") ==
        "crosshair_dot_width = 5",
    "crosshair dot width should be configurable"
  );
  failures += expect(
    console.execute("crosshair_outline") ==
        "crosshair_outline = 0 (default 0)" &&
      console.execute("crosshair_outline 1") ==
        "crosshair_outline = 1" &&
      console.getBool("crosshair_outline"),
    "crosshair outline should be independently toggleable"
  );
  failures += expect(
    console.execute("crosshair_outline_width") ==
        "crosshair_outline_width = 1 (default 1)" &&
      console.execute("crosshair_outline_width -1") ==
        "value out of range for crosshair_outline_width" &&
      console.execute("crosshair_outline_width 3") ==
        "crosshair_outline_width = 3",
    "crosshair outline width should be configurable"
  );
  failures += expect(
    console.execute("r_hitmarker_width") ==
        "r_hitmarker_width = 2 (default 2)" &&
      console.execute("r_hitmarker_width 0.5") ==
        "value out of range for r_hitmarker_width" &&
      console.execute("r_hitmarker_width 4") ==
        "r_hitmarker_width = 4",
    "hitmarker width should be configurable"
  );
  failures += expect(
    console.execute("r_sg_weapon_model_start 1") ==
        "r_sg_weapon_model_start = 1" &&
      console.getBool("r_sg_weapon_model_start"),
    "shotgun weapon model start should be toggleable"
  );
  failures += expect(
    console.execute("r_show_weapons") == "r_show_weapons = 1 (default 1)" &&
      console.execute("r_show_weapons 0") == "r_show_weapons = 0" &&
      !console.getBool("r_show_weapons"),
    "local first-person weapon rendering should be toggleable"
  );
  failures += expect(
    console.execute("r_player_model") ==
      "r_player_model = 0 (default 0)" &&
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
    console.execute("r_texture_filter") ==
        "r_texture_filter = 2 (default 2)" &&
      console.execute("r_texture_filter 0") == "r_texture_filter = 0" &&
      console.execute("r_texture_filter 3") ==
        "value out of range for r_texture_filter",
    "world texture filter cvar should expose nearest, bilinear, and trilinear modes"
  );
  failures += expect(
    console.execute("r_texture_anisotropy") ==
        "r_texture_anisotropy = 8 (default 8)" &&
      console.execute("r_texture_anisotropy 16") ==
        "r_texture_anisotropy = 16" &&
      console.execute("r_texture_anisotropy 0") ==
        "value out of range for r_texture_anisotropy",
    "world texture anisotropy cvar should default to eight and allow sixteen"
  );
  failures += expect(
    console.execute("r_texture_lod_bias") ==
        "r_texture_lod_bias = 0.5 (default 0.5)" &&
      console.execute("r_texture_lod_bias 1") ==
        "r_texture_lod_bias = 1" &&
      console.execute("r_texture_lod_bias 5") ==
        "value out of range for r_texture_lod_bias",
    "world texture LOD bias cvar should expose conservative mip bias tuning"
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
