"""Set up Blender's UI and start playback for the Worker review file."""

import bpy


def configure() -> None:
    rig = next((obj for obj in bpy.context.scene.objects if obj.type == "ARMATURE"), None)
    if rig is None:
        return
    bpy.ops.object.select_all(action="DESELECT")
    rig.select_set(True)
    bpy.context.view_layer.objects.active = rig
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type == "VIEW_3D":
                for space in area.spaces:
                    if space.type == "VIEW_3D":
                        space.region_3d.view_distance = 4.0
                        space.region_3d.view_location = (0.0, 0.0, 0.9)
            elif area.type == "DOPESHEET_EDITOR":
                area.spaces.active.ui_mode = "ACTION"
    if not bpy.context.screen.is_animation_playing:
        bpy.ops.screen.animation_play()


bpy.app.timers.register(configure, first_interval=0.5)
