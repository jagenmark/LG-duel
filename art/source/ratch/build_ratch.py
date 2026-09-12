"""Ratch: reference-driven tailored mesh, using the current Pip/Fuse runtime rig.
Run Blender 4.5 --background --python this_file. Outputs are approval candidates.
"""
import bpy, math, random, json, sys, os
import numpy as np
from pathlib import Path
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'art/source/ratch'; PRE=ROOT/'art/previews/ratch'; EXP=ROOT/'art/exports/ratch'
for p in [OUT,PRE,EXP,OUT/'textures']: p.mkdir(parents=True,exist_ok=True)
random.seed(83)
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=str(ROOT/'assets/models/pip_fuse/pip_fuse.glb'))
rig=next(o for o in bpy.context.scene.objects if o.type=='ARMATURE')
for o in list(bpy.context.scene.objects):
 if o!=rig:bpy.data.objects.remove(o,do_unlink=True)
rig.animation_data.action=None
for t in rig.animation_data.nla_tracks:t.mute=True
rig.data.pose_position='REST'
parts=[]; M={}
# Albedo textures retain weave and irregular wear in the game, not just the studio.
def material(name,col,rough=.85,textile=False,metal=0):
 m=bpy.data.materials.new(name);m.diffuse_color=(*col,1);m.use_nodes=True
 n=m.node_tree.nodes;p=n.get('Principled BSDF');p.inputs['Base Color'].default_value=(*col,1);p.inputs['Roughness'].default_value=rough;p.inputs['Metallic'].default_value=metal
 if textile:
  S=1024; y,x=np.mgrid[0:S,0:S];rng=np.random.default_rng(len(M)+33)
  cloud=np.zeros((S,S))
  for freq,amp in [(2,.16),(5,.10),(13,.06),(37,.035)]:
   cloud+=amp*np.sin(x*math.tau*freq/S+np.sin(y*math.tau*(freq+1)/S))*np.cos(y*math.tau*freq/S+.8)
  weave=.16*np.sin(x*math.pi/2)*(.55+.45*np.cos(y*math.pi/4))+.10*np.cos(y*math.pi/2)
  fleck=rng.normal(0,.075,(S,S));value=np.clip(1+cloud+weave+fleck,.35,1.8)
  if textile=='plaid':value*=1-.23*((x%96<9)|(y%96<9))-.12*((x%96<2)|(y%96<2))
  arr=np.ones((S,S,4),dtype=np.float32)
  for c in range(3):arr[:,:,c]=np.clip(col[c]*value,0,1)**.65
  im=bpy.data.images.new(name+'_woven_albedo',width=S,height=S);im.pixels.foreach_set(arr.ravel());im.filepath_raw=str(OUT/'textures'/f'{name}.png');im.file_format='PNG';im.save();im.pack()
  tex=n.new('ShaderNodeTexImage');tex.image=im;m.node_tree.links.new(tex.outputs['Color'],p.inputs['Base Color'])
  noise=n.new('ShaderNodeTexNoise');noise.inputs['Scale'].default_value=550;noise.inputs['Detail'].default_value=2
  bump=n.new('ShaderNodeBump');bump.inputs['Strength'].default_value=.36;bump.inputs['Distance'].default_value=.002
  m.node_tree.links.new(noise.outputs['Fac'],bump.inputs['Height']);m.node_tree.links.new(bump.outputs['Normal'],p.inputs['Normal'])
 M[name]=m;return m
for args in [('coat',(.115,.091,.052),.94,True),('coat_edge',(.23,.17,.092),.95,True),('shirt',(.45,.37,.23),.94,True),('vest',(.17,.075,.043),.92,True),('pants',(.10,.083,.045),.95,True),('patch_blue',(.032,.046,.049),.95,True),('patch_red',(.091,.042,.03),.94,True),('patch_plaid',(.18,.125,.069),.95,'plaid'),('leather',(.075,.043,.023),.78,True),('sole',(.032,.027,.020),.9,True),('thread',(.27,.192,.10),.94,False),('skin',(.38,.187,.091),.74,False),('nose',(.42,.173,.092),.7,False),('crease',(.29,.131,.064),.85,False),('hair',(.100,.068,.035),.87,False),('hair_light',(.135,.095,.052),.9,False),('hair_gray',(.175,.137,.086),.9,False),('eye',(.39,.34,.26),.35,False),('iris',(.16,.12,.063),.45,False),('black',(.008,.006,.003),.6,False),('brass',(.27,.18,.068),.6,False,.65)]: material(*args)
# Longitudinal colour variation reads as hair within stable broad locks.
S=1024;hy,hx=np.mgrid[0:S,0:S];hu=hx/S;hv=hy/S
variation=(.84+.13*np.cos(hu*math.tau*5+.32*np.sin(hv*11))+.035*np.cos(hu*math.tau*17+hv*7))*(.79+.21*hv)
arr=np.ones((S,S,4),dtype=np.float32)
for c,value in enumerate((.100,.068,.035)):
 linear=value*variation;arr[:,:,c]=np.where(linear<=.0031308,linear*12.92,1.055*linear**(1/2.4)-.055)
hair_image=bpy.data.images.new('Ratch lock albedo',width=S,height=S);hair_image.pixels.foreach_set(arr.ravel());hair_image.filepath_raw=str(OUT/'textures'/'hair.png');hair_image.file_format='PNG';hair_image.save();hair_image.pack()
tex=M['hair'].node_tree.nodes.new('ShaderNodeTexImage');tex.image=hair_image
M['hair'].node_tree.links.new(tex.outputs['Color'],M['hair'].node_tree.nodes.get('Principled BSDF').inputs['Base Color'])
def mesh(name,vs,fs,mat,bone):
 d=bpy.data.meshes.new(name);d.from_pydata(vs,[],fs);d.update();o=bpy.data.objects.new(name,d);bpy.context.collection.objects.link(o);o.data.materials.append(M[mat])
 for p in d.polygons:p.use_smooth=True
 uv=d.uv_layers.new(name='SurfaceUV')
 for poly in d.polygons:
  for li in poly.loop_indices:
   v=d.vertices[d.loops[li].vertex_index].co;uv.data[li].uv=(v.x*3.1+v.y*2.3,v.z*3.1+v.y*.7)
 if bone:
  g=o.vertex_groups.new(name=bone);g.add(list(range(len(vs))),1,'REPLACE');parts.append(o)
 return o
def ell(name,loc,sc,mat,bone,N=40,H=24):
 vs=[];fs=[]
 for j in range(H+1):
  a=math.pi*j/H
  for i in range(N):
   t=math.tau*i/N;vs.append((loc[0]+sc[0]*math.sin(a)*math.cos(t),loc[1]+sc[1]*math.sin(a)*math.sin(t),loc[2]+sc[2]*math.cos(a)))
 for j in range(H):
  for i in range(N):a=j*N+i;b=j*N+(i+1)%N;fs.append((a,a+N,b+N,b))
 return mesh(name,vs,fs,mat,bone)
