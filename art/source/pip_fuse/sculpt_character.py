"""Pip Fuse: continuous surface construction and restrained planar character design."""
def mesh_obj(name,vs,fs,mat,bone):
 d=bpy.data.meshes.new(name);d.from_pydata(vs,[],fs);d.update();o=bpy.data.objects.new(name,d);bpy.context.collection.objects.link(o)
 bpy.ops.object.select_all(action='DESELECT');o.select_set(True);bpy.context.view_layer.objects.active=o
 return finish(o,name,mat,bone)
def path(name,points,r,mat,bone):
 d=bpy.data.curves.new(name,'CURVE');d.dimensions='3D';d.resolution_u=12;d.bevel_depth=r;d.bevel_resolution=2
 sp=d.splines.new('BEZIER');sp.bezier_points.add(len(points)-1)
 for p,co in zip(sp.bezier_points,points):p.co=co;p.handle_left_type='AUTO';p.handle_right_type='AUTO'
 o=bpy.data.objects.new(name,d);bpy.context.collection.objects.link(o);bpy.ops.object.select_all(action='DESELECT');o.select_set(True);bpy.context.view_layer.objects.active=o;bpy.ops.object.convert(target='MESH');return finish(bpy.context.object,name,mat,bone)
def sample(rows,z):
 for k in range(len(rows)-1):
  if z<=rows[k+1][0]:
   h=rows[k+1][0]-rows[k][0];t=max(0,min(1,(z-rows[k][0])/h))
   a=max(0,k-1);b=min(len(rows)-1,k+2)
   out=[]
   for j in range(1,len(rows[0])):
    m0=(rows[k+1][j]-rows[a][j])/(rows[k+1][0]-rows[a][0])
    m1=(rows[b][j]-rows[k][j])/(rows[b][0]-rows[k][0])
    out.append((2*t**3-3*t*t+1)*rows[k][j]+(t**3-2*t*t+t)*h*m0+(-2*t**3+3*t*t)*rows[k+1][j]+(t**3-t*t)*h*m1)
   return out
 return list(rows[-1][1:])
def loft(name,rows,mat,bone,steps=48,N=48,power=1.0):
 vs=[];fs=[]
 for j in range(steps):
  z=rows[0][0]+(rows[-1][0]-rows[0][0])*j/(steps-1);x,y,rx,ry=sample(rows,z)
  for i in range(N):
   t=i*math.tau/N;c=math.cos(t);q=math.sin(t);vs.append((x+rx*math.copysign(abs(c)**power,c),y+ry*math.copysign(abs(q)**power,q),z))
 for j in range(steps-1):
  for i in range(N):a=j*N+i;b=j*N+(i+1)%N;fs.append((a,b,b+N,a+N))
 fs.extend([tuple(reversed(range(N))),tuple((steps-1)*N+i for i in range(N))]);return mesh_obj(name,vs,fs,mat,bone)
def panel(name,coords,mat,bone,thick=.009):
 o=mesh_obj(name,coords,[tuple(range(len(coords)))],mat,bone);m=o.modifiers.new('Tailoring thickness','SOLIDIFY');m.thickness=thick;bpy.ops.object.modifier_apply(modifier=m.name)
 m=o.modifiers.new('Finished edges','BEVEL');m.width=.004;m.segments=3;bpy.ops.object.modifier_apply(modifier=m.name);return o
def gauss(x,c,w):return math.exp(-((x-c)/w)**2)
# Palette is deliberately dark and graphic; polished metal is limited to hardware.
for key,col,rough,metal in [('jacket',(.24,.036,.018),.82,0),('pants',(.027,.048,.06),.9,0),('skin',(.48,.235,.12),.62,0),('nose',(.40,.145,.075),.7,0),('hair',(.06,.021,.009),.72,0),('copper',(.16,.049,.014),.65,0),('leather',(.035,.021,.015),.7,0),('trim',(.46,.27,.07),.38,.55),('cream',(.54,.43,.29),.85,0),('steel',(.16,.20,.21),.34,.75),('white',(.77,.78,.68),.25,0),('iris',(.018,.09,.07),.25,0)]:
 m=M[key];m.diffuse_color=(*col,1);p=m.node_tree.nodes.get('Principled BSDF');p.inputs['Base Color'].default_value=(*col,1);p.inputs['Roughness'].default_value=rough;p.inputs['Metallic'].default_value=metal
