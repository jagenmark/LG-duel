# Ratch lean audit

`LEAN_LEFT` is preserved correctly from Pip, but the inherited action is not a
visible side lean. Its only non-rest pose is a constant Spine quaternion
`(x=0, y=0.14943814, z=0, w=0.98877108)`, equivalent to 0.3 radians
(17.19 degrees) around the bone's local Y axis. Blender bones point along local
Y, so this is an axial torso twist; it does not displace the head sideways.
The clip spans 0.0416667 to 1.0416666 seconds and is constant throughout, so a
different frame cannot reveal a stronger lean.

The current Worker runtime does not use this action for ordinary presentation
frames. `skinnedPlayerPoseRequests` filters the Worker's upper-body
`LEAN_LEFT`/`LEAN_RIGHT` layer, then `appendBonePalette` rotates the Spine
subtree around the Spine pivot in armature space by `proceduralLean * 34`
degrees. This preserves the sampled locomotion or idle pose and produces the
actual visible side lean.

`review_animations.py` therefore previews `LEAN_LEFT` as the runtime standing
composition: `Idle_Gun_TwoHanded` at frame 12 plus 34 degrees around armature
Y at the Spine pivot. In the imported Ratch rig this moves the Head origin by
`(+0.240453, 0, -0.073514)` Blender units, confirming a visible left bend.
The runtime glTF rotation is about -Z; Blender +Y exports as glTF -Z, so these
transforms have the same sign and axis.

`validate_ratch.py` passes all 25 animations: the logical input/output bytes
and name-remapped channel targets in Ratch match Pip, including `LEAN_LEFT`.
The preview-only composition does not modify either GLB or its animation data.
