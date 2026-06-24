# Audio Previews

These WAV files are short sound proposals for the duel prototype. The client
loads only the explicit runtime cues listed below from `assets/audio`; other
files are kept as audition material. If a runtime WAV is missing, invalid, or
SDL audio is unavailable, gameplay continues and the client falls back to the
existing synthesized cue where one exists.

## Lightning Gun Fire

- `lg_fire_selected_low_drone.wav`: runtime lightning-gun firing loop, copied from preview 03.
- `lg_fire_preview_01_buzzy_drone.wav`: baseline low cartoon LG hum with a little electrical grit.
- `lg_fire_preview_02_arc_pluck.wav`: heavier transformer-like buzz with slow beating.
- `lg_fire_preview_03_soft_tesla_drone.wav`: rounder low drone meant to sit under sustained fire.
- `lg_fire_preview_04_chewy_zap_loop.wav`: nastier arcade buzz, still led by low-frequency body.

These are shooting-loop previews for holding the lightning gun beam, separate
from the hit confirmation ping. Keep the non-selected previews around as extra
source material for later weapon, impact, or ambience work.

## Expanded Weapon Fire

- `mg_fire_selected_snap.wav`: short dry automatic snap for machine gun fire.
- `sg_fire_selected_blast.wav`: compact low blast with a gritty tail for shotgun fire.
- `gl_fire_selected_thump.wav`: rounded launcher thump for grenade launcher fire events.
- `pg_fire_selected_pulse.wav`: bright short energy pulse for plasma gun fire events.

The client loads these selected WAVs when present and falls back to synthesized
fire cues from `src/app/GameApp.cpp` when a file is unavailable. MG, SG, and RL
are driven by authoritative `weaponFires` on `main`; GL and PG preview assets
are selected so their cues are ready when gameplay starts emitting authoritative
fire events for those weapons.

## Footsteps

- `footstep_preview_01_concrete_snap.wav`: runtime footstep cue. Short concrete-like thump with some grit.
- `footstep_preview_02_soft_pad.wav`: softer and rounder, less distracting in sustained strafes.
- `footstep_preview_03_metal_tick.wav`: brighter attack for a more arena-metal floor.
- `footstep_preview_04_gritty_slide.wav`: noisier scrape with a longer tail.

Runtime footstep volume is controlled with `s_footstep_volume` and still passes
through the global `s_volume` master.
