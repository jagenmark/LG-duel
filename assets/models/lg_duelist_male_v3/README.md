# LG Duel Male Duelist v3

This directory contains the active authored third-person duelist source and its
runtime GLB export.

## Source and runtime files

- `art/blender/lg_duelist_male.blend`: tracked Blender source of truth.
- `art/exports/lg_duelist_male.glb`: runtime mesh, rig, skin, and actions.
- `art/exports/presentation_clips.json`: generated action inventory for the
  first directional-presentation pass.

The model faces Blender `-Y`. The runtime loader maps the model axes into the
player render basis and applies presentation-only upper-body aim pitch after
sampling an action.

## Presentation animation pass

The first authored presentation pack adds:

| Action | Purpose |
|---|---|
| `RUN_BACK` | Readable backpedalling without reversing `RUN` at runtime. |
| `STRAFE_LEFT` | Left travel with lower-body turn and counter-twisted torso. |
| `STRAFE_RIGHT` | Mirrored right-travel treatment. |
| `START_FORWARD` | Short acceleration/start response. |
| `STOP_FORWARD` | Short braking/stop response. |
| `LAND_LIGHT` | Normal landing recovery. |
| `LAND_HEAVY` | Deeper compression after a high-speed fall. |

The actions are derived reproducibly from the existing hand-authored `RUN`,
`IDLE`, and `LAND` sources. They remain ordinary Blender actions in the tracked
blend and ordinary named animations in the GLB; the game does not depend on the
authoring script at runtime.

## Rebuild and preview

From the repository root with Blender 5.x installed:

```powershell
& 'C:\Program Files\Blender Foundation\Blender 5.1\blender.exe' `
  'assets\models\lg_duelist_male_v3\art\blender\lg_duelist_male.blend' `
  --background --python 'tools\author_duelist_presentation_clips.py'

& 'C:\Program Files\Blender Foundation\Blender 5.1\blender.exe' `
  'assets\models\lg_duelist_male_v3\art\blender\lg_duelist_male.blend' `
  --background --python 'tools\render_duelist_presentation_previews.py'
```

Preview renders are written below `build/animation_previews` and are not source
assets. Blender backup/autosave files are also not source assets and must not be
committed.

## Deferred authored clips

- Turn-in-place should wait until the renderer has explicit lower-body travel
  yaw separate from upper-body aim yaw.
- Weapon-family ready poses and additive firing actions should wait until weapon
  pose descriptors and fire-event animation hooks can consume them without
  weapon-specific scene-builder branches.