def tube(name,pts,r,mat,bone,N=7):
 # Smooth Catmull-Rom centerline, tapered ends; real geometry exported to glTF.
 p=[Vector(x) for x in pts];cs=[]
 for k in range(len(p)-1):
  a=p[max(0,k-1)];b=p[k];c=p[k+1];d=p[min(len(p)-1,k+2)]
  for j in range(5):
   t=j/5;cs.append(.5*((2*b)+(-a+c)*t+(2*a-5*b+4*c-d)*t*t+(-a+3*b-3*c+d)*t*t*t))
 cs.append(p[-1]);vs=[];fs=[]
 for j,c in enumerate(cs):
  tangent=(cs[min(j+1,len(cs)-1)]-cs[max(0,j-1)]).normalized();u=tangent.cross(Vector((0,1,0)))
  if u.length<.01:u=tangent.cross(Vector((1,0,0)))
  u.normalize();v=tangent.cross(u);t=j/(len(cs)-1);rad=r*(.16+.84*math.sin(math.pi*t)**.35)
  for i in range(N):
   a=i*math.tau/N;delta=rad*(math.cos(a)*u+math.sin(a)*v)
   if mat.startswith('hair') and not name.startswith('Wiry eyebrow'):
    normal=Vector((c.x,c.y-.025,0)).normalized() if name=='Tangled hair lock' else Vector((0,-1,0))
    delta-=normal*(delta.dot(normal)*.55)
   vs.append(c+delta)
 for j in range(len(cs)-1):
  for i in range(N):a=j*N+i;b=j*N+(i+1)%N;fs.append((a,b,b+N,a+N))
 ob=mesh(name,vs,fs,mat,bone)
 if mat.startswith('hair'):
  for loop in ob.data.loops:
   j,i=divmod(loop.vertex_index,N)
   ob.data.uv_layers.active.data[loop.index].uv=((1+math.cos(i*math.tau/N))*.5,j/(len(cs)-1))
 ob['tube_sides']=N;ob['tube_radius']=r;return ob
def panel(name,coords,mat,bone,thick=.004):
 o=mesh(name,coords,[tuple(range(len(coords)))],mat,bone)
 bpy.context.view_layer.objects.active=o
 m=o.modifiers.new('Cloth thickness','SOLIDIFY');m.thickness=thick;bpy.ops.object.modifier_apply(modifier=m.name)
 m=o.modifiers.new('Worn softened edge','BEVEL');m.width=.0018;m.segments=2;bpy.ops.object.modifier_apply(modifier=m.name)
 return o
def sample(rows,z):
 for k in range(len(rows)-1):
  if z<=rows[k+1][0]:
   t=max(0,min(1,(z-rows[k][0])/(rows[k+1][0]-rows[k][0])));h=rows[k+1][0]-rows[k][0];a=max(0,k-1);b=min(len(rows)-1,k+2)
   return [(2*t**3-3*t*t+1)*rows[k][i]+(t**3-2*t*t+t)*h*(rows[k+1][i]-rows[a][i])/(rows[k+1][0]-rows[a][0])+(-2*t**3+3*t*t)*rows[k+1][i]+(t**3-t*t)*h*(rows[b][i]-rows[k][i])/(rows[b][0]-rows[k][0]) for i in range(1,len(rows[k]))]
 return rows[-1][1:]
def loft(name,rows,mat,bone,N=64,H=60,wrinkle=.0,rag=.0):
 vs=[];fs=[]
 for j in range(H):
  t=j/(H-1);z=rows[0][0]+(rows[-1][0]-rows[0][0])*t;x,y,rx,ry=sample(rows,z)
  for i in range(N):
   a=i*math.tau/N;w=wrinkle*(.55*math.sin(9*a+z*38)+.45*math.sin(17*a-z*53))
   zz=z+rag*(math.sin(i*7.13)*.5+.5)*(1-t)**16
   vs.append((x+(rx+w)*math.cos(a),y+(ry+w)*math.sin(a),zz))
 for j in range(H-1):
  for i in range(N):a=j*N+i;b=j*N+(i+1)%N;fs.append((a,b,b+N,a+N))
 fs.extend([tuple(reversed(range(N))),tuple((H-1)*N+i for i in range(N))]);return mesh(name,vs,fs,mat,bone)
def stitch_edge(name,points,bone,spacing=.018):
 for a,b in zip(points,points[1:]):
  a=Vector(a);b=Vector(b);d=b-a;num=max(1,int(d.length/spacing))
  tangent=d.normalized();perp=Vector((-tangent.z,0,tangent.x))*.0045
  for k in range(num):
   c=a+d*((k+.5)/num);tube(name,[c-perp,c+Vector((0,-.002,0)),c+perp],.0011,'thread',bone,N=6)
def patch(name,c,w,h,mat,bone,back=False):
 x,y,z=c;sgn=1 if back else -1
 corners=[Vector((x-w*.52,y,z-h*.47)),Vector((x+w*.46,y-.001,z-h*.53)),Vector((x+w*.52,y,z+h*.43)),Vector((x-w*.43,y+.001,z+h*.51))]
 coords=[]
 for k in range(4):
  for j in range(7):
   v=corners[k].lerp(corners[(k+1)%4],j/7);v.x+=random.uniform(-.0025,.0025);v.z+=random.uniform(-.003,.003);coords.append(tuple(v))
 panel(name,coords,mat,bone,.003)
 ps=[(a,b+sgn*.004,c) for a,b,c in coords];stitch_edge(name+' mending',ps+[ps[0]],bone)
 # Broken warp threads around cut fabric.
 for j in range(7):
  xx=x-w/2+w*j/6;tube(name+' fray',[(xx,y,z-h/2),(xx+.003,y-.001,z-h/2-.008)],.00065,'coat_edge',bone,N=5)
# Pelvis and trouser legs, shaped with crease relief and ragged cuffs.
loft('Trouser seat',[(.78,0,.024,.17,.108),(.90,0,.02,.189,.115),(1.02,0,.02,.177,.11)],'pants','Hips',wrinkle=.003)
loft('Open linen shirt',[(.91,0,-.005,.166,.112),(1.10,0,.006,.175,.123),(1.32,0,.014,.224,.135),(1.43,0,.017,.229,.113),(1.50,0,.01,.082,.062)],'shirt','Spine',wrinkle=.003,rag=.028)
loft('Neck tendons',[(1.44,0,.012,.062,.06),(1.56,0,.022,.064,.064),(1.64,0,.012,.053,.042)],'skin','Head',H=35)
# Open, asymmetric long coat. Split back tails and a visibly cut front edge.
coatrows=[(.56,0,.018,.23,.153),(.75,0,.023,.212,.147),(.94,0,.023,.195,.132),(1.10,0,.023,.205,.142),(1.30,0,.023,.258,.153),(1.405,0,.025,.256,.136),(1.455,0,.02,.212,.104),(1.485,0,.015,.098,.077)]
def coatpoint(u,v,off=0):
 z=.56+(.925)*v;x,y,rx,ry=sample(coatrows,z);a=.43+u*(math.tau-.86)
 wave=.0045*math.sin(17*a+z*14)+.0015*math.sin(35*a-z*23)+.001*math.sin(67*a+z*51)
 hem=.036*math.sin(u*333)+.025*math.sin(u*159)+.035*math.sin(u*78)
 z+=hem*(1-v)**20
 # A V vent in the center of the back.
 z+=.23*max(0,1-abs(u-.5)/.04)*(1-v)**12
 return (x+(rx+wave+off)*math.sin(a),y-(ry+wave+off)*math.cos(a),z)
