# Performance Benchmarks

LG Duel benchmarks are repeatable, opt-in developer artifacts for finding rendering regressions. They are not a gameplay mode, a player-facing quality setting, or an authority path. Normal client and server launches do not load benchmark scenarios.

Descriptors live in `config/benchmarks/`. Results, screenshots, and comparison baselines belong under `build/benchmarks/<scenario>/<run_group>/<run_id>/` and are ignored by Git. Do not commit a result as a universal performance claim: drivers, power policy, compositor state, and selected backend all matter.

## Architecture And Trust Boundary

The normal client contains a benchmark recorder that is inert unless the process is launched with both `--dev-control` and `--benchmark`. The recorder uses the same renderer-facing state and map-loading rules as ordinary play, but it does not add timing data or synthetic state to UDP packets, snapshots, or `ServerGame`. The benchmark camera is presentation-only; bot setup still travels through the existing server-authoritative command path.

The existing localhost developer-control plane remains the reusable seam for a live-client benchmark that needs map loading, a development camera, or a real PNG capture. It is opt-in (`--dev-control`/`--control-port`), loopback-only, and unavailable in normal play. A benchmark must use that structured control plane rather than grow a general console or remote-control interface. Its development camera changes only the state passed to rendering, not an authoritative player body.

MCP is separate from gameplay networking. The repository-local MCP adapter calls the same Python orchestration and typed loopback operation as the CLI, then returns summaries and artifact references. It does not own frame timing, make the client authoritative, or expose a non-loopback listener.

## What Is Measured

Each result must label every metric with its scope.

| Category | Meaning |
| --- | --- |
| Direct | Client-thread wall time around a measured render frame; the primary frame-time distribution. |
| Derived | Mean, p50, p95, p99, maximum, FPS conversion, run-to-run deltas, and regression verdicts calculated from direct samples. |
| Backend-specific CPU timing | Renderer diagnostics such as scene build, dynamic upload preparation, swapchain acquire, draw issue, submit, total render, and diagnostics counts. These remain CPU-side observations and retain backend/present-mode labels. |
| Unavailable | GPU execution duration, GPU allocation cost, GPU memory use, and general GPU utilization. SDL 3.4.10 exposes no GPU timestamp-query API for this renderer, and SDL_Renderer exposes none either. CPU submit/acquire timing is not a substitute for GPU execution time. |

Record selected backend, requested and selected present mode, resolution, fullscreen/vsync/frame-cap state, map content hash, scenario/version, build identity, system/driver information, and any fallback. Vulkan runs query `vulkaninfo --summary` outside the measured interval and record the physical-device name, driver name/version, Vulkan API version, effective `VK_DRIVER_FILES`/legacy loader variables, ICD manifest SHA-256, and resolved driver library. This metadata is written to both `aggregate.json` and every native `run-*/result.json`. A GPU-required scenario is invalid if the renderer fell back to SDL_Renderer. Comparisons reject a different GPU, graphics-driver version, or Vulkan API version.

### Percentiles

Version 1 uses nearest rank: sort samples ascending and select the one-based rank `ceil(p * N)`. It never interpolates a frame time that was not observed. `p99.9` is reported only for at least 1,000 samples. Changing this convention requires a benchmark-version bump or an explicit incompatible-comparison refusal.

## Scenario Format

Every descriptor is JSON with `schema_version: 1` and `expected_benchmark_version: 1`. Curated descriptors also carry `benchmark_version: 1` as human-readable metadata. A runner must reject a future or incompatible version rather than guessing semantics.

