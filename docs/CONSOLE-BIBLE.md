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

| Value | Function |
|---|---|
| Unset/other | Use the explicitly selected SDL_Renderer compatibility path. |
| `gpu` | Require SDL_GPU with Vulkan; initialization failure aborts startup. |
| `sdl_gpu` | Same as `gpu`. |
| `vulkan` | Same as `gpu`. |

An explicit GPU request never changes renderer class to SDL_Renderer or another
SDL_GPU driver. Development launchers additionally verify the selected Vulkan
ICD, physical GPU identity, driver, Vulkan version, and software-renderer state.

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
| `cl_late_mouse_sample` | bool | `1` | `0..1` | Ingen direkt | Arkiv | Läser musen igen efter swapchain-acquire och före vyberoende renderarbete. `0` stänger av den sena läsningen för A/B-test. Mätt kostnad, tid till submit och fasvinst är apptider, inte fördröjning för hela kedjan från mus till skärm. |
| `cl_fov` | float | `90` | `45..140` | Q3/QL FOV-baseline `90` | Arkiv | First-person field of view. |
| `cl_zoom_fov` | float | `45` | `20..140` | Q3 `cg_zoomfov 22.5`, men projektet använder egen baseline | Arkiv | Field of view medan allmän `+zoom` hålls. Påverkar inte Sniper Rifle ADS. |
| `cl_zoom_sniper_fov` | float | `45` | `20..140` | Projektets tidigare Sniper Rifle baseline | Arkiv | Field of view för Sniper Rifle ADS. Oberoende av `cl_zoom_fov`; scope-masken ändrar inte form eller plats. |
| `cl_zoom_sensitivity` | float | `0` | `0..10` | Ingen direkt | Arkiv | First-person sensitivity multiplier while `+zoom` is held. `0` auto-matches the FOV ratio. |
| `cl_death_spectate_threshold` | float | `3` | `0..30` seconds | None | Archive | A live-respawn delay at or above this value switches the death camera to a living teammate after the hold. Shorter delays retain the local death-position view. |
| `cl_death_camera_hold` | float | `0.5` | `0..10` seconds | None | Archive | Minimum time to retain the local death-position view before teammate spectating begins. |
| `cl_death_desaturation` | float | `1` | `0..1` | None | Archive | Strength of the neutral grey death-view treatment. `0` disables it and `1` uses the full effect. HUD text remains readable above the treatment. |
| `cl_viewmodel_motion_scale` | float | `1` | `0..2` | None | Archive | Master scale for client-only first-person weapon motion. `0` returns an exactly neutral viewmodel transform and does not affect the camera, crosshair, aim, or simulation. |
| `cl_viewmodel_bob_scale` | float | `0.65` | `0..2` | None | Archive | Scale for stride-distance-driven first-person weapon bob. `0` disables only bob. |
| `cl_viewmodel_sway_scale` | float | `0.55` | `0..2` | None | Archive | Scale for immediate mouse-delta weapon sway. The sway rotates only the rendered viewmodel. `0` disables only sway. |
| `cl_viewmodel_inertia_scale` | float | `0.55` | `0..2` | None | Archive | Scale for lateral movement inertia and acceleration/braking response. `0` disables only inertia. |
| `cl_viewmodel_landing_scale` | float | `0.65` | `0..2` | None | Archive | Scale for airborne float and landing compression. `0` disables only jump/landing response. |
| `cl_camera_position_response` | float | `0` | `0..0.15` | None | Archive | Optional extremely subtle translation-only camera response derived from presentation motion. Default `0` is exactly neutral; no camera rotation is ever added. |
| `r_player_model` | int | `1` | `0..1` | None | Archive | Remote player body renderer. `0` uses legacy boxes and `1` uses the Quaternius Worker GLB default. The Duelist asset stays archived and is not a runtime option. |
| `cl_health_size` | float | `2` | `0.5..20` | Ingen | Arkiv | Skala för HP-HUD:en. |
| `cl_health_style` | int | `0` | `0..2` | Ingen | Arkiv | HP-HUD: `0` bottom-left bar, `1` centrerad HP-siffra med dynamisk färg, `2` crosshair-nära HP vänster och ammo höger. |
| `cl_speed_size` | float | `1.5` | `0.5..6` | Ingen | Arkiv | Textskala för speed-indikatorn under crosshair. |
| `cl_showfps` | bool | `0` | bool | Ingen | Arkiv | Visar FPS, genomsnittlig frame time och renderer-backend i fönstertiteln. |
| `cl_showspeed` | bool | `1` | bool | Q3/QL-style UPS | Arkiv | Visar horisontell predicted speed under crosshair som `<värde> ups`. Intern hastighet multipliceras med `40`, så `8 = 320 ups`. |
| `cl_show_net` | bool | `1` | bool | Ingen | Arkiv | Visar ping, ticks, command ack, rewind, prediction och overload i titeln. |
| `cl_netgraph` | int | `0` | `0..2` | None | Archive | Right-side network HUD. `0` hides it; `1` keeps ping, jitter, loss, snapshot rate, effective interpolation delay, and current/target buffer lead compact; `2` adds the controller's timeline error, playback rate, startup and underrun state, snapshot count, presentation/newest/collision ticks, correction counters, transport details, prediction/rewind diagnostics, and the ten-second event graph. Underruns and hard corrections are display-only edge events and never alter playback. |
| `cl_netgraph_scale` | float | `1.75` | `0.75..3` | None | Archive | Scales the complete network HUD, including text, spacing, panel size, and the expanded history graph. It is automatically constrained to the current window. |
| `net_sim_latency_ms` | int | `0` | `0..5000` | Ingen | Nej | Lokal klient-UDP-simulator: extra one-way latency i ms efter connect. `60` pa bade outgoing och incoming ger ungefar +120 ms RTT. |
| `net_sim_jitter_ms` | int | `0` | `0..5000` | Ingen | Nej | Lokal klient-UDP-simulator: slumpad one-way variation runt `net_sim_latency_ms` per datagram. Delay clampas till minst `0`. |
| `net_sim_loss_percent` | int | `0` | `0..100` | Ingen | Nej | Lokal klient-UDP-simulator: oberoende sannolikhet per datagram att droppas. |
| `net_sim_reorder_percent` | int | `0` | `0..100` | Ingen | Nej | Lokal klient-UDP-simulator: probabilistisk transport-reordering. Valda datagram far en liten extra hold sa senare datagram kan ga fore; packet bytes och protocol state andras inte. Effekten beror pa faktisk packet cadence. |
| `net_sim_seed` | int | `0` | `0..2147483647` | Ingen | Nej | Lokal klient-UDP-simulator: RNG-seed for reproducerbara loss/jitter/reorder-fall. `0` anvander fast default-seed. |
| `cl_show_lagcomp` | bool | `0` | bool | Ingen | Arkiv | Visar riktig rewind-data när den används, annars att lag compensation inte används. |
| `cl_show_alive_counts` | bool | `0` | bool | Ingen | Arkiv | Visar antal levande röda och blå spelare på HUD:en i Clan Arena. Kan växlas med `toggle cl_show_alive_counts`. |
| `cl_interp_mode` | int | `1` | `0..1` | Ingen | Arkiv | Remote interpolation mode. `0`: legacy senaste snapshot-par + lokal render-alpha och gammal viewed tick. `1`: buffrad interpolation med `cl_interp`. |
| `cl_interp` | float | `0.024` | `0..0.25` | 3 ticks vid 125 Hz | Arkiv | Remote player snapshot interpolation delay i sekunder. Lägre värde minskar visuell latency men kräver jämnare snapshots; högre värde döljer jitter bättre. |
| `cl_interp_adaptive` | bool | `1` | `0..1` | None | Archive | Adjust the remote-player interpolation reserve from measured snapshot jitter. |
| `cl_interp_min` | float | `0.016` | `0..0.25` | 2 ticks at 125 Hz | Archive | Minimum delay used by adaptive interpolation. |
| `cl_interp_max` | float | `0.064` | `0..0.25` | 8 ticks at 125 Hz | Archive | Maximum delay used by adaptive interpolation. |
| `cl_interp_extrapolate` | float | `0.016` | `0..0.05` | Legacy compatibility | Archive | Retained for existing configs. The unified interpolation controller does not extrapolate during buffer underrun; it holds the newest authoritative snapshot and resumes from the same presentation timeline when data returns. |

