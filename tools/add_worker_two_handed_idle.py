"""Add a separate two-handed gun idle to a Worker review file."""

from __future__ import annotations

import sys
import math
from pathlib import Path

import bpy
from mathutils import Quaternion, Vector


def main() -> None:
    args = sys.argv[sys.argv.index("--") + 1:]
    source = Path(args[0]).resolve()
    output = Path(args[1]).resolve()
    bpy.ops.wm.open_mainfile(filepath=str(source), load_ui=False)

    rig = next((obj for obj in bpy.context.scene.objects if obj.type == "ARMATURE"), None)
    idle = bpy.data.actions.get("Idle_Gun_Pointing")
    if rig is None or idle is None:
        raise RuntimeError("Worker rig or gun idle is missing")
    for bone_name in ("LowerArm.L", "Wrist.L", "Wrist.R"):
        if bone_name not in rig.pose.bones:
            raise RuntimeError(f"required bone is missing: {bone_name}")

    old = bpy.data.actions.get("Idle_Gun_TwoHanded")
    if old is not None:
        bpy.data.actions.remove(old)
    action = idle.copy()
    action.name = "Idle_Gun_TwoHanded"
    rig.animation_data_create()
    rig.animation_data.action = action

    start = int(action.frame_range[0])
    end = int(action.frame_range[1])
    scene = bpy.context.scene
    scene.frame_start = start
    scene.frame_end = end

    target = bpy.data.objects.new("two_hand_grip_target", None)
    pole = bpy.data.objects.new("two_hand_elbow_pole", None)
    scene.collection.objects.link(target)
    scene.collection.objects.link(pole)
    target.hide_render = True
    pole.hide_render = True

    # Keep the support wrist close to the weapon wrist. The fixed world-space
    # offset prevents both hands from occupying the same point.
    for frame in range(start, end + 1):
        scene.frame_set(frame)
        scene.view_layers[0].update()
        weapon_wrist = rig.matrix_world @ rig.pose.bones["Wrist.R"].head
        left_shoulder = rig.matrix_world @ rig.pose.bones["UpperArm.L"].head
        target.location = weapon_wrist + Vector((0.18, 0.0, 0.015))
        pole.location = left_shoulder + Vector((0.35, -0.45, -0.05))
        target.keyframe_insert(data_path="location", frame=frame)
        pole.keyframe_insert(data_path="location", frame=frame)

    lower_arm = rig.pose.bones["LowerArm.L"]
    ik = lower_arm.constraints.new("IK")
    ik.name = "Two-handed gun grip"
    ik.target = target
    ik.pole_target = pole
    ik.chain_count = 2
    ik.use_tail = True

    bpy.context.view_layer.objects.active = rig
    rig.select_set(True)
    bpy.ops.nla.bake(
        frame_start=start,
        frame_end=end,
        step=1,
        only_selected=False,
        visual_keying=True,
        clear_constraints=True,
        clear_parents=False,
        use_current_action=True,
        bake_types={"POSE"},
    )
    bpy.data.objects.remove(target, do_unlink=True)
    bpy.data.objects.remove(pole, do_unlink=True)

    # Roll the support wrist a quarter turn around the forearm so its palm
    # faces sideways into the launcher rather than up or down.
    wrist = rig.pose.bones["Wrist.L"]
    for frame in range(start, end + 1):
        scene.frame_set(frame)
        scene.view_layers[0].update()
        current = wrist.matrix_basis.to_quaternion()
        wrist.rotation_mode = "QUATERNION"
        wrist.rotation_quaternion = current @ Quaternion(
            (0.0, 1.0, 0.0), math.pi * 0.5
        )
        wrist.keyframe_insert(data_path="rotation_quaternion", frame=frame)

    action.name = "Idle_Gun_TwoHanded"
    action["source_action"] = idle.name
    action["review_status"] = "new two-handed gun idle; original idle preserved"
    rig.animation_data.action = action
    scene.frame_set(start)
    scene["review_note"] = (
        "Idle_Gun_TwoHanded is selected. Press Space to play. "
        "The original IDLE and Idle_Gun clips remain unchanged."
    )
    output.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(output), check_existing=False)


if __name__ == "__main__":
    main()
