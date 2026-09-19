#pragma once
#include "WorkflowModel.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace Workflow::Design
{
using Face = std::vector<Vector>;
// Native Add Special's Zone Portal: Portal | Invisible | Non-solid.
constexpr unsigned kPortalPolyFlags = 0x04000009u;
struct Solid { std::vector<Face> faces; bool subtract=false; unsigned flags=0; };
inline double Number(const std::string& text,double minimum=-100000,double maximum=100000)
{
    size_t used=0;double n=std::stod(text,&used);
    if(used!=text.size() || !std::isfinite(n) || n<minimum || n>maximum)throw std::runtime_error("Enter a finite number in the supported range.");
    return n;
}
inline void CheckVector(const Vector& v) { for(auto n:v)if(!std::isfinite(n) || std::abs(n)>1000000)throw std::runtime_error("Position is out of range."); }
inline Solid Box(Vector lo,Vector hi,bool subtract=false)
{
    CheckVector(lo);CheckVector(hi);for(int i=0;i<3;++i)if(hi[i]<=lo[i])throw std::runtime_error("A brush must have positive dimensions.");
    const int corners[6][4]={{1,3,7,5},{0,4,6,2},{2,6,7,3},{0,1,5,4},{4,5,7,6},{0,2,3,1}};
    Solid s;s.subtract=subtract;
    for(auto& face:corners){Face f;for(auto c:face)f.push_back({c&1?hi[0]:lo[0],c&2?hi[1]:lo[1],c&4?hi[2]:lo[2]});s.faces.push_back(f);}return s;
}
// Any six-sided solid whose corners follow the box's corner numbering (bit 0
// along the first direction, bit 1 the second, bit 2 up), so wedges wind the
// same way boxes do.
inline Solid Hexa(const std::array<Vector,8>& c,bool subtract=false)
{
    for(const auto& v:c)CheckVector(v);
    const int corners[6][4]={{1,3,7,5},{0,4,6,2},{2,6,7,3},{0,1,5,4},{4,5,7,6},{0,2,3,1}};
    Solid s;s.subtract=subtract;
    for(auto& face:corners){Face f;for(auto i:face)f.push_back(c[i]);s.faces.push_back(f);}
    return s;
}
// A convex solid from its faces, each wound so its normal points away from the
// solid's centre, as the engine expects.
inline Solid Convex(std::vector<Face> faces,unsigned flags=0)
{
    Vector centre{};size_t count=0;
    for(auto& f:faces)for(auto& v:f){CheckVector(v);for(int i=0;i<3;++i)centre[i]+=v[i];++count;}
    if(count==0)throw std::runtime_error("A solid needs faces.");
    for(int i=0;i<3;++i)centre[i]/=static_cast<double>(count);
    for(auto& f:faces)
    {
        if(f.size()<3)throw std::runtime_error("A face needs three points.");
        const auto& a=f[0];const auto& b=f[1];const auto& c=f[2];
        const Vector u{b[0]-a[0],b[1]-a[1],b[2]-a[2]},v{c[0]-a[0],c[1]-a[1],c[2]-a[2]};
        const Vector normal{u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]};
        const Vector out{a[0]-centre[0],a[1]-centre[1],a[2]-centre[2]};
        if(normal[0]*out[0]+normal[1]*out[1]+normal[2]*out[2]<0)std::reverse(f.begin(),f.end());
    }
    Solid s;s.faces=std::move(faces);s.flags=flags;return s;
}
// A triangular prism from a sloped top triangle down to the floor.
inline Solid Prism(const Vector& a,const Vector& b,const Vector& c,unsigned flags)
{
    const Vector ba{a[0],a[1],0},bb{b[0],b[1],0},bc{c[0],c[1],0};
    return Convex({{a,b,c},{ba,bb,bc},{a,ba,bb,b},{b,bb,bc,c},{c,bc,ba,a}},flags);
}
inline bool StairKind(const std::string& kind) { return kind=="Stairs" || kind=="Stairs L" || kind=="Stairs U" || kind=="Spiral"; }
// Treads follow a resize: about 32 units of run each and no more than 24 of
// rise, so a longer or taller flight gains steps and a shorter one loses them.
inline void RecountSteps(Json& spec)
{
    const auto kind=spec.at("kind").get<std::string>();
    const double w=spec.at("width"),l=spec.at("length"),h=spec.at("height");
    int steps=spec.value("steps",8);
    if(kind=="Stairs")steps=std::max(1,static_cast<int>(std::lround(l/32)));
    else if(kind=="Stairs L" || kind=="Stairs U")steps=2*std::max(1,static_cast<int>(std::lround((l-w)/32)));
    else if(kind=="Spiral")steps=std::max(8,static_cast<int>(std::ceil(h/24)));
    else return;
    steps=std::max(steps,static_cast<int>(std::ceil(h/24)));
    spec["steps"]=std::clamp(steps,kind=="Spiral"?3:(kind=="Stairs"?1:2),128);
}
// Pawns bump over steps; an invisible semi-solid ramp laid on the steps' front
// edges (one rise above the treads at the back) lets them glide. Its flags are
// the engine's PF_Invisible and PF_SemiSolid.
constexpr unsigned kGlideFlags=0x00000001u|0x00000020u;
// A ramp over a flight running along +Y from y0 to y1 across x0..x1, rising
// from z0 at the front edge to z1 at the back.
inline Solid GlideRamp(double x0,double x1,double y0,double y1,double z0,double z1)
{
    std::array<Vector,8> c;
    for(int k=0;k<8;++k)c[k]={(k&1)?x1:x0,(k&2)?y1:y0,(k&4)?((k&2)?z1:z0):0};
    auto solid=Hexa(c);
    solid.flags=kGlideFlags;
    return solid;
}
// A single two-sided polygon across local Y, used for zone portals in doorways.
inline Solid Sheet(double width,double height,unsigned flags)
{
    if(!std::isfinite(width) || !std::isfinite(height) || width<1 || height<1)throw std::runtime_error("A portal needs positive dimensions.");
    Solid s;s.flags=flags;
    s.faces.push_back({{-width/2,0,0},{width/2,0,0},{width/2,0,height},{-width/2,0,height}});
    return s;
}
// Carve construction subtracts the interior from this engine's solid space.
// Shell construction keeps the original carve-then-add-walls pieces so that
// blockouts saved by earlier versions regenerate exactly as they were placed.
inline bool Carved(const Json& spec) { return spec.value("construction",std::string("Shell"))=="Carve"; }
inline std::vector<Solid> Geometry(const Json& spec)
{
    auto kind=spec.at("kind").get<std::string>();
    const double w=spec.at("width"),l=spec.at("length"),h=spec.at("height"),t=spec.value("thickness",16.0);
    for(auto n:{w,l,h,t})if(!std::isfinite(n) || n<1 || n>65536)throw std::runtime_error("Dimensions must be between 1 and 65536 units.");
    const bool carve=Carved(spec);
    std::vector<Solid> out;
    auto box=[&](Vector a,Vector b,bool sub=false){out.push_back(Box(a,b,sub));};
    if(kind=="Room" || kind=="Corridor" || kind=="Vent" || kind=="Crawlway")
    {
        // A vent is a corridor at crouch height and a crawlway a lower one:
        // same shape, different clearance to check against.
        const bool open=kind!="Room";
        if(carve)
        {
            // One brush carves the interior. An open-ended piece also cuts
            // through the wall thickness at each end so it meets its
            // neighbours.
            box({-w/2,-l/2-(open?t:0),0},{w/2,l/2+(open?t:0),h},true);
        }
        else
        {
            box({-w/2-t,-l/2-t,-t},{w/2+t,l/2+t,h+t},true);
            box({-w/2-t,-l/2-t,-t},{w/2+t,l/2+t,0});
            box({-w/2-t,-l/2-t,0},{-w/2,l/2+t,h});box({w/2,-l/2-t,0},{w/2+t,l/2+t,h});
            if(!open){box({-w/2,-l/2-t,0},{w/2,-l/2,h});box({-w/2,l/2,0},{w/2,l/2+t,h});}
            if(spec.value("ceiling",true))box({-w/2-t,-l/2-t,h},{w/2+t,l/2+t,h+t});
        }
    }
    else if(kind=="Doorway")
    {
        box({-w/2,-l/2,0},{w/2,l/2,h},true);
        if(spec.value("portal",false))out.push_back(Sheet(w,h,kPortalPolyFlags));
    }
    else if(kind=="Platform")box({-w/2,-l/2,0},{w/2,l/2,h});
    else if(kind=="Stairs")
    {
        int steps=spec.value("steps",8);if(steps<1 || steps>128)throw std::runtime_error("Use 1 to 128 steps.");
        for(int i=0;i<steps;++i)box({-w/2,-l/2+l*i/steps,0},{w/2,-l/2+l*(i+1)/steps,h*(i+1)/steps});
        if(steps>1)out.push_back(GlideRamp(-w/2,w/2,-l/2,l/2-l/steps,h/steps,h));
    }
    else if(kind=="Stairs L" || kind=="Stairs U")
    {
        // Two flights and a landing. Width is one flight; length is the
        // footprint along the first flight. An L turns right by a quarter, a U
        // comes back beside the first flight.
        int steps=spec.value("steps",8);if(steps<2 || steps>128)throw std::runtime_error("Use 2 to 128 steps for a turning stair.");
        const int n1=(steps+1)/2,n2=steps-n1;
        const double run=l-w;if(run<8)throw std::runtime_error("A turning stair needs a length at least 8 units more than its width.");
        const double rise=h/steps,h1=rise*n1;
        if(kind=="Stairs L")
        {
            for(int i=0;i<n1;++i)box({-w/2,-l/2+run*i/n1,0},{w/2,-l/2+run*(i+1)/n1,rise*(i+1)});
            box({-w/2,-l/2+run,0},{w/2,l/2,h1});
            for(int i=0;i<n2;++i)box({w/2+run*i/n2,-l/2+run,0},{w/2+run*(i+1)/n2,l/2,h1+rise*(i+1)});
            if(n1>1)out.push_back(GlideRamp(-w/2,w/2,-l/2,-l/2+run-run/n1,rise,h1));
            if(n2>1)
            {
                // The second flight runs along +X: the same ramp, turned.
                std::array<Vector,8> c;
                const double x0=w/2,x1=w/2+run-run/n2,y0=-l/2+run,y1=l/2;
                // Corner bits: first along +Y, second along -X, so the solid winds
                // the same way a box does.
                for(int k=0;k<8;++k)c[k]={(k&2)?x0:x1,(k&1)?y1:y0,(k&4)?((k&2)?h1+rise:h):0};
                auto solid=Hexa(c);
                solid.flags=kGlideFlags;
                out.push_back(solid);
            }
        }
        else
        {
            for(int i=0;i<n1;++i)box({-w,-l/2+run*i/n1,0},{0,-l/2+run*(i+1)/n1,rise*(i+1)});
            box({-w,-l/2+run,0},{w,l/2,h1});
            for(int i=0;i<n2;++i)box({0,l/2-w-run*(i+1)/n2,0},{w,l/2-w-run*i/n2,h1+rise*(i+1)});
            if(n1>1)out.push_back(GlideRamp(-w,0,-l/2,-l/2+run-run/n1,rise,h1));
            if(n2>1)
            {
                // The second flight comes back along -Y: front edge at the landing.
                std::array<Vector,8> c;
                const double yLow=-l/2+run/n2,yHigh=l/2-w; // Back edge at the low end, front at the landing.
                for(int k=0;k<8;++k)c[k]={(k&1)?w:0,(k&2)?yHigh:yLow,(k&4)?((k&2)?h1+rise:h):0};
                auto solid=Hexa(c);
                solid.flags=kGlideFlags;
                out.push_back(solid);
            }
        }
    }
    else if(kind=="Spiral")
    {
        // Wedge steps round a post, one full turn over the height. Width is the
        // outer diameter.
        int steps=spec.value("steps",12);if(steps<3 || steps>128)throw std::runtime_error("Use 3 to 128 steps for a spiral.");
        const double pi=3.14159265358979323846,R=w/2,r=std::max(12.0,w/8),rise=h/steps;
        if(R<=r+8)throw std::runtime_error("A spiral needs a width of at least 64 units.");
        for(int i=0;i<steps;++i)
        {
            const double a0=2*pi*i/steps,a1=2*pi*(i+1)/steps,top=rise*(i+1);
            std::array<Vector,8> c;
            for(int k=0;k<8;++k)
            {
                const double rad=(k&1)?R:r,ang=(k&2)?a1:a0,z=(k&4)?top:0;
                c[k]={rad*std::cos(ang),rad*std::sin(ang),z};
            }
            out.push_back(Hexa(c));
        }
        box({-r,-r,0},{r,r,h});
        // The glide ramp climbs from each tread's front edge to the next one's,
        // as two sloped prisms per step so every face stays planar.
        for(int i=0;i+1<steps;++i)
        {
            const double a0=2*pi*i/steps,a1=2*pi*(i+1)/steps,z0=rise*(i+1),z1=rise*(i+2);
            const Vector p1{r*std::cos(a0),r*std::sin(a0),z0},p2{R*std::cos(a0),R*std::sin(a0),z0},p3{R*std::cos(a1),R*std::sin(a1),z1},p4{r*std::cos(a1),r*std::sin(a1),z1};
            out.push_back(Prism(p1,p2,p3,kGlideFlags));
            out.push_back(Prism(p1,p3,p4,kGlideFlags));
        }
    }
    else if(kind=="Ramp")
    {
        Vector a{-w/2,-l/2,0},b{w/2,-l/2,0},c{w/2,l/2,0},d{-w/2,l/2,0},e{w/2,l/2,h},f{-w/2,l/2,h};
        out.push_back({{{a,d,c,b},{a,b,e,f},{b,c,e},{a,f,d},{d,f,e,c}},false});
    }
    else throw std::runtime_error("Unknown blockout shape.");
    return out;
}
// Brush text for a piece. Its rotation is baked into the polygons rather than
// left on the brush actor: this editor's wireframes apply an actor's Rotation
// but its geometry build does not, so a rotated brush would build sideways.
// A material path puts the same texture on every face, so a blockout arrives
// as a greybox rather than in the default texture.
inline Json Definition(const Json& spec,const Rotation& rotation={},const std::string& material="")
{
    if(material.find_first_of("\"\r\n ")!=std::string::npos)throw std::runtime_error("Invalid material path.");
    auto geometry=Geometry(spec);Json actors=Json::array();size_t index=0;
    const Pose turn{{},rotation};
    auto coordinate=[](double value){return std::abs(value)<1e-6?0.0:value;};
    for(auto& solid:geometry)
    {
        auto name="Block"+std::to_string(index++);std::ostringstream text;
        text.precision(9);
        text<<"Begin Actor Class=Engine.Brush Name="<<name<<"\nCsgOper="<<(solid.subtract?"CSG_Subtract":"CSG_Add")<<"\n";
        if(solid.flags)text<<"PolyFlags="<<solid.flags<<"\n";
        text<<"Begin Brush Name="<<name<<"Model\nBegin PolyList\n";
        for(auto& face:solid.faces)
        {
            text<<"Begin Polygon";
            if(!material.empty() && Fold(material)!="none")text<<" Texture="<<material;
            text<<" Flags="<<solid.flags<<"\n";
            for(auto local:face)
            {
                const auto v=rotation==Rotation{}?local:TransformPoint(local,turn);
                text<<"Vertex "<<coordinate(v[0])<<","<<coordinate(v[1])<<","<<coordinate(v[2])<<"\n";
            }
            text<<"End Polygon\n";
        }
        text<<"End PolyList\nEnd Brush\nBrush=Model'MyLevel."<<name<<"Model'\nEnd Actor\n";
        actors.push_back({{"name",name},{"class","Engine.Brush"},{"path","MyLevel."+name},{"text",text.str()},{"position",Vector{}},{"rotation",Rotation{}},{"tag","None"},{"event","None"}});
    }
    return {{"id","blockout"},{"actors",actors},{"bindings",Json::array()},{"dependencies",Json::array()}};
}
inline double Distance(const Vector& a,const Vector& b)
{CheckVector(a);CheckVector(b);double d=0;for(int i=0;i<3;++i)d+=(a[i]-b[i])*(a[i]-b[i]);return std::sqrt(d);}
inline double Calibration(double pixels,double units)
{if(!std::isfinite(pixels)||!std::isfinite(units)||pixels<1 || units<=0 || units>1000000)throw std::runtime_error("Calibration needs two distinct image points and a positive world distance.");return units/pixels;}
inline std::vector<Vector> Align(const std::vector<Vector>& positions,int axis,const std::string& mode,double spacing)
{
    if(positions.empty() || axis<0 || axis>2 || !std::isfinite(spacing))throw std::runtime_error("Select actors and a valid axis.");
    auto result=positions;for(auto& p:positions)CheckVector(p);
    if(mode=="Align")for(auto& p:result)p[axis]=positions.front()[axis];
    else if(mode=="Distribute")
    {
        std::vector<size_t> order;for(size_t i=0;i<positions.size();++i)order.push_back(i);
        std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b){return positions[a][axis]<positions[b][axis];});
        for(size_t i=0;i<order.size();++i)result[order[i]][axis]=positions[order[0]][axis]+spacing*i;
    }
    else throw std::runtime_error("Unknown alignment mode.");
    for(auto& p:result)CheckVector(p);return result;
}
inline Json Repeat(const Json& definition,int count,const Vector& spacing,int yawStep)
{
    if(count<1 || count>128 || definition.at("actors").size()*count>2000)throw std::runtime_error("Use up to 128 copies and 2000 actors.");
    CheckVector(spacing);Json result=definition;result["actors"]=Json::array();
    for(int i=0;i<count;++i)
    {
        Pose pose;for(int a=0;a<3;++a)pose.position[a]=spacing[a]*i;CheckVector(pose.position);pose.rotation[1]=static_cast<int>((static_cast<int64_t>(yawStep)*i)%65536);
        std::map<std::string,std::string> bindings;for(auto& b:definition.at("bindings"))bindings[b.at("id")]=b.at("path");
        auto copy=PreparePlacement(definition,pose,"Copy"+std::to_string(i)+"_","MyLevel",bindings);
        for(auto actor:copy["actors"])
        {
            actor["path"]="MyLevel."+actor.at("name").get<std::string>();
            for(const char* prop:{"Tag","Event"}){auto value=Property(actor.at("text"),prop);if(!value.empty())actor[Fold(prop)]=value;}
            result["actors"].push_back(actor);
        }
    }
    return result;
}

