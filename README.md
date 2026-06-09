# LG Duel

A narrow C++ Lightning Gun duel prototype inspired by Quake-like arena combat.

This project is intentionally not a general-purpose FPS engine. The first goal is a small, testable 1v1 LG duel with fixed-tick simulation, raw mouse input, Quake-like movement, server-authoritative networking, prediction/reconciliation, and snapshot interpolation.

## Build

Required:

- C++20 compiler
- CMake 3.24+

Recommended:

- Ninja or Visual Studio 2022 on Windows
- SDL3 for window/input once the playable app starts using platform code

Configure, build, and test:

```powershell
cmake --preset default
cmake --build --preset default
ctest --preset default
```

Equivalent plain CMake commands:

```powershell
cmake -S . -B build/default -DBUILD_TESTING=ON
cmake --build build/default
ctest --test-dir build/default --output-on-failure
```

SDL3 is auto-detected for now. Set `LG_DUEL_REQUIRE_SDL3=ON` when the app should fail configuration if SDL3 is missing. Without SDL3, the app target still builds as a non-playable skeleton and the pure simulation tests remain available.

## Current Playable Slice

When built with SDL3, `lg_duel` runs a one-player local movement sandbox:

- `W/S`: forward/back
- `A/D`: strafe
- `Space`: jump / positive up command
- `Ctrl` or `Shift`: negative up command for future flight mode
- Mouse: raw relative look
- `Esc`: quit

The simulation runs at a fixed 125 Hz. The window title reports movement mode, position, and velocity; the renderer shows a simple top-down arena, player heading, speed bar, and height bar.
