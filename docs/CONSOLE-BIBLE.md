# CONSOLE-BIBLE

Komplett referens för LG Duel-klienten, klientkonsolen, dedikerade servern,
serverkonsolen, bindings och startargument.

Dokumentet beskriver implementationen i den aktuella kodbasen och ska
uppdateras när ett cvar, kommando, intervall eller standardvärde ändras.

## 1. Grundsyntax

Konsolen är skiftlägeskänslig för cvar- och kommandonamn. Använd namnen exakt
som de skrivs i detta dokument.

```text
<cvar>
<cvar> <värde>
set <cvar>
set <cvar> <värde>
toggle <bool-cvar>
reset <cvar>
help <cvar|kommando>
cvarlist
cmdlist
```

`<cvar>` eller `set <cvar>` visar:

```text
namn = aktuellt_värde (default projektdefault, Q3/QL default referens)
```

Q3/QL-delen visas bara för cvars där projektet registrerar en uttrycklig
referens.

### Datatyper

| Typ | Giltig syntax |
|---|---|
| `bool` | `1`, `true`, `on`, `0`, `false`, `off` |
| `int` | Heltal, exempelvis `0`, `2`, `255` |
| `float` | Ändligt decimaltal, exempelvis `0.12`, `8`, `1.5` |
| `string` | Text. Citattecken kan gruppera mellanslag vid tokenisering. |
| `host` | Värdnamn, IPv4- eller annan sträng som UDP-transporten kan använda. |
| `port` | Heltal för UDP-port. Se respektive startkommando. |

Intervall är inklusiva. Ett värde exakt på min- eller maxgränsen är giltigt.

### Lagring, defaultfiler och auktoritet

- `Arkiv`: sparas automatiskt i klientens `client.cfg`.
- `Runtime`: återställs när klienten eller servern startas om.
- `Serverstyrd`: klientens värde skickas till servern och replikeras tillbaka
  så att server och prediction använder samma värde.
- Klienten laddar `config/default_client.cfg` före användarens `client.cfg`.
  `writeconfig` skriver bara användarens arkiverade cvars och bindings.
- Servern laddar `config/server_cvars.cfg` vid start. Den filen innehåller
  serverns `sv_*`-defaults och de tillfälliga utvecklings-`g_*`-defaultsen.
- Servern laddar `config/balance.cfg` för auktoritativa gameplay-värden som
  inte är console-cvars. Klienter ska inte ladda en lokal `balance.cfg`.
- Serverkonsolens `sv_*` och `g_*` är runtime-värden och sparas inte automatiskt.

## 2. Klientstart och miljö

### Klient

```text
lg_duel_client [host] [port]
```

| Input | Typ | Default | Giltigt | Funktion |
|---|---|---:|---|---|
| `host` | string | `127.0.0.1` | Icke-tom transportadress | Server som klienten ansluter till. |
| `port` | int | `27960` | Kodens CLI-parser accepterar `0..65535` | Serverns UDP-port. Port `0` är normalt inte användbar för klientanslutning. |

### Renderer-backend

```text
LG_DUEL_RENDER_BACKEND=gpu lg_duel_client
```

| Värde | Funktion |
|---|---|
| Ej satt/annat | Använd SDL_Renderer-kompatibilitetsvägen. |
| `gpu` | Begär SDL_GPU. |
| `sdl_gpu` | Samma som `gpu`. |
| `vulkan` | Samma som `gpu`; Vulkan föredras. |

SDL_GPU försöker Vulkan först, därefter SDL:s automatiska GPU-val och till sist
SDL_Renderer om GPU-initiering misslyckas.

## 3. Klient-cvars

### 3.1 Klient, kamera och HUD

| Cvar | Typ | Default | Giltigt intervall/värden | Q3/QL-referens | Lagring | Funktion |
|---|---:|---:|---|---|---|---|
| `cl_config_version` | int | `0` | `0..100` | Ingen | Arkiv | Intern migrationsversion för klientkonfiguration. Bör normalt inte ändras manuellt. |
| `sensitivity` | float | `5` | `0..100` | Q3/QL `sensitivity 5`, `m_yaw 0.022` | Arkiv | Musens grundkänslighet på Q3/QL-skalan: `1` motsvarar `0.022` grader per rå muscount före zoom/accel. |
| `cl_mouseAccel` | float | `0` | `0..1000` | QL `cl_mouseAccel 0` | Arkiv | QL-style musacceleration. `0` stänger av acceleration. |
| `cl_mouseAccelPower` | float | `2` | `1..10` | QL `cl_mouseAccelPower 2` | Arkiv | Exponenten i QL-formeln. `2` ger klassisk linjär ökning av effektiv känslighet med mushastighet. |
| `cl_mouseAccelOffset` | float | `0` | `0..1000` | QL `cl_mouseAccelOffset 0` | Arkiv | Mushastighet i counts/ms innan acceleration börjar. Hastigheter under offset behåller baskänsligheten. |
| `cl_mouseSensCap` | float | `0` | `0..100` | QL `cl_mouseSensCap 0` | Arkiv | Tak för accelererad känslighet. `0` betyder inget tak. |
| `cl_fov` | float | `90` | `45..140` | Q3/QL FOV-baseline `90` | Arkiv | First-person field of view. |
| `cl_zoom_fov` | float | `45` | `20..140` | Q3 `cg_zoomfov 22.5`, men projektet använder egen baseline | Arkiv | Field of view medan `+zoom` hålls. Påverkar bara klientens vy/aimberäkning, inte simulation eller server. |
| `cl_zoom_sensitivity` | float | `0` | `0..10` | Ingen direkt | Arkiv | First-person sensitivity multiplier while `+zoom` is held. `0` auto-matches the FOV ratio. |
| `cl_health_size` | float | `2` | `0.5..20` | Ingen | Arkiv | Skala för HP-HUD:en. |
| `cl_health_style` | int | `0` | `0..2` | Ingen | Arkiv | HP-HUD: `0` bottom-left bar, `1` centrerad HP-siffra med dynamisk färg, `2` crosshair-nära HP vänster och ammo höger. |
| `cl_speed_size` | float | `1.5` | `0.5..6` | Ingen | Arkiv | Textskala för speed-indikatorn under crosshair. |
| `cl_showfps` | bool | `0` | bool | Ingen | Arkiv | Visar FPS, genomsnittlig frame time och renderer-backend i fönstertiteln. |
| `cl_showspeed` | bool | `1` | bool | Q3/QL-style UPS | Arkiv | Visar horisontell predicted speed under crosshair som `<värde> ups`. Intern hastighet multipliceras med `40`, så `8 = 320 ups`. |
| `cl_show_net` | bool | `1` | bool | Ingen | Arkiv | Visar ping, ticks, command ack, rewind, prediction och overload i titeln. |
| `net_sim_latency_ms` | int | `0` | `0..5000` | Ingen | Nej | Lokal klient-UDP-simulator: extra one-way latency i ms efter connect. `60` pa bade outgoing och incoming ger ungefar +120 ms RTT. |
| `net_sim_jitter_ms` | int | `0` | `0..5000` | Ingen | Nej | Lokal klient-UDP-simulator: slumpad one-way variation runt `net_sim_latency_ms` per datagram. Delay clampas till minst `0`. |
| `net_sim_loss_percent` | int | `0` | `0..100` | Ingen | Nej | Lokal klient-UDP-simulator: oberoende sannolikhet per datagram att droppas. |
| `net_sim_reorder_percent` | int | `0` | `0..100` | Ingen | Nej | Lokal klient-UDP-simulator: probabilistisk transport-reordering. Valda datagram far en liten extra hold sa senare datagram kan ga fore; packet bytes och protocol state andras inte. Effekten beror pa faktisk packet cadence. |
| `net_sim_seed` | int | `0` | `0..2147483647` | Ingen | Nej | Lokal klient-UDP-simulator: RNG-seed for reproducerbara loss/jitter/reorder-fall. `0` anvander fast default-seed. |
| `cl_show_lagcomp` | bool | `0` | bool | Ingen | Arkiv | Visar riktig rewind-data när den används, annars att lag compensation inte används. |
| `cl_show_alive_counts` | bool | `0` | bool | Ingen | Arkiv | Visar antal levande röda och blå spelare på HUD:en i Clan Arena. Kan växlas med `toggle cl_show_alive_counts`. |
| `cl_interp_mode` | int | `1` | `0..1` | Ingen | Arkiv | Remote interpolation mode. `0`: legacy senaste snapshot-par + lokal render-alpha och gammal viewed tick. `1`: buffrad interpolation med `cl_interp`. |
| `cl_interp` | float | `0.024` | `0..0.25` | 3 ticks vid 125 Hz | Arkiv | Remote player snapshot interpolation delay i sekunder. Lägre värde minskar visuell latency men kräver jämnare snapshots; högre värde döljer jitter bättre. |

