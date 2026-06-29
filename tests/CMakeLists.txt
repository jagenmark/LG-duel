add_executable(lg_duel_smoke_tests
  smoke/SmokeTests.cpp
)

target_link_libraries(lg_duel_smoke_tests PRIVATE lg_duel_client_app)
target_compile_features(lg_duel_smoke_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_smoke_tests)

if(WIN32 AND TARGET SDL3::SDL3)
  add_custom_command(
    TARGET lg_duel_smoke_tests
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:SDL3::SDL3>" "$<TARGET_FILE_DIR:lg_duel_smoke_tests>"
  )
endif()

add_test(NAME lg_duel_smoke_tests COMMAND lg_duel_smoke_tests)

add_executable(lg_duel_movement_tests
  sim/MovementTests.cpp
)

target_link_libraries(lg_duel_movement_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_movement_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_movement_tests)

add_test(NAME lg_duel_movement_tests COMMAND lg_duel_movement_tests)

add_executable(lg_duel_combat_tests
  sim/CombatTests.cpp
)

target_link_libraries(lg_duel_combat_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_combat_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_combat_tests)

add_test(NAME lg_duel_combat_tests COMMAND lg_duel_combat_tests)

add_executable(lg_duel_duel_rules_tests
  sim/DuelRulesTests.cpp
)

target_link_libraries(lg_duel_duel_rules_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_duel_rules_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_duel_rules_tests)

add_test(NAME lg_duel_duel_rules_tests COMMAND lg_duel_duel_rules_tests)

add_executable(lg_duel_clan_arena_rules_tests
  sim/ClanArenaRulesTests.cpp
)

target_link_libraries(lg_duel_clan_arena_rules_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_clan_arena_rules_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_clan_arena_rules_tests)

add_test(NAME lg_duel_clan_arena_rules_tests COMMAND lg_duel_clan_arena_rules_tests)

add_executable(lg_duel_server_tests
  net/ServerGameTests.cpp
)

target_link_libraries(lg_duel_server_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_server_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_server_tests)

add_test(NAME lg_duel_server_tests COMMAND lg_duel_server_tests)

add_executable(lg_duel_weapon_switching_tests
  net/WeaponSwitchingTests.cpp
)

target_link_libraries(lg_duel_weapon_switching_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_weapon_switching_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_weapon_switching_tests)

add_test(NAME lg_duel_weapon_switching_tests COMMAND lg_duel_weapon_switching_tests)

add_executable(lg_duel_clan_arena_server_tests
  net/ClanArenaServerTests.cpp
)

target_link_libraries(lg_duel_clan_arena_server_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_clan_arena_server_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_clan_arena_server_tests)

add_test(NAME lg_duel_clan_arena_server_tests COMMAND lg_duel_clan_arena_server_tests)

add_executable(lg_duel_client_tests
  client/ClientGameTests.cpp
)

target_link_libraries(lg_duel_client_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_client_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_client_tests)

add_test(NAME lg_duel_client_tests COMMAND lg_duel_client_tests)

add_executable(lg_duel_hit_confirm_audio_tests
  client/HitConfirmAudioTests.cpp
)

target_link_libraries(lg_duel_hit_confirm_audio_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_hit_confirm_audio_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_hit_confirm_audio_tests)

add_test(NAME lg_duel_hit_confirm_audio_tests COMMAND lg_duel_hit_confirm_audio_tests)

add_executable(lg_duel_local_hit_feedback_tests
  client/LocalHitFeedbackTests.cpp
)

target_link_libraries(lg_duel_local_hit_feedback_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_local_hit_feedback_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_local_hit_feedback_tests)

add_test(NAME lg_duel_local_hit_feedback_tests COMMAND lg_duel_local_hit_feedback_tests)

add_executable(lg_duel_collision_tests
  sim/CollisionTests.cpp
)

target_link_libraries(lg_duel_collision_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_collision_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_collision_tests)

add_test(NAME lg_duel_collision_tests COMMAND lg_duel_collision_tests)

add_executable(lg_duel_arena_map_tests
  sim/ArenaMapTests.cpp
)

