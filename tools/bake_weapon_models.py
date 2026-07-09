"""Regenerate src/render/BakedWeaponModels.hpp from the weapon .blend sources.

Run with Blender, from the repository root:
  blender --background --factory-startup --python tools/bake_weapon_models.py
"""

from pathlib import Path

import bpy


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "src" / "render" / "BakedWeaponModels.hpp"
MODELS = (
    (
        "Shotgun",
        ROOT / "assets" / "models" / "shotgun" / "lg_duel_shotgun_sg.blend",
    ),
    (
        "MachineGun",
        ROOT
        / "assets"
        / "models"
        / "machine_gun"
        / "lg_duel_machine_gun_minigun_steel.blend",
    ),
)


def game_meshes():
    return [
        obj
        for obj in bpy.data.objects
        if obj.type == "MESH"
        and "preview_" not in obj.name
        and "marker" not in obj.name
    ]


def material_color(obj, material_index):
    material = obj.material_slots[material_index].material
    principled = material.node_tree.nodes.get("Principled BSDF")
    color = principled.inputs["Base Color"].default_value
    return tuple(max(0, min(255, round(channel * 255.0))) for channel in color[:3])


def bake_model(symbol, source):
    bpy.ops.wm.open_mainfile(filepath=str(source))
    meshes = game_meshes()
    world_points = [obj.matrix_world @ vertex.co for obj in meshes for vertex in obj.data.vertices]
    minimum_x = min(point.x for point in world_points)
    maximum_x = max(point.x for point in world_points)
    center_y = (min(point.y for point in world_points) + max(point.y for point in world_points)) * 0.5
    center_z = (min(point.z for point in world_points) + max(point.z for point in world_points)) * 0.5
    model_length = maximum_x - minimum_x

    triangles = []
    for obj in meshes:
        obj.data.calc_loop_triangles()
        for triangle in obj.data.loop_triangles:
            points = []
            for vertex_index in triangle.vertices:
                point = obj.matrix_world @ obj.data.vertices[vertex_index].co
                points.append(
                    (
                        (point.x - minimum_x) / model_length,
                        (point.z - center_z) / model_length,
                        -(point.y - center_y) / model_length,
                    )
                )
            triangles.append((points, material_color(obj, triangle.material_index)))

    lines = [
        f"inline constexpr std::array<BakedWeaponModelTriangle, {len(triangles)}> "
        f"k{symbol}WeaponModel = {{{{"
    ]
    for points, color in triangles:
        formatted_points = ", ".join(
            "{" + ", ".join(f"{coordinate:.6f}F" for coordinate in point) + "}"
            for point in points
        )
        lines.append(
            f"  {{{{{{{formatted_points}}}}}, "
            f"{{{color[0]}, {color[1]}, {color[2]}, 255}}}},"
        )
    lines.append("}};")
    return "\n".join(lines)


header = """#pragma once

#include "render/Renderer.hpp"

#include <array>
#include <cstddef>

namespace lg {

struct BakedWeaponModelTriangle {
  std::array<Vec3, 3> vertices;
  RenderColor color;
};

"""
body = "\n\n".join(bake_model(symbol, source) for symbol, source in MODELS)
OUTPUT.write_text(header + body + "\n\n} // namespace lg\n", encoding="utf-8")
print(f"Wrote {OUTPUT}")
