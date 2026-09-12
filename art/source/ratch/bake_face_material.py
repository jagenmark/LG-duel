"""Bake Ratch-specific pigmentation and local cavity shading into its face map.
This lives on the asset and does not change lighting for other player models.
"""
import bpy, math, bmesh
from mathutils import Vector

def bake_face_material(face, output_dir, materials):
    material=bpy.data.materials.new('face');material.use_nodes=True
    materials['face']=material
    face.data.materials.clear();face.data.materials.append(material)
    for polygon in face.data.polygons:polygon.material_index=0
    bm=bmesh.new();bm.from_mesh(face.data)
    bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
    bmesh.ops.triangulate(bm,faces=[f for f in bm.faces if len(f.verts)>4])
    bm.to_mesh(face.data);bm.free();face.data.update()
    # Cavities and the underside of the nose cannot share a cylindrical map.
    # Unwrap non-overlapping islands before baking to avoid dark overlap patches.
    bpy.ops.object.select_all(action='DESELECT');face.select_set(True);bpy.context.view_layer.objects.active=face
    bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(35),island_margin=.012,area_weight=.2,correct_aspect=True,scale_to_bounds=True)
    bpy.ops.object.mode_set(mode='OBJECT')
    pigment=face.data.color_attributes.new(name='FacePigment',type='FLOAT_COLOR',domain='CORNER')
    def g(x,c,w):return math.exp(-((x-c)/w)**2)
    for li,loop in enumerate(face.data.loops):
        p=face.data.vertices[loop.vertex_index].co;x,y,z=p;ax=abs(x)
        color=Vector((.43,.245,.135))
        nose=g(x,0,.041)*g(z,1.727,.047)*min(1,max(0,(-y-.075)/.045))
        cheek=g(ax,.094,.036)*g(z,1.753,.030)*min(1,max(0,-y/.045))
        color=color.lerp(Vector((.48,.205,.145)),nose*.82)
        color=color.lerp(Vector((.43,.215,.131)),cheek*.32)
        # Restrained weathering; anatomical shadows are baked with AO below.
        f=.965+.022*math.sin(x*317+math.sin(z*213))+.014*math.sin(y*809+z*567)
        for k in range(3):
            f-=.045*g(z,1.852+k*.012+.003*math.cos(x*36+k),.0015)*g(x,0,.070)
        f-=.055*g(ax,.096,.020)*g(z,1.776,.025)
        for j in range(3):color[j]*=max(.75,f)
        pigment.data[li].color=(*color,1)
    nodes=material.node_tree.nodes;nodes.clear();links=material.node_tree.links
    out=nodes.new('ShaderNodeOutputMaterial');emit=nodes.new('ShaderNodeEmission')
    attr=nodes.new('ShaderNodeVertexColor');attr.layer_name='FacePigment'
    ao=nodes.new('ShaderNodeAmbientOcclusion');ao.inputs['Distance'].default_value=.038;ao.samples=16
    strength=nodes.new('ShaderNodeMath');strength.operation='MULTIPLY_ADD';strength.inputs[1].default_value=.60;strength.inputs[2].default_value=.40
    mix=nodes.new('ShaderNodeMixRGB');mix.blend_type='MULTIPLY';mix.inputs[0].default_value=1
    links.new(ao.outputs['AO'],strength.inputs[0]);links.new(attr.outputs['Color'],mix.inputs[1]);links.new(strength.outputs[0],mix.inputs[2])
    links.new(mix.outputs[0],emit.inputs['Color']);links.new(emit.outputs[0],out.inputs['Surface'])
    im=bpy.data.images.new('Ratch face baked albedo',width=2048,height=2048,alpha=False)
    im.colorspace_settings.name='sRGB'
    tex=nodes.new('ShaderNodeTexImage');tex.image=im;nodes.active=tex
    scene=bpy.context.scene;scene.render.engine='CYCLES';scene.cycles.samples=16
    scene.render.bake.margin=16;scene.render.bake.use_clear=True
    bpy.ops.object.select_all(action='DESELECT');face.select_set(True);bpy.context.view_layer.objects.active=face
    bpy.ops.object.bake(type='EMIT')
    im.filepath_raw=str(output_dir/'textures'/'face.png');im.file_format='PNG';im.save();im.pack()
    nodes.clear();out=nodes.new('ShaderNodeOutputMaterial');bsdf=nodes.new('ShaderNodeBsdfPrincipled');bsdf.inputs['Roughness'].default_value=.82
    tex=nodes.new('ShaderNodeTexImage');tex.image=im
    links.new(tex.outputs['Color'],bsdf.inputs['Base Color']);links.new(bsdf.outputs[0],out.inputs['Surface'])
    # Pigment is baked; avoid an unintended second vertex-color multiplication.
    face.data.color_attributes.remove(pigment)
    return im
