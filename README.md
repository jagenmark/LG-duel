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

When built with SDL3, `lg_duel` runs a local loopback client/server lightning-gun sandbox:

- `W/S`: forward/back
- `A/D`: strafe
- `Space`: jump / positive up command
- `Ctrl` or `Shift`: negative up command for future flight mode
- Mouse: raw relative look
- Left mouse: fire the continuous lightning gun
- `R`: request an authoritative match reset
- `Esc`: quit

The server owns two complete player states and runs movement, player collision, beam tracing, full-vector LG knockback, continuous damage, death, timed respawn, and reset at a fixed 125 Hz. The local client sends sequenced commands through `LoopbackTransport` and renders received server snapshots. The window title reports server tick, command sequence/ack, prediction corrections, dropped simulation time, collision state, movement mode, target health/respawn, hit registration, position, and velocity.

Simulation catch-up is capped at eight ticks per rendered frame. Excess whole ticks are dropped and reported instead of allowing an unbounded spiral after a long stall.

The client predicts local movement immediately, reconciles against acknowledged authoritative snapshots, replays pending commands, and interpolates the remote player between snapshots. Prediction correction count, correction distance, and pending command count are shown in the window title.

## Network Protocol

Commands and snapshots use a versioned, explicitly serialized little-endian wire format with packet magic, packet type, payload length, fixed-width fields, and a 512-byte packet limit. Loopback transport uses this codec too, so local play exercises the same validation boundary intended for UDP. Decoding rejects incompatible versions, malformed lengths, invalid enums/booleans, oversized packets, and non-finite simulation values.

`SimulatedTransport` provides deterministic tick-based latency, jitter, packet loss, duplication, and reordering profiles independently for commands and snapshots. Its seeded behavior and packet statistics support reproducible netcode tests before UDP is introduced.

## Project Direction

The intended online version uses native SDL clients connected to a dedicated C++ server while retaining the top-down 2D presentation. A separate browser implementation is out of scope for the current roadmap. This keeps movement, combat, prediction, reconciliation, and protocol behavior in one C++ codebase.
