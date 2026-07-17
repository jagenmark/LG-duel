# Quake 3 map conversion inventory

Status: **over_limit**

## Sources

- q3map2: NetRadiant-custom 20260114, `2.5.17n-git-68ecbed`, setup `scripts/setup-q3map2.ps1`, archive SHA-256 `25c2e14e2b0bd7a9897b2f943c8821458873c8713973f9c3d68d49f26fe79e35`
- Raw map: `overkill.raw.map` (2440317 bytes, SHA-256 `30184ddbeb78028d63b4658c97591672e1000c7646b8b00967f914e1e42e5efc`)
- BSP: `overkill.bsp`, IBSP v47, 6863428 bytes, SHA-256 `ee3c8d361b148db8399d15369e3a47c128c4b5f91a1956757be5b0b5e68b7133`
- Generated candidate: 1103104 bytes, SHA-256 `1fdb734a8d46b9babd80d8bcbd159db8a7e46f12a45d7446159bf5ebf7d728b6`
- BSP world bounds: min `[-1664.0, -4120.0, -1640.0]`, max `[3000.000244140625, 2368.0, 1824.0]`
- AAS: `overkill.aas`, EAAS v5, BSP checksum `-1989511671`, 867588 bytes, SHA-256 `e53625957ad4e2dd2cb9987c6b24f74b6635d2c5308c7764339dd4b22bc60b92`
- AAS routes: metadata only; no AAS route import was attempted.

### BSP lumps

| Index | Lump | Offset | Bytes | Records | Trailing bytes |
|---:|---|---:|---:|---:|---:|
| 0 | `entities` | 6751336 | 19094 | 19094 | 0 |
| 1 | `textures` | 208 | 4968 | 69 | 0 |
| 2 | `planes` | 5176 | 91232 | 5702 | 0 |
| 3 | `nodes` | 418680 | 240588 | 6683 | 0 |
| 4 | `leafs` | 96408 | 322272 | 6714 | 0 |
| 5 | `leaf_faces` | 795724 | 104324 | 26081 | 0 |
| 6 | `leaf_brushes` | 900048 | 44272 | 11068 | 0 |
| 7 | `models` | 944320 | 1200 | 30 | 0 |
| 8 | `brushes` | 659268 | 23832 | 1986 | 0 |
| 9 | `brush_sides` | 683100 | 112624 | 14078 | 0 |
| 10 | `vertices` | 945520 | 1259896 | 28634 | 0 |
| 11 | `mesh_vertices` | 6770648 | 92268 | 23067 | 0 |
| 12 | `effects` | 6770432 | 216 | 3 | 0 |
| 13 | `faces` | 2205416 | 433056 | 4164 | 0 |
| 14 | `lightmaps` | 3865048 | 1277952 | 26 | 0 |
| 15 | `light_volumes` | 5143000 | 1608336 | 201042 | 0 |
| 16 | `visibility` | 2638472 | 1226576 | n/a | n/a |

## Inventory

- Entities: 233
- Classic brushes: 1986
- Patches (explicitly omitted): 306
- Valid brushes: 1975
- Invalid brushes: 11
- Degenerate faces: 0

### Classnames

