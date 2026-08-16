# Recording, killcam, transfer, and operations

The local runtime controls in this page are implemented. Remote transfer and
remote killcam controls remain outside this work. The core records a
`ReplayDemo`, and the app/server layers use a bounded worker for codec and disk
work.

## Planned recording and playback controls

| Operation | Exact command/cvar name | Status |
| --- | --- | --- |
| Start a named recording | `demo_record [name]` | Implemented on the server |
| Stop and finalize recording | `demo_stop` | Implemented; save is queued |
| Enable automatic match recording | `sv_demo_autorecord 0|1` | Implemented on the server |
| List or resolve demo files | `demo_list` | Implemented with safe local names |
| Play a demo | `demo_play <name>` | Implemented for a disconnected local client |
| Stop playback | `demo_stop` | Implemented in the client |
| Pause/resume and single-tick step | `demo_pause`, `demo_resume`, `demo_togglepause`, `demo_step [ticks]` | Implemented |
| Seek by tick or time | `demo_seek <seconds|tick:n>` | Implemented |
| Set playback speed | `demo_speed <0.25..4>` | Implemented |
| Set first-person, chase, or free camera | `demo_camera first|chase|free` | Implemented; free mode starts at the followed body |
| Follow or cycle a player | `demo_follow <slot>` | Implemented |
| Enable rolling buffer and set retention | Pending | Core archive exists; no runtime setting |

`ReplayFile` saves only a new `.lgdemo` file and refuses to replace an existing
recording. It creates an exclusive, uniquely named temporary file, flushes it,
then publishes the final name without overwriting it. It does not disturb a
predictable partial file owned by another writer. `ReplayStorage` applies the
name and directory policy before a job starts. `ReplayIoService` reports a
disk or codec error without stopping the match.

The worker queue is bounded. A full queue rejects the request with a console
error. The worker has no callback into `GameApp`, `ServerGame`, or the renderer;
the owner polls `ReplayIoService::Result` and applies the result on its own
thread.

## Rolling buffer and killcam flow

The server-owned rolling archive is built. Its default retention is 1,500 ticks
(12 seconds at 125 Hz), with a 16 MiB rolling-storage cap, a 250-tick
checkpoint interval, and a 125-tick hash interval. It retains resolved inputs,
checkpoints, hashes, and lethal records in bounded queues. Its accounting charges
native tick, checkpoint, and lag-history storage. It trims or stops before a
record would exceed its cap, and reports retained ticks, inputs, checkpoints,
lethal records, estimated bytes, and dropped records.

The tick path asks whether a completed checkpoint is due before it copies one.
It does not capture a checkpoint on every rolling tick. Rolling reset and
self-contained lethal-segment extraction are built; no app killcam calls them.

```text
authoritative lethal event
  -> record death tick, victim, killer/no-killer, weapon, projectile ID when useful,
     current provenance, and replay generation
  -> choose configured pre- and post-death ticks
  -> find a retained checkpoint at or before the segment start
  -> copy that checkpoint plus all needed inputs and hashes through the segment end
  -> validate the self-contained segment and generation
  -> return a `ReplayDemo` for a future local session or transfer
```

A segment that has no valid checkpoint, crosses a map/reset generation, exceeds
its cap, crosses a dropped authority boundary, or has missing data is rejected.
The archive returns a self-contained `ReplayDemo`; no client currently receives
or presents it.

Lethal provenance distinguishes direct, splash, self, and world cases. Each
event carries a nonzero sequence within the replay generation and preserves a
projectile sequence when the damage came from a projectile. Full recordings,
rolling archives, and the bounded pending queue use the same event record.

The local runtime has first-person, chase, and free camera state. It presents
the followed replay body through a single source adapter. The normal renderer,
HUD, projectile effects, transient effects, and camera code read that source.
The replay runtime owns its `ServerGame` and `ReplayPlaybackRunner`; it never
rewinds or writes the live `ClientGame`.

The future killcam must never pause or rewind the server, change respawn rules,
delay a round or match, inject replay commands into the live player, or replace
live client state. It must abort on control return, skip, respawn, round/match
transition, map change, disconnect, generation mismatch, incomplete data, or
transfer failure.

## Replay transfer

The bounded transfer codec and sender/receiver state machine are built in
`ReplayTransfer`. They are not wired into `NetCodec` or `UdpTransport`, so no
live server sends a replay and no remote client receives one. Replay bytes do
not appear in ordinary gameplay snapshots.

The state machine has explicit `Begin`, `Chunk`, `Ack`, and `Cancel` packet
types. Its records carry:

- transfer ID and replay generation;
- segment byte count and chunk count;
- chunk index/count and payload;
- acknowledgements; and
- cancellation reason.

