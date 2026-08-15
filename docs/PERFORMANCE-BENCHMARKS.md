# Performance Benchmarks

LG Duel benchmarks are repeatable, opt-in developer artifacts for finding rendering regressions. They are not a gameplay mode, a player-facing quality setting, or an authority path. Normal client and server launches do not load benchmark scenarios.

Descriptors live in `config/benchmarks/`. Results, screenshots, and comparison baselines belong under `build/benchmarks/<scenario>/<run_group>/<run_id>/` and are ignored by Git. Do not commit a result as a universal performance claim: drivers, power policy, compositor state, and selected backend all matter.

All benchmark scenarios default to `s_volume 0`. The Python runner and native
scenario parser apply and record that default, so sound mixing does not add
noise to CPU results. A descriptor may set another value when audio cost is the
subject of the benchmark; the scenario hash and saved cvar contract then record
that override.

## Graphics profile baseline

Benchmarks default to the named `Default` graphics profile at `render_scale: 1.0`
(100%). Descriptors may select `Low`, `Default`, `Competitive`, or `High`; all four
use 100%. A manual scale is valid from 50% to 150%. Values above 100% are
`Extreme / benchmark-only` and are not recommended for normal play. Native results
record both values and note that captures need the same profile and scale.

Descriptor cvars override matching profile values. `render_scale` remains the
separate top-level descriptor field and wins over a duplicate cvar, so each
artifact has one explicit requested scale.

## Graphics benchmark contract

Every GPU artifact records the requested profile and scale, plus the effective
comparison values: anti-aliasing mode, sun-shadow quality, contact shadows,
material quality, player-rim quality, atmosphere grade, bloom, and render
scale. At benchmark finalization, before cvars restore, the native client reads
each required cvar from its live console and returns the exact console strings
as `effective_cvars`. The runner rejects a run with a missing value; it does not
copy or infer profile values from Python.

The artifact also records the renderer, verified GPU name, graphics-driver name
and version, Vulkan API version, ICD record, selected present mode, and
executable SHA-256. GPU comparison rejects a different profile contract, GPU,
driver, renderer, or presentation mode. Executable SHA-256 is retained as
reported comparison information, not a fatal gate, so a renderer commit can be
compared with its bootstrap base.

`eyetoeye-readability-pan` is the Default control. It pans through the north,
east, and south EyeToEye views with one fixed remote player, and captures the
light, contrast, and dark checkpoints after timing ends.
`eyetoeye-readability-pan-competitive` uses the identical presentation path
with the Competitive profile. These captures check stable lighting, player
silhouette, team/readability cues, and camera-pan stability. They are visual
review evidence, not a claim about server authority.

The native result may contain `render_pass_diagnostics`. The runner copies this
optional object into each normalized run when it is available. Older renderers
do not produce it; absence does not invalidate a result or turn into a zero.
Pass diagnostics are observation data, not a substitute for GPU timestamps.

## Architecture And Trust Boundary

The normal client contains a benchmark recorder that is inert unless the process is launched with both `--dev-control` and `--benchmark`. The recorder uses the same renderer-facing state and map-loading rules as ordinary play, but it does not add timing data or synthetic state to UDP packets, snapshots, or `ServerGame`. The benchmark camera is presentation-only; bot setup still travels through the existing server-authoritative command path.

The existing localhost developer-control plane remains the reusable seam for a live-client benchmark that needs map loading, a development camera, or a real PNG capture. It is opt-in (`--dev-control`/`--control-port`), loopback-only, and unavailable in normal play. A benchmark must use that structured control plane rather than grow a general console or remote-control interface. Its development camera changes only the state passed to rendering, not an authoritative player body.

MCP is separate from gameplay networking. The repository-local MCP adapter calls the same Python orchestration and typed loopback operation as the CLI, then returns summaries and artifact references. It does not own frame timing, make the client authoritative, or expose a non-loopback listener.

## What Is Measured

Each result must label every metric with its scope.

