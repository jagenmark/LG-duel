# Ratch verification — September 12, 2026

The PR was assembled and built in an isolated checkout based on
`origin/main` at `0ed5eb642cdddc4fa096b7b1f7e2860a9501a2a5`.
The installed GLB, both atlases and manifest match the polished source checkout
and isolated build copies byte for byte.

| Check | Result | What it establishes |
| --- | --- | --- |
| Debug client/server build | Passed | Selected integration changes build together from the base branch |
| `lg_duel_client_cvars_tests` | Passed | Model values 0/1/2 accepted, 3 rejected, default remains 1 |
| `lg_duel_scene_3d_tests` | Passed | Pip/Ratch loading, selected bounds, weapon socket, skin palettes, inherited clips, crouch scale and existing scene coverage |
| `lg_duel_player_presentation_tests` | Passed | Pose behavior and authoritative standing size through crouch interpolation |
| `lg_duel_movement_tests` | Passed | Sharing the existing 0.62 crouch constant preserves base movement behavior |
| `validate_ratch.py` | Passed | All 25 animation samples and targets preserved, 16-joint bind contract, finite attributes and valid weights |
| Engine asset validator | Passed | 21 primitives, 457,665 vertices, 858,224 triangles; 1,200 pose samples at 16 instances, without a timing threshold |
| Engine material checker | Passed | Valid SHA-bound manifest, authored images available, varying UVs for all 21 primitives |
| Compressed Blender reopen | Passed | Editable mesh, 25 actions and 12 packed images remain present after lossless compression |
| Isolated Vulkan client | Visually reviewed | Ratch body, authored texture, outline and weapon render; live Ratch → Pip → Ratch switching succeeds |

Build and focused test commands:

```sh
cmake --preset default
cmake --build --preset default --target lg_duel_client lg_duel_server lg_duel_scene_3d_tests lg_duel_player_presentation_tests lg_duel_client_cvars_tests lg_duel_movement_tests lg_duel_asset_validate -j 6
ctest --preset default -R 'lg_duel_(scene_3d|player_presentation|client_cvars|movement)_tests' --output-on-failure
python3 art/source/ratch/validate_ratch.py assets/models/ratch/ratch.glb assets/models/pip_fuse/pip_fuse.glb --report art/exports/ratch/runtime/validation.md
build/default/lg_duel_asset_validate assets/models/ratch/ratch.glb --instances 16
```

The local configure reused the same pinned SDL source checkout via
`FETCHCONTENT_SOURCE_DIR_SDL3`; the build directory was fresh. Material-check
compilation instructions are in [README.md](README.md).

The [PR runtime metadata](../../previews/ratch/pr_gpu_verification.json) records
SDL_GPU/Vulkan, Intel LNL and Mesa 26.2.2 in `eyetoeye`. The committed
[full-body](../../previews/ratch/in_game.png),
[close-up](../../previews/ratch/in_game_closeup.png),
[Pip switch](../../previews/ratch/switch_pip.png) and
[Ratch return](../../previews/ratch/switch_ratch.png) captures come from that
isolated build. The owned client and server were stopped after restoring model
1 and disabling connected-body preview.

Eleven Blender stills and sixteen pose views were reviewed during polishing.
The death camera was adjusted to include the entire fallen character; the lean
preview applies the runtime procedural bend without modifying source clips.
The face/profile, boots in run/slide, and head/hair from multiple angles were
inspected. These are sampled visual checks, not proof of every deformation.

This verification does not establish production frame time, memory budgets,
Windows behavior, other GPU vendors, final reference likeness or correct visual
crouch/collider alignment. The full repository test suite and remote CI were
not run locally for this draft. See [LIMITATIONS.md](LIMITATIONS.md).
