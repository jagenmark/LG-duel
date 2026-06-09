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
  src/app/GameApp.cpp
  src/render/Renderer.cpp
  src/sim/Arena.cpp
  src/sim/Movement.cpp
)

target_include_directories(lg_duel_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(lg_duel_core PUBLIC cxx_std_20)
lg_duel_set_warnings(lg_duel_core)
lg_duel_configure_sdl3(lg_duel_core)

add_executable(lg_duel
  src/app/main.cpp
)

target_link_libraries(lg_duel PRIVATE lg_duel_core)
lg_duel_set_warnings(lg_duel)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