// Grid snapping. Zero or negative spacing leaves the coordinate untouched.
inline double SnapTo(double value,double grid)
{
    if(!std::isfinite(value))throw std::runtime_error("Position is out of range.");
    if(!std::isfinite(grid) || grid<=0)return value;
    return std::round(value/grid)*grid;
}
inline Vector SnapVector(const Vector& value,const Vector& grid)
{
    Vector result=value;for(int i=0;i<3;++i)result[i]=SnapTo(value[i],grid[i]);CheckVector(result);return result;
}

// Dragging a preview face keeps the opposite face where it is: the dimension
// changes by the drag distance and the centre moves by half of it.
inline void Resize(Json& spec,Pose& frame,int axis,int side,double delta)
{
    if(axis<0 || axis>2 || (side!=1 && side!=-1) || !std::isfinite(delta))throw std::runtime_error("Invalid resize handle.");
    if(axis==2 && side!=1)throw std::runtime_error("Blockouts are anchored on their floor.");
    const char* field=axis==0?"width":axis==1?"length":"height";
    const double old=spec.at(field);
    const double wanted=std::clamp(old+delta*(axis==2?1:side),1.0,65536.0);
    const double applied=(wanted-old)*(axis==2?1:side);
    spec[field]=wanted;
    const auto kind=spec.value("kind",std::string());
    // A spiral is round: its width and length are one diameter.
    if(kind=="Spiral" && axis!=2){spec["width"]=wanted;spec["length"]=wanted;}
    if(StairKind(kind))RecountSteps(spec);
    if(axis==2)return;
    Vector local{};local[axis]=applied/2;
    frame.position=TransformPoint(local,frame);
    CheckVector(frame.position);
}

