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

## Windows Playtest Package

The `Windows Playtest Package` GitHub Actions workflow builds a self-contained
Windows x64 client ZIP. Run it from the repository's Actions page and enter the
current public server address and UDP port. Download the
`LG-Duel-Windows-x64` artifact when the run completes.

The package contains the client executable, `SDL3.dll`, a double-click
`Play LG Duel.bat` launcher, the player guide, and `server-address.txt`.
Friends should extract the entire ZIP and keep all files together.

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

The server assigns player slots during a version-checked handshake. The client retries connection requests, sends the latest three sequenced commands in each UDP datagram, measures ping with tokenized ping/pong packets, and times out silent connections after five seconds. The client remains open when disconnected or when a connection attempt fails.

Client controls:

- `W/S`: forward/back
- `A/D`: strafe
- `Space`: jump / positive up command
- `Ctrl` or `Shift`: negative up command for future flight mode
- Mouse: raw relative look
- Left mouse: fire the continuous lightning gun
- `R`: request an authoritative match reset
- `F3`: toggle ready state
- `§` (the physical grave/section key left of `1`): toggle the client console
- `Esc`: quit

The server owns two complete player states and runs movement, player collision, beam tracing, full-vector LG knockback, continuous damage, scoring, synchronized round respawns, and match state at a fixed 125 Hz. For LG hit tests, it rewinds the target to the newest server snapshot tick visible to the shooter, capped at 25 ticks (200 ms), while applying damage and knockback to current authoritative state. Clients render disposable authoritative snapshots while predicting local movement. The HUD presents match information while optional diagnostics remain available in the window title.

The default arena is a compact 2D Thunderstruck-inspired layout with opposing courts, a broken central divider, offset cover blocks, and upper/lower connector lanes. Internal walls use the same geometry for authoritative movement collision, player separation, LG occlusion, prediction, and rendering.

LG hits brighten the target and draw a hitmarker at the beam impact point. Round and match result screens show server-authoritative LG contact accuracy and damage dealt for both players.

## Client Console

The client console supports typed cvars, validation, key bindings, command and cvar listing, help, history, autocomplete, and archived configuration. Press `§` to open it, Enter to execute, Up/Down for history, and Tab to complete. Archived values and bindings are saved to the platform-specific SDL preferences directory as `client.cfg`.

Core commands:

```text
set <cvar> [value]
toggle <bool cvar>
reset <cvar>
help <cvar|command>
cvarlist
cmdlist
clear
net_stats
writeconfig
quit
connect <host> [port]
disconnect
reconnect
bind <key> <command>
unbind <key>
unbindall
bindlist
actionlist
```

Button-style commands automatically receive their matching release command. For example:

```text
unbind a
bind leftarrow "+moveleft"
bind mouse1 "+attack"
bind section "toggleconsole"
```

Gameplay actions follow Quake 3's naming scheme: `+forward`, `+back`, `+moveleft`, `+moveright`, `+moveup`, `+movedown`, and `+attack`. Use `actionlist` in the console to list them. Game-specific one-shot commands include `resetmatch`, `toggleconsole`, and `quit`. Key names are case-insensitive; `leftarrow`, `rightarrow`, `uparrow`, `downarrow`, `grave`, and `backquote` are accepted aliases. The canonical `section` key refers to the physical `§`/grave key left of `1`.

`connect <host> [port]` replaces the active connection. A numeric single argument is treated as a localhost port, so `connect 27960` connects to `127.0.0.1:27960`. `disconnect` releases the server slot immediately. `reconnect` uses the most recently requested host and port.

Initial client cvars include `sensitivity`, `cl_aim_mode`, `cl_fov`, `cl_camera_zoom`, `cl_rotate_view`, `cl_health_size`, `cl_showfps`, `cl_show_net`, `s_enable`, `s_volume`, `r_vsync`, `r_playersize`, `crosshair_enable`, `crosshair_style`, crosshair size/gap/thickness/alpha/RGB controls, and beam width/alpha/RGB controls.

`cl_rotate_view` applies to relative aim mode (`cl_aim_mode 0`). Absolute cursor aim (`cl_aim_mode 1`) keeps the camera world-aligned so the simulated facing direction remains stable and points toward the cursor.

`cl_camera_zoom 1` preserves the default view. Values above `1` zoom in and values below `1` zoom out. `r_playersize` independently sets both player markers' width and height in screen pixels.

Client audio uses generated tones with no external sound assets. `s_enable` toggles hit and round-result cues, while `s_volume` controls their volume from `0` to `1`.

Runtime movement testing uses `g_accel`, `g_friction`, and `g_maxspeed`. Changes are sent to the authoritative server and replicated to connected clients so prediction uses the same values. `g_maxspeed` controls both the ground and air speed caps. These testing values are intentionally not archived.

Simulation catch-up is capped at eight ticks per rendered frame. Excess whole ticks are dropped and reported instead of allowing an unbounded spiral after a long stall.

The client predicts local movement immediately, reconciles against acknowledged authoritative snapshots, replays pending commands, and interpolates the remote player between snapshots. Prediction correction count, correction distance, and pending command count are shown in the window title.

## Match Flow

The HUD shows connection state, connected-player count, health, score, ready prompts, countdown, round result, and match result. With the default rules:

- Both player slots must be occupied.
- Each player presses `F3` to ready up.
- Every round starts with a five-second countdown.
- Movement remains enabled during countdown; the server rejects attacks until it expires.
- A kill awards one round and respawns both players for the next countdown.
- The first player to 10 rounds wins the match.
- After match end, scores and readiness reset.

The dedicated server accepts console commands on standard input. Authoritative match settings are:

```text
sv_roundlimit 10
sv_timelimit 0
sv_playerlimit 2
sv_countdown 5
sv_roundend 1
sv_matchend 5
sv_showopponenthealth 0
```

`sv_timelimit` is expressed in minutes and `0` disables it. The server replicates these settings to both clients. Opponent health is hidden by default; `sv_showopponenthealth 1` enables it symmetrically for everyone.

## Network Protocol

Handshakes, redundant command bundles, snapshots, and ping/pong messages use a versioned, explicitly serialized little-endian wire format with packet magic, packet type, payload length, fixed-width fields, and a 512-byte packet limit. Loopback transport uses this codec too. Decoding rejects incompatible versions, malformed lengths, invalid enums/booleans, oversized packets, and non-finite simulation values.

`SimulatedTransport` provides deterministic tick-based latency, jitter, packet loss, duplication, and reordering profiles independently for commands and snapshots. Its seeded behavior and packet statistics support reproducible netcode tests before UDP is introduced.

## Project Direction

The intended online version uses native SDL clients connected to a dedicated C++ server while retaining the top-down 2D presentation. A separate browser implementation is out of scope for the current roadmap. This keeps movement, combat, prediction, reconciliation, and protocol behavior in one C++ codebase.
