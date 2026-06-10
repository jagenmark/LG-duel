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
  src/client/ClientSession.cpp
  src/client/Interpolation.cpp
  src/client/Prediction.cpp
  src/console/ConsoleSystem.cpp
  src/input/InputBindings.cpp
  src/net/LoopbackTransport.cpp
  src/net/NetCodec.cpp
  src/net/SimulatedTransport.cpp
  src/net/UdpTransport.cpp
  src/server/ServerGame.cpp
  src/sim/Arena.cpp
  src/sim/Collision.cpp
  src/sim/Combat.cpp
  src/sim/Movement.cpp
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
  src/app/GameApp.cpp
  src/render/Renderer.cpp
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

add_executable(lg_duel_server
  src/server/main.cpp
  src/server/ServerApp.cpp
)

target_link_libraries(lg_duel_server PRIVATE lg_duel_core)
lg_duel_set_warnings(lg_duel_server)

add_executable(lg_duel_server_probe
  tools/ServerProbe.cpp
)

target_link_libraries(lg_duel_server_probe PRIVATE lg_duel_core)
lg_duel_set_warnings(lg_duel_server_probe)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
