# Recording, killcam, transfer, and operations

All controls in this page are planned. Their exact console command and cvar
names are **pending implementation**. Do not infer a command name from the
operation label below.

## Planned recording and playback controls

| Operation | Exact command/cvar name | Status |
| --- | --- | --- |
| Start a named recording | Pending | Not implemented or named here |
| Stop and finalize recording | Pending | Not implemented or named here |
| Enable automatic match recording | Pending | Not implemented or named here |
| List or resolve demo files | Pending | Not implemented or named here |
| Play a demo | Pending | Not implemented or named here |
| Stop playback | Pending | Not implemented or named here |
| Pause/resume and single-tick step | Pending | Not implemented or named here |
| Seek by tick or time | Pending | Not implemented or named here |
| Set playback speed | Pending | Not implemented or named here |
| Set first-person, chase, or free camera | Pending | Not implemented or named here |
| Follow or cycle a player | Pending | Not implemented or named here |
| Enable rolling buffer and set retention | Pending | Not implemented or named here |

When added, named recordings must sanitize the requested name and write only to
the ignored runtime demo directory selected by the implementation. A full-demo
writer uses bounded buffering and safe flushes. A disk error ends or disables
recording without stopping the match.

## Rolling buffer and killcam flow

The optional server buffer retains a bounded recent history. Its default target
is about 8–15 seconds, with the exact setting pending. It retains commands,
required lifecycle events, periodic checkpoints, hashes, and lethal markers.
It uses fixed caps for records, bytes, and checkpoints. It must not grow a
per-tick vector without bound or add routine heap work on the hot path where a
bounded store works.

```text
authoritative lethal event
  -> record death tick, victim, killer/no-killer, weapon, projectile ID when useful,
     direct/splash/self/world class, and replay generation
  -> choose configured pre- and post-death ticks
  -> find a checkpoint at or before the segment start
  -> copy that checkpoint plus all commands/events through the segment end
  -> validate the self-contained segment and generation
  -> local replay session or bounded replay-transfer stream
  -> separate replay presentation; live play continues
```

A segment that has no valid checkpoint, crosses a map/reset generation, exceeds
its cap, or has missing data is rejected. The client skips the killcam and stays
in live play.

The default camera plan is killer first-person, then killer chase, then
victim/world for suicide, world, or no-killer deaths. The planned display has a
killcam label, killer and weapon when known, progress, and a skip control. Those
display controls are pending; no UI exists by virtue of this document.

The killcam never pauses or rewinds the server, changes respawn rules, delays a
round or match, injects replay commands into the live player, or replaces live
client state. It ends or aborts on local control return, skip, respawn, round or
match transition, map change, killer/victim disconnect, replay-generation
mismatch, incomplete data, or transfer failure.

## Replay transfer

Remote killcam data travels on a separate bounded replay-transfer stream. It
never appears in normal gameplay snapshots.

The implementation must define explicit replay-transfer packet types and bump
the gameplay protocol when their wire layout changes. Exact packet names are
pending. Each transfer record includes, at minimum:

- transfer ID and replay generation;
- segment identity and total byte or chunk count;
- chunk index/count or byte range;
- payload length and integrity data; and
- acknowledgement, cancellation, or error data when that record needs it.

Every application datagram, including headers, stays at or below 1,200 bytes.
The sender sets hard limits for segment bytes and chunk count, handles duplicate
and out-of-order chunks, retransmits acknowledged gaps, supports cancellation,
times out safely, and rate-limits the stream. Commands, snapshots, projectile
updates, and other live traffic always win its send budget. A failed transfer
only skips the killcam.

## Hidden-information policy

An ordinary player must not receive a full historical server state just because
that player died. An ordinary transfer may contain only what the chosen past
view and mode allow.

Until a reviewed team-visibility filter proves this rule for each team mode:

- ordinary remote killcams are enabled only for Duel;
- ordinary remote killcams are disabled for Clan Arena and McGuffin;
- local/developer use follows a separate explicit authorization policy; and
- a developer full-state demo must be marked as such and must not become the
  ordinary killcam export path.

## Measures and telemetry

The implementation must report bounded diagnostics for recorder enabled state,
retained ticks/commands/checkpoints/bytes, average bytes per tick, full-demo
bytes written, checkpoint time, recorder tick CPU time, playback simulation
time, segment bytes, transfer progress/retries/failures, and first divergence.

It must measure disabled recording, rolling recording, full recording, a busy
player/projectile match, and replay speed. This documentation makes no result
or speed claim. Disabled recording should add negligible work; enabled work must
remain within the declared caps.

## Known limits

- Replay is authoritative simulation, not video capture or export.
- It does not reproduce raw packet timing or pixel-identical local prediction.
- It does not run bot AI during playback or save bot AI state.
- First- and third-person pullout animation support is not part of this work.
- A demo can fail cleanly across gameplay, map, content, or protocol changes.
- No broadcast director, cloud upload, or unbounded replay archive is planned.
- Remote team-mode killcams remain off until safe filtering is implemented and
  tested.

## Troubleshooting

### Corrupt or incompatible demo

1. Read the diagnostic before retrying. Do not try to play a valid prefix.
2. Check the format version, map content hash, map revision, tick rate,
   configuration, protocol/build data, and checksum failure named by the reader.
3. Restore the exact required map/content/build when the diagnostic says the
   file is incompatible. Otherwise record a new demo.
4. If a transfer failed, skip the killcam. Do not retry by placing replay bytes
   into gameplay snapshots.

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
