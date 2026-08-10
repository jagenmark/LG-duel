# Replay architecture and authority

## One data path

Saved demos and killcams use one path. They differ only in record lifetime and
which valid range a reader requests.

```text
human packet -> server validation and edge de-duplication --+
                                                         |
bot command producer -> final UserCommand --------------+-> resolved-command record
                                                                |
match, roster, map, and rule changes -------------------------+
                                                                v
                         authoritative recorder -> full `.lgdemo` writer
                                                    or bounded rolling buffer
                                                                |
                                 checkpoint + commands + events + hashes
                                           |                    |
                                           |                    +-> verifier
                                           v
                                 playback runner -> replay-only client state
                                                        |
                                               cameras, HUD, audio, effects

lethal marker -> rolling-buffer segment extraction -> the same playback runner
```

The core now implements the resolved-input, `ReplayDemo`, codec, checkpoint,
headless runner, rolling archive, lethal-segment extraction, transfer state,
file helpers, and presentation-session state. The remaining app work is a disk
job and controls, live transport hookup, and renderer/audio/HUD hookup.

The server records the final command after human acceptance and after bot
generation, but before movement and combat consume it. It captures the completed
state, checkpoint, and hash after the authoritative tick finishes. This order
keeps command input and resulting state separate.

`4cff068` implements this boundary in `ServerGame::tick`:

1. Live play runs `receiveCommands()`, `updateMatchState()`, and
   `updateBotCommands()`.
2. The recorder calls `captureResolvedReplayInput()` before simulation. Its
   `ReplayTickInput::tick` equals the current pre-simulation server tick.
3. Playback skips both `receiveCommands()` and bot generation. It injects the
   recorded input at that same boundary with `applyReplayInput()`.
4. The tick simulates normally, increments `serverTick`, records lag history,
   then captures the completed `ReplayCheckpoint` and its hash. Those outputs
   use the incremented tick label.

The recorder hook does not enter `updateBotCommands`, bot planning, or
transport code.

Raw UDP arrivals are not replay input. Bundles, retries, packet timing, and
acknowledgements affect delivery, not the command that gameplay accepted.

## Resolved-command boundary

Bots are opaque command producers. Replay records what a bot did, not its plan
for doing it.

For every occupied slot and server tick, the record needs the final effective
command and the data used with it:

- normal movement axes and view angles;
- jump, dash, attack, and other accepted one-shot edges;
- requested weapon, zoom or stance values, and other gameplay fields;
- the original attack aim and `viewedServerTick` used for lag compensation;
- command sequence or edge state where later simulation needs it;
- slot/body connection changes, human-or-bot marker, name, team, ready state,
  spectator state, phase, rules, map, and configuration changes.

The current v2 `ReplayTickInput` records only present slots. It keeps each
present slot’s resolved command, `viewedServerTick`, consumed action edges,
accepted jump/dash/attack/throw edges, and original attack edge command. An
absent slot has no replay payload and must have default input state. It does not
yet encode separate dynamic roster, name, team, ready, rule, map, or
configuration-change records. Those records remain pending.

During replay, both human and bot slots inject those recorded commands through
the normal authoritative input path. Bot generation stays off. A bot marker may
remain in metadata for names and UI, but it has no effect on playback logic.

Replay code must never read or write bot perception, memory, goals, navigation,
combat plan, aim control, stuck recovery, difficulty internals, or bot random
state.

## Checkpoints and state hashes

A replay checkpoint is a dedicated, bot-free copy of state needed to resume a
fixed-step match. It is not `ScenarioState`, a debug snapshot, or a native C++
object dump. Its encoded fields have a defined order and use fixed-width values.

The checkpoint and canonical hash include, as applicable:

- server tick, map identity/revision/content hash, game mode, rules, phase,
  phase timers, overtime, scores, and winners;
- authoritative configuration and its revision;
- connection, participation, ready, team, player, respawn, selected-weapon,
  ammo, fractional-ammo, cooldown, ADS, charge, and pullout state;
- live projectiles with stable slots, sequence or generation values, and the
  counters that can change later projectile events;
- health pickup, ice-pool, McGuffin, spawn-selection, gameplay random, and
  future-relevant event-sequence state;
- consumed action-edge state and the bounded lag-compensation history required
  just after a restore.

The checkpoint and hash exclude network buffers and acknowledgements, renderer,
audio, UI, wall-clock values, client prediction, debug-only data, and all bot
internals including bot random state and `ScenarioBotState`.

The hash uses the same canonical order and normalized numeric rules as the
checkpoint codec. The current recorder stores an initial hash and then hashes at
its configured regular interval. The runner compares each stored hash and stops
at the first mismatch. It currently reports the tick and the category
`authoritative gameplay checkpoint`; finer subsystem detail remains pending.

Seeking restores the nearest earlier checkpoint and simulates recorded commands
forward. Linear playback and such a seek must reach the same hash.

Restore validates a checkpoint before it changes server state. A bad history,
spawn cursor, map/configuration match, or other invalid field rejects the
restore without a partial rewind. During replay playback the server suppresses
snapshot, projectile, and chat transport output; live recording still publishes
its normal transport output.

The rolling archive uses the same resolved inputs and completed checkpoints as
full recording. Its default retention is 1,500 ticks (12 seconds at 125 Hz),
with a 16 MiB rolling-storage cap that charges native tick, checkpoint, and
lag-history storage. It trims or stops before exceeding that cap. Segment
extraction selects a retained checkpoint at or before the requested pre-death
tick and includes the needed inputs, hashes, and lethal record through the
requested end tick. It returns a self-contained `ReplayDemo`, not a
killcam-only state format.

## Authority and presentation

| Kind | Replay source | Playback rule |
| --- | --- | --- |
| Player movement, damage, health, ammo, cooldowns, projectiles, pickups, rules, scores, objectives, map/config, gameplay random, lag history | Checkpoints, resolved commands, and required explicit authoritative events | Restore or simulate through the ordinary server code. |
| Fire, explosion, hit, frag, and other short-lived display events | Reconstructed where safe; explicitly recorded only when reconstruction is unsafe | Drive replay effects from replay time. Do not make them game authority. |
| Cameras, HUD, bob, sway, recoil, barrel spin, animation, audio, and viewmodels | Derived from replay state and replay clock | Rebuild or reset on seek, speed change, and followed-player change. |
| Raw packets, retries, ACKs, UI state, renderer state, wall-clock time, bot plan/state | Never authoritative replay data | Do not store or feed them to playback. |

The current playback runner is headless. A separate replay client session is
still pending. When it lands, it must stay separate from live `ClientGame`
state: live play keeps receiving snapshots, and replay state must not rewind the
live world, replace its transport state, send replay commands as live commands,
or write back into live client state.

A replay can closely reconstruct a killer's first-person action from recorded
view angles, weapon state, attacks, and authoritative outcomes. It does not
promise the exact pixels from the killer's locally predicted original frame.

## Reset and failure boundaries

Map changes, map revisions, hard reset, and replay-generation changes clear the
rolling record and end a matching replay or killcam. The archive rejects a
segment that spans a generation or lacks a valid earlier checkpoint. No
player-facing killcam currently invokes that abort path; future presentation
must leave live play intact when it rejects data.
