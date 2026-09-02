# Mapmaking

LG Duel loads a small part of the Quake and TrenchBroom text `.map` format.
It does not support Quake BSP files or every `.map` feature.

## Set up TrenchBroom

Copy `tools/trenchbroom/LG Duel/` into TrenchBroom's games folder. The setup
lists each spawn, trigger, pickup, light, teleport, McGuffin entity, and LG Duel
world setting that the game supports.

## Create a map

Build cuboid or other convex brushes in `worldspawn`. Add player spawns as
point entities named `lg_spawn`, then save the file as `maps/<name>.map`.

LG Duel imports editor units at `1/40` scale. For example, 40 editor units
become one game unit, and a 16-unit stair step becomes 0.4 game units, just
below the current walkable step height.

Each spawn needs an `origin` such as `"160 -120 40"`. It may also set `angle`
or `yaw` in degrees. `lg_bounds_min` and `lg_bounds_max` can set the arena bounds
on `worldspawn`; otherwise, the importer derives padded bounds from brushes and
spawns.

## Lighting

`worldspawn` can set map-wide fill light with `lg_ambient_intensity` (default
`0.30`) and `lg_ambient_color` as either normalized RGB or `0..255` RGB. This
fill affects static map surfaces, players, and weapons.

Use `light` or `light_point` for static point lights. Each light needs an
`origin` and accepts:

- Quake-style `light` intensity
- optional `radius`
- `_color` or `color` as normalized RGB
- `_light` as an intensity or `r g b intensity`
- `casts_shadows`
- `source_radius`
- signed `priority`
- fixed-seed `flicker`

The importer scales light positions and radii by the same `1/40` rule as other
map data.

Steady lights without shadows bake into static world colors and also light
nearby actors. Flickering and shadow-casting lights stay live. Point-shadow
maps cache the static world; moving actors receive those shadows but do not
cast into or clear the cache. `r_point_lights` sets the live-light count, and
`r_point_shadows` sets the cached shadow count and size.

Outdoor maps may define one invisible `light_sun`. Set `direction` to the path
of its rays, such as `0 0 -1` for downward light. It also accepts `intensity`,
`color` or `_color`, and `angle` plus `pitch` when `direction` is absent.

## Triggers

Use a `trigger_teleport` brush whose `target` names a `target_position`. The
target sets the exit point and facing. The map tools expose both entities as
one teleport object so callers do not need to edit target links.

Use a non-solid cuboid `trigger_kill` brush for an instant world-death zone.
It ends a life on first touch without awarding a weapon kill or frag.

## Textures

The importer keeps brush texture names as material IDs and sends them to
clients. Each named texture must exist under `textures/`. For example,
`512x512/Brick/Brick_14-512x512` resolves to
`textures/512x512/Brick/Brick_14-512x512.png`.

The SDL_GPU first-person renderer samples these textures on cuboid wall faces.
The game no longer uses the old grass and brown prototype treatment.

For more detail about map parsing, collision, rendering, and asset rules, see
[Maps and assets](architecture/maps-assets.md).
