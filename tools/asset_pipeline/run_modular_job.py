#!/usr/bin/env python3
"""Run the fixed Blender pipeline with strict direct-action transfer for matched rigs."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[2]
CORE = ROOT / "tools" / "asset_pipeline_blender.py"
SPEC = importlib.util.spec_from_file_location("lg_asset_pipeline_core", CORE)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load the fixed asset pipeline")
core = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(core)

POSE_SIGNATURES = {}
GAMEPLAY_JUMP_FACT = None


def _job_options():
    values = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if not values:
        raise RuntimeError("missing modular job request")
    job = json.loads(Path(values[0]).read_text(encoding="utf-8"))
    return job.get("options", {})


def _transfer_actions(bpy, source, forward, up, bone_map, mode, clip_names):
    if mode != "retarget":
        return ORIGINAL_TRANSFER(bpy, source, forward, up, bone_map, mode, clip_names)
    target_rigs = [obj for obj in core._scene_objects(bpy) if obj.type == "ARMATURE"]
    if len(target_rigs) != 1:
        raise core.JobError("animation transfer needs exactly one mesh armature")
    target = target_rigs[0]
    target_bones = {bone.name for bone in target.data.bones}
    objects_before = set(bpy.data.objects)
    actions_before = set(bpy.data.actions)
    core._import_asset(bpy, source, forward, up)
    new_objects = [obj for obj in bpy.data.objects if obj not in objects_before]
    source_rigs = [obj for obj in new_objects if obj.type == "ARMATURE"]
    if len(source_rigs) != 1:
        raise core.JobError("animation source needs exactly one armature")
    source_rig = source_rigs[0]
    source_bones = {bone.name for bone in source_rig.data.bones}
    new_actions = [action for action in bpy.data.actions if action not in actions_before]
    if not new_actions:
        raise core.JobError("animation source has no actions")
    unknown_aliases = sorted(set(bone_map) - target_bones)
    if unknown_aliases:
        raise core.JobError("retarget alias names unknown target bones: " + ", ".join(unknown_aliases))
    missing_sources = sorted(name for name in target_bones if bone_map.get(name, name) not in source_bones)
    if missing_sources:
        raise core.JobError("retarget source lacks target bones or aliases: " + ", ".join(missing_sources))
    for target_name in sorted(target_bones):
        source_name = bone_map.get(target_name, target_name)
        rotation = target.pose.bones[target_name].constraints.new("COPY_ROTATION")
        rotation.name = "LG modular retarget rotation"
        rotation.target = source_rig
        rotation.subtarget = source_name
        rotation.owner_space = "POSE"
        rotation.target_space = "POSE"
        rotation.mix_mode = "REPLACE"
    location = target.pose.bones["Root"].constraints.new("COPY_LOCATION")
    location.name = "LG modular retarget root location"
    location.target = source_rig
    location.subtarget = bone_map.get("Root", "Root")
    location.owner_space = "POSE"
    location.target_space = "POSE"
    target.animation_data_create()
    source_rig.animation_data_create()
    source_tracks = [(track, track.mute) for track in source_rig.animation_data.nla_tracks]
    for track, _ in source_tracks:
        track.mute = True
    baked_actions = []
    core._select(bpy, [target], target)
    bpy.ops.object.mode_set(mode="POSE")
    bpy.ops.pose.select_all(action="SELECT")
    for source_action in sorted(new_actions, key=lambda item: item.name):
        _bind_action(source_rig, source_action)
        target.animation_data.action = None
        start = int(source_action.frame_range[0])
        end = int(source_action.frame_range[1])
        bpy.context.scene.frame_start = start
        bpy.context.scene.frame_end = end
        result = bpy.ops.nla.bake(
            frame_start=start, frame_end=end, step=1, only_selected=True,
            visual_keying=True, clear_constraints=False, clear_parents=False,
            use_current_action=False, clean_curves=True, bake_types={"POSE"},
        )
        if "FINISHED" not in result or target.animation_data.action is None:
            raise core.JobError(f"failed to bake retargeted action: {source_action.name}")
        baked = target.animation_data.action
        source_name = source_action.name.rsplit("|", 1)[-1]
        baked.name = clip_names.get(source_name, source_name)
        baked_actions.append(baked)
        target.animation_data.action = None
    bpy.ops.object.mode_set(mode="OBJECT")
    for track, was_muted in source_tracks:
        track.mute = was_muted
    for pose_bone in target.pose.bones:
        for constraint in list(pose_bone.constraints):
            pose_bone.constraints.remove(constraint)
    source_rig_name = source_rig.name
    removal_names = [obj.name for obj in new_objects if obj != source_rig]
    for action in new_actions:
        bpy.data.actions.remove(action)
    source_object = bpy.data.objects.get(source_rig_name)
    if source_object is not None:
        bpy.data.objects.remove(source_object, do_unlink=True)
    for object_name in removal_names:
        obj = bpy.data.objects.get(object_name)
        if obj is not None:
            bpy.data.objects.remove(obj, do_unlink=True)
    target.animation_data_create()
    for action in baked_actions:
        track = target.animation_data.nla_tracks.new()
        track.name = action.name
        strip = track.strips.new(action.name, int(action.frame_range[0]), action)
        if getattr(action, "slots", None) and hasattr(strip, "action_slot"):
            strip.action_slot = action.slots[0]
    return {
        "source": str(source), "armature": target.name, "bones": len(target_bones),
        "actions_added": len(baked_actions), "target_bones_retargeted": len(target_bones),
        "bone_aliases": dict(sorted(bone_map.items())),
        "vertex_groups_renamed": 0, "mode": mode,
    }


def _rename_target_bones(bpy, bone_map, required):
    armatures = [obj for obj in core._scene_objects(bpy) if obj.type == "ARMATURE"]
    existing = {bone.name for armature in armatures for bone in armature.data.bones}
    missing_required = sorted(name for name in required if name not in bone_map and name not in bone_map.values())
    if missing_required:
        raise core.JobError("required bone mapping absent: " + ", ".join(missing_required))
    missing_sources = sorted(name for name in bone_map if name not in existing)
    if missing_sources:
        raise core.JobError("bone map source not found: " + ", ".join(missing_sources))
    targets = list(bone_map.values())
    if len(targets) != len(set(targets)):
        raise core.JobError("bone map targets must be unique")
    count = 0
    for armature in armatures:
        local = {bone.name for bone in armature.data.bones}
        applicable = {src: dst for src, dst in bone_map.items() if src in local and src != dst}
        conflicts = sorted(dst for src, dst in applicable.items() if dst in local and dst not in applicable)
        if conflicts:
            raise core.JobError("bone map target already exists: " + ", ".join(conflicts))
        temporary = {}
        for index, source_name in enumerate(sorted(applicable)):
            temp = f"__LG_BATCH_RENAME_{index:04d}__"
            armature.data.bones[source_name].name = temp
            temporary[temp] = applicable[source_name]
        for temp, target_name in temporary.items():
            armature.data.bones[temp].name = target_name
            count += 1
    for mesh in core._meshes(bpy, include_generated=False):
        for source_name, target_name in bone_map.items():
            group = mesh.vertex_groups.get(source_name)
            if group and source_name != target_name:
                if mesh.vertex_groups.get(target_name):
                    raise core.JobError(f"vertex group target already exists: {target_name}")
                group.name = target_name
    # Direct actions were mapped to final target names before this rename.
    return count


def _bind_action(rig, action):
    rig.animation_data_create()
    rig.animation_data.action = action
    slots = getattr(action, "slots", None)
    if slots and hasattr(rig.animation_data, "action_slot"):
        rig.animation_data.action_slot = slots[0]


def _create_gameplay_jump(bpy, spec):
    """Create an in-place jump while the game owns world movement."""
    if spec is None:
        return None
    if not isinstance(spec, dict):
        raise core.JobError("options.gameplay_jump must be an object")
    source_name = spec.get("source")
    output_name = spec.get("name")
    frames = spec.get("frames", [1, 5, 10, 16, 22])
    hip_offsets = spec.get("hip_offsets", [0.0, -0.12, 0.04, 0.1, 0.04])
    if not isinstance(source_name, str) or not source_name or not isinstance(output_name, str) or not output_name:
        raise core.JobError("gameplay_jump needs non-empty source and name values")
    if bpy.data.actions.get(output_name) is not None:
        raise core.JobError(f"gameplay_jump output already exists: {output_name}")
    if (
        not isinstance(frames, list) or len(frames) != 5 or
        not all(isinstance(frame, int) and frame >= 0 for frame in frames) or
        frames != sorted(set(frames))
    ):
        raise core.JobError("gameplay_jump.frames must be five increasing whole frames")
    if (
        not isinstance(hip_offsets, list) or len(hip_offsets) != 5 or
        not all(isinstance(value, (int, float)) for value in hip_offsets)
    ):
        raise core.JobError("gameplay_jump.hip_offsets must contain five numbers")
    source = bpy.data.actions.get(source_name)
    rigs = [obj for obj in core._scene_objects(bpy) if obj.type == "ARMATURE"]
    if source is None or len(rigs) != 1:
        raise core.JobError("gameplay_jump source or armature not found")
    rig = rigs[0]
    leg_names = ("Hips", "UpperLeg.L", "LowerLeg.L", "Foot.L", "UpperLeg.R", "LowerLeg.R", "Foot.R")
    missing = [name for name in leg_names if name not in rig.pose.bones]
    if missing:
        raise core.JobError("gameplay_jump bones are missing: " + ", ".join(missing))

    from mathutils import Matrix, Quaternion

    rig.animation_data_create()
    muted = [(track, track.mute) for track in rig.animation_data.nla_tracks]
    for track, _ in muted:
        track.mute = True
    _bind_action(rig, source)
    sample_frame = int(round(sum(source.frame_range) * 0.5))
    bpy.context.scene.frame_set(sample_frame)
    bpy.context.view_layer.update()
    base = {bone.name: bone.matrix_basis.copy() for bone in rig.pose.bones}

    action = bpy.data.actions.new(output_name)
    _bind_action(rig, action)
    # Five clear poses: ready, compression, launch, peak, and airborne hand-off.
    leg_angles = [
        (0.0, 0.0, 0.0, 0.0),
        (-45.0, 80.0, -38.0, 72.0),
        (8.0, -12.0, -10.0, 18.0),
        (-15.0, 30.0, 10.0, -22.0),
        (-8.0, 18.0, 5.0, -12.0),
    ]
    for pose_index, frame in enumerate(frames):
        for bone in rig.pose.bones:
            matrix = base[bone.name]
            bone.rotation_mode = "QUATERNION"
            bone.location = matrix.to_translation()
            bone.rotation_quaternion = matrix.to_quaternion()
            bone.scale = matrix.to_scale()
        hips = rig.pose.bones["Hips"]
        # Drop the body in armature space; the hip bone's local axes vary by rig.
        hips.matrix = Matrix.Translation((0.0, 0.0, float(hip_offsets[pose_index]))) @ hips.matrix
        left_upper, left_lower, right_upper, right_lower = leg_angles[pose_index]
        for name, degrees in (
            ("UpperLeg.L", left_upper), ("LowerLeg.L", left_lower),
            ("UpperLeg.R", right_upper), ("LowerLeg.R", right_lower),
        ):
            bone = rig.pose.bones[name]
            bone.rotation_quaternion = bone.rotation_quaternion @ Quaternion((1.0, 0.0, 0.0), core.math.radians(degrees))
        for bone in rig.pose.bones:
            bone.keyframe_insert(data_path="location", frame=frame, group=bone.name)
            bone.keyframe_insert(data_path="rotation_quaternion", frame=frame, group=bone.name)
            bone.keyframe_insert(data_path="scale", frame=frame, group=bone.name)
    track = rig.animation_data.nla_tracks.new()
    track.name = output_name
    strip = track.strips.new(output_name, frames[0], action)
    if getattr(action, "slots", None) and hasattr(strip, "action_slot"):
        strip.action_slot = action.slots[0]
    for existing, was_muted in muted:
        existing.mute = was_muted
    rig.animation_data.action = None
    return {
        "name": output_name,
        "source_pose": source_name,
        "frame_range": [frames[0], frames[-1]],
        "in_place": True,
        "source_roll_preserved_as": "Roll",
    }


def _create_two_handed_idle_and_jump(bpy, spec):
    global GAMEPLAY_JUMP_FACT
    result = ORIGINAL_TWO_HANDED(bpy, spec)
    GAMEPLAY_JUMP_FACT = _create_gameplay_jump(bpy, _job_options().get("gameplay_jump"))
    return result


def _pose_signature(bpy, rig):
    bpy.context.view_layer.update()
    values = []
    for bone in sorted(rig.pose.bones, key=lambda item: item.name):
        matrix = bone.matrix
        values.extend(round(float(matrix[row][column]), 5) for row in range(4) for column in range(4))
    return tuple(values)


def _render_action_alone(bpy, output_dir, output_name, views):
    rigs = [obj for obj in core._scene_objects(bpy) if obj.type == "ARMATURE"]
    if len(rigs) != 1 or rigs[0].animation_data is None or rigs[0].animation_data.action is None:
        return ORIGINAL_RENDER(bpy, output_dir, output_name, views)
    rig = rigs[0]
    action = rig.animation_data.action
    muted = [(track, track.mute) for track in rig.animation_data.nla_tracks]
    for track, _ in muted:
        track.mute = True
    try:
        _bind_action(rig, action)
        POSE_SIGNATURES[action.name] = _pose_signature(bpy, rig)
        return ORIGINAL_RENDER(bpy, output_dir, output_name, views)
    finally:
        for track, was_muted in muted:
            track.mute = was_muted


def _run_with_pose_gate(job, result_path):
    result = ORIGINAL_RUN(job, result_path)
    required = ("IDLE", "RUN", "Idle_Gun_TwoHanded")
    missing = [name for name in required if name not in POSE_SIGNATURES]
    if missing:
        raise core.JobError("pose validation did not sample: " + ", ".join(missing))
    duplicate_pairs = [
        f"{left}={right}"
        for index, left in enumerate(required)
        for right in required[index + 1:]
        if POSE_SIGNATURES[left] == POSE_SIGNATURES[right]
    ]
    if duplicate_pairs:
        raise core.JobError("representative animation poses are identical: " + ", ".join(duplicate_pairs))
    result["processing"]["pose_validation"] = {
        "passed": True,
        "actions": list(required),
        "distinct_pairs": 3,
    }
    if GAMEPLAY_JUMP_FACT is not None:
        result["processing"]["gameplay_jump"] = GAMEPLAY_JUMP_FACT
    return result


ORIGINAL_TRANSFER = core._attach_animation_source
ORIGINAL_RENDER = core._render_previews
ORIGINAL_TWO_HANDED = core._create_two_handed_idle
ORIGINAL_RUN = core.run
core._attach_animation_source = _transfer_actions
core._rename_bones = _rename_target_bones
core._render_previews = _render_action_alone
core._create_two_handed_idle = _create_two_handed_idle_and_jump
core.run = _run_with_pose_gate

if __name__ == "__main__":
    raise SystemExit(core.main())
