// The contextual properties sheet in the side panel: whatever was clicked or
// chosen last shows its settings here and applies them as they are edited, so
// no property window has to be opened. Pieces keep the fixed inspector; lights,
// security devices, player references, routes and a route being drawn get rows
// built for them. Included inside MapDesignPanel.inl after LightingPanel.inl.
struct SheetField { std::string key,label,value; std::vector<std::string> choices; };
// How many rows a sheet can hold; the ids from DSheetField up to DSheetLabel
// leave room for these and their labels.
constexpr size_t kSheetFieldLimit=180;
void SheetDestroyControls(DesignState& s)
{
    for(int id=DSheetField;id<DSheetButton+10;++id)
        if(auto control=GetDlgItem(s.window,id))DestroyWindow(control);
    s.sheetPlacements.clear();
    s.sheetContent=0;
    s.sheetScroll=0;
    SheetLayout(s);
}
// Puts the sheet's rows where the current scroll offset says, hides the ones
// that would fall outside the panel, and shows the bar only when the rows do
// not all fit. The area runs from under the title to just above the status
// line, so it grows with the window.
void SheetLayout(DesignState& s)
{
    if(!s.window)return;
    RECT client{};
    GetClientRect(s.window,&client);
    const int top=s.inspectorTop+kInspectorBodyTop,bottom=std::max(top+26,static_cast<int>(client.bottom)-78),view=bottom-top;
    const bool scrolls=s.sheetContent>view;
    s.sheetScroll=std::clamp(s.sheetScroll,0,scrolls?s.sheetContent-view:0);
    for(const auto& placement:s.sheetPlacements)
    {
        auto control=GetDlgItem(s.window,placement.id);
        if(!control)continue;
        const int y=top+placement.y-s.sheetScroll;
        // A drop-down list's height is how far it opens, not how tall its row
        // is, so the row height decides whether it is in view.
        const bool showing=y+std::min(placement.height,26)>top && y<bottom;
        if(showing)MoveWindow(control,placement.x,y,placement.width,placement.height,TRUE);
        ShowWindow(control,showing?SW_SHOWNA:SW_HIDE);
    }
    if(!s.sheetBar)return;
    ShowWindow(s.sheetBar,scrolls?SW_SHOW:SW_HIDE);
    if(!scrolls)return;
    MoveWindow(s.sheetBar,392,top,16,view,TRUE);
    SCROLLINFO info{sizeof(info)};
    info.fMask=SIF_RANGE|SIF_PAGE|SIF_POS;
    info.nMin=0;info.nMax=s.sheetContent-1;info.nPage=view;info.nPos=s.sheetScroll;
    SetScrollInfo(s.sheetBar,SB_CTL,&info,TRUE);
}
void SheetPieceControls(DesignState& s,bool show)
{
    for(int id=DName;id<=DDiscard;++id)if(auto control=GetDlgItem(s.window,id))ShowWindow(control,show?SW_SHOW:SW_HIDE);
    if(auto presets=GetDlgItem(s.window,DPreset))ShowWindow(presets,show?SW_SHOW:SW_HIDE);
    for(int id=DInspectorLabel;id<DInspectorLabel+13;++id)if(auto control=GetDlgItem(s.window,id))ShowWindow(control,show?SW_SHOW:SW_HIDE);
}
// Shows a message where the properties would be.
void SheetHint(DesignState& s,const std::string& hint)
{
    s.writingSheet=true;
    s.sheet.kind.clear();
    s.sheet.subject=Json{};
    s.sheet.keys.clear();
    s.sheet.shown.clear();
    SheetDestroyControls(s);
    if(s.pending.is_null())
    {
        SheetPieceControls(s,false);
        SetWindowTextA(GetDlgItem(s.window,DInspectorTitle),hint.c_str());
    }
    s.writingSheet=false;
}
void SheetBuild(DesignState& s,const std::string& kind,const Json& subject,const std::string& title,
                const std::vector<SheetField>& fields,const std::vector<std::string>& buttons)
{
    s.writingSheet=true;
    s.sheet.kind=kind;
    s.sheet.subject=subject;
    s.sheet.keys.clear();
    s.sheet.shown.clear();
    SheetDestroyControls(s);
    SheetPieceControls(s,false);
    SetWindowTextA(GetDlgItem(s.window,DInspectorTitle),title.c_str());
    auto place=[&](int id,int x,int y,int width,int height){s.sheetPlacements.push_back({id,x,y,width,height});};
    int y=0;
    for(size_t i=0;i<fields.size() && i<kSheetFieldLimit;++i)
    {
        const auto& field=fields[i];
        const int id=DSheetField+static_cast<int>(i);
        std::string shown=field.value;
        const bool yesNo=field.choices.size()==2 && field.choices[0]=="Yes" && field.choices[1]=="No";
        if(yesNo)
        {
            auto box=Control(s.window,"BUTTON",field.label,BS_AUTOCHECKBOX,id,12,0,376,22);
            SendMessage(box,BM_SETCHECK,field.value=="Yes"?BST_CHECKED:BST_UNCHECKED,0);
            shown=field.value=="Yes"?"Yes":"No";
            place(id,12,y,376,22);
        }
        else
        {
            Control(s.window,"STATIC",field.label,0,DSheetLabel+static_cast<int>(i),12,0,150,20);
            place(DSheetLabel+static_cast<int>(i),12,y+4,150,20);
            if(field.choices.empty())
            {
                auto edit=Control(s.window,"EDIT",field.value,ES_AUTOHSCROLL,id,166,0,222,22);
                SetWindowSubclass(edit,DesignFieldProc,id,reinterpret_cast<DWORD_PTR>(&s));
                place(id,166,y,222,22);
            }
            else
            {
                auto combo=Control(s.window,"COMBOBOX","",CBS_DROPDOWNLIST|WS_VSCROLL,id,166,0,222,220);
                int select=0;
                for(size_t c=0;c<field.choices.size();++c)
                {
                    SendMessageA(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(field.choices[c].c_str()));
                    if(field.choices[c]==field.value)select=static_cast<int>(c);
                }
                SendMessage(combo,CB_SETCURSEL,select,0);
                shown=field.choices[select];
                place(id,166,y,222,220);
            }
        }
        s.sheet.keys.push_back(field.key);
        s.sheet.shown[field.key]=shown;
        y+=26;
    }
    y+=6;
    for(size_t i=0;i<buttons.size() && i<8;++i)
    {
        const int id=DSheetButton+static_cast<int>(i),x=12+static_cast<int>(i%2)*196,row=y+static_cast<int>(i/2)*30;
        Control(s.window,"BUTTON",buttons[i],0,id,x,0,182,26);
        place(id,x,row,182,26);
    }
    s.sheetContent=y+((static_cast<int>(std::min(buttons.size(),size_t{8}))+1)/2)*30;
    s.sheetScroll=0;
    SheetLayout(s);
    s.writingSheet=false;
}
std::map<std::string,std::string> SheetValues(DesignState& s)
{
    std::map<std::string,std::string> values;
    for(size_t i=0;i<s.sheet.keys.size();++i)
    {
        auto control=GetDlgItem(s.window,DSheetField+static_cast<int>(i));
        if(!control)continue;
        char cls[32]{};
        GetClassNameA(control,cls,sizeof(cls));
        std::string value;
        if(_stricmp(cls,"ComboBox")==0)
        {
            const int index=static_cast<int>(SendMessage(control,CB_GETCURSEL,0,0));
            if(index>=0)
            {
                const int length=static_cast<int>(SendMessage(control,CB_GETLBTEXTLEN,index,0));
                std::string text(length+1,'\0');
                SendMessageA(control,CB_GETLBTEXT,index,reinterpret_cast<LPARAM>(text.data()));
                text.resize(length);
                value=text;
            }
        }
        else if(_stricmp(cls,"Button")==0)value=SendMessage(control,BM_GETCHECK,0,0)==BST_CHECKED?"Yes":"No";
        else value=Text(control);
        values[s.sheet.keys[i]]=value;
    }
    return values;
}
std::vector<SheetField> SheetFrom(const std::vector<InputField>& fields)
{
    std::vector<SheetField> out;
    for(const auto& field:fields)out.push_back({field.label,field.label,field.value,field.choices});
    return out;
}
// The subjects.
void SheetShowLight(DesignState& s,const Json& light)
{
    SheetBuild(s,"light",light,"Light "+SecurityName(light)+(light.value("switchable",false)?" (switchable)":""),SheetFrom(LightFields(light)),{"Select in editor","Delete light"});
}
// A game actor is not a security device: the editor's property window holds
// its settings. What the plan can edit is where it faces, which is what the
// arrow on its icon shows and its handle turns.
void SheetShowGameActor(DesignState& s,const Json& actor)
{
    std::vector<SheetField> fields={
        {"yaw","Facing (degrees)",Design::Round(ObjectiveYaw(actor)*360.0/65536,1),{}},
        {"z","Height (Z)",Design::Round(actor.at("position").get<Vector>()[2]),{}}};
    SheetBuild(s,"actor",actor,ObjectiveLabel(actor),fields,{"Select in editor"});
    DesignStatus(s,ObjectiveLabel(actor)+" faces "+Design::Round(ObjectiveYaw(actor)*360.0/65536)
        +" degrees. Drag the handle on its arrow to turn it, or type the angle here; its name and settings are in the editor's property window.");
}
void SheetShowDevice(DesignState& s,const Json& device)
{
    const auto kind=device.value("kind",std::string());
    if(!Security::Find(kind) && !device.contains("event"))
    {
        SheetShowGameActor(s,device);
        return;
    }
    auto fields=SheetFrom(SecurityFields(device));
    if(Security::Detector(kind))
    {
        SheetField wiring{"Wired to alarm","Wired to alarm","(nothing)",{"(nothing)"}};
        for(const auto& alarm:s.securityActors)
        {
            if(alarm.value("kind",std::string())!="Alarm")continue;
            const auto label=alarm.value("name",std::string())+" ("+SecurityName(alarm)+")";
            wiring.choices.push_back(label);
            if(Fold(alarm.value("tag",std::string()))==Fold(device.value("event",std::string())))wiring.value=label;
        }
        fields.push_back(wiring);
    }
    SheetBuild(s,"device",device,kind+" "+SecurityName(device),fields,{"Select in editor","Delete "+Fold(kind)});
}
void SheetShowGuide(DesignState& s,size_t index)
{
    const auto& data=DesignData(s);
    if(index>=data.at("guides").size()){SheetHint(s,"The reference is gone.");return;}
    const auto& guide=data.at("guides")[index];
    std::vector<SheetField> fields={
        {"name","Name",guide.value("name",std::string()),{}},
        {"width","Width (units)",Design::Round(guide.value("width",96.0)),{}},
        {"height","Height (units)",Design::Round(guide.value("height",180.0)),{}},
        {"posture","Posture",guide.value("posture",std::string("Standing")),{"Standing","Crouching"}}};
    SheetBuild(s,"guide",Json{{"index",index}},"Player reference: "+guide.value("name",std::string()),fields,{"Remove reference"});
}
void SheetShowAnnotation(DesignState& s,size_t index)
{
    const auto& data=DesignData(s);
    if(index>=data.at("annotations").size()){SheetHint(s,"The annotation is gone.");return;}
    const auto& annotation=data.at("annotations")[index];
    const bool route=annotation.value("kind",std::string())=="Route";
    std::vector<SheetField> fields={
        {"name","Name",annotation.value("name",std::string()),{}},
        {"team","Team",annotation.value("team",std::string("Any")),{"Any","Spy","Merc"}},
        {"crouched","Crouched (vents)",annotation.value("crouched",false)?"Yes":"No",{"Yes","No"}}};
    std::string title=annotation.value("kind",std::string("Annotation"))+": "+annotation.value("name",std::string());
    if(route)try{title=Design::RouteSummary(annotation,data);}catch(const std::exception&){}
    SheetBuild(s,"annotation",Json{{"index",index}},title,fields,{"Remove"});
}
// --- Level, environment and map settings ------------------------------------
// The LevelInfo's own settings, its zone light and fog, and the mission's
// settings, read through the editor's reflection and edited in place. Simple
// leaves only; a small struct (a colour) is spread over its fields; only the
// categories that belong to the subject, with everything but the generic
// Actor categories as the fallback when a class names its categories
// differently.
const char* const kSettingsGenericCategories[]={"Advanced","Collision","Display","Force","Karma","Lighting","LightColor","Movement","Object","Sound","Networking","Corona","Physics","Events","Havok","Editor"};
Json SettingsActor(DesignState& s,const std::string& which)
{
    if(which=="mission")
    {
        for(const auto& actor:s.objectives)if(actor.value("kind",std::string())=="Mission")return actor;
        throw std::runtime_error("The map has no SMission yet: right-click the plan and choose Mission here.");
    }
    for(const auto& actor:Editor::Actors())
    {
        const auto cls=actor.value("class",std::string());
        if(cls=="Engine.LevelInfo" || (cls.size()>10 && cls.compare(cls.size()-10,10,".LevelInfo")==0))return actor;
    }
    throw std::runtime_error("The map has no LevelInfo actor.");
}
std::vector<SheetField> SettingsFields(const Json& snapshot,const std::set<std::string>& categories,bool everything)
{
    std::vector<SheetField> fields;
    const auto& values=snapshot.at("values");
    auto leaf=[&](const std::string& key,const std::string& label,const Json& schema,const Json& value)
    {
        const auto kind=schema.value("kind",std::string());
        SheetField field{key,label,"",{}};
        const std::string text=value.is_string()?value.get<std::string>():value.dump();
        if(kind=="BoolProperty"){field.value=Fold(text)=="true"?"Yes":"No";field.choices={"Yes","No"};}
        else if(kind=="ByteProperty" && !schema.value("choices",Json::array()).empty())
        {
            for(const auto& choice:schema.at("choices"))field.choices.push_back(choice.get<std::string>());
            field.value=text;
        }
        else if(kind=="StrProperty")
        {
            try{field.value=Json::parse(text).get<std::string>();}catch(const std::exception&){field.value=text;}
        }
        else if(kind=="IntProperty" || kind=="FloatProperty" || kind=="NameProperty" || kind=="ByteProperty" || kind=="ObjectProperty" || kind=="ClassProperty")field.value=text;
        else return;
        fields.push_back(field);
    };
    for(auto it=snapshot.at("schema").begin();it!=snapshot.at("schema").end();++it)
    {
        const auto& name=it.key();
        const auto& schema=it.value();
        const auto category=schema.value("category",std::string());
        if(everything){bool generic=false;for(const char* g:kSettingsGenericCategories)if(category==g)generic=true;if(generic)continue;}
        else if(!categories.count(category))continue;
        if(!values.contains(name))continue;
        const Json& value=values.at(name);
        if(schema.value("kind",std::string())=="StructProperty")
        {
            const auto sub=schema.value("fields",Json::object());
            if(!value.is_object() || sub.size()>6)continue; // Colours and other small structs only.
            for(auto f=sub.begin();f!=sub.end();++f)if(value.contains(f.key()))leaf(name+"."+f.key(),name+" "+f.key(),f.value(),value.at(f.key()));
        }
        else leaf(name,name,schema,value);
        if(fields.size()>=kSheetFieldLimit)break;
    }
    return fields;
}
void SheetShowSettings(DesignState& s,const std::string& which)
{
    const Json actor=SettingsActor(s,which);
    // The level and environment sheets edit the map's LevelInfo, which the
    // ordinary read holds back along with the builder brush.
    const Json snapshot=Editor::InspectSettingsActor(actor);
    std::set<std::string> categories;
    std::string title;
    if(which=="level"){categories={"LevelInfo","LevelSummary","Audio","Music","Game","Story","Level"};title="Level settings ("+SecurityName(actor)+")";}
    else if(which=="environment"){categories={"ZoneLight","ZoneInfo","ZoneSound","Fog","DistanceFog","Weather","Sky","Zone"};title="Environment: ambient light and fog ("+SecurityName(actor)+")";}
    else{categories={};title="Map settings: mission "+SecurityName(actor);}
    auto fields=SettingsFields(snapshot,categories,which=="mission");
    if(fields.empty())fields=SettingsFields(snapshot,{},true);
    if(fields.empty())throw std::runtime_error("No editable settings were found on "+SecurityName(actor)+".");
    SheetBuild(s,"settings",Json{{"which",which},{"actor",actor},{"schema",snapshot.at("schema")},{"title",title}},title,fields,{"Select in editor","Refresh"});
}
void SheetShowRoute(DesignState& s)
{
    std::vector<SheetField> fields={
        {"name","Name",s.annotationName,{}},
        {"kind","Annotation",s.annotationKind,{"Route","Objective","Spy spawn","Merc spawn"}},
        {"team","Team (routes)",s.annotationTeam,{"Any","Spy","Merc"}},
        {"crouched","Crouched (vents)",s.annotationCrouched?"Yes":"No",{"Yes","No"}}};
    SheetBuild(s,"route",Json{},"Drawing: click points in the view ("+std::to_string(s.points.size())+" so far)",fields,{"Finish route / marker","Cancel drawing"});
}
// Applies edited values to the subject.
void SheetApply(DesignState& s)
{
    if(s.writingSheet || s.sheet.kind.empty())return;
    auto values=SheetValues(s);
    if(values==s.sheet.shown)return;
    std::vector<std::string> ordered;
    for(const auto& key:s.sheet.keys)ordered.push_back(values[key]);
    const auto kind=s.sheet.kind;
    const Json subject=s.sheet.subject;
    if(kind=="light")
    {
        Editor::SetActorProperties(subject,LightFieldsToProperties(ordered));
        s.sheet.shown=values;
        DesignRefresh(s);
        DesignStatus(s,SecurityName(subject)+" updated. One Undo step; rebuild lighting to see it.");
        return;
    }
    if(kind=="device")
    {
        const auto deviceKind=subject.value("kind",std::string());
        const bool detector=Security::Detector(deviceKind);
        std::vector<std::string> own(ordered.begin(),detector?ordered.end()-1:ordered.end());
        auto properties=SecurityFieldsToProperties(deviceKind,own);
        if(!properties.empty())Editor::SetActorProperties(subject,properties);
        if(detector && values["Wired to alarm"]!=s.sheet.shown["Wired to alarm"])
        {
            const auto choice=values["Wired to alarm"];
            if(choice=="(nothing)")Editor::UnwireDetector(subject);
            else
                for(const auto& alarm:s.securityActors)
                    if(alarm.value("kind",std::string())=="Alarm" && alarm.value("name",std::string())+" ("+SecurityName(alarm)+")"==choice)
                        Editor::LinkDetectorToAlarm(subject,alarm);
        }
        s.sheet.shown=values;
        DesignRefresh(s);
        DesignStatus(s,SecurityName(subject)+" updated. One Undo step.");
        return;
    }
    if(kind=="actor")
    {
        Pose pose{subject.at("position").get<Vector>(),subject.at("rotation").get<Rotation>()};
        const double degrees=std::fmod(Design::Number(values["yaw"],-100000,100000),360.0);
        pose.rotation[1]=((static_cast<int>(std::lround(degrees*65536/360))%65536)+65536)%65536;
        pose.position[2]=Design::Number(values["z"]);
        Editor::MoveSecurityActor(subject,pose,Json::object());
        s.sheet.shown=values;
        DesignRefresh(s);
        DesignStatus(s,SecurityName(subject)+" now faces "+Design::Round(pose.rotation[1]*360.0/65536)
            +" degrees at Z "+Design::Round(pose.position[2])+". One Undo step.");
        return;
    }
    if(kind=="guide" || kind=="annotation")
    {
        Json data=DesignData(s);
        const size_t index=subject.value("index",size_t{0});
        auto& list=data[kind=="guide"?"guides":"annotations"];
        if(index>=list.size()){SheetHint(s,"The item is gone.");return;}
        auto& item=list[index];
        item["name"]=values["name"].substr(0,120);
        if(kind=="guide")
        {
            item["width"]=Design::Number(values["width"],1,10000);
            item["height"]=Design::Number(values["height"],1,10000);
            item["posture"]=values["posture"];
        }
        else
        {
            item["team"]=values["team"];
            item["crouched"]=values["crouched"]=="Yes";
        }
        s.sheet.shown=values;
        DesignSave(s,data);
        InvalidateRect(s.canvas,nullptr,FALSE);
        DesignStatus(s,kind=="guide"?"Reference updated.":"Annotation updated.");
        return;
    }
    if(kind=="settings")
    {
        // Changed rows only; a struct goes back whole, from the rows that show it.
        const auto& schema=subject.at("schema");
        Json properties=Json::object();
        for(const auto& key:s.sheet.keys)
        {
            if(values[key]==s.sheet.shown[key])continue;
            const auto dot=key.find('.');
            const std::string name=key.substr(0,dot);
            const Json leaf=dot==std::string::npos?schema.at(name):schema.at(name).at("fields").at(key.substr(dot+1));
            const auto leafKind=leaf.value("kind",std::string());
            Json value=leafKind=="BoolProperty"?Json(values[key]=="Yes"?"True":"False"):leafKind=="StrProperty"?Json(Json(values[key]).dump()):Json(values[key]);
            if(dot==std::string::npos)properties[name]=value;
            else
            {
                if(!properties.contains(name))
                {
                    properties[name]=Json::object();
                    for(const auto& other:s.sheet.keys)
                        if(other.rfind(name+".",0)==0)properties[name][other.substr(name.size()+1)]=values[other];
                }
                properties[name][key.substr(dot+1)]=value;
            }
        }
        if(properties.empty())return;
        try{Editor::SetActorProperties(subject.at("actor"),properties);}
        catch(const std::exception& e){DesignStatus(s,e.what());SheetRefreshLater(s);return;}
        s.sheet.shown=values;
        DesignRefresh(s);
        DesignStatus(s,subject.value("title",std::string("Settings"))+": updated. One Undo step.");
        return;
    }
    if(kind=="route")
    {
        s.annotationName=values["name"];
        s.annotationKind=values["kind"];
        s.annotationTeam=values["team"];
        s.annotationCrouched=values["crouched"]=="Yes";
        s.sheet.shown=values;
        InvalidateRect(s.canvas,nullptr,FALSE);
    }
}
// The buttons under the fields.
void SheetButton(DesignState& s,int index)
{
    const auto kind=s.sheet.kind;
    const Json subject=s.sheet.subject;
    if(kind=="actor")
    {
        if(index!=0)return;
        Editor::Select(Json::array({subject}),true);
        DesignRefresh(s);
        DesignStatus(s,"Selected "+SecurityName(subject)+" in the editor; its name and settings are in the editor's property window.");
        return;
    }
    if(kind=="light" || kind=="device")
    {
        if(index==0)
        {
            Editor::Select(Json::array({subject}),true);
            DesignRefresh(s);
            DesignStatus(s,"Selected "+SecurityName(subject)+" in the editor.");
        }
        else if(index==1)
        {
            Editor::DeleteSecurityActor(subject);
            SheetHint(s,"Deleted "+SecurityName(subject)+". Undo brings it back.");
            DesignRefresh(s);
            DesignStatus(s,"Deleted "+SecurityName(subject)+". Undo brings it back.");
        }
        return;
    }
    if(kind=="guide" || kind=="annotation")
    {
        if(index!=0)return;
        Json data=DesignData(s);
        const size_t at=subject.value("index",size_t{0});
        auto& list=data[kind=="guide"?"guides":"annotations"];
        if(at<list.size())list.erase(at);
        SheetHint(s,kind=="guide"?"Reference removed. Undo brings it back.":"Annotation removed. Undo brings it back.");
        DesignSave(s,data);
        InvalidateRect(s.canvas,nullptr,FALSE);
        return;
    }
    if(kind=="settings")
    {
        if(index==0)
        {
            Editor::Select(Json::array({subject.at("actor")}),true);
            DesignRefresh(s);
            DesignStatus(s,"Selected "+SecurityName(subject.at("actor"))+" in the editor; every property is in the editor's property window.");
        }
        else if(index==1)SheetShowSettings(s,subject.value("which",std::string("level")));
        return;
    }
    if(kind=="route")
    {
        if(index==0){DesignCommand(s,DFinish);SheetHint(s,"Stored. Click a route point to edit it.");}
        else
        {
            s.mode.clear();s.points=Json::array();
            SheetHint(s,"Drawing cancelled.");
            InvalidateRect(s.canvas,nullptr,FALSE);
        }
    }
}
// Re-reads the subject after the map or workspace changed; the piece inspector
// takes over whenever a preview is active.
void SheetRefresh(DesignState& s)
{
    if(s.sheet.kind.empty() || !s.pending.is_null())return;
    const auto kind=s.sheet.kind;
    const Json subject=s.sheet.subject;
    if(kind=="light")
    {
        for(const auto& light:s.lights)if(light.at("path")==subject.at("path")){SheetShowLight(s,light);return;}
        SheetHint(s,"The light is no longer in the map.");
    }
    else if(kind=="device")
    {
        for(const auto& device:s.securityActors)if(device.at("path")==subject.at("path")){SheetShowDevice(s,device);return;}
        SheetHint(s,"The device is no longer in the map.");
    }
    else if(kind=="actor")
    {
        for(const auto& actor:s.objectives)if(actor.at("path")==subject.at("path")){SheetShowGameActor(s,actor);return;}
        SheetHint(s,"The actor is no longer in the map.");
    }
    else if(kind=="guide")SheetShowGuide(s,subject.value("index",size_t{0}));
    else if(kind=="annotation")SheetShowAnnotation(s,subject.value("index",size_t{0}));
    else if(kind=="settings")
    {
        try{SheetShowSettings(s,subject.value("which",std::string("level")));}
        catch(const std::exception& e){SheetHint(s,e.what());}
    }
    else if(kind=="route")
    {
        if(s.mode=="Route")SheetShowRoute(s);
        else SheetHint(s,"Click a light, device, reference or route point to edit it here.");
    }
}
void SheetRefreshLater(DesignState& s)
{
    if(s.window && !s.sheet.kind.empty())PostMessage(s.window,WM_APP+7,0,0);
}
