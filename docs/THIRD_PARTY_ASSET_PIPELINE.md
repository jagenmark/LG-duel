# Third-party asset pipeline

The asset CLI keeps outside files out of shipped asset folders until a person has reviewed them. It records where each file came from, checks the stated license, inspects the content, runs fixed processing steps, and builds a review import. It does not decide that vague license text grants rights.

## Policy and trust rules

[`asset_pipeline/policy.json`](../asset_pipeline/policy.json) is the project policy. A source host must appear in `approved_sources`. This allowlist limits where the tool may fetch; it does not approve each asset. Each candidate must give an exact license ID, license URL, title, author, rights flags, attribution text, and saved license evidence. The tool rejects missing, mixed, custom, editorial-only, non-commercial, no-derivatives, or conflicting terms. Words such as “free” and search text never count as proof.

CC0-1.0 and CC-BY-4.0 pass the license gate. CC-BY-4.0 creates an attribution entry. Share-alike and project-specific licenses need a policy and legal review. Non-commercial, no-derivatives, editorial, personal-use, unknown, and custom terms fail.

A tracked review note may record a chosen look, pose, or clip before the final file exists. Such a choice does not approve a file for production. Keep the final file hash unset until that exact file has been built and checked.

The budgets in the policy are hard review limits, not goals for blind mesh cuts. If an asset exceeds a limit, processing may make a separate LOD. It must keep the source and report the change. If the limit needs a clear loss in shape, rig, motion, or texture quality, the job stops for review.

Player review limits are 30,000 vertices, 50,000 triangles, 16 materials, 2,048 pixels per texture side, 96 bones, four skin weights per vertex, and 40 clips. The material and clip limits allow modular stylized bodies and preserve useful source motion without forcing blind merges or deletion.

Supported inputs are FBX, OBJ, glTF, and GLB. Blender may not support every feature in each file. Native `.blend` files stay outside this intake path because loading them gives them more access than an imported data file. Runtime review output uses binary GLB. Textures use PNG, JPEG, or TGA as input; the normal review output uses PNG or JPEG data in or beside the GLB.

## Commands

Run `python scripts/asset.py --help` from the repo root. Each command reads the last stage and writes a JSON result, so an agent can check each step on its own.

```text
asset search
asset inspect-license
asset download
asset inspect
asset process
asset validate
asset import
```

`search` reads a source adapter or checked catalog and returns candidates; it does not infer a license. `inspect-license` saves and checks exact evidence. `download` writes only below `build/asset-staging`, hashes the bytes, and unpacks with path, size, link, and executable checks. `inspect` reports scene facts and faults. `process` calls Blender in factory, background, no-auto-run mode and writes cooked files beside reports. `validate` runs structure checks and, when present, `lg_duel_asset_validate`. `import` copies only a passed review package to `imports/assets/review`. A later code review may move named runtime files to production.

Never run code found in an asset. Blender still parses untrusted data, so use a locked-down VM or container for assets from people you do not trust. The command flags reduce risk but do not form a full sandbox.

## Job contents and reports

Each job keeps the original download, unpacked files, license snapshot, source evidence, provenance JSON, inspection JSON, Blender request and result, validation JSON, a short before/after Markdown report, fixed-view PNG previews, and any credit text. Provenance includes the source URL, title, author, exact license ID and URL, commercial-use, change and credit status, required credit, UTC fetch time, and the original SHA-256. A local override says that it came from a local file; it does not claim that the tool fetched the stated URL.

The review packet must show pass or stop for license, content, processing, budget, rig, and engine checks. Missing bone maps, failed loading, failed checks, or a budget miss that would need a clear visual loss stops the job. GPU outline quality, final weapon placement, animation look, and frame time need review in the client; a CPU test must not claim those checks passed.

## Blender jobs

The trusted script is `tools/asset_pipeline_blender.py`. The CLI starts it in a fresh Blender process and passes JSON after `--`. The request pins axis, scale, budget, LOD, material, texture, skin, animation, rig-map, proxy, and preview choices. The result gives before and after facts, warnings, output paths, budget results, and whether a person must review the look.

The process step only reads bound files from checked staging packages. It saves and checks each input hash. The validate step only runs the engine tool set in policy. Player models also use `asset_pipeline/validation/player_model.json`, which requires a skin, animation, weapon socket, and proxy nodes.

The tracked Worker recipe is `asset_pipeline/recipes/quaternius_worker.json`. It also records the approved two-handed aimed-idle build so a later run does not depend on an edited review `.blend` file.

Retarget jobs must name every required source-to-project bone link. The script stops on a missing required link. Collision and hitbox data are review aids only; they do not change server collision or hit rules.

## CC0 demo

`examples/assets/cc0_crate` holds a small text OBJ, its catalog record, and a saved CC0 record. It lets tests run the search, license, fetch, inspect, process-contract, validate, and review-import path without the network. It is a pipeline sample, not a shipped game asset.