### 3.2 Ljud

| Cvar | Typ | Default | Giltigt | Q3/QL-referens | Lagring | Funktion |
|---|---:|---:|---|---|---|---|
| `s_enable` | bool | `1` | bool | Ingen direkt | Arkiv | Slår av/på klientens ljudeffekter. |
| `s_volume` | float | `0.35` | `0..1` | Ingen direkt | Arkiv | Volym för hit-, countdown- och round-ljud. |
| `s_footstep_volume` | float | `0.45` | `0..1` | Ingen direkt | Arkiv | Separat fotstegsvolym. Multipliceras med `s_volume`. |

### 3.3 Serverstyrd movement och gameplay

Dessa cvars kan skrivas i serverkonsolen och defaultas från
`config/server_cvars.cfg`. Under utveckling kan de också skrivas i
klientkonsolen och skickas till den auktoritativa servern. De gäller
symmetriskt för båda spelarna och replikeras tillbaka till klienterna.
Klientens `g_*`-värden arkiveras inte.

Projektets rörelseskala är `1 intern enhet = 40 Q3/QL units`.

| Cvar | Typ | Projektdefault | Giltigt | Q3/QL-default eller ekvivalent | Funktion |
|---|---:|---:|---|---|---|
| `g_accel` | float | `10` | `0..1000` | `pm_accelerate 10` | Markacceleration mot `g_maxspeed`. |
| `g_airaccel` | float | `1` | `0..1000` | `pm_airaccelerate 1` | Acceleration i luften. |
| `g_aircontrol` | bool | `0` | bool | Q3/QL: `0`, QW-style: `1` | Vaxlar extra air control. `0` behaller Q3/QL-kansla utan QuakeWorld-lik styrning i luften. `1` later forward-input vrida horisontell luftfart mot siktriktningen utan att direkt ge gratis fart. |
| `g_friction` | float | `6` | `0..100` | `pm_friction 6` | Friktion när spelaren är grounded. |
| `g_stopspeed` | float | `2.5` | `0..100` | `pm_stopspeed 100`, motsvarar `2.5` internt | Minsta kontrollhastighet i friktionsberäkningen. |
| `g_maxspeed` | float | `8` | `0.1..100` | `g_speed 320`, motsvarar `8` internt | Sustained mark- och air-speed cap. |
| `g_dash_targetspeed` | float | `11.5` | `0..100` | `460 UPS`, internal `11.5` | Dash target speed along the locked input direction. |
| `g_dash_maxspeed` | float | `12.5` | `0..100` | `500 UPS`, internal `12.5` | Cap for speed created by dash. Existing speed above this cap is preserved. |
| `g_dash_accel` | float | `200` | `0..1000` | `8000 UPS/s`, internal `200` | Dash acceleration during the active dash window. |
| `g_dash_duration` | float | `0.10` | `0..2` | No Q3/QL equivalent | Seconds of active dash acceleration after dash starts. |
| `g_dash_cooldown` | float | `0.85` | `0..10` | No Q3/QL equivalent | Seconds before dash can be started again. |
| `g_dash_groundhop` | float | `3.25` | `0..100` | `130 UPS`, internal `3.25` | Minimum vertical velocity for a grounded dash hop. Uses max, not addition. |
| `g_dash_airhop` | float | `1.875` | `0..100` | `75 UPS`, internal `1.875` | Minimum vertical velocity for airborne dash correction. Uses max, not addition. |
| `g_lg_knockback` | float | `1000` | `0..100000` | Q3 `g_knockback 1000`, motsvarar `22` internt | LG-knockback per sekund. Skalas om linjärt så `0` motsvarar gamla `682`, `500` gamla `841`, och `1000` gamla `1000`. |
| `g_lg_fire_hz` | float | `20` | `1..125` | Ingen direkt stabil cvar | Authoritative LG damage/knockback and FG damage/freeze instances per second. Default 20 Hz gives 6 damage per instance with `g_lg_damage 120` or `g_fg_damage 120`; FG freeze amount comes from `balance.cfg`. |
| `g_rl_knockback` | float | `1000` | `0..1000` | Q3 `g_knockback 1000`, motsvarar `22` internt | RL-knockback per explosion, skalad med splash-damage. |
| `g_knockback_time_ms` | int | `100` | `0..250` | Q3-style knockback movement timer | Antal millisekunder som grounded knockback anvander air movement utan ground friction. `0` stanger av speciallaget men behaller damage och direkt knockback. |
| `g_fg_damage` | int | `120` | `1..500` | Ingen standardmekanik | Authoritative freeze gun damage per second, distributed over `g_lg_fire_hz` instances. Does not add FG knockback. |
| `g_vampirism` | float | `0` | `0..2` | Ingen standardmekanik | Healing som multipel av utdelad skada. `0.1 = 10%`, `1 = 100%`, `2 = 200%`. Fraktioner ackumuleras och avrundas när helt HP kan delas ut. |
| `g_selfdamage` | float | `100` | `0..100` | `100` | Procent av egen splash-damage som appliceras. Värdet rundas till närmaste heltal innan det skickas till servern. |
| `g_healthamount` | int | `100` | `1..100000` | `100` | HP som varje spelare startar med vid spawn, rundstart och warmup-respawn. |
| `g_flight` | bool | `0` | bool | Ingen direkt LG-duel-motsvarighet | Aktiverar obegränsad flight för båda spelarna. |
| `g_flightaccel` | float | `32` | `0..1000` | Ingen direkt | Acceleration/thrust under flight. |
| `g_flightmaxspeed` | float | `12` | `0.1..100` | Ingen direkt | Maximal flight-hastighet. `12` motsvarar `480 UPS`. |
| `g_flightdamping` | float | `2` | `0..100` | Ingen direkt | Dämpning av flight-velocity utan thrust. |
| `g_playersize_xy` | float | `1` | `0.5..3` | Ingen direkt | Skalar båda spelarnas auktoritativa radie/hitbox i X/Y. |
| `g_playersize_z` | float | `1` | `0.5..3` | Ingen direkt | Skalar båda spelarnas auktoritativa höjd/hitbox i Z. |
| `cl_player_name` | string | empty | `0..20` bytes | Ingen direkt | Archived local player name. `player <name>` writes this cvar and sends the name to the server. |

