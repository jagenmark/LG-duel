# LG Duel

LG Duel is a compact C++ arena FPS focused on Quake-like duel combat: fixed-tick movement, authoritative server simulation, UDP snapshots, client prediction/reconciliation, and fast old-school rendering. The aim is to keep the simulation testable, keep packets small, keep frame and server tick costs predictable, and make gameplay changes easy to reason about.

## Current Shape

- Native C++ client and headless C++ server.
- Fixed 125 Hz authoritative server tick.
- Shared client/server movement, collision, combat, and map structures.
- UDP protocol with versioned command packets, command bundles, snapshots, ping/pong, connect, and disconnect packets.
- Local movement prediction, authoritative reconciliation, and buffered remote interpolation.
- Hitscan and projectile weapons, duel and clan-arena rules, transient combat/audio events, and server-side lag compensation for hitscan-style traces.
- SDL rendering with an SDL_GPU path for cached static world rendering and an SDL_Renderer fallback.
- Native `.lgmap` arena loading plus a restricted Quake/TrenchBroom `.map` import path.

## Build And Test

Requirements:

- C++20 compiler
- CMake 3.24+
- Ninja or Visual Studio generator on Windows
- SDL3 for the playable client

Use the repository presets:

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

The expected local build tree is `build/default`.

## Run Locally

Start a server:

```powershell
.\build\default\lg_duel_server.exe 27960
```

Start a client:

```powershell
.\build\default\lg_duel_client.exe 127.0.0.1 27960
```

On non-Windows shells, use the same executable names without `.exe`.

The Windows batch launchers in the repository root and packaged builds are convenience wrappers around the same client/server binaries.

## Controls

Default controls are intentionally simple:

- `W/A/S/D`: move
- Mouse: aim
- Left mouse: fire
- `Space`: jump
- `1`..`7`: select weapons
- `Q` / `E` / `R`: quick weapon binds
- `F3`: ready
- `F5`: reset match
- `Tab`: scoreboard
- grave/section key: console
- `Esc`: quit

Bindings, cvars, and console commands are documented in [docs/CONSOLE-BIBLE.md](docs/CONSOLE-BIBLE.md).

## Maps And Assets

Runtime maps live in `maps/`. The native format is `.lgmap`; restricted TrenchBroom/Quake `.map` files are also supported for authoring. Map requests are server-authoritative and replicated to clients by map revision.

Textures live under `textures/`; shaders, audio, and models live under `assets/`. Gameplay grenade tuning is loaded from `config/gameplay.cfg`.

See [docs/architecture/maps-assets.md](docs/architecture/maps-assets.md) for the map pipeline, limitations, texture/material assumptions, and collision/render data split.

## Documentation

Architecture docs:

- [Architecture overview](docs/architecture/README.md)
- [Server tick](docs/architecture/server-tick.md)
- [Networking](docs/architecture/networking.md)
- [Combat and projectiles](docs/architecture/combat-projectiles.md)
- [Rendering](docs/architecture/rendering.md)
- [Maps and assets](docs/architecture/maps-assets.md)
- [Config and testing](docs/architecture/config-testing.md)
- [Performance](docs/architecture/performance.md)

Reference:

- [Console and cvar bible](docs/CONSOLE-BIBLE.md)
