# Replay and demo system

This folder defines the replay contract for LG-duel. The core ledger is
`7020ef5` (format v1), `4cff068` (authoritative record/playback), `bce44e2`
(rolling archive), `8199355` (capture cadence), `3513191` (transfer state), and
`9c1693f` (file, presentation, final hash, and measures). It is not a list of
player-facing controls.

The core has a bot-free format, recorder, headless playback runner, checkpoint
restore, hash checks, seek, rolling archive, self-contained lethal-segment
extraction, transfer state, strict file helpers, presentation-session state, and
measures. It does not yet have a `GameApp` UI, runtime demo commands, background
file job, live UDP hookup, renderer/audio/HUD hookup, or player-facing killcam.
Treat every item marked **pending** as a requirement, not as a control a player
can use.

## Reading order

- [Architecture](ARCHITECTURE.md) covers the one recording path, authority,
  checkpoints, hashes, and client state.
- [Format and validation](FORMAT.md) covers `.lgdemo` versioning, chunks, and
  clean failure.
- [Operations and killcam](OPERATIONS.md) covers planned controls, rolling
  retention, transfer, privacy, measures, limits, and fixes for bad demos.
- [Bot merge contract](BOT-MERGE.md) records the bot boundary, known overlap,
  and the required post-merge test.

## Scope

One authoritative replay record supports both saved demos and killcams. A
killcam extracts a short, self-contained part of that same record. It must not
have its own simulator, command path, checkpoint type, or renderer.

Replays reproduce server simulation. They do not record video frames, raw UDP
arrival order, or a player's local predicted frame. Playback must use the real
fixed-step gameplay code without fake UDP clients.

## Status

| Area | Status at this commit |
| --- | --- |
| Authoritative recorder and headless playback runner | Implemented in `lg::replay`; covered by `lg_duel_replay_playback_tests` |
| `.lgdemo` v1 encoder/decoder and canonical hash | Implemented; covered by `lg_duel_replay_codec_tests` |
| Checkpoint restore, per-tick hash check, and seek | Implemented in the headless runner |
| Strict `.lgdemo` save/load helpers | Implemented; no app command or background job calls them |
| Replay clock, camera, follow, seek, and skip session state | Implemented; no `GameApp` renderer/audio/HUD hookup |
| Rolling server buffer and self-contained lethal segment extraction | Implemented; no player-facing killcam consumes it |
| Bounded transfer codec and sender/receiver state machine | Implemented; no `NetCodec` or `UdpTransport` live hookup |
| Team-mode visibility guard | Implemented in transfer policy; ordinary remote delivery remains unhooked |
| Replay measures | Recorded by `lg_duel_replay_performance_tests`; see [Operations](OPERATIONS.md) |
| Replay and bot compatibility test | Implemented in `lg_duel_replay_playback_tests`; required after bot merges |

The required safe remote policy is narrow until a tested team filter exists:

- Ordinary remote killcams may run in Duel.
- Local and developer sessions may use their explicit developer policy.
- Ordinary remote killcams stay off in Clan Arena and McGuffin.

No setting may widen that policy by accident.

## Terms

- **Resolved command:** the final command consumed by server simulation for a
  slot and tick.
- **Authoritative state:** state that can change later server simulation.
- **Presentation state:** camera, animation, audio, HUD, and other client-only
  state derived from replay time and authoritative output.
- **Replay generation:** a value that changes at a hard map or match reset.
  A segment must not cross it.
