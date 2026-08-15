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

- Static world GPU cache: `StaticWorldMesh` in `Renderer.cpp`, keyed by `arenaStaticWorldFingerprint()`. Its renderer-owned triangle-chunk BVH is independent of the authoritative collision broadphase. `r_world_frustum_cull` keeps the CPU query path, while the guarded `r_world_gpu_indirect` prototype tests the same chunk AABBs in compute and writes one indirect command per chunk for the main-camera depth/color passes. The GPU path does not read commands back; benchmark results report command slots and material groups, while MainScene timestamps show GPU cost when available.
- Static textures are loaded from disk and uploaded to GPU resources, not embedded in per-frame packets.
- Snapshot map data is revision-gated by `mapRevision` and `hasArena`.
- Transient combat events use bounded arrays plus short retention windows instead of unbounded event logs.
- Client prediction stores only pending commands and replays them on reconciliation.

## Scaling Assumptions

- Players are capped by `kMaxPlayers`/`kDuelPlayerCount`.
- Active projectiles are capped at 32 slots per player. The server keeps the
  full fixed pool, while each network update carries only a bounded batch.
- Arena geometry and gameplay trigger counts are fixed-size in `Arena`.
- Packaged static geometry builds an immutable flattened BVH at map-load time. Movement/world-trace queries use conservative candidates but replay narrow phases in authored order; unfinalized arenas retain the linear fallback.
- Scene geometry scales with visible players, active projectiles/effects, and arena geometry. Static world cost should be paid on arena change, not each frame on the GPU path.
- Network cost scales mostly with fixed snapshot fields, player count, bounded
  projectile update batches, transient event windows, and only occasionally
  arena payload size.
- At the full 512-projectile bound, the 24-tick correction target sends up to
  22 correction records per tick: about 116 KB/s to one client or 1.9 MB/s
  across 16 clients before UDP/IP headers. A solo 25-shot plasma load needs
  two correction records per tick, about 13 KB/s. Spawn and removal bursts add
  short bounded packets; normal gameplay snapshots keep their separate
  1,200-byte limit.

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
CPU-side renderer diagnostics. The optional patched SDL_GPU Vulkan path also
records a GPU timestamp interval for measured work in the primary per-frame
command buffer. Results arrive after a delay and map back to the exact measured
frame id. The benchmark waits for outstanding results only after CPU sampling
ends. SDL_Renderer and builds without the patch record the reason and leave GPU
values empty. Submit/acquire time must not be reported as GPU execution time.
GPU timestamps do not measure present, compositor, scanout, queue wait, GPU
memory, or presentation latency. Record backend, selected present mode, map
hash, resolution, settings, timestamp details, SDL identity, and fallback
state; compare only compatible results. For GPU-generated world commands, compare
the CPU control and GPU mode on each target class. An Intel Core Ultra 7 258V
iGPU must be treated as its own low-power GPU target, not as a stand-in for a
discrete GPU. See
[Performance benchmarks](../PERFORMANCE-BENCHMARKS.md) for the full GPU scope,
scenario fields, warmup boundaries, bot/effect limits, captures, repetition,
validity checks, and interpretation.

Collision and trace changes must also run the headless shared-simulation workloads. They time the real movement and `traceWorld` paths, verify deterministic replay checksums, and support a forced-linear same-binary comparison so broadphase evidence is not inferred from renderer FPS.

## Footguns

- Hidden allocations in helper functions still count when called per tick/frame/packet.
- Arena changes affect server collision, client render cache, network arena payload size, and map revision handling.
- Debug logging in render or server loops must be gated and rate-limited.
- A visual quality fix that increases vertices, draw calls, texture size, or per-frame uploads needs an explicit budget.
