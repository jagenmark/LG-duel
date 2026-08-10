# Replay and demo system

This folder defines the replay contract for LG-duel. It describes the replay
work that is under implementation. It is not a list of shipped controls.

At this commit, there is no released replay UI, demo command, killcam transfer,
or claim of measured replay speed. Treat every item marked **pending** as a
requirement for the implementation work, not as a feature a player can use.

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
| Authoritative recorder and playback runner | Pending implementation |
| `.lgdemo` encoder, decoder, and verifier | Pending implementation |
| Saved-demo commands and automatic recording setting | Pending command names and implementation |
| Replay session, cameras, HUD, and controls | Pending implementation |
| Rolling server buffer and lethal segment extraction | Pending implementation |
| UDP replay transfer and ordinary remote killcam | Pending implementation |
| Team-mode visibility filtering | Not approved; ordinary remote killcams stay disabled there |
| Replay and bot compatibility test | Pending implementation; required after the bot merge |

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
