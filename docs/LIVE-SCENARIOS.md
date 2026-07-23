# Live client/server scenarios

Phase 2 runs schema-version 1 scenarios through the real client, UDP transport,
authoritative server, snapshots, prediction, interpolation, and renderer. The
headless Phase 1 runner remains the strict repeat-hash check.

## Run one live scenario

Build the required targets:

```powershell
cmake --preset default
cmake --build --preset default --target lg_duel_client lg_duel_server lg_duel_scenarios
```

Run an attested SDL_GPU/Vulkan case:

```powershell
python scripts/lg_live_scenario.py scenarios/live/gpu_capture_after_event.json
```

Use the fallback renderer only when the scenario does not require GPU
attestation:

```powershell
python scripts/lg_live_scenario.py scenarios/live/basic_forward.json `
  --renderer fallback --allow-fallback
```

The runner first asks `lg_duel_scenarios --validate-only` for canonical JSON.
Python does not define a second scenario schema. The server receives that saved
canonical file, not the mutable source file. The runner then uses fresh ports
and a run token, launches owned binaries, waits for server setup, arms a
token-bound start gate, sends input through the normal client command path,
waits for ticks, snapshots, and command acknowledgements, gathers state, and
stops only its own processes.

The MCP adapter exposes the same flow as `lg_run_live_scenario`.

## Live schema fields

Set `execution.mode` to `client_server`. Live scenarios use the same world,
players, timeline, and authoritative assertions as headless scenarios.

An optional network profile uses the terms supported by the client UDP
simulator:

```json
{
  "network": {
    "latency_ms": 40,
    "jitter_ms": 0,
    "packet_loss_percent": 0,
    "reorder_percent": 0,
    "seed": 1234
  }
}
```

The seed is required. The current simulator applies one profile to both
directions. It does not inject duplicate packets. Evidence records a bounded
list of each generated immediate, delayed, dropped, and reordered choice.

Each live assertion must name one class:

- `AUTHORITATIVE_DETERMINISTIC`
- `CLIENT_BOUNDED`
- `RENDERER_ATTESTED`
- `VISUAL_REVIEW`

Authoritative player, projectile, event, and hash checks reuse the Phase 1
assertion code. Live checks cover command acknowledgement, one-tick edge counts,
pending commands, correction distance and count, convergence, connection state,
renderer identity, and capture size.

Captures may wait for an authoritative tick or event. `wait_rendered_frames`
only waits for rendered frames; it does not schedule play input. A tick capture
may run at an input boundary or in a gap, but it may not split an active input
span. The runner records the requested tick and its trigger checkpoint. It then
samples the accepted snapshot, presentation, server, and client ticks after the
frame wait and just before capture.

Live runs use one owned process pair. `execution.repeat` must be `1`; repeat a
live case by launching it again. Live runs allow at most 2,500 server ticks and
1,250 ticks in one input span. These bounds also cap checkpoint and event data.

## Typed state and control

The server accepts live setup only when all three private launch flags are
present: a scenario file, a fresh run folder, and a run token. It waits for one
real local player, then applies the existing typed `ScenarioSetup`. Bots remain
server-owned. Normal server launches do not read scenario files or capture
scenario state.

The server writes `ready.json` while it keeps the client connected. It starts
the scenario clock only after an atomic `start.request.json` with the same run
token arrives. It then reapplies typed setup and writes checkpoint zero. This
keeps startup and renderer work outside the scenario clock. Evidence records
the declared tick, dispatch tick, and late-tick count. OS scheduling can still
make dispatch a few ticks late, so live command timing remains bounded.
The runner fails if dispatch is more than 32 server ticks late. Live assertions
run at completion; authoritative `at_tick` checks remain a headless feature.

The client control service adds typed operations for:

- client state
- network simulation
- client-tick wait
- snapshot-tick wait
- command-acknowledgement wait

Client state includes predicted and last authoritative local state, pending
command age, correction vector and size, interpolation state, ignored duplicate
and stale snapshot counts, transport facts, and network simulation choices.
It also includes the largest pending-command count and correction distance
seen since prediction started. Bounded maximum checks use those high-water
values, not only the final sample. Edge checks use the server's cumulative
execution counts rather than edge sequence IDs.

One-tick edges travel in the same typed input request as the held input span.
The client clears each named edge after the first generated command while it
keeps movement and other held input active.

The first live setup supports fields already present in Phase 1. LG Duel has no
armour state, so live setup cannot set armour. One real human plus typed bots is
supported. More than one real client is not yet supported.

## Evidence and failure handling

Each run writes `manifest.json`, `summary.json`, `junit.xml`, and
`environment.json`. The scenario folder contains:

```text
scenario.json
result.json
assertions.json
authoritative-events.json
client-events.json
client-samples.json
server-state.json
client-state.json
network-decisions.json
hashes.json
reconciliation.json
process-log.json
client.stdout.log
client.stderr.log
server.stdout.log
server.stderr.log
screenshots/
```

Per-tick checkpoints keep the latest 256 authoritative events. The final
result keeps the full bounded journal.

The runner writes partial evidence after validation, launch, setup, control,
capture, or shutdown failures. A GPU request fails if Vulkan preflight,
renderer identity, GPU identity, or capture checks fail. It never changes to
the fallback renderer on its own.

Owned live clients use a 1280 by 720 window for capture checks, regardless of
archived user video settings. The override applies only when the launcher sets
explicit live-scenario mode. It does not save the values to `client.cfg`.
A screenshot assertion requires the reported file to exist. The runner reads
the PNG header and checks its width and height against both the renderer report
and the scenario.

Live process timing depends on the OS, renderer, and packet schedule. Use exact
headless hashes for strict determinism. Use bounded tick or numeric checks for
prediction, acknowledgement, reconciliation, and recovery. A screenshot proves
which renderer made a frame and records its size; it is not the sole play check.
