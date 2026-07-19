#!/usr/bin/env python3
"""Render consistent review thumbnails for every model in one folder."""

from __future__ import annotations

import argparse
import json
import pathlib
import subprocess
import sys


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--blender", type=pathlib.Path, required=True)
    parser.add_argument("--processor", type=pathlib.Path, required=True)
    parser.add_argument("--input-dir", type=pathlib.Path, required=True)
    parser.add_argument("--output-dir", type=pathlib.Path, required=True)
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    failures: list[str] = []
    for source in sorted(args.input_dir.glob("*.fbx")):
        name = source.stem.lower()
        output = args.output_dir / name
        output.mkdir(parents=True, exist_ok=True)
        request = {
            "input_path": str(source.resolve()),
            "output_dir": str(output.resolve()),
            "asset_type": "player_model",
            "output_name": name,
            "unit_scale_meters": 1.0,
            "source_up_axis": "Y",
            "source_forward_axis": "-Z",
            "budgets": {
                "max_vertices": 30000, "max_triangles": 50000,
                "max_materials": 4, "max_texture_dimension": 2048,
                "max_bones": 96, "max_influences_per_vertex": 4,
                "max_animations": 32,
            },
            "options": {
                "triangulate": True, "consolidate_materials": False,
                "texture_max_dimension": 2048, "lod_ratios": [],
                "max_influences": 4, "animation_renames": {},
                "animation_trims": {}, "bone_map": {}, "required_bones": [],
                "generate_collision": False, "generate_hitboxes": False,
                "preview_views": ["front", "iso"],
            },
        }
        request_path = output / "request.json"
        result_path = output / "report.json"
        request_path.write_text(json.dumps(request, indent=2) + "\n", encoding="utf-8")
        result = subprocess.run(
            [str(args.blender), "--background", "--factory-startup", "--disable-autoexec",
             "--python", str(args.processor.resolve()), "--", str(request_path),
             "--result", str(result_path)],
            capture_output=True, text=True, check=False,
        )
        if result.returncode:
            failures.append(source.name)
            print(result.stdout, file=sys.stderr)
            print(result.stderr, file=sys.stderr)

    if failures:
        print("Gallery failures: " + ", ".join(failures), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
