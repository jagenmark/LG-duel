option(LG_DUEL_REQUIRE_SDL3 "Require SDL3 during CMake configuration" OFF)
option(LG_DUEL_FETCH_SDL3 "Fetch the pinned SDL3 source when no installed package is available" OFF)
option(LG_DUEL_USE_PATCHED_SDL3 "Build the pinned SDL3 source with LG Duel GPU timestamps" OFF)
set(
  LG_DUEL_SDL3_SOURCE_DIR
  ""
  CACHE PATH
  "Optional local SDL3 source directory to use before fetching from GitHub"
)

set(
  LG_DUEL_SDL3_GIT_TAG
  "8e37db5e797b6167f3a00d697d816a684bd259c7"
  CACHE STRING
  "Pinned SDL3 revision used when LG_DUEL_FETCH_SDL3 is enabled"
)

set(LG_DUEL_SDL3_PATCH_BASE "8e37db5e797b6167f3a00d697d816a684bd259c7")
set(LG_DUEL_SDL3_PATCH_FILE "${CMAKE_SOURCE_DIR}/third_party/sdl3-gpu-timestamps.patch")

function(lg_duel_set_sdl3_build_options)
  set(SDL_SHARED ON CACHE BOOL "" FORCE)
  set(SDL_STATIC OFF CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
endfunction()

function(lg_duel_apply_sdl3_patch source_dir)
  if(NOT EXISTS "${LG_DUEL_SDL3_PATCH_FILE}")
    message(FATAL_ERROR "Patched SDL3 was requested, but ${LG_DUEL_SDL3_PATCH_FILE} is missing.")
  endif()

  find_package(Git REQUIRED)
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
    WORKING_DIRECTORY "${source_dir}"
    RESULT_VARIABLE revision_result
    OUTPUT_VARIABLE source_revision
    ERROR_VARIABLE revision_error
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if(NOT revision_result EQUAL 0)
    message(FATAL_ERROR "Could not read the SDL3 source revision at ${source_dir}: ${revision_error}")
  endif()
  if(NOT source_revision STREQUAL LG_DUEL_SDL3_PATCH_BASE)
    message(
      FATAL_ERROR
      "The LG Duel SDL3 patch needs ${LG_DUEL_SDL3_PATCH_BASE}, but ${source_dir} is at ${source_revision}."
    )
  endif()

  execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --check --whitespace=nowarn "${LG_DUEL_SDL3_PATCH_FILE}"
    WORKING_DIRECTORY "${source_dir}"
    RESULT_VARIABLE patch_check_result
    ERROR_VARIABLE patch_check_error
  )
  if(patch_check_result EQUAL 0)
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" apply --whitespace=nowarn "${LG_DUEL_SDL3_PATCH_FILE}"
      WORKING_DIRECTORY "${source_dir}"
      RESULT_VARIABLE patch_result
      ERROR_VARIABLE patch_error
    )
    if(NOT patch_result EQUAL 0)
      message(FATAL_ERROR "Could not apply the LG Duel SDL3 patch: ${patch_error}")
    endif()
    message(STATUS "Applied the LG Duel SDL3 GPU timestamp patch")
  else()
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" apply --reverse --check --whitespace=nowarn "${LG_DUEL_SDL3_PATCH_FILE}"
      WORKING_DIRECTORY "${source_dir}"
      RESULT_VARIABLE reverse_check_result
      ERROR_VARIABLE reverse_check_error
    )
    if(NOT reverse_check_result EQUAL 0)
      message(
        FATAL_ERROR
        "The SDL3 source at ${source_dir} cannot take the LG Duel patch and does not already contain it. "
        "Apply check: ${patch_check_error} Reverse check: ${reverse_check_error}"
      )
    endif()
    message(STATUS "The LG Duel SDL3 GPU timestamp patch is already applied")
  endif()
endfunction()