// Axis-aligned bounds used for snapping a dragged piece against the geometry
// already in the map.
struct Extent { Vector lo{},hi{}; };
inline Extent Bounds(const std::vector<Vector>& points)
{
    if(points.empty())throw std::runtime_error("Bounds need at least one point.");
    Extent box{points.front(),points.front()};
    for(const auto& p:points)
    {
        CheckVector(p);
        for(int i=0;i<3;++i){box.lo[i]=std::min(box.lo[i],p[i]);box.hi[i]=std::max(box.hi[i],p[i]);}
    }
    return box;
}
// The smallest move along one axis that lines a dragged face up with a face
// already in the map. Returns 0 when nothing is within tolerance.
inline double SnapToFaces(double low,double high,const std::vector<Extent>& others,int axis,double tolerance)
{
    if(axis<0 || axis>2 || !std::isfinite(tolerance) || tolerance<=0)return 0;
    double best=0,distance=tolerance;
    for(const auto& other:others)
        for(double face:{other.lo[axis],other.hi[axis]})
            for(double edge:{low,high})
            {
                const double delta=face-edge;
                if(std::abs(delta)<distance){distance=std::abs(delta);best=delta;}
            }
    return best;
}
// Snaps a moving box to neighbouring faces on the two axes a design view can
// show. Axes with no neighbour in range are left for the grid to handle.
inline Vector SnapBoxToNeighbours(const Extent& moving,const std::vector<Extent>& others,int first,int second,double tolerance)
{
    Vector offset{};
    for(int axis:{first,second})
    {
        if(axis<0 || axis>2)continue;
        offset[axis]=SnapToFaces(moving.lo[axis],moving.hi[axis],others,axis,tolerance);
    }
    return offset;
}
// Projected outline of a brush, for filled floor-plan drawing. Andrew's
// monotone chain; collinear points are dropped.
using Point = std::array<double,2>;
inline std::vector<Point> ConvexHull(std::vector<Point> points)
{
    for(const auto& p:points)
        for(double n:p)
            if(!std::isfinite(n))throw std::runtime_error("Outline point is out of range.");
    std::sort(points.begin(),points.end());
    points.erase(std::unique(points.begin(),points.end()),points.end());
    if(points.size()<3)return points;
    auto cross=[](const Point& o,const Point& a,const Point& b)
    {
        return (a[0]-o[0])*(b[1]-o[1])-(a[1]-o[1])*(b[0]-o[0]);
    };
    std::vector<Point> hull(2*points.size());
    size_t count=0;
    for(size_t i=0;i<points.size();++i)
    {
        while(count>=2 && cross(hull[count-2],hull[count-1],points[i])<=0)--count;
        hull[count++]=points[i];
    }
    for(size_t i=points.size()-1,lower=count+1;i>0;--i)
    {
        while(count>=lower && cross(hull[count-2],hull[count-1],points[i-1])<=0)--count;
        hull[count++]=points[i-1];
    }
    hull.resize(count?count-1:0);
    return hull;
}

