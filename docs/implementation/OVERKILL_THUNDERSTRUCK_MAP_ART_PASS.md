# Overkill and Thunderstruck map art pass

## Scope

This pass changes only face materials and static light data. It keeps all brush
plane points, texture projections, gameplay entity origins, spawns, targets,
transforms, bounds, clips, and triggers.

## Overkill

The pass keeps every material token and the seven key-light origins. It uses
the reviewed import light rig:

- the sun uses `255 226 184` at `0.85`;
- the lower centre uses warm light at `0.80` with a radius of `1400`;
- the north route uses cool light at `0.75` with a radius of `1300`;
- the south route uses warm light at `0.75` with a radius of `1400`;
- the west route uses cool light at `0.70` with a radius of `1200`;
- the east route uses warm light at `0.70` with a radius of `1200`;
- the north-east teleport exit uses amber light at `0.90` with a radius of
  `900`.

Five cool fill lights sit lower in the lower centre, upper crossing, south
route, teleporter approach, and teleporter exit. They lift vertical route
detail without raising the already bright floors. The renderer bakes all 11
point lights into world vertex colors during scene setup; they add no draw,
pass, shader, or per-frame GPU work.

## Thunderstruck

The pass replaces brown brick with grey brick on 105 faces across 18 whole
world brushes. It adds olive brick to 16 upward stair faces and all six faces
of the visible roof shell. Nine deep-roof faces use the darker grey brick.
World brushes 134 and 135 keep their plaster.

Six point lights now mark the main routes with warm, cool, or neutral light.
Their origins do not move. One cool sun uses direction `0.25 -0.45 -1` and
intensity `0.45`.

## Checks

Run:

```powershell
python scripts/verify_map_art_pass.py
```

The read-only check rebuilds the exact approved change from the pinned graphics
base `13c1c75`, then compares it with the two worktree maps. Pinning the source
keeps the check useful after these map changes enter a later commit. The script
has no mode that writes either map. Use `--base <revision>` only to inspect a
different source revision.

The check also compares all plane points, origins, and full map text; checks
exact face counts and light values; and resolves every non-common texture file.
