"""Build the LG Duel industrial rocket launcher in Blender.

Run from the repository root with:
    blender -b --factory-startup --python assets/models/rocket_launcher/create_rocket_launcher.py

The asset convention is +X forward and +Z up. Preview objects live in a
separate collection and are excluded from game exports.
"""

from __future__ import annotations

import math
from pathlib import Path

import bpy
from mathutils import Vector


ASSET_DIR = Path(__file__).resolve().parent
BLEND_PATH = ASSET_DIR / "lg_duel_rocket_launcher.blend"
GLB_PATH = ASSET_DIR / "lg_duel_rocket_launcher.glb"
FBX_PATH = ASSET_DIR / "lg_duel_rocket_launcher.fbx"
PREVIEW_PATH = ASSET_DIR / "lg_duel_rocket_launcher_preview.png"


def clear_scene():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)


def new_collection(name):
    result = bpy.data.collections.new(name)
    bpy.context.scene.collection.children.link(result)
    return result


def move_to(obj, target):
    for current in list(obj.users_collection):
        current.objects.unlink(obj)
    target.objects.link(obj)


def material(name, color, metallic=0.0, roughness=0.45):
    mat = bpy.data.materials.new(name)
    mat.diffuse_color = (*color, 1.0)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value = (*color, 1.0)
    bsdf.inputs["Metallic"].default_value = metallic
    bsdf.inputs["Roughness"].default_value = roughness
    return mat


def assign(obj, mat):
    obj.data.materials.append(mat)


def apply_bevel(obj, width=0.008, segments=2):
    modifier = obj.modifiers.new("readable edge bevel", "BEVEL")
    modifier.width = width
    modifier.segments = segments
    modifier.limit_method = "ANGLE"
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.modifier_apply(modifier=modifier.name)
    obj.select_set(False)


def bone_parent(obj, rig, bone):
    world = obj.matrix_world.copy()
    obj.parent = rig
    obj.parent_type = "BONE"
    obj.parent_bone = bone
    # Bone parenting adds a head-relative transform. Restore the authored
    # weapon-space matrix so rigid pieces do not jump during generation.
    obj.matrix_world = world


def box(name, location, size, mat, rig, bone, target, bevel=0.008, rotation=None):
    bpy.ops.mesh.primitive_cube_add(location=location, rotation=rotation or (0, 0, 0))
    obj = bpy.context.object
    obj.name = name
    obj.scale = tuple(value * 0.5 for value in size)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    if bevel:
        apply_bevel(obj, bevel, 2)
    assign(obj, mat)
    move_to(obj, target)
    bone_parent(obj, rig, bone)
    return obj


def cylinder_x(name, location, radius, depth, mat, rig, bone, target,
               vertices=16, bevel=0.005):
    bpy.ops.mesh.primitive_cylinder_add(
        vertices=vertices, radius=radius, depth=depth, location=location,
        rotation=(0.0, math.pi / 2.0, 0.0)
    )
    obj = bpy.context.object
    obj.name = name
    if bevel:
        apply_bevel(obj, bevel, 1)
    assign(obj, mat)
    move_to(obj, target)
    bone_parent(obj, rig, bone)
    return obj


def torus_x(name, location, major_radius, minor_radius, mat, rig, bone, target):
    bpy.ops.mesh.primitive_torus_add(
        major_radius=major_radius, minor_radius=minor_radius,
        major_segments=16, minor_segments=6, location=location,
        rotation=(0.0, math.pi / 2.0, 0.0)
    )
    obj = bpy.context.object
    obj.name = name
    assign(obj, mat)
    move_to(obj, target)
    bone_parent(obj, rig, bone)
    return obj


def empty(name, location, rig, bone, target, size=0.07):
    obj = bpy.data.objects.new(name, None)
    obj.location = location
    # Bone-parented empties have bone-relative transforms in Blender. Retain
    # the authored weapon-space point explicitly for deterministic baking.
    obj["lg_weapon_space_position"] = list(location)
    obj.empty_display_type = "ARROWS"
    obj.empty_display_size = size
    target.objects.link(obj)
    bone_parent(obj, rig, bone)
    return obj


def build_rig(target):
    data = bpy.data.armatures.new("RL_RIG_DATA")
    rig = bpy.data.objects.new("RL_RIG", data)
    target.objects.link(rig)
    rig.show_in_front = True
    bpy.context.view_layer.objects.active = rig
    rig.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")

    root = data.edit_bones.new("RL_ROOT")
    root.head = (0.0, 0.0, 0.0)
    root.tail = (0.18, 0.0, 0.0)

    recoil = data.edit_bones.new("RL_RECOIL_BLOCK")
    recoil.head = (0.50, 0.0, 0.08)
    recoil.tail = (0.70, 0.0, 0.08)
    recoil.parent = root

    latch = data.edit_bones.new("RL_TOP_LATCH")
    latch.head = (-0.35, 0.0, 0.28)
    latch.tail = (-0.15, 0.0, 0.28)
    latch.parent = root

    bpy.ops.object.mode_set(mode="OBJECT")
    rig.select_set(False)
    return rig