// Movement limits. These are starting values to verify in game, not engine
// guarantees: class defaults describe collision, not traversal.
inline Json DefaultMovement()
{
    // A standing player is about 96 units wide and 180 tall in play. A vent is
    // taken crouched (125 high); a crawlway at a slow walk (105 high).
    return {{"stepHeight",35.0},{"slope",45.0},{"width",96.0},{"height",180.0},
            {"crouchWidth",96.0},{"crouchHeight",125.0},{"crawlHeight",105.0},{"crouchFactor",.5},{"climbSpeed",120.0},
            {"spySpeed",300.0},{"mercSpeed",300.0}};
}
// A vent is a corridor at crouch height and a crawlway a lower one, so both are
// checked against crouch clearance instead of standing clearance.
inline bool Crouching(const std::string& kind) { return kind=="Vent" || kind=="Crawlway"; }
inline double ClearanceHeight(const std::string& kind,const Json& movement)
{
    if(kind=="Crawlway")return movement.value("crawlHeight",movement.at("crouchHeight").get<double>()/2);
    return Crouching(kind)?movement.at("crouchHeight").get<double>():movement.at("height").get<double>();
}
inline double ClearanceWidth(const std::string& kind,const Json& movement)
{
    return Crouching(kind)?movement.at("crouchWidth").get<double>():movement.at("width").get<double>();
}
inline Json Movement(const Json& design)
{
    auto result=DefaultMovement();
    if(design.is_object() && design.contains("movement"))
        for(auto& [key,value]:design.at("movement").items())
            if(result.contains(key) && value.is_number())result[key]=value;
    for(auto& [key,value]:result.items())
    {
        const double n=value;
        if(!std::isfinite(n) || n<=0 || n>100000)throw std::runtime_error("Movement limits must be positive numbers.");
        if(key=="slope" && n>=90)throw std::runtime_error("Enter a walkable slope below 90 degrees.");
    }
    return result;
}
inline double RampSlope(double rise,double run)
{
    if(!std::isfinite(rise) || !std::isfinite(run) || run<=0)throw std::runtime_error("A ramp needs a positive length.");
    return std::atan2(rise,run)*180/3.14159265358979323846;
}
inline std::string Round(double value,int decimals=0)
{
    std::ostringstream text;text.setf(std::ios::fixed);text.precision(decimals);text<<value;return text.str();
}
// Warnings only: a blockout is never blocked by a traversal check.
inline std::vector<std::string> TraversalWarnings(const Json& spec,const Json& design)
{
    Geometry(spec);auto movement=Movement(design);std::vector<std::string> warnings;
    const double w=spec.at("width"),l=spec.at("length"),h=spec.at("height");
    const double step=movement.at("stepHeight"),slope=movement.at("slope");
    const auto kind=spec.at("kind").get<std::string>();
    const double clearWidth=ClearanceWidth(kind,movement),clearHeight=ClearanceHeight(kind,movement);
    if(StairKind(kind))
    {
        const int steps=spec.value("steps",8);
        const double rise=h/std::max(steps,1);
        if(rise>step)warnings.push_back("Each step rises "+Round(rise,1)+" units, above the "+Round(step,1)+"-unit step height. Use "+std::to_string(static_cast<int>(std::ceil(h/step)))+" steps or more.");
    }
    if(kind=="Ramp")
    {
        const double angle=RampSlope(h,l);
        if(angle>slope)warnings.push_back("The ramp rises at "+Round(angle,1)+" degrees, above the "+Round(slope,1)+"-degree walkable slope. Lengthen it to "+Round(h/std::tan(slope*3.14159265358979323846/180),0)+" units.");
    }
    if(kind=="Platform" && h>step)warnings.push_back("The platform is "+Round(h,1)+" units high, above the "+Round(step,1)+"-unit step height. Reach it with stairs, a ramp or a jump.");
    if(kind=="Doorway" || kind=="Room" || kind=="Corridor" || Crouching(kind))
    {
        const std::string posture=Crouching(kind)?(kind=="Crawlway"?" crawling":" crouching"):"";
        if(w<clearWidth)warnings.push_back("The "+Fold(kind)+" is "+Round(w,1)+" units wide, below the "+Round(clearWidth,1)+"-unit"+posture+" clearance.");
        if(h<clearHeight)warnings.push_back("The "+Fold(kind)+" is "+Round(h,1)+" units high, below the "+Round(clearHeight,1)+"-unit"+posture+" clearance.");
        // A vent a standing pawn fits through is not a vent.
        if(Crouching(kind) && h>=movement.at("height").get<double>())
            warnings.push_back("At "+Round(h,1)+" units the "+Fold(kind)+" is tall enough to walk through; lower it below "+Round(movement.at("height").get<double>(),1)+" units or use a corridor.");
    }
    return warnings;
}

