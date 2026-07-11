"""Author the first directional duelist animation pack in Blender 5.x.

Run from the repository root:
  blender assets/models/lg_duelist_male_v3/art/blender/lg_duelist_male.blend \
    --background --python tools/author_duelist_presentation_clips.py

The script derives deliberately restrained authored clips from the existing
hand-tuned RUN, IDLE, and LAND actions, saves the source blend, and exports the
runtime GLB. Re-running it replaces only the generated actions by name.
"""

from __future__ import annotations

import json
import math
from pathlib import Path

import bpy


ROOT = Path(bpy.path.abspath("//")).parents[4]
BLEND_PATH = ROOT / "assets/models/lg_duelist_male_v3/art/blender/lg_duelist_male.blend"
GLB_PATH = ROOT / "assets/models/lg_duelist_male_v3/art/exports/lg_duelist_male.glb"
REPORT_PATH = ROOT / "assets/models/lg_duelist_male_v3/art/exports/presentation_clips.json"

GENERATED = (
    "RUN_BACK",
    "STRAFE_LEFT",
    "STRAFE_RIGHT",
    "START_FORWARD",
    "STOP_FORWARD",
    "LAND_LIGHT",
    "LAND_HEAVY",
)


def channel_bag(action: bpy.types.Action):
    if not action.slots or not action.layers or not action.layers[0].strips:
        raise RuntimeError(f"Action {action.name} has no Blender 5 channel bag")
    return action.layers[0].strips[0].channelbag(action.slots[0])


def curves(action: bpy.types.Action):
    return list(channel_bag(action).fcurves)


def find_curve(action: bpy.types.Action, bone: str, prop: str, axis: int):
    path = f'pose.bones["{bone}"].{prop}'
    return next(
        (curve for curve in curves(action)
         if curve.data_path == path and curve.array_index == axis),
        None,
    )


def replace_copy(source_name: str, target_name: str):
    existing = bpy.data.actions.get(target_name)
    if existing is not None:
        bpy.data.actions.remove(existing)
    action = bpy.data.actions[source_name].copy()
    action.name = target_name
    action.use_fake_user = True
    return action


def shift_curve(action, bone: str, prop: str, axis: int, amount_fn):
    curve = find_curve(action, bone, prop, axis)
    if curve is None:
        raise RuntimeError(f"Missing curve {bone}.{prop}[{axis}] in {action.name}")
    start, end = action.frame_range
    extent = max(1.0, end - start)
    for key in curve.keyframe_points:
        t = (key.co.x - start) / extent
        amount = amount_fn(t)
        key.co.y += amount
        key.handle_left.y += amount
        key.handle_right.y += amount


def scale_curve(action, bone: str, prop: str, axis: int, factor: float):
    curve = find_curve(action, bone, prop, axis)
    if curve is None:
        raise RuntimeError(f"Missing curve {bone}.{prop}[{axis}] in {action.name}")
    for key in curve.keyframe_points:
        key.co.y *= factor
        key.handle_left.y *= factor
        key.handle_right.y *= factor


def reverse_time(action):
    start, end = action.frame_range
    for curve in curves(action):
        for key in curve.keyframe_points:
            key.co.x = start + end - key.co.x
            old_left = key.handle_left.x
            key.handle_left.x = start + end - key.handle_right.x
            key.handle_right.x = start + end - old_left
        curve.keyframe_points.sort()
        curve.update()


def remap_time(action, new_end: float):
    start, end = action.frame_range
    scale = (new_end - 1.0) / max(1.0, end - start)
    for curve in curves(action):
        for key in curve.keyframe_points:
            key.co.x = 1.0 + (key.co.x - start) * scale
            key.handle_left.x = 1.0 + (key.handle_left.x - start) * scale
            key.handle_right.x = 1.0 + (key.handle_right.x - start) * scale
        curve.update()
    action.use_frame_range = True
    action.frame_start = 1.0
    action.frame_end = new_end


def blend_toward_idle(action, stopping: bool):
    idle = bpy.data.actions["IDLE"]
    start, end = action.frame_range
    extent = max(1.0, end - start)
    for curve in curves(action):
        idle_curve = next(
            (candidate for candidate in curves(idle)
             if candidate.data_path == curve.data_path
             and candidate.array_index == curve.array_index),
            None,
        )
        if idle_curve is None:
            continue
        idle_value = idle_curve.evaluate(idle.frame_range[0])
        for key in curve.keyframe_points:
            t = (key.co.x - start) / extent
            smooth = t * t * (3.0 - 2.0 * t)
            run_weight = 1.0 - smooth if stopping else smooth
            value = idle_value + (key.co.y - idle_value) * run_weight
            delta = value - key.co.y
            key.co.y = value
            key.handle_left.y += delta
            key.handle_right.y += delta