### 3.2 Ljud

| Cvar | Typ | Default | Giltigt | Q3/QL-referens | Lagring | Funktion |
|---|---:|---:|---|---|---|---|
| `s_enable` | bool | `1` | bool | Ingen direkt | Arkiv | Slår av/på klientens ljudeffekter. |
| `s_volume` | float | `0.35` | `0..1` | Ingen direkt | Arkiv | Volym för hit-, countdown- och round-ljud. |
| `s_footstep_volume` | float | `0.45` | `0..1` | Ingen direkt | Arkiv | Separat fotstegsvolym. Multipliceras med `s_volume`. |
| `s_frag_volume` | float | `1.5` | `0..2` | None | Archive | Frag/kill sound scale, multiplied by `s_volume`. |
| `s_rg_fire_volume` | float | `1` | `0..1` | None | Archive | Sniper Rifle fire sound scale, multiplied by `s_volume`. The old `rg` cvar name stays for config compatibility. |
| `s_rg_ready_volume` | float | `1` | `0..1` | None | Archive | Sniper Rifle ready chime scale, multiplied by `s_volume`. The old `rg` cvar name stays for config compatibility. |

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
| `g_lg_knockback` | float | `1000` | `0..100000` | Q3 `g_knockback 1000`, equals `22` internally | LG knockback per second in direct Q3 scale. `0` disables knockback and `500` applies half the default impulse. |
| `g_lg_fire_hz` | float | `20` | `1..125` | Ingen direkt stabil cvar | Authoritative LG damage/knockback and FG damage/freeze instances per second. Default 20 Hz gives 6 damage per instance with `g_lg_damage 120` or `g_fg_damage 120`; FG freeze amount comes from `balance.cfg`. |
| `g_rl_knockback` | float | `1000` | `0..1000` | Q3 `g_knockback 1000`, motsvarar `22` internt | RL-knockback per explosion, skalad med splash-damage. |
| `g_knockback_time_ms` | int | `100` | `0..250` | Q3-style knockback movement timer | Antal millisekunder som grounded knockback anvander air movement utan ground friction. `0` stanger av speciallaget men behaller damage och direkt knockback. |
| `g_fg_damage` | int | `120` | `1..500` | Ingen standardmekanik | Authoritative freeze gun damage per second, distributed over `g_lg_fire_hz` instances. Does not add FG knockback. |
| `g_rg_damage` | int | `50` | `1..500` | TF2 Sniper base body damage | Authoritative Sniper Rifle damage at zero charge. Scoped charge scales this up before the Sniper Rifle's headshot multiplier applies. The old `rg` name stays for config and bind compatibility. |
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

