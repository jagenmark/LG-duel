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

### Lagring och auktoritet

- `Arkiv`: sparas automatiskt i klientens `client.cfg`.
- `Runtime`: återställs när klienten eller servern startas om.
- `Serverstyrd`: klientens värde skickas till servern och replikeras tillbaka
  så att server och prediction använder samma värde.
- Serverkonsolens `sv_*` är runtime-värden och sparas inte automatiskt.

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
| `sensitivity` | float | `1` | `0.1..10` | Q3 `sensitivity 5` | Arkiv | Multiplikator för rå relativ musinput. Skalningen är projektspecifik och värdena är därför inte direkt likvärdiga. |
| `cl_aim_mode` | int | `0` | `0..1` | Ingen direkt | Arkiv | `0`: relative 3D. `1`: absolute 2D. Perspektivläget tvingar relative 3D. |
| `cl_render_mode` | int | `0` | `0..1` | Ingen | Arkiv | `0`: top-down. `1`: first-person 3D. |
| `cl_fov` | float | `90` | `45..140` | Q3/QL FOV-baseline `90` | Arkiv | Top-down-vyomfång och perspektivets field of view. |
| `cl_zoom_fov` | float | `45` | `20..140` | Q3 `cg_zoomfov 22.5`, men projektet använder egen baseline | Arkiv | Field of view medan `+zoom` hålls. Påverkar bara klientens vy/aimberäkning, inte simulation eller server. |
| `cl_zoom_sensitivity` | float | `0` | `0..10` | Ingen direkt | Arkiv | Multiplikator på `sensitivity` medan `+zoom` hålls. `0` använder auto: `tan(cl_zoom_fov / 2) / tan(cl_fov / 2)` med vinklar i grader. Positiva värden är manuell override. |
| `cl_camera_zoom` | float | `1` | `0.25..4` | Ingen direkt | Arkiv | Top-down-zoom. Över `1` zoomar in. |
| `cl_rotate_view` | bool | `0` | bool | Ingen | Arkiv | Roterar top-down relative-aim-vyn så spelarens riktning pekar uppåt. Ignoreras i 3D. |
| `cl_health_size` | float | `2` | `0.5..6` | Ingen | Arkiv | Textskala för speed/health längst ned i mitten. |
| `cl_showfps` | bool | `0` | bool | Ingen | Arkiv | Visar FPS, genomsnittlig frame time och renderer-backend i fönstertiteln. |
| `cl_showspeed` | bool | `1` | bool | Q3/QL-style UPS | Arkiv | Visar horisontell predicted speed. Intern hastighet multipliceras med `40`, så `8 = 320 UPS`. |
| `cl_show_net` | bool | `1` | bool | Ingen | Arkiv | Visar ping, ticks, command ack, rewind, prediction och overload i titeln. |
| `cl_show_lagcomp` | bool | `0` | bool | Ingen | Arkiv | Visar nuvarande och rewound hitbox samt lag-comp-data. |

### 3.2 Ljud

| Cvar | Typ | Default | Giltigt | Q3/QL-referens | Lagring | Funktion |
|---|---:|---:|---|---|---|---|
| `s_enable` | bool | `1` | bool | Ingen direkt | Arkiv | Slår av/på klientens ljudeffekter. |
| `s_volume` | float | `0.35` | `0..1` | Ingen direkt | Arkiv | Volym för hit-, countdown- och round-ljud. |
| `s_footstep_volume` | float | `0.45` | `0..1` | Ingen direkt | Arkiv | Separat fotstegsvolym. Multipliceras med `s_volume`. |

### 3.3 Serverstyrd movement och gameplay

Dessa cvars skrivs i klientkonsolen men skickas till den auktoritativa servern.
De gäller symmetriskt för båda spelarna och replikeras tillbaka till klienterna.
De arkiveras inte.

Projektets rörelseskala är `1 intern enhet = 40 Q3/QL units`.

