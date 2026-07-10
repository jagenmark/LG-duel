"""Bake the freeze-gun GLB into material meshes and socket metadata."""
import sys
from pathlib import Path
import bpy
ROOT=Path(__file__).resolve().parents[3]; sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from baked_material_export import gather_mesh_triangles, write_header
DIR=Path(__file__).resolve().parent
def point(name):
    o=bpy.data.objects[name]; authored=o.get("lg_weapon_space_position"); return tuple(authored) if authored is not None else tuple(o.matrix_world.translation)
def decl(name,value): return f"inline constexpr Vec3 {name} = {{{', '.join(f'{float(v):.7f}F' for v in value)}}};\n"
def translated_to_local(triangles,pivot):
    result=[]
    for vertices,color,metallic,roughness in triangles:
        result.append(([((position[0]-pivot[0],position[1]-pivot[1],position[2]-pivot[2]),normal) for position,normal in vertices],color,metallic,roughness))
    return result
def main():
    bpy.ops.wm.open_mainfile(filepath=str(DIR/"lg_duel_freeze_gun.blend")); meta={n:point(o) for n,o in (("kFreezeGunMuzzleSocket","FG_MUZZLE_SOCKET"),("kFreezeGunGripSocket","FG_RIGHT_HAND_GRIP_SOCKET"),("kFreezeGunSupportGripSocket","FG_SUPPORT_HAND_GRIP_SOCKET"),("kFreezeGunPickupCenter","FG_PICKUP_CENTER"))}; coolant_pivot=(-.02,0,.13); meta["kFreezeGunCoolantPivot"]=coolant_pivot
    bpy.ops.wm.read_factory_settings(use_empty=True); bpy.ops.import_scene.gltf(filepath=str(DIR/"lg_duel_freeze_gun.glb")); bpy.context.scene.frame_set(1)
    meshes=[o for o in bpy.context.scene.objects if o.type=="MESH"]
    focus=[o for o in meshes if o.name.startswith("FG_FOCUS_CORE")]; coolant=[o for o in meshes if o.name.startswith("FG_COOLANT_CHAMBER")]; body=[o for o in meshes if o not in focus and o not in coolant]
    body_t=gather_mesh_triangles(body); focus_t=gather_mesh_triangles(focus); coolant_t=translated_to_local(gather_mesh_triangles(coolant),coolant_pivot)
    write_header(ROOT/"src/render/BakedFreezeGunModel.hpp","assets/models/freeze_gun/lg_duel_freeze_gun.glb",[("kFreezeGunBodyMaterialModel",body_t),("kFreezeGunFocusMaterialModel",focus_t),("kFreezeGunCoolantMaterialModel",coolant_t)])
    p=ROOT/"src/render/BakedFreezeGunModel.hpp"; text=p.read_text(); text=text.replace("} // namespace lg\n","\n".join(decl(k,v) for k,v in meta.items())+"\n} // namespace lg\n"); p.write_text(text)
if __name__=="__main__": main()
