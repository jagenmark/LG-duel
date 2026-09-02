# LG Duel

LG Duel is a native C++ arena FPS used to build and test low-latency
multiplayer systems. It includes a playable client, a headless server, several
game modes, and a first-person SDL renderer.

The project aims to grow beyond its first Lightning Gun duel mode into an
original game with more movement, weapons, bodies, abilities, and team play.
See [Project context](docs/PROJECT_CONTEXT.md) for the full scope and design
rules.

## Features

- Fixed 125 Hz server tick
- UDP networking with client prediction and server correction
- Duel, Clan Arena, Free For All, and McGuffin modes
- Hitscan and projectile weapons with server-side lag correction
- SDL_GPU rendering with a 2D HUD and menu layer
- Quake and TrenchBroom text-map import

## Build and test

You need a C++20 compiler, CMake 3.24 or newer, Ninja or Visual Studio, and
SDL3 for the client.

Use the project presets:

```powershell
cmake --preset default
cmake --build --preset default
ctest --preset default
```

The default preset fetches the pinned SDL3 source into `build/default`. On
Windows, this command sets up the build and compiles the client:

```powershell
.\bootstrap-windows-client.cmd
```

The first setup needs network access to fetch SDL3.

## Run

Start a server:

```powershell
.\build\default\lg_duel_server.exe 27960
```

Start a client:

```powershell
.\build\default\lg_duel_client.exe 127.0.0.1 27960
```

On other systems, use the same executable names without `.exe`.

## Controls

- `W/A/S/D`: move
- Mouse: aim
- Left mouse: fire
- `Space`: jump
- `Ctrl`: crouch, or move down while flight is on
- `Shift`: walk quietly
- `1` through `9`: select a weapon
- `Q`, `E`, and `R`: quick weapon binds
- `F3`: ready
- `F5`: reset the match
- `Tab`: scoreboard
- Grave or section key: console
- `Esc`: quit

See the [console and cvar guide](docs/CONSOLE-BIBLE.md) for all controls,
commands, and settings.

## Maps and assets

Runtime maps live in `maps/`. Runtime textures live in `textures/`; shaders,
audio, and models live in `assets/`. Source and trial assets live in `art/`.
Game and client settings live in `config/`.

See [Mapmaking](docs/MAPMAKING.md) to set up TrenchBroom and create a map.

## Documentation

- [Project context](docs/PROJECT_CONTEXT.md)
- [Architecture](docs/architecture/README.md)
- [Mapmaking](docs/MAPMAKING.md)
- [Console commands and settings](docs/CONSOLE-BIBLE.md)
- [Developer control and visual capture](docs/DEVELOPER-CONTROL.md)
- [Performance tests](docs/PERFORMANCE-BENCHMARKS.md)
- [Task integration](docs/INTEGRATION-WORKFLOW.md)