### 3.4a Sniper Rifle balance keys

The Sniper Rifle uses the old Railgun slot and `weapon.rg.*` keys so saved binds
and server files still work. The server builds charge only while the player holds
ADS with this weapon. ADS and its scope/FOV view ease in over `0.2` seconds;
charge starts once that settle time ends. Leaving ADS or switching weapons clears
the charge. Firing spends all charge even on a miss.

| Key | Default | Valid range | Function |
|---|---:|---|---|
| `weapon.rg.range` | `300` | `0.1..5000` | Hitscan range. |
| `weapon.rg.eye_height` | `0.65` | `0..10` | Shot start height relative to the player. |
| `weapon.rg.knockback` | `0` | `0..1000` | Knockback on hit. |
| `weapon.rg.charge_seconds` | `3.3` | `0.05..30` | Time in ADS needed to reach full charge. |
| `weapon.rg.max_damage_multiplier` | `3` | `1..10` | Body damage scale at full charge. Damage grows in a straight line from `g_rg_damage`. |
| `weapon.rg.headshot_multiplier` | `3` | `1..10` | Headshot damage scale, applied after charge. With the defaults, headshots deal `150` damage at zero charge and `450` at full charge. |
| `weapon.rg.cooldown_ticks` | `62` | `1..5000` | Ticks before the next shot. |
| `weapon.rg.spawn_ammo` | `50` | `0..999` | Spawn ammo when `g_infiniteammo 0`. |

### 3.4b Headshot balance keys

The server applies these values to confirmed head hits. Each weapon can use a
different scale without changing its body damage. Values may use decimals; the
server rounds the final shot damage to the nearest whole point.

