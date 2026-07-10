"""Create the original LG Duel freeze gun, exports, and preview.

Run from the repository root with Blender 5.x. Weapon space is +X forward and
+Z up. The short focus animation is an art reference; gameplay remains the
authority for when the gun fires.
"""
from pathlib import Path
import math
import bpy
from mathutils import Vector

DIR = Path(__file__).resolve().parent

def mat(name, color, metal=0.0, rough=0.45, emission=None):
    m=bpy.data.materials.new(name); m.use_nodes=True
    p=m.node_tree.nodes.get("Principled BSDF")
    p.inputs["Base Color"].default_value=(*color,1); p.inputs["Metallic"].default_value=metal; p.inputs["Roughness"].default_value=rough
    if emission:
        p.inputs["Emission Color"].default_value=(*emission,1); p.inputs["Emission Strength"].default_value=2.5
    return m

def finish(o, material, collection, bevel=0.008):
    o.data.materials.append(material)
    if bevel:
        q=o.modifiers.new("readable bevel","BEVEL"); q.width=bevel; q.segments=2; q.limit_method="ANGLE"
        bpy.context.view_layer.objects.active=o; bpy.ops.object.modifier_apply(modifier=q.name)
    for c in list(o.users_collection): c.objects.unlink(o)
    collection.objects.link(o); return o

def box(name, loc, size, material, collection, rot=(0,0,0), bevel=.008):
    bpy.ops.mesh.primitive_cube_add(location=loc, rotation=rot); o=bpy.context.object; o.name=name; o.scale=tuple(v/2 for v in size)
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True); return finish(o,material,collection,bevel)

def cyl(name, loc, radius, depth, material, collection, vertices=16, rot=(0,math.pi/2,0)):
    bpy.ops.mesh.primitive_cylinder_add(vertices=vertices,radius=radius,depth=depth,location=loc,rotation=rot)
    o=bpy.context.object; o.name=name; return finish(o,material,collection,.005)

def parent_keep(o, parent): o.parent=parent; o.matrix_parent_inverse=parent.matrix_world.inverted()
def empty(name, loc, collection):
    o=bpy.data.objects.new(name,None); o.location=loc; o["lg_weapon_space_position"]=list(loc); o.empty_display_type="ARROWS"; o.empty_display_size=.06; collection.objects.link(o); return o
def look(o,target): o.rotation_euler=(Vector(target)-o.location).to_track_quat("-Z","Y").to_euler()

