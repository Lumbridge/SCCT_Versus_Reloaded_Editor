#pragma once
#include "MapDesignModel.h"
#include <limits>
#include <optional>

// Smooth Staircase: an invisible blocking volume laid over a staircase's step
// edges so pawns walk up and down it as a ramp instead of bumping over each
// riser. The stock stair builders (linear, curved and spiral) leave no record
// on the brush they add, so a staircase is recognised by its shape: treads are
// the upward faces, and two treads are consecutive steps when the lower one's
// back edge lies under the upper one's front edge (the nosing). The ramp's top
// runs through every nosing and on down to the floor one step in front of the
// first, so the bottom step is smoothed too; a straight flight is one flat slab, a winding one
// (curved or spiral) a helical slab of triangles, closed into one solid either
// way so there are no seams to catch on.
namespace Workflow::StairSmooth
{
using Design::Face;
using Design::Solid;

// Tallest riser still treated as a step.
constexpr double kMaxRise=64;
// Treads within this height of each other are one level; edges within this
// distance of each other line up.
constexpr double kTolerance=0.5;
// Shortest nosing that counts: shorter shared edges are corners touching.
constexpr double kMinNosing=4;
// A tread this many times deeper than the flight's usual tread is a landing:
// the ramp stops at it and starts again after it.
constexpr double kLandingRatio=2.5;

// One closed solid (world coordinates) and the input brush it smooths.
struct Ramp { Solid solid; size_t brush=0; };
struct Result
{
    std::vector<Ramp> ramps;
    std::vector<size_t> brushes; // Input brushes found to be staircases.
    int steps=0;                 // Step edges the ramps run over.
    bool winding=false;          // A curved or spiral flight.
};

namespace Detail
{
    inline Vector Sub(const Vector& a,const Vector& b){return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};}
    inline Vector Add(const Vector& a,const Vector& b){return {a[0]+b[0],a[1]+b[1],a[2]+b[2]};}
    inline Vector Scale(const Vector& a,double s){return {a[0]*s,a[1]*s,a[2]*s};}
    inline double Dot(const Vector& a,const Vector& b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
    inline Vector Cross(const Vector& a,const Vector& b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
    inline double Length(const Vector& a){return std::sqrt(Dot(a,a));}
    // Plan-view (XY) helpers.
    inline double Dot2(const Vector& a,const Vector& b){return a[0]*b[0]+a[1]*b[1];}
    inline double Cross2(const Vector& a,const Vector& b){return a[0]*b[1]-a[1]*b[0];}
    inline double Length2(const Vector& a){return std::sqrt(Dot2(a,a));}
    inline Vector Mid(const Vector& a,const Vector& b){return Scale(Add(a,b),0.5);}
    // Newell's normal: right-handed, so it points out of a face wound the way
    // Design::Box winds its faces.
    inline Vector Normal(const Face& f)
    {
        Vector n{};
        for(size_t i=0;i<f.size();++i)
        {
            const auto& p=f[i];const auto& q=f[(i+1)%f.size()];
            n[0]+=(p[1]-q[1])*(p[2]+q[2]);n[1]+=(p[2]-q[2])*(p[0]+q[0]);n[2]+=(p[0]-q[0])*(p[1]+q[1]);
        }
        return n;
    }
    // Positive when the faces wind outward the way Design::Box does.
    inline double SignedVolume(const std::vector<Face>& faces)
    {
        double v=0;
        for(const auto& f:faces)for(size_t i=1;i+1<f.size();++i)v+=Dot(f[0],Cross(f[i],f[i+1]));
        return v/6;
    }
    inline double SegmentDistance2(const Vector& p,const Vector& a,const Vector& b)
    {
        const Vector d=Sub(b,a),w=Sub(p,a);const double l=Dot2(d,d);
        const double t=l>0?std::clamp(Dot2(w,d)/l,0.0,1.0):0.0;
        return Length2(Sub(w,Scale(d,t)));
    }
    struct Edge { Vector p,q; };
    struct Level { double z=0; std::vector<Edge> edges; };
    struct Nosing { Vector a,b; };

    // The upward faces of each brush, grouped into levels by height.
    inline std::vector<Level> Levels(const std::vector<std::vector<Face>>& brushes)
    {
        struct Tread { double z; const Face* face; };
        std::vector<Tread> treads;
        for(const auto& faces:brushes)
        {
            // Engine polygons may wind either way round; the brush's volume
            // says which. A sheet has none and no treads.
            const double volume=SignedVolume(faces);
            if(std::abs(volume)<1)continue;
            for(const auto& f:faces)
            {
                if(f.size()<3)continue;
                auto n=Normal(f);const double l=Length(n);
                if(l<1e-6)continue;
                if(volume<0)n=Scale(n,-1);
                if(n[2]/l<0.99)continue;
                double z=0;for(const auto& p:f)z+=p[2];
                treads.push_back({z/static_cast<double>(f.size()),&f});
            }
        }
        std::sort(treads.begin(),treads.end(),[](const Tread& a,const Tread& b){return a.z<b.z;});
        std::vector<Level> levels;size_t inLevel=0;
        for(const auto& t:treads)
        {
            if(levels.empty() || t.z-levels.back().z>kTolerance){levels.push_back({t.z,{}});inLevel=0;}
            auto& level=levels.back();
            level.z=(level.z*static_cast<double>(inLevel)+t.z)/static_cast<double>(inLevel+1);++inLevel;
            for(size_t i=0;i<t.face->size();++i)level.edges.push_back({(*t.face)[i],(*t.face)[(i+1)%t.face->size()]});
        }
        for(auto& level:levels)for(auto& e:level.edges){e.p[2]=level.z;e.q[2]=level.z;}
        return levels;
    }
    // Where the upper tread's front edge meets the lower tread's back edge in
    // plan: the stretch of the upper level's edges that lies along the lower
    // level's edges, longest line first.
    inline std::optional<Nosing> FindNosing(const Level& upper,const Level& lower)
    {
        struct Overlap { Vector origin,direction;double lo,hi; };
        std::vector<Overlap> found;
        for(const auto& e:upper.edges)
        {
            const Vector d=Sub(e.q,e.p);const double l=Length2(d);
            if(l<kMinNosing)continue;
            const Vector u{d[0]/l,d[1]/l,0};
            for(const auto& f:lower.edges)
            {
                const Vector fp=Sub(f.p,e.p),fq=Sub(f.q,e.p);
                if(std::abs(Cross2(u,fp))>kTolerance || std::abs(Cross2(u,fq))>kTolerance)continue;
                const double t0=Dot2(u,fp),t1=Dot2(u,fq);
                const double lo=std::max(0.0,std::min(t0,t1)),hi=std::min(l,std::max(t0,t1));
                if(hi-lo>=kMinNosing)found.push_back({e.p,u,lo,hi});
            }
        }
        if(found.empty())return std::nullopt;
        const auto best=*std::max_element(found.begin(),found.end(),[](const Overlap& a,const Overlap& b){return a.hi-a.lo<b.hi-b.lo;});
        double lo=best.lo,hi=best.hi;
        for(const auto& o:found)
        {
            const Vector s=Add(o.origin,Scale(o.direction,o.lo)),t=Add(o.origin,Scale(o.direction,o.hi));
            if(std::abs(Cross2(best.direction,Sub(s,best.origin)))>kTolerance || std::abs(Cross2(best.direction,Sub(t,best.origin)))>kTolerance)continue;
            const double a=Dot2(best.direction,Sub(s,best.origin)),b=Dot2(best.direction,Sub(t,best.origin));
            lo=std::min({lo,a,b});hi=std::max({hi,a,b});
        }
        Nosing n{Add(best.origin,Scale(best.direction,lo)),Add(best.origin,Scale(best.direction,hi))};
        n.a[2]=n.b[2]=upper.z;
        return n;
    }
    inline bool OnLevelEdge(const Vector& p,const Level& level)
    {
        for(const auto& e:level.edges)if(SegmentDistance2(p,e.p,e.q)<=1.0)return true;
        return false;
    }
    // The step from one nosing to the next, taken back one step from the
    // first, at height z: a turn and a shift in plan, so it follows a spiral
    // as well as a straight flight.
    inline Nosing StepBack(const Nosing& n1,const Nosing& n2,double z)
    {
        const Vector d1=Sub(n1.b,n1.a),d2=Sub(n2.b,n2.a);
        const double turn=std::atan2(Cross2(d1,d2),Dot2(d1,d2)),c=std::cos(turn),s=std::sin(turn);
        auto rotate=[&](const Vector& v,double sign){return Vector{c*v[0]-sign*s*v[1],sign*s*v[0]+c*v[1],0};};
        const Vector shift=Sub(n2.a,rotate(n1.a,1));
        // n1 = R n0 + shift, so n0 = R^-1 (n1 - shift).
        Nosing n0{rotate(Sub(n1.a,shift),-1),rotate(Sub(n1.b,shift),-1)};
        n0.a[2]=n0.b[2]=z;
        return n0;
    }
    // The front edge of the lowest tread, which no tread below marks.
    inline std::optional<Nosing> FirstNosing(const Nosing& n1,const Nosing& n2,const Level& lowest)
    {
        const auto n0=StepBack(n1,n2,lowest.z);
        if(!OnLevelEdge(n0.a,lowest) || !OnLevelEdge(n0.b,lowest) || !OnLevelEdge(Mid(n0.a,n0.b),lowest))return std::nullopt;
        return n0;
    }
    inline void Orient(Face& f,const Vector& outward)
    {
        if(Dot(Normal(f),outward)<0)std::reverse(f.begin(),f.end());
    }
    inline bool Degenerate(const Face& f){return Length(Normal(f))<1e-3;}
    // The closed slab under a run of nosings, thickness units deep.
    inline Solid RampSolid(const std::vector<Nosing>& run,double thickness,bool& winding)
    {
        const size_t m=run.size()-1;
        // A flat flight: every nosing on the plane through the first and last.
        bool planar=false;
        {
            const Vector n=Cross(Sub(run[0].b,run[0].a),Sub(run[m].a,run[0].a));const double l=Length(n);
            if(l>1e-6)
            {
                planar=true;
                for(const auto& s:run)for(const auto& p:{s.a,s.b})if(std::abs(Dot(Sub(p,run[0].a),n))/l>kTolerance)planar=false;
            }
        }
        if(!planar)winding=true;
        std::vector<size_t> index;
        if(planar)index={0,m};else for(size_t i=0;i<=m;++i)index.push_back(i);
        const Vector down{0,0,-thickness};
        auto below=[&](const Vector& p){return Add(p,down);};
        std::vector<Face> faces;
        auto add=[&](Face f,const Vector& outward){if(Degenerate(f))return;Orient(f,outward);faces.push_back(std::move(f));};
        for(size_t k=0;k+1<index.size();++k)
        {
            const auto& s=run[index[k]];const auto& t=run[index[k+1]];
            std::vector<Face> top=planar?std::vector<Face>{{s.a,s.b,t.b,t.a}}:std::vector<Face>{{s.a,s.b,t.b},{s.a,t.b,t.a}};
            for(auto& f:top)
            {
                Face under;for(const auto& p:f)under.push_back(below(p));
                add(f,{0,0,1});add(under,{0,0,-1});
            }
            // Walls drop straight down from the ramp's two side edges.
            const Vector across=Sub(Mid(s.a,t.a),Mid(s.b,t.b));
            add({s.a,t.a,below(t.a),below(s.a)},{across[0],across[1],0});
            add({s.b,t.b,below(t.b),below(s.b)},{-across[0],-across[1],0});
        }
        const Vector back=Sub(Mid(run[0].a,run[0].b),Mid(run[1].a,run[1].b));
        add({run[0].a,run[0].b,below(run[0].b),below(run[0].a)},{back[0],back[1],0});
        const Vector ahead=Sub(Mid(run[m].a,run[m].b),Mid(run[m-1].a,run[m-1].b));
        add({run[m].a,run[m].b,below(run[m].b),below(run[m].a)},{ahead[0],ahead[1],0});
        Solid solid;solid.faces=std::move(faces);
        if(SignedVolume(solid.faces)<=0)throw std::runtime_error("The staircase ramp folds over itself.");
        return solid;
    }
}

// The ramps for brushes taken together as one staircase. Throws when they do
// not form one.
inline Result Analyse(const std::vector<std::vector<Face>>& brushes)
{
    using namespace Detail;
    for(const auto& faces:brushes)for(const auto& f:faces)for(const auto& p:f)Design::CheckVector(p);
    const auto levels=Levels(brushes);
    double lowestPoint=std::numeric_limits<double>::max();
    for(const auto& faces:brushes)for(const auto& f:faces)for(const auto& p:f)lowestPoint=std::min(lowestPoint,p[2]);
    if(levels.size()<3)throw std::runtime_error("A staircase needs at least three treads.");
    std::vector<std::optional<Nosing>> nosings(levels.size());
    for(size_t k=1;k<levels.size();++k)
    {
        const double rise=levels[k].z-levels[k-1].z;
        if(rise>kTolerance && rise<=kMaxRise)nosings[k]=FindNosing(levels[k],levels[k-1]);
    }
    Result result;
    for(size_t k=1;k<levels.size();)
    {
        if(!nosings[k]){++k;continue;}
        // A flight: consecutive levels each joined to the one below.
        const size_t first=k;std::vector<Nosing> flight;std::vector<double> rises;
        for(;k<levels.size() && nosings[k];++k)
        {
            auto n=*nosings[k];
            // Same side first as the step below, so the ramp does not twist.
            if(!flight.empty())
            {
                const auto& p=flight.back();
                if(Length2(Sub(n.a,p.b))+Length2(Sub(n.b,p.a))<Length2(Sub(n.a,p.a))+Length2(Sub(n.b,p.b)))std::swap(n.a,n.b);
            }
            flight.push_back(n);rises.push_back(levels[k].z-levels[k-1].z);
        }
        if(flight.size()<2)continue;
        // Landings split the flight.
        std::vector<double> depths;
        for(size_t i=1;i<flight.size();++i)depths.push_back(Length2(Sub(Mid(flight[i].a,flight[i].b),Mid(flight[i-1].a,flight[i-1].b))));
        auto sorted=depths;std::nth_element(sorted.begin(),sorted.begin()+sorted.size()/2,sorted.end());
        const double usual=sorted[sorted.size()/2];
        std::vector<std::vector<Nosing>> runs{{flight[0]}};
        for(size_t i=1;i<flight.size();++i)
        {
            if(usual>0 && depths[i-1]>kLandingRatio*usual)runs.push_back({});
            runs.back().push_back(flight[i]);
        }
        auto sortedRises=rises;std::nth_element(sortedRises.begin(),sortedRises.begin()+sortedRises.size()/2,sortedRises.end());
        const double rise=sortedRises[sortedRises.size()/2];
        const double thickness=std::clamp(rise,8.0,32.0);
        auto alignTo=[&](Nosing n,const Nosing& next)
        {
            if(Length2(Sub(n.a,next.a))>Length2(Sub(n.a,next.b)))std::swap(n.a,n.b);
            return n;
        };
        // The lowest tread's front edge, when it lines up with the rest.
        if(runs[0].size()>=2)if(auto n0=FirstNosing(runs[0][0],runs[0][1],levels[first-1]))
            runs[0].insert(runs[0].begin(),alignTo(*n0,runs[0][0]));
        // Then one more step back, down at the floor: the bottom riser becomes
        // part of the slope instead of a step up onto the ramp. The floor is a
        // rise below the lowest tread, and never below the stairs themselves.
        bool floor=false;
        if(runs[0].size()>=2)
        {
            const double z=std::max(runs[0][0].a[2]-rise,lowestPoint);
            if(runs[0][0].a[2]-z>kTolerance)
            {
                runs[0].insert(runs[0].begin(),alignTo(StepBack(runs[0][0],runs[0][1],z),runs[0][0]));
                floor=true;
            }
        }
        for(size_t r=0;r<runs.size();++r)
        {
            const auto& run=runs[r];
            if(run.size()<2)continue;
            result.ramps.push_back({RampSolid(run,thickness,result.winding),0});
            result.steps+=static_cast<int>(run.size())-(r==0 && floor?1:0);
        }
    }
    if(result.ramps.empty())throw std::runtime_error("No staircase found: select a brush with at least three steps, each tread starting where the one below ends.");
    for(size_t i=0;i<brushes.size();++i)result.brushes.push_back(i);
    return result;
}
// Each brush that is a staircase on its own; failing that, all of them as one
// (stairs built from a brush per step). Ramps belong to the first brush of a
// staircase made of several.
inline Result Smooth(const std::vector<std::vector<Face>>& brushes)
{
    Result total;
    for(size_t i=0;i<brushes.size();++i)
    {
        try
        {
            auto r=Analyse({brushes[i]});
            for(auto& ramp:r.ramps){ramp.brush=i;total.ramps.push_back(std::move(ramp));}
            total.brushes.push_back(i);total.steps+=r.steps;total.winding=total.winding || r.winding;
        }
        catch(const std::runtime_error&){}
    }
    if(!total.ramps.empty() || brushes.size()<2)
    {
        if(total.ramps.empty())return Analyse(brushes);
        return total;
    }
    return Analyse(brushes);
}
}
