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

Adaptation schema v2 also pins `source_bsp_sha256` and
`raw_decompile_sha256`. Conversion stops on either mismatch, so reviewed brush
decisions cannot silently migrate to a different BSP or decompile.

## End-to-end usage

```powershell
.\scripts\import-q3-bsp.ps1 `
  -SourceBsp .\imports\q3\overkill\overkill.bsp `
  -SourceAas .\imports\q3\overkill\overkill.aas `
  -Adaptation .\config\q3-import\overkill.json
```

The `-AllowOverLimit` switch does not truncate content. It permits marked
diagnostic output when a projected category exceeds an LG limit. Without it,
the converter writes the candidate and reports, then fails so capacity loss
cannot go unnoticed.

The Python conversion stage:

- inventories BSP/AAS metadata, entities, brushes, patches, materials, bounds,
  gameplay constructs, invalid geometry, and projected LG counts;
- emits only validated classic static brushes and clean supported entities;
- maps Q3 visuals to separately available Tiny3 placeholders by default, or
  to original checked-in LG materials through explicit adaptation roles;
- omits all-`sfx/hellfog` atmospheric brushes before material adaptation so
  they produce neither render nor collision geometry;
- preserves `common/weapclip` as weapon-clip provenance, while `common/clip`
  and `common/playerclip` emit as player clip; current gameplay traces both
  conservatively;
- emits every accepted source worldspawn/func_static brush as its own
  `func_group` with source entity index, brush index, classname, and collision
  classification properties; worldspawn contains import hashes and derived
  reconstructed/marker geometry;
- reconstructs only adaptation-selected quadratic patches as deterministic thin
  convex prisms and keeps every other omitted patch explicit in the report;
- converts clean brush teleports whose target has one unambiguous destination;
- supports a deliberate ordered selection of 2–32 spawns instead of relying on source order;
- writes deterministic JSON and Markdown reports next to the candidate.

`config/q3-import/overkill.json` is the reviewed adaptation for Overkill. It
retains all 32 source deathmatch spawns in their source order, reconstructs the important arches, bridge curves, and
teleporter pad, restores and marks the authored teleport route, supplies reviewed
sun/static lighting in place of unavailable BSP lightmaps, and maps broad source
shader families onto four original LG-Duel texture roles. Adaptation choices and
their resulting geometry counts are preserved in the generated reports.
Its `brush_policy` defaults to `allow` and may address a static brush only by
the stable `(source_entity_index, source_brush_index)` locator. Reviewed rules
require a reason and may allow, drop, or override classification to
`visible_solid`, `playerclip`, or `weapclip`; visible overrides may choose one
configured material role. Overrides never resurrect invalid geometry, fog,
non-solid utilities, or mixed collision materials. JSON and Markdown reports
record an ordered row for every static source brush, including automatic and
requested decisions and the final result.

Validate a candidate through the real loader:

```powershell
cmake --preset default
cmake --build --preset default
.\build\default\lg_duel_map_validate.exe .\maps\overkill_import.map
```

For visual inspection, start the verified GPU developer-control client and use
a map camera preset. Capture operations reject fallback renderers:

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
setting. The derived arena broadphase accelerates collision and trace queries;
the paired simulation workloads and equivalence tests guard its performance and
results. LG-Duel supports up to 32 spawns. Reviewed adaptations should
deliberately select and order 2–32 source spawns rather than depend on source
entity order.
