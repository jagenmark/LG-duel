"""Build the LG Duel low/medium-poly stainless revolver in Blender.

Run from the repository root with:
    blender -b --python assets/models/revolver/create_revolver.py

The script creates the editable .blend, a GLB and FBX export, and a preview PNG.
Weapon-space convention matches the other authored weapons: +X forward, +Z up.
"""

from __future__ import annotations

import math
from pathlib import Path

import bpy
from mathutils import Vector


ASSET_DIR = Path(__file__).resolve().parent
BLEND_PATH = ASSET_DIR / "lg_duel_revolver_stainless.blend"
GLB_PATH = ASSET_DIR / "lg_duel_revolver_stainless.glb"
FBX_PATH = ASSET_DIR / "lg_duel_revolver_stainless.fbx"
PREVIEW_PATH = ASSET_DIR / "lg_duel_revolver_stainless_preview.png"


def clear_scene() -> None:
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for datablocks in (bpy.data.meshes, bpy.data.curves, bpy.data.materials,
                       bpy.data.cameras, bpy.data.lights):
        for block in list(datablocks):
            if block.users == 0:
                datablocks.remove(block)


def collection(name: str) -> bpy.types.Collection:
    result = bpy.data.collections.new(name)
    bpy.context.scene.collection.children.link(result)
    return result


def move_to_collection(obj: bpy.types.Object, target: bpy.types.Collection) -> None:
    for current in list(obj.users_collection):
        current.objects.unlink(obj)
    target.objects.link(obj)


def material(name: str, color, metallic=0.0, roughness=0.45) -> bpy.types.Material:
    mat = bpy.data.materials.new(name)
    mat.diffuse_color = (*color, 1.0)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value = (*color, 1.0)
    bsdf.inputs["Metallic"].default_value = metallic
    bsdf.inputs["Roughness"].default_value = roughness
    return mat


def assign_material(obj: bpy.types.Object, mat: bpy.types.Material) -> None:
    obj.data.materials.append(mat)


def bevel(obj: bpy.types.Object, width=0.008, segments=2) -> None:
    modifier = obj.modifiers.new("small readable bevels", "BEVEL")
    modifier.width = width
    modifier.segments = segments
    modifier.limit_method = "ANGLE"
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.modifier_apply(modifier=modifier.name)
    obj.select_set(False)


def box(name, location, scale, mat, parent, target, bevel_width=0.008):
    bpy.ops.mesh.primitive_cube_add(location=location)
    obj = bpy.context.object
    obj.name = name
    obj.scale = (scale[0] / 2, scale[1] / 2, scale[2] / 2)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    if bevel_width:
        bevel(obj, bevel_width, 2)
    assign_material(obj, mat)
    obj.parent = parent
    move_to_collection(obj, target)
    return obj


def cylinder_x(name, location, radius, depth, vertices, mat, parent, target,
               rotation=0.0, bevel_width=0.004):
    bpy.ops.mesh.primitive_cylinder_add(
        vertices=vertices,
        radius=radius,
        depth=depth,
        location=location,
        rotation=(0.0, math.pi / 2, 0.0),
    )
    obj = bpy.context.object
    obj.name = name
    # Local cylinder Z becomes world X. Rotating around local Z before the axis
    # alignment controls the readable facets around the chamber.
    obj.rotation_euler.rotate_axis("Z", rotation)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    if bevel_width:
        bevel(obj, bevel_width, 1)
    assign_material(obj, mat)
    obj.parent = parent
    move_to_collection(obj, target)
    return obj


def profile_prism(name, points, y_center, thickness, mat, parent, target,
                  bevel_width=0.006):
    """Extrude an X/Z outline across Y."""
    half = thickness / 2
    vertices = [(x, y_center - half, z) for x, z in points]
    vertices += [(x, y_center + half, z) for x, z in points]
    count = len(points)
    faces = [tuple(range(count - 1, -1, -1)), tuple(range(count, count * 2))]
    for i in range(count):
        j = (i + 1) % count
        faces.append((i, j, count + j, count + i))
    mesh = bpy.data.meshes.new(f"{name}_mesh")
    mesh.from_pydata(vertices, [], faces)
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    target.objects.link(obj)
    assign_material(obj, mat)
    obj.parent = parent
    if bevel_width:
        bevel(obj, bevel_width, 2)
    return obj


