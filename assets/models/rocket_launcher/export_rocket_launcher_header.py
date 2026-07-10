"""Bake the cooked rocket-launcher GLB into material meshes and metadata."""

from __future__ import annotations

import sys
from pathlib import Path

import bpy


REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from baked_material_export import gather_mesh_triangles, write_header


SOURCE = Path(__file__).with_name("lg_duel_rocket_launcher.glb")
AUTHORING_SOURCE = Path(__file__).with_name("lg_duel_rocket_launcher.blend")
OUTPUT = REPO_ROOT / "src/render/BakedRocketLauncherModel.hpp"
RIG_NAME = "RL_RIG"
ROOT_BONE = "RL_ROOT"
RECOIL_BONE = "RL_RECOIL_BLOCK"
LATCH_BONE = "RL_TOP_LATCH"


def bone_meshes(bone_name: str) -> list[bpy.types.Object]:
    return [
        obj
        for obj in bpy.context.scene.objects
        if obj.type == "MESH"
        and obj.parent is not None
        and obj.parent.name == RIG_NAME
        and obj.parent_type == "BONE"
        and obj.parent_bone == bone_name
    ]


def translated_to_local(triangles: list, pivot) -> list:
    result = []
    for vertices, color, metallic, roughness in triangles:
        converted = []
        for position, normal in vertices:
            converted.append((
                (
                    position[0] - pivot.x,
                    position[1] - pivot.y,
                    position[2] - pivot.z,
                ),
                normal,
            ))
        result.append((converted, color, metallic, roughness))
    return result


def world_point(name: str):
    obj = bpy.data.objects[name]
    authored = obj.get("lg_weapon_space_position")
    if authored is not None:
        return tuple(float(component) for component in authored)
    return obj.matrix_world.translation.copy()


def declaration(name: str, value) -> str:
    return (
        f"inline constexpr Vec3 {name} = {{" +
        ", ".join(f"{component:.7f}F" for component in value) +
        "};\n"
    )


def main() -> None:
    # The cooked GLB retains mesh/bone classification, but Blender empties that
    # are bone-parented import at the bone origin. Read socket positions from
    # the editable source before switching to the cooked geometry.
    bpy.ops.wm.open_mainfile(filepath=str(AUTHORING_SOURCE))
    authoring_rig = bpy.data.objects[RIG_NAME]
    bpy.context.scene.frame_set(1)
    metadata_points = {
        "recoil": authoring_rig.matrix_world @ authoring_rig.pose.bones[RECOIL_BONE].head,
        "latch": authoring_rig.matrix_world @ authoring_rig.pose.bones[LATCH_BONE].head,
        "muzzle": world_point("RL_MUZZLE_SOCKET"),
        "grip": world_point("RL_RIGHT_HAND_GRIP_SOCKET"),
        "support": world_point("RL_SUPPORT_HAND_GRIP_SOCKET"),
        "pickup": world_point("RL_PICKUP_CENTER"),
    }
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=str(SOURCE))
    rig = bpy.data.objects[RIG_NAME]
    bpy.context.scene.frame_set(1)

    recoil_pivot = metadata_points["recoil"]
    latch_pivot = metadata_points["latch"]
    body = gather_mesh_triangles(bone_meshes(ROOT_BONE))
    recoil = translated_to_local(
        gather_mesh_triangles(bone_meshes(RECOIL_BONE)),
        recoil_pivot,
    )
    latch = translated_to_local(
        gather_mesh_triangles(bone_meshes(LATCH_BONE)),
        latch_pivot,
    )
    write_header(
        OUTPUT,
        "assets/models/rocket_launcher/lg_duel_rocket_launcher.glb",
        [
            ("kRocketLauncherBodyMaterialModel", body),
            ("kRocketLauncherRecoilMaterialModel", recoil),
            ("kRocketLauncherLatchMaterialModel", latch),
        ],
    )

    metadata = "\n".join((
        declaration("kRocketLauncherRecoilPivot", recoil_pivot),
        declaration("kRocketLauncherLatchPivot", latch_pivot),
        declaration("kRocketLauncherMuzzleSocket", metadata_points["muzzle"]),
        declaration("kRocketLauncherGripSocket", metadata_points["grip"]),
        declaration("kRocketLauncherSupportGripSocket", metadata_points["support"]),
        declaration("kRocketLauncherPickupCenter", metadata_points["pickup"]),
    ))
    text = OUTPUT.read_text(encoding="utf-8")
    OUTPUT.write_text(
        text.replace("} // namespace lg\n", metadata + "\n} // namespace lg\n"),
        encoding="utf-8",
    )
    print(
        f"Wrote {OUTPUT} ({len(body)} body, {len(recoil)} recoil, "
        f"{len(latch)} latch triangles)"
    )


if __name__ == "__main__":
    main()
