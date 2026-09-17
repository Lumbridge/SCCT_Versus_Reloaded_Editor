"""Create inspectable OBJ and candidate SCCT ASE files from extracted PSKX meshes.
No game files are modified. ASE orientation/material assignment needs native testing.
"""
import pathlib, struct, json, math

def convert(path,out):
    data=path.read_bytes();at=0;chunks={}
    while at<len(data):
        name,flags,size,count=struct.unpack_from('<20siii',data,at);at+=32
        assert size>=0 and count>=0 and at+size*count<=len(data)
        chunks[name.rstrip(b'\0').decode()]=(size,count,data[at:at+size*count]);at+=size*count
    assert at==len(data)
    size,count,blob=chunks['PNTS0000'];assert size==12
    vertices=list(struct.iter_unpack('<3f',blob));assert all(math.isfinite(x) for v in vertices for x in v)
    size,count,blob=chunks['VTXW0000'];assert size==16
    wedges=[struct.unpack_from('<IffBBH',blob,i*size) for i in range(count)]
    size,count,blob=chunks['FACE0000'];assert size==12
    faces=list(struct.iter_unpack('<3HBBI',blob))
    assert all(w[0]<len(vertices) for w in wedges)
    assert all(i<len(wedges) for f in faces for i in f[:3])
    size,count,blob=chunks['MATT0000'];assert size==88
    materials=[blob[i*size:i*size+64].split(b'\0')[0].decode('latin1') for i in range(count)]
    assert all(f[3]<len(materials) for f in faces)
    out.mkdir(exist_ok=True,parents=True);name=path.stem
    lines=['# Local Vegas extraction proof. PSKX right-handed coordinates.']
    lines += ['v '+' '.join(map(str,v)) for v in vertices]
    lines += [f'vt {w[1]} {1-w[2]}' for w in wedges]
    lines += ['f '+' '.join(f'{wedges[i][0]+1}/{i+1}' for i in f[:3]) for f in faces]
    (out/(name+'.obj')).write_text('\n'.join(lines)+'\n')
    a=['*3DSMAX_ASCIIEXPORT 200','*COMMENT "Vegas mesh conversion feasibility sample"','*MATERIAL_LIST {',' *MATERIAL_COUNT 1',' *MATERIAL 0 {','  *MATERIAL_NAME "VegasSample"','  *MATERIAL_CLASS "Multi/Sub-Object"',f'  *NUMSUBMTLS {len(materials)}']
    for i,m in enumerate(materials):a += [f'  *SUBMATERIAL {i} {{',f'   *MATERIAL_NAME "{m}"','   *MATERIAL_CLASS "Standard"','   *MATERIAL_DIFFUSE 0.7 0.7 0.7','   *MAP_DIFFUSE {','    *BITMAP "Engine.DefaultTexture"','    *UVW_U_TILING 1.0','    *UVW_V_TILING 1.0','   }','  }']
    a += [' }','}','*GEOMOBJECT {',f' *NODE_NAME "{name}"',' *MESH {','  *TIMEVALUE 0',f'  *MESH_NUMVERTEX {len(vertices)}',f'  *MESH_NUMFACES {len(faces)}','  *MESH_VERTEX_LIST {']
    a += [f'   *MESH_VERTEX {i} '+' '.join(map(str,v)) for i,v in enumerate(vertices)]
    a += ['  }','  *MESH_FACE_LIST {']
    for i,f in enumerate(faces):
        # SCCT ASE import reflects Y; reverse winding and UV corners together.
        vs=[wedges[j][0] for j in (f[0], f[2], f[1])]
        a.append(f'   *MESH_FACE {i}: A: {vs[0]} B: {vs[1]} C: {vs[2]} AB: 1 BC: 1 CA: 1 *MESH_SMOOTHING 1 *MESH_MTLID {f[3]}')
    a += ['  }',f'  *MESH_NUMTVERTEX {len(wedges)}','  *MESH_TVERTLIST {']
    a += [f'   *MESH_TVERT {i} {w[1]} {1-w[2]} 0' for i,w in enumerate(wedges)]
    a += ['  }',f'  *MESH_NUMTVFACES {len(faces)}','  *MESH_TFACELIST {']
    a += [f'   *MESH_TFACE {i} {f[0]} {f[2]} {f[1]}' for i,f in enumerate(faces)]
    a += ['  }',' }',' *MATERIAL_REF 0','}']
    a=['\t'*(len(line)-len(line.lstrip(' ')))+line.lstrip(' ') for line in a]
    (out/(name+'.ase')).write_text('\n'.join(a)+'\n')
    return dict(name=name,vertices=len(vertices),triangles=len(faces),materials=materials,bounds=[[min(v[i] for v in vertices) for i in range(3)],[max(v[i] for v in vertices) for i in range(3)]])

if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser(description='Convert a PSKX mesh to SCCT ASE and inspectable OBJ.')
    parser.add_argument('source',type=pathlib.Path);parser.add_argument('output',type=pathlib.Path)
    args=parser.parse_args();print(json.dumps(convert(args.source,args.output),indent=2))
