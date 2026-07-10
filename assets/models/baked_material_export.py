"""Shared Blender helpers for LG Duel's baked material-mesh headers."""

from __future__ import annotations

from pathlib import Path

import bpy


def linear_to_srgb(value: float) -> int:
    value = max(0.0, min(1.0, value))
    encoded = 12.92 * value if value <= 0.0031308 else 1.055 * value ** (1.0 / 2.4) - 0.055
    return round(max(0.0, min(1.0, encoded)) * 255.0)


def cpp_float(value: float) -> str:
    if abs(value) < 0.0000005:
        value = 0.0
    return f"{value:.7f}F"


def material_properties(obj: bpy.types.Object, material_index: int):
    if material_index < len(obj.material_slots):
        material = obj.material_slots[material_index].material
        if material is not None:
            principled = material.node_tree.nodes.get("Principled BSDF") if material.use_nodes else None
            color = (
                principled.inputs["Base Color"].default_value
                if principled is not None
                else material.diffuse_color
            )
            return (
                tuple(linear_to_srgb(color[channel]) for channel in range(3)) + (255,),
                float(principled.inputs["Metallic"].default_value) if principled else 0.0,
                float(principled.inputs["Roughness"].default_value) if principled else 0.5,
            )
    return ((190, 190, 190, 255), 0.0, 0.5)


def gather_mesh_triangles(objects) -> list:
    depsgraph = bpy.context.evaluated_depsgraph_get()
    triangles = []
    for source in sorted(objects, key=lambda item: item.name):
        if source.type != "MESH":
            continue
        evaluated = source.evaluated_get(depsgraph)
        mesh = evaluated.to_mesh()
        try:
            mesh.calc_loop_triangles()
            normal_matrix = evaluated.matrix_world.to_3x3().inverted().transposed()
            for triangle in mesh.loop_triangles:
                color, metallic, roughness = material_properties(source, triangle.material_index)
                vertices = []
                for vertex_index, loop_index in zip(triangle.vertices, triangle.loops):
                    point = evaluated.matrix_world @ mesh.vertices[vertex_index].co
                    normal = (normal_matrix @ mesh.corner_normals[loop_index].vector).normalized()
                    vertices.append(((point.x, point.y, point.z), (normal.x, normal.y, normal.z)))
                triangles.append((vertices, color, metallic, roughness))
        finally:
            evaluated.to_mesh_clear()
    return triangles


def emit_array(name: str, triangles: list) -> list[str]:
    lines = [
        f"inline constexpr std::array<BakedWeaponMaterialTriangle, {len(triangles)}> {name} = {{{{"
    ]
    for vertices, color, metallic, roughness in triangles:
        vertex_text = ", ".join(
            "{{" + ", ".join(cpp_float(component) for component in position) + "}, {" +
            ", ".join(cpp_float(component) for component in normal) + "}}"
            for position, normal in vertices
        )
        lines.append(
            "  {{{" + vertex_text + "}}, {" +
            f"{color[0]}, {color[1]}, {color[2]}, {color[3]}" +
            "}, " + cpp_float(metallic) + ", " + cpp_float(roughness) + "},"
        )
    lines.append("}};")
    return lines


def write_header(output: Path, source_label: str, arrays) -> None:
    lines = [
        "#pragma once",
        "",
        "#include \"render/BakedWeaponModels.hpp\"",
        "",
        "#include <array>",
        "",
        "namespace lg {",
        "",
        f"// Generated from {source_label}.",
        "// Regenerate with the matching export script; do not hand-edit.",
    ]
    for name, triangles in arrays:
        lines.extend(["", *emit_array(name, triangles)])
    lines.extend(["", "} // namespace lg", ""])
    output.write_text("\n".join(lines), encoding="utf-8")
