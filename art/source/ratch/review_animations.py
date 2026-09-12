"""Render inherited runtime animation poses in the Ratch source studio."""
import bpy, json, math, os
from pathlib import Path
from mathutils import Matrix, Vector
ROOT=Path(__file__).resolve().parents[3]
OUT=Path(os.environ.get('RATCH_REVIEW_OUT',ROOT/'art/previews/ratch/animation'))
OUT.mkdir(parents=True,exist_ok=True)
bpy.ops.wm.open_mainfile(filepath=str(ROOT/'art/source/ratch/ratch.blend'))
for o in list(bpy.data.objects):
 if o.type=='ARMATURE' or o.name=='Ratch_ApprovalMesh':bpy.data.objects.remove(o,do_unlink=True)
# Remove source actions so runtime names stay exact for the audit.
for a in list(bpy.data.actions):bpy.data.actions.remove(a)
bpy.ops.import_scene.gltf(filepath=str(ROOT/'art/exports/ratch/ratch.glb'))
rig=next(o for o in bpy.context.scene.objects if o.type=='ARMATURE')
for t in rig.animation_data.nla_tracks:t.mute=True
scene=bpy.context.scene;cam=scene.camera;scene.cycles.samples=16;scene.render.resolution_x=800;scene.render.resolution_y=1000
scene.render.resolution_percentage=int(os.environ.get('RATCH_REVIEW_PERCENT','100'))
records=[]
def matrix_delta(a,b):
 return max(abs(a[row][column]-b[row][column]) for row in range(4) for column in range(4))

poses=[
 ('Idle_Gun_TwoHanded',12,None),('RUN',6,None),('RUN',18,None),
 ('DUCKING',12,None),('GLIDE_SLIDE',12,None),
 # The current Worker/Ratch runtime ignores the LEAN_LEFT action in its
 # presentation stack.  It keeps the locomotion pose and rotates the Spine
 # subtree around its armature-space pivot by proceduralLean * 34 degrees.
 # Show the full standing lean here instead of the much subtler inherited
 # 17.2-degree standalone action, which is retained unchanged in the GLB.
 ('LEAN_LEFT',12,math.radians(34.0)),
 ('JUMP',7,None),('Death',18,None),
]
requested={name.strip() for name in os.environ.get('RATCH_POSES','').split(',') if name.strip()}
if requested:
 unknown=requested-{clip for clip,_,_ in poses}
 if unknown:raise RuntimeError('Unknown RATCH_POSES: '+', '.join(sorted(unknown)))
 poses=[pose for pose in poses if pose[0] in requested]
for clip,frame,side_lean in poses:
 sampled_clip='Idle_Gun_TwoHanded' if side_lean is not None else clip
 action=bpy.data.actions.get(sampled_clip)
 if action is None:raise RuntimeError('Missing '+clip)
 rig.animation_data.action=None
 for bone in rig.pose.bones:bone.matrix_basis.identity()
 rig.animation_data.action=action
 if action.slots:rig.animation_data.action_slot=action.slots[0]
 scene.frame_set(frame);bpy.context.view_layer.update()
 if side_lean is not None:
  # Freeze the evaluated base pose before detaching the action. Rendering
  # reevaluates active actions and would otherwise overwrite the preview-only
  # armature-space transform below.
  sampled_basis={bone.name:bone.matrix_basis.copy() for bone in rig.pose.bones}
  sampled_matrices={bone.name:bone.matrix.copy() for bone in rig.pose.bones}
  rig.animation_data.action=None
  for bone in rig.pose.bones:bone.matrix_basis.identity()
  bpy.context.view_layer.update()
  for bone in rig.pose.bones:bone.matrix_basis=sampled_basis[bone.name]
  bpy.context.view_layer.update()
  restored_delta=max(matrix_delta(bone.matrix,sampled_matrices[bone.name]) for bone in rig.pose.bones)
  if restored_delta>1e-5:raise RuntimeError(f'Frozen pose drifted by {restored_delta:.8f}')
  child_basis={bone.name:bone.matrix_basis.copy() for bone in rig.pose.bones if bone.name!='Spine'}
  spine=rig.pose.bones['Spine'];pivot=spine.head.copy()
  around_pivot=(Matrix.Translation(pivot) @ Matrix.Rotation(side_lean,4,'Y') @ Matrix.Translation(-pivot))
  spine.matrix=around_pivot @ spine.matrix
  bpy.context.view_layer.update()
  child_delta=max(matrix_delta(bone.matrix_basis,child_basis[bone.name]) for bone in rig.pose.bones if bone.name!='Spine')
  if child_delta>1e-5:raise RuntimeError(f'Lean changed child-local transforms by {child_delta:.8f}')
 for view,loc in [('three_quarter',(3,-6,2.6)),('side',(6,0,1.8))]:
  cam.location=loc;target=Vector((0,0,1.05));cam.rotation_euler=(target-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.ortho_scale=2.5
  if clip=='Death':
   # The fall extends beyond the upright portrait framing. Fit the evaluated
   # model so both ends of the character remain visible in the audit.
   depsgraph=bpy.context.evaluated_depsgraph_get();corners=[]
   for ob in scene.objects:
    if ob.type=='MESH' and any(m.type=='ARMATURE' for m in ob.modifiers):
     evaluated=ob.evaluated_get(depsgraph)
     corners.extend(evaluated.matrix_world @ Vector(c) for c in evaluated.bound_box)
   if not corners:raise RuntimeError('No skinned geometry for death framing')
   low=Vector(tuple(min(p[i] for p in corners) for i in range(3)))
   high=Vector(tuple(max(p[i] for p in corners) for i in range(3)))
   target=(low+high)*.5;cam.location=target+Vector(loc)-Vector((0,0,1.05))
   cam.rotation_euler=(target-cam.location).to_track_quat('-Z','Y').to_euler()
   camera_space=[cam.rotation_euler.to_matrix().transposed() @ (p-target) for p in corners]
   width=max(p.x for p in camera_space)-min(p.x for p in camera_space)
   height=max(p.y for p in camera_space)-min(p.y for p in camera_space)
   cam.data.ortho_scale=max(height,width/(scene.render.resolution_x/scene.render.resolution_y))*1.12
  name=f'{clip}_{frame}_{view}';scene.render.filepath=str(OUT/(name+'.png'));bpy.ops.render.render(write_still=True)
  record={'clip':clip,'frame':frame,'view':view,'image':str(OUT/(name+'.png'))}
  if side_lean is not None:
   record.update({'sampled_clip':sampled_clip,'composition':'runtime procedural Spine-subtree lean','side_lean_degrees':math.degrees(side_lean),'frozen_pose_max_delta':restored_delta,'unchanged_child_basis_max_delta':child_delta})
  records.append(record)
report=OUT/'review.json'
if requested and report.exists():
 previous=json.loads(report.read_text())
 keys={(r['clip'],r['frame'],r['view']) for r in records}
 records=[r for r in previous if (r['clip'],r['frame'],r['view']) not in keys]+records
report.write_text(json.dumps(records,indent=2))
