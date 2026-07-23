# Modular men batch jobs

Each character recipe only names the shared profile, source character, and output name. The profile owns the rig reference, animation set, weapon socket, two-handed pose, budgets, and preview list.

Prepare and check a request without starting Blender:

```powershell
python tools/asset_pipeline/modular_men_batch.py asset_pipeline/recipes/quaternius_farmer.json --prepare-only
```

Build Farmer for review:

```powershell
python tools/asset_pipeline/modular_men_batch.py asset_pipeline/recipes/quaternius_farmer.json --blender "C:/Program Files/Blender Foundation/Blender 5.1/blender.exe"
```

The job first writes under `build/asset-staging/review-batches`. Each input gets checked against its intake report and hash. A Blender check compares all bone names, parents, and bind matrices with Worker before the main build starts. Any failed check stops the job. The normal player-model checks and fixed engine tool then seal a copy under `imports/assets/review`. That copy includes the source file, source record, license text, reports, previews, and processed GLB. It is review data, not a game asset.

The modular job retargets and bakes each source action onto the checked character rig. Character `Hand.L/R` bones read animation `Wrist.L/R`; every other target bone requires the same source name. The build stops if any target bone has no source or stated alias. It then renames the baked character hand bones and skin groups to `Wrist.L/R` for the weapon socket and two-handed setup.
