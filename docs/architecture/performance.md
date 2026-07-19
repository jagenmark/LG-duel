# Performance Architecture

LG Duel is designed around predictable fixed-tick simulation, compact snapshots, bounded dynamic entities, cached static rendering, and simple visual geometry. Performance is an architecture constraint, not a cleanup pass.

## Goals

- Stable 125 Hz server simulation.
- Low client input latency through local movement prediction.
- Bounded packet sizes and no repeated static data in gameplay snapshots.
- Fast frame builds with cached static world geometry on the GPU path.
- Fixed-size or small bounded arrays for players, projectiles, and transient events.

## Hot Paths

Per server tick:

- `ServerGame::tick()` receives commands, updates match state, movement, collision, combat, projectiles, transient events, history, and sends a snapshot.
- Main scaling factors are `kDuelPlayerCount`, `kMaxRocketProjectiles`, `arena.wallCount`, `arena.brushCount`, and the fixed `arena.jumpPadCount`.
- Avoid allocations, logging, filesystem access, rendering work, and unbounded loops here.

Per client frame:

- `GameApp.cpp` builds presentation state, optionally advances local render prediction, computes remote views, HUD, lingering visual events, then calls `Renderer::render()`.
- GPU path uploads dynamic vertices per frame but caches static world mesh/textures by arena fingerprint.
- SDL_Renderer fallback draws immediate geometry and is less optimized.

Packet encode/decode:

- `NetCodec` writes/reads positional binary fields with strict validation.
- `WirePacket` reserves `kMaxPacketBytes`; snapshots can include arena data only when `includeArena`/`hasArena` is true.

## Caching And Batching

- Static world GPU cache: `StaticWorldMesh` in `Renderer.cpp`, keyed by `arenaStaticWorldFingerprint()`. Its renderer-owned triangle-chunk BVH is independent of the authoritative collision broadphase. The current direct-draw query is experimental and defaults off because the checked-in Overkill flythrough does not yet show an aggregate win; future GPU-driven visibility or map PVS can consume the same chunks.
- Static textures are loaded from disk and uploaded to GPU resources, not embedded in per-frame packets.
- Snapshot map data is revision-gated by `mapRevision` and `hasArena`.
- Transient combat events use bounded arrays plus short retention windows instead of unbounded event logs.
- Client prediction stores only pending commands and replays them on reconciliation.

## Scaling Assumptions

- Players are capped by `kMaxPlayers`/`kDuelPlayerCount`.
- Active projectiles are capped by `kMaxRocketProjectiles`.
- Arena geometry and gameplay trigger counts are fixed-size in `Arena`.
- Packaged static geometry builds an immutable flattened BVH at map-load time. Movement/world-trace queries use conservative candidates but replay narrow phases in authored order; unfinalized arenas retain the linear fallback.
- Scene geometry scales with visible players, active projectiles/effects, and arena geometry. Static world cost should be paid on arena change, not each frame on the GPU path.
- Network cost scales mostly with fixed snapshot fields, player count, projectile count, transient event windows, and only occasionally arena payload size.

## Rendering Budgets

Projectile visuals use cheap approximations: boxes, wire boxes, and low-detail spheres. Dynamic player/weapon/effect geometry is rebuilt each frame, so new effects should be low vertex count, pooled/cached where possible, or represented as simple segments/sprites.

Static world triangles may be larger, but should be built and uploaded only when the arena or material/light data changes. Debug render overlays and logs are gated by cvars or environment variables and should stay off by default.

## Networking Budgets

Do not send static map, mesh, texture, or verbose debug data every tick. Use ids, revisions, checksums, deltas, or explicit on-change payloads. New snapshot fields should answer whether the data is authoritative, whether clients can derive it, whether it can be quantized, and whether it is per-tick or event-only.

## Repeatable Benchmarks

Performance claims need repeatable artifacts, not a single on-screen FPS
reading. The opt-in benchmark suite loads versioned descriptors from
`config/benchmarks/`, uses fixed presentation state/camera semantics, warms
resources before sampling, and writes ignored results under `build/benchmarks/`.
It is separate from normal play and gameplay authority: benchmark state never
enters snapshots, UDP packets, or server simulation.

The primary result is a client-thread frame-time distribution plus labelled
CPU-side renderer diagnostics. SDL 3.4.10 and SDL_Renderer do not provide GPU
timestamp queries here, so submit/acquire durations must not be reported as
GPU execution time, allocation cost, or memory use. Record backend, selected
present mode, map hash, resolution, settings, and fallback state; compare only
compatible results. See [Performance benchmarks](../PERFORMANCE-BENCHMARKS.md)
for scenario fields, warmup boundaries, bot/effect limitations, captures,
repetition, validity checks, and interpretation.

Collision and trace changes must also run the headless shared-simulation workloads. They time the real movement and `traceWorld` paths, verify deterministic replay checksums, and support a forced-linear same-binary comparison so broadphase evidence is not inferred from renderer FPS.

## Footguns

- Hidden allocations in helper functions still count when called per tick/frame/packet.
- Arena changes affect server collision, client render cache, network arena payload size, and map revision handling.
- Debug logging in render or server loops must be gated and rate-limited.
- A visual quality fix that increases vertices, draw calls, texture size, or per-frame uploads needs an explicit budget.
