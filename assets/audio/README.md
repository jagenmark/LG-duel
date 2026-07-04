# Audio Previews

These files are short sound proposals for the duel prototype. The client
loads the explicit runtime cues listed below from `assets/audio`; other files
are kept as audition material. Runtime cues may be WAV or OGG Vorbis. If a
runtime audio file is missing, invalid, or SDL
audio is unavailable, gameplay continues without that cue rather than
regenerating the sound from code.

## Lightning Gun Fire

- `lg_fire_selected_low_drone.wav`: runtime lightning-gun firing loop, copied from preview 03.
- `lg_fire_preview_01_buzzy_drone.wav`: baseline low cartoon LG hum with a little electrical grit.
- `lg_fire_preview_02_arc_pluck.wav`: heavier transformer-like buzz with slow beating.
- `lg_fire_preview_03_soft_tesla_drone.wav`: rounder low drone meant to sit under sustained fire.
- `lg_fire_preview_04_chewy_zap_loop.wav`: nastier arcade buzz, still led by low-frequency body.

These are shooting-loop previews for holding the lightning gun beam, separate
from the hit confirmation ping. Keep the non-selected previews around as extra
source material for later weapon, impact, or ambience work.

## Hit Confirm

- `hit_confirm_light.wav`: low-damage hit confirmation ping.
- `hit_confirm_medium.wav`: medium-damage hit confirmation ping.
- `hit_confirm_heavy.wav`: high-damage hit confirmation ping.
- `pain_grunt.wav`: played when a player takes damage; a new grunt is skipped while the previous one is still playing.
- `frag.wav`: local killer frag cue, played only for the player who earns the frag.

The client selects one hit-confirm file from the reported damage amount. Pain
grunts are driven by health decreases in authoritative snapshots.

## Weapon Fire And Explosion

- `rg_fire_discharge.wav`: railgun fire discharge.
- `rg_ready_chime.wav`: railgun cooldown-ready notification.
- `rl_fire_launch.wav`: rocket launcher fire cue.
- `rl_explosion_pop.wav`: rocket explosion cue.

- `mg_fire_selected_snap.wav`: short dry automatic snap for machine gun fire.
- `sshotf1b.ogg`: shotgun fire cue.
- `gl_fire.wav`: grenade launcher fire cue. Local POV shots play centered; remote shots are panned and distance-scaled from the firing position.
- `gl_bounce.wav`: grenade bounce cue. Every authoritative world bounce is panned and distance-scaled from the bounce position.
- `pg_fire_selected_pulse.wav`: bright short energy pulse for plasma gun fire events.

Several files were converted from the old generated prototype sounds so
designers can replace them directly.

## Footsteps

- `footstep_01.wav`/`.ogg` through `footstep_04.wav`/`.ogg`: optional runtime footstep variants. Numbered variants are preferred when present.
- `step1.wav`/`.ogg` through `step4.wav`/`.ogg`: accepted imported footstep variant names for the same four slots.
- `footstep.wav`/`.ogg`: legacy fallback footstep cue when no numbered variants exist.
- `jump1_visor.wav`: jump cue when a player leaves the ground from a jump.
- `land1.ogg`: landing cue when a player returns to the ground after being airborne.

Runtime footstep volume is controlled with `s_footstep_volume` in
`config/sound_mixer.cfg` and still passes through the global `s_volume` master.

## Match UI Cues

- `round_win_chime.wav`: played when the local player wins a round or match.
- `round_loss_chime.wav`: played when the local player loses a round or match.
- `countdown_5_beep.wav`: countdown cue for five seconds remaining.
- `countdown_4_beep.wav`: countdown cue for four seconds remaining.
- `countdown_3_beep.wav`: countdown cue for three seconds remaining.
- `countdown_2_beep.wav`: countdown cue for two seconds remaining.
- `countdown_1_beep.wav`: countdown cue for one second remaining.

Per-cue volume multipliers live in `config/sound_mixer.cfg`. The current
runtime mixer covers weapon fire (`s_lg_fire_volume`, `s_mg_fire_volume`,
`s_sg_fire_volume`, `s_gl_fire_volume`, `s_rl_fire_volume`,
`s_rg_fire_volume`, `s_pg_fire_volume`), rail ready, rocket/grenade explosion,
grenade bounce, hit confirm, frag, pain, footsteps, round win/loss, and
countdown cues.