| Key | Default | Valid range | Function |
|---|---:|---|---|
| `weapon.lg.headshot_multiplier` | `2` | `1..10` | Lightning Gun headshot damage scale. |
| `weapon.fg.headshot_multiplier` | `2` | `1..10` | Freeze Gun headshot damage scale. It does not change freeze buildup. |
| `weapon.rg.headshot_multiplier` | `3` | `1..10` | Sniper Rifle headshot damage scale. Charge damage applies first. |
| `weapon.re.headshot_multiplier` | `2` | `1..10` | Revolver headshot damage scale. |
| `weapon.mg.headshot_multiplier` | `2` | `1..10` | Machine Gun headshot damage scale. |
| `weapon.sg.headshot_multiplier` | `2` | `1..10` | Shotgun headshot scale for each pellet that hits the head. |

### 3.4c Revolver balance keys

The Revolver has its own server-owned shot tuning, cooldown state, and ammo.
Its gameplay shares only the common instant-hit trace with the Sniper Rifle.
Its beam-style shot display and current fire sound asset remain shared visual
and sound parts. Changing `g_rg_damage` or any `weapon.rg.*` key does not change
the Revolver.

| Key | Default | Valid range | Function |
|---|---:|---|---|
| `weapon.re.damage` | `80` | `1..500` | Body damage per shot. |
| `weapon.re.range` | `300` | `0.1..5000` | Instant-hit range. |
| `weapon.re.eye_height` | `0.65` | `0..10` | Shot start height relative to the player. |
| `weapon.re.knockback` | `0` | `0..1000` | Knockback on hit. |
| `weapon.re.headshot_multiplier` | `2` | `1..10` | Headshot damage scale. |
| `weapon.re.cooldown_ticks` | `62` | `1..5000` | Ticks before the next Revolver shot. |
| `weapon.re.spawn_ammo` | `50` | `0..999` | Spawn ammo when `g_infiniteammo 0`. |

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
| `r_render_scale` | float | `1` | `0.5..1.5` | None | Internal render scale. The Default profile uses native scale `1`. |
| `r_frustum_cull` | bool | `1` | bool | Ingen direkt | CPU-side konservativ frustum-culling av remote player-kroppar, vapen, geometri-outline och flytande healthbars i 3D. |
| `r_world_frustum_cull` | bool | `0` | bool | None directly | Experimental GPU-only conservative BVH frustum culling for cached static-world chunks. It remains opt-in because the current direct-draw path does not yet beat full material batches across the Overkill flythrough; `0` preserves the five-batch control path. |
| `r_show_collision` | int | `0` | `0..5` | Q3 `r_showtris` and editor filters, conceptually | GPU-only collision audit overlay. `0`: off. `1`: all categories. `2`: visible solids (blue). `3`: playerclip (green). `4`: weapclip (orange). `5`: jump-pad, teleport, and McGuffin-base triggers (purple). The explicit SDL_Renderer fallback reports an effective mode of `0`; typed control rejects activation there. Playerclip and weapclip remain behaviorally identical until authoritative trace masks are separated. |
| `r_texture_filter` | int | `2` | `0..2` | Q3 `r_textureMode` närmast | World/material texture filtering. `0`: nearest. `1`: bilinear with mipmaps. `2`: trilinear with mipmaps. |
| `r_texture_anisotropy` | int | `8` | `1..16`, renderer snappar till `1/2/4/8/16` | Q3/driver aniso settings närmast | World/material anisotropic filtering level. Unsupported anisotropy disables safely with a renderer log. |
| `r_texture_lod_bias` | float | `0.5` | `-2..4` | Q3/driver LOD-bias närmast | World/material mip LOD bias. Positive values choose blurrier, more stable mip levels; changes recreate the sampler without reloading textures. |
| `r_weapon_pos` | int | `0` | `0..2` | None directly | First-person weapon position: `0` center, `1` wide right, `2` wide left. This changes only local presentation and visual muzzle origins. |
| `r_combat_effects` | int | `2` | `0..2` | None | Master combat-effect quality. `0` clears and skips the new machine-gun effects, `1` uses the reduced set, and `2` enables the full restrained set. This never changes gameplay results. |
| `r_muzzle_light_intensity` | float | `2.4` | `0..12` | None | Presentation-only warm muzzle-light strength. |
| `r_muzzle_light_radius` | float | `3.2` | `0..16` world units | None | Radius of each short machine-gun muzzle light. |
| `r_muzzle_light_duration` | float | `0.13` | `0..0.25` seconds | None | Lifetime of each muzzle light. Its shaped tail bridges close machine-gun shots while the fixed pool prevents sustained-fire growth. |
| `r_tonemap_exposure` | float | `1` | `0.25..4` | None | Fixed exposure used by the filmic world and weapon tone map. There is no automatic exposure. |
| `r_display_gamma` | float | `1` | `0.5..1.5` | None | Archived final display gamma. It runs after tone mapping and grading, so `1` is neutral and white highlights stay white. |
| `r_atmosphere_grade` | int | `2` | `0..3` | None | Linked static-world grade and haze quality. `0`: off, `1`: low, `2`: default, `3`: high. F10 exposes these four values without exposing the raw grade or haze constants. |
| `r_bloom` | bool | `1` | bool | None | Enables the compact bloom response on bright effect sprites. It does not process HUD pixels. |
| `r_bloom_intensity` | float | `0.18` | `0..1` | None | Strength of the compact bright-effect bloom response. |
| `r_bloom_threshold` | float | `1.15` | `0.5..4` | None | Brightness threshold for compact effect bloom. Ordinary scene surfaces do not enter this path. |
| `r_antialiasing` | int | `1` | `0..2` | None | Anti-aliasing quality. `0`: off, `1`: 2x MSAA, `2`: 4x MSAA. The Default profile uses `1`. |
| `r_sun_shadows` | int | `2` | `0..2` | None | Sun-shadow quality. `0`: off, `1`: low, `2`: high. The Default profile uses `2`. |
| `r_point_lights` | int | `1` | `0..2` | None | Live authored point-light budget. `0` disables authored lights but keeps the bounded temporary combat-light pool; `1` enables the normal budget; `2` enables the high budget. It does not control material highlights. |
| `r_point_shadows` | int | `1` | `0..2` | None | Point-shadow quality for selected live lights. `0` disables their shadows, `1` uses the normal budget, and `2` uses the high budget. |
| `r_contact_shadows` | bool | `1` | bool | None | Enables contact shadows on players and props. |
| `r_material_quality` | int | `1` | `0..2` | None | Material response quality. `0` keeps Lambert diffuse light but omits specular and other enhanced response; `1` and `2` add the bounded enhanced paths. |
| `r_player_rim` | int | `1` | `0..2` | None | Player rim-light quality. `0`: off, `1`: low, `2`: high. |
| `r_casings` | bool | `1` | bool | None | Enables local, presentation-only cartridge casings. |
| `r_casing_count` | float | `1` | `0..1` | None | Seeded per-shot casing spawn ratio. `0` disables spawning; `1` attempts one casing for each machine-gun shot. |
| `r_casing_lifetime` | float | `2.4` | `0.05..10` seconds | None | Visual casing lifetime. |
| `r_casing_max` | int | `48` | `0..96` | None | Maximum active casings. The oldest active casing is reused when this limit is full. |
| `r_impact_particles` | float | `1` | `0..2` | None | Impact-particle count multiplier. |
| `r_impact_particle_max` | int | `192` | `0..384` | None | Shared cap for active machine-gun muzzle and impact particles. |
| `r_decals_max` | int | `128` | `0..256` | None | Maximum active bullet decals. The oldest decal is reused when full. |
| `r_decal_lifetime` | float | `24` | `0.05..120` seconds | None | Bullet-decal lifetime. Map/session reset clears all decals sooner. |
| `r_mg_barrel_max_rps` | float | `14` | `0..40` revolutions/second | None | Maximum playback rate for the existing authored machine-gun barrel motion. It does not add a second spin system. |
| `r_mg_barrel_spin_up` | float | `0.25` | `0..2` seconds | None | Time for the existing authored barrel playback to reach its maximum rate. Gameplay firing does not wait for it. |
| `r_mg_barrel_spin_down` | float | `0.55` | `0..3` seconds | None | Time for the existing authored barrel playback to stop after firing input stops. |
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
| `r_damage_numbers_mode` | int | `0` | `0..3` | Diabotical/arena-shooter damage feedback | Archived world-space damage-number mode. `0`: off. `1`: one number per confirmed local damage instance. `2`: per-instance numbers plus one immediate cumulative burst tally per target. `3`: cumulative burst tally only. Mode `4` was removed because tally-only damage numbers now always use the target's latest damage-event world position. |
| `r_damage_numbers_window` | float | `0.4` | `0..2` seconds | Ingen direkt | Seconds without qualifying local damage before a target's cumulative tally expires and the next hit starts a new burst. |
| `r_damage_numbers_duration` | float | `0.65` | `0.05..3` seconds | Ingen direkt | Visual lifetime of each individual damage number. |
| `r_damage_numbers_size` | float | `1.6` | `0.5..6` | Ingen direkt | Damage-number font scale. |
| `r_damage_numbers_alpha` | float | `1` | `0..1` | Ingen direkt | Damage-number opacity. |
| `r_damage_numbers_r` | int | `255` | `0..255` | Ingen direkt | Damage-number red channel. |
| `r_damage_numbers_g` | int | `236` | `0..255` | Ingen direkt | Damage-number green channel. |
| `r_damage_numbers_b` | int | `128` | `0..255` | Ingen direkt | Damage-number blue channel. |
| `r_damage_numbers_damage_color` | bool | `0` | bool | Ingen direkt | When enabled, damage numbers blend from the configured color toward red as damage increases. Headshots are shown as bold numeric text with a red accent instead of a `HEADSHOT` label. |
| `r_damage_numbers_offset_x` | float | `0` | `-400..400` | Ingen direkt | Horizontal screen offset from the projected world-space damage anchor. |
| `r_damage_numbers_offset_y` | float | `-46` | `-400..400` | Ingen direkt | Vertical screen offset from the projected world-space damage anchor. |

