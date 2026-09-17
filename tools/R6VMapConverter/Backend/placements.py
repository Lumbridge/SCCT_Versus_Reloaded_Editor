from pathlib import Path
import json,struct,math,re,itertools,collections
from inspect_vegas import Package
from config import WORK as r, BATCH, MAP, PACKAGE

def prepare_placements():
 out=r
 (out/'mirrors').mkdir(exist_ok=True)
 rows=json.loads((r/'placements.json').read_text())
 meshes={x['source']:x for x in json.loads((BATCH/'delivery-inventory.json').read_text())}
 mats=json.loads((BATCH/'material-conversion.json').read_text())
 p=Package(MAP);by={p.ref(i+1):e for i,e in enumerate(p.exports)}
 variants={};operations=[];manifest=[];skipped=[];bounds=[];override_count=0
 # UE rotator matrix: column vectors X,Y,Z in world space.
 def rotation(r):
  pitch,yaw,roll=[x*math.pi/32768 for x in r];cp,sp=math.cos(pitch),math.sin(pitch);cy,sy=math.cos(yaw),math.sin(yaw);cr,sr=math.cos(roll),math.sin(roll)
  return [[cp*cy,sr*sp*cy-cr*sy,-cr*sp*cy-sr*sy],[cp*sy,sr*sp*sy+cr*cy,sr*cy-cr*sp*sy],[sp,-sr*cp,cr*cp]]
 def transform(m,v):return [sum(a*b for a,b in zip(row,v)) for row in m]
 for i,row in enumerate(rows):
  ce=by[row['component']];cp,end=p.props(ce,28);ap,_=p.props(by[row['actor']],28)
  ref=struct.unpack_from('<i',p.data,ce['offset']+24)[0];meshref=(MAP.stem+'.' if ref>0 else '')+p.ref(ref);assert meshref==row['mesh'],(meshref,row['mesh'])
  for prop,key,default in [('Location','location',[0,0,0]),('Rotation','rotation',[0,0,0]),('DrawScale','scale',1),('DrawScale3D','scale3d',[1,1,1])]:assert ap.get(prop,{}).get('value',default)==row[key],(row['actor'],prop)
  if meshref=='None':skipped.append({'component':row['component'],'reason':'No source static mesh reference'});continue
  assert not any(cp.get(k,{}).get('value',False) for k in ['bAbsoluteTranslation','bAbsoluteRotation','bAbsoluteScale'])
  mesh=meshes[meshref];rot=list(row['rotation']);localrot=cp.get('Rotation',{}).get('value',[0,0,0])
  component_scale=cp.get('Scale3D',{}).get('value',[1,1,1]);uniform=cp.get('Scale',{}).get('value',1)
  actor_scale=[s*row['scale'] for s in row['scale3d']]
  local_scale=[s*uniform for s in component_scale]
  if any(localrot):
   scale=actor_scale
   sign=tuple(-1 if s<0 else 1 for s in scale)
   cr=rotation(localrot)
   local_matrix=[[sign[j]*cr[j][k]*local_scale[k] for k in range(3)] for j in range(3)]
  else:
   scale=[actor_scale[j]*local_scale[j] for j in range(3)]
   sign=tuple(-1 if s<0 else 1 for s in scale)
   local_matrix=[[sign[j] if j==k else 0 for k in range(3)] for j in range(3)]
  determinant=(local_matrix[0][0]*(local_matrix[1][1]*local_matrix[2][2]-local_matrix[1][2]*local_matrix[2][1])-local_matrix[0][1]*(local_matrix[1][0]*local_matrix[2][2]-local_matrix[1][2]*local_matrix[2][0])+local_matrix[0][2]*(local_matrix[1][0]*local_matrix[2][1]-local_matrix[1][1]*local_matrix[2][0]))
  native=mesh['native']
  if local_matrix!=[[1,0,0],[0,1,0],[0,0,1]]:
   key=(meshref,tuple(v for line in local_matrix for v in line))
   if key not in variants:
    num=len(variants);name=f'Variant{num:03d}_'+mesh['name'];file=out/'mirrors'/(f'Variant{num:03d}.ase');text=Path(mesh['ase']).read_text()
    def vtx(m):
     ase=list(map(float,m[2].split()));source=[ase[0],-ase[1],ase[2]];native=transform(local_matrix,source)
     return m[1]+' '.join(str(v) for v in [native[0],-native[1],native[2]])
    text=re.sub(r'(\*MESH_VERTEX\s+\d+\s+)([^\r\n]+)',vtx,text)
    if determinant<0:
     text=re.sub(r'(\*MESH_FACE\s+\d+: A: \d+ B: )(\d+)( C: )(\d+)',lambda m:m[1]+m[4]+m[3]+m[2],text)
     text=re.sub(r'(\*MESH_TFACE\s+\d+\s+\d+\s+)(\d+)(\s+)(\d+)',lambda m:m[1]+m[4]+m[3]+m[2],text)
    file.write_text(text);variants[key]={'source':meshref,'matrix':local_matrix,'determinant':determinant,'name':name,'native':PACKAGE+'.Transforms.'+name,'ase':str(file),'source_ase':mesh['ase'],'material_slots':mesh['material_slots'],'triangles':mesh['triangles']}
   native=variants[key]['native']
  pivot=ap.get('PrePivot',{}).get('value',[0,0,0]);translation=cp.get('Translation',{}).get('value',[0,0,0])
  offset=transform(rotation(row['rotation']),[(translation[j]-pivot[j])*actor_scale[j] for j in range(3)])
  location=[row['location'][j]+offset[j] for j in range(3)]
  props={'Location':dict(zip(['X','Y','Z'],map(str,location))),'Rotation':dict(zip(['Pitch','Yaw','Roll'],map(str,rot))),'DrawScale':'1','DrawScale3D':dict(zip(['X','Y','Z'],(str(abs(s)) for s in scale))),'StaticMesh':"StaticMesh'"+native+"'",'DrawType':'DT_StaticMesh','bHidden':'True' if ap.get('bHidden',{}).get('value',False) else 'False'}
  # Preserve explicit source collision and lighting exclusions, especially background meshes.
  for source,target in [('bCollideActors','bCollideActors'),('bBlockActors','bBlockActors')]:
   if source in ap:props[target]='True' if ap[source]['value'] else 'False'
  for source,target in [('CollideActors','bCollideActors'),('BlockActors','bBlockActors'),('BlockNonZeroExtent','bBlockNonZeroExtentTraces'),('BlockZeroExtent','bBlockZeroExtentTraces'),('CastShadow','bShadowCast'),('bCastDynamicShadow','bActorShadows'),('bAcceptsDynamicLights','bUseDynamicLights')]:
   if source in cp:props[target]='True' if cp[source]['value'] else 'False'
  if cp.get('bAcceptsLights',{}).get('value') is False:props.update(bUnlit='True',bStaticLighting='False',bApplyToStaticLighting='False',bUseDynamicLights='False')
  if props.get('bBlockActors')=='False':props.update(bBlockPlayers='False',bBlockCamera='False')
  if 'CullDistance' in cp:props['CullDistance']=str(cp['CullDistance']['value'])
  override=[]
  if 'Materials' in cp:
   a=cp['Materials'];n=struct.unpack_from('<i',p.data,a['offset'])[0];refs=struct.unpack_from('<'+'i'*n,p.data,a['offset']+4);override=[(MAP.stem+'.' if ref>0 else '')+p.ref(ref) for ref in refs]
   props['Skins']=['None' if name=='None' else "Shader'"+mats[name]['native']+"'" for name in override];override_count+=1
  name=f'Placed_{i:04d}_'+row['actor'].split('.')[-1];op={'op':'create','id':name,'class':'Engine.StaticMeshActor','properties':props};operations.append(op)
  # Compare final world transform with source component transform at all bounds corners.
  ar=rotation(row['rotation']);cr=rotation(localrot);nr=rotation(rot);maxerr=0
  for corner in itertools.product(*zip(*mesh['bounds'])):
   # ASE/PSK coordinates are Y-reflected; SCCT native import restores source Y.
   src=[corner[0],-corner[1],corner[2]];v=transform(cr,[src[j]*local_scale[j] for j in range(3)]);before=transform(ar,[v[j]*actor_scale[j] for j in range(3)]);baked=transform(local_matrix,src);after=transform(nr,[baked[j]*abs(scale[j]) for j in range(3)])
   maxerr=max(maxerr,max(abs(a-b) for a,b in zip(before,after)));bounds.append([location[j]+after[j] for j in range(3)])
  assert maxerr<0.00001,(row['actor'],maxerr)
  manifest.append({'actor':'MyLevel.'+name,'source':row,'native_mesh':native,'native_properties':props,'source_material_overrides':override,'transform_corner_error':maxerr})
 box=[[min(v[j] for v in bounds),max(v[j] for v in bounds)] for j in range(3)];extent=2**math.ceil(math.log2(max(abs(v) for m in manifest for v in m['source']['location'])+2048));assert extent<=262144,box
 report={'placements':len(manifest),'skipped_no_mesh':len(skipped),'mirrored_placements':sum(any(float(v)<0 for v in m['source']['scale3d']) or m['source']['scale']<0 for m in manifest),'mirrored_variants':len(variants),'component_rotations':sum(bool(m['source']['component_properties']) for m in manifest),'material_overrides':override_count,'world_bounds':box,'review_room_half_extent':extent,'max_transform_corner_error':max(m['transform_corner_error'] for m in manifest)}
 (out/'placement-inventory.json').write_text(json.dumps(manifest,indent=2));(out/'mirrored-variants.json').write_text(json.dumps(list(variants.values()),indent=2));(out/'skipped-components.json').write_text(json.dumps(skipped,indent=2));(out/'placement-summary.json').write_text(json.dumps(report,indent=2))

 # Native import uses a leaf filename for material lookup, with all materials loaded first.
 job=json.loads((BATCH/'build-job.json').read_text())
 job['meshCommands'] += [f'STATICMESH IMPORT FILE="{v["ase"]}" NAME={v["name"]} PACKAGE={PACKAGE} GROUP=Transforms' for v in variants.values()]
 job['expected'] += [v['native'] for v in variants.values()]
 job['skipSnapshot']=True
 (r/'build-job.json').write_text(json.dumps(job,indent=2))
 def value(v):
  return '('+','.join(k+'='+str(n) for k,n in v.items())+')' if isinstance(v,dict) else str(v)
 lines=['Begin Map']
 for op in operations:
  lines.append('Begin Actor Class=Engine.StaticMeshActor Name='+op['id'])
  for key,v in op['properties'].items():
   if isinstance(v,list):lines.extend(f'    {key}({i})={value(x)}' for i,x in enumerate(v))
   else:lines.append('    '+key+'='+value(v))
  lines.append('End Actor')
 lines.append('End Map')
 t3d=r/'Placements.t3d';t3d.write_text('\n'.join(lines))
 # Portable coordinates are supplied as T3D; a map-changes document needs a live map identity.
 packagepath=BATCH/(PACKAGE+'.usx')
 target=r/(PACKAGE+'Placements.sdc')
 expected=[m['actor'] for m in manifest]
 fields=['Location','Rotation','DrawScale','DrawScale3D','StaticMesh','Skins','bHidden']
 mapjob={'room':extent,'commands':[f'OBJ LOAD FILE="{packagepath}"',f'MAP IMPORTADD FILE="{t3d}"'],'after':[f'MAP SAVE FILE="{target}"'],'inspect':expected,'inspectFields':fields,'inspectionFile':str(r/'native-created.json'),'skipSnapshot':True,'expected':expected}
 (r/'map-job.json').write_text(json.dumps(mapjob,indent=2))
 reloadjob={'commands':[f'OBJ LOAD FILE="{packagepath}"',f'MAP LOAD FILE="{target}"'],'inspect':expected,'inspectFields':fields,'inspectionFile':str(r/'native-reloaded.json'),'skipSnapshot':True,'expected':expected}
 (r/'reload-job.json').write_text(json.dumps(reloadjob,indent=2))
