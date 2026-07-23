Implement deterministic gameplay scenarios, automated performance-regression comparisons, and pull-request CI integration in the LG-duel repository.

## Repository context

Before changing code, inspect the latest `main` and reuse the systems that already exist.

The repository now includes:

* an authoritative 125 Hz server simulation
* client prediction and reconciliation
* snapshot interpolation
* simulated network latency and packet loss
* benchmark scenarios and telemetry
* fixed-camera benchmark configuration files
* GPU renderer attestation
* screenshot and multi-view capture
* a localhost-only developer-control service
* an MCP adapter for Codex
* tick-bounded player input through the normal client command path
* basic player-state reporting
* cvar and game-console control
* asset-pipeline validation and review reports
* CTest and Python test infrastructure

The developer-control/MCP layer already supports operations equivalent to:

```text
lg_start
lg_stop
lg_restart
lg_status
lg_load_map
lg_reload_map
lg_get_camera
lg_set_camera
lg_set_player_view
lg_set_player_weapon
lg_send_input
lg_wait_frames
lg_get_cvar
lg_set_cvar
lg_exec_console
lg_capture_screenshot
lg_capture_map_views
lg_run_benchmark
```

`lg_send_input` can apply movement, attack, jump, dash, crouch, sneak, zoom, view angles, and weapon selection for a bounded number of simulation ticks. It enters the normal client command path and must remain that way.

Do not create a second unrelated game-control system.

Build the scenario runner on top of, or alongside, these existing primitives while using direct simulation APIs where a faster headless execution mode is appropriate.

---

# Primary goals

Create a system that allows an autonomous coding agent to:

1. express gameplay behaviour as deterministic, versioned scenarios
2. run those scenarios without human input
3. verify authoritative state and gameplay events
4. detect nondeterministic divergence
5. compare performance between a baseline revision and a candidate revision
6. attach structured evidence to a pull request
7. fail pull-request CI when correctness or configured performance limits regress

The finished system must work locally and in GitHub Actions.

Do not make unrelated gameplay or rendering redesigns.

---

# Part 1: Deterministic gameplay scenario system

## 1. Versioned scenario format

Create a declarative, versioned scenario format, preferably JSON unless another existing repository convention is clearly better.

A scenario should be able to specify:

* schema version
* scenario name and description
* execution mode
* map
* game mode
* random seed
* server tick rate
* maximum simulation ticks
* initial players and entities
* teams
* health, armour, ammo, and weapons
* spawn positions
* view angles
* bots or scripted participants
* network simulation settings
* cvar overrides
* scripted input timeline
* expected authoritative events
* state assertions
* deterministic hash expectations
* optional screenshots
* optional benchmark collection

Example shape:

```json
{
  "schema_version": 1,
  "name": "rocket_splash_blocked_by_wall",
  "execution": {
    "mode": "client_server",
    "max_ticks": 500,
    "repeat": 3
  },
  "world": {
    "map": "test_arena",
    "game_mode": "duel",
    "seed": 12345
  },
  "players": [],
  "timeline": [],
  "assertions": [],
  "captures": []
}
```

The exact structure may differ, but it must remain:

* deterministic
* versioned
* validated
* readable in source control
* easy for an autonomous agent to generate
* explicit about units and coordinate systems
* independent of wall-clock timing

Invalid scenarios must fail with clear field-specific errors.

Do not accept unknown fields silently unless the schema explicitly supports extensions.

---

## 2. Execution modes

Support at least two execution modes.

### Headless authoritative simulation

Use the fastest practical path for deterministic correctness testing.

This mode should:

* avoid SDL and rendering
* avoid wall-clock sleeps
* directly step simulation ticks
* accept the same conceptual input commands as live play
* expose authoritative state and events
* generate per-tick or checkpoint hashes

### Real client/server path

Use the existing client, server, developer-control, and MCP infrastructure where practical.

This mode should verify systems including:

* input command creation
* command transmission
* server receipt
* prediction
* reconciliation
* snapshot delivery
* interpolation
* rendering
* GPU captures

Reuse existing operations such as:

* `send_input`
* `set_player_view`
* `set_player_weapon`
* `wait_frames`
* `status`
* screenshot capture

Do not directly mutate server state to simulate ordinary player input.

It is acceptable for explicit test setup operations to establish initial state, but these must be clearly separated from normal gameplay commands.