# Anatomically tapered, high-collared work jacket. Silhouette comes from the cut.
loft('Tailored jacket shell',[(.995,0,.015,.188,.126),(1.04,0,.015,.2,.13),(1.13,0,.02,.223,.141),(1.3,0,.022,.277,.157),(1.405,0,.025,.288,.145),(1.455,0,.021,.23,.13),(1.49,0,.015,.103,.09)],'jacket','Spine',power=.78)
loft('Neck',[(1.46,0,.012,.077,.07),(1.57,0,.021,.067,.063),(1.62,0,.015,.075,.067)],'skin','Head',steps=20)
loft('Raised jacket collar',[(1.445,0,.02,.117,.103),(1.485,0,.023,.107,.096),(1.522,0,.025,.09,.081)],'jacket','Spine',steps=14)
# Fitted charcoal chest inset and angled folded lapels.
panel('Undershirt', [(-.06,-.112,1.485),(.06,-.112,1.485),(.08,-.143,1.365),(-.025,-.143,1.36)],'pants','Spine')
panel('Left folded lapel',[(-.085,-.083,1.50),(-.175,-.113,1.425),(-.095,-.155,1.32),(-.032,-.162,1.394)],'cream','Spine')
panel('Right folded lapel',[(.083,-.08,1.50),(.15,-.119,1.43),(.07,-.153,1.34),(.03,-.158,1.40)],'jacket','Spine')
path('Jacket central welt',[(.028,-.137,1.04),(.019,-.146,1.16),(.014,-.152,1.31),(.016,-.15,1.38)],.005,'leather','Spine')
for i in range(20):box('Zip teeth',(.023,-.154,1.09+i*.011),(.008,.006,.003),'trim','Spine',.001)
box('Zip slider',(.019,-.166,1.315),(.017,.01,.034),'steel','Spine',.004)
for s in [-1,1]:
 path('Chest seam',[(s*.215,-.091,1.404),(s*.204,-.13,1.32),(s*.166,-.125,1.06)],.002,'leather','Spine')
 path('Shoulder tailoring',[(s*.12,-.08,1.462),(s*.213,-.094,1.434),(s*.276,-.08,1.40)],.002,'cream','Spine')
