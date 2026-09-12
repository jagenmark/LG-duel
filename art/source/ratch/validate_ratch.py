#!/usr/bin/env python3
"""Independent structural and animation validator for a preserved Ratch GLB."""
from __future__ import annotations
import argparse, json, math, struct
from pathlib import Path

GLB = struct.Struct('<4sII'); CHUNK = struct.Struct('<I4s')
COMP = {5120: ('<b', 1), 5121: ('<B', 1), 5122: ('<h', 2), 5123: ('<H', 2), 5125: ('<I', 4), 5126: ('<f', 4)}
NCOMP = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT2': 4, 'MAT3': 9, 'MAT4': 16}

def read(path):
    raw = path.read_bytes(); magic, version, length = GLB.unpack_from(raw)
    if magic != b'glTF' or version != 2 or length != len(raw): raise ValueError(f'{path}: invalid GLB')
    off = GLB.size; doc = None; binary = b''
    while off < len(raw):
        size, kind = CHUNK.unpack_from(raw, off); off += CHUNK.size
        data = raw[off:off+size]; off += size
        if kind == b'JSON': doc = json.loads(data.rstrip(b' \t\r\n\0'))
        elif kind == b'BIN\0': binary = data
    if doc is None: raise ValueError(f'{path}: missing JSON')
    return doc, binary

def accessor_values(doc, binary, index):
    a = doc['accessors'][index]; view = doc['bufferViews'][a['bufferView']]
    if 'sparse' in a: raise ValueError('sparse accessor unsupported')
    fmt, width = COMP[a['componentType']]; count = NCOMP[a['type']]
    stride = view.get('byteStride', width * count)
    start = view.get('byteOffset', 0) + a.get('byteOffset', 0)
    out = []
    for i in range(a['count']):
        row = [struct.unpack_from(fmt, binary, start + i*stride + j*width)[0] for j in range(count)]
        if a.get('normalized'):
            limit = 127 if a['componentType'] == 5120 else 32767 if a['componentType'] == 5122 else 255 if a['componentType'] == 5121 else 65535
            row = [max(-1., x/limit) if a['componentType'] in (5120,5122) else x/limit for x in row]
        out.append(row)
    return out

def accessor_raw(doc, binary, index):
    a = doc['accessors'][index]; v = doc['bufferViews'][a['bufferView']]
    fmt, width = COMP[a['componentType']]; n = NCOMP[a['type']]; stride = v.get('byteStride', width*n)
    start = v.get('byteOffset', 0) + a.get('byteOffset', 0)
    return b''.join(binary[start+i*stride:start+i*stride+width*n] for i in range(a['count']))

def node_info(doc):
    nodes = doc['nodes']; names = {n.get('name'): i for i,n in enumerate(nodes) if n.get('name')}
    parents = {c:i for i,n in enumerate(nodes) for c in n.get('children',[])}
    return nodes, names, {i: nodes[p].get('name') if p is not None else None for i,p in ((i,parents.get(i)) for i in range(len(nodes)))}

def qdelta(a,b): return min(max(abs(x-y) for x,y in zip(a,b)), max(abs(x+y) for x,y in zip(a,b)))
def trs(n): return n.get('translation',[0,0,0]), n.get('rotation',[0,0,0,1]), n.get('scale',[1,1,1])
def maxdelta(a,b): return max((abs(x-y) for x,y in zip(a,b)), default=0.)

