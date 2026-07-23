#!/usr/bin/env python3
"""Check one modular character armature and bind pose against Worker in Blender."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import sys
import traceback
from typing import Any


def _args() -> argparse.Namespace:
    values = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("request", type=Path)
    parser.add_argument("--result", type=Path, required=True)
    return parser.parse_args(values)


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _import_fbx(bpy: Any, path: Path) -> tuple[Any, list[Any], list[str]]:
    before = set(bpy.data.objects)
    result = bpy.ops.import_scene.fbx(filepath=str(path), axis_forward="-Z", axis_up="Y")
    if "FINISHED" not in result:
        raise RuntimeError(f"Blender failed to import {path.name}")
    added = [obj for obj in bpy.data.objects if obj not in before]
    rigs = [obj for obj in added if obj.type == "ARMATURE"]
    meshes = [obj for obj in added if obj.type == "MESH"]
    if len(rigs) != 1 or not meshes:
        raise RuntimeError(f"{path.name} needs exactly one armature and at least one mesh")
    materials = sorted({slot.material.name for mesh in meshes for slot in mesh.material_slots if slot.material})
    return rigs[0], meshes, materials


def _bone_rows(rig: Any) -> list[dict[str, Any]]:
    rows = []
    for bone in sorted(rig.data.bones, key=lambda item: item.name):
        rows.append({
            "name": bone.name,
            "parent": bone.parent.name if bone.parent else None,
            "matrix": [[float(bone.matrix_local[row][column]) for column in range(4)] for row in range(4)],
        })
    return rows


def run(request: dict[str, Any]) -> dict[str, Any]:
    import bpy

    source = Path(request["input_path"]).resolve()
    reference = Path(request["rig_reference_path"]).resolve()
    bindings = request.get("input_bindings", [])
    sealed = {str(Path(item["path"]).resolve()): item["sha256"] for item in bindings if isinstance(item, dict)}
    for path in (source, reference):
        if str(path) not in sealed or _sha256(path) != sealed[str(path)]:
            raise RuntimeError(f"rig input is not sealed or its hash changed: {path}")
    tolerance = request.get("rig_matrix_tolerance", 0.0001)
    if isinstance(tolerance, bool) or not isinstance(tolerance, (int, float)) or not math.isfinite(tolerance) or tolerance <= 0:
        raise RuntimeError("rig_matrix_tolerance must be a positive finite number")
    bpy.ops.wm.read_factory_settings(use_empty=True)
    source_rig, meshes, materials = _import_fbx(bpy, source)
    source_rows = _bone_rows(source_rig)
    reference_rig, _, _ = _import_fbx(bpy, reference)
    reference_rows = _bone_rows(reference_rig)
    source_by_name = {row["name"]: row for row in source_rows}
    reference_by_name = {row["name"]: row for row in reference_rows}
    missing = sorted(set(reference_by_name) - set(source_by_name))
    extra = sorted(set(source_by_name) - set(reference_by_name))
    parent_mismatches = sorted(
        name for name in set(source_by_name) & set(reference_by_name)
        if source_by_name[name]["parent"] != reference_by_name[name]["parent"]
    )
    pose_mismatches = []
    largest_delta = 0.0
    for name in sorted(set(source_by_name) & set(reference_by_name)):
        lhs = source_by_name[name]["matrix"]
        rhs = reference_by_name[name]["matrix"]
        delta = max(abs(lhs[row][column] - rhs[row][column]) for row in range(4) for column in range(4))
        largest_delta = max(largest_delta, delta)
        if delta > tolerance:
            pose_mismatches.append(name)
    if missing or extra or parent_mismatches or pose_mismatches:
        raise RuntimeError(
            "Worker rig mismatch: "
            f"missing={missing}, extra={extra}, parents={parent_mismatches}, bind_pose={pose_mismatches}"
        )
    fingerprint = hashlib.sha256(json.dumps(source_rows, sort_keys=True).encode()).hexdigest()
    return {
        "status": "ok", "compatible": True, "source": str(source), "reference": str(reference),
        "bone_count": len(source_rows), "bind_pose_fingerprint": fingerprint,
        "source_bones": sorted(source_by_name),
        "largest_matrix_delta": largest_delta, "matrix_tolerance": tolerance,
        "source_meshes": sorted(mesh.name for mesh in meshes), "source_materials": materials,
    }


def main() -> int:
    args = _args()
    try:
        request = json.loads(args.request.read_text(encoding="utf-8"))
        report = run(request)
        code = 0
    except Exception as exc:
        report = {"status": "error", "compatible": False, "errors": [str(exc)], "traceback": traceback.format_exc()}
        code = 1
    args.result.parent.mkdir(parents=True, exist_ok=True)
    args.result.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return code


if __name__ == "__main__":
    raise SystemExit(main())
