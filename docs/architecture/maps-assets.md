# Maps And Assets

Map data ends as an `Arena` from `src/sim/Arena.hpp`: bounds, fixed-size cuboid walls, convex brushes, fixed-size jumppad triggers, static lights, optional sun light, and spawn positions. Solid geometry is used for server collision/combat traces and client rendering; jumppad triggers are gameplay-only.

## Loading Pipeline

Runtime maps are restricted Quake/TrenchBroom `.map` files parsed by `loadArenaFromMapText()` in `src/map/MapParser.cpp` and converted by `src/map/MapToArena.cpp` via `loadArenaFromFile()`. The embedded fallback arena is `thunderstruckArena()` in `src/sim/Arena.cpp`, unless `LG_DUEL_MAP` points to a loadable map.

`.map` support is a conversion layer:

- `src/map/MapParser.*` parses entities, properties, brushes, and Quake-style face texture parameters.
- `src/map/MapToArena.*` converts `worldspawn` and `func_group` brushes to cuboid `ArenaWall`s when possible, otherwise convex `ArenaBrush` hulls.
- `lg_spawn` entities become spawn positions.
- `trigger_jumppad` brush entities become non-solid, non-rendered `ArenaJumpPad` trigger AABBs. Visible pad surfaces should be ordinary `worldspawn` or `func_group` geometry; the trigger brush can use `common/trigger` or `textures/common/trigger` for editor visibility only.
- `worldspawn`/`func_group` brushes using `common/playerclip` or `textures/common/playerclip` on every face become collision-only solids. They stay in `ArenaWall`/`ArenaBrush` for collision and traces, but `renderable=false` keeps them out of static world rendering and lighting. Mixed playerclip/non-playerclip brushes are rejected; apply playerclip to the whole brush.
- `target_position` point entities provide optional jumppad landing targets by `targetname`.
- `light`/`light_point` and `light_sun` become static lighting data.
- `trigger_teleport` is currently ignored.

Server map requests flow through `ServerGame::loadRequestedMap()`. Names are restricted to simple stems/extensions, resolved under `mapDirectory_`, and attempted as `.map` when no extension is given. Successful loads call `setArena()`, bump `mapRevision_`, reset the match, and force clients to receive updated arena data.

## Collision Vs Render Data

Collision and traces use `ArenaWall` AABBs and `ArenaBrush` convex planes/vertices. Rendering uses the same structures plus material ids, face material ids, texture projections, light data, and a `renderable` bit. Playerclip solids keep collision data but skip render geometry. `ArenaJumpPad` data is not solid, is not rendered, and is checked only by movement. There is no separate server-only collision asset yet, so avoid adding render-only heavyweight data to `Arena` unless it is revision-gated and justified.

## Units, Materials, And Textures

Quake map units convert to LG units with `1 / 40`. Texture projection stores Quake-space axes/offsets/scales and rendering multiplies LG positions back by 40 for UV generation.

Materials are hashed/stable ids from material paths. Renderer texture loading expects PNGs under `textures`, with aliases both including and excluding `.png`. Windows packages copy only texture PNGs referenced by at least one map face, so new map materials must resolve under `textures`. Packaged builds also rely on shaders under `assets/shaders` and audio/model assets under `assets`.

`textures/common/playerclip.png` is an editor-only visible tool texture. Mapper workflow for smoothing stairs:

1. Build the normal visible stair brushes.
2. Create a ramp brush over the stair tops.
3. Apply `common/playerclip` to every face of the ramp.
4. Keep the visible stairs for presentation; the ramp is invisible in-game but solid for player movement.

## Current Limitations

- Valve 220 texture axes are explicitly rejected by `MapParser`.
- Convex brush limits are fixed: `ArenaBrush::kMaxFaces`, `kMaxVertices`, and per-face max vertices.
- Arena counts are fixed: 255 walls, 128 brushes, 32 jumppads, 64 static lights.
- Multiple `light_sun` entities are not supported.
- Spawn yaw is parsed only for validity; spawn orientation is not stored in `Arena`, so actual orientation intent is unclear.
- Jumppads do not use brush/entity rotation as launch authority. Launch priority is target-based ballistic, explicit direction and speed, angle/pitch and speed, then straight up.
- Teleport triggers are parsed as ignored entities, not gameplay.
- Playerclip currently blocks players, hitscan/world traces, rockets, grenades, and plasma. The collision/trace API does not yet carry cheap content masks to distinguish players from projectiles.

## Footguns

- Do not replicate map or texture data every tick. Use map revision and `hasArena`.
- Keep server map loading out of the tick hot path except explicit map-change requests.
- Be careful adding fields to `Arena`: they affect network snapshot size when arena data is sent and static-world cache fingerprints.
- If adding asset packaging assumptions, update packaging scripts and docs together.
