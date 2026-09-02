# Rocket fire options

All five sounds are original mono, 48 kHz, 16-bit WAV files made for quick
weapon response.

- `rocket_fire_01_arena_punch.wav` — tight, dry arena shot; the closest match
  to a fast old-school shooter feel.
- `rocket_fire_02_heavy_tube.wav` — more low-end weight and a longer tube tail.
- `rocket_fire_03_dirty_ignition.wav` — rougher burn with the strongest grit.
- `rocket_fire_04_mech_clack.wav` — a clear metal action over the blast.
- `rocket_fire_05_deep_whoomp.wav` — the deepest and longest option.

To try one in the game, back up `assets/audio/rl_fire_launch.wav`, then copy
the chosen file over that path. The generator lives at
`scripts/generate_rocket_fire_options.py` and uses fixed seeds, so it makes
the same files on each run.