def add_fire_animation(rig):
    scene = bpy.context.scene
    scene.frame_start = 1
    scene.frame_end = 13
    scene.render.fps = 60
    rig.animation_data_create()
    action = bpy.data.actions.new("RL_FIRE_MECHANICAL")
    rig.animation_data.action = action

    recoil = rig.pose.bones["RL_RECOIL_BLOCK"]
    latch = rig.pose.bones["RL_TOP_LATCH"]
    for frame, x in ((1, 0.0), (2, 0.0), (4, -0.052), (7, 0.009), (11, 0.0), (13, 0.0)):
        recoil.location = (x, 0.0, 0.0)
        recoil.keyframe_insert("location", frame=frame, group="mechanical recoil")
    for frame, angle in ((1, 0.0), (2, 0.0), (4, math.radians(-8.0)),
                         (7, math.radians(2.0)), (11, 0.0), (13, 0.0)):
        latch.rotation_mode = "XYZ"
        latch.rotation_euler = (0.0, angle, 0.0)
        latch.keyframe_insert("rotation_euler", frame=frame, group="top latch kick")
    scene.frame_set(1)


def look_at(obj, target):
    obj.rotation_euler = (Vector(target) - obj.location).to_track_quat("-Z", "Y").to_euler()


def add_preview(target, floor_mat):
    bpy.ops.mesh.primitive_plane_add(size=5.0, location=(0.0, 0.0, -0.61))
    floor = bpy.context.object
    floor.name = "preview_floor_not_for_export"
    assign(floor, floor_mat)
    move_to(floor, target)

    bpy.ops.object.camera_add(location=(2.60, -6.80, 2.15))
    camera = bpy.context.object
    camera.name = "preview_camera"
    camera.data.lens = 72
    look_at(camera, (0.03, 0.0, -0.06))
    move_to(camera, target)
    bpy.context.scene.camera = camera

    def light(name, location, energy, size, color):
        data = bpy.data.lights.new(name, "AREA")
        data.energy = energy
        data.shape = "DISK"
        data.size = size
        data.color = color
        obj = bpy.data.objects.new(name, data)
        obj.location = location
        look_at(obj, (0.05, 0.0, 0.0))
        target.objects.link(obj)

    light("preview_key", (0.7, -2.4, 2.5), 520, 1.5, (1.0, 0.88, 0.76))
    light("preview_fill", (-1.5, -0.4, 0.9), 260, 1.2, (0.55, 0.70, 1.0))
    light("preview_rim", (0.9, 1.5, 1.8), 360, 1.0, (1.0, 0.34, 0.20))


