// Included by NativeMapRecoveryProbe after its existing fixture helpers.
#include "../Reloaded.Editor/Include/nlohmann/json.hpp"
#include <stdexcept>
#include <commctrl.h>
#include <commdlg.h>
#include <dlgs.h>
namespace WorkflowProbe {
UINT objectivePopupChoice=0;bool objectivePopupFound=false;int objectivePopupTicks=0;
void CALLBACK ChooseObjectivePopup(HWND,UINT,UINT_PTR,DWORD)
{
    if(++objectivePopupTicks>20){EndMenu();return;}
    if(objectivePopupFound)return;
    EnumThreadWindows(GetCurrentThreadId(),[](HWND window,LPARAM)->BOOL {
        char cls[64]{};GetClassNameA(window,cls,sizeof(cls));if(strcmp(cls,"#32768"))return TRUE;
        auto menu=reinterpret_cast<HMENU>(SendMessage(window,0x01e1,0,0));
        for(int i=0;i<GetMenuItemCount(menu);++i)if(GetMenuItemID(menu,i)==objectivePopupChoice)
        {
            objectivePopupFound=true;
            GUITHREADINFO gui{sizeof(gui)};GetGUIThreadInfo(GetCurrentThreadId(),&gui);
            auto receiver=gui.hwndMenuOwner?gui.hwndMenuOwner:window;
            SendMessage(window,0x01e5,i,0); // MN_SELECTITEM: target this fixture's menu item.
            PostMessage(receiver,WM_KEYDOWN,VK_RETURN,0);return FALSE;
        }
        return TRUE;
    },0);
}
bool vertexPopupChecked=false;
void CALLBACK InspectVertexPopup(HWND,UINT,UINT_PTR,DWORD)
{
    EnumThreadWindows(GetCurrentThreadId(),[](HWND window,LPARAM)->BOOL {
        char cls[64]{};GetClassNameA(window,cls,sizeof(cls));
        if(!strcmp(cls,"#32768")){
            auto menu=reinterpret_cast<HMENU>(SendMessage(window,0x01e1,0,0)); // MN_GETHMENU
            auto state=GetMenuState(menu,40944,MF_BYCOMMAND);
            vertexPopupChecked=state!=UINT(-1) && !(state&(MF_DISABLED|MF_GRAYED));
            EndMenu();return FALSE;
        }
        return TRUE;
    },0);
}
unsigned char* MagicActor(const nlohmann::json& identity)
{
    auto path=identity.at("path").get<std::string>();auto name=path.substr(path.find_last_of('.')+1);
    auto level=*reinterpret_cast<unsigned char**>(*reinterpret_cast<uintptr_t*>(kEditor)+0x130);auto data=*reinterpret_cast<unsigned char***>(level+0x2c);int count=*reinterpret_cast<int*>(level+0x30);
    for(int i=2;i<count;++i)if(data[i] && name==ObjectName(data[i]))return data[i];throw std::runtime_error("Magic fixture actor missing.");
}
void CALLBACK AcceptDesignPlay(HWND,UINT,UINT_PTR,DWORD)
{
    EnumThreadWindows(GetCurrentThreadId(),[](HWND w,LPARAM)->BOOL {char title[128]{};GetWindowTextA(w,title,sizeof(title));if(!strcmp(title,"Play Map Options"))SendMessage(GetDlgItem(w,IDOK),BM_CLICK,0,0);return TRUE;},0);
}
BOOL WINAPI DesignKeepEditorWindow(HWND){return TRUE;}
nlohmann::json designLaunchStarts;
bool designLaunchSeen=false,designLaunchPose=false;
HINSTANCE WINAPI DesignCaptureLaunch(HWND,LPCSTR,LPCSTR,LPCSTR parameters,LPCSTR,INT)
{
    designLaunchSeen=parameters && strstr(parameters,"Editeur=true");designLaunchPose=true;
    for(auto& start:designLaunchStarts){auto p=reinterpret_cast<float*>(MagicActor(start)+0x80);designLaunchPose=designLaunchPose && p[0]==300 && p[1]==400 && p[2]==500;}
    return reinterpret_cast<HINSTANCE>(33);
}
nlohmann::json ActivateMagic(const nlohmann::json& identity,const nlohmann::json& source,bool activate)
{
    using A=uintptr_t;auto actor=MagicActor(identity);auto type=*reinterpret_cast<unsigned char**>(actor+0x24);unsigned char* function=nullptr;
    for(auto child=*reinterpret_cast<unsigned char**>(type+0x30);child;child=*reinterpret_cast<unsigned char**>(child+0x2c))if(!strcmp(ObjectName(child),"NativeTrigger"))function=child;
    if(!function)throw std::runtime_error("NativeTrigger fixture function missing.");
    // ProcessEvent suppresses script execution in editor mode. Exercise the
    // native thunk with a real FFrame expression stream in this disposable map.
    unsigned char code[8]={0x20,0,0,0,0,0x2a,static_cast<unsigned char>(activate?0x26:0x25),0x16};auto other=reinterpret_cast<A>(MagicActor(source));memcpy(code+1,&other,4);
    struct Frame{void* vtable;void* node;void* object;unsigned char* code;void* locals;void* previous;} frame{nullptr,function,actor,code,nullptr,nullptr};
    using Native=void(__thiscall*)(void*,void*,void*);reinterpret_cast<Native>(*reinterpret_cast<A*>(function+0x6c))(actor,&frame,nullptr);
    nlohmann::json result={{"queue",nlohmann::json::array()},{"groups",nlohmann::json::array()}};
    auto groups=*reinterpret_cast<unsigned char**>(actor+0x2f8);int count=*reinterpret_cast<int*>(actor+0x2fc);for(int i=0;i<count;++i)result["groups"].push_back({{"repeat",*reinterpret_cast<int*>(groups+i*24+12)},{"index",*reinterpret_cast<int*>(groups+i*24+16)}});
    auto queue=*reinterpret_cast<unsigned char**>(actor+0x304);count=*reinterpret_cast<int*>(actor+0x308);for(int i=0;i<count;++i)result["queue"].push_back({{"delay",*reinterpret_cast<float*>(queue+i*24+4)},{"due",*reinterpret_cast<float*>(queue+i*24+20)}});
    return result;
}
// Native reflection diagnostics for the game-specific event-group fixture.
nlohmann::json MagicSchema()
{
    using J=nlohmann::json;using A=uintptr_t;
    auto name=[](A object)->std::string {int index=*reinterpret_cast<int*>(object+0x20);auto names=*reinterpret_cast<A*>(0x1169cfbc);return reinterpret_cast<const char*>(*reinterpret_cast<A*>(names+index*4)+12);};
    A data=*reinterpret_cast<A*>(0x11697b70);int count=*reinterpret_cast<int*>(0x11697b74);J result=J::array();
    for(int i=0;i<count;++i)
    {
        A object=*reinterpret_cast<A*>(data+i*4);if(!object || name(object)!="SMagicEvent" || name(*reinterpret_cast<A*>(object+0x24))!="Class")continue;
        J cls={{"class",name(*reinterpret_cast<A*>(object+0x18))+"."+name(object)},{"properties",J::array()}};
        cls["functions"]=J::array();
        for(A child=*reinterpret_cast<A*>(object+0x30);child;child=*reinterpret_cast<A*>(child+0x2c))if(name(*reinterpret_cast<A*>(child+0x24))=="Function")
        {
            cls["functions"].push_back({{"name",name(child)},{"native",*reinterpret_cast<unsigned*>(child+0x6c)}});
        }
        for(A p=*reinterpret_cast<A*>(object+0x58);p;p=*reinterpret_cast<A*>(p+0x40))
        {
            J prop={{"name",name(p)},{"kind",name(*reinterpret_cast<A*>(p+0x24))},{"offset",*reinterpret_cast<int*>(p+0x3c)},{"stride",*reinterpret_cast<unsigned short*>(p+0x32)},{"dimension",*reinterpret_cast<unsigned short*>(p+0x30)}};
            if(name(p)=="Groups")
            {
                auto inner=*reinterpret_cast<A*>(p+0x64);prop["innerKind"]=name(*reinterpret_cast<A*>(inner+0x24));prop["innerStride"]=*reinterpret_cast<unsigned short*>(inner+0x32);
                auto structure=*reinterpret_cast<A*>(inner+0x64);prop["structure"]=name(structure);prop["fields"]=J::array();
                for(A f=*reinterpret_cast<A*>(structure+0x58);f;f=*reinterpret_cast<A*>(f+0x40))
                {
                    J field={{"name",name(f)},{"kind",name(*reinterpret_cast<A*>(f+0x24))},{"offset",*reinterpret_cast<int*>(f+0x3c)}};
                    if(name(f)=="EventGroup")
                    {
                        auto item=*reinterpret_cast<A*>(f+0x64);field["innerKind"]=name(*reinterpret_cast<A*>(item+0x24));field["innerStride"]=*reinterpret_cast<unsigned short*>(item+0x32);
                        if(field["innerKind"]=="StructProperty")
                        {
                            auto fields=*reinterpret_cast<A*>(item+0x64);field["structure"]=name(fields);field["fields"]=J::array();
                            // SuperEvent fields use UStruct.Children rather than the cached property chain.
                            for(A child=*reinterpret_cast<A*>(fields+0x30);child;child=*reinterpret_cast<A*>(child+0x2c))
                                field["fields"].push_back({{"name",name(child)},{"kind",name(*reinterpret_cast<A*>(child+0x24))},{"offset",*reinterpret_cast<int*>(child+0x3c)}});
                        }
                    }
                    prop["fields"].push_back(field);
                }
            }
            cls["properties"].push_back(prop);
        }
        result.push_back(cls);
    }
    return result;
}
std::string formName;
bool cancelTagPreview=false;
std::string excludeTagEntity;
std::filesystem::path previewScreenshot;
void Screenshot(HWND window,const std::filesystem::path& path);
bool packagePreviewChecked=false;
std::wstring jsonDialogPath;
bool jsonDialogCancel=false;
bool authoringPreviewCancel=false,authoringPreviewSeen=false;
int jsonDialogTicks=0;
void CALLBACK AnswerJsonDialog(HWND,UINT,UINT_PTR,DWORD)
{
    ++jsonDialogTicks;
    EnumThreadWindows(GetCurrentThreadId(),[](HWND w,LPARAM)->BOOL {
        wchar_t title[256]{};GetWindowTextW(w,title,256);
        if(!wcscmp(title,L"Map JSON")){PostMessage(w,WM_COMMAND,GetDlgItem(w,IDOK)?IDOK:IDCANCEL,0);return TRUE;}
        if(!wcscmp(title,L"Preview Map JSON Import"))
        {
            authoringPreviewSeen=true;if(!previewScreenshot.empty())Screenshot(w,previewScreenshot);
            PostMessage(w,WM_COMMAND,authoringPreviewCancel?IDCANCEL:IDOK,0);return TRUE;
        }
        if(wcscmp(title,L"Choose Reference Image") && wcscmp(title,L"Export Map to JSON") && wcscmp(title,L"Import Map from JSON") && wcscmp(title,L"Export SMagicEvent as JSON") && wcscmp(title,L"Import JSON into the open SMagicEvent"))return TRUE;
        if(jsonDialogCancel || jsonDialogTicks>40){PostMessage(w,WM_COMMAND,IDCANCEL,0);return TRUE;}
        if(jsonDialogTicks!=2 || !IsWindowEnabled(w))return TRUE;
        SendMessageW(w,CDM_SETCONTROLTEXT,edt1,reinterpret_cast<LPARAM>(jsonDialogPath.c_str()));
        EnumChildWindows(w,[](HWND child,LPARAM)->BOOL {
            wchar_t cls[64]{};GetClassNameW(child,cls,64);
            if(!wcscmp(cls,L"Edit") && (GetDlgCtrlID(child)==1001 || GetDlgCtrlID(child)==edt1 || GetDlgCtrlID(GetParent(child))==cmb13 || GetDlgCtrlID(GetParent(GetParent(child)))==cmb13))SetWindowTextW(child,jsonDialogPath.c_str());
            return TRUE;
        },0);
        PostMessage(w,WM_COMMAND,IDOK,0);return TRUE;
    },0);
}
HWND FindDialog(const char* title)
{
    struct Search { const char* title;HWND result; } search{title,nullptr};
    EnumThreadWindows(GetCurrentThreadId(),[](HWND w,LPARAM p)->BOOL {
        auto s=reinterpret_cast<Search*>(p);char text[256]{};GetWindowTextA(w,text,256);
        if(!strcmp(text,s->title)){s->result=w;return FALSE;}return TRUE;
    },reinterpret_cast<LPARAM>(&search));return search.result;
}
void CALLBACK Answer(HWND,UINT,UINT_PTR,DWORD)
{
    EnumThreadWindows(GetCurrentThreadId(),[](HWND w,LPARAM)->BOOL {
        char cls[100]{};GetClassNameA(w,cls,100);
        if(!strcmp(cls,"ReloadedWorkflowForm"))
        {
            char title[256]{};GetWindowTextA(w,title,256);
            if(strstr(title,"Save New") || strstr(title,"Save Selection") || strstr(title,"Rename"))SetWindowTextA(GetDlgItem(w,1000),formName.c_str());
            if(strstr(title,"Edit Assembly Contents"))SendMessage(GetDlgItem(w,1000),CB_SETCURSEL,1,0);
            SendMessage(w,WM_COMMAND,IDOK,0);
        }
        if(!strcmp(cls,"#32770") && GetDlgItem(w,IDOK))
        {
            char title[256]{};GetWindowTextA(w,title,256);
            if(!strcmp(title,"Select saved playable map (save/build your edits first)"))
            {
                packagePreviewChecked=true;
                SendMessage(w,WM_COMMAND,IDCANCEL,0);
                return TRUE;
            }
            if(!strcmp(title,"Package Map for Sharing"))
            {
                if(IsWindowEnabled(GetDlgItem(w,103)))
                {
                    packagePreviewChecked=IsWindowEnabled(GetDlgItem(w,IDOK)) && ListView_GetItemCount(GetDlgItem(w,100))>0;
                    ListView_SetCheckState(GetDlgItem(w,100),0,FALSE);
                    packagePreviewChecked=packagePreviewChecked && ListView_GetCheckState(GetDlgItem(w,100),0);
                    if(!previewScreenshot.empty())Screenshot(w,previewScreenshot);
                    SendMessage(w,WM_COMMAND,IDCANCEL,0);
                }
                return TRUE;
            }
            if(strstr(title,"Tag") && !excludeTagEntity.empty())
            {
                auto list=GetDlgItem(w,1000);char controlClass[100]{};GetClassNameA(list,controlClass,100);
                if(!strcmp(controlClass,WC_LISTVIEWA))for(int row=0;row<ListView_GetItemCount(list);++row)
                {
                    char actor[2048]{};LVITEMA item{};item.iSubItem=0;item.pszText=actor;item.cchTextMax=sizeof(actor);SendMessageA(list,LVM_GETITEMTEXTA,row,reinterpret_cast<LPARAM>(&item));
                    if(excludeTagEntity==actor)ListView_SetCheckState(list,row,FALSE);
                }
                if(!previewScreenshot.empty())Screenshot(w,previewScreenshot);
            }
            SendMessage(w,WM_COMMAND,cancelTagPreview && strstr(title,"Tag")?IDCANCEL:IDOK,0);
        }
        return TRUE;
    },0);
}
void Click(HWND window,int command,const char* name="Native entry")
{
    formName=name;UINT_PTR timer=SetTimer(nullptr,0,50,Answer);SendMessage(window,WM_COMMAND,command,0);KillTimer(nullptr,timer);
}
void Screenshot(HWND window,const std::filesystem::path& path)
{
    RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);
    RECT r{};GetWindowRect(window,&r);int width=r.right-r.left,height=r.bottom-r.top;
    HDC screen=GetDC(window),dc=CreateCompatibleDC(screen);HBITMAP bitmap=CreateCompatibleBitmap(screen,width,height);auto old=SelectObject(dc,bitmap);
    PrintWindow(window,dc,2);SelectObject(dc,old);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    std::vector<char> pixels(static_cast<size_t>(width)*height*4);GetDIBits(dc,bitmap,0,height,pixels.data(),&info,DIB_RGB_COLORS);
    BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);header.bfSize=header.bfOffBits+static_cast<DWORD>(pixels.size());
    std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<char*>(&header),sizeof(header));out.write(reinterpret_cast<char*>(&info.bmiHeader),sizeof(info.bmiHeader));out.write(pixels.data(),pixels.size());
    DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(window,screen);
}
}
void RunWorkflowTests(HMODULE editorDll, const char* destination, bool restart=false, bool snapOnly=false)
{
    using J=nlohmann::json;
    try
    {
        auto oldLibrary=directory/"ReloadedEditor"/"library.json";
        if(!restart && std::filesystem::exists(oldLibrary)) std::filesystem::rename(oldLibrary,directory/"ReloadedEditor"/("library-before-"+std::to_string(GetTickCount64())+".json"));
        using Request=int(__cdecl*)(const char*,char*,unsigned);
        auto request=reinterpret_cast<Request>(GetProcAddress(editorDll,"ReloadedWorkflowRequest"));
        if(!request) request=reinterpret_cast<Request>(GetProcAddress(editorDll,"_ReloadedWorkflowRequest"));
        if(!request) throw std::runtime_error("Workflow API export missing.");
        auto call=[&](J q)->J {
            Record("workflow",q.at("op").get<std::string>().c_str());
            std::vector<char> buffer(8*1024*1024);auto text=q.dump();int ok=request(text.c_str(),buffer.data(),static_cast<unsigned>(buffer.size()));
            if(ok<0) {buffer.resize(-ok);ok=request(text.c_str(),buffer.data(),static_cast<unsigned>(buffer.size()));}
            J result=J::parse(buffer.data());if(!ok || !result.at("ok").get<bool>()) throw std::runtime_error(result.value("error","Request failed"));
            return result.at("result");
        };
        auto require=[&](bool ok,const char* text){if(!ok)throw std::runtime_error(text);Record("workflow_check",text);};
        auto checkSurfaceBrushSelection=[&](size_t minimumBrushes)
        {
            auto read=[](uintptr_t p){return *reinterpret_cast<uintptr_t*>(p);};
            auto model=read(read(read(kEditor)+0x130)+0x13c),surfaces=read(model+0x94);
            auto count=read(model+0x98);require(count>0,"source BSP faces available for brush selection");
            std::vector<DWORD> flags;for(size_t i=0;i<count;++i)flags.push_back(read(surfaces+i*0x2c+0x14));
            auto setFlags=[&](bool all){for(size_t i=0;i<count;++i)*reinterpret_cast<DWORD*>(surfaces+i*0x2c+0x14)=(flags[i]&~0x02000000u)|(all?0x02000000u:0);};
            auto menuEnabled=[&]()
            {
                using Load=HMENU(WINAPI*)(HINSTANCE,LPCSTR);
                auto menu=(*reinterpret_cast<Load*>(0x11af23f0))(GetModuleHandle(nullptr),MAKEINTRESOURCEA(108));
                require(menu!=nullptr,"native surface context menu loads through hook");
                auto state=GetMenuState(GetSubMenu(menu,0),40926,MF_BYCOMMAND);DestroyMenu(menu);
                require(state!=UINT(-1),"Select Brush appears in native face context menu");return !(state&(MF_DISABLED|MF_GRAYED));
            };
            setFlags(false);require(!menuEnabled(),"Select Brush disabled without selected faces");
            *reinterpret_cast<DWORD*>(surfaces+0x14)|=0x02000000u;
            auto cameras=call({{"op","view.capture"}})["cameras"];
            auto expected=call({{"op","surface.brushes"}});require(expected.size()==1 && menuEnabled(),"selected face resolves one editable source brush");
            call({{"op","select"},{"actors",J::array()}});SendMessage(frameWindow,WM_COMMAND,40926,0);
            auto selected=call({{"op","actors"},{"selected",true}});
            require(selected.size()==1 && selected[0]["path"]==expected[0]["path"],"face menu command selects its source brush through native actor selection");
            require(call({{"op","view.capture"}})["cameras"]==cameras,"selecting source brush preserves all viewport cameras");
            require((read(surfaces+0x14)&0x02000000u)!=0,"brush selection preserves selected face");
            {
                auto actor=WorkflowProbe::MagicActor(expected[0]);float original[3];memcpy(original,actor+0x80,12);
                for(int axis=0;axis<3;++axis)reinterpret_cast<float*>(actor+0x80)[axis]+=.375f;
                const auto before=call({{"op","brush.snap.bounds"},{"surfaces",true}});
                using Load=HMENU(WINAPI*)(HINSTANCE,LPCSTR);
                auto menu=(*reinterpret_cast<Load*>(0x11af23f0))(GetModuleHandle(nullptr),MAKEINTRESOURCEA(108));
                require(menu && GetMenuState(GetSubMenu(menu,0),40943,MF_BYCOMMAND)!=UINT(-1),"surface context offers all-axis edge snap");DestroyMenu(menu);
                SendMessage(frameWindow,WM_COMMAND,40943,0);
                const auto after=call({{"op","brush.snap.bounds"},{"surfaces",true}});
                for(int axis=0;axis<3;++axis){double grid=after["grid"][axis];bool aligned=false;for(const char* bound:{"min","max"}){double v=after[bound][axis].get<double>()/grid;aligned|=std::abs(v-std::round(v))<.0001;}require(aligned,"face menu snaps source brush bounds");}
                Exec("TRANSACTION UNDO");require(call({{"op","brush.snap.bounds"},{"surfaces",true}})==before,"surface snap Undo restores source brush and selection");
                memcpy(actor+0x80,original,12);
            }
            auto poly=read(surfaces+0x20);*reinterpret_cast<uintptr_t*>(surfaces+0x20)=0;
            require(!menuEnabled(),"face without source polygon disables Select Brush");
            bool rejected=false;try{call({{"op","surface.brushes"}});}catch(const std::exception&){rejected=true;}
            *reinterpret_cast<uintptr_t*>(surfaces+0x20)=poly;
            require(rejected && call({{"op","actors"},{"selected",true}})==selected,"missing source is rejected without changing actor selection");
            setFlags(true);expected=call({{"op","surface.brushes"}});
            require(expected.size()>=minimumBrushes,"multiple selected faces resolve distinct source brushes");
            SendMessage(frameWindow,WM_COMMAND,40926,0);selected=call({{"op","actors"},{"selected",true}});
            require(selected.size()==expected.size(),"multiple faces select each owning brush once");
            for(const auto& brush:expected){bool found=false;for(const auto& a:selected)if(a["path"]==brush["path"])found=true;require(found,"owning brush remains selected across viewports");}
            for(size_t i=0;i<count;++i)*reinterpret_cast<DWORD*>(surfaces+i*0x2c+0x14)=flags[i];
        };
        std::ofstream(directory/"workflow_magic_schema.json")<<WorkflowProbe::MagicSchema().dump(2);
        if(restart)
        {
            require(Exec(std::string("MAP LOAD FILE=\"")+destination+"\"")!=0,"fresh editor opens saved workflow map");
            J savedAuthoring;std::ifstream(directory/"map_authoring_saved.json")>>savedAuthoring;
            for(const auto& actor:savedAuthoring)require(call({{"op","magic.inspect"},{"actor",actor.at("actor")}})["values"]==actor.at("values"),"batch-authored actors, particle components and delayed links survive restart");
            J savedCameras;std::ifstream cameraInput(directory/"camera_network_saved.json");cameraInput>>savedCameras;
            for(const auto& camera:savedCameras)require(call({{"op","magic.inspect"},{"actor",camera.at("actor")}})["values"]==camera.at("values"),"camera names, pose and network links survive editor restart");
            std::ifstream magicInput(directory/"magic_setpiece.json");J savedMagic;magicInput>>savedMagic;
            for(const auto& savedActor:savedMagic)require(call({{"op","magic.inspect"},{"actor",savedActor.at("actor")}})["values"]==savedActor.at("values"),"set piece actor settings survive a full editor restart");
            auto reopenedEvent=call({{"op","magic.inspect"},{"actor",savedMagic[0]["actor"]}});auto reopenedGroups=reopenedEvent["values"]["Groups"];reopenedGroups[0]["EventGroup"][0]["Delay"]="1.5";
            call({{"op","magic.edit"},{"snapshot",reopenedEvent},{"changes",{{"Groups",reopenedGroups}}}});Exec("TRANSACTION UNDO");
            require(call({{"op","magic.inspect"},{"actor",savedMagic[0]["actor"]}})["values"]==reopenedEvent["values"],"reopened event uses the same editing and undo workflow");
            using Filename=void(__thiscall*)(void*,const char*);
            reinterpret_cast<Filename>(0x10e05e1c)(*reinterpret_cast<void**>(0x1165e80c),destination);
            std::ifstream input(oldLibrary);J saved;input>>saved;input.close();
            auto key=call({{"op","map"}}).at("key").get<std::string>();auto view=saved.at("maps").at(key).at("views").at(0);
            SendMessage(frameWindow,WM_COMMAND,40921,0);auto window=WorkflowProbe::FindDialog("Working Views");require(window!=nullptr,"working views reopen after process restart");
            ListView_SetItemState(GetDlgItem(window,100),0,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
            WorkflowProbe::Click(window,201);
            require(call({{"op","view.capture"}}).at("cameras")==view.at("cameras"),"saved adjusted cameras restore after process restart");
            WorkflowProbe::Click(window,203);std::ifstream updatedInput(oldLibrary);J updated;updatedInput>>updated;
            require(updated["maps"][key]["views"].size()==1 && updated["maps"][key]["views"][0]["id"]==view["id"] && updated["maps"][key]["views"][0]["name"]==view["name"],"view update after restart preserves identity and name without duplicates");
            auto count=call({{"op","actors"}}).size();auto entry=updated.at("assemblies").at(0);
            auto placed=call({{"op","assembly.place"},{"definition",entry},{"position",{0,512,0}},{"rotation",{0,0,0}},{"bindings",J::object()}});
            require(placed.at("members").size()==entry.at("actors").size() && call({{"op","actors"}}).size()==count+entry.at("actors").size(),"updated assembly persists for future placements after restart");
            DestroyWindow(window);Record("PASS","Workflow views and updated assemblies survive a full editor restart.");return;
        }
        J actors=call({{"op","actors"}});require(actors.size()>=3,"fixture actors available");
        checkSurfaceBrushSelection(1);
        {
            auto selection=call({{"op","actors"},{"selected",true}});
            auto brush=std::find_if(actors.begin(),actors.end(),[](const J& a){return a.at("class")=="Engine.Brush" && a.value("authorable",false);});
            require(brush!=actors.end(),"grid snap fixture brush available");
            {
                const auto view=call({{"op","view.capture"}});
                const auto rows=call({{"op","brush.visibility"}});
                require(rows.size()==7,"brush visibility has all categories");
                const int category=rows[3]["total"].get<int>()?3:2;
                require(rows[category]["total"].get<int>()>0,"CSG brushes classified");
                call({{"op","brush.visibility.set"},{"category",category},{"action","select"}});
                require(call({{"op","actors"},{"selected",true}}).size()==rows[category]["total"].get<size_t>(),"select type selects matching brushes");
                call({{"op","brush.visibility.set"},{"category",category},{"action","hide"}});
                require(call({{"op","brush.visibility"}})[category]["visible"]==0,"hide type hides brushes");
                require(call({{"op","actors"},{"selected",true}}).empty(),"hiding brushes clears their selection");
                Exec("TRANSACTION UNDO");
                require(call({{"op","brush.visibility"}})[category]["visible"]==rows[category]["total"],"visibility Undo restores brushes");
                call({{"op","brush.visibility.set"},{"category",category},{"action","only"}});
                const auto isolated=call({{"op","brush.visibility"}});
                for(int i=0;i<7;++i)require(isolated[i]["visible"]==(i==category?isolated[i]["total"]:J(0)),"only type isolates matching actors");
                SendMessage(frameWindow,WM_COMMAND,40953,0);
                HWND panel=WorkflowProbe::FindDialog("Brush Visibility");require(panel!=nullptr,"brush visibility panel opens");
                WorkflowProbe::Screenshot(panel,directory/"brush_visibility.bmp");
                WorkflowProbe::Click(panel,200);
                const auto shown=call({{"op","brush.visibility"}});
                for(auto& row:shown)require(row["visible"]==row["total"],"show all reveals every category");
                SendMessage(GetDlgItem(panel,100+category*3),BM_CLICK,0,0);
                require(call({{"op","brush.visibility"}})[category]["visible"]==0,"visibility checkbox hides a type");
                SendMessage(GetDlgItem(panel,100+category*3),BM_CLICK,0,0);
                require(call({{"op","brush.visibility"}})[category]["visible"]==rows[category]["total"],"visibility checkbox reveals a type");
                DestroyWindow(panel);
                SetActiveWindow(frameWindow);SetForegroundWindow(frameWindow);
                call({{"op","view.restore"},{"view",view}});
            }
            auto native=WorkflowProbe::MagicActor(*brush);
            float original[3];memcpy(original,native+0x80,12);
            const float displaced[3]={original[0]+1.25f,original[1]-2.25f,original[2]+3.25f};memcpy(native+0x80,displaced,12);
            auto editor=*reinterpret_cast<unsigned char**>(kEditor);float savedGrid[3];memcpy(savedGrid,editor+0x200,12);
            const float grid[3]={16,32,8};memcpy(editor+0x200,grid,12);
            call({{"op","select"},{"actors",J::array({*brush})}});
            const auto before=call({{"op","brush.snap.bounds"}});
            using Load=HMENU(WINAPI*)(HINSTANCE,LPCSTR);
            auto menu=(*reinterpret_cast<Load*>(0x11af23f0))(GetModuleHandle(nullptr),MAKEINTRESOURCEA(107));
            require(menu && GetMenuState(GetSubMenu(menu,0),40936,MF_BYCOMMAND)!=UINT(-1),"brush context has axis snap commands");DestroyMenu(menu);
            SendMessage(frameWindow,WM_COMMAND,40936,0);
            auto after=call({{"op","brush.snap.bounds"}});
            auto aligned=[&](const J& b,int axis){for(const char* bound:{"min","max"}){double v=b.at(bound).at(axis).get<double>();if(std::abs(v/std::abs(grid[axis])-std::round(v/std::abs(grid[axis])))<.0001)return true;}return false;};
            require(aligned(after,0),"X outer bound lands on active grid");
            for(int axis=1;axis<3;++axis)require(after["min"][axis]==before["min"][axis] && after["max"][axis]==before["max"][axis],"X snap preserves other axes");
            Exec("TRANSACTION UNDO");require(call({{"op","brush.snap.bounds"}})==before,"grid snap Undo restores bounds and selection");
            Exec("TRANSACTION REDO");require(call({{"op","brush.snap.bounds"}})==after,"grid snap Redo restores snapped bounds");
            call({{"op","brush.snap"},{"axes",7}});after=call({{"op","brush.snap.bounds"}});
            for(int axis=0;axis<3;++axis){require(aligned(after,axis),"all-axis bounds snap");require(std::abs((after["max"][axis].get<double>()-after["min"][axis].get<double>())-(before["max"][axis].get<double>()-before["min"][axis].get<double>()))<.001,"snap preserves brush dimensions");}
            call({{"op","brush.snap"},{"axes",7}});
            Exec("TRANSACTION UNDO");require(aligned(call({{"op","brush.snap.bounds"}}),0),"already aligned snap does not add an Undo step");
            Exec("TRANSACTION UNDO");require(call({{"op","brush.snap.bounds"}})==before,"two edits require two Undo steps");
            call({{"op","select"},{"actors",J::array()}});bool rejected=false;
            try{call({{"op","brush.snap"},{"axes",1}});}catch(const std::exception&){rejected=true;}require(rejected,"empty brush snap rejected");
            memcpy(native+0x80,original,12);memcpy(editor+0x200,savedGrid,12);
            call({{"op","select"},{"actors",selection}});
        }
        {
            const auto brush=*std::find_if(actors.begin(),actors.end(),[](const J& a){return a.at("class")=="Engine.Brush" && a.value("authorable",false);});
            call({{"op","select"},{"actors",J::array({brush})}});
            auto actor=WorkflowProbe::MagicActor(brush),editor=*reinterpret_cast<unsigned char**>(kEditor);
            auto model=*reinterpret_cast<unsigned char**>(actor+0x238),polys=*reinterpret_cast<unsigned char**>(model+0x50);
            const int count=*reinterpret_cast<int*>(polys+0x2c);auto data=*reinterpret_cast<unsigned char**>(polys+0x28);
            std::vector<unsigned char> original(data,data+count*0x14c);
            const int mode=*reinterpret_cast<int*>(editor+0x1ac);*reinterpret_cast<int*>(editor+0x1ac)=0x19;
            float grid[3];memcpy(grid,editor+0x200,12);const float testGrid[3]={16,16,16};memcpy(editor+0x200,testGrid,12);
            auto selectionHeader=reinterpret_cast<uintptr_t*>(0x11685a9c);std::array<uintptr_t,3> savedHeader{selectionHeader[0],selectionHeader[1],selectionHeader[2]};
            std::vector<std::array<uintptr_t,3>> entries;
            float corner[3];memcpy(corner,data+0x18,12);
            for(int pi=0;pi<count;++pi){auto p=data+pi*0x14c;for(int vi=0;vi<*reinterpret_cast<unsigned short*>(p+0x148);++vi){auto v=reinterpret_cast<float*>(p+0x18+vi*12);if(!memcmp(v,corner,12)){v[0]+=1.25f;v[1]-=2.25f;v[2]+=3.25f;entries.push_back({reinterpret_cast<uintptr_t>(actor),static_cast<uintptr_t>(pi),static_cast<uintptr_t>(vi)});}}}
            require(entries.size()>=3,"vertex fixture includes all polygon copies of one corner");
            selectionHeader[0]=reinterpret_cast<uintptr_t>(entries.data());selectionHeader[1]=selectionHeader[2]=entries.size();
            auto before=call({{"op","vertex.selection"}});std::vector<unsigned char> displaced(data,data+count*0x14c);
            unsigned cause[3]={0,0,2};auto timer=SetTimer(nullptr,0,50,WorkflowProbe::InspectVertexPopup);
            reinterpret_cast<void(__thiscall*)(void*,void*,void*)>(0x10e045e9)(nullptr,cause,nullptr);KillTimer(nullptr,timer);
            require(WorkflowProbe::vertexPopupChecked && call({{"op","vertex.selection"}})==before,"right-click vertex menu preserves selection and cancelled geometry");
            SendMessage(frameWindow,WM_COMMAND,40944,0);auto after=call({{"op","vertex.selection"}});
            for(size_t i=0;i<after.size();++i){double x=after[i]["world"][0];require(std::abs(x/16-std::round(x/16))<.0001,"selected vertex X snaps in world space");for(int axis=1;axis<3;++axis)require(before[i]["world"][axis]==after[i]["world"][axis],"vertex X snap preserves Y and Z");}
            for(int pi=0;pi<count;++pi){auto p=data+pi*0x14c;for(int vi=0;vi<*reinterpret_cast<unsigned short*>(p+0x148);++vi){bool chosen=false;for(auto e:entries)chosen|=e[1]==pi && e[2]==vi;if(!chosen)require(!memcmp(p+0x18+vi*12,displaced.data()+pi*0x14c+0x18+vi*12,12),"unselected vertices remain exact");}}
            Exec("TRANSACTION UNDO");auto undone=call({{"op","vertex.selection"}});if(undone!=before){Record("vertex_before",before.dump().c_str());Record("vertex_undone",undone.dump().c_str());}require(undone==before,"vertex snap Undo restores selected corner");
            Exec("TRANSACTION REDO");require(call({{"op","vertex.selection"}})==after,"vertex snap Redo restores selected corner");
            call({{"op","vertex.snap"},{"axes",7}});after=call({{"op","vertex.selection"}});
            for(auto v:after)for(double coordinate:v["world"]){require(std::abs(coordinate/16-std::round(coordinate/16))<.0001,"selected corner all-axis alignment");}
            require(after[0]["world"]==after.back()["world"],"shared corner stays joined");
            Exec("TRANSACTION UNDO");Exec("TRANSACTION UNDO");
            // Undo may reallocate the UPolys array. Always resolve it again.
            data=*reinterpret_cast<unsigned char**>(polys+0x28);memcpy(data,original.data(),original.size());
            std::copy(savedHeader.begin(),savedHeader.end(),selectionHeader);*reinterpret_cast<int*>(editor+0x1ac)=mode;memcpy(editor+0x200,grid,12);
        }
        if(snapOnly){Record("PASS","native brush and selected-vertex grid snap commands and transactions");return;}
        {
            auto inspect=[&](const J& actor){return call({{"op","magic.inspect"},{"actor",actor}});};
            auto ref=[](const J& actor){auto type=actor.at("class").get<std::string>();return type.substr(type.find_last_of('.')+1)+"'"+actor.at("path").get<std::string>()+"'";};
            auto add=[&](const J& owner,const char* type){return call({{"op","objective.add"},{"owner",inspect(owner)},{"class",type}});};
            auto menuHas=[&](const J& actor,UINT command)
            {
                call({{"op","select"},{"actors",J::array({actor})}});
                using Load=HMENU(WINAPI*)(HINSTANCE,LPCSTR);
                auto menu=(*reinterpret_cast<Load*>(0x11af23f0))(GetModuleHandle(nullptr),MAKEINTRESOURCEA(107));
                auto state=GetMenuState(GetSubMenu(menu,0),command,MF_BYCOMMAND);DestroyMenu(menu);return state!=UINT(-1);
            };
            auto mission=call({{"op","magic.create"},{"class","SBase.SMission"}});
            require(menuHas(mission,40949) && !menuHas(mission,40950),"mission context offers Add SObjective only");
            auto objective=add(mission,"SBase.SObjective").at(0);
            auto second=add(mission,"SBase.SObjective").at(0);
            require(inspect(mission)["values"]["Objectives"]==J::array({ref(objective),ref(second)}),"adding objectives preserves existing mission links");
            require(menuHas(objective,40950) && menuHas(objective,40951) && menuHas(objective,40952) && !menuHas(objective,40949),"objective context offers computer, bomb and flag pair");
            auto computer=add(objective,"SBase.SComputerObjectiveTrigger").at(0);
            auto bomb=add(objective,"SBase.SBombTargetObjectiveTrigger").at(0);
            auto before=inspect(objective);auto pair=add(objective,"SBase.SFlag");
            require(pair.size()==2,"flag action creates two actors");
            auto flag=pair.at(1),drop=pair.at(0);
            require(inspect(flag)["values"]["DropZone"][0]==ref(drop),"flag references its new drop zone");
            require(inspect(objective)["values"]["Triggers"]==J::array({ref(computer),ref(bomb),ref(flag)}),"objective retains all three linked triggers");
            auto after=inspect(objective)["values"];
            auto nearby=[&](const J& actor,const J& parent){auto a=inspect(actor)["values"]["Location"],b=inspect(parent)["values"]["Location"];double d=0;for(const char* axis:{"X","Y","Z"}){auto delta=std::stod(a[axis].get<std::string>())-std::stod(b[axis].get<std::string>());d+=delta*delta;}return d>0 && d<=32*32;};
            require(nearby(objective,mission) && nearby(computer,objective) && nearby(bomb,objective) && nearby(flag,objective) && nearby(drop,objective),"all objective actors including drop zone are within 32 units of their parent");
            auto count=call({{"op","actors"}}).size();
            Exec("TRANSACTION UNDO");
            require(inspect(objective)["values"]==before["values"] && call({{"op","actors"}}).size()==count-2,"one undo removes flag pair and restores objective links");
            Exec("TRANSACTION REDO");
            require(inspect(objective)["values"]==after && inspect(flag)["values"]["DropZone"][0]==ref(drop),"redo restores flag pair and all links");
            auto edges=call({{"op","connections"},{"actor",flag["path"]}});
            require(edges.size()>=2,"connections manager reports objective and drop-zone links");
            bool stale=false;try{call({{"op","objective.add"},{"owner",before},{"class","SBase.SFlag"}});}catch(const std::exception&){stale=true;}
            require(stale && call({{"op","actors"}}).size()==count,"stale objective request leaves map unchanged");
            bool wrong=false;try{add(mission,"SBase.SComputerObjectiveTrigger");}catch(const std::exception&){wrong=true;}
            require(wrong && call({{"op","actors"}}).size()==count,"wrong parent is rejected without creating actors");
            call({{"op","select"},{"actors",J::array({mission})}});
            SendMessage(frameWindow,WM_COMMAND,40949,0);
            require(inspect(mission)["values"]["Objectives"].size()==3,"native context command creates and links objective");
            Exec("TRANSACTION UNDO");
            // Exercise the actual popup import with a non-frame owner; direct
            // WM_COMMAND to frameWindow misses the viewport routing regression.
            call({{"op","select"},{"actors",J::array({mission})}});
            auto popupOwner=CreateWindowExA(0,"STATIC","Objective popup fixture",WS_POPUP,20,20,300,200,nullptr,nullptr,GetModuleHandle(nullptr),nullptr);
            ShowWindow(popupOwner,SW_SHOWNOACTIVATE);
            using Load=HMENU(WINAPI*)(HINSTANCE,LPCSTR);using Track=BOOL(WINAPI*)(HMENU,UINT,int,int,int,HWND,const RECT*);
            auto menu=(*reinterpret_cast<Load*>(0x11af23f0))(GetModuleHandle(nullptr),MAKEINTRESOURCEA(107));
            WorkflowProbe::objectivePopupChoice=40949;WorkflowProbe::objectivePopupFound=false;WorkflowProbe::objectivePopupTicks=0;
            auto popupTimer=SetTimer(nullptr,0,50,WorkflowProbe::ChooseObjectivePopup);
            auto popupResult=(*reinterpret_cast<Track*>(0x11af23c0))(GetSubMenu(menu,0),TPM_RIGHTBUTTON,40,40,0,popupOwner,nullptr);
            KillTimer(nullptr,popupTimer);DestroyMenu(menu);DestroyWindow(popupOwner);
            Record("objective_popup_result",(std::to_string(popupResult)+" ticks="+std::to_string(WorkflowProbe::objectivePopupTicks)).c_str());
            require(WorkflowProbe::objectivePopupFound && inspect(mission)["values"]["Objectives"].size()==3,"viewport-style popup owner routes Add SObjective without the frame dispatcher");
            Exec("TRANSACTION UNDO");
            call({{"op","select"},{"actors",J::array({objective})}});
            SendMessage(frameWindow,WM_COMMAND,40920,0);
            auto manager=WorkflowProbe::FindDialog("Gameplay Connections");
            require(manager && IsWindowEnabled(GetDlgItem(manager,202)),"connections manager enables objective creation for its current actor");
            SendMessage(manager,WM_COMMAND,203,0);auto objectiveGraph=WorkflowProbe::FindDialog("Gameplay Connection Graph");
            require(objectiveGraph!=nullptr,"objective graph opens");
            SendMessage(GetDlgItem(objectiveGraph,104),BM_SETCHECK,BST_UNCHECKED,0);
            call({{"op","select"},{"actors",J::array({mission})}});
            WorkflowProbe::objectivePopupChoice=40952;WorkflowProbe::objectivePopupFound=false;WorkflowProbe::objectivePopupTicks=0;
            popupTimer=SetTimer(nullptr,0,50,WorkflowProbe::ChooseObjectivePopup);
            SendMessage(GetDlgItem(objectiveGraph,120),WM_CONTEXTMENU,0,MAKELPARAM(-1,-1));KillTimer(nullptr,popupTimer);
            require(WorkflowProbe::objectivePopupFound && inspect(objective)["values"]["Triggers"].size()==4,"graph node popup adds linked flag pair to its node despite different viewport selection");
            Exec("TRANSACTION UNDO");SendMessage(objectiveGraph,WM_CLOSE,0,0);
            SendMessage(GetDlgItem(manager,103),BM_SETCHECK,BST_UNCHECKED,0);
            call({{"op","select"},{"actors",J::array({computer})}});SendMessage(manager,WM_COMMAND,200,0);
            require(IsWindowEnabled(GetDlgItem(manager,202)),"connections manager retains objective target with Follow Selection off");
            SendMessage(GetDlgItem(manager,103),BM_SETCHECK,BST_CHECKED,0);SendMessage(manager,WM_COMMAND,200,0);
            require(!IsWindowEnabled(GetDlgItem(manager,202)),"connections manager disables objective creation for unrelated actors");
            SendMessage(manager,WM_CLOSE,0,0);
            call({{"op","select"},{"actors",J::array()}});
        }
        {
            auto baseline=call({{"op","design.scene"}});
            require(Exec(std::string("MAP SAVE FILE=\"")+destination+"\"")!=0,"save disposable map before design metadata persistence checks");
            using DesignFilename=void(__thiscall*)(void*,const char*);
            reinterpret_cast<DesignFilename>(0x10e05e1c)(*reinterpret_cast<void**>(0x1165e80c),destination);
            std::ofstream(directory/"design_clearances.json")<<call({{"op","design.clearances"}}).dump(2);
            J spec={{"kind","Room"},{"width",512},{"length",768},{"height",256},{"thickness",16},{"steps",8},{"ceiling",true},{"name","Native design room"}};
            auto create=[&](J value,J previous=J{}){return call({{"op","design.block"},{"spec",value},{"position",{1024,2048,64}},{"rotation",{0,16384,0}},{"previous",previous}});};
            auto room=create(spec);require(room["members"].size()==7,"blockout creates subtractive space and six boundary brushes");
            auto live=call({{"op","design.scene"}});require(live.size()==baseline.size()+7,"blockout native actor count");
            for(auto& member:room["members"]){auto a=std::find_if(live.begin(),live.end(),[&](const J& item){return item["path"]==member["path"];});require(a!=live.end() && (*a)["edges"].size()==24,"blockout exports real native brush geometry");}
            spec["width"]=640;auto resized=create(spec,room);require(call({{"op","design.scene"}}).size()==live.size(),"dimension edit replaces brushes without accumulating duplicates");
            Exec("TRANSACTION UNDO");auto undone=call({{"op","design.scene"}});
            require(undone.size()==live.size(),"resize undoes as one transaction");
            for(auto& member:room["members"])require(std::any_of(undone.begin(),undone.end(),[&](const J& item){return item["path"]==member["path"];}),"resize undo restores original brush identities");
            Exec("TRANSACTION REDO");call({{"op","design.layer"},{"members",resized["members"]},{"hidden",true},{"locked",true}});
            auto hidden=call({{"op","design.scene"}});for(auto& member:resized["members"])for(auto& a:hidden)if(a["path"]==member["path"])require(a["hidden"]==true && a["locked"]==true,"native layer hides and locks actors");
            Exec("TRANSACTION UNDO");Exec("TRANSACTION UNDO");Exec("TRANSACTION UNDO");require(call({{"op","design.scene"}}).size()==baseline.size(),"blockout undo returns to baseline");
            spec["kind"]="Stairs";auto stairs=create(spec);require(stairs["members"].size()==8,"native stair brush generation");Exec("TRANSACTION UNDO");
            spec["kind"]="Ramp";auto ramp=create(spec);auto rampScene=call({{"op","design.scene"}});J selected=J::array();for(auto& a:rampScene)if(a["path"]==ramp["members"][0]["path"])selected.push_back(a);
            require(selected.size()==1 && selected[0]["edges"].size()==18,"ramp imports five outward polygon faces");
            call({{"op","design.align"},{"scene",selected},{"axis",0},{"mode","Align"}});Exec("TRANSACTION UNDO");Exec("TRANSACTION UNDO");
            auto starts=call({{"op","design.spawns"}});
            if(starts.empty()){call({{"op","magic.create"},{"class","Engine.PlayerStart"}});starts=call({{"op","design.spawns"}});}
            require(!starts.empty(),"temporary spawn candidates available");
            call({{"op","design.play"},{"start",starts[0]},{"position",{300,400,500}},{"rotation",{0,16384,0}},{"launch",false}});
            require(call({{"op","design.spawns"}})==starts,"temporary playtest restores exact spawn positions and rotations");
            WorkflowProbe::designLaunchStarts=J::array();for(auto& start:starts)if(start["team"]==starts[0]["team"])WorkflowProbe::designLaunchStarts.push_back(start);
            auto launchSlot=reinterpret_cast<void**>(0x11af228c);auto originalLaunch=*launchSlot;DWORD protection=0;require(VirtualProtect(launchSlot,sizeof(void*),PAGE_READWRITE,&protection)!=0,"test launch interception available");
            auto minimizeSlot=reinterpret_cast<void**>(0x11af23cc);auto originalMinimize=*minimizeSlot;*minimizeSlot=reinterpret_cast<void*>(&WorkflowProbe::DesignKeepEditorWindow);
            *launchSlot=reinterpret_cast<void*>(&WorkflowProbe::DesignCaptureLaunch);auto playTimer=SetTimer(nullptr,0,50,WorkflowProbe::AcceptDesignPlay);
            try{call({{"op","design.play"},{"start",starts[0]},{"position",{300,400,500}},{"rotation",{0,16384,0}},{"launch",true}});}
            catch(...){KillTimer(nullptr,playTimer);*minimizeSlot=originalMinimize;*launchSlot=originalLaunch;DWORD ignored;VirtualProtect(launchSlot,sizeof(void*),protection,&ignored);throw;}
            KillTimer(nullptr,playTimer);*minimizeSlot=originalMinimize;*launchSlot=originalLaunch;DWORD ignored;VirtualProtect(launchSlot,sizeof(void*),protection,&ignored);
            require(WorkflowProbe::designLaunchSeen && WorkflowProbe::designLaunchPose,"native Play Level reaches launch with the temporary team spawn pose");
            require(call({{"op","design.spawns"}})==starts,"native Play Level returns with original editor spawns restored");
            SendMessage(frameWindow,WM_COMMAND,40948,0);auto design=WorkflowProbe::FindDialog("Map Design");require(design!=nullptr,"Map Design menu opens native workspace");
            WorkflowProbe::Click(design,706); // New blockout, then verify a preview has no native actors.
            auto previewCount=call({{"op","actors"}}).size();WorkflowProbe::Screenshot(design,directory/"map_design_preview.bmp");
            SendMessage(design,WM_COMMAND,708,0);require(call({{"op","actors"}}).size()==previewCount+7,"design preview applies through its actual Place button");
            WorkflowProbe::Click(design,710); // Configurable player clearance guide.
            SendMessage(design,WM_COMMAND,711,0);auto canvas=GetDlgItem(design,719);
            SendMessage(canvas,WM_LBUTTONDOWN,0,MAKELPARAM(100,100));SendMessage(canvas,WM_LBUTTONDOWN,0,MAKELPARAM(300,100));
            WorkflowProbe::Click(design,716);SendMessage(canvas,WM_LBUTTONDOWN,0,MAKELPARAM(200,220));SendMessage(canvas,WM_LBUTTONDOWN,0,MAKELPARAM(400,300));SendMessage(design,WM_COMMAND,717,0);
            WorkflowProbe::Click(design,713,"Design layer");
            // Generate a tiny reference image locally; no copyrighted reference
            // imagery or user's files are needed for this UI test.
            auto image=directory/"design_reference.bmp";BITMAPFILEHEADER bh{};BITMAPINFOHEADER bi{};bi.biSize=sizeof(bi);bi.biWidth=64;bi.biHeight=64;bi.biPlanes=1;bi.biBitCount=32;bh.bfType=0x4d42;bh.bfOffBits=sizeof(bh)+sizeof(bi);bh.bfSize=bh.bfOffBits+64*64*4;
            {std::ofstream out(image,std::ios::binary);out.write(reinterpret_cast<char*>(&bh),sizeof(bh));out.write(reinterpret_cast<char*>(&bi),sizeof(bi));for(int y=0;y<64;++y)for(int x=0;x<64;++x){unsigned colour=(x%16<2||y%16<2)?0x00406080:0x00f0e0c0;out.write(reinterpret_cast<char*>(&colour),4);}}
            WorkflowProbe::jsonDialogPath=image.wstring();WorkflowProbe::jsonDialogCancel=false;
            auto fileTimer=SetTimer(nullptr,0,50,WorkflowProbe::AnswerJsonDialog);auto formTimer=SetTimer(nullptr,0,60,WorkflowProbe::Answer);SendMessage(design,WM_COMMAND,703,0);KillTimer(nullptr,fileTimer);KillTimer(nullptr,formTimer);
            SendMessage(design,WM_COMMAND,704,0);SendMessage(canvas,WM_LBUTTONDOWN,0,MAKELPARAM(100,100));
            formTimer=SetTimer(nullptr,0,50,WorkflowProbe::Answer);SendMessage(canvas,WM_LBUTTONDOWN,0,MAKELPARAM(300,100));KillTimer(nullptr,formTimer);
            WorkflowProbe::Screenshot(design,directory/"map_design_workspace.bmp");
            auto key=call({{"op","map"}})["key"].get<std::string>();std::ifstream libraryInput(directory/"ReloadedEditor"/"library.json");J designLibrary;libraryInput>>designLibrary;
            auto savedDesign=designLibrary["maps"][key]["design"];
            require(savedDesign["annotations"].size()==2 && savedDesign["layers"].size()==1 && savedDesign["guides"].size()==1 && savedDesign.contains("reference"),"design reference, measurement, route, guide and layer persist from actual dialogs");
            DestroyWindow(design);Exec("TRANSACTION UNDO");
            Record("workflow_check","Map Design native blockout, layers, spawn restoration and UI passed");
        }
        {
            auto originalSelection=call({{"op","actors"},{"selected",true}});
            require(Exec("OBJ LOAD FILE=\"..\\Packages\\StaticMeshes\\TestMapStaticM.usx\"")!=0,"load mesh fixture package");
            auto assets=call({{"op","magic.assets"},{"type","StaticMesh"}});require(!assets.empty(),"mesh fixture asset available");
            auto mesh=call({{"op","magic.create"},{"class","Engine.StaticMeshActor"}});
            auto native=WorkflowProbe::MagicActor(mesh);
            auto field=[&](const char* name)->unsigned char* {
                for(auto type=*reinterpret_cast<unsigned char**>(native+0x24);type;type=*reinterpret_cast<unsigned char**>(type+0x28))
                    for(auto p=*reinterpret_cast<unsigned char**>(type+0x58);p;p=*reinterpret_cast<unsigned char**>(p+0x40))
                        if(!strcmp(ObjectName(p),name))return native+*reinterpret_cast<int*>(p+0x3c);
                throw std::runtime_error(std::string("Mesh fixture property missing: ")+name);
            };
            auto wanted=assets[0]["path"].get<std::string>();auto objects=*reinterpret_cast<unsigned char***>(0x11697b70);int objectCount=*reinterpret_cast<int*>(0x11697b74);unsigned char* meshObject=nullptr;
            for(int i=0;i<objectCount;++i)if(objects[i]){std::string path;for(auto object=objects[i];object;object=*reinterpret_cast<unsigned char**>(object+0x18))path=std::string(ObjectName(object))+(path.empty()?"":"."+path);if(path==wanted){meshObject=objects[i];break;}}
            require(meshObject!=nullptr,"resolve fixture mesh object");*reinterpret_cast<void**>(field("StaticMesh"))=meshObject;
            auto vector=[&](const char* name,float x,float y,float z){float value[3]={x,y,z};memcpy(field(name),value,sizeof(value));};
            vector("Location",0,0,0);vector("PrePivot",0,0,0);vector("DrawScale3D",1,1,1);*reinterpret_cast<float*>(field("DrawScale"))=1;
            memset(field("Rotation"),0,12);
            call({{"op","select"},{"actors",J::array({mesh})}});
            auto local=call({{"op","mesh.bounds"}});
            vector("Location",200,-100,75);vector("PrePivot",3,5,7);vector("DrawScale3D",-1,0.5f,1.5f);*reinterpret_cast<float*>(field("DrawScale"))=2;
            int rotation[3]={0,16384,0};memcpy(field("Rotation"),rotation,sizeof(rotation));
            auto bounds=call({{"op","mesh.bounds"}});
            // Independent quarter-turn oracle: world=(200-y, -100-2*x, 75+3*z), after subtracting PrePivot.
            double lo[3]={200-(local["max"][1].get<double>()-5),-100-2*(local["max"][0].get<double>()-3),75+3*(local["min"][2].get<double>()-7)};
            double hi[3]={200-(local["min"][1].get<double>()-5),-100-2*(local["min"][0].get<double>()-3),75+3*(local["max"][2].get<double>()-7)};
            for(int axis=0;axis<3;++axis)require(std::abs(bounds["min"][axis].get<double>()-lo[axis])<0.01 && std::abs(bounds["max"][axis].get<double>()-hi[axis])<0.01,"mesh bounds include rotation, signed nonuniform scale and pivot");
            auto level=*reinterpret_cast<unsigned char**>(*reinterpret_cast<uintptr_t*>(kEditor)+0x130);
            auto builder=(*reinterpret_cast<unsigned char***>(level+0x2c))[1];
            auto brushState=[&](){
                J value={{"Location",{{"X",std::to_string(*reinterpret_cast<float*>(builder+0x80))},{"Y",std::to_string(*reinterpret_cast<float*>(builder+0x84))},{"Z",std::to_string(*reinterpret_cast<float*>(builder+0x88))}}}};
                for(auto type=*reinterpret_cast<unsigned char**>(builder+0x24);type;type=*reinterpret_cast<unsigned char**>(type+0x28))
                    for(auto p=*reinterpret_cast<unsigned char**>(type+0x58);p;p=*reinterpret_cast<unsigned char**>(p+0x40)){
                        std::string name=ObjectName(p);if(name!="Rotation" && name!="PrePivot" && name!="DrawScale" && name!="DrawScale3D" && name!="MainScale" && name!="PostScale")continue;
                        auto at=builder+*reinterpret_cast<int*>(p+0x3c);auto size=*reinterpret_cast<unsigned short*>(p+0x32);value[name]=std::vector<unsigned char>(at,at+size);
                    }return value;
            };
            auto brushText=[&](){auto file=directory/"builder-fit.t3d";require(Exec("BRUSH EXPORT FILE=\""+file.string()+"\"")!=0,"export builder brush for verification");std::ifstream in(file);return std::string(std::istreambuf_iterator<char>(in),{});};
            auto before=brushState();auto beforeText=brushText();auto viewBefore=call({{"op","view.capture"}})["cameras"];
            using Load=HMENU(WINAPI*)(HINSTANCE,LPCSTR);
            auto menu=(*reinterpret_cast<Load*>(0x11af23f0))(GetModuleHandle(nullptr),MAKEINTRESOURCEA(107));
            require(menu && GetMenuState(GetSubMenu(menu,0),40933,MF_BYCOMMAND)!=UINT(-1),"static mesh context menu contains builder fit action");DestroyMenu(menu);
            auto currentMaterial=reinterpret_cast<uintptr_t*>(*reinterpret_cast<uintptr_t*>(kEditor)+0x138);
            auto oldMaterial=*currentMaterial;*currentMaterial=0;
            SendMessage(frameWindow,WM_COMMAND,40933,0);
            *currentMaterial=oldMaterial;
            auto after=brushState();auto afterText=brushText();
            require(afterText!=beforeText,"builder fit replaces brush geometry");
            size_t materialCount=0;for(size_t pos=0;(pos=afterText.find("Texture=Engine.DefaultTexture",pos))!=std::string::npos;++pos)++materialCount;
            require(materialCount==6,"builder fit assigns default texture to every face when no material is selected");
            require(call({{"op","actors"},{"selected",true}})[0]["path"]==mesh["path"] && call({{"op","view.capture"}})["cameras"]==viewBefore,"builder fit preserves mesh selection and viewport cameras");
            for(int axis=0;axis<3;++axis){const char* key=axis==0?"X":axis==1?"Y":"Z";require(std::abs(std::stod(after["Location"][key].get<std::string>())-(lo[axis]+hi[axis])*0.5)<0.01,"builder centered on mesh bounds");}
            std::istringstream lines(afterText);std::string line;int vertices=0;
            while(std::getline(lines,line)){auto pos=line.find("Vertex");if(pos==std::string::npos)continue;auto xyz=line.substr(pos+6);std::replace(xyz.begin(),xyz.end(),',',' ');std::istringstream values(xyz);double x,y,z;if(values>>x>>y>>z){double v[3]={x,y,z};for(int axis=0;axis<3;++axis)require(std::abs(std::abs(v[axis])-(hi[axis]-lo[axis])*0.5)<0.02,"builder vertices match fitted half extents");++vertices;}}
            require(vertices==24,"builder fit creates six quad faces");
            require(Exec("TRANSACTION UNDO")!=0,"undo builder fit");require(brushState()==before && brushText()==beforeText,"one undo restores builder geometry and transform");
            require(Exec("TRANSACTION REDO")!=0,"redo builder fit");require(brushState()==after && brushText()==afterText,"redo restores fitted brush");
            Exec("TRANSACTION UNDO");
            call({{"op","select"},{"actors",J::array()}});bool rejected=false;try{call({{"op","builder.fit"}});}catch(const std::exception&){rejected=true;}
            require(rejected && brushState()==before && brushText()==beforeText,"empty selection rejected without changing builder");
            auto mixed=originalSelection;mixed.push_back(mesh);call({{"op","select"},{"actors",mixed}});
            rejected=false;try{call({{"op","builder.fit"}});}catch(const std::exception&){rejected=true;}
            require(rejected && brushState()==before && brushText()==beforeText,"mixed mesh and brush selection rejected without edits");
            menu=(*reinterpret_cast<Load*>(0x11af23f0))(GetModuleHandle(nullptr),MAKEINTRESOURCEA(107));
            require(menu && GetMenuState(GetSubMenu(menu,0),40933,MF_BYCOMMAND)==UINT(-1),"builder fit omitted from non-mesh context menu");DestroyMenu(menu);
            auto second=call({{"op","magic.create"},{"class","Engine.StaticMeshActor"}});native=WorkflowProbe::MagicActor(second);
            *reinterpret_cast<void**>(field("StaticMesh"))=meshObject;vector("Location",-200,300,400);vector("PrePivot",0,0,0);vector("DrawScale3D",1,1,1);*reinterpret_cast<float*>(field("DrawScale"))=1;memset(field("Rotation"),0,12);
            call({{"op","select"},{"actors",J::array({mesh,second})}});auto combined=call({{"op","mesh.bounds"}});
            require(combined["count"]==2,"builder fit includes both selected meshes");
            const double offset[3]={-200,300,400};
            for(int axis=0;axis<3;++axis){lo[axis]=(std::min)(lo[axis],local["min"][axis].get<double>()+offset[axis]);hi[axis]=(std::max)(hi[axis],local["max"][axis].get<double>()+offset[axis]);require(std::abs(combined["min"][axis].get<double>()-lo[axis])<0.01 && std::abs(combined["max"][axis].get<double>()-hi[axis])<0.01,"multiple mesh bounds form their union");}
            uintptr_t selectedMaterial=0;
            for(int i=0;i<objectCount;++i)if(objects[i] && !strcmp(ObjectName(objects[i]),"BETON_Lit_Spec")){selectedMaterial=reinterpret_cast<uintptr_t>(objects[i]);break;}
            require(selectedMaterial!=0,"selected material fixture available");*currentMaterial=selectedMaterial;
            call({{"op","builder.fit"}});auto together=brushState();*currentMaterial=oldMaterial;
            auto textured=brushText();require(textured.find("Texture=TXT_INI.BSP.BETON_Lit_Spec")!=std::string::npos,"builder fit uses selected material when available");
            for(int axis=0;axis<3;++axis){const char* key=axis==0?"X":axis==1?"Y":"Z";require(std::abs(std::stod(together["Location"][key].get<std::string>())-(lo[axis]+hi[axis])*0.5)<0.01,"builder centers on all selected meshes");}
            require(call({{"op","actors"},{"selected",true}}).size()==2,"multi-mesh fit preserves both selected meshes");
            Exec("TRANSACTION UNDO");require(brushState()==before && brushText()==beforeText,"multi-mesh fit undoes in one step");Exec("TRANSACTION UNDO");
            // Remove this test actor and restore the original builder/selection before the remaining suite.
            Exec("TRANSACTION UNDO");call({{"op","select"},{"actors",originalSelection}});
        }
        J view=call({{"op","view.capture"}});require(!view.at("cameras").empty(),"capture native cameras");
        auto adjusted=view;
        for(auto& c:adjusted["cameras"]) {c["position"][0]=c["position"][0].get<double>()+32.0;}
        call({{"op","view.restore"},{"view",adjusted}});
        auto again=call({{"op","view.capture"}});require(again["cameras"]==adjusted["cameras"],"camera adjustment restores exactly");
        call({{"op","view.restore"},{"view",view}});
        auto map=call({{"op","map"}});Record("workflow_map",map.dump().c_str());
        std::string material="TXT_INI.BSP.BETON_Lit_Spec";
        auto usages=call({{"op","usages"},{"asset",material}});
        require(!usages.empty(),"find fixture BSP material usages");
        J replace=J::array();for(auto& usage:usages)if(usage.at("surface").get<int>()>=0 && usage.at("replaceable").get<bool>()){replace.push_back(usage);break;}
        require(replace.size()==1,"single BSP usage chosen for replacement");
        call({{"op","replace"},{"source",material},{"replacement","Engine.DefaultTexture"},{"usages",replace}});
        require(call({{"op","usages"},{"asset",material}}).size()+1==usages.size(),"replacement changes only checked surface");
        Exec("TRANSACTION UNDO");require(call({{"op","usages"},{"asset",material}}).size()==usages.size(),"replacement undo restores source material");
        Exec("TRANSACTION REDO");require(call({{"op","usages"},{"asset",material}}).size()+1==usages.size(),"replacement redo works");
        require(Rebuild(),"rebuild after material replacement");
        require(call({{"op","usages"},{"asset",material}}).size()+1==usages.size(),"replacement survives rebuild through source polygons");
        J linked={{"id","links"},{"name","Link fixture"},{"pivot",{0,0,0}},{"bindings",J::array()},{"dependencies",J::array({"Engine.Light"})},{"actors",J::array()}};
        for(int i=0;i<3;++i)
        {
            std::string name="Relay"+std::to_string(i),tag=i==0?"Start":"End",event=i==0?"End":"Start";
            linked["actors"].push_back({{"name",name},{"path","Assembly."+name},{"class","Engine.Light"},{"text","Begin Actor Class=Light Name="+name+"\nTag="+tag+"\nEvent="+event+"\nEnd Actor\n"},{"tag",tag},{"event",event},{"position",{i*64,0,0}},{"rotation",{0,0,0}}});
        }
        auto linkInstance=call({{"op","assembly.place"},{"definition",linked},{"position",{0,0,0}},{"rotation",{0,0,0}},{"bindings",J::object()}});
        auto edges=call({{"op","connections"},{"actor",linkInstance["members"][0]["path"]}});
        require(edges.size()==4,"connection inspector resolves cyclic and one-to-many Event/Tag links");
        auto allLinked=call({{"op","actors"}});std::string tag0;
        for(const auto& a:allLinked)if(a["path"]==linkInstance["members"][0]["path"])tag0=a["tag"];
        auto secondLinks=call({{"op","assembly.place"},{"definition",linked},{"position",{0,256,0}},{"rotation",{0,0,0}},{"bindings",J::object()}});
        auto secondEdges=call({{"op","connections"},{"actor",secondLinks["members"][0]["path"]}});
        require(secondEdges.size()==4 && call({{"op","connections"},{"actor",linkInstance["members"][0]["path"]}}).size()==4,"repeated assembly insertion isolates internal tag groups");
        std::string endTag;for(const auto& a:allLinked)if(a["path"]==linkInstance["members"][1]["path"])endTag=a["tag"];
        J magic={{"id","magic-fixture"},{"name","Magic event fixture"},{"pivot",{0,0,0}},{"bindings",J::array()},{"dependencies",J::array({"SBase.SMagicEvent"})},{"actors",J::array()}};
        std::string magicText="Begin Actor Class=SMagicEvent Name=MagicFixture\nGroups(0)=(EventGroup=((Event="+tag0+"),(Event="+endTag+"),(Event=None)))\nGroups(1)=(EventGroup=((Event=WorkflowMissingTarget)))\nEnd Actor\n";
        magic["actors"].push_back({{"name","MagicFixture"},{"path","Assembly.MagicFixture"},{"class","SBase.SMagicEvent"},{"text",magicText},{"tag","MagicFixture"},{"event","None"},{"position",{0,0,0}},{"rotation",{0,0,0}}});
        auto magicInstance=call({{"op","assembly.place"},{"definition",magic},{"position",{0,512,0}},{"rotation",{0,0,0}},{"bindings",J::object()}});
        auto magicSnapshot=call({{"op","magic.inspect"},{"actor",magicInstance["members"][0]}});
        std::ofstream(directory/"magic_workbench_schema.json")<<magicSnapshot.dump(2);
        {
            auto document=call({{"op","magic.json.export"},{"actor",magicSnapshot["actor"]}});
            std::ofstream(directory/"magic_event_export.json")<<document.dump(2);
            require(document["properties"].contains("Groups") && !document["properties"].contains("Tag") && !document["properties"].contains("Location") && !document["context"]["actors"].empty(),"event JSON includes settings and actor context while preserving destination identity");
            auto edited=document;edited["properties"]["Groups"][0]["EventGroup"][0]["Delay"]="2.25";edited["properties"]["Groups"][0]["Repeat"]="3";
            call({{"op","magic.json.import"},{"snapshot",magicSnapshot},{"document",edited}});
            auto imported=call({{"op","magic.inspect"},{"actor",magicSnapshot["actor"]}});
            require(std::stod(imported["values"]["Groups"][0]["EventGroup"][0]["Delay"].get<std::string>())==2.25 && imported["values"]["Groups"][0]["Repeat"]=="3","JSON import applies group timing and repeat settings together");
            require(imported["values"]["Tag"]==magicSnapshot["values"]["Tag"] && imported["values"]["Location"]==magicSnapshot["values"]["Location"],"JSON import preserves Tag and transform");
            bool rejected=false;try{call({{"op","magic.json.import"},{"snapshot",magicSnapshot},{"document",document}});}catch(const std::exception&){rejected=true;}
            require(rejected,"JSON import rejects a stale event snapshot");
            Exec("TRANSACTION UNDO");require(call({{"op","magic.inspect"},{"actor",magicSnapshot["actor"]}})["values"]==magicSnapshot["values"],"JSON import is one undo step");
            Exec("TRANSACTION REDO");require(call({{"op","magic.inspect"},{"actor",magicSnapshot["actor"]}})["values"]==imported["values"],"JSON import redo restores all changes");Exec("TRANSACTION UNDO");
            auto invalid=edited;invalid["properties"]["Groups"][0]["EventGroup"][1]["Delay"]="-2";rejected=false;
            try{call({{"op","magic.json.import"},{"snapshot",magicSnapshot},{"document",invalid}});}catch(const std::exception&){rejected=true;}
            require(rejected && call({{"op","magic.inspect"},{"actor",magicSnapshot["actor"]}})["values"]==magicSnapshot["values"],"invalid later action leaves the whole event unchanged");
            invalid=edited;invalid["properties"]["StopActor"]="Actor'MyLevel.MissingJsonTarget'";rejected=false;
            try{call({{"op","magic.json.import"},{"snapshot",magicSnapshot},{"document",invalid}});}catch(const std::exception&){rejected=true;}
            require(rejected && call({{"op","magic.inspect"},{"actor",magicSnapshot["actor"]}})["values"]==magicSnapshot["values"],"missing JSON object reference is rejected before edits");
        }

        {
            auto selectionBefore=call({{"op","actors"},{"selected",true}});
            auto exported=call({{"op","authoring.export"}});
            require(exported["actors"].size()>0 && exported["classes"].size()>0 && exported.contains("geometryT3d"),"map authoring exports actor properties, schemas, assets and world geometry");
            require(call({{"op","actors"},{"selected",true}})==selectionBefore,"map export preserves selection");
            std::ofstream(directory/"map-authoring.json")<<exported.dump(2);
            auto doc=exported["changes"];
            auto location=J{{"X","128"},{"Y","96"},{"Z","64"}};
            auto create=[&](const char* id,const char* type){return J{{"op","create"},{"id",id},{"class",type},{"properties",{{"Location",location}}}};};
            doc["operations"]=J::array({create("AuthoringEvent","SBase.SMagicEvent"),create("AuthoringLight","Engine.Light"),create("AuthoringSound","SBase.SAmbientSoundTrigger"),create("AuthoringSteam","SBase.SSpawnableEmitter"),create("AuthoringTrigger","Engine.Trigger"),
                {{"op","component"},{"id","SteamParticles"},{"owner","AuthoringSteam"},{"class","Engine.SpriteEmitter"},{"properties",J::object()}},
                {{"op","link"},{"event","AuthoringEvent"},{"target","AuthoringTrigger"},{"trigger",true}},
                {{"op","link"},{"event","AuthoringEvent"},{"target","AuthoringSound"},{"trigger",false}},
                {{"op","link"},{"event","AuthoringEvent"},{"target","AuthoringSteam"},{"trigger",false}}});
            doc["operations"][0]["properties"]["StopActor"]={{"$ref","AuthoringLight"}};
            doc["operations"].back()["delay"]="1.5";
            std::ofstream(directory/"map-changes.json")<<doc.dump(2);
            const auto beforeActors=call({{"op","actors"}});
            auto preview=call({{"op","authoring.preview"},{"document",doc}});
            require(!preview["changes"].empty() && call({{"op","actors"}})==beforeActors,"preview lists map changes without mutating actors");
            auto invalid=doc;invalid["operations"][0]["properties"]["StopActor"]={{"$ref","Missing"}};
            bool rejected=false;try{call({{"op","authoring.apply"},{"document",invalid}});}catch(...){rejected=true;}
            require(rejected && call({{"op","actors"}})==beforeActors,"missing batch references fail without partial creation");
            invalid=doc;invalid["operations"].push_back({{"op","link"},{"event","AuthoringEvent"},{"target","AuthoringSound"},{"trigger",false},{"group",999}});
            rejected=false;try{call({{"op","authoring.apply"},{"document",invalid}});}catch(...){rejected=true;}
            require(rejected && call({{"op","actors"}})==beforeActors,"late batch failure rolls back all created actors and links");
            auto applied=call({{"op","authoring.apply"},{"document",doc}});
            require(applied["created"].size()==6,"batch creates lights, sound, trigger, event, emitter and particle component");
            J states=J::array();for(const auto& identity:applied["created"])states.push_back(call({{"op","magic.inspect"},{"actor",identity}}));
            require(states[0]["values"]["StopActor"].get<std::string>().find("AuthoringLight")!=std::string::npos,"forward actor references resolve after batch creation");
            require(states[4]["values"]["Event"]==states[0]["values"]["Tag"] && states[0]["values"]["Groups"][0]["EventGroup"].size()==2,"batch connects trigger and timed-event targets");
            require(std::stod(states[0]["values"]["Groups"][0]["EventGroup"][1]["Delay"].get<std::string>())==1.5,"batch preserves an authored action delay");
            require(std::stod(states[1]["values"]["Location"]["X"].get<std::string>())==128 && std::stod(states[1]["values"]["Location"]["Y"].get<std::string>())==96 && std::stod(states[1]["values"]["Location"]["Z"].get<std::string>())==64,"batch places actors at exact absolute coordinates");
            require(states[3]["values"]["Emitters"].size()==1,"batch attaches an owned particle component");
            require(call({{"op","actors"},{"selected",true}})==selectionBefore,"batch preserves selection");
            Exec("TRANSACTION UNDO");require(call({{"op","actors"}})==beforeActors,"one undo removes the entire set piece");
            Exec("TRANSACTION REDO");
            for(const auto& state:states)require(call({{"op","magic.inspect"},{"actor",state["actor"]}})["values"]==state["values"],"one redo restores set piece properties and connections");
            auto existingLink=exported["changes"];existingLink["operations"]=J::array({{{"op","link"},{"event",states[0]["actor"]["path"]},{"target",states[2]["actor"]["path"]},{"trigger",false}}});
            rejected=false;try{call({{"op","authoring.preview"},{"document",existingLink}});}catch(...){rejected=true;}
            require(rejected,"linking existing actors requires exported before values");
            existingLink["expect"]={{states[0]["actor"]["path"].get<std::string>(),states[0]["values"]},{states[2]["actor"]["path"].get<std::string>(),states[2]["values"]}};
            call({{"op","authoring.apply"},{"document",existingLink}});
            rejected=false;try{call({{"op","authoring.apply"},{"document",existingLink}});}catch(...){rejected=true;}
            require(rejected,"changed event wiring rejects stale link expectations");
            Exec("TRANSACTION UNDO");
            auto update=exported["changes"];update["operations"]=J::array({{{"op","update"},{"actor",states[1]["actor"]},{"before",states[1]["values"]},{"properties",{{"LightBrightness","123"}}}}});
            call({{"op","authoring.apply"},{"document",update}});
            rejected=false;try{call({{"op","authoring.apply"},{"document",update}});}catch(...){rejected=true;}
            require(rejected,"map authoring rejects stale existing-actor edits");
            Exec("TRANSACTION UNDO");require(call({{"op","magic.inspect"},{"actor",states[1]["actor"]}})["values"]==states[1]["values"],"batch update undo restores old light settings");
            Exec("TRANSACTION UNDO");
            require(call({{"op","actors"}})==beforeActors,"authoring fixture leaves pre-existing actors unchanged");
            auto uiFile=directory/"map-authoring-ui.json";WorkflowProbe::jsonDialogPath=uiFile.wstring();WorkflowProbe::jsonDialogCancel=false;WorkflowProbe::jsonDialogTicks=0;
            auto timer=SetTimer(nullptr,0,100,WorkflowProbe::AnswerJsonDialog);SendMessage(frameWindow,WM_COMMAND,40934,0);KillTimer(nullptr,timer);
            require(std::filesystem::exists(uiFile),"map authoring export menu saves through the native file dialog");
            WorkflowProbe::jsonDialogPath=(directory/"map-changes.json").wstring();WorkflowProbe::jsonDialogTicks=0;WorkflowProbe::authoringPreviewCancel=true;WorkflowProbe::authoringPreviewSeen=false;
            WorkflowProbe::previewScreenshot=directory/"map_authoring_preview.bmp";
            timer=SetTimer(nullptr,0,100,WorkflowProbe::AnswerJsonDialog);SendMessage(frameWindow,WM_COMMAND,40935,0);KillTimer(nullptr,timer);
            require(WorkflowProbe::authoringPreviewSeen && call({{"op","actors"}})==beforeActors,"cancelled map preview leaves actors unchanged");
            WorkflowProbe::previewScreenshot.clear();WorkflowProbe::jsonDialogTicks=0;WorkflowProbe::authoringPreviewCancel=false;WorkflowProbe::authoringPreviewSeen=false;
            timer=SetTimer(nullptr,0,100,WorkflowProbe::AnswerJsonDialog);SendMessage(frameWindow,WM_COMMAND,40935,0);KillTimer(nullptr,timer);
            require(WorkflowProbe::authoringPreviewSeen && call({{"op","actors"}}).size()==beforeActors.size()+5,"map import menu applies the reviewed set piece");
            Exec("TRANSACTION UNDO");require(call({{"op","actors"}})==beforeActors,"UI batch import has one-step Undo");
        }
        std::ofstream(directory/"magic_workbench_classes.json")<<call({{"op","magic.classes"}}).dump(2);
        for(const auto* type:{"Engine.Trigger","Engine.Mover","SBase.SAmbientSoundTrigger","SBase.SSpawnableEmitter","SBase.SDamageVolume"})
        {
            auto created=call({{"op","magic.create"},{"class",type},{"geometry",std::string(type)=="SBase.SDamageVolume"?"box":"point"}});
            auto snapshot=call({{"op","magic.inspect"},{"actor",created}});
            std::ofstream(directory/(std::string(type)+".json"))<<snapshot.dump(2);
            if(std::string(type)=="Engine.Mover")
            {
                require(snapshot["values"].contains("KeyPos") && snapshot["values"].contains("Location"),"workbench exposes mover key arrays and actor transforms");
                bool invalidKey=false;try{call({{"op","magic.edit"},{"snapshot",snapshot},{"changes",{{"KeyNum","255"}}}});}catch(...){invalidKey=true;}require(invalidKey,"out-of-range mover key is rejected before native editing");
                call({{"op","magic.key"},{"snapshot",snapshot},{"key",1}});Exec("TRANSACTION UNDO");
                require(call({{"op","magic.inspect"},{"actor",created}})["values"]==snapshot["values"],"mover key capture undo restores full pose");
            }
            if(std::string(type)=="SBase.SSpawnableEmitter")
            {
                auto component=call({{"op","magic.component"},{"snapshot",snapshot},{"class","Engine.SpriteEmitter"}});
                auto componentSnapshot=call({{"op","magic.inspect"},{"actor",component}});
                require(!componentSnapshot["values"].empty(),"particle component exposes editable reflected options");
                std::ofstream(directory/"magic_particle_component.json")<<componentSnapshot.dump(2);
                Exec("TRANSACTION UNDO");require(call({{"op","magic.inspect"},{"actor",created}})["values"]==snapshot["values"],"particle component insertion uses native undo");
            }
            Exec("TRANSACTION UNDO");
        }
        auto builderVolume=call({{"op","magic.create"},{"class","SBase.SDamageVolume"},{"geometry","builder"}});
        require(!builderVolume.empty(),"workbench creates a damage volume from builder polygons");Exec("TRANSACTION UNDO");
        auto editedGroups=magicSnapshot["values"]["Groups"];editedGroups[0]["EventGroup"][0]["Delay"]="1.25";
        call({{"op","magic.edit"},{"snapshot",magicSnapshot},{"changes",{{"Groups",editedGroups}}}});
        require(call({{"op","magic.inspect"},{"actor",magicInstance["members"][0]}})["values"]["Groups"][0]["EventGroup"][0]["Delay"]=="1.250000","workbench edits nested native delay");
        Exec("TRANSACTION UNDO");
        require(call({{"op","magic.inspect"},{"actor",magicInstance["members"][0]}})["values"]==magicSnapshot["values"],"workbench nested edit uses one native undo");
        auto runtime=call({{"op","magic.create"},{"class","SBase.SMagicEvent"}});
        auto runtimeSnapshot=call({{"op","magic.inspect"},{"actor",runtime}});
        auto runtimeGroups=magicSnapshot["values"]["Groups"];runtimeGroups.erase(runtimeGroups.begin()+1);runtimeGroups[0]["EventGroup"].erase(runtimeGroups[0]["EventGroup"].begin()+2);
        runtimeGroups[0]["EventGroup"][0]["Event"]="None";runtimeGroups[0]["EventGroup"][1]["Event"]="None";
        runtimeGroups[0]["EventGroup"][0]["Delay"]="5";runtimeGroups[0]["EventGroup"][1]["Delay"]="2";runtimeGroups[0]["Repeat"]="2";
        call({{"op","magic.edit"},{"snapshot",runtimeSnapshot},{"changes",{{"Groups",runtimeGroups}}}});
        auto nativeState=WorkflowProbe::ActivateMagic(runtime,linkInstance["members"][0],true);
        std::ofstream(directory/"magic_activation.json")<<nativeState.dump(2);
        require(nativeState["queue"].size()==2 && nativeState["groups"][0]["repeat"]==1,"native activation schedules parallel actions and consumes one repeat");
        double dueDifference=std::abs(nativeState["queue"][0]["due"].get<double>()-nativeState["queue"][1]["due"].get<double>());
        require(std::abs(dueDifference-3.0)<0.01,"native delays share activation time rather than accumulating");
        Exec("TRANSACTION UNDO");
        runtimeSnapshot=call({{"op","magic.inspect"},{"actor",runtime}});runtimeGroups[0]["Sequence"]="True";runtimeGroups[0]["Repeat"]="1";
        call({{"op","magic.edit"},{"snapshot",runtimeSnapshot},{"changes",{{"Groups",runtimeGroups}}}});
        auto ignoredActivation=WorkflowProbe::ActivateMagic(runtime,linkInstance["members"][0],false);
        require(ignoredActivation["queue"].empty() && ignoredActivation["groups"][0]["index"]==0,"ValidOn rejects untrigger without advancing sequence");
        auto firstActivation=WorkflowProbe::ActivateMagic(runtime,linkInstance["members"][0],true);
        require(firstActivation["queue"].size()==1 && firstActivation["groups"][0]["index"]==1 && firstActivation["groups"][0]["repeat"]==1,"sequence schedules one action per activation");
        auto secondActivation=WorkflowProbe::ActivateMagic(runtime,linkInstance["members"][0],true);
        require(secondActivation["queue"].size()==2 && secondActivation["groups"][0]["index"]==0 && secondActivation["groups"][0]["repeat"]==-1,"sequence consumes repeat after completing its action cycle");
        auto exhausted=WorkflowProbe::ActivateMagic(runtime,linkInstance["members"][0],true);require(exhausted==secondActivation,"exhausted event group schedules no further actions");
        Exec("TRANSACTION UNDO");Exec("TRANSACTION UNDO");
        call({{"op","select"},{"actors",J::array({magicInstance["members"][0]})}});
        SendMessage(frameWindow,WM_COMMAND,40927,0);auto workbench=WorkflowProbe::FindDialog("SMagicEvent Workbench");require(workbench!=nullptr,"SMagicEvent workbench opens for an existing actor");
        {
            require(IsWindowEnabled(GetDlgItem(workbench,144)) && IsWindowEnabled(GetDlgItem(workbench,145)),"JSON buttons enabled for open event");
            auto path=directory/L"event-ui-roundtrip.json";WorkflowProbe::jsonDialogPath=path.wstring();WorkflowProbe::jsonDialogCancel=false;WorkflowProbe::jsonDialogTicks=0;
            auto timer=SetTimer(nullptr,0,100,WorkflowProbe::AnswerJsonDialog);SendMessage(workbench,WM_COMMAND,144,0);KillTimer(nullptr,timer);
            require(std::filesystem::exists(path),"Export JSON button saves a file through the native dialog");
            J document;{std::ifstream input(path);input>>document;}require(document["format"]=="scct.smagic-event","UI exports versioned event JSON");
            document["properties"]["Groups"][0]["EventGroup"][0]["Delay"]="3.5";{std::ofstream output(path);output<<document.dump(2);}
            WorkflowProbe::jsonDialogTicks=0;timer=SetTimer(nullptr,0,100,WorkflowProbe::AnswerJsonDialog);SendMessage(workbench,WM_COMMAND,145,0);KillTimer(nullptr,timer);
            require(std::stod(call({{"op","magic.inspect"},{"actor",magicSnapshot["actor"]}})["values"]["Groups"][0]["EventGroup"][0]["Delay"].get<std::string>())==3.5,"Import JSON button applies edited file and refreshes event");
            WorkflowProbe::Click(workbench,104);
            require(call({{"op","magic.inspect"},{"actor",magicSnapshot["actor"]}})["values"]==magicSnapshot["values"],"Workbench Undo restores JSON import");
            WorkflowProbe::jsonDialogCancel=true;WorkflowProbe::jsonDialogTicks=0;timer=SetTimer(nullptr,0,100,WorkflowProbe::AnswerJsonDialog);SendMessage(workbench,WM_COMMAND,145,0);KillTimer(nullptr,timer);
            require(call({{"op","magic.inspect"},{"actor",magicSnapshot["actor"]}})["values"]==magicSnapshot["values"],"cancelled JSON import leaves event untouched");WorkflowProbe::jsonDialogCancel=false;
        }
        WorkflowProbe::Click(workbench,116);require(call({{"op","magic.inspect"},{"actor",magicInstance["members"][0]}})["values"]["Groups"].size()==3,"workbench Add Group changes native groups");WorkflowProbe::Click(workbench,104);
        SendMessage(GetDlgItem(workbench,115),CB_SETCURSEL,0,0);SendMessage(workbench,WM_COMMAND,MAKEWPARAM(115,CBN_SELCHANGE),0);
        SendMessage(GetDlgItem(workbench,121),WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(175,72));SendMessage(GetDlgItem(workbench,121),WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(305,72));SendMessage(GetDlgItem(workbench,121),WM_LBUTTONUP,0,MAKELPARAM(305,72));
        require(std::stod(call({{"op","magic.inspect"},{"actor",magicInstance["members"][0]}})["values"]["Groups"][0]["EventGroup"][0]["Delay"].get<std::string>())==2.0,"timeline drag commits snapped native delay");WorkflowProbe::Click(workbench,104);
        auto tree=GetDlgItem(workbench,132);
        auto findDelay=[&](auto&& self,HTREEITEM node)->HTREEITEM {for(auto item=node;item;item=TreeView_GetNextSibling(tree,item)){char label[256]{};TVITEMA value{};value.mask=TVIF_TEXT;value.hItem=item;value.pszText=label;value.cchTextMax=256;SendMessageA(tree,TVM_GETITEMA,0,reinterpret_cast<LPARAM>(&value));if(!strncmp(label,"Delay ",6))return item;if(auto child=self(self,TreeView_GetChild(tree,item)))return child;}return nullptr;};
        auto delayRow=findDelay(findDelay,TreeView_GetRoot(tree));require(delayRow!=nullptr,"selected action exposes a precise delay inspector");TreeView_SelectItem(tree,delayRow);SetWindowTextA(GetDlgItem(workbench,133),"1.75");SendMessage(GetDlgItem(workbench,133),WM_KEYDOWN,VK_RETURN,0);
        require(std::stod(call({{"op","magic.inspect"},{"actor",magicInstance["members"][0]}})["values"]["Groups"][0]["EventGroup"][0]["Delay"].get<std::string>())==1.75,"Enter commits a precise inspector field");WorkflowProbe::Click(workbench,104);
        WorkflowProbe::Screenshot(workbench,directory/"magic_workbench.bmp");
        RECT originalWindow{};GetWindowRect(workbench,&originalWindow);RECT scaled{originalWindow.left,originalWindow.top,originalWindow.left+2070,originalWindow.top+1230};SendMessage(workbench,WM_DPICHANGED,MAKEWPARAM(144,144),reinterpret_cast<LPARAM>(&scaled));WorkflowProbe::Screenshot(workbench,directory/"magic_workbench_144dpi.bmp");SendMessage(workbench,WM_DPICHANGED,MAKEWPARAM(96,96),reinterpret_cast<LPARAM>(&originalWindow));
        SetFocus(GetDlgItem(workbench,107));SendMessage(GetDlgItem(workbench,107),WM_KEYDOWN,VK_TAB,0);require(GetFocus()==GetDlgItem(workbench,108),"workbench keyboard Tab moves between controls");SendMessage(workbench,WM_CLOSE,0,0);
        auto magicEdges=call({{"op","connections"},{"actor",magicInstance["members"][0]["path"]}});
        std::ofstream(directory/"workflow_magic_edges.json")<<magicEdges.dump(2);
        require(magicEdges.size()==4,"nested SMagicEvent groups resolve all matching tags and omit None");
        int missing=0,shared=0;for(const auto& e:magicEdges){if(!e.at("resolved").get<bool>())++missing;if(e.at("property")=="Groups[0].EventGroup[1].Event")++shared;}
        require(missing==1 && shared==2,"SMagicEvent preserves one-to-many targets and reports unmatched nested event");
        auto incomingMagic=call({{"op","connections"},{"actor",linkInstance["members"][0]["path"]}});require(incomingMagic.size()==5,"SMagicEvent group link appears as incoming on target actor");
        auto sharedPreview=call({{"op","tag.preview"},{"actor",linkInstance["members"][1]},{"tag","WorkflowSharedRenamed"}});
        size_t renamedTargets=0;for(const auto& change:sharedPreview["changes"])if(change["property"]=="Tag")++renamedTargets;
        require(renamedTargets==2 && sharedPreview["changes"].size()==4,"shared tag preview includes both target actors and both dependent event fields");
        call({{"op","tag.rename"},{"preview",sharedPreview}});
        auto currentTag=[&](const J& id){for(const auto& a:call({{"op","actors"}}))if(a["path"]==id["path"])return a["tag"].get<std::string>();throw std::runtime_error("Tag test actor missing");};
        require(currentTag(linkInstance["members"][1])=="WorkflowSharedRenamed" && currentTag(linkInstance["members"][2])=="WorkflowSharedRenamed","shared tag rename updates every matching actor");
        require(call({{"op","connections"},{"actor",magicInstance["members"][0]["path"]}})==magicEdges,"rename preserves nested EventGroup links and unmatched targets");
        require(call({{"op","connections"},{"actor",linkInstance["members"][0]["path"]}})==incomingMagic,"rename preserves ordinary Event one-to-many links");
        Exec("TRANSACTION UNDO");require(currentTag(linkInstance["members"][1])==endTag && currentTag(linkInstance["members"][2])==endTag,"single undo restores shared actor Tags");
        require(call({{"op","connections"},{"actor",magicInstance["members"][0]["path"]}})==magicEdges,"undo restores nested EventGroup name assignments");
        Exec("TRANSACTION REDO");require(currentTag(linkInstance["members"][1])=="WorkflowSharedRenamed","single redo reapplies group rename");
        require(call({{"op","connections"},{"actor",magicInstance["members"][0]["path"]}})==magicEdges,"redo preserves nested links");
        bool collision=false;try{call({{"op","tag.preview"},{"actor",linkInstance["members"][1]},{"tag",tag0}});}catch(const std::exception&){collision=true;}require(collision,"rename rejects a Tag already used by another group");
        bool invalid=false;try{call({{"op","tag.preview"},{"actor",linkInstance["members"][1]},{"tag","None"}});}catch(const std::exception&){invalid=true;}require(invalid,"rename rejects None without disconnecting targets");
        bool stale=false;try{call({{"op","tag.rename"},{"preview",sharedPreview}});}catch(const std::exception&){stale=true;}require(stale,"stale rename previews are rejected without mutation");
        auto partialPreview=call({{"op","tag.preview"},{"actor",linkInstance["members"][1]},{"tag","WorkflowPartial"}});
        partialPreview["excluded"]=J::array();
        for(size_t i=0;i<partialPreview["changes"].size();++i)
            if(partialPreview["changes"][i]["actor"]["path"]!=linkInstance["members"][1]["path"] || partialPreview["changes"][i]["property"]!="Tag")partialPreview["excluded"].push_back(i);
        call({{"op","tag.rename"},{"preview",partialPreview}});
        require(currentTag(linkInstance["members"][1])=="WorkflowPartial" && currentTag(linkInstance["members"][2])=="WorkflowSharedRenamed","partial rename leaves excluded actor unchanged");
        auto remaining=call({{"op","connections"},{"actor",magicInstance["members"][0]["path"]}});
        require(remaining.size()==magicEdges.size()-1,"excluded nested event still targets the unrenamed group member");
        Exec("TRANSACTION UNDO");require(currentTag(linkInstance["members"][1])=="WorkflowSharedRenamed" && call({{"op","connections"},{"actor",magicInstance["members"][0]["path"]}})==magicEdges,"undo restores partial rename and connections");
        std::ofstream(directory/"workflow_actors.json")<<actors.dump(2);
        J members=J::array();for(const auto& a:actors) if(a.at("class")=="Engine.Brush" && a.at("path")!=actors.at(1).at("path")) members.push_back(a);
        require(!members.empty(),"authored brush selected for assembly");
        // Deliberately select only the non-builder brush using native identities.
        members=J::array({members.back()});call({{"op","select"},{"actors",members}});
        J definition=call({{"op","assembly.capture"},{"members",members},{"position",{0,0,0}},{"rotation",{0,0,0}}});
        definition["id"]="native-test";definition["name"]="Native fixture brush";
        std::ofstream(directory/"workflow_definition.json")<<definition.dump(2);
        require(definition.at("actors").size()==1,"brush export captured");
        J bindings=J::object();for(auto& b:definition.at("bindings")) bindings[b.at("id").get<std::string>()]="";
        auto count=call({{"op","actors"}}).size();
        auto instance=call({{"op","assembly.place"},{"definition",definition},{"position",{1200,0,0}},{"rotation",{0,16384,0}},{"bindings",bindings}});
        require(call({{"op","actors"}}).size()==count+1,"assembly creates exactly one actor");
        Exec("TRANSACTION UNDO");require(call({{"op","actors"}}).size()==count,"assembly insertion is undoable");
        Exec("TRANSACTION REDO");require(call({{"op","actors"}}).size()==count+1,"assembly insertion supports redo");
        auto recaptured=call({{"op","assembly.capture"},{"members",instance.at("members")},{"position",{1200,0,0}},{"rotation",{0,16384,0}}});
        require(recaptured.at("actors").size()==1,"placed assembly can be edited again");
        require(Rebuild(),"ordinary geometry rebuild after insertion");
        checkSurfaceBrushSelection(2);
        using Save=int(__thiscall*)(void*,const char*);
        require(reinterpret_cast<Save>(0x10E0416B)(*reinterpret_cast<void**>(kEditor),destination)!=0,"source map save after assembly insertion");
        require(Exec(std::string("MAP LOAD FILE=\"")+destination+"\"")!=0,"ordinary reopen after assembly insertion");
        auto reopened=call({{"op","actors"}});std::ofstream(directory/"workflow_reopened.json")<<reopened.dump(2);
        for(const auto& member:instance.at("members"))
        {
            auto path=member.at("path").get<std::string>();auto name=path.substr(path.find_last_of('.')+1);bool found=false;
            for(const auto& a:reopened) if(a.at("name")==name && a.at("class")==member.at("class"))found=true;
            require(found,"assembly actor survives reopening");
        }
        using SetFilename=void(__thiscall*)(void*,const char*);
        reinterpret_cast<SetFilename>(0x10e05e1c)(*reinterpret_cast<void**>(0x1165e80c),destination);
        SendMessage(frameWindow,WM_COMMAND,40921,0);
        HWND views=WorkflowProbe::FindDialog("Working Views");require(views!=nullptr,"working views dialog opens");
        WorkflowProbe::Click(views,202,"Native view");
        auto library=directory/"ReloadedEditor"/"library.json";
        auto readLibrary=[&](){std::ifstream input(library);J value;input>>value;return value;};
        auto saved=readLibrary();auto key=call({{"op","map"}}).at("key").get<std::string>();
        require(saved["maps"][key]["views"].size()==1,"view saved once");
        auto id=saved["maps"][key]["views"][0]["id"];
        auto edited=call({{"op","view.capture"}});edited["cameras"][0]["position"][0]=777.0;call({{"op","view.restore"},{"view",edited}});
        WorkflowProbe::Click(views,203);
        auto updated=readLibrary();require(updated["maps"][key]["views"].size()==1 && updated["maps"][key]["views"][0]["id"]==id,"view update retains saved identity without duplication");
        require(updated["maps"][key]["views"][0]["cameras"][0]["position"][0]==777.0,"view update stores adjusted camera");
        WorkflowProbe::Screenshot(views,directory/"workflow_views.bmp");
        call({{"op","select"},{"actors",instance.at("members")}});
        SendMessage(frameWindow,WM_COMMAND,40922,0);
        HWND assemblies=WorkflowProbe::FindDialog("Actor Assemblies");require(assemblies!=nullptr,"assembly browser opens");
        WorkflowProbe::Click(assemblies,202,"Native assembly");
        auto firstLibrary=readLibrary();require(firstLibrary["assemblies"].size()==1,"assembly definition saved");auto assemblyId=firstLibrary["assemblies"][0]["id"];
        WorkflowProbe::Click(assemblies,201); // place a tracked instance at the default pose
        WorkflowProbe::Click(assemblies,203); // edit that instance
        Exec("ACTOR ADD CLASS=Engine.Light");auto lights=call({{"op","actors"},{"selected",true}});require(!lights.empty(),"extra light created for membership editing");
        WorkflowProbe::Click(assemblies,207);WorkflowProbe::Click(assemblies,210);
        auto withLight=readLibrary();require(withLight["assemblies"].size()==1 && withLight["assemblies"][0]["id"]==assemblyId && withLight["assemblies"][0]["actors"].size()==2,"add member updates existing assembly in place");
        WorkflowProbe::Click(assemblies,203);call({{"op","select"},{"actors",lights}});WorkflowProbe::Click(assemblies,208);WorkflowProbe::Click(assemblies,210);
        auto withoutLight=readLibrary();require(withoutLight["assemblies"][0]["actors"].size()==1 && withoutLight["assemblies"][0]["id"]==assemblyId,"remove member updates same assembly definition");
        bool lightStillExists=false;for(const auto& actor:call({{"op","actors"}}))if(actor.at("path")==lights[0]["path"])lightStillExists=true;
        require(lightStillExists,"removing membership does not delete scene actor");
        auto selection=withoutLight["maps"][key]["instances"][0]["members"];selection.push_back(lights[0]);
        call({{"op","select"},{"actors",selection}});WorkflowProbe::Click(assemblies,206);
        auto selectionUpdate=readLibrary();
        require(selectionUpdate["assemblies"].size()==1 && selectionUpdate["assemblies"][0]["id"]==assemblyId && selectionUpdate["assemblies"][0]["actors"].size()==2,"selection shortcut updates same assembly entry");
        require(selectionUpdate["assemblies"][0]["pivot"]==firstLibrary["assemblies"][0]["pivot"],"membership updates preserve saved pivot");
        auto beforeFuture=call({{"op","actors"}}).size();WorkflowProbe::Click(assemblies,201);
        require(call({{"op","actors"}}).size()==beforeFuture+2,"future placement uses updated membership without changing existing copies");
        WorkflowProbe::Screenshot(assemblies,directory/"workflow_assemblies.bmp");
        SendMessage(frameWindow,WM_COMMAND,40920,0);auto connections=WorkflowProbe::FindDialog("Gameplay Connections");require(connections!=nullptr,"gameplay connection inspector opens");
        WorkflowProbe::Screenshot(connections,directory/"workflow_connections.bmp");
        WorkflowProbe::Click(connections,203);auto graph=WorkflowProbe::FindDialog("Gameplay Connection Graph");require(graph!=nullptr,"connection graph opens from list");
        SendMessage(GetDlgItem(graph,100),CB_SETCURSEL,1,0);SendMessage(graph,WM_COMMAND,MAKEWPARAM(100,CBN_SELCHANGE),0);
        auto allEdges=call({{"op","connections"},{"actor",""}});require(allEdges.size()>=8,"whole-level snapshot includes both independent connection islands");
        WorkflowProbe::Screenshot(graph,directory/"workflow_graph_level.bmp");
        auto graphTarget=linkInstance["members"][0]["path"].get<std::string>();SetWindowTextA(GetDlgItem(graph,105),graphTarget.c_str());WorkflowProbe::Click(graph,106);WorkflowProbe::Click(graph,111);
        auto graphSelection=call({{"op","actors"},{"selected",true}});require(graphSelection.size()==1 && graphSelection[0]["path"]==graphTarget,"graph search and focus select native counterpart actor");
        call({{"op","select"},{"actors",J::array()}});RECT graphCanvas{};GetClientRect(GetDlgItem(graph,120),&graphCanvas);
        SendMessage(GetDlgItem(graph,120),WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(graphCanvas.right/2,graphCanvas.bottom/2));
        auto doubleClickSelection=call({{"op","actors"},{"selected",true}});require(doubleClickSelection.size()==1 && doubleClickSelection[0]["path"]==graphTarget,"double-clicking graph node selects and focuses counterpart");
        auto renameTarget=linkInstance["members"][1]["path"].get<std::string>();SetWindowTextA(GetDlgItem(graph,105),renameTarget.c_str());WorkflowProbe::Click(graph,106);
        WorkflowProbe::Click(graph,112,"GraphSharedRenamed");require(currentTag(linkInstance["members"][1])=="GraphSharedRenamed" && currentTag(linkInstance["members"][2])=="GraphSharedRenamed","graph rename dialog updates whole shared tag group");
        require(call({{"op","connections"},{"actor",magicInstance["members"][0]["path"]}})==magicEdges,"graph rename dialog preserves dependent links");
        WorkflowProbe::cancelTagPreview=true;WorkflowProbe::Click(graph,112,"CancelledTag");WorkflowProbe::cancelTagPreview=false;
        require(currentTag(linkInstance["members"][1])=="GraphSharedRenamed","cancelling graph rename preview leaves Tags unchanged");
        WorkflowProbe::previewScreenshot=directory/"tag_rename_exclusions.bmp";
        WorkflowProbe::excludeTagEntity=linkInstance["members"][2]["path"].get<std::string>();WorkflowProbe::Click(graph,112,"GraphPartialRenamed");WorkflowProbe::excludeTagEntity.clear();WorkflowProbe::previewScreenshot.clear();
        require(currentTag(linkInstance["members"][1])=="GraphPartialRenamed" && currentTag(linkInstance["members"][2])=="GraphSharedRenamed","popup checkbox excludes an entity from shared tag rename");
        Exec("TRANSACTION UNDO");require(currentTag(linkInstance["members"][1])=="GraphSharedRenamed","popup partial rename is one undo operation");
        WorkflowProbe::Click(graph,109); // fit the searched node's island
        WorkflowProbe::Screenshot(graph,directory/"workflow_graph_island.bmp");
        SendMessage(GetDlgItem(graph,100),CB_SETCURSEL,0,0);SendMessage(graph,WM_COMMAND,MAKEWPARAM(100,CBN_SELCHANGE),0);WorkflowProbe::Click(graph,107);
        WorkflowProbe::Screenshot(graph,directory/"workflow_graph_selected.bmp");
        J busy=linked;busy["id"]="radial-fixture";busy["actors"]=J::array();
        for(int i=0;i<17;++i)
        {
            std::string name=i==0?"EventHub":"EventTarget"+std::to_string(i),tag=i==0?"Hub":"Targets",event=i==0?"Targets":"None";
            busy["actors"].push_back({{"name",name},{"path","Assembly."+name},{"class","Engine.Light"},{"text","Begin Actor Class=Light Name="+name+"\nTag="+tag+"\nEvent="+event+"\nEnd Actor\n"},{"tag",tag},{"event",event},{"position",{i*64,0,0}},{"rotation",{0,0,0}}});
        }
        auto busyInstance=call({{"op","assembly.place"},{"definition",busy},{"position",{0,1024,0}},{"rotation",{0,0,0}},{"bindings",J::object()}});
        auto busyHub=busyInstance["members"][0]["path"].get<std::string>();
        require(call({{"op","connections"},{"actor",busyHub}}).size()==16,"radial graph fixture retains all sixteen event targets");
        SendMessage(GetDlgItem(graph,100),CB_SETCURSEL,1,0);SendMessage(graph,WM_COMMAND,MAKEWPARAM(100,CBN_SELCHANGE),0);
        WorkflowProbe::Click(graph,110);WorkflowProbe::Click(graph,108);WorkflowProbe::Screenshot(graph,directory/"workflow_graph_radial_level.bmp");
        SetWindowTextA(GetDlgItem(graph,105),busyHub.c_str());WorkflowProbe::Click(graph,106);WorkflowProbe::Click(graph,111);WorkflowProbe::Click(graph,109);
        auto busySelection=call({{"op","actors"},{"selected",true}});require(busySelection.size()==1 && busySelection[0]["path"]==busyHub,"radial island hub remains searchable and selectable");
        WorkflowProbe::Screenshot(graph,directory/"workflow_graph_radial_island.bmp");
        DestroyWindow(graph);
        DestroyWindow(connections);DestroyWindow(assemblies);DestroyWindow(views);
        J setpiece=J::array();auto setEvent=call({{"op","magic.create"},{"class","SBase.SMagicEvent"}});setpiece.push_back(setEvent);
        for(const auto* type:{"Engine.Trigger","Engine.Trigger","Engine.Mover","SBase.SAmbientSoundTrigger","SBase.SSmokeEmitter","SBase.SDamageVolume"})
        {
            auto beforeEvent=call({{"op","magic.inspect"},{"actor",setEvent}});auto countBefore=call({{"op","actors"}}).size();
            auto created=call({{"op","magic.create"},{"class",type},{"event",beforeEvent},{"trigger",std::string(type)=="Engine.Trigger"},{"geometry",std::string(type)=="SBase.SDamageVolume"?"box":"point"}});
            auto afterEvent=call({{"op","magic.inspect"},{"actor",setEvent}});
            Exec("TRANSACTION UNDO");require(call({{"op","actors"}}).size()==countBefore && call({{"op","magic.inspect"},{"actor",setEvent}})["values"]==beforeEvent["values"],"compound actor creation and wiring undo together");
            Exec("TRANSACTION REDO");require(call({{"op","magic.inspect"},{"actor",setEvent}})["values"]==afterEvent["values"],"compound creation and wiring redo restores event");setpiece.push_back(created);
        }
        auto setSnapshot=call({{"op","magic.inspect"},{"actor",setEvent}});auto setGroups=setSnapshot["values"]["Groups"];
        auto countBeforeFailure=call({{"op","actors"}}).size();bool failedCompound=false;
        try{call({{"op","magic.create"},{"class","Engine.Trigger"},{"event",setSnapshot},{"group",999}});}catch(...){failedCompound=true;}
        require(failedCompound && call({{"op","actors"}}).size()==countBeforeFailure && call({{"op","magic.inspect"},{"actor",setEvent}})["values"]==setSnapshot["values"],"failed compound creation rolls back actors and event data");
        for(size_t i=0;i<setGroups[0]["EventGroup"].size();++i)setGroups[0]["EventGroup"][i]["Delay"]=std::to_string(i*0.75);
        auto off=setGroups[0]["EventGroup"].back();off["Delay"]="5";off["Type"]="EVT_Untrigger";setGroups[0]["EventGroup"].push_back(off);
        call({{"op","magic.edit"},{"snapshot",setSnapshot},{"changes",{{"Groups",setGroups}}}});
        bool staleRejected=false;try{call({{"op","magic.edit"},{"snapshot",setSnapshot},{"changes",{{"Groups",setGroups}}}});}catch(...){staleRejected=true;}require(staleRejected,"workbench rejects stale edits without overwriting newer data");
        auto moverSnapshot=call({{"op","magic.inspect"},{"actor",setpiece[3]}});call({{"op","magic.edit"},{"snapshot",moverSnapshot},{"changes",{{"InitialState","TriggerOpenTimed"},{"MoveTime","2.5"}}}});
        auto componentOwner=call({{"op","magic.inspect"},{"actor",setpiece[5]}});std::ofstream(directory/"magic_setpiece_emitter.json")<<componentOwner.dump(2);
        J savedMagic=J::array();for(const auto& identity:setpiece){auto snapshot=call({{"op","magic.inspect"},{"actor",identity}});savedMagic.push_back({{"actor",identity},{"values",snapshot["values"]}});}
        std::ofstream(directory/"magic_setpiece.json")<<savedMagic.dump(2);
        call({{"op","select"},{"actors",J::array({setEvent})}});SendMessage(frameWindow,WM_COMMAND,40927,0);auto setWindow=WorkflowProbe::FindDialog("SMagicEvent Workbench");require(setWindow!=nullptr,"set piece opens in the same workbench");WorkflowProbe::Screenshot(setWindow,directory/"magic_setpiece_workbench.bmp");SendMessage(setWindow,WM_CLOSE,0,0);
        {
            auto snapshot=call({{"op","camera.snapshot"}});require(snapshot.empty(),"camera fixture starts empty");
            J paths=J::array(),identities=J::array();
            for(int i=0;i<3;++i)
            {
                auto actor=call({{"op","camera.add"},{"snapshot",snapshot},{"paths",paths},{"loop",true}});
                paths.push_back(actor.at("path"));identities.push_back(actor);snapshot=call({{"op","camera.snapshot"}});
            }
            auto original=snapshot;
            Exec("TRANSACTION UNDO");require(call({{"op","camera.snapshot"}}).size()==2,"adding a camera and updating its neighbours undo together");
            Exec("TRANSACTION REDO");require(call({{"op","camera.snapshot"}})==original,"camera creation redo restores all reciprocal links");
            auto reversed=J::array({paths[2],paths[1],paths[0]});
            call({{"op","camera.order"},{"snapshot",snapshot},{"paths",reversed},{"loop",true}});
            auto first=call({{"op","magic.inspect"},{"actor",identities[2]}});
            require(first["values"]["bFirstCam"]=="True" && first["values"]["NextCam"].get<std::string>().find(paths[1].get<std::string>())!=std::string::npos,"reorder sets first flag and next camera");
            bool rejected=false;try{call({{"op","camera.order"},{"snapshot",original},{"paths",paths}});}catch(...){rejected=true;}require(rejected,"stale network snapshot is rejected");
            Exec("TRANSACTION UNDO");require(call({{"op","camera.snapshot"}})==original,"reordering all cameras uses one undo");
            rejected=false;try{call({{"op","camera.order"},{"snapshot",original},{"paths",J::array({paths[0]})}});}catch(...){rejected=true;}
            require(rejected && call({{"op","camera.snapshot"}})==original,"partial component edits leave camera links unchanged");
            call({{"op","select"},{"actors",J::array({identities[0]})}});SendMessage(frameWindow,WM_COMMAND,40932,0);
            auto manager=WorkflowProbe::FindDialog("SCamNetwork Manager");require(manager!=nullptr,"camera manager menu opens");
            require(SendMessage(GetDlgItem(manager,101),LB_GETCOUNT,0,0)==3,"camera manager shows all three cameras in network order");
            SetWindowTextA(GetDlgItem(manager,107),"Loading bay east");WorkflowProbe::Click(manager,108);
            first=call({{"op","magic.inspect"},{"actor",identities[0]}});
            require(first["values"]["CamName"]=="\"Loading bay east\"","camera manager renames in-game display name");
            auto oldTag=first["values"]["Tag"];
            SetWindowTextA(GetDlgItem(manager,107),"Loading bay \"east\"");WorkflowProbe::Click(manager,108);
            first=call({{"op","magic.inspect"},{"actor",identities[0]}});
            require(first["values"]["CamName"]==J("Loading bay \"east\"").dump() && first["values"]["Tag"]==oldTag,"camera names preserve spaces and quotes without changing Tags");
            WorkflowProbe::Click(manager,105);require(call({{"op","magic.inspect"},{"actor",identities[0]}})["values"]["CamName"]=="\"Loading bay east\"","rename undo restores the previous display name");
            WorkflowProbe::Click(manager,119);first=call({{"op","magic.inspect"},{"actor",identities[0]}});
            auto capturedViews=call({{"op","view.capture"}})["cameras"];
            require(std::stod(first["values"]["Location"]["X"].get<std::string>())==capturedViews[0]["position"][0].get<double>(),"place at viewport captures camera position");
            auto camerasBefore=call({{"op","view.capture"}})["cameras"];
            WorkflowProbe::Click(manager,115);require(SendMessage(GetDlgItem(manager,101),LB_GETCURSEL,0,0)==1,"Next steps to the second camera");
            auto previewViews=call({{"op","view.capture"}})["cameras"];auto second=call({{"op","magic.inspect"},{"actor",identities[1]}});
            require(previewViews[0]["position"][0].get<double>()==std::stod(second["values"]["Location"]["X"].get<std::string>()),"Next previews the selected camera position");
            WorkflowProbe::Click(manager,117);require(call({{"op","view.capture"}})["cameras"]==camerasBefore,"return view restores the original viewport cameras exactly");
            WorkflowProbe::Click(manager,109);first=call({{"op","magic.inspect"},{"actor",identities[1]}});
            require(first["values"]["bFirstCam"]=="True","Move up rewires and sets the first camera");
            WorkflowProbe::Click(manager,120);snapshot=call({{"op","camera.snapshot"}});first=call({{"op","magic.inspect"},{"actor",identities[1]}});
            require(snapshot.size()==3 && first["values"]["NextCam"]=="None" && first["values"]["PrevCam"]=="None","detach keeps the camera actor and removes both links");
            WorkflowProbe::Click(manager,105);require(call({{"op","magic.inspect"},{"actor",identities[1]}})["values"]["bFirstCam"]=="True","detach undo restores the original network");
            WorkflowProbe::Screenshot(manager,directory/"camera_network_manager.bmp");
            RECT cameraWindow{};GetWindowRect(manager,&cameraWindow);RECT cameraScaled{cameraWindow.left,cameraWindow.top,cameraWindow.left+1500,cameraWindow.top+1095};SendMessage(manager,WM_DPICHANGED,MAKEWPARAM(144,144),reinterpret_cast<LPARAM>(&cameraScaled));WorkflowProbe::Screenshot(manager,directory/"camera_network_manager_144dpi.bmp");SendMessage(manager,WM_DPICHANGED,MAKEWPARAM(96,96),reinterpret_cast<LPARAM>(&cameraWindow));
            SendMessage(manager,WM_CLOSE,0,0);std::ofstream(directory/"camera_network_saved.json")<<call({{"op","camera.snapshot"}}).dump(2);
        }
        {
            J document;std::ifstream(directory/"map-changes.json")>>document;document["map"]=call({{"op","authoring.export"}}).at("map");
            auto applied=call({{"op","authoring.apply"},{"document",document}});J states=J::array();
            for(const auto& actor:applied["created"])states.push_back(call({{"op","magic.inspect"},{"actor",actor}}));
            std::ofstream(directory/"map_authoring_saved.json")<<states.dump(2);
        }
        require(reinterpret_cast<Save>(0x10E0416B)(*reinterpret_cast<void**>(kEditor),destination)!=0,"save final workflow map for restart test");
        auto playable=directory.parent_path()/"Packages"/"Maps"/std::filesystem::path(destination).filename();
        WorkflowProbe::previewScreenshot=directory/"map_package_preview.bmp";WorkflowProbe::packagePreviewChecked=false;
        auto packageTimer=SetTimer(nullptr,0,50,WorkflowProbe::Answer);
        call({{"op","package.preview"},{"map",playable.string()}});KillTimer(nullptr,packageTimer);WorkflowProbe::previewScreenshot.clear();
        require(WorkflowProbe::packagePreviewChecked,"packaging dialog scans the saved map and keeps the playable map checked");
        WorkflowProbe::packagePreviewChecked=false;
        packageTimer=SetTimer(nullptr,0,50,WorkflowProbe::Answer);
        SendMessage(frameWindow,WM_COMMAND,40928,0);KillTimer(nullptr,packageTimer);
        require(WorkflowProbe::packagePreviewChecked,"File menu packaging command opens its map chooser");
        std::ofstream(directory/"workflow_result.json")<<J({{"view",view},{"instance",instance}}).dump(2);
        Record("PASS","Native workflow replacement, connections, views, assembly insertion and in-place updates, undo/redo, rebuild and save/reopen verified.");
    }
    catch(const std::exception& e){Record("FAIL",e.what());}
}