Every application datagram, including headers, stays at or below 1,200 bytes.
The transfer caps a segment at 512 KiB and 512 chunks. This remains separate
from the 512 MiB saved-demo and recorder limits. It handles duplicate and
out-of-order chunks, retries acknowledged gaps, supports cancellation, times
out safely, and rate-limits sends. A receiver expires on idle or overall timeout
when a cancel packet is lost, so stale transfer state cannot remain pinned. A
future transport hookup must give commands,
snapshots, projectile updates, and other live traffic priority. A failed
transfer must only skip the killcam.

## Hidden-information policy

An ordinary player must not receive a full historical server state just because
that player died. An ordinary transfer may contain only what the chosen past
view and mode allow.

`permitsRemoteKillcam` enforces the current transfer-layer policy. Until a
reviewed team-visibility filter proves this rule for each team mode:

- ordinary remote killcams are allowed only for Duel metadata;
- ordinary remote killcams are disabled for Clan Arena and McGuffin;
- local/developer use follows a separate explicit authorization policy; and
- a developer full-state demo must not become the ordinary killcam export path.

The transfer has no live hookup, so this policy does not yet enable a remote
killcam for any player.

## Measures and telemetry

`ReplayRecorderStats` reports recorded ticks, checkpoints, hashes, and estimated
bytes. Rolling stats add retained records, bytes, and drops. Transfer stats add
chunks, acknowledgements, retries, and cancellation. The app still needs
diagnostics for file bytes, segment bytes, and live transfer.

`lg_duel_replay_performance_tests` recorded this focused 512-tick measure:

| Measure | Result |
| --- | --- |
| Replay disabled | 330.40 us/tick; 0 resolved-input captures and 0 checkpoint captures |
| Rolling archive | 382.68 us/tick |
| Full recording | 388.21 us/tick |
| Headless playback | 13,796 ticks/s (110.37x real time) |
| 8-second, two-player encoded segment | 392,541 B; 332 packets at or below 1,200 B |
| 10-minute, two-player Duel native resident bound | 491,489,968 B; fixed 16-slot native frames, vector capacity, and checkpoints |
| 10-minute, 16-player encoded bound | 361,079,432 B |
| Current 512-tick full measure | 793,528 B encoded; 2,419,434 B resident |
| Saved `.lgdemo` maximum | 512 MiB |
| Recorder native resident maximum | 512 MiB |

These are test measurements, not a claim about all hardware or live-match
performance. The enabled paths remain bounded by their configured caps. A long
full recording can use close to 512 MiB, but stops cleanly at the cap.

## Known limits

- Replay is authoritative simulation, not video capture or export.
- Local replay audio event playback uses the existing snapshot event path; it
  does not reproduce the original client prediction frame.
- Free camera has no separate movement command yet; it starts at the followed
  body.
- Rolling retention has no player-facing runtime setting yet.
- `ReplayTransfer` has no `NetCodec` or `UdpTransport` hookup; no remote replay
  transfer or player-facing killcam exists.
- It does not reproduce raw packet timing or pixel-identical local prediction.
- It does not run bot AI during playback or save bot AI state.
- Lethal provenance records direct, splash, self, and world causes plus
  projectile sequence where present.
- First- and third-person pullout animation support is not part of this work.
- A demo can fail cleanly across gameplay, map, content, or protocol changes.
- No broadcast director, cloud upload, or unbounded replay archive is planned.

## Troubleshooting

### Corrupt or incompatible demo

1. Read the `demo load failed` or `demo playback failed` diagnostic before retrying. Do not try to play a valid prefix.
2. Check the format version, map content hash, map revision, tick rate,
   configuration, protocol/build data, and checksum failure named by the reader.
3. Restore the exact required map/content/build when the diagnostic says the
   file is incompatible. Otherwise record a new demo.
4. If a future transfer fails, skip the killcam. Do not place replay bytes in
   gameplay snapshots.

### Divergent demo

1. Record the first divergent tick and major state group from the verifier.
2. Compare the recorded and replayed resolved commands and `viewedServerTick`
   around that tick, then compare the nearest checkpoint and prior hash.
3. Check map/configuration/revision data and lag-compensation history before
   changing simulation code.
4. For a bot slot, confirm playback had bot generation disabled and injected the
   recorded command. Do not inspect bot planner, navigation, aim, or random
   internals; they are outside the replay contract.
5. Keep the bad file and verifier report for a focused replay test. Do not mask
   the mismatch by accepting a later hash.

### Local command examples

```text
demo_list
demo_play match_name
demo_resume
demo_speed 0.5
demo_camera chase
demo_follow 2
demo_seek tick:500
demo_pause
demo_stop
```
