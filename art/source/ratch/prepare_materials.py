#!/usr/bin/env python3
"""Bake Ratch's original UV domains into the engine's 2048px atlas contract.
Textiles each get a 512px tile; constant materials share smaller swatches.
The preserved source GLB is never modified. Runtime output goes in runtime/.
"""
from __future__ import annotations
import argparse, hashlib, json, math, re, struct
from pathlib import Path
from PIL import Image
from preserve_animations import read_glb, write_glb

SIZE, CELL, GUTTER = 2048, 512, 16
# Preserve the reference's brown outer garments. Concentrate the team colour
# in the waistcoat: even a small tint weight strongly recolours dark cloth.
TINT = {'coat':.04, 'coat_edge':.03, 'shirt':.06, 'vest':.65,
        'pants':.02, 'patch_red':.035, 'patch_blue':.035, 'patch_plaid':.035}

def read_uv(doc, binary, index):
    acc=doc['accessors'][index]
    assert acc['componentType']==5126 and acc['type']=='VEC2'
    view=doc['bufferViews'][acc['bufferView']]
    start=view.get('byteOffset',0)+acc.get('byteOffset',0)
    stride=view.get('byteStride',8)
    return [struct.unpack_from('<2f',binary,start+i*stride) for i in range(acc['count'])]

def srgb(x):
    return 12.92*x if x<=.0031308 else 1.055*x**(1/2.4)-.055

def textile_tile(path, domain, size):
    source=Image.open(path).convert('RGB');sp=source.load()
    u0,v0,u1,v1=domain;du=max(u1-u0,1e-9);dv=max(v1-v0,1e-9)
    # Sample at 2x output resolution, then filter for minification.
    out=Image.new('RGB',(size*2,size*2));pixels=out.load();n=size*2
    for y in range(n):
        sy=int(((v0+(y+.5)/n*dv)%1)*source.height)%source.height
        for x in range(n):
            sx=int(((u0+(x+.5)/n*du)%1)*source.width)%source.width
            pixels[x,y]=sp[sx,sy]
    return out.resize((size,size),Image.Resampling.LANCZOS)

def paste_with_gutter(atlas, interior, x, y, cell, gutter):
    n=cell-2*gutter;x0=x+gutter;y0=y+gutter
    atlas.paste(interior,(x0,y0))
    atlas.paste(interior.crop((0,0,1,n)).resize((gutter,n)),(x,y0))
    atlas.paste(interior.crop((n-1,0,n,n)).resize((gutter,n)),(x0+n,y0))
    row=atlas.crop((x,y0,x+cell,y0+n))
    atlas.paste(row.crop((0,0,cell,1)).resize((cell,gutter)),(x,y))
    atlas.paste(row.crop((0,n-1,cell,n)).resize((cell,gutter)),(x,y0+n))

