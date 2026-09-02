# LG Duel Male Duelist Handoff

Detta paket innehåller den aktuella spelarmodellen och arbetskontexten för att fortsätta på en annan dator.

## Viktiga filer

- `art/blender/lg_duelist_male.blend`  
  Blender-källfilen. Fortsätt helst härifrån.

- `art/exports/lg_duelist_male.glb`  
  Senaste GLB-exporten med mesh, armature och `weapon_socket_r`.

- `art/previews/*.png`  
  Kontrollbilder från front, sida, 3/4, head closeup och head profile.

- `art/previews/lg_duelist_male_report.json`  
  Maskinläsbar sammanfattning av senaste export.

- `reference/tf2_head_style_reference.png`  
  Referensbild för huvudets formspråk: stark brow, bred käke, kort hår, enkel stiliserad profil.

- `reference/profile_issue_reference.png`  
  Screenshot från problemet som skulle fixas: tidigare profil hade dålig siluett och flytande ansiktsdelar.

## Nuvarande status

Senaste revision: `v7 profile-first head fix`.

Målet med v7 var att fixa profilen:

- Inga flytande face-planes i sidovy.
- Näsa, mun och haka är mer inbyggda i huvudets profil.
- Bakhuvudet är trimmat jämfört med tidigare version.
- Fronten är fortfarande ungefär i rätt riktning, men blev lite bred/platt.

Stats från senaste rapport:

- Triangles: cirka `1374`
- Höjd: cirka `1.72` Blender units
- Fötter på `Z = 0`
- Modellen vänder mot Blender `-Y`
- Mesh: `LGDuelist_Male_Body`
- Armature: `LGDuelist_Male_Armature`
- `weapon_socket_r`: finns

## Material

Aktuella material:

- `MAT_Skin`
- `MAT_SkinShadow`
- `MAT_Hair`
- `MAT_ClothPrimary`
- `MAT_ClothAccent`
- `MAT_BootsGear`
- `MAT_EyeWhite`

Materialen är flat/stylized. Inga bildtexturer används ännu.

## Rigg

Ben i armature:

- `root`
- `pelvis`
- `spine_01`
- `spine_02`
- `neck`
- `head`
- `upper_arm_l`
- `lower_arm_l`
- `hand_l`
- `upper_arm_r`
- `lower_arm_r`
- `hand_r`
- `weapon_socket_r`
- `thigh_l`
- `calf_l`
- `foot_l`
- `thigh_r`
- `calf_r`
- `foot_r`

Det finns även en enkel preview action: `weapon_hold_preview`.

## Kända brister

Var självkritisk här:

- Profilen är bättre än v6, men fronten blev för bred/platt.
- Huvudets hår/brow kan sänkas och formas bättre.
- Ansiktet behöver fortfarande kännas mer som en sammanhängande modell, mindre som enkla byggplan.
- Torson är fortfarande ganska grafisk, men den är mindre än tidigare.
- Armarna är fixade så de inte krokar bakåt i sidovy, men kan fortfarande behöva mer anatomisk form.

## Rekommenderat workflow på andra datorn

För att undvika Blender-frys och få bättre feedback:

1. Öppna `art/blender/lg_duelist_male.blend`.
2. Iterera i små steg, helst bara huvud/profil först.
3. Använd `Workbench` eller `Material Preview`, inte Cycles, under iteration.
4. Ta viewport screenshots i låg upplösning.
5. Exportera GLB först när formen är godkänd.
6. Undvik att rebuilda hela modellen med script varje gång.

Nästa bra manuella steg:

- Smalna huvudets front något.
- Behåll den bättre sidoprofilen.
- Sänk/forma hår- och brow-volymen.
- Gör näsa/mun/haka till renare meshformer.
- Kontrollera med front, profil och 3/4 innan export.