| Category | Meaning |
| --- | --- |
| Direct | Client-thread wall time around a measured render frame; the primary frame-time distribution. |
| Render-frame subsystems | Coarse client-thread CPU spans for network processing, fixed-tick work performed during the frame, movement/collision, traces, interpolation, animation, world visibility, render-instance construction, world and dynamic command encoding, UI, swapchain acquisition, and submission. Nested spans are attribution data and must not be added together as if they were disjoint. |
| Simulation-tick subsystems | One sample per client fixed prediction tick for inclusive tick simulation plus its nested network, movement/collision, and trace spans. A render frame may contain zero or multiple tick samples. |
| Derived | Mean, p50, p95, p99, maximum, FPS conversion, run-to-run deltas, and regression verdicts calculated from direct samples. |
| Backend-specific CPU timing | Renderer diagnostics such as scene build, dynamic upload preparation, swapchain acquire, draw issue, submit, total render, and diagnostics counts. These remain CPU-side observations and retain backend/present-mode labels. |
| GPU execution timing | A total GPU timestamp interval plus named stage intervals for shadow, main scene, view model, bloom, scene composite, outline mask, outline dilation, outline composite, and UI overlay. |
| Unavailable | GPU allocation cost, GPU memory use, general GPU use, and presentation latency. CPU submit/acquire timing is not a substitute for GPU execution time. |

The main GPU interval excludes CPU scene build and command encoding, swapchain
acquire blocking, queue delay before the first command starts, present,
compositor work, scanout, and work in other command buffers. It does not measure
input or presentation latency.

The outline interval covers the compatibility clear and depth work when that
work runs, then the outline mask, dilation, and composite work. It excludes all
other GPU work. Each frame states whether the outline interval applies. A
missing value stays empty; it never becomes zero.

Named stage intervals time the GPU commands for that stage. Some stages do not
run on every profile or frame. The outline total contains its three nested
stages, while uploads and gaps remain only in the primary interval, so stage
values must not be added and treated as the primary total.

GPU timestamps arrive several frames after submission. The renderer tags each
measured command buffer with its exact benchmark frame id. The benchmark polls
ready results during the run, then waits for the remaining results only after
CPU frame sampling stops. It patches the sample with the matching id. Warmup
frames have no tag and never enter either CPU or GPU summaries.

This support needs the SDL_GPU Vulkan path and the optional patched SDL build.
The patch adds a small timestamp and readback cost to measured frames. Results
record whether timing is available, the backend, timestamp valid bits and
period, readback delay, tool version, and the SDL base and patch identity. The
build picks the patched SDL form when it is present. A later official SDL query
API can replace the patch without changing the metric scope. SDL_Renderer and
an unpatched SDL build report a clear reason and leave timing cells empty. GPU
timing support does not affect native artifact validity, but the trusted GPU
enforcement profiles require the primary GPU median and therefore fail without it.

Record selected backend, requested and selected present mode, resolution, fullscreen/vsync/frame-cap state, map content hash, scenario/version, build identity, system/driver information, and any fallback. The shared GPU launcher queries `vulkaninfo --summary` outside the measured interval, verifies the selected Intel ICD before startup, and attests the renderer after control answers. Benchmark child processes remove `VK_DRIVER_FILES` and `VK_ICD_FILENAMES` so the Vulkan loader uses its normal driver search. Results record the physical-device name, driver name/version, Vulkan API version, ICD manifest SHA-256, resolved driver library, and verification state. This metadata is written to both `aggregate.json` and every native `run-*/result.json`. A GPU-required scenario aborts if the renderer falls back or any attested value differs. Comparisons reject a different GPU, graphics-driver version, or Vulkan API version.

### Percentiles

Version 1 uses nearest rank: sort samples ascending and select the one-based rank `ceil(p * N)`. It never interpolates a frame time that was not observed. `p99.9` is reported only for at least 1,000 samples. Changing this convention requires a benchmark-version bump or an explicit incompatible-comparison refusal.

`telemetry.csv` records the per-render-frame subsystem values and
`simulation-ticks.csv` records the independent fixed-tick stream. `result.json`
exposes median, p95, and p99 for every subsystem under
`subsystem_timings.render_frame` and `subsystem_timings.simulation_tick`.
GPU values use the same nearest-rank rule under `gpu_execution_timings`.
`telemetry.csv` keeps `gpu_primary_command_buffer_ms`, `outline_gpu_ms`, and
`outline_gpu_state`, then adds one value and state pair for every named GPU
stage. It also records result receipt, readback delay, and a per-frame failure
reason. Unavailable numbers use empty cells.
All spans use `steady_clock`. Benchmark-only clock reads run only during the
measured stage. Late mouse timing also feeds the live performance display, so
its clock reads run on each frame where the callback runs. Renderer spans are
CPU command construction and API submission time, never GPU execution time.

