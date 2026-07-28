# Overkill and Thunderstruck map art pass

## Scope

This pass changes only face materials and light keys. It keeps all brush plane
points, texture projections, entity origins, spawns, targets, transforms,
bounds, clips, and triggers.

## Overkill

The pass keeps every material token and light origin. It softens the seven
authored lights:

- the sun uses `244 232 210` at `0.70`;
- the lower centre and south route use warm light at `0.65`;
- the north and west routes use cool light at `0.80` and `0.75`;
- the east route uses warm light at `0.65`;
- the north-east teleport exit uses amber light at `0.85`.

The point-light radii range from 760 at the teleport exit to 1300 at the upper
north crossing.

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
