# LG Duel

A narrow C++ Lightning Gun duel prototype inspired by Quake-like arena combat.

Full client/server console reference: [CONSOLE-BIBLE.md](CONSOLE-BIBLE.md).

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

On Windows, prefer the preset commands above instead of plain `cmake -S . -B
build`, because CMake may otherwise choose the `NMake Makefiles` generator. If
`nmake` or the Visual Studio `cl` compiler is not available, configure with
Ninja and an explicit compiler:

```powershell
$env:Path += ";C:\Users\gosee\Documents\Codex\tools\cmake-4.3.3-windows-x86_64\bin;C:\Users\gosee\Documents\Codex\tools\ninja;C:\Users\gosee\Documents\Codex\tools\llvm-mingw-20260616-ucrt-x86_64\bin"
$env:CC = "C:\Users\gosee\Documents\Codex\tools\llvm-mingw-20260616-ucrt-x86_64\bin\clang.exe"
$env:CXX = "C:\Users\gosee\Documents\Codex\tools\llvm-mingw-20260616-ucrt-x86_64\bin\clang++.exe"
cmake --preset default
cmake --build --preset default
```

Use the preset build tree at `build/default` for local work. Do not use a
top-level `build/` directory for configure/build/test commands in this
repository.

Equivalent plain CMake commands:

```powershell
cmake -S . -B build/default -DBUILD_TESTING=ON
cmake --build build/default
ctest --test-dir build/default --output-on-failure
```

SDL3 is auto-detected for now. Set `LG_DUEL_REQUIRE_SDL3=ON` when the app
should fail configuration if SDL3 is missing. Without SDL3, the app target still
builds as a non-playable skeleton and the pure simulation tests remain
available.

For local Codex threads, SDL3 can be provided without network access by keeping
the pinned SDL3 source cache at `build/gpu/_deps/sdl3-src`. The CMake setup also
checks `../build/gpu/_deps/sdl3-src`, which helps when working from a cleaned-up
clone beside an older build. To point CMake at a specific local SDL3 checkout,
configure with:

```powershell
cmake --preset default -DLG_DUEL_SDL3_SOURCE_DIR="C:\path\to\sdl3-src"
```

When SDL3 is found this way, configure prints `Using local SDL3 source at ...`
and `lg_duel_client` builds as the playable SDL app instead of the skeleton.

## Windows Playtest Package

The `Windows Playtest Package` GitHub Actions workflow builds a self-contained
Windows x64 client ZIP. Run it from the repository's Actions page and enter the
current public server address and UDP port. Download the
`LG-Duel-Windows-x64` artifact when the run completes.

The package contains the client executable, `SDL3.dll`, the required GPU
shaders, a double-click `Play LG Duel.bat` launcher, the player guide, and
`server-address.txt`. The launcher selects SDL_GPU, preferring Vulkan with
automatic renderer fallback.
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
- `Ctrl` or `Shift`: negative flight thrust
- Mouse: raw relative look
- Left mouse: fire the selected weapon
- `Q` / `E` / `R`: select rocket launcher / lightning gun / railgun
- `F5`: request an authoritative match reset
- `F3`: toggle ready state
- `§` (the physical grave/section key left of `1`): toggle the client console
- `Esc`: quit

The server owns two complete player states and runs movement, player collision, beam tracing, full-vector LG knockback, continuous damage, scoring, synchronized round respawns, and match state at a fixed 125 Hz. For LG hit tests, it rewinds the target to the newest server snapshot tick visible to the shooter, capped at 25 ticks (200 ms), while applying damage and knockback to current authoritative state. `cl_show_lagcomp 1` displays the exact current target bounds in cyan and rewound bounds in amber, together with requested/applied ticks, clamping, historical tick, and both 3D positions. Clients render disposable authoritative snapshots while predicting local movement.

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

Gameplay actions follow Quake 3's naming scheme: `+forward`, `+back`, `+moveleft`, `+moveright`, `+moveup`, `+movedown`, `+attack`, and `+zoom`. Use `actionlist` in the console to list them. Game-specific commands include `weapon <lg|rg|rl|1|2|3>`, `resetmatch`, `toggleconsole`, and `quit`. Default weapon binds are `Q` for RL, `E` for LG, and `R` for RG. Key names are case-insensitive; `leftarrow`, `rightarrow`, `uparrow`, `downarrow`, `grave`, and `backquote` are accepted aliases. The canonical `section` key refers to the physical `§`/grave key left of `1`.

`connect <host> [port]` replaces the active connection. A numeric single argument is treated as a localhost port, so `connect 27960` connects to `127.0.0.1:27960`. `disconnect` releases the server slot immediately. `reconnect` uses the most recently requested host and port.

