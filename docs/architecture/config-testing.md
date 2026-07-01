# Config And Testing

Runtime tuning is split between console cvars, command-carried tuning requests, hard-coded defaults, and `config/gameplay.cfg`.

## Gameplay Config

`src/sim/GameplayConfig.*` loads `config/gameplay.cfg` version 1. At this writing it only configures authoritative Grenade Launcher tuning:

- launch: speed, vertical boost, gravity
- bounce: damping, rest speed, bounce sound threshold
- collision/visual size: projectile radius and direct-hit hitbox radius
- explosion: fuse, radius, direct/splash damage, knockback
- cooldown ticks

`ServerGame` searches upward from the current directory for `config/gameplay.cfg` in its constructor. If loading succeeds, it seeds `grenadeLauncherTuning_`; if it fails, the server logs and keeps defaults.

## Runtime Configurable

Client cvars are registered in `src/app/ClientCvars.*` and consumed heavily in `GameApp.cpp`. Some values are local presentation only: render mode, colors, crosshair, HUD, audio, interpolation, frame cap, video, debug display, and network simulation.

Some cvar-derived values are sent in `CommandPacket` when requested and become authoritative after the server accepts them: movement tuning, player size scales, weapon damage values, lightning/rocket knockback, lightning fire rate, vampirism, self-damage percent, health amount, bot dodge settings, and weapon switching mode.

Hard-coded authoritative defaults still exist in `ServerGame.cpp`, `Combat.hpp`, `Movement.hpp`, `NetProtocol.hpp`, and rules files. Treat these as code constants unless a cvar/config path explicitly updates them.

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
- If changing protocol fields, update protocol tests and bump `kProtocolVersion`.
- If changing authoritative tuning, prefer a focused regression test before changing code.
- Keep presentation cvars out of server authority unless they affect gameplay and are validated in `NetCodec`.
