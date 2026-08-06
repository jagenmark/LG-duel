# Worker material atlases

`worker_albedo.png` is an sRGB 512 by 512 atlas. It contains broad, clean
base colours only. It has no baked light, shadow, dirt, damage, or small
surface detail.

`worker_material_mask.png` is a linear 512 by 512 atlas. Its channels are:

| Channel | Meaning |
| --- | --- |
| R | Team-tint weight |
| G | Perceptual roughness |
| B | Metallic weight |
| A | Reserved emissive weight; zero for this pass |

The Worker source GLB exposes finite but degenerate skinned UVs. The
model-local manifest therefore selects a padded atlas cell per reviewed
material region. This preserves the reviewed GLB, geometry, rig, clips, and
weapon socket while still using one shared albedo atlas and one shared mask.

`source/worker_albedo_grid_reference.png` was generated as a clean palette
reference. `tools/generate_worker_material_atlas.py` crops its broad swatches
into the deterministic runtime atlas and creates the exact linear mask.

## Region plan

| GLB material | Atlas cell | Intended response | Team tint |
| --- | --- | --- | --- |
| Skin, Skin.001 | 0,0 | Soft dielectric skin | None |
| Worker_Yellow, Worker_Yellow.001 | 1,0 | Matte shirt, broad front and rear read | High |
| Worker_Vest | 2,0 | Matte torso vest, broad side and rear read | Full |
| LightBrown | 3,0 | Neutral gloves | None |
| Grey | 0,1 | Small hard accessory with restrained metal | None |
| Black | 1,1 | Dark, rough footwear | None |
| Eyebrows, Moustache | 2,1 | Dark, rough facial hair | None |
| Eye | 3,1 | Soft dielectric eye | None |
| Brown | 0,2 | Neutral leather-like hard detail | None |
| Brown2 | 1,2 | Darker neutral leather-like detail | None |

The shirt and vest are the only team-tinted regions. They are broad clothing
regions visible from the front, side, and rear. Skin, hair, eyes, gloves,
boots, and hard accessories use a zero mask weight.

## Rebuild and check

Run `python tools/generate_worker_material_atlas.py` after changing the source
palette or mask values. Run `python tools/generate_worker_material_atlas.py
--check` in validation. The PNG files contain base level 0 only; the renderer
generates the full mip chain once during shared resource creation.
