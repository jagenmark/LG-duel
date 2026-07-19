# Quake 3 map conversion inventory

Status: **convertible**

## Sources

- q3map2: NetRadiant-custom 20260114, `2.5.17n-git-68ecbed`, setup `scripts/setup-q3map2.ps1`, archive SHA-256 `25c2e14e2b0bd7a9897b2f943c8821458873c8713973f9c3d68d49f26fe79e35`
- Raw map: `overkill.raw.map` (2440317 bytes, SHA-256 `30184ddbeb78028d63b4658c97591672e1000c7646b8b00967f914e1e42e5efc`)
- BSP: `overkill.bsp`, IBSP v47, 6863428 bytes, SHA-256 `ee3c8d361b148db8399d15369e3a47c128c4b5f91a1956757be5b0b5e68b7133`
- Generated candidate: 1662798 bytes, SHA-256 `de342e0bc20677210c606d0af55fc87bb5a49b7983e1b8e947ea701c351669a2`
- BSP world bounds: min `[-1664.0, -4120.0, -1640.0]`, max `[3000.000244140625, 2368.0, 1824.0]`
- AAS: `overkill.aas`, EAAS v5, BSP checksum `-1989511671`, 867588 bytes, SHA-256 `e53625957ad4e2dd2cb9987c6b24f74b6635d2c5308c7764339dd4b22bc60b92`
- Adaptation binding: schema v2, verified `True`, BSP `ee3c8d361b148db8399d15369e3a47c128c4b5f91a1956757be5b0b5e68b7133`, raw `30184ddbeb78028d63b4658c97591672e1000c7646b8b00967f914e1e42e5efc`
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
- Source patches: 306
- Reconstructed patches: 38 (242 convex brushes)
- Valid brushes: 2218
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
- Source teleports: 1 (converted: 1)
- Runtime-active spawns: 32 (inactive authored spawns: 0)

## Projected LG counts

| Category | Projected | Limit | Result |
|---|---:|---:|---|
| walls | 1254 | 2048 | OK |
| convex_brushes | 915 | 1024 | OK |
| lights | 6 | 96 | OK |
| jump_pads | 5 | 48 | OK |
| health_pickups | 16 | 32 | OK |
| spawns | 32 | 32 | OK |
| teleports | 1 | 16 | OK |

## Static brush provenance and policy

Default action: `allow`. Rules: 0 (drops: 0, overrides: 0).

