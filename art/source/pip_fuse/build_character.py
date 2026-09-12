"""Original Pip Fuse character. Run with Blender --background --python this_file.
All geometry, palette and animation authored here; no input assets are read.
"""
import bpy, math, json
from pathlib import Path
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[3]
bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)
M={}
for name,color in {'jacket':(.53,.105,.055),'trim':(.94,.57,.12),'pants':(.075,.12,.15),'leather':(.085,.045,.029),'skin':(.69,.36,.19),'nose':(.77,.30,.16),'hair':(.23,.065,.018),'copper':(.68,.24,.045),'cream':(.94,.81,.55),'white':(.95,.94,.81),'iris':(.08,.25,.23),'black':(.016,.012,.009),'steel':(.23,.3,.31)}.items():
 m=bpy.data.materials.new(name); m.diffuse_color=(*color,1); m.use_nodes=True; p=m.node_tree.nodes.get('Principled BSDF'); p.inputs['Base Color'].default_value=(*color,1); p.inputs['Roughness'].default_value=.78; M[name]=m
M['jacket'].name='PipFuse_Jacket'
parts=[]
def finish(o,n,mat,bone):
 o.name=n
 for poly in o.data.polygons:poly.use_smooth=True
 o.data.materials.append(M[mat]); bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
 if bone:
  g=o.vertex_groups.new(name=bone); g.add(list(range(len(o.data.vertices))),1,'REPLACE'); parts.append(o)
 return o
def ell(n,loc,scale,mat,bone,seg=32):
 bpy.ops.mesh.primitive_uv_sphere_add(segments=seg,ring_count=20,location=loc); o=bpy.context.object; o.scale=scale; return finish(o,n,mat,bone)
def box(n,loc,scale,mat,bone,bevel=.04):
 bpy.ops.mesh.primitive_cube_add(size=1,location=loc); o=bpy.context.object; o.scale=scale; bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
 mod=o.modifiers.new('Tailored rounded edges','BEVEL'); mod.width=bevel; mod.segments=4; bpy.context.view_layer.objects.active=o; bpy.ops.object.modifier_apply(modifier=mod.name); return finish(o,n,mat,bone)
def link(n,a,b,r1,r2,mat,bone):
 d=Vector(b)-Vector(a); bpy.ops.mesh.primitive_cone_add(vertices=32,radius1=r1,radius2=r2,depth=d.length,location=(Vector(a)+Vector(b))/2); o=bpy.context.object; o.rotation_euler=d.to_track_quat('Z','Y').to_euler(); return finish(o,n,mat,bone)
def rings(n,rows,mat,bone):
 vs=[]; fs=[]; N=32
 for z,x,y,rx,ry in rows:
  for i in range(N):
   t=i*math.tau/N; vs.append((x+rx*math.cos(t),y+ry*math.sin(t),z))
 for j in range(len(rows)-1):
  for i in range(N): a=j*N+i; b=j*N+(i+1)%N; fs.append((a,b,b+N,a+N))
 fs += [tuple(reversed(range(N))),tuple((len(rows)-1)*N+i for i in range(N))]
 mesh=bpy.data.meshes.new(n); mesh.from_pydata(vs,[],fs); mesh.update(); o=bpy.data.objects.new(n,mesh); bpy.context.collection.objects.link(o); bpy.context.view_layer.objects.active=o; o.select_set(True)
 mod=o.modifiers.new('Sculpted surface subdivision','SUBSURF');mod.levels=2;bpy.ops.object.modifier_apply(modifier=mod.name)
 return finish(o,n,mat,bone)
exec(compile((Path(__file__).parent/'sculpt_character.py').read_text(), 'sculpt_character.py', 'exec'))
# Original deformation skeleton.
bpy.ops.object.select_all(action='DESELECT'); bpy.ops.object.armature_add(); rig=bpy.context.object; rig.name='PipFuse_Rig'; bpy.ops.object.mode_set(mode='EDIT'); rig.data.edit_bones.remove(rig.data.edit_bones[0])
bones={}
def bone(n,a,b,parent=None):
 e=rig.data.edit_bones.new(n); e.head=a;e.tail=b
 if parent:e.parent=bones[parent]
 bones[n]=e
