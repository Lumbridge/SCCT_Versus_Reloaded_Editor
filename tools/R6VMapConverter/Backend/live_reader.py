"""Read-only access to the verified Vegas build. No write/injection API is used."""
import ctypes as c, hashlib, json, os, struct
from ctypes import wintypes as w
from pathlib import Path
from config import MAP, WORK

k=c.WinDLL('kernel32',use_last_error=True)
k.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD];k.OpenProcess.restype=w.HANDLE
k.CloseHandle.argtypes=[w.HANDLE]
k.ReadProcessMemory.argtypes=[w.HANDLE,c.c_void_p,c.c_void_p,c.c_size_t,c.POINTER(c.c_size_t)];k.ReadProcessMemory.restype=w.BOOL
k.QueryFullProcessImageNameW.argtypes=[w.HANDLE,w.DWORD,w.LPWSTR,c.POINTER(w.DWORD)];k.QueryFullProcessImageNameW.restype=w.BOOL
h=k.OpenProcess(0x410,False,int(os.environ['R6V_CAPTURE_PID']))
if not h:raise c.WinError(c.get_last_error())
exe=c.create_unicode_buffer(32768);length=w.DWORD(len(exe))
if not k.QueryFullProcessImageNameW(h,0,exe,c.byref(length)):raise c.WinError(c.get_last_error())
if Path(exe.value).name.lower()!='r6vegas_game.exe':raise RuntimeError('Selected process is not Rainbow Six Vegas')
expected=json.loads((Path(__file__).parent/'vegas-compatibility.json').read_text())['sha256']
if hashlib.sha256(Path(exe.value).read_bytes()).hexdigest()!=expected:
    raise RuntimeError('Live recovery does not support this Vegas executable build. Offline conversion remains available.')

def read(address,length):
    buffer=c.create_string_buffer(length);count=c.c_size_t()
    if not k.ReadProcessMemory(h,address,buffer,length,c.byref(count)):raise c.WinError(c.get_last_error())
    if count.value!=length:raise OSError('Short memory read')
    return buffer.raw

def words(address,count):return struct.unpack('<'+'I'*count,read(address,count*4))
np,nn,nc=words(0x124ab90c,3);op,on,oc=words(0x124a5b94,3)
if not (1000<nn<=nc<1000000 and 1000<on<=oc<1000000):raise RuntimeError('Unsupported runtime object tables')
ptrs=words(np,nn);names={}

def name(i):
    if not 0<=i<len(ptrs):raise OSError('Invalid name reference')
    if i not in names:names[i]=read(ptrs[i]+16,256).decode('utf-16le',errors='replace').split('\0')[0]
    return names[i]

def path(p,depth=0):
    if not p:return ''
    if depth>16:raise OSError('Cyclic object path')
    outer,ni,cls=words(p+0x20,3)
    return (path(outer,depth+1)+'.' if outer else '')+name(ni)

def capture():
    textures=[];worlds=[]
    for p in words(op,on):
        if not p:continue
        try:
            outer,ni,cls=words(p+0x20,3);cn=name(words(cls+0x24,1)[0]);n=name(ni)
            if cn=='World':worlds.append(path(p))
            if cn=='Texture2D' and not n.startswith('Default__'):
                textures.append(dict(address=p,name=path(p),data=read(p,0x300).hex()))
        except OSError:continue
    if not any(x.lower().startswith(MAP.stem.lower()+'.') for x in worlds):
        raise RuntimeError('Load the selected map in Vegas before recovering its textures. Loaded worlds: '+str(worlds))
    (WORK/'streamed/live-textures.json').write_text(json.dumps(textures))
    print(f'Found {len(textures)} live textures in the selected map.',flush=True)
