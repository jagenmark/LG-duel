# Machine-gun visual validation

`scenarios/live/machine_gun_visual_validation.json` is the fixed capture case
for machine-gun barrel effects. It uses the real client, server, snapshots, and
renderer. It does not change game code.

The source map is `scenario_wall`. Player 0 starts at `[-4, 0, 0.9]`, looks
along positive X, and fires into the wall at X `0`. This gives a close,
straight-on wall hit. The angled shot uses the same start with yaw `20`. The
last section moves forward and right while firing, so the first-person capture
includes normal motion, view sway, and recoil.

## Fixed sequence

| Capture | Server tick | Input | Expected fire count at or before capture |
| --- | ---: | --- | ---: |
| `mgvv-single-close` | 1 | One close wall shot | 1 |
| `mgvv-short-burst-close` | 48 | 28 held-fire ticks | 4 |
| `mgvv-sustained-close` | 164 | 100 held-fire ticks, then 4 release ticks | 12 |
| `mgvv-single-angled` | 181 | One shot at yaw 20 | 13 |
| `mgvv-moving-sustained` | 256 | 56 moving held-fire ticks | 18 |

The counts follow the tracked machine-gun cooldown of 13 ticks. Held-input
spans leave a few ticks before the next cooldown boundary so ordinary command
release delivery cannot add or remove a boundary shot. The final event
assertion checks the total of 18 `weapon_fired` events. The live result
also records each capture's trigger tick, client tick, snapshot tick,
presentation tick, and renderer response. Use those fields when writing a
review note; do not infer effect load from a PNG alone.

## Run and review

Build the noninteractive targets, then run the scenario:

```powershell
cmake --preset default
cmake --build --preset default --target lg_duel_client lg_duel_server lg_duel_scenarios
python scripts/lg_live_scenario.py scenarios/live/machine_gun_visual_validation.json
```

The runner creates a fresh directory below
`build/scenario-results/machine_gun_visual_validation/`. Review its
`summary.json`, `assertions.json`, `environment.json`,
`authoritative-events.json`, and `screenshots/` directory. The captures retain
the names in the table, which makes same-name before/after runs easy to pair.
Compare only matching capture names from runs with the same map hash, build,
renderer identity, GPU, driver, and screenshot dimensions.

The runner owns a `1280x720` window and requires `SDL_GPU/vulkan`; every
capture hides the HUD and overlays. This supplies fixed resolution, profile,
scene, camera path, and effect timing. `environment.json` holds the renderer
and GPU attestation. The project has no render-scale control or render-scale
field in the live-scenario protocol, so render scale cannot yet be set or
recorded. Record that limitation with any review.

The captures are evidence for visible barrel, muzzle, tracer, and wall-hit
presentation. They are not pixel-identical cross-driver tests. Review the five
named PNG pairs side by side, especially the muzzle/barrel state, tracer path,
wall contact, and motion state. A failed renderer assertion, missing PNG, or
mismatched dimensions invalidates the visual review.

## Noninteractive validation

Validate the JSON schema without launching the game:

```powershell
.\build\default\lg_duel_scenarios.exe --scenario scenarios/live/machine_gun_visual_validation.json --validate-only
python scripts/test_lg_live_scenario.py
```

CTest runs the same direct-file check as
`lg_duel_live_scenario_python_tests`. Keep this command in that form because
the test imports helper modules from `scripts/`.

The schema check proves the C++ scenario reader accepts the fixed timeline,
capture points, and assertions. The Python tests cover the live runner's
capture ordering and PNG evidence checks. A full run needs a working local
Vulkan setup and therefore remains an opt-in visual review step.