vs=[];fs=[];N=150;H=100
for j in range(H):
 for i in range(N):vs.append(coatpoint(i/(N-1),j/(H-1)))
for j in range(H-1):
 for i in range(N-1):a=j*N+i;fs.append((a,a+1,a+N+1,a+N))
coat=mesh('Ratch split long coat',vs,fs,'coat','Spine')
# Coattails follow pelvis/thigh progressively, avoiding a rigid slab under running.
for g in list(coat.vertex_groups):coat.vertex_groups.remove(g)
for n in ['Spine','Hips','Thigh.L','Thigh.R']:coat.vertex_groups.new(name=n)
for v in coat.data.vertices:
 z=v.co.z;side='L' if v.co.x<0 else 'R'
 if z>=1.02: weights={'Spine':1}
 elif z>.87:
  t=(z-.87)/.15;weights={'Spine':t,'Hips':1-t}
 else:
  t=min(.72,(.87-z)/.36);weights={'Hips':1-t,'Thigh.'+side:t}
 for n,w in weights.items():coat.vertex_groups[n].add([v.index],w,'REPLACE')
# Tattered edge fibers are each short and directional.
for i in range(145):
 u=(i+.2)/146;p=Vector(coatpoint(u,0,.001));bone='Thigh.L' if p.x<0 else 'Thigh.R'
 tube('Coat hem unraveling',[p,p+Vector((random.uniform(-.004,.004),0,-.009)),p+Vector((random.uniform(-.01,.01),.002,-random.uniform(.015,.036)))],.0008,'coat_edge',bone,N=5)
for u in [0,1,.22,.78]:
 pts=[coatpoint(u,j/25,.0025) for j in range(26)]
 tube('Coat vertical welt',pts,.0016,'coat_edge','Spine')
# Open V shirt, shadowed clavicle area and curling linen edges.
# Neckline is formed by the collar and the continuous neck mesh.
for sign in [-1,1]:
 pass # Fine neck details must remain on the anatomical surface.
# Separate waistcoat panels and notched lapels.
for s in [-1,1]:
 panel('Waistcoat front',[(s*.023,-.14,.946),(s*.15,-.131,.91),(s*.19,-.143,1.31),(s*.092,-.111,1.46),(s*.04,-.141,1.31)],'vest','Spine')
 panel('Notched coat lapel',[(s*.085,-.083,1.495),(s*.206,-.097,1.43),(s*.179,-.145,1.382),(s*.207,-.138,1.36),(s*.106,-.164,1.19),(s*.076,-.159,1.32),(s*.123,-.134,1.395)],'coat','Spine',.009)
 stitch_edge('Lapel saddle seam',[(s*.09,-.091,1.48),(s*.195,-.108,1.43),(s*.166,-.154,1.381),(s*.195,-.148,1.36),(s*.109,-.174,1.21)],'Spine',.013)
 panel('Open linen collar',[(s*.011,-.065,1.49),(s*.076,-.073,1.487),(s*.099,-.129,1.39),(s*.022,-.13,1.445)],'shirt','Spine')
 # Coat buttons and slitted pocket, not glued-on blocks.
 for z in [1.31,1.20,1.09,.98]:
  x=s*(.115 if z>1.2 else .098);ell('Antique coat button',(x,-.161,z),(.009,.003,.010),'brass','Spine',N=20,H=12)
  ell('Button inset',(x,-.164,z),(.004,.001,.005),'leather','Spine',N=16,H=8)
 tube('Slant pocket welt',[(s*.15,-.12,1.06),(s*.202,-.075,1.095)],.004,'coat_edge','Spine')
for z in [1.02,1.10,1.18,1.26,1.34]:
 ell('Shirt mother of pearl button',(0,-.139,z),(.006,.003,.007),'thread','Spine',N=20,H=12)
 tube('Shirt placket',[(.013,-.137,z-.036),(.012,-.141,z+.035)],.0015,'coat_edge','Spine')
