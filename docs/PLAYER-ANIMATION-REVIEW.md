# Player animation revision — 2026-09-05

The active player is Pip/Fuse (`assets/models/pip_fuse/pip_fuse.glb`). The
historical `workerPlayerModel()` loader name does not identify the loaded asset.
The older Quaternius Worker asset is unchanged by this revision.

## Crouch and collision

Standing collision height is 1.80 units; crouched height is now 1.44 (80%),
previously 1.116 (62%). Movement collision and weapon traces use the same
player bounds. The crouch, directional crouch-walk, and slide meshes reach the
new collider top within 0.025 units across sampled phases. Tests also check
that a shot at 1.35 units reaches the crouched head and a shot at 1.46 misses.
The existing cylinder/body and head-box trace shapes remain approximate shapes,
not per-triangle skeletal hit detection.

The renderer sizes the skeleton using the loaded mesh's authored height and
keeps that size stable through stance interpolation. Crouch comes from bent
hips and knees rather than vertical model compression. Weapon sockets use the
same transform. Ankle joints and blended boot weights keep the soles planted
while the shins bend. The mesh positions, topology, normals, colors, and
materials are preserved.

## Animation coverage

The original Pip/Fuse source reused run for start/stop, the same action for both
landings, the same action for jump/fall, and the same motion for crouch-walk and
sneak. Its clips also used one duration despite different runtime clocks.

The revision adds or replaces:

- Crouch idle, forward/backward/left/right crouch walking, and a held slide pose.
- Forward/backward/left/right sneak steps.
- Distinct start and braking weight shifts.
- Distinct jump tuck, falling leg extension, and light/heavy landing recovery.
- A death fall that settles on the floor; the old end pose hovered about 0.65
  authored units above it.

Forward/backward runs and strafes use bent-knee strides, a stable weapon hold,
and normalized cycle timing. Idle aiming, upper-body aim/lean, and weapon socket attachment
remain active. Slide deliberately holds its planted pose rather than cycling
walking feet. Animation transitions still crossfade; collision stance changes
remain server-authoritative.

`python scripts/author_player_animations.py` (Python with NumPy) regenerates the
runtime poses, ankle rigging, and clip timing. The Blender character builder
invokes this postprocessor after exporting. It preserves unrelated asset data
and removes superseded animation storage on repeated runs.

## Verification

Focused movement, hit tracing, scene/render, presentation, client, and Glide Duel
server tests pass. Rendering tests cover the active clip inventory, unchanged
skeleton scale on crouch, planted feet and collider height across crouch phases,
and a grounded completed death pose. A live GPU client review uses held-input
captures with the actual sampled clips and player state recorded beside each
image; a pose label alone is not evidence of the state.

Early close captures are in `build/animation-review-pip-close/`. The final audit
and its exact state records are in `build/animation-review-pip-final/`.

The final live audit captured 21 checks: idle, front/side crouch, four crouch
walk directions, slide, four run directions, four sneak directions, start,
stop, jump, fall, and heavy landing. Actual clip/weight records passed for every
check. The stop checkpoint was moved later to capture the braking transition
after interpolation, rather than treating a still-running pose as a stop.
Light landing and the final death pose were checked through the model/presentation
tests; the live capture set does not claim coverage of those two states.

## Knee hinge constraint

Reverse knee flexion is a defect in authored animation, including the current
non-ragdoll death clip. The generator rejects keyframes with reversed knee
flexion. It also handles antiparallel IK alignment explicitly instead of
returning an unchanged rotation for a required half turn.

The scene test measures signed knee flexion from the sampled hip, knee, and
ankle transforms in the pelvis frame. It checks all 25 clips at 125 Hz through
1.536 seconds (covering the longest clip), plus 112,500 blended poses: all
ordered clip pairs, six independent sample times per clip, and five transition
weights using the runtime's layered weighting. Both knees must have finite,
nonnegative flexion within floating-point tolerance. These checks pass; no
reversed knee was found in the current revised poses. This is sampled coverage,
not a proof over every possible continuous interpolation time.

A further live side-view review confirmed forward knee bends in crouch, crouch
walking, sliding, forward and backward running, and landing. Its evidence is stored in `build/animation-review-pip-knees/`.