### 3.4 Freeze Gun balance keys

These values live in server-authoritative `config/balance.cfg`, not in the
console cvar registry. The Freeze Gun fires a hitscan beam at `g_lg_fire_hz`,
applies damage from `g_fg_damage` with no knockback, and builds target-owned freeze on a `0..100`
scale. Freeze decays every server tick. The movement slow is linear: with the
default `weapon.fg.max_slow_fraction 0.4`, `100` freeze is a 40% all-axis slow
and `50` freeze is a 20% all-axis slow. Shooting walkable world surfaces creates
temporary ice pools. Pools lower local ground friction for everyone, apply a
downslope slide force on ramps, and do not add freeze level by themselves.

| Key | Default | Valid range | Function |
|---|---:|---|---|
| `weapon.fg.range` | `18` | `0.1..1000` | Hitscan beam range. |
| `weapon.fg.eye_height` | `0.65` | `0..10` | Beam muzzle height relative to player height. |
| `weapon.fg.freeze_per_second` | `50` | `0..1000` | Freeze level added per second of confirmed hits. Multiple attackers add independently to the target's level. |
| `weapon.fg.decay_per_second` | `20` | `0..1000` | Freeze level removed per second while the player is alive. |
| `weapon.fg.max_slow_fraction` | `0.4` | `0..0.95` | Slow fraction at full freeze. |
| `weapon.fg.spawn_ammo` | `150` | `0..999` | Spawn ammo when `g_infiniteammo 0`. |
| `weapon.fg.ice_pool_max_radius` | `2.4` | `0..100` | Maximum radius for one merged ice pool spot, in world units. |
| `weapon.fg.ice_pool_growth_per_second` | `10` | `0..1000` | Asymptotic pool growth rate while FG keeps hitting the same spot. Higher values reach max radius faster. |
| `weapon.fg.ice_pool_lifetime_seconds` | `3` | `0..60` | Seconds before an ice pool expires after its last meaningful FG contact. |
| `weapon.fg.ice_pool_friction` | `1` | `0..100` | Local grounded friction while standing on an ice pool, replacing `g_friction` only for that contact. |
| `weapon.fg.ice_pool_slope_gravity_scale` | `1` | `0..10` | Multiplier for gravity projected down icy ramps. `0` disables the extra downhill pull. |
| `weapon.fg.ice_pool_control_scale` | `0.35` | `0..1` | Ground acceleration multiplier while standing on ice. Lower values make uphill recovery harder. |
| `weapon.fg.ice_pool_merge_distance` | `1` | `0..100` | Extra distance used when deciding whether a new floor hit grows an existing pool instead of creating another. |

### 3.5 Crosshair

| Cvar | Typ | Default | Giltigt | Q3/QL-referens | Funktion |
|---|---:|---:|---|---|---|
| `crosshair` | bool | `1` | bool | Q3 `cg_drawCrosshair 4`; Q3-värdet väljer även grafik | Visa crosshair. |
| `crosshair_style` | int | `0` | `0..2` | Ingen 1:1-indexering | `0`: cross. `1`: cross + dot. `2`: dot. |
| `crosshair_size` | float | `8` | `1..40` | Q3 `cg_crosshairSize 24` | Armlängd i pixlar. Geometrin skiljer sig från Q3-grafiken. |
| `crosshair_width` | float | `2` | `1..10` | Ingen direkt | Crosshair line width in pixels. |
| `crosshair_gap` | float | `3` | `0..30` | Ingen direkt | Avstånd från centrum till armar. |
| `crosshair_dot` | bool | `0` | bool | Ingen direkt | Draw a center dot over any crosshair style. |
| `crosshair_dot_width` | float | `2` | `1..20` | Ingen direkt | Center dot size in pixels. |
| `crosshair_outline` | bool | `0` | bool | Ingen direkt | Draw a black outline behind the crosshair and dot. |
| `crosshair_outline_width` | float | `1` | `0..10` | Ingen direkt | Crosshair outline width in pixels. |
| `crosshair_alpha` | float | `1` | `0..1` | Ingen direkt standard | Opacitet. |
| `crosshair_r` | int | `255` | `0..255` | Ingen direkt standard | Röd kanal. |
| `crosshair_g` | int | `255` | `0..255` | Ingen direkt standard | Grön kanal. |
| `crosshair_b` | int | `255` | `0..255` | Ingen direkt standard | Blå kanal. |
| `crosshair_hit` | bool | `1` | bool | Ingen direkt | Aktivera färgrespons på crosshair vid träff. |
| `crosshair_hit_r` | int | `255` | `0..255` | Ingen direkt | Crosshairets träfffärg, röd. |
| `crosshair_hit_g` | int | `255` | `0..255` | Ingen direkt | Crosshairets träfffärg, grön. |
| `crosshair_hit_b` | int | `255` | `0..255` | Ingen direkt | Crosshairets träfffärg, blå. |
| `crosshair_hit_duration` | float | `0.12` | `0..2` sekunder | Ingen direkt | Hur länge träfffärgen ligger kvar. |
| `crosshair_hit_fade` | bool | `1` | bool | Ingen direkt | `1`: gradvis återgång. `0`: binär färg tills durationen löper ut. |