// Doorways in a room wall. Offsets run along the wall from its centre, in the
// room's own local axes, so a rotated room places its doorways with it.
inline Pose WallDoorway(const Json& room,const Pose& pose,const Json& doorway,const std::string& wall,double offset,double sill)
{
    Geometry(room);Geometry(doorway);
    const auto host=room.at("kind").get<std::string>();
    if(host!="Room" && host!="Corridor" && !Crouching(host))throw std::runtime_error("Select a room, corridor or vent piece first.");
    const double w=room.at("width"),l=room.at("length"),h=room.at("height"),t=room.value("thickness",16.0);
    const double doorWidth=doorway.at("width"),doorHeight=doorway.at("height"),depth=doorway.at("length");
    if(!std::isfinite(offset) || !std::isfinite(sill) || sill<0)throw std::runtime_error("Enter a finite offset and a sill at or above the floor.");
    const bool alongX=wall=="+Y" || wall=="-Y";
    const double span=alongX?w:l;
    if(std::abs(offset)+doorWidth/2>span/2)throw std::runtime_error("The doorway extends past that wall. Reduce its width or offset.");
    if(sill+doorHeight>h)throw std::runtime_error("The doorway reaches above the ceiling. Lower the sill or its height.");
    if(depth<t)throw std::runtime_error("The doorway is shallower than the wall. Use at least the wall thickness.");
    // The opening starts at the interior face and cuts outward through the
    // wall, so a doorway of the wall's own thickness removes exactly the slab.
    Vector local{};
    if(wall=="+Y")local={offset,l/2+depth/2,sill};
    else if(wall=="-Y")local={offset,-l/2-depth/2,sill};
    else if(wall=="+X")local={w/2+depth/2,offset,sill};
    else if(wall=="-X")local={-w/2-depth/2,offset,sill};
    else throw std::runtime_error("Choose a +X, -X, +Y or -Y wall.");
    Pose result;
    result.position=TransformPoint(local,pose);
    result.rotation=pose.rotation;
    if(!alongX)result.rotation[1]=static_cast<int>((static_cast<int64_t>(pose.rotation[1])+16384)%65536);
    CheckVector(result.position);
    return result;
}

