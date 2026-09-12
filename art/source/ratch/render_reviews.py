"""Refresh studio views from the saved Blender candidate without rebuilding it."""
import bpy, math, os
from pathlib import Path
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[3]
PRE=ROOT/'art/previews/ratch'
bpy.ops.wm.open_mainfile(filepath=str(ROOT/'art/source/ratch/ratch.blend'))
scene=bpy.context.scene;cam=scene.camera
def aim(o,target):o.rotation_euler=(Vector(target)-o.location).to_track_quat('-Z','Y').to_euler()
for name,loc,scale,target in [('side_audit',(6,0,1.78),2.42,(0,0,1.09)),('face_side_audit',(6,-.05,1.80),.77,(0,0,1.80)),('front',(0,-6,2.0),2.40,(0,0,1.08)),('three_quarter',(2.8,-6,2.7),2.48,(0,0,1.09)),('back',(0,6,1.7),2.48,(0,0,1.09)),('profile_left',(-6,0,1.8),2.48,(0,0,1.09)),('profile_right',(6,0,1.8),2.48,(0,0,1.09)),('head_profile',(6,-.3,1.85),.76,(0,0,1.80)),('head_low',(1,-6,.8),.80,(0,0,1.79)),('head_high',(1,-5,4),.83,(0,0,1.80)),('portrait',(.75,-6,2.08),.77,(0,-.015,1.82))]:
 if os.environ.get('RATCH_VIEWS') and name not in os.environ['RATCH_VIEWS'].split(','):continue
 cam.location=loc;cam.data.ortho_scale=scale;aim(cam,target);scene.render.filepath=str(PRE/(name+'.png'));bpy.ops.render.render(write_still=True)
