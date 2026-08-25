#include "app/ClientCvars.hpp"
#include "app/GraphicsProfiles.hpp"
#include "console/ConsoleSystem.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) {
    return 0;
  }
  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

std::unordered_map<std::string, std::string> defaultClientConfigValues() {
  std::ifstream file(
    std::string(LG_DUEL_SOURCE_DIR) + "/config/default_client.cfg"
  );
  std::unordered_map<std::string, std::string> values;
  std::string line;
  while (std::getline(file, line)) {
    std::istringstream input(line);
    std::string command;
    std::string name;
    std::string value;
    if (input >> command >> name >> value && command == "set") {
      values.emplace(std::move(name), std::move(value));
    }
  }
  return values;
}

} // namespace

int main() {
  int failures = 0;
  lg::ConsoleSystem console;
  lg::registerClientCvars(console);

  const std::vector<std::string> initialArchivedConfig =
    console.archivedConfigLines();
  const auto profileValue = [](
                              const lg::GraphicsProfileDefinition& profile,
                              std::string_view cvar
                            ) {
    const auto value = std::find_if(
      profile.values.begin(),
      profile.values.end(),
      [cvar](const lg::GraphicsProfileValue& entry) {
        return entry.cvar == cvar;
      }
    );
    return value == profile.values.end() ? std::string_view{} : value->value;
  };
  const lg::GraphicsProfileDefinition& defaultProfile =
    lg::graphicsProfileDefinition(lg::GraphicsProfile::Default);
  const auto defaultConfig = defaultClientConfigValues();
  bool defaultConfigMatchesProfile = !defaultConfig.empty();
  bool codeDefaultsMatchProfile = true;
  for (const lg::GraphicsProfileValue& value : defaultProfile.values) {
    const auto configured = defaultConfig.find(std::string(value.cvar));
    defaultConfigMatchesProfile = defaultConfigMatchesProfile &&
      configured != defaultConfig.end() && configured->second == value.value;
    const std::string archived = "set " + std::string(value.cvar) + " " +
      std::string(value.value);
    codeDefaultsMatchProfile = codeDefaultsMatchProfile &&
      std::find(initialArchivedConfig.begin(), initialArchivedConfig.end(), archived) !=
        initialArchivedConfig.end();
  }
  failures += expect(
    defaultConfigMatchesProfile && codeDefaultsMatchProfile,
    "fresh config and registered graphics defaults should exactly match Default"
  );
  failures += expect(
    console.execute("cl_late_mouse_sample") ==
        "cl_late_mouse_sample = 1 (default 1)" &&
      console.getBool("cl_late_mouse_sample") &&
      std::find(
        initialArchivedConfig.begin(),
        initialArchivedConfig.end(),
        "set cl_late_mouse_sample 1"
      ) != initialArchivedConfig.end(),
    "late mouse sampling should default on and persist in client config"
  );
  failures += expect(
    console.getFloat("r_display_gamma") == 1.0F &&
      std::find(
        initialArchivedConfig.begin(),
        initialArchivedConfig.end(),
        "set r_display_gamma 1"
      ) != initialArchivedConfig.end() &&
      console.execute("r_display_gamma 0.5") ==
        "r_display_gamma = 0.5" &&
      console.execute("r_display_gamma 1.5") ==
        "r_display_gamma = 1.5" &&
      console.execute("r_display_gamma 0.49") ==
        "value out of range for r_display_gamma" &&
      console.execute("r_display_gamma 1.51") ==
        "value out of range for r_display_gamma" &&
      console.execute("r_display_gamma 1.25") ==
        "r_display_gamma = 1.25",
    "display gamma should default neutral and enforce its endpoints"
  );
  failures += expect(
    console.getBool("r_damage_indicator") &&
      console.getFloat("r_damage_indicator_distance") == 24.0F &&
      std::find(
        initialArchivedConfig.begin(),
        initialArchivedConfig.end(),
        "set r_damage_indicator 1"
      ) != initialArchivedConfig.end() &&
      std::find(
        initialArchivedConfig.begin(),
        initialArchivedConfig.end(),
        "set r_damage_indicator_distance 24"
      ) != initialArchivedConfig.end(),
    "screen-edge damage warning should default on, hug the border, and persist"
  );
  const std::vector<std::string> displayGammaArchivedConfig =
    console.archivedConfigLines();
  failures += expect(
    std::find(
      displayGammaArchivedConfig.begin(),
      displayGammaArchivedConfig.end(),
      "set r_display_gamma 1.25"
    ) != displayGammaArchivedConfig.end(),
    "display gamma should persist through archived client config"
  );

  for (const lg::GraphicsProfileDefinition& profile : lg::kGraphicsProfiles) {
    const auto hasValue = [&](std::string_view cvar) {
      return std::any_of(
        profile.values.begin(),
        profile.values.end(),
        [cvar](const lg::GraphicsProfileValue& value) {
          return value.cvar == cvar;
        }
      );
    };
    failures += expect(
      hasValue("r_combat_effects") &&
        hasValue("r_tonemap_exposure") &&
        hasValue("r_atmosphere_grade") &&
        hasValue("r_bloom") &&
        hasValue("r_bloom_intensity") &&
        hasValue("r_antialiasing") &&
        hasValue("r_sun_shadows") &&
        hasValue("r_point_lights") &&
        hasValue("r_point_shadows") &&
        hasValue("r_contact_shadows") &&
        hasValue("r_material_quality") &&
        hasValue("r_ambient_grounding") &&
        hasValue("r_player_rim") &&
        hasValue("r_casings") &&
        hasValue("r_impact_particles") &&
        hasValue("r_decals_max"),
      "each F10 graphics profile should define every high-level visual quality setting"
    );
  }
  failures += expect(
    profileValue(lg::kGraphicsProfiles[0], "r_atmosphere_grade") == "1" &&
      profileValue(lg::kGraphicsProfiles[1], "r_atmosphere_grade") == "2" &&
      profileValue(lg::kGraphicsProfiles[2], "r_atmosphere_grade") == "0" &&
      profileValue(lg::kGraphicsProfiles[3], "r_atmosphere_grade") == "3",
    "low, default, competitive, and high profiles should map atmosphere to low, default, off, and high"
  );

  failures += expect(
    profileValue(lg::kGraphicsProfiles[0], "r_antialiasing") == "0" &&
      profileValue(lg::kGraphicsProfiles[1], "r_antialiasing") == "1" &&
      profileValue(lg::kGraphicsProfiles[2], "r_antialiasing") == "1" &&
      profileValue(lg::kGraphicsProfiles[3], "r_antialiasing") == "2" &&
      profileValue(lg::kGraphicsProfiles[0], "r_sun_shadows") == "0" &&
      profileValue(lg::kGraphicsProfiles[1], "r_sun_shadows") == "2" &&
      profileValue(lg::kGraphicsProfiles[2], "r_sun_shadows") == "1" &&
      profileValue(lg::kGraphicsProfiles[3], "r_sun_shadows") == "2" &&
      profileValue(lg::kGraphicsProfiles[0], "r_point_lights") == "1" &&
      profileValue(lg::kGraphicsProfiles[1], "r_point_lights") == "1" &&
      profileValue(lg::kGraphicsProfiles[2], "r_point_lights") == "0" &&
      profileValue(lg::kGraphicsProfiles[3], "r_point_lights") == "2" &&
      profileValue(lg::kGraphicsProfiles[0], "r_point_shadows") == "0" &&
      profileValue(lg::kGraphicsProfiles[1], "r_point_shadows") == "1" &&
      profileValue(lg::kGraphicsProfiles[2], "r_point_shadows") == "0" &&
      profileValue(lg::kGraphicsProfiles[3], "r_point_shadows") == "2" &&
      profileValue(lg::kGraphicsProfiles[0], "r_contact_shadows") == "1" &&
      profileValue(lg::kGraphicsProfiles[1], "r_contact_shadows") == "1" &&
      profileValue(lg::kGraphicsProfiles[2], "r_contact_shadows") == "0" &&
      profileValue(lg::kGraphicsProfiles[3], "r_contact_shadows") == "1" &&
      profileValue(lg::kGraphicsProfiles[0], "r_material_quality") == "0" &&
      profileValue(lg::kGraphicsProfiles[1], "r_material_quality") == "1" &&
      profileValue(lg::kGraphicsProfiles[2], "r_material_quality") == "0" &&
      profileValue(lg::kGraphicsProfiles[3], "r_material_quality") == "2" &&
      profileValue(lg::kGraphicsProfiles[0], "r_ambient_grounding") == "0" &&
      profileValue(lg::kGraphicsProfiles[1], "r_ambient_grounding") == "2" &&
      profileValue(lg::kGraphicsProfiles[2], "r_ambient_grounding") == "1" &&
      profileValue(lg::kGraphicsProfiles[3], "r_ambient_grounding") == "2" &&
      profileValue(lg::kGraphicsProfiles[0], "r_player_rim") == "0" &&
      profileValue(lg::kGraphicsProfiles[1], "r_player_rim") == "1" &&
      profileValue(lg::kGraphicsProfiles[2], "r_player_rim") == "1" &&
      profileValue(lg::kGraphicsProfiles[3], "r_player_rim") == "2" &&
      profileValue(lg::kGraphicsProfiles[0], "r_draw_player_outlines") == "0" &&
      profileValue(lg::kGraphicsProfiles[0], "r_player_outline_mode") == "0" &&
      profileValue(lg::kGraphicsProfiles[0], "r_player_outline_style") == "0" &&
      profileValue(lg::kGraphicsProfiles[1], "r_bloom") == "1" &&
      profileValue(lg::kGraphicsProfiles[2], "r_bloom") == "0" &&
      profileValue(lg::kGraphicsProfiles[1], "r_draw_player_outlines") == "1" &&
      profileValue(lg::kGraphicsProfiles[1], "r_player_outline_mode") == "2" &&
      profileValue(lg::kGraphicsProfiles[2], "r_draw_player_outlines") == "1" &&
      profileValue(lg::kGraphicsProfiles[2], "r_player_outline_mode") == "2",
    "profiles should map budgeted effects while keeping default and competitive readability"
  );

  failures += expect(
    lg::graphicsProfileName(lg::GraphicsProfile::Default) == "Default" &&
      lg::graphicsProfileName(static_cast<lg::GraphicsProfile>(-1)) ==
        "Default" &&
      lg::graphicsProfileDefinition(static_cast<lg::GraphicsProfile>(4)).profile ==
        lg::GraphicsProfile::Default,
    "invalid graphics profile values should fall back to Default"
  );

  failures += expect(
    console.getInt("r_antialiasing") == 1 &&
      console.getInt("r_sun_shadows") == 2 &&
      console.getInt("r_point_lights") == 1 &&
      console.getInt("r_point_shadows") == 1 &&
      console.getBool("r_contact_shadows") &&
      console.getInt("r_material_quality") == 1 &&
      console.getInt("r_ambient_grounding") == 2 &&
      console.getInt("r_ambient_debug") == 0 &&
      console.getInt("r_player_rim") == 1 &&
      console.execute("r_antialiasing 3") ==
        "value out of range for r_antialiasing" &&
      console.execute("r_sun_shadows -1") ==
        "value out of range for r_sun_shadows" &&
      console.execute("r_point_lights 3") ==
        "value out of range for r_point_lights" &&
      console.execute("r_point_shadows -1") ==
        "value out of range for r_point_shadows" &&
      console.execute("r_material_quality 3") ==
        "value out of range for r_material_quality" &&
      console.execute("r_ambient_grounding 3") ==
        "value out of range for r_ambient_grounding" &&
      console.execute("r_ambient_debug -1") ==
        "value out of range for r_ambient_debug" &&
      console.execute("r_player_rim 3") ==
        "value out of range for r_player_rim",
    "new graphics cvars should expose saved defaults and reject bad quality values"
  );
  const std::vector<std::string> graphicsArchivedConfig =
    console.archivedConfigLines();
  constexpr std::array<std::string_view, 8> graphicsArchivedLines{{
    "set r_antialiasing 1",
    "set r_sun_shadows 2",
    "set r_point_lights 1",
    "set r_point_shadows 1",
    "set r_contact_shadows 1",
    "set r_material_quality 1",
    "set r_ambient_grounding 2",
    "set r_player_rim 1",
  }};
  failures += expect(
    std::all_of(
      graphicsArchivedLines.begin(),
      graphicsArchivedLines.end(),
      [&](std::string_view line) {
        return std::find(
          graphicsArchivedConfig.begin(),
          graphicsArchivedConfig.end(),
          line
        ) != graphicsArchivedConfig.end();
      }
    ),
    "new graphics cvars should persist through archived client config"
  );

  failures += expect(
    console.getInt("r_atmosphere_grade") == 2 &&
      console.execute("r_atmosphere_grade 0") ==
        "r_atmosphere_grade = 0" &&
      console.execute("r_atmosphere_grade 3") ==
        "r_atmosphere_grade = 3" &&
      console.execute("r_atmosphere_grade 4") ==
        "value out of range for r_atmosphere_grade",
    "atmosphere grade should expose exactly four saved quality values"
  );

  failures += expect(
    console.getBool("cl_show_console_cat") &&
      console.execute("cl_show_console_cat 0") == "cl_show_console_cat = 0" &&
      !console.getBool("cl_show_console_cat") &&
      console.execute("cl_show_console_cat 1") == "cl_show_console_cat = 1" &&
      console.getBool("cl_show_console_cat"),
    "console cat visibility should default on and be an archived client toggle"
  );

  failures += expect(
    console.getBool("cl_interp_adaptive") &&
      console.getFloat("cl_interp_min") == 0.016F &&
      console.getFloat("cl_interp_max") == 0.064F &&
      console.getFloat("cl_interp_extrapolate") == 0.016F &&
      console.execute("cl_interp_extrapolate 0.051") ==
        "value out of range for cl_interp_extrapolate",
    "adaptive interpolation cvars should expose bounded shipped defaults"
  );

  failures += expect(
    console.execute("cl_death_spectate_threshold") ==
        "cl_death_spectate_threshold = 3 (default 3)" &&
      console.execute("cl_death_camera_hold") ==
        "cl_death_camera_hold = 0.5 (default 0.5)" &&
      console.execute("cl_death_desaturation") ==
        "cl_death_desaturation = 1 (default 1)" &&
      console.execute("cl_death_desaturation 1.1") ==
        "value out of range for cl_death_desaturation",
    "death-camera presentation cvars should expose bounded shipped defaults"
  );

  failures += expect(
    console.execute("sensitivity") ==
      "sensitivity = 5 (default 5, Q3/QL default 5)" &&
      console.execute("sensitivity 65.1") == "sensitivity = 65.1" &&
      console.execute("sensitivity 100.5") == "value out of range for sensitivity",
    "sensitivity should use the QL scale and allow migrated legacy values"
  );
  failures += expect(
    console.execute("cl_zoom_fov") ==
        "cl_zoom_fov = 45 (default 45)" &&
      console.execute("cl_zoom_sniper_fov") ==
        "cl_zoom_sniper_fov = 45 (default 45)" &&
      console.execute("cl_zoom_sniper_fov 19.9") ==
        "value out of range for cl_zoom_sniper_fov" &&
      console.execute("cl_zoom_sniper_fov 140.1") ==
        "value out of range for cl_zoom_sniper_fov" &&
      console.execute("cl_zoom_fov 60") == "cl_zoom_fov = 60" &&
      console.execute("cl_zoom_sniper_fov 30") ==
        "cl_zoom_sniper_fov = 30",
    "general and sniper zoom FOV cvars should have separate bounded values"
  );
  failures += expect(
    lg::resolvedZoomFieldOfView(
      90.0F,
      console.getFloat("cl_zoom_fov"),
      console.getFloat("cl_zoom_sniper_fov"),
      false,
      false,
      0.0F
    ) == 90.0F &&
      lg::resolvedZoomFieldOfView(
        90.0F,
        console.getFloat("cl_zoom_fov"),
        20.0F,
        true,
        false,
        0.0F
      ) == 60.0F &&
      lg::resolvedZoomFieldOfView(
        90.0F,
        140.0F,
        console.getFloat("cl_zoom_sniper_fov"),
        true,
        true,
        1.0F
      ) == 30.0F &&
      lg::resolvedZoomFieldOfView(
        90.0F,
        140.0F,
        console.getFloat("cl_zoom_sniper_fov"),
        true,
        true,
        0.5F
      ) == 60.0F,
    "each zoom cvar should affect only its own camera stage"
  );
  const float generalAutoSensitivity =
    lg::zoomSensitivityMultiplier(90.0F, 60.0F, 0.0F);
  const float sniperAutoSensitivity =
    lg::zoomSensitivityMultiplier(90.0F, 30.0F, 0.0F);
  failures += expect(
    std::fabs(generalAutoSensitivity - 0.5773503F) < 0.0001F &&
      std::fabs(sniperAutoSensitivity - 0.2679492F) < 0.0001F &&
      lg::zoomSensitivityMultiplier(90.0F, 60.0F, 0.4F) == 0.4F &&
      lg::zoomSensitivityMultiplier(90.0F, 30.0F, 0.4F) == 0.4F,
    "general and sniper zoom should share auto and manual sensitivity rules"
  );
  const std::vector<std::string> zoomArchivedConfig =
    console.archivedConfigLines();
  failures += expect(
    std::find(
      zoomArchivedConfig.begin(),
      zoomArchivedConfig.end(),
      "set cl_zoom_fov 60"
    ) != zoomArchivedConfig.end() &&
      std::find(
        zoomArchivedConfig.begin(),
        zoomArchivedConfig.end(),
        "set cl_zoom_sniper_fov 30"
      ) != zoomArchivedConfig.end(),
    "both zoom FOV cvars should persist in the client config"
  );
  failures += expect(
    console.execute("cl_mouseAccel") ==
      "cl_mouseAccel = 0 (default 0, Q3/QL default 0)" &&
      console.execute("cl_mouseAccel 0.1") == "cl_mouseAccel = 0.1" &&
      console.getFloat("cl_mouseAccel") == 0.1F,
    "QL mouse acceleration cvar should be registered"
  );
  failures += expect(
    console.execute("cl_mouseAccelPower") ==
      "cl_mouseAccelPower = 2 (default 2, Q3/QL default 2)" &&
      console.execute("cl_mouseAccelPower 0.5") ==
        "value out of range for cl_mouseAccelPower",
    "QL mouse acceleration power should default to two and reject sub-one values"
  );
  failures += expect(
    console.execute("cl_mouseAccelOffset") ==
      "cl_mouseAccelOffset = 0 (default 0, Q3/QL default 0)" &&
      console.execute("cl_mouseAccelOffset 5") == "cl_mouseAccelOffset = 5",
    "QL mouse acceleration offset should be configurable"
  );
  failures += expect(
    console.execute("cl_mouseSensCap") ==
      "cl_mouseSensCap = 0 (default 0, Q3/QL default 0)" &&
      console.execute("cl_mouseSensCap 30") == "cl_mouseSensCap = 30",
    "QL mouse sensitivity cap should be configurable"
  );
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
    console.execute("cl_viewmodel_motion_scale") ==
        "cl_viewmodel_motion_scale = 1 (default 1)" &&
      console.execute("cl_viewmodel_motion_scale 0") ==
        "cl_viewmodel_motion_scale = 0" &&
      console.execute("cl_viewmodel_motion_scale 2.1") ==
        "value out of range for cl_viewmodel_motion_scale" &&
      console.execute("cl_viewmodel_bob_scale") ==
        "cl_viewmodel_bob_scale = 0.65 (default 0.65)" &&
      console.execute("cl_viewmodel_sway_scale") ==
        "cl_viewmodel_sway_scale = 0.55 (default 0.55)" &&
      console.execute("cl_viewmodel_inertia_scale") ==
        "cl_viewmodel_inertia_scale = 0.55 (default 0.55)" &&
      console.execute("cl_viewmodel_landing_scale") ==
        "cl_viewmodel_landing_scale = 0.65 (default 0.65)" &&
      console.execute("cl_camera_position_response") ==
        "cl_camera_position_response = 0 (default 0)" &&
      console.execute("cl_camera_position_response 0.16") ==
        "value out of range for cl_camera_position_response",
    "viewmodel motion cvars should expose bounded competitive defaults"
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
      console.execute("cl_health_style 6") ==
        "value out of range for cl_health_style" &&
      console.execute("cl_health_style 5") == "cl_health_style = 5",
    "health HUD style cvar should expose the three classic and three art layouts"
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
    console.execute("r_weapon_switch_animation") ==
        "r_weapon_switch_animation = 1 (default 1)" &&
      console.execute("r_weapon_switch_animation 0") ==
        "r_weapon_switch_animation = 0" &&
      !console.getBool("r_weapon_switch_animation") &&
      console.execute("r_viewmodel_hands") == "r_viewmodel_hands = 0 (default 0)" &&
      console.execute("r_viewmodel_hands 1") == "r_viewmodel_hands = 1" &&
      console.getBool("r_viewmodel_hands") &&
      console.execute("r_dev_camera_draw_connected_body") ==
        "r_dev_camera_draw_connected_body = 0 (default 0)" &&
      console.execute("r_dev_camera_draw_connected_body 1") ==
        "r_dev_camera_draw_connected_body = 1" &&
      console.getBool("r_dev_camera_draw_connected_body"),
    "weapon presentation and development-camera proof controls should stay separate"
  );
  failures += expect(
    console.execute("r_weapon_pos") == "r_weapon_pos = 0 (default 0)" &&
      console.execute("r_weapon_pos 1") == "r_weapon_pos = 1" &&
      console.getInt("r_weapon_pos") == 1 &&
      console.execute("r_weapon_pos 3") == "value out of range for r_weapon_pos",
    "first-person weapon position should support center, right, and left presets"
  );
  failures += expect(
    console.execute("r_combat_effects") ==
        "r_combat_effects = 2 (default 2)" &&
      console.execute("r_combat_effects 3") ==
        "value out of range for r_combat_effects" &&
      console.execute("r_muzzle_light_duration") ==
        "r_muzzle_light_duration = 0.13 (default 0.13)" &&
      console.execute("r_bloom_threshold") ==
        "r_bloom_threshold = 1.15 (default 1.15)" &&
      console.execute("r_casing_max 97") ==
        "value out of range for r_casing_max" &&
      console.execute("r_decals_max") ==
        "r_decals_max = 128 (default 128)" &&
      console.execute("r_decal_lifetime") ==
        "r_decal_lifetime = 24 (default 24)",
    "combat effect cvars should expose bounded restrained defaults"
  );
  failures += expect(
    console.execute("r_mg_barrel_max_rps") ==
        "r_mg_barrel_max_rps = 14 (default 14)" &&
      console.execute("r_mg_barrel_spin_up") ==
        "r_mg_barrel_spin_up = 0.25 (default 0.25)" &&
      console.execute("r_mg_barrel_spin_down") ==
        "r_mg_barrel_spin_down = 0.55 (default 0.55)",
    "authored machine-gun barrel playback tuning should keep current defaults"
  );
  failures += expect(
    console.execute("r_player_model") ==
      "r_player_model = 1 (default 1)" &&
      console.execute("r_player_model 0") == "r_player_model = 0" &&
      console.execute("r_player_model 1") == "r_player_model = 1" &&
      console.execute("r_player_model 2") == "value out of range for r_player_model",
    "remote player model cvar should select legacy boxes or the Worker default"
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
      "value out of range for r_damage_numbers_mode",
    "removed redundant damage number mode should no longer be configurable"
  );
  failures += expect(
    console.execute("r_damage_numbers_damage_color") ==
      "r_damage_numbers_damage_color = 0 (default 0)" &&
      console.execute("r_damage_numbers_damage_color 1") ==
        "r_damage_numbers_damage_color = 1" &&
      console.getBool("r_damage_numbers_damage_color"),
    "damage-scaled damage number color should be toggleable"
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
    console.execute("r_render_scale") == "r_render_scale = 1 (default 1)" &&
      console.execute("r_render_scale 0.5") == "r_render_scale = 0.5" &&
      console.execute("r_render_scale 1.5") == "r_render_scale = 1.5" &&
      console.execute("r_render_scale 1.6") == "value out of range for r_render_scale",
    "render scale should use the safe 50 to 150 percent range"
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
    console.execute("r_world_frustum_cull") ==
      "r_world_frustum_cull = 0 (default 0)" &&
      console.execute("r_world_frustum_cull 1") ==
        "r_world_frustum_cull = 1" &&
      console.getBool("r_world_frustum_cull"),
    "experimental static-world frustum culling should default off and be toggleable"
  );
  failures += expect(
    console.execute("r_show_collision") ==
        "r_show_collision = 0 (default 0)" &&
      console.execute("r_show_collision 5") == "r_show_collision = 5" &&
      console.execute("r_show_collision 6") ==
        "value out of range for r_show_collision" &&
      console.getInt("r_show_collision") == 5,
    "collision visualization should expose the five bounded debug categories"
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
    console.execute("r_player_outline_mode") ==
        "r_player_outline_mode = 2 (default 2)" &&
      console.execute("r_player_outline_mode 0") == "r_player_outline_mode = 0" &&
      console.execute("r_player_outline_mode 2") == "r_player_outline_mode = 2" &&
      console.execute("r_player_outline_mode 3") ==
        "value out of range for r_player_outline_mode" &&
      console.execute("r_player_outline_width") ==
        "r_player_outline_width = 1.5 (default 1.5)" &&
      console.execute("r_player_outline_width 3") == "r_player_outline_width = 3" &&
      console.execute("r_player_outline_width 3.1") ==
        "value out of range for r_player_outline_width" &&
      console.execute("r_player_outline_debug_mask 1") ==
        "r_player_outline_debug_mask = 1",
    "native player outline controls should expose bounded mode and pixel width"
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
    std::find(
      archivedConfig.begin(),
      archivedConfig.end(),
      "set cl_show_console_cat 1"
    ) != archivedConfig.end(),
    "console cat visibility should persist through archived client config"
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
  failures += expect(
    console.execute("cl_netgraph") == "cl_netgraph = 0 (default 0)" &&
      console.execute("cl_netgraph 2") == "cl_netgraph = 2" &&
      console.execute("cl_netgraph 3") == "value out of range for cl_netgraph" &&
      console.execute("cl_netgraph_scale") ==
        "cl_netgraph_scale = 1.75 (default 1.75)" &&
      console.execute("cl_netgraph_scale 3.1") ==
        "value out of range for cl_netgraph_scale",
    "netgraph cvar should expose off, compact, and expanded modes"
  );

  return failures == 0 ? 0 : 1;
}
