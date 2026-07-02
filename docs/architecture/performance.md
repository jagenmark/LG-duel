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
- Main scaling factors are `kDuelPlayerCount`, `kMaxRocketProjectiles`, `arena.wallCount`, and `arena.brushCount`.
- Avoid allocations, logging, filesystem access, rendering work, and unbounded loops here.

Per client frame:

- `GameApp.cpp` builds presentation state, optionally advances local render prediction, computes remote views, HUD, lingering visual events, then calls `Renderer::render()`.
- GPU path uploads dynamic vertices per frame but caches static world mesh/textures by arena fingerprint.
- SDL_Renderer fallback draws immediate geometry and is less optimized.

Packet encode/decode:

- `NetCodec` writes/reads positional binary fields with strict validation.
- `WirePacket` reserves `kMaxPacketBytes`; snapshots can include arena data only when `includeArena`/`hasArena` is true.

## Caching And Batching

- Static world GPU cache: `StaticWorldMesh` in `Renderer.cpp`, keyed by `arenaStaticWorldFingerprint()`.
- Static textures are loaded from disk and uploaded to GPU resources, not embedded in per-frame packets.
- Snapshot map data is revision-gated by `mapRevision` and `hasArena`.
- Transient combat events use bounded arrays plus short retention windows instead of unbounded event logs.
- Client prediction stores only pending commands and replays them on reconciliation.

## Scaling Assumptions

- Players are capped by `kMaxPlayers`/`kDuelPlayerCount`.
- Active projectiles are capped by `kMaxRocketProjectiles`.
- Arena geometry counts are fixed-size in `Arena`.
- Scene geometry scales with visible players, active projectiles/effects, and arena geometry. Static world cost should be paid on arena change, not each frame on the GPU path.
- Network cost scales mostly with fixed snapshot fields, player count, projectile count, transient event windows, and only occasionally arena payload size.

## Rendering Budgets

Projectile visuals use cheap approximations: boxes, wire boxes, and low-detail spheres. Dynamic player/weapon/effect geometry is rebuilt each frame, so new effects should be low vertex count, pooled/cached where possible, or represented as simple segments/sprites.

Static world triangles may be larger, but should be built and uploaded only when the arena or material/light data changes. Debug render overlays and logs are gated by cvars or environment variables and should stay off by default.

`traceWorld()` instrumentation is gated by `r_perf` or `trace_record_start`. When `r_perf` is enabled, the detailed perf HUD reports trace calls, wall checks, convex brush candidates, exact brush tests, face checks, brush-box skips, and latest/average/p95 trace time. Keep it off for normal play; use `r_perf_reset` before repeated HUD measurement runs.

For route-based trace experiments, use `trace_record_start` at the route start and `trace_record_stop [csv_path]` at the route end. The recorder keeps every per-frame sample from that interval, writes the samples to CSV, and prints totals, per-frame averages, and trace-time percentiles. Use this before and after trace optimizations on the same map, route, resolution, player count, and visual settings; compare calls, brush candidates, exact brush tests, face checks, box skips, and total trace time.

## Networking Budgets

Do not send static map, mesh, texture, or verbose debug data every tick. Use ids, revisions, checksums, deltas, or explicit on-change payloads. New snapshot fields should answer whether the data is authoritative, whether clients can derive it, whether it can be quantized, and whether it is per-tick or event-only.

## Footguns

- Hidden allocations in helper functions still count when called per tick/frame/packet.
- Arena changes affect server collision, client render cache, network arena payload size, and map revision handling.
- Debug logging in render or server loops must be gated and rate-limited.
- A visual quality fix that increases vertices, draw calls, texture size, or per-frame uploads needs an explicit budget.