# Waistband and flat seat; overlapping surfaces stay inside the garment silhouette.
loft('Trouser pelvis',[(.80,0,.025,.177,.115),(.86,0,.016,.205,.132),(.96,0,.015,.201,.13),(1.01,0,.015,.19,.128)],'pants','Hips',steps=24,power=.7)
loft('Leather waist belt',[(.982,0,.015,.201,.141),(1.025,0,.015,.201,.141)],'leather','Hips',steps=3,power=.72)
box('Brass belt frame',(.025,-.136,1.003),(.077,.025,.054),'trim','Hips',.008)
box('Belt buckle inset',(.025,-.151,1.003),(.05,.006,.032),'leather','Hips',.004)
# Sleeves and forearms are shaped lofts, with muscle and fabric volume built into the surface.
for s in [-1,1]:
 side='L' if s<0 else 'R';arm='Arm.'+side;fore='Forearm.'+side;hand='Hand.'+side;thigh='Thigh.'+side;shin='Shin.'+side
 loft('Cut sleeve '+side,[(1.115,s*.413,.012,.096,.086),(1.17,s*.397,.016,.103,.093),(1.28,s*.35,.024,.116,.116),(1.365,s*.301,.026,.116,.125),(1.425,s*.26,.025,.085,.087),(1.441,s*.25,.025,.04,.055)],'jacket',arm,steps=40,power=.87)
 loft('Rolled sleeve cuff '+side,[(1.108,s*.416,.012,.101,.089),(1.13,s*.409,.012,.102,.09),(1.15,s*.403,.015,.1,.09)],'cream',arm,steps=10)
 loft('Forearm anatomy '+side,[(.902,s*.479,-.005,.062,.059),(.953,s*.47,.005,.073,.067),(1.04,s*.445,.013,.09,.079),(1.09,s*.428,.012,.083,.073),(1.133,s*.411,.012,.071,.067)],'skin',fore,steps=36)
 # Supple glove wrist and a proportioned hand, not a giant boxing mitt.
 loft('Glove wrist '+side,[(.859,s*.49,-.008,.074,.062),(.905,s*.48,-.002,.071,.064),(.932,s*.476,.002,.073,.065)],'leather',hand,steps=12,power=.78)
 loft('Gloved palm '+side,[(.762,s*.508,-.016,.060,.054),(.8,s*.506,-.018,.077,.059),(.858,s*.493,-.011,.075,.060),(.88,s*.488,-.01,.064,.056)],'leather',hand,steps=20,power=.73)
 for i in range(4):
  x=s*.508+(i-1.5)*.032;top=.808;bottom=.745+abs(i-1.4)*.008
  loft('Curled finger',[(bottom,x,-.027,.011,.022),(bottom+.013,x,-.036,.015,.026),(top-.013,x,-.04,.015,.028),(top,x,-.032,.012,.023)],'leather',hand,steps=12,N=20)
  path('Finger seam',[(x-.008,-.064,bottom+.024),(x,-.069,bottom+.021),(x+.008,-.064,bottom+.024)],.0012,'black',hand)
 thumb=ell('Opposing thumb',(s*.437,-.028,.814),(.026,.039,.051),'leather',hand);thumb.rotation_euler.y=-s*.4
 panel('Glove back reinforcement',[(s*.463,-.067,.858),(s*.541,-.067,.842),(s*.548,-.079,.807),(s*.467,-.079,.82)],'pants',hand,thick=.003)
 path('Wrist stitch',[(s*.425,-.026,.89),(s*.48,-.069,.892),(s*.538,-.029,.89)],.0015,'cream',hand)
 loft('Trouser thigh '+side,[(.515,s*.157,.013,.078,.078),(.58,s*.15,.015,.09,.084),(.71,s*.132,.019,.104,.101),(.83,s*.114,.022,.111,.112),(.91,s*.105,.02,.103,.113)],'pants',thigh,steps=36,power=.85)
 loft('Trouser lower leg '+side,[(.195,s*.175,.017,.067,.07),(.25,s*.17,.015,.072,.073),(.34,s*.169,.018,.079,.078),(.46,s*.16,.018,.082,.08),(.55,s*.153,.014,.077,.078)],'pants',shin,steps=30)
 path('Pressed trouser seam',[(s*.226,-.026,.86),(s*.226,-.04,.7),(s*.228,-.03,.56)],.0016,'steel',thigh)
 box('Worn knee panel',(s*.158,-.064,.51),(.127,.031,.13),'leather',shin,.022)
 # Sculpted boot with a lower profile, inset tongue, layered welt and heel.
 box('Boot sole',(s*.18,-.077,.035),(.20,.329,.055),'black',shin,.023)
 box('Boot leather upper',(s*.18,-.076,.108),(.193,.306,.142),'leather',shin,.046)
 loft('Boot ankle',[(.10,s*.179,.02,.081,.075),(.185,s*.175,.018,.076,.07),(.244,s*.175,.019,.079,.073)],'leather',shin,steps=18,power=.78)
 box('Boot tongue',(s*.18,-.066,.195),(.096,.027,.102),'pants',shin,.012)
 for i in range(4):
  z=.163+i*.017
  path('Cross lace',[(s*.144,-.083,z),(s*.213,-.084,z+.013)],.0027,'cream',shin)
 path('Toe stitched welt',[(s*.09,-.15,.09),(s*.18,-.23,.09),(s*.27,-.15,.09)],.0015,'cream',shin)
 panel('Toe reinforcement',[(s*.105,-.182,.125),(s*.18,-.229,.12),(s*.255,-.182,.125),(s*.253,-.194,.07),(s*.107,-.194,.07)],'steel',shin,.004)
# Continuous facial surface: jaw, cheekbones, eye sockets, brow and nose are one mesh.
head_rows=[(1.552,.048,.046,-.003),(1.584,.087,.066,-.009),(1.62,.116,.089,.009),(1.673,.127,.104,.017),(1.732,.139,.112,.022),(1.79,.132,.11,.028),(1.847,.127,.105,.031),(1.89,.105,.091,.035),(1.912,.055,.048,.033),(1.92,.005,.005,.031)]
def face_y(x,z):
 rx,ry,cy=sample(head_rows,z);v=max(0,1-(x/max(.001,rx))**2);y=cy-ry*v**.29
 # Restrained cheek planes, strong brow ridge and integrated straight nose.
 y-=.012*(gauss(x,.087,.03)+gauss(x,-.087,.03))*gauss(z,1.737,.028)
 y+=.012*(gauss(x,.058,.033)+gauss(x,-.058,.033))*gauss(z,1.783,.019)
 y-=.016*(gauss(x,.055,.034)+gauss(x,-.055,.034))*gauss(z,1.807,.013)
 y-=.066*gauss(x,0,.024)*gauss(z,1.739,.051)
 y-=.026*gauss(x,0,.034)*gauss(z,1.711,.016)
 y-=.018*gauss(x,0,.068)*gauss(z,1.642,.025)
 return y
