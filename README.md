# LG Duel

LG Duel is a playable reference game and testbed for a broader, high-performance competitive FPS foundation.

The project is inspired by Quake-like arena shooters, with particular focus on low input latency, stable frame pacing, high and predictable frame rates, authoritative networking, responsive client prediction, and clear competitive combat feedback.

LG Duel is not intended to remain only a small Lightning Gun duel prototype. Its current duel gameplay is used to develop, measure, and validate reusable FPS systems that can later support more weapons, projectiles, player bodies, maps, teams, abilities, and game modes.

The long-term goal is an original game in the space between arena FPS and hero shooter: mechanically expressive movement and combat, combined with distinct body archetypes, weapons, abilities, and team-oriented strategic variety.

See PROJECT_CONTEXT.md, located in C:\Users\gosee\Documents\Codex\LG-duel-clean\docs, for the project’s scope, architectural direction, performance priorities, current transitional systems, and implementation principles.

## Current Shape

LG Duel currently includes:

* Native C++ client and headless C++ server.
* Fixed 125 Hz authoritative server tick.
* Shared client/server movement, collision, combat, and map structures.
* UDP protocol with versioned command packets, command bundles, snapshots, ping/pong, connect, and disconnect packets.
* Local movement prediction, authoritative reconciliation, and buffered remote interpolation.
* Hitscan and projectile weapons, duel and clan-arena rules, transient combat/audio events, and server-side lag compensation for hitscan-style traces.
* First-person 3D SDL rendering with an SDL_GPU path for cached static world rendering, dynamic effects, player/weapon/projectile presentation, and a 2D HUD/UI overlay.
* Restricted Quake/TrenchBroom `.map` arena loading.

Several current rendering and content paths remain prototype or transitional implementations. They are documented as such in PROJECT_CONTEXT.md; new substantial work should converge toward the reusable architecture described there.

## Documentation

Project direction:

* [Project context and implementation principles](PROJECT_CONTEXT.md)

Architecture docs:

* [Architecture overview](docs/architecture/README.md)
* [Server tick](docs/architecture/server-tick.md)
* [Networking](docs/architecture/networking.md)
* [Combat and projectiles](docs/architecture/combat-projectiles.md)
* [Rendering](docs/architecture/rendering.md)
* [Maps and assets](docs/architecture/maps-assets.md)
* [Config and testing](docs/architecture/config-testing.md)
* [Performance](docs/architecture/performance.md)

Reference:

* [Console and cvar bible](docs/CONSOLE-BIBLE.md)


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
- `Ctrl`: crouch/duck when flight is off, move down when flight is on
- `Shift`: sneak / quiet walk
- `1`..`7`: select weapons
- `Q` / `E` / `R`: quick weapon binds
- `F3`: ready
- `F5`: reset match
- `Tab`: scoreboard
- grave/section key: console
- `Esc`: quit

Bindings, cvars, and console commands are documented in [docs/CONSOLE-BIBLE.md](docs/CONSOLE-BIBLE.md).

## Maps And Assets

Runtime maps live in `maps/` as restricted TrenchBroom/Quake `.map` files. Map requests are server-authoritative and replicated to clients by map revision.

Textures live under `textures/`; shaders, audio, and models live under `assets/`. Gameplay tuning files live under `config/`: authoritative non-cvar balance in `balance.cfg`, server startup cvars in `server_cvars.cfg`, default client cvars/binds in `default_client.cfg`, and client-only sound cue volumes in `sound_mixer.cfg`.

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


## Mapmaking

### Restricted TrenchBroom `.map` Workflow

LG Duel loads a narrow Quake/TrenchBroom text `.map` subset. This is an
authoring format only; it is not Quake or BSP compatibility.

Create a new TrenchBroom map, use cuboid or other convex brushes in
`worldspawn`, place player spawns with point entities named
`lg_spawn`, and save it as
`maps/<name>.map`. Coordinates are authored in Quake/TrenchBroom units
and imported at `1/40` scale, so `40` editor units become `1` LG Duel world
unit. A `16`-unit stair step therefore becomes `0.4` LG units, just below the
current walkable step height.

Spawn entities need an `origin` key like `"160 -120 40"` and may include
`angle` or `yaw` in degrees. Optional worldspawn keys `lg_bounds_min` and
`lg_bounds_max` can set arena bounds in the same Quake/TrenchBroom units;
otherwise bounds are computed from converted boxes and spawns with padding.
Static point lights may use `classname` `light` or `light_point` with an
`origin`. The importer accepts Quake-style `light` intensity, optional
`radius`, `_color`/`color` as `0..1` RGB triples, and `_light` as either an
intensity or `r g b intensity`. Light positions and radii are converted from
TrenchBroom units at the same `1/40` scale and are baked into static world
vertex colors in the first-person renderer. Outdoor maps may also define one
invisible `light_sun` entity with `direction` as the direction light rays
travel, for example `0 0 -1` for downward light. `light_sun` supports
`intensity`, `color`/`_color`, and `angle` plus `pitch` as a fallback when
`direction` is omitted.

Brush texture names are preserved as material ids and replicated to clients.
Referenced textures must exist under `textures/`; for example a TrenchBroom
face material `512x512/Brick/Brick_14-512x512` resolves to
`textures/512x512/Brick/Brick_14-512x512.png`. The SDL_GPU first-person
renderer samples those textures on cuboid wall faces. The old grass/brown
prototype treatment is no longer used.

The repository includes a TrenchBroom game setup in
`tools/trenchbroom/LG Duel/`. Install or copy that folder into TrenchBroom's
games directory to get the `lg_spawn` point entity and LG Duel worldspawn keys
in the editor.
