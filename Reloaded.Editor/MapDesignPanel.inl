// Included inside WorkflowTools' anonymous namespace, sharing its persistent
// per-map library and standard native forms.
enum DesignControl { DPlane=700,DFit,DRefresh,DReference,DCalibrate,DRemoveReference,DBlock,DEdit,DPlace,DDetach,DGuides,DMeasure,DAlign,DLayer,DRepeat,DPlay,DRoute,DFinish,DClear,DCanvas,DStatus,DDepth };
struct DesignState
{
    HWND window{},canvas{},status{};unsigned epoch=0,revision=0;Json scene=Json::array(),pending,previous,points=Json::array();
    Pose frame{};int plane=0;double zoom=.2,panX=300,panY=300,depth=0;POINT last{};bool dragging=false;
    std::string mode,annotationName,annotationKind;std::unique_ptr<Gdiplus::Bitmap> image;std::string imagePath;
};
HWND designWindow=nullptr;
Json DesignData() {Sync();return document["maps"][mapKey].value("design",Json{{"pieces",Json::array()},{"layers",Json::array()},{"annotations",Json::array()}});}
void DesignSave(const Json& data){Json next=document;next["maps"][mapKey]["design"]=data;Save(next);}
void DesignStatus(DesignState& s,const std::string& text){SetWindowTextA(s.status,text.c_str());}
Vector DesignWorld(DesignState& s,double x,double y)
{
    Vector v{};int a=s.plane==2?1:0,b=s.plane==0?1:2;v[3-a-b]=s.depth;v[a]=(x-s.panX)/s.zoom;v[b]=(s.panY-y)/s.zoom;return v;
}
Gdiplus::PointF DesignScreen(DesignState& s,const Vector& v)
{int a=s.plane==2?1:0,b=s.plane==0?1:2;return {static_cast<float>(s.panX+v[a]*s.zoom),static_cast<float>(s.panY-v[b]*s.zoom)};}
void DesignRefresh(DesignState& s)
{
    Sync();if(s.epoch!=mapEpoch){s.pending=Json{};s.previous=Json{};s.points=Json::array();s.mode.clear();s.image.reset();s.imagePath.clear();s.epoch=mapEpoch;}
    s.scene=Editor::DesignScene();s.revision=Editor::Revision();InvalidateRect(s.canvas,nullptr,FALSE);
}
void DesignFit(DesignState& s)
{
    Vector lo{},hi{};bool first=true;auto add=[&](Vector p){if(first){lo=hi=p;first=false;}else for(int i=0;i<3;++i){lo[i]=std::min(lo[i],p[i]);hi[i]=std::max(hi[i],p[i]);}};
    for(auto& actor:s.scene)if(!actor.at("hidden").get<bool>()){add(actor.at("position").get<Vector>());for(auto& e:actor.at("edges")){add(e[0].get<Vector>());add(e[1].get<Vector>());}}
    auto data=DesignData();if(data.contains("reference") && data["reference"]["plane"]==s.plane)
    {
        auto ref=data["reference"];Gdiplus::Bitmap image(std::filesystem::path(ref.at("path").get<std::string>()).c_str());
        if(image.GetLastStatus()==Gdiplus::Ok){Vector origin=ref.at("origin");add(origin);origin[s.plane==2?1:0]+=image.GetWidth()*ref.at("scale").get<double>();origin[s.plane==0?1:2]-=image.GetHeight()*ref.at("scale").get<double>();add(origin);}
    }
    if(!s.pending.is_null())for(auto& solid:Design::Geometry(s.pending))for(auto& f:solid.faces)for(auto& v:f)add(TransformPoint(v,s.frame));
    RECT r{};GetClientRect(s.canvas,&r);int a=s.plane==2?1:0,b=s.plane==0?1:2;
    s.zoom=std::clamp(std::min((r.right-100)/std::max(hi[a]-lo[a],256.0),(r.bottom-100)/std::max(hi[b]-lo[b],256.0)),.002,8.0);
    s.panX=r.right/2.0-(hi[a]+lo[a])/2*s.zoom;s.panY=r.bottom/2.0+(hi[b]+lo[b])/2*s.zoom;InvalidateRect(s.canvas,nullptr,FALSE);
}
void DesignPaint(DesignState& s,HDC dc)
{
    using namespace Gdiplus;RECT rect{};GetClientRect(s.canvas,&rect);Bitmap buffer(std::max(1L,rect.right),std::max(1L,rect.bottom));Graphics g(&buffer);
    g.Clear(Color(247,248,250));g.SetSmoothingMode(SmoothingModeAntiAlias);auto data=DesignData();
    if(data.contains("reference"))
    {
        auto ref=data.at("reference");auto path=ref.at("path").get<std::string>();
        if(path!=s.imagePath){s.image.reset(Bitmap::FromFile(std::filesystem::path(path).c_str()));s.imagePath=path;}
        if(s.image && s.image->GetLastStatus()==Ok && ref.at("plane")==s.plane)
        {
            auto origin=DesignScreen(s,ref.at("origin").get<Vector>());float scale=static_cast<float>(ref.at("scale").get<double>()*s.zoom);
            ColorMatrix matrix={1,0,0,0,0, 0,1,0,0,0, 0,0,1,0,0, 0,0,0,static_cast<float>(ref.at("opacity").get<double>()),0, 0,0,0,0,1};
            ImageAttributes attrs;attrs.SetColorMatrix(&matrix);
            g.DrawImage(s.image.get(),RectF(origin.X,origin.Y,s.image->GetWidth()*scale,s.image->GetHeight()*scale),0,0,static_cast<float>(s.image->GetWidth()),static_cast<float>(s.image->GetHeight()),UnitPixel,&attrs);
        }
    }
    Pen grid(Color(40,100,110,120)),axis(Color(90,70,90,100));double spacing=64;
    while(spacing*s.zoom<24)spacing*=2;while(spacing*s.zoom>150)spacing/=2;
    for(double x=std::fmod(s.panX,spacing*s.zoom);x<rect.right;x+=spacing*s.zoom)g.DrawLine(&grid,static_cast<float>(x),0.f,static_cast<float>(x),static_cast<float>(rect.bottom));
    for(double y=std::fmod(s.panY,spacing*s.zoom);y<rect.bottom;y+=spacing*s.zoom)g.DrawLine(&grid,0.f,static_cast<float>(y),static_cast<float>(rect.right),static_cast<float>(y));
    g.DrawLine(&axis,static_cast<float>(s.panX),0.f,static_cast<float>(s.panX),static_cast<float>(rect.bottom));g.DrawLine(&axis,0.f,static_cast<float>(s.panY),static_cast<float>(rect.right),static_cast<float>(s.panY));
    Font font(L"Segoe UI",10);SolidBrush ink(Color(30,45,60));
    auto label=[&](const std::string& text,PointF p){auto wide=std::wstring(text.begin(),text.end());g.DrawString(wide.c_str(),-1,&font,p,&ink);};
    auto line=[&](Pen& pen,Vector a,Vector b){g.DrawLine(&pen,DesignScreen(s,a),DesignScreen(s,b));};
    std::vector<std::pair<PointF,std::vector<std::string>>> selectedLabels;
    for(auto& actor:s.scene)
    {
        if(actor.at("hidden").get<bool>())continue;
        Pen pen(actor.at("selected").get<bool>()?Color(230,90,30):actor.at("locked").get<bool>()?Color(160,165,175):Color(60,95,120),actor.at("selected").get<bool>()?2.f:1.f);
        for(auto& edge:actor.at("edges"))line(pen,edge[0].get<Vector>(),edge[1].get<Vector>());
        auto p=DesignScreen(s,actor.at("position").get<Vector>());if(actor.at("edges").empty())g.DrawEllipse(&pen,p.X-3,p.Y-3,6.f,6.f);
        if(actor.at("selected").get<bool>())
        {
            auto path=actor.at("path").get<std::string>();auto name=path.substr(path.find_last_of('.')+1);
            auto found=std::find_if(selectedLabels.begin(),selectedLabels.end(),[&](const auto& entry){return std::abs(entry.first.X-p.X)<16 && std::abs(entry.first.Y-p.Y)<16;});
            if(found==selectedLabels.end())selectedLabels.push_back({p,{name}});else found->second.push_back(name);
        }
    }
    for(auto& entry:selectedLabels)label(entry.second.size()==1?entry.second.front():std::to_string(entry.second.size())+" selected actors",{entry.first.X+5,entry.first.Y+8});
    if(!s.pending.is_null())
    {
        Pen pen(Color(0,145,105),2);for(auto& solid:Design::Geometry(s.pending))for(auto& f:solid.faces)for(size_t i=0;i<f.size();++i)line(pen,TransformPoint(f[i],s.frame),TransformPoint(f[(i+1)%f.size()],s.frame));
        label(s.pending.value("name",std::string("Preview"))+" (preview)",DesignScreen(s,s.frame.position));
    }
    for(auto& guide:data.value("guides",Json::array()))
    {
        Vector p=guide.at("position");double width=guide.at("width"),height=guide.at("height");Pen pen(Color(190,80,170),2);
        auto a=p,b=p;int horizontal=s.plane==2?1:0;a[horizontal]-=width/2;b[horizontal]+=width/2;
        if(s.plane==0){a[1]-=width/2;b[1]+=width/2;}else b[2]+=height;
        auto p1=DesignScreen(s,a),p2=DesignScreen(s,b);g.DrawRectangle(&pen,p1.X,p2.Y,p2.X-p1.X,p1.Y-p2.Y);
        if(s.plane!=0){auto head=p;head[2]+=height*.85;auto hp=DesignScreen(s,head);float radius=static_cast<float>(width*.2*s.zoom);g.DrawEllipse(&pen,hp.X-radius,hp.Y-radius,2*radius,2*radius);auto feet=p,body=p;body[2]+=height*.7;line(pen,feet,body);}
        auto caption=DesignScreen(s,b);caption.Y-=20;label(guide.at("name").get<std::string>()+": "+std::to_string(static_cast<int>(width))+" x "+std::to_string(static_cast<int>(height)),caption);
    }
    auto annotations=data.at("annotations");if(!s.points.empty())annotations.push_back({{"name",s.annotationName},{"kind",s.mode},{"points",s.points}});
    for(auto& annotation:annotations)
    {
        Pen pen(annotation.at("kind")=="Measure"?Color(160,70,160):Color(35,145,70),2);const auto& points=annotation.at("points");double distance=0;
        for(size_t i=0;i<points.size();++i){auto p=DesignScreen(s,points[i].get<Vector>());g.DrawEllipse(&pen,p.X-4,p.Y-4,8.f,8.f);if(i){line(pen,points[i-1].get<Vector>(),points[i].get<Vector>());distance+=Design::Distance(points[i-1].get<Vector>(),points[i].get<Vector>());}}
        if(!points.empty())label(annotation.at("name").get<std::string>()+(points.size()>1?" ("+std::to_string(static_cast<int>(distance))+" units)":""),DesignScreen(s,points.back().get<Vector>()));
    }
    label(s.plane==0?"TOP XY":s.plane==1?"FRONT XZ":"SIDE YZ",{10,8});Graphics target(dc);target.DrawImage(&buffer,0,0);
}
Json DesignPiece(DesignState& s)
{
    auto selection=Editor::SelectedIdentities();auto data=DesignData();
    for(auto it=data["pieces"].rbegin();it!=data["pieces"].rend();++it)for(auto& selected:selection)for(auto& member:it->at("members"))if(selected.at("path")==member.at("path"))return *it;
    throw std::runtime_error("Select a generated blockout brush first.");
}
void DesignBlockForm(DesignState& s,bool editing)
{
    Json old=editing?DesignPiece(s):Json{};Json spec=editing?old.at("spec"):Json{{"kind","Room"},{"width",1024},{"length",1024},{"height",256},{"thickness",16},{"steps",8},{"ceiling",true},{"name","Blockout"}};
    Pose frame=editing?Pose{old.at("position").get<Vector>(),old.at("rotation").get<Rotation>()}:Editor::BuilderPose();
    std::vector<InputField> fields={{"Name",spec.value("name",std::string("Blockout")),{}},{"Shape",spec.at("kind"),{"Room","Corridor","Doorway","Stairs","Ramp","Platform"}},
        {"Width (X)",spec["width"].dump(),{}},{"Length / depth (Y)",spec["length"].dump(),{}},{"Height / rise (Z)",spec["height"].dump(),{}},{"Wall / slab thickness",spec["thickness"].dump(),{}},{"Stair count",spec["steps"].dump(),{}},
        {"Ceiling",spec["ceiling"].get<bool>()?"Yes":"No",{"Yes","No"}},
        {"Base X",std::to_string(frame.position[0]),{}},{"Base Y",std::to_string(frame.position[1]),{}},{"Base Z",std::to_string(frame.position[2]),{}},{"Yaw (degrees)",std::to_string(frame.rotation[1]*360.0/65536),{}},
        {"Placement anchor","Centre of floor",{"Centre of floor","Lower-left floor corner"}}};
    if(!Ask(s.window,editing?"Edit Blockout Dimensions":"Preview New Blockout",fields))return;
    spec["name"]=fields[0].value;spec["kind"]=fields[1].value;spec["width"]=Design::Number(fields[2].value,1,65536);spec["length"]=Design::Number(fields[3].value,1,65536);spec["height"]=Design::Number(fields[4].value,1,65536);spec["thickness"]=Design::Number(fields[5].value,1,65536);double steps=Design::Number(fields[6].value,1,128);if(steps!=std::floor(steps))throw std::runtime_error("Stair count must be a whole number.");spec["steps"]=static_cast<int>(steps);spec["ceiling"]=fields[7].value=="Yes";
    for(int i=0;i<3;++i)frame.position[i]=Design::Number(fields[8+i].value);frame.rotation[1]=static_cast<int>(std::fmod(Design::Number(fields[11].value),360.0)*65536/360);
    if(fields[12].value=="Lower-left floor corner")frame.position=TransformPoint({spec["width"].get<double>()/2,spec["length"].get<double>()/2,0},frame);
    Design::Geometry(spec);s.pending=spec;s.previous=old;s.frame=frame;DesignFit(s);DesignStatus(s,"Green brushes are a preview. Place / Apply creates one Undo step. Rebuild geometry afterwards.");
}
void DesignCommand(DesignState& s,int id)
{
    Sync();if(s.epoch!=mapEpoch){DesignRefresh(s);if(id!=DRefresh)throw std::runtime_error("The map changed. Try the action again in the current map.");}
    auto data=DesignData();
    if(id==DRefresh){DesignRefresh(s);return;}
    if(id==DPlane){s.plane=static_cast<int>(SendMessage(GetDlgItem(s.window,DPlane),CB_GETCURSEL,0,0));s.points=Json::array();s.mode.clear();DesignFit(s);return;}
    if(id==DFit){DesignFit(s);return;}
    if(id==DDepth){std::vector<InputField> f={{s.plane==0?"Floor Z":s.plane==1?"Depth Y":"Depth X",std::to_string(s.depth),{}}};if(Ask(s.window,"Annotation Plane Depth",f))s.depth=Design::Number(f[0].value);return;}
    if(id==DBlock || id==DEdit){DesignBlockForm(s,id==DEdit);return;}
    if(id==DPlace)
    {
        if(s.pending.is_null())throw std::runtime_error("Create or edit a preview first.");auto piece=Editor::DesignBlockout(s.pending,s.frame,s.previous);
        if(!s.previous.is_null())for(auto& layer:data["layers"])
        {
            bool included=false;for(auto& member:layer["members"])for(auto& previous:s.previous["members"])if(member.at("path")==previous.at("path"))included=true;
            // Keep historical identities as well so native Undo can restore the
            // old brushes without losing their logical layer membership.
            if(included)for(auto& member:piece["members"])layer["members"].push_back(member);
        }
        data["pieces"].push_back(piece);try{DesignSave(data);}catch(...){Editor::Exec("TRANSACTION UNDO");throw;}
        s.pending=Json{};s.previous=Json{};DesignRefresh(s);DesignStatus(s,"Blockout placed. Build geometry to update collision and rendered BSP.");return;
    }
    if(id==DDetach)
    {
        auto piece=DesignPiece(s);auto& pieces=data["pieces"];pieces.erase(std::remove(pieces.begin(),pieces.end(),piece),pieces.end());DesignSave(data);s.pending=Json{};s.previous=Json{};DesignStatus(s,"Detached. The brushes remain in the map for manual editing.");return;
    }
    if(id==DReference)
    {
        wchar_t path[32768]{};OPENFILENAMEW ofn{sizeof(ofn)};ofn.hwndOwner=s.window;ofn.lpstrTitle=L"Choose Reference Image";ofn.lpstrFile=path;ofn.nMaxFile=32768;ofn.lpstrFilter=L"Reference image\0*.png;*.jpg;*.jpeg;*.bmp\0\0";ofn.Flags=OFN_FILEMUSTEXIST|OFN_NOCHANGEDIR;
        if(!GetOpenFileNameW(&ofn))return;if(std::filesystem::file_size(path)>32*1024*1024)throw std::runtime_error("Choose a reference image smaller than 32 MiB.");Gdiplus::Bitmap image(path);if(image.GetLastStatus()!=Gdiplus::Ok || !image.GetWidth() || image.GetWidth()>16000 || image.GetHeight()>16000 || static_cast<uint64_t>(image.GetWidth())*image.GetHeight()>16000000)throw std::runtime_error("Choose a readable image up to 16 megapixels and 16000 pixels per side.");
        std::vector<InputField> fields={{"Units per image pixel","1",{}},{"Opacity (0 to 1)","0.4",{}},{"Top-left horizontal units","0",{}},{"Top-left vertical units","0",{}}};if(!Ask(s.window,"Place Reference Image",fields))return;
        double scale=Design::Number(fields[0].value,.0001,10000),opacity=Design::Number(fields[1].value,0,1);Vector origin{};origin[s.plane==2?1:0]=Design::Number(fields[2].value);origin[s.plane==0?1:2]=Design::Number(fields[3].value);
        auto destination=Editor::Directory()/"References"/(Id()+std::filesystem::path(path).extension().string());std::filesystem::create_directories(destination.parent_path());std::filesystem::copy_file(path,destination);
        data["reference"]={{"path",destination.string()},{"scale",scale},{"opacity",opacity},{"origin",origin},{"plane",s.plane}};DesignSave(data);DesignStatus(s,"Reference copied into ReloadedEditor/References. Calibrate with two image points if needed.");
    }
    else if(id==DRemoveReference){data.erase("reference");DesignSave(data);s.image.reset();s.imagePath.clear();}
    else if(id==DCalibrate || id==DMeasure)
    {
        if(id==DCalibrate && (!data.contains("reference") || data["reference"]["plane"]!=s.plane))throw std::runtime_error("Add an image in this view first.");
        s.mode=id==DCalibrate?"Calibrate":"Measure";s.annotationName="Measurement";s.points=Json::array();DesignStatus(s,"Click two points in the design view. Right-click cancels.");
    }
    else if(id==DGuides)
    {
        auto profiles=Editor::DesignClearances();std::string name="Custom clearance",width="48",height="96";
        if(!profiles.empty())
        {
            std::vector<std::string> choices;for(auto& profile:profiles)choices.push_back(profile.at("name"));choices.push_back("Custom clearance");
            std::vector<InputField> choose={{"Pawn class defaults","",choices}};if(!Ask(s.window,"Choose Player Clearance Profile",choose))return;
            auto index=std::find(choices.begin(),choices.end(),choose[0].value)-choices.begin();name=choose[0].value;
            if(index<static_cast<ptrdiff_t>(profiles.size())){width=profiles[index]["width"].dump();height=profiles[index]["height"].dump();}
        }
        auto p=Editor::BuilderPose().position;std::vector<InputField> f={{"Guide name",name,{}},{"Width (units)",width,{}},{"Height (units)",height,{}},{"Base X",std::to_string(p[0]),{}},{"Base Y",std::to_string(p[1]),{}},{"Base Z",std::to_string(p[2]),{}}};
        if(!Ask(s.window,"Player Clearance Guide (configure to your movement test)",f))return;
        for(int i=0;i<3;++i)p[i]=Design::Number(f[i+3].value);if(!data.contains("guides"))data["guides"]=Json::array();data["guides"].push_back({{"name",f[0].value},{"width",Design::Number(f[1].value,1,10000)},{"height",Design::Number(f[2].value,1,10000)},{"position",p}});DesignSave(data);DesignStatus(s,"Guide dimensions are illustrative starting values: enter measured clearances and verify movement in-game.");
    }
    else if(id==DAlign)
    {
        DesignRefresh(s);Json selected=Json::array();for(auto& a:s.scene)if(a.at("selected").get<bool>())selected.push_back(a);
        std::vector<InputField> f={{"Operation","Align",{"Align","Distribute"}},{"Axis","X",{"X","Y","Z"}},{"Distribution spacing","128",{}}};if(!Ask(s.window,"Align Origins / Distribute Selection",f))return;
        Editor::DesignAlign(selected,f[1].value=="X"?0:f[1].value=="Y"?1:2,f[0].value,Design::Number(f[2].value));DesignRefresh(s);
    }
    else if(id==DLayer)
    {
        Json nativeMembers;bool nativeHidden=false,nativeLocked=false;
        std::vector<std::string> choices={"New layer from selection"};for(auto& layer:data["layers"])choices.push_back(layer.at("name"));
        std::vector<InputField> f={{"Layer","",choices},{"Action","Select",{"Select","Hide","Show","Lock movement","Unlock movement","Add selection","Remove selection","Delete layer"}}};if(!Ask(s.window,"Named Map Layers",f))return;
        auto index=std::find(choices.begin(),choices.end(),f[0].value)-choices.begin();
        if(index==0)
        {
            auto members=Editor::SelectedIdentities();if(members.empty())throw std::runtime_error("Select actors or brushes first.");std::string name="Layer";if(!GetName(s.window,"Name Layer",name))return;
            if(std::find(choices.begin(),choices.end(),name)!=choices.end())throw std::runtime_error("That layer name is already used.");
            // Exclusive membership makes visibility and lock changes predictable.
            for(auto& layer:data["layers"])for(auto& m:members)for(auto& old:layer["members"])if(m.at("path")==old.at("path"))throw std::runtime_error("Remove selected actors from their existing layer first.");
            data["layers"].push_back({{"name",name},{"members",members},{"hidden",false},{"locked",false}});
        }
        else
        {
            auto& layer=data["layers"][index-1];auto action=f[1].value;
            if(action=="Select")Editor::Select(layer.at("members"),true);
            else if(action=="Delete layer"){nativeMembers=layer.at("members");data["layers"].erase(index-1);}
            else if(action=="Add selection" || action=="Remove selection")
            {
                auto selection=Editor::SelectedIdentities();for(auto& member:selection)
                {
                    auto& members=layer["members"];auto found=std::find_if(members.begin(),members.end(),[&](const Json& m){return m.at("path")==member.at("path");});
                    if(action=="Remove selection"){if(found!=members.end()){if(nativeMembers.is_null())nativeMembers=Json::array();nativeMembers.push_back(*found);members.erase(found);}}
                    else if(found==members.end())
                    {
                        for(auto& other:data["layers"])for(auto& m:other["members"])if(m.at("path")==member.at("path"))throw std::runtime_error("An actor already belongs to another layer.");members.push_back(member);
                    }
                }
                if(action=="Add selection"){nativeMembers=layer.at("members");nativeHidden=layer.at("hidden");nativeLocked=layer.at("locked");}
            }
            else
            {
                if(action=="Hide" || action=="Show")layer["hidden"]=action=="Hide";else layer["locked"]=action=="Lock movement";
                nativeMembers=layer.at("members");nativeHidden=layer.at("hidden");nativeLocked=layer.at("locked");
            }
        }
        if(!nativeMembers.is_null())Editor::DesignLayer(nativeMembers,nativeHidden,nativeLocked);
        try{DesignSave(data);}catch(...){if(!nativeMembers.is_null())Editor::Exec("TRANSACTION UNDO");throw;}
        DesignRefresh(s);DesignStatus(s,"Layer membership saved. Movement locks use native bLockLocation; hiding affects editor visibility only.");
    }
    else if(id==DRepeat)
    {
        auto members=Editor::SelectedIdentities();auto frame=Editor::BuilderPose();std::vector<InputField> f={{"Number of new copies","4",{}},{"Spacing X","128",{}},{"Spacing Y","0",{}},{"Spacing Z","0",{}},{"Yaw step (degrees)","0",{}},{"Pivot","Builder brush",{"Builder brush","First selected actor"}}};if(!Ask(s.window,"Repeat Selection (new copies)",f))return;
        if(f[5].value=="First selected actor"){DesignRefresh(s);for(auto& a:s.scene)if(a.at("selected").get<bool>()){frame.position=a.at("position").get<Vector>();break;}}
        auto def=Editor::CaptureAssembly(members,frame);def["id"]="design-repeat";if(!def["bindings"].empty())throw std::runtime_error("Include actors used by external links, or use the assembly library to bind a single copy.");
        double copies=Design::Number(f[0].value,1,128);if(copies!=std::floor(copies))throw std::runtime_error("Copy count must be a whole number.");
        auto repeated=Design::Repeat(def,static_cast<int>(copies),{Design::Number(f[1].value),Design::Number(f[2].value),Design::Number(f[3].value)},static_cast<int>(std::fmod(Design::Number(f[4].value),360.0)*65536/360));
        Editor::PlaceAssembly(repeated,frame,{});DesignRefresh(s);DesignStatus(s,"Repeated copies placed in one Undo step. The originals remain unchanged.");
    }
    else if(id==DPlay)
    {
        auto starts=Editor::DesignSpawns();if(starts.empty())throw std::runtime_error("Place a PlayerStart for each team in the map first.");std::vector<std::string> choices;
        for(auto& start:starts)choices.push_back("Team "+start.at("team").get<std::string>()+" - "+start.at("path").get<std::string>());
        auto cameras=Editor::CaptureView().at("cameras");Pose pose=Editor::BuilderPose();for(auto& c:cameras){int mode=c.at("mode");if(mode!=13 && mode!=14 && mode!=15){pose={c.at("position").get<Vector>(),c.at("rotation").get<Rotation>()};break;}}
        std::vector<InputField> f={{"Team spawn","",choices},{"Test location","Perspective viewport",{"Perspective viewport","Builder brush"}}};if(!Ask(s.window,"Temporary Playtest Spawn",f))return;if(f[1].value=="Builder brush")pose=Editor::BuilderPose();
        auto index=std::find(choices.begin(),choices.end(),f[0].value)-choices.begin();Editor::DesignPlay(starts[index],pose);DesignStatus(s,"Play Level returned. Editor spawn locations restored. Use the matching team in Play Map Options.");
    }
    else if(id==DRoute)
    {
        std::vector<InputField> f={{"Name","Route A",{}},{"Annotation","Route",{"Route","Objective","Spy spawn","Merc spawn"}}};if(!Ask(s.window,"Plan Routes and Objectives",f))return;s.annotationName=f[0].value;s.annotationKind=f[1].value;s.mode="Route";s.points=Json::array();DesignStatus(s,"Click route points or a marker position. Finish stores the annotation; right-click cancels.");
    }
    else if(id==DFinish)
    {
        if(s.mode!="Route" || s.points.empty())throw std::runtime_error("Start a route or marker and click its position first.");
        if(s.annotationKind=="Route" && s.points.size()<2)throw std::runtime_error("A route needs at least two points.");
        data["annotations"].push_back({{"name",s.annotationName},{"kind",s.annotationKind},{"points",s.points}});DesignSave(data);s.points=Json::array();s.mode.clear();
    }
    else if(id==DClear)
    {
        std::vector<std::string> names={"Clear player guides"};for(size_t i=0;i<data["annotations"].size();++i)names.push_back(std::to_string(i+1)+": "+data["annotations"][i].at("name").get<std::string>());
        std::vector<InputField> f={{"Remove","",names}};if(!Ask(s.window,"Remove Design Annotation",f))return;auto i=std::find(names.begin(),names.end(),f[0].value)-names.begin();if(i==0)data.erase("guides");else data["annotations"].erase(i-1);DesignSave(data);
    }
    InvalidateRect(s.canvas,nullptr,FALSE);
}
LRESULT CALLBACK DesignCanvasProc(HWND window,UINT message,WPARAM w,LPARAM l)
{
    auto s=reinterpret_cast<DesignState*>(GetWindowLongPtr(window,GWLP_USERDATA));if(!s)return DefWindowProcA(window,message,w,l);
    try
    {
        if(message==WM_ERASEBKGND)return 1;
        if(message==WM_PRINTCLIENT){DesignPaint(*s,reinterpret_cast<HDC>(w));return 0;}
        if(message==WM_PAINT){PAINTSTRUCT ps{};auto dc=BeginPaint(window,&ps);try{DesignPaint(*s,dc);}catch(...){EndPaint(window,&ps);throw;}EndPaint(window,&ps);return 0;}
        if(message==WM_MBUTTONDOWN){s->dragging=true;s->last={GET_X_LPARAM(l),GET_Y_LPARAM(l)};SetCapture(window);return 0;}
        if(message==WM_MBUTTONUP){s->dragging=false;ReleaseCapture();return 0;}
        if(message==WM_MOUSEMOVE && s->dragging){POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};s->panX+=p.x-s->last.x;s->panY+=p.y-s->last.y;s->last=p;InvalidateRect(window,nullptr,FALSE);return 0;}
        if(message==WM_MOUSEWHEEL){POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};ScreenToClient(window,&p);auto world=DesignWorld(*s,p.x,p.y);s->zoom=std::clamp(s->zoom*(GET_WHEEL_DELTA_WPARAM(w)>0?1.2:1/1.2),.002,8.0);auto after=DesignScreen(*s,world);s->panX+=p.x-after.X;s->panY+=p.y-after.Y;InvalidateRect(window,nullptr,FALSE);return 0;}
        if(message==WM_RBUTTONDOWN){s->mode.clear();s->points=Json::array();s->pending=Json{};s->previous=Json{};InvalidateRect(window,nullptr,FALSE);return 0;}
        if(message==WM_LBUTTONDOWN)
        {
            Sync();if(s->epoch!=mapEpoch){DesignRefresh(*s);return 0;}auto p=DesignWorld(*s,GET_X_LPARAM(l),GET_Y_LPARAM(l));
            if(!s->mode.empty())
            {
                if(s->points.size()>=2000)throw std::runtime_error("Finish the route before adding more points.");
                if(s->mode=="Route" && s->annotationKind!="Route")s->points=Json::array();s->points.push_back(p);
                if((s->mode=="Measure" || s->mode=="Calibrate") && s->points.size()==2)
                {
                    auto data=DesignData();double distance=Design::Distance(s->points[0].get<Vector>(),p);
                    if(s->mode=="Calibrate")
                    {
                        std::vector<InputField> f={{"Known distance (world units)","256",{}}};if(Ask(s->window,"Calibrate Reference Scale",f))
                        {double old=data["reference"]["scale"];data["reference"]["scale"]=Design::Calibration(distance/old,Design::Number(f[0].value,.001,1000000));DesignSave(data);}
                    }
                    else{data["annotations"].push_back({{"name","Measurement"},{"kind","Measure"},{"points",s->points}});DesignSave(data);DesignStatus(*s,"Measured "+std::to_string(distance)+" Unreal units.");}
                    s->mode.clear();s->points=Json::array();
                }
            }
            else
            {
                Json selected;double best=14;for(auto& actor:s->scene)if(!actor.at("hidden").get<bool>() && !actor.at("locked").get<bool>())
                {auto at=DesignScreen(*s,actor.at("position").get<Vector>());double distance=std::hypot(at.X-GET_X_LPARAM(l),at.Y-GET_Y_LPARAM(l));if(distance<best){best=distance;selected=actor;}}
                if(!selected.is_null()){Editor::Select(Json::array({selected}),false);DesignRefresh(*s);}
            }
            InvalidateRect(window,nullptr,FALSE);return 0;
        }
    }
    catch(const std::exception& e){DesignStatus(*s,e.what());}
    return DefWindowProcA(window,message,w,l);
}
LRESULT CALLBACK DesignProc(HWND window,UINT message,WPARAM w,LPARAM l)
{
    auto s=reinterpret_cast<DesignState*>(GetWindowLongPtr(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){s=static_cast<DesignState*>(reinterpret_cast<CREATESTRUCT*>(l)->lpCreateParams);s->window=window;SetWindowLongPtr(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s));}
    if(!s)return DefWindowProcA(window,message,w,l);
    try
    {
        if(message==WM_CREATE)
        {
            Control(window,"COMBOBOX","",CBS_DROPDOWNLIST,DPlane,12,12,190,150);for(const char* plane:{"Top (XY)","Front (XZ)","Side (YZ)"})SendMessageA(GetDlgItem(window,DPlane),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(plane));SendMessage(GetDlgItem(window,DPlane),CB_SETCURSEL,0,0);
            MoveWindow(GetDlgItem(window,DPlane),12,12,125,150,TRUE);Control(window,"BUTTON","Depth...",0,DDepth,140,12,62,25);
            const std::pair<int,const char*> buttons[]={{DFit,"Fit map / preview"},{DRefresh,"Refresh from editor"},{DReference,"Reference image..."},{DCalibrate,"Calibrate image"},{DRemoveReference,"Remove reference"},{DBlock,"New blockout..."},{DEdit,"Edit selected piece..."},{DPlace,"Place / Apply preview"},{DDetach,"Detach selected piece"},{DGuides,"Player clearance..."},{DMeasure,"Measure two points"},{DAlign,"Align / distribute..."},{DLayer,"Layers..."},{DRepeat,"Repeat selection..."},{DPlay,"Playtest from here..."},{DRoute,"Route / objective..."},{DFinish,"Finish route / marker"},{DClear,"Remove annotation..."}};
            int y=46;for(auto [id,title]:buttons){Control(window,"BUTTON",title,0,id,12,y,190,27);y+=30;}
            s->canvas=CreateWindowExA(WS_EX_CLIENTEDGE,"ReloadedMapDesignCanvas","",WS_CHILD|WS_VISIBLE,216,12,700,620,window,reinterpret_cast<HMENU>(DCanvas),GetModuleHandle(nullptr),nullptr);SetWindowLongPtr(s->canvas,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s));
            s->status=Control(window,"STATIC","",0,DStatus,12,600,900,48);DesignRefresh(*s);DesignFit(*s);DesignStatus(*s,"Wheel: zoom. Middle drag: pan. Click actor origin: select. Right-click: cancel preview. Annotations are editor-only.");SetTimer(window,1,700,nullptr);return 0;
        }
        if(message==WM_SIZE){int width=LOWORD(l),height=HIWORD(l);MoveWindow(s->canvas,216,12,std::max(1,width-228),std::max(1,height-76),TRUE);MoveWindow(s->status,12,height-56,width-24,50,TRUE);return 0;}
        if(message==WM_GETMINMAXINFO){reinterpret_cast<MINMAXINFO*>(l)->ptMinTrackSize={950,730};return 0;}
        if(message==WM_COMMAND){if(LOWORD(w)==DPlane && HIWORD(w)!=CBN_SELCHANGE)return 0;DesignCommand(*s,LOWORD(w));return 0;}
        if(message==WM_TIMER){Sync();if(s->epoch!=mapEpoch || s->revision!=Editor::Revision())DesignRefresh(*s);return 0;}
        if(message==WM_CLOSE){DestroyWindow(window);return 0;}
        if(message==WM_NCDESTROY){KillTimer(window,1);designWindow=nullptr;SetWindowLongPtr(window,GWLP_USERDATA,0);delete s;return DefWindowProcA(window,message,w,l);}
    }
    catch(const std::exception& e){DesignStatus(*s,e.what());if(message==WM_COMMAND)MessageBoxA(window,e.what(),"Map Design",MB_OK|MB_ICONINFORMATION);}
    return DefWindowProcA(window,message,w,l);
}
void OpenDesign(HWND owner)
{
    if(designWindow){ShowWindow(designWindow,SW_RESTORE);SetForegroundWindow(designWindow);return;}
    static ULONG_PTR token=0;if(!token){Gdiplus::GdiplusStartupInput input;if(Gdiplus::GdiplusStartup(&token,&input,nullptr)!=Gdiplus::Ok)throw std::runtime_error("Cannot start the design renderer.");}
    WNDCLASSA wc{};wc.hInstance=GetModuleHandle(nullptr);wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);wc.lpfnWndProc=DesignCanvasProc;wc.lpszClassName="ReloadedMapDesignCanvas";RegisterClassA(&wc);
    wc.lpfnWndProc=DesignProc;wc.lpszClassName="ReloadedMapDesign";RegisterClassA(&wc);auto state=new DesignState;
    designWindow=CreateWindowExA(WS_EX_CONTROLPARENT,wc.lpszClassName,"Map Design",WS_OVERLAPPEDWINDOW|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,1200,800,owner,nullptr,wc.hInstance,state);
    if(!designWindow)throw std::runtime_error("Cannot open Map Design.");
}