| Entity | Brush | Class | Line | Materials | Automatic | Requested | Effective | Result / reason |
|---:|---:|---|---:|---|---|---|---|---|
| 0 | 0 | `worldspawn` | 10 | `gothic_trim/pitted_rust, sfx/hellfog` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1 | `worldspawn` | 20 | `common/caulk, gothic_block/gkc15_big` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 2 | `worldspawn` | 30 | `common/caulk, gothic_block/blocks11b, gothic_block/gkc15_big` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 3 | `worldspawn` | 40 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 4 | `worldspawn` | 50 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 5 | `worldspawn` | 60 | `common/caulk, gothic_door/xian_tourneyarch_inside2, gothic_door/xian_tourneyarch_tall2b_pot, gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 6 | `worldspawn` | 70 | `common/caulk, gothic_block/blocks11b, gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 7 | `worldspawn` | 80 | `common/caulk, gothic_block/blocks11b, gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 8 | `worldspawn` | 90 | `common/caulk, gothic_block/blocks11b, gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 9 | `worldspawn` | 100 | `common/caulk, gothic_block/blocks11b, gothic_light/pentagram_light1_10k` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 10 | `worldspawn` | 110 | `gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 11 | `worldspawn` | 120 | `common/caulk, gothic_block/blocks11b, gothic_block/gkc15_big` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 12 | `worldspawn` | 130 | `gothic_block/blocks11b, gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 13 | `worldspawn` | 140 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 14 | `worldspawn` | 150 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 15 | `worldspawn` | 160 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 16 | `worldspawn` | 170 | `common/caulk, gothic_door/xian_tourneyarch_inside2, gothic_door/xian_tourneyarch_tall2b_pot, gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 17 | `worldspawn` | 180 | `gothic_block/blocks11b, gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 18 | `worldspawn` | 190 | `common/caulk, gothic_block/blocks11b, gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 19 | `worldspawn` | 200 | `gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 20 | `worldspawn` | 210 | `gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 21 | `worldspawn` | 220 | `gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 22 | `worldspawn` | 230 | `gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 23 | `worldspawn` | 240 | `gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 24 | `worldspawn` | 250 | `gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 25 | `worldspawn` | 260 | `common/caulk, gothic_door/xian_tourneyarch_inside2, gothic_door/xian_tourneyarch_tall2b_pot, gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 26 | `worldspawn` | 270 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 27 | `worldspawn` | 280 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 28 | `worldspawn` | 290 | `gothic_light/ironcrosslt2_1000, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 29 | `worldspawn` | 300 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 30 | `worldspawn` | 310 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 31 | `worldspawn` | 320 | `common/caulk, gothic_door/xian_tourneyarch_inside2, gothic_door/xian_tourneyarch_tall2b_pot, gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 32 | `worldspawn` | 330 | `gothic_block/blocks11b, gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 33 | `worldspawn` | 340 | `gothic_block/blocks11b, gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 34 | `worldspawn` | 350 | `common/caulk, gothic_block/blocks11b, gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 35 | `worldspawn` | 360 | `gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 36 | `worldspawn` | 370 | `gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 37 | `worldspawn` | 380 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 38 | `worldspawn` | 390 | `gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 39 | `worldspawn` | 400 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 40 | `worldspawn` | 410 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 41 | `worldspawn` | 420 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 42 | `worldspawn` | 430 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 43 | `worldspawn` | 440 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 44 | `worldspawn` | 450 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 45 | `worldspawn` | 460 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 46 | `worldspawn` | 470 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 47 | `worldspawn` | 480 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 48 | `worldspawn` | 490 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 49 | `worldspawn` | 500 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 50 | `worldspawn` | 510 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 51 | `worldspawn` | 520 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 52 | `worldspawn` | 530 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 53 | `worldspawn` | 540 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 54 | `worldspawn` | 550 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 55 | `worldspawn` | 560 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 56 | `worldspawn` | 570 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 57 | `worldspawn` | 580 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 58 | `worldspawn` | 590 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 59 | `worldspawn` | 600 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 60 | `worldspawn` | 610 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 61 | `worldspawn` | 620 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 62 | `worldspawn` | 630 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 63 | `worldspawn` | 640 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 64 | `worldspawn` | 650 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 65 | `worldspawn` | 660 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 66 | `worldspawn` | 670 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 67 | `worldspawn` | 680 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 68 | `worldspawn` | 690 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 69 | `worldspawn` | 700 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 70 | `worldspawn` | 710 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 71 | `worldspawn` | 720 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 72 | `worldspawn` | 730 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 73 | `worldspawn` | 740 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 74 | `worldspawn` | 750 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 75 | `worldspawn` | 760 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 76 | `worldspawn` | 770 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 77 | `worldspawn` | 780 | `common/caulk, gothic_door/xian_tourneyarch_inside2, gothic_door/xian_tourneyarch_tall2b_pot` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 78 | `worldspawn` | 790 | `gothic_block/blocks11b, gothic_door/xian_tourneyarch_inside2, gothic_door/xian_tourneyarch_tall2b_pot` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 79 | `worldspawn` | 800 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 80 | `worldspawn` | 810 | `common/caulk, gothic_door/xian_tourneyarch_inside2, gothic_door/xian_tourneyarch_tall2b_pot` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 81 | `worldspawn` | 820 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 82 | `worldspawn` | 830 | `common/caulk, gothic_door/xian_tourneyarch_inside2, gothic_door/xian_tourneyarch_tall2b_pot, gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 83 | `worldspawn` | 840 | `common/caulk, gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 84 | `worldspawn` | 850 | `common/caulk, gothic_block/blocks11b, gothic_door/xian_tourneyarch_inside2, gothic_door/xian_tourneyarch_tall2b_pot` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 85 | `worldspawn` | 860 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 86 | `worldspawn` | 870 | `common/caulk, gothic_block/blocks11b, gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 87 | `worldspawn` | 880 | `common/caulk, gothic_block/blocks11b, gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 88 | `worldspawn` | 890 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 89 | `worldspawn` | 900 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 90 | `worldspawn` | 910 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 91 | `worldspawn` | 920 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 92 | `worldspawn` | 930 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 93 | `worldspawn` | 940 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 94 | `worldspawn` | 950 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 95 | `worldspawn` | 960 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 96 | `worldspawn` | 970 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 97 | `worldspawn` | 980 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 98 | `worldspawn` | 990 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 99 | `worldspawn` | 1000 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 100 | `worldspawn` | 1010 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 101 | `worldspawn` | 1020 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 102 | `worldspawn` | 1030 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 103 | `worldspawn` | 1040 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 104 | `worldspawn` | 1050 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 105 | `worldspawn` | 1060 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 106 | `worldspawn` | 1070 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 107 | `worldspawn` | 1080 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 108 | `worldspawn` | 1090 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 109 | `worldspawn` | 1100 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 110 | `worldspawn` | 1110 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 111 | `worldspawn` | 1120 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 112 | `worldspawn` | 1130 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 113 | `worldspawn` | 1140 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 114 | `worldspawn` | 1150 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 115 | `worldspawn` | 1160 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 116 | `worldspawn` | 1170 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 117 | `worldspawn` | 1180 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 118 | `worldspawn` | 1190 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 119 | `worldspawn` | 1200 | `base_trim/pewter_shiney, common/caulk` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 120 | `worldspawn` | 1210 | `base_trim/pewter_shiney, common/caulk` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 121 | `worldspawn` | 1220 | `base_trim/pewter_shiney, common/caulk` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 122 | `worldspawn` | 1230 | `base_trim/pewter_shiney, common/caulk` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 123 | `worldspawn` | 1240 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 124 | `worldspawn` | 1250 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 125 | `worldspawn` | 1260 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 126 | `worldspawn` | 1270 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 127 | `worldspawn` | 1280 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 128 | `worldspawn` | 1290 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 129 | `worldspawn` | 1300 | `common/caulk, gothic_block/blocks11b, gothic_ceiling/ceilingtechplain` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 130 | `worldspawn` | 1310 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 131 | `worldspawn` | 1320 | `gothic_block/blocks11b, gothic_block/blocks18c_3, gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 132 | `worldspawn` | 1330 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 133 | `worldspawn` | 1340 | `base_trim/pewter_shiney, common/caulk, gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 134 | `worldspawn` | 1350 | `base_trim/pewter_shiney, common/caulk` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 135 | `worldspawn` | 1360 | `base_trim/pewter_shiney, common/caulk, gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 136 | `worldspawn` | 1370 | `base_trim/pewter_shiney, common/caulk` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 137 | `worldspawn` | 1380 | `base_floor/pjgrate2, common/caulk, common/nodraw` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 138 | `worldspawn` | 1390 | `base_trim/pewter_shiney, common/caulk` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 139 | `worldspawn` | 1400 | `base_trim/pewter_shiney, common/caulk` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 140 | `worldspawn` | 1410 | `base_trim/pewter_shiney, common/caulk, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 141 | `worldspawn` | 1420 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 142 | `worldspawn` | 1430 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 143 | `worldspawn` | 1440 | `gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 144 | `worldspawn` | 1450 | `gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 145 | `worldspawn` | 1460 | `gothic_block/blocks11b, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 146 | `worldspawn` | 1470 | `gothic_block/blocks11b, gothic_block/blocks18c_3, gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 147 | `worldspawn` | 1480 | `gothic_door/archxiandm1dblack_pot, gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 148 | `worldspawn` | 1490 | `common/caulk, gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 149 | `worldspawn` | 1500 | `gothic_block/blocks11b, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 150 | `worldspawn` | 1510 | `gothic_block/blocks11b, gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 151 | `worldspawn` | 1520 | `gothic_block/blocks11b, gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 152 | `worldspawn` | 1530 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 153 | `worldspawn` | 1540 | `gothic_block/blocks11b, gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 154 | `worldspawn` | 1550 | `base_trim/pewter_shiney, common/caulk, gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 155 | `worldspawn` | 1560 | `base_trim/pewter_shiney, common/caulk` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 156 | `worldspawn` | 1570 | `base_trim/pewter_shiney, common/caulk, gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 157 | `worldspawn` | 1580 | `base_trim/pewter_shiney, common/caulk` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 158 | `worldspawn` | 1590 | `base_floor/pjgrate2, common/caulk, common/nodraw` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 159 | `worldspawn` | 1600 | `base_trim/pewter_shiney, common/caulk` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 160 | `worldspawn` | 1610 | `base_trim/pewter_shiney, common/caulk` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 161 | `worldspawn` | 1620 | `base_trim/pewter_shiney, common/caulk, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 162 | `worldspawn` | 1630 | `base_trim/pewter_shiney, common/caulk, gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 163 | `worldspawn` | 1640 | `base_trim/pewter_shiney, common/caulk` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 164 | `worldspawn` | 1650 | `base_trim/pewter_shiney, common/caulk, gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 165 | `worldspawn` | 1660 | `base_trim/pewter_shiney, common/caulk` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 166 | `worldspawn` | 1670 | `base_floor/pjgrate2, common/caulk, common/nodraw` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 167 | `worldspawn` | 1680 | `base_trim/pewter_shiney, common/caulk` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 168 | `worldspawn` | 1690 | `base_trim/pewter_shiney, common/caulk` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 169 | `worldspawn` | 1700 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 170 | `worldspawn` | 1710 | `base_trim/pewter_shiney, common/caulk, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 171 | `worldspawn` | 1720 | `common/caulk, gothic_block/gkc15_big, gothic_ceiling/ceilingtechplain` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 172 | `worldspawn` | 1730 | `common/caulk, gothic_block/blocks11b, gothic_block/gkc15_big` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 173 | `worldspawn` | 1740 | `gothic_block/gkc15_big, gothic_ceiling/ceilingtechplain` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 174 | `worldspawn` | 1750 | `gothic_block/blocks11b, gothic_block/gkc15_big` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 175 | `worldspawn` | 1760 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 176 | `worldspawn` | 1770 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 177 | `worldspawn` | 1780 | `common/caulk, gothic_block/blocks11b, gothic_light/pentagram_light1_10k` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 178 | `worldspawn` | 1790 | `common/caulk, gothic_block/gkc15_big, gothic_block/gkc_large_right` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 179 | `worldspawn` | 1800 | `common/caulk, gothic_door/xian_tourneyarch_inside2, gothic_door/xian_tourneyarch_tall2b_pot` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 180 | `worldspawn` | 1810 | `common/caulk, gothic_door/xian_tourneyarch_inside2, gothic_door/xian_tourneyarch_tall2b_pot` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 181 | `worldspawn` | 1820 | `common/caulk, gothic_door/xian_tourneyarch_inside2, gothic_door/xian_tourneyarch_tall2b_pot` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 182 | `worldspawn` | 1830 | `common/caulk, gothic_door/xian_tourneyarch_tall2b_pot` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 183 | `worldspawn` | 1840 | `common/caulk, gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 184 | `worldspawn` | 1850 | `common/caulk, gothic_block/blocks11b, gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 185 | `worldspawn` | 1860 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 186 | `worldspawn` | 1870 | `gothic_block/blocks11b, gothic_door/xian_tourneyarch_tall2b_pot` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 187 | `worldspawn` | 1880 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 188 | `worldspawn` | 1890 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 189 | `worldspawn` | 1900 | `common/caulk, gothic_block/blocks11b, gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 190 | `worldspawn` | 1910 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 191 | `worldspawn` | 1920 | `common/caulk, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 192 | `worldspawn` | 1930 | `common/caulk, gothic_block/blocks11b, gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 193 | `worldspawn` | 1940 | `common/caulk, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 194 | `worldspawn` | 1950 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 195 | `worldspawn` | 1960 | `gothic_block/blocks11b, gothic_floor/q1metal7_98d_256x256, gothic_floor/xstepborder10` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 196 | `worldspawn` | 1970 | `gothic_floor/xstepborder10, gothic_floor/xstepborder12, gothic_floor/xstepborder8, gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 197 | `worldspawn` | 1980 | `gothic_floor/xstepborder10, gothic_floor/xstepborder12, gothic_floor/xstepborder8, gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 198 | `worldspawn` | 1990 | `gothic_floor/xstepborder10, gothic_floor/xstepborder12, gothic_floor/xstepborder8, gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 199 | `worldspawn` | 2000 | `gothic_floor/xstepborder10, gothic_floor/xstepborder12, gothic_floor/xstepborder8, gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 200 | `worldspawn` | 2010 | `gothic_floor/xstepborder10, gothic_floor/xstepborder12, gothic_floor/xstepborder8, gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 201 | `worldspawn` | 2020 | `gothic_floor/xstepborder10, gothic_floor/xstepborder12, gothic_floor/xstepborder8, gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 202 | `worldspawn` | 2030 | `gothic_floor/xstepborder10, gothic_floor/xstepborder12, gothic_floor/xstepborder8, gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 203 | `worldspawn` | 2040 | `gothic_floor/xstepborder10, gothic_floor/xstepborder12, gothic_floor/xstepborder8, gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 204 | `worldspawn` | 2050 | `gothic_floor/xstepborder10, gothic_floor/xstepborder12, gothic_floor/xstepborder8, gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 205 | `worldspawn` | 2060 | `gothic_floor/xstepborder10, gothic_floor/xstepborder12, gothic_floor/xstepborder8, gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 206 | `worldspawn` | 2070 | `gothic_floor/xstepborder10, gothic_floor/xstepborder12, gothic_floor/xstepborder8, gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 207 | `worldspawn` | 2080 | `gothic_floor/xstepborder10, gothic_floor/xstepborder12, gothic_floor/xstepborder8, gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 208 | `worldspawn` | 2090 | `gothic_floor/xstepborder10, gothic_floor/xstepborder12, gothic_floor/xstepborder8, gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 209 | `worldspawn` | 2100 | `gothic_floor/xstepborder10, gothic_floor/xstepborder12, gothic_floor/xstepborder8, gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 210 | `worldspawn` | 2110 | `gothic_floor/xstepborder10, gothic_floor/xstepborder12, gothic_floor/xstepborder8, gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 211 | `worldspawn` | 2120 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 212 | `worldspawn` | 2130 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 213 | `worldspawn` | 2140 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 214 | `worldspawn` | 2150 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 215 | `worldspawn` | 2160 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 216 | `worldspawn` | 2170 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 217 | `worldspawn` | 2180 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 218 | `worldspawn` | 2190 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 219 | `worldspawn` | 2200 | `gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 220 | `worldspawn` | 2210 | `gothic_light/ironcrosslt2_1000, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 221 | `worldspawn` | 2220 | `gothic_light/ironcrosslt2_1000, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 222 | `worldspawn` | 2230 | `common/caulk, gothic_block/blocks11b, gothic_light/pentagram_light1_10k` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 223 | `worldspawn` | 2240 | `common/caulk, gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 224 | `worldspawn` | 2250 | `gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 225 | `worldspawn` | 2260 | `common/caulk, gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 226 | `worldspawn` | 2270 | `common/caulk, gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 227 | `worldspawn` | 2280 | `base_light/ceil1_38_10k, common/caulk, gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 228 | `worldspawn` | 2290 | `common/caulk, gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 229 | `worldspawn` | 2300 | `gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 230 | `worldspawn` | 2310 | `base_light/ceil1_38_10k, gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 231 | `worldspawn` | 2320 | `base_light/ceil1_38_10k, gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 232 | `worldspawn` | 2330 | `gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 233 | `worldspawn` | 2340 | `gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 234 | `worldspawn` | 2350 | `base_light/ceil1_38_10k, gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 235 | `worldspawn` | 2360 | `base_light/ceil1_38_10k, gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 236 | `worldspawn` | 2370 | `gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 237 | `worldspawn` | 2380 | `base_light/ceil1_38_10k, gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 238 | `worldspawn` | 2390 | `gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 239 | `worldspawn` | 2400 | `gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 240 | `worldspawn` | 2410 | `gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 241 | `worldspawn` | 2420 | `base_light/ceil1_38_10k, gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 242 | `worldspawn` | 2430 | `base_light/ceil1_38_10k, gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 243 | `worldspawn` | 2440 | `gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 244 | `worldspawn` | 2450 | `gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 245 | `worldspawn` | 2460 | `base_light/ceil1_38_10k, gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 246 | `worldspawn` | 2470 | `base_light/ceil1_38_10k, gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 247 | `worldspawn` | 2480 | `gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 248 | `worldspawn` | 2490 | `gothic_block/blocks18b, gothic_ceiling/ceilingtechplain, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 249 | `worldspawn` | 2500 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 250 | `worldspawn` | 2510 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 251 | `worldspawn` | 2520 | `gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 252 | `worldspawn` | 2530 | `skies/overkill` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 253 | `worldspawn` | 2540 | `skies/overkill` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 254 | `worldspawn` | 2550 | `common/caulk, gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 255 | `worldspawn` | 2560 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 256 | `worldspawn` | 2570 | `gothic_block/blocks18b, gothic_ceiling/ceilingtechplain, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 257 | `worldspawn` | 2580 | `skies/overkill` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 258 | `worldspawn` | 2590 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 259 | `worldspawn` | 2600 | `gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 260 | `worldspawn` | 2610 | `gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 261 | `worldspawn` | 2620 | `gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 262 | `worldspawn` | 2630 | `skies/overkill` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 263 | `worldspawn` | 2640 | `skies/overkill` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 264 | `worldspawn` | 2650 | `skies/overkill` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 265 | `worldspawn` | 2660 | `common/caulk, gothic_block/blocks11b, gothic_light/pentagram_light1_10k` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 266 | `worldspawn` | 2670 | `gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 267 | `worldspawn` | 2680 | `base_support/cable` | `omit:invalid_geometry` | `allow` | `omitted` | omitted: invalid_geometry |
| 0 | 268 | `worldspawn` | 2692 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 269 | `worldspawn` | 2706 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 270 | `worldspawn` | 2716 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 271 | `worldspawn` | 2726 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 272 | `worldspawn` | 2736 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 273 | `worldspawn` | 2746 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 274 | `worldspawn` | 2756 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 275 | `worldspawn` | 2766 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 276 | `worldspawn` | 2776 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 277 | `worldspawn` | 2786 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 278 | `worldspawn` | 2796 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 279 | `worldspawn` | 2806 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 280 | `worldspawn` | 2816 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 281 | `worldspawn` | 2826 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 282 | `worldspawn` | 2840 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 283 | `worldspawn` | 2850 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 284 | `worldspawn` | 2860 | `gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 285 | `worldspawn` | 2870 | `gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 286 | `worldspawn` | 2880 | `gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 287 | `worldspawn` | 2890 | `gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 288 | `worldspawn` | 2900 | `gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 289 | `worldspawn` | 2910 | `gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 290 | `worldspawn` | 2920 | `gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 291 | `worldspawn` | 2930 | `common/caulk, gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 292 | `worldspawn` | 2940 | `gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 293 | `worldspawn` | 2950 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 294 | `worldspawn` | 2960 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 295 | `worldspawn` | 2970 | `gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 296 | `worldspawn` | 2980 | `gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 297 | `worldspawn` | 2990 | `gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 298 | `worldspawn` | 3000 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 299 | `worldspawn` | 3010 | `gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 300 | `worldspawn` | 3020 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 301 | `worldspawn` | 3030 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 302 | `worldspawn` | 3040 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 303 | `worldspawn` | 3050 | `base_trim/pewter_shiney` | `omit:invalid_geometry` | `allow` | `omitted` | omitted: invalid_geometry |
| 0 | 304 | `worldspawn` | 3060 | `base_trim/pewter_shiney` | `omit:invalid_geometry` | `allow` | `omitted` | omitted: invalid_geometry |
| 0 | 305 | `worldspawn` | 3074 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 306 | `worldspawn` | 3084 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 307 | `worldspawn` | 3094 | `base_support/cable` | `omit:invalid_geometry` | `allow` | `omitted` | omitted: invalid_geometry |
| 0 | 308 | `worldspawn` | 3106 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 309 | `worldspawn` | 3120 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 310 | `worldspawn` | 3130 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 311 | `worldspawn` | 3140 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 312 | `worldspawn` | 3150 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 313 | `worldspawn` | 3160 | `common/caulk, gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 314 | `worldspawn` | 3170 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 315 | `worldspawn` | 3180 | `gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 316 | `worldspawn` | 3190 | `gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 317 | `worldspawn` | 3200 | `gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 318 | `worldspawn` | 3210 | `gothic_block/gkc15_big` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 319 | `worldspawn` | 3220 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 320 | `worldspawn` | 3230 | `common/caulk, gothic_block/blocks11b, gothic_light/pentagram_light1_10k` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 321 | `worldspawn` | 3240 | `common/caulk, gothic_block/blocks11b, gothic_light/pentagram_light1_10k` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 322 | `worldspawn` | 3250 | `gothic_light/ironcrosslt2_1000, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 323 | `worldspawn` | 3260 | `gothic_trim/pitted_rust, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 324 | `worldspawn` | 3270 | `common/caulk, gothic_door/xian_tourneyarch_inside2, gothic_door/xian_tourneyarch_tall2b_pot` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 325 | `worldspawn` | 3280 | `common/caulk, gothic_door/xian_tourneyarch_inside2, gothic_door/xian_tourneyarch_tall2b_pot` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 326 | `worldspawn` | 3290 | `common/caulk, gothic_door/xian_tourneyarch_tall2b_pot` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 327 | `worldspawn` | 3300 | `common/caulk, gothic_block/blocks11b, gothic_floor/q1metal7_98d_256x256, gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 328 | `worldspawn` | 3310 | `gothic_block/blocks11b, gothic_door/xian_tourneyarch_inside2, gothic_door/xian_tourneyarch_tall2b_pot` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 329 | `worldspawn` | 3320 | `common/caulk, gothic_trim/pitted_rust, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 330 | `worldspawn` | 3330 | `common/caulk, gothic_block/gkc15_big` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 331 | `worldspawn` | 3340 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 332 | `worldspawn` | 3350 | `common/caulk, gothic_block/blocks11b, gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 333 | `worldspawn` | 3360 | `common/caulk, gothic_block/blocks11b, gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 334 | `worldspawn` | 3370 | `common/caulk, gothic_block/blocks11b, gothic_floor/q1metal7_98d_256x256, gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 335 | `worldspawn` | 3380 | `common/caulk, gothic_door/xian_tourneyarch_tall2b_pot` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 336 | `worldspawn` | 3390 | `common/caulk, gothic_door/xian_tourneyarch_inside2, gothic_door/xian_tourneyarch_tall2b_pot` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 337 | `worldspawn` | 3400 | `common/caulk, gothic_door/xian_tourneyarch_inside2, gothic_door/xian_tourneyarch_tall2b_pot` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 338 | `worldspawn` | 3410 | `common/caulk, gothic_door/xian_tourneyarch_inside2, gothic_door/xian_tourneyarch_tall2b_pot` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 339 | `worldspawn` | 3420 | `gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 340 | `worldspawn` | 3430 | `base_light/ceil1_38_10k, gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 341 | `worldspawn` | 3440 | `base_light/ceil1_38_10k, gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 342 | `worldspawn` | 3450 | `gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 343 | `worldspawn` | 3460 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 344 | `worldspawn` | 3470 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 345 | `worldspawn` | 3480 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 346 | `worldspawn` | 3490 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 347 | `worldspawn` | 3500 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 348 | `worldspawn` | 3510 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 349 | `worldspawn` | 3520 | `gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 350 | `worldspawn` | 3530 | `base_light/ceil1_38_10k, gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 351 | `worldspawn` | 3540 | `base_light/ceil1_38_10k, gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 352 | `worldspawn` | 3550 | `gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 353 | `worldspawn` | 3560 | `gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 354 | `worldspawn` | 3570 | `gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 355 | `worldspawn` | 3580 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 356 | `worldspawn` | 3590 | `gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 357 | `worldspawn` | 3600 | `gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 358 | `worldspawn` | 3610 | `common/caulk, gothic_block/gkc15_big` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 359 | `worldspawn` | 3620 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 360 | `worldspawn` | 3630 | `gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 361 | `worldspawn` | 3640 | `skies/overkill` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 362 | `worldspawn` | 3650 | `skies/overkill` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 363 | `worldspawn` | 3660 | `gothic_block/blocks11b, gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 364 | `worldspawn` | 3670 | `gothic_block/blocks11b, gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 365 | `worldspawn` | 3680 | `gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 366 | `worldspawn` | 3690 | `gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 367 | `worldspawn` | 3700 | `gothic_block/gkc15_big` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 368 | `worldspawn` | 3710 | `gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 369 | `worldspawn` | 3720 | `gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 370 | `worldspawn` | 3730 | `gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 371 | `worldspawn` | 3740 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 372 | `worldspawn` | 3750 | `common/caulk, gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 373 | `worldspawn` | 3760 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 374 | `worldspawn` | 3770 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 375 | `worldspawn` | 3780 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 376 | `worldspawn` | 3790 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 377 | `worldspawn` | 3800 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 378 | `worldspawn` | 3814 | `base_support/cable` | `omit:invalid_geometry` | `allow` | `omitted` | omitted: invalid_geometry |
| 0 | 379 | `worldspawn` | 3826 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 380 | `worldspawn` | 3836 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 381 | `worldspawn` | 3846 | `base_trim/pewter_shiney` | `omit:invalid_geometry` | `allow` | `omitted` | omitted: invalid_geometry |
| 0 | 382 | `worldspawn` | 3860 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 383 | `worldspawn` | 3870 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 384 | `worldspawn` | 3880 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 385 | `worldspawn` | 3890 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 386 | `worldspawn` | 3900 | `gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 387 | `worldspawn` | 3910 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 388 | `worldspawn` | 3920 | `gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 389 | `worldspawn` | 3930 | `gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 390 | `worldspawn` | 3940 | `gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 391 | `worldspawn` | 3950 | `base_trim/pewter_shiney` | `omit:invalid_geometry` | `allow` | `omitted` | omitted: invalid_geometry |
| 0 | 392 | `worldspawn` | 3960 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 393 | `worldspawn` | 3970 | `gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 394 | `worldspawn` | 3980 | `common/caulk, gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 395 | `worldspawn` | 3990 | `gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 396 | `worldspawn` | 4000 | `gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 397 | `worldspawn` | 4010 | `gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 398 | `worldspawn` | 4020 | `gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 399 | `worldspawn` | 4030 | `gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 400 | `worldspawn` | 4040 | `gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 401 | `worldspawn` | 4050 | `gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 402 | `worldspawn` | 4060 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 403 | `worldspawn` | 4070 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 404 | `worldspawn` | 4080 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 405 | `worldspawn` | 4094 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 406 | `worldspawn` | 4104 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 407 | `worldspawn` | 4114 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 408 | `worldspawn` | 4124 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 409 | `worldspawn` | 4134 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 410 | `worldspawn` | 4144 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 411 | `worldspawn` | 4154 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 412 | `worldspawn` | 4164 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 413 | `worldspawn` | 4174 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 414 | `worldspawn` | 4184 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 415 | `worldspawn` | 4194 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 416 | `worldspawn` | 4204 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 417 | `worldspawn` | 4214 | `base_trim/pewter_shiney` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 418 | `worldspawn` | 4228 | `base_support/cable` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 419 | `worldspawn` | 4240 | `gothic_block/blocks11b, gothic_block/gkc15_big` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 420 | `worldspawn` | 4250 | `gothic_trim/metalsupsolid, gothic_trim/pitted_rust3_black, sfx/bounce_xq1metalbig` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 421 | `worldspawn` | 4260 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 422 | `worldspawn` | 4270 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 423 | `worldspawn` | 4280 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 424 | `worldspawn` | 4290 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 425 | `worldspawn` | 4300 | `gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 426 | `worldspawn` | 4310 | `common/caulk, gothic_block/blocks11b, gothic_light/pentagram_light1_10k` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 427 | `worldspawn` | 4320 | `gothic_light/ironcrosslt2_1000, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 428 | `worldspawn` | 4330 | `gothic_light/ironcrosslt2_1000, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 429 | `worldspawn` | 4340 | `gothic_light/ironcrosslt2_1000, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 430 | `worldspawn` | 4350 | `common/caulk, gothic_block/blocks11b, gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 431 | `worldspawn` | 4360 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 432 | `worldspawn` | 4370 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 433 | `worldspawn` | 4380 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 434 | `worldspawn` | 4390 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 435 | `worldspawn` | 4400 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 436 | `worldspawn` | 4410 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 437 | `worldspawn` | 4420 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 438 | `worldspawn` | 4430 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 439 | `worldspawn` | 4440 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 440 | `worldspawn` | 4450 | `common/caulk, gothic_block/blocks11b, gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 441 | `worldspawn` | 4460 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 442 | `worldspawn` | 4470 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 443 | `worldspawn` | 4480 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 444 | `worldspawn` | 4490 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 445 | `worldspawn` | 4500 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 446 | `worldspawn` | 4510 | `gothic_ceiling/ceilingtechplain` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 447 | `worldspawn` | 4520 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 448 | `worldspawn` | 4530 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 449 | `worldspawn` | 4540 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 450 | `worldspawn` | 4550 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 451 | `worldspawn` | 4560 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 452 | `worldspawn` | 4570 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 453 | `worldspawn` | 4580 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 454 | `worldspawn` | 4590 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 455 | `worldspawn` | 4600 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 456 | `worldspawn` | 4610 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 457 | `worldspawn` | 4620 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 458 | `worldspawn` | 4630 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 459 | `worldspawn` | 4640 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 460 | `worldspawn` | 4650 | `gothic_trim/newskull` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 461 | `worldspawn` | 4660 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 462 | `worldspawn` | 4670 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 463 | `worldspawn` | 4680 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 464 | `worldspawn` | 4690 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 465 | `worldspawn` | 4700 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 466 | `worldspawn` | 4710 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 467 | `worldspawn` | 4720 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 468 | `worldspawn` | 4730 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 469 | `worldspawn` | 4740 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 470 | `worldspawn` | 4750 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 471 | `worldspawn` | 4760 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 472 | `worldspawn` | 4770 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 473 | `worldspawn` | 4780 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 474 | `worldspawn` | 4790 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 475 | `worldspawn` | 4800 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 476 | `worldspawn` | 4810 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 477 | `worldspawn` | 4820 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 478 | `worldspawn` | 4830 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 479 | `worldspawn` | 4840 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 480 | `worldspawn` | 4850 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 481 | `worldspawn` | 4860 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 482 | `worldspawn` | 4870 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 483 | `worldspawn` | 4880 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 484 | `worldspawn` | 4890 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 485 | `worldspawn` | 4900 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 486 | `worldspawn` | 4910 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 487 | `worldspawn` | 4920 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 488 | `worldspawn` | 4930 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 489 | `worldspawn` | 4940 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 490 | `worldspawn` | 4950 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 491 | `worldspawn` | 4960 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 492 | `worldspawn` | 4970 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 493 | `worldspawn` | 4980 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 494 | `worldspawn` | 4990 | `common/caulk, gothic_trim/newskull` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 495 | `worldspawn` | 5000 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 496 | `worldspawn` | 5010 | `gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 497 | `worldspawn` | 5020 | `gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 498 | `worldspawn` | 5030 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 499 | `worldspawn` | 5040 | `gothic_block/blocks11b, gothic_floor/q1metal7_98d_256x256, gothic_floor/xstepborder10, gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 500 | `worldspawn` | 5050 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 501 | `worldspawn` | 5060 | `gothic_floor/xstepborder10, gothic_floor/xstepborder12, gothic_floor/xstepborder8, gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 502 | `worldspawn` | 5070 | `gothic_floor/xstepborder10, gothic_floor/xstepborder12, gothic_floor/xstepborder8, gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 503 | `worldspawn` | 5080 | `gothic_floor/xstepborder10, gothic_floor/xstepborder12, gothic_floor/xstepborder8, gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 504 | `worldspawn` | 5090 | `gothic_floor/xstepborder10, gothic_floor/xstepborder12, gothic_floor/xstepborder8, gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 505 | `worldspawn` | 5100 | `gothic_floor/xstepborder10, gothic_floor/xstepborder12, gothic_floor/xstepborder8, gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 506 | `worldspawn` | 5110 | `gothic_floor/xstepborder10, gothic_floor/xstepborder12, gothic_floor/xstepborder8, gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 507 | `worldspawn` | 5120 | `gothic_floor/xstepborder10, gothic_floor/xstepborder12, gothic_floor/xstepborder8, gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 508 | `worldspawn` | 5130 | `gothic_floor/xstepborder10, gothic_floor/xstepborder12, gothic_floor/xstepborder8, gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 509 | `worldspawn` | 5140 | `gothic_floor/xstepborder10, gothic_floor/xstepborder12, gothic_floor/xstepborder8, gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 510 | `worldspawn` | 5150 | `gothic_floor/xstepborder10, gothic_floor/xstepborder12, gothic_floor/xstepborder8, gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 511 | `worldspawn` | 5160 | `gothic_floor/xstepborder10, gothic_floor/xstepborder12, gothic_floor/xstepborder8, gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 512 | `worldspawn` | 5170 | `gothic_floor/xstepborder10, gothic_floor/xstepborder12, gothic_floor/xstepborder8, gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 513 | `worldspawn` | 5180 | `gothic_floor/xstepborder10, gothic_floor/xstepborder12, gothic_floor/xstepborder8, gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 514 | `worldspawn` | 5190 | `gothic_floor/xstepborder10, gothic_floor/xstepborder12, gothic_floor/xstepborder8, gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 515 | `worldspawn` | 5200 | `gothic_floor/xstepborder10, gothic_floor/xstepborder12, gothic_floor/xstepborder8, gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 516 | `worldspawn` | 5210 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 517 | `worldspawn` | 5220 | `gothic_block/blocks11b, gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 518 | `worldspawn` | 5230 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 519 | `worldspawn` | 5240 | `gothic_block/gkc15_big` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 520 | `worldspawn` | 5250 | `gothic_block/blocks11b, gothic_light/pentagram_light1_10k` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 521 | `worldspawn` | 5260 | `common/caulk, gothic_block/blocks11b, gothic_light/pentagram_light1_10k` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 522 | `worldspawn` | 5270 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 523 | `worldspawn` | 5280 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 524 | `worldspawn` | 5290 | `gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 525 | `worldspawn` | 5300 | `gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 526 | `worldspawn` | 5310 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 527 | `worldspawn` | 5320 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 528 | `worldspawn` | 5330 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 529 | `worldspawn` | 5340 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 530 | `worldspawn` | 5350 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 531 | `worldspawn` | 5360 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 532 | `worldspawn` | 5370 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 533 | `worldspawn` | 5380 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 534 | `worldspawn` | 5390 | `gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 535 | `worldspawn` | 5400 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 536 | `worldspawn` | 5410 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 537 | `worldspawn` | 5420 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 538 | `worldspawn` | 5430 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 539 | `worldspawn` | 5440 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 540 | `worldspawn` | 5450 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 541 | `worldspawn` | 5460 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 542 | `worldspawn` | 5470 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 543 | `worldspawn` | 5480 | `gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 544 | `worldspawn` | 5490 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 545 | `worldspawn` | 5500 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 546 | `worldspawn` | 5510 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 547 | `worldspawn` | 5520 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 548 | `worldspawn` | 5530 | `gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 549 | `worldspawn` | 5540 | `gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 550 | `worldspawn` | 5550 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 551 | `worldspawn` | 5560 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 552 | `worldspawn` | 5570 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 553 | `worldspawn` | 5580 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 554 | `worldspawn` | 5590 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 555 | `worldspawn` | 5600 | `gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 556 | `worldspawn` | 5610 | `gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 557 | `worldspawn` | 5620 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 558 | `worldspawn` | 5630 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 559 | `worldspawn` | 5640 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 560 | `worldspawn` | 5650 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 561 | `worldspawn` | 5660 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 562 | `worldspawn` | 5670 | `gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 563 | `worldspawn` | 5680 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 564 | `worldspawn` | 5690 | `common/caulk, gothic_block/blocks11b, gothic_light/pentagram_light1_10k` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 565 | `worldspawn` | 5700 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 566 | `worldspawn` | 5710 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 567 | `worldspawn` | 5720 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 568 | `worldspawn` | 5730 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 569 | `worldspawn` | 5740 | `common/caulk, gothic_block/blocks11b, gothic_light/pentagram_light1_10k` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 570 | `worldspawn` | 5750 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 571 | `worldspawn` | 5760 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 572 | `worldspawn` | 5770 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 573 | `worldspawn` | 5780 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 574 | `worldspawn` | 5790 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 575 | `worldspawn` | 5800 | `gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 576 | `worldspawn` | 5810 | `gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 577 | `worldspawn` | 5820 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 578 | `worldspawn` | 5830 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 579 | `worldspawn` | 5840 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 580 | `worldspawn` | 5850 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 581 | `worldspawn` | 5860 | `gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 582 | `worldspawn` | 5870 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 583 | `worldspawn` | 5880 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 584 | `worldspawn` | 5890 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 585 | `worldspawn` | 5900 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 586 | `worldspawn` | 5910 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 587 | `worldspawn` | 5920 | `gothic_block/gkc15_big` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 588 | `worldspawn` | 5930 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 589 | `worldspawn` | 5940 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 590 | `worldspawn` | 5950 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 591 | `worldspawn` | 5960 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 592 | `worldspawn` | 5970 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 593 | `worldspawn` | 5980 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 594 | `worldspawn` | 5990 | `gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 595 | `worldspawn` | 6000 | `gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 596 | `worldspawn` | 6010 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 597 | `worldspawn` | 6020 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 598 | `worldspawn` | 6030 | `common/caulk, gothic_block/blocks11b, gothic_light/pentagram_light1_10k` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 599 | `worldspawn` | 6040 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 600 | `worldspawn` | 6050 | `gothic_block/blocks11b, gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 601 | `worldspawn` | 6060 | `gothic_block/blocks11b, gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 602 | `worldspawn` | 6070 | `gothic_block/blocks11b, gothic_block/gkc15_big` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 603 | `worldspawn` | 6080 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 604 | `worldspawn` | 6090 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 605 | `worldspawn` | 6100 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 606 | `worldspawn` | 6110 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 607 | `worldspawn` | 6120 | `gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 608 | `worldspawn` | 6130 | `gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 609 | `worldspawn` | 6140 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 610 | `worldspawn` | 6150 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 611 | `worldspawn` | 6160 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 612 | `worldspawn` | 6170 | `gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 613 | `worldspawn` | 6180 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 614 | `worldspawn` | 6190 | `common/caulk, gothic_block/blocks11b, gothic_light/pentagram_light1_10k` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 615 | `worldspawn` | 6200 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 616 | `worldspawn` | 6210 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 617 | `worldspawn` | 6220 | `gothic_block/blocks11b, gothic_block/gkc15_big` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 618 | `worldspawn` | 6230 | `gothic_block/blocks11b, gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 619 | `worldspawn` | 6240 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 620 | `worldspawn` | 6250 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 621 | `worldspawn` | 6260 | `gothic_ceiling/ceilingtechplain` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 622 | `worldspawn` | 6270 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 623 | `worldspawn` | 6280 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 624 | `worldspawn` | 6290 | `gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 625 | `worldspawn` | 6300 | `gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 626 | `worldspawn` | 6310 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 627 | `worldspawn` | 6320 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 628 | `worldspawn` | 6330 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 629 | `worldspawn` | 6340 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 630 | `worldspawn` | 6350 | `common/caulk, gothic_block/blocks11b, gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 631 | `worldspawn` | 6360 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 632 | `worldspawn` | 6370 | `common/caulk, gothic_block/blocks11b, gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 633 | `worldspawn` | 6380 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 634 | `worldspawn` | 6390 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 635 | `worldspawn` | 6400 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 636 | `worldspawn` | 6410 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 637 | `worldspawn` | 6420 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 638 | `worldspawn` | 6430 | `gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 639 | `worldspawn` | 6440 | `gothic_block/blocks11b, gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 640 | `worldspawn` | 6450 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 641 | `worldspawn` | 6460 | `gothic_light/ironcrosslt2_1000, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 642 | `worldspawn` | 6470 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 643 | `worldspawn` | 6480 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 644 | `worldspawn` | 6490 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 645 | `worldspawn` | 6500 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 646 | `worldspawn` | 6510 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 647 | `worldspawn` | 6520 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 648 | `worldspawn` | 6530 | `gothic_light/ironcrosslt2_1000, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 649 | `worldspawn` | 6540 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 650 | `worldspawn` | 6550 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 651 | `worldspawn` | 6560 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 652 | `worldspawn` | 6570 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 653 | `worldspawn` | 6580 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 654 | `worldspawn` | 6590 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 655 | `worldspawn` | 6600 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 656 | `worldspawn` | 6610 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 657 | `worldspawn` | 6620 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 658 | `worldspawn` | 6630 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 659 | `worldspawn` | 6640 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 660 | `worldspawn` | 6650 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 661 | `worldspawn` | 6660 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 662 | `worldspawn` | 6670 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 663 | `worldspawn` | 6680 | `gothic_trim/newskull` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 664 | `worldspawn` | 6690 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 665 | `worldspawn` | 6700 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 666 | `worldspawn` | 6710 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 667 | `worldspawn` | 6720 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 668 | `worldspawn` | 6730 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 669 | `worldspawn` | 6740 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 670 | `worldspawn` | 6750 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 671 | `worldspawn` | 6760 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 672 | `worldspawn` | 6770 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 673 | `worldspawn` | 6780 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 674 | `worldspawn` | 6790 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 675 | `worldspawn` | 6800 | `gothic_trim/newskull` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 676 | `worldspawn` | 6810 | `gothic_block/gkc15_big` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 677 | `worldspawn` | 6820 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 678 | `worldspawn` | 6830 | `gothic_block/blocks11b, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 679 | `worldspawn` | 6840 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 680 | `worldspawn` | 6850 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 681 | `worldspawn` | 6860 | `common/caulk, gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 682 | `worldspawn` | 6870 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 683 | `worldspawn` | 6880 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 684 | `worldspawn` | 6890 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 685 | `worldspawn` | 6900 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 686 | `worldspawn` | 6910 | `common/caulk, gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 687 | `worldspawn` | 6920 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 688 | `worldspawn` | 6930 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 689 | `worldspawn` | 6940 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 690 | `worldspawn` | 6950 | `common/caulk, gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 691 | `worldspawn` | 6960 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 692 | `worldspawn` | 6970 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 693 | `worldspawn` | 6980 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 694 | `worldspawn` | 6990 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 695 | `worldspawn` | 7000 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 696 | `worldspawn` | 7010 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 697 | `worldspawn` | 7020 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 698 | `worldspawn` | 7030 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 699 | `worldspawn` | 7040 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 700 | `worldspawn` | 7050 | `common/caulk, gothic_wall/xiantourneywall_c1_pot` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 701 | `worldspawn` | 7060 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 702 | `worldspawn` | 7070 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 703 | `worldspawn` | 7080 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 704 | `worldspawn` | 7090 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 705 | `worldspawn` | 7100 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 706 | `worldspawn` | 7110 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 707 | `worldspawn` | 7120 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 708 | `worldspawn` | 7130 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 709 | `worldspawn` | 7140 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 710 | `worldspawn` | 7150 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 711 | `worldspawn` | 7160 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 712 | `worldspawn` | 7170 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 713 | `worldspawn` | 7180 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 714 | `worldspawn` | 7190 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 715 | `worldspawn` | 7200 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 716 | `worldspawn` | 7210 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 717 | `worldspawn` | 7220 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 718 | `worldspawn` | 7230 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 719 | `worldspawn` | 7240 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 720 | `worldspawn` | 7250 | `common/caulk, gothic_floor/metalbridge06, gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 721 | `worldspawn` | 7260 | `common/caulk, gothic_floor/metalbridge06, gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 722 | `worldspawn` | 7270 | `common/caulk, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 723 | `worldspawn` | 7280 | `gothic_ceiling/ceilingtechplain, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 724 | `worldspawn` | 7290 | `gothic_ceiling/ceilingtechplain, gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 725 | `worldspawn` | 7300 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 726 | `worldspawn` | 7310 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 727 | `worldspawn` | 7320 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 728 | `worldspawn` | 7330 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 729 | `worldspawn` | 7340 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 730 | `worldspawn` | 7350 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 731 | `worldspawn` | 7360 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 732 | `worldspawn` | 7370 | `common/caulk, gothic_block/blocks11b, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 733 | `worldspawn` | 7380 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 734 | `worldspawn` | 7390 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 735 | `worldspawn` | 7400 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 736 | `worldspawn` | 7410 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 737 | `worldspawn` | 7420 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 738 | `worldspawn` | 7430 | `common/caulk, gothic_wall/xiantourneywall_c1_pot` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 739 | `worldspawn` | 7440 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 740 | `worldspawn` | 7450 | `common/caulk, gothic_wall/xiantourneywall_c1_pot` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 741 | `worldspawn` | 7460 | `common/caulk, gothic_block/blocks11b, gothic_door/archxiandm1dblack_pot` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 742 | `worldspawn` | 7470 | `common/caulk, gothic_block/blocks11b, gothic_door/archxiandm1dblack_pot, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 743 | `worldspawn` | 7480 | `common/caulk, gothic_door/archxiandm1dblack_pot, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 744 | `worldspawn` | 7490 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 745 | `worldspawn` | 7500 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 746 | `worldspawn` | 7510 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 747 | `worldspawn` | 7520 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 748 | `worldspawn` | 7530 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 749 | `worldspawn` | 7540 | `stone/pjrock21` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 750 | `worldspawn` | 7550 | `gothic_floor/largerblock3b2, gothic_floor/metalbridge06` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 751 | `worldspawn` | 7560 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 752 | `worldspawn` | 7570 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 753 | `worldspawn` | 7580 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 754 | `worldspawn` | 7590 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 755 | `worldspawn` | 7600 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 756 | `worldspawn` | 7610 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 757 | `worldspawn` | 7620 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 758 | `worldspawn` | 7630 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 759 | `worldspawn` | 7640 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 760 | `worldspawn` | 7650 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 761 | `worldspawn` | 7660 | `common/caulk, sfx/bounce_xq1metalbig` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 762 | `worldspawn` | 7670 | `stone/pjrock21` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 763 | `worldspawn` | 7680 | `sfx/metalbridge06_bounce` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 764 | `worldspawn` | 7690 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 765 | `worldspawn` | 7700 | `gothic_floor/largerblock3b2, gothic_floor/metalbridge06` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 766 | `worldspawn` | 7710 | `gothic_floor/largerblock3b2, gothic_floor/metalbridge06` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 767 | `worldspawn` | 7720 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 768 | `worldspawn` | 7730 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 769 | `worldspawn` | 7740 | `gothic_ceiling/ceilingtechplain, gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 770 | `worldspawn` | 7750 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 771 | `worldspawn` | 7760 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 772 | `worldspawn` | 7770 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 773 | `worldspawn` | 7780 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 774 | `worldspawn` | 7790 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 775 | `worldspawn` | 7800 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 776 | `worldspawn` | 7810 | `common/caulk, gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 777 | `worldspawn` | 7820 | `common/caulk, gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 778 | `worldspawn` | 7830 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 779 | `worldspawn` | 7840 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 780 | `worldspawn` | 7850 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 781 | `worldspawn` | 7860 | `common/caulk, gothic_floor/metalbridge06, gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 782 | `worldspawn` | 7870 | `gothic_floor/metalbridge06, gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 783 | `worldspawn` | 7880 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 784 | `worldspawn` | 7890 | `gothic_trim/pitted_rust, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 785 | `worldspawn` | 7900 | `common/caulk, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 786 | `worldspawn` | 7910 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 787 | `worldspawn` | 7920 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 788 | `worldspawn` | 7930 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 789 | `worldspawn` | 7940 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 790 | `worldspawn` | 7950 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 791 | `worldspawn` | 7960 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 792 | `worldspawn` | 7970 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 793 | `worldspawn` | 7980 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 794 | `worldspawn` | 7990 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 795 | `worldspawn` | 8000 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 796 | `worldspawn` | 8010 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 797 | `worldspawn` | 8020 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 798 | `worldspawn` | 8030 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 799 | `worldspawn` | 8040 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 800 | `worldspawn` | 8050 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 801 | `worldspawn` | 8060 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 802 | `worldspawn` | 8070 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 803 | `worldspawn` | 8080 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 804 | `worldspawn` | 8090 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 805 | `worldspawn` | 8100 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 806 | `worldspawn` | 8110 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 807 | `worldspawn` | 8120 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 808 | `worldspawn` | 8130 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 809 | `worldspawn` | 8140 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 810 | `worldspawn` | 8150 | `common/caulk, gothic_trim/pitted_rust, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 811 | `worldspawn` | 8160 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 812 | `worldspawn` | 8170 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 813 | `worldspawn` | 8180 | `gothic_trim/pitted_rust, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 814 | `worldspawn` | 8190 | `gothic_trim/pitted_rust, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 815 | `worldspawn` | 8200 | `gothic_block/gkc10, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 816 | `worldspawn` | 8210 | `gothic_trim/pitted_rust, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 817 | `worldspawn` | 8220 | `common/caulk, gothic_trim/pitted_rust, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 818 | `worldspawn` | 8230 | `gothic_block/gkc10, gothic_trim/pitted_rust, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 819 | `worldspawn` | 8240 | `gothic_block/gkc10, gothic_trim/pitted_rust, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 820 | `worldspawn` | 8250 | `common/caulk, gothic_trim/km_arena1tower7, gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 821 | `worldspawn` | 8260 | `gothic_trim/km_arena1tower4, gothic_trim/km_arena1tower7` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 822 | `worldspawn` | 8270 | `gothic_trim/km_arena1tower4, gothic_trim/km_arena1tower7` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 823 | `worldspawn` | 8280 | `gothic_trim/km_arena1tower7, gothic_trim/metalsupsolid, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 824 | `worldspawn` | 8290 | `gothic_trim/pitted_rust, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 825 | `worldspawn` | 8300 | `gothic_trim/pitted_rust, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 826 | `worldspawn` | 8310 | `common/caulk, gothic_block/blocks18c_3, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 827 | `worldspawn` | 8320 | `common/caulk, gothic_trim/pitted_rust, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 828 | `worldspawn` | 8330 | `common/caulk, gothic_trim/pitted_rust, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 829 | `worldspawn` | 8340 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 830 | `worldspawn` | 8350 | `common/caulk, gothic_block/blocks18c_3, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 831 | `worldspawn` | 8360 | `gothic_trim/pitted_rust, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 832 | `worldspawn` | 8370 | `gothic_block/gkc10` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 833 | `worldspawn` | 8380 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 834 | `worldspawn` | 8390 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 835 | `worldspawn` | 8400 | `gothic_block/gkc10, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 836 | `worldspawn` | 8410 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 837 | `worldspawn` | 8420 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 838 | `worldspawn` | 8430 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 839 | `worldspawn` | 8440 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 840 | `worldspawn` | 8450 | `gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 841 | `worldspawn` | 8460 | `gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 842 | `worldspawn` | 8470 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 843 | `worldspawn` | 8480 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 844 | `worldspawn` | 8490 | `gothic_block/gkc10` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 845 | `worldspawn` | 8500 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 846 | `worldspawn` | 8510 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 847 | `worldspawn` | 8520 | `gothic_block/gkc10` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 848 | `worldspawn` | 8530 | `gothic_block/gkc10` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 849 | `worldspawn` | 8540 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 850 | `worldspawn` | 8550 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 851 | `worldspawn` | 8560 | `gothic_block/gkc10` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 852 | `worldspawn` | 8570 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 853 | `worldspawn` | 8580 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 854 | `worldspawn` | 8590 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 855 | `worldspawn` | 8600 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 856 | `worldspawn` | 8610 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 857 | `worldspawn` | 8620 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 858 | `worldspawn` | 8630 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 859 | `worldspawn` | 8640 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 860 | `worldspawn` | 8650 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 861 | `worldspawn` | 8660 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 862 | `worldspawn` | 8670 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 863 | `worldspawn` | 8680 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 864 | `worldspawn` | 8690 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 865 | `worldspawn` | 8700 | `common/caulk, gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 866 | `worldspawn` | 8710 | `common/caulk, gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 867 | `worldspawn` | 8720 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 868 | `worldspawn` | 8730 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 869 | `worldspawn` | 8740 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 870 | `worldspawn` | 8750 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 871 | `worldspawn` | 8760 | `common/caulk, gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 872 | `worldspawn` | 8770 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 873 | `worldspawn` | 8780 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 874 | `worldspawn` | 8790 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 875 | `worldspawn` | 8800 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 876 | `worldspawn` | 8810 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 877 | `worldspawn` | 8820 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 878 | `worldspawn` | 8830 | `common/caulk, gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 879 | `worldspawn` | 8840 | `common/caulk, gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 880 | `worldspawn` | 8850 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 881 | `worldspawn` | 8860 | `common/caulk, gothic_wall/dm5_archifin_pot` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 882 | `worldspawn` | 8870 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 883 | `worldspawn` | 8880 | `gothic_block/blocks11b, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 884 | `worldspawn` | 8890 | `common/caulk, gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 885 | `worldspawn` | 8900 | `sfx/bounce_xq1metalbig` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 886 | `worldspawn` | 8910 | `common/caulk, gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 887 | `worldspawn` | 8920 | `common/caulk, gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 888 | `worldspawn` | 8930 | `common/caulk, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 889 | `worldspawn` | 8940 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 890 | `worldspawn` | 8950 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 891 | `worldspawn` | 8960 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 892 | `worldspawn` | 8970 | `gothic_block/gkc15_big` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 893 | `worldspawn` | 8980 | `gothic_floor/metalbridge06` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 894 | `worldspawn` | 8990 | `gothic_floor/metalbridge06` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 895 | `worldspawn` | 9000 | `gothic_floor/metalbridge06` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 896 | `worldspawn` | 9010 | `gothic_floor/metalbridge06` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 897 | `worldspawn` | 9020 | `gothic_floor/metalbridge06` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 898 | `worldspawn` | 9030 | `gothic_floor/metalbridge06` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 899 | `worldspawn` | 9040 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 900 | `worldspawn` | 9050 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 901 | `worldspawn` | 9060 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 902 | `worldspawn` | 9070 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 903 | `worldspawn` | 9080 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 904 | `worldspawn` | 9090 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 905 | `worldspawn` | 9100 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 906 | `worldspawn` | 9110 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 907 | `worldspawn` | 9120 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 908 | `worldspawn` | 9130 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 909 | `worldspawn` | 9140 | `gothic_block/gkc10` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 910 | `worldspawn` | 9150 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 911 | `worldspawn` | 9160 | `gothic_block/gkc15_big` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 912 | `worldspawn` | 9170 | `gothic_block/gkc10` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 913 | `worldspawn` | 9180 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 914 | `worldspawn` | 9190 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 915 | `worldspawn` | 9200 | `common/caulk, gothic_block/gkc10, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 916 | `worldspawn` | 9210 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 917 | `worldspawn` | 9220 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 918 | `worldspawn` | 9230 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 919 | `worldspawn` | 9240 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 920 | `worldspawn` | 9250 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 921 | `worldspawn` | 9260 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 922 | `worldspawn` | 9270 | `gothic_ceiling/ceilingtechplain` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 923 | `worldspawn` | 9280 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 924 | `worldspawn` | 9290 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 925 | `worldspawn` | 9300 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 926 | `worldspawn` | 9310 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 927 | `worldspawn` | 9320 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 928 | `worldspawn` | 9330 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 929 | `worldspawn` | 9340 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 930 | `worldspawn` | 9350 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 931 | `worldspawn` | 9360 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 932 | `worldspawn` | 9370 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 933 | `worldspawn` | 9380 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 934 | `worldspawn` | 9390 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 935 | `worldspawn` | 9400 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 936 | `worldspawn` | 9410 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 937 | `worldspawn` | 9420 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 938 | `worldspawn` | 9430 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 939 | `worldspawn` | 9440 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 940 | `worldspawn` | 9450 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 941 | `worldspawn` | 9460 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 942 | `worldspawn` | 9470 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 943 | `worldspawn` | 9480 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 944 | `worldspawn` | 9490 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 945 | `worldspawn` | 9500 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 946 | `worldspawn` | 9510 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 947 | `worldspawn` | 9520 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 948 | `worldspawn` | 9530 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 949 | `worldspawn` | 9540 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 950 | `worldspawn` | 9550 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 951 | `worldspawn` | 9560 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 952 | `worldspawn` | 9570 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 953 | `worldspawn` | 9580 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 954 | `worldspawn` | 9590 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 955 | `worldspawn` | 9600 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 956 | `worldspawn` | 9610 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 957 | `worldspawn` | 9620 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 958 | `worldspawn` | 9630 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 959 | `worldspawn` | 9640 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 960 | `worldspawn` | 9650 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 961 | `worldspawn` | 9660 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 962 | `worldspawn` | 9670 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 963 | `worldspawn` | 9680 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 964 | `worldspawn` | 9690 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 965 | `worldspawn` | 9700 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 966 | `worldspawn` | 9710 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 967 | `worldspawn` | 9720 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 968 | `worldspawn` | 9730 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 969 | `worldspawn` | 9740 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 970 | `worldspawn` | 9750 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 971 | `worldspawn` | 9760 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 972 | `worldspawn` | 9770 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 973 | `worldspawn` | 9780 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 974 | `worldspawn` | 9790 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 975 | `worldspawn` | 9800 | `skies/overkill` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 976 | `worldspawn` | 9810 | `skies/overkill` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 977 | `worldspawn` | 9820 | `skies/overkill` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 978 | `worldspawn` | 9830 | `skies/overkill` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 979 | `worldspawn` | 9840 | `skies/overkill` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 980 | `worldspawn` | 9850 | `gothic_block/gkc10` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 981 | `worldspawn` | 9860 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 982 | `worldspawn` | 9870 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 983 | `worldspawn` | 9880 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 984 | `worldspawn` | 9890 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 985 | `worldspawn` | 9900 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 986 | `worldspawn` | 9910 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 987 | `worldspawn` | 9920 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 988 | `worldspawn` | 9930 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 989 | `worldspawn` | 9940 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 990 | `worldspawn` | 9950 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 991 | `worldspawn` | 9960 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 992 | `worldspawn` | 9970 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 993 | `worldspawn` | 9980 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 994 | `worldspawn` | 9990 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 995 | `worldspawn` | 10000 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 996 | `worldspawn` | 10010 | `base_light/ceil1_38_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 997 | `worldspawn` | 10020 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 998 | `worldspawn` | 10030 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 999 | `worldspawn` | 10040 | `base_light/ceil1_38_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1000 | `worldspawn` | 10050 | `base_light/ceil1_38_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1001 | `worldspawn` | 10060 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1002 | `worldspawn` | 10070 | `base_light/ceil1_38_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1003 | `worldspawn` | 10080 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1004 | `worldspawn` | 10090 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1005 | `worldspawn` | 10100 | `base_light/ceil1_38_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1006 | `worldspawn` | 10110 | `base_light/ceil1_38_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1007 | `worldspawn` | 10120 | `base_light/ceil1_38_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1008 | `worldspawn` | 10130 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1009 | `worldspawn` | 10140 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1010 | `worldspawn` | 10150 | `base_light/ceil1_38_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1011 | `worldspawn` | 10160 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1012 | `worldspawn` | 10170 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1013 | `worldspawn` | 10180 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1014 | `worldspawn` | 10190 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1015 | `worldspawn` | 10200 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1016 | `worldspawn` | 10210 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1017 | `worldspawn` | 10220 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1018 | `worldspawn` | 10230 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1019 | `worldspawn` | 10240 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1020 | `worldspawn` | 10250 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1021 | `worldspawn` | 10260 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1022 | `worldspawn` | 10270 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1023 | `worldspawn` | 10280 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1024 | `worldspawn` | 10290 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1025 | `worldspawn` | 10300 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1026 | `worldspawn` | 10310 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1027 | `worldspawn` | 10320 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1028 | `worldspawn` | 10330 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1029 | `worldspawn` | 10340 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1030 | `worldspawn` | 10350 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1031 | `worldspawn` | 10360 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1032 | `worldspawn` | 10370 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1033 | `worldspawn` | 10380 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1034 | `worldspawn` | 10390 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1035 | `worldspawn` | 10400 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1036 | `worldspawn` | 10410 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1037 | `worldspawn` | 10420 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1038 | `worldspawn` | 10430 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1039 | `worldspawn` | 10440 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1040 | `worldspawn` | 10450 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1041 | `worldspawn` | 10460 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1042 | `worldspawn` | 10470 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1043 | `worldspawn` | 10480 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1044 | `worldspawn` | 10490 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1045 | `worldspawn` | 10500 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1046 | `worldspawn` | 10510 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1047 | `worldspawn` | 10520 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1048 | `worldspawn` | 10530 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1049 | `worldspawn` | 10540 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1050 | `worldspawn` | 10550 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1051 | `worldspawn` | 10560 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1052 | `worldspawn` | 10570 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1053 | `worldspawn` | 10580 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1054 | `worldspawn` | 10590 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1055 | `worldspawn` | 10600 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1056 | `worldspawn` | 10610 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1057 | `worldspawn` | 10620 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1058 | `worldspawn` | 10630 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1059 | `worldspawn` | 10640 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1060 | `worldspawn` | 10650 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1061 | `worldspawn` | 10660 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1062 | `worldspawn` | 10670 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1063 | `worldspawn` | 10680 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1064 | `worldspawn` | 10690 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1065 | `worldspawn` | 10700 | `common/caulk, gothic_block/blocks11b, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1066 | `worldspawn` | 10710 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1067 | `worldspawn` | 10720 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1068 | `worldspawn` | 10730 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1069 | `worldspawn` | 10740 | `gothic_block/blocks11b, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1070 | `worldspawn` | 10750 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1071 | `worldspawn` | 10760 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1072 | `worldspawn` | 10770 | `gothic_block/blocks18c_3, gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1073 | `worldspawn` | 10780 | `gothic_block/blocks11b, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1074 | `worldspawn` | 10790 | `gothic_block/blocks11b, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1075 | `worldspawn` | 10800 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1076 | `worldspawn` | 10810 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1077 | `worldspawn` | 10820 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1078 | `worldspawn` | 10830 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1079 | `worldspawn` | 10840 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1080 | `worldspawn` | 10850 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1081 | `worldspawn` | 10860 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1082 | `worldspawn` | 10870 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1083 | `worldspawn` | 10880 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1084 | `worldspawn` | 10890 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1085 | `worldspawn` | 10900 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1086 | `worldspawn` | 10910 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1087 | `worldspawn` | 10920 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1088 | `worldspawn` | 10930 | `gothic_block/blocks18b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1089 | `worldspawn` | 10940 | `gothic_block/blocks11b, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1090 | `worldspawn` | 10950 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1091 | `worldspawn` | 10960 | `gothic_floor/largerblock3b2, gothic_floor/metalbridge06` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1092 | `worldspawn` | 10970 | `common/caulk, gothic_wall/dm5_archifin_pot` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1093 | `worldspawn` | 10980 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1094 | `worldspawn` | 10990 | `common/caulk, gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1095 | `worldspawn` | 11000 | `common/caulk, gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1096 | `worldspawn` | 11010 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1097 | `worldspawn` | 11020 | `common/caulk, gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1098 | `worldspawn` | 11030 | `common/caulk, gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1099 | `worldspawn` | 11040 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1100 | `worldspawn` | 11050 | `common/caulk, gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1101 | `worldspawn` | 11060 | `common/caulk, gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1102 | `worldspawn` | 11070 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1103 | `worldspawn` | 11080 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1104 | `worldspawn` | 11090 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1105 | `worldspawn` | 11100 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1106 | `worldspawn` | 11110 | `common/caulk, gothic_trim/pitted_roof` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1107 | `worldspawn` | 11120 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1108 | `worldspawn` | 11130 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1109 | `worldspawn` | 11140 | `common/caulk, gothic_trim/pitted_roof, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1110 | `worldspawn` | 11150 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1111 | `worldspawn` | 11160 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1112 | `worldspawn` | 11170 | `gothic_block/gkc10` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1113 | `worldspawn` | 11180 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1114 | `worldspawn` | 11190 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1115 | `worldspawn` | 11200 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1116 | `worldspawn` | 11210 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1117 | `worldspawn` | 11220 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1118 | `worldspawn` | 11230 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1119 | `worldspawn` | 11240 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1120 | `worldspawn` | 11250 | `common/caulk, gothic_block/blocks18c_3, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1121 | `worldspawn` | 11260 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1122 | `worldspawn` | 11270 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1123 | `worldspawn` | 11280 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1124 | `worldspawn` | 11290 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1125 | `worldspawn` | 11300 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1126 | `worldspawn` | 11310 | `gothic_block/gkc10` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1127 | `worldspawn` | 11320 | `gothic_block/gkc10` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1128 | `worldspawn` | 11330 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1129 | `worldspawn` | 11340 | `gothic_block/gkc10` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1130 | `worldspawn` | 11350 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1131 | `worldspawn` | 11360 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1132 | `worldspawn` | 11370 | `common/caulk, gothic_block/blocks18c_3, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1133 | `worldspawn` | 11380 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1134 | `worldspawn` | 11390 | `gothic_block/gkc10` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1135 | `worldspawn` | 11400 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1136 | `worldspawn` | 11410 | `gothic_block/gkc10` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1137 | `worldspawn` | 11420 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1138 | `worldspawn` | 11430 | `gothic_block/blocks11b, gothic_block/gkc15_big` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1139 | `worldspawn` | 11440 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1140 | `worldspawn` | 11450 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1141 | `worldspawn` | 11460 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1142 | `worldspawn` | 11470 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1143 | `worldspawn` | 11480 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1144 | `worldspawn` | 11490 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1145 | `worldspawn` | 11500 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1146 | `worldspawn` | 11510 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1147 | `worldspawn` | 11520 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1148 | `worldspawn` | 11530 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1149 | `worldspawn` | 11540 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1150 | `worldspawn` | 11550 | `gothic_block/blocks18c_3, gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1151 | `worldspawn` | 11560 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1152 | `worldspawn` | 11570 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1153 | `worldspawn` | 11580 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1154 | `worldspawn` | 11590 | `skies/overkill` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1155 | `worldspawn` | 11600 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1156 | `worldspawn` | 11610 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1157 | `worldspawn` | 11620 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1158 | `worldspawn` | 11630 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1159 | `worldspawn` | 11640 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1160 | `worldspawn` | 11650 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1161 | `worldspawn` | 11660 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1162 | `worldspawn` | 11670 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1163 | `worldspawn` | 11680 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1164 | `worldspawn` | 11690 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1165 | `worldspawn` | 11700 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1166 | `worldspawn` | 11710 | `gothic_block/blocks11b, gothic_door/archxiandm1dblack_pot` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1167 | `worldspawn` | 11720 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1168 | `worldspawn` | 11730 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1169 | `worldspawn` | 11740 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1170 | `worldspawn` | 11750 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1171 | `worldspawn` | 11760 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1172 | `worldspawn` | 11770 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1173 | `worldspawn` | 11780 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1174 | `worldspawn` | 11790 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1175 | `worldspawn` | 11800 | `gothic_floor/largerblock3b2, gothic_floor/metalbridge06` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1176 | `worldspawn` | 11810 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1177 | `worldspawn` | 11820 | `gothic_block/blocks11b, gothic_block/blocks18c_3, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1178 | `worldspawn` | 11830 | `gothic_block/blocks11b, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1179 | `worldspawn` | 11840 | `common/caulk, gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1180 | `worldspawn` | 11850 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1181 | `worldspawn` | 11860 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1182 | `worldspawn` | 11870 | `gothic_block/gkc10` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1183 | `worldspawn` | 11880 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1184 | `worldspawn` | 11890 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1185 | `worldspawn` | 11900 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1186 | `worldspawn` | 11910 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1187 | `worldspawn` | 11920 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1188 | `worldspawn` | 11930 | `skies/overkill` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1189 | `worldspawn` | 11940 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1190 | `worldspawn` | 11950 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1191 | `worldspawn` | 11960 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1192 | `worldspawn` | 11970 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1193 | `worldspawn` | 11980 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1194 | `worldspawn` | 11990 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1195 | `worldspawn` | 12000 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1196 | `worldspawn` | 12010 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1197 | `worldspawn` | 12020 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1198 | `worldspawn` | 12030 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1199 | `worldspawn` | 12040 | `common/caulk, gothic_block/blocks18c_3, gothic_wall/xiantourneywall_c1_pot` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1200 | `worldspawn` | 12050 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1201 | `worldspawn` | 12060 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1202 | `worldspawn` | 12070 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1203 | `worldspawn` | 12080 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1204 | `worldspawn` | 12090 | `gothic_block/blocks11b, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1205 | `worldspawn` | 12100 | `common/caulk, gothic_door/archxiandm1dblack_pot, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1206 | `worldspawn` | 12110 | `common/caulk, gothic_block/blocks11b, gothic_door/archxiandm1dblack_pot, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1207 | `worldspawn` | 12120 | `common/caulk, gothic_door/archxiandm1dblack_pot` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1208 | `worldspawn` | 12130 | `common/caulk, gothic_block/blocks11b, gothic_door/archxiandm1dblack_pot` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1209 | `worldspawn` | 12140 | `common/caulk, gothic_block/blocks11b, gothic_door/archxiandm1dblack_pot` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1210 | `worldspawn` | 12150 | `common/caulk, gothic_block/blocks18c_3, gothic_wall/xiantourneywall_c1_pot` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1211 | `worldspawn` | 12160 | `common/caulk, gothic_wall/xiantourneywall_c1_pot` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1212 | `worldspawn` | 12170 | `common/caulk, gothic_trim/pitted_rust, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1213 | `worldspawn` | 12180 | `common/caulk, gothic_trim/pitted_rust, gothic_trim/pitted_rust3_black, skies/overkill` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1214 | `worldspawn` | 12190 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1215 | `worldspawn` | 12200 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1216 | `worldspawn` | 12210 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1217 | `worldspawn` | 12220 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1218 | `worldspawn` | 12230 | `common/caulk, gothic_trim/pitted_rust, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1219 | `worldspawn` | 12240 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1220 | `worldspawn` | 12250 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1221 | `worldspawn` | 12260 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1222 | `worldspawn` | 12270 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1223 | `worldspawn` | 12280 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1224 | `worldspawn` | 12290 | `gothic_trim/pitted_rust, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1225 | `worldspawn` | 12300 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1226 | `worldspawn` | 12310 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1227 | `worldspawn` | 12320 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1228 | `worldspawn` | 12330 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1229 | `worldspawn` | 12340 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1230 | `worldspawn` | 12350 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1231 | `worldspawn` | 12360 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1232 | `worldspawn` | 12370 | `common/caulk, gothic_block/blocks11b, gothic_block/blocks18c_3, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1233 | `worldspawn` | 12380 | `common/caulk, gothic_block/blocks11b, gothic_block/blocks18c_3, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1234 | `worldspawn` | 12390 | `common/caulk, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1235 | `worldspawn` | 12400 | `common/caulk, gothic_trim/pitted_rust, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1236 | `worldspawn` | 12410 | `gothic_block/blocks18c_3, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1237 | `worldspawn` | 12420 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1238 | `worldspawn` | 12430 | `common/caulk, gothic_block/blocks11b, gothic_block/blocks18c_3, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1239 | `worldspawn` | 12440 | `common/caulk, gothic_block/blocks11b, gothic_block/blocks18c_3, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1240 | `worldspawn` | 12450 | `common/caulk, gothic_trim/pitted_rust, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1241 | `worldspawn` | 12460 | `gothic_trim/pitted_rust, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1242 | `worldspawn` | 12470 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1243 | `worldspawn` | 12480 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1244 | `worldspawn` | 12490 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1245 | `worldspawn` | 12500 | `gothic_trim/pitted_rust, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1246 | `worldspawn` | 12510 | `common/caulk, gothic_block/blocks11b, gothic_light/pentagram_light1_10k` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1247 | `worldspawn` | 12520 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1248 | `worldspawn` | 12530 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1249 | `worldspawn` | 12540 | `common/caulk, gothic_trim/pitted_rust, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1250 | `worldspawn` | 12550 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1251 | `worldspawn` | 12560 | `skies/overkill` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1252 | `worldspawn` | 12570 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1253 | `worldspawn` | 12580 | `gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1254 | `worldspawn` | 12590 | `common/caulk, gothic_block/blocks11b, gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1255 | `worldspawn` | 12600 | `common/caulk, gothic_block/blocks11b, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1256 | `worldspawn` | 12610 | `common/caulk, gothic_block/blocks11b, gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1257 | `worldspawn` | 12620 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1258 | `worldspawn` | 12630 | `common/caulk, gothic_block/blocks11b, gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1259 | `worldspawn` | 12640 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1260 | `worldspawn` | 12650 | `common/caulk, gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1261 | `worldspawn` | 12660 | `common/caulk, gothic_block/blocks11b, gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1262 | `worldspawn` | 12670 | `gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1263 | `worldspawn` | 12680 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1264 | `worldspawn` | 12690 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1265 | `worldspawn` | 12700 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1266 | `worldspawn` | 12710 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1267 | `worldspawn` | 12720 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1268 | `worldspawn` | 12730 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1269 | `worldspawn` | 12740 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1270 | `worldspawn` | 12750 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1271 | `worldspawn` | 12760 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1272 | `worldspawn` | 12770 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1273 | `worldspawn` | 12780 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1274 | `worldspawn` | 12790 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1275 | `worldspawn` | 12800 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1276 | `worldspawn` | 12810 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1277 | `worldspawn` | 12820 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1278 | `worldspawn` | 12830 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1279 | `worldspawn` | 12840 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1280 | `worldspawn` | 12850 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1281 | `worldspawn` | 12860 | `gothic_block/blocks18c_3, gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1282 | `worldspawn` | 12870 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1283 | `worldspawn` | 12880 | `gothic_block/blocks18c_3, gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1284 | `worldspawn` | 12890 | `skies/overkill` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1285 | `worldspawn` | 12900 | `gothic_trim/pitted_rust, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1286 | `worldspawn` | 12910 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1287 | `worldspawn` | 12920 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1288 | `worldspawn` | 12930 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1289 | `worldspawn` | 12940 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1290 | `worldspawn` | 12950 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1291 | `worldspawn` | 12960 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1292 | `worldspawn` | 12970 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1293 | `worldspawn` | 12980 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1294 | `worldspawn` | 12990 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1295 | `worldspawn` | 13000 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1296 | `worldspawn` | 13010 | `skies/overkill` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1297 | `worldspawn` | 13020 | `skies/overkill` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1298 | `worldspawn` | 13030 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1299 | `worldspawn` | 13040 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1300 | `worldspawn` | 13050 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1301 | `worldspawn` | 13060 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1302 | `worldspawn` | 13070 | `skies/overkill` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1303 | `worldspawn` | 13080 | `skies/overkill` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1304 | `worldspawn` | 13090 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1305 | `worldspawn` | 13100 | `skies/overkill` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1306 | `worldspawn` | 13110 | `skies/overkill` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1307 | `worldspawn` | 13120 | `gothic_trim/pitted_rust, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1308 | `worldspawn` | 13130 | `skies/overkill` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1309 | `worldspawn` | 13140 | `gothic_trim/pitted_rust, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1310 | `worldspawn` | 13150 | `gothic_block/blocks18c_3, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1311 | `worldspawn` | 13160 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1312 | `worldspawn` | 13170 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1313 | `worldspawn` | 13180 | `gothic_trim/pitted_rust, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1314 | `worldspawn` | 13190 | `gothic_trim/pitted_rust, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1315 | `worldspawn` | 13200 | `skies/overkill` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1316 | `worldspawn` | 13210 | `skies/overkill` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1317 | `worldspawn` | 13220 | `skies/overkill` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1318 | `worldspawn` | 13230 | `gothic_trim/pitted_rust, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1319 | `worldspawn` | 13240 | `skies/overkill` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1320 | `worldspawn` | 13250 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1321 | `worldspawn` | 13260 | `gothic_trim/pitted_rust, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1322 | `worldspawn` | 13270 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1323 | `worldspawn` | 13280 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1324 | `worldspawn` | 13290 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1325 | `worldspawn` | 13300 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1326 | `worldspawn` | 13310 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1327 | `worldspawn` | 13320 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1328 | `worldspawn` | 13330 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1329 | `worldspawn` | 13340 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1330 | `worldspawn` | 13350 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1331 | `worldspawn` | 13360 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1332 | `worldspawn` | 13370 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1333 | `worldspawn` | 13380 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1334 | `worldspawn` | 13390 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1335 | `worldspawn` | 13400 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1336 | `worldspawn` | 13410 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1337 | `worldspawn` | 13420 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1338 | `worldspawn` | 13430 | `gothic_block/blocks11b, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1339 | `worldspawn` | 13440 | `gothic_block/blocks11b, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1340 | `worldspawn` | 13450 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1341 | `worldspawn` | 13460 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1342 | `worldspawn` | 13470 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1343 | `worldspawn` | 13480 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1344 | `worldspawn` | 13490 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1345 | `worldspawn` | 13500 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1346 | `worldspawn` | 13510 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1347 | `worldspawn` | 13520 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1348 | `worldspawn` | 13530 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1349 | `worldspawn` | 13540 | `gothic_block/gkc10` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1350 | `worldspawn` | 13550 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1351 | `worldspawn` | 13560 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1352 | `worldspawn` | 13570 | `skies/overkill` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1353 | `worldspawn` | 13580 | `skies/overkill` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1354 | `worldspawn` | 13590 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1355 | `worldspawn` | 13600 | `gothic_trim/pitted_rust, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1356 | `worldspawn` | 13610 | `gothic_trim/pitted_rust, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1357 | `worldspawn` | 13620 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1358 | `worldspawn` | 13630 | `common/caulk, gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1359 | `worldspawn` | 13640 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1360 | `worldspawn` | 13650 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1361 | `worldspawn` | 13660 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1362 | `worldspawn` | 13670 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1363 | `worldspawn` | 13680 | `common/caulk, gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1364 | `worldspawn` | 13690 | `common/caulk, gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1365 | `worldspawn` | 13700 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1366 | `worldspawn` | 13710 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1367 | `worldspawn` | 13720 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1368 | `worldspawn` | 13730 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1369 | `worldspawn` | 13740 | `common/caulk, gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1370 | `worldspawn` | 13750 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1371 | `worldspawn` | 13760 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1372 | `worldspawn` | 13770 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1373 | `worldspawn` | 13780 | `common/caulk, gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1374 | `worldspawn` | 13790 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1375 | `worldspawn` | 13800 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1376 | `worldspawn` | 13810 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1377 | `worldspawn` | 13820 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1378 | `worldspawn` | 13830 | `common/caulk, gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1379 | `worldspawn` | 13840 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1380 | `worldspawn` | 13850 | `gothic_trim/pitted_rust, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1381 | `worldspawn` | 13860 | `gothic_trim/pitted_rust, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1382 | `worldspawn` | 13870 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1383 | `worldspawn` | 13880 | `common/caulk, gothic_floor/blocks17floor2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1384 | `worldspawn` | 13890 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1385 | `worldspawn` | 13900 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1386 | `worldspawn` | 13910 | `skies/overkill` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1387 | `worldspawn` | 13920 | `common/caulk, gothic_block/blocks11b, gothic_light/pentagram_light1_10k` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1388 | `worldspawn` | 13930 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1389 | `worldspawn` | 13940 | `gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1390 | `worldspawn` | 13950 | `gothic_light/ironcrosslt2_1000` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1391 | `worldspawn` | 13960 | `gothic_light/ironcrosslt2_1000` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1392 | `worldspawn` | 13970 | `gothic_floor/blocks17floor2, gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1393 | `worldspawn` | 13980 | `gothic_light/pentagram_light1_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1394 | `worldspawn` | 13990 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1395 | `worldspawn` | 14000 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1396 | `worldspawn` | 14010 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1397 | `worldspawn` | 14020 | `gothic_light/pentagram_light1_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1398 | `worldspawn` | 14030 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1399 | `worldspawn` | 14040 | `gothic_light/pentagram_light1_10k` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1400 | `worldspawn` | 14050 | `gothic_light/pentagram_light1_10k` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1401 | `worldspawn` | 14060 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1402 | `worldspawn` | 14070 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1403 | `worldspawn` | 14080 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1404 | `worldspawn` | 14090 | `common/caulk, gothic_light/pentagram_light1_10k` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1405 | `worldspawn` | 14100 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1406 | `worldspawn` | 14110 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1407 | `worldspawn` | 14120 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1408 | `worldspawn` | 14130 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1409 | `worldspawn` | 14140 | `common/caulk, gothic_light/pentagram_light1_10k` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1410 | `worldspawn` | 14150 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1411 | `worldspawn` | 14160 | `common/caulk, gothic_block/blocks11b, gothic_light/pentagram_light1_10k` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1412 | `worldspawn` | 14170 | `common/caulk, gothic_block/blocks11b, gothic_light/pentagram_light1_10k` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1413 | `worldspawn` | 14180 | `common/caulk, gothic_block/blocks11b, gothic_light/pentagram_light1_10k` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1414 | `worldspawn` | 14190 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1415 | `worldspawn` | 14200 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1416 | `worldspawn` | 14210 | `gothic_trim/pitted_rust, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1417 | `worldspawn` | 14220 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1418 | `worldspawn` | 14230 | `common/caulk, gothic_trim/pitted_rust, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1419 | `worldspawn` | 14240 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1420 | `worldspawn` | 14250 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1421 | `worldspawn` | 14260 | `gothic_light/ironcrosslt2_1000, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1422 | `worldspawn` | 14270 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1423 | `worldspawn` | 14280 | `common/caulk, gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1424 | `worldspawn` | 14290 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1425 | `worldspawn` | 14300 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1426 | `worldspawn` | 14310 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1427 | `worldspawn` | 14320 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1428 | `worldspawn` | 14330 | `gothic_block/blocks11b, gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1429 | `worldspawn` | 14340 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1430 | `worldspawn` | 14350 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1431 | `worldspawn` | 14360 | `gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1432 | `worldspawn` | 14370 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1433 | `worldspawn` | 14380 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1434 | `worldspawn` | 14390 | `gothic_ceiling/ceilingtechplain` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1435 | `worldspawn` | 14400 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1436 | `worldspawn` | 14410 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1437 | `worldspawn` | 14420 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1438 | `worldspawn` | 14430 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1439 | `worldspawn` | 14440 | `gothic_block/blocks17j` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1440 | `worldspawn` | 14450 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1441 | `worldspawn` | 14460 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1442 | `worldspawn` | 14470 | `gothic_block/blocks17j` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1443 | `worldspawn` | 14480 | `gothic_floor/xstepborder10, gothic_floor/xstepborder12, gothic_floor/xstepborder8` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1444 | `worldspawn` | 14490 | `gothic_floor/xstepborder10, gothic_floor/xstepborder12, gothic_floor/xstepborder8` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1445 | `worldspawn` | 14500 | `gothic_floor/xstepborder10, gothic_floor/xstepborder12, gothic_floor/xstepborder8` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1446 | `worldspawn` | 14510 | `gothic_floor/xstepborder10, gothic_floor/xstepborder12, gothic_floor/xstepborder8` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1447 | `worldspawn` | 14520 | `gothic_floor/xstepborder10, gothic_floor/xstepborder12, gothic_floor/xstepborder8` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1448 | `worldspawn` | 14530 | `gothic_floor/xstepborder10, gothic_floor/xstepborder12, gothic_floor/xstepborder8` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1449 | `worldspawn` | 14540 | `gothic_floor/xstepborder10, gothic_floor/xstepborder12, gothic_floor/xstepborder8` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1450 | `worldspawn` | 14550 | `gothic_floor/xstepborder10, gothic_floor/xstepborder12, gothic_floor/xstepborder8` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1451 | `worldspawn` | 14560 | `gothic_floor/xstepborder10, gothic_floor/xstepborder12, gothic_floor/xstepborder8` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1452 | `worldspawn` | 14570 | `gothic_floor/xstepborder10, gothic_floor/xstepborder12, gothic_floor/xstepborder8` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1453 | `worldspawn` | 14580 | `gothic_floor/xstepborder10, gothic_floor/xstepborder12, gothic_floor/xstepborder8` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1454 | `worldspawn` | 14590 | `gothic_floor/xstepborder10, gothic_floor/xstepborder12, gothic_floor/xstepborder8` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1455 | `worldspawn` | 14600 | `gothic_floor/xstepborder10, gothic_floor/xstepborder12, gothic_floor/xstepborder8` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1456 | `worldspawn` | 14610 | `gothic_floor/xstepborder10, gothic_floor/xstepborder12, gothic_floor/xstepborder8` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1457 | `worldspawn` | 14620 | `gothic_floor/xstepborder10, gothic_floor/xstepborder12, gothic_floor/xstepborder8` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1458 | `worldspawn` | 14630 | `gothic_block/blocks17j` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1459 | `worldspawn` | 14640 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1460 | `worldspawn` | 14650 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1461 | `worldspawn` | 14660 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1462 | `worldspawn` | 14670 | `common/caulk, gothic_light/pentagram_light1_10k` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1463 | `worldspawn` | 14680 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1464 | `worldspawn` | 14690 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1465 | `worldspawn` | 14700 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1466 | `worldspawn` | 14710 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1467 | `worldspawn` | 14720 | `common/caulk, gothic_light/pentagram_light1_10k` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1468 | `worldspawn` | 14730 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1469 | `worldspawn` | 14740 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1470 | `worldspawn` | 14750 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1471 | `worldspawn` | 14760 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1472 | `worldspawn` | 14770 | `gothic_block/blocks17j` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1473 | `worldspawn` | 14780 | `gothic_block/blocks17j` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1474 | `worldspawn` | 14790 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1475 | `worldspawn` | 14800 | `common/caulk, gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1476 | `worldspawn` | 14810 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1477 | `worldspawn` | 14820 | `gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1478 | `worldspawn` | 14831 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1479 | `worldspawn` | 14842 | `gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1480 | `worldspawn` | 14852 | `gothic_block/blocks11b, gothic_door/xian_tourneyarch_tall2b_pot` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1481 | `worldspawn` | 14862 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1482 | `worldspawn` | 14873 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1483 | `worldspawn` | 14883 | `base_light/ceil1_38_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1484 | `worldspawn` | 14893 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1485 | `worldspawn` | 14903 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1486 | `worldspawn` | 14913 | `base_light/ceil1_38_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1487 | `worldspawn` | 14923 | `base_light/ceil1_38_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1488 | `worldspawn` | 14933 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1489 | `worldspawn` | 14943 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1490 | `worldspawn` | 14954 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1491 | `worldspawn` | 14965 | `gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1492 | `worldspawn` | 14976 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1493 | `worldspawn` | 14986 | `base_light/ceil1_38_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1494 | `worldspawn` | 14996 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1495 | `worldspawn` | 15006 | `gothic_ceiling/ceilingtechplain` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1496 | `worldspawn` | 15016 | `base_light/ceil1_38_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1497 | `worldspawn` | 15026 | `base_light/ceil1_38_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1498 | `worldspawn` | 15036 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1499 | `worldspawn` | 15046 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1500 | `worldspawn` | 15056 | `base_light/ceil1_38_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1501 | `worldspawn` | 15066 | `base_light/ceil1_38_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1502 | `worldspawn` | 15076 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1503 | `worldspawn` | 15086 | `base_light/ceil1_38_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1504 | `worldspawn` | 15096 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1505 | `worldspawn` | 15106 | `base_light/ceil1_38_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1506 | `worldspawn` | 15116 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1507 | `worldspawn` | 15126 | `gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1508 | `worldspawn` | 15136 | `gothic_block/blocks11b, gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1509 | `worldspawn` | 15146 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1510 | `worldspawn` | 15156 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1511 | `worldspawn` | 15166 | `gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1512 | `worldspawn` | 15176 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1513 | `worldspawn` | 15186 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1514 | `worldspawn` | 15196 | `base_light/ceil1_38_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1515 | `worldspawn` | 15206 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1516 | `worldspawn` | 15216 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1517 | `worldspawn` | 15226 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1518 | `worldspawn` | 15236 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1519 | `worldspawn` | 15246 | `base_light/ceil1_38_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1520 | `worldspawn` | 15256 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1521 | `worldspawn` | 15266 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1522 | `worldspawn` | 15276 | `base_light/ceil1_38_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1523 | `worldspawn` | 15286 | `gothic_block/blocks11b, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1524 | `worldspawn` | 15296 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1525 | `worldspawn` | 15306 | `base_light/ceil1_38_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1526 | `worldspawn` | 15316 | `base_light/ceil1_38_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1527 | `worldspawn` | 15326 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1528 | `worldspawn` | 15336 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1529 | `worldspawn` | 15346 | `base_light/ceil1_38_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1530 | `worldspawn` | 15356 | `base_light/ceil1_38_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1531 | `worldspawn` | 15366 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1532 | `worldspawn` | 15376 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1533 | `worldspawn` | 15386 | `base_light/ceil1_38_10k, common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1534 | `worldspawn` | 15396 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1535 | `worldspawn` | 15406 | `gothic_ceiling/ceilingtechplain` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1536 | `worldspawn` | 15416 | `gothic_ceiling/ceilingtechplain` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1537 | `worldspawn` | 15426 | `gothic_ceiling/ceilingtechplain` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1538 | `worldspawn` | 15436 | `gothic_ceiling/ceilingtechplain` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1539 | `worldspawn` | 15446 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1540 | `worldspawn` | 15456 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1541 | `worldspawn` | 15466 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1542 | `worldspawn` | 15476 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1543 | `worldspawn` | 15486 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1544 | `worldspawn` | 15496 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1545 | `worldspawn` | 15506 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1546 | `worldspawn` | 15516 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1547 | `worldspawn` | 15526 | `skies/overkill` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1548 | `worldspawn` | 15536 | `skies/overkill` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1549 | `worldspawn` | 15546 | `skies/overkill` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1550 | `worldspawn` | 15556 | `skies/overkill` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1551 | `worldspawn` | 15566 | `skies/overkill` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1552 | `worldspawn` | 15576 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1553 | `worldspawn` | 15586 | `gothic_block/blocks11b, gothic_block/gkc15_big` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1554 | `worldspawn` | 15596 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1555 | `worldspawn` | 15606 | `common/caulk, gothic_block/blocks11b, gothic_block/gkc15_big, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1556 | `worldspawn` | 15616 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1557 | `worldspawn` | 15626 | `gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1558 | `worldspawn` | 15636 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1559 | `worldspawn` | 15646 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1560 | `worldspawn` | 15656 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1561 | `worldspawn` | 15666 | `common/caulk, gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1562 | `worldspawn` | 15676 | `common/caulk, gothic_block/blocks11b, gothic_block/gkc15_big` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1563 | `worldspawn` | 15686 | `common/caulk, gothic_block/blocks11b, gothic_block/gkc15_big` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1564 | `worldspawn` | 15696 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1565 | `worldspawn` | 15706 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1566 | `worldspawn` | 15716 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1567 | `worldspawn` | 15726 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1568 | `worldspawn` | 15736 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1569 | `worldspawn` | 15746 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1570 | `worldspawn` | 15756 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1571 | `worldspawn` | 15767 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1572 | `worldspawn` | 15777 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1573 | `worldspawn` | 15787 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1574 | `worldspawn` | 15797 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1575 | `worldspawn` | 15807 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1576 | `worldspawn` | 15817 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1577 | `worldspawn` | 15827 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1578 | `worldspawn` | 15837 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1579 | `worldspawn` | 15847 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1580 | `worldspawn` | 15857 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1581 | `worldspawn` | 15867 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1582 | `worldspawn` | 15877 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1583 | `worldspawn` | 15887 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1584 | `worldspawn` | 15897 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1585 | `worldspawn` | 15907 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1586 | `worldspawn` | 15917 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1587 | `worldspawn` | 15927 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1588 | `worldspawn` | 15937 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1589 | `worldspawn` | 15947 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1590 | `worldspawn` | 15957 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1591 | `worldspawn` | 15967 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1592 | `worldspawn` | 15977 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1593 | `worldspawn` | 15987 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1594 | `worldspawn` | 15997 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1595 | `worldspawn` | 16008 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1596 | `worldspawn` | 16018 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1597 | `worldspawn` | 16028 | `common/caulk, gothic_block/blocks11b, gothic_light/pentagram_light1_10k` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1598 | `worldspawn` | 16038 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1599 | `worldspawn` | 16048 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1600 | `worldspawn` | 16058 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1601 | `worldspawn` | 16068 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1602 | `worldspawn` | 16078 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1603 | `worldspawn` | 16088 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1604 | `worldspawn` | 16098 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1605 | `worldspawn` | 16109 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1606 | `worldspawn` | 16119 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1607 | `worldspawn` | 16130 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1608 | `worldspawn` | 16140 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1609 | `worldspawn` | 16151 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1610 | `worldspawn` | 16162 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1611 | `worldspawn` | 16172 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1612 | `worldspawn` | 16183 | `base_light/ceil1_38_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1613 | `worldspawn` | 16193 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1614 | `worldspawn` | 16203 | `base_light/ceil1_38_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1615 | `worldspawn` | 16213 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1616 | `worldspawn` | 16223 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1617 | `worldspawn` | 16233 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1618 | `worldspawn` | 16243 | `base_light/ceil1_38_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1619 | `worldspawn` | 16253 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1620 | `worldspawn` | 16263 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1621 | `worldspawn` | 16273 | `base_light/ceil1_38_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1622 | `worldspawn` | 16283 | `base_light/ceil1_38_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1623 | `worldspawn` | 16293 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1624 | `worldspawn` | 16303 | `gothic_ceiling/ceilingtechplain` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1625 | `worldspawn` | 16313 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1626 | `worldspawn` | 16323 | `base_light/ceil1_38_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1627 | `worldspawn` | 16333 | `base_light/ceil1_38_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1628 | `worldspawn` | 16343 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1629 | `worldspawn` | 16353 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1630 | `worldspawn` | 16363 | `base_light/ceil1_38_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1631 | `worldspawn` | 16373 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1632 | `worldspawn` | 16383 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1633 | `worldspawn` | 16394 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1634 | `worldspawn` | 16404 | `base_light/ceil1_38_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1635 | `worldspawn` | 16414 | `base_light/ceil1_38_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1636 | `worldspawn` | 16424 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1637 | `worldspawn` | 16434 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1638 | `worldspawn` | 16444 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1639 | `worldspawn` | 16455 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1640 | `worldspawn` | 16465 | `gothic_block/blocks11b, gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1641 | `worldspawn` | 16475 | `gothic_block/blocks11b, gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1642 | `worldspawn` | 16485 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1643 | `worldspawn` | 16495 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1644 | `worldspawn` | 16505 | `base_light/ceil1_38_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1645 | `worldspawn` | 16515 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1646 | `worldspawn` | 16525 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1647 | `worldspawn` | 16535 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1648 | `worldspawn` | 16545 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1649 | `worldspawn` | 16555 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1650 | `worldspawn` | 16565 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1651 | `worldspawn` | 16575 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1652 | `worldspawn` | 16585 | `base_light/ceil1_38_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1653 | `worldspawn` | 16595 | `base_light/ceil1_38_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1654 | `worldspawn` | 16605 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1655 | `worldspawn` | 16615 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1656 | `worldspawn` | 16625 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1657 | `worldspawn` | 16635 | `base_light/ceil1_38_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1658 | `worldspawn` | 16645 | `gothic_block/blocks11b, gothic_ceiling/ceilingtechplain` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1659 | `worldspawn` | 16655 | `gothic_ceiling/ceilingtechplain` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1660 | `worldspawn` | 16665 | `gothic_ceiling/ceilingtechplain` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1661 | `worldspawn` | 16675 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1662 | `worldspawn` | 16685 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1663 | `worldspawn` | 16696 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1664 | `worldspawn` | 16707 | `common/caulk, gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1665 | `worldspawn` | 16718 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1666 | `worldspawn` | 16728 | `common/caulk, gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1667 | `worldspawn` | 16739 | `common/caulk, gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1668 | `worldspawn` | 16750 | `common/caulk, gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1669 | `worldspawn` | 16761 | `gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1670 | `worldspawn` | 16772 | `gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1671 | `worldspawn` | 16783 | `gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1672 | `worldspawn` | 16794 | `common/caulk, gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1673 | `worldspawn` | 16804 | `common/caulk, gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1674 | `worldspawn` | 16815 | `common/caulk, gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1675 | `worldspawn` | 16826 | `gothic_block/blocks11b, gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1676 | `worldspawn` | 16836 | `common/caulk, gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1677 | `worldspawn` | 16846 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1678 | `worldspawn` | 16856 | `base_light/ceil1_38_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1679 | `worldspawn` | 16866 | `base_light/ceil1_38_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1680 | `worldspawn` | 16876 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1681 | `worldspawn` | 16886 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1682 | `worldspawn` | 16896 | `base_light/ceil1_38_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1683 | `worldspawn` | 16906 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1684 | `worldspawn` | 16916 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1685 | `worldspawn` | 16926 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1686 | `worldspawn` | 16936 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1687 | `worldspawn` | 16946 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1688 | `worldspawn` | 16957 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1689 | `worldspawn` | 16968 | `gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1690 | `worldspawn` | 16979 | `base_light/ceil1_38_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1691 | `worldspawn` | 16989 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1692 | `worldspawn` | 16999 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1693 | `worldspawn` | 17009 | `base_light/ceil1_38_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1694 | `worldspawn` | 17019 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1695 | `worldspawn` | 17029 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1696 | `worldspawn` | 17040 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1697 | `worldspawn` | 17050 | `base_light/ceil1_38_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1698 | `worldspawn` | 17060 | `base_light/ceil1_38_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1699 | `worldspawn` | 17070 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1700 | `worldspawn` | 17080 | `gothic_ceiling/ceilingtechplain` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1701 | `worldspawn` | 17090 | `base_light/ceil1_38_10k, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1702 | `worldspawn` | 17100 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1703 | `worldspawn` | 17110 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1704 | `worldspawn` | 17121 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1705 | `worldspawn` | 17132 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1706 | `worldspawn` | 17142 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1707 | `worldspawn` | 17152 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1708 | `worldspawn` | 17163 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1709 | `worldspawn` | 17174 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1710 | `worldspawn` | 17185 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1711 | `worldspawn` | 17196 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1712 | `worldspawn` | 17206 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1713 | `worldspawn` | 17216 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1714 | `worldspawn` | 17226 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1715 | `worldspawn` | 17236 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1716 | `worldspawn` | 17246 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1717 | `worldspawn` | 17256 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1718 | `worldspawn` | 17266 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1719 | `worldspawn` | 17276 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1720 | `worldspawn` | 17286 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1721 | `worldspawn` | 17296 | `common/caulk, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1722 | `worldspawn` | 17306 | `common/caulk, gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1723 | `worldspawn` | 17316 | `common/caulk, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1724 | `worldspawn` | 17326 | `common/caulk, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1725 | `worldspawn` | 17336 | `common/caulk, gothic_door/archxiandm1dblack_pot, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1726 | `worldspawn` | 17346 | `common/caulk, gothic_block/blocks11b, gothic_door/archxiandm1dblack_pot, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1727 | `worldspawn` | 17356 | `common/caulk, gothic_door/archxiandm1dblack_pot` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1728 | `worldspawn` | 17366 | `common/caulk, gothic_block/blocks11b, gothic_door/archxiandm1dblack_pot` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1729 | `worldspawn` | 17376 | `common/caulk, gothic_block/blocks11b, gothic_door/archxiandm1dblack_pot` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1730 | `worldspawn` | 17386 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1731 | `worldspawn` | 17396 | `common/caulk` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1732 | `worldspawn` | 17406 | `common/caulk` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1733 | `worldspawn` | 17416 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1734 | `worldspawn` | 17426 | `common/caulk` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1735 | `worldspawn` | 17436 | `gothic_floor/largerblock3b2` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1736 | `worldspawn` | 17446 | `common/caulk, gothic_block/blocks11b, gothic_door/archxiandm1dblack_pot` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1737 | `worldspawn` | 17456 | `common/caulk, gothic_block/blocks11b, gothic_door/archxiandm1dblack_pot, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1738 | `worldspawn` | 17466 | `common/caulk, gothic_door/archxiandm1dblack_pot, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1739 | `worldspawn` | 17476 | `gothic_block/blocks11b, gothic_door/archxiandm1dblack_pot` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1740 | `worldspawn` | 17486 | `gothic_ceiling/ceilingtechplain` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1741 | `worldspawn` | 17498 | `gothic_ceiling/ceilingtechplain` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1742 | `worldspawn` | 17510 | `common/caulk, gothic_ceiling/ceilingtechplain` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1743 | `worldspawn` | 17520 | `common/caulk, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1744 | `worldspawn` | 17530 | `common/caulk, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1745 | `worldspawn` | 17540 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1746 | `worldspawn` | 17550 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1747 | `worldspawn` | 17560 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1748 | `worldspawn` | 17570 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1749 | `worldspawn` | 17580 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1750 | `worldspawn` | 17590 | `gothic_block/blocks11b, gothic_block/gkc15_big` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1751 | `worldspawn` | 17600 | `gothic_block/blocks11b, gothic_block/gkc15_big` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1752 | `worldspawn` | 17610 | `gothic_block/blocks11b, gothic_block/gkc15_big` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1753 | `worldspawn` | 17620 | `common/caulk, gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1754 | `worldspawn` | 17630 | `common/caulk, gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1755 | `worldspawn` | 17640 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1756 | `worldspawn` | 17650 | `common/caulk, gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1757 | `worldspawn` | 17660 | `common/caulk, gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1758 | `worldspawn` | 17670 | `common/caulk, gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1759 | `worldspawn` | 17680 | `common/caulk, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1760 | `worldspawn` | 17690 | `common/caulk, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1761 | `worldspawn` | 17700 | `common/caulk, gothic_ceiling/ceilingtechplain` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1762 | `worldspawn` | 17710 | `gothic_ceiling/ceilingtechplain` | `omit:invalid_geometry` | `allow` | `omitted` | omitted: invalid_geometry |
| 0 | 1763 | `worldspawn` | 17722 | `gothic_ceiling/ceilingtechplain` | `omit:invalid_geometry` | `allow` | `omitted` | omitted: invalid_geometry |
| 0 | 1764 | `worldspawn` | 17734 | `gothic_block/blocks11b, gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1765 | `worldspawn` | 17744 | `gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1766 | `worldspawn` | 17754 | `gothic_trim/metalsupsolid, gothic_trim/pitted_rust3_black, sfx/launchpad_blocks17` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1767 | `worldspawn` | 17764 | `gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1768 | `worldspawn` | 17774 | `gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1769 | `worldspawn` | 17784 | `gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1770 | `worldspawn` | 17794 | `gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1771 | `worldspawn` | 17804 | `gothic_block/blocks11b, gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1772 | `worldspawn` | 17814 | `common/caulk, gothic_block/blocks11b, gothic_door/archxiandm1dblack_pot, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1773 | `worldspawn` | 17824 | `common/caulk, gothic_door/archxiandm1dblack_pot, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1774 | `worldspawn` | 17834 | `gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1775 | `worldspawn` | 17844 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1776 | `worldspawn` | 17854 | `gothic_block/blocks18c_3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1777 | `worldspawn` | 17864 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1778 | `worldspawn` | 17874 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1779 | `worldspawn` | 17884 | `gothic_trim/pitted_rust` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1780 | `worldspawn` | 17894 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1781 | `worldspawn` | 17904 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1782 | `worldspawn` | 17914 | `gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1783 | `worldspawn` | 17924 | `gothic_trim/baseboard09_c3` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1784 | `worldspawn` | 17934 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1785 | `worldspawn` | 17944 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1786 | `worldspawn` | 17954 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1787 | `worldspawn` | 17964 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1788 | `worldspawn` | 17975 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1789 | `worldspawn` | 17986 | `gothic_ceiling/ceilingtechplain` | `omit:invalid_geometry` | `allow` | `omitted` | omitted: invalid_geometry |
| 0 | 1790 | `worldspawn` | 17998 | `gothic_ceiling/ceilingtechplain` | `omit:invalid_geometry` | `allow` | `omitted` | omitted: invalid_geometry |
| 0 | 1791 | `worldspawn` | 18010 | `common/caulk, gothic_ceiling/ceilingtechplain` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1792 | `worldspawn` | 18020 | `common/caulk, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1793 | `worldspawn` | 18030 | `common/caulk, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1794 | `worldspawn` | 18040 | `common/caulk, gothic_block/blocks11b, gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1795 | `worldspawn` | 18050 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1796 | `worldspawn` | 18060 | `common/caulk, gothic_block/blocks11b, gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1797 | `worldspawn` | 18070 | `common/caulk, gothic_block/blocks11b, gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1798 | `worldspawn` | 18080 | `common/caulk, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1799 | `worldspawn` | 18090 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1800 | `worldspawn` | 18100 | `common/caulk, gothic_block/blocks11b, gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1801 | `worldspawn` | 18110 | `common/caulk, gothic_block/blocks11b, gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1802 | `worldspawn` | 18120 | `common/caulk, gothic_block/blocks11b, gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1803 | `worldspawn` | 18130 | `common/caulk, gothic_block/blocks11b, gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1804 | `worldspawn` | 18140 | `common/caulk, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1805 | `worldspawn` | 18150 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1806 | `worldspawn` | 18160 | `common/caulk, gothic_block/blocks11b, gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1807 | `worldspawn` | 18170 | `common/caulk, gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1808 | `worldspawn` | 18180 | `common/caulk, gothic_block/blocks11b, gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1809 | `worldspawn` | 18190 | `common/caulk, gothic_block/blocks11b, gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1810 | `worldspawn` | 18200 | `gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1811 | `worldspawn` | 18210 | `common/caulk, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1812 | `worldspawn` | 18220 | `common/caulk, gothic_block/blocks11b, gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1813 | `worldspawn` | 18230 | `common/caulk, gothic_trim/pitted_rust3_black` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1814 | `worldspawn` | 18240 | `common/caulk, gothic_block/blocks11b, gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1815 | `worldspawn` | 18250 | `gothic_floor/q1metal7_98d_256x256` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1816 | `worldspawn` | 18260 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1817 | `worldspawn` | 18270 | `gothic_block/blocks11b` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1818 | `worldspawn` | 18280 | `common/caulk` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1819 | `worldspawn` | 18290 | `common/caulk` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1820 | `worldspawn` | 18300 | `gothic_trim/metalsupsolid` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1821 | `worldspawn` | 18310 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1822 | `worldspawn` | 18321 | `common/weapclip` | `weapclip` | `allow` | `weapclip` | emitted |
| 0 | 1823 | `worldspawn` | 18331 | `common/nodraw, ctf/ctf_redflag` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1824 | `worldspawn` | 18341 | `common/nodraw, ctf/ctf_redflag` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1825 | `worldspawn` | 18351 | `common/nodraw, ctf/ctf_redflag` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1826 | `worldspawn` | 18361 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1827 | `worldspawn` | 18371 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1828 | `worldspawn` | 18381 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1829 | `worldspawn` | 18391 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1830 | `worldspawn` | 18401 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1831 | `worldspawn` | 18411 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1832 | `worldspawn` | 18421 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1833 | `worldspawn` | 18431 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1834 | `worldspawn` | 18441 | `sfx/hellfog` | `omit:fog_volume` | `allow` | `omitted` | omitted: fog_volume |
| 0 | 1835 | `worldspawn` | 18451 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1836 | `worldspawn` | 18460 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1837 | `worldspawn` | 18469 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1838 | `worldspawn` | 18478 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1839 | `worldspawn` | 18487 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1840 | `worldspawn` | 18496 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1841 | `worldspawn` | 18505 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1842 | `worldspawn` | 18514 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1843 | `worldspawn` | 18523 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1844 | `worldspawn` | 18532 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1845 | `worldspawn` | 18541 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1846 | `worldspawn` | 18551 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1847 | `worldspawn` | 18561 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1848 | `worldspawn` | 18571 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1849 | `worldspawn` | 18581 | `common/hint` | `omit:confident_non_solid_utility` | `allow` | `omitted` | omitted: confident_non_solid_utility |
| 0 | 1850 | `worldspawn` | 18591 | `common/hint` | `omit:confident_non_solid_utility` | `allow` | `omitted` | omitted: confident_non_solid_utility |
| 0 | 1851 | `worldspawn` | 18601 | `common/hint` | `omit:confident_non_solid_utility` | `allow` | `omitted` | omitted: confident_non_solid_utility |
| 0 | 1852 | `worldspawn` | 18611 | `common/hint` | `omit:confident_non_solid_utility` | `allow` | `omitted` | omitted: confident_non_solid_utility |
| 0 | 1853 | `worldspawn` | 18621 | `common/hint` | `omit:confident_non_solid_utility` | `allow` | `omitted` | omitted: confident_non_solid_utility |
| 0 | 1854 | `worldspawn` | 18631 | `common/hint` | `omit:confident_non_solid_utility` | `allow` | `omitted` | omitted: confident_non_solid_utility |
| 0 | 1855 | `worldspawn` | 18641 | `common/hint` | `omit:confident_non_solid_utility` | `allow` | `omitted` | omitted: confident_non_solid_utility |
| 0 | 1856 | `worldspawn` | 18651 | `common/hint` | `omit:confident_non_solid_utility` | `allow` | `omitted` | omitted: confident_non_solid_utility |
| 0 | 1857 | `worldspawn` | 18661 | `common/hint` | `omit:confident_non_solid_utility` | `allow` | `omitted` | omitted: confident_non_solid_utility |
| 0 | 1858 | `worldspawn` | 18671 | `common/hint` | `omit:confident_non_solid_utility` | `allow` | `omitted` | omitted: confident_non_solid_utility |
| 0 | 1859 | `worldspawn` | 18681 | `common/hint` | `omit:confident_non_solid_utility` | `allow` | `omitted` | omitted: confident_non_solid_utility |
| 0 | 1860 | `worldspawn` | 18691 | `common/hint` | `omit:confident_non_solid_utility` | `allow` | `omitted` | omitted: confident_non_solid_utility |
| 0 | 1861 | `worldspawn` | 18701 | `common/hint` | `omit:confident_non_solid_utility` | `allow` | `omitted` | omitted: confident_non_solid_utility |
| 0 | 1862 | `worldspawn` | 18711 | `common/hint` | `omit:confident_non_solid_utility` | `allow` | `omitted` | omitted: confident_non_solid_utility |
| 0 | 1863 | `worldspawn` | 18721 | `common/hint` | `omit:confident_non_solid_utility` | `allow` | `omitted` | omitted: confident_non_solid_utility |
| 0 | 1864 | `worldspawn` | 18731 | `common/hint` | `omit:confident_non_solid_utility` | `allow` | `omitted` | omitted: confident_non_solid_utility |
| 0 | 1865 | `worldspawn` | 18741 | `common/hint` | `omit:confident_non_solid_utility` | `allow` | `omitted` | omitted: confident_non_solid_utility |
| 0 | 1866 | `worldspawn` | 18751 | `common/hint` | `omit:confident_non_solid_utility` | `allow` | `omitted` | omitted: confident_non_solid_utility |
| 0 | 1867 | `worldspawn` | 18761 | `common/hint` | `omit:confident_non_solid_utility` | `allow` | `omitted` | omitted: confident_non_solid_utility |
| 0 | 1868 | `worldspawn` | 18771 | `common/hint` | `omit:confident_non_solid_utility` | `allow` | `omitted` | omitted: confident_non_solid_utility |
| 0 | 1869 | `worldspawn` | 18781 | `common/hint` | `omit:confident_non_solid_utility` | `allow` | `omitted` | omitted: confident_non_solid_utility |
| 0 | 1870 | `worldspawn` | 18791 | `common/hint` | `omit:confident_non_solid_utility` | `allow` | `omitted` | omitted: confident_non_solid_utility |
| 0 | 1871 | `worldspawn` | 18801 | `common/hint` | `omit:confident_non_solid_utility` | `allow` | `omitted` | omitted: confident_non_solid_utility |
| 0 | 1872 | `worldspawn` | 18811 | `common/hint` | `omit:confident_non_solid_utility` | `allow` | `omitted` | omitted: confident_non_solid_utility |
| 0 | 1873 | `worldspawn` | 18821 | `common/hint` | `omit:confident_non_solid_utility` | `allow` | `omitted` | omitted: confident_non_solid_utility |
| 0 | 1874 | `worldspawn` | 18831 | `common/hint` | `omit:confident_non_solid_utility` | `allow` | `omitted` | omitted: confident_non_solid_utility |
| 0 | 1875 | `worldspawn` | 18841 | `common/hint` | `omit:confident_non_solid_utility` | `allow` | `omitted` | omitted: confident_non_solid_utility |
| 0 | 1876 | `worldspawn` | 18851 | `common/hint` | `omit:confident_non_solid_utility` | `allow` | `omitted` | omitted: confident_non_solid_utility |
| 0 | 1877 | `worldspawn` | 18861 | `common/hint` | `omit:confident_non_solid_utility` | `allow` | `omitted` | omitted: confident_non_solid_utility |
| 0 | 1878 | `worldspawn` | 18871 | `common/hint` | `omit:confident_non_solid_utility` | `allow` | `omitted` | omitted: confident_non_solid_utility |
| 0 | 1879 | `worldspawn` | 18881 | `common/hint` | `omit:confident_non_solid_utility` | `allow` | `omitted` | omitted: confident_non_solid_utility |
| 0 | 1880 | `worldspawn` | 18891 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1881 | `worldspawn` | 18901 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1882 | `worldspawn` | 18911 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1883 | `worldspawn` | 18921 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1884 | `worldspawn` | 18931 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1885 | `worldspawn` | 18941 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1886 | `worldspawn` | 18951 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1887 | `worldspawn` | 18961 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1888 | `worldspawn` | 18971 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1889 | `worldspawn` | 18981 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1890 | `worldspawn` | 18991 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1891 | `worldspawn` | 19001 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1892 | `worldspawn` | 19011 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1893 | `worldspawn` | 19021 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1894 | `worldspawn` | 19031 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1895 | `worldspawn` | 19041 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1896 | `worldspawn` | 19051 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1897 | `worldspawn` | 19061 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1898 | `worldspawn` | 19071 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1899 | `worldspawn` | 19081 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1900 | `worldspawn` | 19091 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1901 | `worldspawn` | 19101 | `common/nodrop` | `omit:confident_non_solid_utility` | `allow` | `omitted` | omitted: confident_non_solid_utility |
| 0 | 1902 | `worldspawn` | 19111 | `sfx/hellfog` | `omit:fog_volume` | `allow` | `omitted` | omitted: fog_volume |
| 0 | 1903 | `worldspawn` | 19121 | `common/nodraw, sfx/flame1_hell` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1904 | `worldspawn` | 19131 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1905 | `worldspawn` | 19141 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1906 | `worldspawn` | 19151 | `sfx/hellfog` | `omit:fog_volume` | `allow` | `omitted` | omitted: fog_volume |
| 0 | 1907 | `worldspawn` | 19161 | `common/nodrop` | `omit:confident_non_solid_utility` | `allow` | `omitted` | omitted: confident_non_solid_utility |
| 0 | 1908 | `worldspawn` | 19171 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1909 | `worldspawn` | 19181 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1910 | `worldspawn` | 19191 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1911 | `worldspawn` | 19201 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1912 | `worldspawn` | 19211 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1913 | `worldspawn` | 19221 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1914 | `worldspawn` | 19231 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1915 | `worldspawn` | 19241 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1916 | `worldspawn` | 19251 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1917 | `worldspawn` | 19261 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1918 | `worldspawn` | 19271 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1919 | `worldspawn` | 19281 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1920 | `worldspawn` | 19291 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1921 | `worldspawn` | 19301 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1922 | `worldspawn` | 19311 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1923 | `worldspawn` | 19321 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1924 | `worldspawn` | 19331 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1925 | `worldspawn` | 19341 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1926 | `worldspawn` | 19351 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1927 | `worldspawn` | 19361 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1928 | `worldspawn` | 19371 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1929 | `worldspawn` | 19383 | `common/nodrop` | `omit:confident_non_solid_utility` | `allow` | `omitted` | omitted: confident_non_solid_utility |
| 0 | 1930 | `worldspawn` | 19393 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1931 | `worldspawn` | 19403 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1932 | `worldspawn` | 19413 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1933 | `worldspawn` | 19425 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1934 | `worldspawn` | 19435 | `base_floor/pjgrate2, common/caulk, common/nodraw` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1935 | `worldspawn` | 19445 | `base_floor/pjgrate2, common/caulk, common/nodraw` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1936 | `worldspawn` | 19455 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1937 | `worldspawn` | 19465 | `base_floor/pjgrate2, common/caulk, common/nodraw` | `visible_solid` | `allow` | `visible_solid` | emitted |
| 0 | 1938 | `worldspawn` | 19475 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1939 | `worldspawn` | 19485 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1940 | `worldspawn` | 19495 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1941 | `worldspawn` | 19505 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1942 | `worldspawn` | 19516 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1943 | `worldspawn` | 19527 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1944 | `worldspawn` | 19537 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1945 | `worldspawn` | 19547 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1946 | `worldspawn` | 19557 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1947 | `worldspawn` | 19567 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1948 | `worldspawn` | 19577 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1949 | `worldspawn` | 19587 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1950 | `worldspawn` | 19597 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1951 | `worldspawn` | 19607 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1952 | `worldspawn` | 19617 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1953 | `worldspawn` | 19627 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1954 | `worldspawn` | 19637 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1955 | `worldspawn` | 19647 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1956 | `worldspawn` | 19657 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1957 | `worldspawn` | 19667 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1958 | `worldspawn` | 19677 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1959 | `worldspawn` | 19687 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1960 | `worldspawn` | 19697 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1961 | `worldspawn` | 19707 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1962 | `worldspawn` | 19717 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1963 | `worldspawn` | 19727 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1964 | `worldspawn` | 19737 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1965 | `worldspawn` | 19747 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1966 | `worldspawn` | 19757 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1967 | `worldspawn` | 19767 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1968 | `worldspawn` | 19777 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1969 | `worldspawn` | 19787 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1970 | `worldspawn` | 19797 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1971 | `worldspawn` | 19807 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1972 | `worldspawn` | 19817 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |
| 0 | 1973 | `worldspawn` | 19827 | `common/clip` | `playerclip` | `allow` | `playerclip` | emitted |

