// The contextual properties sheet in the side panel: whatever was clicked or
// chosen last shows its settings here and applies them as they are edited, so
// no property window has to be opened. Pieces keep the fixed inspector; lights,
// security devices, player references, routes and a route being drawn get rows
// built for them. Included inside MapDesignPanel.inl after LightingPanel.inl.
struct SheetField { std::string key,label,value; std::vector<std::string> choices; };
void SheetDestroyControls(DesignState& s)
{
    for(int id=DSheetField;id<DSheetButton+10;++id)
        if(auto control=GetDlgItem(s.window,id))DestroyWindow(control);
}
void SheetPieceControls(DesignState& s,bool show)
{
    for(int id=DName;id<=DDiscard;++id)if(auto control=GetDlgItem(s.window,id))ShowWindow(control,show?SW_SHOW:SW_HIDE);
    for(int id=DInspectorLabel;id<DInspectorLabel+12;++id)if(auto control=GetDlgItem(s.window,id))ShowWindow(control,show?SW_SHOW:SW_HIDE);
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
    int y=s.inspectorTop+24;
    for(size_t i=0;i<fields.size() && i<40;++i)
    {
        const auto& field=fields[i];
        const int id=DSheetField+static_cast<int>(i);
        std::string shown=field.value;
        const bool yesNo=field.choices.size()==2 && field.choices[0]=="Yes" && field.choices[1]=="No";
        if(yesNo)
        {
            auto box=Control(s.window,"BUTTON",field.label,BS_AUTOCHECKBOX,id,12,y,376,22);
            SendMessage(box,BM_SETCHECK,field.value=="Yes"?BST_CHECKED:BST_UNCHECKED,0);
            shown=field.value=="Yes"?"Yes":"No";
        }
        else
        {
            Control(s.window,"STATIC",field.label,0,DSheetLabel+static_cast<int>(i),12,y+4,150,20);
            if(field.choices.empty())
            {
                auto edit=Control(s.window,"EDIT",field.value,ES_AUTOHSCROLL,id,166,y,222,22);
                SetWindowSubclass(edit,DesignFieldProc,id,reinterpret_cast<DWORD_PTR>(&s));
            }
            else
            {
                auto combo=Control(s.window,"COMBOBOX","",CBS_DROPDOWNLIST|WS_VSCROLL,id,166,y,222,220);
                int select=0;
                for(size_t c=0;c<field.choices.size();++c)
                {
                    SendMessageA(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(field.choices[c].c_str()));
                    if(field.choices[c]==field.value)select=static_cast<int>(c);
                }
                SendMessage(combo,CB_SETCURSEL,select,0);
                shown=field.choices[select];
            }
        }
        s.sheet.keys.push_back(field.key);
        s.sheet.shown[field.key]=shown;
        y+=26;
    }
    y+=6;
    for(size_t i=0;i<buttons.size() && i<8;++i)
        Control(s.window,"BUTTON",buttons[i],0,DSheetButton+static_cast<int>(i),12+static_cast<int>(i%2)*196,y+static_cast<int>(i/2)*30,190,26);
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
void SheetShowDevice(DesignState& s,const Json& device)
{
    const auto kind=device.value("kind",std::string());
    if(!Security::Find(kind) && !device.contains("event"))
    {
        SheetHint(s,ObjectiveLabel(device)+": select it to edit its name and settings in the editor's property window.");
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
    else if(kind=="guide")SheetShowGuide(s,subject.value("index",size_t{0}));
    else if(kind=="annotation")SheetShowAnnotation(s,subject.value("index",size_t{0}));
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