bone('Hips',(0,0,.86),(0,0,1.04)); bone('Spine',(0,0,1.04),(0,0,1.47),'Hips'); bone('Head',(0,0,1.47),(0,0,1.9),'Spine')
for s in [-1,1]:
 side='L' if s<0 else 'R'; bone('Thigh.'+side,(s*.13,0,.91),(s*.16,0,.54),'Hips'); bone('Shin.'+side,(s*.16,0,.54),(s*.18,0,.14),'Thigh.'+side); bone('Arm.'+side,(s*.29,0,1.39),(s*.43,0,1.12),'Spine'); bone('Forearm.'+side,(s*.43,0,1.12),(s*.49,-.025,.91),'Arm.'+side); bone('Hand.'+side,(s*.49,-.025,.91),(s*.515,-.035,.76),'Forearm.'+side)
bone('weapon_socket',(.508,-.065,.805),(.508,-.165,.805),'Hand.R')
bpy.ops.object.mode_set(mode='OBJECT')
bpy.ops.object.select_all(action='DESELECT')
for o in parts:o.select_set(True)
bpy.context.view_layer.objects.active=parts[0]; bpy.ops.object.join(); mesh=bpy.context.object; mesh.name='PipFuse_OriginalMesh'; mod=mesh.modifiers.new('Pip skeletal deformation','ARMATURE'); mod.object=rig; mesh.parent=rig
rig.animation_data_create()
clips=['Idle_Gun_TwoHanded','LEAN_LEFT','LEAN_RIGHT','IDLE','RUN','RUN_BACK','STRAFE_LEFT','STRAFE_RIGHT','START_FORWARD','STOP_FORWARD','SNEAK','DUCKING','CROUCH_WALK','JUMP','FALL','LAND_LIGHT','LAND_HEAVY','Death']
for name in clips:
 action=bpy.data.actions.new(name); rig.animation_data.action=action
 for f in range(1,26,3):
  t=(f-1)/24; phase=t*math.tau
  for p in rig.pose.bones:p.rotation_mode='XYZ'; p.rotation_euler=(0,0,0);p.location=(0,0,0)
  moving=name in ['RUN','RUN_BACK','STRAFE_LEFT','STRAFE_RIGHT','START_FORWARD','STOP_FORWARD','SNEAK','CROUCH_WALK']
  if moving:
   amp=.64 if name not in ['SNEAK','CROUCH_WALK'] else .28
   for s,side in [(-1,'L'),(1,'R')]:
    rig.pose.bones['Thigh.'+side].rotation_euler.x=s*amp*math.sin(phase)
    rig.pose.bones['Shin.'+side].rotation_euler.x=-max(0,s*math.sin(phase))*.75
    rig.pose.bones['Arm.'+side].rotation_euler.x=-s*.34*math.sin(phase)
   rig.pose.bones['Hips'].location.z=.025*math.sin(phase*2)
   if name=='RUN_BACK':
    for side in ['L','R']:rig.pose.bones['Thigh.'+side].rotation_euler.x *= -.75
   if name.startswith('STRAFE'):
    sign=1 if name=='STRAFE_LEFT' else -1
    for side in ['L','R']:rig.pose.bones['Thigh.'+side].rotation_euler.z=sign*.28*math.sin(phase)
    rig.pose.bones['Spine'].rotation_euler.y=sign*.10
  if name.startswith('LEAN'):rig.pose.bones['Spine'].rotation_euler.y=(.3 if name=='LEAN_LEFT' else -.3)
  if name=='Idle_Gun_TwoHanded':
   for side in ['L','R']:
    rig.pose.bones['Arm.'+side].rotation_euler.x=-.28
    rig.pose.bones['Forearm.'+side].rotation_euler.x=-1.15+.035*math.sin(phase)
   rig.pose.bones['Spine'].rotation_euler.y=.018*math.sin(phase)
  if name in ['DUCKING','CROUCH_WALK','SNEAK','LAND_LIGHT','LAND_HEAVY']:
   rig.pose.bones['Spine'].rotation_euler.x=.18; rig.pose.bones['Hips'].location.z=-.10*(math.sin(math.pi*t) if name.startswith('LAND') else 1)
  if name in ['JUMP','FALL']:
   for side in ['L','R']:rig.pose.bones['Thigh.'+side].rotation_euler.x=-.25;rig.pose.bones['Shin.'+side].rotation_euler.x=-.4
  if name=='Death':rig.pose.bones['Hips'].rotation_euler.x=-1.45*t;rig.pose.bones['Hips'].location.z=-.65*t
  if name=='IDLE':rig.pose.bones['Spine'].rotation_euler.y=.018*math.sin(phase);rig.pose.bones['Head'].rotation_euler.z=.025*math.sin(phase)
  for p in rig.pose.bones:
   p.keyframe_insert('rotation_euler',frame=f);p.keyframe_insert('location',frame=f)
 track=rig.animation_data.nla_tracks.new();track.name=name;track.strips.new(name,1,action);track.mute=True
