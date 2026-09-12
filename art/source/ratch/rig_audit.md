# Pip/Fuse rig audit for Ratch

## Runtime asset

`assets/models/pip_fuse/pip_fuse.glb` is Blender glTF 2.0 output (Blender I/O
4.5.47), with one skinned mesh, one skin, 16 joints, 13 materials, and 25
animation clips. The runtime mesh is `PipFuse_RuntimeMesh`; its source mesh is
decimated to 60% in `art/source/pip_fuse/build_character.py` before export.

The skin joint names and order are:

```text
Hips, Spine, Head,
Arm.L, Forearm.L, Hand.L, weapon_socket,
Arm.R, Forearm.R, Hand.R,
Thigh.L, Shin.L, Foot.L, Thigh.R, Shin.R, Foot.R
```

The actual GLB node order differs from this skin order, so a replacement must
map animation channels by node name. `weapon_socket` is a skinned joint and is
used by the renderer for the held weapon attachment. `Foot.L` and `Foot.R`
were added by `scripts/author_player_animations.py`; they are children of the
corresponding shins and have their own inverse bind matrices.

The 25 clips currently present are:

```text
IDLE, Idle_Gun_TwoHanded, LEAN_LEFT, LEAN_RIGHT,
DUCKING, CROUCH_WALK, GLIDE_SLIDE, CROUCH_BACK, CROUCH_LEFT, CROUCH_RIGHT,
SNEAK, SNEAK_BACK, SNEAK_LEFT, SNEAK_RIGHT, JUMP, FALL,
START_FORWARD, STOP_FORWARD, LAND_LIGHT, LAND_HEAVY, Death,
RUN, RUN_BACK, STRAFE_LEFT, STRAFE_RIGHT
```

The editable Pip source has 18 actions and 14 joints; the runtime animation
authoring pass expands these to the authoritative 25 clips and 16 joints.

## Authoring and coordinates

`build_character.py` creates the original deformation rig in Blender Z-up
coordinates: X is lateral (negative X is the left side), Z is vertical, and Y
is the front/back depth axis. The skeleton rest locations are Hips `(0,0,.86)`
to Spine `(0,0,1.04)`, Head `(0,0,1.47)` to `(0,0,1.90)`, thighs to shins to
hands, plus `weapon_socket` under `Hand.R`. The script first creates 18
procedural clips and exports a runtime GLB, then runs the animation authoring
pass; the saved runtime file therefore contains the post-pass result.

`author_player_animations.py` reads the exported GLB directly, uses
`Idle_Gun_TwoHanded` as the neutral standing source, adds ankle joints and
blends their weights from the shin, and replaces 21 locomotion/contact/air/death
clips with 33-frame linear samples. It preserves the four original clips
`IDLE`, `Idle_Gun_TwoHanded`, `LEAN_LEFT`, and `LEAN_RIGHT`. `HEIGHT=1.95174`,
`CROUCH_RATIO=.80`; the pass solves knees in global space and keeps feet at
standing orientation. Rerunning it mutates the GLB, so use the preservation
utility after any Blender export.

## Safest Ratch workflow

Build Ratch with the same joint names, hierarchy, local rest transforms,
inverse bind matrices, and model-space orientation. Skin the new mesh to that
rig, export it as a separate GLB, and run:

```sh
python3 art/source/ratch/preserve_animations.py \
  ratch-export.glb \
  assets/models/pip_fuse/pip_fuse.glb \
  assets/models/ratch/ratch.glb
```

The utility validates joint sets, parent names, rest translation/rotation/
scale, inverse bind matrices, and every animation target. It then replaces the
target animation list with Pip's animation definitions, copies each source
animation accessor's buffer bytes verbatim into the target BIN chunk, and
rewrites only accessor offsets and channel node indices. Pip is never written.
Use `--tolerance` only for known exporter float noise; the default is `1e-5`.

The first Ratch export exposed a Blender foot-bone artifact: `Foot.L` and
`Foot.R` retained the expected head/rest transform, hierarchy, and bind
translation, but their inverse binds had a scale-only drift of about
`1.2e-5` on one diagonal element. This comes from the imported synthetic bone
tail (the foot head is at the ankle while Blender reports a tail toward the
shin direction); it is not a geometry or animation mismatch. The utility
accepts only this narrowly characterized `Foot.*` scale drift, rejects all
other matrix discrepancies, and restores the exact Pip foot inverse-bind
matrix by joint name before copying animations. The resulting candidate is
`art/exports/ratch/ratch.glb`.

Independent verification is available with:

```sh
python3 art/source/ratch/validate_ratch.py \
  art/exports/ratch/ratch.glb \
  assets/models/pip_fuse/pip_fuse.glb \
  --report art/exports/ratch/validation.md
build/default/lg_duel_asset_validate art/exports/ratch/ratch.glb --instances 16
```

The first command compares raw logical input/output bytes for every animation
sampler, checks name-remapped channel targets, rig hierarchy/rest transforms,
exact corrected foot binds, finite mesh attributes, joint ranges, and weight
sums. The second is the existing engine-backed headless validator in
`tools/AssetValidate.cpp`; it exercises `GltfSkinnedModel::load`, bounds,
skinning, animation sampling, and 16-instance CPU pose sampling. It reports
that GPU appearance still requires a client preview.

This keeps runtime animation samples exact while allowing Ratch geometry,
materials, and mesh topology to differ. The one visual risk is deformation:
rig compatibility does not guarantee that a high-poly surface behaves well at
extreme crouch/death poses, so validate every clip in Blender before selection.

## Runtime integration

`workerPlayerModel()` retains its historical API name and loads Pip/Fuse.
`ratchPlayerModel()` loads the separate Ratch package. `playerModelForSetting()`
selects Pip for `1` and Ratch for `2`; `0` uses legacy boxes in Scene3D.
Both models share the upper-body, procedural lean and weapon-socket rules via
`usesPipPlayerRig()`. Body and socket scaling use the selected model's bounds,
and preserve standing skeleton size through crouch transitions.

The renderer switches model resources when the selected model changes. Windows
packaging requires both packages. Pip remains the default; its GLB is separate
from Ratch and is the animation-preservation source.