def author_backpedal():
    action = replace_copy("RUN", "RUN_BACK")
    reverse_time(action)
    shift_curve(action, "pelvis", "rotation_euler", 0, lambda _t: -0.055)
    shift_curve(action, "spine_01", "rotation_euler", 0, lambda _t: 0.13)
    shift_curve(action, "spine_02", "rotation_euler", 0, lambda _t: 0.08)


def author_strafe(name: str, direction: float):
    action = replace_copy("RUN", name)
    # Turn the lower body toward travel, then counter-twist the torso so aim
    # remains readable. The legs exchange some sagittal swing for lateral swing.
    shift_curve(action, "pelvis", "rotation_euler", 1, lambda _t: direction * 0.48)
    shift_curve(action, "spine_01", "rotation_euler", 1, lambda _t: -direction * 0.30)
    shift_curve(action, "spine_02", "rotation_euler", 1, lambda _t: -direction * 0.18)
    for bone in ("thigh_l", "thigh_r"):
        source = find_curve(action, bone, "rotation_euler", 0)
        lateral = find_curve(action, bone, "rotation_euler", 2)
        if source is None or lateral is None:
            raise RuntimeError(f"Missing thigh curves in {name}")
        samples = [(key.co.x, source.evaluate(key.co.x)) for key in lateral.keyframe_points]
        scale_curve(action, bone, "rotation_euler", 0, 0.48)
        for key, (_, forward_swing) in zip(lateral.keyframe_points, samples):
            amount = direction * forward_swing * 0.72
            key.co.y += amount
            key.handle_left.y += amount
            key.handle_right.y += amount
    shift_curve(
        action,
        "pelvis",
        "location",
        0,
        lambda t: direction * (0.018 + 0.025 * math.sin(t * math.tau)),
    )


def author_start_stop():
    start = replace_copy("RUN", "START_FORWARD")
    remap_time(start, 12.0)
    blend_toward_idle(start, stopping=False)
    stop = replace_copy("RUN", "STOP_FORWARD")
    reverse_time(stop)
    remap_time(stop, 12.0)
    blend_toward_idle(stop, stopping=True)
    shift_curve(stop, "spine_01", "rotation_euler", 0, lambda t: -0.08 * math.sin(math.pi * t))


def author_landings():
    replace_copy("LAND", "LAND_LIGHT")
    heavy = replace_copy("LAND", "LAND_HEAVY")
    impact = lambda t: math.sin(math.pi * min(1.0, max(0.0, t)))
    shift_curve(heavy, "pelvis", "location", 2, lambda t: -0.105 * impact(t))
    shift_curve(heavy, "spine_01", "rotation_euler", 0, lambda t: -0.17 * impact(t))
    shift_curve(heavy, "spine_02", "rotation_euler", 0, lambda t: 0.10 * impact(t))
    for bone in ("thigh_l", "thigh_r"):
        shift_curve(heavy, bone, "rotation_euler", 0, lambda t: 0.12 * impact(t))


def export_runtime(armature):
    bpy.ops.object.select_all(action="DESELECT")
    def descends_from(obj, ancestor):
        parent = obj.parent
        while parent is not None:
            if parent == ancestor:
                return True
            parent = parent.parent
        return False

    selected = list(
        obj for obj in bpy.data.objects
        if obj == armature
        or descends_from(obj, armature)
        or any(
            modifier.type == "ARMATURE" and modifier.object == armature
            for modifier in getattr(obj, "modifiers", [])
        )
    )
    for obj in selected:
        if not obj.hide_render:
            obj.select_set(True)
    bpy.context.view_layer.objects.active = armature
    GLB_PATH.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.export_scene.gltf(
        filepath=str(GLB_PATH),
        export_format="GLB",
        use_selection=True,
        export_animations=True,
        export_animation_mode="ACTIONS",
        export_nla_strips=False,
        export_skins=True,
        export_morph=False,
        export_cameras=False,
        export_lights=False,
    )


def main():
    armature = bpy.data.objects.get("LGDuelist_Male_Armature")
    if armature is None or armature.type != "ARMATURE":
        raise RuntimeError("Primary duelist armature not found")
    required = {"RUN", "IDLE", "LAND"}
    missing = sorted(required - set(bpy.data.actions.keys()))
    if missing:
        raise RuntimeError(f"Missing source actions: {missing}")

    author_backpedal()
    author_strafe("STRAFE_LEFT", -1.0)
    author_strafe("STRAFE_RIGHT", 1.0)
    author_start_stop()
    author_landings()

    bpy.ops.wm.save_as_mainfile(filepath=str(BLEND_PATH))
    export_runtime(armature)
    report = {
        "revision": "presentation_directional_v1",
        "generated_actions": [
            {
                "name": name,
                "frame_range": list(bpy.data.actions[name].frame_range),
            }
            for name in GENERATED
        ],
        "deferred": [
            "turn-in-place until lower-body yaw decoupling exists",
            "weapon-family ready and additive fire until weapon pose/event hooks exist",
        ],
    }
    REPORT_PATH.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report))


if __name__ == "__main__":
    main()