### 3.9 Motståndarmodell och träfffärg

| Cvar | Typ | Default | Giltigt | Q3/QL-referens | Funktion |
|---|---:|---:|---|---|---|
| `r_enemy_r` | int | `224` | `0..255` | Närmaste koncept: enemy color/forced model | Modellens rödkanal. |
| `r_enemy_g` | int | `82` | `0..255` | Ingen exakt 1:1-default | Modellens grönkanal. |
| `r_enemy_b` | int | `92` | `0..255` | Ingen exakt 1:1-default | Modellens blåkanal. |
| `r_enemy_alpha` | float | `1` | `0..1` | Ingen direkt | Modellens opacity. |
| `r_player_outline_mode` | int | `2` | `0..2` | None | Top-level player outline mode. `0`: off. `1`: keep the path chosen by `r_player_outline_style`. `2`: force the native output-resolution screen-space path; this is the Default-profile path. |
| `r_player_outline_style` | int | `0` | `0..1` | None | Compatibility path selector used by mode `1`. `0`: legacy geometry-expanded fallback. `1`: SDL_GPU half-resolution screen-space mask/dilation/composite path. |
| `r_player_outline_width` | float | `1.5` | `0..3` | None | Native mode `2` width for enemy and teammate outlines, in final output pixels. |
| `r_player_outline_debug_mask` | bool | `0` | bool | None | In native mode `2`, show the raw source group mask instead of the outer contour. This is a temporary view and is not archived. |
| `r_enemy_outline` | bool | `1` | bool | Ingen direkt | Draw enemy model outline in first-person 3D. |
| `r_enemy_outline_width` | float | `3` | `0..6` | Ingen direkt | Outline width in final display pixels for screen-space style `1`; legacy style `0` keeps approximate geometry fallback scaling. Intended normal range `1..6`. |
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
| `r_teammate_outline_width` | float | `3` | `0..6` | Ingen direkt | Outline width in final display pixels for screen-space style `1`; legacy style `0` keeps approximate geometry fallback scaling. Intended normal range `1..6`. |
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
| `gamemode <mode>` | `duel`, `ca`, `clanarena`, `mcg`, or `mcguffin` | Selects the mode during warmup. McGuffin requires a compatible active map. |
| `team <team>` | `red`, `blue`, `none`, `unassigned`, `spectator`, or `spec` | Selects a team during warmup. `spectator` releases the authoritative player body while retaining the connection; during warmup a spectator can claim a free body with `team red`, `team blue`, or `team none`. Clan Arena and McGuffin players must choose Red or Blue before `ready`. |
| `bot_weapon [mg\|sg\|gl\|rl\|lg\|sr\|pg\|fg\|re\|1..9]` | Optional weapon token | Shows or changes the server-authoritative weapon used by all current and future training bots. The default is Machine Gun (`mg`). `rg` remains an alias for `sr`. Normal weapon-switch and pullout rules still apply. |
| `connect <host> [port]` | host string; port `1..65535` | Ansluter till server. |
| `connect <port>` | port `1..65535` | Shorthand för `127.0.0.1:<port>`. |
| `disconnect` | inga | Frigör serverplatsen och kopplar ned. |
| `reconnect` | inga | Återansluter till senast begärda host/port. |
| `net_stats` | none | Prints connection identity, ping, jitter, bidirectional loss, snapshot rate/age, bandwidth, packet sizes, local network-simulation counters, and the interpolation controller's buffer lead, timeline error, playback rate, startup/underrun state, correction counts, buffered snapshots, and presentation/newest ticks. |
| `messagemode` | inga | Öppnar chat-input. Defaultbindning `T`. |
| `showchat` | no arguments | Shows chat history for five seconds. |
| `+showchat` / `-showchat` | no arguments | Holds expanded chat history open. The mouse wheel scrolls it. Default binding: `Z`. |