function(lg_duel_configure_sdl3 target)
  if(LG_DUEL_USE_PATCHED_SDL3)
    set(sdl3_source_dir "")

    if(LG_DUEL_SDL3_SOURCE_DIR)
      set(sdl3_source_dir "${LG_DUEL_SDL3_SOURCE_DIR}")
    elseif(LG_DUEL_FETCH_SDL3)
      include(FetchContent)
      FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG ${LG_DUEL_SDL3_PATCH_BASE}
        GIT_SHALLOW FALSE
        # Populate first so the patch lands before SDL's CMake code runs.
        SOURCE_SUBDIR lg-duel-populate-only
      )
      FetchContent_MakeAvailable(SDL3)
      set(sdl3_source_dir "${sdl3_SOURCE_DIR}")
    else()
      message(
        FATAL_ERROR
        "LG_DUEL_USE_PATCHED_SDL3 needs LG_DUEL_SDL3_SOURCE_DIR or LG_DUEL_FETCH_SDL3=ON."
      )
    endif()

    if(NOT EXISTS "${sdl3_source_dir}/CMakeLists.txt")
      message(FATAL_ERROR "SDL3 source was not found at ${sdl3_source_dir}.")
    endif()

    lg_duel_apply_sdl3_patch("${sdl3_source_dir}")
    lg_duel_set_sdl3_build_options()
    add_subdirectory(
      "${sdl3_source_dir}"
      "${CMAKE_BINARY_DIR}/_deps/sdl3-patched-build"
      EXCLUDE_FROM_ALL
    )

    # Keep build identity stable across Git's Windows line-ending settings.
    file(READ "${LG_DUEL_SDL3_PATCH_FILE}" sdl3_patch_contents)
    string(REPLACE "\r\n" "\n" sdl3_patch_contents "${sdl3_patch_contents}")
    string(REPLACE "\r" "\n" sdl3_patch_contents "${sdl3_patch_contents}")
    string(SHA256 sdl3_patch_sha256 "${sdl3_patch_contents}")
    string(TOLOWER "${sdl3_patch_sha256}" sdl3_patch_sha256)
    target_link_libraries(${target} PUBLIC SDL3::SDL3)
    target_compile_definitions(
      ${target}
      PUBLIC
        LG_DUEL_HAS_SDL3=1
        LG_DUEL_SDL_GPU_TIMESTAMP_EXT=1
        "LG_DUEL_SDL_BASE_REVISION=\"${LG_DUEL_SDL3_PATCH_BASE}\""
        "LG_DUEL_SDL_PATCH_IDENTITY=\"sha256:${sdl3_patch_sha256}\""
    )
    return()
  endif()

  target_compile_definitions(
    ${target}
    PUBLIC
      LG_DUEL_SDL_GPU_TIMESTAMP_EXT=0
      "LG_DUEL_SDL_BASE_REVISION=\"\""
      "LG_DUEL_SDL_PATCH_IDENTITY=\"\""
  )

  find_package(SDL3 QUIET CONFIG)

  if(NOT SDL3_FOUND AND LG_DUEL_SDL3_SOURCE_DIR)
    if(NOT EXISTS "${LG_DUEL_SDL3_SOURCE_DIR}/CMakeLists.txt")
      message(FATAL_ERROR "SDL3 source was not found at ${LG_DUEL_SDL3_SOURCE_DIR}.")
    endif()
    message(STATUS "Using explicit SDL3 source at ${LG_DUEL_SDL3_SOURCE_DIR}")
    lg_duel_set_sdl3_build_options()
    add_subdirectory(
      "${LG_DUEL_SDL3_SOURCE_DIR}"
      "${CMAKE_BINARY_DIR}/_deps/sdl3-local-build"
      EXCLUDE_FROM_ALL
    )
    set(SDL3_FOUND TRUE)
  endif()

  if(NOT SDL3_FOUND AND LG_DUEL_FETCH_SDL3)
    include(FetchContent)
    lg_duel_set_sdl3_build_options()
    FetchContent_Declare(
      SDL3
      GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
      GIT_TAG ${LG_DUEL_SDL3_GIT_TAG}
      GIT_SHALLOW FALSE
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
      message(
        FATAL_ERROR
        "SDL3 was requested but was not found. Set LG_DUEL_FETCH_SDL3=ON, "
        "set LG_DUEL_SDL3_SOURCE_DIR, install SDL3, or set LG_DUEL_REQUIRE_SDL3=OFF."
      )
    endif()

    message(STATUS "SDL3 not found; building non-SDL milestone skeleton.")
  endif()
endfunction()
