# EyeToEye collision-safe art pilot

## Scope

This pass changes map face materials and light keys only. It keeps every brush
point, texture plane value, entity, spawn, group, origin, and collision rule.
It does not change the four spawn-cover brick groups.

The three standard EyeToEye camera views were already in
`config/dev-camera-presets.json`. Their positions match the north, east, and
south spawn approaches at the map import scale, so this pass does not change
that file.

## Face changes

The map now uses:

- sandstone on the centre upward face and five main-route upward faces;
- oxidized trim on eight centre side faces, all 84 tall-post faces, and all
  120 lamp-shell faces;
- basalt on all 96 perimeter stone faces;
- amber on four cardinal-pad upward faces and the centre fire face.

The centre underside keeps its old metal. Main-route side faces keep their old
roof or metal materials. Cardinal-pad side and lower faces keep their old tile
material. Spawn cover keeps `Tiny/Bricks/Bricks_16-128x128`.

## Light changes

The sun uses direction `0.35 -0.5 -1`, color `238 226 206`, and intensity
`0.6`.

The four outer lamps use color `255 220 180`, intensity `0.75`, and radius
`500`. The four floor lights use color `255 150 72`, intensity `0.35`, and
radius `560`. No light origin moves.

## Checks

The focused arena-map test checks the parsed material face totals, sun values,
point-light values, point-light count, and all eight light origins.

The same checked-in test pins a 64-bit fingerprint of the parsed collision
data. It covers arena bounds; solid and visual wall and brush counts; wall and
brush bounds; brush vertices; face normals, distances, vertex counts, and
vertex links; collision roles; source entity, brush, and patch IDs; and render
flags. It leaves out material IDs, texture projections, lights, and spawns, so
those art and setup fields can change without hiding a collision move. This
guard proves that the parser produces the same collision planes and bounds; it
does not compare the raw source points as text. Before hashing, it rounds each
parsed float to the nearest `0.00001` LG world unit. That fixed tolerance avoids
last-bit math changes across compilers while still catching a collision move at
gameplay scale.

The package texture check resolves every map material to a PNG under
`textures`.
