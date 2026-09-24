// Security tools for the Map Design workspace: a window that places lasers,
// cameras, motion sensors, presence detectors, mines and alarms with clicks in
// the plan, wires detectors to an alarm as they are placed, and shows the
// wiring. Included inside MapDesignPanel.inl before the canvas procedure.
enum SecurityControl
{
    DSecList=800,DSecAlarmChoice,DSecLaser,DSecCamera,DSecMotion,DSecPresence,DSecMine,DSecAlarm,
    DSecLinkSelected,DSecOutputs,DSecRefresh,DSecHeight,DSecHint,DSecLabel
};
std::string SecurityName(const Json& actor)
{
    const auto path=actor.at("path").get<std::string>();
    return path.substr(path.find_last_of('.')+1);
}
Json SecurityChosenAlarm(DesignState& s)
{
    for(const auto& actor:s.securityActors)
        if(actor.value("kind",std::string())=="Alarm" && actor.at("path")==s.securityAlarm)return actor;
    return Json{};
}
// The alarm combo lists every alarm in the map; the list shows the wiring.
void SecurityRefreshList(DesignState& s)
{
    if(!s.securityWindow)return;
    auto combo=GetDlgItem(s.securityWindow,DSecAlarmChoice);
    SendMessage(combo,CB_RESETCONTENT,0,0);
    SendMessageA(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>("No alarm (wire later)"));
    int selected=0,index=1;
    for(const auto& actor:s.securityActors)
    {
        if(actor.value("kind",std::string())!="Alarm")continue;
        auto label=actor.value("name",std::string())+" ("+SecurityName(actor)+", Tag "+actor.value("tag",std::string("None"))+")";
        SendMessageA(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
        if(actor.at("path")==s.securityAlarm)selected=index;
        ++index;
    }
    SendMessage(combo,CB_SETCURSEL,selected,0);
    if(selected==0)s.securityAlarm.clear();
    auto list=GetDlgItem(s.securityWindow,DSecList);
    SendMessage(list,LB_RESETCONTENT,0,0);
    s.securityRows=Json::array();
    auto add=[&](const std::string& text,const std::string& path)
    {
        SendMessageA(list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));
        s.securityRows.push_back(path);
    };
    for(const auto& row:Security::Wiring(s.securityActors))
    {
        Json alarm;
        for(const auto& actor:s.securityActors)if(actor.at("path")==row.at("alarm"))alarm=actor;
        add("ALARM "+alarm.value("name",std::string())+"  ["+SecurityName(alarm)+", Tag "+alarm.value("tag",std::string("None"))+", "+Design::Round(alarm.value("duration",60.0))+" s]",row.at("alarm"));
        for(const auto& detector:row.at("detectors"))
        {
            Json actor;for(const auto& candidate:s.securityActors)if(candidate.at("path")==detector)actor=candidate;
            add("    <- "+actor.value("kind",std::string())+" "+SecurityName(actor),detector);
        }
        if(row.at("detectors").empty())add("    <- (no detector wired)","");
        for(const auto& output:row.at("outputs"))
        {
            Json actor;for(const auto& candidate:s.securityActors)if(candidate.at("path")==output)actor=candidate;
            add("    -> triggers "+actor.value("kind",std::string())+" "+SecurityName(actor),output);
        }
        if(row.at("outputs").empty())add("    -> (triggers nothing yet)","");
    }
    for(const auto& actor:s.securityActors)
    {
        const auto kind=actor.value("kind",std::string());
        if(!Security::Detector(kind))continue;
        bool wired=false;
        for(const auto& row:Security::Wiring(s.securityActors))for(const auto& d:row.at("detectors"))if(d==actor.at("path"))wired=true;
        if(!wired)add("UNWIRED "+kind+" "+SecurityName(actor)+(Fold(actor.value("event",std::string("none")))=="none"?"":" (Event "+actor.value("event",std::string())+" reaches nothing)"),actor.at("path"));
    }
    if(s.securityRows.empty())add("No security devices yet. Add one with the buttons above.","");
}
void SecurityStatus(DesignState& s,const std::string& text)
{
    if(s.securityWindow)SetWindowTextA(GetDlgItem(s.securityWindow,DSecHint),text.c_str());
    DesignStatus(s,text);
}
double SecurityHeight(DesignState& s,double fallback)
{
    if(!s.securityWindow)return fallback;
    try{return Design::Number(Text(GetDlgItem(s.securityWindow,DSecHeight)),0,4096);}
    catch(const std::exception&){return fallback;}
}
// Starts a placement: the next click(s) in the design view place the device.
void SecurityBegin(DesignState& s,const std::string& kind)
{
    const auto* device=Security::Find(kind);
    if(!device)throw std::runtime_error("Unknown security device.");
    s.mode="Security";
    s.securityMode=kind;
    s.securityFirstSet=false;
    s.points=Json::array();
    const auto alarm=SecurityChosenAlarm(s);
    std::string wiring=Security::Detector(kind)?(alarm.is_null()?" It will not be wired; choose an alarm above to wire new detectors as they are placed.":" It will be wired to "+alarm.value("name",std::string("the chosen alarm"))+"."):"";
    SecurityStatus(s,std::string(device->hint)+wiring+" Right-click or Escape ends placement.");
    InvalidateRect(s.canvas,nullptr,FALSE);
}
void SecurityWire(DesignState& s,const Json& detector)
{
    const auto alarm=SecurityChosenAlarm(s);
    if(alarm.is_null())return;
    Editor::LinkDetectorToAlarm(detector,alarm);
}
// A click in the plan while placing a device.
void SecurityClick(DesignState& s,const Vector& at)
{
    const auto* device=Security::Find(s.securityMode);
    if(!device){s.mode.clear();return;}
    const auto kind=s.securityMode;
    Vector point=at;
    const double floor=s.depth;
    if(device->clicks==2 && !s.securityFirstSet)
    {
        s.securityFirst=point;
        s.securityFirstSet=true;
        SecurityStatus(s,kind=="Laser"?"Now click where the beam ends.":kind=="Camera"?"Now click a point the camera watches.":"Now click the opposite corner of the area.");
        InvalidateRect(s.canvas,nullptr,FALSE);
        return;
    }
    Json created;
    std::string summary;
    const Json spies="Class'SBase.SPawnAttaque'";
    if(kind=="Laser")
    {
        auto beam=Security::LaserFromClicks(s.securityFirst,point);
        beam.pose.position[2]=floor+SecurityHeight(s,48);
        created=Editor::CreateSecurityActor(device->type,beam.pose,{{"LaserLength",std::to_string(beam.length)},{"DetectedClass",spies}});
        summary="Laser placed, "+std::to_string(beam.length)+" units long.";
    }
    else if(kind=="Camera")
    {
        Pose pose{s.securityFirst,{0,Security::YawTowards(s.securityFirst,point),0}};
        pose.position[2]=floor+SecurityHeight(s,200);
        created=Editor::CreateSecurityActor(device->type,pose,Json::object());
        summary="Camera placed, watching "+Design::Round(std::hypot(point[0]-s.securityFirst[0],point[1]-s.securityFirst[1]))+" units towards the second click.";
    }
    else if(kind=="Motion sensor")
    {
        const auto movement=Design::Movement(DesignData(s));
        auto box=Security::SensorBox(s.securityFirst,point,SecurityHeight(s,movement.at("height").get<double>()));
        box.lo[2]=floor;box.hi[2]=floor+(box.hi[2]-box.lo[2]);
        auto result=Editor::CreateMotionSensor(box.lo,box.hi,{{"DetectedClass",spies}});
        created=result.at("sensor");
        summary="Motion sensor placed over a "+Design::Round(box.hi[0]-box.lo[0])+" x "+Design::Round(box.hi[1]-box.lo[1])+" area.";
    }
    else if(kind=="Presence detector")
    {
        Pose pose{point,{}};pose.position[2]=floor+SecurityHeight(s,48);
        created=Editor::CreateSecurityActor(device->type,pose,{{"DetectedClass",spies},{"CollisionRadius","128.000000"},{"CollisionHeight","64.000000"}});
        summary="Presence detector placed, 128-unit radius.";
    }
    else if(kind=="Mine")
    {
        Pose pose{point,{}};pose.position[2]=floor;
        created=Editor::CreateSecurityActor(device->type,pose,Json::object());
        summary="Mine planted.";
    }
    else if(kind=="Alarm")
    {
        Pose pose{point,{}};pose.position[2]=floor+64;
        created=Editor::CreateSecurityActor(device->type,pose,Json::object());
        s.securityAlarm=created.at("path");
        summary="Alarm placed and chosen for new detectors. Double-click it in the editor to name it and choose what it locks.";
    }
    s.securityFirstSet=false;
    if(Security::Detector(kind))
    {
        try{SecurityWire(s,created);if(!SecurityChosenAlarm(s).is_null())summary+=" Wired to the alarm.";}
        catch(const std::exception& e){summary+=std::string(" Not wired: ")+e.what();}
    }
    DesignRefresh(s);
    SecurityStatus(s,summary+" Click again to place another, or right-click to stop.");
}
// The settings of a device that decide how it plays, read from the actor, and
// the properties they become.
std::vector<InputField> SecurityFields(const Json& device)
{
    const auto kind=device.value("kind",std::string());
    Json values=Json::object();
    try{values=Editor::InspectActor(device).value("values",Json::object());}catch(const std::exception&){}
    auto current=[&](const char* name,const std::string& fallback)
    {
        auto it=values.find(name);
        if(it==values.end())return fallback;
        return it->is_string()?it->get<std::string>():it->dump();
    };
    auto number=[&](const char* name,const char* fallback,int decimals)
    {
        try{return Design::Round(Design::Number(current(name,fallback),-1e9,1e9),decimals);}
        catch(const std::exception&){return std::string(fallback);}
    };
    auto boolean=[&](const char* name,const char* fallback){return Fold(current(name,fallback))=="true"?std::string("Yes"):std::string("No");};
    auto text=[&](const char* name,const char* fallback)
    {
        auto value=current(name,fallback);
        if(value.size()>=2 && value.front()=='"' && value.back()=='"')value=value.substr(1,value.size()-2);
        return value;
    };
    auto detected=[&](const char* fallback)
    {
        const auto value=current("DetectedClass",fallback);
        return value.find("Attaque")!=std::string::npos?std::string("Spies"):value.find("DEFENSE")!=std::string::npos?std::string("Mercs"):std::string("Keep as is");
    };
    const std::vector<std::string> yesNo={"Yes","No"},teams={"Spies","Mercs","Keep as is"};
    if(kind=="Camera")
        return {{"Cone angle (degrees)",number("VisibilityConeAngle","40",1),{}},{"View distance (units)",number("VisibilityMaxDistance","2000",0),{}},
                {"Patrol sweep (degrees, 0 = fixed)",number("PatrolAngle","90",0),{}},{"Turn speed",number("RotationVelocity","5000",0),{}},
                {"Detection delay (seconds)",number("DetectionDelay","2",2),{}},{"Can be destroyed",boolean("bDamageable","False"),yesNo}};
    if(kind=="Laser")
        return {{"Beam length (units)",number("LaserLength","300",0),{}},{"Detects",detected("Class'SBase.SPawnAttaque'"),teams},
                {"Hidden in normal vision",boolean("bHideLaserInNormalVision","True"),yesNo},{"Chance to trigger (%)",number("SpawnPerCent","100",0),{}},
                {"Triggers once only",boolean("bTriggerOnceOnly","False"),yesNo}};
    if(kind=="Motion sensor")
        return {{"Detection delay (seconds)",number("DetectionDelay","1",2),{}},{"Minimum speed to detect",number("MinDetectedVelocity","150",0),{}},
                {"Detects",detected("Class'SBase.SPawnAttaque'"),teams},{"Triggers once only",boolean("bTriggerOnceOnly","False"),yesNo}};
    if(kind=="Presence detector")
        return {{"Radius (units)",number("CollisionRadius","128",0),{}},{"Height (units)",number("CollisionHeight","64",0),{}},
                {"Detects",detected("Class'SBase.SPAWNDEFENSE'"),teams},{"Untriggers when they leave",boolean("bUntrigger","False"),yesNo},
                {"Detects grabbed bodies",boolean("bDetectGrab","True"),yesNo}};
    if(kind=="Mine")
        return {{"Arming delay",number("TempsActivation","0",0),{}},{"Interaction radius (units)",number("fInteractionRadius","90",0),{}},
                {"Can be destroyed",boolean("bDamageable","True"),yesNo},{"Hit points",number("HitPoints","0",0),{}}};
    if(kind=="Alarm")
        return {{"Name",text("AlarmName","\"Unnamed alarm\""),{}},{"Message to mercs",text("AlarmDescription","\"Unnamed alarm is active\""),{}},
                {"Message to spies",text("AlarmDescriptionSpy","\"Unnamed alarm activated\""),{}},{"Duration (seconds)",number("Duration","60",0),{}},
                {"Message priority",number("MessagePriority","5",0),{}}};
    return {};
}
Json SecurityFieldsToProperties(const std::string& kind,const std::vector<std::string>& f)
{
    Json props=Json::object();
    auto num=[&](const char* name,const std::string& value,double lo,double hi,int decimals){props[name]=Design::Round(Design::Number(value,lo,hi),decimals);};
    auto yes=[&](const char* name,const std::string& value){props[name]=value=="Yes"?"True":"False";};
    auto team=[&](const std::string& value)
    {
        if(value=="Spies")props["DetectedClass"]="Class'SBase.SPawnAttaque'";
        else if(value=="Mercs")props["DetectedClass"]="Class'SBase.SPAWNDEFENSE'";
    };
    auto quoted=[&](const char* name,const std::string& value){props[name]="\""+value+"\"";};
    auto need=[&](size_t count){if(f.size()<count)throw std::runtime_error("Incomplete device settings.");};
    if(kind=="Camera")
    {
        need(6);
        num("VisibilityConeAngle",f[0],1,179,1);num("VisibilityMaxDistance",f[1],16,65536,0);
        num("PatrolAngle",f[2],0,360,0);num("RotationVelocity",f[3],0,1000000,0);
        num("DetectionDelay",f[4],0,600,2);yes("bDamageable",f[5]);
    }
    else if(kind=="Laser")
    {
        need(5);
        num("LaserLength",f[0],8,65536,0);team(f[1]);yes("bHideLaserInNormalVision",f[2]);
        num("SpawnPerCent",f[3],0,100,0);yes("bTriggerOnceOnly",f[4]);
    }
    else if(kind=="Motion sensor")
    {
        need(4);
        num("DetectionDelay",f[0],0,600,2);num("MinDetectedVelocity",f[1],0,100000,0);
        team(f[2]);yes("bTriggerOnceOnly",f[3]);
    }
    else if(kind=="Presence detector")
    {
        need(5);
        num("CollisionRadius",f[0],1,10000,0);num("CollisionHeight",f[1],1,10000,0);
        team(f[2]);yes("bUntrigger",f[3]);yes("bDetectGrab",f[4]);
    }
    else if(kind=="Mine")
    {
        need(4);
        num("TempsActivation",f[0],0,100000,0);num("fInteractionRadius",f[1],0,10000,0);
        yes("bDamageable",f[2]);num("HitPoints",f[3],0,1000000,0);
    }
    else if(kind=="Alarm")
    {
        need(5);
        quoted("AlarmName",f[0]);quoted("AlarmDescription",f[1]);quoted("AlarmDescriptionSpy",f[2]);
        num("Duration",f[3],0,100000,0);num("MessagePriority",f[4],0,100,0);
    }
    return props;
}
void SheetShowDevice(DesignState& s,const Json& device);
void SecurityEditForm(DesignState& s,const Json& device)
{
    if(SecurityFields(device).empty())throw std::runtime_error("This actor has no quick settings here; use the editor's property window.");
    SheetShowDevice(s,device);
}
// Puts a wall-mounted device on the nearest wall of a placed piece, flush with
// the inner face and facing into the room: a laser then spans the room, a camera looks
// across it, a motion sensor sits on the wall.
void SecuritySnapToWall(DesignState& s,const Json& device)
{
    const auto kind=device.value("kind",std::string());
    const Vector position=device.at("position");
    const auto& data=DesignData(s);
    double best=1e18,bestSpan=0;
    Pose bestPose{};
    std::string bestWall,bestName;
    for(const auto& piece:data.at("pieces"))
    {
        const auto pieceKind=piece.at("spec").at("kind").get<std::string>();
        if(pieceKind!="Room" && pieceKind!="Corridor" && !Design::Crouching(pieceKind))continue;
        bool live=false;
        for(auto& member:piece.at("members"))
            for(auto& actor:s.scene)
                if(actor.at("path")==member.at("path"))live=true;
        if(!live)continue;
        const Pose pose{piece.at("position").get<Vector>(),piece.at("rotation").get<Rotation>()};
        const double w=piece.at("spec").at("width"),l=piece.at("spec").at("length");
        const auto local=TransformPoint(position,pose,true);
        const std::pair<const char*,double> walls[]={{"+Y",l/2},{"-Y",-l/2},{"+X",w/2},{"-X",-w/2}};
        for(const auto& [wall,at]:walls)
        {
            const bool alongX=wall[1]=='Y';
            const double across=alongX?local[1]:local[0],along=alongX?local[0]:local[1];
            const double span=alongX?w:l;
            if(std::abs(along)>span/2+64)continue;
            const double distance=std::abs(across-at);
            if(distance>=best)continue;
            best=distance;
            const double sign=wall[0]=='+'?1:-1;
            Vector target{0,0,local[2]},inward{0,0,local[2]};
            // Flush on the inner face, so the device sits on the wall itself.
            if(alongX){target[0]=std::clamp(along,-span/2+8,span/2-8);target[1]=at;}
            else{target[1]=std::clamp(along,-span/2+8,span/2-8);target[0]=at;}
            inward=target;
            if(alongX)inward[1]-=sign*64;else inward[0]-=sign*64;
            bestPose.position=TransformPoint(target,pose);
            bestPose.position[2]=position[2];
            const auto facing=TransformPoint(inward,pose);
            bestPose.rotation=device.at("rotation").get<Rotation>();
            if(kind!="Motion sensor")bestPose.rotation[1]=Security::YawTowards(bestPose.position,{facing[0],facing[1],bestPose.position[2]});
            bestSpan=alongX?l:w;
            bestWall=wall;
            bestName=piece.at("spec").value("name",pieceKind);
        }
    }
    if(best>=1e18)throw std::runtime_error("No placed room, corridor or vent near this device: snapping uses the pieces in the plan.");
    Json properties=Json::object();
    if(kind=="Laser")properties["LaserLength"]=std::to_string(static_cast<int>(std::max(8.0,bestSpan)));
    Editor::MoveSecurityActor(device,bestPose,properties);
    DesignRefresh(s);
    DesignStatus(s,SecurityName(device)+" snapped to the "+bestWall+" wall of "+bestName+(kind=="Motion sensor"?".":", facing into it.")
        +(kind=="Laser"?" The beam spans the "+Design::Round(bestSpan)+" units to the far wall.":"")+" One Undo step.");
}
// Wiring buttons: selected detectors to the chosen alarm, and the chosen alarm
// to whatever is selected.
void SecurityLinkSelected(DesignState& s)
{
    const auto alarm=SecurityChosenAlarm(s);
    if(alarm.is_null())throw std::runtime_error("Choose an alarm in the list first.");
    int wired=0;
    for(const auto& actor:s.securityActors)
        if(actor.value("selected",false) && Security::Detector(actor.value("kind",std::string())))
        {
            Editor::LinkDetectorToAlarm(actor,alarm);
            ++wired;
        }
    if(!wired)throw std::runtime_error("Select one or more detectors (lasers, cameras, sensors, mines) first.");
    DesignRefresh(s);
    SecurityStatus(s,std::to_string(wired)+" detector(s) now trigger "+alarm.value("name",std::string("the alarm"))+".");
}
void SecurityOutputs(DesignState& s)
{
    const auto alarm=SecurityChosenAlarm(s);
    if(alarm.is_null())throw std::runtime_error("Choose an alarm in the list first.");
    Json targets=Json::array();
    for(const auto& actor:Editor::SelectedIdentities())if(actor.at("path")!=alarm.at("path"))targets.push_back(actor);
    Editor::AddAlarmOutputs(alarm,targets);
    DesignRefresh(s);
    SecurityStatus(s,alarm.value("name",std::string("The alarm"))+" now triggers "+std::to_string(targets.size())+" selected actor(s): lights, doors, events or cameras.");
}
LRESULT CALLBACK SecurityProc(HWND window,UINT message,WPARAM w,LPARAM l)
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
            const std::pair<int,const char*> buttons[]={{DSecLaser,"Add laser"},{DSecCamera,"Add camera"},{DSecMotion,"Add motion sensor"},
                {DSecPresence,"Add presence detector"},{DSecMine,"Add mine"},{DSecAlarm,"Add alarm"}};
            for(int i=0;i<6;++i)Control(window,"BUTTON",buttons[i].second,0,buttons[i].first,12+(i%3)*190,12+(i/3)*30,184,26);
            Control(window,"STATIC","Wire new detectors to",0,DSecLabel,12,78,150,20);
            Control(window,"COMBOBOX","",CBS_DROPDOWNLIST|WS_VSCROLL,DSecAlarmChoice,166,74,300,200);
            Control(window,"STATIC","Height above floor",0,DSecLabel+1,476,78,110,20);
            Control(window,"EDIT","48",ES_AUTOHSCROLL,DSecHeight,586,74,60,22);
            Control(window,"BUTTON","Wire selected detectors to alarm",0,DSecLinkSelected,12,106,240,26);
            Control(window,"BUTTON","Alarm triggers selected actors",0,DSecOutputs,258,106,240,26);
            Control(window,"BUTTON","Refresh",0,DSecRefresh,504,106,120,26);
            Control(window,"STATIC","Lasers, cameras and sensors send their Event to an alarm's Tag; the alarm's Events list triggers lights, doors and events. Double-click a row to select it.",0,DSecHint,12,140,640,36);
            Control(window,"LISTBOX","",LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|WS_VSCROLL|WS_BORDER,DSecList,12,180,640,240);
            SecurityRefreshList(*s);
            return 0;
        }
        if(message==WM_SIZE)
        {
            MoveWindow(GetDlgItem(window,DSecList),12,180,std::max(1,LOWORD(l)-24),std::max(1,HIWORD(l)-192),TRUE);
            return 0;
        }
        if(message==WM_COMMAND)
        {
            const int id=LOWORD(w),notification=HIWORD(w);
            if(id==DSecAlarmChoice && notification==CBN_SELCHANGE)
            {
                const auto index=SendDlgItemMessage(window,DSecAlarmChoice,CB_GETCURSEL,0,0);
                s->securityAlarm.clear();
                int seen=0;
                for(const auto& actor:s->securityActors)
                    if(actor.value("kind",std::string())=="Alarm" && ++seen==index)s->securityAlarm=actor.at("path");
                return 0;
            }
            if(id==DSecList && notification==LBN_DBLCLK)
            {
                const auto row=SendDlgItemMessage(window,DSecList,LB_GETCURSEL,0,0);
                if(row<0 || row>=static_cast<LRESULT>(s->securityRows.size()))return 0;
                const auto path=s->securityRows[row].get<std::string>();
                if(path.empty())return 0;
                for(const auto& actor:s->securityActors)
                    if(actor.at("path")==path){Editor::Select(Json::array({actor}),true);DesignRefresh(*s);}
                return 0;
            }
            if(notification!=BN_CLICKED && notification!=0)return 0;
            if(id==DSecLaser)SecurityBegin(*s,"Laser");
            else if(id==DSecCamera)SecurityBegin(*s,"Camera");
            else if(id==DSecMotion)SecurityBegin(*s,"Motion sensor");
            else if(id==DSecPresence)SecurityBegin(*s,"Presence detector");
            else if(id==DSecMine)SecurityBegin(*s,"Mine");
            else if(id==DSecAlarm)SecurityBegin(*s,"Alarm");
            else if(id==DSecLinkSelected)SecurityLinkSelected(*s);
            else if(id==DSecOutputs)SecurityOutputs(*s);
            else if(id==DSecRefresh){DesignRefresh(*s);SecurityStatus(*s,"Security devices refreshed.");}
            return 0;
        }
        if(message==WM_CLOSE){DestroyWindow(window);return 0;}
        if(message==WM_NCDESTROY){s->securityWindow=nullptr;SetWindowLongPtr(window,GWLP_USERDATA,0);return DefWindowProcA(window,message,w,l);}
    }
    catch(const std::exception& e){SecurityStatus(*s,e.what());}
    return DefWindowProcA(window,message,w,l);
}
void SecurityOpen(DesignState& s)
{
    DesignRefresh(s);
    if(!s.securityWindow)
    {
        WNDCLASSA wc{};
        wc.hInstance=GetModuleHandle(nullptr);
        wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
        wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);
        wc.lpfnWndProc=SecurityProc;
        wc.lpszClassName="ReloadedSecurity";
        RegisterClassA(&wc);
        s.securityWindow=CreateWindowExA(WS_EX_TOOLWINDOW|WS_EX_CONTROLPARENT,wc.lpszClassName,"Security",WS_OVERLAPPEDWINDOW|WS_VISIBLE,
                                         CW_USEDEFAULT,CW_USEDEFAULT,680,470,s.window,nullptr,wc.hInstance,&s);
        if(!s.securityWindow)throw std::runtime_error("Cannot open the security tools.");
    }
    SecurityRefreshList(s);
    ShowWindow(s.securityWindow,SW_SHOWNORMAL);
    SetForegroundWindow(s.securityWindow);
}
// Where a device's aim handle sits on the plan: a laser's beam end, or a point
// in front of a camera. Only devices with a direction have one.
bool SecurityAimHandle(DesignState& s,const Json& actor,const Vector& position,const Rotation& rotation,double length,Gdiplus::PointF& handle)
{
    const auto kind=actor.value("kind",std::string());
    if(kind!="Laser" && kind!="Camera")return false;
    if(kind=="Laser"){handle=DesignScreen(s,Security::BeamEnd(position,rotation,length));return true;}
    handle=DesignScreen(s,Security::BeamEnd(position,rotation,std::max(48.0,64/s.zoom)));
    return true;
}
// The device body or aim handle under a point in the view.
bool SecurityHandleAt(DesignState& s,double x,double y,Json& device,bool& aim)
{
    double best=10;
    device=Json{};
    for(const auto& actor:s.securityActors)
    {
        if(!Security::Find(actor.value("kind",std::string())))continue;
        const Vector position=actor.at("position");
        if(!DesignOnFloor(s,position[2],position[2]))continue;
        Gdiplus::PointF handle;
        if(SecurityAimHandle(s,actor,position,actor.at("rotation").get<Rotation>(),actor.value("length",300.0),handle))
        {
            const double distance=DesignPointDistance(handle,x,y);
            if(distance<best){best=distance;device=actor;aim=true;}
        }
        const double distance=DesignPointDistance(DesignScreen(s,position),x,y);
        if(distance<best){best=distance;device=actor;aim=false;}
    }
    return !device.is_null();
}
// Devices and their wiring on the plan.
void SecurityPaint(DesignState& s,Gdiplus::Graphics& g,const std::function<void(const std::string&,Gdiplus::PointF)>& label)
{
    using namespace Gdiplus;
    const bool dragging=(s.drag.kind==DesignDrag::Kind::Device || s.drag.kind==DesignDrag::Kind::DeviceAim) && s.drag.moved;
    auto line=[&](Pen& pen,const Vector& a,const Vector& b){g.DrawLine(&pen,DesignScreen(s,a),DesignScreen(s,b));};
    std::map<std::string,Vector> alarms;
    for(const auto& actor:s.securityActors)
        if(actor.value("kind",std::string())=="Alarm")alarms[Fold(actor.value("tag",std::string()))]=actor.at("position").get<Vector>();
    Pen wire(Color(120,200,60,60),1);
    wire.SetDashStyle(DashStyleDot);
    for(const auto& stored:s.securityActors)
    {
        Json actor=stored;
        if(!DesignTypeShown(s,actor.at("path").get<std::string>()))continue;
        // A device being dragged is drawn where the cursor has it.
        if(dragging && actor.at("path")==s.drag.device.at("path"))
        {
            actor["position"]=s.drag.devicePose.position;
            actor["rotation"]=s.drag.devicePose.rotation;
            if(actor.value("kind",std::string())=="Laser")actor["length"]=s.drag.deviceLength;
        }
        const auto kind=actor.value("kind",std::string());
        const Vector position=actor.at("position");
        if(!DesignOnFloor(s,position[2],position[2]))continue;
        const auto at=DesignScreen(s,position);
        const bool selected=actor.value("selected",false);
        Gdiplus::PointF handle;
        if(SecurityAimHandle(s,actor,position,actor.at("rotation").get<Rotation>(),actor.value("length",300.0),handle))
        {
            // The aim handle: drag it to turn the device (and stretch a laser).
            Pen ring(Color(230,40,40,40),1.5f);
            SolidBrush fill(Color(230,255,255,255));
            g.FillEllipse(&fill,handle.X-5,handle.Y-5,10.f,10.f);
            g.DrawEllipse(&ring,handle.X-5,handle.Y-5,10.f,10.f);
        }
        if(kind=="Laser")
        {
            Pen beam(selected?Color(255,255,60,60):Color(200,220,30,30),2);
            const auto end=Security::BeamEnd(position,actor.at("rotation").get<Rotation>(),actor.value("length",300.0));
            line(beam,position,end);
            SolidBrush base(Color(220,220,30,30));
            g.FillRectangle(&base,at.X-4,at.Y-4,8.f,8.f);
            label("laser "+Design::Round(actor.value("length",300.0)),{at.X+6,at.Y-18});
        }
        else if(kind=="Camera")
        {
            // The view cone, projected onto this view: yaw turns it in the top
            // view, pitch tilts it in the front and side views. A camera looking
            // straight into the view has no cone to draw.
            const double cone=std::clamp(actor.value("cone",40.0),5.0,170.0);
            const float reach=static_cast<float>(actor.value("reach",2000.0)*s.zoom);
            const auto direction=Security::BeamEnd({0,0,0},actor.at("rotation").get<Rotation>(),1.0);
            const double dh=direction[DesignHorizontal(s)],dv=direction[DesignVertical(s)];
            if(std::hypot(dh,dv)>0.05)
            {
                const double angle=std::atan2(-dv,dh)*180/3.14159265358979323846;
                SolidBrush view(Color(selected?80:45,240,140,40));
                g.FillPie(&view,at.X-reach,at.Y-reach,2*reach,2*reach,static_cast<float>(angle-cone/2),static_cast<float>(cone));
            }
            SolidBrush body(Color(230,200,110,30));
            g.FillEllipse(&body,at.X-5,at.Y-5,10.f,10.f);
            label("security camera",{at.X+6,at.Y-18});
        }
        else if(kind=="Presence detector" || kind=="Mine")
        {
            const float radius=static_cast<float>(actor.value("radius",90.0)*s.zoom);
            Pen ring(kind=="Mine"?Color(200,200,40,40):Color(200,60,120,220),1);
            ring.SetDashStyle(DashStyleDash);
            g.DrawEllipse(&ring,at.X-radius,at.Y-radius,2*radius,2*radius);
            SolidBrush body(kind=="Mine"?Color(240,200,40,40):Color(240,60,120,220));
            if(kind=="Mine")
            {
                PointF diamond[4]={{at.X,at.Y-6},{at.X+6,at.Y},{at.X,at.Y+6},{at.X-6,at.Y}};
                g.FillPolygon(&body,diamond,4);
            }
            else g.FillEllipse(&body,at.X-5,at.Y-5,10.f,10.f);
            label(kind=="Mine"?"mine":"presence detector",{at.X+8,at.Y-18});
        }
        else if(kind=="Motion sensor")
        {
            SolidBrush body(Color(240,40,160,200));
            g.FillEllipse(&body,at.X-5,at.Y-5,10.f,10.f);
            label("motion sensor"+std::string(actor.value("volumes",Json::array()).empty()?" (no volume)":""),{at.X+8,at.Y-18});
        }
        else if(kind=="Alarm")
        {
            SolidBrush body(Color(240,200,60,60));
            g.FillEllipse(&body,at.X-8,at.Y-8,16.f,16.f);
            SolidBrush ink(Color(255,255,255,255));
            g.FillRectangle(&ink,at.X-1.5f,at.Y-5,3.f,6.f);
            g.FillRectangle(&ink,at.X-1.5f,at.Y+2,3.f,2.f);
            label("alarm "+actor.value("name",std::string()),{at.X+10,at.Y-18});
        }
        else continue;
        // The wire from a detector to its alarm.
        if(Security::Detector(kind))
        {
            auto found=alarms.find(Fold(actor.value("event",std::string())));
            if(found!=alarms.end())line(wire,position,found->second);
        }
    }
    if(s.mode=="Security" && s.securityFirstSet)
    {
        const auto at=DesignScreen(s,s.securityFirst);
        Pen mark(Color(255,30,30,30),2);
        g.DrawLine(&mark,at.X-6,at.Y,at.X+6,at.Y);
        g.DrawLine(&mark,at.X,at.Y-6,at.X,at.Y+6);
        label("first point: now click the second",{at.X+8,at.Y-18});
    }
}
