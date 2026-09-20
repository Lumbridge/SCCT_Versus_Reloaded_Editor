// Stages for the Map Design workspace: a window that lists the match's
// stages, which objectives each one holds, how many must be done, and what
// happens when the stage is complete. Apply writes the whole plan to the map
// in one Undo step through the stage model. Included inside
// MapDesignPanel.inl after SecurityPanel.inl.
enum StageControl
{
    DStgList=1000,DStgAdd,DStgRemove,DStgUp,DStgDown,DStgObjectives,DStgAll,DStgSome,DStgCount,
    DStgActions,DStgOpen,DStgLight,DStgSound,DStgAnnounce,DStgTrigger,DStgRemoveAction,DStgDelay,DStgSetDelay,
    DStgLock,DStgApply,DStgReload,DStgExport,DStgImport,DStgHint,DStgTitle,DStgMerc,DStgSpy,DStgSeconds,
    DStgLabel=1040,DStgMenuFirst=1100,DStgMenuLast=1899
};
Json* StageCurrent(DesignState& s)
{
    if(!s.stagePlan.is_object() || !s.stagePlan.contains("stages"))s.stagePlan=Stages::Empty();
    auto& stages=s.stagePlan["stages"];
    if(stages.empty())return nullptr;
    if(s.stageIndex<0)s.stageIndex=0;
    if(s.stageIndex>=static_cast<int>(stages.size()))s.stageIndex=static_cast<int>(stages.size())-1;
    return &stages[s.stageIndex];
}
int StageIndexOf(DesignState& s,const std::string& path)
{
    if(!s.stagePlan.is_object())return 0;
    int n=0;
    for(const auto& stage:s.stagePlan.value("stages",Json::array()))
    {
        ++n;
        for(const auto& objective:stage.value("objectives",Json::array()))if(objective==path)return n;
    }
    return 0;
}
std::string StageActorLabel(const Json& actor)
{
    const auto name=actor.value("name",std::string()),path=SecurityName(actor);
    return name.empty()?path:name+" ("+path+")";
}
void StageStatus(DesignState& s,const std::string& text)
{
    if(s.stageWindow)SetWindowTextA(GetDlgItem(s.stageWindow,DStgHint),text.c_str());
    DesignStatus(s,text);
}
// The window follows the plan: stage rows, the objectives list with the
// current stage's members selected, its threshold and its actions.
void StageRefreshList(DesignState& s)
{
    if(!s.stageWindow)return;
    s.stageSyncing=true;
    auto* current=StageCurrent(s);
    const auto& stages=s.stagePlan["stages"];
    auto list=GetDlgItem(s.stageWindow,DStgList);
    SendMessage(list,LB_RESETCONTENT,0,0);
    int n=0;
    for(const auto& stage:stages)
    {
        ++n;
        const int total=static_cast<int>(stage.value("objectives",Json::array()).size()),required=stage.value("required",0);
        std::string row="Stage "+std::to_string(n)+": "+std::to_string(total)+(total==1?" objective":" objectives");
        if(total>1)row+=required?", needs "+std::to_string(required):", needs all";
        row+=", "+std::to_string(stage.value("actions",Json::array()).size())+" action(s)";
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
        const int stage=StageIndexOf(s,path);
        std::string row=StageActorLabel(actor);
        if(stage && stage!=s.stageIndex+1)row+="  - in stage "+std::to_string(stage);
        if(actor.value("triggers",Json::array()).empty())row+="  - no terminal yet";
        const auto index=SendMessageA(objectives,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(row.c_str()));
        if(current && stage==s.stageIndex+1)SendMessage(objectives,LB_SETSEL,TRUE,index);
        s.stageObjectiveRows.push_back(path);
    }
    if(s.stageObjectiveRows.empty())SendMessageA(objectives,LB_ADDSTRING,0,reinterpret_cast<LPARAM>("No objectives in the map yet: right-click the plan for Mission here and Objective here."));
    const int required=current?current->value("required",0):0;
    CheckDlgButton(s.stageWindow,DStgAll,required==0?BST_CHECKED:BST_UNCHECKED);
    CheckDlgButton(s.stageWindow,DStgSome,required?BST_CHECKED:BST_UNCHECKED);
    SetWindowTextA(GetDlgItem(s.stageWindow,DStgCount),std::to_string(required?required:1).c_str());
    auto actions=GetDlgItem(s.stageWindow,DStgActions);
    const auto selectedAction=SendMessage(actions,LB_GETCURSEL,0,0);
    SendMessage(actions,LB_RESETCONTENT,0,0);
    if(current)
    {
        if(s.stagePlan.value("lockLater",true) && s.stageIndex+1<static_cast<int>(stages.size()))
        {
            int terminals=0;
            for(const auto& path:stages[s.stageIndex+1].value("objectives",Json::array()))
                if(const auto* actor=Stages::ByPath(s.stageActors,path.get<std::string>()))terminals+=static_cast<int>(actor->value("triggers",Json::array()).size());
            const std::string row="unlock stage "+std::to_string(s.stageIndex+2)+"'s "+std::to_string(terminals)+" terminal(s) (automatic)";
            SendMessageA(actions,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(row.c_str()));
        }
        for(const auto& action:current->value("actions",Json::array()))
        {
            const auto row=Stages::Describe(action,s.stageActors);
            SendMessageA(actions,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(row.c_str()));
        }
        if(selectedAction>=0)SendMessage(actions,LB_SETCURSEL,selectedAction,0);
    }
    CheckDlgButton(s.stageWindow,DStgLock,s.stagePlan.value("lockLater",true)?BST_CHECKED:BST_UNCHECKED);
    for(int id:{DStgRemove,DStgUp,DStgDown,DStgObjectives,DStgAll,DStgSome,DStgCount,DStgOpen,DStgLight,DStgSound,DStgAnnounce,DStgTrigger,DStgRemoveAction,DStgSetDelay})
        EnableWindow(GetDlgItem(s.stageWindow,id),current!=nullptr);
    s.stageSyncing=false;
}
// The index of the selected author action, past the automatic unlock row.
int StageSelectedAction(DesignState& s)
{
    auto* current=StageCurrent(s);
    if(!current)return -1;
    int row=static_cast<int>(SendDlgItemMessage(s.stageWindow,DStgActions,LB_GETCURSEL,0,0));
    if(row<0)return -1;
    if(s.stagePlan.value("lockLater",true) && s.stageIndex+1<static_cast<int>(s.stagePlan["stages"].size()))--row;
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
    if(!current)throw std::runtime_error("Add a stage first.");
    if(!action.contains("delay"))action["delay"]=StageDelay(s);
    (*current)["actions"].push_back(action);
    StageRefreshList(s);
    StageStatus(s,"Added: "+Stages::Describe(action,s.stageActors)+". Apply to map writes it.");
}
// A popup of the actors an action can name; the choice becomes the action.
void StageChoose(DesignState& s,int command)
{
    auto* current=StageCurrent(s);
    if(!current)throw std::runtime_error("Add a stage first.");
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
    Json probe=Stages::Empty();Json stage=Stages::NewStage();stage["actions"].push_back(action);probe["stages"].push_back(stage);
    Stages::Validate(probe);
    StageAddAction(s,action);
}
// Objectives ticked in the list belong to the current stage; ticking one
// that is in another stage moves it here.
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
            auto& other=s.stagePlan["stages"][elsewhere-1];
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
    StageStatus(s,"Stage "+std::to_string(s.stageIndex+1)+" has "+std::to_string(chosen.size())+" objective(s)."+(moved?" "+std::to_string(moved)+" moved here from another stage.":""));
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
    s.stageSyncing=true;
    SendMessage(GetDlgItem(s.stageWindow,DStgList),LB_DELETESTRING,s.stageIndex,0);
    const int shown=current->value("required",0);
    std::string row="Stage "+std::to_string(s.stageIndex+1)+": "+std::to_string(total)+(total==1?" objective":" objectives")+(total>1?(shown?", needs "+std::to_string(shown):", needs all"):"")+", "+std::to_string(current->value("actions",Json::array()).size())+" action(s)";
    SendMessageA(GetDlgItem(s.stageWindow,DStgList),LB_INSERTSTRING,s.stageIndex,reinterpret_cast<LPARAM>(row.c_str()));
    SendMessage(GetDlgItem(s.stageWindow,DStgList),LB_SETCURSEL,s.stageIndex,0);
    s.stageSyncing=false;
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
    const std::wstring initial=L"stages.json";
    std::copy(initial.begin(),initial.end(),path.begin());
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner=s.stageWindow;ofn.lpstrFile=path.data();ofn.nMaxFile=32768;
    ofn.lpstrTitle=exporting?L"Export Stages":L"Import Stages";
    ofn.lpstrFilter=L"Stages plan\0*.json\0\0";ofn.nFilterIndex=1;ofn.lpstrDefExt=L"json";
    ofn.Flags=OFN_NOCHANGEDIR|(exporting?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
    if(!(exporting?GetSaveFileNameW(&ofn):GetOpenFileNameW(&ofn)))return;
    if(exporting)
    {
        Stages::Validate(s.stagePlan);
        Json document=s.stagePlan;
        document["instructions"]="Stages of a Versus match for the Reloaded Editor's Map Design > Stages window. Objectives and targets are actor paths in the map this was exported from; import it into that map, or edit the paths.";
        WriteDocument(path.data(),document);
        StageStatus(s,"Stages exported.");
        return;
    }
    auto document=ReadDocument(path.data(),Json::object());
    Stages::Validate(document);
    document.erase("instructions");
    s.stagePlan=document;s.stageIndex=0;
    StageRefreshList(s);
    StageStatus(s,"Stages imported: "+std::to_string(document.at("stages").size())+" stage(s). Apply to map writes them.");
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
            Control(window,"STATIC","Stages, in match order",0,DStgLabel,12,10,200,18);
            Control(window,"LISTBOX","",LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|WS_VSCROLL|WS_BORDER,DStgList,12,30,220,150);
            Control(window,"BUTTON","Add stage",0,DStgAdd,12,186,106,26);
            Control(window,"BUTTON","Remove",0,DStgRemove,126,186,106,26);
            Control(window,"BUTTON","Move up",0,DStgUp,12,216,106,26);
            Control(window,"BUTTON","Move down",0,DStgDown,126,216,106,26);
            Control(window,"STATIC","Objectives in this stage (click to tick; Ctrl+click for several)",0,DStgLabel+1,244,10,380,18);
            Control(window,"LISTBOX","",LBS_NOTIFY|LBS_MULTIPLESEL|LBS_NOINTEGRALHEIGHT|WS_VSCROLL|WS_BORDER,DStgObjectives,244,30,380,150);
            Control(window,"STATIC","The stage is complete when",0,DStgLabel+2,244,190,180,18);
            Control(window,"BUTTON","all of them are done",BS_AUTORADIOBUTTON|WS_GROUP,DStgAll,244,210,170,22);
            Control(window,"BUTTON","this many are done:",BS_AUTORADIOBUTTON,DStgSome,420,210,150,22);
            Control(window,"EDIT","1",ES_AUTOHSCROLL|ES_NUMBER,DStgCount,574,210,50,22);
            Control(window,"BUTTON","Lock later stages' terminals until their stage begins",BS_AUTOCHECKBOX|BS_MULTILINE,DStgLock,636,30,190,44);
            Control(window,"STATIC","Announcement (both HUDs)",0,DStgLabel+3,636,84,190,18);
            Control(window,"STATIC","Title",0,DStgLabel+4,636,106,60,18);
            Control(window,"EDIT","",ES_AUTOHSCROLL,DStgTitle,700,104,126,22);
            Control(window,"STATIC","Merc text",0,DStgLabel+5,636,132,60,18);
            Control(window,"EDIT","",ES_AUTOHSCROLL,DStgMerc,700,130,126,22);
            Control(window,"STATIC","Spy text",0,DStgLabel+6,636,158,60,18);
            Control(window,"EDIT","",ES_AUTOHSCROLL,DStgSpy,700,156,126,22);
            Control(window,"STATIC","Seconds",0,DStgLabel+7,636,184,60,18);
            Control(window,"EDIT","8",ES_AUTOHSCROLL|ES_NUMBER,DStgSeconds,700,182,50,22);
            Control(window,"BUTTON","Announce on HUD",0,DStgAnnounce,636,210,190,26);
            Control(window,"STATIC","When this stage is complete",0,DStgLabel+8,12,248,300,18);
            Control(window,"LISTBOX","",LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|WS_VSCROLL|WS_BORDER,DStgActions,12,268,612,150);
            Control(window,"BUTTON","Open door / lift...",0,DStgOpen,636,268,190,26);
            Control(window,"BUTTON","Switch light...",0,DStgLight,636,298,190,26);
            Control(window,"BUTTON","Play sound...",0,DStgSound,636,328,190,26);
            Control(window,"BUTTON","Trigger actor...",0,DStgTrigger,636,358,190,26);
            Control(window,"BUTTON","Remove action",0,DStgRemoveAction,636,392,190,26);
            Control(window,"STATIC","Delay (s)",0,DStgLabel+9,12,428,60,18);
            Control(window,"EDIT","0",ES_AUTOHSCROLL,DStgDelay,74,426,60,22);
            Control(window,"BUTTON","Set delay on selected action",0,DStgSetDelay,144,424,200,26);
            Control(window,"BUTTON","Apply to map",0,DStgApply,12,462,150,28);
            Control(window,"BUTTON","Reload from map",0,DStgReload,170,462,150,28);
            Control(window,"BUTTON","Export JSON...",0,DStgExport,328,462,140,28);
            Control(window,"BUTTON","Import JSON...",0,DStgImport,476,462,140,28);
            Control(window,"STATIC","Each stage's objectives feed a Stage<N>_Gate event that counts completions and fires Stage<N>_Complete, which holds the actions listed here. Apply writes it all in one Undo step. Double-click a stage or an action to select its actors.",0,DStgHint,12,498,814,60);
            StageRefreshList(*s);
            return 0;
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
                for(const auto& path:current->value("objectives",Json::array()))
                    if(const auto* actor=Stages::ByPath(s->stageActors,path.get<std::string>()))chosen.push_back({{"path",actor->at("path")},{"class",actor->at("class")}});
                for(const auto& tag:{Stages::GateTag(s->stageIndex+1),Stages::CompleteTag(s->stageIndex+1)})
                    if(const auto* actor=Stages::ByTag(s->stageActors,tag))chosen.push_back({{"path",actor->at("path")},{"class",actor->at("class")}});
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
            if(notification!=BN_CLICKED && notification!=0)return 0;
            if(id==DStgAdd)
            {
                if(!s->stagePlan.is_object())s->stagePlan=Stages::Empty();
                s->stagePlan["stages"].push_back(Stages::NewStage());
                s->stageIndex=static_cast<int>(s->stagePlan["stages"].size())-1;
                StageRefreshList(*s);
                StageStatus(*s,"Stage "+std::to_string(s->stageIndex+1)+" added. Tick its objectives, then add what happens when it is complete.");
            }
            else if(id==DStgRemove)
            {
                if(StageCurrent(*s)){s->stagePlan["stages"].erase(s->stageIndex);StageRefreshList(*s);StageStatus(*s,"Stage removed. Later stages move up; Apply to map renumbers their events.");}
            }
            else if(id==DStgUp || id==DStgDown)
            {
                auto& stages=s->stagePlan["stages"];
                const int target=s->stageIndex+(id==DStgUp?-1:1);
                if(StageCurrent(*s) && target>=0 && target<static_cast<int>(stages.size()))
                {
                    std::swap(stages[s->stageIndex],stages[target]);s->stageIndex=target;StageRefreshList(*s);
                }
            }
            else if(id==DStgAll || id==DStgSome)StageThresholdChanged(*s);
            else if(id==DStgOpen || id==DStgLight || id==DStgSound || id==DStgTrigger)StageChoose(*s,id);
            else if(id==DStgAnnounce)StageAnnounce(*s);
            else if(id==DStgRemoveAction)
            {
                const int index=StageSelectedAction(*s);
                if(index<0)throw std::runtime_error("Select one of the stage's own actions to remove it; the unlock row is automatic.");
                (*StageCurrent(*s))["actions"].erase(index);
                StageRefreshList(*s);
            }
            else if(id==DStgSetDelay)
            {
                const int index=StageSelectedAction(*s);
                if(index<0)throw std::runtime_error("Select one of the stage's own actions first.");
                (*StageCurrent(*s))["actions"][index]["delay"]=StageDelay(*s);
                StageRefreshList(*s);
            }
            else if(id==DStgLock)
            {
                s->stagePlan["lockLater"]=IsDlgButtonChecked(window,DStgLock)==BST_CHECKED;
                StageRefreshList(*s);
            }
            else if(id==DStgApply)StageApply(*s);
            else if(id==DStgReload)
            {
                DesignRefresh(*s);
                s->stagePlan=Stages::Read(s->stageActors);s->stageIndex=0;
                StageRefreshList(*s);
                StageStatus(*s,"Reloaded "+std::to_string(s->stagePlan["stages"].size())+" stage(s) from the map.");
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
        wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);
        wc.lpfnWndProc=StageProc;
        wc.lpszClassName="ReloadedStages";
        RegisterClassA(&wc);
        s.stageWindow=CreateWindowExA(WS_EX_TOOLWINDOW|WS_EX_CONTROLPARENT,wc.lpszClassName,"Stages",WS_OVERLAPPEDWINDOW|WS_VISIBLE,
                                      CW_USEDEFAULT,CW_USEDEFAULT,856,600,s.window,nullptr,wc.hInstance,&s);
        if(!s.stageWindow)throw std::runtime_error("Cannot open the stages window.");
    }
    StageRefreshList(s);
    ShowWindow(s.stageWindow,SW_SHOWNORMAL);
    SetForegroundWindow(s.stageWindow);
    if(s.stagePlan["stages"].empty())StageStatus(s,"No stages yet. Add stage, tick the objectives that belong to it, then say what happens when it is complete.");
}
