# CC0 rocket sound sources

The V4 rocket-fire options use these public-domain sources:

- `cc0_rocket_engine.wav` — “Rocket Engine” by theMinesAreShakin,
  downloaded from OpenGameArt:
  https://opengameart.org/content/rocket-engine
- `25-CC0-bang-sfx.zip` and the extracted `bangs` folder — “25 CC0 bang /
  firework SFX” by rubberduck, downloaded from OpenGameArt:
  https://opengameart.org/content/25-cc0-bang-firework-sfx

Both source pages list the work under Creative Commons Zero (CC0).

To rebuild the V4 options:

```powershell
python -m pip install -r scripts/requirements-rocket-audio.txt
python scripts/generate_rocket_fire_options_v4.py
```
