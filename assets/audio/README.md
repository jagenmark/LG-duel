# Audio Previews

These WAV files are short footstep sound proposals for the duel prototype.
The current client still synthesizes runtime audio directly in code, but the
previews make the intended direction easy to audition and compare.

- `footstep_preview_01_concrete_snap.wav`: selected baseline. Short concrete-like thump with some grit.
- `footstep_preview_02_soft_pad.wav`: softer and rounder, less distracting in sustained strafes.
- `footstep_preview_03_metal_tick.wav`: brighter attack for a more arena-metal floor.
- `footstep_preview_04_gritty_slide.wav`: noisier scrape with a longer tail.

Runtime footstep volume is controlled with `s_footstep_volume` and still passes
through the global `s_volume` master.
