// Zones for the Map Design workspace: a window that lists the match's zones,
// which objectives each one holds, how many must be done, and what happens
// when the zone is complete. Apply writes the whole plan to the map in one
// Undo step through the zone model, as a mission per zone under the map's top
// mission. Included inside MapDesignPanel.inl after SecurityPanel.inl.
enum StageControl
{
    DStgList=1000,DStgAdd,DStgRemove,DStgUp,DStgDown,DStgObjectives,DStgAll,DStgSome,DStgCount,
    DStgActions,DStgOpen,DStgLight,DStgSound,DStgAnnounce,DStgTrigger,DStgRemoveAction,DStgDelay,DStgSetDelay,
    DStgName,DStgBrief,DStgBriefDef,DStgWin,DStgApply,DStgReload,DStgExport,DStgImport,DStgHint,DStgTitle,DStgMerc,DStgSpy,DStgSeconds,
    DStgLabel=1040,DStgMenuFirst=1100,DStgMenuLast=1899
};
Json* StageCurrent(DesignState& s)
{
    if(!s.stagePlan.is_object() || !s.stagePlan.contains("zones"))s.stagePlan=Stages::Empty();
    auto& zones=s.stagePlan["zones"];
    if(zones.empty())return nullptr;
    if(s.stageIndex<0)s.stageIndex=0;
    if(s.stageIndex>=static_cast<int>(zones.size()))s.stageIndex=static_cast<int>(zones.size())-1;
    return &zones[s.stageIndex];
}
int StageIndexOf(DesignState& s,const std::string& path)
{
    if(!s.stagePlan.is_object())return 0;
    int n=0;
    for(const auto& zone:s.stagePlan.value("zones",Json::array()))
    {
        ++n;
        for(const auto& objective:zone.value("objectives",Json::array()))if(objective==path)return n;
    }
    return 0;
}
std::string StageActorLabel(const Json& actor)
{
    const auto name=actor.value("name",std::string()),path=SecurityName(actor);
    return name.empty()?path:name+" ("+path+")";
}
// One row of the zone list.
std::string StageRowText(const Json& zone,int n)
{
    const int total=static_cast<int>(zone.value("objectives",Json::array()).size()),required=zone.value("required",0);
    const auto name=zone.value("name",std::string());
    std::string row="Zone "+std::to_string(n)+(name.empty()?"":": "+name)+" - "+std::to_string(total)+(total==1?" objective":" objectives");
    if(total>1)row+=required?", needs "+std::to_string(required):", needs all";
    row+=", "+std::to_string(zone.value("actions",Json::array()).size())+" action(s)";
    return row;
}
// Rewrites the selected zone's row alone, so typing in its fields does not
// rebuild the window under the caret.
void StageUpdateRow(DesignState& s)
{
    auto* current=StageCurrent(s);
    if(!current || !s.stageWindow)return;
    s.stageSyncing=true;
    auto list=GetDlgItem(s.stageWindow,DStgList);
    const auto row=StageRowText(*current,s.stageIndex+1);
    SendMessage(list,LB_DELETESTRING,s.stageIndex,0);
    SendMessageA(list,LB_INSERTSTRING,s.stageIndex,reinterpret_cast<LPARAM>(row.c_str()));
    SendMessage(list,LB_SETCURSEL,s.stageIndex,0);
    s.stageSyncing=false;
}
void StageStatus(DesignState& s,const std::string& text)
{
    if(s.stageWindow)SetWindowTextA(GetDlgItem(s.stageWindow,DStgHint),text.c_str());
    DesignStatus(s,text);
}
// The window follows the plan: zone rows, the objectives list with the
// current zone's members selected, its threshold, its briefing and its
// actions.
void StageRefreshList(DesignState& s)
{
    if(!s.stageWindow)return;
    s.stageSyncing=true;
    auto* current=StageCurrent(s);
    const auto& zones=s.stagePlan["zones"];
    auto list=GetDlgItem(s.stageWindow,DStgList);
    SendMessage(list,LB_RESETCONTENT,0,0);
    int n=0;
    for(const auto& zone:zones)
    {
        ++n;
        const auto row=StageRowText(zone,n);
        SendMessageA(list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(row.c_str()));
    }
    if(current)SendMessage(list,LB_SETCURSEL,s.stageIndex,0);
    auto objectives=GetDlgItem(s.stageWindow,DStgObjectives);
    SendMessage(objectives,LB_RESETCONTENT,0,0);
    s.stageObjectiveRows.clear();
    for(const auto& actor:s.stageActors)
    {
        if(actor.value("kind",std::string())!="Objective")continue;
        const auto path=actor.at("path").get<std::string>();
        const int zone=StageIndexOf(s,path);
        std::string row=StageActorLabel(actor);
        if(!zone)row+="  - in no zone yet";
        else if(zone!=s.stageIndex+1)row+="  - in zone "+std::to_string(zone);
        if(actor.value("triggers",Json::array()).empty())row+="  - no terminal yet";
        const auto index=SendMessageA(objectives,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(row.c_str()));
        if(current && zone==s.stageIndex+1)SendMessage(objectives,LB_SETSEL,TRUE,index);
        s.stageObjectiveRows.push_back(path);
    }
    if(s.stageObjectiveRows.empty())SendMessageA(objectives,LB_ADDSTRING,0,reinterpret_cast<LPARAM>("No objectives in the map yet: right-click the plan for Mission here and Objective here."));
    const int required=current?current->value("required",0):0;
    CheckDlgButton(s.stageWindow,DStgAll,required==0?BST_CHECKED:BST_UNCHECKED);
    CheckDlgButton(s.stageWindow,DStgSome,required?BST_CHECKED:BST_UNCHECKED);
    SetWindowTextA(GetDlgItem(s.stageWindow,DStgCount),std::to_string(required?required:1).c_str());
    SetWindowTextA(GetDlgItem(s.stageWindow,DStgWin),std::to_string(s.stagePlan.value("required",0)).c_str());
    SetWindowTextA(GetDlgItem(s.stageWindow,DStgLabel+14),
        ("of "+std::to_string(Stages::Objectives(s.stagePlan))+"; 0 = "+std::to_string(Stages::Thresholds(s.stagePlan))).c_str());
    SetWindowTextA(GetDlgItem(s.stageWindow,DStgName),current?current->value("name",std::string()).c_str():"");
    SetWindowTextA(GetDlgItem(s.stageWindow,DStgBrief),current?current->value("brief",std::string()).c_str():"");
    SetWindowTextA(GetDlgItem(s.stageWindow,DStgBriefDef),current?current->value("briefDefend",std::string()).c_str():"");
    auto actions=GetDlgItem(s.stageWindow,DStgActions);
    const auto selectedAction=SendMessage(actions,LB_GETCURSEL,0,0);
    SendMessage(actions,LB_RESETCONTENT,0,0);
    if(current)
    {
        for(const auto& action:current->value("actions",Json::array()))
        {
            const auto row=Stages::Describe(action,s.stageActors);
            SendMessageA(actions,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(row.c_str()));
        }
        if(selectedAction>=0)SendMessage(actions,LB_SETCURSEL,selectedAction,0);
    }
    for(int id:{DStgRemove,DStgUp,DStgDown,DStgObjectives,DStgAll,DStgSome,DStgCount,DStgName,DStgBrief,DStgBriefDef,DStgOpen,DStgLight,DStgSound,DStgAnnounce,DStgTrigger,DStgRemoveAction,DStgSetDelay})
        EnableWindow(GetDlgItem(s.stageWindow,id),current!=nullptr);
    s.stageSyncing=false;
}
// The index of the selected action of the current zone.
int StageSelectedAction(DesignState& s)
{
    auto* current=StageCurrent(s);
    if(!current)return -1;
    const int row=static_cast<int>(SendDlgItemMessage(s.stageWindow,DStgActions,LB_GETCURSEL,0,0));
    return row>=0 && row<static_cast<int>(current->value("actions",Json::array()).size())?row:-1;
}
std::string StageDelay(DesignState& s)
{
    auto text=Text(GetDlgItem(s.stageWindow,DStgDelay));
    if(text.empty())text="0";
    return Magic::Seconds(Magic::Number(text));
}
void StageAddAction(DesignState& s,Json action)
{
    auto* current=StageCurrent(s);
    if(!current)throw std::runtime_error("Add a zone first.");
    if(!action.contains("delay"))action["delay"]=StageDelay(s);
    (*current)["actions"].push_back(action);
    StageRefreshList(s);
    StageStatus(s,"Added: "+Stages::Describe(action,s.stageActors)+". Apply to map writes it.");
}
// A popup of the actors an action can name; the choice becomes the action.
void StageChoose(DesignState& s,int command)
{
    auto* current=StageCurrent(s);
    if(!current)throw std::runtime_error("Add a zone first.");
    s.stageMenu.clear();
    auto menu=CreatePopupMenu();
    if(!menu)throw std::runtime_error("Cannot open the menu.");
    auto add=[&](const std::string& text,Json choice)
    {
        if(s.stageMenu.size()>=DStgMenuLast-DStgMenuFirst)return;
        const UINT flags=MF_STRING|(s.stageMenu.size()%32==31?MF_MENUBARBREAK:0);
        AppendMenuA(menu,flags,DStgMenuFirst+static_cast<UINT>(s.stageMenu.size()),text.c_str());
        s.stageMenu.push_back(std::move(choice));
    };
    std::set<std::string> kinds;
    std::string kind,empty;
    if(command==DStgOpen){kinds={"Mover"};kind="open";empty="No movers in the map. Place a door or lift first.";}
    else if(command==DStgLight){kinds={"Light"};kind="light";empty="No switchable lights in the map. Place one with Light here > switchable light.";}
    else if(command==DStgSound){kinds={"Sound"};kind="sound";empty="No sounds are loaded.";}
    else {kinds={"Magic event","Alarm","Other","Sound","Light","Mover","Objective trigger"};kind="trigger";empty="Nothing in the map can be triggered yet.";}
    for(const auto& wanted:{"Magic event","Alarm","Other","Sound","Light","Mover","Objective trigger"})
    {
        if(!kinds.count(wanted))continue;
        for(const auto& actor:s.stageActors)
        {
            if(actor.value("kind",std::string())!=wanted)continue;
            std::string text=StageActorLabel(actor);
            if(kind=="trigger")text=std::string(wanted)+": "+text;
            if(wanted==std::string("Mover"))text+="  ["+actor.value("state",std::string("None"))+"]";
            if(wanted==std::string("Sound") && !actor.value("sound",std::string()).empty())text+="  ["+actor.value("sound",std::string())+"]";
            Json action={{"kind",kind},{"target",actor.at("path")}};
            if(kind=="open")action["hold"]=true;
            add(text,action);
        }
    }
    if(kind=="sound")
    {
        try
        {
            auto assets=Editor::EventAssets("Sound");
            std::vector<std::string> paths;
            for(const auto& asset:assets)paths.push_back(asset.at("path").get<std::string>());
            std::sort(paths.begin(),paths.end());
            for(const auto& path:paths)add("new sound trigger: "+path,{{"kind","sound"},{"sound","Sound'"+path+"'"}});
        }
        catch(const std::exception&){}
    }
    if(s.stageMenu.empty()){DestroyMenu(menu);throw std::runtime_error(empty);}
    POINT at{};GetCursorPos(&at);
    const int choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_LEFTALIGN|TPM_TOPALIGN,at.x,at.y,0,s.stageWindow,nullptr);
    DestroyMenu(menu);
    if(choice<DStgMenuFirst || choice-DStgMenuFirst>=static_cast<int>(s.stageMenu.size()))return;
    StageAddAction(s,s.stageMenu[choice-DStgMenuFirst]);
}
void StageAnnounce(DesignState& s)
{
    Json action={{"kind","announce"},{"title",Text(GetDlgItem(s.stageWindow,DStgTitle))},{"merc",Text(GetDlgItem(s.stageWindow,DStgMerc))},
        {"spy",Text(GetDlgItem(s.stageWindow,DStgSpy))},{"seconds",Text(GetDlgItem(s.stageWindow,DStgSeconds))}};
    if(action["title"].get<std::string>().empty() && action["merc"].get<std::string>().empty() && action["spy"].get<std::string>().empty())
        throw std::runtime_error("Type the announcement's title or text first.");
    if(action["seconds"].get<std::string>().empty())action["seconds"]="8";
    Json probe=Stages::Empty();Json zone=Stages::NewZone();zone["actions"].push_back(action);probe["zones"].push_back(zone);
    Stages::Validate(probe);
    StageAddAction(s,action);
}
// Objectives ticked in the list belong to the current zone; ticking one that
// is in another zone moves it here.
void StageObjectivesChanged(DesignState& s)
{
    auto* current=StageCurrent(s);
    if(!current || s.stageSyncing)return;
    auto list=GetDlgItem(s.stageWindow,DStgObjectives);
    Json chosen=Json::array();
    int moved=0;
    for(size_t i=0;i<s.stageObjectiveRows.size();++i)
    {
        if(SendMessage(list,LB_GETSEL,i,0)<=0)continue;
        const auto& path=s.stageObjectiveRows[i];
        const int elsewhere=StageIndexOf(s,path);
        if(elsewhere && elsewhere!=s.stageIndex+1)
        {
            auto& other=s.stagePlan["zones"][elsewhere-1];
            Json kept=Json::array();
            for(const auto& objective:other["objectives"])if(objective!=path)kept.push_back(objective);
            other["objectives"]=kept;
            if(other.value("required",0)>static_cast<int>(kept.size()))other["required"]=0;
            ++moved;
        }
        chosen.push_back(path);
    }
    (*current)["objectives"]=chosen;
    if(current->value("required",0)>static_cast<int>(chosen.size()))(*current)["required"]=0;
    StageRefreshList(s);
    int loose=0;
    for(const auto& path:s.stageObjectiveRows)if(!StageIndexOf(s,path))++loose;
    StageStatus(s,"Zone "+std::to_string(s.stageIndex+1)+" has "+std::to_string(chosen.size())+" objective(s)."+(moved?" "+std::to_string(moved)+" moved here from another zone.":"")+
                  (loose?" "+std::to_string(loose)+" objective(s) still in no zone; every objective belongs to one before Apply.":""));
}
void StageThresholdChanged(DesignState& s)
{
    auto* current=StageCurrent(s);
    if(!current || s.stageSyncing)return;
    if(IsDlgButtonChecked(s.stageWindow,DStgAll)==BST_CHECKED){(*current)["required"]=0;StageRefreshList(s);return;}
    const int total=static_cast<int>(current->value("objectives",Json::array()).size());
    int required=0;
    try{required=static_cast<int>(Design::Number(Text(GetDlgItem(s.stageWindow,DStgCount)),1,std::max(1,total)));}
    catch(const std::exception&){required=std::max(1,total);}
    (*current)["required"]=required>=total?0:required;
    StageUpdateRow(s);
}
// How many objectives win the match. 0 means every zone's threshold added up,
// which is what working through all the zones completes; a smaller number ends
// the match before the last zone does.
void StageWinChanged(DesignState& s)
{
    if(s.stageSyncing)return;
    if(!s.stagePlan.is_object() || !s.stagePlan.contains("zones"))return;
    const int total=Stages::Objectives(s.stagePlan);
    int required=0;
    try{required=static_cast<int>(Design::Number(Text(GetDlgItem(s.stageWindow,DStgWin)),0,std::max(0,total)));}
    catch(const std::exception&){required=0;}
    s.stagePlan["required"]=required>=Stages::Thresholds(s.stagePlan)?0:required;
}
// The zone's name and the two teams' briefings.
void StageTextChanged(DesignState& s,int id)
{
    auto* current=StageCurrent(s);
    if(!current || s.stageSyncing)return;
    const char* key=id==DStgName?"name":id==DStgBrief?"brief":"briefDefend";
    (*current)[key]=Text(GetDlgItem(s.stageWindow,id));
    if(id==DStgName)StageUpdateRow(s);
}
void StageApply(DesignState& s)
{
    Stages::Validate(s.stagePlan);
    auto result=Editor::ApplyStages(s.stagePlan);
    DesignRefresh(s);
    std::string text="Applied: "+std::to_string(result.value("creates",size_t{}))+" actor(s) created, "+std::to_string(result.value("updates",size_t{}))+" updated, in one Undo step. Save the map to keep it.";
    for(const auto& note:result.value("notes",Json::array()))text+=" "+note.get<std::string>();
    StageStatus(s,text);
}
void StageFile(DesignState& s,bool exporting)
{
    std::vector<wchar_t> path(32768,L'\0');
    const std::wstring initial=L"zones.json";
    std::copy(initial.begin(),initial.end(),path.begin());
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner=s.stageWindow;ofn.lpstrFile=path.data();ofn.nMaxFile=32768;
    ofn.lpstrTitle=exporting?L"Export Zones":L"Import Zones";
    ofn.lpstrFilter=L"Zones plan\0*.json\0\0";ofn.nFilterIndex=1;ofn.lpstrDefExt=L"json";
    ofn.Flags=OFN_NOCHANGEDIR|(exporting?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
    if(!(exporting?GetSaveFileNameW(&ofn):GetOpenFileNameW(&ofn)))return;
    if(exporting)
    {
        Stages::Validate(s.stagePlan);
        Json document=s.stagePlan;
        document["instructions"]="Zones of a Versus match for the Reloaded Editor's Map Design > Zones window. Each zone becomes an SMission under the map's top mission, holding the objectives named here. Objectives, missions and targets are actor paths in the map this was exported from; import it into that map, or edit the paths.";
        WriteDocument(path.data(),document);
        StageStatus(s,"Zones exported.");
        return;
    }
    auto document=ReadDocument(path.data(),Json::object());
    Stages::Validate(document);
    document.erase("instructions");
    s.stagePlan=document;s.stageIndex=0;
    StageRefreshList(s);
    StageStatus(s,"Zones imported: "+std::to_string(document.at("zones").size())+" zone(s). Apply to map writes them.");
}
// The window is laid out from its own size rather than fixed coordinates: the
// three lists take the room the window has, everything around them keeps its
// height, and the right-hand column stays against the right edge. The numbers
// below are the design layout, so a window at its smallest looks as it always
// did.
constexpr int kStageDesignHeight=614,kStageListHeight=150;
void StageLayout(HWND window,int width,int height)
{
    if(width<=0 || height<=0)return;
    auto place=[&](int id,int x,int y,int w,int h)
    {
        if(auto control=GetDlgItem(window,id))MoveWindow(control,x,y,std::max(1,w),std::max(1,h),TRUE);
    };
    const int margin=12,rightWidth=190,right=std::max(400,width-margin-rightWidth);
    const int leftWidth=220,midX=244,midWidth=std::max(180,right-12-midX);
    // Extra height is shared out: the top lists take two fifths, the actions
    // list the rest, because actions are what a busy zone has most of.
    const int extra=std::max(0,height-kStageDesignHeight);
    const int lists=kStageListHeight+extra*2/5,actions=kStageListHeight+extra-extra*2/5;
    place(DStgLabel,margin,10,200,18);
    place(DStgList,margin,30,leftWidth,lists);
    place(DStgAdd,margin,lists+36,106,26);
    place(DStgRemove,margin+114,lists+36,106,26);
    place(DStgUp,margin,lists+66,106,26);
    place(DStgDown,margin+114,lists+66,106,26);
    place(DStgLabel+1,midX,10,midWidth,18);
    place(DStgObjectives,midX,30,midWidth,lists);
    place(DStgLabel+2,midX,lists+40,180,18);
    place(DStgAll,midX,lists+60,170,22);
    place(DStgSome,midX+176,lists+60,150,22);
    place(DStgCount,midX+330,lists+60,50,22);
    // The announcement column, against the right edge.
    place(DStgLabel+3,right,10,rightWidth,18);
    place(DStgLabel+4,right,32,60,18);
    place(DStgTitle,right+64,30,rightWidth-64,22);
    place(DStgLabel+5,right,58,60,18);
    place(DStgMerc,right+64,56,rightWidth-64,22);
    place(DStgLabel+6,right,84,60,18);
    place(DStgSpy,right+64,82,rightWidth-64,22);
    place(DStgLabel+7,right,110,60,18);
    place(DStgSeconds,right+64,108,50,22);
    place(DStgAnnounce,right,136,rightWidth,26);
    place(DStgLabel+13,right,170,rightWidth,32);
    place(DStgWin,right,204,50,22);
    place(DStgLabel+14,right+56,206,rightWidth-56,20);
    // The zone name and the two briefings share the width of the lists.
    const int fields=std::max(90,(right-12-margin-3*90)/3);
    place(DStgLabel+10,margin,lists+98,70,18);
    place(DStgName,margin+74,lists+96,fields,22);
    const int briefLabel=margin+74+fields+16;
    place(DStgLabel+11,briefLabel,lists+98,80,18);
    place(DStgBrief,briefLabel+84,lists+96,fields,22);
    const int defendLabel=briefLabel+84+fields+16;
    place(DStgLabel+12,defendLabel,lists+98,86,18);
    place(DStgBriefDef,defendLabel+90,lists+96,std::max(120,right-12-defendLabel-90),22);
    place(DStgLabel+8,margin,lists+132,300,18);
    place(DStgActions,margin,lists+152,right-12-margin,actions);
    place(DStgOpen,right,lists+152,rightWidth,26);
    place(DStgLight,right,lists+182,rightWidth,26);
    place(DStgSound,right,lists+212,rightWidth,26);
    place(DStgTrigger,right,lists+242,rightWidth,26);
    place(DStgRemoveAction,right,lists+276,rightWidth,26);
    const int delay=lists+152+actions+8;
    place(DStgLabel+9,margin,delay+2,60,18);
    place(DStgDelay,margin+62,delay,60,22);
    place(DStgSetDelay,margin+132,delay-2,200,26);
    const int apply=delay+36;
    place(DStgApply,margin,apply,150,28);
    place(DStgReload,margin+158,apply,150,28);
    place(DStgExport,margin+316,apply,140,28);
    place(DStgImport,margin+464,apply,140,28);
    place(DStgHint,margin,apply+36,width-2*margin,std::max(34,height-margin-apply-36));
    InvalidateRect(window,nullptr,TRUE);
}
// The zone and action lists paint their own rows: a numbered badge for a zone,
// a coloured dot for what an action does, and the detail in a lighter ink than
// the name, so a long plan can be read down rather than parsed.
Gdiplus::Color StageActionInk(const std::string& kind)
{
    using namespace Gdiplus;
    if(kind=="open")return Color(255,41,109,164);
    if(kind=="light")return Color(255,201,132,12);
    if(kind=="sound")return Color(255,132,72,164);
    if(kind=="announce")return Color(255,34,139,88);
    if(kind=="trigger")return Color(255,192,57,43);
    return Color(255,110,118,130);
}
void StagePaintRow(DesignState& s,DRAWITEMSTRUCT* item)
{
    using namespace Gdiplus;
    if(item->itemID==static_cast<UINT>(-1))return;
    const bool zones=item->CtlID==DStgList;
    const RECT& r=item->rcItem;
    const float x=static_cast<float>(r.left),y=static_cast<float>(r.top);
    const float width=static_cast<float>(r.right-r.left),height=static_cast<float>(r.bottom-r.top);
    const bool selected=(item->itemState&ODS_SELECTED)!=0;
    Graphics g(item->hDC);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    SolidBrush back(selected?Color(255,214,230,247):Color(255,255,255,255));
    g.FillRectangle(&back,x,y,width,height);
    std::string head,tail;
    Color ink(255,41,109,164);
    if(zones)
    {
        const auto& list=s.stagePlan["zones"];
        if(item->itemID>=list.size())return;
        const auto& zone=list[item->itemID];
        const int total=static_cast<int>(zone.value("objectives",Json::array()).size()),required=zone.value("required",0);
        const auto name=zone.value("name",std::string());
        head=name.empty()?"Zone "+std::to_string(item->itemID+1):name;
        tail=std::to_string(total)+(total==1?" objective":" objectives");
        if(total>1)tail+=required?", needs "+std::to_string(required):", needs all";
        tail+="  |  "+std::to_string(zone.value("actions",Json::array()).size())+" action(s)";
    }
    else
    {
        const auto* current=StageCurrent(s);
        if(!current)return;
        const auto actions=current->value("actions",Json::array());
        if(item->itemID>=actions.size())return;
        const auto& action=actions[item->itemID];
        auto kind=action.value("kind",std::string("action"));
        ink=StageActionInk(kind);
        if(!kind.empty())kind[0]=static_cast<char>(std::toupper(static_cast<unsigned char>(kind[0])));
        head=kind;
        tail=Stages::Describe(action,s.stageActors);
    }
    SolidBrush badge(zones?Color(255,41,109,164):ink);
    StringFormat centre;
    centre.SetAlignment(StringAlignmentCenter);
    centre.SetLineAlignment(StringAlignmentCenter);
    if(zones)
    {
        g.FillEllipse(&badge,x+8,y+height/2-9,18.f,18.f);
        Font badgeFont(L"Segoe UI",8.f,FontStyleBold);
        SolidBrush white(Color(255,255,255,255));
        const auto number=std::to_wstring(item->itemID+1);
        g.DrawString(number.c_str(),-1,&badgeFont,RectF(x+8,y+height/2-9,18.f,18.f),&centre,&white);
    }
    else g.FillEllipse(&badge,x+12,y+height/2-5,10.f,10.f);
    Font headFont(L"Segoe UI",9.f,FontStyleBold);
    Font detailFont(L"Segoe UI",9.f);
    SolidBrush dark(Color(255,32,38,48)),lighter(Color(255,108,116,128));
    StringFormat line;
    line.SetTrimming(StringTrimmingEllipsisCharacter);
    line.SetFormatFlags(StringFormatFlagsNoWrap);
    line.SetLineAlignment(StringAlignmentCenter);
    const std::wstring wideHead(head.begin(),head.end()),wideTail(tail.begin(),tail.end());
    RectF measured;
    g.MeasureString(wideHead.c_str(),-1,&headFont,PointF(0,0),&measured);
    const float textX=x+(zones?32:30);
    g.DrawString(wideHead.c_str(),-1,&headFont,RectF(textX,y,std::max(20.f,x+width-10-textX),height),&line,&dark);
    const float tailX=textX+measured.Width+6;
    if(tailX<x+width-24)
        g.DrawString(wideTail.c_str(),-1,&detailFont,RectF(tailX,y,x+width-10-tailX,height),&line,&lighter);
    if(item->itemState&ODS_FOCUS)
    {
        Pen focus(Color(110,41,109,164),1);
        g.DrawRectangle(&focus,x+.5f,y+.5f,width-1,height-1);
    }
}
LRESULT CALLBACK StageProc(HWND window,UINT message,WPARAM w,LPARAM l)
{
    auto* s=reinterpret_cast<DesignState*>(GetWindowLongPtr(window,GWLP_USERDATA));
    if(message==WM_CREATE)
    {
        s=reinterpret_cast<DesignState*>(reinterpret_cast<CREATESTRUCT*>(l)->lpCreateParams);
        SetWindowLongPtr(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s));
        s->stageWindow=window;
    }
    if(!s)return DefWindowProcA(window,message,w,l);
    try
    {
        if(message==WM_CREATE)
        {
            Control(window,"STATIC","Zones, in match order",0,DStgLabel,12,10,200,18);
            Control(window,"LISTBOX","",LBS_NOTIFY|LBS_OWNERDRAWFIXED|LBS_NOINTEGRALHEIGHT|WS_VSCROLL|WS_BORDER,DStgList,12,30,220,150);
            Control(window,"BUTTON","Add zone",0,DStgAdd,12,186,106,26);
            Control(window,"BUTTON","Remove",0,DStgRemove,126,186,106,26);
            Control(window,"BUTTON","Move up",0,DStgUp,12,216,106,26);
            Control(window,"BUTTON","Move down",0,DStgDown,126,216,106,26);
            Control(window,"STATIC","Objectives in this zone (click to tick; Ctrl+click for several)",0,DStgLabel+1,244,10,380,18);
            Control(window,"LISTBOX","",LBS_NOTIFY|LBS_MULTIPLESEL|LBS_NOINTEGRALHEIGHT|WS_VSCROLL|WS_BORDER,DStgObjectives,244,30,380,150);
            Control(window,"STATIC","The zone is complete when",0,DStgLabel+2,244,190,180,18);
            Control(window,"BUTTON","all of them are done",BS_AUTORADIOBUTTON|WS_GROUP,DStgAll,244,210,170,22);
            Control(window,"BUTTON","this many are done:",BS_AUTORADIOBUTTON,DStgSome,420,210,150,22);
            Control(window,"EDIT","1",ES_AUTOHSCROLL|ES_NUMBER,DStgCount,574,210,50,22);
            Control(window,"STATIC","Announcement (both HUDs)",0,DStgLabel+3,636,10,190,18);
            Control(window,"STATIC","Title",0,DStgLabel+4,636,32,60,18);
            Control(window,"EDIT","",ES_AUTOHSCROLL,DStgTitle,700,30,126,22);
            Control(window,"STATIC","Merc text",0,DStgLabel+5,636,58,60,18);
            Control(window,"EDIT","",ES_AUTOHSCROLL,DStgMerc,700,56,126,22);
            Control(window,"STATIC","Spy text",0,DStgLabel+6,636,84,60,18);
            Control(window,"EDIT","",ES_AUTOHSCROLL,DStgSpy,700,82,126,22);
            Control(window,"STATIC","Seconds",0,DStgLabel+7,636,110,60,18);
            Control(window,"EDIT","8",ES_AUTOHSCROLL|ES_NUMBER,DStgSeconds,700,108,50,22);
            Control(window,"BUTTON","Announce on HUD",0,DStgAnnounce,636,136,190,26);
            Control(window,"STATIC","The spies win the match after this many objectives",0,DStgLabel+13,636,170,190,32);
            Control(window,"EDIT","0",ES_AUTOHSCROLL|ES_NUMBER,DStgWin,636,204,50,22);
            Control(window,"STATIC","",0,DStgLabel+14,692,206,134,20);
            Control(window,"STATIC","Zone name",0,DStgLabel+10,12,250,70,18);
            Control(window,"EDIT","",ES_AUTOHSCROLL,DStgName,86,248,180,22);
            Control(window,"STATIC","Spy briefing",0,DStgLabel+11,276,250,80,18);
            Control(window,"EDIT","",ES_AUTOHSCROLL,DStgBrief,360,248,180,22);
            Control(window,"STATIC","Merc briefing",0,DStgLabel+12,550,250,86,18);
            Control(window,"EDIT","",ES_AUTOHSCROLL,DStgBriefDef,640,248,186,22);
            Control(window,"STATIC","When this zone is complete",0,DStgLabel+8,12,282,300,18);
            Control(window,"LISTBOX","",LBS_NOTIFY|LBS_OWNERDRAWFIXED|LBS_NOINTEGRALHEIGHT|WS_VSCROLL|WS_BORDER,DStgActions,12,302,612,150);
            Control(window,"BUTTON","Open door / lift...",0,DStgOpen,636,302,190,26);
            Control(window,"BUTTON","Switch light...",0,DStgLight,636,332,190,26);
            Control(window,"BUTTON","Play sound...",0,DStgSound,636,362,190,26);
            Control(window,"BUTTON","Trigger actor...",0,DStgTrigger,636,392,190,26);
            Control(window,"BUTTON","Remove action",0,DStgRemoveAction,636,426,190,26);
            Control(window,"STATIC","Delay (s)",0,DStgLabel+9,12,462,60,18);
            Control(window,"EDIT","0",ES_AUTOHSCROLL,DStgDelay,74,460,60,22);
            Control(window,"BUTTON","Set delay on selected action",0,DStgSetDelay,144,458,200,26);
            Control(window,"BUTTON","Apply to map",0,DStgApply,12,496,150,28);
            Control(window,"BUTTON","Reload from map",0,DStgReload,170,496,150,28);
            Control(window,"BUTTON","Export JSON...",0,DStgExport,328,496,140,28);
            Control(window,"BUTTON","Import JSON...",0,DStgImport,476,496,140,28);
            Control(window,"STATIC","Each zone becomes an SMission holding its objectives, all playable at once; the map's top mission holds the zones and runs them in order, so nothing has to be locked by hand. A zone's Event fires the actions listed here when it ends. The top mission's own total is how many objectives win the match: 0 means every zone's threshold added up. Every objective in the map belongs to one zone. Apply writes it all in one Undo step. Double-click a zone or an action to select its actors.",0,DStgHint,12,532,814,70);
            StageRefreshList(*s);
            RECT client{};
            GetClientRect(window,&client);
            StageLayout(window,client.right,client.bottom);
            return 0;
        }
        if(message==WM_SIZE){StageLayout(window,LOWORD(l),HIWORD(l));return 0;}
        if(message==WM_GETMINMAXINFO)
        {
            // Below this the three lists have no room left to lose.
            reinterpret_cast<MINMAXINFO*>(l)->ptMinTrackSize={856,660};
            return 0;
        }
        if(message==WM_MEASUREITEM){reinterpret_cast<MEASUREITEMSTRUCT*>(l)->itemHeight=24;return TRUE;}
        if(message==WM_DRAWITEM)
        {
            auto item=reinterpret_cast<DRAWITEMSTRUCT*>(l);
            if((item->CtlID==DStgList || item->CtlID==DStgActions) && item->itemAction!=ODA_FOCUS)StagePaintRow(*s,item);
            return TRUE;
        }
        if(message==WM_CTLCOLORSTATIC)
        {
            // The window is white, so its labels are too.
            SetBkMode(reinterpret_cast<HDC>(w),TRANSPARENT);
            return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
        }
        if(message==WM_COMMAND)
        {
            const int id=LOWORD(w),notification=HIWORD(w);
            if(id==DStgList && notification==LBN_SELCHANGE)
            {
                if(s->stageSyncing)return 0;
                s->stageIndex=static_cast<int>(SendDlgItemMessage(window,DStgList,LB_GETCURSEL,0,0));
                StageRefreshList(*s);
                return 0;
            }
            if(id==DStgList && notification==LBN_DBLCLK)
            {
                auto* current=StageCurrent(*s);
                if(!current)return 0;
                Json chosen=Json::array();
                auto pick=[&](const Json* actor){if(actor)chosen.push_back({{"path",actor->at("path")},{"class",actor->at("class")}});};
                const auto* mission=Stages::ByPath(s->stageActors,current->value("mission",std::string()));
                pick(mission);
                for(const auto& path:current->value("objectives",Json::array()))pick(Stages::ByPath(s->stageActors,path.get<std::string>()));
                if(mission)pick(Stages::ByTag(s->stageActors,mission->value("event",std::string())));
                if(!chosen.empty()){Editor::Select(chosen,true);DesignRefresh(*s);}
                return 0;
            }
            if(id==DStgObjectives && notification==LBN_SELCHANGE){StageObjectivesChanged(*s);return 0;}
            if(id==DStgActions && notification==LBN_DBLCLK)
            {
                const int index=StageSelectedAction(*s);
                if(index<0)return 0;
                const auto& action=(*StageCurrent(*s))["actions"][index];
                if(const auto* actor=Stages::ByPath(s->stageActors,action.value("target",std::string())))
                {
                    Editor::Select(Json::array({{{"path",actor->at("path")},{"class",actor->at("class")}}}),true);
                    DesignRefresh(*s);
                }
                return 0;
            }
            if(id==DStgCount && notification==EN_CHANGE){if(IsDlgButtonChecked(window,DStgSome)==BST_CHECKED)StageThresholdChanged(*s);return 0;}
            if(id==DStgWin && notification==EN_CHANGE){StageWinChanged(*s);return 0;}
            if((id==DStgName || id==DStgBrief || id==DStgBriefDef) && notification==EN_CHANGE){StageTextChanged(*s,id);return 0;}
            if(notification!=BN_CLICKED && notification!=0)return 0;
            if(id==DStgAdd)
            {
                if(!s->stagePlan.is_object())s->stagePlan=Stages::Empty();
                s->stagePlan["zones"].push_back(Stages::NewZone());
                s->stageIndex=static_cast<int>(s->stagePlan["zones"].size())-1;
                StageRefreshList(*s);
                StageStatus(*s,"Zone "+std::to_string(s->stageIndex+1)+" added. Tick the objectives that belong to it, then say what happens when it is complete.");
            }
            else if(id==DStgRemove)
            {
                if(StageCurrent(*s)){s->stagePlan["zones"].erase(s->stageIndex);StageRefreshList(*s);StageStatus(*s,"Zone removed from the plan. Its objectives need a zone before Apply; the mission it used stays in the map for you to delete.");}
            }
            else if(id==DStgUp || id==DStgDown)
            {
                auto& zones=s->stagePlan["zones"];
                const int target=s->stageIndex+(id==DStgUp?-1:1);
                if(StageCurrent(*s) && target>=0 && target<static_cast<int>(zones.size()))
                {
                    std::swap(zones[s->stageIndex],zones[target]);s->stageIndex=target;StageRefreshList(*s);
                }
            }
            else if(id==DStgAll || id==DStgSome)StageThresholdChanged(*s);
            else if(id==DStgOpen || id==DStgLight || id==DStgSound || id==DStgTrigger)StageChoose(*s,id);
            else if(id==DStgAnnounce)StageAnnounce(*s);
            else if(id==DStgRemoveAction)
            {
                const int index=StageSelectedAction(*s);
                if(index<0)throw std::runtime_error("Select one of the zone's actions to remove it.");
                (*StageCurrent(*s))["actions"].erase(index);
                StageRefreshList(*s);
            }
            else if(id==DStgSetDelay)
            {
                const int index=StageSelectedAction(*s);
                if(index<0)throw std::runtime_error("Select one of the zone's actions first.");
                (*StageCurrent(*s))["actions"][index]["delay"]=StageDelay(*s);
                StageRefreshList(*s);
            }
            else if(id==DStgApply)StageApply(*s);
            else if(id==DStgReload)
            {
                DesignRefresh(*s);
                s->stagePlan=Stages::Read(s->stageActors);s->stageIndex=0;
                StageRefreshList(*s);
                StageStatus(*s,"Reloaded "+std::to_string(s->stagePlan["zones"].size())+" zone(s) from the map.");
            }
            else if(id==DStgExport)StageFile(*s,true);
            else if(id==DStgImport)StageFile(*s,false);
            return 0;
        }
        if(message==WM_CLOSE){DestroyWindow(window);return 0;}
        if(message==WM_NCDESTROY){s->stageWindow=nullptr;SetWindowLongPtr(window,GWLP_USERDATA,0);return DefWindowProcA(window,message,w,l);}
    }
    catch(const std::exception& e){StageStatus(*s,e.what());}
    return DefWindowProcA(window,message,w,l);
}
void StageOpen(DesignState& s)
{
    DesignRefresh(s);
    if(!s.stageWindow)
    {
        s.stagePlan=Stages::Read(s.stageActors);
        s.stageIndex=0;
        WNDCLASSA wc{};
        wc.hInstance=GetModuleHandle(nullptr);
        wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
        wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
        wc.lpfnWndProc=StageProc;
        wc.lpszClassName="ReloadedStages";
        RegisterClassA(&wc);
        s.stageWindow=CreateWindowExA(WS_EX_TOOLWINDOW|WS_EX_CONTROLPARENT,wc.lpszClassName,"Zones",WS_OVERLAPPEDWINDOW|WS_VISIBLE,
                                      CW_USEDEFAULT,CW_USEDEFAULT,856,660,s.window,nullptr,wc.hInstance,&s);
        if(!s.stageWindow)throw std::runtime_error("Cannot open the zones window.");
    }
    StageRefreshList(s);
    ShowWindow(s.stageWindow,SW_SHOWNORMAL);
    SetForegroundWindow(s.stageWindow);
    if(s.stagePlan["zones"].empty())StageStatus(s,"No zones yet. Add zone, tick the objectives that belong to it, then say what happens when it is complete. Every objective in the map belongs to one zone.");
}
