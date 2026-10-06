// Standalone: cl /std:c++20 /W4 /WX /EHsc tests\StairSmoothModelTests.cpp Reloaded.Editor\WorkflowModel.cpp
#include "../Reloaded.Editor/StairSmoothModel.h"
#include <iostream>
#include <map>
#include <source_location>
using namespace Workflow;
using namespace Workflow::StairSmooth;
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool Near(double a,double b,double tolerance=1e-6){return std::abs(a-b)<=tolerance;}
template<class F> void Reject(F f,std::source_location where=std::source_location::current())
{
    bool caught=false;
    try{f();}catch(const std::exception&){caught=true;}
    if(!caught)throw std::runtime_error("invalid input accepted at line "+std::to_string(where.line()));
}
// Every solid as one brush's polygons, or each solid as its own brush.
std::vector<Face> OneBrush(const std::vector<Design::Solid>& solids)
{
    std::vector<Face> faces;
    for(const auto& s:solids)for(const auto& f:s.faces)faces.push_back(f);
    return faces;
}
std::vector<std::vector<Face>> Brushes(const std::vector<Design::Solid>& solids)
{
    std::vector<std::vector<Face>> brushes;
    for(const auto& s:solids)brushes.push_back(s.faces);
    return brushes;
}
std::vector<Design::Solid> Shape(const std::string& kind,double w,double l,double h,int steps)
{
    return Design::Geometry({{"kind",kind},{"width",w},{"length",l},{"height",h},{"thickness",16},{"steps",steps}});
}
// Closed and outward: every edge is crossed once each way, every face is flat
// and the volume is positive.
void CheckClosed(const Design::Solid& solid,const char* what)
{
    using Point=std::tuple<double,double,double>;
    std::map<std::pair<Point,Point>,int> edges;
    auto point=[](const Vector& v){return Point{v[0],v[1],v[2]};};
    for(const auto& f:solid.faces)
    {
        Check(f.size()>=3 && f.size()<=16,what);
        const auto n=Detail::Normal(f);const double l=Detail::Length(n);
        Check(l>1e-3,what);
        for(const auto& p:f)Check(std::abs(Detail::Dot(Detail::Sub(p,f[0]),n))/l<1e-6,what);
        for(size_t i=0;i<f.size();++i)++edges[{point(f[i]),point(f[(i+1)%f.size()])}];
    }
    for(const auto& [edge,count]:edges)
    {
        auto back=edges.find({edge.second,edge.first});
        Check(count==1 && back!=edges.end() && back->second==1,what);
    }
    Check(Detail::SignedVolume(solid.faces)>0,what);
}
double TopZ(const Design::Solid& solid,double y)
{
    double best=-1e9;
    for(const auto& f:solid.faces)for(const auto& p:f)if(Near(p[1],y,1e-6))best=std::max(best,p[2]);
    return best;
}
int main()
{
  try
  {
    // A straight flight as one brush of step blocks (as the linear builder
    // makes it): 8 steps of 34 x 32, 128 wide. One flat slab from the first
    // step's front edge, one rise up, to the top step's.
    {
        std::vector<Design::Solid> steps;
        for(int i=0;i<8;++i)steps.push_back(Design::Box({-64,32.0*i,0},{64,32.0*(i+1),34.0*(i+1)}));
        auto r=Smooth({OneBrush(steps)});
        Check(r.ramps.size()==1 && r.steps==8 && !r.winding,"straight flight: one flat ramp over 8 step edges");
        const auto& ramp=r.ramps[0].solid;
        CheckClosed(ramp,"straight ramp is closed");
        Check(ramp.faces.size()==6,"straight ramp is one slab");
        Check(Near(TopZ(ramp,-32),0) && Near(TopZ(ramp,224),272),"the ramp runs from the floor a step in front to the last nosing");
        // Thickness: one rise, under the floor at its start.
        double lowest=1e9;for(const auto& f:ramp.faces)for(const auto& p:f)lowest=std::min(lowest,p[2]);
        Check(Near(lowest,-32),"ramp is a rise thick (34 clamps to 32)");
        // One slope, 34 up for every 32 along from the floor, on top and 32 below.
        for(const auto& f:ramp.faces)for(const auto& p:f)
            Check(Near(p[2],(p[1]+32)*34/32) || Near(p[2],(p[1]+32)*34/32-32),"one slope from the floor");
        for(const auto& f:ramp.faces)for(const auto& p:f)Check(p[0]>=-64-1e-9 && p[0]<=64+1e-9,"ramp keeps to the stair's width");
        // The engine may hand polygons wound the other way round.
        auto reversed=OneBrush(steps);for(auto& f:reversed)std::reverse(f.begin(),f.end());
        auto again=Smooth({reversed});
        Check(again.ramps.size()==1 && again.steps==8,"reversed winding reads the same");
        Check(Near(TopZ(again.ramps[0].solid,-32),0),"reversed winding gives the same ramp");
        // A flight turned 30 degrees round: still flat.
        const Pose turn{{100,-50,8},{0,5461,0}};
        auto turned=OneBrush(steps);for(auto& f:turned)for(auto& p:f)p=TransformPoint(p,turn);
        auto t=Smooth({turned});
        Check(t.ramps.size()==1 && t.steps==8 && !t.winding,"a turned flight is still one flat ramp");
        CheckClosed(t.ramps[0].solid,"turned ramp is closed");
    }
    // Map Design's straight stairs: a brush per step plus its glide ramp.
    // No brush is a staircase alone; together they are.
    {
        auto solids=Shape("Stairs",128,544,272,16);
        auto r=Smooth(Brushes(solids));
        Check(r.ramps.size()==1 && r.steps==16 && r.brushes.size()==solids.size(),"brush-per-step stairs smooth together");
        CheckClosed(r.ramps[0].solid,"brush-per-step ramp is closed");
        Check(r.ramps[0].brush==0,"ramp belongs to the first brush");
    }
    // Spirals: one full turn, as one brush and as a brush per step.
    {
        auto solids=Shape("Spiral",384,384,272,17);
        auto r=Smooth({OneBrush(solids)});
        Check(r.ramps.size()==1 && r.winding && r.steps==17,"spiral: one winding ramp over every step");
        CheckClosed(r.ramps[0].solid,"spiral ramp is closed");
        // Its top stays between the post and the outer edge.
        for(const auto& f:r.ramps[0].solid.faces)for(const auto& p:f)
        {
            const double radius=std::hypot(p[0],p[1]);
            Check(radius>=48-1e-6 && radius<=192+1e-6,"spiral ramp stays on the treads");
        }
        auto apart=Smooth(Brushes(solids));
        Check(apart.ramps.size()==1 && apart.winding && apart.steps==17,"spiral from a brush per step");
        CheckClosed(apart.ramps[0].solid,"spiral ramp from separate brushes is closed");
        // Two turns.
        auto tall=Smooth({OneBrush(Shape("Spiral",256,256,544,34))});
        Check(tall.ramps.size()==1 && tall.steps==34,"two turns of spiral");
        CheckClosed(tall.ramps[0].solid,"two-turn spiral ramp is closed");
    }
    // A curved flight of step columns, 90 degrees over 8 steps round a
    // 128-unit inner radius (as the curved builder makes it).
    {
        std::vector<Design::Solid> steps;
        const double pi=3.14159265358979323846;
        for(int i=0;i<8;++i)
        {
            const double a0=pi/2*i/8,a1=pi/2*(i+1)/8;
            std::array<Vector,8> c;
            for(int k=0;k<8;++k)
            {
                const double rad=(k&1)?256:128,ang=(k&2)?a1:a0,z=(k&4)?24.0*(i+1):0;
                c[k]={rad*std::cos(ang),rad*std::sin(ang),z};
            }
            steps.push_back(Design::Hexa(c));
        }
        auto r=Smooth({OneBrush(steps)});
        Check(r.ramps.size()==1 && r.winding && r.steps==8,"curved flight: one winding ramp");
        CheckClosed(r.ramps[0].solid,"curved ramp is closed");
    }
    // L and U stairs stop at the landing and start again after it.
    for(const char* kind:{"Stairs L","Stairs U"})
    {
        auto r=Smooth(Brushes(Shape(kind,128,416,272,18)));
        Check(r.ramps.size()==2,"a landing splits the ramp");
        for(const auto& ramp:r.ramps)CheckClosed(ramp.solid,"turning stair ramps are closed");
    }
    // Two staircases selected together: each smooths on its own.
    {
        std::vector<Design::Solid> a,b;
        for(int i=0;i<4;++i)
        {
            a.push_back(Design::Box({0,32.0*i,0},{64,32.0*(i+1),16.0*(i+1)}));
            b.push_back(Design::Box({500,32.0*i,0},{564,32.0*(i+1),20.0*(i+1)}));
        }
        auto r=Smooth({OneBrush(a),OneBrush(b),Design::Box({-600,0,0},{-500,100,100}).faces});
        Check(r.ramps.size()==2 && r.brushes==std::vector<size_t>{0,1},"each staircase gets its own ramp");
        Check(r.ramps[0].brush==0 && r.ramps[1].brush==1,"ramps belong to their brushes");
    }
    // Not staircases.
    Reject([]{Smooth({Design::Box({0,0,0},{256,256,128}).faces});});
    {
        // Two steps: nothing to smooth between.
        std::vector<Design::Solid> two{Design::Box({0,0,0},{64,32,16}),Design::Box({0,32,0},{64,64,32})};
        Reject([&]{Smooth({OneBrush(two)});});
        // Risers too tall to be steps.
        std::vector<Design::Solid> tall;
        for(int i=0;i<4;++i)tall.push_back(Design::Box({0,64.0*i,0},{64,64.0*(i+1),100.0*(i+1)}));
        Reject([&]{Smooth({OneBrush(tall)});});
        // Blocks that do not meet: no shared step edge.
        std::vector<Design::Solid> apart;
        for(int i=0;i<4;++i)apart.push_back(Design::Box({0,48.0*i,0},{64,48.0*i+32,16.0*(i+1)}));
        Reject([&]{Smooth({OneBrush(apart)});});
        // A sheet.
        Reject([]{Smooth({{{{0,0,0},{64,0,0},{64,64,0},{0,64,0}}}});});
    }
    Reject([]{Smooth({{{{0,0,0},{1e9,0,0},{0,1,0}}}});});

    std::cout<<"Stair smooth model tests passed\n";
    return 0;
  }
  catch(const std::exception& e){std::cerr<<"FAILED: "<<e.what()<<"\n";return 1;}
}
