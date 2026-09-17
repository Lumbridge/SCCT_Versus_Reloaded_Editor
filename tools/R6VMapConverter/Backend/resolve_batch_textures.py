import os
from pathlib import Path
import json,sys,struct,collections
from PIL import Image
sys.path.insert(0,str(Path(__file__).parent));from inspect_vegas import Package
from config import WORK as r, BATCH as out, COOKED as base, CACHE, MAP
dest=out/'textures';dest.mkdir(exist_ok=True)
paths={p.stem:p for p in list(base.rglob('*.uppc'))+[MAP]};cache={}
def package(n):
 if n not in cache:cache[n]=Package(paths[n])
 return cache[n]
def get(full,cls=None):
 pn,local=full.split('.',1);p=package(pn);e=next(e for i,e in enumerate(p.exports) if p.ref(i+1)==local and (cls is None or e['cls']==cls));return p,e
def qualify(p,n):return n if n.split('.')[0] in paths else p.path.stem+'.'+n
stream={x['source']:x for x in CACHE if x.get('status')=='recovered' and Path(x.get('file','')).is_file()}
existing={}
materials=json.loads((out/'source-materials.json').read_text());textures={}
def texture(name):
 if name in textures:return textures[name]
 result={'source':name,'status':'missing'};candidates=[]
 for lookup in [existing,stream]:
  if name in lookup:
   x=lookup[name];im=Image.open(x['file']).convert('RGBA');candidates.append((im.width*im.height,im,'recovered cache'))
 try:
  p,e=get(name,'Engine.Texture2D');props,at=p.props(e,28 if p.path.suffix.lower()=='.rmpc' else 0);end,count=struct.unpack_from('<2i',p.data,at);at=end;w,h,fmt,n=struct.unpack_from('<4i',p.data,at);at+=16
  for j in range(n):
   end,count=struct.unpack_from('<2i',p.data,at);data=p.data[at+8:at+8+count];at=end;mw,mh=struct.unpack_from('<2i',p.data,at);at+=8
   if not count:continue
   if fmt in [5,6,7]:im=Image.frombytes('RGBA',(max(4,mw),max(4,mh)),data,'bcn',({5:1,6:2,7:3}[fmt],{5:'DXT1',6:'DXT3',7:'DXT5'}[fmt])).crop((0,0,mw,mh))
   elif fmt==2:im=Image.frombytes('RGBA',(mw,mh),data,'raw','BGRA')
   else:continue
   candidates.append((mw*mh,im,'resident pixels'))
 except Exception as ex:result['reason']=str(ex)
 if candidates:
  _,im,method=max(candidates,key=lambda x:x[0]);alpha=im.getextrema()[3][0]<255;file=dest/(name.replace('.','_')+('.tga' if alpha else '.bmp'));im.convert('RGBA' if alpha else 'RGB').save(file);result.update(status='recovered',file=str(file.resolve()),width=im.width,height=im.height,alpha=alpha,method=method)
 textures[name]=result;return result

def graph(full,field,seen=None):
 seen=set() if seen is None else seen
 if full in seen:return [],None
 seen.add(full)
 try:
  p,e=get(full);props,_=p.props(e,28 if p.path.suffix.lower()=='.rmpc' else 0)
  if 'Texture' in props:return [qualify(p,props['Texture']['value'])],None
  if e['cls'].endswith('Constant3Vector'):return [],[props.get(k,{}).get('value',0) for k in ['R','G','B']]
  targets=[]
  if field in props:targets=[props[field]]
  elif 'MaterialExpression' in e['cls']:targets=[v for v in props.values() if v['type']=='StructProperty']
  found=[];color=None
  for v in targets:
   try:
    sub,_=p.props({'offset':v['offset'],'size':v['size']})
    for sv in sub.values():
     if sv['type']=='ObjectProperty' and sv['value']!='None':
      ts,c=graph(qualify(p,sv['value']),field,seen);found+=ts;color=c if c is not None else color
   except:pass
  return list(dict.fromkeys(found)),color
 except:return [],None

for name,m in materials.items():
 pars=m.get('parameters',{});candidates=[];color=None
 for k in ['D','E','Texture']:
  if isinstance(pars.get(k),str) and pars[k]!='None':candidates.append((pars[k],k))
 if not candidates:
  for field in ['DiffuseColor','EmissiveColor']:
   ts,c=graph(name,field)
   if len(ts)==1:candidates.append((ts[0],field))
   if c is not None:color=c
 m['colour_candidates']=[{'texture':n,'channel':k} for n,k in candidates]
 m['colour_constant']=color
 for n,k in candidates:texture(n)
 for k in ['O','Om','OpacityMask']:
  if isinstance(pars.get(k),str) and pars[k]!='None':texture(pars[k])
(out/'source-materials-resolved.json').write_text(json.dumps(materials,indent=2));(out/'texture-inventory.json').write_text(json.dumps(textures,indent=2))
print('textures',collections.Counter(x['status'] for x in textures.values()))
print('materials without recovered colour',sum(not any(textures[x['texture']]['status']=='recovered' for x in m['colour_candidates']) for m in materials.values()))
print('missing',[n for n,x in textures.items() if x['status']=='missing'])