### 3.6 Renderer och lokal LG-beam

| Cvar | Typ | Default | Giltigt | Q3/QL-referens | Funktion |
|---|---:|---:|---|---|---|
| `r_vsync` | bool | `1` | bool | Q3 `r_swapInterval 0` | GPU: mailbox/vsync när på, immediate när av om plattformen stöder det. Projektet har alltså motsatt standard mot Q3. |
| `r_frustum_cull` | bool | `1` | bool | Ingen direkt | CPU-side konservativ frustum-culling av remote player-kroppar, vapen, geometri-outline och flytande healthbars i 3D. |
| `r_texture_filter` | int | `2` | `0..2` | Q3 `r_textureMode` närmast | World/material texture filtering. `0`: nearest. `1`: bilinear with mipmaps. `2`: trilinear with mipmaps. |
| `r_texture_anisotropy` | int | `8` | `1..16`, renderer snappar till `1/2/4/8/16` | Q3/driver aniso settings närmast | World/material anisotropic filtering level. Unsupported anisotropy disables safely with a renderer log. |
| `r_texture_lod_bias` | float | `0.5` | `-2..4` | Q3/driver LOD-bias närmast | World/material mip LOD bias. Positive values choose blurrier, more stable mip levels; changes recreate the sampler without reloading textures. |
| `r_perf` | bool | `0` | bool | Ingen direkt | Visa renderer-diagnostik pa HUD. |
| `r_perf_detail` | bool | `0` | bool | Ingen direkt | Visa detaljerad renderer-diagnostik for remote frustum-culling och geometri. |
| `r_beam_width` | float | `2` | `1..12` | Ingen direkt stabil cvar | Local LG beam width in first-person world units. |
| `r_beam_alpha` | float | `1` | `0..1` | Ingen direkt | Lokal beam-opacity. |
| `r_beam_r` | int | `74` | `0..255` | Ingen direkt standard | Lokal beam, röd kanal. |
| `r_beam_g` | int | `166` | `0..255` | Ingen direkt standard | Lokal beam, grön kanal. |
| `r_beam_b` | int | `255` | `0..255` | Ingen direkt standard | Lokal beam, blå kanal. |
| `r_beam_hit` | bool | `1` | bool | Ingen direkt | Aktivera färgrespons på lokal beam vid träff. |
| `r_beam_hit_r` | int | `255` | `0..255` | Ingen direkt | Beamens träfffärg, röd. |
| `r_beam_hit_g` | int | `255` | `0..255` | Ingen direkt | Beamens träfffärg, grön. |
| `r_beam_hit_b` | int | `255` | `0..255` | Ingen direkt | Beamens träfffärg, blå. |
| `r_beam_hit_duration` | float | `0.12` | `0..2` sekunder | Ingen direkt | Hur länge träfffärgen ligger kvar. |
| `r_beam_hit_fade` | bool | `1` | bool | Ingen direkt | `1`: gradvis återgång. `0`: binär färg tills durationen löper ut. |

Beamens minimala pulsanimation är presentationsstyrd: fasta endpoints, cirka
`±4%` bredd och `±5%` ljusstyrka. Den påverkar inte simulation eller aim.

### 3.7 Motståndarens beam

| Cvar | Typ | Default | Giltigt | Q3/QL-referens | Funktion |
|---|---:|---:|---|---|---|
| `r_enemy_beam_width` | float | `2` | `1..12` | Ingen direkt | Motståndarens beam-bredd. |
| `r_enemy_beam_alpha` | float | `1` | `0..1` | Ingen direkt | Motståndarens beam-opacity. |
| `r_enemy_beam_r` | int | `255` | `0..255` | Ingen direkt | Röd kanal. |
| `r_enemy_beam_g` | int | `110` | `0..255` | Ingen direkt | Grön kanal. |
| `r_enemy_beam_b` | int | `80` | `0..255` | Ingen direkt | Blå kanal. |

### 3.8 Hitmarker

| Cvar | Typ | Default | Giltigt | Q3/QL-referens | Funktion |
|---|---:|---:|---|---|---|
| `r_hitmarker` | bool | `1` | bool | Ingen standard-Q3-motsvarighet | Visa center-screen hitmarker. |
| `r_hitmarker_duration` | float | `0.12` | `0..2` sekunder | Ingen | Synlig tid. |
| `r_hitmarker_size` | float | `10` | `2..40` | Ingen | Armlängd i pixlar. |
| `r_hitmarker_width` | float | `2` | `1..10` | Ingen | Hitmarker line width in pixels. |
| `r_hitmarker_r` | int | `255` | `0..255` | Ingen | Röd kanal. |
| `r_hitmarker_g` | int | `255` | `0..255` | Ingen | Grön kanal. |
| `r_hitmarker_b` | int | `255` | `0..255` | Ingen | Blå kanal. |
| `r_damage_numbers_mode` | int | `0` | `0..4` | Diabotical/arena-shooter damage feedback | Archived damage-number mode. `0`: off. `1`: one number per confirmed local damage instance. `2`: per-instance numbers plus one immediate cumulative burst tally per target. `3`: cumulative burst tally only. `4`: cumulative burst tally only, projected at the target's latest damage-event world position. |
| `r_damage_numbers_window` | float | `0.4` | `0..2` seconds | Ingen direkt | Seconds without qualifying local damage before a target's cumulative tally expires and the next hit starts a new burst. |
| `r_damage_numbers_duration` | float | `0.65` | `0.05..3` seconds | Ingen direkt | Visual lifetime of each individual damage number. |
| `r_damage_numbers_size` | float | `1.6` | `0.5..6` | Ingen direkt | Damage-number font scale. |
| `r_damage_numbers_alpha` | float | `1` | `0..1` | Ingen direkt | Damage-number opacity. |
| `r_damage_numbers_r` | int | `255` | `0..255` | Ingen direkt | Damage-number red channel. |
| `r_damage_numbers_g` | int | `236` | `0..255` | Ingen direkt | Damage-number green channel. |
| `r_damage_numbers_b` | int | `128` | `0..255` | Ingen direkt | Damage-number blue channel. |
| `r_damage_numbers_offset_x` | float | `0` | `-400..400` | Ingen direkt | Horizontal screen offset from the crosshair or aim point. |
| `r_damage_numbers_offset_y` | float | `-46` | `-400..400` | Ingen direkt | Vertical screen offset from the crosshair or aim point. |