Late mouse sampling has its own fields because its values describe different
parts of one input path. `late_mouse_sample_ms` is the CPU cost of the late
mouse callback. `mouse_sample_to_submit_ms` is the time from that sample to
render submission. `mouse_sample_phase_gain_ms` is how much later the late
sample ran than the normal frame input sample. The last two values overlap
other frame work, and phase gain is not CPU cost. Do not add any of these
values to the named CPU spans.

`result.json` stores their median, p95, and p99 summaries plus enabled and
applied frame counts under `late_mouse_sample`. For an A/B run, set
`cl_late_mouse_sample 0` in one scenario and `cl_late_mouse_sample 1` in the
other while keeping the rest of the scenario and system state fixed. These
numbers describe app callback timing and sample placement only. They do not
measure mouse device latency, event delivery latency, GPU queue delay, scanout,
or display response, so they are not end-to-end input latency.

Second-based runs reserve for up to 4,096 render samples per second before
measurement; exceeding that safety ceiling aborts instead of reallocating and
polluting the measured tail.

### Per-frame timeline and visual report

Native runs may also write `frame-timeline.json` (schema version `1`). This is
the stable per-frame artifact: each frame has a `frame_index`, elapsed time,
total CPU time, optional total GPU time, named CPU/GPU values, workload
counters, GPU stage states, readback data, a sibling `late_mouse_sample`
object, and an `event_markers` array. The late mouse object keeps callback
cost, sample-to-submit time, phase gain, and enabled/applied flags together
without treating them as additive CPU subsystems. GPU
values are `null` when the backend or one query cannot provide execution
timing; CPU submit or swapchain time must not be relabelled as GPU time. Fixed
simulation ticks are kept as event markers
(with their tick index and name when available), so one render frame can show
zero, one, or several tick events.

`telemetry.csv` and `simulation-ticks.csv` remain the raw measurement source of
truth. The JSON timeline joins those streams for frame inspection and does not
replace either CSV. Reports must use the recorded values and metadata, without
re-measuring a run.

Generate a portable report from a candidate, and optionally a compatible
baseline, with:

```powershell
python scripts/lg_frame_timeline_report.py build/benchmarks/<scenario>/<run-group>/run-1 --baseline build/benchmarks/<scenario>/<baseline-group>/run-1 --output build/benchmarks/<scenario>/timeline-report
```

The command writes a self-contained `frame-timeline.html`, a static
`frame-timeline.svg`, and a machine-readable `timeline-analysis.json`. The HTML has an inspectable frame
timeline, distribution, worst-frame table, pattern summary, and (when the
metadata permits) baseline-versus-candidate deltas. It runs in headless CI and
uses no chart package or network fetch.

Version 1 classifies isolated spikes, bursts, periodic spikes, sustained
regressions, and alternating or sawtooth pacing with fixed deterministic
thresholds. `timeline-analysis.json` records those thresholds, sample counts, and a
confidence level for each finding; too few samples or missing fields produce
`unavailable` rather than a guess. A marker or subsystem that overlaps a spike
is a correlation only. The report does not prove that marker or subsystem
caused the frame cost.

The screenshot below shows the standalone report layout. It uses synthetic
data only and is not a measured LG Duel performance result.

![Synthetic frame timeline report](performance-examples/frame-timeline-synthetic-demo.png)

## Scenario Format

Every descriptor is JSON with `schema_version: 1` and `expected_benchmark_version: 1`. Curated descriptors also carry `benchmark_version: 1` as human-readable metadata. A runner must reject a future or incompatible version rather than guessing semantics.

