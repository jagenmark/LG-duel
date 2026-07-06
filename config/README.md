# LG Duel Config Files

These files are shipped with the game and copied beside the client/server
executables in packaged builds.

## balance.cfg

`balance.cfg` is loaded by `ServerGame` at startup. It contains authoritative
gameplay values that are not console cvars.

Examples:
- weapon ranges, spread, projectile speed, projectile radius, projectile fuse
- freeze gun freeze buildup, decay, max slow, ice pool tuning, and spawn ammo
- weapon cooldown ticks
- QL weapon pullout ticks
- jumppad retrigger cooldown

Do not put `sv_*`, `g_*`, `r_*`, `cl_*`, `s_*`, `vid_*`, or binds here. If a
value can be changed through the console, it belongs in a cvar config instead.

Only the server loads this file for gameplay authority. Clients should learn
authoritative gameplay state from server snapshots or explicit future
send-on-change config data, not from a local `balance.cfg`.

## server_cvars.cfg

`server_cvars.cfg` is executed by `lg_duel_server` after server cvars are
registered and before the first tick. It contains startup defaults for server
console variables.

Examples:
- `sv_roundlimit`, `sv_countdown`, `sv_showopponenthealth`
- temporary development `g_*` gameplay cvars such as `g_accel`,
  `g_lg_damage`, `g_fg_damage`, `g_healthamount`, and `g_weaponswitching`

The server console can still change these values at runtime. For now, clients
can also push `g_*` tuning requests for development; those accepted requests can
temporarily override the server startup values until the server cvar is changed
again or the server restarts. This client-push path is temporary and should not
be expanded.

## default_client.cfg

`default_client.cfg` is loaded by the client before the user-specific
`client.cfg` in `%AppData%/LG Duel/LG Duel`. It contains default client cvars
and default key binds.

The user `client.cfg` is written by `writeconfig` and overrides this file. Do
not put server-authoritative balance values here.

## sound_mixer.cfg

`sound_mixer.cfg` is a focused client-side mix file for per-cue audio volumes.
It is loaded after client cvars are registered. These values are presentation
only and do not affect server gameplay.

## Code Defaults

C++ structs keep conservative fallback values so tests and library-style
construction still produce valid state if a config file is missing. Runtime
apps should use these files as the editable source of truth and should log
clearly when a shipped config is missing or invalid.
