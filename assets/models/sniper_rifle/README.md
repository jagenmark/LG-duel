# Sniper rifle

Quaternius Animated Guns Pack sniper rifle, prepared for LG Duel's railgun.
The runtime model uses `+X` forward and `+Z` up, with its main grip at the
origin. The GLB keeps the source rig and these clips for later use:

- `FireWBullet`, frames 1-20
- `FireWOBullet`, frames 1-10
- `Reload`, frames 1-23

The current renderer bakes the rest pose into a material mesh. It does not play
the clips yet. The source file has named flat materials but no saved colors, so
the export script adds a dark steel and green stock color set.

Regenerate from the repository root with Blender 5.x:

```powershell
& 'C:\Program Files\Blender Foundation\Blender 5.1\blender.exe' -b --factory-startup --disable-autoexec --python assets/models/sniper_rifle/export_sniper_rifle.py
```

Source and license details are in `ATTRIBUTION.md` and `LICENSE.txt`.