// World bounds of a placed or previewed piece.
inline Extent PieceExtent(const Json& spec,const Pose& pose)
{
    std::vector<Vector> points;
    for(auto& solid:Geometry(spec))
        for(auto& face:solid.faces)
            for(auto& v:face)points.push_back(TransformPoint(v,pose));
    return Bounds(points);
}
inline bool Overlaps(const Extent& a,const Extent& b,double margin)
{
    for(int axis=0;axis<3;++axis)
        if(a.hi[axis]+margin<b.lo[axis] || b.hi[axis]+margin<a.lo[axis])return false;
    return true;
}
// Design check: layout problems that a plan view hides. Each issue names the
// piece it belongs to by index so the panel can select it.
struct Issue { std::string severity,text; int piece=-1; };
inline std::vector<Issue> DesignIssues(const Json& pieces,const Json& design)
{
    std::vector<Issue> issues;
    struct Placed { std::string kind,name; Extent extent; double thickness; };
    std::vector<Placed> placed;
    for(size_t i=0;i<pieces.size();++i)
    {
        const auto& piece=pieces[i];
        const auto& spec=piece.at("spec");
        const Pose pose{piece.at("position").get<Vector>(),piece.at("rotation").get<Rotation>()};
        try
        {
            placed.push_back({spec.at("kind"),spec.value("name",std::string("Piece "+std::to_string(i+1))),PieceExtent(spec,pose),spec.value("thickness",16.0)});
            for(const auto& warning:TraversalWarnings(spec,design))issues.push_back({"warning",placed.back().name+": "+warning,static_cast<int>(i)});
        }
        catch(const std::exception& e){issues.push_back({"error",std::string("Piece ")+std::to_string(i+1)+" cannot be measured: "+e.what(),static_cast<int>(i)});placed.push_back({});}
    }
    auto space=[](const std::string& kind){return kind=="Room" || kind=="Corridor" || Crouching(kind);};
    for(size_t i=0;i<placed.size();++i)
    {
        const auto& a=placed[i];
        if(a.kind.empty())continue;
        if(a.kind=="Room")
        {
            bool reached=false;
            for(size_t j=0;j<placed.size() && !reached;++j)
                if(i!=j && !placed[j].kind.empty() && (placed[j].kind=="Doorway" || placed[j].kind=="Corridor" || Crouching(placed[j].kind)))
                    reached=Overlaps(a.extent,placed[j].extent,a.thickness+1);
            if(!reached)issues.push_back({"error",a.name+" has no doorway, corridor or vent reaching any of its walls.",static_cast<int>(i)});
        }
        if(a.kind=="Doorway")
        {
            bool host=false;
            for(size_t j=0;j<placed.size() && !host;++j)
                if(i!=j && space(placed[j].kind))host=Overlaps(a.extent,placed[j].extent,placed[j].thickness+1);
            if(!host)issues.push_back({"error",a.name+" does not cut into any room, corridor or vent.",static_cast<int>(i)});
        }
        for(size_t j=i+1;j<placed.size();++j)
        {
            const auto& b=placed[j];
            if(!space(a.kind) || !space(b.kind))continue;
            // Two carved spaces sharing interior volume merge into one.
            if(Overlaps(a.extent,b.extent,-2))
                issues.push_back({"warning",a.name+" and "+b.name+" carve into each other; they will merge into one space.",static_cast<int>(i)});
        }
    }
    return issues;
}
// Quick-add: a new piece placed flush against the outside of a host piece's
// wall, centred on a point along it. Doorways cut through the wall instead and
// use WallDoorway.
inline Pose AttachedPiece(const Json& host,const Pose& hostPose,const Json& piece,const std::string& wall,double offset)
{
    Geometry(host);Geometry(piece);
    const auto kind=host.at("kind").get<std::string>();
    if(kind!="Room" && kind!="Corridor" && !Crouching(kind))throw std::runtime_error("Attach new pieces to a room, corridor or vent.");
    if(!std::isfinite(offset))throw std::runtime_error("Enter a finite offset along the wall.");
    const double w=host.at("width"),l=host.at("length"),t=host.value("thickness",16.0);
    const double length=piece.at("length");
    const bool alongX=wall=="+Y" || wall=="-Y";
    // Its near face meets the outer face of the host's wall.
    const double away=(alongX?l/2:w/2)+t+length/2;
    Vector local{};
    if(wall=="+Y")local={offset,away,0};
    else if(wall=="-Y")local={offset,-away,0};
    else if(wall=="+X")local={away,offset,0};
    else if(wall=="-X")local={-away,offset,0};
    else throw std::runtime_error("Choose a +X, -X, +Y or -Y wall.");
    Pose result;
    result.position=TransformPoint(local,hostPose);
    result.rotation=hostPose.rotation;
    if(!alongX)result.rotation[1]=static_cast<int>((static_cast<int64_t>(hostPose.rotation[1])+16384)%65536);
    CheckVector(result.position);
    return result;
}

