# Maps And Assets

Map data ends as an `Arena` from `src/sim/Arena.hpp`: bounds, fixed-size cuboid walls, convex brushes, static lights, optional sun light, and spawn positions. The same arena is used for server collision/combat traces and client rendering.

## Loading Pipeline

`.lgmap` text is parsed by `loadArenaFromText()` in `src/sim/ArenaMap.cpp` via `loadArenaFromFile()`. The embedded fallback arena is `thunderstruckArena()` in `src/sim/Arena.cpp`, unless `LG_DUEL_MAP` points to a loadable map.

Quake `.map` support is a conversion layer:

- `src/map/MapParser.*` parses entities, properties, brushes, and Quake-style face texture parameters.
- `src/map/MapToArena.*` converts `worldspawn` and `func_group` brushes to cuboid `ArenaWall`s when possible, otherwise convex `ArenaBrush` hulls.
- `lg_spawn` entities become spawn positions.
- `light`/`light_point` and `light_sun` become static lighting data.
- `trigger_teleport` is currently ignored.

Server map requests flow through `ServerGame::loadRequestedMap()`. Names are restricted to simple stems/extensions, resolved under `mapDirectory_`, and attempted as `.lgmap` then `.map` when no extension is given. Successful loads call `setArena()`, bump `mapRevision_`, reset the match, and force clients to receive updated arena data.

## Collision Vs Render Data

Collision and traces use `ArenaWall` AABBs and `ArenaBrush` convex planes/vertices. Rendering uses the same structures plus material ids, face material ids, texture projections, and light data. There is no separate server-only collision asset yet, so avoid adding render-only heavyweight data to `Arena` unless it is revision-gated and justified.

## Units, Materials, And Textures

Quake map units convert to LG units with `1 / 40`. Texture projection stores Quake-space axes/offsets/scales and rendering multiplies LG positions back by 40 for UV generation.

Materials are hashed/stable ids from material paths. Renderer texture loading expects PNGs under `textures`, with aliases both including and excluding `.png`. Windows packages copy only texture PNGs referenced by at least one map face, so new map materials must resolve under `textures`. Packaged builds also rely on shaders under `assets/shaders` and audio/model assets under `assets`.

## Current Limitations

- Valve 220 texture axes are explicitly rejected by `MapParser`.
- Convex brush limits are fixed: `ArenaBrush::kMaxFaces`, `kMaxVertices`, and per-face max vertices.
- Arena counts are fixed: 255 walls, 128 brushes, 64 static lights.
- Multiple `light_sun` entities are not supported.
- Spawn yaw is parsed only for validity in `.map` conversion; converted output currently writes `yaw=0`, so actual orientation intent is unclear.
- Teleport triggers are parsed as ignored entities, not gameplay.

## Footguns

- Do not replicate map or texture data every tick. Use map revision and `hasArena`.
- Keep server map loading out of the tick hot path except explicit map-change requests.
- Be careful adding fields to `Arena`: they affect network snapshot size when arena data is sent and static-world cache fingerprints.
- If adding asset packaging assumptions, update packaging scripts and docs together.
