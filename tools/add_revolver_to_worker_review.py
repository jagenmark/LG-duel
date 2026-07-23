"""Append the LG Duel revolver and align its grip socket to Worker's hand."""

from __future__ import annotations

import sys
from pathlib import Path

import bpy
from mathutils import Matrix


def main() -> None:
    args = sys.argv[sys.argv.index("--") + 1:]
    review = Path(args[0]).resolve()
    revolver = Path(args[1]).resolve()
    output = Path(args[2]).resolve()
    bpy.ops.wm.open_mainfile(filepath=str(review), load_ui=False)

    with bpy.data.libraries.load(str(revolver), link=False) as (source, target):
        target.objects = list(source.objects)
    appended = [obj for obj in target.objects if obj is not None]
    for obj in appended:
        if obj.name not in bpy.context.scene.collection.objects:
            bpy.context.scene.collection.objects.link(obj)
    for obj in list(appended):
        if obj.type in {"CAMERA", "LIGHT"} or obj.name.startswith("preview_"):
            bpy.data.objects.remove(obj, do_unlink=True)

    root = bpy.data.objects.get("REV_ROOT_game_axes_x_forward")
    grip = bpy.data.objects.get("REV_RIGHT_HAND_GRIP_SOCKET")
    hand_socket = bpy.data.objects.get("weapon_socket")
    if root is None or grip is None or hand_socket is None:
        raise RuntimeError("required Worker or revolver socket is missing")
    rig = next((obj for obj in bpy.context.scene.objects if obj.type == "ARMATURE"), None)
    idle = bpy.data.actions.get("IDLE")
    if rig is None or idle is None:
        raise RuntimeError("Worker rig or IDLE action is missing")
    rig.animation_data_create()
    rig.animation_data.action = idle
    bpy.context.scene.frame_start = int(idle.frame_range[0])
    bpy.context.scene.frame_end = int(idle.frame_range[1])
    bpy.context.scene.frame_set(25)
    bpy.context.view_layer.update()
    grip_from_root = root.matrix_world.inverted() @ grip.matrix_world
    # Runtime convention: the revolver uses +X along the barrel and its remote
    # player scale is 0.45. The review socket expects barrel-forward at -Z.
    socket_basis = Matrix((
        (0.0, 1.0, 0.0, 0.0),
        (0.0, 0.0, -1.0, 0.0),
        (-1.0, 0.0, 0.0, 0.0),
        (0.0, 0.0, 0.0, 1.0),
    ))
    desired_grip = (
        Matrix.Translation(hand_socket.matrix_world.translation)
        @ socket_basis
        @ Matrix.Diagonal((0.45, 0.45, 0.45, 1.0))
    )
    root.matrix_world = desired_grip @ grip_from_root.inverted()
    aligned_world = root.matrix_world.copy()
    root.parent = hand_socket
    root.matrix_parent_inverse = hand_socket.matrix_world.inverted()
    root.matrix_world = aligned_world
    root["review_attachment"] = "REV_RIGHT_HAND_GRIP_SOCKET -> weapon_socket"
    output.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(output), check_existing=False)


if __name__ == "__main__":
    main()
