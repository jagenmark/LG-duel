# Maps And Assets

Map data ends as an `Arena` from `src/sim/Arena.hpp`: bounds, ambient
lighting, fixed-size cuboid walls, convex brushes, spawns, jumppads, teleports,
kill volumes, health pickups, static lights, an optional sun, and optional McGuffin map data.
Solid geometry is used for server collision/combat traces and client rendering.
Gameplay trigger volumes do not render.

## Loading Pipeline

Runtime maps are restricted Quake/TrenchBroom `.map` files parsed by `loadArenaFromMapText()` in `src/map/MapParser.cpp` and converted by `src/map/MapToArena.cpp` via `loadArenaFromFile()`. Gameplay map loading is file-backed through the configured map directory; the dedicated server starts by requesting the packaged `eyetoeye` map.

`.map` support is a conversion layer:

- `src/map/MapParser.*` parses entities, properties, brushes, and Quake-style face texture parameters.
- `src/map/MapToArena.*` converts `worldspawn` and `func_group` brushes to cuboid `ArenaWall`s when possible, otherwise convex `ArenaBrush` hulls.
- `lg_spawn` entities become legacy spawn positions. Team maps may additionally
  use `info_player_team`/`lg_spawn` with physical `spawn_group` values
  `red_base` or `blue_base`; their authored `angle`/`yaw` is retained.
- `trigger_jumppad` brush entities become non-solid, non-rendered `ArenaJumpPad` trigger AABBs. Visible pad surfaces should be ordinary `worldspawn` or `func_group` geometry; the trigger brush can use `common/trigger` or `textures/common/trigger` for editor visibility only.
- `trigger_kill` brush entities with cuboid brushes become non-solid,
  non-rendered world-death volumes. A living player dies on first touch, with
  no player frag or weapon damage credit. Sloped brushes are rejected.
- `item_health_small` and `item_health_large` point entities become static `ArenaHealthPickup` entries. Server snapshots replicate only their fixed availability bits.
- `worldspawn`/`func_group` brushes using `common/playerclip` or `textures/common/playerclip` on every face become collision-only solids. They stay in `ArenaWall`/`ArenaBrush` for collision and traces, but `renderable=false` keeps them out of static world rendering and lighting. Mixed playerclip/non-playerclip brushes are rejected; apply playerclip to the whole brush.
- `target_position` point entities provide jumppad landing targets and teleport
  exits by `targetname`.
- One `info_mcguffin_spawn` supplies the neutral McGuffin spawn. One
  `trigger_mcguffin_base` brush per team supplies the Red and Blue base
  volumes. See `docs/MCGUFFIN-SPEC.md` for the full map contract.
- `worldspawn` can set `lg_ambient_color` and `lg_ambient_intensity` for
  map-wide fill light.
- `worldspawn` can set the optional `lg_sky` key. Accepted values are
  `aurora` and `crimson-sunset`. Missing values, `none`, `off`, and unknown
  values select no sky.
- A face whose normalized material name is exactly `common/sky` or
  `textures/common/sky` is a sky opening. It keeps all solid, trace, and map
  data, but static scene building skips that face. Similar names do not count.
- `light`/`light_point` become static local lights. Managed lights can also set
  shadow casting, source radius, priority, and fixed-seed flicker. A flickering
  light changes its strength, not its static shadow shape.
- One optional `light_sun` becomes the map sun.
- `trigger_teleport` brush entities become non-solid teleport volumes. Their
  `target` must name a `target_position`, which supplies the exit point and
  authored exit angle.
- Other entity classes do not produce runtime map data. Add a class to this
  list when support lands so mapper and tool scope stays tied to the loader.

Server map requests flow through `ServerGame::loadRequestedMap()`. Names are restricted to simple stems/extensions, resolved under `mapDirectory_`, and attempted as `.map` when no extension is given. Successful loads call `setArena()`, bump `mapRevision_`, reset the match, and force clients to receive updated arena data.

The opt-in developer-control client reuses this request path. It validates the
runtime map locally for early parse/conversion errors, queues the existing map
command, and acknowledges success only after receiving the requested map with
a newer authoritative revision. `scripts/watch-maps.ps1` remains the
source-to-`build/default/maps` synchronizer.

