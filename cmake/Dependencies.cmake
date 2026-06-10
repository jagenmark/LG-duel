option(LG_DUEL_REQUIRE_SDL3 "Require SDL3 during CMake configuration" OFF)
option(LG_DUEL_FETCH_SDL3 "Fetch the pinned SDL3 source when no installed package is available" OFF)

set(
  LG_DUEL_SDL3_GIT_TAG
  "release-3.4.10"
  CACHE STRING
  "Pinned SDL3 release used when LG_DUEL_FETCH_SDL3 is enabled"
)

function(lg_duel_configure_sdl3 target)
  find_package(SDL3 QUIET CONFIG)

  if(NOT SDL3_FOUND AND LG_DUEL_FETCH_SDL3)
    include(FetchContent)
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
      SDL3
      GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
      GIT_TAG ${LG_DUEL_SDL3_GIT_TAG}
      GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(SDL3)
    set(SDL3_FOUND TRUE)
  endif()

  if(SDL3_FOUND)
    target_link_libraries(${target} PUBLIC SDL3::SDL3)
    target_compile_definitions(${target} PUBLIC LG_DUEL_HAS_SDL3=1)
  else()
    target_compile_definitions(${target} PUBLIC LG_DUEL_HAS_SDL3=0)

    if(LG_DUEL_REQUIRE_SDL3)
      message(FATAL_ERROR "SDL3 was requested but was not found. Install SDL3 or set LG_DUEL_REQUIRE_SDL3=OFF.")
    endif()

    message(STATUS "SDL3 not found; building non-SDL milestone skeleton.")
  endif()
endfunction()