| Classname | Count |
|---|---:|
| `advertisement` | 4 |
| `ammo_bullets` | 3 |
| `ammo_cells` | 1 |
| `ammo_grenades` | 1 |
| `ammo_lightning` | 1 |
| `ammo_pack` | 10 |
| `ammo_rockets` | 2 |
| `ammo_shells` | 3 |
| `ammo_slugs` | 1 |
| `func_static` | 13 |
| `info_player_deathmatch` | 32 |
| `info_player_intermission` | 1 |
| `item_armor_body` | 1 |
| `item_armor_combat` | 1 |
| `item_armor_jacket` | 1 |
| `item_armor_shard` | 4 |
| `item_health` | 8 |
| `item_health_large` | 1 |
| `item_health_mega` | 1 |
| `item_health_small` | 7 |
| `item_quad` | 1 |
| `item_regen` | 1 |
| `misc_portal_camera` | 1 |
| `misc_teleporter_dest` | 1 |
| `target_location` | 34 |
| `target_position` | 26 |
| `target_speaker` | 32 |
| `team_CTF_blueplayer` | 8 |
| `team_CTF_redplayer` | 9 |
| `team_dom_point` | 3 |
| `trigger_hurt` | 3 |
| `trigger_multiple` | 3 |
| `trigger_push` | 5 |
| `trigger_teleport` | 1 |
| `weapon_grenadelauncher` | 1 |
| `weapon_hmg` | 1 |
| `weapon_lightning` | 1 |
| `weapon_plasmagun` | 1 |
| `weapon_railgun` | 1 |
| `weapon_rocketlauncher` | 1 |
| `weapon_shotgun` | 2 |
| `worldspawn` | 1 |

### Source face materials

| Material | Faces |
|---|---:|
| `base_floor/pjgrate2` | 24 |
| `base_light/ceil1_38_10k` | 62 |
| `base_support/cable` | 32 |
| `base_trim/pewter_shiney` | 490 |
| `common/caulk` | 1031 |
| `common/clip` | 645 |
| `common/hint` | 186 |
| `common/nodraw` | 26 |
| `common/nodrop` | 18 |
| `common/trigger` | 86 |
| `common/weapclip` | 6 |
| `ctf/ctf_redflag` | 3 |
| `gothic_block/blocks11b` | 1605 |
| `gothic_block/blocks17j` | 30 |
| `gothic_block/blocks18b` | 310 |
| `gothic_block/blocks18c_3` | 1203 |
| `gothic_block/gkc10` | 118 |
| `gothic_block/gkc15_big` | 116 |
| `gothic_block/gkc_large_right` | 4 |
| `gothic_ceiling/ceilingtechplain` | 150 |
| `gothic_door/archxiandm1dblack_pot` | 35 |
| `gothic_door/xian_tourneyarch_inside2` | 24 |
| `gothic_door/xian_tourneyarch_tall2b_pot` | 39 |
| `gothic_floor/blocks17floor2` | 273 |
| `gothic_floor/largerblock3b2` | 479 |
| `gothic_floor/metalbridge06` | 45 |
| `gothic_floor/q1metal7_98d_256x256` | 395 |
| `gothic_floor/xstepborder10` | 92 |
| `gothic_floor/xstepborder12` | 45 |
| `gothic_floor/xstepborder8` | 100 |
| `gothic_light/ironcrosslt2_1000` | 22 |
| `gothic_light/pentagram_light1_10K` | 61 |
| `gothic_trim/baseboard09_c3` | 263 |
| `gothic_trim/km_arena1tower4` | 3 |
| `gothic_trim/km_arena1tower7` | 16 |
| `gothic_trim/metalsupsolid` | 312 |
| `gothic_trim/newskull` | 23 |
| `gothic_trim/pitted_roof` | 6 |
| `gothic_trim/pitted_rust` | 2530 |
| `gothic_trim/pitted_rust3_black` | 830 |
| `gothic_wall/dm5_archifin_pot` | 2 |
| `gothic_wall/xiantourneywall_c1_pot` | 6 |
| `sfx/bounce_xq1metalbig` | 14 |
| `sfx/flame1_hell` | 1 |
| `sfx/hellfog` | 19 |
| `sfx/launchpad_blocks17` | 2 |
| `sfx/metalbridge06_bounce` | 6 |
| `skies/overkill` | 217 |
| `stone/pjrock21` | 12 |

## Gameplay conversion

- Source spawns: 32
- Source lights: 0
- Source health pickups: 16
- Source trigger_push entities: 5
- Source teleports (omitted): 1
- Runtime-active spawns: 6 (inactive authored spawns: 26)