def profile_ring(name, outer, inner, y_center, thickness, mat, parent, target,
                 bevel_width=0.004):
    """Extrude a ring made from equal-length X/Z outer and inner loops."""
    half = thickness / 2
    count = len(outer)
    vertices = []
    vertices += [(x, y_center - half, z) for x, z in outer]
    vertices += [(x, y_center + half, z) for x, z in outer]
    vertices += [(x, y_center - half, z) for x, z in inner]
    vertices += [(x, y_center + half, z) for x, z in inner]
    faces = []
    for i in range(count):
        j = (i + 1) % count
        ob_i, ob_j = i, j
        of_i, of_j = count + i, count + j
        ib_i, ib_j = 2 * count + i, 2 * count + j
        inf_i, inf_j = 3 * count + i, 3 * count + j
        faces += [
            (ob_i, ob_j, of_j, of_i),
            (ib_j, ib_i, inf_i, inf_j),
            (ob_j, ob_i, ib_i, ib_j),
            (of_i, of_j, inf_j, inf_i),
        ]
    mesh = bpy.data.meshes.new(f"{name}_mesh")
    mesh.from_pydata(vertices, [], faces)
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    target.objects.link(obj)
    assign_material(obj, mat)
    obj.parent = parent
    if bevel_width:
        bevel(obj, bevel_width, 2)
    return obj


def empty(name, location, parent, target, display="PLAIN_AXES", size=0.08):
    obj = bpy.data.objects.new(name, None)
    obj.location = location
    obj.empty_display_type = display
    obj.empty_display_size = size
    target.objects.link(obj)
    obj.parent = parent
    return obj


def add_fire_animation(recoil_root, cylinder_rotator) -> None:
    scene = bpy.context.scene
    scene.frame_start = 1
    scene.frame_end = 12
    scene.render.fps = 24

    # Short readable kick: 0.125 seconds to peak and settle by 0.5 seconds.
    poses = {
        1: ((0.0, 0.0, 0.0), 0.0),
        2: ((0.0, 0.0, 0.0), 0.0),
        4: ((-0.060, 0.0, 0.025), math.radians(-8.0)),
        7: ((0.012, 0.0, -0.004), math.radians(1.5)),
        10: ((0.0, 0.0, 0.0), 0.0),
        12: ((0.0, 0.0, 0.0), 0.0),
    }
    for frame, (location, pitch) in poses.items():
        recoil_root.location = location
        recoil_root.rotation_euler = (0.0, pitch, 0.0)
        recoil_root.keyframe_insert("location", frame=frame, group="recoil")
        recoil_root.keyframe_insert("rotation_euler", frame=frame, group="recoil")

    # One sixth of a full rotation per shot. Hold at rest, index during kick.
    for frame, angle in ((1, 0.0), (2, 0.0), (4, math.radians(60.0)),
                         (12, math.radians(60.0))):
        cylinder_rotator.rotation_euler = (angle, 0.0, 0.0)
        cylinder_rotator.keyframe_insert("rotation_euler", frame=frame,
                                         group="cylinder_index")

    for obj in (recoil_root, cylinder_rotator):
        if obj.animation_data and obj.animation_data.action:
            action = obj.animation_data.action
            action.name = "REV_FIRE_RECOIL" if obj is recoil_root else "REV_FIRE_CYLINDER"
            # Blender 5.x stores newly keyed curves in layered Action slots and
            # no longer exposes Action.fcurves directly. Keyframes already use
            # Bezier interpolation by default, so no compatibility shim is
            # needed here.
    scene.frame_set(1)


def look_at(obj, target) -> None:
    direction = Vector(target) - obj.location
    obj.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()


def add_preview(preview_collection, floor_mat) -> None:
    bpy.ops.mesh.primitive_plane_add(size=3.5, location=(0.12, 0.0, -0.49))
    floor = bpy.context.object
    floor.name = "preview_floor_not_for_game_export"
    assign_material(floor, floor_mat)
    move_to_collection(floor, preview_collection)

    bpy.ops.object.camera_add(location=(1.72, -2.05, 0.72))
    camera = bpy.context.object
    camera.name = "preview_camera_three_quarter"
    camera.data.lens = 62
    look_at(camera, (0.08, 0.0, -0.075))
    move_to_collection(camera, preview_collection)
    bpy.context.scene.camera = camera

    def area_light(name, location, energy, size, color):
        data = bpy.data.lights.new(name, "AREA")
        data.energy = energy
        data.shape = "DISK"
        data.size = size
        data.color = color
        obj = bpy.data.objects.new(name, data)
        obj.location = location
        look_at(obj, (0.08, 0.0, 0.0))
        preview_collection.objects.link(obj)
        return obj

    area_light("preview_softbox_key", (0.45, -1.3, 1.25), 480, 1.0,
               (1.0, 0.89, 0.75))
    area_light("preview_softbox_fill", (-0.65, 0.8, 0.55), 280, 0.8,
               (0.55, 0.72, 1.0))
    area_light("preview_top_rim", (0.25, 0.2, 1.5), 360, 0.65,
               (1.0, 1.0, 1.0))