vs=[];fs=[];N=96;H=96
for j in range(H):
 z=head_rows[0][0]+(head_rows[-1][0]-head_rows[0][0])*j/(H-1);rx,ry,cy=sample(head_rows,z)
 for i in range(N):
  t=math.tau*i/N;x=rx*math.cos(t);y=cy+ry*math.sin(t)
  if math.sin(t)<0:y=face_y(x,z)
  vs.append((x,y,z))
for j in range(H-1):
 for i in range(N):a=j*N+i;b=j*N+(i+1)%N;fs.append((a,b,b+N,a+N))
fs.extend([tuple(reversed(range(N))),tuple((H-1)*N+i for i in range(N))]);mesh_obj('Continuous facial sculpt',vs,fs,'skin','Head')
# Compact eyes set beneath the brow; eyelids use ribbons that meet the facial surface.
for s in [-1,1]:
 cx=s*.058;z=1.783;cy=face_y(cx,z)-.008
 ell('Inset sclera',(cx,cy,z),(.033,.013,.015),'white','Head',seg=40)
 ell('Jade iris',(cx,cy-.012,z+.001),(.009,.0025,.010),'iris','Head')
 ell('Eye pupil',(cx,cy-.015,z+.001),(.004,.0015,.006),'black','Head')
 ell('Corneal glint',(cx-.002,cy-.017,z+.005),(.0017,.001,.0017),'white','Head')
 for upper in [True,False]:
  vv=[];ff=[]
  for i in range(25):
   t=i/24;x=cx+(t-.5)*.067;zz=z+(1 if upper else -1)*.015*math.sin(math.pi*t)
   yy=cy-.012*math.sin(math.pi*t)
   vv.extend([(x,yy-.0005,zz),(x,face_y(x,zz+(.007 if upper else -.005))-.002,zz+(.007 if upper else -.005))])
  for i in range(24):ff.append((i*2,i*2+1,i*2+3,i*2+2))
  mesh_obj('Integrated eyelid',vv,ff,'skin','Head')
 # Tapered eyebrow ribbon, with an asymmetric lift.
 coords=[]
 for i in range(15):
  t=i/14;x=cx+(t-.5)*.072;zz=1.811+.008*math.sin(math.pi*t)+s*.005*(t-.5);y=face_y(x,zz)-.003
  width=.006*(math.sin(math.pi*t)**.5)+.0005;coords.extend([(x,y,zz-width/2),(x,y-.001,zz+width/2)])
 mesh_obj('Sculpted tapered eyebrow',coords,[(i*2,i*2+1,i*2+3,i*2+2) for i in range(14)],'hair','Head')
 ell('Ear shell',(s*.139,.027,1.762),(.022,.026,.049),'skin','Head')
 path('Ear inner helix',[(s*.149,.003,1.738),(s*.154,-.001,1.78),(s*.14,.003,1.794)],.0025,'nose','Head')
 ell('Nostril recess',(s*.015,face_y(s*.015,1.712)-.001,1.707),(.006,.002,.003),'hair','Head')
# A thin asymmetric mouth and modeled vermilion surface; no protruding tube lips.
vv=[];ff=[]
for i in range(40):
 t=i/39;x=(t-.5)*.108;z=1.67+.009*t*t;y=face_y(x,z)-.0017;w=.0018*math.sin(math.pi*t)
 vv.extend([(x,y,z-w),(x,y-.0005,z+w)])
for i in range(39):ff.append((i*2,i*2+1,i*2+3,i*2+2))
mesh_obj('Mouth expression line',vv,ff,'hair','Head')
# Swept hair is one scalp surface with shallow directional ridges, not separate blobs.
vs=[];fs=[];NX=64;NY=48
for j in range(NY):
 v=j/(NY-1)
 for i in range(NX):
  u=i/(NX-1);x=-.128+u*.256+.035*math.sin(math.pi*v)
  y=-.085+v*.215
  z=1.849+.091*math.sin(math.pi*(v*.75+.15))*(math.sin(math.pi*u)**.25)+.018*(1-u)*math.sin(math.pi*v)
  z+=.0023*math.sin(9*math.pi*u+v*2)*math.sin(math.pi*v)
  if j==0:z-=.018*(.5+.5*math.cos(u*5*math.pi))
  vs.append((x,y,z))