---

## 3. Deterministic initial-state setup

The current developer-control status mainly exposes the local player and is insufficient for full gameplay verification.

Add a safe, typed test setup interface that can establish deterministic scenario state.

It should support, where practical:

* loading a specific map and revision
* selecting game mode
* setting a random seed
* creating or removing bots
* assigning teams
* positioning players
* setting view angles
* setting health, armour, ammo, and selected weapon
* resetting projectiles, events, scores, and round state
* starting, pausing, resetting, and stepping the scenario

This interface must:

* be available only in explicit development/test mode
* remain localhost-only for live control
* use typed operations
* validate all values
* not expose arbitrary memory modification
* not expose shell or process execution
* not alter normal network protocol behaviour

Prefer typed operations over `exec_console`.

The existing generic console operation may remain for interactive development, but deterministic scenarios must not depend on arbitrary console strings when a typed operation can be provided.

---

## 4. Tick-based input timeline

Allow scenarios to schedule input by authoritative or client-command tick.

Support at minimum:

* forward movement
* right movement
* up movement if applicable
* jump
* crouch
* dash
* sneak
* attack
* zoom
* weapon selection
* absolute or relative yaw
* absolute or relative pitch
* input hold duration
* one-tick action edges

Use the same input semantics as the existing `lg_send_input` operation.

Example:

```json
{
  "at_tick": 20,
  "player": 0,
  "input": {
    "weapon": "rocket_launcher",
    "yaw": 90.0,
    "pitch": -5.0,
    "attack": true
  },
  "duration_ticks": 1
}
```

Do not use wall-clock sleeps for gameplay sequencing.

`wait_frames` may be used only for renderer synchronization and capture, not gameplay timing.

---

## 5. Authoritative gameplay event journal

Add a structured event journal suitable for machine verification.

It should contain stable, serializable events such as:

* command accepted
* weapon selected
* weapon fired
* projectile spawned
* projectile moved
* projectile impacted world
* projectile impacted player
* explosion created
* damage attempted
* damage blocked
* damage applied
* knockback applied
* headshot
* player killed
* player respawned
* item picked up
* score changed
* round started
* round ended
* objective state changed
* prediction correction
* reconciliation
* snapshot accepted
* snapshot rejected as duplicate or stale

Each event should include the fields relevant to that event, including where applicable:

* authoritative simulation tick
* command sequence
* actor player index
* target player index
* entity ID
* weapon
* damage amount
* position
* direction
* requested rewind tick
* applied rewind tick
* state before
* state after
* stable event sequence

Do not expose:

* pointers
* memory addresses
* unordered iteration-dependent identifiers
* unstable implementation details

Use bounded storage or streamed output so long test runs cannot cause unbounded memory growth.

The journal should be available to:

* headless scenario execution
* client/server scenario execution
* test artifacts
* assertion evaluation

---

## 6. Assertions

Support structured assertions for at least:

### Player state

* position within tolerance
* velocity within tolerance
* health
* armour
* ammo
* alive/dead
* grounded state
* selected weapon
* cooldown state
* freeze state
* knockback state

### Projectile state

* projectile exists
* projectile removed
* owner
* weapon type
* position within tolerance
* velocity within tolerance
* impact tick
* direct-hit target

### Combat

* weapon fired
* pellet count
* damage dealt
* damage not dealt
* splash damage blocked by geometry
* headshot status
* kill attribution
* self-damage
* knockback magnitude or direction
* lag-compensation rewind amount

### Match state

* score
* round phase
* winner
* respawn
* objective ownership
* objective transition

### Networking and prediction

* command acknowledged
* correction magnitude below threshold
* snapshot queue depth below limit
* duplicate snapshots ignored
* recovery after packet delay or loss
* no unbounded pending-command growth

### Determinism

* final-state hash
* checkpoint hash
* repeated-run hash equality
* event-stream hash equality

Example failure:

```text
Scenario: rocket_splash_blocked_by_wall
Tick: 184
Assertion: player[1].health == 100
Actual: 63
Relevant event:
  type: damage_applied
  weapon: rocket_launcher
  source: player[0]
  target: player[1]
  explosion_position: [12.4, 8.1, 1.2]
```

Assertions must return structured JSON in addition to readable text.

Do not use screenshots as the sole correctness oracle for gameplay behaviour.

