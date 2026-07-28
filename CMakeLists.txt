cmake_minimum_required(VERSION 3.24)

project(
  lg_duel
  VERSION 0.1.0
  DESCRIPTION "Competitive first-person 3D arena FPS foundation"
  LANGUAGES CXX
)

include(CTest)
include(cmake/CompilerWarnings.cmake)
include(cmake/Dependencies.cmake)

function(lg_duel_set_windows_stack target)
  if(MSVC)
    target_link_options(${target} PRIVATE /STACK:67108864)
  elseif(WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_link_options(${target} PRIVATE "-Wl,--stack,67108864")
  endif()
endfunction()

add_library(lg_duel_core
  src/benchmark/Benchmark.cpp
  src/client/ClientGame.cpp
  src/client/HitConfirmAudio.cpp
  src/client/ClientSession.cpp
  src/client/Interpolation.cpp
  src/client/LocalHitFeedback.cpp
  src/client/Prediction.cpp
  src/console/ConsoleConfig.cpp
  src/console/ConsoleSystem.cpp
  src/dev/DevControlProtocol.cpp
  src/dev/DevControlServer.cpp
  src/dev/DevJson.cpp
  src/dev/PngWriter.cpp
  src/input/InputBindings.cpp
  src/map/MapParser.cpp
  src/map/MapToArena.cpp
  src/net/ClientNetworkSimulator.cpp
  src/net/LoopbackTransport.cpp
  src/net/NetCodec.cpp
  src/net/SimulatedTransport.cpp
  src/net/UdpTransport.cpp
  src/scenario/LiveScenarioSession.cpp
  src/scenario/ScenarioEvidence.cpp
  src/scenario/ScenarioRuntime.cpp
  src/scenario/ScenarioSchema.cpp
  src/server/ServerGame.cpp
  src/sim/Arena.cpp
  src/sim/ArenaBroadphase.cpp
  src/sim/ArenaMap.cpp
  src/sim/BalanceConfig.cpp
  src/sim/Collision.cpp
  src/sim/Combat.cpp
  src/sim/ClanArenaRules.cpp
  src/sim/McGuffinRules.cpp
  src/sim/DuelRules.cpp
  src/sim/GameplayCvars.cpp
  src/sim/Movement.cpp
  src/sim/MapRegistry.cpp
  src/sim/WeaponCatalog.cpp
)

target_include_directories(lg_duel_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(lg_duel_core PUBLIC cxx_std_20)
lg_duel_set_warnings(lg_duel_core)

if(WIN32)
  target_link_libraries(lg_duel_core PUBLIC ws2_32)
endif()

add_library(lg_duel_client_app
  src/app/AudioAssets.cpp
  src/app/ClientAudio.cpp
  src/app/ClientCvars.cpp
  src/app/DeathCamera.cpp
  src/app/ConsoleInput.cpp
  src/app/GameApp.cpp
  src/app/HudPresentation.cpp
  src/app/PerfTelemetry.cpp
  src/app/Scoreboard.cpp
  src/app/TextInput.cpp
  src/render/BitmapFont.cpp
  src/render/ChatLayout.cpp
  src/render/CombatEffects.cpp
  src/render/ConsoleLayout.cpp
  src/render/ConsoleCat.cpp
  src/render/GpuTimestampTiming.cpp
  src/render/GltfSkinnedModel.cpp
  src/render/ImpactMaterials.cpp
  src/render/PlayerPresentation.cpp
  src/render/Renderer.cpp
  src/render/Scene3D.cpp
  src/render/ScreenUi.cpp
  src/render/ViewModelPresentation.cpp
  src/render/WorldVisibility.cpp
)

target_link_libraries(lg_duel_client_app PUBLIC lg_duel_core)
target_include_directories(lg_duel_client_app
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_compile_features(lg_duel_client_app PUBLIC cxx_std_20)
lg_duel_set_warnings(lg_duel_client_app)
lg_duel_configure_sdl3(lg_duel_client_app)

add_executable(lg_duel_client
  src/app/main.cpp
)

target_link_libraries(lg_duel_client PRIVATE lg_duel_client_app)
lg_duel_set_warnings(lg_duel_client)

add_custom_command(
  TARGET lg_duel_client
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/color2d.vert.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/color2d.vert.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/color2d.frag.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/color2d.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/world3d.vert.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/world3d.vert.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/world3d.frag.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/world3d.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/world3d_direct.frag.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/world3d_direct.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/world_surface.vert.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/world_surface.vert.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/world_surface.frag.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/world_surface.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/world_surface_direct.frag.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/world_surface_direct.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/sun_shadow_world.vert.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/sun_shadow_world.vert.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/sun_shadow_static.vert.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/sun_shadow_static.vert.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/sun_shadow_material.vert.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/sun_shadow_material.vert.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/sun_shadow_gltf.vert.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/sun_shadow_gltf.vert.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/sun_shadow.frag.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/sun_shadow.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/outline_mask.frag.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/outline_mask.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/outline_clear.vert.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/outline_clear.vert.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/outline_clear.frag.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/outline_clear.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/outline_dilate.frag.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/outline_dilate.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/outline_composite.vert.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/outline_composite.vert.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/outline_composite.frag.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/outline_composite.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/outline_native_dilate.frag.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/outline_native_dilate.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/outline_native_composite.frag.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/outline_native_composite.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/instanced_mesh.vert.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/instanced_mesh.vert.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/static_mesh_instance.vert.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/static_mesh_instance.vert.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/material_mesh_instance.vert.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/material_mesh_instance.vert.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/material_weapon.frag.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/material_weapon.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/material_weapon_direct.frag.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/material_weapon_direct.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/gltf_player_model.vert.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/gltf_player_model.vert.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/gltf_player_model.frag.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/gltf_player_model.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/gltf_player_model_direct.frag.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/gltf_player_model_direct.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/instanced_billboard.vert.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/instanced_billboard.vert.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/instanced_color.frag.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/instanced_color.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/instanced_color_direct.frag.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/instanced_color_direct.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/instanced_glow.frag.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/instanced_glow.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/bloom_source.frag.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/bloom_source.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/bloom_blur.frag.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/bloom_blur.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/scene_composite.frag.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/scene_composite.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/scene_composite_no_bloom.frag.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/scene_composite_no_bloom.frag.spv"
)

if(WIN32 AND TARGET SDL3::SDL3)
  add_custom_command(
    TARGET lg_duel_client
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:SDL3::SDL3>" "$<TARGET_FILE_DIR:lg_duel_client>"
  )
endif()

add_executable(lg_duel_server
  src/server/main.cpp
  src/server/ServerApp.cpp
)

target_link_libraries(lg_duel_server PRIVATE lg_duel_core)
lg_duel_set_warnings(lg_duel_server)

# Textures are tracked at the repository root. Keep the assets/ fallback for
# checkouts that package all runtime content beneath assets, but never create
# an empty texture tree: map material paths must resolve to real files.
set(LG_DUEL_TEXTURES_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/textures")
if(NOT IS_DIRECTORY "${LG_DUEL_TEXTURES_SOURCE}" AND
   IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/textures")
  set(LG_DUEL_TEXTURES_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/assets/textures")
endif()
if(NOT IS_DIRECTORY "${LG_DUEL_TEXTURES_SOURCE}")
  message(FATAL_ERROR
    "LG Duel runtime textures are missing. Expected '${CMAKE_CURRENT_SOURCE_DIR}/textures' "
    "or '${CMAKE_CURRENT_SOURCE_DIR}/assets/textures'. Restore the tracked texture assets; "
    "do not create an empty textures directory.")
endif()

add_custom_target(lg_duel_runtime_assets ALL
  COMMAND ${CMAKE_COMMAND} -E remove_directory
    "$<TARGET_FILE_DIR:lg_duel_client>/maps"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/maps"
    "$<TARGET_FILE_DIR:lg_duel_client>/maps"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/config"
    "$<TARGET_FILE_DIR:lg_duel_client>/config"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/audio"
    "$<TARGET_FILE_DIR:lg_duel_client>/assets/audio"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/models"
    "$<TARGET_FILE_DIR:lg_duel_client>/assets/models"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts"
    "$<TARGET_FILE_DIR:lg_duel_client>/assets/fonts"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${LG_DUEL_TEXTURES_SOURCE}"
    "$<TARGET_FILE_DIR:lg_duel_client>/textures"
  COMMAND ${CMAKE_COMMAND} -E make_directory
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/color2d.vert.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/color2d.vert.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/color2d.frag.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/color2d.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/world3d.vert.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/world3d.vert.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/world3d.frag.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/world3d.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/world3d_direct.frag.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/world3d_direct.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/world_surface.vert.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/world_surface.vert.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/world_surface.frag.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/world_surface.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/world_surface_direct.frag.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/world_surface_direct.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/sun_shadow_world.vert.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/sun_shadow_world.vert.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/sun_shadow_static.vert.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/sun_shadow_static.vert.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/sun_shadow_material.vert.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/sun_shadow_material.vert.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/sun_shadow_gltf.vert.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/sun_shadow_gltf.vert.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/sun_shadow.frag.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/sun_shadow.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/outline_mask.frag.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/outline_mask.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/outline_clear.vert.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/outline_clear.vert.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/outline_clear.frag.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/outline_clear.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/outline_dilate.frag.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/outline_dilate.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/outline_composite.vert.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/outline_composite.vert.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/outline_composite.frag.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/outline_composite.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/outline_native_dilate.frag.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/outline_native_dilate.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/outline_native_composite.frag.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/outline_native_composite.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/instanced_mesh.vert.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/instanced_mesh.vert.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/static_mesh_instance.vert.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/static_mesh_instance.vert.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/material_mesh_instance.vert.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/material_mesh_instance.vert.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/material_weapon.frag.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/material_weapon.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/material_weapon_direct.frag.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/material_weapon_direct.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/gltf_player_model.vert.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/gltf_player_model.vert.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/gltf_player_model.frag.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/gltf_player_model.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/gltf_player_model_direct.frag.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/gltf_player_model_direct.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/instanced_billboard.vert.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/instanced_billboard.vert.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/instanced_color.frag.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/instanced_color.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/instanced_color_direct.frag.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/instanced_color_direct.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/instanced_glow.frag.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/instanced_glow.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/bloom_source.frag.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/bloom_source.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/bloom_blur.frag.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/bloom_blur.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/scene_composite.frag.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/scene_composite.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/scene_composite_no_bloom.frag.spv"
    "$<TARGET_FILE_DIR:lg_duel_client>/shaders/scene_composite_no_bloom.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E remove_directory
    "$<TARGET_FILE_DIR:lg_duel_server>/maps"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/maps"
    "$<TARGET_FILE_DIR:lg_duel_server>/maps"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/config"
    "$<TARGET_FILE_DIR:lg_duel_server>/config"
  COMMENT "Syncing runtime maps, config, shaders, textures, and audio"
)

add_dependencies(lg_duel_client lg_duel_runtime_assets)
add_dependencies(lg_duel_server lg_duel_runtime_assets)

add_executable(lg_duel_server_probe
  tools/ServerProbe.cpp
)

target_link_libraries(lg_duel_server_probe PRIVATE lg_duel_core)
lg_duel_set_warnings(lg_duel_server_probe)

add_executable(lg_duel_map_validate
  tools/MapValidate.cpp
)

target_link_libraries(lg_duel_map_validate PRIVATE lg_duel_core)
lg_duel_set_warnings(lg_duel_map_validate)

add_executable(lg_duel_sim_benchmark
  tools/SimulationBenchmark.cpp
)

target_link_libraries(lg_duel_sim_benchmark PRIVATE lg_duel_core)
lg_duel_set_warnings(lg_duel_sim_benchmark)

add_executable(lg_duel_scenarios
  tools/ScenarioRunner.cpp
)

target_link_libraries(lg_duel_scenarios PRIVATE lg_duel_core)
lg_duel_set_warnings(lg_duel_scenarios)

add_executable(lg_duel_asset_validate
  tools/AssetValidate.cpp
  src/render/GltfSkinnedModel.cpp
)

target_link_libraries(lg_duel_asset_validate PRIVATE lg_duel_core)
lg_duel_set_warnings(lg_duel_asset_validate)

foreach(target IN ITEMS
  lg_duel_client
  lg_duel_server
  lg_duel_server_probe
  lg_duel_map_validate
  lg_duel_sim_benchmark
  lg_duel_scenarios
  lg_duel_asset_validate
)
  lg_duel_set_windows_stack(${target})
endforeach()

add_custom_target(lg_duel_validate_maps ALL
  COMMAND $<TARGET_FILE:lg_duel_map_validate> "${CMAKE_CURRENT_SOURCE_DIR}/maps"
  DEPENDS lg_duel_map_validate
  COMMENT "Validating maps"
)

add_dependencies(lg_duel_runtime_assets lg_duel_validate_maps)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
