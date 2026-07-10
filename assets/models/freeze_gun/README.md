# Freeze gun

Original compact cryogenic-beam weapon. `+X` is forward and `+Z` is up.
`FG_FOCUS_CORE` contains a short reference pulse; runtime presentation drives
the focusing motion without affecting the authoritative beam trace.

Regenerate from the repository root with Blender 5.x:

```powershell
& 'C:\Program Files\Blender Foundation\Blender 5.1\blender.exe' -b --factory-startup --python assets/models/freeze_gun/create_freeze_gun.py
& 'C:\Program Files\Blender Foundation\Blender 5.1\blender.exe' -b --factory-startup --python assets/models/freeze_gun/export_freeze_gun_header.py
```
