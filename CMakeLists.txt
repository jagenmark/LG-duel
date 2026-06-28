cmake_minimum_required(VERSION 3.24)

project(
  lg_duel
  VERSION 0.1.0
  DESCRIPTION "Narrow Quake-like Lightning Gun duel prototype"
  LANGUAGES CXX
)

include(CTest)
include(cmake/CompilerWarnings.cmake)
include(cmake/Dependencies.cmake)

add_library(lg_duel_core
  src/client/ClientGame.cpp
  src/client/HitConfirmAudio.cpp
  src/client/ClientSession.cpp
  src/client/Interpolation.cpp
  src/client/LocalHitFeedback.cpp
  src/client/Prediction.cpp
  src/console/ConsoleSystem.cpp
  src/input/InputBindings.cpp
  src/map/MapParser.cpp
  src/map/MapToArena.cpp
  src/net/LoopbackTransport.cpp
  src/net/NetCodec.cpp
  src/net/SimulatedTransport.cpp
  src/net/UdpTransport.cpp
  src/server/ServerGame.cpp
  src/sim/Arena.cpp
  src/sim/ArenaMap.cpp
  src/sim/Collision.cpp
  src/sim/Combat.cpp
  src/sim/ClanArenaRules.cpp
  src/sim/DuelRules.cpp
  src/sim/GameplayConfig.cpp
  src/sim/Movement.cpp
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
  src/app/ConsoleInput.cpp
  src/app/GameApp.cpp
  src/app/HudPresentation.cpp
  src/app/Scoreboard.cpp
  src/app/TextInput.cpp
  src/render/BitmapFont.cpp
  src/render/ChatLayout.cpp
  src/render/ConsoleLayout.cpp
  src/render/Renderer.cpp
  src/render/Scene3D.cpp
  src/render/ScreenUi.cpp
  src/render/TopDownScene.cpp
)

target_link_libraries(lg_duel_client_app PUBLIC lg_duel_core)
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

add_custom_target(lg_duel_runtime_assets ALL
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
    "${CMAKE_CURRENT_SOURCE_DIR}/textures"
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

add_custom_target(lg_duel_validate_maps ALL
  COMMAND $<TARGET_FILE:lg_duel_map_validate> "${CMAKE_CURRENT_SOURCE_DIR}/maps"
  DEPENDS lg_duel_map_validate
  COMMENT "Validating maps"
)

add_dependencies(lg_duel_runtime_assets lg_duel_validate_maps)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
