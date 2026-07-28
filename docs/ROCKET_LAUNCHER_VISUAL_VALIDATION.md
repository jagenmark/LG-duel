# Rocket Launcher visual validation

`scenarios/live/rocket_launcher_visual_validation.json` is a fixed first-person
Rocket Launcher case on `scenario_wall`. It uses the real client, server,
snapshots, projectile path, and renderer at `1280x720` on
`SDL_GPU/vulkan`. The runner hides the HUD and overlays for all four captures.

Player 0 starts at `[-4, 0, 0.9]`, aims along positive X, and fires at the
wall whose near face is X `0`. The tracked Rocket Launcher speed is `22.5`
LG units per second. At 125 server ticks per second, a four-unit trip takes
about 22 ticks before projectile size and the launch offset are taken into
account.

The evidence run sends three real shots at logical ticks 0, 120, and 240.
Those gaps exceed the 100-tick weapon cooldown. The first shot backs the
muzzle image, the second backs the flight image, and the third backs the
impact image. A blocking GPU readback may let an earlier shot finish, but it
cannot consume the only shot needed for a later transient phase. The runner
adds the measured readback span to later run-time dispatch points.

## Run the three views

Build the client, server, and C++ scenario reader first. These commands state
both client settings, so each run reads them back and records them.

Default full effects and bloom:

```powershell
python scripts/lg_live_scenario.py scenarios/live/rocket_launcher_visual_validation.json --client-cvar r_combat_effects=2 --client-cvar r_bloom=1
```

Reduced effects with bloom:

```powershell
python scripts/lg_live_scenario.py scenarios/live/rocket_launcher_visual_validation.json --client-cvar r_combat_effects=1 --client-cvar r_bloom=1
```

Full effects with bloom off:

```powershell
python scripts/lg_live_scenario.py scenarios/live/rocket_launcher_visual_validation.json --client-cvar r_combat_effects=2 --client-cvar r_bloom=0
```

The runner accepts only `r_combat_effects=0|1|2` and `r_bloom=0|1` through
this option. It applies them after the owned client reports ready and before it
writes the scenario start request. The runner always stops its owned client in
cleanup, so the overrides end with that process. It does not write the archived
client settings file.

## Capture checkpoints

The fixed capture names are:

| Capture | Trigger | Purpose |
| --- | --- | --- |
| `rlvv-before` | tick 0, before input | no local shot, rocket, or blast |
| `rlvv-muzzle-peak` | tick 1 | local Rocket Launcher fire and spawned rocket |
| `rlvv-rocket-flight` | tick 130, ten ticks after shot 2 | local rocket in flight, with no fire or blast |
| `rlvv-impact` | third authoritative `explosion_created` event | local blast, with no live rocket |

The client puts `frame_state` in the screenshot reply from the exact frame fed
to the renderer. It records this state before GPU readback, so a slow readback
cannot replace it with a later snapshot. It also adds the rocket, tracer, and
blast counts that the renderer submitted for that same frame. The runner checks
both sets of frame data against the event history in the trigger checkpoint. It fails the run if a
muzzle, flight, or impact image is idle, if any image has the wrong render
phase, or if the server event history does not support the phase. The
`before_fire` image must also prove that the shot has not yet happened.
Each accepted record stores the latest matching `weapon_fired` event and its
event window as `phase_evidence`, which ties muzzle, flight, and impact to the
shot that backs that image.

The four names above remain the logical names in `screenshots.json` and the run
artifact. The raw client files use
`YYYYMMDDTHHMMSSZ-<scenario>-<run-token>-<view>-NN.png`. UTC time, a per-run
token, and a two-digit sequence keep full, reduced, and bloom-off source images
apart even when runs start in the same second.

Each run writes `client-cvar-overrides.json`. It includes:

- `requested`: the two CLI values;
- `applied`: values read back from the client;
- `records`: the old value, set reply, read-back value, and match result;
- `applied_before_scenario_start`: ordering attestation;
- `restore`: cleanup scope.

`environment.json` repeats this setting record beside the renderer, GPU,
driver, build, and session data. `summary.json`, `assertions.json`,
`authoritative-events.json`, `screenshots.json`, and `screenshots/` bind the
result to the authoritative shot, the capture-time frame state, and the four
PNG files.

## Rocket-heavy benchmarks

The three checked-in benchmark cases keep the same map, camera, resolution,
three-second warm-up, eight-second measured span, five Rocket Launcher bots,
and held player Rocket Launcher fire:

```powershell
.\scripts\lg-benchmark.ps1 run --scenario rocket-launcher-vfx-full --repetitions 5 --json
.\scripts\lg-benchmark.ps1 run --scenario rocket-launcher-vfx-reduced --repetitions 5 --json
.\scripts\lg-benchmark.ps1 run --scenario rocket-launcher-vfx-bloom-off --repetitions 5 --json
```

The cases use real `bot_weapon rl` and player fire. Their `effects` block asks
for no added presentation fixture; live combat creates the rockets and blasts.
All three record `r_combat_effects` and `r_bloom`, and request screenshots at
20%, 50%, and 80% progress. Bot aim, movement, hits, and deaths still vary with
live match state, so use repeat groups and compare only like run records.

## Checks

```powershell
.\build\default\lg_duel_scenarios.exe --scenario scenarios/live/rocket_launcher_visual_validation.json --validate-only
python scripts/test_lg_live_scenario.py
python scripts/lg_benchmark.py list
```

The C++ command checks the authoritative live schema, including the closed
render-phase list. The Python tests check the narrow setting parser,
set/read-back order, phase rejection, unique UTC names, capture order, and run
records. A full visual pass still needs a working local Vulkan setup and image
review.
