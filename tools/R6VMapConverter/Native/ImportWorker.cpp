// Injected only by run_native_map_recovery_test.ps1 into its isolated editor.
// Run engine commands on the editor's UI thread, never on the injection thread.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <share.h>
#include <sstream>
#include <iomanip>
#include <vector>
#include <fstream>
#include <map>
#include <set>
#include "../../../Reloaded.Editor/Include/nlohmann/json.hpp"

namespace {
HHOOK hook;
HWND frameWindow;
std::filesystem::path directory;
FILE* report;
bool startupFailed;
constexpr UINT kRun=WM_APP+0x319;
constexpr uintptr_t kEditor=0x1165DFA0;
constexpr uintptr_t kWarn=0x115BEFB0;
void Record(const char* stage, const char* detail = "") {
    fprintf(report, "%s %s\n", stage, detail);
    fflush(report);
}

const char* NameText(int index) {
    auto names = *reinterpret_cast<unsigned char***>(0x1169CFBC);
    int count = *reinterpret_cast<int*>(0x1169CFC0);
    return names && index >= 0 && index < count && names[index]
        ? reinterpret_cast<const char*>(names[index]+12) : "<invalid>";
}

const char* ObjectName(void* object) {
    return object ? NameText(*reinterpret_cast<int*>(static_cast<unsigned char*>(object)+0x20)) : "<null>";
}

unsigned char* FindProperty(unsigned char* actor, const char* name) {
    auto type = *reinterpret_cast<unsigned char**>(actor+0x24);
    for (auto property = *reinterpret_cast<unsigned char**>(type+0x58); property;
         property = *reinterpret_cast<unsigned char**>(property+0x40))
        if (!strcmp(ObjectName(property),name)) return property;
    return nullptr;
}


int Exec(const std::string& command) {
    Record("exec", command.substr(0, command.find_first_of("\r\n")).c_str());
    auto editor = *reinterpret_cast<unsigned char**>(kEditor);
    void* output = *reinterpret_cast<void**>(kWarn);
    if (!editor || !output) return 0;
    void* exec = editor + 0x28;
    using Fn = int(__thiscall*)(void*, const char*, void*);
    return reinterpret_cast<Fn>((*reinterpret_cast<void***>(exec))[0])(
        exec, command.c_str(), output);
}


bool SetCubeBrush(int halfExtent, float x=0, float y=0, float z=0) {
    // Six outward-facing quads form one ordinary subtractive room brush.
    const int vertices[6][4][3] = {
        {{512,-512,-512},{512,512,-512},{512,512,512},{512,-512,512}},
        {{-512,-512,-512},{-512,-512,512},{-512,512,512},{-512,512,-512}},
        {{-512,512,-512},{-512,512,512},{512,512,512},{512,512,-512}},
        {{-512,-512,-512},{512,-512,-512},{512,-512,512},{-512,-512,512}},
        {{-512,-512,512},{512,-512,512},{512,512,512},{-512,512,512}},
        {{-512,-512,-512},{-512,512,-512},{512,512,-512},{512,-512,-512}}
    };
    std::ostringstream command;
    command << std::setprecision(9);
    command << "BRUSH SET\nBegin PolyList\n";
    for (const auto& face : vertices) {
        command << "Begin Polygon Texture=TXT_INI.BSP.BETON_Lit_Spec Flags=0\n";
        for (const auto& point : face)
            command << "Vertex " << x+point[0]*halfExtent/512 << ',' << y+point[1]*halfExtent/512
                << ',' << z+point[2]*halfExtent/512 << "\n";
        command << "End Polygon\n";
    }
    command << "End PolyList\n";
    return Exec(command.str()) != 0;
}


std::string ObjectPath(unsigned char* obj) {
 if(!obj)return "None";
 auto outer=*reinterpret_cast<unsigned char**>(obj+0x18);
 return (outer?ObjectPath(outer)+".":"")+ObjectName(obj);
}
unsigned char* FindByPath(const std::string& path) {
 auto data=*reinterpret_cast<unsigned char***>(0x11697B70);int n=*reinterpret_cast<int*>(0x11697B74);
 for(int i=0;i<n;++i)if(data[i] && _stricmp(ObjectPath(data[i]).c_str(),path.c_str())==0)return data[i];
 return nullptr;
}
void SetReflected(unsigned char* obj,const std::string& name,const std::string& value) {
 auto prop=FindProperty(obj,name.c_str());if(!prop)throw std::runtime_error("Missing property "+ObjectPath(obj)+"."+name);
 auto offset=*reinterpret_cast<unsigned*>(prop+0x3C);
 using Import=const char*(__thiscall*)(void*,const char*,void*,unsigned);
 auto fn=reinterpret_cast<Import>((*reinterpret_cast<void***>(prop))[0x94/4]);
 if(!fn(prop,value.c_str(),obj+offset,0))throw std::runtime_error("Property import failed "+name);
}
nlohmann::json ReadPropertyValue(unsigned char* prop,unsigned char* slot,int depth=0) {
 using J=nlohmann::json;if(depth>6)return "depth-limit";
 std::string kind=ObjectName(*reinterpret_cast<void**>(prop+0x24));
 if(kind=="ObjectProperty" || kind=="ClassProperty")return ObjectPath(*reinterpret_cast<unsigned char**>(slot));
 if(kind=="BoolProperty")return (*reinterpret_cast<unsigned*>(slot)&*reinterpret_cast<unsigned*>(prop+0x64))!=0;
 if(kind=="ByteProperty")return *slot;
 if(kind=="IntProperty")return *reinterpret_cast<int*>(slot);
 if(kind=="FloatProperty")return *reinterpret_cast<float*>(slot);
 if(kind=="NameProperty")return NameText(*reinterpret_cast<int*>(slot));
 if(kind=="StructProperty") {J result=J::object();auto type=*reinterpret_cast<unsigned char**>(prop+0x64);std::string structName=ObjectName(type);if(structName=="Vector"){auto v=reinterpret_cast<float*>(slot);return J{{"X",v[0]},{"Y",v[1]},{"Z",v[2]}};}if(structName=="Rotator"){auto v=reinterpret_cast<int*>(slot);return J{{"Pitch",v[0]},{"Yaw",v[1]},{"Roll",v[2]}};}for(auto ch=*reinterpret_cast<unsigned char**>(type+0x58);ch;ch=*reinterpret_cast<unsigned char**>(ch+0x40))result[ObjectName(ch)]=ReadPropertyValue(ch,slot+*reinterpret_cast<unsigned*>(ch+0x3C),depth+1);return result;}
 if(kind=="ArrayProperty") {int n=*reinterpret_cast<int*>(slot+4);if(n>1024)return "large-array";J result=J::array();auto inner=*reinterpret_cast<unsigned char**>(prop+0x64);auto stride=*reinterpret_cast<unsigned short*>(inner+0x32);auto data=*reinterpret_cast<unsigned char**>(slot);for(int i=0;i<n;++i)result.push_back(ReadPropertyValue(inner,data+i*stride,depth+1));return result;}
 return kind;
}
nlohmann::json DumpReflected(unsigned char* obj) {
 nlohmann::json j=nlohmann::json::object();auto cls=*reinterpret_cast<unsigned char**>(obj+0x24);
 for(auto prop=*reinterpret_cast<unsigned char**>(cls+0x58);prop;prop=*reinterpret_cast<unsigned char**>(prop+0x40))j[ObjectName(prop)]=ReadPropertyValue(prop,obj+*reinterpret_cast<unsigned*>(prop+0x3C));return j;
}
unsigned char* CreateMaterial(const nlohmann::json& desc) {
 auto cls=FindByPath(desc.at("class").get<std::string>());auto outer=FindByPath(desc.at("package").get<std::string>());
 if(!cls||!outer)throw std::runtime_error("Construct class/package missing");
 auto data=*reinterpret_cast<unsigned char***>(0x11697B70);int n=*reinterpret_cast<int*>(0x11697B74);unsigned char* tag=nullptr;
 for(int i=0;i<n && !tag;++i)if(data[i] && !strcmp(ObjectName(*reinterpret_cast<void**>(data[i]+0x24)),"Brush"))tag=FindProperty(data[i],"Tag");
 if(!tag)throw std::runtime_error("Name importer absent");int fname=0;
 using Import=const char*(__thiscall*)(void*,const char*,void*,unsigned);
 auto imp=reinterpret_cast<Import>((*reinterpret_cast<void***>(tag))[0x94/4]);std::string text=desc.at("name");
 if(!imp(tag,text.c_str(),&fname,0))throw std::runtime_error("Name import failed");
 using Construct=unsigned char*(__cdecl*)(void*,void*,int,unsigned,void*,void*,void*);
 auto result=reinterpret_cast<Construct>(0x10fadf80)(cls,outer,fname,0x80004,nullptr,*reinterpret_cast<void**>(kWarn),nullptr);
 if(!result)throw std::runtime_error("Material construction failed");
 auto properties=desc.value("properties",nlohmann::json::object());
 for(auto& [key,v]:properties.items())SetReflected(result,key,v.get<std::string>());
 return result;
}
void RunTest() {
 if(report)fclose(report);
 report=_fsopen((directory/"native_recovery_report.txt").string().c_str(),"w",_SH_DENYNO);
 if(!report)return;
 Record("started","Vegas ASE import proof");
 auto bar=GetMenu(frameWindow); bool menuFound=false;
 for(int i=0;i<GetMenuItemCount(bar);++i){auto sub=GetSubMenu(bar,i);for(int j=0;j<GetMenuItemCount(sub);++j)if(GetMenuItemID(sub,j)==40948){char title[128]={};GetMenuStringA(bar,i,title,sizeof(title),MF_BYPOSITION);Record("map_design_menu",title);menuFound=true;}}
 if(!menuFound){Record("FAIL","Map Design menu absent");return;}

 if(!Exec("MAP NEW")){Record("FAIL","MAP NEW failed");return;}

 try {
 using J=nlohmann::json;
 using Request=int(__cdecl*)(const char*,char*,unsigned);
 auto request=reinterpret_cast<Request>(GetProcAddress(GetModuleHandleA("Reloaded.Editor.dll"),"ReloadedWorkflowRequest"));
 if(!request)throw std::runtime_error("Workflow API missing");
 auto call=[&](J q){std::vector<char> b(32*1024*1024);auto t=q.dump();int ok=request(t.c_str(),b.data(),(unsigned)b.size());auto r=J::parse(b.data());if(!ok||!r.at("ok").get<bool>())throw std::runtime_error(r.value("error","failed"));return r.at("result");};




 auto root=directory;
 J job;std::ifstream(root/"native-job.json")>>job;
 if(job.contains("room")){if(!SetCubeBrush(job["room"].get<int>()) || !Exec("BRUSH SUBTRACT"))throw std::runtime_error("Review space creation failed");}
 for(auto& cmd:job.at("commands")){if(!Exec(cmd.get<std::string>()))throw std::runtime_error("Command failed: "+cmd.get<std::string>());}
 if(job.contains("materials"))for(auto& desc:job["materials"])CreateMaterial(desc);
 if(job.contains("edits"))for(auto& desc:job["edits"]){auto obj=FindByPath(desc.at("object"));if(!obj)throw std::runtime_error("Edit object absent");for(auto& [key,v]:desc.at("properties").items())SetReflected(obj,key,v.get<std::string>());}
 if(job.contains("meshCommands"))for(auto& cmd:job["meshCommands"])if(!Exec(cmd.get<std::string>()))throw std::runtime_error("Mesh command failed");
 if(job.contains("document")){auto before=call({{"op","authoring.export"}});auto doc=job["document"];doc["map"]=before["map"];call({{"op","authoring.apply"},{"document",doc}});}
 if(job.contains("focus")){auto chosen=FindByPath(job["focus"].get<std::string>());if(!chosen)throw std::runtime_error("Focus actor missing");auto editor=*reinterpret_cast<unsigned char**>(kEditor);auto level=*reinterpret_cast<unsigned char**>(editor+0x130);auto actors=*reinterpret_cast<unsigned char***>(level+0x2C);int count=*reinterpret_cast<int*>(level+0x30);using Select=void(__thiscall*)(void*,void*,void*,int,int);for(int i=0;i<count;++i)if(actors[i])reinterpret_cast<Select>(0x10eb9a20)(editor,level,actors[i],actors[i]==chosen,0);Exec("CAMERA ALIGN");}
 if(job.contains("after"))for(auto& cmd:job["after"]){if(!Exec(cmd.get<std::string>()))throw std::runtime_error("Command failed: "+cmd.get<std::string>());}
 if(job.contains("inspect")){J inspections=J::object();for(auto& name:job["inspect"]){auto obj=FindByPath(name.get<std::string>());if(!obj)throw std::runtime_error("Inspect object absent: "+name.get<std::string>());if(job.contains("inspectFields")){J fields=J::object();for(auto& key:job["inspectFields"]){auto prop=FindProperty(obj,key.get<std::string>().c_str());if(!prop)throw std::runtime_error("Inspect field missing");fields[key.get<std::string>()]=ReadPropertyValue(prop,obj+*reinterpret_cast<unsigned*>(prop+0x3C));}inspections[name.get<std::string>()]=fields;}else inspections[name.get<std::string>()]=DumpReflected(obj);}std::ofstream(root/job.value("inspectionFile",std::string("native-properties.json")))<<inspections.dump(2);}
 if(job.value("skipSnapshot",false)){for(auto& name:job.at("expected"))if(!FindByPath(name.get<std::string>()))throw std::runtime_error("Missing expected object: "+name.get<std::string>());Record("PASS","Native job and reflected object verification completed.");return;}
 auto snap=call({{"op","authoring.export"}});std::ofstream(root/job.value("snapshot",std::string("native-snapshot.json")))<<snap.dump(2);
 auto text=snap.dump();
 for(auto& name:job.at("expected"))if(text.find(name.get<std::string>())==std::string::npos)throw std::runtime_error("Missing asset: "+name.get<std::string>());
 Record("PASS","Native job completed and expected assets verified.");

 }catch(const std::exception& e){Record("FAIL",e.what());}

}

LRESULT CALLBACK OnMessage(int code, WPARAM removed, LPARAM parameter) {
    if (code >= 0) {
        auto message = reinterpret_cast<CWPSTRUCT*>(parameter);
        if (message->message == kRun) {
            UnhookWindowsHookEx(hook);
            hook = nullptr;
            RunTest();
        }
    }
    return CallNextHookEx(hook, code, removed, parameter);
}

BOOL CALLBACK RecordStartupErrorControl(HWND window, LPARAM) {
    char text[4096] = {};
    GetWindowTextA(window,text,sizeof(text));
    if (report && text[0]) Record("startup_error_text",text);
    return TRUE;
}

BOOL CALLBACK FindEditorWindow(HWND window, LPARAM threadAddress) {
    DWORD process = 0;
    DWORD thread = GetWindowThreadProcessId(window, &process);
    if (process != GetCurrentProcessId()) return TRUE;
    if (!GetPrivateProfileIntA("test", "visible_editor", 0,
        (directory / "native_recovery_test.ini").string().c_str())) ShowWindow(window, SW_HIDE);
    char title[256] = {};
    GetWindowTextA(window, title, sizeof(title));
    char klass[128] = {};
    GetClassNameA(window, klass, sizeof(klass));
    if (report) {
        fprintf(report, "window=%p thread=%lu class=%s title=%s\n", window, thread, klass, title);
        fflush(report);
    }
    if (!strcmp(klass,"#32770") && !strcmp(title,"Critical Error")) {
        EnumChildWindows(window,RecordStartupErrorControl,0);
        FILE* failure = _fsopen((directory/"native_recovery_report.txt").string().c_str(),"w",_SH_DENYNO);
        if (failure) {
            fputs("FAIL Native editor startup reached a Critical Error; see native_recovery_boot.txt.\n",failure);
            fclose(failure);
        }
        startupFailed=true;
        return FALSE;
    }
    if (strstr(klass, "WEditorFrame")) {
        *reinterpret_cast<DWORD*>(threadAddress) = thread;
        frameWindow = window;
    }
    return TRUE;
}

DWORD WINAPI WaitUntilReady(void*) {
    report = _fsopen((directory / "native_recovery_boot.txt").string().c_str(), "w", _SH_DENYNO);
    if (report) Record("waiting_for_editor");
    for (int attempt = 0; attempt < 240; ++attempt) {
        DWORD uiThread = 0;
        EnumWindows(FindEditorWindow, reinterpret_cast<LPARAM>(&uiThread));
        if (startupFailed) return 1;
        auto editor = *reinterpret_cast<unsigned char**>(kEditor);
        if (report) {
            fprintf(report, "attempt=%d editor=%p uiThread=%lu\n", attempt, editor, uiThread);
            fflush(report);
        }
        if (uiThread && editor && *reinterpret_cast<void**>(editor + 0x130)) {
            // Initial windows exist before startup completes. Wait briefly,
            // then execute through the frame's actual UI-thread message hook.
            Sleep(5000);
            // Both threads are in this process; no cross-process DLL load is needed.
            hook = SetWindowsHookExA(WH_CALLWNDPROC, OnMessage, nullptr, uiThread);
            if (report) { fprintf(report, "hook=%p error=%lu\n", hook, GetLastError()); fflush(report); }
            DWORD_PTR result = 0;
            if (hook && SendMessageTimeoutA(frameWindow, kRun, 0, 0,
                SMTO_ABORTIFHUNG, 300000, &result)) return 0;
            return 1;
        }
        Sleep(250);
    }
    return 1;
}
}

BOOL WINAPI DllMain(HMODULE module, DWORD reason, void*) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        char path[MAX_PATH] = {};
        GetModuleFileNameA(module, path, MAX_PATH);
        directory = std::filesystem::path(path).parent_path();
        if (HANDLE worker = CreateThread(nullptr, 0, WaitUntilReady, nullptr, 0, nullptr)) CloseHandle(worker);
    }
    return TRUE;
}
