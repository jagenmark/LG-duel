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

add_executable(lg_duel_mcguffin_rules_tests
  sim/McGuffinRulesTests.cpp
)

target_link_libraries(lg_duel_mcguffin_rules_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_mcguffin_rules_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_mcguffin_rules_tests)

add_test(NAME lg_duel_mcguffin_rules_tests COMMAND lg_duel_mcguffin_rules_tests)

add_executable(lg_duel_server_tests
  net/ServerGameTests.cpp
)

target_link_libraries(lg_duel_server_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_server_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_server_tests)

add_test(NAME lg_duel_server_tests COMMAND lg_duel_server_tests)

add_executable(lg_duel_scenario_tests
  scenario/ScenarioTests.cpp
)

target_link_libraries(lg_duel_scenario_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_scenario_tests PRIVATE cxx_std_20)
target_compile_definitions(
  lg_duel_scenario_tests
  PRIVATE
    LG_DUEL_SCENARIO_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/scenario/fixtures"
)
lg_duel_set_warnings(lg_duel_scenario_tests)

add_test(NAME lg_duel_scenario_tests COMMAND lg_duel_scenario_tests)
set_tests_properties(
  lg_duel_scenario_tests
  PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
)

add_executable(lg_duel_live_scenario_session_tests
  scenario/LiveScenarioSessionTests.cpp
  ${PROJECT_SOURCE_DIR}/src/server/ServerApp.cpp
)

target_link_libraries(lg_duel_live_scenario_session_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_live_scenario_session_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_live_scenario_session_tests)

add_test(
  NAME lg_duel_live_scenario_session_tests
  COMMAND lg_duel_live_scenario_session_tests
)
set_tests_properties(
  lg_duel_live_scenario_session_tests
  PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
)

add_test(
  NAME lg_duel_scenario_starter_suite
  COMMAND $<TARGET_FILE:lg_duel_scenarios>
    --suite smoke
    --repeat 3
    --output ${PROJECT_BINARY_DIR}/scenario-ctest-results
)
set_tests_properties(
  lg_duel_scenario_starter_suite
  PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
)

add_executable(lg_duel_projectile_self_knockback_tests
  net/ProjectileSelfKnockbackTests.cpp
)

target_link_libraries(lg_duel_projectile_self_knockback_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_projectile_self_knockback_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_projectile_self_knockback_tests)

add_test(
  NAME lg_duel_projectile_self_knockback_tests
  COMMAND lg_duel_projectile_self_knockback_tests
)
add_executable(lg_duel_bot_server_tests
  net/BotServerTests.cpp
)

target_link_libraries(lg_duel_bot_server_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_bot_server_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_bot_server_tests)

add_test(NAME lg_duel_bot_server_tests COMMAND lg_duel_bot_server_tests)

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

add_executable(lg_duel_mcguffin_server_tests
  net/McGuffinServerTests.cpp
)

target_link_libraries(lg_duel_mcguffin_server_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_mcguffin_server_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_mcguffin_server_tests)

add_test(NAME lg_duel_mcguffin_server_tests COMMAND lg_duel_mcguffin_server_tests)

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

add_executable(lg_duel_arena_broadphase_tests
  sim/ArenaBroadphaseTests.cpp
)

target_link_libraries(lg_duel_arena_broadphase_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_arena_broadphase_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_arena_broadphase_tests)

