"""Bake the authored revolver meshes into the game's static weapon format.

Run through Blender with the revolver .blend open. The body and cylinder are
kept separate so runtime presentation can rotate the cylinder independently.
"""

from __future__ import annotations

import math
from pathlib import Path

import bpy
from mathutils import Vector


REPO_ROOT = Path(__file__).resolve().parents[3]
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


def linear_to_srgb(value: float) -> int:
    value = max(0.0, min(1.0, value))
    encoded = 12.92 * value if value <= 0.0031308 else 1.055 * value ** (1.0 / 2.4) - 0.055
    return round(max(0.0, min(1.0, encoded)) * 255.0)


def cpp_float(value: float) -> str:
    if abs(value) < 0.0000005:
        value = 0.0
    return f"{value:.7f}F"


def material_color(obj: bpy.types.Object, material_index: int) -> tuple[int, int, int, int]:
    if material_index < len(obj.material_slots):
        material = obj.material_slots[material_index].material
        if material is not None:
            color = material.diffuse_color
            return (
                linear_to_srgb(color[0]),
                linear_to_srgb(color[1]),
                linear_to_srgb(color[2]),
                255,
            )
    return (190, 190, 190, 255)


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
            target = cylinder if is_cylinder_part(source) else body
            for triangle in mesh.loop_triangles:
                color = material_color(source, triangle.material_index)
                vertices = []
                for vertex_index in triangle.vertices:
                    point = evaluated.matrix_world @ mesh.vertices[vertex_index].co
                    if target is cylinder:
                        point -= pivot_world
                    vertices.append((point.x, point.y, point.z))
                target.append((vertices, color))
        finally:
            evaluated.to_mesh_clear()
    return body, cylinder


def emit_array(name: str, triangles: list) -> list[str]:
    lines = [
        f"inline constexpr std::array<BakedWeaponModelTriangle, {len(triangles)}> {name} = {{{{"
    ]
    for vertices, color in triangles:
        vertex_text = ", ".join(
            "{" + ", ".join(cpp_float(component) for component in vertex) + "}"
            for vertex in vertices
        )
        lines.append(
            "  {{{" + vertex_text + "}}, {" +
            f"{color[0]}, {color[1]}, {color[2]}, {color[3]}" +
            "}},"
        )
    lines.append("}};")
    return lines


def main() -> None:
    body, cylinder = gather_triangles()
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
        "} // namespace lg",
        "",
    ]
    OUTPUT.write_text("\n".join(lines), encoding="utf-8")
    print(f"Wrote {OUTPUT} ({len(body)} body triangles, {len(cylinder)} cylinder triangles)")


if __name__ == "__main__":
    main()