for j in range(NY-1):
 for i in range(NX-1):a=j*NX+i;fs.append((a,a+1,a+1+NX,a+NX))
o=mesh_obj('Single sculpted swept hairstyle',vs,fs,'copper','Head');mod=o.modifiers.new('Hair shell thickness','SOLIDIFY');mod.thickness=.018;bpy.ops.object.modifier_apply(modifier=mod.name)
for s in [-1,1]:
 panel('Sharp sideburn',[(s*.128,.047,1.867),(s*.13,-.006,1.832),(s*.124,-.007,1.797),(s*.13,.047,1.815)],'hair','Head',.006)
# Signature equipment is compact and functional, with a clean hierarchy of details.
strap=panel('Diagonal utility harness',[(-.193,-.109,1.436),(-.151,-.132,1.441),(.109,-.144,1.043),(.067,-.145,1.038)],'leather','Spine',.012)
path('Harness edge stitch',[(-.174,-.143,1.429),(-.074,-.163,1.275),(.087,-.155,1.06)],.0013,'cream','Spine')
box('Harness slide buckle',(-.051,-.166,1.247),(.055,.022,.052),'steel','Spine',.007)
box('Chest tool pocket',(.159,-.132,1.29),(.085,.03,.104),'jacket','Spine',.01)
panel('Chest pocket flap',[(.11,-.151,1.351),(.203,-.144,1.35),(.197,-.162,1.323),(.118,-.166,1.32)],'cream','Spine',.004)
for x in [.139,.168]:link('Pocket tool',(x,-.137,1.335),(x,-.132,1.389),.005,.005,'steel','Spine')
box('Utility hip case',(-.227,.016,.974),(.073,.133,.165),'leather','Hips',.016)
box('Case closing strap',(-.269,.013,.997),(.012,.045,.101),'cream','Hips',.004)
# Compact shoulder dial, straps and inset markings.
link('Pressure dial casing',(-.286,-.082,1.407),(-.286,-.125,1.407),.048,.048,'steel','Arm.L')
ell('Inset pressure dial',(-.286,-.128,1.407),(.040,.005,.040),'cream','Arm.L')
for i in range(11):
 t=math.radians(-130+i*26);x=-.286+math.sin(t)*.030;z=1.407+math.cos(t)*.030
 o=box('Dial scale',(x,-.134,z),(.0015,.002,.005),'black','Arm.L',.0005);o.rotation_euler.y=t
path('Dial needle',[(-.29,-.136,1.4),(-.276,-.136,1.43)],.0018,'jacket','Arm.L')
path('Pressure hose',[(-.319,-.067,1.414),(-.33,.08,1.43),(-.257,.157,1.317),(-.195,.16,1.218)],.010,'leather','Spine')
box('Rear pressure regulator',(-.13,.177,1.25),(.14,.075,.177),'steel','Spine',.014)
for z in [1.2,1.225,1.25,1.275]:box('Regulator vent',(-.13,.217,z),(.095,.006,.005),'black','Spine',.002)
# Crisp normals on manufactured components; skin and textiles retain smooth shading.
for o in parts:
 if o.name.startswith(('Boot sole','Brass belt','Harness slide','Regulator','Rear pressure','Zip','Case')):
  bpy.context.view_layer.objects.active=o
  mod=o.modifiers.new('Weighted manufactured normals','WEIGHTED_NORMAL');mod.keep_sharp=True
  try:bpy.ops.object.modifier_apply(modifier=mod.name)
  except RuntimeError:pass

# Garment overlays stand clear of the underlying shell instead of intersecting it.
for o in parts:
 if o.name.startswith(('Left folded lapel','Right folded lapel','Undershirt','Diagonal utility harness','Chest pocket flap')):
  o.location.y-=.035
 if o.name.startswith('Rolled sleeve cuff'):
  for v in o.data.vertices:
   side=-1 if v.co.x<0 else 1
   v.co.x=side*.41+(v.co.x-side*.41)*1.055
   v.co.y=.012+(v.co.y-.012)*1.075