for s in [-1,1]:
 side='L' if s<0 else 'R';arm='Arm.'+side;fore='Forearm.'+side;hand='Hand.'+side;thigh='Thigh.'+side;shin='Shin.'+side;foot='Foot.'+side
 sleeve=loft('Wrinkled full sleeve '+side,[(.895,s*.486,-.01,.060,.061),(1.01,s*.461,.006,.076,.075),(1.115,s*.422,.015,.083,.083),(1.25,s*.352,.022,.083,.080),(1.37,s*.294,.024,.080,.085),(1.42,s*.261,.024,.069,.066),(1.445,s*.250,.024,.033,.035),(1.452,s*.244,.024,.006,.007)],'coat',arm,wrinkle=.0045,rag=.021)
 sleeve.vertex_groups.new(name=fore);sleeve.vertex_groups.new(name='Spine')
 for v in sleeve.data.vertices:
  t=max(0,min(1,(1.19-v.co.z)/.16));sp=max(0,min(1,(v.co.z-1.34)/.10));sleeve.vertex_groups[arm].add([v.index],(1-t)*(1-sp),'REPLACE');sleeve.vertex_groups[fore].add([v.index],t,'REPLACE');sleeve.vertex_groups['Spine'].add([v.index],sp,'REPLACE')
 loft('Cuff band '+side,[(.913,s*.483,-.012,.063,.065),(.947,s*.475,-.007,.07,.07)],'leather',fore,H=8,wrinkle=.002)
 loft('Fingerless glove palm '+side,[(.80,s*.509,-.02,.047,.033),(.855,s*.496,-.013,.060,.037),(.91,s*.489,-.013,.055,.044)],'leather',hand,H=25,N=40,wrinkle=.001)
 for i in range(4):
  x=s*.507+(i-1.5)*.024;bottom=.733+abs(i-1.3)*.011
  tube('Bare finger',[(x,-.033,.827),(x,-.043,.788),(x+.001,-.040,bottom)],.011,'skin',hand,N=12)
  ell('Fingernail',(x,-.049,bottom+.008),(.007,.0018,.01),'shirt',hand,N=16,H=10)
  for z in [.785,.805]:tube('Knuckle creases',[(x-.006,-.051,z),(x,-.053,z-.002),(x+.006,-.051,z)],.0006,'crease',hand,N=5)
 tube('Opposing bare thumb',[(s*.466,-.01,.863),(s*.449,-.029,.832),(s*.441,-.04,.804)],.014,'skin',hand,N=14)
 for z in [.85,.879]:tube('Glove stitched band',[(s*.45,-.036,z),(s*.49,-.054,z),(s*.535,-.034,z)],.0014,'thread',hand)
 leg=loft('Worn trouser leg '+side,[(.217,s*.18,.011,.073,.071),(.30,s*.175,.015,.075,.072),(.42,s*.164,.018,.086,.079),(.54,s*.158,.014,.081,.079),(.69,s*.139,.018,.097,.096),(.85,s*.119,.019,.102,.10),(.94,s*.108,.02,.096,.099)],'pants',thigh,wrinkle=.004,rag=.029)
 leg.vertex_groups.new(name=shin)
 for v in leg.data.vertices:
  t=max(0,min(1,(.61-v.co.z)/.14));leg.vertex_groups[thigh].add([v.index],1-t,'REPLACE');leg.vertex_groups[shin].add([v.index],t,'REPLACE')
 patch('Knee repair '+side,(s*.16,-.070,.53),.10,.133,'patch_blue' if s>0 else 'patch_red',shin)
 patch('Shin repair '+side,(s*.18,-.065,.32),.066,.07,'patch_plaid',shin)
 patch('Elbow darn '+side,(s*.415,-.071,1.14),.093,.092,'patch_red' if s>0 else 'patch_blue',fore)
 # Continuous boot volume with a flat attached sole, not stacked ellipsoids.
 bootmat='patch_blue' if s>0 else 'leather'
 loft('Layered outsole '+side,[(.009,s*.18,-.076,.087,.149),(.015,s*.18,-.076,.099,.165),(.035,s*.18,-.076,.100,.167),(.051,s*.18,-.076,.097,.163)],'sole',foot,N=64,H=20)
 loft('Stitched leather welt '+side,[(.045,s*.18,-.076,.097,.163),(.054,s*.18,-.076,.100,.167),(.063,s*.18,-.076,.096,.161)],'coat_edge',foot,N=64,H=12)
 loft('Sculpted boot toe '+side,[(.054,s*.18,-.076,.094,.159),(.075,s*.18,-.076,.095,.160),(.105,s*.18,-.072,.091,.150),(.137,s*.18,-.040,.081,.119),(.175,s*.177,.005,.074,.081),(.225,s*.176,.017,.070,.064),(.290,s*.175,.015,.065,.060)],bootmat,foot,N=72,H=65,wrinkle=.0008)
 panel('Boot tongue',[(s*.18-.035,-.066,.115),(s*.18+.035,-.066,.115),(s*.18+.041,-.053,.251),(s*.18-.035,-.053,.255)],'leather',foot,.009)
 for j in range(5):
  z=.126+j*.025;y=-.080+j*.004
  for a in [-1,1]:ell('Lace eyelet',(s*.18+a*.043,y,z),(.005,.003,.005),'brass',foot,N=12,H=8)
  tube('Crossed bootlace',[(s*.18-.043,y-.005,z),(s*.18,y-.014,z+.013),(s*.18+.043,y-.001,z+.025)],.0024,'thread',foot)
  tube('Crossed bootlace',[(s*.18+.043,y-.005,z),(s*.18,y-.016,z+.013),(s*.18-.043,y-.001,z+.025)],.0024,'thread',foot)
 tube('Lace bow',[(s*.18,-.09,.229),(s*.18-.049,-.108,.265),(s*.18-.055,-.098,.235),(s*.18,-.09,.229),(s*.18+.043,-.103,.26),(s*.18+.05,-.096,.235),(s*.18,-.09,.229)],.0022,'thread',foot)
 for j in range(27):
  a=math.tau*j/27;tube('Sole saddle stitching',[(s*.18+.098*math.cos(a),-.08+.164*math.sin(a),.056),(s*.18+.099*math.cos(a+.055),-.08+.164*math.sin(a+.055),.068)],.0009,'thread',foot,N=5)
 patch('Scuffed toe repair',(s*.18,-.238,.103),.067,.038,'coat_edge',foot)
 for j in range(34):
  a=j*math.tau/34;p=Vector((s*.18+.074*math.cos(a),.011+.072*math.sin(a),.233+random.uniform(0,.018)))
  tube('Trouser cuff fraying',[p,p+Vector((.002,0,-.018))],.0008,'coat_edge',shin,N=5)
# Broad mended fabric fields on front and rear coat.
for u,z,w,h,mat in [(.09,.78,.094,.14,'patch_plaid'),(.88,.69,.09,.12,'patch_red'),(.38,1.23,.125,.17,'coat_edge'),(.56,1.36,.10,.12,'patch_plaid'),(.62,.92,.12,.11,'patch_blue'),(.39,.72,.11,.17,'patch_plaid')]:
 p=coatpoint(u,(z-.56)/.925,.006);patch('Coat patch',p,w,h,mat,'Spine' if z>1 else 'Hips',back=p[1]>0)
# Continuous face with nose bridge, cheek hollows and aged eye sockets.
headrows=[(1.565,.044,.044,.018),(1.60,.070,.055,.010),(1.65,.096,.072,.015),(1.72,.126,.090,.021),(1.79,.132,.102,.027),(1.85,.124,.098,.029),(1.905,.097,.084,.033),(1.928,.01,.01,.03)]
def gauss(x,c,w):return math.exp(-((x-c)/w)**2)
def face_y(x,z):
 rx,ry,cy=sample(headrows,z);v=max(0,1-(x/max(.001,rx))**2);y=cy-ry*v**.35
 # Broad cheekbone, hollow below it, inset orbit and hooded brow.
 y-=.020*(gauss(x,.092,.033)+gauss(x,-.092,.033))*gauss(z,1.755,.021)
 y+=.012*(gauss(x,.092,.026)+gauss(x,-.092,.026))*gauss(z,1.701,.035)
 y+=.023*(gauss(x,.056,.029)+gauss(x,-.056,.029))*gauss(z,1.794,.023)
 y-=.020*(gauss(x,.046,.037)+gauss(x,-.046,.037))*gauss(z,1.825,.015)
 # A single continuous pear-shaped nose: narrow root, long bridge and low tip.
 y-=.038*gauss(x,0,.022)*gauss(z,1.783,.048)
 y-=.046*gauss(x,0,.033)*gauss(z,1.744,.037)
 y-=.010*(gauss(x,.028,.012)+gauss(x,-.028,.012))*gauss(z,1.726,.012)
 # Actual relief in the cheek, tear trough and nasolabial folds.
 ax=abs(x)
 y+=.0030*gauss(z,1.773-.070*(ax-.04),.0025)*gauss(ax,.067,.031)
 y-=.0085*gauss(z,1.770-.085*(ax-.04),.014)*gauss(ax,.062,.030)
 y+=.004*gauss(z,1.751-.16*(ax-.04),.0035)*gauss(ax,.069,.028)
 # Weathered brow and crow feet are sculpted relief in the skin.
 for k in range(3):
  y+=.0018*gauss(z,1.852+k*.012+.003*math.cos(x*36+k),.0016)*gauss(x,0,.072)
 for k in range(3):
  y+=.0022*gauss(z,1.790-(ax-.083)*(.2+k*.34)-k*.006,.0018)*gauss(ax,.098,.019)
 t=(1.719-z)/.062
 if 0<t<1:
  fold=.036+.046*t
  y+=.0025*gauss(ax,fold,.005)*math.sin(math.pi*t)
  y-=.005*gauss(ax,fold+.010,.010)*math.sin(math.pi*t)
 return y
vs=[];fs=[];N=224;H=200
for j in range(H):
 z=1.565+.363*j/(H-1);rx,ry,cy=sample(headrows,z)
 for i in range(N):
  a=math.tau*i/N;x=rx*math.cos(a);y=face_y(x,z) if math.sin(a)<0 else cy+ry*math.sin(a);vs.append((x,y,z))
for j in range(H-1):
 for i in range(N):a=j*N+i;b=j*N+(i+1)%N;fs.append((a,b,b+N,a+N))