| Field | Required semantics |
| --- | --- |
| `name`, `labels`, `map` | Safe identifier, searchable classification, and a repository map name. The map must load and its content hash is compatibility data. |
| `backend_requirement` | `gpu` requires the SDL_GPU path; no fallback result may be compared as equivalent. |
| `resolution`, `fullscreen`, `vsync`, `frame_cap`, `fov` | Explicit presentation contract. `frame_cap: 0` means uncapped. |
| `warmup_seconds` or `warmup_frames`; `measured_seconds` or `measured_frames` | Exactly one unit must be supplied for each phase. Warmup has no timing samples; only the measured interval contributes samples. Curated descriptors use explicit warm-ups and measured intervals sized for their workload. |
| `camera_start`, `camera_path` | LG-unit position plus degree yaw/pitch/FOV. `camera_path` is a direct ordered array of keyframes, each using exactly one of normalized `progress` or `time_seconds`. |
| `player_state` | Presentation-only local-player state and UI visibility. It is not a server command. |
| `actors` | Structured requested bot setup: `bots`, `attack_mode`, `stare`, `standstill`, `dodge`, optional dodge intervals, and `expected_count`; `commands` documents the equivalent real console sequence. |
| `effects` | Requested bounded projectile/tracer/explosion load. `fixture_only: true` explicitly marks synthetic presentation content. |
| `cvars` | Narrow presentation-only overrides. They must be emitted in the result and restored/isolated by the runner. |
| `screenshots` | Array of `{ "name", "progress" }`; capture happens outside timed sampling. |
| `residual_nondeterminism` | Honest list of known uncontrolled inputs or unsupported conditions; an empty list means none are known. |

Camera progress during measured rendering uses elapsed time quantized down to the 125 Hz simulation interval, not mouse input. A time/progress value therefore resolves to the same transform independent of render rate. Screenshot checkpoints are outside timed sampling and evaluate their normalized progress exactly, so changing the measured duration does not subtly move a checkpoint. Keyframes must be nondecreasing and are linearly interpolated between fixed positions/angles; duplicate static endpoints intentionally make a stationary camera explicit.

The seven supplied scenarios establish a small comparison suite:

- `eyetoeye-static-baseline`: low-action cached-world control.
- `eyetoeye-duel-like`: normal two-participant Lightning-Gun bot duel request.
- `eyetoeye-bot-animation`: full six-player-capacity bot movement/animation request using `bot_add`, `bot_dodge`, `bot_stare`, and `bot_standstill`.
- `eyetoeye-projectile-effects`: a declarative future projectile/effect presentation fixture. Current bots select only the Lightning Gun and the native runner does not inject synthetic projectiles, so this descriptor is deliberately marked invalid at execution (`supported_workload: false`) rather than silently measuring a different workload. Use the headless `trace-projectile` workload for current quantitative projectile-query evidence.
- `overkill-high-visibility`: static large-map structural/sightline stress using a checked-in `overkill_import` camera preset.
- `eyetoeye-static-long`: 5-second warm-up plus a 25-second static baseline for tail stability.
- `overkill-static-flythrough`: 15-second warm-up plus a deterministic 60-second presentation-only camera interpolation through all three checked-in Overkill structural views; the world remains static.

The camera coordinates come from `config/dev-camera-presets.json`, not arbitrary map-space guesses. Bot commands are current commands: `bot_add [count]` is permitted only in warmup; `bot_attack 0|off|easy|medium|hard`, `bot_stare`, `bot_standstill`, and `bot_dodge` control supported training behavior. Today, bot combat always selects the Lightning Gun.

## Running, Repeating, And Comparing

Build the repository normally, then let the wrapper start an owned client with the explicit benchmark flag. The PowerShell wrapper owns the supported CLI contract:

```powershell
.\scripts\lg-benchmark.ps1 list
.\scripts\lg-benchmark.ps1 run --scenario eyetoeye-static-baseline --repetitions 5 --json
.\scripts\lg-benchmark.ps1 --timeout 900 run --scenario overkill-static-flythrough --repetitions 7 --controlled-environment
.\scripts\lg-benchmark.ps1 baseline-create --scenario eyetoeye-static-baseline --name gpu-driver-current --repetitions 5
.\scripts\lg-benchmark.ps1 compare --baseline gpu-driver-current --result build/benchmarks/eyetoeye-static-baseline/<run-group> --threshold-percent 5 --tail-threshold-percent 8
.\scripts\lg-benchmark.ps1 report --result build/benchmarks/eyetoeye-static-baseline/<run-group> --detailed
```

