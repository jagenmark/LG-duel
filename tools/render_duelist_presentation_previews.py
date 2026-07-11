"""Render compact workbench previews for authored duelist presentation clips."""

from pathlib import Path

import bpy


ROOT = Path(bpy.path.abspath("//")).parents[4]
OUTPUT = ROOT / "build/animation_previews"
CLIPS = {
    "RUN_BACK": (1, 7, 13, 19),
    "STRAFE_LEFT": (1, 7, 13, 19),
    "STRAFE_RIGHT": (1, 7, 13, 19),
    "START_FORWARD": (1, 6, 12),
    "STOP_FORWARD": (1, 6, 12),
    "LAND_LIGHT": (1, 5, 10),
    "LAND_HEAVY": (1, 5, 10),
}


def main():
    armature = bpy.data.objects["LGDuelist_Male_Armature"]
    armature.animation_data_create()
    scene = bpy.context.scene
    scene.camera = bpy.data.objects["camera_three_quarter_front"]
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.render.resolution_x = 360
    scene.render.resolution_y = 360
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.film_transparent = False
    scene.display.shading.light = "STUDIO"
    scene.display.shading.show_shadows = True
    scene.display.shading.show_cavity = True
    scene.display.shading.cavity_type = "WORLD"
    OUTPUT.mkdir(parents=True, exist_ok=True)
    for clip, frames in CLIPS.items():
        action = bpy.data.actions.get(clip)
        if action is None:
            raise RuntimeError(f"Missing action {clip}")
        armature.animation_data.action = action
        for frame in frames:
            scene.frame_set(frame)
            scene.render.filepath = str(OUTPUT / f"{clip}_{frame:02d}.png")
            bpy.ops.render.render(write_still=True)
    print(f"Rendered {sum(map(len, CLIPS.values()))} previews to {OUTPUT}")


if __name__ == "__main__":
    main()
