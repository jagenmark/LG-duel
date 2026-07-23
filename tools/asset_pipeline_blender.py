#!/usr/bin/env python3
"""Run one deterministic LG Duel asset job inside headless Blender.

Invocation:
  blender --background --factory-startup --python tools/asset_pipeline_blender.py -- \
      request.json --result result.json

The request may also be passed as an inline JSON object. The script writes the
result on both success and failure so the parent process never has to parse a
Blender log.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import sys
import re
import traceback
from pathlib import Path
from typing import Any


TOOL_NAME = "lg-duel-blender-pipeline"


def _safe_still_name(value: Any) -> str:
    if not isinstance(value, str) or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,95}", value):
        raise JobError("animation preview still name must be one safe file stem")
    return value
SUPPORTED_EXTENSIONS = {".obj", ".fbx", ".gltf", ".glb"}
AXES = {"X", "Y", "Z", "-X", "-Y", "-Z"}


class JobError(RuntimeError):
    pass


def _parse_args() -> argparse.Namespace:
    args = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("request", help="JSON file path or inline JSON object")
    parser.add_argument("--result", required=True, help="result JSON file path")
    return parser.parse_args(args)


def _load_request(value: str) -> dict[str, Any]:
    text = value.strip()
    if text.startswith("{"):
        data = json.loads(text)
    else:
        request_path = _real(Path(value))
        data = json.loads(request_path.read_text(encoding="utf-8"))
        if isinstance(data, dict):
            for key in ("input_path", "output_dir"):
                if isinstance(data.get(key), str) and not Path(data[key]).is_absolute():
                    data[key] = str(request_path.parent / data[key])
    if not isinstance(data, dict):
        raise JobError("request JSON must be an object")
    return data


def _real(path: Path) -> Path:
    return Path(os.path.realpath(os.path.abspath(path)))


def _require_number(data: dict[str, Any], key: str, default: float | None = None) -> float:
    value = data.get(key, default)
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise JobError(f"{key} must be a finite number")
    return float(value)


def _axis(value: Any, key: str) -> str:
    axis = str(value).upper()
    if axis not in AXES:
        raise JobError(f"{key} must be one of {sorted(AXES)}")
    return axis


def _check_job(job: dict[str, Any], result_path: Path) -> tuple[Path, Path, str, dict, dict]:
    for key in ("input_path", "output_dir", "asset_type", "output_name"):
        if not isinstance(job.get(key), str) or not job[key].strip():
            raise JobError(f"{key} must be a non-empty string")
    source = _real(Path(job["input_path"]))
    output_dir = _real(Path(job["output_dir"]))
    if not source.is_file():
        raise JobError(f"input file does not exist: {source}")
    if source.suffix.lower() not in SUPPORTED_EXTENSIONS:
        raise JobError(f"unsupported input extension: {source.suffix}")
    name = job["output_name"].strip()
    if Path(name).name != name or name in {".", ".."}:
        raise JobError("output_name must be a plain file stem")
    if name.lower().endswith(".glb"):
        name = name[:-4]
    if not name:
        raise JobError("output_name must not be empty")
    output = _real(output_dir / f"{name}.glb")
    preview_dir = _real(output_dir / f"{name}_previews")
    result_path = _real(result_path)
    # Source files are immutable. Reject every known output collision up front.
    if source in {output, result_path} or source == preview_dir or preview_dir in source.parents:
        raise JobError("an output path would overwrite or contain the source")
    budgets = job.get("budgets", {})
    options = job.get("options", {})
    if not isinstance(budgets, dict) or not isinstance(options, dict):
        raise JobError("budgets and options must be objects")
    return source, output_dir, name, budgets, options


def _operator_params(operator: Any, values: dict[str, Any]) -> dict[str, Any]:
    names = {p.identifier for p in operator.get_rna_type().properties}
    return {key: value for key, value in values.items() if key in names}


def _blender_axis(axis: str) -> str:
    return ("NEGATIVE_" + axis[1:]) if axis.startswith("-") else axis


def _import_asset(bpy: Any, source: Path, forward: str, up: str) -> None:
    ext = source.suffix.lower()
    if ext == ".obj":
        if hasattr(bpy.ops.wm, "obj_import"):
            params = _operator_params(
                bpy.ops.wm.obj_import,
                {"filepath": str(source), "forward_axis": _blender_axis(forward), "up_axis": _blender_axis(up)},
            )
            result = bpy.ops.wm.obj_import(**params)
        elif hasattr(bpy.ops.import_scene, "obj"):
            result = bpy.ops.import_scene.obj(filepath=str(source), axis_forward=forward, axis_up=up)
        else:
            raise JobError("this Blender build has no OBJ importer")
    elif ext == ".fbx":
        result = bpy.ops.import_scene.fbx(filepath=str(source), axis_forward=forward, axis_up=up)
    else:
        # glTF has fixed axes; Blender's importer performs the standard conversion.
        result = bpy.ops.import_scene.gltf(filepath=str(source))
    if "FINISHED" not in result:
        raise JobError(f"Blender failed to import {source.name}")


def _attach_animation_source(
    bpy: Any, source: Path, forward: str, up: str, bone_map: dict[str, str],
    mode: str, clip_names: dict[str, str]
) -> dict[str, Any]:
    """Bind the imported meshes to a second, compatible animated rig."""
    mesh_objects = _meshes(bpy, include_generated=False)
    target_rigs = [obj for obj in _scene_objects(bpy) if obj.type == "ARMATURE"]
    if len(target_rigs) != 1:
        raise JobError("animation transfer needs exactly one mesh armature")
    target_rig = target_rigs[0]
    objects_before = set(bpy.data.objects)
    actions_before = set(bpy.data.actions)
    _import_asset(bpy, source, forward, up)
    new_objects = [obj for obj in bpy.data.objects if obj not in objects_before]
    source_rigs = [obj for obj in new_objects if obj.type == "ARMATURE"]
    if len(source_rigs) != 1:
        raise JobError("animation source needs exactly one armature")
    source_rig = source_rigs[0]
    source_bones = {bone.name for bone in source_rig.data.bones}
    target_bones = {bone.name for bone in target_rig.data.bones}
    for old, new in sorted(bone_map.items()):
        if old not in target_bones:
            raise JobError(f"animation bone-map source not found: {old}")
        if new not in source_bones:
            raise JobError(f"animation bone-map target not found: {new}")
    missing = sorted((target_bones - set(bone_map)) - source_bones)
    if missing:
        raise JobError("animation rig lacks mesh bones: " + ", ".join(missing))
    new_actions = [action for action in bpy.data.actions if action not in actions_before]
    if mode == "retarget":
        if bone_map:
            raise JobError("retarget mode does not support bone renames")
        common_bones = sorted(target_bones & source_bones)
        if not common_bones:
            raise JobError("retarget rigs have no common bones")
        for bone_name in common_bones:
            target_bone = target_rig.pose.bones[bone_name]
            rotation = target_bone.constraints.new("COPY_ROTATION")
            rotation.target = source_rig
            rotation.subtarget = bone_name
            rotation.owner_space = "POSE"
            rotation.target_space = "POSE"
            rotation.mix_mode = "REPLACE"
        target_rig.animation_data_create()
        source_rig.animation_data_create()
        baked_actions = []
        _select(bpy, [target_rig], target_rig)
        bpy.ops.object.mode_set(mode="POSE")
        bpy.ops.pose.select_all(action="SELECT")
        for source_action in sorted(new_actions, key=lambda item: item.name):
            source_rig.animation_data.action = source_action
            target_rig.animation_data.action = None
            start = int(math.floor(source_action.frame_range[0]))
            end = int(math.ceil(source_action.frame_range[1]))
            bpy.context.scene.frame_start = start
            bpy.context.scene.frame_end = end
            result = bpy.ops.nla.bake(
                frame_start=start, frame_end=end, step=1, only_selected=True,
                visual_keying=True, clear_constraints=False, clear_parents=False,
                use_current_action=False, clean_curves=True, bake_types={"POSE"},
            )
            if "FINISHED" not in result or target_rig.animation_data.action is None:
                raise JobError(f"failed to bake retargeted clip: {source_action.name}")
            baked = target_rig.animation_data.action
            source_name = source_action.name.rsplit("|", 1)[-1]
            baked.name = clip_names.get(source_name, source_name)
            baked_actions.append(baked)
            target_rig.animation_data.action = None
        bpy.ops.object.mode_set(mode="OBJECT")
        for pose_bone in target_rig.pose.bones:
            for constraint in list(pose_bone.constraints):
                pose_bone.constraints.remove(constraint)
        for action in new_actions:
            bpy.data.actions.remove(action)
        bpy.data.objects.remove(source_rig, do_unlink=True)
        for obj in new_objects:
            if obj != source_rig:
                bpy.data.objects.remove(obj, do_unlink=True)
        for action in baked_actions:
            track = target_rig.animation_data.nla_tracks.new()
            track.name = action.name
            track.strips.new(action.name, int(action.frame_range[0]), action)
        return {
            "source": str(source), "armature": target_rig.name,
            "bones": len(target_rig.data.bones), "actions_added": len(baked_actions),
            "vertex_groups_renamed": 0, "mode": mode,
        }
    if mode == "actions":
        if bone_map:
            raise JobError("action transfer does not support bone renames")
        target_rig.animation_data_create()
        for action in sorted(new_actions, key=lambda item: item.name):
            source_name = action.name.rsplit("|", 1)[-1]
            action.name = clip_names.get(source_name, source_name)
            track = target_rig.animation_data.nla_tracks.new()
            track.name = action.name
            track.strips.new(action.name, int(action.frame_range[0]), action)
        bpy.data.objects.remove(source_rig, do_unlink=True)
        for obj in new_objects:
            if obj != source_rig:
                bpy.data.objects.remove(obj, do_unlink=True)
        return {
            "source": str(source), "armature": target_rig.name,
            "bones": len(target_rig.data.bones), "actions_added": len(new_actions),
            "vertex_groups_renamed": 0, "mode": mode,
        }
    if mode != "rebind":
        raise JobError("animation_transfer_mode must be actions, retarget, or rebind")
    for old_name in sorted(target_bones):
        new_name = bone_map.get(old_name, old_name)
        if new_name not in source_bones:
            continue
        old_matrix = target_rig.data.bones[old_name].matrix_local
        new_matrix = source_rig.data.bones[new_name].matrix_local
        if any(abs(old_matrix[row][column] - new_matrix[row][column]) > 0.0001 for row in range(4) for column in range(4)):
            raise JobError(f"animation rig rest pose differs at bone: {old_name}")
    renamed_groups = 0
    for mesh in mesh_objects:
        for old, new in sorted(bone_map.items()):
            group = mesh.vertex_groups.get(old)
            if group is not None:
                group.name = new
                renamed_groups += 1
        for modifier in mesh.modifiers:
            if modifier.type == "ARMATURE" and modifier.object == target_rig:
                modifier.object = source_rig
        if mesh.parent == target_rig:
            mesh.parent = source_rig
    bpy.data.objects.remove(target_rig, do_unlink=True)
    for obj in new_objects:
        if obj != source_rig and obj.type != "MESH":
            bpy.data.objects.remove(obj, do_unlink=True)
    return {
        "source": str(source),
        "armature": source_rig.name,
        "bones": len(source_rig.data.bones),
        "actions_added": len(new_actions), "vertex_groups_renamed": renamed_groups, "mode": mode,
    }


def _attach_mesh_parts(bpy: Any, parts: list[Any], forward: str, up: str) -> list[dict[str, Any]]:
    rigs = [obj for obj in _scene_objects(bpy) if obj.type == "ARMATURE"]
    if len(rigs) != 1:
        raise JobError("mesh parts need exactly one base armature")
    base_rig = rigs[0]
    added: list[dict[str, Any]] = []
    for entry in parts:
        if not isinstance(entry, dict) or not isinstance(entry.get("path"), str):
            raise JobError("each mesh_parts entry needs path and mesh_names")
        names = entry.get("mesh_names")
        if not isinstance(names, list) or not names or not all(isinstance(name, str) for name in names):
            raise JobError("mesh_parts.mesh_names must be a non-empty string list")
        path = _real(Path(entry["path"]))
        if not path.is_file() or path.suffix.lower() not in SUPPORTED_EXTENSIONS:
            raise JobError("mesh part path must name a supported asset file")
        objects_before = set(bpy.data.objects)
        _import_asset(bpy, path, forward, up)
        new_objects = [obj for obj in bpy.data.objects if obj not in objects_before]
        part_rigs = [obj for obj in new_objects if obj.type == "ARMATURE"]
        selected_meshes = [obj for obj in new_objects if obj.type == "MESH" and obj.name in names]
        if len(part_rigs) != 1 or len(selected_meshes) != len(names):
            raise JobError(f"mesh part did not contain its declared rig and meshes: {path.name}")
        part_rig = part_rigs[0]
        base_bones = {bone.name for bone in base_rig.data.bones}
        part_bones = {bone.name for bone in part_rig.data.bones}
        if base_bones != part_bones:
            raise JobError(f"mesh part bone names differ: {path.name}")
        for bone_name in sorted(base_bones):
            lhs = base_rig.data.bones[bone_name].matrix_local
            rhs = part_rig.data.bones[bone_name].matrix_local
            if any(abs(lhs[row][column] - rhs[row][column]) > 0.0001 for row in range(4) for column in range(4)):
                raise JobError(f"mesh part rest pose differs at {bone_name}: {path.name}")
        for mesh in selected_meshes:
            for modifier in mesh.modifiers:
                if modifier.type == "ARMATURE" and modifier.object == part_rig:
                    modifier.object = base_rig
            if mesh.parent == part_rig:
                mesh.parent = base_rig
        for obj in new_objects:
            if obj not in selected_meshes:
                bpy.data.objects.remove(obj, do_unlink=True)
        added.append({"source": str(path), "meshes": sorted(names)})
    return added


def _repair_skeleton(bpy: Any, parent_overrides: dict[str, str], remove_bones: list[str]) -> dict[str, Any]:
    rigs = [obj for obj in _scene_objects(bpy) if obj.type == "ARMATURE"]
    if (parent_overrides or remove_bones) and len(rigs) != 1:
        raise JobError("skeleton repair needs exactly one armature")
    if not parent_overrides and not remove_bones:
        return {"parents_changed": {}, "bones_removed": []}
    rig = rigs[0]
    existing = {bone.name for bone in rig.data.bones}
    missing = sorted((set(parent_overrides) | set(parent_overrides.values()) | set(remove_bones)) - existing)
    if missing:
        raise JobError("skeleton repair bones not found: " + ", ".join(missing))
    weighted = {
        group.name
        for mesh in _meshes(bpy, include_generated=False)
        for group in mesh.vertex_groups
        if any(
            item.group == group.index and item.weight > 0.0
            for vertex in mesh.data.vertices for item in vertex.groups
        )
    }
    unsafe = sorted(set(remove_bones) & weighted)
    if unsafe:
        raise JobError("cannot remove weighted bones: " + ", ".join(unsafe))
    _select(bpy, [rig], rig)
    bpy.ops.object.mode_set(mode="EDIT")
    for bone_name, parent_name in sorted(parent_overrides.items()):
        bone = rig.data.edit_bones[bone_name]
        bone.parent = rig.data.edit_bones[parent_name]
        bone.use_connect = False
    for bone_name in remove_bones:
        rig.data.edit_bones.remove(rig.data.edit_bones[bone_name])
    bpy.ops.object.mode_set(mode="OBJECT")
    return {"parents_changed": dict(sorted(parent_overrides.items())), "bones_removed": sorted(remove_bones)}


def _create_attachment_points(bpy: Any, points: dict[str, str]) -> list[str]:
    rigs = [obj for obj in _scene_objects(bpy) if obj.type == "ARMATURE"]
    if points and len(rigs) != 1:
        raise JobError("attachment points need exactly one armature")
    if not points:
        return []
    rig = rigs[0]
    names: list[str] = []
    for name, bone_name in sorted(points.items()):
        if not name or Path(name).name != name or not isinstance(bone_name, str):
            raise JobError("attachment_points must map plain names to bone names")
        if rig.data.bones.get(bone_name) is None:
            raise JobError(f"attachment bone not found: {bone_name}")
        marker = bpy.data.objects.new(name, None)
        bpy.context.scene.collection.objects.link(marker)
        marker.empty_display_type = "ARROWS"
        marker.empty_display_size = 0.08
        marker.parent = rig
        marker.parent_type = "BONE"
        marker.parent_bone = bone_name
        names.append(name)
    return names


def _scene_objects(bpy: Any) -> list[Any]:
    return sorted(list(bpy.context.scene.objects), key=lambda obj: obj.name)


def _meshes(bpy: Any, include_generated: bool = True) -> list[Any]:
    result = []
    for obj in _scene_objects(bpy):
        if obj.type != "MESH":
            continue
        if not include_generated and obj.get("lg_generated"):
            continue
        result.append(obj)
    return result


def _select(bpy: Any, objects: list[Any], active: Any | None = None) -> None:
    bpy.ops.object.select_all(action="DESELECT")
    for obj in objects:
        obj.select_set(True)
    if objects:
        bpy.context.view_layer.objects.active = active or objects[0]


def _normalize_transforms(bpy: Any, scale_meters: float) -> None:
    if scale_meters <= 0:
        raise JobError("unit_scale_meters must be greater than zero")
    roots = [obj for obj in _scene_objects(bpy) if obj.parent is None]
    for obj in roots:
        obj.scale = tuple(component * scale_meters for component in obj.scale)
    # Applying only roots keeps parent-child scale from being applied twice.
    for obj in roots:
        _select(bpy, [obj], obj)
        if obj.type in {"MESH", "ARMATURE", "EMPTY"}:
            bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)


def _triangulate(bpy: Any) -> None:
    for obj in _meshes(bpy, include_generated=False):
        _select(bpy, [obj], obj)
        bpy.ops.object.mode_set(mode="EDIT")
        bpy.ops.mesh.select_all(action="SELECT")
        # Fixed quad splits and clipped n-gons avoid view or tool-state choices.
        bpy.ops.mesh.quads_convert_to_tris(quad_method="FIXED", ngon_method="CLIP")
        if hasattr(bpy.ops.mesh, "normals_make_consistent"):
            bpy.ops.mesh.normals_make_consistent(inside=False)
        bpy.ops.object.mode_set(mode="OBJECT")
        obj.data.validate(clean_customdata=False)
        obj.data.update(calc_edges=True)


def _material_key(bpy: Any, material: Any) -> tuple:
    if material.use_nodes and material.node_tree:
        nodes = []
        for node in sorted(material.node_tree.nodes, key=lambda item: (item.bl_idname, item.name)):
            values = []
            for socket in node.inputs:
                value = getattr(socket, "default_value", None)
                if value is not None:
                    try:
                        values.append((socket.name, tuple(round(float(v), 8) for v in value)))
                    except (TypeError, ValueError):
                        try:
                            values.append((socket.name, round(float(value), 8)))
                        except (TypeError, ValueError):
                            values.append((socket.name, str(value)))
            image = getattr(node, "image", None)
            settings = tuple(
                (name, str(getattr(node, name)))
                for name in ("operation", "blend_type", "data_type", "interpolation", "projection", "extension", "distribution", "space", "uv_map")
                if hasattr(node, name)
            )
            nodes.append((node.bl_idname, tuple(values), settings, _real(Path(bpy.path.abspath(image.filepath))).as_posix() if image and image.filepath else ""))
        links = sorted(
            (link.from_node.name, link.from_socket.name, link.to_node.name, link.to_socket.name)
            for link in material.node_tree.links
        )
        return ("nodes", tuple(nodes), tuple(links), material.blend_method if hasattr(material, "blend_method") else "")
    return ("basic", tuple(round(float(v), 8) for v in material.diffuse_color))


def _consolidate_materials(bpy: Any) -> int:
    canonical: dict[tuple, Any] = {}
    replaced = 0
    for obj in _meshes(bpy, include_generated=False):
        for index, material in enumerate(list(obj.data.materials)):
            if material is None:
                continue
            key = _material_key(bpy, material)
            if key in canonical:
                obj.data.materials[index] = canonical[key]
                replaced += 1
            else:
                canonical[key] = material
    return replaced


def _process_textures(bpy: Any, maximum: int | None, target_format: str | None) -> list[dict[str, Any]]:
    facts = []
    if maximum is not None and maximum < 1:
        raise JobError("options.texture_max_dimension must be at least 1")
    if target_format is not None:
        target_format = target_format.upper()
        if target_format not in {"PNG", "JPEG"}:
            raise JobError("options.texture_format must be PNG or JPEG")
    for image in sorted(bpy.data.images, key=lambda item: item.name):
        if image.source not in {"FILE", "GENERATED", "VIEWER"} or not image.has_data:
            continue
        before = [int(image.size[0]), int(image.size[1])]
        if maximum and max(before) > maximum:
            ratio = maximum / max(before)
            image.scale(max(1, round(before[0] * ratio)), max(1, round(before[1] * ratio)))
        if target_format:
            image.file_format = target_format
        facts.append({"name": image.name, "before": before, "after": [int(image.size[0]), int(image.size[1])], "format": image.file_format})
    return facts


def _limit_skin_weights(bpy: Any, maximum: int) -> int:
    if maximum < 1:
        raise JobError("options.max_influences must be at least 1")
    changed = 0
    for obj in _meshes(bpy, include_generated=False):
        for vertex in obj.data.vertices:
            weights = sorted(((item.group, item.weight) for item in vertex.groups), key=lambda item: (-item[1], item[0]))
            if len(weights) <= maximum:
                continue
            keep = weights[:maximum]
            keep_ids = {group for group, _ in keep}
            for group, _ in weights[maximum:]:
                obj.vertex_groups[group].remove([vertex.index])
            total = sum(weight for _, weight in keep)
            if total > 0:
                for group, weight in keep:
                    obj.vertex_groups[group].add([vertex.index], weight / total, "REPLACE")
            changed += 1
    return changed


def _rename_bones(bpy: Any, bone_map: dict[str, str], required: list[str]) -> int:
    armatures = sorted((obj for obj in _scene_objects(bpy) if obj.type == "ARMATURE"), key=lambda obj: obj.name)
    existing = {bone.name for obj in armatures for bone in obj.data.bones}
    mapped_sources = {src for src, dst in bone_map.items() if dst in required or src in required}
    # Required canonical bones must be named by the map, even for identity maps.
    missing_required = sorted(name for name in required if name not in bone_map and name not in bone_map.values())
    if missing_required:
        raise JobError(f"required bone mapping absent: {', '.join(missing_required)}")
    unresolved_required = sorted(src for src in mapped_sources if src not in existing)
    if unresolved_required:
        raise JobError(f"required bone mapping source not found: {', '.join(unresolved_required)}")
    missing_sources = sorted(name for name in bone_map if name not in existing)
    if missing_sources:
        raise JobError(f"bone map source not found: {', '.join(missing_sources)}")
    targets = list(bone_map.values())
    if len(targets) != len(set(targets)) or any(not isinstance(name, str) or not name for name in targets):
        raise JobError("bone map targets must be unique non-empty names")
    count = 0
    for armature in armatures:
        local = {bone.name for bone in armature.data.bones}
        applicable = {src: dst for src, dst in bone_map.items() if src in local and src != dst}
        conflicts = sorted(dst for src, dst in applicable.items() if dst in local and dst not in applicable)
        if conflicts:
            raise JobError(f"bone map target already exists: {', '.join(conflicts)}")
        temporary = {}
        for index, src in enumerate(sorted(applicable)):
            temp = f"__LG_RETARGET_{index:04d}__"
            armature.data.bones[src].name = temp
            temporary[temp] = applicable[src]
        for temp, dst in temporary.items():
            armature.data.bones[temp].name = dst
            count += 1
    for obj in _meshes(bpy, include_generated=False):
        for src, dst in bone_map.items():
            group = obj.vertex_groups.get(src)
            if group and src != dst:
                if obj.vertex_groups.get(dst):
                    raise JobError(f"vertex group target already exists: {dst}")
                group.name = dst
    for action in bpy.data.actions:
        for curve in action.fcurves:
            for src, dst in bone_map.items():
                token = f'pose.bones["{src}"]'
                if token in curve.data_path:
                    curve.data_path = curve.data_path.replace(token, f'pose.bones["{dst}"]')
    return count


def _process_animations(bpy: Any, renames: dict[str, str], trims: dict[str, Any]) -> list[dict[str, Any]]:
    actions = {action.name: action for action in bpy.data.actions}
    for old, new in sorted(renames.items()):
        if old not in actions:
            raise JobError(f"animation rename source not found: {old}")
        if not isinstance(new, str) or not new or (new in actions and new != old):
            raise JobError(f"invalid or duplicate animation name: {new!r}")
        actions[old].name = new
        actions[new] = actions.pop(old)
    facts = []
    for name, spec in sorted(trims.items()):
        if name not in actions:
            raise JobError(f"animation trim source not found: {name}")
        if isinstance(spec, list) and len(spec) == 2:
            start, end = spec
        elif isinstance(spec, dict):
            start, end = spec.get("start"), spec.get("end")
        else:
            raise JobError(f"animation trim for {name} must be [start, end] or an object")
        if not isinstance(start, (int, float)) or not isinstance(end, (int, float)) or start > end:
            raise JobError(f"invalid animation trim range for {name}")
        action = actions[name]
        removed = 0
        for curve in action.fcurves:
            for point in list(curve.keyframe_points):
                if point.co.x < start or point.co.x > end:
                    curve.keyframe_points.remove(point, fast=True)
                    removed += 1
            curve.update()
        facts.append({"name": name, "start": start, "end": end, "keys_removed": removed})
    return facts


def _duplicate_animation_aliases(bpy: Any, aliases: dict[str, str]) -> list[dict[str, str]]:
    rigs = [obj for obj in _scene_objects(bpy) if obj.type == "ARMATURE"]
    if aliases and len(rigs) != 1:
        raise JobError("animation aliases need exactly one armature")
    created = []
    for alias, source_name in sorted(aliases.items()):
        if not alias or bpy.data.actions.get(alias) is not None:
            raise JobError(f"invalid or duplicate animation alias: {alias}")
        source = bpy.data.actions.get(source_name)
        if source is None:
            raise JobError(f"animation alias source not found: {source_name}")
        action = source.copy()
        action.name = alias
        track = rigs[0].animation_data.nla_tracks.new()
        track.name = alias
        track.strips.new(alias, int(action.frame_range[0]), action)
        created.append({"name": alias, "source": source_name})
    return created


def _create_two_handed_idle(bpy: Any, spec: dict[str, Any] | None) -> dict[str, Any] | None:
    """Build a fixed two-handed idle from an existing aimed idle clip."""
    if spec is None:
        return None
    if not isinstance(spec, dict):
        raise JobError("options.two_handed_idle must be an object")
    source_name = spec.get("source")
    output_name = spec.get("name")
    if not isinstance(source_name, str) or not source_name or not isinstance(output_name, str) or not output_name:
        raise JobError("two_handed_idle needs non-empty source and name values")
    if bpy.data.actions.get(output_name) is not None:
        raise JobError(f"two_handed_idle output already exists: {output_name}")
    source = bpy.data.actions.get(source_name)
    rigs = [obj for obj in _scene_objects(bpy) if obj.type == "ARMATURE"]
    if source is None or len(rigs) != 1:
        raise JobError("two_handed_idle source or armature not found")
    rig = rigs[0]
    required = ("UpperArm.L", "LowerArm.L", "Wrist.L", "Wrist.R")
    missing = [name for name in required if name not in rig.pose.bones]
    if missing:
        raise JobError("two_handed_idle bones are missing: " + ", ".join(missing))

    def vector_option(name: str, default: tuple[float, float, float]) -> tuple[float, float, float]:
        value = spec.get(name, list(default))
        if (
            not isinstance(value, list) or len(value) != 3 or
            any(isinstance(item, bool) or not isinstance(item, (int, float)) or not math.isfinite(item) for item in value)
        ):
            raise JobError(f"two_handed_idle.{name} must contain three finite numbers")
        return tuple(float(item) for item in value)

    hand_offset = vector_option("hand_offset", (0.18, 0.0, 0.015))
    pole_offset = vector_option("pole_offset", (0.35, -0.45, -0.05))
    wrist_roll = spec.get("wrist_roll_degrees", 90.0)
    if isinstance(wrist_roll, bool) or not isinstance(wrist_roll, (int, float)) or not math.isfinite(wrist_roll):
        raise JobError("two_handed_idle.wrist_roll_degrees must be finite")

    from mathutils import Quaternion, Vector

    action = source.copy()
    action.name = output_name
    rig.animation_data_create()
    muted = [(track, track.mute) for track in rig.animation_data.nla_tracks]
    for track, _ in muted:
        track.mute = True
    rig.animation_data.action = action
    start = int(math.floor(action.frame_range[0]))
    end = int(math.ceil(action.frame_range[1]))
    scene = bpy.context.scene
    target = bpy.data.objects.new(f"{output_name}_grip_target", None)
    pole = bpy.data.objects.new(f"{output_name}_elbow_pole", None)
    scene.collection.objects.link(target)
    scene.collection.objects.link(pole)
    for frame in range(start, end + 1):
        scene.frame_set(frame)
        bpy.context.view_layer.update()
        target.location = rig.matrix_world @ rig.pose.bones["Wrist.R"].head + Vector(hand_offset)
        pole.location = rig.matrix_world @ rig.pose.bones["UpperArm.L"].head + Vector(pole_offset)
        target.keyframe_insert(data_path="location", frame=frame)
        pole.keyframe_insert(data_path="location", frame=frame)
    constraint = rig.pose.bones["LowerArm.L"].constraints.new("IK")
    constraint.name = "LG two-handed idle grip"
    constraint.target = target
    constraint.pole_target = pole
    constraint.chain_count = 2
    constraint.use_tail = True
    _select(bpy, [rig], rig)
    result = bpy.ops.nla.bake(
        frame_start=start, frame_end=end, step=1, only_selected=False,
        visual_keying=True, clear_constraints=True, clear_parents=False,
        use_current_action=True, bake_types={"POSE"},
    )
    if "FINISHED" not in result:
        raise JobError("failed to bake two_handed_idle")
    helper_actions = [
        obj.animation_data.action
        for obj in (target, pole)
        if obj.animation_data is not None and obj.animation_data.action is not None
    ]
    bpy.data.objects.remove(target, do_unlink=True)
    bpy.data.objects.remove(pole, do_unlink=True)
    for helper_action in helper_actions:
        if helper_action.users == 0:
            bpy.data.actions.remove(helper_action)
    wrist = rig.pose.bones["Wrist.L"]
    roll = math.radians(float(wrist_roll))
    for frame in range(start, end + 1):
        scene.frame_set(frame)
        bpy.context.view_layer.update()
        current = wrist.matrix_basis.to_quaternion()
        wrist.rotation_mode = "QUATERNION"
        wrist.rotation_quaternion = current @ Quaternion((0.0, 1.0, 0.0), roll)
        wrist.keyframe_insert(data_path="rotation_quaternion", frame=frame)
    track = rig.animation_data.nla_tracks.new()
    track.name = output_name
    track.strips.new(output_name, start, action)
    for existing, was_muted in muted:
        existing.mute = was_muted
    rig.animation_data.action = None
    return {
        "name": output_name,
        "source": source_name,
        "hand_offset": list(hand_offset),
        "pole_offset": list(pole_offset),
        "wrist_roll_degrees": float(wrist_roll),
        "frame_range": [start, end],
    }


def _metrics(bpy: Any, include_generated: bool = False) -> dict[str, int]:
    objects = _meshes(bpy, include_generated=include_generated)
    triangles = 0
    vertices = 0
    material_names = set()
    for obj in objects:
        obj.data.calc_loop_triangles()
        triangles += len(obj.data.loop_triangles)
        vertices += len(obj.data.vertices)
        material_names.update(material.name for material in obj.data.materials if material)
    images = {image.name for image in bpy.data.images if image.users}
    return {"mesh_objects": len(objects), "vertices": vertices, "triangles": triangles, "materials": len(material_names), "textures": len(images), "animations": len(bpy.data.actions)}


def _create_lods(bpy: Any, output_name: str, ratios: list[Any], budgets: dict[str, Any]) -> list[dict[str, Any]]:
    base_objects = _meshes(bpy, include_generated=False)
    base_triangles = _metrics(bpy)["triangles"]
    target_list = budgets.get("lod_triangles")
    if target_list is not None and (not isinstance(target_list, list) or len(target_list) != len(ratios)):
        raise JobError("budgets.lod_triangles must match options.lod_ratios")
    facts = []
    for lod_index, raw_ratio in enumerate(ratios, 1):
        if isinstance(raw_ratio, bool) or not isinstance(raw_ratio, (int, float)) or not 0 < raw_ratio < 1:
            raise JobError("each LOD ratio must be greater than 0 and less than 1")
        target = int(target_list[lod_index - 1]) if target_list is not None else max(1, math.floor(base_triangles * raw_ratio))
        if target < 1 or target >= base_triangles:
            raise JobError(f"LOD{lod_index} target must be between 1 and {base_triangles - 1} triangles")
        actual = 0
        for base in base_objects:
            clone = base.copy()
            clone.data = base.data.copy()
            clone.animation_data_clear()
            clone.name = f"{output_name}_LOD{lod_index}_{base.name}"
            clone["lg_generated"] = "lod"
            bpy.context.collection.objects.link(clone)
            clone_target = max(1, math.floor(target * len(base.data.loop_triangles) / base_triangles))
            ratio = min(1.0, clone_target / max(1, len(base.data.loop_triangles)))
            modifier = clone.modifiers.new(name="LG deterministic LOD", type="DECIMATE")
            modifier.decimate_type = "COLLAPSE"
            modifier.ratio = ratio
            modifier.use_collapse_triangulate = True
            _select(bpy, [clone], clone)
            bpy.ops.object.modifier_apply(modifier=modifier.name)
            clone.data.calc_loop_triangles()
            actual += len(clone.data.loop_triangles)
        facts.append({"level": lod_index, "ratio": raw_ratio, "target_triangles": target, "actual_triangles": actual})
    return facts


def _world_bounds(objects: list[Any]) -> tuple[list[float], list[float]]:
    from mathutils import Vector

    # Blender 5 exposes bound-box corners as property arrays, not vectors.
    points = [obj.matrix_world @ Vector(corner) for obj in objects for corner in obj.bound_box]
    if not points:
        raise JobError("import produced no mesh geometry")
    return ([min(point[i] for point in points) for i in range(3)], [max(point[i] for point in points) for i in range(3)])


def _add_box(bpy: Any, name: str, low: list[float], high: list[float], kind: str) -> Any:
    center = [(low[i] + high[i]) * 0.5 for i in range(3)]
    size = [max(high[i] - low[i], 0.001) for i in range(3)]
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=center)
    obj = bpy.context.object
    obj.name = name
    obj.dimensions = size
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    obj.display_type = "WIRE"
    obj["lg_generated"] = kind
    return obj


def _create_proxies(bpy: Any, output_name: str, collision: bool, hitboxes: bool) -> list[str]:
    base = _meshes(bpy, include_generated=False)
    names = []
    if collision:
        low, high = _world_bounds(base)
        names.append(_add_box(bpy, f"UCX_{output_name}_00", low, high, "collision").name)
    if hitboxes:
        for index, obj in enumerate(base):
            low, high = _world_bounds([obj])
            names.append(_add_box(bpy, f"HITBOX_{output_name}_{index:02d}", low, high, "hitbox").name)
    return names


def _compute_tangents(bpy: Any, warnings: list[str]) -> None:
    for obj in _meshes(bpy, include_generated=True):
        mesh = obj.data
        mesh.validate(clean_customdata=False)
        mesh.update(calc_edges=True)
        if mesh.uv_layers:
            try:
                mesh.calc_tangents(uvmap=mesh.uv_layers.active.name)
            except RuntimeError as exc:
                warnings.append(f"could not calculate tangents for {obj.name}: {exc}")
        else:
            warnings.append(f"{obj.name} has no UV map; tangents were not calculated")


def _preview_direction(view: Any) -> tuple[str, tuple[float, float, float]]:
    fixed = {
        "front": (0.0, -1.0, 0.0), "back": (0.0, 1.0, 0.0),
        "left": (-1.0, 0.0, 0.0), "right": (1.0, 0.0, 0.0),
        "top": (0.0, 0.0, 1.0), "iso": (1.0, -1.0, 0.75),
    }
    if isinstance(view, str) and view.lower() in fixed:
        return view.lower(), fixed[view.lower()]
    if isinstance(view, dict) and isinstance(view.get("name"), str):
        direction = view.get("direction")
        if isinstance(direction, list) and len(direction) == 3 and all(isinstance(v, (int, float)) for v in direction):
            name = view["name"]
            if not re.fullmatch(r"[A-Za-z0-9_-]{1,64}", name):
                raise JobError("preview names may contain only letters, digits, '_' and '-'")
            return name, tuple(float(v) for v in direction)
    raise JobError(f"invalid preview view: {view!r}")


def _render_previews(bpy: Any, output_dir: Path, output_name: str, views: list[Any]) -> list[str]:
    if not views:
        return []
    from mathutils import Vector

    base = _meshes(bpy, include_generated=False)
    low, high = _world_bounds(base)
    center = Vector(tuple((low[i] + high[i]) * 0.5 for i in range(3)))
    radius = max(max(high[i] - low[i] for i in range(3)) * 0.75, 0.25)
    preview_dir = output_dir / f"{output_name}_previews"
    preview_dir.mkdir(parents=True, exist_ok=True)
    camera_data = bpy.data.cameras.new("LG_PREVIEW_CAMERA")
    camera = bpy.data.objects.new("LG_PREVIEW_CAMERA", camera_data)
    bpy.context.collection.objects.link(camera)
    camera_data.type = "ORTHO"
    camera_data.ortho_scale = radius * 2.4
    bpy.context.scene.camera = camera
    world = bpy.context.scene.world or bpy.data.worlds.new("LG_PREVIEW_WORLD")
    bpy.context.scene.world = world
    world.color = (0.04, 0.04, 0.04)
    light_data = bpy.data.lights.new("LG_PREVIEW_KEY", "AREA")
    light_data.energy = 1000.0
    light_data.size = radius * 2.0
    light = bpy.data.objects.new("LG_PREVIEW_KEY", light_data)
    bpy.context.collection.objects.link(light)
    light.location = center + Vector((radius * 2, -radius * 2, radius * 3))
    light.rotation_euler = (Vector((0, 0, -1))).to_track_quat("-Z", "Y").to_euler()
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.render.resolution_x = 512
    scene.render.resolution_y = 512
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.film_transparent = False
    outputs = []
    generated = [obj for obj in _scene_objects(bpy) if obj.get("lg_generated")]
    for obj in generated:
        obj.hide_render = True
    for raw in views:
        name, direction = _preview_direction(raw)
        vector = Vector(direction)
        if vector.length == 0:
            raise JobError(f"preview direction for {name} is zero")
        vector.normalize()
        camera.location = center + vector * radius * 3.0
        camera.rotation_euler = (center - camera.location).to_track_quat("-Z", "Y").to_euler()
        path = preview_dir / f"{name}.png"
        scene.render.filepath = str(path)
        bpy.ops.render.render(write_still=True)
        outputs.append(str(path))
    for obj in generated:
        obj.hide_render = False
    return outputs


def _purge(bpy: Any) -> None:
    # A fixed-point purge keeps results free of import leftovers across Blender versions.
    for _ in range(8):
        result = bpy.ops.outliner.orphans_purge(do_local_ids=True, do_linked_ids=True, do_recursive=True)
        if "CANCELLED" in result:
            break


def _export_glb(bpy: Any, output: Path) -> None:
    export_objects = [obj for obj in _scene_objects(bpy) if obj.type in {"MESH", "ARMATURE", "EMPTY"}]
    _select(bpy, export_objects)
    params = {
        "filepath": str(output), "export_format": "GLB", "use_selection": True,
        "export_apply": True, "export_animations": True, "export_yup": True,
        "export_tangents": True, "export_materials": "EXPORT",
    }
    result = bpy.ops.export_scene.gltf(**_operator_params(bpy.ops.export_scene.gltf, params))
    if "FINISHED" not in result or not output.is_file():
        raise JobError("Blender failed to export GLB")


def _budget_result(metrics: dict[str, int], budgets: dict[str, Any], lods: list[dict[str, Any]]) -> dict[str, Any]:
    mapping = {"triangles": "triangles", "vertices": "vertices", "materials": "materials", "textures": "textures"}
    checks = {}
    passed = True
    for key, metric in mapping.items():
        budget_key = key if key in budgets else f"max_{key}"
        if budget_key not in budgets:
            continue
        limit = budgets[budget_key]
        if isinstance(limit, bool) or not isinstance(limit, int) or limit < 0:
            raise JobError(f"budgets.{budget_key} must be a non-negative integer")
        ok = metrics[metric] <= limit
        checks[key] = {"actual": metrics[metric], "limit": limit, "passed": ok}
        passed = passed and ok
    for lod in lods:
        ok = lod["actual_triangles"] <= lod["target_triangles"]
        checks[f"lod{lod['level']}_triangles"] = {"actual": lod["actual_triangles"], "limit": lod["target_triangles"], "passed": ok}
        passed = passed and ok
    return {"passed": passed, "checks": checks}


def run(job: dict[str, Any], result_path: Path) -> dict[str, Any]:
    import bpy

    source, output_dir, output_name, budgets, options = _check_job(job, result_path)
    output_dir.mkdir(parents=True, exist_ok=True)
    output = _real(output_dir / f"{output_name}.glb")
    warnings: list[str] = []
    bpy.ops.wm.read_factory_settings(use_empty=True)
    forward = _axis(job.get("source_forward_axis", "-Z"), "source_forward_axis")
    up = _axis(job.get("source_up_axis", "Y"), "source_up_axis")
    if forward.lstrip("-") == up.lstrip("-"):
        raise JobError("source forward and up axes must differ")
    _import_asset(bpy, source, forward, up)
    if not _meshes(bpy):
        raise JobError("import produced no mesh objects")
    mesh_parts = options.get("mesh_parts", [])
    if not isinstance(mesh_parts, list):
        raise JobError("options.mesh_parts must be a list")
    attached_parts = _attach_mesh_parts(bpy, mesh_parts, forward, up) if mesh_parts else []
    parent_overrides = options.get("bone_parent_overrides", {})
    remove_bones = options.get("remove_unweighted_bones", [])
    if not isinstance(parent_overrides, dict) or not all(
        isinstance(child, str) and isinstance(parent, str) for child, parent in parent_overrides.items()
    ) or not isinstance(remove_bones, list) or not all(isinstance(name, str) for name in remove_bones):
        raise JobError("bone_parent_overrides and remove_unweighted_bones have invalid types")
    skeleton_repair = _repair_skeleton(bpy, parent_overrides, remove_bones)
    before = _metrics(bpy)
    animation_transfer = None
    animation_source = options.get("animation_source_path")
    if animation_source is not None:
        if not isinstance(animation_source, str) or not animation_source:
            raise JobError("options.animation_source_path must be a file path")
        animation_path = _real(Path(animation_source))
        if not animation_path.is_file() or animation_path.suffix.lower() not in SUPPORTED_EXTENSIONS:
            raise JobError("options.animation_source_path must name a supported asset file")
        transfer_map = options.get("animation_bone_map", {})
        if not isinstance(transfer_map, dict) or not all(
            isinstance(old, str) and isinstance(new, str) for old, new in transfer_map.items()
        ):
            raise JobError("options.animation_bone_map must be a string map")
        transfer_mode = str(options.get("animation_transfer_mode", "rebind"))
        clip_names = options.get("animation_clip_names", {})
        if not isinstance(clip_names, dict) or not all(
            isinstance(old, str) and isinstance(new, str) for old, new in clip_names.items()
        ):
            raise JobError("options.animation_clip_names must be a string map")
        animation_transfer = _attach_animation_source(
            bpy, animation_path, forward, up, transfer_map, transfer_mode, clip_names
        )
    allowed_files = {Path(item["path"]).resolve() for item in job.get("input_bindings", [])}
    for image in bpy.data.images:
        raw_path = str(getattr(image, "filepath", ""))
        if not raw_path or getattr(image, "packed_file", None):
            continue
        resolved_image = Path(bpy.path.abspath(raw_path)).resolve()
        if resolved_image not in allowed_files:
            raise JobError(f"model references an image outside bound staging inputs: {raw_path}")
    scale = _require_number(job, "unit_scale_meters", 1.0)
    _normalize_transforms(bpy, scale)
    if options.get("triangulate", True):
        _triangulate(bpy)
    consolidated = _consolidate_materials(bpy) if options.get("consolidate_materials", False) else 0
    maximum = options.get("texture_max_dimension")
    if maximum is not None and (isinstance(maximum, bool) or not isinstance(maximum, int)):
        raise JobError("options.texture_max_dimension must be an integer")
    textures = _process_textures(bpy, maximum, options.get("texture_format"))
    influences = options.get("max_influences", 4)
    if isinstance(influences, bool) or not isinstance(influences, int):
        raise JobError("options.max_influences must be an integer")
    weighted_vertices = _limit_skin_weights(bpy, influences)
    bone_map = options.get("bone_map", {})
    required = options.get("required_bones", [])
    if not isinstance(bone_map, dict) or not isinstance(required, list) or not all(isinstance(v, str) for v in required):
        raise JobError("options.bone_map must be an object and required_bones a string list")
    if required and not bone_map:
        raise JobError("required bones need an explicit bone_map")
    bones_renamed = _rename_bones(bpy, bone_map, required) if bone_map or required else 0
    renames = options.get("animation_renames", {})
    trims = options.get("animation_trims", {})
    if not isinstance(renames, dict) or not isinstance(trims, dict):
        raise JobError("animation_renames and animation_trims must be objects")
    animations = _process_animations(bpy, renames, trims)
    animation_aliases = options.get("animation_aliases", {})
    if not isinstance(animation_aliases, dict) or not all(
        isinstance(alias, str) and isinstance(source_name, str)
        for alias, source_name in animation_aliases.items()
    ):
        raise JobError("options.animation_aliases must be a string map")
    aliases = _duplicate_animation_aliases(bpy, animation_aliases)
    two_handed_idle = _create_two_handed_idle(bpy, options.get("two_handed_idle"))
    attachment_points = options.get("attachment_points", {})
    if not isinstance(attachment_points, dict):
        raise JobError("options.attachment_points must be an object")
    attachments = _create_attachment_points(bpy, attachment_points)
    ratios = options.get("lod_ratios", [])
    if not isinstance(ratios, list):
        raise JobError("options.lod_ratios must be a list")
    lods = _create_lods(bpy, output_name, ratios, budgets)
    proxies = _create_proxies(
        bpy, output_name, bool(options.get("generate_collision", False)), bool(options.get("generate_hitboxes", False))
    )
    _compute_tangents(bpy, warnings)
    preview_animation = options.get("preview_animation")
    if preview_animation is not None:
        if not isinstance(preview_animation, dict) or not isinstance(preview_animation.get("name"), str):
            raise JobError("options.preview_animation needs a name and optional frame")
        action = bpy.data.actions.get(preview_animation["name"])
        rigs = [obj for obj in _scene_objects(bpy) if obj.type == "ARMATURE"]
        if action is None or len(rigs) != 1:
            raise JobError("preview animation or armature not found")
        rigs[0].animation_data_create()
        rigs[0].animation_data.action = action
        bpy.context.scene.frame_set(int(preview_animation.get("frame", action.frame_range[0])))
    views = options.get("preview_views", [])
    if not isinstance(views, list):
        raise JobError("options.preview_views must be a list")
    previews = _render_previews(bpy, output_dir, output_name, views)
    animation_stills = []
    still_requests = options.get("animation_preview_stills", [])
    if not isinstance(still_requests, list):
        raise JobError("options.animation_preview_stills must be a list")
    rigs = [obj for obj in _scene_objects(bpy) if obj.type == "ARMATURE"]
    for still in still_requests:
        if not isinstance(still, dict) or not isinstance(still.get("name"), str):
            raise JobError("each animation preview still needs name, action, and frame")
        still_name = _safe_still_name(still["name"])
        action = bpy.data.actions.get(str(still.get("action", "")))
        if action is None or len(rigs) != 1:
            raise JobError(f"animation preview action not found: {still.get('action')}")
        rigs[0].animation_data_create()
        rigs[0].animation_data.action = action
        bpy.context.scene.frame_set(int(still.get("frame", action.frame_range[0])))
        animation_stills.extend(_render_previews(bpy, output_dir, still_name, ["iso"]))
    after = _metrics(bpy)
    budget = _budget_result(after, budgets, lods)
    if not budget["passed"]:
        raise JobError("processed asset exceeds one or more explicit budgets")
    _purge(bpy)
    _export_glb(bpy, output)
    return {
        "status": "ok", "tool": TOOL_NAME, "version": bpy.app.version_string,
        "asset_type": job["asset_type"], "before": before, "after": after,
        "outputs": {"glb": str(output), "previews": previews, "animation_stills": animation_stills},
        "warnings": warnings, "errors": [],
        "budget_result": budget, "visual_review_required": True,
        "processing": {"materials_consolidated": consolidated, "weighted_vertices_limited": weighted_vertices,
                       "bones_renamed": bones_renamed, "animations_trimmed": animations, "textures": textures,
                       "animation_transfer": animation_transfer, "animation_aliases": aliases,
                       "two_handed_idle": two_handed_idle,
                       "mesh_parts": attached_parts, "skeleton_repair": skeleton_repair,
                       "attachment_points": attachments,
                       "lods": lods, "proxies": proxies},
    }


def main() -> int:
    args = _parse_args()
    result_path = _real(Path(args.result))
    result: dict[str, Any]
    try:
        job = _load_request(args.request)
        bindings = job.get("input_bindings", [])
        if not isinstance(bindings, list) or not bindings:
            raise JobError("input_bindings must seal every processing input")
        for binding in bindings:
            if not isinstance(binding, dict) or not isinstance(binding.get("path"), str) or not isinstance(binding.get("sha256"), str):
                raise JobError("each input binding needs path and sha256")
            digest = hashlib.sha256(Path(binding["path"]).read_bytes()).hexdigest()
            if digest != binding["sha256"]:
                raise JobError(f"processing input hash changed: {binding['path']}")
        result = run(job, result_path)
        result["input_bindings"] = bindings
        code = 0
    except Exception as exc:
        try:
            import bpy
            version = bpy.app.version_string
        except Exception:
            version = "unavailable"
        result = {
            "status": "error", "tool": TOOL_NAME, "version": version,
            "before": {}, "after": {}, "outputs": {}, "warnings": [],
            "errors": [str(exc)], "budget_result": {"passed": False, "checks": {}},
            "visual_review_required": True,
        }
        if not isinstance(exc, (JobError, ValueError, OSError, json.JSONDecodeError)):
            result["errors"].append(traceback.format_exc())
        code = 1
    result_path.parent.mkdir(parents=True, exist_ok=True)
    result_path.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return code


if __name__ == "__main__":
    raise SystemExit(main())
