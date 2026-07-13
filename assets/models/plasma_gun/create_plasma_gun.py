"""Create the LG Duel contained-core plasma gun, exports, and preview.

Run from the repository root with Blender 5.x. Weapon space is +X forward and
+Z up. The short containment pulse is an art reference; runtime presentation
responds to replicated fire events without changing authoritative projectiles.
"""
from pathlib import Path
import math

import bpy
from mathutils import Vector


DIR = Path(__file__).resolve().parent
CORE_PIVOT = (0.31, 0.0, 0.13)
PRONG_PIVOT = (0.16, 0.0, 0.13)


def material(name, color, metallic=0.0, roughness=0.45, emission=None):
    result = bpy.data.materials.new(name)
    result.use_nodes = True
    principled = result.node_tree.nodes.get("Principled BSDF")
    principled.inputs["Base Color"].default_value = (*color, 1.0)
    principled.inputs["Metallic"].default_value = metallic
    principled.inputs["Roughness"].default_value = roughness
    if emission is not None:
        principled.inputs["Emission Color"].default_value = (*emission, 1.0)
        principled.inputs["Emission Strength"].default_value = 3.2
    return result


def finish(obj, mat, collection, bevel=0.008):
    obj.data.materials.append(mat)
    if bevel > 0.0:
        modifier = obj.modifiers.new("readable bevel", "BEVEL")
        modifier.width = bevel
        modifier.segments = 2
        modifier.limit_method = "ANGLE"
        bpy.context.view_layer.objects.active = obj
        bpy.ops.object.modifier_apply(modifier=modifier.name)
    for owner in list(obj.users_collection):
        owner.objects.unlink(obj)
    collection.objects.link(obj)
    return obj


def box(name, location, size, mat, collection, rotation=(0.0, 0.0, 0.0), bevel=0.008):
    bpy.ops.mesh.primitive_cube_add(location=location, rotation=rotation)
    obj = bpy.context.object
    obj.name = name
    obj.scale = tuple(component * 0.5 for component in size)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    return finish(obj, mat, collection, bevel)


def cylinder(name, location, radius, depth, mat, collection, vertices=16, rotation=(0.0, math.pi / 2.0, 0.0), bevel=0.005):
    bpy.ops.mesh.primitive_cylinder_add(
        vertices=vertices,
        radius=radius,
        depth=depth,
        location=location,
        rotation=rotation,
    )
    obj = bpy.context.object
    obj.name = name
    return finish(obj, mat, collection, bevel)


def empty(name, location, collection):
    obj = bpy.data.objects.new(name, None)
    obj.location = location
    obj["lg_weapon_space_position"] = list(location)
    obj.empty_display_type = "ARROWS"
    obj.empty_display_size = 0.06
    collection.objects.link(obj)
    return obj


def look_at(obj, target):
    obj.rotation_euler = (Vector(target) - obj.location).to_track_quat("-Z", "Y").to_euler()


def prong(name, location, size, rotation, dark, ceramic, cyan, collection):
    box(f"PG_PRONG_{name}_FRAME", location, size, dark, collection, rotation, 0.018)
    insert_location = (
        location[0] + 0.035,
        location[1] * 0.86,
        CORE_PIVOT[2] + (location[2] - CORE_PIVOT[2]) * 0.86,
    )
    box(
        f"PG_PRONG_{name}_CERAMIC",
        insert_location,
        (size[0] * 0.67, size[1] * 0.70, size[2] * 0.72),
        ceramic,
        collection,
        rotation,
        0.012,
    )
    tip = (0.79, location[1] * 0.58, CORE_PIVOT[2] + (location[2] - CORE_PIVOT[2]) * 0.58)
    cylinder(
        f"PG_PRONG_{name}_EMITTER",
        tip,
        0.042,
        0.10,
        cyan,
        collection,
        10,
        bevel=0.003,
    )