| Cvar | Typ | Projektdefault | Giltigt | Q3/QL-default eller ekvivalent | Funktion |
|---|---:|---:|---|---|---|
| `g_accel` | float | `80` | `0..1000` | `pm_accelerate 10` | Markacceleration mot `g_maxspeed`. |
| `g_airaccel` | float | `24` | `0..1000` | `pm_airaccelerate 1` | Acceleration i luften. |
| `g_aircontrol` | bool | `0` | bool | Q3/QL: `0`, QW-style: `1` | Vaxlar extra air control. `0` behaller Q3/QL-kansla utan QuakeWorld-lik styrning i luften. `1` later forward-input vrida horisontell luftfart mot siktriktningen utan att direkt ge gratis fart. |
| `g_friction` | float | `8` | `0..100` | `pm_friction 6` | Friktion när spelaren är grounded. |
| `g_stopspeed` | float | `2.5` | `0..100` | `pm_stopspeed 100`, motsvarar `2.5` internt | Minsta kontrollhastighet i friktionsberäkningen. |
| `g_maxspeed` | float | `8` | `0.1..100` | `g_speed 320`, motsvarar `8` internt | Sustained mark- och air-speed cap. |
| `g_knockback` | float | `22` | `0..1000` | Q3 `g_knockback 1000`; inte samma interna enhet | LG-knockback per sekund. |
| `g_vampirism` | float | `0` | `0..2` | Ingen standardmekanik | Healing som multipel av utdelad skada. `0.1 = 10%`, `1 = 100%`, `2 = 200%`. Fraktioner ackumuleras och avrundas när helt HP kan delas ut. |
| `g_selfdamage` | float | `100` | `0..100` | `100` | Procent av egen splash-damage som appliceras. Värdet rundas till närmaste heltal innan det skickas till servern. |
| `g_healthamount` | int | `100` | `1..100000` | `100` | HP som varje spelare startar med vid spawn, rundstart och warmup-respawn. |
| `g_flight` | bool | `0` | bool | Ingen direkt LG-duel-motsvarighet | Aktiverar obegränsad flight för båda spelarna. |
| `g_flightaccel` | float | `32` | `0..1000` | Ingen direkt | Acceleration/thrust under flight. |
| `g_flightmaxspeed` | float | `12` | `0.1..100` | Ingen direkt | Maximal flight-hastighet. `12` motsvarar `480 UPS`. |
| `g_flightdamping` | float | `2` | `0..100` | Ingen direkt | Dämpning av flight-velocity utan thrust. |
| `g_playersize_xy` | float | `1` | `0.5..3` | Ingen direkt | Skalar båda spelarnas auktoritativa radie/hitbox i X/Y. |
| `g_playersize_z` | float | `1` | `0.5..3` | Ingen direkt | Skalar båda spelarnas auktoritativa höjd/hitbox i Z. |

### 3.4 Crosshair

| Cvar | Typ | Default | Giltigt | Q3/QL-referens | Funktion |
|---|---:|---:|---|---|---|
| `crosshair_enable` | bool | `1` | bool | Q3 `cg_drawCrosshair 4`; Q3-värdet väljer även grafik | Visa crosshair. |
| `crosshair_style` | int | `0` | `0..2` | Ingen 1:1-indexering | `0`: cross. `1`: cross + dot. `2`: dot. |
| `crosshair_size` | float | `8` | `1..40` | Q3 `cg_crosshairSize 24` | Armlängd i pixlar. Geometrin skiljer sig från Q3-grafiken. |
| `crosshair_thickness` | float | `2` | `1..10` | Ingen direkt | Linjetjocklek i pixlar. |
| `crosshair_gap` | float | `3` | `0..30` | Ingen direkt | Avstånd från centrum till armar. |
| `crosshair_alpha` | float | `1` | `0..1` | Ingen direkt standard | Opacitet. |
| `crosshair_r` | int | `255` | `0..255` | Ingen direkt standard | Röd kanal. |
| `crosshair_g` | int | `255` | `0..255` | Ingen direkt standard | Grön kanal. |
| `crosshair_b` | int | `255` | `0..255` | Ingen direkt standard | Blå kanal. |

### 3.5 Renderer och lokal LG-beam

