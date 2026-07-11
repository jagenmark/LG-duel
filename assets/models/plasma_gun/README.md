# Plasma gun

Original contained-core plasma accelerator. `+X` is forward and `+Z` is up.
The body, three containment prongs, and faceted plasma core are exported as
separate material meshes. Runtime presentation contracts the core and shifts
the prongs after replicated fire events without changing projectile authority.

Regenerate from the repository root with Blender 5.x:

```powershell
& 'C:\Program Files\Blender Foundation\Blender 5.1\blender.exe' -b --factory-startup --python assets/models/plasma_gun/create_plasma_gun.py
& 'C:\Program Files\Blender Foundation\Blender 5.1\blender.exe' -b --factory-startup --python assets/models/plasma_gun/export_plasma_gun_header.py
```
