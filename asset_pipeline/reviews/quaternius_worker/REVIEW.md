# Quaternius Worker review record

## Status

The reviewed Worker GLB is now also integrated as the production player-model option `r_player_model 2`. The runtime copy is `assets/models/quaternius_worker/quaternius_worker.glb`; new characters from the same pack must still complete their own review before production use.

The checked review GLB is `imports/assets/review/quaternius_worker/quaternius_worker.glb`. Its SHA-256 is `b72bb9287f761550b059f4dffcf721c78ae19d814c0de74633e4cbe18c455c60`.

## Source and rights

- Asset: Worker from Ultimate Modular Men Pack
- Author: Quaternius
- Source page: https://quaternius.com/packs/ultimatemodularcharacters.html
- Source download: https://drive.google.com/drive/folders/1USAAquX2JJWuA2m6zol0KUkFe3UkZ8zX?usp=sharing
- License: CC0-1.0
- License terms: https://creativecommons.org/publicdomain/zero/1.0/
- Saved evidence: `asset_pipeline/candidates/quaternius_worker_skeletal_mesh_license.json`
- Attribution required: no
- Changes allowed: yes
- Commercial use allowed: yes

Source hashes from the staging provenance records:

- `Worker.zip`: `ec4f387035e052a3458ce56b8bb8b7684d6b82077cfefb74e23b35ccc209e4b7`
- The import packet saves the Quaternius source page, input hashes, and all fixed stills.
- `Animations.fbx`: `259ed6f6e5f1bb2a15215a66a9ed02e8956083a281a8098103490d1a00292b7f`

## Review build

The sealed process report is `imports/assets/review/quaternius_worker/reports/process-tool.json`. It reports 5,240 triangles, 2,676 source mesh vertices, 13 materials, 73 bones, and 33 animation clips. Its size checks pass. It also marks visual review as required.

The headless engine report passes file load, mesh, bounds, skin weight, animation sample, proxy data, and fixed multi-instance CPU checks. It does not check GPU output, the look of the outline, weapon fit, motion quality, or frame time in the client.

## Approved idle choice

Use the custom `Idle_Gun_TwoHanded` action for the gameplay aimed idle. This two-handed weapon pose is the approved idle choice. Keep the source single-handed clips in the file, but do not select them as the main aimed idle.

The checked GLB contains `Idle_Gun_TwoHanded`, built from `Idle_Gun_Pointing` with an 18 cm hand offset and a 90-degree support-wrist roll. The original clips remain available.

## Production state

The game loads this model through the skinned player path and maps its aimed idle to `Idle_Gun_TwoHanded`. The runtime integration does not approve other characters from the pack; each new mesh still needs fixed previews, engine validation, and a separate visual review.
