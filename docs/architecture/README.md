# Architecture Overview

LG Duel is a small fixed-tick arena FPS. The architecture is split around an authoritative server simulation, a client that predicts only its own movement, shared simulation code, compact UDP packets, and renderer-facing presentation data.

## Main Systems

- Client app: `src/app/GameApp.cpp` owns SDL setup, input, cvars, audio, HUD state, render pacing, and the per-frame loop.
- Client networking/game state: `src/client/ClientSession.*` owns connection state; `src/client/ClientGame.*` sends commands, receives snapshots, stores the current arena, runs local prediction, and advances interpolation.
- Server: `src/server/ServerApp.*` hosts transports and ticks `src/server/ServerGame.*`, which owns authoritative match state, commands, bots, combat, projectiles, transient events, history, and snapshot publishing.
- Shared simulation: `src/sim/Movement.*`, `src/sim/Combat.*`, `src/sim/Collision.*`, `src/sim/Arena.*`, `src/sim/DuelRules.*`, and `src/sim/ClanArenaRules.*` are used by both client and server where deterministic behavior matters.
- Networking: `src/net/NetProtocol.hpp` defines packet/snapshot structs; `src/net/NetCodec.*` defines the wire layout and validation; `src/net/UdpTransport.*` and loopback/simulated transports move packets.
- Rendering: `src/render/Renderer.*` selects SDL_GPU or SDL_Renderer fallback. `src/render/Scene3D.*`, `TopDownScene.*`, and `ScreenUi.*` build renderable geometry/UI from simulation snapshots and presentation state.
- Maps/assets/config: `src/map/MapParser.*` and `MapToArena.*` load restricted Quake `.map` files into `src/sim/Arena.*`; `config/gameplay.cfg` currently configures authoritative grenade tuning.
- Tests: `tests/CMakeLists.txt` defines focused executables for sim, net/protocol, server, client prediction, render scene building, cvars, input, HUD, audio, and smoke coverage.

## Core Flow

The server ticks at `kFixedTickRate` (`125 Hz` in `src/shared/Constants.hpp`). Clients send `CommandPacket`s with movement, view angles, selected weapon, optional cvar/tuning requests, chat/name/map requests, and the server tick they were viewing. The server applies accepted commands, advances simulation, publishes a `ServerSnapshot`, and acknowledges command sequence numbers.

The local client predicts its own `PlayerState` by replaying unacknowledged commands through shared movement code. Remote players are interpolated from buffered snapshots. Combat and projectiles are authoritative on the server; client-side lingering beams, hit feedback, audio, and HUD effects are visual presentation only.

## Focused Docs

- [Server Tick](server-tick.md)
- [Networking](networking.md)
- [Combat And Projectiles](combat-projectiles.md)
- [Rendering](rendering.md)
- [Maps And Assets](maps-assets.md)
- [Config And Testing](config-testing.md)
- [Performance](performance.md)

## Invariants And Footguns

- The server is authoritative for health, positions, selected weapons, cooldowns, projectiles, match phase, scores, and map revision.
- Clients may predict movement, but must reconcile to acknowledged authoritative snapshots.
- `ServerSnapshot::arena` is large and should only be sent when `hasArena`/revision requires it.
- Protocol layout is positional; changing packet fields requires updating both encoder and decoder and bumping `kProtocolVersion`.
- Static render geometry should be cached or rebuilt only when the arena/material state changes.
- Avoid adding allocation-heavy work to server ticks, packet encode/decode, prediction, or per-frame scene construction.
