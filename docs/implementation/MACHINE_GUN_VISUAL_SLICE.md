# Machine-gun visual slice validation

## Fixed validation setup

Use a GPU development-control client and `scenario_wall`. Keep the camera at the normal first-person player view, use the machine gun, and keep the same window size for every capture.

Record two matched runs:

1. Before control: `r_combat_effects 0`, `r_bloom 0`.
2. After slice: `r_combat_effects 2`, `r_bloom 1`, shipped defaults for all other combat cvars.

Do not compare different camera points or maps.

## Sequence

Run each item once at the normal present mode, once with `r_maxfps 30`, and once with `r_maxfps 240` or uncapped when the display cannot reach 240:

1. Tap attack for one shot.
2. Fire a three-to-five-shot burst.
3. Hold attack for five seconds.
4. Release attack and watch the existing barrel playback stop.
5. Fire at the wall from close range.
6. Turn to produce an angled wall hit.
7. Strafe while firing.
8. Move the view while firing to exercise sway.
9. Hold fire until casings reach `r_casing_max`.
10. Set `r_decals_max 8`, fill it, and confirm the ninth mark recycles the oldest.
11. Reload `scenario_wall` and confirm lights, casings, particles, and decals return to zero.
12. Repeat with `r_combat_effects 1`, then `0`, `r_casings 0`, and `r_bloom 0`.

At each shot mode, check that the flash and light remain at `MG_MUZZLE_SOCKET`, the casing starts at `MG_CASING_EJECT_SOCKET`, and the barrel still follows its existing authored pivot and playback. Check recoil, sway, movement, and release transitions. Gameplay fire rate and hit results must remain unchanged.

## Captures

Save matched fixed-view PNGs under `build/captures/machine-gun-visual-slice/`:

- `before-idle.png`
- `before-sustained.png`
- `after-single-shot.png`
- `after-sustained.png`
- `after-impact-angle.png`
- `after-budget-filled.png`
- `after-disabled.png`

Inspect the actual PNGs. Automated tests cannot prove attachment, timing, brightness, clutter, or readability.

## Evidence to record

For both before and after runs, record:

- CPU frame-time average, p95, and p99;
- GPU frame time when the selected backend supports timestamps;
- peak temporary lights;
- peak casings;
- peak particles;
- peak decals;
- selected backend, GPU, resolution, present mode, and frame cap.

Use the existing benchmark and `r_perf` paths. Do not present one local machine’s result as a general performance claim.

## Pass conditions

- The full shot chain reads as one event: authored barrel motion, layered flash, local light, casing, impact burst, and bounded mark.
- Effects track the current weapon transform through movement, sway, recoil, and transitions at each tested frame cap.
- Sustained fire remains compact and does not become a solid flash or broad glow.
- Active counts never exceed their configured limits and return to zero or the retained decal count after lifetimes expire.
- Reload and disabled settings submit no stale work.
- The compatibility renderer and dedicated server retain their prior safe behaviour.

## Verified results

The fixed live scenario passed on SDL_GPU/Vulkan at 1280x720:

- effects enabled:
  `build/scenario-results/live-1785072541580-4e13b28e`;
- effects disabled:
  `build/scenario-results/live-1785072564590-3d215f1e`.

Both runs produced 18 authoritative machine-gun shots. The enabled run reached
1 temporary light, 12 casings, 15 particles, and 18 decals with no dropped
effects. The disabled run reported zero spawned or submitted combat effects at
all five capture points. The map content hash was `630595866` in both runs.

The checked-in sustained-fire benchmark ran three times at 240 FPS and three
times at 30 FPS:

- 240 FPS:
  `build/benchmarks/machine-gun-visual-slice/20260726T131459Z-813343186cd7`;
- 30 FPS:
  `build/benchmarks/machine-gun-visual-slice-low-fps/20260726T134905Z-01f2e318faa7`;
- effects-off comparison:
  `build/benchmarks/machine-gun-visual-slice-baseline/20260726T131435Z-813343186cd7`.

The effects-on groups were valid and stable. At 240 FPS, median CPU render time
was 0.865 ms and median GPU command time was 0.544 ms. At 30 FPS, the matching
values were 0.828 ms and 0.720 ms. The effects-off group was valid but not
stable, so it supports a local cost check only; it does not support a broad
regression claim.

### Sustained muzzle-envelope tuning check

The held-fire pulse keeps each authored per-shot peak, then leaves a small
seeded core until the next normal machine-gun shot. The matched one-run checks
used the same sustained-fire descriptors after the tuning:

- 240 FPS: `build/benchmarks/machine-gun-visual-slice/20260726T174418Z-36521921b17c` — median frame 4.167 ms, CPU render 0.681 ms, median 76 active effects and peak 107.
- 30 FPS: `build/benchmarks/machine-gun-visual-slice-low-fps/20260726T174427Z-36521921b17c` — median frame 33.33 ms, CPU render 0.949 ms, median 149 active effects and peak 175.

Both runs were valid and stable, with no frame-cap pacing regression. This
debug-build pass did not return GPU timestamp samples, so it records GPU time
as unavailable rather than calling CPU submit time GPU time.

The user manually reviewed sustained machine-gun fire on 2026-07-27 and
approved the lively overlapping pulse. That live approval supersedes the
failed automated peak-capture timing and unavailable independent reviewer for
this small tuning change; it does not replace the recorded technical checks.

The exact direct-control evidence pair uses `scenario_wall`, camera
`[-6.45, 0, 1.55]`, yaw and pitch `0`, FOV `100`, and 1920x1200 output. The
effects-on frame shows the authored barrel, shot flash, tracer, local floor
light, casings, and wall mark. The effects-off frame uses the same view and has
no combat effects.

## Current limits

- Bright effect bloom uses the compact additive glow path, not a full
  downsampled post-process chain.
- Temporary lights do not cast shadows.
- Casings use visual gravity and tumble without world collision.
- Impact surface choice uses the small current surface set and a generic
  fallback.
- The compatibility renderer keeps its prior output and does not render the new
  GPU effects.