add_test(NAME lg_duel_arena_broadphase_tests COMMAND lg_duel_arena_broadphase_tests)
set_tests_properties(lg_duel_arena_broadphase_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(lg_duel_quake_map_tests
  sim/QuakeMapTests.cpp
)

target_link_libraries(lg_duel_quake_map_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_quake_map_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_quake_map_tests)

add_test(NAME lg_duel_quake_map_tests COMMAND lg_duel_quake_map_tests)
set_tests_properties(lg_duel_quake_map_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

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

add_executable(lg_duel_replay_codec_tests
  replay/ReplayCodecTests.cpp
)

target_link_libraries(lg_duel_replay_codec_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_replay_codec_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_replay_codec_tests)

add_test(NAME lg_duel_replay_codec_tests COMMAND lg_duel_replay_codec_tests)

add_executable(lg_duel_replay_playback_tests
  replay/ReplayPlaybackTests.cpp
)

target_link_libraries(lg_duel_replay_playback_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_replay_playback_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_replay_playback_tests)

add_test(NAME lg_duel_replay_playback_tests COMMAND lg_duel_replay_playback_tests)

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

add_executable(lg_duel_dev_control_tests
  dev/DevControlTests.cpp
)

target_link_libraries(lg_duel_dev_control_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_dev_control_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_dev_control_tests)

add_test(NAME lg_duel_dev_control_tests COMMAND lg_duel_dev_control_tests)

add_executable(lg_duel_benchmark_tests
  benchmark/BenchmarkTests.cpp
)

target_link_libraries(lg_duel_benchmark_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_benchmark_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_benchmark_tests)

add_test(NAME lg_duel_benchmark_tests COMMAND lg_duel_benchmark_tests)

find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_test(
    NAME lg_duel_control_python_tests
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/test_lg_tools.py
  )
  add_test(
    NAME lg_duel_benchmark_python_tests
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/test_lg_benchmark.py
  )
  add_test(
    NAME lg_duel_frame_timeline_python_tests
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/test_lg_frame_timeline_report.py
  )
  add_test(
    NAME lg_duel_match_load_report_python_tests
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/test_build_match_load_reports.py
  )
  add_test(
    NAME lg_duel_performance_policy_python_tests
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/test_lg_performance_policy.py
  )
  add_test(
    NAME lg_duel_benchmark_comparison_python_tests
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/test_lg_compare_benchmarks.py
  )
  add_test(
    NAME lg_duel_verification_evidence_python_tests
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/test_lg_verification.py
  )
  add_test(
    NAME lg_duel_launch_python_tests
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/test_lg_launch.py
  )
  add_test(
    NAME lg_duel_live_scenario_python_tests
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/test_lg_live_scenario.py
  )
  add_test(
    NAME lg_duel_q3_import_python_tests
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/test_import_q3_map.py
  )
  execute_process(
    COMMAND ${Python3_EXECUTABLE} -c "import PIL"
    RESULT_VARIABLE LG_DUEL_PILLOW_IMPORT_RESULT
    OUTPUT_QUIET
    ERROR_QUIET
  )
  if(LG_DUEL_PILLOW_IMPORT_RESULT EQUAL 0)
    add_test(
      NAME lg_duel_sky_converter_python_tests
      COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/test_convert_sky_panorama.py
    )
  else()
    message(
      STATUS
      "Pillow not found; skipping sky panorama converter tests"
    )
  endif()
  add_test(
    NAME lg_duel_asset_pipeline_python_tests
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/test_asset_pipeline.py
  )
endif()

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

add_executable(lg_duel_option_menu_layout_tests
  render/OptionMenuLayoutTests.cpp
)

target_link_libraries(lg_duel_option_menu_layout_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_option_menu_layout_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_option_menu_layout_tests)

add_test(
  NAME lg_duel_option_menu_layout_tests
  COMMAND lg_duel_option_menu_layout_tests
)

add_executable(lg_duel_client_cvars_tests
  app/ClientCvarsTests.cpp
  ${PROJECT_SOURCE_DIR}/src/app/ClientCvars.cpp
)

target_link_libraries(lg_duel_client_cvars_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_client_cvars_tests PRIVATE cxx_std_20)
target_compile_definitions(lg_duel_client_cvars_tests PRIVATE
  LG_DUEL_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
lg_duel_set_warnings(lg_duel_client_cvars_tests)

add_test(NAME lg_duel_client_cvars_tests COMMAND lg_duel_client_cvars_tests)

add_executable(lg_duel_misc_menu_tests
  app/MiscMenuTests.cpp
  ${PROJECT_SOURCE_DIR}/src/app/ClientCvars.cpp
  ${PROJECT_SOURCE_DIR}/src/app/MiscMenu.cpp
)

target_link_libraries(lg_duel_misc_menu_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_misc_menu_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_misc_menu_tests)

add_test(NAME lg_duel_misc_menu_tests COMMAND lg_duel_misc_menu_tests)

add_executable(lg_duel_death_camera_tests
  app/DeathCameraTests.cpp
  ${PROJECT_SOURCE_DIR}/src/app/DeathCamera.cpp
)

target_link_libraries(lg_duel_death_camera_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_death_camera_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_death_camera_tests)

add_test(NAME lg_duel_death_camera_tests COMMAND lg_duel_death_camera_tests)

add_executable(lg_duel_perf_telemetry_tests
  app/PerfTelemetryTests.cpp
  ${PROJECT_SOURCE_DIR}/src/app/PerfTelemetry.cpp
)

target_link_libraries(lg_duel_perf_telemetry_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_perf_telemetry_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_perf_telemetry_tests)

add_test(NAME lg_duel_perf_telemetry_tests COMMAND lg_duel_perf_telemetry_tests)

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
set_tests_properties(
  lg_duel_audio_assets_tests
  PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)

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

add_executable(lg_duel_sky_tests
  render/SkyTests.cpp
)

target_link_libraries(lg_duel_sky_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_sky_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_sky_tests)

add_test(NAME lg_duel_sky_tests COMMAND lg_duel_sky_tests)

add_executable(lg_duel_gpu_timestamp_timing_tests
  render/GpuTimestampTimingTests.cpp
  ${PROJECT_SOURCE_DIR}/src/render/GpuTimestampTiming.cpp
)

target_link_libraries(lg_duel_gpu_timestamp_timing_tests PRIVATE lg_duel_core)
target_compile_features(
  lg_duel_gpu_timestamp_timing_tests
  PRIVATE cxx_std_20
)
lg_duel_set_warnings(lg_duel_gpu_timestamp_timing_tests)

add_test(
  NAME lg_duel_gpu_timestamp_timing_tests
  COMMAND lg_duel_gpu_timestamp_timing_tests
)

add_executable(lg_duel_world_visibility_tests
  render/WorldVisibilityTests.cpp
  ${PROJECT_SOURCE_DIR}/src/render/WorldVisibility.cpp
)

target_link_libraries(lg_duel_world_visibility_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_world_visibility_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_world_visibility_tests)

add_test(NAME lg_duel_world_visibility_tests COMMAND lg_duel_world_visibility_tests)

add_executable(lg_duel_static_ambient_probe_tests
  render/StaticAmbientProbeTests.cpp
  ${PROJECT_SOURCE_DIR}/src/render/StaticAmbientProbe.cpp
)

target_link_libraries(lg_duel_static_ambient_probe_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_static_ambient_probe_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_static_ambient_probe_tests)

add_test(
  NAME lg_duel_static_ambient_probe_tests
  COMMAND lg_duel_static_ambient_probe_tests
)

add_executable(lg_duel_screen_ui_tests
  render/ScreenUiTests.cpp
  ${PROJECT_SOURCE_DIR}/src/app/TextInput.cpp
  ${PROJECT_SOURCE_DIR}/src/render/BitmapFont.cpp
  ${PROJECT_SOURCE_DIR}/src/render/ChatLayout.cpp
  ${PROJECT_SOURCE_DIR}/src/render/ConsoleLayout.cpp
  ${PROJECT_SOURCE_DIR}/src/render/ConsoleCat.cpp
  ${PROJECT_SOURCE_DIR}/src/render/ScreenUi.cpp
)

target_link_libraries(lg_duel_screen_ui_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_screen_ui_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_screen_ui_tests)

add_test(NAME lg_duel_screen_ui_tests COMMAND lg_duel_screen_ui_tests)

add_executable(lg_duel_scene_3d_tests
  render/Scene3DTests.cpp
  ${PROJECT_SOURCE_DIR}/src/render/GltfSkinnedModel.cpp
  ${PROJECT_SOURCE_DIR}/src/render/Scene3D.cpp
  ${PROJECT_SOURCE_DIR}/src/render/StaticAmbientProbe.cpp
)

# Scene tests load the same runtime GLB selected by the client, so a focused
# test build must refresh assets just like the full client build does.
add_dependencies(lg_duel_scene_3d_tests lg_duel_runtime_assets)

target_link_libraries(lg_duel_scene_3d_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_scene_3d_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_scene_3d_tests)

add_test(NAME lg_duel_scene_3d_tests COMMAND lg_duel_scene_3d_tests)

add_executable(lg_duel_gltf_material_tests
  render/GltfMaterialTests.cpp
  ${PROJECT_SOURCE_DIR}/src/render/GltfSkinnedModel.cpp
)

add_dependencies(lg_duel_gltf_material_tests lg_duel_runtime_assets)
target_link_libraries(lg_duel_gltf_material_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_gltf_material_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_gltf_material_tests)

add_test(NAME lg_duel_gltf_material_tests COMMAND lg_duel_gltf_material_tests)

add_executable(lg_duel_player_presentation_tests
  render/PlayerPresentationTests.cpp
  ${PROJECT_SOURCE_DIR}/src/render/PlayerPresentation.cpp
)

target_link_libraries(lg_duel_player_presentation_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_player_presentation_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_player_presentation_tests)

add_test(NAME lg_duel_player_presentation_tests COMMAND lg_duel_player_presentation_tests)

add_executable(lg_duel_viewmodel_presentation_tests
  render/ViewModelPresentationTests.cpp
  ${PROJECT_SOURCE_DIR}/src/render/ViewModelPresentation.cpp
)

target_link_libraries(lg_duel_viewmodel_presentation_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_viewmodel_presentation_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_viewmodel_presentation_tests)

add_test(NAME lg_duel_viewmodel_presentation_tests COMMAND lg_duel_viewmodel_presentation_tests)

add_executable(lg_duel_weapon_presentation_tests
  render/WeaponPresentationTests.cpp
)

target_link_libraries(lg_duel_weapon_presentation_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_weapon_presentation_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_weapon_presentation_tests)

add_test(NAME lg_duel_weapon_presentation_tests COMMAND lg_duel_weapon_presentation_tests)

add_executable(lg_duel_combat_effects_tests
  render/CombatEffectsTests.cpp
  ${PROJECT_SOURCE_DIR}/src/render/CombatEffects.cpp
)

target_link_libraries(lg_duel_combat_effects_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_combat_effects_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_combat_effects_tests)

add_test(NAME lg_duel_combat_effects_tests COMMAND lg_duel_combat_effects_tests)

add_executable(lg_duel_impact_materials_tests
  render/ImpactMaterialsTests.cpp
  ${PROJECT_SOURCE_DIR}/src/render/ImpactMaterials.cpp
)

target_link_libraries(lg_duel_impact_materials_tests PRIVATE lg_duel_core)
target_compile_features(lg_duel_impact_materials_tests PRIVATE cxx_std_20)
lg_duel_set_warnings(lg_duel_impact_materials_tests)

add_test(NAME lg_duel_impact_materials_tests COMMAND lg_duel_impact_materials_tests)

add_test(
  NAME lg_duel_asset_validate_tests
  COMMAND lg_duel_asset_validate
    "${PROJECT_SOURCE_DIR}/assets/models/lg_duelist_male_v3/art/exports/lg_duelist_male.glb"
    --instances 2
)

add_test(
  NAME lg_duel_asset_validate_worker_review
  COMMAND lg_duel_asset_validate
    "${PROJECT_SOURCE_DIR}/imports/assets/review/quaternius_worker/quaternius_worker.glb"
    --manifest "${PROJECT_SOURCE_DIR}/asset_pipeline/validation/player_model.json"
    --instances 16
)

add_test(
  NAME lg_duel_asset_validate_static_prop
  COMMAND lg_duel_asset_validate
    "${PROJECT_SOURCE_DIR}/assets/models/shotgun/lg_duel_shotgun_sg.glb"
    --instances 16
)

add_test(
  NAME lg_duel_asset_validate_rejects_missing_file
  COMMAND lg_duel_asset_validate "${PROJECT_BINARY_DIR}/missing-review-asset.glb"
)
set_tests_properties(lg_duel_asset_validate_rejects_missing_file PROPERTIES WILL_FAIL TRUE)

get_property(lg_duel_test_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(target IN LISTS lg_duel_test_targets)
  lg_duel_set_windows_stack(${target})
endforeach()
