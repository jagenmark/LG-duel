# Stainless Revolver

Low/medium-poly revolver based on `reference/revolver_concept.png`.

The editable model uses the same orientation as the other authored weapons:
`+X` points forward and `+Z` points up. The model is parented under
`REV_ROOT_game_axes_x_forward`, with `REV_RECOIL_ROOT` providing whole-weapon
recoil and `REV_CYLINDER_ROTATOR_one_sixth_step` providing the cylinder pivot.

The included 12-frame `REV_FIRE_RECOIL` / `REV_FIRE_CYLINDER` animation moves
the weapon slightly back and up, settles it, and indexes the six-chamber
cylinder by exactly 60 degrees.

Regenerate all authored outputs from the repository root:

```bash
blender -b --python assets/models/revolver/create_revolver.py
```

Outputs:

- `lg_duel_revolver_stainless.blend` — editable source with preview setup
- `lg_duel_revolver_stainless.glb` — runtime-oriented export
- `lg_duel_revolver_stainless.fbx` — interchange export
- `lg_duel_revolver_stainless_preview.png` — rendered modeling preview

The `PREVIEW_NOT_FOR_EXPORT` collection contains only the camera, lights, and
floor. The script excludes it from GLB/FBX exports.

After changing the Blender geometry, refresh the game's compiled body and
cylinder meshes with:

```bash
blender -b assets/models/revolver/lg_duel_revolver_stainless.blend \
  --python assets/models/revolver/export_revolver_header.py
```