| Cvar | Typ | Default | Giltigt | Q3/QL-referens | Funktion |
|---|---:|---:|---|---|---|
| `r_vsync` | bool | `1` | bool | Q3 `r_swapInterval 0` | GPU: mailbox/vsync när på, immediate när av om plattformen stöder det. Projektet har alltså motsatt standard mot Q3. |
| `r_beam_width` | float | `2` | `1..12` | Ingen direkt stabil cvar | Lokal LG-beams bredd. Pixlar i overlay/top-down; skalas till world width i 3D. |
| `r_beam_alpha` | float | `1` | `0..1` | Ingen direkt | Lokal beam-opacity. |
| `r_beam_r` | int | `74` | `0..255` | Ingen direkt standard | Lokal beam, röd kanal. |
| `r_beam_g` | int | `166` | `0..255` | Ingen direkt standard | Lokal beam, grön kanal. |
| `r_beam_b` | int | `255` | `0..255` | Ingen direkt standard | Lokal beam, blå kanal. |
| `r_beam_hit_enable` | bool | `1` | bool | Ingen direkt | Aktivera färgrespons på lokal beam vid träff. |
| `r_beam_hit_r` | int | `255` | `0..255` | Ingen direkt | Beamens träfffärg, röd. |
| `r_beam_hit_g` | int | `255` | `0..255` | Ingen direkt | Beamens träfffärg, grön. |
| `r_beam_hit_b` | int | `255` | `0..255` | Ingen direkt | Beamens träfffärg, blå. |
| `r_beam_hit_duration` | float | `0.12` | `0..2` sekunder | Ingen direkt | Hur länge träfffärgen ligger kvar. |
| `r_beam_hit_fade` | bool | `1` | bool | Ingen direkt | `1`: gradvis återgång. `0`: binär färg tills durationen löper ut. |

Beamens minimala pulsanimation är presentationsstyrd: fasta endpoints, cirka
`±4%` bredd och `±5%` ljusstyrka. Den påverkar inte simulation eller aim.

### 3.6 Motståndarens beam

| Cvar | Typ | Default | Giltigt | Q3/QL-referens | Funktion |
|---|---:|---:|---|---|---|
| `r_enemy_beam_width` | float | `2` | `1..12` | Ingen direkt | Motståndarens beam-bredd. |
| `r_enemy_beam_alpha` | float | `1` | `0..1` | Ingen direkt | Motståndarens beam-opacity. |
| `r_enemy_beam_r` | int | `255` | `0..255` | Ingen direkt | Röd kanal. |
| `r_enemy_beam_g` | int | `110` | `0..255` | Ingen direkt | Grön kanal. |
| `r_enemy_beam_b` | int | `80` | `0..255` | Ingen direkt | Blå kanal. |

### 3.7 Hitmarker

| Cvar | Typ | Default | Giltigt | Q3/QL-referens | Funktion |
|---|---:|---:|---|---|---|
| `r_hitmarker_enable` | bool | `1` | bool | Ingen standard-Q3-motsvarighet | Visa center-screen hitmarker. |
| `r_hitmarker_duration` | float | `0.12` | `0..2` sekunder | Ingen | Synlig tid. |
| `r_hitmarker_size` | float | `10` | `2..40` | Ingen | Armlängd i pixlar. |
| `r_hitmarker_thickness` | float | `2` | `1..10` | Ingen | Tjocklek i pixlar. |
| `r_hitmarker_r` | int | `255` | `0..255` | Ingen | Röd kanal. |
| `r_hitmarker_g` | int | `255` | `0..255` | Ingen | Grön kanal. |
| `r_hitmarker_b` | int | `255` | `0..255` | Ingen | Blå kanal. |

### 3.8 Motståndarmodell och träfffärg