The network layer provides sixteen authoritative player bodies plus eight separate
spectator connections. If all player bodies are occupied, a newly accepted
connection starts as a spectator. Spectators are excluded from readiness,
team balance, objectives, scoring, spawning, and player limits.

Chat is sent with `Enter`, limited to `240` UTF-8 bytes, and the authoritative
server retains the latest `40` messages. A newly connected client receives that
history through acknowledged, MTU-sized chat packets rather than gameplay
snapshots. Opening chat with `T` or receiving a new message returns the view to
the newest message.

### 4.3 Klient och konfiguration

| Kommando | Argument | Funktion |
|---|---|---|
| `quit` | inga | Avslutar klienten. |
| `clear` | inga | Tömmer console scrollback. |
| `writeconfig` | inga | Skriver arkiverade cvars och bindings till `client.cfg`. |
| `toggleconsole` | inga | Öppnar/stänger konsolen. |
| `actionlist` | inga | Listar bindbara gameplay-actions. |
| `settings` | none | Opens the graphics menu. Default bind: `F10`. |
| `misc` | none | Opens the tools and debug menu. Default bind: `F11`. |
| `mcguffin_throw` | none | Throws the carried McGuffin using the authoritative server tuning. Default bind: `G`. |
| `spectate_next` | none | Follows the next eligible player. Death spectating is restricted to living teammates; dedicated spectators cycle every living active player. Mouse1 performs the same contextual action without changing its normal `+attack` bind. |
| `spectate_prev` | none | Follows the previous eligible player. Death spectating is restricted to living teammates; dedicated spectators cycle every living active player. Mouse2 performs the same contextual action without changing its normal `+zoom` bind. |

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

