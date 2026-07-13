"""Bake the plasma-gun GLB into material meshes and socket metadata."""
import sys
from pathlib import Path

import bpy


ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from baked_material_export import gather_mesh_triangles, write_header


DIR = Path(__file__).resolve().parent
CORE_PIVOT = (0.31, 0.0, 0.13)
PRONG_PIVOT = (0.16, 0.0, 0.13)


def point(name):
    obj = bpy.data.objects[name]
    authored = obj.get("lg_weapon_space_position")
    return tuple(authored) if authored is not None else tuple(obj.matrix_world.translation)


def declaration(name, value):
    components = ", ".join(f"{float(component):.7f}F" for component in value)
    return f"inline constexpr Vec3 {name} = {{{components}}};\n"


def translated_to_local(triangles, pivot):
    result = []
    for vertices, color, metallic, roughness in triangles:
        local_vertices = [
            (
                (
                    position[0] - pivot[0],
                    position[1] - pivot[1],
                    position[2] - pivot[2],
                ),
                normal,
            )
            for position, normal in vertices
        ]
        result.append((local_vertices, color, metallic, roughness))
    return result


def main():
    bpy.ops.wm.open_mainfile(filepath=str(DIR / "lg_duel_plasma_gun.blend"))
    metadata = {
        "kPlasmaGunMuzzleSocket": point("PG_MUZZLE_SOCKET"),
        "kPlasmaGunGripSocket": point("PG_RIGHT_HAND_GRIP_SOCKET"),
        "kPlasmaGunSupportGripSocket": point("PG_SUPPORT_HAND_GRIP_SOCKET"),
        "kPlasmaGunPickupCenter": point("PG_PICKUP_CENTER"),
        "kPlasmaGunCorePivot": CORE_PIVOT,
        "kPlasmaGunProngPivot": PRONG_PIVOT,
    }

    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=str(DIR / "lg_duel_plasma_gun.glb"))
    bpy.context.scene.frame_set(1)
    meshes = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]
    core = [obj for obj in meshes if obj.name.startswith("PG_CORE")]
    prongs = [obj for obj in meshes if obj.name.startswith("PG_PRONG_")]
    body = [obj for obj in meshes if obj not in core and obj not in prongs]

    write_header(
        ROOT / "src/render/BakedPlasmaGunModel.hpp",
        "assets/models/plasma_gun/lg_duel_plasma_gun.glb",
        [
            ("kPlasmaGunBodyMaterialModel", gather_mesh_triangles(body)),
            (
                "kPlasmaGunProngMaterialModel",
                translated_to_local(gather_mesh_triangles(prongs), PRONG_PIVOT),
            ),
            (
                "kPlasmaGunCoreMaterialModel",
                translated_to_local(gather_mesh_triangles(core), CORE_PIVOT),
            ),
        ],
    )
    output = ROOT / "src/render/BakedPlasmaGunModel.hpp"
    text = output.read_text()
    declarations = "\n".join(declaration(name, value) for name, value in metadata.items())
    output.write_text(text.replace("} // namespace lg\n", declarations + "\n} // namespace lg\n"))


if __name__ == "__main__":
    main()