fs.extend([tuple(reversed(range(N))),tuple((H-1)*N+i for i in range(N))])
mesh('Continuous aged facial surface',vs,fs,'skin','Head')
ell('Pendulous red nose',(.001,-.120,1.724),(.033,.035,.034),'nose','Head',N=64,H=40)
eye_cutters=[]
for s in [-1,1]:
 cx=s*.055;cz=1.793+(.001 if s<0 else -.0015);ey=.043*-1
 def eye_front(x,z):
  q=((x-cx)/.027)**2+((z-cz)/.023)**2
  return ey-.026*math.sqrt(max(.035,1-q))
 def aperture(t,margin=0):
  x=cx+(.023+margin)*math.cos(t)
  h=.0060 if math.sin(t)>0 else .0082
  z=cz+(h+margin)*math.sin(t)-s*(x-cx)*.105
  return x,z
 # Closed eyeball occupies a real opening in the head. Lids occlude its sphere.
 eye=ell('Inset eyeball',(cx,ey,cz),(.027,.026,.023),'eye','Head',N=64,H=40)
 # Broad upper hood and drooping lower bag; the outer edge is buried in skin.
 vv=[];ff=[];NN=80;RR=12
 for back in [False,True]:
  for j in range(RR+1):
   r=j/RR
   for i in range(NN):
    t=math.tau*i/NN;x0,z0=aperture(t)
    x=cx+(x0-cx)*(1+.65*r)
    outer=.028 if math.sin(t)>0 else .020
    z=z0+outer*r*math.sin(t)
    yf=eye_front(x0,z0)-.0007
    yf=yf*(1-r)+face_y(x,z)*r
    yf-=(.007 if math.sin(t)>0 else .0045)*math.sin(math.pi*r)
    yf+=.007*r**6
    if back:yf+=.010
    vv.append((x,yf,z))
 layer=(RR+1)*NN
 for j in range(RR):
  for i in range(NN):
   a=j*NN+i;b=j*NN+(i+1)%NN
   ff.extend([(a,b,b+NN,a+NN),(a+layer,a+NN+layer,b+NN+layer,b+layer)])
 for i in range(NN):
  n=(i+1)%NN;a=RR*NN+i;b=RR*NN+n
  ff.extend([(i,i+layer,n+layer,n),(a,b,b+layer,a+layer)])
 mesh('Continuous orbital lids',vv,[tuple(reversed(f)) for f in ff],'skin','Head')
 # A cut through the skin leaves only the asymmetric eye aperture exposed.
 vv=[];ff=[]
 for y in [-.22,.025]:
  for i in range(NN):
   x,z=aperture(math.tau*i/NN,-.0003);vv.append((x,y,z))
 ff=[tuple(reversed(range(NN))),tuple(NN+i for i in range(NN))]
 for i in range(NN):n=(i+1)%NN;ff.append((i,n,n+NN,i+NN))
 cutter=mesh('Orbital opening cutter',vv,[tuple(reversed(f)) for f in ff],'skin',None);eye_cutters.append(cutter)
 # Iris and pupil lie on the sphere and are naturally clipped by the eyelids.
 for label,rad,offset,mat in [('Iris',.010,.00035,'iris'),('Pupil',.0046,.0007,'black')]:
  vv=[(cx,eye_front(cx,cz)-offset,cz)];ff=[];NN=64
  for j in range(1,11):
   r=j/10
   for i in range(NN):
    t=math.tau*i/NN;x=cx+rad*r*math.cos(t);z=cz+rad*r*math.sin(t)
    vv.append((x,eye_front(x,z)-offset,z))
  for i in range(NN):ff.append((0,1+i,1+(i+1)%NN))
  for j in range(9):
   for i in range(NN):
    a=1+j*NN+i;b=1+j*NN+(i+1)%NN;ff.append((a,a+NN,b+NN,b))
  mesh(label,vv,ff,mat,'Head')
 # Anxious raised inner brows, individually tapered hairs.
 for k in range(25):
  t=k/24;x=s*(.026+.071*t);z=1.854-.031*t+.007*math.sin(t*math.pi)+random.uniform(-.002,.002)
  tube('Wiry eyebrow',[(x,face_y(x,z)-.003,z-.005),(x+s*.007,face_y(x,z)-.008,z+.006),(x+s*.014,face_y(x,z)-.005,z+.002)],.0010,'hair' if k%3 else 'hair_light','Head',N=6)
 ell('Ear pinna',(s*.139,.014,1.773),(.025,.027,.052),'skin','Head')
 ell('Ear concha',(s*.148,-.011,1.774),(.012,.004,.031),'crease','Head')
 tube('Ear helix',[(s*.137,-.009,1.731),(s*.158,-.011,1.754),(s*.159,-.008,1.802),(s*.141,-.01,1.817),(s*.132,-.014,1.789)],.005,'skin','Head',N=9)
tube('Quiet asymmetric mouth',[(x,face_y(x,z)-.0005,z) for x,z in [(-.058,1.683),(-.025,1.685),(0,1.682),(.035,1.684),(.064,1.69)]],.00065,'crease','Head')
# Layered beard: an underlying contoured envelope and hundreds of tapered curls.
loft('Beard foundation',[(1.53,0,-.025,.016,.020),(1.56,0,-.025,.043,.052),(1.62,0,-.019,.074,.080),(1.667,0,.002,.094,.086),(1.687,0,.022,.102,.070)],'hair','Head',N=64,H=35,wrinkle=.003)
for k in range(115):
 x=random.uniform(-.105,.105);z=random.uniform(1.605,1.687);frac=abs(x)/.105
 y=-.092+.042*frac**1.8-random.uniform(0,.006);length=random.uniform(.042,.10)*(1-.35*frac)
 pts=[]
 for j in range(5):
  t=j/4;pts.append((x*(1-.25*t)+.0035*math.sin(t*8+k),y-.006*math.sin(t*math.pi)+.018*t*t,z-length*t))
 tube('Beard individual lock',pts,random.uniform(.0023,.0048),random.choices(['hair','hair_light','hair_gray'],[8,3,1])[0],'Head',N=5)
# Sideburns and beard edge curls grow out of the skin and continue around the
# jaw. This hides the beard envelope from side and underside cameras.
for sign in [-1,1]:
 for k in range(70):
  x=sign*random.uniform(.074,.119);z=random.uniform(1.652,1.766);x=sign*min(abs(x),sample(headrows,z)[0]*.95);y=face_y(x,z)-.001
  length=random.uniform(.018,.039)
  tube('Cheek sideburn strand',[(x,y,z),(x-sign*.004,y-.004,z-length*.45),(x-sign*.010,y-.006,z-length)],random.uniform(.0020,.0034),'hair' if k%3 else 'hair_light','Head',N=5)
 for k in range(60):
  t=random.random();z=1.548+.126*t;xx,cy,rx,ry=sample([(1.53,0,-.025,.016,.020),(1.56,0,-.025,.043,.052),(1.62,0,-.019,.074,.080),(1.667,0,.002,.094,.086),(1.687,0,.022,.102,.070)],z);a=random.uniform(.76,.96);cx=sign*rx*a;y=cy-ry*math.sqrt(1-a*a)-.001
  tube('Beard side curl',[(cx,y,z),(cx+sign*.004,y-.004,z-.012),(cx-sign*.008,y-.008,z-.032)],random.uniform(.0020,.0038),'hair_light' if k%4==0 else 'hair','Head',N=5)
