# Replay and demo system

This folder defines the replay contract for LG-duel. The core ledger is
`7020ef5` (initial format), `4cff068` (authoritative record/playback), `bce44e2`
(rolling archive), `8199355` (capture cadence), `3513191` (transfer state), and
`9c1693f` (file, presentation, final hash, and measures), followed by
`e5b6c3f` (historical format v2 and safety repair), and `392f6ee` (bounded native replay
recording memory). It is not a list of player-facing controls.

The system has a bot-free v5 format, recorder, headless playback runner,
checkpoint restore, hash checks, seek, rolling archive, self-contained
lethal-segment extraction, transfer state, strict file helpers, bounded replay
workers, a replay-only runtime, and a `GameApp` presentation source. The local
demo path and the narrow PR-C remote Duel transfer path use the existing replay
runtime and presentation source. HUD progress, team-mode visibility filters,
and cinematic killcam controls remain outside this work.

## Reading order

- [Architecture](ARCHITECTURE.md) covers the one recording path, authority,
  checkpoints, hashes, and client state.
- [Format and validation](FORMAT.md) covers `.lgdemo` versioning, chunks, and
  clean failure.
- [Operations and killcam](OPERATIONS.md) covers controls, rolling retention,
  transfer, privacy, measures, limits, and fixes for bad demos.
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
| `.lgdemo` v5 sparse-slot encoder/decoder and canonical hash | Implemented; v1 through v4 are rejected; covered by `lg_duel_replay_codec_tests` |
| Full authoritative config and authority-boundary records | Implemented; boundary state applies before its recorded tick |
| Lethal sequence and direct/splash/self/world provenance | Implemented in full and rolling replay records |
| Checkpoint restore, per-tick hash check, and seek | Implemented in the headless runner |
| Strict `.lgdemo` save/load helpers | Implemented with exclusive temporary creation and called only by `ReplayIoService` jobs |
| Saved demo and recorder capacity | Both cap at 512 MiB and stop cleanly at the cap |
| Replay storage and safe path policy | Implemented in `ReplayStorage`; names are single safe stems and files list in name order |
| Bounded local replay I/O | Implemented in `ReplayIoService`; save, load, decode, list, and delete run on one owned worker |
| Replay-only runtime and controls | Implemented in `ReplayRuntime` and `GameApp`; pause, step, speed, seek, camera, and follow are local |
| Replay presentation source | Implemented in `GameApp`; replay state feeds the same renderer, HUD, projectile, effects, and camera inputs |
| Rolling server buffer and self-contained lethal segment extraction | Implemented; the PR-C coordinator consumes Duel segments after the tick |
| Bounded transfer codec and sender/receiver state machine | Implemented with typed `NetCodec`, loopback, and UDP transport messages; covered by focused tests |
| Remote Duel coordinator and client receiver | Implemented as a bounded post-tick encode/transfer/decode path; one server worker and one active transfer per client |
| Team-mode visibility guard | Implemented in transfer policy; ordinary remote delivery is limited to Duel |
| Replay measures | Recorded by `lg_duel_replay_performance_tests`; see [Operations](OPERATIONS.md) |
| Replay and bot compatibility test | Implemented in `lg_duel_replay_playback_tests`; required after bot merges |

The required safe remote policy is narrow until a tested team filter exists:

- Ordinary remote killcams may run in Duel.
- Local and developer sessions may use their explicit developer policy.
- Ordinary remote killcams stay off in Clan Arena and McGuffin.

No setting may widen that policy by accident.

## Local runtime

Saved demos live below the client preference directory in `demos`. The storage
layer creates the directory on first use. A demo name may contain only letters,
numbers, `-`, and `_`; the `.lgdemo` suffix is optional and is added once. Path
components, parent paths, drive prefixes, reserved device names, and duplicate
suffixes are rejected. Save never replaces an existing file.

`ReplayIoService` owns one worker and a bounded queue. The worker performs file
and codec work. The app polls results on its main thread. The server uses the
same service for recording saves, directory scans, and deletes. `ServerGame::tick`
only records native replay data; it does not encode or write a file.

The local client commands are:

```text
demo_list
demo_play <name>
demo_stop
demo_pause
demo_resume
demo_togglepause
demo_step [ticks]
demo_seek <seconds|tick:n>
demo_speed <0.25..4>
demo_camera first|chase|free
demo_follow <slot>
demo_delete <name>
demo_status
```

`demo_play` is local-only and asks the user to disconnect before playback. The
runtime checks the map name, content hash, protocol, simulation revision, build
fingerprint, gameplay config, checkpoint, and authority boundaries. A hash
mismatch stops playback. Seek and follow changes rebuild the replay frame
source and do not rewind or write the live `ClientGame`.

The server adds `demo_record [name]`, `demo_stop`, `demo_status`, `demo_list`,
and `demo_delete`. `sv_demo_autorecord` starts a local recording when a match
enters live play. `sv_demo_checkpoint_ticks`, `sv_demo_hash_ticks`,
`sv_demo_max_file_mb`, and `sv_demo_max_resident_mb` set recorder bounds.

The PR-C remote controls are:

```text
sv_killcam 0|1
sv_killcam_before_seconds <0.1..30>
sv_killcam_after_seconds <0..10>
sv_killcam_transfer_timeout_ms <100..30000>
sv_killcam_max_segment_kb <1..512>
sv_killcam_packets_per_tick <1..64>
sv_replay_rolling_seconds <3..80>
sv_replay_rolling_max_mb <1..64>
killcam_status
killcam_skip
```

After `ServerGame::tick` completes, the server coordinator accepts a lethal
event only when the victim has an authenticated live client, the session and
replay generation still match, and the mode is ordinary Duel. It extracts the
rolling segment, queues encoding on the server-owned bounded worker, then sends
typed Begin/Chunk messages over the authenticated UDP endpoint. The client
validates the session, transfer IDs, generation, chunk CRCs, and whole-demo
SHA-256 before it queues decode on its `ReplayIoService`. A valid demo starts
the existing `ReplayRuntime`; a bad, stale, cross-match, oversized, or
unauthorized transfer is cancelled and never replaces the live source.

The coordinator and client receiver run outside `ServerGame::tick` and render.
They do not read or write replay files on the server tick path. `killcam_skip`,
disconnect, timeout, map/reset generation changes, and failed decode clear the
remote session without pausing or rewinding live play.

## Terms

- **Resolved command:** the final command consumed by server simulation for a
  slot and tick.
- **Authoritative state:** state that can change later server simulation.
- **Presentation state:** camera, animation, audio, HUD, and other client-only
  state derived from replay time and authoritative output.
- **Replay generation:** a value that changes at a hard map or match reset.
  A segment must not cross it.