Shared simulation hot paths have a separate headless executable so renderer scheduling cannot be mistaken for collision cost. The same PowerShell entry point builds artifact-compatible reports:

```powershell
.\scripts\lg-benchmark.ps1 sim-run --workload movement-collision --map overkill_import --repetitions 5 --warmup-batches 40 --measured-batches 60 --operations-per-batch 256
.\scripts\lg-benchmark.ps1 sim-run --workload trace-projectile --map overkill_import --repetitions 5 --warmup-batches 60 --measured-batches 100 --operations-per-batch 256
.\scripts\lg-benchmark.ps1 sim-run --workload trace-projectile --map overkill_import --repetitions 1 --warmup-batches 10 --measured-batches 20 --operations-per-batch 256 --profile-broadphase
```

`movement-collision` drives the real shared 125 Hz `simulateMovement` path from fixed spawn states and commands. `trace-projectile` separately measures long `traceWorld` rays and short projectile-style swept segments. Both hash all returned state, repeat the identical workload, and invalidate a result if replay checksums differ. `--force-linear` disables the derived collision index for a paired same-binary broadphase comparison; it is a diagnostic implementation selector and is recorded in native JSON.

`--profile-broadphase` is an explicit diagnostic run. It records total static solids, queries, nodes visited, BVH candidates returned, candidates actually passed to the existing narrow phase, per-query maxima, and fallback count in `broadphase-profile.csv` and native JSON. Profiling adds counter overhead, so use a separate unprofiled run for timing comparisons. Large maxima identify pathological queries even when the averages are healthy.

`--controlled-environment` temporarily duplicates and activates the Windows High performance power plan, records the active plan and AC/battery state, then restores the original plan and removes the temporary one. It does not close applications. Close browsers, Discord, Spotify, Steam, capture tools, and other GPU-heavy applications yourself; the report records a conservative list of detected background applications so an accidentally loaded run is visible.

Use `--port`, `--timeout`, or `--json` when the wrapper needs those global options. The exact executable/build directory is preset dependent; use wrapper help rather than assuming a packaged game contains it. MCP exposes the same opt-in work as thin adapter tools: `lg_list_benchmarks`, `lg_run_benchmark`, `lg_compare_benchmarks`, `lg_get_benchmark_result`, and `lg_create_benchmark_baseline`. It returns structured results and requested PNGs, not a hand-written summary.

A run warms selected map, renderer resources, and fixed scenario state; then resets scenario time/state for every repetition and collects only the declared measured interval. Screenshots, PNG encoding, filesystem writes, process start-up, map loading, baseline reading, and comparison output are outside timing samples. Results retain raw samples and a per-run summary so a future percentile implementation can be audited. Every run also slices newly appended client/server stdout and stderr into `run-*/logs/`.

Use one baseline per comparable environment. By default the comparator refuses different scenario/version, map hash, backend or fallback, resolution, presentation settings, camera/state hash, or percentile method. An explicit force option may produce an annotated non-comparable report, never a normal regression verdict. For repeats, compare the documented aggregate (for example, median of per-run p95 values) rather than the luckiest run.

The simulation runner first summarizes batch samples within each repetition, then uses the median of repetition medians and the median of repetition p95/p99 values. Stability is the coefficient of variation across repetition medians and is not assessed with fewer than three repetitions. Batch-to-batch geometry differences therefore do not masquerade as host noise. Tukey outliers remain in all statistics and are reported.

## Static Collision Broadphase

Packaged maps build a deterministic immutable flattened BVH when `loadLocalMap` succeeds. It indexes static wall AABBs and convex-brush bounds only; players and projectiles remain the existing small bounded sets. Movement sweeps and world traces query conservative AABBs, mark candidates in fixed wall/brush bitsets, then run the existing narrow phase in ascending authored order. BVH traversal order can therefore never change equal-distance or walkable-drop tie behavior. Directly constructed/unfinalized arenas, stale count metadata, and traversal-stack overflow use the original linear path.

