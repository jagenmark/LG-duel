# Quake 3 BSP Import Pipeline

LG Duel imports Quake 3 and Quake Live BSPs as local, diagnostic source data.
Do not commit BSP, AAS, or raw decompile files, and do not assume that source
textures, shaders, models, sounds, or other visual assets are redistributable.

## Toolchain

`scripts/setup-q3map2.ps1` downloads the pinned NetRadiant-custom `20260114`
Windows x64 archive and verifies its exact byte size, SHA-256, and q3map2
`2.5.17n-git-68ecbed` banner before installing it under the ignored build tree.
The complete archive is extracted because q3map2 depends on its shipped DLLs;
the install manifest hashes every extracted file and verifies the distribution
on reuse. A canonical digest of that complete 635-file hash list is pinned in
the setup script, so editing both a DLL and the adjacent manifest cannot pass.

`scripts/decompile-q3-bsp.ps1` reads the BSP header and selects `quake3` for
IBSP v46 or `quakelive` for IBSP v47. It rejects other formats, stages a copy
of the source, and preserves q3map2's generated map unchanged as
`<import>/work/<name>.raw.map`.

The end-to-end wrapper always runs that decompile step, even when a same-name
raw map already exists. This prevents a replaced BSP from being attributed to
stale intermediate geometry. It stages the raw map, candidate, and both reports
as one complete generation before replacing prior artifacts; the reports are
published last and bind both raw and candidate bytes by SHA-256.

## End-to-end usage

```powershell
.\scripts\import-q3-bsp.ps1 `
  -SourceBsp .\imports\q3\overkill\overkill.bsp `
  -SourceAas .\imports\q3\overkill\overkill.aas `
  -AllowOverLimit
```

The `-AllowOverLimit` switch does not truncate content. It permits marked
diagnostic output when a projected category exceeds an LG limit. Without it,
the converter writes the candidate and reports, then fails so capacity loss
cannot go unnoticed.

The Python conversion stage:

- inventories BSP/AAS metadata, entities, brushes, patches, materials, bounds,
  gameplay constructs, invalid geometry, and projected LG counts;
- emits only validated classic static brushes and clean supported entities;
- maps all Q3 visuals to separately available Tiny3 placeholder textures;
- converts common clip brushes to invisible LG player collision;
- never tessellates patches or silently approximates teleports and unsupported
  gameplay entities;
- writes deterministic JSON and Markdown reports next to the candidate.

Validate a candidate through the real loader:

```powershell
cmake --preset default
cmake --build --preset default
.\build\default\lg_duel_map_validate.exe .\maps\overkill_import.map
```

For visual inspection, start the developer-control client and use a map camera
preset:

```powershell
.\scripts\lg-control.ps1 start
.\scripts\lg-control.ps1 capture-map-views --map overkill_import --preset structural
```

The old 256-wall and 256-convex-brush values were engine container limits, not
TrenchBroom format limits. The current fixed arena capacities are 2048 walls
and 1024 convex brushes, with lazy heap-backed storage so ordinary maps do not
pay the full footprint. TrenchBroom can author maps above either threshold, but
LG-Duel's real loader rejects them explicitly. Collision and traces still scan
active geometry linearly, so increasing the caps again should be paired with a
spatial broadphase and performance profiling rather than treated as a map-editor
setting. LG-Duel also activates only the first six authored duel spawns; reports
state emitted, active, and inactive spawn counts separately.
