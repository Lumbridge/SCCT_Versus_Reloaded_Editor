from pathlib import Path
import struct,zlib,json,collections
from config import MAP, WORK
p=MAP.with_suffix('.usdx');b=p.read_bytes();at=16;rs=[];guid=b[:16].hex();flags=typ=n=None
while at<len(b):
 start=at;flag=struct.unpack_from('<I',b,at)[0];double=False
 if flag>16:
  guid=b[at:at+16].hex();at+=16;flag=struct.unpack_from('<I',b,at)[0]
  if flag>16:at=start;double=True
 if not double:
  flags,typ,n=struct.unpack_from('<3I',b,at);at+=12;assert flags<=16 and 0<n<=16,(at,flags,typ,n)
 offsets=struct.unpack_from('<'+'I'*n,b,at);at+=4*n;mips=[]
 for off in offsets:
  size,magic=struct.unpack_from('<2I',b,off);assert magic==0x7ab3ef2c,(len(rs),start,off,hex(magic),flag,double,offsets);cur=off+8;data=b''
  while len(data)<size:
   raw,comp=struct.unpack_from('<2I',b,cur);cur+=8;blob=b[cur:cur+comp];cur+=comp
   chunk=blob if comp==raw else zlib.decompress(blob);assert len(chunk)==raw;data+=chunk
  assert len(data)==size
  footer=struct.unpack_from('<I',b,cur)[0];assert footer==0x23d7fc4e;cur+=4
  mips.append(dict(offset=off,size=size))
 rs.append(dict(index=len(rs),start=start,guid=guid,flag=flags,type=typ,count=n,double=double,mips=mips));at=cur
assert at==len(b)
out=WORK/'streamed';out.mkdir(exist_ok=True);(out/'index.json').write_text(json.dumps(rs,indent=2))
print('parsed',len(rs),'entries',len(b),'bytes');print('flags',collections.Counter(r['flag'] for r in rs));print('types',collections.Counter(r['type'] for r in rs));print('double',sum(r['double'] for r in rs));print(rs[:2])


