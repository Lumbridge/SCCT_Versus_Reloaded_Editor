from live_reader import *
from config import WORK, MAP
import pathlib,struct,json
import ctypes as c,array,collections
class MBI(c.Structure):_fields_=[('BaseAddress',c.c_void_p),('AllocationBase',c.c_void_p),('AllocationProtect',c.c_ulong),('PartitionId',c.c_ushort),('RegionSize',c.c_size_t),('State',c.c_ulong),('Protect',c.c_ulong),('Type',c.c_ulong)]
k.VirtualQueryEx.argtypes=[c.c_void_p,c.c_void_p,c.POINTER(MBI),c.c_size_t];k.VirtualQueryEx.restype=c.c_size_t
textures=json.loads((WORK/'streamed/live-textures.json').read_text());tex={t['address']:t for t in textures};records=json.loads((WORK/'streamed/index.json').read_text());lookup=collections.defaultdict(list)
for i,t in enumerate(records):
 end=records[i+1]['start'] if i+1<len(records) else MAP.with_suffix('.usdx').stat().st_size
 span=end-t['start']
 if t['mips'][0]['offset']-t['start']==28+4*t['count']:span-=16
 lookup[(sum(m['size'] for m in t['mips']),span)].append(t)
hits=[];regions=set()
at=0
while at<0x7fff0000:
 m=MBI()
 if not k.VirtualQueryEx(h,at,c.byref(m),c.sizeof(m)):break
 base=m.BaseAddress or 0;at=base+m.RegionSize
 if m.State==0x1000 and m.Type==0x20000 and not m.Protect&0x101 and m.Protect&0xee:
  for start in range(base,at,1024*1024):regions.add((start,min(1024*1024,at-start)))
print('Read-only texture metadata scan:',len(regions),'regions',sum(s for b,s in regions),'bytes',flush=True)
for base,size in regions:
 try:blob=read(base,size)
 except OSError:continue
 a=array.array('I');a.frombytes(blob[:len(blob)//4*4])
 for i in range(len(a)-6):
  if a[i] not in tex:continue
  ptr,handle,mip,total,comp,zero=a[i:i+6]
  matches=lookup.get((total,comp),[])
  if not matches or zero!=0 or mip>16:continue
  t=tex[ptr];b=bytes.fromhex(t['data']);w,hh,fmt=struct.unpack_from('<3I',b,0xa8)
  valid=[]
  for match in matches:
   ww=max(1,w>>match['flag']);he=max(1,hh>>match['flag']);block=8 if fmt==5 else 16 if fmt in (6,7) else 0
   if block and max(1,(ww+3)//4)*max(1,(he+3)//4)*block==match['mips'][0]['size']:valid.append(match['index'])
  if valid:hits.append(dict(name=t['name'],width=w,height=hh,format=fmt,record_indices=valid,metadata_address=base+i*4))
(WORK/'streamed/named-records.json').write_text(json.dumps(hits,indent=2));print('mapped',len(set(x['name'] for x in hits)))
