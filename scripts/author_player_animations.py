"""Author Pip/Fuse player animation and planted-foot leg IK.

Run with Python + numpy. Adds ankle joints and boot weights, replaces stance/contact/airborne clips,
and retimes locomotion in the existing GLB. Mesh positions, materials, and
unrelated clips survive.
The standing two-handed weapon pose is the source, so rerunning does not compound the edits.
"""
from pathlib import Path
import json
import math
import struct
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
PATH = ROOT / 'assets/models/pip_fuse/pip_fuse.glb'
HEIGHT = 1.95174
CROUCH_RATIO = .80
STANDING_HEAD = 1.95174


def quat_matrix(q):
    x, y, z, w = np.array(q) / np.linalg.norm(q)
    return np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],
                     [2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],
                     [2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]])


def matrix_quat(m):
    # Eigenvector form stays stable at the imported rig's 180-degree joints.
    k = np.array([[m[0,0]-m[1,1]-m[2,2],m[0,1]+m[1,0],m[0,2]+m[2,0],m[2,1]-m[1,2]],
                  [m[0,1]+m[1,0],m[1,1]-m[0,0]-m[2,2],m[1,2]+m[2,1],m[0,2]-m[2,0]],
                  [m[0,2]+m[2,0],m[1,2]+m[2,1],m[2,2]-m[0,0]-m[1,1],m[1,0]-m[0,1]],
                  [m[2,1]-m[1,2],m[0,2]-m[2,0],m[1,0]-m[0,1],m.trace()]]) / 3
    _, v = np.linalg.eigh(k)
    q = v[:, -1]
    return q if q[3] >= 0 else -q


def align(a, b):
    a, b = a / np.linalg.norm(a), b / np.linalg.norm(b)
    axis = np.cross(a,b)
    c = np.dot(a,b)
    if c < -1 + 1e-8:
        # Antiparallel vectors need an explicit half turn: their cross product
        # is zero, so the ordinary Rodrigues formula cannot choose an axis.
        basis = np.eye(3)[np.argmin(np.abs(a))]
        axis = np.cross(a, basis)
        axis /= np.linalg.norm(axis)
        return 2 * np.outer(axis, axis) - np.eye(3)
    skew = np.array([[0,-axis[2],axis[1]],[axis[2],0,-axis[0]],[-axis[1],axis[0],0]])
    return np.eye(3) + skew + skew @ skew / max(1e-8, 1+c)