---

## 7. State hashing and divergence reporting

Create stable deterministic state hashing.

Include authoritative state such as:

* players
* projectiles
* scores
* match state
* objective state
* RNG state or deterministic RNG position
* relevant cooldowns
* relevant gameplay entities

Exclude:

* wall-clock values
* pointer values
* unordered container iteration order
* renderer timing
* OS-specific paths
* non-authoritative presentation state unless explicitly testing presentation

Support:

* final-state hash
* per-tick hashes
* configurable checkpoint hashes
* event-stream hashes
* repeat execution

When two repeated executions diverge, report:

```text
First differing tick: 184
Subsystem: projectiles
Entity: projectile[3]
Field: position.x
Run A: 12.421875
Run B: 12.429688
```

Where exact field-level comparison is not practical, report the smallest stable state partition that differs.

---

## 8. Initial scenario suite

Create a small, meaningful starter suite.

Include scenarios for:

### Core movement

* standing still
* forward movement
* jump and landing
* crouch
* dash
* player collision/body blocking
* movement after artificial packet delay

### Hitscan weapons

* direct railgun hit
* railgun miss
* headshot multiplier
* machine-gun spread determinism
* revolver independent cooldown
* shotgun centre pellet and pattern determinism

The shotgun test should be designed to expose the currently reported issue where the weapon can stop functioning at medium or long range and where visual pellet patterns may not match hit registration.

Do not change shotgun gameplay merely to make the test pass unless that change is explicitly part of the implementation and justified.

### Projectiles

* rocket direct hit
* rocket splash in open line of sight
* rocket splash blocked by world geometry
* rocket self-damage
* grenade bounce
* grenade resting and fuse
* plasma direct hit
* projectile lifetime expiry

The wall-blocked rocket scenario must initially reflect actual behaviour. If it exposes the known through-wall splash problem, preserve the failing scenario as a targeted regression test and either:

1. fix the bug as a small, isolated correctness change, or
2. clearly mark it as an expected failure linked to the existing issue

Do not silently encode the current broken behaviour as correct.

### Networking and reconciliation

* fixed artificial latency
* packet loss
* reordered snapshots
* duplicate snapshots
* late snapshots
* bounded recovery after a 20 ms, 50 ms, 100 ms, and 250 ms stall
* no duplicated one-tick attack or jump edges
* no unbounded prediction-command accumulation

### Match behaviour

* death and respawn
* score change
* round transition
* at least one McGuffin state transition if practical

Choose scenarios that can be made stable using existing maps and systems.

Do not add superficial scenarios solely to increase the count.

---

## 9. Scenario outputs

Write an evidence directory such as:

```text
scenario-results/
  manifest.json
  summary.json
  junit.xml
  environment.json
  scenarios/
    rocket_splash_blocked_by_wall/
      scenario.json
      result.json
      assertions.json
      events.json
      final-state.json
      hashes.json
      divergence.json
      log.txt
      screenshots/
```

The summary should include:

* total passed
* total failed
* total expected failures
* total skipped
* execution mode
* runtime
* repeat count
* state hashes
* first failure
* artifact paths

Upload artifacts even when execution fails.

---

# Part 2: Performance benchmark comparison

## 10. Reuse the existing benchmark framework

The repository already has:

* benchmark configuration JSON
* benchmark schema/version fields
* scenario hashes
* camera paths
* actor configuration
* renderer requirements
* telemetry CSV output
* subsystem timing
* screenshot capture
* benchmark result comparison helpers
* GPU renderer verification
* fixed outline benchmark configurations

Extend these systems.

Do not create an unrelated second benchmark format.

Existing benchmark configs such as the fixed native-outline and legacy-outline comparisons should remain usable.

---

## 11. Baseline-versus-candidate workflow

Create a command that can:

1. determine the PR merge base or accept an explicit baseline commit
2. create isolated baseline and candidate worktrees or build directories
3. build the same targets with the same configuration
4. run identical benchmark scenarios
5. collect multiple repetitions
6. verify comparability
7. calculate aggregate statistics
8. classify the result
9. emit JSON and Markdown reports

Support forms equivalent to:

```text
compare-benchmarks --baseline <commit> --candidate HEAD
```

and:

```text
compare-benchmarks \
  --baseline-results <directory> \
  --candidate-results <directory>
```