Managed map format v2 stores teleports as one typed object with a stable ID,
trigger bounds, exit origin, and exit yaw. Its writer makes the linked
`trigger_teleport` brush and `target_position`, fixed `common/trigger`
material, internal IDs, and target name. This keeps raw link strings out of the
public agent API. Managed batch edits check the full map before one atomic
write.

Typed lighting and teleport tools also work on hand-authored project maps.
Ambient edits patch worldspawn values. Sun edits may add, replace, or remove the
one `light_sun`. Other edits replace API-owned entity spans; they do not add a
managed state marker or rewrite other map text. A mapper may adopt an
importer-compatible point light with a unique `lg_agent_id`. A legacy teleport
cannot be adopted by a tag alone because the typed pair needs internal trigger
and target IDs plus link data; recreate it with `lg_map_add_teleport`. Geometry
and spawn edits on hand maps remain TrenchBroom work.

## Collision Vs Render Data

Collision and traces use `ArenaWall` AABBs and `ArenaBrush` convex
planes/vertices. Rendering uses the same structures plus material ids, face
material ids, texture projections, light data, a `renderable` bit, and a small
per-face surface kind. A sky face stays solid for movement and combat but does
not add static mesh triangles. Playerclip solids keep collision data but skip
render geometry. `ArenaJumpPad` and `ArenaKillVolume` data is not solid and is
not rendered. Movement checks jump pads; the server checks kill volumes after
final movement repair. There is no separate server-only collision asset
yet, so avoid adding render-only large data to `Arena` unless it has a clear
version and need.

## Units, Materials, And Textures

Quake map units convert to LG units with `1 / 40`. Texture projection stores Quake-space axes/offsets/scales and rendering multiplies LG positions back by 40 for UV generation.

Materials are hashed/stable ids from material paths. Renderer texture loading expects PNGs under `textures`, with aliases both including and excluding `.png`. Windows packages copy only texture PNGs referenced by at least one map face, so new map materials must resolve under `textures`. Packaged builds also rely on shaders under `assets/shaders` and audio/model assets under `assets`.

The Q3 importer keeps `common/sky` only when every face in a source brush uses
a `skies/` material. A mixed brush uses the normal imported wall material on
all faces. This keeps a mixed visual brush closed.

Sky source panoramas, face rules, hashes, and rebuild commands are in
`assets/sky/README.md`. The source panoramas never ship. Only the client build
and Windows client package copy the cube faces and sky shaders. The server and
headless checks do not need them.

`textures/common/playerclip.png` is an editor-only visible tool texture. Mapper workflow for smoothing stairs:

1. Build the normal visible stair brushes.
2. Create a ramp brush over the stair tops.
3. Apply `common/playerclip` to every face of the ramp.
4. Keep the visible stairs for presentation; the ramp is invisible in-game but solid for player movement.

## Current Limitations

- Valve 220 texture axes are explicitly rejected by `MapParser`.
- Convex brush limits are fixed: `ArenaBrush::kMaxFaces`, `kMaxVertices`, and per-face max vertices.
- Arena counts are fixed: 2048 walls, 1024 brushes, 48 jumppads, 16 teleports,
  32 kill volumes, 32 health pickups, and 96 static lights.
- Multiple `light_sun` entities are not supported.
- Legacy Duel/CA spawn yaw remains unused. Authored team-spawn yaw is stored and
  applied by the authoritative team spawn selector.
- Jumppads do not use brush/entity rotation as launch authority. Launch priority is target-based ballistic, explicit direction and speed, angle/pitch and speed, then straight up.
- Playerclip currently blocks players, hitscan/world traces, rockets, grenades, and plasma. The collision/trace API does not yet carry cheap content masks to distinguish players from projectiles.

## Footguns

- Do not replicate map or texture data every tick. Use map revision and `hasArena`.
- Keep server map loading out of the tick hot path except explicit map-change requests.
- Be careful adding fields to `Arena`: they affect network snapshot size when arena data is sent and static-world cache fingerprints.
- If adding asset packaging assumptions, update packaging scripts and docs together.
