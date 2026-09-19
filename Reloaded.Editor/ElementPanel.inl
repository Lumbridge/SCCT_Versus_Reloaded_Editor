// Gameplay elements — zip lines, hand-over-hand bars, pipes, ladders, climbable
// fences, poles and ledge grabs — placed in stages: the ends in the top view,
// their heights in a front or side view chosen for the line, and for ladders
// and pipes the side the player climbs from. The editor's own GE ADD command
// builds the element. Included inside MapDesignPanel.inl after ObjectivePanel.inl.
struct ElementKind { const char* name; const char* command; int points; const char* third; };
inline const std::vector<ElementKind>& ElementKinds()
{
    static const std::vector<ElementKind> kinds={
        {"Zip line","ZL",2,nullptr},
        {"Hand over hand","HOH",2,nullptr},
        {"Pipe","PIPE",3,"the side the player climbs from"},
        {"Ladder","LADDER",3,"the side the player climbs from"},
        {"Climbable fence","FENCE",2,nullptr},
        {"Pole","POLE",2,nullptr},
        {"Ledge grab","LG",2,nullptr}};
    return kinds;
}
const ElementKind* ElementFind(const std::string& name)
{
    for(const auto& kind:ElementKinds())if(name==kind.name)return &kind;
    return nullptr;
}
// Shows a view without touching the placement in progress.
void ElementShowPlane(DesignState& s,int plane,double depth)
{
    s.plane=plane;
    s.depth=depth;
    SendDlgItemMessage(s.window,DPlane,CB_SETCURSEL,plane,0);
    DesignRefresh(s);
    DesignFit(s);
}
std::string ElementPrompt(DesignState& s)
{
    const auto* kind=ElementFind(s.elementKind);
    if(!kind)return "";
    const std::string name=Fold(kind->name);
    switch(s.elementStage)
    {
    case 0:return "Click where the "+name+" starts (top view).";
    case 1:return "Click where the "+name+" ends. For a vertical element click the same spot.";
    case 2:return "Now the heights: click the height of the START (this view shows it side-on).";
    case 3:return "Click the height of the END.";
    default:return std::string("Click ")+(kind->third?kind->third:"the third point")+" (top view).";
    }
}
void ElementBegin(DesignState& s,const std::string& kind,const Vector* start)
{
    if(!ElementFind(kind))throw std::runtime_error("Unknown gameplay element.");
    if(!s.pending.is_null())DesignDeactivate(s);
    s.mode="Element";
    s.elementKind=kind;
    s.elementPoints.clear();
    s.elementStage=0;
    if(s.plane!=0)ElementShowPlane(s,0,s.depth);
    if(start)
    {
        s.elementPoints.push_back(*start);
        s.elementStage=1;
    }
    DesignStatus(s,ElementPrompt(s)+" Right-click or Escape cancels.");
    InvalidateRect(s.canvas,nullptr,FALSE);
}
void ElementCancel(DesignState& s)
{
    const bool active=s.mode=="Element";
    s.mode.clear();
    s.elementPoints.clear();
    s.elementStage=0;
    if(active && s.plane!=0)ElementShowPlane(s,0,s.elementDepth);
    if(active)DesignStatus(s,"Gameplay element cancelled.");
    InvalidateRect(s.canvas,nullptr,FALSE);
}
// The brush that makes an element visible: a bar along a zip line, pipe, pole
// or hand-over-hand run, a slab for a ladder, a wall for a fence, a lip for a
// ledge. Bars run along the piece's own X, so pitch tilts them.
struct ElementVisual { Json spec; Pose pose; };
ElementVisual ElementVisualFor(const std::string& kind,const std::vector<Vector>& p)
{
    const double pi=3.14159265358979323846;
    const Vector& a=p.at(0);
    const Vector& b=p.at(1);
    const double dx=b[0]-a[0],dy=b[1]-a[1],dz=b[2]-a[2];
    const double planar=std::hypot(dx,dy),length=std::max(8.0,std::hypot(planar,dz));
    auto box=[&](double w,double l,double h,const std::string& name)
    {
        return Json{{"kind","Platform"},{"construction","Carve"},{"width",std::max(w,1.0)},{"length",std::max(l,1.0)},{"height",std::max(h,1.0)},
                    {"thickness",16},{"steps",8},{"ceiling",false},{"portal",false},{"name",name}};
    };
    const Vector mid{(a[0]+b[0])/2,(a[1]+b[1])/2,(a[2]+b[2])/2};
    const int yaw=planar>1e-6?Security::YawTowards(a,b):0;
    int pitch=static_cast<int>(std::lround(std::atan2(dz,std::max(planar,1e-9))/(2*pi)*65536))%65536;
    if(pitch<0)pitch+=65536;
    auto bar=[&](double across,double name_height,const std::string& name)
    {
        auto spec=box(length,across,name_height,name);
        const Rotation rotation{pitch,yaw,0};
        // The box grows upward from its position; centre it on the line.
        const auto lift=TransformPoint({0,0,name_height/2},{{},rotation});
        Vector position=mid;
        for(int i=0;i<3;++i)position[i]-=lift[i];
        return ElementVisual{spec,{position,rotation}};
    };
    if(kind=="Zip line")return bar(6,6,"Zip line");
    if(kind=="Hand over hand")return bar(8,8,"Hand-over-hand bar");
    if(kind=="Pipe")return bar(14,14,"Pipe");
    if(kind=="Pole")return bar(8,8,"Pole");
    if(kind=="Ladder")return bar(48,4,"Ladder");
    if(kind=="Climbable fence")
    {
        const double base=std::min(a[2],b[2]),top=std::max(a[2],b[2]);
        Vector position{mid[0],mid[1],base};
        return {box(std::max(planar,8.0),6,std::max(top-base,16.0),"Climbable fence"),{position,{0,yaw,0}}};
    }
    // Ledge grab: a lip along the edge at the start's height.
    Vector position{mid[0],mid[1],a[2]-8};
    return {box(std::max(planar,8.0),10,8,"Ledge"),{position,{0,yaw,0}}};
}
// Creates an element: the editor's GE, then its visual brush, then the record
// the plan draws from.
void ElementCreate(DesignState& s,const std::string& kindName,const std::vector<Vector>& points)
{
    const auto* kind=ElementFind(kindName);
    if(!kind || points.size()<static_cast<size_t>(kind->points))throw std::runtime_error("The element needs more points.");
    std::string command=std::string("GE ADD ")+kind->command;
    for(int i=0;i<kind->points;++i)
    {
        const auto& p=points[i];
        Design::CheckVector(p);
        const auto n=std::to_string(i+1);
        command+=" X"+n+"="+Design::Round(p[0],3)+" Y"+n+"="+Design::Round(p[1],3)+" Z"+n+"="+Design::Round(p[2],3);
    }
    if(!Editor::Exec(command))throw std::runtime_error("The editor refused: "+command);
    std::string visualNote;
    Json members=Json::array();
    try
    {
        const auto visual=ElementVisualFor(kindName,points);
        auto piece=Editor::DesignBlockout(visual.spec,visual.pose,Json{});
        Json data=DesignData(s);
        data["pieces"].push_back(piece);
        members=piece.at("members");
        if(!data.contains("elements") || !data["elements"].is_array())data["elements"]=Json::array();
        data["elements"].push_back({{"kind",kindName},{"points",points},{"members",members},{"command",command}});
        DesignSave(s,data,false);
        visualNote=" A "+Fold(visual.spec.value("name",std::string("brush")))+" brush marks it.";
    }
    catch(const std::exception& e){visualNote=std::string(" (No visual brush: ")+e.what()+")";}
    DesignRefresh(s);
    DesignStatus(s,std::string(kind->name)+" added with "+command.substr(7)+"."+visualNote+" Undo twice removes both.");
}
void ElementFinish(DesignState& s)
{
    const auto kind=s.elementKind;
    const auto points=s.elementPoints;
    s.mode.clear();
    s.elementPoints.clear();
    s.elementStage=0;
    if(s.plane!=0)ElementShowPlane(s,0,s.elementDepth);
    ElementCreate(s,kind,points);
}
// One click of the placement.
void ElementClick(DesignState& s,const Vector& at)
{
    const auto* kind=ElementFind(s.elementKind);
    if(!kind){s.mode.clear();return;}
    if(s.elementStage==0)
    {
        s.elementPoints.push_back(at);
        s.elementStage=1;
    }
    else if(s.elementStage==1)
    {
        s.elementPoints.push_back(at);
        s.elementStage=2;
        // Side-on to the line: the front view when it runs mostly along X,
        // the side view when along Y. The view's depth cuts through the middle.
        const auto& a=s.elementPoints[0];
        const auto& b=s.elementPoints[1];
        const bool alongX=std::abs(b[0]-a[0])>=std::abs(b[1]-a[1]);
        s.elementDepth=s.depth;
        ElementShowPlane(s,alongX?1:2,alongX?(a[1]+b[1])/2:(a[0]+b[0])/2);
    }
    else if(s.elementStage==2)
    {
        s.elementPoints[0][2]=at[2];
        s.elementStage=3;
    }
    else if(s.elementStage==3)
    {
        s.elementPoints[1][2]=at[2];
        if(kind->points>2)
        {
            s.elementStage=4;
            ElementShowPlane(s,0,s.elementDepth);
        }
        else{ElementFinish(s);return;}
    }
    else
    {
        Vector third=at;
        third[2]=(s.elementPoints[0][2]+s.elementPoints[1][2])/2;
        s.elementPoints.push_back(third);
        ElementFinish(s);
        return;
    }
    DesignStatus(s,ElementPrompt(s)+" Right-click or Escape cancels.");
    InvalidateRect(s.canvas,nullptr,FALSE);
}
// Elements already in the map: a line between the ends, in every view, while
// their brushes are still there.
void ElementPaintStored(DesignState& s,Gdiplus::Graphics& g,const std::function<void(const std::string&,Gdiplus::PointF)>& label)
{
    using namespace Gdiplus;
    Json data;
    try{data=DesignData(s);}catch(const std::exception&){return;}
    if(!data.contains("elements") || !data["elements"].is_array())return;
    for(const auto& element:data["elements"])
    {
        bool live=false;
        for(const auto& member:element.value("members",Json::array()))
            for(const auto& actor:s.scene)
                if(actor.at("path")==member.at("path"))live=true;
        if(!live)continue;
        const auto& points=element.at("points");
        if(points.size()<2)continue;
        const Vector a=points[0],b=points[1];
        if(!DesignOnFloor(s,std::min(a[2],b[2]),std::max(a[2],b[2])))continue;
        const auto kind=element.value("kind",std::string());
        Pen line(kind=="Zip line"?Color(220,60,60,200):kind=="Ladder"?Color(220,200,120,30):kind=="Pipe"?Color(220,120,120,130):Color(220,200,90,20),3);
        const auto from=DesignScreen(s,a),to=DesignScreen(s,b);
        g.DrawLine(&line,from,to);
        SolidBrush dot(Color(240,255,255,255));
        g.FillEllipse(&dot,from.X-3,from.Y-3,6.f,6.f);
        g.FillEllipse(&dot,to.X-3,to.Y-3,6.f,6.f);
        if(points.size()>2)
        {
            Pen side(Color(160,255,255,255),1);
            side.SetDashStyle(DashStyleDot);
            g.DrawLine(&side,DesignScreen(s,Vector{(a[0]+b[0])/2,(a[1]+b[1])/2,(a[2]+b[2])/2}),DesignScreen(s,points[2].get<Vector>()));
        }
        label(Fold(kind),{(from.X+to.X)/2+6,(from.Y+to.Y)/2-16});
    }
}
// Floors: the spacing at a point, and the pieces that join one floor to the
// next. A floor is the height of the room at the point plus its wall.
double FloorSpacingAt(DesignState& s,const Vector& at)
{
    try
    {
        for(const auto& piece:DesignData(s).at("pieces"))
        {
            const auto kind=piece.at("spec").at("kind").get<std::string>();
            if(kind!="Room" && kind!="Corridor")continue;
            const auto bounds=DesignBoundsOf(piece.at("spec"),{piece.at("position").get<Vector>(),piece.at("rotation").get<Rotation>()});
            if(at[0]<bounds.lo[0] || at[0]>bounds.hi[0] || at[1]<bounds.lo[1] || at[1]>bounds.hi[1])continue;
            if(s.depth<bounds.lo[2]-1 || s.depth>bounds.hi[2])continue;
            return (bounds.hi[2]-bounds.lo[2])+piece.at("spec").value("thickness",16.0);
        }
    }
    catch(const std::exception&) { /* Fall back to the default spacing. */ }
    return 272;
}
void FloorGo(DesignState& s,const Vector& at,bool up)
{
    const double spacing=FloorSpacingAt(s,at);
    s.depth+=up?spacing:-spacing;
    InvalidateRect(s.canvas,nullptr,FALSE);
    DesignStatus(s,std::string(up?"Up":"Down")+" one floor: the plan now shows depth "+Design::Round(s.depth)+" (floors are "+Design::Round(spacing)+" apart here). Pieces placed now land on this floor.");
}
Json FloorPlace(DesignState& s,const Json& items)
{
    Json placed=Json::array();
    for(size_t start=0;start<items.size();start+=64)
    {
        Json batch=Json::array();
        for(size_t i=start;i<items.size() && i<start+64;++i)batch.push_back(items[i]);
        for(auto& piece:Editor::DesignBlockoutBatch(batch))placed.push_back(piece);
    }
    Json data=DesignData(s);
    for(const auto& piece:placed)data["pieces"].push_back(piece);
    DesignSave(s,data,false);
    DesignRefresh(s);
    try{DesignOrderPiecesLast(s);}catch(const std::exception&){}
    return placed;
}
void FloorDuplicate(DesignState& s,const Vector& at,bool up)
{
    const double spacing=FloorSpacingAt(s,at);
    const double offset=up?spacing:-spacing;
    Json items=Json::array();
    for(const auto& piece:DesignData(s).at("pieces"))
    {
        bool live=false;
        for(auto& member:piece.at("members"))
            for(auto& actor:s.scene)
                if(actor.at("path")==member.at("path"))live=true;
        if(!live)continue;
        auto position=piece.at("position").get<Vector>();
        if(std::abs(position[2]-s.depth)>8)continue;
        position[2]+=offset;
        Json spec=piece.at("spec");
        spec["name"]=spec.value("name",std::string("Piece"))+(up?" (above)":" (below)");
        items.push_back({{"spec",spec},{"position",position},{"rotation",piece.at("rotation")},{"previous",Json{}}});
    }
    if(items.empty())throw std::runtime_error("No pieces sit on the floor at depth "+Design::Round(s.depth)+" to duplicate.");
    const auto placed=FloorPlace(s,items);
    s.depth+=offset;
    InvalidateRect(s.canvas,nullptr,FALSE);
    DesignStatus(s,std::to_string(placed.size())+" piece(s) duplicated "+(up?"above":"below")+", "+Design::Round(spacing)+" units away, and the plan moved to that floor. One Undo step; rebuild geometry when done.");
}
// Stairs of a chosen shape from a floor up (or down) to the next, inside a
// stairwell carved to exactly their footprint, so nothing is left beside them.
void FloorStairsAt(DesignState& s,const Vector& at,double base,double spacing,const std::string& kind)
{
    const int steps=std::max(2,static_cast<int>(std::ceil(spacing/16)));
    const double width=128;
    const bool up=base>=s.depth-1; // Rising from this floor, or down into the one below.
    Json stairs={{"kind",kind},{"construction","Carve"},{"width",width},{"height",spacing},{"thickness",16},{"steps",steps},{"ceiling",false},{"portal",false},{"name",std::string(up?"Stairs up":"Stairs down")}};
    double shaftWidth=width,shaftLength=steps*32.0,shaftX=0;
    if(kind=="Stairs")stairs["length"]=steps*32.0;
    else if(kind=="Stairs L" || kind=="Stairs U")
    {
        const int n1=(steps+1)/2;
        const double run=n1*32.0;
        stairs["length"]=run+width;
        shaftLength=run+width;
        if(kind=="Stairs L"){shaftWidth=width+run;shaftX=run/2;}
        else shaftWidth=2*width;
    }
    else
    {
        // 384 across leaves 144 units of tread between post and wall; the
        // shaft has 24 units of clearance round the spiral.
        stairs["width"]=384;stairs["length"]=384;stairs["steps"]=std::max(steps,12);
        shaftWidth=384+48;shaftLength=384+48;
    }
    Json shaft={{"kind","Room"},{"construction","Carve"},{"width",shaftWidth},{"length",shaftLength},{"height",spacing+8},{"thickness",16},{"steps",8},{"ceiling",true},{"portal",false},{"name","Stairwell"}};
    Vector position=at;
    position[2]=base;
    Vector shaftPosition=position;
    shaftPosition[0]+=shaftX;
    Json items=Json::array({{{"spec",shaft},{"position",shaftPosition},{"rotation",Rotation{}},{"previous",Json{}}},
                            {{"spec",stairs},{"position",position},{"rotation",Rotation{}},{"previous",Json{}}}});
    FloorPlace(s,items);
    DesignStatus(s,std::string(kind=="Stairs"?"Straight":kind=="Stairs L"?"L-shaped":kind=="Stairs U"?"U-shaped":"Spiral")+" stairs placed, rising "+Design::Round(spacing)+" units from "+Design::Round(base)
        +", in a stairwell cut to their footprint. Click the stairs to turn or move them; edit the shape or steps in the panel. One Undo step.");
}
void FloorStairs(DesignState& s,const Vector& at,bool up,const std::string& kind)
{
    const double spacing=FloorSpacingAt(s,at);
    FloorStairsAt(s,at,up?s.depth:s.depth-spacing,spacing,kind);
}
void FloorClimb(DesignState& s,const Vector& at,const std::string& kind)
{
    const double spacing=FloorSpacingAt(s,at);
    Json hole={{"kind","Room"},{"construction","Carve"},{"width",96},{"length",96},{"height",spacing+8},{"thickness",16},{"steps",8},{"ceiling",true},{"portal",false},{"name",kind+" shaft"}};
    Vector position=at;
    position[2]=s.depth;
    FloorPlace(s,Json::array({{{"spec",hole},{"position",position},{"rotation",Rotation{}},{"previous",Json{}}}}));
    Vector bottom=at,top=at,side=at;
    bottom[2]=s.depth;
    top[2]=s.depth+spacing;
    side[1]-=48;
    side[2]=s.depth+spacing/2;
    ElementCreate(s,kind,{bottom,top,side});
}
// A lift between a floor and the next: a shaft cut through the slab and an
// SLift platform that rises when stood on and comes back after a pause.
void FloorLiftAt(DesignState& s,const Vector& at,double base,double spacing)
{
    Json shaft={{"kind","Room"},{"construction","Carve"},{"width",208},{"length",208},{"height",spacing+24},{"thickness",16},{"steps",8},{"ceiling",true},{"portal",false},{"name","Lift shaft"}};
    Vector position=at;
    position[2]=base-16;
    FloorPlace(s,Json::array({{{"spec",shaft},{"position",position},{"rotation",Rotation{}},{"previous",Json{}}}}));
    Vector platform=at;
    platform[2]=base-8;
    const double moveTime=std::clamp(spacing/160.0,1.5,8.0);
    try{Editor::CreateLift(platform,192,192,16,spacing,moveTime);}
    catch(const std::exception& e)
    {
        Editor::Exec("TRANSACTION UNDO");
        DesignRefresh(s);
        throw std::runtime_error(std::string("The shaft was undone because the lift could not be created: ")+e.what());
    }
    DesignRefresh(s);
    DesignStatus(s,"Lift placed: a 192 x 192 platform at Z "+Design::Round(base)+" rises "+Design::Round(spacing)+" units in "+Design::Round(moveTime,1)
        +" s when stood on and returns after 3 s, in a shaft cut through the floor above. Two Undo steps (shaft, then lift). Edit MoveTime, StayOpenTime or InitialState in the Magic Event workbench.");
}
void FloorLift(DesignState& s,const Vector& at,bool up)
{
    const double spacing=FloorSpacingAt(s,at);
    FloorLiftAt(s,at,up?s.depth:s.depth-spacing,spacing);
}
void FloorOpening(DesignState& s,const Vector& at)
{
    const double spacing=FloorSpacingAt(s,at);
    Json hole={{"kind","Room"},{"construction","Carve"},{"width",128},{"length",128},{"height",spacing+8},{"thickness",16},{"steps",8},{"ceiling",true},{"portal",false},{"name","Floor opening"}};
    Vector position=at;
    position[2]=s.depth;
    FloorPlace(s,Json::array({{{"spec",hole},{"position",position},{"rotation",Rotation{}},{"previous",Json{}}}}));
    DesignStatus(s,"A 128 x 128 opening now cuts through to the floor above. Click it to resize or move it. One Undo step.");
}
// The points so far, and the line between the ends.
void ElementPaint(DesignState& s,Gdiplus::Graphics& g,const std::function<void(const std::string&,Gdiplus::PointF)>& label)
{
    ElementPaintStored(s,g,label);
    if(s.mode!="Element" || s.elementPoints.empty())return;
    using namespace Gdiplus;
    Pen line(Color(230,200,90,20),3);
    Pen ring(Color(255,255,255,255),2);
    SolidBrush dot(Color(240,200,90,20));
    for(size_t i=0;i<s.elementPoints.size();++i)
    {
        const auto at=DesignScreen(s,s.elementPoints[i]);
        g.FillEllipse(&dot,at.X-6,at.Y-6,12.f,12.f);
        g.DrawEllipse(&ring,at.X-6,at.Y-6,12.f,12.f);
        label(i==0?"start":i==1?"end":"climb from here",{at.X+9,at.Y-8});
    }
    if(s.elementPoints.size()>=2)g.DrawLine(&line,DesignScreen(s,s.elementPoints[0]),DesignScreen(s,s.elementPoints[1]));
    label(Fold(s.elementKind)+": "+ElementPrompt(s),{10,30});
}
