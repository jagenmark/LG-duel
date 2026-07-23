# Deterministic scenarios

LG Duel scenarios run the real authoritative server at the fixed 125 Hz
simulation rate. They do not start SDL, render, sleep, or use wall-clock time.

## Run scenarios

Configure and build once:

```powershell
cmake --preset default
cmake --build --preset default --target lg_duel_scenarios
```

Run the Phase 1 smoke suite, one named scenario, or one file:

```powershell
.\build\default\lg_duel_scenarios.exe --suite smoke --repeat 3
.\build\default\lg_duel_scenarios.exe --scenario forward_movement
.\build\default\lg_duel_scenarios.exe --scenario .\path\case.json --repeat 3
```

Use `--output <dir>` to choose the evidence root and `--maps <dir>` to choose
the map folder. The root contains `manifest.json`, `summary.json`, `junit.xml`,
and one full artifact set under `scenarios/<name>/<run-id>/`.

## Add a scenario

Copy a small file from `scenarios/smoke`. Schema version 1 requires:

- `execution.mode` set to `headless`
- a maximum tick count and repeat count
- a safe map name, game mode, and explicit seed
- typed player state
- a tick-based input timeline
- assertions at a tick or at completion

Positions use game metres with +Z up. Angles use degrees. Input starts at
`at_tick`, lasts for `duration_ticks`, and enters the normal authoritative
`UserCommand` path. Put `jump`, `dash`, or `attack` in `one_tick_edges` when
the action must happen once.

The parser rejects unknown fields, bad weapons, unsafe map names, overlapping
input for one player, input for bots or disconnected slots, unknown event
types, and values outside the schema limits. A known gameplay bug may name one
assertion with `expected_failure.issue`,
`expected_failure.assertion_index`, and `expected_failure.reason`. A different
failure still fails the run. An unexpected pass also fails the run so the stale
mark gets removed.

Assertions cover player position and velocity with tolerance, health, alive
state, selected weapon, projectile presence or removal, event fields and
counts, and stable state hashes. Failure evidence includes the scenario, run,
tick, expected value, actual value, and a plain diagnostic.

Phase 2 extends schema version 1 with a strict `client_server` mode, live
network settings, check classes, and capture points. See
`docs/LIVE-SCENARIOS.md`. The headless path ignores no live fields: the parser
validates one schema, and each runner rejects the wrong execution mode.

Phase 1 hashes promise repeat stability within one build. They do not promise
the same hash across compilers. The event journal reads stable authoritative
server event fields after each tick and caps each result at 100,000 entries.
It does not serve as a general replay or demo format.