Initial client cvars include `sensitivity`, `cl_aim_mode`, `cl_fov`, `cl_zoom_fov`, `cl_zoom_sensitivity`, `cl_camera_zoom`, `cl_rotate_view`, `cl_health_size`, `cl_showfps`, `cl_showspeed`, `cl_show_net`, `cl_interp`, `g_playersize_xy`, `g_playersize_z`, `s_enable`, `s_volume`, `s_footstep_volume`, `r_vsync`, crosshair controls, beam controls, enemy model controls, and hit-feedback controls. `cl_showspeed 1` displays current horizontal movement speed in Q3/QL-style units per second. `cl_interp` controls remote player snapshot interpolation delay in seconds; the default `0.024` is three 125 Hz simulation ticks.

The SDL_GPU renderer can be selected at runtime with
`LG_DUEL_RENDER_BACKEND=gpu`. It prefers Vulkan, falls back to SDL's automatic
GPU backend selection, and then falls back to SDL_Renderer if no GPU backend
can claim the window. SDL_GPU renders both the backend-neutral top-down scene
and the first-person 3D scene. The 3D path uses world-space triangles, camera
uniforms, depth-tested solid arena geometry, separate non-depth-writing
translucent geometry, and a screen-space HUD pass. UI text uses a persistent
bitmap-font atlas.

The SDL_GPU path keeps one frame in flight. With `r_vsync 1`, it prefers
mailbox presentation and falls back to standard synchronized presentation.
With `r_vsync 0`, it requests immediate presentation for the lowest available
latency. Set `cl_showfps 1` to show average FPS, frame time, and the active
renderer backend in the window title.

`cl_rotate_view` applies only to top-down relative aim (`cl_render_mode 0`, `cl_aim_mode 0`). Absolute cursor aim (`cl_aim_mode 1`) is available only in the top-down renderer. Perspective mode (`cl_render_mode 1`) always uses relative mouse yaw/pitch, ignores `cl_rotate_view`, and sends true 3D pitch to authoritative beam simulation.

`r_enemy_lean 1` adds Q3-style velocity lean to the rendered enemy model in perspective mode. Tune it with `r_enemy_lean_scale`; `1` approximates Q3's `cg_runroll 0.005`, `0` disables the effect without changing local POV, simulation, hit registration, or networking.

Hold `Mouse2` (`+zoom`) to use `cl_zoom_fov` and zoom-scaled sensitivity. With the default `cl_zoom_sensitivity 0`, the multiplier is auto-matched as `tan(cl_zoom_fov / 2) / tan(cl_fov / 2)` using degree values. Set `cl_zoom_sensitivity` above zero for a manual multiplier. This is client-side view/input scaling only; it does not change simulation, hitboxes, damage, or networking.

`cl_camera_zoom 1` preserves the default top-down view. Values above `1` zoom in and values below `1` zoom out. `g_playersize_xy` and `g_playersize_z` request authoritative horizontal and vertical scales from `0.5` to `3.0`; the server applies them symmetrically to both players' collision bounds, hitboxes, and rendered model size.

Client audio uses generated tones at runtime, with footstep WAV previews checked in under `assets/audio`. `s_enable` toggles client cues, `s_volume` controls the global volume from `0` to `1`, and `s_footstep_volume` controls footsteps separately as a channel multiplier.

Runtime movement testing uses `g_accel`, `g_airaccel`, `g_friction`, `g_stopspeed`, `g_maxspeed`, `g_flight`, `g_flightaccel`, `g_flightmaxspeed`, and `g_flightdamping`. Changes are sent to the authoritative server and replicated to connected clients so prediction uses the same values. `g_flight 1` equips unrestricted flight symmetrically for both players. W/S thrust along full camera pitch/yaw, A/D strafe while upright, Space thrusts up, and Ctrl/Shift thrust down. Flight has no fuel, cooldown, duration limit, or artificial hover ceiling; arena collision still applies. Disabling it transitions players back to airborne or grounded movement. Query a variable without a value to see its current value, project default, and Q3/QL reference default where applicable. These testing values are intentionally not archived.

Hold `Tab` to show the scoreboard. It displays both replicated player names, round score, aggregate LG accuracy, and aggregate damage for the current match. Use `player <name>` in the client console to set a name.

`cl_render_mode 0` uses the standard top-down renderer. `cl_render_mode 1` uses
a first-person perspective view from the local player's yaw and pitch. It
renders a floor grid, solid arena walls, the opponent model, both lightning
beams, hit-color feedback, hitmarkers, and the shared HUD. SDL_Renderer remains
available as a compatibility fallback; SDL_GPU/Vulkan is the performance path.

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
