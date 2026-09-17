from pathlib import Path
import json,struct,zlib,collections,hashlib
from PIL import Image
from config import WORK, MAP
root=WORK/'streamed';rs=json.loads((root/'index.json').read_text());mapping=json.loads((root/'named-records.json').read_text());b=MAP.with_suffix('.usdx').read_bytes();out=root/'images';out.mkdir(exist_ok=True)
def raw(idx,mip=0):
 m=rs[idx]['mips'][mip];at=m['offset']+8;data=b''
 while len(data)<m['size']:
  size,comp=struct.unpack_from('<2I',b,at);at+=8;chunk=b[at:at+comp];at+=comp;data+=chunk if size==comp else zlib.decompress(chunk)
 assert len(data)==m['size'];return data
byaddr={r['metadata_address']:r for r in mapping}
byname=collections.defaultdict(list)
for m in mapping:
 byname[m['name']].append(m)
manifest=[]
for name,rows in sorted(byname.items()):
 candidates=[]
 for row in rows:
  indices=row['record_indices'];data=[raw(i) for i in indices]
  if len(set(hashlib.sha256(d).digest() for d in data))!=1:
   scores={i:0 for i in indices}
   for delta in [-72,-48,-24,24,48,72]:
    peer=byaddr.get(row['metadata_address']+delta)
    if peer:
     for i in indices:
      if i+delta//24 in peer['record_indices']:scores[i]+=1
   best=max(scores.values());chosen=[i for i in indices if scores[i]==best]
   if best<2 or len(chosen)!=1:continue
   indices=chosen;data=[raw(indices[0])]

  i=indices[0];flag=rs[i]['flag'];w=max(1,row['width']>>flag);h=max(1,row['height']>>flag);fmt=row['format'];assert fmt in (5,6,7)
  decoded=Image.frombytes('RGBA',(max(4,w),max(4,h)),data[0],'bcn',({5:1,6:2,7:3}[fmt],{5:'DXT1',6:'DXT3',7:'DXT5'}[fmt])).crop((0,0,w,h))
  candidates.append((w*h,decoded,row,i))
 if not candidates:manifest.append(dict(source=name,status='ambiguous'));continue
 _,im,row,i=max(candidates,key=lambda x:x[0]);filename=name.replace('.','_');alpha=im.getextrema()[3][0]<255
 im.save(out/(filename+'.png'));im.convert('RGB' if not alpha else 'RGBA').save(out/(filename+('.bmp' if not alpha else '.tga')))
 manifest.append(dict(source=name,status='recovered',width=im.width,height=im.height,format=row['format'],alpha=alpha,record=i,file=str(Path('images')/(filename+('.bmp' if not alpha else '.tga')))))
(root/'recovered-all.json').write_text(json.dumps(manifest,indent=2));print(collections.Counter(x['status'] for x in manifest))