target_link_libraries(lg_duel_arena_map_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_arena_map_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_arena_map_tests)

add_test(NAME lg_duel_arena_map_tests COMMAND lg_duel_arena_map_tests)

add_executable(lg_duel_quake_map_tests
  sim/QuakeMapTests.cpp
)

target_link_libraries(lg_duel_quake_map_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_quake_map_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_quake_map_tests)

add_test(NAME lg_duel_quake_map_tests COMMAND lg_duel_quake_map_tests)

add_executable(lg_duel_fixed_tick_tests
  sim/FixedTickTests.cpp
)

target_link_libraries(lg_duel_fixed_tick_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_fixed_tick_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_fixed_tick_tests)

add_test(NAME lg_duel_fixed_tick_tests COMMAND lg_duel_fixed_tick_tests)

add_executable(lg_duel_protocol_tests
  net/ProtocolTests.cpp
)

target_link_libraries(lg_duel_protocol_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_protocol_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_protocol_tests)

add_test(NAME lg_duel_protocol_tests COMMAND lg_duel_protocol_tests)

add_executable(lg_duel_client_network_simulator_tests
  net/ClientNetworkSimulatorTests.cpp
)

target_link_libraries(lg_duel_client_network_simulator_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_client_network_simulator_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_client_network_simulator_tests)

add_test(
  NAME lg_duel_client_network_simulator_tests
  COMMAND lg_duel_client_network_simulator_tests
)

add_executable(lg_duel_network_simulation_tests
  net/NetworkSimulationTests.cpp
)

target_link_libraries(lg_duel_network_simulation_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_network_simulation_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_network_simulation_tests)

add_test(NAME lg_duel_network_simulation_tests COMMAND lg_duel_network_simulation_tests)

add_executable(lg_duel_determinism_tests
  sim/DeterminismTests.cpp
)

target_link_libraries(lg_duel_determinism_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_determinism_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_determinism_tests)

add_test(NAME lg_duel_determinism_tests COMMAND lg_duel_determinism_tests)

add_executable(lg_duel_udp_tests
  net/UdpTransportTests.cpp
)

target_link_libraries(lg_duel_udp_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_udp_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_udp_tests)

add_test(NAME lg_duel_udp_tests COMMAND lg_duel_udp_tests)

add_executable(lg_duel_console_tests
  console/ConsoleSystemTests.cpp
)

target_link_libraries(lg_duel_console_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_console_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_console_tests)

add_test(NAME lg_duel_console_tests COMMAND lg_duel_console_tests)

add_executable(lg_duel_weapon_catalog_tests
  sim/WeaponCatalogTests.cpp
)

target_link_libraries(lg_duel_weapon_catalog_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_weapon_catalog_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_weapon_catalog_tests)

add_test(NAME lg_duel_weapon_catalog_tests COMMAND lg_duel_weapon_catalog_tests)

add_executable(lg_duel_input_binding_tests
  input/InputBindingsTests.cpp
)

target_link_libraries(lg_duel_input_binding_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_input_binding_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_input_binding_tests)

add_test(NAME lg_duel_input_binding_tests COMMAND lg_duel_input_binding_tests)

add_executable(lg_duel_console_input_tests
  app/ConsoleInputTests.cpp
  ${PROJECT_SOURCE_DIR}/src/app/ConsoleInput.cpp
  ${PROJECT_SOURCE_DIR}/src/app/TextInput.cpp
)

target_link_libraries(lg_duel_console_input_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_console_input_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_console_input_tests)

add_test(NAME lg_duel_console_input_tests COMMAND lg_duel_console_input_tests)

add_executable(lg_duel_client_cvars_tests
  app/ClientCvarsTests.cpp
  ${PROJECT_SOURCE_DIR}/src/app/ClientCvars.cpp
)

target_link_libraries(lg_duel_client_cvars_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_client_cvars_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_client_cvars_tests)

add_test(NAME lg_duel_client_cvars_tests COMMAND lg_duel_client_cvars_tests)

add_executable(lg_duel_client_audio_tests
  app/ClientAudioTests.cpp
)

target_link_libraries(lg_duel_client_audio_tests PRIVATE lg_duel_client_app)
target_compile_features(lg_duel_client_audio_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_client_audio_tests)

