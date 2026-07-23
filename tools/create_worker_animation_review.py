"""Create an interactive Blender review file from the processed Worker GLB."""

from __future__ import annotations

import sys
from pathlib import Path

import bpy


def main() -> None:
    args = sys.argv[sys.argv.index("--") + 1:]
    source = Path(args[0]).resolve()
    output = Path(args[1]).resolve()
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=str(source))

    for obj in list(bpy.context.scene.objects):
        if obj.type == "MESH" and not obj.name.startswith("Worker_"):
            bpy.data.objects.remove(obj, do_unlink=True)
    rigs = [obj for obj in bpy.context.scene.objects if obj.type == "ARMATURE"]
    if len(rigs) != 1:
        raise RuntimeError("expected one Worker armature")
    rig = rigs[0]
    action = bpy.data.actions.get("RUN")
    if action is None:
        raise RuntimeError("RUN action is missing")
    rig.animation_data_create()
    rig.animation_data.action = action
    bpy.context.scene.frame_start = int(action.frame_range[0])
    bpy.context.scene.frame_end = int(action.frame_range[1])
    bpy.context.scene.frame_set(bpy.context.scene.frame_start)
    bpy.context.scene.render.fps = 30
    bpy.context.scene.render.fps_base = 1.0
    bpy.context.scene["review_note"] = (
        "Press Space to play/pause. Use Dope Sheet > Action Editor to select another clip."
    )
    bpy.ops.object.select_all(action="DESELECT")
    rig.select_set(True)
    bpy.context.view_layer.objects.active = rig
    output.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(output), check_existing=False)


if __name__ == "__main__":
    main()
