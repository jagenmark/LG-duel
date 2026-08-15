"""Create a Blender review file with real Worker lean actions.

The source repository only contains the Worker GLB. This script keeps the
imported rig and meshes, then builds two named actions from the existing strafe
poses. The target actions keep the full strafe cycle and tilt the upper body as
one section from the lower back in the direction of the moving legs.
"""

from __future__ import annotations

import copy
import hashlib
import json
import math
import re
import struct
from pathlib import Path

import bpy
from mathutils import Matrix, Quaternion, Vector


ROOT = Path(__file__).resolve().parents[1]
SOURCE_GLB = ROOT / "assets/models/quaternius_worker/quaternius_worker.glb"
OUTPUT_BLEND = ROOT / "assets/models/quaternius_worker/review/worker_lean_review.blend"
OUTPUT_GLB = ROOT / "assets/models/quaternius_worker/review/quaternius_worker_with_lean.glb"
MATERIAL_MANIFEST = ROOT / "assets/models/quaternius_worker/material-manifest.json"

LEAN_ROOT_BONE = "Abdomen"
SPINE_LEAN_ANGLE = math.radians(34.0)
# Rotate the whole upper body at the lower back in armature space. Descendant
# bones keep the chest and shoulder turns from the source strafe action.
SPINE_LEAN_AXIS = Vector((0.0, 1.0, 0.0))


def import_worker() -> tuple[bpy.types.Object, bpy.types.Scene]:
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    bpy.ops.import_scene.gltf(filepath=str(SOURCE_GLB))
    armature = bpy.data.objects["CharacterArmature"]
    scene = bpy.context.scene
    for bone in armature.pose.bones:
        bone.rotation_mode = "QUATERNION"
    return armature, scene


def sample_pose(
    armature: bpy.types.Object,
    scene: bpy.types.Scene,
    action: bpy.types.Action,
    frame: float,
) -> dict[str, tuple[Vector, object, Vector]]:
    armature.animation_data_create()
    armature.animation_data.action = action
    whole_frame = math.floor(frame)
    scene.frame_set(whole_frame, subframe=frame - whole_frame)
    bpy.context.view_layer.update()
    return {
        bone.name: (
            bone.location.copy(),
            bone.rotation_quaternion.copy(),
            bone.scale.copy(),
        )
        for bone in armature.pose.bones
    }


def make_lean_action(
    armature: bpy.types.Object,
    scene: bpy.types.Scene,
    base_action: bpy.types.Action,
    name: str,
    direction: float,
) -> bpy.types.Action:
    old_action = bpy.data.actions.get(name)
    if old_action is not None:
        bpy.data.actions.remove(old_action)

    action = bpy.data.actions.new(name)
    action.use_fake_user = True
    armature.animation_data_create()
    armature.animation_data.action = action

    source_start = base_action.frame_range[0]
    source_end = base_action.frame_range[1]
    target_start = 1.0
    target_end = 20.0
    frame_count = int(target_end - target_start)
    for index in range(frame_count + 1):
        target_frame = target_start + index
        blend = index / frame_count
        source_frame = source_start + (source_end - source_start) * blend
        base_pose = sample_pose(armature, scene, base_action, source_frame)
        armature.animation_data.action = action
        scene.frame_set(int(target_frame))
        for bone_name, (location, rotation, scale) in base_pose.items():
            bone = armature.pose.bones[bone_name]
            bone.location = location
            bone.rotation_mode = "QUATERNION"
            bone.rotation_quaternion = rotation
            bone.scale = scale

        bpy.context.view_layer.update()
        bone = armature.pose.bones[LEAN_ROOT_BONE]
        pivot = bone.head.copy()
        side_rotation = Quaternion(
            SPINE_LEAN_AXIS,
            direction * SPINE_LEAN_ANGLE,
        ).to_matrix().to_4x4()
        bone.matrix = (
            Matrix.Translation(pivot)
            @ side_rotation
            @ Matrix.Translation(-pivot)
            @ bone.matrix
        )
        bpy.context.view_layer.update()

        for bone in armature.pose.bones:
            bone.keyframe_insert(data_path="location", frame=target_frame)
            bone.keyframe_insert(data_path="rotation_quaternion", frame=target_frame)
            bone.keyframe_insert(data_path="scale", frame=target_frame)

    action.frame_start = target_start
    action.frame_end = target_end
    action.use_frame_range = True
    action.use_cyclic = True
    action.pose_markers.new("Lean pose")
    return action


def set_hidden(obj: bpy.types.Object, hidden: bool) -> None:
    obj.hide_viewport = hidden
    obj.hide_render = hidden


def point_camera(camera: bpy.types.Object, target: Vector) -> None:
    direction = target - camera.location
    camera.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()


