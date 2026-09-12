# Ratch — remaining limitations

This draft captures the September 12, 2026 polish and runtime integration.
Select Ratch with `r_player_model 2`; `1` selects Pip/Fuse and `0` uses boxes.
The integration was approved on September 5; this document does not imply
approval of final reference fidelity or production performance.

## Appearance against the reference

- Facial skin is smoother and more symmetric than the reference. Fine wrinkles,
  irregular weathering, cheek detail and the tired expression need more work.
- Hair and beard read as sculpted locks in close-ups. Thin edges can still alias
  at gameplay distances, especially against dark indoor backgrounds.
- Game lighting remains darker and flatter than the Blender studio. The baked
  facial pigment and local occlusion improve readability, but cannot reproduce
  the reference's skin scattering, detailed surface shading or cinematic light.
  Baked occlusion can also over-darken cavities under some lighting.
- Team colour is concentrated on the waistcoat, with small weights on other
  cloth. Lighting and tint still change the reference's brown palette.
- Garment distressing, silhouette asymmetry and material microdetail are
  simplified. There is no facial rig, hair simulation or cloth simulation.

Compare the [reference](../../concepts/ratch_reference.png),
[portrait](../../previews/ratch/portrait.png),
[profile](../../previews/ratch/face_side_audit.png),
[game view](../../previews/ratch/in_game.png) and
[game close-up](../../previews/ratch/in_game_closeup.png).
The [interactive gallery](../../previews/ratch/review.html) includes previous
images and eight pose samples from two angles.

## Runtime cost and deformation

The runtime has **858,224 triangles, 457,665 exported vertices, 21 material
primitives, 16 joints and 25 clips**, with two 2048 × 2048 atlases. The triangle
count is 21.3% below the prior version, but remains very high for a multiplayer
character. The GLB is about 53 MiB. There are no LODs, distance-based hair
simplifications or streaming variants. Retopology and measured GPU/VRAM budgets
are required before treating this as a production-ready default.

The 16-instance/1,200-pose CPU validation checks correctness and finiteness;
it has no timing threshold and does **not** establish GPU frame time or memory
headroom. Live captures show appearance, not sustained multi-player performance.

All inherited animation samples and bind transforms are preserved. Broad
animation sampling and selected rendered poses cannot exclude garment or hair
intersections in every blend, aim angle and gameplay situation. Cloth follows
bone weights and can intersect the body in extreme poses. The inherited crouch animations reach approximately 80% of standing height,
while this PR preserves the base game's 62% collision crouch. The visible head
and upper body can therefore extend above the crouched collider. Resolving
that mismatch requires an explicit animation or gameplay decision; the draft
does not silently change collision height. The inherited
`GLIDE_SLIDE` clip is retained and previewed; this PR does not add a glide-mode
state selector. The lean gallery composes the game's procedural 34-degree
spine bend because the original standalone lean clip is an axial twist.

## Integration scope and validation coverage

This self-contained PR includes the required Pip/Fuse asset and rig support,
which were previously local. Consequently, the `r_player_model 1` default
changes from the base branch's Quaternius Worker to Pip/Fuse. The historical
`workerPlayerModel()` API name is retained. Ratch is option `2`.

Model bounds determine body and weapon-socket scaling. Crouching preserves the
standing skeleton size through capsule interpolation. The PR includes selector,
pose and scene coverage; unrelated gameplay, HUD, replay and tool changes from
the working checkout are excluded.

The final asset was visually checked on Linux through SDL_GPU/Vulkan on Intel
LNL/Mesa in `eyetoeye`. Windows packaging lists the required runtime files, but
Windows, other GPU vendors, and other graphics backends have not been visually
verified. See [verification results](VERIFICATION.md) for reproducible checks
and the limits of each result.

## Source and rebuild limits

The compressed `.blend`, source scripts, textures and runtime package are
included. Intermediate exports, historical runtime copies and `.blend1` backups
are omitted. Rebuild instructions are in [README.md](README.md).

The editable Pip Blender source retains the original 14-joint/18-action
construction; its post-export authoring script produces the authoritative
16-joint/25-clip runtime rig inherited by Ratch. Rebuilding Pip or changing its
animations requires rebuilding and revalidating Ratch's preservation contract.
