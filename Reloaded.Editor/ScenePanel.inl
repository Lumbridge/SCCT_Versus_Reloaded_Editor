// Scene panel for the Map Design workspace: every piece, brush, light, device
// and game actor in one list, grouped by group, type or floor, with a text
// filter, a visibility checkbox and a lock per row, and groups that select and
// move together. Included inside MapDesignPanel.inl before the canvas procedure.
enum SceneControl
{
    DScnList=1200,DScnFilter,DScnGroupBy,DScnShow,DScnRefresh,DScnHint,DScnNewGroup,DScnSolo,DScnShowAll,DScnUnlockAll,DScnLabel,DScnLabel2,DScnLabel3,DScnHide,
    DScnCtxSelect=1220,DScnCtxFrame,DScnCtxEdit,DScnCtxHide,DScnCtxShow,DScnCtxLock,DScnCtxUnlock,DScnCtxSolo,DScnCtxShowAll,DScnCtxUnlockAll,
    DScnCtxRename,DScnCtxDelete,DScnCtxNewGroup,DScnCtxRemoveGroup,DScnCtxSelectType,DScnCtxSelectGroup,DScnCtxRenameGroup,DScnCtxDeleteGroup,
    DScnCtxCollapseAll,DScnCtxExpandAll,DScnCtxAddGroupFirst=1260,DScnCtxAddGroupLast=1299
};
const char* const kSceneShowNames[]={"All","Selected","Hidden","Locked","Pieces","Brushes","Lights","Security","Game actors","Movers and volumes","Other"};
const char* const kSceneGroupNames[]={"Group","Type","Floor"};
std::string SceneShortName(const std::string& path){return path.substr(path.find_last_of('.')+1);}
// What a loose actor is, in the words the rest of the panel uses.
std::string SceneType(DesignState& s,const Json& actor)
{
    const auto path=actor.at("path").get<std::string>();
    for(const auto& device:s.securityActors)if(device.at("path")==path)return device.value("kind",std::string("Device"));
    for(const auto& objective:s.objectives)if(objective.at("path")==path)return objective.value("kind",std::string("Game actor"));
    for(const auto& light:s.lights)if(light.at("path")==path)return "Light";
    if(actor.value("mover",false))return "Mover";
    if(actor.value("portal",false))return "Portal";
    if(actor.value("volume",false))return "Volume";
    const int csg=actor.value("csg",0);
    // A brush the toolkit named but no piece records any more: its record
    // was lost (a library save that failed, or a map saved at another time).
    const auto name=SceneShortName(path);
    if(name.rfind("Design_",0)==0 && name.find("_Block")!=std::string::npos)return csg==2?"Orphan piece brush (carve)":"Orphan piece brush";
    if(csg==2)return "Brush (carve)";
    if(csg==1)return "Brush (add)";
    if(!actor.at("edges").empty())return "Brush";
    const auto cls=actor.value("class",std::string("Actor"));
    return cls.substr(cls.find_last_of('.')+1);
}
std::string SceneCategory(const SceneRow& row)
{
    if(row.piece)return "Pieces";
    const auto& t=row.type;
    if(t.rfind("Brush",0)==0 || t=="Portal")return "Brushes";
    if(t=="Light")return "Lights";
    for(const char* kind:{"Laser","Camera","Motion sensor","Presence detector","Mine","Alarm"})if(t==kind)return "Security";
    for(const char* kind:{"Mission","Objective","Computer terminal","Bomb target","Objective trigger","Flag","Drop zone","Player start"})if(t==kind)return "Game actors";
    if(t=="Mover" || t=="Volume" || t=="Magic event")return "Movers and volumes";
    return "Other";
}
std::string SceneFloorName(DesignState& s,double z)
{
    const auto& levels=DesignLevels(s);
    for(size_t i=0;i<levels.size();++i)
    {
        const double low=levels[i].base-.5,high=(i+1<levels.size()?levels[i+1].base:levels[i].top+16)-.5;
        if(z>=low && z<high)return "Floor Z "+Design::Round(levels[i].base);
    }
    return "Other heights";
}
// The rows: pieces first (one row for all their brushes), then every other
// actor, filtered, grouped under header rows, and collapsed where asked.
void SceneBuildRows(DesignState& s)
{
    const auto& data=DesignData(s);
    std::map<std::string,std::string> layerOf;
    for(auto& [name,members]:DesignLayers(s,data))for(auto& m:members)layerOf[m.at("path").get<std::string>()]=name;
    std::map<std::string,const Json*> actorAt;
    for(auto& actor:s.scene)actorAt[actor.at("path").get<std::string>()]=&actor;
    std::set<std::string> inPiece;
    std::vector<SceneRow> rows;
    for(const auto& piece:data.at("pieces"))
    {
        SceneRow row;row.piece=true;row.data=piece;
        bool anyShown=false;size_t live=0,lockedCount=0;
        for(auto& member:piece.at("members"))
        {
            const auto path=member.at("path").get<std::string>();
            auto found=actorAt.find(path);if(found==actorAt.end())continue;
            const auto& actor=*found->second;
            ++live;inPiece.insert(path);row.members.push_back(Json{{"path",path},{"class",actor.at("class")}});
            if(!actor.at("hidden").get<bool>())anyShown=true;
            if(actor.at("locked").get<bool>())++lockedCount;
            if(actor.at("selected").get<bool>())row.selected=true;
            if(row.layer.empty() && layerOf.count(path))row.layer=layerOf[path];
        }
        if(live==0)continue;
        row.key="piece:"+piece.at("members")[0].at("path").get<std::string>();
        row.name=piece.at("spec").value("name",std::string("Piece"));
        row.type=piece.at("spec").value("kind",std::string("Piece"));
        row.hidden=!anyShown;row.locked=lockedCount==live;
        row.z=piece.at("position").get<Vector>()[2];
        rows.push_back(row);
    }
    for(auto& actor:s.scene)
    {
        const auto path=actor.at("path").get<std::string>();
        if(inPiece.count(path))continue;
        SceneRow row;row.key="actor:"+path;row.name=SceneShortName(path);row.type=SceneType(s,actor);
        row.members.push_back(Json{{"path",path},{"class",actor.at("class")}});
        row.hidden=actor.at("hidden").get<bool>();row.locked=actor.at("locked").get<bool>();row.selected=actor.at("selected").get<bool>();
        row.z=actor.at("position").get<Vector>()[2];
        if(layerOf.count(path))row.layer=layerOf[path];
        rows.push_back(row);
    }
    const auto filter=Fold(s.sceneFilter);
    std::vector<SceneRow> kept;
    for(auto& row:rows)
    {
        if(!filter.empty() && Fold(row.name+" "+row.type+" "+row.layer).find(filter)==std::string::npos)continue;
        const int show=s.sceneShow;
        if(show==1 && !row.selected)continue;
        if(show==2 && !row.hidden)continue;
        if(show==3 && !row.locked)continue;
        if(show>=4 && SceneCategory(row)!=kSceneShowNames[show])continue;
        row.group=s.sceneGroupBy==0?(row.layer.empty()?"Ungrouped":row.layer):s.sceneGroupBy==1?row.type:SceneFloorName(s,row.z);
        kept.push_back(row);
    }
    std::stable_sort(kept.begin(),kept.end(),[&](const SceneRow& a,const SceneRow& b)
    {
        // Groups by name ("Ungrouped" last), then higher floors first, then name.
        const bool ua=a.group=="Ungrouped",ub=b.group=="Ungrouped";
        if(ua!=ub)return ub;
        if(Fold(a.group)!=Fold(b.group))return s.sceneGroupBy==2?a.z>b.z:Fold(a.group)<Fold(b.group);
        if(std::abs(a.z-b.z)>.5)return a.z>b.z;
        return Fold(a.name)<Fold(b.name);
    });
    s.sceneRows.clear();
    for(size_t i=0;i<kept.size();)
    {
        SceneRow header;header.header=true;header.group=kept[i].group;header.name=kept[i].group;header.key="group:"+kept[i].group;
        header.layer=s.sceneGroupBy==0 && kept[i].group!="Ungrouped"?kept[i].layer:std::string();
        bool allHidden=true,allLocked=true;
        size_t j=i;
        for(;j<kept.size() && kept[j].group==kept[i].group;++j)
        {
            for(auto& m:kept[j].members)header.members.push_back(m);
            if(!kept[j].hidden)allHidden=false;
            if(!kept[j].locked)allLocked=false;
            if(kept[j].selected)header.selected=true;
            header.z=std::max(header.z,kept[j].z);
        }
        header.hidden=allHidden;header.locked=allLocked;
        s.sceneRows.push_back(header);
        if(!s.sceneCollapsed.count(header.group))for(size_t k=i;k<j;++k)s.sceneRows.push_back(kept[k]);
        i=j;
    }
}
void SceneSetText(HWND list,int row,int column,const std::string& value)
{
    std::string copy=value;
    LVITEMA item{};item.iSubItem=column;item.pszText=copy.data();
    SendMessageA(list,LVM_SETITEMTEXTA,row,reinterpret_cast<LPARAM>(&item));
}
// Fills the list; when the rows are the same as before only their text and
// states change, so the scroll position and focus stay put.
void SceneRefreshList(DesignState& s)
{
    if(!s.sceneWindow)return;
    auto list=GetDlgItem(s.sceneWindow,DScnList);
    SceneBuildRows(s);
    std::vector<std::string> keys;
    for(auto& row:s.sceneRows)keys.push_back(row.key);
    const bool same=keys==s.sceneKeys;
    s.sceneSyncing=true;
    SendMessage(list,WM_SETREDRAW,FALSE,0);
    if(!same)SendMessage(list,LVM_DELETEALLITEMS,0,0);
    for(int i=0;i<static_cast<int>(s.sceneRows.size());++i)
    {
        const auto& row=s.sceneRows[i];
        if(!same)
        {
            LVITEMA item{};item.mask=LVIF_TEXT|LVIF_PARAM;item.iItem=i;item.pszText=const_cast<char*>("");item.lParam=i;
            SendMessageA(list,LVM_INSERTITEMA,0,reinterpret_cast<LPARAM>(&item));
        }
        const std::string name=row.header
            ? std::string(s.sceneCollapsed.count(row.group)?"[+] ":"[-] ")+row.name+"  ("+std::to_string(row.members.size())+")"
            : "      "+row.name;
        SceneSetText(list,i,0,name);
        SceneSetText(list,i,1,row.header?"":row.type);
        SceneSetText(list,i,2,row.header?"":row.layer);
        SceneSetText(list,i,3,row.header?"":Design::Round(row.z));
        SceneSetText(list,i,4,row.locked?"LOCKED":"");
        ListView_SetCheckState(list,i,!row.hidden);
        ListView_SetItemState(list,i,row.selected && !row.header?LVIS_SELECTED:0,LVIS_SELECTED);
    }
    s.sceneKeys=keys;
    SendMessage(list,WM_SETREDRAW,TRUE,0);
    InvalidateRect(list,nullptr,TRUE);
    s.sceneSyncing=false;
    size_t entries=0,hidden=0,locked=0,selected=0;
    for(auto& row:s.sceneRows)if(!row.header){++entries;if(row.hidden)++hidden;if(row.locked)++locked;if(row.selected)++selected;}
    SetWindowTextA(GetDlgItem(s.sceneWindow,DScnHint),(std::to_string(entries)+" entries shown, "+std::to_string(selected)+" selected, "+std::to_string(hidden)+" hidden, "+std::to_string(locked)+" locked. "
        "Tick to show, click LOCKED to toggle a lock, click a group header to fold it. Double-click shows an entry in the plan; right-click for actions.").c_str());
}
std::vector<int> SceneSelectedRows(HWND list)
{
    std::vector<int> rows;
    int i=-1;
    while((i=ListView_GetNextItem(list,i,LVNI_SELECTED))!=-1)rows.push_back(i);
    return rows;
}
// The actors the chosen rows stand for, each once.
Json SceneMembersOf(DesignState& s,const std::vector<int>& rows)
{
    Json members=Json::array();
    std::set<std::string> seen;
    for(int r:rows)
    {
        if(r<0 || r>=static_cast<int>(s.sceneRows.size()))continue;
        for(auto& m:s.sceneRows[r].members)
            if(seen.insert(m.at("path").get<std::string>()).second)members.push_back(m);
    }
    return members;
}
Json SceneAllActors(DesignState& s)
{
    Json all=Json::array();
    for(auto& actor:s.scene)all.push_back(Json{{"path",actor.at("path")},{"class",actor.at("class")}});
    return all;
}
void SceneSetFlags(DesignState& s,const Json& members,int hidden,int locked)
{
    if(members.empty())return;
    Editor::DesignSetFlags(members,hidden,locked);
    DesignRefresh(s);
}
// Pans and zooms the plan onto some actors, switching the floor filter to
// their storey when it would hide them.
void SceneFrame(DesignState& s,const Json& members)
{
    Vector lo{},hi{};bool first=true;
    auto add=[&](const Vector& p)
    {
        if(first){lo=hi=p;first=false;}
        else for(int i=0;i<3;++i){lo[i]=std::min(lo[i],p[i]);hi[i]=std::max(hi[i],p[i]);}
    };
    for(auto& actor:s.scene)
    {
        bool wanted=false;
        for(auto& m:members)if(m.at("path")==actor.at("path"))wanted=true;
        if(!wanted)continue;
        add(actor.at("position").get<Vector>());
        for(auto& e:actor.at("edges")){add(e[0].get<Vector>());add(e[1].get<Vector>());}
    }
    if(first)throw std::runtime_error("Those actors are not in the map any more. Refresh the list.");
    if(s.floorFilter && !Design::WithinFloor(s.floorLow,s.floorHigh,lo[2],hi[2]))
    {
        const auto& levels=DesignLevels(s);
        for(size_t i=0;i<levels.size();++i)
        {
            const double low=levels[i].base-.5,high=(i+1<levels.size()?levels[i+1].base:levels[i].top+16)-.5;
            if(lo[2]>=low && lo[2]<high){DesignSetLevel(s,static_cast<int>(levels.size()-i));break;}
        }
    }
    RECT r{};GetClientRect(s.canvas,&r);
    const int a=DesignHorizontal(s),b=DesignVertical(s);
    const double span=std::max({hi[a]-lo[a],hi[b]-lo[b],256.0});
    s.zoom=std::clamp(std::min((r.right-160)/span,(r.bottom-160)/span),.002,4.0);
    s.panX=r.right/2.0-(hi[a]+lo[a])/2*s.zoom;
    s.panY=r.bottom/2.0+(hi[b]+lo[b])/2*s.zoom;
    InvalidateRect(s.canvas,nullptr,FALSE);
}
// --- Groups: the workspace layers, mirrored into the map's Group field.
Json& SceneStoredLayer(DesignState& s,Json& data,const std::string& name)
{
    for(auto& layer:data["layers"])if(Fold(layer.at("name").get<std::string>())==Fold(name))return layer;
    // A group that arrived with the map through its Group field.
    for(auto& [known,members]:DesignLayers(s,data))
        if(Fold(known)==Fold(name))
        {
            data["layers"].push_back({{"name",known},{"members",members},{"hidden",false},{"locked",false}});
            return data["layers"].back();
        }
    throw std::runtime_error("No group called "+name+".");
}
// Takes actors out of whatever group holds them, in the workspace and the map.
void SceneLeaveGroups(DesignState& s,Json& data,const Json& members)
{
    for(auto& layer:data["layers"])
    {
        Json leaving=Json::array();
        auto& stored=layer["members"];
        stored.erase(std::remove_if(stored.begin(),stored.end(),[&](const Json& m)
        {
            for(auto& id:members)if(id.at("path")==m.at("path")){leaving.push_back(m);return true;}
            return false;
        }),stored.end());
        if(!leaving.empty())Editor::DesignGroupMembers(leaving,layer.at("name").get<std::string>(),"remove");
    }
    // Groups only the map knows about.
    std::map<std::string,Json> native;
    for(auto& id:members)
        for(auto& actor:s.scene)
            if(actor.at("path")==id.at("path"))
                for(auto& name:Design::GroupNames(actor.value("group",std::string("None"))))
                {
                    bool stored=false;
                    for(auto& layer:data["layers"])if(Fold(layer.at("name").get<std::string>())==Fold(name))stored=true;
                    if(stored)continue;
                    if(!native.count(name))native[name]=Json::array();
                    native[name].push_back(id);
                }
    for(auto& [name,leaving]:native)Editor::DesignGroupMembers(leaving,name,"remove");
}
void SceneSaveGroups(DesignState& s,Json& data,const std::string& done)
{
    try{DesignSave(s,data);}catch(...){Editor::Exec("TRANSACTION UNDO");throw;}
    DesignRefresh(s);
    DesignStatus(s,done+" Groups are kept in the workspace and in the map's own Group field, so they travel with the .sdc.");
}
void SceneGroupCreate(DesignState& s,const std::string& name,const Json& members)
{
    if(members.empty())throw std::runtime_error("Choose entries first.");
    if(!Design::ValidGroupName(name))throw std::runtime_error("Use 1-62 letters, digits or underscores for a group that travels with the map.");
    Json data=DesignData(s);
    for(auto& [existing,ignored]:DesignLayers(s,data))if(Fold(existing)==Fold(name))throw std::runtime_error("That group name is already used.");
    SceneLeaveGroups(s,data,members);
    data["layers"].push_back({{"name",name},{"members",members},{"hidden",false},{"locked",false}});
    Editor::DesignGroupMembers(members,name,"add");
    SceneSaveGroups(s,data,"Grouped "+std::to_string(members.size())+" actor(s) as "+name+". Clicking any of them in the plan selects the group, and dragging one moves the group.");
}
void SceneGroupAdd(DesignState& s,const std::string& name,const Json& members)
{
    if(members.empty())throw std::runtime_error("Choose entries first.");
    Json data=DesignData(s);
    SceneLeaveGroups(s,data,members);
    auto& layer=SceneStoredLayer(s,data,name);
    for(auto& m:members)layer["members"].push_back(m);
    Editor::DesignGroupMembers(members,layer.at("name").get<std::string>(),"add");
    SceneSaveGroups(s,data,"Added "+std::to_string(members.size())+" actor(s) to "+layer.at("name").get<std::string>()+".");
}
void SceneGroupRemove(DesignState& s,const Json& members)
{
    if(members.empty())throw std::runtime_error("Choose entries first.");
    Json data=DesignData(s);
    SceneLeaveGroups(s,data,members);
    SceneSaveGroups(s,data,"Removed "+std::to_string(members.size())+" actor(s) from their group.");
}
void SceneGroupRename(DesignState& s,const std::string& name,const std::string& next)
{
    if(!Design::ValidGroupName(next))throw std::runtime_error("Use 1-62 letters, digits or underscores for a group that travels with the map.");
    Json data=DesignData(s);
    for(auto& [existing,ignored]:DesignLayers(s,data))if(Fold(existing)==Fold(next) && Fold(existing)!=Fold(name))throw std::runtime_error("That group name is already used.");
    auto& layer=SceneStoredLayer(s,data,name);
    const Json members=layer.at("members");
    Editor::DesignGroupMembers(members,name,"remove");
    Editor::DesignGroupMembers(members,next,"add");
    layer["name"]=next;
    SceneSaveGroups(s,data,"Renamed the group "+name+" to "+next+".");
}
void SceneGroupDelete(DesignState& s,const std::string& name)
{
    Json data=DesignData(s);
    auto& layer=SceneStoredLayer(s,data,name);
    const Json members=layer.at("members");
    Editor::DesignGroupMembers(members,name,"remove");
    auto& layers=data["layers"];
    layers.erase(std::remove_if(layers.begin(),layers.end(),[&](const Json& l){return Fold(l.at("name").get<std::string>())==Fold(name);}),layers.end());
    SceneSaveGroups(s,data,"Ungrouped "+name+": its "+std::to_string(members.size())+" actor(s) stay in the map.");
}
// The group an actor belongs to, and the group's other members, for selecting
// and moving together.
std::string SceneGroupOf(DesignState& s,const std::string& path)
{
    for(auto& [name,members]:DesignLayers(s,DesignData(s)))
        for(auto& m:members)
            if(m.at("path")==path)return name;
    return {};
}
Json SceneGroupMembers(DesignState& s,const std::string& name)
{
    for(auto& [known,members]:DesignLayers(s,DesignData(s)))if(Fold(known)==Fold(name))return members;
    return Json::array();
}
// --- The window.
void SceneRenamePiece(DesignState& s,const SceneRow row)
{
    std::string name=row.name;
    if(!GetName(s.window,"Rename Piece",name))return;
    Json data=DesignData(s);
    for(auto& piece:data["pieces"])if(piece.at("members")==row.data.at("members"))piece["spec"]["name"]=name;
    DesignSave(s,data);
    DesignRefresh(s);
    DesignStatus(s,"Renamed the piece to "+name+".");
}
void SceneDelete(DesignState& s,const Json& members)
{
    if(members.empty())return;
    DesignDeactivate(s);
    Editor::Select(members,false);
    DesignRefresh(s);
    DesignDeleteSelection(s);
}
void SceneAction(DesignState& s,int choice,const std::vector<int>& rows)
{
    auto list=GetDlgItem(s.sceneWindow,DScnList);
    const Json members=SceneMembersOf(s,rows);
    // A copy, not a pointer into s.sceneRows: these actions call DesignRefresh,
    // which rebuilds the rows, so anything pointing into them would dangle.
    const bool one=rows.size()==1 && rows[0]>=0 && rows[0]<static_cast<int>(s.sceneRows.size());
    const SceneRow single=one?s.sceneRows[rows[0]]:SceneRow{};
    // The group the rows concern: a header of one, or members of one.
    std::string group;
    for(int r:rows)
    {
        if(r<0 || r>=static_cast<int>(s.sceneRows.size()))continue;
        const auto& row=s.sceneRows[r];
        const auto own=row.header?row.layer:row.layer;
        if(own.empty())continue;
        if(group.empty())group=own;
        else if(Fold(group)!=Fold(own)){group.clear();break;}
    }
    if(choice==DScnCtxSelect){Editor::Select(members,true);DesignRefresh(s);DesignStatus(s,"Selected "+std::to_string(members.size())+" actor(s) in the editor.");}
    else if(choice==DScnCtxFrame)
    {
        Editor::Select(members,false);
        DesignRefresh(s);
        SceneFrame(s,members);
        if(one && single.piece && (s.pending.is_null() || !s.previous.is_null()))DesignActivate(s,single.data);
        DesignStatus(s,"Showing "+(one?single.name:std::to_string(members.size())+" actors")+" in the plan.");
    }
    else if(choice==DScnCtxEdit && one && single.piece)
    {
        Editor::Select(members,false);
        DesignRefresh(s);
        DesignActivate(s,single.data);
        SceneFrame(s,members);
        DesignWarn(s,"Editing "+single.name+". Drag it, drag a square to resize, use the arrow keys, or edit its fields.");
    }
    else if(choice==DScnCtxHide)SceneSetFlags(s,members,1,-1);
    else if(choice==DScnCtxShow)SceneSetFlags(s,members,0,-1);
    else if(choice==DScnCtxLock){SceneSetFlags(s,members,-1,1);DesignStatus(s,"Locked "+std::to_string(members.size())+" actor(s): they cannot be selected or moved in the plan until unlocked.");}
    else if(choice==DScnCtxUnlock){SceneSetFlags(s,members,-1,0);DesignStatus(s,"Unlocked "+std::to_string(members.size())+" actor(s).");}
    else if(choice==DScnCtxSolo)
    {
        Json others=Json::array();
        for(auto& actor:s.scene)
        {
            bool kept=false;
            for(auto& m:members)if(m.at("path")==actor.at("path"))kept=true;
            if(!kept)others.push_back(Json{{"path",actor.at("path")},{"class",actor.at("class")}});
        }
        Editor::DesignSetFlags(others,1,-1);
        Editor::DesignSetFlags(members,0,-1);
        DesignRefresh(s);
        DesignStatus(s,"Solo: only the chosen "+std::to_string(members.size())+" actor(s) are shown. Show all brings the rest back.");
    }
    else if(choice==DScnCtxShowAll){SceneSetFlags(s,SceneAllActors(s),0,-1);DesignStatus(s,"Everything is shown again.");}
    else if(choice==DScnCtxUnlockAll){SceneSetFlags(s,SceneAllActors(s),-1,0);DesignStatus(s,"Everything is unlocked.");}
    else if(choice==DScnCtxNewGroup)
    {
        std::string name="Group";
        if(!GetName(s.window,"Name Group",name))return;
        SceneGroupCreate(s,name,members);
    }
    else if(choice>=DScnCtxAddGroupFirst && choice<=DScnCtxAddGroupLast)
    {
        const auto layers=DesignLayers(s,DesignData(s));
        const size_t index=choice-DScnCtxAddGroupFirst;
        if(index<layers.size())SceneGroupAdd(s,layers[index].first,members);
    }
    else if(choice==DScnCtxRemoveGroup)SceneGroupRemove(s,members);
    else if(choice==DScnCtxSelectGroup && !group.empty()){Editor::Select(SceneGroupMembers(s,group),true);DesignRefresh(s);DesignStatus(s,"Selected the group "+group+".");}
    else if(choice==DScnCtxRenameGroup && !group.empty())
    {
        std::string name=group;
        if(!GetName(s.window,"Rename Group",name))return;
        SceneGroupRename(s,group,name);
    }
    else if(choice==DScnCtxDeleteGroup && !group.empty())SceneGroupDelete(s,group);
    else if(choice==DScnCtxSelectType && one && !single.header)
    {
        std::vector<int> same;
        for(int i=0;i<static_cast<int>(s.sceneRows.size());++i)if(!s.sceneRows[i].header && s.sceneRows[i].type==single.type)same.push_back(i);
        Editor::Select(SceneMembersOf(s,same),false);
        DesignRefresh(s);
        DesignStatus(s,"Selected every "+single.type+" entry ("+std::to_string(same.size())+").");
    }
    else if(choice==DScnCtxRename && one && single.piece)SceneRenamePiece(s,single);
    else if(choice==DScnCtxDelete)SceneDelete(s,members);
    else if(choice==DScnCtxCollapseAll || choice==DScnCtxExpandAll)
    {
        s.sceneCollapsed.clear();
        if(choice==DScnCtxCollapseAll)for(auto& row:s.sceneRows)if(row.header)s.sceneCollapsed.insert(row.group);
        SceneRefreshList(s);
    }
    (void)list;
}
void SceneContextMenu(DesignState& s,HWND window,POINT screen)
{
    auto list=GetDlgItem(window,DScnList);
    const auto rows=SceneSelectedRows(list);
    if(rows.empty())return;
    bool anyHidden=false,anyShown=false,anyLocked=false,anyUnlocked=false,anyGrouped=false;
    int pieces=0;
    const bool one=rows.size()==1 && rows[0]>=0 && rows[0]<static_cast<int>(s.sceneRows.size());
    const SceneRow single=one?s.sceneRows[rows[0]]:SceneRow{};
    std::string group;bool oneGroup=true;
    for(int r:rows)
    {
        if(r<0 || r>=static_cast<int>(s.sceneRows.size()))continue;
        const auto& row=s.sceneRows[r];
        if(row.hidden)anyHidden=true;else anyShown=true;
        if(row.locked)anyLocked=true;else anyUnlocked=true;
        if(row.piece)++pieces;
        if(!row.layer.empty())anyGrouped=true;
        if(row.layer.empty())oneGroup=false;
        else if(group.empty())group=row.layer;
        else if(Fold(group)!=Fold(row.layer))oneGroup=false;
    }
    if(!oneGroup)group.clear();
    HMENU menu=CreatePopupMenu();
    if(!menu)throw std::runtime_error("Could not open the menu.");
    auto item=[&](int id,const std::string& text){AppendMenuA(menu,MF_STRING,id,text.c_str());};
    auto separator=[&]{AppendMenuA(menu,MF_SEPARATOR,0,nullptr);};
    item(DScnCtxSelect,"Select in the editor");
    item(DScnCtxFrame,"Show in the plan");
    if(one && single.piece)item(DScnCtxEdit,"Edit "+single.name);
    separator();
    if(anyHidden)item(DScnCtxShow,"Show");
    if(anyShown)item(DScnCtxHide,"Hide");
    if(anyUnlocked)item(DScnCtxLock,"Lock");
    if(anyLocked)item(DScnCtxUnlock,"Unlock");
    item(DScnCtxSolo,"Solo: hide everything else");
    item(DScnCtxShowAll,"Show all");
    item(DScnCtxUnlockAll,"Unlock all");
    separator();
    item(DScnCtxNewGroup,"New group from these...");
    const auto layers=DesignLayers(s,DesignData(s));
    if(!layers.empty())
    {
        HMENU addTo=CreatePopupMenu();
        for(size_t i=0;i<layers.size() && i<40;++i)AppendMenuA(addTo,MF_STRING,DScnCtxAddGroupFirst+static_cast<int>(i),layers[i].first.c_str());
        AppendMenuA(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(addTo),"Add to group");
    }
    if(anyGrouped)item(DScnCtxRemoveGroup,"Remove from group");
    if(!group.empty())
    {
        item(DScnCtxSelectGroup,"Select the whole group "+group);
        item(DScnCtxRenameGroup,"Rename group "+group+"...");
        item(DScnCtxDeleteGroup,"Ungroup "+group+" (keep its members)");
    }
    separator();
    if(one && !single.header)item(DScnCtxSelectType,"Select every "+single.type);
    if(one && single.piece)item(DScnCtxRename,"Rename piece...");
    item(DScnCtxDelete,"Delete");
    separator();
    item(DScnCtxCollapseAll,"Collapse all groups");
    item(DScnCtxExpandAll,"Expand all groups");
    const auto choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_LEFTALIGN,screen.x,screen.y,0,window,nullptr);
    DestroyMenu(menu);
    if(choice)SceneAction(s,choice,rows);
}
LRESULT CALLBACK SceneProc(HWND window,UINT message,WPARAM w,LPARAM l)
{
    auto s=reinterpret_cast<DesignState*>(GetWindowLongPtr(window,GWLP_USERDATA));
    if(message==WM_NCCREATE)
    {
        s=static_cast<DesignState*>(reinterpret_cast<CREATESTRUCT*>(l)->lpCreateParams);
        SetWindowLongPtr(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s));
    }
    if(!s)return DefWindowProcA(window,message,w,l);
    try
    {
        if(message==WM_CREATE)
        {
            Control(window,"STATIC","Filter",0,DScnLabel,6,10,40,20);
            Control(window,"EDIT","",ES_AUTOHSCROLL,DScnFilter,48,6,306,24);
            Control(window,"STATIC","Group by",0,DScnLabel2,6,40,56,20);
            Control(window,"COMBOBOX","",CBS_DROPDOWNLIST,DScnGroupBy,62,36,100,200);
            for(const char* name:kSceneGroupNames)SendMessageA(GetDlgItem(window,DScnGroupBy),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));
            SendMessage(GetDlgItem(window,DScnGroupBy),CB_SETCURSEL,s->sceneGroupBy,0);
            Control(window,"STATIC","Show",0,DScnLabel3,170,40,36,20);
            Control(window,"COMBOBOX","",CBS_DROPDOWNLIST|WS_VSCROLL,DScnShow,206,36,148,260);
            for(const char* name:kSceneShowNames)SendMessageA(GetDlgItem(window,DScnShow),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));
            SendMessage(GetDlgItem(window,DScnShow),CB_SETCURSEL,s->sceneShow,0);
            Control(window,"BUTTON","New group...",0,DScnNewGroup,6,66,96,26);
            Control(window,"BUTTON","Solo",0,DScnSolo,106,66,56,26);
            Control(window,"BUTTON","Show all",0,DScnShowAll,166,66,64,26);
            Control(window,"BUTTON","Unlock all",0,DScnUnlockAll,234,66,70,26);
            Control(window,"BUTTON","Hide",0,DScnHide,308,66,46,26);
            auto list=Control(window,"SysListView32","",LVS_REPORT|LVS_SHOWSELALWAYS|WS_BORDER,DScnList,6,98,348,380);
            SendMessage(list,LVM_SETEXTENDEDLISTVIEWSTYLE,0,LVS_EX_CHECKBOXES|LVS_EX_FULLROWSELECT);
            const std::pair<const char*,int> columns[]={{"Name",160},{"Type",84},{"Group",64},{"Z",44},{"Lock",50}};
            for(int i=0;i<5;++i)
            {
                LVCOLUMNA column{};column.mask=LVCF_TEXT|LVCF_WIDTH;column.pszText=const_cast<char*>(columns[i].first);column.cx=columns[i].second;
                SendMessageA(list,LVM_INSERTCOLUMNA,i,reinterpret_cast<LPARAM>(&column));
            }
            Control(window,"STATIC","",0,DScnHint,6,484,348,40);
            s->sceneKeys.clear();
            SceneRefreshList(*s);
            return 0;
        }
        if(message==WM_SIZE)
        {
            MoveWindow(GetDlgItem(window,DScnList),6,98,std::max(1,LOWORD(l)-12),std::max(1,HIWORD(l)-98-48),TRUE);
            MoveWindow(GetDlgItem(window,DScnHint),6,HIWORD(l)-44,std::max(1,LOWORD(l)-12),40,TRUE);
            return 0;
        }
        if(message==WM_COMMAND)
        {
            const int id=LOWORD(w),notification=HIWORD(w);
            if(id==DScnFilter && notification==EN_CHANGE){s->sceneFilter=Text(GetDlgItem(window,DScnFilter));SceneRefreshList(*s);return 0;}
            if(id==DScnGroupBy && notification==CBN_SELCHANGE){s->sceneGroupBy=static_cast<int>(SendMessage(GetDlgItem(window,DScnGroupBy),CB_GETCURSEL,0,0));s->sceneCollapsed.clear();SceneRefreshList(*s);return 0;}
            if(id==DScnShow && notification==CBN_SELCHANGE){s->sceneShow=static_cast<int>(SendMessage(GetDlgItem(window,DScnShow),CB_GETCURSEL,0,0));SceneRefreshList(*s);return 0;}
            if(notification!=BN_CLICKED && notification!=0)return 0;
            auto list=GetDlgItem(window,DScnList);
            if(id==DScnRefresh){DesignRefresh(*s);DesignStatus(*s,"Scene list refreshed.");}
            else if(id==DScnHide){DestroyWindow(window);return 0;}
            else if(id==DScnNewGroup)SceneAction(*s,DScnCtxNewGroup,SceneSelectedRows(list));
            else if(id==DScnSolo)
            {
                const auto rows=SceneSelectedRows(list);
                if(rows.empty())throw std::runtime_error("Choose entries in the list first.");
                SceneAction(*s,DScnCtxSolo,rows);
            }
            else if(id==DScnShowAll)SceneAction(*s,DScnCtxShowAll,{});
            else if(id==DScnUnlockAll)SceneAction(*s,DScnCtxUnlockAll,{});
            return 0;
        }
        if(message==WM_NOTIFY)
        {
            auto header=reinterpret_cast<NMHDR*>(l);
            if(header->idFrom!=DScnList)return 0;
            auto list=header->hwndFrom;
            if(header->code==LVN_ITEMCHANGED)
            {
                if(s->sceneSyncing)return 0;
                auto change=reinterpret_cast<NMLISTVIEW*>(l);
                if(change->iItem<0 || change->iItem>=static_cast<int>(s->sceneRows.size()))return 0;
                const auto row=s->sceneRows[change->iItem];
                const unsigned oldImage=change->uOldState&LVIS_STATEIMAGEMASK,newImage=change->uNewState&LVIS_STATEIMAGEMASK;
                if(oldImage && newImage && oldImage!=newImage)
                {
                    // The checkbox: shown when ticked.
                    const bool shown=ListView_GetCheckState(list,change->iItem)!=0;
                    SceneSetFlags(*s,row.members,shown?0:1,-1);
                    return 0;
                }
                if((change->uNewState^change->uOldState)&LVIS_SELECTED)SetTimer(window,2,150,nullptr);
                return 0;
            }
            if(header->code==NM_CLICK)
            {
                auto activate=reinterpret_cast<NMITEMACTIVATE*>(l);
                LVHITTESTINFO info{};info.pt=activate->ptAction;
                SendMessage(list,LVM_SUBITEMHITTEST,0,reinterpret_cast<LPARAM>(&info));
                if(info.iItem<0 || info.iItem>=static_cast<int>(s->sceneRows.size()) || (info.flags&LVHT_ONITEMSTATEICON))return 0;
                const auto row=s->sceneRows[info.iItem];
                if(info.iSubItem==4)
                {
                    SceneSetFlags(*s,row.members,-1,row.locked?0:1);
                    DesignStatus(*s,(row.locked?"Unlocked ":"Locked ")+row.name+".");
                    return 0;
                }
                if(row.header && info.iSubItem==0)
                {
                    if(s->sceneCollapsed.count(row.group))s->sceneCollapsed.erase(row.group);
                    else s->sceneCollapsed.insert(row.group);
                    SceneRefreshList(*s);
                }
                return 0;
            }
            if(header->code==NM_DBLCLK)
            {
                auto activate=reinterpret_cast<NMITEMACTIVATE*>(l);
                if(activate->iItem<0 || activate->iItem>=static_cast<int>(s->sceneRows.size()))return 0;
                KillTimer(window,2);
                SceneAction(*s,DScnCtxFrame,{activate->iItem});
                return 0;
            }
            if(header->code==NM_RCLICK)
            {
                auto activate=reinterpret_cast<NMITEMACTIVATE*>(l);
                if(activate->iItem>=0 && !(ListView_GetItemState(list,activate->iItem,LVIS_SELECTED)&LVIS_SELECTED))
                {
                    s->sceneSyncing=true;
                    ListView_SetItemState(list,-1,0,LVIS_SELECTED);
                    ListView_SetItemState(list,activate->iItem,LVIS_SELECTED,LVIS_SELECTED);
                    s->sceneSyncing=false;
                }
                KillTimer(window,2);
                POINT screen{};GetCursorPos(&screen);
                SceneContextMenu(*s,window,screen);
                return 0;
            }
            if(header->code==LVN_KEYDOWN)
            {
                auto key=reinterpret_cast<NMLVKEYDOWN*>(l);
                if(key->wVKey==VK_DELETE){KillTimer(window,2);SceneAction(*s,DScnCtxDelete,SceneSelectedRows(list));return 0;}
                if(key->wVKey=='A' && (GetKeyState(VK_CONTROL)&0x8000)){ListView_SetItemState(list,-1,LVIS_SELECTED,LVIS_SELECTED);return 0;}
                if(key->wVKey=='L' && (GetKeyState(VK_CONTROL)&0x8000)){KillTimer(window,2);SceneAction(*s,(GetKeyState(VK_SHIFT)&0x8000)?DScnCtxUnlock:DScnCtxLock,SceneSelectedRows(list));return 0;}
                if(key->wVKey=='G' && (GetKeyState(VK_CONTROL)&0x8000)){KillTimer(window,2);SceneAction(*s,DScnCtxNewGroup,SceneSelectedRows(list));return 0;}
                if(key->wVKey=='H' && (GetKeyState(VK_CONTROL)&0x8000)){KillTimer(window,2);SceneAction(*s,(GetKeyState(VK_SHIFT)&0x8000)?DScnCtxShow:DScnCtxHide,SceneSelectedRows(list));return 0;}
                return 0;
            }
            return 0;
        }
        if(message==WM_TIMER && w==2)
        {
            // Rows chosen in the list become the editor's selection.
            KillTimer(window,2);
            if(s->sceneSyncing)return 0;
            const auto rows=SceneSelectedRows(GetDlgItem(window,DScnList));
            Editor::Select(SceneMembersOf(*s,rows),false);
            DesignRefresh(*s);
            return 0;
        }
        if(message==WM_CLOSE){DestroyWindow(window);return 0;}
        if(message==WM_NCDESTROY)
        {
            s->sceneWindow=nullptr;s->sceneKeys.clear();SetWindowLongPtr(window,GWLP_USERDATA,0);
            DesignRelayout(*s); // The canvas takes the dock's strip back.
            return DefWindowProcA(window,message,w,l);
        }
    }
    catch(const std::exception& e){DesignStatus(*s,e.what());SetWindowTextA(GetDlgItem(window,DScnHint),e.what());}
    return DefWindowProcA(window,message,w,l);
}
void SceneOpen(DesignState& s)
{
    if(s.sceneWindow)
    {
        // The menu entry toggles the dock.
        DestroyWindow(s.sceneWindow);
        DesignStatus(s,"Scene panel hidden. Workspace > Scene panel shows it again.");
        return;
    }
    DesignRefresh(s);
    WNDCLASSA wc{};
    wc.hInstance=GetModuleHandle(nullptr);
    wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
    wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);
    wc.lpfnWndProc=SceneProc;
    wc.lpszClassName="ReloadedScene";
    RegisterClassA(&wc);
    RECT client{};GetClientRect(s.window,&client);
    s.sceneWindow=CreateWindowExA(WS_EX_CONTROLPARENT,wc.lpszClassName,"Scene",WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS|WS_BORDER,
                                  std::max(0L,client.right-12-kSceneDockWidth),12,kSceneDockWidth,std::max(1L,client.bottom-92),s.window,nullptr,wc.hInstance,&s);
    if(!s.sceneWindow)throw std::runtime_error("Cannot open the scene panel.");
    DesignRelayout(s);
    SceneRefreshList(s);
    SetFocus(GetDlgItem(s.sceneWindow,DScnList));
}