The converter did not truncate the candidate. The checked-in adaptation deliberately selects its authored spawns, assigns original LG materials by named source-shader roles, and reconstructs only its listed patches; remaining unsupported content stays explicit below.

Patch policy: Only adaptation-selected patches are reconstructed as thin deterministic convex prisms; all others remain omitted.

Shader policy: Source shader names map by checked-in adaptation roles to original LG textures.

Collision material policy: All-common/clip and common/playerclip brushes emit common/playerclip; all-common/weapclip brushes remain common/weapclip. LG-Duel currently traces both conservatively as collision.

Volume material policy: All-sfx/hellfog brushes are omitted atmospheric volumes and emit neither render nor collision geometry.

Scale policy: No rescale or rebalance is applied. Authored Quake coordinates are preserved; LG-Duel's existing loader converts them at 1/40 and bounds gain 40 Quake units of padding.

## Omitted content

- Patches: 268
- Brushes `confident_non_solid_utility`: 34
- Brushes `fog_volume`: 3
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
- Entities `target_location:unsupported`: 34
- Entities `target_speaker:unsupported`: 32
- Entities `team_CTF_blueplayer:unsupported`: 8
- Entities `team_CTF_redplayer:unsupported`: 9
- Entities `team_dom_point:unsupported`: 3
- Entities `trigger_hurt:unsupported`: 3
- Entities `trigger_multiple:unsupported`: 3
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