# Moustache streams down and outward from below the nostrils.
for s in [-1,1]:
 for k in range(25):
  t=k/24;x=s*(.006+.022*t);z=1.708-random.uniform(0,.013)
  tube('Moustache strand',[(x,-.12,z),(s*.034,-.125,z-.006),(s*.063,-.111,z-.017),(s*(.083+random.uniform(0,.014)),-.092,z-.033)],random.uniform(.0018,.0032),'hair_light' if k%5==0 else 'hair','Head',N=5)
# Unkempt hair around temples and back, with flyaways beyond the hat silhouette.
for k in range(230):
 a=random.uniform(-math.pi,math.pi);front=abs(a)<.65
 if front:
  a=random.choice([-1,1])*random.uniform(.92,1.45)
 z=random.uniform(1.87,1.932);length=random.uniform(.125,.18) if abs(a)>1.32 else random.uniform(.05,.08);rx=.133;ry=.104
 pts=[]
 for j in range(6):
  t=j/5;aa=a+.070*math.sin(t*5+k);rr=.009*math.sin(t*math.pi)+.003*math.sin(t*9+k)
  pts.append(((rx+rr)*math.sin(aa),.025-(ry+rr)*math.cos(aa),z-length*t))
 tube('Tangled hair lock',pts,random.uniform(.0030,.0060),'hair' if k%5 else 'hair_light','Head',N=6)
# A scalp shell seats the roots of the separate hair strands from every angle.
vv=[];ff=[];NH=48;HH=35
for j in range(HH):
 z=1.76+.165*j/(HH-1);rx,ry,cy=sample(headrows,z)
 for i in range(NH):
  a=.86+(math.tau-1.72)*i/(NH-1);vv.append(((rx+.0005)*math.sin(a),cy-(ry+.0005)*math.cos(a),z+.012*math.sin(a*19)*(1-j/(HH-1))**9))
for j in range(HH-1):
 for i in range(NH-1):a=j*NH+i;ff.append((a,a+1,a+NH+1,a+NH))
mesh('Hair root scalp envelope',vv,ff,'hair','Head')
# Asymmetric battered hat: broad wavy brim and dented leaning crown.
N=144;H=20;vs=[];fs=[]
for j in range(H):
 r=j/(H-1)
 for i in range(N):
  a=i*math.tau/N;rx=.125+.088*r;ry=.105+.073*r
  vs.append((rx*math.cos(a),.024+ry*math.sin(a),1.908+.007*math.cos(a)+(.010*math.sin(a*3+.5)+.014*math.cos(a*2))*r*r+.004*math.sin(a*19)*r**6))
for j in range(H-1):
 for i in range(N):a=j*N+i;b=j*N+(i+1)%N;fs.append((a,b,b+N,a+N))
o=mesh('Bent torn hat brim',vs,fs,'coat','Head');bpy.context.view_layer.objects.active=o;m=o.modifiers.new('Felt thickness','SOLIDIFY');m.thickness=.006;bpy.ops.object.modifier_apply(modifier=m.name)
loft('Dented crooked hat crown',[(1.91,0,.024,.133,.112),(1.946,-.006,.026,.126,.103),(2.005,-.015,.035,.112,.087),(2.078,-.025,.038,.083,.070),(2.108,-.047,.035,.069,.053),(2.121,-.052,.035,.036,.026)],'coat','Head',N=96,H=60,wrinkle=.004)
loft('Faded hat band',[(1.926,-.002,.025,.133,.113),(1.968,-.008,.03,.124,.102)],'leather','Head',N=96,H=10,wrinkle=.001)
patch('Hat burgundy patch',(.067,-.060,2.019),.074,.07,'patch_red','Head')
patch('Hat crown darn',(-.045,-.043,2.096),.055,.032,'coat_edge','Head')
for i in range(92):
 a=i*math.tau/92;p=Vector((.213*math.cos(a),.024+.178*math.sin(a),1.908+.007*math.cos(a)+.010*math.sin(a*3+.5)+.014*math.cos(a*2)+.004*math.sin(a*19)))
 tube('Brim frayed felt',[p,p+Vector((math.cos(a)*.011,math.sin(a)*.01,random.uniform(-.006,.011)))],.0009,'coat_edge','Head',N=5)
# Small broken seams and abrasion marks on the garment, all actual geometry.
for u in [.045,.15,.30,.43,.57,.7,.86,.95]:
 for k in range(8):
  z=random.uniform(.65,1.37);pp=Vector(coatpoint(u,(z-.56)/.925,.002))
  tangent=Vector((.005*random.choice([-1,1]),-.0007,random.uniform(.009,.022)))
  tube('Abraded coat yarn',[pp,pp+tangent*.5,pp+tangent],.0007,'coat_edge','Spine' if z>1 else 'Hips',N=5)
# Worn felt pinholes and small crown stitch repairs.
for k in range(18):
 x=random.uniform(-.083,.08);z=random.uniform(1.98,2.065)
 ell('Felt moth hole',(x,-.053,z),(.0025,.001,.0035),'leather','Head',N=10,H=8)
# A continuous heavy upper-lid fold masks the sclera for a weary expression.
for sign in [-1,1]:
 cx=sign*.058;cy=face_y(cx,1.796)-.006;vv=[];ff=[]
 for i in range(25):
  t=i/24;x=cx+(t-.5)*.061;z=1.796+.009*math.sin(math.pi*t)-sign*(t-.5)*.008
  vv.extend([(x,cy-.013*math.sin(math.pi*t)-.0007,z),(x,face_y(x,z+.013)-.002,z+.013)])
 for i in range(24):ff.append((2*i,2*i+1,2*i+3,2*i+2))
 # The sewn-looking overlay is omitted; the continuous brow and curved eyelid supply the form.
for key in ['skin','nose']:
 m=M[key];n=m.node_tree.nodes;nt=m.node_tree;bs=n.get('Principled BSDF')
 tex=n.new('ShaderNodeTexNoise');tex.inputs['Scale'].default_value=330;tex.inputs['Detail'].default_value=3
 bump=n.new('ShaderNodeBump');bump.inputs['Strength'].default_value=.18;bump.inputs['Distance'].default_value=.0006
 nt.links.new(tex.outputs['Fac'],bump.inputs['Height']);nt.links.new(bump.outputs['Normal'],bs.inputs['Normal'])
# Attachment pass: cloth repairs are densely tessellated and conformed to their
# actual support surface. Their skin weights are interpolated from that surface.
# This is done in 3D, not by hiding detached cards behind a frontal camera.
import bmesh
from mathutils.bvhtree import BVHTree
attachment_report=[]
def support_bvh(objects):
 vv=[];ff=[];weightrows=[]
 for ob in objects:
  offset=len(vv);vv.extend([v.co.copy() for v in ob.data.vertices])
  for v in ob.data.vertices:weightrows.append({ob.vertex_groups[g.group].name:g.weight for g in v.groups})
  ff.extend([tuple(offset+i for i in f.vertices) for f in ob.data.polygons])
 return BVHTree.FromPolygons(vv,ff),vv,ff,weightrows