// Routes. Lengths are measured in three dimensions along the clicked points,
// with the climb split out: a ladder or a drop is not walked at running speed.
inline double PathLength(const Json& points)
{
    double total=0;
    for(size_t i=1;i<points.size();++i)total+=Distance(points[i-1].get<Vector>(),points[i].get<Vector>());
    return total;
}
inline double TravelSeconds(double length,double speed)
{
    if(!std::isfinite(length) || length<0)throw std::runtime_error("A route needs a finite length.");
    if(!std::isfinite(speed) || speed<=0)throw std::runtime_error("Enter a positive movement speed.");
    return length/speed;
}
inline double TeamSpeed(const std::string& team,const Json& movement)
{
    if(team=="Spy")return movement.at("spySpeed");
    if(team=="Merc")return movement.at("mercSpeed");
    return 0;
}
struct Journey { double length=0,ground=0,climb=0,spy=0,merc=0; };
// Both teams' times for one route: ground distance at each team's speed,
// vertical distance at the climb speed, and a crouched route slowed by the
// crouch factor.
inline Journey RouteJourney(const Json& annotation,const Json& design)
{
    auto movement=Movement(design);
    const auto& points=annotation.at("points");
    Journey journey;
    for(size_t i=1;i<points.size();++i)
    {
        const auto from=points[i-1].get<Vector>(),to=points[i].get<Vector>();
        CheckVector(from);CheckVector(to);
        const double rise=std::abs(to[2]-from[2]);
        const double run=std::hypot(to[0]-from[0],to[1]-from[1]);
        // A step-up is taken in stride; anything steeper is climbed or dropped.
        const bool climbed=rise>movement.at("stepHeight").get<double>() && rise>run;
        journey.length+=Distance(from,to);
        if(climbed)journey.climb+=rise;
        else journey.ground+=Distance(from,to);
    }
    const double crouch=annotation.value("crouched",false)?movement.at("crouchFactor").get<double>():1;
    const double climbSeconds=journey.climb>0?TravelSeconds(journey.climb,movement.at("climbSpeed")):0;
    journey.spy=(journey.ground>0?TravelSeconds(journey.ground,movement.at("spySpeed").get<double>()*crouch):0)+climbSeconds;
    journey.merc=(journey.ground>0?TravelSeconds(journey.ground,movement.at("mercSpeed").get<double>()*crouch):0)+climbSeconds;
    return journey;
}
// One line for a route: how far it runs and how long each team takes on it.
inline std::string RouteTimes(const Json& annotation,const Json& design)
{
    const auto& points=annotation.at("points");
    if(points.size()<2)return "";
    const auto journey=RouteJourney(annotation,design);
    std::string text=Round(journey.length)+" units";
    if(journey.climb>0)text+=" ("+Round(journey.climb)+" climbed)";
    text+=", spy "+Round(journey.spy,1)+" s, merc "+Round(journey.merc,1)+" s";
    if(annotation.value("crouched",false))text+=", crouched";
    return text;
}
inline std::string RouteSummary(const Json& annotation,const Json& design)
{
    const auto& points=annotation.at("points");
    auto name=annotation.at("name").get<std::string>();
    if(points.size()<2)return name;
    auto team=annotation.value("team",std::string("Any"));
    if(team!="Any")name+=" ("+Fold(team)+")";
    return name+": "+RouteTimes(annotation,design);
}
// Two routes side by side, each timed for the team it belongs to. A route
// without a team is timed for whichever team the other one is not.
inline std::string RouteComparison(const Json& first,const Json& second,const Json& design)
{
    struct Leg { std::string name,team; double seconds,length; };
    std::vector<Leg> legs;
    std::vector<const Json*> annotations{&first,&second};
    for(size_t i=0;i<annotations.size();++i)
    {
        const Json& annotation=*annotations[i];
        if(annotation.at("points").size()<2)throw std::runtime_error("Compare two routes with at least two points each.");
        auto team=annotation.value("team",std::string("Any"));
        if(team=="Any")
        {
            const auto other=annotations[1-i]->value("team",std::string("Any"));
            team=other=="Spy"?"Merc":other=="Merc"?"Spy":"Spy";
        }
        const auto journey=RouteJourney(annotation,design);
        legs.push_back({annotation.at("name"),team,team=="Spy"?journey.spy:journey.merc,journey.length});
    }
    std::string text;
    for(auto& leg:legs)
        text+=leg.name+" ("+Fold(leg.team)+"): "+Round(leg.length)+" units, "+Round(leg.seconds,1)+" s. ";
    const double difference=legs[0].seconds-legs[1].seconds;
    if(std::abs(difference)<.05)return text+"Both arrive together.";
    const size_t winner=difference<0?0:1;
    return text+legs[winner].name+" arrives "+Round(std::abs(difference),1)+" s earlier.";
}