The index is derived data held alongside `Arena`; it is omitted from map hashes and network encoding, so no protocol field or gameplay snapshot changed. `lg_duel_arena_broadphase_tests` compares indexed and forced-linear results bit-for-bit for 1,000 rays and 500 movement sweeps on every packaged map. The simulation workloads provide the paired quantitative gate before enabling further indexed query types.

## Validity And Visual Safeguards

A usable result has the requested backend, exact warmup/measured counts, finite nonnegative samples, compatible metadata, and no scenario-validation warning. It records a real rendered screenshot at each requested progress for human visual review. Each screenshot checkpoint is bound to its deterministic camera transform, map name/revision/content hash, image dimensions, byte count, SHA-256, and artifact path. Screenshots verify composition, material availability, missing geometry, visible actor/effect load, and backend fallback; they are not pixel-perfect cross-driver tests.

Do not hide a regression by disabling culling, players, weapons, effects, outlines, HUD, or texture behavior unless the descriptor labels that choice and the comparison uses that same choice. Conversely, do not enable debug HUDs, logs, captures, or GPU readbacks inside measurement. A visual fixture must be labelled `fixture_only`; it is evidence about a renderer path, not evidence that server combat produced the state.

## Adding A Scenario

1. Copy a small descriptor in `config/benchmarks/` and retain version `1`.
2. Select a checked-in map and verified camera coordinate/preset; use a fixed static path unless camera motion is what the scenario measures.
3. State resolution, backend, pacing, warmup, measured duration, actors, effects, and screenshot progress explicitly.
4. Use real server bot commands where available. If desired gameplay is unsupported, fail the authoritative scenario or declare the remaining renderer fixture and its limitation in `residual_nondeterminism`.
5. Run it repeatedly, inspect PNGs, and add focused parser/state-hash tests with the implementation. Do not add a baseline artifact to source control.

## Reproducing And Interpreting Results

Use AC power and a stable power plan, close GPU-heavy applications, wait for background work to settle, and run enough repeats to see variance. For Overkill rendering use at least seven repetitions; a run set above the 3% headline CV stability gate is exploratory rather than comparison evidence. Record driver/OS/build changes. Compare p50 for typical cost and p95/p99/max for pacing risk; average FPS alone is insufficient. A direct CPU-time change is a regression signal, not proof of GPU execution speed.

Static-world improvements should show in the GPU-required static baseline; dynamic player/outline/effect work needs the corresponding diagnostic counts. A high-visible imported map is a useful structural stress case, not a proxy for every duel map. SDL_Renderer fallback is less representative because it lacks the static SDL_GPU world cache and screen-space outline path; never merge it into GPU baseline trends.

## Troubleshooting

- **Benchmark control unavailable:** build the client, stop any ordinary development-control client on the chosen port, and let the wrapper relaunch it with `--benchmark`.
- **GPU requirement failed:** record the fallback/error and fix the selected SDL_GPU backend/driver before comparing results.
- **Vulkan loader has no default ICD:** set `VK_DRIVER_FILES` to the verified vendor manifest for the launch shell. The result must show `vulkan_metadata_status: available`; do not compare a run whose ICD identity is unknown.
- **Map or textures differ:** rerun from the same repository/build output and inspect map content hash and screenshot. Imported-map texture coverage can differ from compact `eyetoeye`.
- **Bot setup rejected:** start from warmup; `bot_add` cannot change the roster after warmup. Verify `expected_count` before timing.
- **Noisy p95/p99:** repeat the scenario, check thermal/power/compositor changes and background work, and compare compatible runs only.
- **Projectile claim is misleading:** use the supplied scenario only as a labelled presentation fixture until bots can authoritatively select projectile weapons through a deterministic gameplay path.
