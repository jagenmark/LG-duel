"""Bake the cooked machine-gun GLB into LG Duel's material mesh format."""

from __future__ import annotations

import sys
from pathlib import Path

import bpy


REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from baked_material_export import gather_mesh_triangles, write_header


SOURCE = Path(__file__).with_name("lg_duel_machine_gun_minigun_steel.glb")
OUTPUT = REPO_ROOT / "src/render/BakedMachineGunModel.hpp"
LEGACY_SCALE = 1.0 / 0.8975


def legacy_game_basis(triangles: list) -> list:
    """Match the existing baked MG grip origin, scale, and X-forward basis."""
    transformed = []
    for vertices, color, metallic, roughness in triangles:
        converted = []
        for position, normal in vertices:
            converted.append((
                (
                    (position[0] + 0.12) * LEGACY_SCALE,
                    (position[2] - 0.035) * LEGACY_SCALE,
                    -(position[1] - 0.051) * LEGACY_SCALE,
                ),
                (normal[0], normal[2], -normal[1]),
            ))
        transformed.append((converted, color, metallic, roughness))
    return transformed


def legacy_game_point(position) -> tuple[float, float, float]:
    return (
        (position[0] + 0.12) * LEGACY_SCALE * 0.78,
        (position[2] - 0.035) * LEGACY_SCALE * 0.78,
        -(position[1] - 0.051) * LEGACY_SCALE * 0.78,
    )


def main() -> None:
    # The GLB is the existing low-poly cooked mesh. Importing it here avoids
    # baking Blender preview modifiers that quadruple the runtime triangle count.
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=str(SOURCE))
    spin_axis = bpy.data.objects["MG_BARREL_SPIN_AXIS"]
    rotating_objects = set(spin_axis.children_recursive)
    barrel_objects = [obj for obj in rotating_objects if obj.type == "MESH"]
    body_objects = [
        obj for obj in bpy.context.scene.objects
        if obj.type == "MESH" and obj not in rotating_objects
    ]
    body_triangles = legacy_game_basis(gather_mesh_triangles(body_objects))
    barrel_triangles = legacy_game_basis(gather_mesh_triangles(barrel_objects))
    write_header(
        OUTPUT,
        "assets/models/machine_gun/lg_duel_machine_gun_minigun_steel.glb",
        [
            ("kMachineGunBodyMaterialModel", body_triangles),
            ("kMachineGunBarrelMaterialModel", barrel_triangles),
        ],
    )
    pivot = legacy_game_point(spin_axis.matrix_world.translation)
    muzzle = legacy_game_point(
        bpy.data.objects["MG_MUZZLE_SOCKET"].matrix_world.translation
    )
    casing_eject = legacy_game_point(
        bpy.data.objects["MG_CASING_EJECT_SOCKET"].matrix_world.translation
    )
    text = OUTPUT.read_text(encoding="utf-8")
    pivot_declaration = (
        "inline constexpr Vec3 kMachineGunBarrelPivot = {" +
        ", ".join(f"{component:.7f}F" for component in pivot) +
        "};\n"
        "inline constexpr Vec3 kMachineGunMuzzleSocket = {" +
        ", ".join(f"{component:.7f}F" for component in muzzle) +
        "};\n"
        "inline constexpr Vec3 kMachineGunCasingEjectSocket = {" +
        ", ".join(f"{component:.7f}F" for component in casing_eject) +
        "};\n\n"
    )
    OUTPUT.write_text(
        text.replace("} // namespace lg\n", pivot_declaration + "} // namespace lg\n"),
        encoding="utf-8",
    )
    print(
        f"Wrote {OUTPUT} ({len(body_triangles)} body triangles, "
        f"{len(barrel_triangles)} barrel triangles)"
    )


if __name__ == "__main__":
    main()
