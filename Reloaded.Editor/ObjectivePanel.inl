// Game-mode actors on the plan: player starts, the mission, its objectives and
// their devices, placed from the right-click menu and drawn with icons. Included
// inside MapDesignPanel.inl after LightingPanel.inl.
const char* const kSpyTeam="0";
const char* const kMercTeam="1";
// Small round menu icons: a blue S for spies, a red M for mercs.
HBITMAP ObjectiveMenuIcon(bool spy)
{
    static HBITMAP icons[2]={};
    if(icons[spy?0:1])return icons[spy?0:1];
    const int size=std::max(12,GetSystemMetrics(SM_CXMENUCHECK));
    HDC screen=GetDC(nullptr);
    HDC dc=CreateCompatibleDC(screen);
    HBITMAP bitmap=CreateCompatibleBitmap(screen,size,size);
    auto old=SelectObject(dc,bitmap);
    RECT all{0,0,size,size};
    FillRect(dc,&all,GetSysColorBrush(COLOR_MENU));
    HBRUSH fill=CreateSolidBrush(spy?RGB(40,110,220):RGB(210,60,50));
    HPEN pen=CreatePen(PS_SOLID,1,spy?RGB(20,60,140):RGB(130,30,20));
    auto oldBrush=SelectObject(dc,fill),oldPen=SelectObject(dc,pen);
    Ellipse(dc,0,0,size,size);
    SetBkMode(dc,TRANSPARENT);
    SetTextColor(dc,RGB(255,255,255));
    HFONT font=CreateFontA(size-3,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,"Segoe UI");
    auto oldFont=SelectObject(dc,font);
    DrawTextA(dc,spy?"S":"M",1,&all,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    SelectObject(dc,oldFont);SelectObject(dc,oldBrush);SelectObject(dc,oldPen);SelectObject(dc,old);
    DeleteObject(font);DeleteObject(fill);DeleteObject(pen);DeleteDC(dc);ReleaseDC(nullptr,screen);
    icons[spy?0:1]=bitmap;
    return bitmap;
}
std::string ObjectiveLabel(const Json& actor)
{
    const auto kind=actor.value("kind",std::string());
    const auto name=actor.value("name",std::string());
    if(kind=="Player start")return std::string(actor.value("team",std::string())==kSpyTeam?"spy":"merc")+" start "+SecurityName(actor);
    return Fold(kind)+" "+(name.empty()?SecurityName(actor):name);
}
// The game actor nearest a point in the view.
Json ObjectiveAt(DesignState& s,double x,double y)
{
    Json found;
    double best=10;
    for(const auto& actor:s.objectives)
    {
        const Vector position=actor.at("position");
        if(!DesignOnFloor(s,position[2],position[2]))continue;
        const double distance=DesignPointDistance(DesignScreen(s,position),x,y);
        if(distance<best){best=distance;found=actor;}
    }
    return found;
}
// A player start's facing handle: 28 pixels out along its yaw in the top view.
bool ObjectiveAimHandle(DesignState& s,const Json& actor,Gdiplus::PointF& handle)
{
    if(s.plane!=0 || actor.value("kind",std::string())!="Player start")return false;
    const auto at=DesignScreen(s,actor.at("position").get<Vector>());
    const double yaw=actor.at("rotation")[1].get<int>()*2*3.14159265358979323846/65536;
    handle={static_cast<float>(at.X+std::cos(yaw)*28),static_cast<float>(at.Y-std::sin(yaw)*28)};
    return true;
}
bool ObjectiveAimHandleAt(DesignState& s,double x,double y,Json& start)
{
    double best=9;
    start=Json{};
    for(const auto& actor:s.objectives)
    {
        Gdiplus::PointF handle;
        if(!ObjectiveAimHandle(s,actor,handle))continue;
        const Vector position=actor.at("position");
        if(!DesignOnFloor(s,position[2],position[2]))continue;
        const double distance=DesignPointDistance(handle,x,y);
        if(distance<best){best=distance;start=actor;}
    }
    return !start.is_null();
}
// Icons: starts as team-coloured discs with a letter, the mission as a star,
// objectives as diamonds, their devices as small squares, flags as pennants.
void ObjectivePaint(DesignState& s,Gdiplus::Graphics& g,const std::function<void(const std::string&,Gdiplus::PointF)>& label)
{
    using namespace Gdiplus;
    const float pi=3.14159265f;
    for(const auto& actor:s.objectives)
    {
        const auto kind=actor.value("kind",std::string());
        const Vector position=actor.at("position");
        if(!DesignOnFloor(s,position[2],position[2]))continue;
        const auto at=DesignScreen(s,position);
        const bool selected=actor.value("selected",false);
        Pen outline(Color(255,255,255,255),1.5f);
        if(kind=="Player start")
        {
            const bool spy=actor.value("team",std::string())==kSpyTeam;
            SolidBrush body(spy?Color(240,40,110,220):Color(240,210,60,50));
            g.FillEllipse(&body,at.X-8,at.Y-8,16.f,16.f);
            g.DrawEllipse(&outline,at.X-8,at.Y-8,16.f,16.f);
            // Facing, with a handle to drag it round.
            const double yaw=actor.at("rotation")[1].get<int>()*2*3.14159265358979323846/65536;
            Pen aim(Color(255,255,255,255),2);
            g.DrawLine(&aim,at.X,at.Y,static_cast<float>(at.X+std::cos(yaw)*14),static_cast<float>(at.Y-std::sin(yaw)*14));
            Gdiplus::PointF handle;
            if(ObjectiveAimHandle(s,actor,handle))
            {
                Pen ring(Color(230,40,40,40),1.5f);
                SolidBrush fill(Color(230,255,255,255));
                g.FillEllipse(&fill,handle.X-5,handle.Y-5,10.f,10.f);
                g.DrawEllipse(&ring,handle.X-5,handle.Y-5,10.f,10.f);
            }
            label(std::string(spy?"S":"M")+(selected?" (selected)":""),{at.X-4,at.Y-8});
        }
        else if(kind=="Mission")
        {
            PointF star[10];
            for(int i=0;i<10;++i)
            {
                const float r=i%2?4.f:10.f,a=-pi/2+i*pi/5;
                star[i]={at.X+std::cos(a)*r,at.Y+std::sin(a)*r};
            }
            SolidBrush body(Color(240,240,180,40));
            g.FillPolygon(&body,star,10);
            g.DrawPolygon(&outline,star,10);
            label("mission "+actor.value("name",std::string()),{at.X+12,at.Y-8});
        }
        else if(kind=="Objective")
        {
            PointF diamond[4]={{at.X,at.Y-9},{at.X+9,at.Y},{at.X,at.Y+9},{at.X-9,at.Y}};
            SolidBrush body(Color(240,90,200,90));
            g.FillPolygon(&body,diamond,4);
            g.DrawPolygon(&outline,diamond,4);
            label("objective "+actor.value("name",std::string()),{at.X+12,at.Y-8});
        }
        else if(kind=="Flag" || kind=="Drop zone")
        {
            SolidBrush body(kind=="Flag"?Color(240,230,120,30):Color(120,230,120,30));
            Pen pole(Color(240,80,60,30),2);
            g.DrawLine(&pole,at.X-4,at.Y+8,at.X-4,at.Y-8);
            PointF pennant[3]={{at.X-4,at.Y-8},{at.X+8,at.Y-4},{at.X-4,at.Y}};
            g.FillPolygon(&body,pennant,3);
            if(kind=="Drop zone")g.DrawEllipse(&outline,at.X-10,at.Y-10,20.f,20.f);
            label(Fold(kind),{at.X+12,at.Y-8});
        }
        else
        {
            SolidBrush body(kind=="Bomb target"?Color(240,200,80,40):Color(240,60,150,200));
            g.FillRectangle(&body,at.X-6,at.Y-6,12.f,12.f);
            g.DrawRectangle(&outline,at.X-6,at.Y-6,12.f,12.f);
            label(Fold(kind),{at.X+10,at.Y-8});
        }
    }
}
// Placement from the menu.
void ObjectivePlaceStart(DesignState& s,const std::string& team,const Vector& at)
{
    std::string type="Engine.PlayerStart";
    for(const auto& start:s.objectives)if(start.value("kind",std::string())=="Player start"){type=start.value("class",type);break;}
    Vector position=at;
    position[2]=s.depth+48; // Stands clear of the floor.
    const auto created=Editor::CreatePlayerStart(type,team,{position,{}});
    DesignRefresh(s);
    DesignStatus(s,std::string(team==kSpyTeam?"Spy":"Merc")+" start "+SecurityName(created)+" placed. Drag it to move it; its facing follows the editor's rotation. One Undo step.");
}
void ObjectivePlaceMission(DesignState& s,const Vector& at)
{
    Vector position=at;
    position[2]=s.depth+64;
    const auto created=Editor::CreateSecurityActor("SBase.SMission",{position,{}},{{"ObjectiveName","\"Mission\""},{"Description","\"Complete the mission\""}});
    DesignRefresh(s);
    DesignStatus(s,"Mission "+SecurityName(created)+" placed. Add objectives to it from the right-click menu; name it in the editor's property window. One Undo step.");
}
void ObjectivePlaceUnder(DesignState& s,const Json& owner,const std::string& type,const Vector& at)
{
    auto created=Editor::AddObjectiveActor(owner,type);
    Vector position=at;
    position[2]=s.depth+64;
    try{Editor::MoveSecurityActor(created,{position,{}},Json::object());}
    catch(const std::exception&){ /* It stays beside its parent. */ }
    DesignRefresh(s);
    const auto what=type=="SBase.SObjective"?"Objective":type=="SBase.SComputerObjectiveTrigger"?"Computer terminal":type=="SBase.SBombTargetObjectiveTrigger"?"Bomb target":"Flag (with its drop zone beside the objective)";
    DesignStatus(s,std::string(what)+" "+SecurityName(created)+" placed and linked to "+ObjectiveLabel(owner)+". Two Undo steps: the link, then the move.");
}
