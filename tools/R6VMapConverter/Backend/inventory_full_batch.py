from pathlib import Path
import sys,json,struct,collections
sys.path.insert(0,str(Path(__file__).parent))
from inspect_vegas import Package
from config import WORK as r, BATCH as out, COOKED as base, MAP
paths={p.stem:p for p in list(base.rglob('*.uppc'))+[MAP]};pkgs={}
def pkg(name):
 if name not in pkgs:pkgs[name]=Package(paths[name])
 return pkgs[name]
def qualify(p,ref):return ref if ref.split('.')[0] in paths else p.path.stem+'.'+ref
placements=json.loads((r/'placements.json').read_text());wanted=set(x['mesh'] for x in placements if x['mesh']!='None')
manifest=[];matcache={}
def material(full):
 if full in matcache:return matcache[full]
 try:
  pn,local=full.split('.',1);p=pkg(pn);e=next(e for i,e in enumerate(p.exports) if p.ref(i+1)==local);props,_=p.props(e,28 if p.path.suffix.lower()=='.rmpc' else 0)
  result={'source':full,'class':e['cls'],'parameters':{},'parent':props.get('Parent',{}).get('value')}
  for key in ['TextureParameterValues','VectorParameterValues','ScalarParameterValues']:
   if key not in props:continue
   a=props[key];at=a['offset']+4
   for _ in range(struct.unpack_from('<i',p.data,a['offset'])[0]):
    d,at=p.props({'offset':at,'size':a['offset']+a['size']-at});n=d['ParameterName']['value'];v=d['ParameterValue'];value=v['value']
    if key.startswith('Texture') and value!='None':value=qualify(p,value)
    if key.startswith('Vector'):
     if v['size']==16:value=list(struct.unpack_from('<4f',p.data,v['offset']))
     else:
      fields,_=p.props({'offset':v['offset'],'size':v['size']});value={k:x['value'] for k,x in fields.items()}
    result['parameters'][n]=value
  matcache[full]=result;return result
 except Exception as ex:
  result={'source':full,'error':str(ex)};matcache[full]=result;return result
for full in sorted(wanted):
 row={'source':full,'placed':sum(x['mesh']==full for x in placements)}
 try:
  pn,local=full.split('.',1);p=pkg(pn);e=next(e for i,e in enumerate(p.exports) if p.ref(i+1)==local and e['cls']=='Engine.StaticMesh')
  f=out/'export'/Path(*full.split('.'));f=f.with_suffix('.pskx')
  if not f.exists():
   candidates=list((out/'export'/pn).rglob(full.split('.')[-1]+'.pskx'))
   assert len(candidates)==1,('export missing/ambiguous',str(f));f=candidates[0]
  b=f.read_bytes();at=0;chunks={}
  while at<len(b):
   n,fl,size,count=struct.unpack_from('<20siii',b,at);at+=32;chunks[n.rstrip(b'\0').decode()]=(size,count,b[at:at+size*count]);at+=size*count
  sz,n,fb=chunks['FACE0000'];assert sz==12
  faces=list(struct.iter_unpack('<3HBBI',fb));counts=collections.Counter(x[3] for x in faces);nm=chunks['MATT0000'][1]
  data=p.data[e['offset']:e['offset']+e['size']];needle=struct.pack('<i',nm);at=0;hits=[]
  while True:
   at=data.find(needle,at)
   if at<0:break
   start=at+4;at+=1
   if start+nm*28>len(data):continue
   sections=[struct.unpack_from('<7i',data,start+i*28) for i in range(nm)]
   try:
    if any(s[1] not in (0,1) or s[2] not in (0,1) or s[4]!=counts[i] or (s[4]>0 and s[3]!=sum(counts[j]*3 for j in range(i))) for i,s in enumerate(sections)):continue
    names=[qualify(p,p.ref(s[0])) if s[0] else 'None' for s in sections]
    if any('MAT' not in name.upper() and 'MATERIAL' not in name.upper() for name in names):
     # Some materials have arbitrary names: class is authoritative.
     for s in sections:
      ref=s[0];
      if not ref:continue
      obj=p.exports[ref-1] if ref>0 else p.imports[-ref-1];cls=obj.get('cls','')
      if not any(k in cls for k in ['Material','Texture']):raise ValueError('not material')
    hits.append((start,sections,names))
   except:continue
  assert len(hits)==1,('section matches',len(hits),nm,dict(counts))
  start,sections,names=hits[0]
  row.update(pskx=str(f.resolve()),triangles=n,vertices=chunks['PNTS0000'][1],section_offset=start,sections=[{'material':name,'triangles':s[4],'first_index':s[3]} for s,name in zip(sections,names)],status='mapped')
  for name in names:material(name)
 except Exception as ex:row.update(status='exception',error=str(ex))
 manifest.append(row)
(out/'mesh-inventory.json').write_text(json.dumps(manifest,indent=2));(out/'source-materials.json').write_text(json.dumps(matcache,indent=2));print(collections.Counter(x['status'] for x in manifest));print('materials',len(matcache));print('exceptions',[(x['source'],x.get('error')) for x in manifest if x['status']!='mapped'])