| Field | Required semantics |
| --- | --- |
| `name`, `labels`, `map` | Safe identifier, searchable classification, and a repository map name. The map must load and its content hash is compatibility data. |
| `backend_requirement` | `gpu` requires the SDL_GPU path; no fallback result may be compared as equivalent. |
| `resolution`, `fullscreen`, `vsync`, `frame_cap`, `fov` | Explicit presentation contract. `frame_cap: 0` means uncapped. |
| `warmup_seconds` or `warmup_frames`; `measured_seconds` or `measured_frames` | Exactly one unit must be supplied for each phase. Warmup has no timing samples; only the measured interval contributes samples. Curated descriptors use explicit 2–5 second warm-ups and 5–25 second measured intervals. |
| `camera_start`, `camera_path` | LG-unit position plus degree yaw/pitch/FOV. `camera_path` is a direct ordered array of keyframes, each using exactly one of normalized `progress` or `time_seconds`. |
| `player_state` | Presentation-only local-player state and UI visibility. It is not a server command. |
| `actors` | Structured requested bot setup: `bots`, `attack_mode`, `stare`, `standstill`, `dodge`, optional dodge intervals, and `expected_count`; `commands` documents the equivalent real console sequence. |
| `effects` | Requested bounded projectile/tracer/explosion load. `fixture_only: true` explicitly marks synthetic presentation content. |
| `cvars` | Narrow presentation-only overrides. They must be emitted in the result and restored/isolated by the runner. |
| `screenshots` | Array of `{ "name", "progress" }`; capture happens after timed sampling, so it cannot change frame statistics. |
| `residual_nondeterminism` | Honest list of known uncontrolled inputs or unsupported conditions; an empty list means none are known. |

Camera progress uses measured elapsed time quantized down to the 125 Hz simulation interval, not mouse input. A time/progress value therefore resolves to the same transform independent of render rate. Keyframes must be nondecreasing and are linearly interpolated between fixed positions/angles; duplicate static endpoints intentionally make a stationary camera explicit.

The supplied scenarios establish a small comparison suite. The graphics-contract
cases add to the retained controls:

- `eyetoeye-readability-pan`: Default-profile light/dark/readability control with a deterministic pan and one stationary remote player.
- `eyetoeye-readability-pan-competitive`: same path and actor contract for Competitive.
- `eyetoeye-static-baseline`: low-action cached-world control.
- `eyetoeye-duel-like`: normal two-participant Lightning-Gun bot duel request.
- `eyetoeye-match-load`: two-player live combat with local Lightning Gun fire,
  one moving and dodging Rocket Launcher bot, full combat effects, and a fixed
  camera. Use repeated runs and inspect its recorded workload counts; it is
  developer data, not a pass/fail gate.
- `eyetoeye-bot-animation`: six-player bot movement/animation request using Machine Gun bot models plus `bot_add`, `bot_weapon`, `bot_dodge`, `bot_stare`, and `bot_standstill`; it is a retained comparison workload, not the 16-player capacity ceiling.
- `machine-gun-visual-slice`: supported bounded authoritative effect load. One local and one remote Machine Gun may fire; pools and cvars cap the presentation load. It remains a live-combat workload, so repeated artifacts matter.
- `eyetoeye-projectile-effects`: a clearly labelled presentation-only fixture. Current bots select only the Lightning Gun and the native runner does not inject synthetic rockets, grenades, plasma, or explosions, so it is deliberately invalid at execution (`supported_workload: false`) rather than silently measuring a different workload. Use the headless `trace-projectile` workload for current quantitative projectile-query evidence.
- `overkill-high-visibility`: static large-map structural/sightline stress using a checked-in `overkill_import` camera preset.
- `eyetoeye-static-long`: 5-second warm-up plus a 25-second static baseline for tail stability.
- `overkill-static-flythrough`: deterministic 15-second presentation-only camera interpolation through all three checked-in Overkill structural views; the world remains static.
- `overkill-static-flythrough-bvh-off`: identical camera workload with only `r_world_frustum_cull` disabled, providing a same-build control for static-world BVH comparisons.
- `overkill-static-flythrough-gpu-indirect`: identical camera workload with CPU world culling disabled and the guarded `r_world_gpu_indirect` prototype enabled. Its GPU command visibility is device and driver dependent, and its legacy visible/culled chunk counters stay zero because the prototype does not read commands back.

`r_world_frustum_cull` is intentionally experimental and defaults off. Promote it
only when repeated same-host comparisons against the `bvh-off` descriptor meet
the frame-median and p95 budgets without excessive material-range inflation.