Avoid modifying or polluting the developer’s current working tree.

---

## 12. Comparability validation

Before comparing results, verify matching or compatible values for:

* benchmark schema version
* benchmark implementation version
* scenario name
* scenario hash
* map
* map revision or content hash
* renderer backend
* GPU requirement
* GPU identity
* GPU driver
* Vulkan or graphics API version
* CPU identity
* operating system
* resolution
* fullscreen mode
* VSync
* present mode
* frame cap
* FOV
* server tick rate
* warmup length
* measured duration
* actor count
* actor behaviour
* cvars
* build type
* compiler
* protocol version
* relevant runtime settings

Return `NOT_COMPARABLE` when material fields differ.

Do not compare GPU results from different renderer backends or silently accept an SDL_Renderer fallback for a GPU benchmark.

---

## 13. Metrics

Compare at least:

### Frame performance

* frame-time median
* frame-time p95
* frame-time p99
* frame-time maximum
* frame count
* long-frame count
* frame-time variance or another useful stability measure

Maximum frame time must be reported but not used alone as a failure criterion.

### Simulation

* simulation-tick median
* simulation-tick p95
* simulation-tick p99
* catch-up tick count
* dropped or clamped simulation work
* overload recovery

### Existing CPU subsystems

Include existing telemetry for relevant subsystems such as:

* networking
* snapshot decode
* movement
* collision
* world trace
* projectile simulation
* prediction
* reconciliation
* interpolation
* visibility
* world construction
* render construction
* command encoding
* render submission
* UI
* acquisition
* presentation

Use the actual telemetry names present in the repository.

### Networking

* snapshot bytes
* maximum packet bytes
* packets per second
* kilobits per second
* packet loss
* snapshot jitter
* snapshot age
* late snapshot count
* reordered snapshot count
* queue depth
* overflow or dropped-packet count

Apply hard failure when an application datagram exceeds the existing 1,200-byte limit or when authoritative snapshot encoding fails.

### Resource behaviour

Where available:

* allocations after warmup
* queue growth
* high-water marks
* buffer reuse
* command-history size
* snapshot queue size
* projectile-pool saturation

---

## 14. Add missing GPU and latency telemetry carefully

The current benchmark infrastructure has detailed CPU timings but does not yet provide complete GPU pass timing or input-to-present latency.

Where supported by the existing SDL_GPU/Vulkan architecture, add:

* GPU timestamp queries for major passes
* world pass GPU duration
* player/weapon pass GPU duration
* outline mask GPU duration
* outline dilation GPU duration
* outline composite GPU duration
* UI GPU duration
* total submitted GPU frame duration
* acquire timing
* submit timing
* present-related timing available through the API
* GPU query availability and validity flags

Do not block every frame waiting for GPU timestamps.

Use buffered asynchronous readback where practical.

If reliable display scan-out timing is not available, do not label submit or present timing as full input-to-photon latency.

Add explicit pipeline timestamps for:

* input event received
* user command created
* command packet sent
* command received by server
* authoritative simulation tick applied
* snapshot sent
* snapshot received
* snapshot accepted
* presentation state selected
* render submission

Produce component latency measurements without claiming to measure physical display latency unless actual display instrumentation exists.

---

## 15. Statistical noise handling

Performance comparisons are noisy.

Implement:

* warmup
* multiple repetitions
* median aggregation across repetitions
* sample counts
* run-level results
* outlier visibility
* minimum absolute threshold
* minimum relative threshold
* configurable confidence or stability rules
* `INCONCLUSIVE` status

A regression should usually require:

```text
relative regression exceeds threshold
AND
absolute regression exceeds threshold
```

Do not fail because of a single tiny percentage movement.

Do not remove noisy samples without reporting the exclusion rule.

---

## 16. Version-controlled performance policy

Create a policy file defining thresholds.

Classifications should include:

```text
PASS
WARN
FAIL
INCONCLUSIVE
NOT_COMPARABLE
UNAVAILABLE
```

Distinguish:

### Hard failures

Examples:

* packet exceeds 1,200 bytes
* authoritative snapshot cannot encode
* queue exceeds a defined safe bound
* benchmark requested GPU but used fallback renderer
* benchmark configuration invalid
* benchmark crashed
* required metric missing

### Regression failures

Examples:

* substantial frame p95 increase
* substantial simulation p99 increase
* major packet-size increase
* major world-trace cost increase
* major GPU outline-pass increase

### Warnings

Examples:

* smaller but repeatable regressions
* increased variance
* small packet growth
* unavailable optional GPU data

Use conservative initial defaults.

Do not create thresholds so strict that ordinary GitHub-hosted-runner variance causes frequent false failures.

---

## 17. Reports

Produce machine-readable JSON such as:

```json
{
  "status": "WARN",
  "baseline_commit": "abc123",
  "candidate_commit": "def456",
  "comparable": true,
  "repetitions": 5,
  "metrics": {
    "frame_time_p95_ms": {
      "baseline": 2.31,
      "candidate": 2.38,
      "absolute_change": 0.07,
      "relative_change_percent": 3.03,
      "status": "PASS"
    }
  }
}
```

Produce a Markdown report for GitHub Actions summaries:

```text
## Performance comparison

| Metric | Baseline | Candidate | Change | Status |
|---|---:|---:|---:|---|
| Frame p95 | 2.31 ms | 2.38 ms | +0.07 ms / +3.0% | PASS |
| Snapshot bytes | 1,148 B | 1,382 B | +234 B / +20.4% | FAIL |

Largest regression:
Snapshot bytes exceeded the configured packet budget.

Largest improvement:
World visibility p95 improved by 8.1%.
```

Include:

* top regressions
* top improvements
* noisy metrics
* unavailable metrics
* comparability warnings
* artifact locations

---

# Part 3: Pull-request CI

## 18. Current CI problem

The existing workflows primarily package Windows builds and publish the server Docker image.

The Windows package workflow currently disables tests with:

```text
BUILD_TESTING=OFF
```

The current workflows do not provide a complete required pull-request test gate.

Recent large changes have been merged with PR descriptions stating that tests were not run.

Correct this.

---

## 19. Required PR workflows

Add pull-request workflows for:

### Linux correctness

* configure
* build
* run CTest
* run Python tests
* run headless deterministic scenarios
* run sanitizers where practical

### Windows correctness

* configure
* build
* run CTest
* run relevant Python tests
* verify packaging targets where useful

### Determinism

* run selected scenarios at least three times
* compare final-state and event hashes
* upload divergence artifacts on failure

### Bounded PR performance

* run a short, stable subset suitable for GitHub-hosted runners
* gate reliable CPU and headless metrics
* enforce packet limits
* classify noisy results as inconclusive rather than failing incorrectly
* explicitly mark GPU metrics unavailable when the runner lacks the required backend

### Full benchmark workflow

Provide a manually triggered workflow for:

* self-hosted or trusted GPU runners
* full baseline/candidate comparison
* multiple repetitions
* GPU timings
* screenshots
* complete telemetry artifacts

Add workflow concurrency so obsolete runs for the same PR are cancelled.

Always upload artifacts, including on failure.

---

## 20. Required status checks and documentation

Document which jobs should become required branch-protection checks.

Suggested checks:

```text
linux-build-and-tests
windows-build-and-tests
deterministic-scenarios
protocol-and-packet-budgets
performance-smoke
```

Do not attempt to change repository branch-protection settings unless the available tooling explicitly supports it and the task authorizes that change.

Document the exact GitHub settings the repository owner should enable.

---

# Part 4: Tests for the new infrastructure

Add focused unit and integration tests for:

* scenario schema parsing
* schema-version rejection
* unknown-field handling
* invalid map or weapon rejection
* deterministic initial-state setup
* tick-based input playback
* one-tick input edges
* player-state assertions
* event assertions
* position tolerances
* event serialization
* stable event ordering
* stable state hashing
* repeat-run determinism
* first-divergence reporting
* JUnit output
* JSON summary output
* benchmark result parsing
* comparability validation
* policy parsing
* threshold evaluation
* hard packet-budget failures
* warning classification
* noisy/inconclusive classification
* unavailable GPU telemetry
* Markdown report generation

Add fixtures demonstrating:

* pass
* assertion failure
* deterministic divergence
* benchmark pass
* benchmark warning
* benchmark failure
* not comparable
* inconclusive
* unavailable GPU metric

Include at least one deliberately broken scenario and confirm it fails for the expected reason.

---

# Guardrails

Do not:

* build a second unrelated developer-control service
* bypass the normal client command path for ordinary gameplay input
* use wall-clock sleeps for deterministic gameplay
* depend on arbitrary `exec_console` commands when typed operations are available
* expose shell execution
* expose generic memory mutation
* weaken existing tests
* delete telemetry to hide regressions
* update expected values merely because current code fails
* silently skip failed scenarios
* silently change renderer backends
* compare incompatible benchmark runs
* claim GPU verification when a GPU benchmark did not run
* claim input-to-photon measurement without display-level instrumentation
* use screenshot comparison as the sole gameplay oracle
* perform broad unrelated refactors

Avoid adding more unrelated responsibilities directly to the already large `GameApp.cpp`.

Where practical, introduce focused components such as:

```text
ScenarioSchema
ScenarioRunner
ScenarioStateSetup
ScenarioInputPlayback
GameplayEventJournal
ScenarioAssertions
StateHasher
DivergenceReporter
BenchmarkComparator
PerformancePolicy
EvidenceWriter
```

Keep normal game runtime overhead effectively zero when scenario, benchmark, telemetry, and developer-control modes are disabled.

---

# Verification before completion

Before claiming completion:

1. build all affected targets
2. run all existing CTest tests
3. run all existing Python tests
4. run all new tests
5. run every new deterministic scenario at least three times
6. confirm repeated hashes are identical
7. run deliberate failing scenarios and verify their diagnostics
8. run benchmark-comparison fixtures
9. perform a real baseline-versus-candidate CPU benchmark where the environment permits
10. run GPU benchmarks only when the intended GPU backend is verified
11. inspect generated JSON, JUnit, Markdown, logs, and screenshots
12. confirm CI uploads artifacts after failure

Do not write “tests not run” in the final report unless the environment genuinely made a test impossible. Clearly distinguish:

* passed
* failed
* unavailable
* not attempted
* unsupported in the environment

---

# Final report

Provide a detailed completion report containing:

* architecture added
* files changed
* scenario schema
* execution modes
* typed setup operations
* supported assertions
* event journal format
* hashing method
* divergence-report format
* included scenarios
* benchmark-comparison design
* performance policy
* GPU telemetry support
* CI workflows added or modified
* required-check recommendations
* exact commands run
* test results
* scenario repeat hashes
* example assertion failure
* example divergence report
* example benchmark report
* uploaded artifact layout
* known limitations
* remaining issues

Explicitly mention the status of these known concerns:

* rocket splash through world geometry
* shotgun centre-pellet and pattern behaviour
* snapshot double decoding
* reconstructed grenade collision normals
* GPU timing availability
* input-latency measurement limits

The implementation should leave the repository in a state where a future autonomous Codex task can:

1. implement a feature
2. add or update a deterministic scenario
3. run it locally
4. verify authoritative state and events
5. compare relevant performance
6. generate evidence
7. open a PR whose correctness and performance checks run automatically
8. allow a human to review the completed feature primarily through code, reports, replays or hashes, benchmark results, and captures

---

# Implementation status

- [ ] Phase 1: Deterministic scenario foundation
- [x] Phase 2: Live client/server verification
- [ ] Phase 3: Pull-request CI

Phase 1 code and its focused checks are complete. The box stays unchecked
until the required full test run passes. As of 2026-07-23, the untouched base
commit has the same failures in `lg_duel_server_tests`,
`lg_duel_weapon_switching_tests`, `lg_duel_clan_arena_server_tests`, and
`lg_duel_asset_pipeline_python_tests`.

Phase 2 live verification is complete as of 2026-07-23. It reuses the Phase 1
schema, setup, event journal, assertions, and hashes while running input through
the owned real client, UDP transport, authoritative server, snapshots,
prediction, reconciliation, and renderer. Focused C++ and Python checks pass.
Three repeated fallback runs pass for movement, one-shot edges, and 40 ms
latency. A real 1280 by 720 fallback capture passes after an authoritative rail
event. Forced launch failure leaves no owned process.

The full preset run passes 56 of 60 tests. Its four failures match the
untouched-base failures listed above. SDL_GPU/Vulkan cannot start on this host:
the selected Intel ICD returns `ERROR_INCOMPATIBLE_DRIVER`. The GPU scenario
reports a structured launch failure and does not use the fallback renderer.
Performance comparison, PR gating, and Phase 3 CI work remain unimplemented.