def main():
    raw = PATH.read_bytes()
    size = struct.unpack_from('<I',raw,12)[0]
    doc = json.loads(raw[20:20+size])
    binary = bytearray(raw[28+size:])
    nodes = doc['nodes']
    names = {n.get('name'):i for i,n in enumerate(nodes)}
    parents = {c:i for i,n in enumerate(nodes) for c in n.get('children',[])}
    def accessor(index):
        a = doc['accessors'][index]; v = doc['bufferViews'][a['bufferView']]
        count = {'SCALAR':1,'VEC3':3,'VEC4':4,'MAT4':16}[a['type']]
        return np.frombuffer(binary, dtype={5126:'<f4',5123:'<u2',5121:'u1'}[a['componentType']], count=a['count']*count,
                             offset=v.get('byteOffset',0)+a.get('byteOffset',0)).reshape(-1,count).copy()
    def add_accessor(values,kind,component=5126):
        values=np.asarray(values,dtype={5126:'<f4',5123:'<u2'}[component])
        while len(binary)%4: binary.append(0)
        offset=len(binary); binary.extend(values.tobytes())
        doc['bufferViews'].append({'buffer':0,'byteOffset':offset,'byteLength':values.nbytes})
        a={'bufferView':len(doc['bufferViews'])-1,'componentType':component,'count':len(values),'type':kind}
        if kind=='SCALAR': a.update(min=[float(values.min())],max=[float(values.max())])
        doc['accessors'].append(a); return len(doc['accessors'])-1
    base = [{k:np.array(n.get(k,d),dtype=float) for k,d in
             [('translation',[0,0,0]),('rotation',[0,0,0,1]),('scale',[1,1,1])]} for n in nodes]
    source = next(a for a in doc['animations'] if a['name']=='Idle_Gun_TwoHanded')
    animated = set()
    for c in source['channels']:
        s = source['samplers'][c['sampler']]; target=c['target']
        base[target['node']][target['path']] = accessor(s['output'])[0]
        animated.add(target['node'])
    def globals_for(pose):
        result = {}
        def resolve(i):
            if i in result: return result[i]
            p=pose[i]; local=np.eye(4)
            local[:3,:3]=quat_matrix(p['rotation']) @ np.diag(p['scale'])
            local[:3,3]=p['translation']
            result[i] = resolve(parents[i]) @ local if i in parents else local
            return result[i]
        for i in range(len(nodes)): resolve(i)
        return result
    standing = globals_for(base)
    # The original rig binds each boot to its shin. Add ankle joints so a
    # bent knee does not tip the whole boot through the floor.
    skin=doc['skins'][0]
    if 'Foot.L' not in names:
        inverse_binds=accessor(skin['inverseBindMatrices']).tolist()
        for side,x in [('L',-.18),('R',.18)]:
            parent=names['Shin.'+side]
            global_foot=np.eye(4);global_foot[:3,3]=[x,.14,0]
            local=np.linalg.inv(standing[parent]) @ global_foot
            node={'name':'Foot.'+side,'translation':local[:3,3].tolist(),
                  'rotation':matrix_quat(local[:3,:3]/np.linalg.norm(local[:3,:3],axis=0)).tolist()}
            index=len(nodes);nodes.append(node);nodes[parent].setdefault('children',[]).append(index)
            names[node['name']]=index;parents[index]=parent
            base.append({'translation':np.array(node['translation']), 'rotation':np.array(node['rotation']), 'scale':np.ones(3)})
            joint_index=len(skin['joints']);skin['joints'].append(index)
            inverse_binds.append(np.linalg.inv(global_foot).T.reshape(-1).tolist())
            shin_joint=skin['joints'].index(parent)
            for mesh in doc['meshes']:
                for primitive in mesh['primitives']:
                    attrs=primitive['attributes']
                    if 'JOINTS_0' not in attrs: continue
                    positions=accessor(attrs['POSITION']);joints=accessor(attrs['JOINTS_0']);weights=accessor(attrs['WEIGHTS_0'])
                    for v in range(len(positions)):
                        if int(joints[v,0])!=shin_joint: continue
                        amount=max(0.,min(1.,(.28-positions[v,1])/.10))
                        if amount<=0: continue
                        joints[v]=[joint_index,shin_joint,0,0];weights[v]=[amount,1-amount,0,0]
                    attrs['JOINTS_0']=add_accessor(joints,'VEC4',5123)
                    attrs['WEIGHTS_0']=add_accessor(weights,'VEC4')
        skin['inverseBindMatrices']=add_accessor(inverse_binds,'MAT4')
        standing=globals_for(base)
    animated.update(names[n] for n in ('Foot.L','Foot.R'))
    def set_global_rotation(pose,index,rotation):
        g=globals_for(pose)
        parent=g[parents[index]][:3,:3]
        parent=parent / np.linalg.norm(parent,axis=0)
        pose[index]['rotation']=matrix_quat(parent.T @ rotation)
    def solve_leg(pose,side,foot_target):
        thigh,knee,foot = [names[n+'.'+side] for n in ['Thigh','Shin','Foot']]
        g=globals_for(pose)
        hip=g[thigh][:3,3]
        l1=np.linalg.norm(standing[knee][:3,3]-standing[thigh][:3,3])
        l2=np.linalg.norm(standing[foot][:3,3]-standing[knee][:3,3])
        delta=foot_target-hip; distance=np.linalg.norm(delta); direction=delta/distance
        along=(l1*l1-l2*l2+distance*distance)/(2*distance)
        forward=np.array([0.,0.,1.]); bend=forward-direction*np.dot(forward,direction)
        bend/=np.linalg.norm(bend)
        knee_target=hip+direction*along+bend*math.sqrt(max(0,l1*l1-along*along))
        r=g[thigh][:3,:3]; r=r/np.linalg.norm(r,axis=0)
        set_global_rotation(pose,thigh,align(g[knee][:3,3]-hip,knee_target-hip) @ r)
        g=globals_for(pose); r=g[knee][:3,:3]; r=r/np.linalg.norm(r,axis=0)
        set_global_rotation(pose,knee,align(g[foot][:3,3]-g[knee][:3,3],foot_target-g[knee][:3,3]) @ r)
        # Feet retain their standing orientation instead of tipping with shins.
        r=standing[foot][:3,:3]; r=r/np.linalg.norm(r,axis=0)
        set_global_rotation(pose,foot,r)
    # Sample a neutral mesh once to ground the falling body throughout Death.
    death_base=[{k:v.copy() for k,v in p.items()} for p in base]
    idle=next(a for a in doc['animations'] if a['name']=='IDLE')
    for channel in idle['channels']:
        target=channel['target'];sampler=idle['samplers'][channel['sampler']]
        death_base[target['node']][target['path']]=accessor(sampler['output'])[0]
    death_globals=globals_for(death_base)
    binds=accessor(skin['inverseBindMatrices']).reshape(-1,4,4).transpose(0,2,1)
    matrices=np.array([death_globals[index] @ bind for index,bind in zip(skin['joints'],binds)])
    point_sets=[]
    for mesh in doc['meshes']:
        for primitive in mesh['primitives']:
            attrs=primitive['attributes']
            if 'JOINTS_0' not in attrs: continue
            positions=accessor(attrs['POSITION']);joints=accessor(attrs['JOINTS_0']).astype(int);weights=accessor(attrs['WEIGHTS_0'])
            points=np.c_[positions,np.ones(len(positions))]
            skinned=np.zeros((len(positions),4))
            for slot in range(4):
                skinned += np.einsum('nij,nj->ni',matrices[joints[:,slot]],points)*weights[:,slot,None]
            point_sets.append(skinned[:,:3])
    death_points=np.concatenate(point_sets)
    clips=[]
    for name in ['DUCKING','CROUCH_WALK','GLIDE_SLIDE','CROUCH_BACK','CROUCH_LEFT','CROUCH_RIGHT','SNEAK','SNEAK_BACK','SNEAK_LEFT','SNEAK_RIGHT','JUMP','FALL','START_FORWARD','STOP_FORWARD','LAND_LIGHT','LAND_HEAVY','Death','RUN','RUN_BACK','STRAFE_LEFT','STRAFE_RIGHT']:
        frames=[]
        for frame in range(33):
            phase=frame/32; contact = name in ('START_FORWARD','STOP_FORWARD','LAND_LIGHT','LAND_HEAVY'); airborne=name in ('JUMP','FALL'); running=name in ('RUN','RUN_BACK','STRAFE_LEFT','STRAFE_RIGHT'); crouch=name=='DUCKING' or name=='GLIDE_SLIDE' or name.startswith('CROUCH_'); envelope=math.sin(math.pi*phase); pose=[{k:v.copy() for k,v in p.items()} for p in base]
            if name=='Death':
                pose=[{k:v.copy() for k,v in p.items()} for p in death_base]
                fall=phase*phase*(3-2*phase)
                angle=-1.52*fall;c,s=math.cos(angle),math.sin(angle)
                rotation=np.array([[1,0,0],[0,c,-s],[0,s,c]])
                center=death_globals[names['Hips']][:3,3]
                rotated=(death_points-center) @ rotation.T+center
                hip_rotation=death_globals[names['Hips']][:3,:3]
                set_global_rotation(pose,names['Hips'],rotation @ (hip_rotation/np.linalg.norm(hip_rotation,axis=0)))
                pose[names['Hips']]['translation'][1] += .0075-float(rotated[:,1].min())
                frames.append(pose)
                continue
            # Sit back from the ankles and hinge at the hips, keeping the head
            # level and the two-handed weapon hold from the source pose.
            pose[names['Hips']]['translation'][2] -= .08 * (envelope if contact else 1 if crouch else .25)
            angle = ((-.12 if name=='STOP_FORWARD' else .12)*envelope) if contact else (-.08 if name=='GLIDE_SLIDE' else .15 if crouch else .12 if running else .06)
            c, s = math.cos(angle), math.sin(angle)
            tilt = np.array([[1,0,0],[0,c,-s],[0,s,c]])
            upper = standing[names['Spine']][:3,:3]
            upper = upper / np.linalg.norm(upper,axis=0)
            set_global_rotation(pose,names['Spine'],tilt @ upper)
            neck = standing[names['Head']][:3,:3]
            set_global_rotation(pose,names['Head'],neck / np.linalg.norm(neck,axis=0))
            head_delta = globals_for(pose)[names['Head']][1,3] - standing[names['Head']][1,3]
            drop = ((.16 if name=='LAND_HEAVY' else .09 if name=='LAND_LIGHT' else .045)*envelope) if contact else STANDING_HEAD-HEIGHT*CROUCH_RATIO if crouch else .075+.012*math.cos(4*math.pi*phase) if running else .025
            pose[names['Hips']]['translation'][1] -= drop+head_delta
            for side,offset in [('L',0),('R',.5)]:
                foot=standing[names['Foot.'+side]][:3,3].copy()
                foot[0] += (-.025 if side=='L' else .025) * (envelope if contact else 1)
                if running:
                    cycle=(phase+offset)%1
                    if name.startswith('STRAFE_'):
                        # Imported strafe labels are reversed in runtime mapping.
                        foot[0] += (-.10 if name=='STRAFE_RIGHT' else .10)*math.cos(2*math.pi*cycle)
                    else:
                        foot[2] += (-.26 if name=='RUN_BACK' else .26)*math.cos(2*math.pi*cycle)
                    foot[1] += .16*max(0,-math.sin(2*math.pi*cycle))
                elif name.startswith('CROUCH_') or name.startswith('SNEAK'):
                    cycle=(phase+offset)%1
                    if name.endswith('_LEFT') or name.endswith('_RIGHT'):
                        foot[0] += (-.10 if name.endswith('_LEFT') else .10)*math.cos(2*math.pi*cycle)
                    else:
                        foot[2] += (-.16 if name.endswith('_BACK') else .16)*math.cos(2*math.pi*cycle)
                    foot[1] += .055*max(0,-math.sin(2*math.pi*cycle))
                elif name=='GLIDE_SLIDE':
                    foot[2] += .26 if side=='L' else -.12
                elif airborne:
                    foot[1] += (.16 if name=='JUMP' else .025)*(.35+.65*phase)
                    foot[2] += (.10 if side=='L' else -.035) * (1 if name=='JUMP' else .5)
                elif name=='START_FORWARD':
                    foot[2] += (.14 if side=='L' else -.10)*envelope
                    foot[1] += (.055 if side=='L' else 0)*envelope
                elif name=='STOP_FORWARD':
                    foot[2] += (.12 if side=='L' else -.08)*envelope
                solve_leg(pose,side,foot)
            frames.append(pose)
        for frame, pose in enumerate(frames):
            g = globals_for(pose)
            right = g[names['Hips']][:3, 0]
            right /= np.linalg.norm(right)
            for side in ('L', 'R'):
                hip, knee, foot = [g[names[n+'.'+side]][:3, 3]
                                   for n in ('Thigh', 'Shin', 'Foot')]
                upper, lower = knee-hip, foot-knee
                signed_flexion = np.dot(np.cross(upper, lower), right) / (
                    np.linalg.norm(upper) * np.linalg.norm(lower))
                if not np.isfinite(signed_flexion) or signed_flexion < -1e-5:
                    raise ValueError(f'{name} frame {frame}: reversed {side} knee ({signed_flexion})')
        animation={'name':name,'samplers':[],'channels':[]}
        duration = 1.1 if name=='Death' else .4166667 if name.startswith('LAND_') else .5 if contact else 1.3333333 if name.startswith('SNEAK') else .5833333 if name=='JUMP' else .8333333 if name=='FALL' else 1.
        times=add_accessor(np.linspace(0,duration,33),'SCALAR')
        for index in sorted(animated):
            for path,kind in [('translation','VEC3'),('rotation','VEC4'),('scale','VEC3')]:
                values=np.array([p[index][path] for p in frames])
                if path=='rotation':
                    for f in range(1,len(values)):
                        if np.dot(values[f-1],values[f])<0: values[f]*=-1
                output=add_accessor(values,kind)
                animation['channels'].append({'sampler':len(animation['samplers']),'target':{'node':index,'path':path}})
                animation['samplers'].append({'input':times,'output':output,'interpolation':'LINEAR'})
        clips.append(animation)
    replaced={a['name'] for a in clips}
    doc['animations']=[a for a in doc['animations'] if a['name'] not in replaced]+clips
    # Discard superseded animation storage so repeat runs remain byte-stable.
    used=set()
    for mesh in doc.get('meshes',[]):
        for primitive in mesh['primitives']:
            used.update(primitive['attributes'].values())
            if 'indices' in primitive: used.add(primitive['indices'])
            for target in primitive.get('targets',[]): used.update(target.values())
    for skin in doc.get('skins',[]):
        if 'inverseBindMatrices' in skin: used.add(skin['inverseBindMatrices'])
    for animation in doc['animations']:
        for sampler in animation['samplers']: used.update((sampler['input'],sampler['output']))
    remap={old:new for new,old in enumerate(sorted(used))}
    for mesh in doc.get('meshes',[]):
        for primitive in mesh['primitives']:
            primitive['attributes']={k:remap[v] for k,v in primitive['attributes'].items()}
            if 'indices' in primitive: primitive['indices']=remap[primitive['indices']]
            for target in primitive.get('targets',[]):
                for k,v in target.items(): target[k]=remap[v]
    for skin in doc.get('skins',[]):
        if 'inverseBindMatrices' in skin: skin['inverseBindMatrices']=remap[skin['inverseBindMatrices']]
    for animation in doc['animations']:
        for sampler in animation['samplers']:
            sampler['input']=remap[sampler['input']]; sampler['output']=remap[sampler['output']]
    doc['accessors']=[doc['accessors'][i] for i in sorted(used)]
    used_views={a['bufferView'] for a in doc['accessors'] if 'bufferView' in a}
    for a in doc['accessors']:
        if 'sparse' in a:
            used_views.update(a['sparse'][k]['bufferView'] for k in ('indices','values'))
    used_views.update(i['bufferView'] for i in doc.get('images',[]) if 'bufferView' in i)
    view_map={old:new for new,old in enumerate(sorted(used_views))}
    packed=bytearray(); views=[]
    for old in sorted(used_views):
        view=doc['bufferViews'][old].copy()
        while len(packed)%4: packed.append(0)
        offset=view.get('byteOffset',0)
        data=binary[offset:offset+view['byteLength']]
        view['byteOffset']=len(packed); packed.extend(data); views.append(view)
    for a in doc['accessors']:
        if 'bufferView' in a: a['bufferView']=view_map[a['bufferView']]
        if 'sparse' in a:
            for k in ('indices','values'): a['sparse'][k]['bufferView']=view_map[a['sparse'][k]['bufferView']]
    for i in doc.get('images',[]):
        if 'bufferView' in i: i['bufferView']=view_map[i['bufferView']]
    doc['bufferViews']=views; binary=packed
    doc['buffers'][0]['byteLength']=len(binary)
    encoded=json.dumps(doc,separators=(',',':')).encode(); encoded+=b' '*((-len(encoded))%4)
    binary+=b'\0'*((-len(binary))%4)
    PATH.write_bytes(struct.pack('<4sII',b'glTF',2,28+len(encoded)+len(binary))+struct.pack('<I4s',len(encoded),b'JSON')+encoded+struct.pack('<I4s',len(binary),b'BIN\0')+binary)
    print('Authored crouch, crouch walk, and slide; head target:', HEIGHT*CROUCH_RATIO)

if __name__=='__main__': main()
