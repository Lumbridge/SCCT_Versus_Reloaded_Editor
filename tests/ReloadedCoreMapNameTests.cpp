// Execute the patched x86 blocks with the actual stock FString assignment code.
// No game startup or DLL initialization; all allocations use a test allocator.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <vector>
#include <fstream>
#include <iterator>
#include <string>
static_assert(sizeof(void*) == 4, "x86 required");
void Require(bool ok, const char* message) { if (!ok) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); } }
struct FString { wchar_t* data; int count, capacity; };
struct Allocator {
    virtual void* Allocate(int size, const wchar_t*) { return HeapAlloc(GetProcessHeap(),0,size); }
    virtual void* Reallocate(void* ptr,int size,const wchar_t*) { return ptr ? HeapReAlloc(GetProcessHeap(),0,ptr,size) : HeapAlloc(GetProcessHeap(),0,size); }
    virtual void Release(void* ptr) { if(ptr) HeapFree(GetProcessHeap(),0,ptr); }
};
__declspec(naked) void Run(void*, FString*, void*, unsigned) {
    __asm {
        push ebp
        mov ebp,esp
        push ebx
        push esi
        push edi
        mov ecx,[ebp+8]
        mov edx,[ebp+12]
        mov ebx,[ebp+16]
        mov eax,[ebp+20]
        mov esi,edx
        mov edi,edx
        push ebp
        mov ebp,ecx
        call ebx
        pop ebp
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}
std::vector<unsigned char> Read(const char* path) {
    std::ifstream f(path,std::ios::binary); Require(!!f,"open binary");
    return {std::istreambuf_iterator<char>(f),{}};
}
const unsigned char* Rva(const std::vector<unsigned char>& file,unsigned rva) {
    auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());
    auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(file.data()+dos->e_lfanew);
    const auto* s=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i,++s)
        if(rva>=s->VirtualAddress && rva-s->VirtualAddress<s->SizeOfRawData)
            return file.data()+s->PointerToRawData+rva-s->VirtualAddress;
    Require(false,"RVA found"); return nullptr;
}
int main(int argc,char** argv) {
    Require(argc==3,"usage: test patched-core.dll stock-game");
    auto core=Read(argv[1]),game=Read(argv[2]);
    auto* memory=static_cast<unsigned char*>(VirtualAlloc(reinterpret_cast<void*>(0x10900000),0x400000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    Require(memory==reinterpret_cast<void*>(0x10900000),"fixed native helper memory");
    std::memcpy(memory+0x4750,Rva(game,0x4750),0xa1);
    // Resolve only the helper's wcslen import, memcpy entry and allocator global.
    *reinterpret_cast<void**>(0x10bdf3b4)=reinterpret_cast<void*>(&std::wcslen);
    auto* jump=reinterpret_cast<unsigned char*>(0x109040a0); jump[0]=0xb8;
    *reinterpret_cast<void**>(jump+1)=reinterpret_cast<void*>(&std::memcpy); jump[5]=0xff; jump[6]=0xe0;
    Allocator allocator; *reinterpret_cast<Allocator**>(0x10c73bf0)=&allocator;
    auto* block=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    Require(block!=nullptr,"block allocation");
    const unsigned rvas[]={0xa4c0,0xa4e3,0xa53f},sizes[]={35,32,33};
    const std::wstring names[]={L"OffsD_FxCheck",L"X",L"1234567",L"12345678",std::wstring(240,L'M'),L""};
    for(unsigned i=0;i<3;++i) {
        std::memcpy(block,Rva(core,rvas[i]),sizes[i]); block[sizes[i]]=0xc3;
        FString destination={nullptr,0,0};
        for(const auto& name:names) {
            alignas(8) unsigned char storage[128]={}; auto* frame=storage+64;
            unsigned capacity=name.size()<8 ? 7 : static_cast<unsigned>(name.size());
            if(capacity==7) std::memcpy(frame-0x2c,name.c_str(),(name.size()+1)*2);
            else *reinterpret_cast<const wchar_t**>(frame-0x2c)=name.c_str();
            *reinterpret_cast<const wchar_t**>(frame-0x30)=name.c_str();
            *reinterpret_cast<unsigned*>(frame-0x34)=capacity;
            Run(frame,&destination,block,capacity);
            Require(destination.count==static_cast<int>(name.empty()?0:name.size()+1),"updated count");
            Require(destination.capacity==destination.count,"updated capacity");
            Require(name.empty()?destination.data==nullptr:std::wcscmp(destination.data,name.c_str())==0,"full untruncated name");
            Require(HeapValidate(GetProcessHeap(),0,nullptr)!=FALSE,"intact heap");
            Run(frame,nullptr,block,capacity);
        }
        std::printf("PASS: map-name block %X: grow, shrink, inline/heap, empty, null owner\n",rvas[i]);
    }
    VirtualFree(block,0,MEM_RELEASE); VirtualFree(memory,0,MEM_RELEASE);
    return 0;
}
