# Machine Gun

The machine gun uses the authored-weapon convention: `+X` points forward and
`+Z` points up.

`MG_BARREL_SPIN_AXIS` owns the six barrel tubes, their dark bores, both cluster
collars, and the central axle. `MG_BARREL_REFERENCE_LOOP` rotates that hierarchy
exactly once around local X over one second with linear interpolation.

The Blender clip verifies the pivot and hierarchy. It is not the gameplay spin
controller. Runtime presentation code integrates angular velocity so holding
attack ramps the cluster up and releasing it lets momentum dissipate gradually.
Fire cadence, ammunition, hitscan, and damage remain authoritative simulation.

Regenerate the hierarchy, animation, exports, and preview from the repository
root:

```powershell
& 'C:\Program Files\Blender Foundation\Blender 5.1\blender.exe' `
  -b assets/models/machine_gun/lg_duel_machine_gun_minigun_steel.blend `
  --python assets/models/machine_gun/add_barrel_spin_animation.py
```

The export retains `MG_BARREL_SPIN_AXIS`, `MG_MUZZLE_SOCKET`,
`MG_CASING_EJECT_SOCKET`, grip metadata, and the reference action while
excluding preview lights, camera, floor, and the visible editor-only muzzle
marker. The muzzle socket sits on the cluster center because this design fires
through the common front ring; the casing socket stays on the fixed receiver.