def conform(ob,support,offset=.0015,subdivide=False):
 tree,verts,faces,wrows=support
 if subdivide and not ob.get('tube_sides'):
  bm=bmesh.new();bm.from_mesh(ob.data);bm.normal_update()
  # The already-solidified panel has a front and back. Keep its outward skin
  # before draping so the two surfaces cannot collapse onto one another.
  doomed=[]
  for f in bm.faces:
   center=f.calc_center_median();hit,normal,fi,dist=tree.find_nearest(center)
   if normal is not None and f.normal.dot(normal)<.20:doomed.append(f)
  bmesh.ops.delete(bm,geom=doomed,context='FACES')
  bmesh.ops.triangulate(bm,faces=list(bm.faces));bmesh.ops.subdivide_edges(bm,edges=list(bm.edges),cuts=4,use_grid_fill=True)
  bm.to_mesh(ob.data);bm.free()
 distances=[];newcoords={}
 if ob.get('tube_sides'):
  sides=ob['tube_sides']
  for first in range(0,len(ob.data.vertices),sides):
   ring=list(ob.data.vertices)[first:first+sides]
   center=sum((v.co for v in ring),Vector())/len(ring);hit,normal,fi,dist=tree.find_nearest(center)
   for v in ring:newcoords[v.index]=hit+normal*offset+(v.co-center)
 elif ob.name.startswith(('Antique coat button','Button inset','Shirt mother','Lace eyelet')):
  center=sum((v.co for v in ob.data.vertices),Vector())/len(ob.data.vertices);hit,normal,fi,dist=tree.find_nearest(center)
  for v in ob.data.vertices:newcoords[v.index]=hit+normal*offset+(v.co-center)
 for g in list(ob.vertex_groups):ob.vertex_groups.remove(g)
 groupcache={}
 for v in ob.data.vertices:
  hit,normal,fi,dist=tree.find_nearest(v.co)
  if hit is None:raise RuntimeError('Missing support for '+ob.name)
  distances.append(dist);v.co=newcoords.get(v.index,hit+normal*offset)
  ids=faces[fi];iw=[1/max(.000001,(verts[i]-hit).length) for i in ids];total=sum(iw);ws={}
  for i,w in zip(ids,iw):
   for key,val in wrows[i].items():ws[key]=ws.get(key,0)+val*w/total
  for key,w in ws.items():
   if key not in groupcache:groupcache[key]=ob.vertex_groups.new(name=key)
   groupcache[key].add([v.index],w,'REPLACE')
 attachment_report.append({'object':ob.name,'max_original_gap_m':max(distances),'surface_offset_m':offset})
coat_support=support_bvh([coat]);shirt=bpy.data.objects.get('Open linen shirt');shirt_support=support_bvh([shirt]);chest_support=support_bvh([coat,shirt])
crown_support=support_bvh([bpy.data.objects.get('Dented crooked hat crown')])
for ob in list(parts):
 name=ob.name
 if name.startswith(('Coat patch','Coat vertical welt','Coat hem unraveling','Abraded coat yarn','Slant pocket welt')):
  conform(ob,coat_support,.0013,subdivide=name.startswith('Coat patch') and 'mending' not in name)
 elif name.startswith(('Waistcoat front','Shirt mother','Shirt placket')):conform(ob,shirt_support,.004,subdivide=name.startswith('Waistcoat front'))
 elif name.startswith(('Notched coat lapel','Lapel saddle seam','Antique coat button','Button inset')):conform(ob,chest_support,.006 if name.startswith('Notched') else .008,subdivide=name.startswith('Notched'))
 elif name.startswith('Open linen collar'):conform(ob,shirt_support,.005,subdivide=True)
 elif name.startswith(('Hat burgundy patch','Hat crown darn','Felt moth hole','Faded hat band')):conform(ob,crown_support,.003,subdivide='patch' in name or 'darn' in name)
# Knees, elbows, and boot repairs each use their own articulated support.
for side in ['L','R']:
 leg_support=support_bvh([bpy.data.objects.get('Worn trouser leg '+side)])
 arm_support=support_bvh([bpy.data.objects.get('Wrinkled full sleeve '+side)])
 boot_support=support_bvh([bpy.data.objects.get('Sculpted boot toe '+side)])
 for ob in list(parts):
  if ob.name.startswith(('Knee repair '+side,'Shin repair '+side)):conform(ob,leg_support,.0014,subdivide='mending' not in ob.name)
  elif ob.name.startswith('Elbow darn '+side):conform(ob,arm_support,.0014,subdivide='mending' not in ob.name)
 # Boot lacing is attached to the leather upper, with rings placed on the surface.
 for ob in list(parts):
  if not ob.vertex_groups.get('Foot.'+side):continue
  if ob.name.startswith(('Boot tongue','Lace eyelet','Scuffed toe repair')):conform(ob,boot_support,.003,subdivide=ob.name.startswith(('Boot tongue','Scuffed')))
  elif ob.name.startswith(('Crossed bootlace','Lace bow')):conform(ob,boot_support,.005)
# Keep ears proportionate to the reference and seat them closer to the skull.
for ob in parts:
 if ob.name.startswith(('Ear pinna','Ear concha','Ear helix')):
  for v in ob.data.vertices:
   side=1 if v.co.x>0 else -1
   v.co.x=side*.127+(v.co.x-side*.139)*.80
   v.co.z=1.773+(v.co.z-1.773)*.80
# The upper boot flexes with the shin; lower shoe and sole follow the foot.
# A 7cm overlap under the trouser cuff prevents gaps in deep ankle flexion.
for side in ['L','R']:
 for ob in parts:
  fg=ob.vertex_groups.get('Foot.'+side)
  if fg is None:continue
  sg=ob.vertex_groups.get('Shin.'+side) or ob.vertex_groups.new(name='Shin.'+side)
  for v in ob.data.vertices:
   t=max(0,min(1,(v.co.z-.125)/.105));t=t*t*(3-2*t)
   fg.add([v.index],1-t,'REPLACE');sg.add([v.index],t,'REPLACE')
# Eliminate floating freestanding wrinkle wires by seating them on the skin.
face_support=support_bvh([bpy.data.objects.get('Continuous aged facial surface')])
for ob in list(parts):
 if ob.name.startswith(('Under-eye age crease','Nasolabial carved fold','Forehead weather lines')):conform(ob,face_support,.00035)
# Moustache is a relief on the muzzle. Roots and side tips seat into the face.
for ob in list(parts):
 if ob.name.startswith('Moustache strand'):
  # Move each cross-section as a unit. Projecting every vertex independently
  # collapses the tube into coincident front/back sheets and causes flicker.
  sides=ob['tube_sides']
  for start in range(0,len(ob.data.vertices),sides):
   ring=list(ob.data.vertices[start:start+sides])
   center=sum((v.co for v in ring),Vector())/len(ring)
   target=face_y(center.x,center.z)-.0035-.002*math.sin((center.z-1.67)*55)
   for v in ring:v.co.y+=target-center.y
# The nostrils are surface recesses, never freestanding beads.
for ob in list(parts):
 if ob.name.startswith('Nostril'):
  parts.remove(ob);bpy.data.objects.remove(ob,do_unlink=True)