`r_world_gpu_indirect` is also experimental and defaults off. Compare the GPU
descriptor with both CPU descriptors on the same host, SDL_GPU backend, driver,
power state, resolution, and camera path. Use CPU frame totals and world command
encoding for CPU cost, and the recorded MainScene GPU timestamp for GPU cost.
Do not treat an Intel Core Ultra 7 258V iGPU result as a discrete-GPU result;
it is a valid low-power target and must have its own threshold and report.

The camera coordinates come from `config/dev-camera-presets.json`, not arbitrary map-space guesses. Bot commands are current commands: `bot_add [count]` is permitted only in warmup; `bot_attack 0|off|easy|medium|hard`, `bot_weapon <weapon>`, `bot_stare`, `bot_standstill`, and `bot_dodge` control supported training behavior. Bots default to Machine Gun, and benchmark scenarios can select another authoritative bot weapon with `actors.weapon`.

## Running, Repeating, And Comparing

Benchmarks use the optimized `perf` Release preset by default. Configure it once,
then rebuild it incrementally after each code change:

```powershell
cmake --preset perf
cmake --build --preset perf
```

The wrapper starts an owned client from `build/perf` with the explicit benchmark
flag. The PowerShell wrapper owns the supported CLI contract:

```powershell
.\scripts\lg-benchmark.ps1 list
.\scripts\lg-benchmark.ps1 run --scenario eyetoeye-static-baseline --repetitions 5 --json
.\scripts\lg-benchmark.ps1 run --scenario eyetoeye-match-load --graphics-profile Low --repetitions 5 --json
.\scripts\lg-benchmark.ps1 baseline-create --scenario eyetoeye-static-baseline --name gpu-driver-current --repetitions 5
.\scripts\lg-benchmark.ps1 compare --baseline gpu-driver-current --result build/benchmarks/eyetoeye-static-baseline/<run-group> --threshold-percent 5 --tail-threshold-percent 8
.\scripts\lg-benchmark.ps1 compare-modes --result build/benchmarks/overkill-static-flythrough/<run-group> --result build/benchmarks/overkill-static-flythrough-bvh-off/<run-group> --result build/benchmarks/overkill-static-flythrough-gpu-indirect/<run-group>
.\scripts\lg-benchmark.ps1 report --result build/benchmarks/eyetoeye-static-baseline/<run-group> --detailed
```

`compare-modes` accepts only the three named Overkill static-world results. It
allows their mode name, scenario hash, labels, notes, and the two culling cvars
to differ. It still requires the map, camera, presentation settings, graphics
contract, build, host, backend, driver, resolution, and Git state to match.

Rendered benchmarks use their own local session. The defaults are UDP server
port `28960`, TCP control port `28961`, and launcher state at
`build/benchmark-control/28960-28961`. Normal visual control stays on
`27960/27961` with `build/dev-control`. Set another pair with
`--server-port` and `--control-port`; the runner derives a state folder from
both values. The old `--port` option is an alias for `--control-port`. If both
are set, they must match.

The runner checks both ports without sending data to them. It rejects a busy
port, equal ports, bad port values, and an existing or broken state file. An
atomic claim in the pair's state folder blocks two benchmark runners from
sharing that pair. An owned run stops only the client and server in its
benchmark state before it returns, including error paths. Failed cleanup marks
the saved aggregate invalid. An attached test run never claims or stops the
external session. Results record both ports and the derived state folder under
`environment` and in each native run's `run_conditions`.

Use `--build-mode debug` on `run`, `sim-run`, or `baseline-create` only when a
Debug measurement is intentional. Debug mode selects `build/default`; Release
and Debug artifacts are recorded as different build modes and cannot be compared
as a normal regression result.

```powershell
.\scripts\lg-benchmark.ps1 run --scenario eyetoeye-static-baseline --build-mode debug
```

Shared simulation hot paths have a separate headless executable so renderer scheduling cannot be mistaken for collision cost. The same PowerShell entry point builds artifact-compatible reports:

```powershell
.\scripts\lg-benchmark.ps1 sim-run --workload movement-collision --map overkill_import --repetitions 5 --warmup-batches 40 --measured-batches 60 --operations-per-batch 256
.\scripts\lg-benchmark.ps1 sim-run --workload trace-projectile --map overkill_import --repetitions 5 --warmup-batches 60 --measured-batches 100 --operations-per-batch 256
```

### Revision and result-set comparison

Phase 3 adds one policy-based comparison command on top of these artifacts:

```powershell
python scripts/lg_compare_benchmarks.py `
  --baseline origin/main `
  --candidate HEAD `
  --suite pr_headless `
  --repetitions 5 `
  --profile pr_headless `
  --output build/verification/benchmarks
```

The revision mode resolves both refs to commits, rejects an uncommitted candidate,
and creates two detached worktrees below the new output directory. It configures
separate Release builds with the same options, runs the same bounded suite, copies
raw results and logs into the output, then removes only the worktrees and build
trees it created. It never resets, cleans, or changes the active worktree. A failed
configure, build, run, or cleanup still leaves a partial `manifest.json`.

Existing result sets can be checked without a rebuild:

```powershell
python scripts/lg_compare_benchmarks.py `
  --baseline-results build/verification/baseline `
  --candidate-results build/verification/candidate `
  --profile pr_headless `
  --output build/verification/comparison
```

Each result root must contain exactly one valid `aggregate.json` for every scenario
required by the selected policy profile. Missing, extra, duplicate, malformed, or
mixed scenario artifacts fail before any percentage is calculated.

For the graphics-contract controls, collect exactly five runs of one profile
and compare only its matching policy profile:

```powershell
.\scripts\lg-benchmark.ps1 run --scenario eyetoeye-readability-pan --repetitions 5 --json
python scripts\lg_compare_benchmarks.py --baseline-results <default-baseline> --candidate-results <default-candidate> --profile trusted_gpu --output build\verification\graphics-default

.\scripts\lg-benchmark.ps1 run --scenario eyetoeye-readability-pan-competitive --repetitions 5 --json
python scripts\lg_compare_benchmarks.py --baseline-results <competitive-baseline> --candidate-results <competitive-candidate> --profile trusted_gpu_competitive --output build\verification\graphics-competitive
```

The accepted median GPU regression budgets are Default `<= +25%` and
Competitive `<= +15%`. The measured GPU median is required for both enforcement
profiles: missing timestamp data fails the comparison and cannot produce a
budget pass. Each cap has a `0.05 ms` absolute measurement floor. A result above
the cap fails only when its absolute rise exceeds that floor; an above-cap shift
at or below the floor is `INCONCLUSIVE`, never `PASS` or `WARN`. A zero baseline
has no usable ratio, so normal absolute limits apply. Both profiles require the
primary-GPU p99 and 16.67 ms long-frame checks as spike safeguards. Frame and
CPU breakdowns remain diagnostic: noisy or absent values appear in the report
but do not block a valid GPU comparison. The GPU profiles do not require
network-datagram, snapshot-encode, or launcher-cleanup evidence; those checks
remain required only for `pr_headless`. An unpatched build may still produce an
observe-only artifact, but it cannot satisfy either enforcement profile.

`config/performance-policy.json` is the versioned source of truth. The
`pr_headless` profile uses five runs of the two shared-simulation workloads and
conservative CPU limits. It also requires the same compiler version, build type,
generator, simulation build options, and collision query mode. SDL source,
fetch, require, tag, and patched-build settings appear under
`environment.sdl_configuration`; `pr_headless` ignores them because its
benchmark links only the shared core. The `trusted_gpu` profile compares the
Default readability-pan control and `trusted_gpu_competitive` compares its
Competitive counterpart. Both compare `environment.compile_time_options` and
`environment.sdl_configuration`, and require five verified SDL_GPU/Vulkan runs
on the same SDL setup, profile contract, GPU, driver, API, renderer, observed
resolution, Vulkan ICD record, map, and scenario. Executable SHA-256 remains in
the report as info, rather than blocking the intended bootstrap-to-renderer
comparison. A fallback result can never satisfy either profile.

The policy uses these results:

- `PASS`: all required evidence and stable metrics meet the limits.
- `WARN`: a repeatable change exceeds both warning limits.
- `FAIL`: a hard limit or required check fails, or a stable change exceeds both
  failure limits.
