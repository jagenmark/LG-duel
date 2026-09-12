# Ratch — character source and review

Ratch is a Blender-authored character based on the user-provided turnaround in
`art/concepts/ratch_reference.png`. Its defining forms are a crooked patched
felt hat, weary face with a bulbous nose, individually modeled hair and beard,
layered linen/waistcoat/long coat, mended trousers, fingerless gloves, and
mismatched laced boots.

**Approved and integrated.** Select Ratch with `r_player_model 2`; `1` keeps
Pip/Fuse (the default) and `0` uses legacy boxes. Runtime files live in
`assets/models/ratch/`. Vulkan rendering and live Pip/Ratch switching were
verified on 2026-09-05; the polished assets were rendered again on September 12.
See [LIMITATIONS.md](LIMITATIONS.md) for the remaining work and validation scope.

## Deliverables

- `ratch.blend`: dense editable source, inherited Pip/Fuse rig and actions,
  packed authored textures, lighting, and camera.
- `build_ratch.py`: deterministic geometry/material generation and studio views.
- `assets/models/ratch/`: installed runtime GLB, atlases and material manifest.
- `art/exports/ratch/ratch.glb`: generated source-material GLB with preserved animation data (not committed).
- `art/exports/ratch/runtime/`: retained validation reports; the rebuild creates
  candidate GLB/atlases here. Duplicate generated binaries are not committed.
- `art/previews/ratch/review.html`: reference comparison and angle/pose gallery.
- `art/previews/ratch/attachment_report.json`: surface-fitting operations.

The September 12 source has 858,224 triangles, down from 1,090,730 before this
polish pass. The source runtime rig has
16 joints and 25 clips. `preserve_animations.py` checks the bind/hierarchy
contract and copies animation samples verbatim; it narrowly repairs Blender's
small synthetic-foot-tail bind drift from the original Pip values.

## Rebuild

From the repository root, using Blender 4.5 and Python with NumPy and Pillow:

```sh
blender --background --threads 8 --python art/source/ratch/build_ratch.py
python3 art/source/ratch/preserve_animations.py art/exports/ratch/ratch_geometry.glb assets/models/pip_fuse/pip_fuse.glb art/exports/ratch/ratch.glb
python3 art/source/ratch/prepare_materials.py art/exports/ratch/ratch.glb art/exports/ratch
blender --background --threads 8 --python art/source/ratch/render_reviews.py
blender --background --threads 6 --python art/source/ratch/review_animations.py
python3 art/source/ratch/validate_ratch.py art/exports/ratch/runtime/ratch.glb assets/models/pip_fuse/pip_fuse.glb --report art/exports/ratch/runtime/validation.md
build/default/lg_duel_asset_validate art/exports/ratch/runtime/ratch.glb --instances 16
```

The atlas utility requires Pillow. The Blender builder uses bundled NumPy.
Pip's files are read only throughout these steps.

The builder calls `bake_face_material.py` to bake facial pigment and local
occlusion. Blender's face image is 2048px; the runtime atlas reserves a 1024px
region for it, with gutters, within the engine's 2048px texture limit. Hair and
cloth have individual 512px regions. Other materials use constant swatches.

A separate material check verifies the actual engine metadata and sampled UV
contract, because general geometry validation does not fail on invalid texture
manifests:

```sh
c++ -std=c++20 -I src art/source/ratch/check_runtime_materials.cpp build/default/CMakeFiles/lg_duel_asset_validate.dir/src/render/GltfSkinnedModel.cpp.o build/default/liblg_duel_core.a -o /tmp/ratch_material_check
/tmp/ratch_material_check art/exports/ratch/runtime/ratch.glb
```

## Visual review and scope

The review includes both full side profiles, front/back/three-quarter views,
face profile, above/below close-ups, and eight animation samples from two
angles. Side and low-angle iterations corrected floating patch edges, a hard
scalp boundary, detached lacing/tendon details, an overhanging eyelid overlay,
and the forward mouth line. The nose and ears are joined into a closed facial
sculpt. Garment overlays are surface-fitted and inherit their support's skin
weights; sleeves blend across shoulder/elbow joints and coat tails follow the
pelvis/thighs.