rig.animation_data.action=bpy.data.actions['IDLE']; bpy.context.scene.frame_set(1)
scene=bpy.context.scene;scene.render.fps=24
bpy.ops.object.select_all(action='DESELECT');mesh.select_set(True);rig.select_set(True);bpy.context.view_layer.objects.active=rig
out=ROOT/'assets/models/pip_fuse/pip_fuse.glb'
# Keep the editable sculpt dense; create a lighter runtime copy with skin weights.
rig.select_set(False);bpy.context.view_layer.objects.active=mesh
bpy.ops.object.duplicate();runtime=bpy.context.object;runtime.name='PipFuse_RuntimeMesh'
dec=runtime.modifiers.new('Runtime topology reduction','DECIMATE');dec.ratio=.60
bpy.ops.object.modifier_move_up(modifier=dec.name)
bpy.ops.object.modifier_apply(modifier=dec.name)
rig.select_set(True)
bpy.ops.export_scene.gltf(filepath=str(out),use_selection=True,export_format='GLB',export_animations=True,export_animation_mode='ACTIONS')
# Finish the runtime rig with ankle joints, planted crouch poses and state clips.
import runpy
runpy.run_path(str(ROOT/'scripts/author_player_animations.py'),run_name='__main__')
bpy.data.objects.remove(runtime,do_unlink=True)
# Studio preview, authored lighting only.
scene.world.color=(.065,.065,.065)
box('Studio floor',(0,0,-.06),(200,200,.05),'pants',None)
def aim(o,p):o.rotation_euler=(Vector(p)-o.location).to_track_quat('-Z','Y').to_euler()
for loc,power,size in [((3,-4,5),450,4),((-3,-2,3),300,3),((1,3,4),600,3)]:
 bpy.ops.object.light_add(type='AREA',location=loc);bpy.context.object.data.energy=power;bpy.context.object.data.shape='DISK';bpy.context.object.data.size=size;aim(bpy.context.object,(0,0,1))
bpy.ops.object.camera_add(location=(3,-6,2.9));cam=bpy.context.object;aim(cam,(0,0,1.04));scene.camera=cam;cam.data.type='ORTHO';cam.data.ortho_scale=2.55
scene.render.engine='CYCLES';scene.cycles.samples=32;scene.cycles.use_denoising=True;scene.render.resolution_x=1400;scene.render.resolution_y=1400;scene.render.resolution_percentage=100
scene.view_settings.view_transform='AgX';scene.view_settings.look='AgX - Medium High Contrast';scene.view_settings.exposure=-.35
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'art/source/pip_fuse/pip_fuse.blend'))
for name,loc,scale,target in [('three_quarter',(3,-6,2.9),2.55,(0,0,1.04)),('front',(0,-6,2.1),2.4,(0,0,1.02)),('portrait',(2,-6,2.2),.85,(0,-.02,1.75)),('back',(-3,6,2.7),2.55,(0,0,1.04))]:
 cam.location=loc;cam.data.ortho_scale=scale;aim(cam,target);scene.render.filepath=str(ROOT/f'art/previews/pip_fuse/{name}.png');bpy.ops.render.render(write_still=True)
print('PIP_FUSE_COMPLETE',len(mesh.data.vertices),'vertices',len(mesh.data.polygons),'faces')
