# Rocket Launcher

Industrial, low/medium-poly rocket launcher based on the approved concept in
`reference/rocket_launcher_concept.png`.

The editable model uses the project's authored-weapon convention: `+X` points
forward and `+Z` points up. It contains a minimal rigid-part armature:

- `RL_ROOT` — static weapon body
- `RL_RECOIL_BLOCK` — short internal/muzzle mechanical kick
- `RL_TOP_LATCH` — loading-latch response

`RL_FIRE_MECHANICAL` is a 13-frame clip at 60 FPS. It deliberately animates
only weapon mechanisms. Whole-viewmodel recoil, sway, bob, muzzle flash, smoke,
light, sound, and projectile presentation belong in game code.

Runtime metadata nodes:

- `RL_MUZZLE_SOCKET`
- `RL_RIGHT_HAND_GRIP_SOCKET`
- `RL_SUPPORT_HAND_GRIP_SOCKET`
- `RL_PICKUP_CENTER`

The muzzle socket is presentation metadata only. Authoritative projectile
origins, collision, and damage remain in gameplay simulation.

Regenerate the authored outputs from the repository root:

```powershell
& 'C:\Program Files\Blender Foundation\Blender 5.1\blender.exe' `
  -b --factory-startup `
  --python assets/models/rocket_launcher/create_rocket_launcher.py
```

Outputs:

- `lg_duel_rocket_launcher.blend` — editable Blender source
- `lg_duel_rocket_launcher.glb` — runtime-oriented export
- `lg_duel_rocket_launcher.fbx` — interchange export
- `lg_duel_rocket_launcher_preview.png` — rendered preview

`PREVIEW_NOT_FOR_EXPORT` contains only the camera, lights, and floor.

Bake the cooked GLB into the runtime material-mesh header with:

```powershell
& 'C:\Program Files\Blender Foundation\Blender 5.1\blender.exe' `
  -b --factory-startup `
  --python assets/models/rocket_launcher/export_rocket_launcher_header.py
```

This creates `src/render/BakedRocketLauncherModel.hpp`, splitting the static
body, recoil block, and top latch while retaining their pivots and the authored
grip/muzzle metadata. The generated header is runtime input and should not be
hand-edited.