def validate(target_path, source_path):
    t,tbin=read(target_path); s,sbin=read(source_path); tn,tnames,tparents=node_info(t); sn,snames,sparents=node_info(s)
    errors=[]; notes=[]; tol=1e-5
    if len(t.get('skins',[])) != 1 or len(s.get('skins',[])) != 1: errors.append('both assets must have one skin'); return errors,notes
    ts, ss = t['skins'][0], s['skins'][0]
    tj={tn[i].get('name') for i in ts['joints']}; sj={sn[i].get('name') for i in ss['joints']}
    if tj != sj: errors.append(f'skin joint sets differ: target={sorted(tj)} source={sorted(sj)}')
    for name in sorted(sj & tj):
        ti,si=tnames[name],snames[name]
        if tparents[ti] != sparents[si]: errors.append(f'{name}: parent mismatch')
        tt,tr,tx=trs(tn[ti]); st,sr,sx=trs(sn[si])
        if maxdelta(tt,st)>tol or qdelta(tr,sr)>tol or maxdelta(tx,sx)>tol: errors.append(f'{name}: rest transform mismatch')
    # Bind matrices are compared by joint name. Foot scale-only correction is
    # intentionally exact in the output and therefore must match Pip too.
    if 'inverseBindMatrices' not in ts or 'inverseBindMatrices' not in ss: errors.append('missing inverse bind matrices')
    else:
        tm=accessor_values(t,tbin,ts['inverseBindMatrices']); sm=accessor_values(s,sbin,ss['inverseBindMatrices'])
        tb={tn[j].get('name'):tm[i] for i,j in enumerate(ts['joints'])}; sb={sn[j].get('name'):sm[i] for i,j in enumerate(ss['joints'])}
        for name in sorted(sj & tj):
            d=maxdelta(tb[name],sb[name])
            if d>tol: errors.append(f'{name}: inverse bind mismatch {d:.3g}')
        notes.append('all inverse bind matrices, including Foot.L/R correction, match Pip within 1e-5')
    # Animation payloads must be byte-identical at the logical accessor level.
    sa={a.get('name'):a for a in s.get('animations',[])}; ta={a.get('name'):a for a in t.get('animations',[])}
    if set(sa) != set(ta): errors.append('animation name sets differ')
    for name in sorted(set(sa)&set(ta)):
        a,b=sa[name],ta[name]
        if len(a.get('samplers',[])) != len(b.get('samplers',[])) or len(a.get('channels',[])) != len(b.get('channels',[])): errors.append(f'{name}: sampler/channel count differs'); continue
        for i,(x,y) in enumerate(zip(a['samplers'],b['samplers'])):
            if x.get('interpolation') != y.get('interpolation'): errors.append(f'{name} sampler {i}: interpolation differs')
            if accessor_raw(s,sbin,x['input']) != accessor_raw(t,tbin,y['input']) or accessor_raw(s,sbin,x['output']) != accessor_raw(t,tbin,y['output']): errors.append(f'{name} sampler {i}: raw samples differ')
        for i,(x,y) in enumerate(zip(a['channels'],b['channels'])):
            snode=sn[x['target']['node']].get('name'); tnode=tn[y['target']['node']].get('name')
            if snode != tnode or x['target'].get('path') != y['target'].get('path') or x['sampler'] != y['sampler']: errors.append(f'{name} channel {i}: target remap differs')
    notes.append(f'verified {len(sa)} animations with raw input/output samples and name-based targets')
    # Mesh sanity checks for every skinned primitive.
    joint_count=len(ts['joints'])
    for mi,m in enumerate(t.get('meshes',[])):
        for pi,p in enumerate(m.get('primitives',[])):
            at=p.get('attributes',{})
            for key in ('POSITION','NORMAL'):
                if key in at:
                    for row in accessor_values(t,tbin,at[key]):
                        if not all(math.isfinite(float(x)) for x in row): errors.append(f'mesh {mi} primitive {pi}: nonfinite {key}')
            if 'JOINTS_0' in at and 'WEIGHTS_0' in at:
                joints=accessor_values(t,tbin,at['JOINTS_0']); weights=accessor_values(t,tbin,at['WEIGHTS_0'])
                if len(joints)!=len(weights): errors.append(f'mesh {mi} primitive {pi}: joint/weight count differs')
                for i,(js,ws) in enumerate(zip(joints,weights)):
                    if any(int(x)<0 or int(x)>=joint_count for x in js): errors.append(f'mesh {mi} primitive {pi} vertex {i}: joint out of range')
                    if not all(math.isfinite(float(x)) and x>=-1e-6 for x in ws) or abs(sum(ws)-1.)>1e-4: errors.append(f'mesh {mi} primitive {pi} vertex {i}: invalid weights')
    notes.append('verified finite positions/normals and normalized in-range skin weights')
    return errors,notes

def main():
    p=argparse.ArgumentParser(description=__doc__); p.add_argument('target',type=Path); p.add_argument('source',type=Path); p.add_argument('--report',type=Path)
    a=p.parse_args(); errors,notes=validate(a.target,a.source); status='PASS' if not errors else 'FAIL'; lines=[f'# Ratch preservation validation: {status}','',f'- Target: `{a.target}`',f'- Source: `{a.source}`']+[f'- {n}' for n in notes]
    if errors: lines += ['', '## Errors'] + [f'- {e}' for e in errors]
    text='\n'.join(lines)+'\n'; print(text,end='');
    if a.report: a.report.parent.mkdir(parents=True,exist_ok=True); a.report.write_text(text)
    raise SystemExit(0 if not errors else 1)
if __name__=='__main__': main()