Mouse drag selects visible console text. A drag in the input row selects
editable input text. `Ctrl+A` selects the full input. `Ctrl+C` copies the
selection, and `Ctrl+V` replaces an input selection.

Chat uses the same input selection keys. Mouse drag can also select visible chat
history while chat input is open.

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
| `+showchat` / `-showchat` | Hold chat history open; use the mouse wheel to scroll. |
| `+zoom` / `-zoom` | Hold or release zoom. General zoom uses `cl_zoom_fov`; Sniper Rifle ADS uses `cl_zoom_sniper_fov`. Both use the same `cl_zoom_sensitivity` rule. Sniper ADS also opens the scope and sends ADS state to the server for charge. |
| `weapon <mg\|sg\|gl\|rl\|lg\|sr\|pg\|fg\|re\|1..9>` | Choose machine gun, shotgun, grenade launcher, rocket launcher, lightning gun, Sniper Rifle, plasma gun, freeze gun, or revolver. `rg`, `rail`, and `railgun` remain aliases for `sr`. |

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
| `Mouse1` | `+attack`; next living teammate while spectating |
| `Mouse2` | `+zoom`; previous living teammate while spectating |
| `Mouse3` | `+dash` |
| `G` | `mcguffin_throw` |
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
| `Z` | `+showchat` |
| `Tab` | `+scores` |
| `F10` | `settings` |
| `F11` | `misc` |