## Projected LG counts

| Category | Projected | Limit | Result |
|---|---:|---:|---|
| walls | 1256 | 2048 | OK |
| convex_brushes | 673 | 1024 | OK |
| lights | 0 | 96 | OK |
| jump_pads | 5 | 48 | OK |
| health_pickups | 16 | 32 | OK |
| spawns | 32 | 6 | OVER |

The converter did not truncate the candidate. LG-Duel activates only the first six authored spawns; the active/inactive counts above state that runtime behavior. Patches were not approximated. Q3 shaders and unsupported entities are listed in the JSON report and were not imported.

Collision material policy: All-common/clip and all-common/weapclip brushes become invisible common/playerclip. LG-Duel has no weapon-only collision mask, so weapclip is conservatively retained for all traces.

Scale policy: No rescale or rebalance is applied. Authored Quake coordinates are preserved; LG-Duel's existing loader converts them at 1/40 and bounds gain 40 Quake units of padding.

## Omitted content

- Patches: 306
- Brushes `confident_non_solid_utility`: 34
- Brushes `invalid_geometry`: 11
- Entities `advertisement:unsupported`: 4
- Entities `ammo_bullets:unsupported`: 3
- Entities `ammo_cells:unsupported`: 1
- Entities `ammo_grenades:unsupported`: 1
- Entities `ammo_lightning:unsupported`: 1
- Entities `ammo_pack:unsupported`: 10
- Entities `ammo_rockets:unsupported`: 2
- Entities `ammo_shells:unsupported`: 3
- Entities `ammo_slugs:unsupported`: 1
- Entities `info_player_intermission:unsupported`: 1
- Entities `item_armor_body:unsupported`: 1
- Entities `item_armor_combat:unsupported`: 1
- Entities `item_armor_jacket:unsupported`: 1
- Entities `item_armor_shard:unsupported`: 4
- Entities `item_health_mega:unsupported`: 1
- Entities `item_quad:unsupported`: 1
- Entities `item_regen:unsupported`: 1
- Entities `misc_portal_camera:unsupported`: 1
- Entities `misc_teleporter_dest:teleport_unsupported`: 1
- Entities `target_location:unsupported`: 34
- Entities `target_speaker:unsupported`: 32
- Entities `team_CTF_blueplayer:unsupported`: 8
- Entities `team_CTF_redplayer:unsupported`: 9
- Entities `team_dom_point:unsupported`: 3
- Entities `trigger_hurt:unsupported`: 3
- Entities `trigger_multiple:unsupported`: 3
- Entities `trigger_teleport:teleport_unsupported`: 1
- Entities `weapon_grenadelauncher:unsupported`: 1
- Entities `weapon_hmg:unsupported`: 1
- Entities `weapon_lightning:unsupported`: 1
- Entities `weapon_plasmagun:unsupported`: 1
- Entities `weapon_railgun:unsupported`: 1
- Entities `weapon_rocketlauncher:unsupported`: 1
- Entities `weapon_shotgun:unsupported`: 2

## Invalid geometry

| Entity | Brush | Line | Reason |
|---:|---:|---:|---|
| 0 | 267 | 2680 | convex face resolves to 0 vertices |
| 0 | 303 | 3050 | convex brush has no closed volume |
| 0 | 304 | 3060 | convex face resolves to 0 vertices |
| 0 | 307 | 3094 | convex face resolves to 0 vertices |
| 0 | 378 | 3814 | convex face resolves to 0 vertices |
| 0 | 381 | 3846 | convex face resolves to 0 vertices |
| 0 | 391 | 3950 | convex face resolves to 2 vertices |
| 0 | 1762 | 17710 | convex brush has no closed volume |
| 0 | 1763 | 17722 | convex brush has no closed volume |
| 0 | 1789 | 17986 | convex face resolves to 0 vertices |
| 0 | 1790 | 17998 | convex face resolves to 0 vertices |
