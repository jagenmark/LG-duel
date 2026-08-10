# Recording, killcam, transfer, and operations

All runtime controls in this page are planned. Their exact console command and
cvar names are **pending implementation**. Do not infer a command name from an
operation label. The core C++ API can record a `ReplayDemo`, encode/decode it,
save/load it through helpers, restore it, play it headlessly, and seek. It does
not add app commands, a background file job, or player-facing controls.

## Planned recording and playback controls

| Operation | Exact command/cvar name | Status |
| --- | --- | --- |
| Start a named recording | Pending | No app command |
| Stop and finalize recording | Pending | No app command |
| Enable automatic match recording | Pending | No setting or job |
| List or resolve demo files | Pending | No app command |
| Play a demo | Pending | No app command |
| Stop playback | Pending | No app command |
| Pause/resume and single-tick step | Pending | Core session state exists; no app control |
| Seek by tick or time | Pending | Core runner/session support exists; no app control |
| Set playback speed | Pending | Core session state exists; no app control |
| Set first-person, chase, or free camera | Pending | Core session state exists; no renderer hookup |
| Follow or cycle a player | Pending | Core session state exists; no app control |
| Enable rolling buffer and set retention | Pending | Core archive exists; no runtime setting |

`ReplayFile` saves only a new `.lgdemo` file and refuses to replace an existing
recording. A future app layer must sanitize names, choose the ignored runtime
directory, and run disk work in a bounded background job. A disk error must end
or disable recording without stopping the match.

## Rolling buffer and killcam flow

The server-owned rolling archive is built. Its default retention is 1,500 ticks
(12 seconds at 125 Hz), with a 16 MiB byte cap, a 250-tick checkpoint interval,
and a 125-tick hash interval. It retains resolved inputs, checkpoints, hashes,
and lethal records in bounded queues. It reports retained ticks, inputs,
checkpoints, lethal records, estimated bytes, and dropped records.

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
its cap, or has missing data is rejected. The archive returns a self-contained
`ReplayDemo`; no client currently receives or presents it.

Current lethal provenance distinguishes self, world, and direct cases. The
record does not yet carry enough data to distinguish splash from direct damage.

The default future camera plan is killer first-person, then killer chase, then
victim/world for suicide, world, or no-killer deaths. `ReplayPresentationSession`
has independent clock, camera, follow, seek, speed, step, and skip state. It has
no `GameApp` renderer, audio, HUD, or live-client hookup, so no player-facing
killcam exists.

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
The transfer caps a segment at 512 KiB and 512 chunks. It handles duplicate and
out-of-order chunks, retries acknowledged gaps, supports cancellation, times
out safely, and rate-limits sends. A future transport hookup must give commands,
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
| Replay disabled | 324.86 us/tick |
| Rolling archive | 373.80 us/tick |
| Full recording | 381.25 us/tick |
| Rolling retained estimate | 1,933,584 B |
| Rolling checkpoint capture | 16 captures; 258.20 us total |
| Full-recording estimate | 1,064,960 B |
| Headless playback | 4,643 ticks/s (37.15x real time) |

These are test measurements, not a claim about all hardware or live-match
performance. The enabled paths remain bounded by their configured caps.

## Known limits

- Replay is authoritative simulation, not video capture or export.
- `ReplayPresentationSession` has no `GameApp` renderer, audio, HUD, or
  live-client hookup.
- `ReplayFile` has no console command, automatic recording setting, runtime
  directory policy, or background job.
- `ReplayTransfer` has no `NetCodec` or `UdpTransport` hookup; no remote replay
  transfer or player-facing killcam exists.
- It does not reproduce raw packet timing or pixel-identical local prediction.
- It does not run bot AI during playback or save bot AI state.
- Lethal provenance does not yet distinguish splash damage from direct damage.
- First- and third-person pullout animation support is not part of this work.
- A demo can fail cleanly across gameplay, map, content, or protocol changes.
- No broadcast director, cloud upload, or unbounded replay archive is planned.

## Troubleshooting

### Corrupt or incompatible demo

1. Read the diagnostic before retrying. Do not try to play a valid prefix.
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