def build():
    clear_scene()
    export = new_collection("ROCKET_LAUNCHER_EXPORT")
    preview = new_collection("PREVIEW_NOT_FOR_EXPORT")
    rig = build_rig(export)

    dark = material("RL aged blued steel", (0.045, 0.055, 0.065), 0.86, 0.31)
    steel = material("RL cast steel", (0.22, 0.21, 0.19), 0.82, 0.40)
    edge = material("RL worn steel edges", (0.39, 0.37, 0.33), 0.90, 0.30)
    black = material("RL matte black grip", (0.015, 0.018, 0.020), 0.12, 0.62)
    bore = material("RL bore interior", (0.003, 0.004, 0.005), 0.05, 0.48)
    red = material("RL coral identification paint", (0.62, 0.075, 0.038), 0.55, 0.38)
    cyan = material("RL cyan status lamp", (0.01, 0.65, 0.82), 0.20, 0.22)
    floor = material("preview charcoal", (0.010, 0.013, 0.018), 0.05, 0.60)

    # Main launch tube and restrained stepped heat shielding.
    cylinder_x("RL_MAIN_LAUNCH_TUBE", (0.25, 0.0, 0.10), 0.225, 1.48,
               dark, rig, "RL_ROOT", export, 20, 0.008)
    cylinder_x("RL_REAR_TUBE_COLLAR", (-0.45, 0.0, 0.10), 0.252, 0.11,
               edge, rig, "RL_ROOT", export, 16, 0.006)
    cylinder_x("RL_FORWARD_HEAT_SHIELD", (0.63, 0.0, 0.10), 0.242, 0.40,
               steel, rig, "RL_RECOIL_BLOCK", export, 16, 0.006)
    cylinder_x("RL_MUZZLE_BODY", (0.92, 0.0, 0.10), 0.265, 0.22,
               dark, rig, "RL_RECOIL_BLOCK", export, 16, 0.008)
    torus_x("RL_MUZZLE_RED_ID_RING", (1.045, 0.0, 0.10), 0.244, 0.045,
            red, rig, "RL_RECOIL_BLOCK", export)
    cylinder_x("RL_MUZZLE_BLACK_INTERIOR", (1.067, 0.0, 0.10), 0.205, 0.018,
               bore, rig, "RL_RECOIL_BLOCK", export, 20, 0.0)

    # Boxy rear mechanism with broad readable stamped panels.
    box("RL_REAR_RECEIVER", (-0.65, 0.0, 0.10), (0.52, 0.53, 0.55),
        steel, rig, "RL_ROOT", export, 0.025)
    box("RL_RECEIVER_LEFT_PANEL", (-0.67, -0.278, 0.11), (0.34, 0.032, 0.34),
        dark, rig, "RL_ROOT", export, 0.012)
    for z in (-0.005, 0.09, 0.185):
        box(f"RL_COOLING_SLOT_{z:+.3f}", (-0.69, -0.299, z), (0.23, 0.018, 0.035),
            bore, rig, "RL_ROOT", export, 0.009)
    box("RL_STATUS_LAMP", (-0.47, -0.302, 0.27), (0.045, 0.015, 0.045),
        cyan, rig, "RL_ROOT", export, 0.008)

    # Lower spine and two practical grips.
    box("RL_LOWER_SPINE", (0.20, 0.0, -0.18), (1.48, 0.15, 0.10),
        edge, rig, "RL_ROOT", export, 0.012)
    box("RL_PRIMARY_GRIP", (-0.57, 0.0, -0.34), (0.20, 0.19, 0.48),
        black, rig, "RL_ROOT", export, 0.020, rotation=(0.0, math.radians(-10), 0.0))
    box("RL_TRIGGER_GUARD", (-0.40, 0.0, -0.16), (0.30, 0.11, 0.12),
        dark, rig, "RL_ROOT", export, 0.020)
    box("RL_TRIGGER", (-0.42, -0.005, -0.19), (0.045, 0.07, 0.15),
        red, rig, "RL_ROOT", export, 0.008, rotation=(0.0, math.radians(-12), 0.0))
    box("RL_SUPPORT_GRIP", (0.43, 0.0, -0.39), (0.17, 0.17, 0.42),
        black, rig, "RL_ROOT", export, 0.020)

    # Short carry handle and a mechanically animated loading latch.
    box("RL_HANDLE_FRONT_POST", (0.50, 0.0, 0.42), (0.10, 0.13, 0.28),
        edge, rig, "RL_ROOT", export, 0.012)
    box("RL_HANDLE_REAR_POST", (0.06, 0.0, 0.42), (0.10, 0.13, 0.28),
        edge, rig, "RL_ROOT", export, 0.012)
    box("RL_SHORT_CARRY_HANDLE", (0.28, 0.0, 0.55), (0.52, 0.15, 0.12),
        black, rig, "RL_ROOT", export, 0.020)
    box("RL_TOP_LOADING_LATCH", (-0.47, 0.0, 0.43), (0.28, 0.22, 0.10),
        red, rig, "RL_TOP_LATCH", export, 0.018)

    # Metadata nodes remain presentation-only. Gameplay muzzle origin stays
    # authoritative in simulation and is never derived from this socket.
    empty("RL_MUZZLE_SOCKET", (1.10, 0.0, 0.10), rig, "RL_RECOIL_BLOCK", export)
    empty("RL_RIGHT_HAND_GRIP_SOCKET", (-0.58, 0.0, -0.35), rig, "RL_ROOT", export)
    empty("RL_SUPPORT_HAND_GRIP_SOCKET", (0.43, 0.0, -0.39), rig, "RL_ROOT", export)
    empty("RL_PICKUP_CENTER", (0.10, 0.0, 0.02), rig, "RL_ROOT", export)

    add_fire_animation(rig)
    add_preview(preview, floor)

    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.resolution_x = 1536
    scene.render.resolution_y = 1024
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.filepath = str(PREVIEW_PATH)
    scene.world.color = (0.004, 0.005, 0.008)
    scene.view_settings.look = "AgX - Medium High Contrast"
    ASSET_DIR.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(BLEND_PATH))

    bpy.ops.object.select_all(action="DESELECT")
    for obj in export.all_objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = rig
    bpy.ops.export_scene.gltf(filepath=str(GLB_PATH), export_format="GLB",
                              use_selection=True, export_animations=True,
                              export_yup=True)
    bpy.ops.export_scene.fbx(filepath=str(FBX_PATH), use_selection=True,
                             object_types={"ARMATURE", "EMPTY", "MESH"},
                             bake_anim=True, add_leaf_bones=False,
                             axis_forward="X", axis_up="Z")
    scene.frame_set(1)
    bpy.ops.render.render(write_still=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(BLEND_PATH))
    print(f"Created {BLEND_PATH}")
    print(f"Created {GLB_PATH}")
    print(f"Created {FBX_PATH}")
    print(f"Created {PREVIEW_PATH}")


if __name__ == "__main__":
    build()