| Cvar | Typ | Default | Giltigt | Q3/QL-referens | Funktion |
|---|---:|---:|---|---|---|
| `r_enemy_r` | int | `224` | `0..255` | Närmaste koncept: enemy color/forced model | Modellens rödkanal. |
| `r_enemy_g` | int | `82` | `0..255` | Ingen exakt 1:1-default | Modellens grönkanal. |
| `r_enemy_b` | int | `92` | `0..255` | Ingen exakt 1:1-default | Modellens blåkanal. |
| `r_enemy_alpha` | float | `1` | `0..1` | Ingen direkt | Modellens opacity. |
| `r_enemy_lean` | bool | `1` | bool | Q3 `cg_runroll`-inspirerad model lean | Slår på/av velocity lean för motståndarmodellen i 3D. Påverkar bara renderad modell, inte lokal POV, simulation, aim, hitboxar eller nätkod. |
| `r_enemy_lean_scale` | float | `1` | `0..3` | Q3 `cg_runroll 0.005` | Multiplikator för motståndarmodellens velocity lean. `1` motsvarar ungefär Q3-standard, `0` ger ingen lean även om `r_enemy_lean` är på. |
| `r_enemy_hit_enable` | bool | `1` | bool | Ingen direkt | Byt/blenda modellfärg vid träff. |
| `r_enemy_hit_r` | int | `255` | `0..255` | Ingen | Träfffärg röd. |
| `r_enemy_hit_g` | int | `190` | `0..255` | Ingen | Träfffärg grön. |
| `r_enemy_hit_b` | int | `198` | `0..255` | Ingen | Träfffärg blå. |
| `r_enemy_hit_duration` | float | `0.12` | `0..2` sekunder | Ingen | Träfffärgens duration. |
| `r_enemy_hit_fade` | bool | `1` | bool | Ingen | `1`: gradvis blend. `0`: binär färg. |

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
| `player <name>` | string, `1..20` bytes/tecken i nuvarande ASCII-användning | Sätter och replikerar spelarnamn. Flera ord tillåts. Returnerar `name = ...`. |
| `ready` | inga argument | Togglar ready under väntfasen. Defaultbindning `F3`. |
| `resetmatch` | inga | Begär auktoritativ reset av matchen. Defaultbindning `F5`. |
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
| `+movedown` / `-movedown` | Negativ Z-thrust i flight. |
| `+attack` / `-attack` | Håll/släpp eld med valt vapen. |
| `+scores` / `-scores` | Visa/dölj scoreboard. |
| `+zoom` / `-zoom` | Håll/släpp klient-side zoom. Växlar till `cl_zoom_fov` och effektiv zoomsens. Vid default `cl_zoom_sensitivity 0`: `sensitivity * tan(cl_zoom_fov / 2) / tan(cl_fov / 2)`. |
| `weapon <lg\|rg\|rl\|1\|2\|3>` | Välj lightning gun, railgun eller rocket launcher. Vapenval skickas till servern varje command-tick. |

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
| `Left/Right Shift` | `+movedown` |
| `Mouse1` | `+attack` |
| `Mouse2` | `+zoom` |
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

Skriv kommandona på serverprocessens stdin. Alla är runtime och återställs vid
omstart.

| Cvar | Typ | Default | Giltigt | Q3/QL-referens | Funktion |
|---|---:|---:|---|---|---|
| `sv_roundlimit` | int | `10` | `1..100` | Ingen direkt Q3 roundlimit-standard | Antal vunna rundor som krävs för matchvinst. |
| `sv_timelimit` | int | `0` | `0..120` minuter | Q3 `timelimit 0` | Matchtid; `0` stänger av tidsgränsen. |
| `sv_playerlimit` | int | `2` | `1..6` | Ingen direkt | Antal anslutna spelare som krävs för att matchflödet ska börja. |
| `sv_countdown` | float | `5` | `0..60` sekunder | Ingen exakt standard | Countdown före live round. Movement är aktiv; weapons är låsta under countdown. |
| `sv_roundend` | float | `1` | `0..30` sekunder | Ingen direkt | Delay efter round innan respawn/nästa countdown. |
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
cl_render_mode 1
r_vsync 0
player yg
connect 127.0.0.1 27960
bind mouse2 "+attack"
bind tab "+scores"
writeconfig
```

Server:

```text
status
sv_roundlimit 15
sv_countdown 3
sv_showopponenthealth 0
resetmatch
```

## 10. Kända gränser

- Nuvarande nätprotokoll och simulation har upp till `6` spelarslots.
- `sv_playerlimit` kan vara `1..6` och styr hur många anslutna spelare som
  krävs för att matchflödet ska börja.
- Klientens gameplay-`g_*` är serverstyrda genom nätprotokollet men ställs från
  klientkonsolen. Serverns stdin-konsol registrerar i nuläget inte dessa `g_*`.
- `client.cfg` arkiverar client/render/audio-cvars och bindings, inte gameplay
  tuning-`g_*` eller serverns `sv_*`.
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
