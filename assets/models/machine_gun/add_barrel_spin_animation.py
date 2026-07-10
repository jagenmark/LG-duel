"""Add the authored MG barrel hierarchy and reference spin animation.

Run from the repository root with the source blend already open:
  blender -b assets/models/machine_gun/lg_duel_machine_gun_minigun_steel.blend \
    --python assets/models/machine_gun/add_barrel_spin_animation.py
"""

from __future__ import annotations

import math
from pathlib import Path

import bpy


ASSET_DIR = Path(__file__).resolve().parent
BLEND_PATH = ASSET_DIR / "lg_duel_machine_gun_minigun_steel.blend"
GLB_PATH = ASSET_DIR / "lg_duel_machine_gun_minigun_steel.glb"
FBX_PATH = ASSET_DIR / "lg_duel_machine_gun_minigun_steel.fbx"
PREVIEW_PATH = ASSET_DIR / "lg_duel_machine_gun_minigun_steel_preview.png"

ROTATING_PARTS = (
    *(f"rotating_barrel_{index:02d}_steel_tube" for index in range(1, 7)),
    *(f"barrel_{index:02d}_dark_bore" for index in range(1, 7)),
    "front_barrel_cluster_collar",
    "rear_barrel_cluster_collar",
    "central_dark_rotation_axis",
)


def reparent_preserving_world(obj, parent):
    world = obj.matrix_world.copy()
    obj.parent = parent
    obj.matrix_world = world


def add_reference_animation(axis):
    axis.animation_data_clear()
    for existing in list(bpy.data.actions):
        if existing.name.startswith("MG_BARREL_REFERENCE_LOOP"):
            bpy.data.actions.remove(existing)
    axis.rotation_mode = "XYZ"
    # Quarter-turn keys prevent glTF's quaternion export from collapsing the
    # identical 0- and 360-degree endpoints into a motionless animation.
    for frame, angle in (
        (1, 0.0),
        (16, math.pi * 0.5),
        (31, math.pi),
        (46, math.pi * 1.5),
        (61, math.tau),
    ):
        axis.rotation_euler = (angle, 0.0, 0.0)
        axis.keyframe_insert("rotation_euler", frame=frame, group="barrel reference spin")
    action = axis.animation_data.action
    action.name = "MG_BARREL_REFERENCE_LOOP"

    # This clip is a pivot/export reference only. Runtime code integrates the
    # actual angular velocity, so the verification loop must be exactly linear.
    fcurves = []
    for layer in action.layers:
        for strip in layer.strips:
            for channelbag in strip.channelbags:
                fcurves.extend(channelbag.fcurves)
    for fcurve in fcurves:
        for keyframe in fcurve.keyframe_points:
            keyframe.interpolation = "LINEAR"

    scene = bpy.context.scene
    scene.frame_start = 1
    scene.frame_end = 61
    scene.render.fps = 60
    scene.frame_set(1)


def export_game_asset(root):
    bpy.ops.object.select_all(action="DESELECT")
    selected = []
    for obj in bpy.data.objects:
        is_preview = (
            obj.type in {"CAMERA", "LIGHT"}
            or "preview_" in obj.name.lower()
            or "marker_remove_before_runtime" in obj.name.lower()
        )
        if not is_preview and (obj == root or obj.parent is not None):
            obj.select_set(True)
            selected.append(obj)
    bpy.context.view_layer.objects.active = root
    bpy.ops.export_scene.gltf(
        filepath=str(GLB_PATH), export_format="GLB", use_selection=True,
        export_animations=True, export_yup=True,
    )
    bpy.ops.export_scene.fbx(
        filepath=str(FBX_PATH), use_selection=True,
        object_types={"EMPTY", "MESH"}, bake_anim=True,
        add_leaf_bones=False, axis_forward="X", axis_up="Z",
    )
    return selected


def main():
    root = bpy.data.objects["MG_ROOT_game_axes_x_forward"]
    axis = bpy.data.objects["MG_BARREL_SPIN_AXIS"]
    for name in ROTATING_PARTS:
        reparent_preserving_world(bpy.data.objects[name], axis)
    add_reference_animation(axis)

    bpy.ops.wm.save_as_mainfile(filepath=str(BLEND_PATH))
    export_game_asset(root)
    scene = bpy.context.scene
    scene.frame_set(1)
    scene.render.filepath = str(PREVIEW_PATH)
    scene.render.image_settings.file_format = "PNG"
    bpy.ops.render.render(write_still=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(BLEND_PATH))
    print("MG_BARREL_SPIN_AXIS children:")
    for child in sorted(axis.children, key=lambda item: item.name):
        print(f"  {child.name}")
    print(f"Created action {axis.animation_data.action.name}")


if __name__ == "__main__":
    main()
