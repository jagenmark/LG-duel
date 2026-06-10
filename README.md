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

The build produces:

- `lg_duel_server`: headless authoritative UDP server
- `lg_duel_client`: native SDL top-down client

Start a local server:

```bash
./build/default/lg_duel_server 27960
```

Start up to two clients:

```bash
./build/default/lg_duel_client 127.0.0.1 27960
```

The server assigns player slots during a version-checked handshake. The client retries connection requests, sends the latest three sequenced commands in each UDP datagram, measures ping with tokenized ping/pong packets, and times out silent connections after five seconds.

Client controls:

- `W/S`: forward/back
- `A/D`: strafe
- `Space`: jump / positive up command
- `Ctrl` or `Shift`: negative up command for future flight mode
- Mouse: raw relative look
- Left mouse: fire the continuous lightning gun
- `R`: request an authoritative match reset
- `Esc`: quit

The server owns two complete player states and runs movement, player collision, beam tracing, full-vector LG knockback, continuous damage, death, timed respawn, and reset at a fixed 125 Hz. For LG hit tests, it rewinds the target to the newest server snapshot tick visible to the shooter, capped at 25 ticks (200 ms), while applying damage and knockback to current authoritative state. Clients render disposable authoritative snapshots while predicting local movement. The window title reports assigned player, ping, server tick, command sequence/ack, requested/applied rewind, prediction corrections, dropped simulation time, collision state, movement mode, target health/respawn, hit registration, position, and velocity.

Simulation catch-up is capped at eight ticks per rendered frame. Excess whole ticks are dropped and reported instead of allowing an unbounded spiral after a long stall.

The client predicts local movement immediately, reconciles against acknowledged authoritative snapshots, replays pending commands, and interpolates the remote player between snapshots. Prediction correction count, correction distance, and pending command count are shown in the window title.

## Network Protocol

Handshakes, redundant command bundles, snapshots, and ping/pong messages use a versioned, explicitly serialized little-endian wire format with packet magic, packet type, payload length, fixed-width fields, and a 512-byte packet limit. Loopback transport uses this codec too. Decoding rejects incompatible versions, malformed lengths, invalid enums/booleans, oversized packets, and non-finite simulation values.

`SimulatedTransport` provides deterministic tick-based latency, jitter, packet loss, duplication, and reordering profiles independently for commands and snapshots. Its seeded behavior and packet statistics support reproducible netcode tests before UDP is introduced.

## Project Direction

The intended online version uses native SDL clients connected to a dedicated C++ server while retaining the top-down 2D presentation. A separate browser implementation is out of scope for the current roadmap. This keeps movement, combat, prediction, reconciliation, and protocol behavior in one C++ codebase.
