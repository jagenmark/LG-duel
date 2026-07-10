"""Bake the authored revolver meshes into the game's static weapon format.

Run through Blender with the revolver .blend open. The body and cylinder are
kept separate so runtime presentation can rotate the cylinder independently.
"""

from __future__ import annotations

import sys
from pathlib import Path

import bpy


REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from baked_material_export import cpp_float, emit_array, material_properties


OUTPUT = REPO_ROOT / "src/render/BakedRevolverModel.hpp"
EXPORT_COLLECTION = "REVOLVER_EXPORT"
CYLINDER_PIVOT = "REV_CYLINDER_ROTATOR_one_sixth_step"


def is_cylinder_part(obj: bpy.types.Object) -> bool:
    current = obj.parent
    while current is not None:
        if current.name == CYLINDER_PIVOT:
            return True
        current = current.parent
    return False


def gather_triangles() -> tuple[list, list]:
    scene = bpy.context.scene
    scene.frame_set(1)
    depsgraph = bpy.context.evaluated_depsgraph_get()
    export_collection = bpy.data.collections[EXPORT_COLLECTION]
    pivot = bpy.data.objects[CYLINDER_PIVOT]
    pivot_world = pivot.matrix_world.translation.copy()
    body = []
    cylinder = []
    for source in sorted(export_collection.all_objects, key=lambda item: item.name):
        if source.type != "MESH":
            continue
        evaluated = source.evaluated_get(depsgraph)
        mesh = evaluated.to_mesh()
        try:
            mesh.calc_loop_triangles()
            normal_matrix = evaluated.matrix_world.to_3x3().inverted().transposed()
            target = cylinder if is_cylinder_part(source) else body
            for triangle in mesh.loop_triangles:
                color, metallic, roughness = material_properties(source, triangle.material_index)
                vertices = []
                for vertex_index, loop_index in zip(triangle.vertices, triangle.loops):
                    point = evaluated.matrix_world @ mesh.vertices[vertex_index].co
                    if target is cylinder:
                        point -= pivot_world
                    normal = (normal_matrix @ mesh.corner_normals[loop_index].vector).normalized()
                    vertices.append(((point.x, point.y, point.z), (normal.x, normal.y, normal.z)))
                target.append((vertices, color, metallic, roughness))
        finally:
            evaluated.to_mesh_clear()
    return body, cylinder


def main() -> None:
    body, cylinder = gather_triangles()
    muzzle = bpy.data.objects["REV_MUZZLE_SOCKET"].matrix_world.translation
    lines = [
        "#pragma once",
        "",
        "#include \"render/BakedWeaponModels.hpp\"",
        "",
        "#include <array>",
        "",
        "namespace lg {",
        "",
        "// Generated from assets/models/revolver/lg_duel_revolver_stainless.blend.",
        "// Regenerate with export_revolver_header.py; do not hand-edit.",
        *emit_array("kRevolverBodyModel", body),
        "",
        *emit_array("kRevolverCylinderModel", cylinder),
        "",
        "inline constexpr Vec3 kRevolverMuzzleSocket = {" +
        ", ".join(cpp_float(component) for component in muzzle) +
        "};",
        "",
        "} // namespace lg",
        "",
    ]
    OUTPUT.write_text("\n".join(lines), encoding="utf-8")
    print(f"Wrote {OUTPUT} ({len(body)} body triangles, {len(cylinder)} cylinder triangles)")


if __name__ == "__main__":
    main()
