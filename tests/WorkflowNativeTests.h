// Included by NativeMapRecoveryProbe after its existing fixture helpers.
#include "../Reloaded.Editor/Include/nlohmann/json.hpp"
#include <stdexcept>
#include <commctrl.h>
namespace WorkflowProbe {
unsigned char* MagicActor(const nlohmann::json& identity)
{
    auto path=identity.at("path").get<std::string>();auto name=path.substr(path.find_last_of('.')+1);
    auto level=*reinterpret_cast<unsigned char**>(*reinterpret_cast<uintptr_t*>(kEditor)+0x130);auto data=*reinterpret_cast<unsigned char***>(level+0x2c);int count=*reinterpret_cast<int*>(level+0x30);
    for(int i=2;i<count;++i)if(data[i] && name==ObjectName(data[i]))return data[i];throw std::runtime_error("Magic fixture actor missing.");
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
void RunWorkflowTests(HMODULE editorDll, const char* destination, bool restart=false)
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
        require(reinterpret_cast<Save>(0x10E0416B)(*reinterpret_cast<void**>(kEditor),destination)!=0,"save final workflow map for restart test");
        std::ofstream(directory/"workflow_result.json")<<J({{"view",view},{"instance",instance}}).dump(2);
        Record("PASS","Native workflow replacement, connections, views, assembly insertion and in-place updates, undo/redo, rebuild and save/reopen verified.");
    }
    catch(const std::exception& e){Record("FAIL",e.what());}
}
