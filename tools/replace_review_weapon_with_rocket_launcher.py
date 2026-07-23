"""Replace the review revolver with LG Duel's rocket launcher."""

from __future__ import annotations

import sys
from pathlib import Path

import bpy
from mathutils import Matrix, Vector


def remove_tree(root: bpy.types.Object) -> None:
    children = list(root.children)
    for child in children:
        remove_tree(child)
    bpy.data.objects.remove(root, do_unlink=True)


def main() -> None:
    args = sys.argv[sys.argv.index("--") + 1:]
    review = Path(args[0]).resolve()
    launcher = Path(args[1]).resolve()
    output = Path(args[2]).resolve()
    bpy.ops.wm.open_mainfile(filepath=str(review), load_ui=False)

    revolver = bpy.data.objects.get("REV_ROOT_game_axes_x_forward")
    if revolver is not None:
        remove_tree(revolver)

    with bpy.data.libraries.load(str(launcher), link=False) as (source, target):
        if "ROCKET_LAUNCHER_EXPORT" not in source.collections:
            raise RuntimeError("rocket launcher export collection is missing")
        target.collections = ["ROCKET_LAUNCHER_EXPORT"]
    collection = target.collections[0]
    bpy.context.scene.collection.children.link(collection)

    root = bpy.data.objects.get("RL_RIG")
    grip = bpy.data.objects.get("RL_RIGHT_HAND_GRIP_SOCKET")
    support_grip = bpy.data.objects.get("RL_SUPPORT_HAND_GRIP_SOCKET")
    hand_socket = bpy.data.objects.get("weapon_socket")
    if None in (root, grip, support_grip, hand_socket):
        raise RuntimeError("required Worker or rocket launcher socket is missing")

    bpy.context.view_layer.update()
    grip_position = grip.get("lg_weapon_space_position")
    if grip_position is None:
        raise RuntimeError("rocket launcher right-hand grip metadata is missing")
    grip_from_root = Matrix.Translation(Vector(grip_position))
    # The RL uses +X along the barrel and +Z up. The Worker faces -Y in this
    # scene, so keep the launcher upright and map its barrel to global -Y.
    socket_basis = Matrix((
        (0.0, 1.0, 0.0, 0.0),
        (-1.0, 0.0, 0.0, 0.0),
        (0.0, 0.0, 1.0, 0.0),
        (0.0, 0.0, 0.0, 1.0),
    ))
    desired_grip = (
        Matrix.Translation(hand_socket.matrix_world.translation)
        @ socket_basis
        @ Matrix.Diagonal((0.50, 0.50, 0.50, 1.0))
    )
    root.matrix_world = desired_grip @ grip_from_root.inverted()
    aligned_world = root.matrix_world.copy()
    root.parent = hand_socket
    root.matrix_parent_inverse = hand_socket.matrix_world.inverted()
    root.matrix_world = aligned_world
    root["review_attachment"] = "RL_RIGHT_HAND_GRIP_SOCKET -> weapon_socket"
    root["review_scale"] = 0.50
    root["review_forward"] = "authored +X -> Worker forward -Y"

    rig = next(
        obj for obj in bpy.context.scene.objects
        if obj.type == "ARMATURE" and obj.name != "RL_RIG"
    )
    action = bpy.data.actions.get("Idle_Gun_TwoHanded")
    if action is None:
        raise RuntimeError("Idle_Gun_TwoHanded is missing")
    rig.animation_data_create()
    rig.animation_data.action = action
    bpy.context.scene.frame_start = int(action.frame_range[0])
    bpy.context.scene.frame_end = int(action.frame_range[1])
    bpy.context.scene.frame_set(int(action.frame_range[0]))
    bpy.context.scene["review_note"] = (
        "Idle_Gun_TwoHanded with the LG Duel rocket launcher. Press Space to play."
    )
    output.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(output), check_existing=False)


if __name__ == "__main__":
    main()