### 3.9 Motståndarmodell och träfffärg

| Cvar | Typ | Default | Giltigt | Q3/QL-referens | Funktion |
|---|---:|---:|---|---|---|
| `r_enemy_r` | int | `224` | `0..255` | Närmaste koncept: enemy color/forced model | Modellens rödkanal. |
| `r_enemy_g` | int | `82` | `0..255` | Ingen exakt 1:1-default | Modellens grönkanal. |
| `r_enemy_b` | int | `92` | `0..255` | Ingen exakt 1:1-default | Modellens blåkanal. |
| `r_enemy_alpha` | float | `1` | `0..1` | Ingen direkt | Modellens opacity. |
| `r_player_outline_style` | int | `0` | `0..1` | Ingen direkt | Shared player outline implementation selector. `0`: legacy geometry-expanded fallback. `1`: SDL_GPU half-resolution screen-space mask/dilation/composite path. |
| `r_enemy_outline` | bool | `1` | bool | Ingen direkt | Draw enemy model outline in first-person 3D. |
| `r_enemy_outline_width` | float | `0.045` | `0..6` | Ingen direkt | Outline width in final display pixels for screen-space style `1`; legacy style `0` keeps approximate geometry fallback scaling. Intended normal range `1..6`. |
| `r_enemy_outline_alpha` | float | `1` | `0..1` | Ingen direkt | Enemy outline opacity. |
| `r_enemy_outline_r` | int | `255` | `0..255` | Ingen direkt | Enemy outline red channel. |
| `r_enemy_outline_g` | int | `220` | `0..255` | Ingen direkt | Enemy outline green channel. |
| `r_enemy_outline_b` | int | `84` | `0..255` | Ingen direkt | Enemy outline blue channel. |
| `r_enemy_lean` | bool | `1` | bool | Q3 `cg_runroll`-inspirerad model lean | Slår på/av velocity lean för motståndarmodellen i 3D. Påverkar bara renderad modell, inte lokal POV, simulation, aim, hitboxar eller nätkod. |
| `r_enemy_lean_scale` | float | `1` | `0..3` | Q3 `cg_runroll 0.005` | Multiplikator för motståndarmodellens velocity lean. `1` motsvarar ungefär Q3-standard, `0` ger ingen lean även om `r_enemy_lean` är på. |
| `r_enemy_hit` | bool | `1` | bool | Ingen direkt | Byt/blenda modellfärg vid träff. |
| `r_enemy_hit_r` | int | `255` | `0..255` | Ingen | Träfffärg röd. |
| `r_enemy_hit_g` | int | `190` | `0..255` | Ingen | Träfffärg grön. |
| `r_enemy_hit_b` | int | `198` | `0..255` | Ingen | Träfffärg blå. |
| `r_enemy_hit_duration` | float | `0.12` | `0..2` sekunder | Ingen | Träfffärgens duration. |
| `r_enemy_hit_fade` | bool | `1` | bool | Ingen | `1`: gradvis blend. `0`: binär färg. |
| `r_enemy_health` | bool | `1` | bool | Ingen direkt | Draw floating enemy health bars. |
| `r_enemy_health_damage_only` | bool | `0` | bool | Ingen direkt | Only show enemy health bars after recent damage. |
| `r_enemy_health_fade` | bool | `1` | bool | Ingen direkt | Fade enemy health bars during their damage-only duration. |
| `r_enemy_health_duration` | float | `5` | `0..30` seconds | Ingen direkt | Visible time after damage when damage-only mode is active. |
| `r_enemy_health_max_distance` | float | `0` | `0..1000` | Ingen direkt | Hide enemy health bars beyond this 3D distance; `0` disables the limit. |
| `r_enemy_health_width` | float | `72` | `12..360` | Ingen direkt | Enemy health bar width in pixels. |
| `r_enemy_health_height` | float | `7` | `2..60` | Ingen direkt | Enemy health bar height in pixels. |
| `r_enemy_health_offset_z` | float | `0.35` | `-2..6` | Ingen direkt | Enemy health bar vertical world offset above the model. |
| `r_enemy_health_offset_x` | float | `0` | `-400..400` | Ingen direkt | Enemy health bar horizontal screen offset. |
| `r_enemy_health_offset_y` | float | `-18` | `-400..400` | Ingen direkt | Enemy health bar vertical screen offset. |
| `r_enemy_health_alpha` | float | `1` | `0..1` | Ingen direkt | Enemy health bar opacity. |
| `r_enemy_health_r` | int | `224` | `0..255` | Ingen direkt | Enemy health bar red channel. |
| `r_enemy_health_g` | int | `82` | `0..255` | Ingen direkt | Enemy health bar green channel. |
| `r_enemy_health_b` | int | `92` | `0..255` | Ingen direkt | Enemy health bar blue channel. |

### 3.10 Lagkamratens utseende

Dessa cvars används bara för lagkamrater i Clan Arena. Lagkamrater har ingen
separat träfffärg eller tillhörande `r_teammate_hit_*`-cvars.