def build():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)

    export = bpy.data.collections.new("PLASMA_GUN_EXPORT")
    bpy.context.scene.collection.children.link(export)
    preview = bpy.data.collections.new("PREVIEW_NOT_FOR_EXPORT")
    bpy.context.scene.collection.children.link(preview)

    dark = material("PG gunmetal", (0.025, 0.045, 0.075), 0.86, 0.29)
    ceramic = material("PG ceramic armor", (0.69, 0.73, 0.76), 0.15, 0.40)
    black = material("PG insulated grip", (0.010, 0.016, 0.024), 0.04, 0.75)
    orange = material("PG safety orange", (0.95, 0.25, 0.035), 0.28, 0.36)
    cyan = material("PG containment cyan", (0.015, 0.48, 0.64), 0.20, 0.20, (0.02, 0.85, 1.0))
    core_hot = material("PG plasma core", (0.04, 0.70, 0.92), 0.04, 0.14, (0.10, 0.95, 1.0))

    # Compact receiver and grip keep the open core as the weapon's primary read.
    box("PG_REAR_HOUSING", (-0.38, 0.0, 0.10), (0.48, 0.36, 0.38), dark, export, bevel=0.025)
    box("PG_REAR_CAP", (-0.64, 0.0, 0.10), (0.12, 0.39, 0.41), ceramic, export, bevel=0.024)
    box("PG_TOP_ARMOR", (-0.31, 0.0, 0.30), (0.48, 0.30, 0.12), ceramic, export, bevel=0.018)
    box("PG_PRIMARY_GRIP", (-0.41, 0.0, -0.27), (0.18, 0.19, 0.50), black, export, (0.0, math.radians(-11.0), 0.0), 0.025)
    box("PG_TRIGGER_GUARD", (-0.20, 0.0, -0.10), (0.27, 0.12, 0.13), dark, export, bevel=0.020)
    box("PG_TRIGGER", (-0.23, -0.003, -0.15), (0.035, 0.07, 0.13), orange, export, (0.0, math.radians(-13.0), 0.0), 0.005)
    box("PG_LOWER_POWER_HOUSING", (-0.03, 0.0, -0.09), (0.46, 0.24, 0.17), dark, export, bevel=0.018)
    box("PG_SUPPORT_PAD", (0.04, 0.0, -0.19), (0.23, 0.27, 0.10), black, export, bevel=0.016)

    # Broad collars establish a believable containment mechanism around the orb.
    cylinder("PG_CORE_REAR_COLLAR", (0.04, 0.0, CORE_PIVOT[2]), 0.19, 0.12, dark, export, 16)
    cylinder("PG_CORE_CYAN_RING", (0.11, 0.0, CORE_PIVOT[2]), 0.145, 0.055, cyan, export, 14)
    for y in (-0.16, 0.16):
        box("PG_SIDE_BRACE", (0.03, y, 0.13), (0.34, 0.065, 0.16), ceramic, export, bevel=0.012)

    # Three prongs form a spatial cage: one upper and two lower-side emitters.
    prong("TOP", (0.47, 0.0, 0.37), (0.66, 0.16, 0.13), (0.0, math.radians(-8.0), 0.0), dark, ceramic, cyan, export)
    prong("LOWER_LEFT", (0.47, -0.20, -0.01), (0.66, 0.13, 0.15), (0.0, math.radians(8.0), math.radians(-4.0)), dark, ceramic, cyan, export)
    prong("LOWER_RIGHT", (0.47, 0.20, -0.01), (0.66, 0.13, 0.15), (0.0, math.radians(8.0), math.radians(4.0)), dark, ceramic, cyan, export)
    cylinder("PG_MUZZLE_CONVERGENCE_RING", (0.86, 0.0, 0.13), 0.095, 0.065, dark, export, 12)

    # A low-poly icosphere makes the energy source readable without transparency.
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=2, radius=0.145, location=CORE_PIVOT)
    core = finish(bpy.context.object, core_hot, export, 0.0)
    core.name = "PG_CORE"
    core["lg_plasma_part"] = "core"
    for scale, material_ref in ((1.13, cyan), (0.72, core_hot)):
        bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1, radius=0.145 * scale, location=CORE_PIVOT)
        shell = finish(bpy.context.object, material_ref, export, 0.0)
        shell.name = "PG_CORE_FACET"
        shell["lg_plasma_part"] = "core"

    for obj in export.objects:
        if obj.type == "MESH" and obj.name.startswith("PG_PRONG_"):
            obj["lg_plasma_part"] = "prongs"

    for name, location in (
        ("PG_MUZZLE_SOCKET", (0.93, 0.0, 0.13)),
        ("PG_RIGHT_HAND_GRIP_SOCKET", (-0.42, 0.0, -0.28)),
        ("PG_SUPPORT_HAND_GRIP_SOCKET", (0.04, 0.0, -0.18)),
        ("PG_PICKUP_CENTER", (0.02, 0.0, 0.05)),
    ):
        empty(name, location, export)

    # Reference pulse for checking pivots in Blender; runtime owns the response.
    for frame, scale in ((1, 1.0), (4, 0.78), (8, 1.10), (14, 1.0)):
        core.scale = (scale, scale, scale)
        core.keyframe_insert("scale", frame=frame)
    bpy.context.scene.frame_start = 1
    bpy.context.scene.frame_end = 14
    bpy.context.scene.render.fps = 60
    bpy.context.scene.frame_set(1)

    box("preview_floor", (0.0, 0.0, -0.57), (4.0, 4.0, 0.05), material("preview floor", (0.008, 0.012, 0.018), 0.0, 0.68), preview, bevel=0.0)
    bpy.ops.object.camera_add(location=(2.25, -4.3, 1.55))
    camera = bpy.context.object
    camera.data.lens = 66
    look_at(camera, (0.02, 0.0, 0.02))
    preview.objects.link(camera)
    bpy.context.collection.objects.unlink(camera)
    bpy.context.scene.camera = camera
    for name, location, energy, color, size in (
        ("key", (0.3, -2.4, 2.5), 720, (0.72, 0.90, 1.0), 1.5),
        ("cyan rim", (0.7, 1.7, 1.5), 520, (0.05, 0.60, 1.0), 1.0),
        ("warm fill", (-1.6, -0.2, 0.8), 260, (1.0, 0.62, 0.35), 1.2),
    ):
        data = bpy.data.lights.new(name, "AREA")
        data.energy = energy
        data.color = color
        data.shape = "DISK"
        data.size = size
        lamp = bpy.data.objects.new(name, data)
        lamp.location = location
        look_at(lamp, (0.05, 0.0, 0.05))
        preview.objects.link(lamp)

    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.resolution_x = 1536
    scene.render.resolution_y = 1024
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.filepath = str(DIR / "lg_duel_plasma_gun_preview.png")
    scene.world.color = (0.003, 0.005, 0.008)
    scene.view_settings.look = "AgX - Medium High Contrast"

    bpy.ops.wm.save_as_mainfile(filepath=str(DIR / "lg_duel_plasma_gun.blend"))
    bpy.ops.object.select_all(action="DESELECT")
    for obj in export.all_objects:
        obj.select_set(True)
    bpy.ops.export_scene.gltf(
        filepath=str(DIR / "lg_duel_plasma_gun.glb"),
        export_format="GLB",
        use_selection=True,
        export_animations=True,
        export_yup=True,
    )
    bpy.ops.export_scene.fbx(
        filepath=str(DIR / "lg_duel_plasma_gun.fbx"),
        use_selection=True,
        bake_anim=True,
        axis_forward="X",
        axis_up="Z",
    )
    bpy.ops.render.render(write_still=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(DIR / "lg_duel_plasma_gun.blend"))


if __name__ == "__main__":
    build()
