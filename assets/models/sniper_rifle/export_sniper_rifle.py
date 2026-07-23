"""Build the LG Duel sniper GLB and bake its rest pose for the renderer."""

import sys
from pathlib import Path

import bpy
from mathutils import Matrix


ROOT = Path(__file__).resolve().parents[3]
DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(DIR.parent))
from baked_material_export import gather_mesh_triangles, write_header


# Values use the reviewed, normalized Quaternius model space.
SOURCE_GRIP = (0.0330, -2.1750, 0.3600)
SOURCE_MUZZLE = (0.0330, -2.1050, -0.3970)
MATERIALS = {
    "Black": ((0.025, 0.030, 0.035, 1.0), 0.05, 0.72),
    "Metal": ((0.30, 0.34, 0.37, 1.0), 0.78, 0.32),
    "BulletYellow": ((0.78, 0.48, 0.08, 1.0), 0.62, 0.28),
    "BulletOrange": ((0.58, 0.20, 0.035, 1.0), 0.48, 0.34),
    "Trigger": ((0.16, 0.18, 0.19, 1.0), 0.66, 0.38),
    "Barrel": ((0.20, 0.23, 0.25, 1.0), 0.82, 0.30),
    "Green": ((0.075, 0.18, 0.075, 1.0), 0.02, 0.68),
    "Magazine": ((0.075, 0.085, 0.09, 1.0), 0.30, 0.52),
}


def weapon_point(source):
    """Map Quaternius -Z forward/Y up space to LG Duel +X forward/Z up."""
    return (
        SOURCE_GRIP[2] - source[2],
        SOURCE_GRIP[0] - source[0],
        source[1] - SOURCE_GRIP[1],
    )


def set_materials():
    for material in bpy.data.materials:
        values = MATERIALS.get(material.name)
        if values is None:
            continue
        color, metallic, roughness = values
        material.diffuse_color = color
        material.use_nodes = True
        node = material.node_tree.nodes.get("Principled BSDF")
        node.inputs["Base Color"].default_value = color
        node.inputs["Metallic"].default_value = metallic
        node.inputs["Roughness"].default_value = roughness


def prepare_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(
        filepath=str(DIR / "SniperRifle.fbx"),
        axis_forward="-Z",
        axis_up="Y",
    )
    roots = [obj for obj in bpy.context.scene.objects if obj.parent is None]
    for obj in roots:
        obj.scale = tuple(component * 0.1 for component in obj.scale)
        bpy.ops.object.select_all(action="DESELECT")
        obj.select_set(True)
        bpy.context.view_layer.objects.active = obj
        bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)

    for obj in bpy.context.scene.objects:
        if obj.type != "MESH":
            continue
        bpy.ops.object.select_all(action="DESELECT")
        obj.select_set(True)
        bpy.context.view_layer.objects.active = obj
        bpy.ops.object.mode_set(mode="EDIT")
        bpy.ops.mesh.select_all(action="SELECT")
        bpy.ops.mesh.quads_convert_to_tris(quad_method="FIXED", ngon_method="CLIP")
        bpy.ops.object.mode_set(mode="OBJECT")
        obj.data.validate(clean_customdata=False)
        obj.data.update(calc_edges=True)

    # Keep the source handedness while moving the grip to the weapon origin.
    transform = Matrix((
        (0.0, 0.0, -1.0, SOURCE_GRIP[2]),
        (-1.0, 0.0, 0.0, SOURCE_GRIP[0]),
        (0.0, 1.0, 0.0, -SOURCE_GRIP[1]),
        (0.0, 0.0, 0.0, 1.0),
    ))
    axis_root = bpy.data.objects.new("LG_SniperAxis", None)
    bpy.context.collection.objects.link(axis_root)
    axis_root.matrix_world = transform
    for obj in roots:
        # The armature actions key the armature object itself. A plain parent
        # keeps the LG Duel axis change in place while Blender samples clips.
        obj.parent = axis_root
    set_materials()


def declaration(name, value):
    components = ", ".join(f"{component:.7f}F" for component in value)
    return f"inline constexpr Vec3 {name} = {{{components}}};\n"


def main():
    prepare_scene()
    runtime_glb = DIR / "lg_duel_sniper_rifle.glb"
    bpy.ops.export_scene.gltf(
        filepath=str(runtime_glb),
        export_format="GLB",
        export_animations=True,
        export_skins=True,
    )

    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=str(runtime_glb))
    bpy.context.scene.frame_set(1)
    for obj in bpy.context.scene.objects:
        if obj.animation_data is not None:
            obj.animation_data_clear()
    # Blender may create helper meshes while it loads the rig. Bake only the
    # authored rifle so preview helpers can never enter the game mesh.
    meshes = [
        obj for obj in bpy.context.scene.objects
        if obj.type == "MESH" and obj.name.startswith("Rifle")
    ]
    if len(meshes) != 1:
        raise RuntimeError(f"expected one rifle mesh, found {[obj.name for obj in meshes]}")
    write_header(
        ROOT / "src/render/BakedSniperRifleModel.hpp",
        "assets/models/sniper_rifle/lg_duel_sniper_rifle.glb",
        [("kSniperRifleMaterialModel", gather_mesh_triangles(meshes))],
    )
    output = ROOT / "src/render/BakedSniperRifleModel.hpp"
    text = output.read_text(encoding="utf-8")
    declarations = "\n".join((
        declaration("kSniperRifleGripSocket", (0.0, 0.0, 0.0)),
        declaration("kSniperRifleMuzzleSocket", weapon_point(SOURCE_MUZZLE)),
    ))
    output.write_text(
        text.replace("} // namespace lg\n", declarations + "\n} // namespace lg\n"),
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