| Cvar | Typ | Default | Giltigt | Q3/QL-referens | Funktion |
|---|---:|---:|---|---|---|
| `r_teammate_beam_width` | float | `2` | `1..12` | Ingen direkt | Lagkamratens LG-beam-bredd. |
| `r_teammate_beam_alpha` | float | `1` | `0..1` | Ingen direkt | Lagkamratens LG-beam-opacity. |
| `r_teammate_beam_r` | int | `80` | `0..255` | Ingen direkt | Beamens röda kanal. |
| `r_teammate_beam_g` | int | `220` | `0..255` | Ingen direkt | Beamens gröna kanal. |
| `r_teammate_beam_b` | int | `150` | `0..255` | Ingen direkt | Beamens blå kanal. |
| `r_teammate_r` | int | `82` | `0..255` | Ingen direkt | Modellens röda kanal. |
| `r_teammate_g` | int | `190` | `0..255` | Ingen direkt | Modellens gröna kanal. |
| `r_teammate_b` | int | `224` | `0..255` | Ingen direkt | Modellens blå kanal. |
| `r_teammate_alpha` | float | `1` | `0..1` | Ingen direkt | Modellens opacity. |
| `r_teammate_outline` | bool | `1` | bool | Ingen direkt | Draw teammate model outline in first-person 3D. |
| `r_teammate_outline_width` | float | `0.045` | `0..6` | Ingen direkt | Outline width in final display pixels for screen-space style `1`; legacy style `0` keeps approximate geometry fallback scaling. Intended normal range `1..6`. |
| `r_teammate_outline_alpha` | float | `1` | `0..1` | Ingen direkt | Teammate outline opacity. |
| `r_teammate_outline_r` | int | `128` | `0..255` | Ingen direkt | Teammate outline red channel. |
| `r_teammate_outline_g` | int | `240` | `0..255` | Ingen direkt | Teammate outline green channel. |
| `r_teammate_outline_b` | int | `255` | `0..255` | Ingen direkt | Teammate outline blue channel. |
| `r_teammate_lean` | bool | `1` | bool | Q3 `cg_runroll`-inspirerad | Slår på/av velocity lean för lagkamratmodellen i 3D. |
| `r_teammate_lean_scale` | float | `1` | `0..3` | Q3 `cg_runroll 0.005` | Multiplikator för lagkamratmodellens velocity lean. |
| `r_teammate_health` | bool | `1` | bool | Ingen direkt | Visar flytande health bar över lagkamrater. |
| `r_teammate_health_damage_only` | bool | `0` | bool | Ingen direkt | Visar health bar endast efter nylig skada. |
| `r_teammate_health_fade` | bool | `1` | bool | Ingen direkt | Tonar ut health bar under damage-only-perioden. |
| `r_teammate_health_duration` | float | `5` | `0..30` sekunder | Ingen direkt | Synlig tid efter skada när damage-only används. |
| `r_teammate_health_max_distance` | float | `0` | `0..1000` | Ingen direkt | Maxavstånd i 3D; `0` stänger av avståndsgränsen. |
| `r_teammate_health_width` | float | `72` | `12..360` | Ingen direkt | Health bar-bredd i pixlar. |
| `r_teammate_health_height` | float | `7` | `2..60` | Ingen direkt | Health bar-höjd i pixlar. |
| `r_teammate_health_offset_z` | float | `0.35` | `-2..6` | Ingen direkt | Vertikal offset i världen. |
| `r_teammate_health_offset_x` | float | `0` | `-400..400` | Ingen direkt | Horisontell skärmoffset. |
| `r_teammate_health_offset_y` | float | `-18` | `-400..400` | Ingen direkt | Vertikal skärmoffset. |
| `r_teammate_health_alpha` | float | `1` | `0..1` | Ingen direkt | Health bar-opacity. |
| `r_teammate_health_r` | int | `82` | `0..255` | Ingen direkt | Health barens röda kanal. |
| `r_teammate_health_g` | int | `190` | `0..255` | Ingen direkt | Health barens gröna kanal. |
| `r_teammate_health_b` | int | `224` | `0..255` | Ingen direkt | Health barens blå kanal. |

### 3.11 Nametags

Enemy and teammate nametags are separate so Clan Arena can style friends and enemies independently. They render as first-person screen-space overlays anchored to 3D players.

| Cvar | Typ | Default | Giltigt | Q3/QL-referens | Funktion |
|---|---:|---:|---|---|---|
| `r_enemy_name` | bool | `1` | bool | Ingen direkt | Draw enemy nametags. |
| `r_enemy_name_alpha` | float | `1` | `0..1` | Ingen direkt | Enemy nametag opacity. |
| `r_enemy_name_font_size` | float | `1.5` | `0.5..6` | Ingen direkt | Enemy nametag font scale. |
| `r_enemy_name_offset_z` | float | `0.75` | `-2..6` | Ingen direkt | Enemy nametag world Z offset above the model. |
| `r_enemy_name_offset_x` | float | `0` | `-400..400` | Ingen direkt | Enemy nametag screen X offset. |
| `r_enemy_name_offset_y` | float | `-34` | `-400..400` | Ingen direkt | Enemy nametag screen Y offset. |
| `r_enemy_name_max_distance` | float | `0` | `0..1000` | Ingen direkt | Hide enemy nametags beyond this 3D distance; `0` disables the limit. |
| `r_enemy_name_r` | int | `255` | `0..255` | Ingen direkt | Enemy nametag red channel. |
| `r_enemy_name_g` | int | `235` | `0..255` | Ingen direkt | Enemy nametag green channel. |
| `r_enemy_name_b` | int | `235` | `0..255` | Ingen direkt | Enemy nametag blue channel. |
| `r_teammate_name` | bool | `1` | bool | Ingen direkt | Draw teammate nametags. |
| `r_teammate_name_alpha` | float | `1` | `0..1` | Ingen direkt | Teammate nametag opacity. |
| `r_teammate_name_font_size` | float | `1.5` | `0.5..6` | Ingen direkt | Teammate nametag font scale. |
| `r_teammate_name_offset_z` | float | `0.75` | `-2..6` | Ingen direkt | Teammate nametag world Z offset above the model. |
| `r_teammate_name_offset_x` | float | `0` | `-400..400` | Ingen direkt | Teammate nametag screen X offset. |
| `r_teammate_name_offset_y` | float | `-34` | `-400..400` | Ingen direkt | Teammate nametag screen Y offset. |
| `r_teammate_name_max_distance` | float | `0` | `0..1000` | Ingen direkt | Hide teammate nametags beyond this 3D distance; `0` disables the limit. |
| `r_teammate_name_r` | int | `210` | `0..255` | Ingen direkt | Teammate nametag red channel. |
| `r_teammate_name_g` | int | `245` | `0..255` | Ingen direkt | Teammate nametag green channel. |
| `r_teammate_name_b` | int | `255` | `0..255` | Ingen direkt | Teammate nametag blue channel. |

## 4. Klientkommandon

### 4.1 Inbyggda konsolkommandon

| Kommando | Argument | Funktion |
|---|---|---|
| `set <cvar> [value]` | cvar, valfritt värde | Läser eller skriver ett cvar. Bara första värdet efter namnet används; använd direkt cvar-syntax för numeriska värden. |
| `toggle <cvar>` | bool-cvar | Växlar `0/1`. |
| `reset <cvar>` | cvar | Återställer till projektdefault. |
| `cvarlist` | inga | Sorterad lista över alla cvars och aktuella värden. |
| `cmdlist` | inga | Sorterad lista över alla kommandon. |
| `help <namn>` | cvar/kommando | Beskrivning och projektdefault för cvar. |