def build() -> None:
    clear_scene()
    asset_collection = collection("REVOLVER_EXPORT")
    preview_collection = collection("PREVIEW_NOT_FOR_EXPORT")

    stainless = material("REV brushed stainless steel", (0.43, 0.47, 0.50), 0.92, 0.25)
    edge_steel = material("REV bright stainless edges", (0.69, 0.73, 0.75), 0.96, 0.18)
    dark_steel = material("REV dark steel recesses", (0.055, 0.064, 0.073), 0.82, 0.31)
    bore = material("REV black bore interiors", (0.008, 0.010, 0.012), 0.15, 0.36)
    wood = material("REV sealed dark walnut", (0.105, 0.030, 0.008), 0.05, 0.34)
    wood_edge = material("REV walnut edge facets", (0.22, 0.065, 0.012), 0.02, 0.38)
    floor_mat = material("preview charcoal floor", (0.014, 0.018, 0.024), 0.05, 0.54)

    root = empty("REV_ROOT_game_axes_x_forward", (0, 0, 0), None, asset_collection,
                 display="ARROWS", size=0.16)
    recoil = empty("REV_RECOIL_ROOT", (0, 0, 0), root, asset_collection,
                   display="PLAIN_AXES", size=0.12)

    # Chunky frame and upper receiver, shaped to echo the concept preview.
    frame_outline = [
        (-0.22, 0.22), (0.12, 0.235), (0.20, 0.17), (0.20, -0.015),
        (0.11, -0.10), (-0.11, -0.10), (-0.23, -0.015),
    ]
    profile_prism("frame_heavy_faceted_receiver", frame_outline, 0.0, 0.17,
                  stainless, recoil, asset_collection, 0.012)
    box("frame_top_bright_rib", (-0.01, 0.0, 0.225), (0.31, 0.18, 0.032),
        edge_steel, recoil, asset_collection, 0.006)
    box("frame_lower_shadow_block", (0.08, 0.0, -0.055), (0.24, 0.155, 0.072),
        dark_steel, recoil, asset_collection, 0.009)

    # Barrel assembly.
    box("barrel_main_octagonal_block", (0.405, 0.0, 0.155), (0.48, 0.145, 0.142),
        stainless, recoil, asset_collection, 0.014)
    box("barrel_top_highlight_strip", (0.405, 0.0, 0.228), (0.43, 0.105, 0.024),
        edge_steel, recoil, asset_collection, 0.005)
    box("barrel_underlug", (0.31, 0.0, 0.065), (0.29, 0.12, 0.06),
        dark_steel, recoil, asset_collection, 0.008)
    cylinder_x("muzzle_bright_lip", (0.65, 0.0, 0.155), 0.082, 0.032, 8,
               edge_steel, recoil, asset_collection, rotation=math.pi / 8, bevel_width=0.003)
    cylinder_x("muzzle_dark_bore", (0.668, 0.0, 0.155), 0.052, 0.010, 8,
               bore, recoil, asset_collection, rotation=math.pi / 8, bevel_width=0.0)
    profile_prism("front_sight", [(0.45, 0.236), (0.55, 0.236),
                                   (0.525, 0.282), (0.48, 0.282)],
                  0.0, 0.055, dark_steel, recoil, asset_collection, 0.004)

    # Cylinder and all indexing details share this pivot.
    cylinder_pivot = empty("REV_CYLINDER_ROTATOR_one_sixth_step", (0.025, 0.0, 0.085),
                           recoil, asset_collection, display="CIRCLE", size=0.18)
    cylinder_x("cylinder_12sided_body", (0.0, 0.0, 0.0), 0.145, 0.225, 12,
               stainless, cylinder_pivot, asset_collection, rotation=math.pi / 12,
               bevel_width=0.006)
    cylinder_x("cylinder_front_bright_cap", (0.117, 0.0, 0.0), 0.128, 0.010, 12,
               edge_steel, cylinder_pivot, asset_collection, rotation=math.pi / 12,
               bevel_width=0.002)
    cylinder_x("cylinder_center_pin", (0.126, 0.0, 0.0), 0.026, 0.012, 10,
               dark_steel, cylinder_pivot, asset_collection, bevel_width=0.002)
    for index in range(6):
        angle = math.radians(index * 60)
        y = math.cos(angle) * 0.074
        z = math.sin(angle) * 0.074
        cylinder_x(f"cylinder_chamber_recess_{index + 1}", (0.125, y, z), 0.026,
                   0.013, 10, bore, cylinder_pivot, asset_collection,
                   bevel_width=0.001)

    # Hammer and rear frame.
    profile_prism("hammer_spur", [(-0.22, 0.205), (-0.31, 0.275),
                                  (-0.34, 0.245), (-0.27, 0.145)],
                  0.0, 0.075, dark_steel, recoil, asset_collection, 0.006)
    box("hammer_cross_hatch_cap", (-0.31, 0.0, 0.267), (0.075, 0.095, 0.026),
        edge_steel, recoil, asset_collection, 0.004)

    # Grip: steel tang plus raised walnut panels on both sides.
    grip_outline = [
        (-0.20, 0.015), (-0.07, -0.035), (-0.13, -0.36),
        (-0.22, -0.435), (-0.38, -0.405), (-0.34, -0.285),
        (-0.29, -0.13),
    ]
    profile_prism("grip_full_tang_steel", grip_outline, 0.0, 0.14,
                  dark_steel, recoil, asset_collection, 0.010)
    panel_outline = [
        (-0.20, -0.010), (-0.095, -0.055), (-0.15, -0.335),
        (-0.225, -0.395), (-0.345, -0.37), (-0.31, -0.285),
        (-0.27, -0.13),
    ]
    for side, y in (("left", -0.079), ("right", 0.079)):
        profile_prism(f"walnut_grip_panel_{side}", panel_outline, y, 0.025,
                      wood, recoil, asset_collection, 0.010)
        for pin_index, (x, z) in enumerate(((-0.19, -0.11), (-0.25, -0.33)), 1):
            cylinder_x(f"grip_pin_{side}_{pin_index}", (x, y + (-0.014 if y < 0 else 0.014), z),
                       0.018, 0.010, 8, edge_steel, recoil, asset_collection,
                       bevel_width=0.002)
    profile_prism("grip_walnut_center_inlay", panel_outline, 0.0, 0.115,
                  wood_edge, recoil, asset_collection, 0.006)

    # Trigger guard as a faceted oval ring, plus a simple trigger blade.
    steps = 12
    center_x, center_z = (-0.02, -0.12)
    outer = [(center_x + math.cos(2 * math.pi * i / steps) * 0.135,
              center_z + math.sin(2 * math.pi * i / steps) * 0.115)
             for i in range(steps)]
    inner = [(center_x + math.cos(2 * math.pi * i / steps) * 0.092,
              center_z + math.sin(2 * math.pi * i / steps) * 0.075)
             for i in range(steps)]
    profile_ring("trigger_guard_faceted", outer, inner, 0.0, 0.065,
                 dark_steel, recoil, asset_collection, 0.004)
    profile_prism("trigger_curved_simple", [(-0.02, -0.055), (0.012, -0.072),
                                             (-0.020, -0.165), (-0.047, -0.15)],
                  0.0, 0.036, edge_steel, recoil, asset_collection, 0.004)

    # Runtime/editor sockets.
    empty("REV_MUZZLE_SOCKET", (0.68, 0.0, 0.155), recoil, asset_collection,
          display="ARROWS", size=0.055)
    empty("REV_RIGHT_HAND_GRIP_SOCKET", (-0.23, 0.0, -0.24), recoil,
          asset_collection, display="ARROWS", size=0.065)
    empty("REV_PICKUP_CENTER", (0.08, 0.0, 0.0), recoil, asset_collection,
          display="SPHERE", size=0.05)

    add_fire_animation(recoil, cylinder_pivot)
    add_preview(preview_collection, floor_mat)

    scene = bpy.context.scene
    # Blender 5.1 exposes the Eevee Next renderer under the legacy enum name.
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.resolution_x = 1024
    scene.render.resolution_y = 1024
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.filepath = str(PREVIEW_PATH)
    scene.render.film_transparent = False
    scene.world.color = (0.006, 0.008, 0.012)
    scene.view_settings.look = "AgX - Medium High Contrast"
    scene.render.image_settings.color_mode = "RGBA"

    ASSET_DIR.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(BLEND_PATH))

    # Export only the game asset collection, never camera/lights/floor.
    bpy.ops.object.select_all(action="DESELECT")
    export_objects = [obj for obj in asset_collection.all_objects]
    for obj in export_objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = root
    bpy.ops.export_scene.gltf(
        filepath=str(GLB_PATH),
        export_format="GLB",
        use_selection=True,
        export_animations=True,
        export_yup=True,
    )
    bpy.ops.export_scene.fbx(
        filepath=str(FBX_PATH),
        use_selection=True,
        object_types={"EMPTY", "MESH"},
        bake_anim=True,
        add_leaf_bones=False,
        axis_forward="X",
        axis_up="Z",
    )

    scene.frame_set(1)
    scene.render.filepath = str(PREVIEW_PATH)
    bpy.ops.render.render(write_still=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(BLEND_PATH))
    print(f"Created {BLEND_PATH}")
    print(f"Created {GLB_PATH}")
    print(f"Created {FBX_PATH}")
    print(f"Created {PREVIEW_PATH}")


if __name__ == "__main__":
    build()
