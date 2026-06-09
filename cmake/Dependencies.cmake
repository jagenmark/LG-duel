option(LG_DUEL_REQUIRE_SDL3 "Require SDL3 during CMake configuration" OFF)

function(lg_duel_configure_sdl3 target)
  find_package(SDL3 QUIET CONFIG)

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