def set_review_scene(
    armature: bpy.types.Object,
    scene: bpy.types.Scene,
    lean_left: bpy.types.Action,
) -> None:
    for material in bpy.data.materials:
        color = material.diffuse_color
        material.diffuse_color = (color[0], color[1], color[2], 1.0)
        if not material.use_nodes:
            continue
        for node in material.node_tree.nodes:
            if node.type != "BSDF_PRINCIPLED":
                continue
            alpha = node.inputs.get("Alpha")
            if alpha is not None:
                alpha.default_value = 1.0

    visible_meshes = {"Worker_Body", "Worker_Feet", "Worker_Head", "Worker_Legs"}
    for obj in scene.objects:
        if obj.type == "MESH" and obj.name not in visible_meshes:
            set_hidden(obj, True)
        elif obj.type in {"CAMERA", "LIGHT"}:
            set_hidden(obj, True)

    for obj in scene.objects:
        if obj.type == "MESH" and obj.name in visible_meshes:
            set_hidden(obj, False)

    bpy.ops.mesh.primitive_plane_add(size=8.0, location=(0.0, 0.0, -0.06))
    ground = bpy.context.object
    ground.name = "Review_Ground"
    ground.data.materials.clear()
    material = bpy.data.materials.new("Review_Ground_Material")
    material.diffuse_color = (0.035, 0.045, 0.06, 1.0)
    ground.data.materials.append(material)

    camera_data = bpy.data.cameras.new("Review_Camera")
    camera = bpy.data.objects.new("Review_Camera", camera_data)
    scene.collection.objects.link(camera)
    camera.location = (3.4, -4.8, 2.05)
    camera_data.lens = 58.0
    point_camera(camera, Vector((0.0, 0.0, 0.92)))
    camera.hide_viewport = True
    scene.camera = camera

    def add_area(name: str, location: tuple[float, float, float], energy: float, size: float) -> None:
        data = bpy.data.lights.new(name, type="AREA")
        data.energy = energy
        data.shape = "DISK"
        data.size = size
        light = bpy.data.objects.new(name, data)
        scene.collection.objects.link(light)
        light.location = location
        light.hide_viewport = True
        point_camera(light, Vector((0.0, 0.0, 0.95)))

    add_area("Review_Key", (2.5, -2.5, 3.2), 700.0, 3.0)
    add_area("Review_Fill", (-2.5, -1.5, 1.8), 350.0, 2.5)

    scene.render.engine = "BLENDER_EEVEE"
    scene.render.resolution_x = 640
    scene.render.resolution_y = 720
    scene.render.resolution_percentage = 100
    scene.render.fps = 24
    scene.render.film_transparent = False
    scene.world.color = (0.008, 0.012, 0.02)
    scene.frame_start = 1
    scene.frame_end = 20
    scene.frame_set(10)
    armature.animation_data_create()
    armature.animation_data.action = lean_left
    armature["review_note"] = (
        "LEAN_LEFT and LEAN_RIGHT are real actions made from the matching "
        "strafe cycles with a rigid upper-body lean along armature X. "
        "Review before runtime export."
    )

    bpy.ops.object.select_all(action="DESELECT")
    armature.select_set(True)
    bpy.context.view_layer.objects.active = armature
    armature.show_in_front = True


def export_runtime_candidate() -> None:
    OUTPUT_GLB.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.export_scene.gltf(
        filepath=str(OUTPUT_GLB),
        export_format="GLB",
        use_selection=False,
        export_animations=True,
        export_animation_mode="ACTIONS",
        export_nla_strips=False,
        export_anim_single_armature=True,
        export_merge_animation="ACTION",
        export_yup=True,
    )


def read_glb(path: Path) -> tuple[dict, bytes]:
    data = path.read_bytes()
    magic, version, total_length = struct.unpack_from("<4sII", data, 0)
    if magic != b"glTF" or version != 2 or total_length != len(data):
        raise ValueError(f"invalid GLB header: {path}")
    json_length, json_type = struct.unpack_from("<II", data, 12)
    if json_type != 0x4E4F534A:
        raise ValueError(f"missing GLB JSON chunk: {path}")
    json_start = 20
    json_end = json_start + json_length
    document = json.loads(data[json_start:json_end].decode("utf-8").rstrip(" \0"))
    bin_length, bin_type = struct.unpack_from("<II", data, json_end)
    if bin_type != 0x004E4942:
        raise ValueError(f"missing GLB BIN chunk: {path}")
    bin_start = json_end + 8
    return document, data[bin_start:bin_start + bin_length]


def write_glb(path: Path, document: dict, binary: bytes) -> None:
    json_data = json.dumps(document, separators=(",", ":")).encode("utf-8")
    json_data += b" " * (-len(json_data) % 4)
    binary += b"\0" * (-len(binary) % 4)
    total_length = 12 + 8 + len(json_data) + 8 + len(binary)
    output = bytearray(struct.pack("<4sII", b"glTF", 2, total_length))
    output.extend(struct.pack("<II", len(json_data), 0x4E4F534A))
    output.extend(json_data)
    output.extend(struct.pack("<II", len(binary), 0x004E4942))
    output.extend(binary)
    path.write_bytes(output)


