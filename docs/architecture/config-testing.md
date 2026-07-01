# Config And Testing

Runtime tuning is split between three editable config files, command-carried development tuning requests, and code fallbacks for tests/library construction. The runtime config files are documented in `config/README.md`.

## Gameplay Config Files

`src/sim/BalanceConfig.*` loads `config/balance.cfg` version 1. It is for authoritative gameplay values that are not console cvars:

- weapon ranges, eye-height offsets, spreads, projectile sizes, projectile speeds, projectile lifetimes, fuse/bounce values, and cooldown ticks
- weapon switch pullout ticks
- no `sv_*`, `g_*`, `cl_*`, `r_*`, `s_*`, `vid_*`, or binds

`src/server/ServerApp.*` executes `config/server_cvars.cfg` after registering server cvars. This file owns server startup defaults for `sv_*` and the temporary development `g_*` gameplay cvars. Server stdin can still change these cvars at runtime.

`src/app/GameApp.*` executes `config/default_client.cfg` before the user-specific `%AppData%/LG Duel/LG Duel/client.cfg`. It owns default client cvars and default binds. The user `client.cfg` overrides it and is still written by `writeconfig`.

## Runtime Configurable

Client cvars are registered in `src/app/ClientCvars.*` and consumed heavily in `GameApp.cpp`. Some values are local presentation only: render mode, colors, crosshair, HUD, audio, interpolation, frame cap, video, debug display, and network simulation.

Gameplay cvars are registered through `src/sim/GameplayCvars.*` on both client and server. On the server they are seeded by `server_cvars.cfg`. For now, clients can still send cvar-derived tuning in `CommandPacket` for development and the values become authoritative after the server accepts them: movement tuning, player size scales, weapon damage values, lightning/rocket knockback, lightning fire rate, vampirism, self-damage percent, health amount, bot dodge settings, and weapon switching mode. This client push path is temporary and should not be expanded without explicit approval.

Code defaults still exist as fallbacks for tests, missing config files, and direct library construction. Runtime apps should treat config files as the editing surface. If a value is gameplay-affecting and designer-tunable, prefer `balance.cfg` for non-cvar values or `server_cvars.cfg` for console-backed values instead of introducing another hard-coded literal.

## Testing Strategy

Tests are many small CMake executables in `tests/CMakeLists.txt`. Important groups:

- Sim: movement, combat, collision, arena map parsing, Quake map conversion, duel/clan arena rules, fixed tick, determinism, weapon catalog.
- Net/server: protocol encoding/decoding, UDP transport, network simulation, client network simulator, server game, clan arena server, weapon switching.
- Client: prediction/session/game state, local hit feedback, hit confirm audio.
- App/UI: console system/input, client cvars, audio assets/audio behavior, scoreboard, HUD presentation, chat/input bindings.
- Render: perspective math, top-down scene, 3D scene, screen UI.
- Smoke: executable-level sanity through `lg_duel_smoke_tests`.

## Regressions Tests Should Catch

- Protocol layout/validation drift.
- Movement determinism and prediction/reconciliation mistakes.
- Hitscan/projectile damage, cooldown, and collision behavior.
- Map parser/converter bounds, materials, lights, and invalid input handling.
- Server match phase/rules transitions.
- UI/render scene construction regressions that can be tested without a full GPU frame.

## Footguns

- If changing cvars or console commands, check `docs/CONSOLE-BIBLE.md`.
- Do not mix cvar-backed values into `config/balance.cfg`; use `config/server_cvars.cfg`.
- Do not load local client `balance.cfg` for authoritative gameplay. Clients learn accepted gameplay tuning from the server.
- If changing protocol fields, update protocol tests and bump `kProtocolVersion`.
- If changing authoritative tuning, prefer a focused regression test before changing code.
- Keep presentation cvars out of server authority unless they affect gameplay and are validated in `NetCodec`.