// Floor filters keep one storey readable in the top view.
inline bool WithinFloor(double low,double high,double first,double second)
{
    return std::max(first,second)>=low && std::min(first,second)<=high;
}

// Named layers are kept in the map itself through the native actor Group
// field, so they travel with the .sdc instead of only the workspace file.
inline bool ValidGroupName(const std::string& name)
{
    if(name.empty() || name.size()>62)return false;
    if(!std::isalpha(static_cast<unsigned char>(name[0])) && name[0]!='_')return false;
    for(char c:name)if(!std::isalnum(static_cast<unsigned char>(c)) && c!='_')return false;
    return Fold(name)!="none";
}
inline std::vector<std::string> GroupNames(const std::string& groups)
{
    std::vector<std::string> result;std::istringstream text(groups);std::string name;
    while(std::getline(text,name,','))
    {
        auto begin=name.find_first_not_of(" \t"),end=name.find_last_not_of(" \t");
        if(begin==std::string::npos)continue;
        name=name.substr(begin,end-begin+1);
        if(Fold(name)=="none" || name.empty())continue;
        if(std::find_if(result.begin(),result.end(),[&](const std::string& other){return Fold(other)==Fold(name);})==result.end())result.push_back(name);
    }
    return result;
}
inline std::string JoinGroups(const std::vector<std::string>& names)
{
    if(names.empty())return "None";
    std::string result;for(auto& name:names)result+=(result.empty()?"":",")+name;return result;
}
inline std::string AddGroup(const std::string& groups,const std::string& name)
{
    if(!ValidGroupName(name))throw std::runtime_error("Use 1-62 letters, digits or underscores for a layer that travels with the map.");
    auto names=GroupNames(groups);
    if(std::find_if(names.begin(),names.end(),[&](const std::string& other){return Fold(other)==Fold(name);})==names.end())names.push_back(name);
    return JoinGroups(names);
}
inline std::string RemoveGroup(const std::string& groups,const std::string& name)
{
    auto names=GroupNames(groups);
    names.erase(std::remove_if(names.begin(),names.end(),[&](const std::string& other){return Fold(other)==Fold(name);}),names.end());
    return JoinGroups(names);
}

// Workspace files travel with a packaged map. Reference images are stored by
// file name and resolved against the editor's References folder.
inline Json References(const Json& design)
{
    Json result=Json::array();
    if(design.contains("references") && design.at("references").is_array())result=design.at("references");
    else if(design.contains("reference"))
    {
        auto legacy=design.at("reference");
        auto path=legacy.value("path",std::string());
        legacy["file"]=std::filesystem::path(path).filename().string();
        legacy.erase("path");
        result.push_back(legacy);
    }
    for(auto& reference:result)
    {
        auto file=reference.value("file",std::string());
        if(file.empty() || file.find_first_of("\\/:")!=std::string::npos)throw std::runtime_error("Invalid reference image in this workspace.");
        reference["plane"].get<int>();
    }
    return result;
}
inline Json ExportWorkspace(const Json& design,const std::string& map)
{
    Json portable=design;
    portable.erase("reference");
    portable["references"]=References(design);
    return {{"version",1},{"map",map},{"design",portable}};
}
inline Json ImportWorkspace(const Json& file,const std::string& mapKey)
{
    if(!file.is_object() || file.value("version",0)!=1 || !file.contains("design"))throw std::runtime_error("Choose a Map Design workspace file exported by this editor.");
    Json design=file.at("design");
    for(const char* key:{"pieces","layers","annotations"})if(!design.contains(key) || !design.at(key).is_array())design[key]=Json::array();
    design["references"]=References(design);
    design.erase("reference");
    // Imported pieces describe this map's copies of the same brushes.
    for(auto& piece:design["pieces"])piece["map"]=mapKey;
    return design;
}
}
