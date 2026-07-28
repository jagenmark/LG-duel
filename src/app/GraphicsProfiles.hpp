#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace lg {

enum class GraphicsProfile { Low, Default, Competitive, High };

struct GraphicsProfileValue {
  std::string_view cvar;
  std::string_view value;
};

struct GraphicsProfileDefinition {
  GraphicsProfile profile;
  std::string_view name;
  std::array<GraphicsProfileValue, 21> values;
};

inline constexpr std::array<GraphicsProfileDefinition, 4> kGraphicsProfiles{{
  {GraphicsProfile::Low, "Low", {{
    {"r_render_scale", "1"}, {"r_texture_filter", "1"},
    {"r_texture_anisotropy", "1"}, {"r_texture_lod_bias", "1.5"},
    {"r_frustum_cull", "1"}, {"r_world_frustum_cull", "1"},
    {"r_draw_player_outlines", "0"}, {"r_player_outline_mode", "0"},
    {"r_combat_effects", "1"}, {"r_tonemap_exposure", "1"},
    {"r_atmosphere_grade", "1"},
    {"r_bloom", "0"}, {"r_bloom_intensity", "0.1"},
    {"r_antialiasing", "0"}, {"r_sun_shadows", "0"},
    {"r_contact_shadows", "1"}, {"r_material_quality", "0"},
    {"r_player_rim", "0"},
    {"r_casings", "0"}, {"r_impact_particles", "0.5"},
    {"r_decals_max", "48"}
  }}},
  {GraphicsProfile::Default, "Default", {{
    {"r_render_scale", "1"}, {"r_texture_filter", "2"},
    {"r_texture_anisotropy", "8"}, {"r_texture_lod_bias", "0.5"},
    {"r_frustum_cull", "1"}, {"r_world_frustum_cull", "0"},
    {"r_draw_player_outlines", "1"}, {"r_player_outline_mode", "1"},
    {"r_combat_effects", "2"}, {"r_tonemap_exposure", "1"},
    {"r_atmosphere_grade", "2"},
    {"r_bloom", "1"}, {"r_bloom_intensity", "0.18"},
    {"r_antialiasing", "1"}, {"r_sun_shadows", "2"},
    {"r_contact_shadows", "1"}, {"r_material_quality", "1"},
    {"r_player_rim", "1"},
    {"r_casings", "1"}, {"r_impact_particles", "1"},
    {"r_decals_max", "128"}
  }}},
  {GraphicsProfile::Competitive, "Competitive", {{
    {"r_render_scale", "1"}, {"r_texture_filter", "2"},
    {"r_texture_anisotropy", "4"}, {"r_texture_lod_bias", "0"},
    {"r_frustum_cull", "1"}, {"r_world_frustum_cull", "1"},
    {"r_draw_player_outlines", "1"}, {"r_player_outline_mode", "2"},
    {"r_combat_effects", "1"}, {"r_tonemap_exposure", "1"},
    {"r_atmosphere_grade", "0"},
    {"r_bloom", "0"}, {"r_bloom_intensity", "0.1"},
    {"r_antialiasing", "1"}, {"r_sun_shadows", "1"},
    {"r_contact_shadows", "1"}, {"r_material_quality", "1"},
    {"r_player_rim", "1"},
    {"r_casings", "0"}, {"r_impact_particles", "0.5"},
    {"r_decals_max", "64"}
  }}},
  {GraphicsProfile::High, "High", {{
    {"r_render_scale", "1"}, {"r_texture_filter", "2"},
    {"r_texture_anisotropy", "16"}, {"r_texture_lod_bias", "-0.25"},
    {"r_frustum_cull", "1"}, {"r_world_frustum_cull", "0"},
    {"r_draw_player_outlines", "1"}, {"r_player_outline_mode", "2"},
    {"r_combat_effects", "2"}, {"r_tonemap_exposure", "1"},
    {"r_atmosphere_grade", "3"},
    {"r_bloom", "1"}, {"r_bloom_intensity", "0.24"},
    {"r_antialiasing", "2"}, {"r_sun_shadows", "2"},
    {"r_contact_shadows", "1"}, {"r_material_quality", "2"},
    {"r_player_rim", "2"},
    {"r_casings", "1"}, {"r_impact_particles", "1.5"},
    {"r_decals_max", "192"}
  }}},
}};

inline constexpr const GraphicsProfileDefinition& graphicsProfileDefinition(GraphicsProfile profile) {
  return kGraphicsProfiles[static_cast<std::size_t>(profile)];
}

inline constexpr std::string_view graphicsProfileName(GraphicsProfile profile) {
  return graphicsProfileDefinition(profile).name;
}

} // namespace lg