- `INCONCLUSIVE`: too few valid runs or too much run-to-run spread prevents a
  sound timing verdict.
- `NOT_COMPARABLE`: a material scenario, build, host, renderer, protocol, or
  settings field differs or is missing.
- `UNAVAILABLE`: an optional metric, such as outline GPU timing, is absent. The
  primary GPU median is required by both trusted GPU enforcement profiles.
- `SKIPPED`: a metric does not apply to that scenario.

The PR smoke job keeps `NOT_COMPARABLE` in its report but does not block the PR
for that status. It uses `--not-comparable-exit-zero`; `FAIL` and tool errors
still block the job. Manual and full benchmark runs remain strict by default.

Most timing rules require a regression to exceed both their absolute and
relative limits. The required GPU medians instead use hard relative caps:
Default `+25%` and Competitive `+15%`. An over-cap change of `0.05 ms` or less
is `INCONCLUSIVE`; a larger one fails. A zero baseline uses the normal absolute
limit. Tukey outliers remain in the result; reports list their run number,
fences, and all pre-exclusion values. Version 1 never drops an outlier.

The tool writes deterministic `comparison.json` and `report.md` files. The JSON
includes comparability checks, run counts, raw run values, medians, spread,
outliers, limits, hard checks, and per-metric status. The Markdown puts hard
correctness failures before the timing table and calls out the largest regression,
largest improvement, unavailable data, and noisy data.

The common CI evidence helper writes or updates a portable evidence root:

```powershell
python scripts/lg_verification.py protocol-budget --evidence-root verification
python scripts/lg_verification.py collect-ci `
  --evidence-root verification `
  --platform windows `
  --category protocol `
  --status success
```

Its manifest uses relative artifact paths with hashes and sizes. The protocol
record runs the real protocol tests, checks the source ceiling remains 1,200
bytes, records parsed packet sizes, and fails when the encoder test or hard limit
fails. Revision and stored-result comparisons also require the configured source
ceiling to equal 1,200 bytes; a low observed packet size cannot hide a raised
ceiling.

`movement-collision` drives the real shared 125 Hz `simulateMovement` path from fixed spawn states and commands. `trace-projectile` separately measures long `traceWorld` rays and short projectile-style swept segments. Both hash all returned state, repeat the identical workload, and invalidate a result if replay checksums differ. `--force-linear` disables the derived collision index for a paired same-binary broadphase comparison; it is a diagnostic implementation selector and is recorded in native JSON.

Use `--server-port`, `--control-port`, `--timeout`, or `--json` when the
wrapper needs those global options. `--port` remains a control-port alias. The
exact executable/build directory is preset dependent; use wrapper help rather
than assuming a packaged game contains it. MCP exposes the same opt-in work as
thin adapter tools: `lg_list_benchmarks`, `lg_run_benchmark`,
`lg_compare_benchmarks`, `lg_get_benchmark_result`, and
`lg_create_benchmark_baseline`. The two run tools expose the same typed port
pair and alias rule. They return structured results and requested PNGs, not a
hand-written summary.

A run warms selected map, renderer resources, and fixed scenario state; then resets scenario time/state for every repetition and collects only the declared measured interval. Screenshots, PNG encoding, filesystem writes, process start-up, map loading, baseline reading, and comparison output are outside timing samples. Results retain raw samples and a per-run summary so a future percentile implementation can be audited.

Use one baseline per comparable environment. By default the comparator refuses different scenario/version, map hash, backend or fallback, resolution, presentation settings, camera/state hash, or percentile method. An explicit force option may produce an annotated non-comparable report, never a normal regression verdict. For repeats, compare the documented aggregate (for example, median of per-run p95 values) rather than the luckiest run.

The simulation runner first summarizes batch samples within each repetition, then uses the median of repetition medians and the median of repetition p95/p99 values. Stability is the coefficient of variation across repetition medians. Batch-to-batch geometry differences therefore do not masquerade as host noise. Tukey outliers remain in all statistics and are reported.

## Static Collision Broadphase