The default config has no quit key. Run `quit` in the console or add a custom
bind if you want one.

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
| `sv_playerlimit` | int | `2` | `1..16` | No direct equivalent | Number of connected players required for match flow to begin. |
| `sv_countdown` | float | `5` | `0..60` sekunder | Ingen exakt standard | Countdown före live round. Movement är aktiv; weapons är låsta under countdown. |
| `sv_roundend` | float | `5` | `0..30` sekunder | Ingen direkt | Delay efter round innan respawn/nästa countdown. |
| `sv_respawn_delay` | float | `2` | `0..30` seconds | General server rule | Death-respawn delay used by modes with live respawning, including McGuffin. Zero respawns immediately. Elimination modes ignore it. |
| `sv_mcg_scorelimit` | int | `100` | `1..1000` points | Diabotical: 100 | Points required to win a McGuffin round. |
| `sv_mcg_points_per_second` | int | `1` | `1..20` points/s | Provisional | Installed-objective scoring rate. |
| `sv_mcg_carry_points_per_second` | int | `1` | `1..20` points/s | Provisional | Unbanked carry-credit accumulation rate. |
| `sv_mcg_carry_limit` | int | `10` | `1..100` points | Diabotical: 10 | Maximum unbanked carry credit. |
| `sv_mcg_spawn_delay` | float | `30` | `0..120` seconds | Diabotical: about 30 | Delay before neutral pickup becomes active. |
| `sv_mcg_install_delay` | float | `0` | `0..10` seconds | Provisional | Required own-base installation hold. |
| `sv_mcg_steal_time` | float | `1` | `0..10` seconds | Provisional | Uninterrupted enemy-base steal hold. |
| `sv_mcg_return_time` | float | `30` | `0..120` seconds | LG Duel safety rule | An uncollected ground McGuffin teleports to its initial neutral spawn after this delay. Installed/base states are unaffected; zero disables the safety return. |
| `sv_mcg_throw_speed` | float | `12` | `0..50` world units/s | LG Duel tuning | Forward launch speed along the carrier's aim. |
| `sv_mcg_throw_up_speed` | float | `4` | `0..30` world units/s | LG Duel tuning | Upward speed added to shape the throw arc. |
| `sv_mcg_throw_velocity_inherit` | float | `1` | `0..2` multiplier | LG Duel tuning | Fraction of carrier velocity inherited by the objective. |
| `sv_mcg_throw_gravity` | float | `20` | `0..100` world units/s² | LG Duel tuning | Gravity applied while the thrown objective is airborne. |
| `sv_mcg_throw_bounce` | float | `0.4` | `0..1.5` multiplier | LG Duel tuning | Velocity retained and reflected at world impacts. |
| `sv_mcg_throw_pickup_delay` | float | `0.2` | `0..3` seconds | LG Duel tuning | Global pickup lockout after a throw, preventing immediate self-recapture. |
| `sv_mcg_final_hold` | float | `3` | `0..30` seconds | Provisional | Uncontested hold needed to convert 99 to victory. |
| `sv_mcg_pickup_radius` | float | `0.9` | `0.1..5` world units | LG Duel geometry | Ground-objective touch radius. |
| `sv_matchend` | float | `5` | `0..60` sekunder | Ingen direkt | Delay efter matchvinst innan reset till ready-up. |
| `sv_showopponenthealth` | bool | `1` | bool | Ingen Q3-standard | Visar motståndarens HP-bar för båda klienterna. |

### Serverkommandon

Servern stöder även samtliga inbyggda kommandon i avsnitt 4.1.

| Kommando | Funktion |
|---|---|
| `resetmatch` | Nollställer score och återgår till ready-up. |
| `status` | Skriver `players=<n> phase=<id> score=<p1>-<p2>-...-<p6>`. |
| `mcguffin_debug` | Prints authoritative McGuffin state and timers. |
| `spawn_debug` | Prints the latest authoritative team-spawn scoring decision. |
| `bot_weapon [mg\|sg\|gl\|rl\|lg\|sr\|pg\|fg\|re\|1..9]` | Shows or changes the authoritative weapon used by all current and future training bots. The default is Machine Gun (`mg`). `rg` remains an alias. |

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

- An owned live-scenario client forces `vid_fullscreen 0`, `vid_width 1280`,
  and `vid_height 720` after loading `client.cfg`. This test-only override
  requires development control plus `LG_DUEL_LIVE_SCENARIO=1` and is not saved.
- The network protocol and simulation support up to `16` concurrent player slots,
  plus eight separate spectator connections.
- `sv_playerlimit` accepts `1..16` and controls how many connected players are
  required before match flow begins.
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
