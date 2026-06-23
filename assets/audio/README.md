# Audio Previews

These WAV files are short sound proposals for the duel prototype.
The current client still synthesizes runtime audio directly in code, but the
previews make the intended direction easy to audition and compare.

## Lightning Gun Fire

- `lg_fire_selected_low_drone.wav`: selected baseline, copied from preview 03.
- `lg_fire_preview_01_buzzy_drone.wav`: baseline low cartoon LG hum with a little electrical grit.
- `lg_fire_preview_02_arc_pluck.wav`: heavier transformer-like buzz with slow beating.
- `lg_fire_preview_03_soft_tesla_drone.wav`: rounder low drone meant to sit under sustained fire.
- `lg_fire_preview_04_chewy_zap_loop.wav`: nastier arcade buzz, still led by low-frequency body.

These are shooting-loop previews for holding the lightning gun beam, separate
from the hit confirmation ping. Keep the non-selected previews around as extra
source material for later weapon, impact, or ambience work.

## Footsteps

- `footstep_preview_01_concrete_snap.wav`: selected baseline. Short concrete-like thump with some grit.
- `footstep_preview_02_soft_pad.wav`: softer and rounder, less distracting in sustained strafes.
- `footstep_preview_03_metal_tick.wav`: brighter attack for a more arena-metal floor.
- `footstep_preview_04_gritty_slide.wav`: noisier scrape with a longer tail.

Runtime footstep volume is controlled with `s_footstep_volume` and still passes
through the global `s_volume` master.