This remains a stylized interpretation: the reference's very fine distressing,
facial asymmetry, and cinematic hair/skin rendering are simplified. Hair uses
mesh strands; there is no facial rig or cloth simulation. CPU load/animation
checks and Blender views do not establish live GPU appearance or performance.

## Face revision after profile rejection

The rejected profile is preserved in `revisions/pre_face_revision/`. The new
face replaces separate nasal wings and protruding eye/lid tubes with a
continuous bridge and drooping tip, Boolean nostril cavities, recessed almond
eye surfaces, broad skin lids, cheek relief and smaller ears. The lower jaw
and neck join were adjusted and hair shortened around the orbit.

This is a structural improvement, not a claim of reference-level fidelity.
The concept has substantially richer skin relief, irregular hair clumps and
a more expressive tired face. The user approved this revision for integration on 2026-09-05.
`render_reviews.py` refreshes stills from the saved blend; `RATCH_VIEWS` can
select comma-separated view names for either rendering script.

## Boot and lean review corrections

The detached-looking elliptical soles were replaced by flat-topped outsoles,
intersecting welts and one continuous boot upper. Upper boots overlap beneath
the trousers and blend smoothly from Foot to Shin weights through the ankle.

The LEAN_LEFT gallery pose composes the standing weapon pose with the game's
34-degree procedural spine bend. The inherited clip itself is an axial twist;
it remains unchanged. See `lean_audit.md`. `RATCH_POSES` filters animation
review renders by clip name.

Lean previews freeze the sampled local bone transforms before applying the
additional spine bend. The renderer checks base-pose preservation and ensures
child local transforms are unchanged, so render-time action evaluation cannot
remove the bend or distort the arms.

## Integration validation

Built `lg_duel_client`; `lg_duel_scene_3d_tests` and
`lg_duel_client_cvars_tests` pass. A private development client rendered Ratch
through SDL_GPU/Vulkan with its authored textures, outline and weapon socket,
and switched Pip → Ratch → Pip successfully. The test session was stopped.
The integration capture is `art/previews/ratch/in_game.png`.

## September 12 reference polish

- Eyes are closed eyeball volumes behind asymmetric openings cut through the
  unified facial surface. Broad drooping lids and cheek/orbital relief replace
  the shallow eye patches. Iris and pupil surfaces follow the eyeballs and are
  occluded by the lids.
- The continuous nose has a lower tip and carved nostrils. Facial pigment,
  nose redness and local cavity shading are baked into the albedo so some
  facial form survives the game's diffuse indoor lighting.
- Hair uses fewer, broader, tapered locks with longitudinal texture variation,
  longer temple coverage and layered sideburns. Moustache fitting moves each
  strand cross-section while preserving its thickness; the previous fitting
  collapsed front and back faces together.
- The runtime team-colour mask concentrates tint on the waistcoat and keeps
  only a small tint weight on outer garments, preserving the reference's
  brown coat and trousers instead of turning the whole outfit burgundy.
- Corrected inward winding on the ellipsoid helper and orbital volumes; the
  facial bake recalculates consistent normals and unwraps triangulated UV
  islands before baking.

The previous comparison images are in
`revisions/pre_polish_20260912/`. Historical scripts and duplicate runtime binaries are omitted
from this PR. The review gallery includes those images.
All 25 original clips and 16 joints remain inherited from Pip/Fuse.

This is still a stylized approximation of the reference. Skin has less fine
wrinkling and asymmetry, and the hair reads as sculpted locks in close-ups.
Fine strand edges can still alias at gameplay distances. The current game
lighting and team tint also differ substantially from the Blender studio.
See `art/previews/ratch/revision_review.json` for the latest verification.

Serve the repository root to use the gallery and its runtime download link:

```sh
python3 -m http.server 37259 --bind 127.0.0.1
# Open http://127.0.0.1:37259/art/previews/ratch/review.html
```

The committed `.blend` uses lossless Blender compression. Intermediate GLBs
and `.blend1` backups can be regenerated and are intentionally omitted.