def build():
    bpy.ops.object.select_all(action="SELECT"); bpy.ops.object.delete(use_global=False)
    export=bpy.data.collections.new("FREEZE_GUN_EXPORT"); bpy.context.scene.collection.children.link(export)
    preview=bpy.data.collections.new("PREVIEW_NOT_FOR_EXPORT"); bpy.context.scene.collection.children.link(preview)
    dark=mat("FG gunmetal",(.035,.05,.065),.82,.32); ceramic=mat("FG thermal ceramic",(.68,.72,.72),.18,.42)
    black=mat("FG insulated grip",(.012,.018,.023),.05,.72); orange=mat("FG safety orange",(.92,.22,.035),.35,.38)
    cyan=mat("FG coolant cyan",(.015,.35,.48),.22,.22,(.02,.7,1)); ice=mat("FG focus ice",(.22,.72,.92),.08,.18,(.15,.8,1))
    # Compact rear and angled insulated grip.
    box("FG_REAR_HOUSING",(-.37,0,.08),(.48,.42,.43),dark,export,bevel=.025)
    box("FG_REAR_CERAMIC_CAP",(-.61,0,.08),(.13,.45,.46),ceramic,export,bevel=.025)
    box("FG_PRIMARY_GRIP",(-.38,0,-.29),(.19,.20,.50),black,export,(0,math.radians(-10),0),.025)
    box("FG_TRIGGER_GUARD",(-.19,0,-.13),(.30,.12,.12),dark,export,bevel=.025)
    box("FG_TRIGGER",(-.20,-.005,-.17),(.04,.07,.13),orange,export,(0,math.radians(-12),0),.006)
    # Reinforced transparent-looking coolant chamber and clamps.
    cyl("FG_COOLANT_CHAMBER",(-.02,0,.13),.145,.48,cyan,export,20)
    for x in (-.25,.20): cyl("FG_CHAMBER_CLAMP",(x,0,.13),.175,.07,dark,export,16)
    box("FG_TOP_SPINE",(-.02,0,.30),(.62,.12,.09),dark,export,bevel=.012)
    # Forward ceramic receiver and vents.
    box("FG_FORWARD_RECEIVER",(.39,0,.12),(.42,.38,.36),ceramic,export,bevel=.025)
    box("FG_LOWER_SPINE",(.34,0,-.08),(.65,.16,.11),dark,export,bevel=.012)
    for z in (.04,.13,.22): box("FG_VENT",(.42,-.205,z),(.22,.025,.035),dark,export,bevel=.006)
    # Coolant lines are deliberately chunky and readable in first person.
    for y in (-.20,.20):
        cyl("FG_COOLANT_LINE",(.24,y,-.01),.027,.54,cyan,export,10)
    cyl("FG_REPLACEABLE_CANISTER",(.38,0,-.27),.095,.31,cyan,export,16,rot=(0,0,0))
    cyl("FG_CANISTER_CAP",(.38,0,-.105),.112,.045,dark,export,16,rot=(0,0,0))
    # Forked ceramic emitter, clearly distinct from the LG's long rails.
    box("FG_EMITTER_COLLAR",(.67,0,.12),(.18,.44,.42),dark,export,bevel=.025)
    for y in (-.20,.20):
        box("FG_EMITTER_FORK",(1.04,y,.12),(.78,.11,.16),ceramic,export,bevel=.022)
        box("FG_FROST_INSERT",(1.10,y*.72,.12),(.50,.025,.09),ice,export,bevel=.005)
        box("FG_FORK_METAL_RAIL",(1.04,y,.215),(.70,.065,.035),dark,export,bevel=.006)
        box("FG_FORK_TIP_CLAMP",(1.415,y,.12),(.050,.135,.19),orange,export,bevel=.008)
    # Separately baked focus assembly supports a subtle runtime compression.
    focus=cyl("FG_FOCUS_CORE",(1.12,0,.12),.09,.62,ice,export,8)
    cyl("FG_FOCUS_COLLAR",(.79,0,.12),.125,.055,orange,export,12)
    focus["lg_freeze_part"]="focus"
    for name,loc in (("FG_MUZZLE_SOCKET",(1.47,0,.12)),("FG_RIGHT_HAND_GRIP_SOCKET",(-.39,0,-.30)),("FG_SUPPORT_HAND_GRIP_SOCKET",(.28,0,-.08)),("FG_PICKUP_CENTER",(.10,0,.04))): empty(name,loc,export)
    # Reference focus pulse in the authoring file.
    for frame,x,scale in ((1,1.12,1.0),(5,1.07,.82),(9,1.15,1.08),(15,1.12,1.0)):
        focus.location.x=x; focus.scale=(scale,scale,scale); focus.keyframe_insert("location",frame=frame); focus.keyframe_insert("scale",frame=frame)
    bpy.context.scene.frame_start=1; bpy.context.scene.frame_end=15; bpy.context.scene.render.fps=60; bpy.context.scene.frame_set(1)
    floor=box("preview_floor",(0,0,-.57),(4,4,.05),mat("preview",(.008,.012,.018),0,.65),preview,bevel=0)
    bpy.ops.object.camera_add(location=(2.35,-4.5,1.7)); cam=bpy.context.object; cam.data.lens=66; look(cam,(.08,0,.02)); preview.objects.link(cam); bpy.context.collection.objects.unlink(cam); bpy.context.scene.camera=cam
    for name,loc,energy,color,size in (("key",(.4,-2.5,2.5),700,(.75,.9,1),1.5),("rim",(.8,1.8,1.7),500,(.1,.65,1),1.0),("fill",(-1.7,-.3,.8),300,(1,.65,.4),1.2)):
        d=bpy.data.lights.new(name,"AREA"); d.energy=energy; d.color=color; d.shape="DISK"; d.size=size; o=bpy.data.objects.new(name,d); o.location=loc; look(o,(.1,0,.05)); preview.objects.link(o)
    sc=bpy.context.scene; sc.render.engine="BLENDER_EEVEE"; sc.render.resolution_x=1536; sc.render.resolution_y=1024; sc.render.resolution_percentage=100; sc.render.image_settings.file_format="PNG"; sc.render.filepath=str(DIR/"lg_duel_freeze_gun_preview.png"); sc.world.color=(.003,.005,.008); sc.view_settings.look="AgX - Medium High Contrast"
    bpy.ops.wm.save_as_mainfile(filepath=str(DIR/"lg_duel_freeze_gun.blend"))
    bpy.ops.object.select_all(action="DESELECT")
    for o in export.all_objects: o.select_set(True)
    bpy.ops.export_scene.gltf(filepath=str(DIR/"lg_duel_freeze_gun.glb"),export_format="GLB",use_selection=True,export_animations=True,export_yup=True)
    bpy.ops.export_scene.fbx(filepath=str(DIR/"lg_duel_freeze_gun.fbx"),use_selection=True,bake_anim=True,axis_forward="X",axis_up="Z")
    bpy.ops.render.render(write_still=True); bpy.ops.wm.save_as_mainfile(filepath=str(DIR/"lg_duel_freeze_gun.blend"))
if __name__=="__main__": build()