def prepare(model, output_dir, textures):
    doc,raw=read_glb(model);binary=bytearray(raw)
    materials=doc['materials'];keys=[re.sub(r'\.\d+$','',m.get('name','').lower()) for m in materials]
    textured=[i for i,k in enumerate(keys) if (textures/(k+'.png')).is_file()]
    solids=[i for i in range(len(materials)) if i not in textured]
    # Give the baked face a 1024px region; textile tiles retain 512px each.
    large=[i for i in textured if keys[i]=='face']
    if len(large)>1:raise ValueError('Expected one baked face material')
    occupied={0,1,4,5} if large else set()
    free=[cell for cell in range(16) if cell not in occupied]
    regular=[i for i in textured if i not in large]
    if len(regular)+math.ceil(len(solids)/16)>len(free):raise ValueError('Atlas capacity exceeded')
    regions={i:(0,0,1024,16) for i in large}
    for j,mi in enumerate(regular):
        block=free[j];regions[mi]=(block%4*CELL,block//4*CELL,CELL,GUTTER)
    for j,mi in enumerate(solids):
        block=free[len(regular)+j//16];slot=j%16
        regions[mi]=(block%4*CELL+slot%4*128,block//4*CELL+slot//4*128,128,8)
    atlas=Image.new('RGB',(SIZE,SIZE));mask=Image.new('RGBA',(SIZE,SIZE))
    bindings=[];report=[]
    for mi,material in enumerate(materials):
        key=keys[mi];pbr=material.get('pbrMetallicRoughness',{})
        color=pbr.get('baseColorFactor',[1,1,1,1])[:3]
        rough=pbr.get('roughnessFactor',1);metal=pbr.get('metallicFactor',1);tint=TINT.get(key,0)
        primitives=[p for mesh in doc['meshes'] for p in mesh['primitives'] if p.get('material')==mi]
        uv=[co for p in primitives for co in read_uv(doc,binary,p['attributes']['TEXCOORD_0'])]
        u0,u1=min(u for u,v in uv),max(u for u,v in uv);v0,v1=min(v for u,v in uv),max(v for u,v in uv)
        x,y,cell,gutter=regions[mi];n=cell-2*gutter
        if mi in textured:interior=textile_tile(textures/(key+'.png'),(u0,v0,u1,v1),n)
        else:interior=Image.new('RGB',(n,n),tuple(round(max(0,min(1,srgb(c)))*255) for c in color))
        paste_with_gutter(atlas,interior,x,y,cell,gutter)
        mask.paste((round(tint*255),round(rough*255),round(metal*255),0),(x,y,x+cell,y+cell))
        for primitive in primitives:
            old=primitive['attributes']['TEXCOORD_0'];mapped=[]
            for vi,(u,v) in enumerate(read_uv(doc,binary,old)):
                nu=(u-u0)/(u1-u0) if u1-u0>1e-6 else (vi%17)/16
                nv=(v-v0)/(v1-v0) if v1-v0>1e-6 else ((vi//17)%17)/16
                mapped.extend(((x+gutter+.5+nu*(n-1))/SIZE,(y+gutter+.5+nv*(n-1))/SIZE))
            while len(binary)%4:binary.append(0)
            offset=len(binary);data=struct.pack('<'+'f'*len(mapped),*mapped);binary.extend(data)
            view={'buffer':0,'byteOffset':offset,'byteLength':len(data)}
            doc['bufferViews'].append(view)
            accessor={'bufferView':len(doc['bufferViews'])-1,'componentType':5126,'count':len(mapped)//2,'type':'VEC2'}
            doc['accessors'].append(accessor);primitive['attributes']['TEXCOORD_0']=len(doc['accessors'])-1
        bindings.append({'index':mi,'name':material['name'],'flat_tint_weight':tint})
        report.append({'material':material['name'],'region':[x,y,cell,cell], 'interior_pixels':n,
                       'original_uv_bounds':[u0,v0,u1,v1], 'textured':mi in textured})
    runtime=output_dir/'runtime';runtime.mkdir(parents=True,exist_ok=True)
    doc['buffers'][0]['byteLength']=len(binary)
    write_glb(runtime/'ratch.glb',doc,bytes(binary))
    atlas.save(runtime/'ratch_albedo.png');mask.save(runtime/'ratch_material_mask.png')
    manifest={'schema_version':1,'model':'ratch.glb',
              'model_sha256':hashlib.sha256((runtime/'ratch.glb').read_bytes()).hexdigest(),
              'opaque':True,'uv_mode':'texcoord0','albedo_mode':'replace','mip_policy':'runtime_generate',
              'textures':{'albedo':{'path':'ratch_albedo.png','color_space':'srgb','width':SIZE,'height':SIZE},
                          'packed_mask':{'path':'ratch_material_mask.png','color_space':'linear','width':SIZE,'height':SIZE}},
              'packed_mask_contract':{'r':'team_tint_weight','g':'perceptual_roughness','b':'metallic_weight','a':'emissive_weight_reserved_zero'},
              'require_material_coverage':True,'materials':bindings}
    (runtime/'material-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    (runtime/'atlas_report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'Prepared {len(materials)} materials: {len(textured)} textile tiles, {len(solids)} constant swatches')

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('model',type=Path);parser.add_argument('output_dir',type=Path)
    parser.add_argument('--textures',type=Path,default=Path(__file__).parent/'textures')
    args=parser.parse_args();prepare(args.model,args.output_dir,args.textures)
