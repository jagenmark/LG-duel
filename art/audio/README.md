# Weapon sound sources and audition options

The `auditions` folders hold trial files by weapon and version. The `sources`
folders hold the licensed source packs used by the generators. The selected
runtime files live in `assets/audio` and are listed below.

The rocket impact options use the existing V4 rocket launch WAVs as a tonal
anchor. Those launch files were built from two CC0 OpenGameArt sources:

- [Rocket Engine](https://opengameart.org/content/rocket-engine), by
  theMinesAreShakin.
- [25 CC0 bang / firework SFX](https://opengameart.org/content/25-cc0-bang-firework-sfx),
  by rubberduck.

The first rifle and revolver set used the [Gunshots!](https://opengameart.org/content/gunshots-0)
reference pack by dklon, licensed CC-BY 3.0. The revised `v2` set uses the
recorded [Gunshot Sounds](https://opengameart.org/content/gunshot-sounds)
pack by Tabasco, licensed CC0. It includes Mosin Nagant rifle and CZ-52 pistol
recordings. The revised set keeps the source report, adds low body and a short
mid-band crack, and cuts the high pitched layer.

The `v3` revolver set keeps about one second of the recorded report and room
tail. The runtime currently uses `revolver_v3_01_full_heavy_report.wav` while
the other four remain audition options. The selected runtime files are:

- `assets/audio/rl_explosion_pop.wav`: `rocket_impact_v2_05_big_ground_boom.wav`
- `assets/audio/rg_fire_discharge.wav`: `sniper_v2_05_tight_rifle_boom.wav`
- `assets/audio/revolver_fire.wav`: `revolver_v3_01_full_heavy_report.wav`

All output files are mono, 48 kHz, 16-bit PCM WAV. Generate them with:

```powershell
python scripts/generate_weapon_sound_options.py
python scripts/generate_weapon_sound_options_v2.py
python scripts/generate_revolver_sound_options_v3.py
```
