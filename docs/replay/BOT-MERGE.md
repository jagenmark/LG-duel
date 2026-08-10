# Bot boundary and merge contract

## Contract

The player-like bot work and replay work meet at one point: the final command
that server simulation consumes for a slot and tick. Replay observes that point.
It does not ask why a bot chose the command.

The current post-command recorder hook calls `captureResolvedReplayInput()`
after human command acceptance and `updateBotCommands()`, and before
movement/combat simulation. The completed-state checkpoint/hash hook runs after
the tick increments `serverTick`. If bot code replaces the present command
storage, replay attaches to the replacement resolved-command adapter, not to bot
classes.

Do not add replay fields, callbacks, serialization, tests, or includes to bot
planner code. Do not serialize `ScenarioBotState`, bot random state, or any
successor bot state.

## Exact bot-work file surface observed on 2026-08-10

The active `Build player-like bot AI` branch currently touches these files:

| File | Replay merge position |
| --- | --- |
| `CMakeLists.txt` | Both tasks may add sources. Keep bot sources; add replay sources as separate entries. |
| `src/app/GameApp.cpp` | Keep bot controls intact. Append replay command registration without renaming bot controls. |
| `src/net/NetCodec.cpp` | Keep bot wire changes. Add replay-transfer codecs as distinct packet cases with the protocol update and 1,200-byte checks. |
| `src/server/BotAi.cpp` | Bot-only. Replay must not edit or depend on it. |
| `src/server/BotAi.hpp` | Bot-only. Replay must not edit or depend on it. |
| `src/server/ServerApp.cpp` | Keep bot cvar/setup flow. Add replay enablement and bounded recorder setup beside, not inside, bot setup. |
| `src/server/ServerGame.cpp` | Shared integration point. Preserve bot command generation, then call the neutral replay recorder exactly once after resolved commands and once after the completed tick. |
| `src/server/ServerGame.hpp` | Shared integration point. Add only the narrow replay owner or neutral adapter declarations; do not move bot fields or bot APIs. |
| `tests/net/BotServerTests.cpp` | Bot-only. Keep it bot-focused. Add replay compatibility coverage under `tests/replay/`, not here. |

These are the exact currently known bot-work files. New replay-only files should
remain under `src/replay/`, `tests/replay/`, and `docs/replay/` to avoid widening
this overlap.

## Narrow post-merge conflict resolution

Merge the bot changes first, then reapply replay's narrow changes in this order:

1. In `ServerGame::tick`, find the final resolved commands after both accepted
   human input and bot command production. If bot code now uses a neutral
   resolved-input object, record that object’s final `UserCommand`, accepted
   edges, original attack aim, and `viewedServerTick`.
2. Insert one recorder call there, before any movement, combat, projectile, or
   match simulation consumes those commands. Do not call back into `BotAi`.
3. At the completed-tick boundary, capture a replay checkpoint/hash from the
   bot-free authoritative state. Do not call `captureScenarioState()` and do
   not add bot fields to a checkpoint or hash.
4. Preserve only bot slot metadata needed for a name or UI marker. Playback
   disables bot command generation and injects the record through the same
   normal gameplay command path as a human command.
5. Keep replay transfer code separate from snapshots and keep each datagram at
   most 1,200 bytes. Resolve `NetCodec.cpp` additions by packet type, not by
   folding replay data into snapshot code.
6. Keep bot tests unchanged. Put the replay test in its dedicated replay target.

The only expected hand conflict is the small command-resolution area of
`ServerGame::tick` and, if both sides edit setup, adjacent source-registration
lines in `CMakeLists.txt`. A conflict that needs a bot-internal replay call or a
`ScenarioBotState` field is not a valid resolution; stop and redesign the hook.

## Required bot compatibility test after merge

`tests/replay/ReplayPlaybackTests.cpp` now provides the dedicated compatibility
coverage through `lg_duel_replay_playback_tests`. It records a hard-mode bot,
saves and loads the demo, restores a fresh `ServerGame` with no bot added,
injects resolved inputs, checks every stored hash, and seeks. After both
branches merge, run:

```powershell
cmake --build --preset default --target lg_duel_replay_codec_tests
ctest --test-dir build/default --output-on-failure -R '^lg_duel_replay_codec_tests$'
cmake --build --preset default --target lg_duel_replay_playback_tests
ctest --test-dir build/default --output-on-failure -R '^lg_duel_replay_playback_tests$'
```

The test procedure is fixed:

1. Start a deterministic match with the hard-mode bot used by the focused test.
2. Record only final resolved inputs, metadata, checkpoints, and hashes.
3. Encode and decode the `ReplayDemo`.
4. Restore a fresh `ServerGame` with no bot added. Playback must skip receive
   and bot generation, then inject the recorded resolved input at its original
   pre-simulation tick.
5. Compare every stored hash, finish without divergence, and seek to the
   requested tick.
6. Confirm the test reads no bot planner, perception, navigation, aim, goal,
   difficulty, memory, or random-state field.

The test proves the compatibility contract without freezing the bot design.