def merge_lean_animations() -> None:
    base, base_binary = read_glb(SOURCE_GLB)
    candidate, candidate_binary = read_glb(OUTPUT_GLB)
    lean_names = {"LEAN_LEFT", "LEAN_RIGHT"}
    candidate_leans = [
        animation for animation in candidate.get("animations", [])
        if animation.get("name") in lean_names
    ]
    if {animation.get("name") for animation in candidate_leans} != lean_names:
        raise ValueError("candidate must contain LEAN_LEFT and LEAN_RIGHT")

    merge_record = base.get("extras", {}).get("lg_duel_lean_merge")
    if merge_record is not None:
        base["accessors"] = base["accessors"][:merge_record["accessor_count"]]
        base["bufferViews"] = base["bufferViews"][:merge_record["buffer_view_count"]]
        base_binary = base_binary[:merge_record["binary_length"]]

    base.setdefault("animations", [])[:] = [
        animation for animation in base["animations"]
        if animation.get("name") not in lean_names
    ]
    base.setdefault("bufferViews", [])
    base.setdefault("accessors", [])
    original_accessor_count = len(base["accessors"])
    original_buffer_view_count = len(base["bufferViews"])
    original_binary_length = len(base_binary)
    base_node_by_name = {
        node.get("name"): index for index, node in enumerate(base.get("nodes", []))
    }
    candidate_nodes = candidate.get("nodes", [])
    copied_accessors: dict[int, int] = {}
    merged_binary = bytearray(base_binary)

    def copy_accessor(candidate_index: int) -> int:
        if candidate_index in copied_accessors:
            return copied_accessors[candidate_index]
        accessor = copy.deepcopy(candidate["accessors"][candidate_index])
        candidate_view_index = accessor.get("bufferView")
        if candidate_view_index is None:
            raise ValueError("lean animation accessor has no buffer view")
        candidate_view = candidate["bufferViews"][candidate_view_index]
        start = candidate_view.get("byteOffset", 0)
        end = start + candidate_view["byteLength"]
        merged_binary.extend(b"\0" * (-len(merged_binary) % 4))
        view = copy.deepcopy(candidate_view)
        view["buffer"] = 0
        view["byteOffset"] = len(merged_binary)
        merged_binary.extend(candidate_binary[start:end])
        accessor["bufferView"] = len(base["bufferViews"])
        base["bufferViews"].append(view)
        copied_accessors[candidate_index] = len(base["accessors"])
        base["accessors"].append(accessor)
        return copied_accessors[candidate_index]

    for source_animation in candidate_leans:
        animation = copy.deepcopy(source_animation)
        for sampler in animation.get("samplers", []):
            sampler["input"] = copy_accessor(sampler["input"])
            sampler["output"] = copy_accessor(sampler["output"])
        for channel in animation.get("channels", []):
            candidate_node = candidate_nodes[channel["target"]["node"]]
            node_name = candidate_node.get("name")
            if node_name not in base_node_by_name:
                raise ValueError(f"lean channel targets unknown node: {node_name}")
            channel["target"]["node"] = base_node_by_name[node_name]
        base["animations"].append(animation)

    base["buffers"][0]["byteLength"] = len(merged_binary)
    base.setdefault("extras", {})["lg_duel_lean_merge"] = {
        "accessor_count": original_accessor_count,
        "buffer_view_count": original_buffer_view_count,
        "binary_length": original_binary_length,
    }
    write_glb(SOURCE_GLB, base, bytes(merged_binary))


def update_material_manifest_hash() -> None:
    model_hash = hashlib.sha256(SOURCE_GLB.read_bytes()).hexdigest()
    manifest = MATERIAL_MANIFEST.read_text(encoding="utf-8")
    manifest, replacements = re.subn(
        r'("model_sha256"\s*:\s*")[0-9a-fA-F]+(")',
        rf"\g<1>{model_hash}\g<2>",
        manifest,
        count=1,
    )
    if replacements != 1:
        raise ValueError("material manifest must contain one model_sha256")
    MATERIAL_MANIFEST.write_text(manifest, encoding="utf-8")


def main() -> None:
    armature, scene = import_worker()
    lean_left = make_lean_action(
        armature,
        scene,
        bpy.data.actions["STRAFE_LEFT"],
        "LEAN_LEFT",
        1.0,
    )
    lean_right = make_lean_action(
        armature,
        scene,
        bpy.data.actions["STRAFE_RIGHT"],
        "LEAN_RIGHT",
        -1.0,
    )
    export_runtime_candidate()
    merge_lean_animations()
    update_material_manifest_hash()
    set_review_scene(armature, scene, lean_left)
    OUTPUT_BLEND.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(OUTPUT_BLEND))
    print(f"saved {OUTPUT_BLEND}")
    print(f"exported {OUTPUT_GLB}")
    print("actions", lean_left.name, lean_right.name)


if __name__ == "__main__":
    main()
