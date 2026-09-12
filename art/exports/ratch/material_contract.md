# Ratch runtime material contract

The runtime package uses the engine's model-local, SHA-bound
`material-manifest.json`. The albedo is sRGB; the packed mask is linear and
stores team-tint weight, perceptual roughness, metallic weight, and reserved
zero emissive in RGBA respectively. Both atlases are 2048 × 2048, respecting
the current engine limit.

The facial pigment/occlusion map has a 1024px atlas region. Eleven other
textured materials (cloth and hair) each receive a 512px region with gutters;
nine constant-color materials share smaller swatches. Solid colors are
converted from authored linear values to sRGB. Textile tiles bake the original
UV-domain repeats, and primitive UVs are remapped into those regions.
`uv_mode` is `texcoord0`: `material_cell` would reduce a primitive to one
sampled texel. See `runtime/atlas_report.json` for exact regions and UV domains.

The packed mask retains authored roughness and metallic values. Skin, eyes,
hair, gloves, soles, stitching, and brass stay untinted; coat/vest/linen carry
the team-color weights. The engine uses albedo replacement. Blender's
procedural bump nodes are not part of this runtime shader; the source-material
GLB and `.blend` remain the correct Blender preview/editing assets. The
runtime GLB's UV atlas layout is intended for the engine manifest loader.

General asset validation and a targeted compiled engine material check are
saved in `runtime/engine_validation.json` and
`runtime/material_engine_check.json`. The latter explicitly requires a valid
manifest, available authored textures, textured vertex modes, and varying UVs.
Live GPU captures of the final package are in `art/previews/ratch/`; the
September 12 run used SDL_GPU/Vulkan on Intel LNL. See the source
`LIMITATIONS.md` for the appearance and performance limits.