Packaged maps build a deterministic immutable flattened BVH when `loadLocalMap` succeeds. It indexes static wall AABBs and convex-brush bounds only; players and projectiles remain the existing small bounded sets. Movement sweeps and world traces query conservative AABBs, mark candidates in fixed wall/brush bitsets, then run the existing narrow phase in ascending authored order. BVH traversal order can therefore never change equal-distance or walkable-drop tie behavior. Directly constructed/unfinalized arenas, stale count metadata, and traversal-stack overflow use the original linear path.

The index is derived data held alongside `Arena`; it is omitted from map hashes and network encoding, so no protocol field or gameplay snapshot changed. `lg_duel_arena_broadphase_tests` compares indexed and forced-linear results bit-for-bit for 1,000 rays and 500 movement sweeps on every packaged map. The simulation workloads provide the paired quantitative gate before enabling further indexed query types.

## Validity And Visual Safeguards

A usable result has the requested backend, exact warmup/measured counts, finite nonnegative samples, compatible metadata, and no scenario-validation warning. It records a real rendered screenshot at each requested progress for human visual review. Screenshots verify composition, material availability, missing geometry, visible actor/effect load, and backend fallback; they are not pixel-perfect cross-driver tests.

Do not hide a regression by disabling culling, players, weapons, effects, outlines, HUD, or texture behavior unless the descriptor labels that choice and the comparison uses that same choice. Conversely, do not enable debug HUDs, logs, captures, or GPU readbacks inside measurement. A visual fixture must be labelled `fixture_only`; it is evidence about a renderer path, not evidence that server combat produced the state.

## Adding A Scenario

1. Copy a small descriptor in `config/benchmarks/` and retain version `1`.
2. Select a checked-in map and verified camera coordinate/preset; use a fixed static path unless camera motion is what the scenario measures.
3. State resolution, backend, pacing, warmup, measured duration, actors, effects, and screenshot progress explicitly.
4. Use real server bot commands where available. If desired gameplay is unsupported, fail the authoritative scenario or declare the remaining renderer fixture and its limitation in `residual_nondeterminism`.
5. Run it repeatedly, inspect PNGs, and add focused parser/state-hash tests with the implementation. Do not add a baseline artifact to source control.

## Reproducing And Interpreting Results

Use AC power and a stable power plan, close GPU-heavy applications, wait for background work to settle, and run enough repeats to see variance. Record driver/OS/build changes. Compare p50 for typical cost and p95/p99/max for pacing risk; average FPS alone is insufficient. A direct CPU-time change is a regression signal, not proof of GPU execution speed.

Static-world improvements should show in the GPU-required static baseline; dynamic player/outline/effect work needs the corresponding diagnostic counts. A high-visible imported map is a useful structural stress case, not a proxy for every duel map. For GPU indirect work, report p50/p95/p99 CPU frame time, MainScene GPU time, world draw calls, indirect command slots, and material groups. SDL_Renderer fallback is less representative because it lacks the static SDL_GPU world cache and screen-space outline path; never merge it into GPU baseline trends.

## Troubleshooting

- **Benchmark control unavailable:** build the client, check the chosen
  `28960/28961` pair or your explicit pair, and remove no state by hand while
  an owned process still runs. The normal `27960/27961` visual session does not
  need to stop.
- **Benchmark state already in use or corrupt:** inspect the pair-specific
  `build/benchmark-control/<server>-<control>/` logs and process record. The
  runner fails closed and does not stop or overwrite that session.
- **GPU requirement failed:** record the fallback/error and fix the selected SDL_GPU backend/driver before comparing results.
- **Vulkan loader has no valid default ICD:** repair the driver install so `vulkaninfo --summary` can find the Intel ICD through the loader's normal driver search. Benchmark child processes remove `VK_DRIVER_FILES` and `VK_ICD_FILENAMES`.
- **Map or textures differ:** rerun from the same repository/build output and inspect map content hash and screenshot. Imported-map texture coverage can differ from compact `eyetoeye`.
- **Bot setup rejected:** start from warmup; `bot_add` cannot change the roster after warmup. Verify `expected_count` before timing.
- **Noisy p95/p99:** repeat the scenario, check thermal/power/compositor changes and background work, and compare compatible runs only.
- **Projectile claim is misleading:** use the supplied scenario only as a labelled presentation fixture until bots can authoritatively select projectile weapons through a deterministic gameplay path.