# Unite the nasal bridge, bulb, alae, and ears with the head skin, so no disjoint
# underside of an attached sphere can be exposed in a profile or low camera.
merge=[o for o in parts if o.name.startswith(('Continuous aged facial surface','Pendulous red nose','Nasal wing','Ear pinna','Continuous orbital lids'))]
bpy.ops.object.select_all(action='DESELECT')
for o in merge:o.select_set(True);parts.remove(o)
bpy.context.view_layer.objects.active=merge[0];bpy.ops.object.join();face=bpy.context.object;face.name='Unified face nose ears'
rem=face.modifiers.new('Continuous sculpt union','REMESH');rem.mode='VOXEL';rem.voxel_size=.0016;rem.use_smooth_shade=True;bpy.ops.object.modifier_apply(modifier=rem.name)
sm=face.modifiers.new('Polish union transitions','SMOOTH');sm.factor=.6;sm.iterations=12;bpy.ops.object.modifier_apply(modifier=sm.name)
for g in list(face.vertex_groups):face.vertex_groups.remove(g)
g=face.vertex_groups.new(name='Head');g.add(list(range(len(face.data.vertices))),1,'REPLACE');parts.append(face)
face.data.materials.append(M['nose'])
face.data.update()
for f in face.data.polygons:
 c=f.center
 if abs(c.x)<.035 and 1.709<c.z<1.774 and c.y<-.115:
  for mi,mat in enumerate(face.data.materials):
   if mat==M['nose']:f.material_index=mi
# Orbital cavities pass through the unified facial skin, never through the eye.
for cutter in eye_cutters:
 bpy.context.view_layer.objects.active=face
 mod=face.modifiers.new('Recessed orbital aperture','BOOLEAN');mod.operation='DIFFERENCE';mod.object=cutter
 bpy.ops.object.modifier_apply(modifier=mod.name);bpy.data.objects.remove(cutter,do_unlink=True)
# Carve small nostril cavities into the underside of the continuous nasal form.
for sign in [-1,1]:
 x=sign*.024;z=1.703;y=min(face_y(x,z),-.120-.035*math.sqrt(max(0,1-((x-.001)/.033)**2-((z-1.724)/.034)**2)))
 bpy.ops.mesh.primitive_uv_sphere_add(segments=24,ring_count=16,location=(x,y-.001,z))
 cutter=bpy.context.object;cutter.scale=(.006,.009,.004)
 bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
 bpy.context.view_layer.objects.active=face
 mod=face.modifiers.new('Carved nostril cavity','BOOLEAN');mod.operation='DIFFERENCE';mod.object=cutter
 bpy.ops.object.modifier_apply(modifier=mod.name);bpy.data.objects.remove(cutter,do_unlink=True)
# Boolean-created interior vertices remain rigidly attached to Head.
g=face.vertex_groups.get('Head');g.add(list(range(len(face.data.vertices))),1,'REPLACE')
# Bake the revised face so its cavity shading survives diffuse indoor light.
if not os.environ.get('RATCH_SKIP_FACE_BAKE'):
 sys.path.insert(0,str(OUT))
 from bake_face_material import bake_face_material
 bake_face_material(face,OUT,M)
(PRE/'attachment_report.json').write_text(json.dumps(attachment_report,indent=2))
# Lower and broaden the head to remove the exposed toy-like neck.
for o in parts:
 if o.vertex_groups.get('Head'):
  for v in o.data.vertices:
   if v.co.z>1.515:
    v.co.x*=1.10;v.co.y*=1.06;v.co.z-=.024
# Merge once: thousands of fine details stay manageable in Blender and in-game.
bpy.ops.object.select_all(action='DESELECT')
for o in parts:o.select_set(True)
bpy.context.view_layer.objects.active=parts[0];bpy.ops.object.join();body=bpy.context.object;body.name='Ratch_ApprovalMesh'
mod=body.modifiers.new('Inherited Pip Fuse skin','ARMATURE');mod.object=rig;body.parent=rig
rig.data.pose_position='POSE';bpy.context.scene.frame_set(0)
# No animation is active in the neutral authoring presentation.
for p in rig.pose.bones:p.matrix_basis.identity()
bpy.ops.object.select_all(action='DESELECT');body.select_set(True);rig.select_set(True);bpy.context.view_layer.objects.active=rig
bpy.ops.export_scene.gltf(filepath=str(EXP/'ratch_geometry.glb'),use_selection=True,export_format='GLB',export_animations=False,export_yup=True)
scene=bpy.context.scene;scene.render.engine='CYCLES';scene.cycles.samples=32;scene.cycles.use_denoising=True
scene.render.resolution_x=1200;scene.render.resolution_y=1500;scene.render.resolution_percentage=100
scene.world=bpy.data.worlds.new('Ratch warm studio');scene.world.use_nodes=True;scene.world.node_tree.nodes['Background'].inputs[0].default_value=(.34,.32,.29,1);scene.world.node_tree.nodes['Background'].inputs[1].default_value=.4
material('studio',(.26,.245,.219),.95)
mesh('Studio floor',[(-200,-200,-.002),(200,-200,-.002),(200,200,-.002),(-200,200,-.002)],[(0,1,2,3)],'studio',None)
def aim(o,target):o.rotation_euler=(Vector(target)-o.location).to_track_quat('-Z','Y').to_euler()
for loc,power,size in [((-3,-4,5),430,3),((3,-1,3),220,3),((1,3,4),480,2.5)]:
 bpy.ops.object.light_add(type='AREA',location=loc);o=bpy.context.object;o.data.energy=power;o.data.shape='DISK';o.data.size=size;aim(o,(0,0,1.1))
bpy.ops.object.camera_add(location=(2.8,-6,2.8));cam=bpy.context.object;scene.camera=cam;cam.data.type='ORTHO'
scene.view_settings.view_transform='AgX';scene.view_settings.look='AgX - Medium High Contrast'
cam.data.ortho_scale=2.48;aim(cam,(0,0,1.09));bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'ratch.blend'),compress=True)
report={'vertices':len(body.data.vertices),'triangles':sum(len(p.vertices)-2 for p in body.data.polygons),'materials':len(body.data.materials),'bones':[b.name for b in rig.data.bones],'approval_status':'pending','reference':'art/concepts/ratch_reference.png'}
(PRE/'geometry_report.json').write_text(json.dumps(report,indent=2))
for name,loc,scale,target in [('side_audit',(6,0,1.78),2.42,(0,0,1.09)),('face_side_audit',(6,-.05,1.80),.77,(0,0,1.80)),('front',(0,-6,2.0),2.40,(0,0,1.08)),('three_quarter',(2.8,-6,2.7),2.48,(0,0,1.09)),('back',(0,6,1.7),2.48,(0,0,1.09)),('profile_left',(-6,0,1.8),2.48,(0,0,1.09)),('profile_right',(6,0,1.8),2.48,(0,0,1.09)),('head_profile',(6,-.3,1.85),.76,(0,0,1.80)),('head_low',(1,-6,.8),.80,(0,0,1.79)),('head_high',(1,-5,4),.83,(0,0,1.80)),('portrait',(.75,-6,2.08),.77,(0,-.015,1.82))]:
 if os.environ.get('RATCH_VIEWS') and name not in os.environ['RATCH_VIEWS'].split(','):continue
 cam.location=loc;cam.data.ortho_scale=scale;aim(cam,target);scene.render.filepath=str(PRE/(name+'.png'));bpy.ops.render.render(write_still=True)
print('RATCH_COMPLETE',json.dumps(report))