if(WIN32 AND TARGET SDL3::SDL3)
  add_custom_command(
    TARGET lg_duel_client_audio_tests
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:SDL3::SDL3>" "$<TARGET_FILE_DIR:lg_duel_client_audio_tests>"
  )
endif()

add_test(NAME lg_duel_client_audio_tests COMMAND lg_duel_client_audio_tests)

add_executable(lg_duel_scoreboard_tests
  app/ScoreboardTests.cpp
  ${PROJECT_SOURCE_DIR}/src/app/Scoreboard.cpp
)

target_link_libraries(lg_duel_scoreboard_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_scoreboard_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_scoreboard_tests)

add_test(NAME lg_duel_scoreboard_tests COMMAND lg_duel_scoreboard_tests)

add_executable(lg_duel_hud_presentation_tests
  app/HudPresentationTests.cpp
  ${PROJECT_SOURCE_DIR}/src/app/HudPresentation.cpp
)

target_link_libraries(lg_duel_hud_presentation_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_hud_presentation_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_hud_presentation_tests)

add_test(NAME lg_duel_hud_presentation_tests COMMAND lg_duel_hud_presentation_tests)

add_executable(lg_duel_client_chat_tests
  app/ClientChatTests.cpp
)

target_link_libraries(lg_duel_client_chat_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_client_chat_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_client_chat_tests)

add_test(NAME lg_duel_client_chat_tests COMMAND lg_duel_client_chat_tests)

add_executable(lg_duel_audio_assets_tests
  app/AudioAssetsTests.cpp
  ${PROJECT_SOURCE_DIR}/src/app/AudioAssets.cpp
)

target_link_libraries(lg_duel_audio_assets_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_audio_assets_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_audio_assets_tests)

add_test(NAME lg_duel_audio_assets_tests COMMAND lg_duel_audio_assets_tests)

add_executable(lg_duel_client_session_tests
  client/ClientSessionTests.cpp
)

target_link_libraries(lg_duel_client_session_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_client_session_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_client_session_tests)

add_test(NAME lg_duel_client_session_tests COMMAND lg_duel_client_session_tests)

add_executable(lg_duel_perspective_tests
  render/PerspectiveTests.cpp
)

target_link_libraries(lg_duel_perspective_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_perspective_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_perspective_tests)

add_test(NAME lg_duel_perspective_tests COMMAND lg_duel_perspective_tests)

add_executable(lg_duel_top_down_scene_tests
  render/TopDownSceneTests.cpp
  ${PROJECT_SOURCE_DIR}/src/render/TopDownScene.cpp
)

target_link_libraries(lg_duel_top_down_scene_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_top_down_scene_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_top_down_scene_tests)

add_test(NAME lg_duel_top_down_scene_tests COMMAND lg_duel_top_down_scene_tests)

add_executable(lg_duel_screen_ui_tests
  render/ScreenUiTests.cpp
  ${PROJECT_SOURCE_DIR}/src/app/TextInput.cpp
  ${PROJECT_SOURCE_DIR}/src/render/BitmapFont.cpp
  ${PROJECT_SOURCE_DIR}/src/render/ChatLayout.cpp
  ${PROJECT_SOURCE_DIR}/src/render/ConsoleLayout.cpp
  ${PROJECT_SOURCE_DIR}/src/render/ScreenUi.cpp
)

target_link_libraries(lg_duel_screen_ui_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_screen_ui_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_screen_ui_tests)

add_test(NAME lg_duel_screen_ui_tests COMMAND lg_duel_screen_ui_tests)

add_executable(lg_duel_scene_3d_tests
  render/Scene3DTests.cpp
  ${PROJECT_SOURCE_DIR}/src/render/Scene3D.cpp
)

target_link_libraries(lg_duel_scene_3d_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_scene_3d_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_scene_3d_tests)

add_test(NAME lg_duel_scene_3d_tests COMMAND lg_duel_scene_3d_tests)

get_property(lg_duel_test_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(target IN LISTS lg_duel_test_targets)
  lg_duel_set_windows_stack(${target})
endforeach()