### 4.2 Match, nätverk och identitet

| Kommando | Typ/giltigt | Funktion |
|---|---|---|
| `player <name>` | string, `1..20` bytes/tecken i nuvarande ASCII-användning | Sätter, arkiverar och replikerar spelarnamn via `cl_player_name`. Flera ord tillåts. Returnerar `name = ...`. |
| `ready` | inga argument | Togglar ready under väntfasen. Defaultbindning `F3`. |
| `resetmatch` | inga | Begär auktoritativ reset av matchen. Defaultbindning `F5`. Alla spelare får använda kommandot. |
| `gamemode <läge>` | `duel`, `ca`, `clanarena` eller `clan_arena` | Väljer spelläge under warmup. Alla spelare får använda kommandot. |
| `team <lag>` | `red`, `blue`, `none` eller `unassigned` | Väljer lag under warmup. En Clan Arena-spelare måste välja rött eller blått lag innan `ready`. |
| `connect <host> [port]` | host string; port `1..65535` | Ansluter till server. |
| `connect <port>` | port `1..65535` | Shorthand för `127.0.0.1:<port>`. |
| `disconnect` | inga | Frigör serverplatsen och kopplar ned. |
| `reconnect` | inga | Återansluter till senast begärda host/port. |
| `net_stats` | inga | Skriver connection state, host, port, player slot och ping. |
| `messagemode` | inga | Öppnar chat-input. Defaultbindning `T`. |
| `showchat` | inga | Visar chatthistorik i fem sekunder. Defaultbindning `Z`. |

Chat skickas med `Enter`, begränsas till `64` bytes och historiken behåller de
senaste `8` meddelandena. Chatten döljs fem sekunder efter senaste meddelande
eller manuell expansion.

### 4.3 Klient och konfiguration

| Kommando | Argument | Funktion |
|---|---|---|
| `quit` | inga | Avslutar klienten. |
| `clear` | inga | Tömmer console scrollback. |
| `writeconfig` | inga | Skriver arkiverade cvars och bindings till `client.cfg`. |
| `toggleconsole` | inga | Öppnar/stänger konsolen. |
| `actionlist` | inga | Listar bindbara gameplay-actions. |

### 4.4 Konsolens redigeringsinput

| Input | Funktion |
|---|---|
| Textinput | Lägger till tecken sist på den aktuella kommandoraden. |
| `Enter` | Kör kommandoraden och lägger en icke-tom rad i historiken. |
| `Backspace` | Tar bort sista tecknet. |
| `Up` | Går bakåt i kommandohistoriken. |
| `Down` | Går framåt i historiken; efter senaste posten töms raden. |
| `Tab` | Autocomplete på aktuellt ord. En träff fylls i; flera träffar listas. |
| `Escape` | Stänger konsolen utan att köra eller rensa kommandoraden. |
| Tangent bunden till `toggleconsole` | Stänger konsolen. Default är `section`. |

## 5. Bindings och actions

### Bind-kommandon

| Kommando | Funktion |
|---|---|
| `bind <key>` | Visar nuvarande binding. |
| `bind <key> <command>` | Binder tangent/musknapp till kommando. Kommandot kan innehålla mellanslag. |
| `unbind <key>` | Tar bort binding och skickar release om knappen hålls. |
| `unbindall` | Tar bort samtliga bindings. |
| `bindlist` | Sorterad lista över alla bindings. |

Bindings normaliseras till lowercase. Mellanslag, `_` och `-` tas bort ur
tangentnamn. Följande alias finns:

