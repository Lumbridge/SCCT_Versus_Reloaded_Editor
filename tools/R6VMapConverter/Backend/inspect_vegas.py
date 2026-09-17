"""Read-only package inspection for local PC Vegas 1 (241/66). Research prototype."""
import struct, pathlib, json, collections, sys

class Reader:
    def __init__(self,data,pos=0): self.data,self.pos=data,pos
    def take(self,n):
        assert 0<=n<=len(self.data)-self.pos,(self.pos,n,len(self.data))
        b=self.data[self.pos:self.pos+n];self.pos+=n;return b
    def i(self):return struct.unpack('<i',self.take(4))[0]
    def u(self):return struct.unpack('<I',self.take(4))[0]
    def string(self):
        n=self.i();assert abs(n)<65536
        return self.take(abs(n)*(2 if n<0 else 1)).decode('utf-16le' if n<0 else 'latin1').rstrip('\0')

class Package:
    def __init__(self,path):
        self.path=pathlib.Path(path);self.data=self.path.read_bytes();r=Reader(self.data)
        assert r.u()==0x9e2a83c1
        self.version=r.u();assert self.version==0x004200f1,hex(self.version)
        r.take(12);nc,no,ec,eo,ic,io=[r.i() for _ in range(6)]
        r.pos=no;self.names=[]
        for _ in range(nc):self.names.append(r.string());r.take(8)
        r.pos=io;self.imports=[]
        for _ in range(ic):
            cp,cn,outer,name=[r.i() for _ in range(4)]
            self.imports.append(dict(name=self.name(name),outer=outer,cls=self.name(cn)))
        assert r.pos==eo,(r.pos,eo)
        self.exports=[]
        for _ in range(ec):
            cls,sup,outer,name,arch,fl,fl2,size=[r.i() for _ in range(8)]
            off=r.i() if size else 0
            cm=r.i();assert 0<=cm<100000
            r.take(cm*8)
            assert 0<=off<=len(self.data) and 0<=size<=len(self.data)-off
            self.exports.append(dict(name=self.name(name),outer=outer,clsref=cls,archetype=arch,offset=off,size=size))
        self.table_end=r.pos
        for e in self.exports:e['cls']=self.ref(e['clsref'])
    def name(self,n):
        index=n&0x7ffff;num=(n&0xffffffff)>>19
        assert index<len(self.names),(index,len(self.names))
        return self.names[index]+('_'+str(num-1) if num else '')
    def ref(self,n,depth=0):
        if not n:return 'None'
        assert depth<50
        e=self.exports[n-1] if n>0 else self.imports[-n-1]
        return (self.ref(e['outer'],depth+1)+'.' if e['outer'] else '')+e['name']
    def props(self,e,skip=0):
        r=Reader(self.data,e['offset']+skip);end=e['offset']+e['size'];out={}
        for _ in range(10000):
            name=self.name(r.u())
            if name=='None':return out,r.pos
            typ=self.name(r.u());assert typ.endswith('Property'),(name,typ,r.pos)
            size,idx=r.i(),r.i();sub=None;value=None
            if typ=='StructProperty':sub=self.name(r.u())
            if typ=='BoolProperty':value=bool(r.i())
            assert 0<=size<=end-r.pos,(name,typ,size,r.pos,end)
            data=r.take(size)
            if typ=='ObjectProperty' and size==4:value=self.ref(struct.unpack('<i',data)[0])
            elif typ=='FloatProperty' and size==4:value=struct.unpack('<f',data)[0]
            elif typ=='IntProperty' and size==4:value=struct.unpack('<i',data)[0]
            elif typ=='NameProperty' and size==4:value=self.name(struct.unpack('<I',data)[0])
            elif sub in ('Vector','Rotator') and size==12:value=list(struct.unpack('<3f' if sub=='Vector' else '<3i',data))
            elif typ=='ByteProperty' and size==1:value=data[0]
            out[name+(f'[{idx}]' if idx else '')]=dict(type=typ,struct=sub,value=value,size=size,offset=r.pos-size)
        raise ValueError('too many properties')

if __name__=='__main__':
    p=Package(sys.argv[1]);print(p.path.name,len(p.names),len(p.imports),len(p.exports),'table end',p.table_end,'file',len(p.data))
    print(collections.Counter(x['cls'] for x in p.exports).most_common(20))
    for e in p.exports:
        if e['cls'] in ('Engine.StaticMeshActor','Engine.StaticMeshComponent','Engine.StaticMesh'):
            print(e)
            for skip in (0,4):
                try:print('skip',skip,p.props(e,skip));break
                except Exception as ex:print('skip',skip,'failed',str(ex))
            break