| Alias | Normaliserat namn |
|---|---|
| `§`, `grave`, `backquote`, `` ` `` | `section` |
| `leftarrow` | `left` |
| `rightarrow` | `right` |
| `uparrow` | `up` |
| `downarrow` | `down` |

Vanliga giltiga namn är SDL-scancode-namn efter normalisering, exempelvis
`w`, `space`, `leftctrl`, `rightshift`, `f3`, `escape`, plus `mouse1`,
`mouse2` osv.

### Button-actions

Ett kommando som börjar med `+` får automatiskt motsvarande `-`-kommando när
knappen släpps.

| Action | Funktion |
|---|---|
| `+forward` / `-forward` | Framåtinput. |
| `+back` / `-back` | Bakåtinput. |
| `+moveleft` / `-moveleft` | Strafe vänster. |
| `+moveright` / `-moveright` | Strafe höger. |
| `+moveup` / `-moveup` | Jump; positiv Z-thrust i flight. |
| `+movedown` / `-movedown` | Duck/crouch nar `g_flight 0`; negativ Z-thrust i flight. |
| `+duck` / `-duck`, `+crouch` / `-crouch` | Alias for `+movedown`: duck/crouch nar `g_flight 0`, movedown i flight. |
| `+speed` / `-speed`, `+sneak` / `-sneak` | Sneak/quiet walk med sankt mark-speed och utan vanliga fotsteg. |
| `+attack` / `-attack` | Håll/släpp eld med valt vapen. |
| `+dash` / `-dash` | Start the universal movement dash on press. Default bind: `mouse3`. Direction is sampled from movement input and locked when dash starts. |
| `+scores` / `-scores` | Visa/dölj scoreboard. |
| `+zoom` / `-zoom` | Håll/släpp klient-side zoom. Växlar till `cl_zoom_fov` och effektiv zoomsens. Vid default `cl_zoom_sensitivity 0`: `sensitivity * tan(cl_zoom_fov / 2) / tan(cl_fov / 2)`. |
| `weapon <mg\|sg\|gl\|rl\|lg\|rg\|pg\|fg\|1..8>` | Choose machine gun, shotgun, grenade launcher, rocket launcher, lightning gun, railgun, plasma gun, or freeze gun. Weapon selection is sent to the server every command tick. |

### Standardbindings

| Input | Kommando |
|---|---|
| `section` (`§`/grave) | `toggleconsole` |
| `W` | `+forward` |
| `S` | `+back` |
| `A` | `+moveleft` |
| `D` | `+moveright` |
| `Space` | `+moveup` |
| `Left/Right Ctrl` | `+movedown` |
| `Left/Right Shift` | `+speed` |
| `Mouse1` | `+attack` |
| `Mouse2` | `+zoom` |
| `Mouse3` | `+dash` |
| `1` | `weapon mg` |
| `2` | `weapon sg` |
| `3` | `weapon gl` |
| `4` | `weapon rl` |
| `5` | `weapon lg` |
| `6` | `weapon rg` |
| `7` | `weapon pg` |
| `8` | `weapon fg` |
| `Q` | `weapon rl` |
| `E` | `weapon lg` |
| `R` | `weapon rg` |
| `F5` | `resetmatch` |
| `F3` | `ready` |
| `T` | `messagemode` |
| `Z` | `showchat` |
| `Tab` | `+scores` |
| `Escape` | `quit` |

## 6. Dedikerad server

### Startargument

```text
lg_duel_server [udp-port]
```

| Input | Typ | Default | Parserintervall | Funktion |
|---|---:|---:|---|---|
| `udp-port` | int | `27960` | `0..65535` | Port som servern binder. `0` begär en OS-vald ephemeral port. |

Servern kör auktoritativ simulation i `125 Hz`.

### Server-cvars

Skriv kommandona på serverprocessens stdin. `config/server_cvars.cfg` körs vid
serverstart och sätter defaultvärdena. Alla är runtime och sparas inte
automatiskt.

| Cvar | Typ | Default | Giltigt | Q3/QL-referens | Funktion |
|---|---:|---:|---|---|---|
| `sv_roundlimit` | int | `10` | `1..100` | Ingen direkt Q3 roundlimit-standard | Antal vunna rundor som krävs för matchvinst. |
| `sv_timelimit` | int | `0` | `0..120` minuter | Q3 `timelimit 0` | Matchtid; `0` stänger av tidsgränsen. |
| `sv_playerlimit` | int | `2` | `1..6` | Ingen direkt | Antal anslutna spelare som krävs för att matchflödet ska börja. |
| `sv_countdown` | float | `5` | `0..60` sekunder | Ingen exakt standard | Countdown före live round. Movement är aktiv; weapons är låsta under countdown. |
| `sv_roundend` | float | `5` | `0..30` sekunder | Ingen direkt | Delay efter round innan respawn/nästa countdown. |
| `sv_matchend` | float | `5` | `0..60` sekunder | Ingen direkt | Delay efter matchvinst innan reset till ready-up. |
| `sv_showopponenthealth` | bool | `1` | bool | Ingen Q3-standard | Visar motståndarens HP-bar för båda klienterna. |

### Serverkommandon

Servern stöder även samtliga inbyggda kommandon i avsnitt 4.1.

| Kommando | Funktion |
|---|---|
| `resetmatch` | Nollställer score och återgår till ready-up. |
| `status` | Skriver `players=<n> phase=<id> score=<p1>-<p2>-...-<p6>`. |

`phase` använder:

| ID | Fas |
|---:|---|
| `0` | WaitingForPlayers |
| `1` | WaitingForReady |
| `2` | Countdown |
| `3` | Live |
| `4` | RoundEnd |
| `5` | MatchEnd |

## 7. Server-control-script

Linux-script:

```text
./scripts/server-control.sh <action> [udp-port]
```

| Action | Port | Funktion |
|---|---|---|
| `start` | Default `27960`, giltigt `1..65535` | Bygger, startar och provar servern. Använder user service på defaultport om installerad. |
| `stop` | Ignoreras normalt | Stoppar user service eller PID-startad server. |
| `restart` | Default `27960` | Stoppar och startar. |
| `status` | Default/sparad port | Kontrollerar process och UDP-handshake. |
| `logs` | Ingen | Visar senaste systemd-journal eller `.server/lg-duel-server.log`. |

## 8. Server-probe

```text
lg_duel_server_probe [host] [port]
```

| Input | Typ | Default | Giltigt |
|---|---:|---:|---|
| `host` | string | `127.0.0.1` | Transportadress |
| `port` | int | `27960` | `1..65535` |

Proben väntar högst tre sekunder på handshake och snapshot. Exit code `0`
betyder fungerande serverkontakt.

## 9. Praktiska exempel

```text
g_accel
g_accel 10
set g_friction 6
reset g_friction
toggle cl_showfps
r_vsync 0
player yg
connect 127.0.0.1 27960
bind mouse2 "+attack"
bind tab "+scores"
writeconfig
```

Clan Arena:

```text
gamemode ca
team red
cl_show_alive_counts 1
ready
```

Under warmup visas valt spelläge och eget lag på HUD:en. I Clan Arena färgas
spelarnamnen på scoreboarden efter rött eller blått lag. Ojämna lag är tillåtna,
alla anslutna spelare måste ha valt lag och vara redo, med minst en spelare i vardera laget.

Server:

```text
status
sv_roundlimit 15
sv_countdown 3
sv_showopponenthealth 1
resetmatch
```

## 10. Kända gränser

- Nuvarande nätprotokoll och simulation har upp till `6` spelarslots.
- `sv_playerlimit` kan vara `1..6` och styr hur många anslutna spelare som
  krävs för att matchflödet ska börja.
- `config/balance.cfg` är serverauktoritativ och innehåller bara icke-cvar
  gameplayvärden. Lokala klientkopior ska inte påverka gameplay.
- `config/server_cvars.cfg` sätter serverns `sv_*` och tillfälliga utvecklings-
  `g_*` vid start. Klientens gameplay-`g_*` pushas fortfarande genom
  nätprotokollet för utveckling, men den vägen är avsedd att tas bort senare.
- `config/default_client.cfg` laddas före användarens `client.cfg` och innehåller
  standardcvars och bindings. `client.cfg` arkiverar client/render/audio-cvars
  och bindings, inte gameplay tuning-`g_*` eller serverns `sv_*`.
- String-tokenisering stöder dubbla citattecken men inga escape-sekvenser.
- RGB-värden är heltal `0..255`; alpha är float `0..1`.
- Q3/QL-referenser är jämförelsevärden, inte ett löfte om identisk fysik.

## 11. Referens för Q3-defaults

Q3-värdena har kontrollerats mot id Softwares publicerade Quake III Arena-
källkod:

- [`cg_main.c`](https://github.com/id-Software/Quake-III-Arena/blob/master/code/cgame/cg_main.c):
  `cg_fov`, `cg_drawCrosshair`, `cg_crosshairSize`.
- [`tr_init.c`](https://github.com/id-Software/Quake-III-Arena/blob/master/code/renderer/tr_init.c):
  `r_swapInterval`.
- [`bg_pmove.c`](https://github.com/id-Software/Quake-III-Arena/blob/master/code/game/bg_pmove.c):
  `pm_accelerate`, `pm_airaccelerate`, `pm_friction`, `pm_stopspeed`.

QL anges bara där projektet redan använder samma etablerade baseline eller där
Q3/QL-värdet uttryckligen är gemensamt. Exakta QL-defaults som inte kan beläggas
med offentlig källkod markeras inte som verifierade.
