#pragma once
#include "WorkflowModel.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// Engine-independent preparation of Emitter Library entries for the private
// preview level (EmitterPreview.cpp): every actor and inline object gets a
// name that exists only in the preview package, map-owned references become
// None, one-shot/trigger/sound/collision state is neutralised for looping, and
// the camera is framed from the emitters' own ranges.
namespace Workflow::EmitterPreviewModel
{
    inline std::string Trim(const std::string& s)
    {
        auto first=s.find_first_not_of(" \t\r\n");
        return first==std::string::npos?std::string():s.substr(first,s.find_last_not_of(" \t\r\n")-first+1);
    }
    // Property key of a T3D line, folded: "SizeScale(0)=(...)" -> "sizescale".
    inline std::string Key(const std::string& line)
    {
        auto t=Trim(line);auto end=t.find_first_of("=(");
        return end==std::string::npos?std::string():Fold(Trim(t.substr(0,end)));
    }
    // Value of Key= in a Begin line; its position and length for replacement.
    inline bool Token(const std::string& line,const std::string& key,size_t& at,size_t& length)
    {
        auto folded=Fold(line),wanted=Fold(key)+"=";
        for(size_t i=folded.find(wanted);i!=std::string::npos;i=folded.find(wanted,i+1))
        {
            if(i && folded[i-1]!=' ' && folded[i-1]!='\t')continue;
            at=i+wanted.size();length=0;
            if(at<line.size() && line[at]=='"'){auto close=line.find('"',at+1);if(close==std::string::npos)return false;length=close+1-at;}
            else while(at+length<line.size() && line[at+length]!=' ' && line[at+length]!='\t' && line[at+length]!='\r')++length;
            return length>0;
        }
        return false;
    }
    inline std::string Unquote(std::string value){return value.size()>=2 && value.front()=='"' && value.back()=='"'?value.substr(1,value.size()-2):value;}

    struct Actor { std::string name,type,text;std::vector<std::string> assets,dropped;int objects{}; };
    // Actor properties that tie an entry to its source map, place it, or make
    // it audible; Location/Rotation are written back from the entry item.
    inline const std::set<std::string>& ActorStrip()
    {
        static const std::set<std::string> keys{"location","rotation","tag","event","group","level","region","xlevel","name","bselected","ambientsound","autodestroy","bhidden","bhiddened","bhiddenedgroup","platform","base","attachtag","owner","instigator"};
        return keys;
    }
    // Sub-emitter properties that stop a preview loop (Disabled waits for a trigger,
    // AutoDestroy disables it after one burst) or need the map (collision, sounds).
    inline const std::set<std::string>& ObjectStrip()
    {
        static const std::set<std::string> keys{"disabled","autodestroy","usecollision","usemaxcollisions","spawningsound","collisionsound","sounds","collisionsoundindex","collisionsoundprobability"};
        return keys;
    }
    // prefix+index names the actor, prefix+index+"_"+k its k-th inline object;
    // mapRoots are folded first path segments owned by a map or a library
    // definition ("mylevel", "assembly", the open map's package). position and
    // rotation are the entry actor's own, relative to the entry's pivot, so a
    // multi-actor entry keeps its layout and matches its placement.
    inline Actor Prepare(const std::string& source,const std::string& prefix,int index,const std::string& package,const std::set<std::string>& mapRoots,const Rotation& rotation,const Vector& position={})
    {
        if(source.size()>16*1024*1024)throw std::runtime_error("The emitter text exceeds 16 MiB.");
        Actor out;out.name=prefix+std::to_string(index);
        std::map<std::string,std::string> names;std::istringstream input(source);std::string line,text;int depth=0;bool begun=false,ended=false;
        while(std::getline(input,line))
        {
            if(!line.empty() && line.back()=='\r')line.pop_back();
            auto t=Trim(line),l=Fold(t);
            if(t.empty())continue;
            if(ended)throw std::runtime_error("An emitter entry holds text after its actor.");
            size_t at=0,length=0;
            if(!begun)
            {
                if(l.rfind("begin actor ",0)!=0 || !Token(line,"Class",at,length))throw std::runtime_error("An emitter entry must start with Begin Actor.");
                out.type=Unquote(line.substr(at,length));
                if(!Token(line,"Name",at,length))throw std::runtime_error("The emitter actor has no name.");
                line.replace(at,length,out.name);begun=true;depth=1;text+=line+"\n";continue;
            }
            if(l.rfind("begin actor ",0)==0)throw std::runtime_error("Nested actor declaration.");
            if(l.rfind("begin ",0)==0)
            {
                if(l.rfind("begin object ",0)==0 && Token(line,"Name",at,length))
                {
                    auto name=out.name+"_"+std::to_string(out.objects++);
                    names[Fold(Unquote(line.substr(at,length)))]=name;line.replace(at,length,name);
                }
                ++depth;text+=line+"\n";continue;
            }
            if(l.rfind("end ",0)==0)
            {
                if(--depth<0)throw std::runtime_error("Unbalanced emitter text.");
                text+=line+"\n";if(!depth){if(l!="end actor")throw std::runtime_error("Unbalanced emitter text.");ended=true;}
                continue;
            }
            auto key=Key(t);
            if(depth==1 && ActorStrip().count(key))continue;
            if(depth>=2 && ObjectStrip().count(key))continue;
            text+=line+"\n";
        }
        if(!begun || !ended)throw std::runtime_error("Incomplete emitter actor text.");
        for(double v:position)if(!std::isfinite(v) || std::abs(v)>1e6)throw std::runtime_error("An entry actor has an invalid position.");
        char location[128];snprintf(location,sizeof(location),"(X=%.6f,Y=%.6f,Z=%.6f)",position[0],position[1],position[2]);
        text=SetProperty(SetProperty(text,"Location",location),"Rotation",RotationText(rotation));
        auto refs=References(text);
        for(auto i=refs.rbegin();i!=refs.rend();++i)
        {
            auto path=Fold(i->path);auto dot=path.find('.');
            auto last=path.substr(path.find_last_of('.')+1),first=path.substr(0,dot);
            std::string replacement;
            if(auto found=names.find(last);found!=names.end())replacement=i->type+"'"+package+"."+found->second+"'";
            else if(dot==std::string::npos || mapRoots.count(first)){replacement="None";out.dropped.push_back(i->path);}
            else {out.assets.push_back(i->path);continue;}
            text.replace(i->begin,i->end-i->begin,replacement);
        }
        std::reverse(out.assets.begin(),out.assets.end());std::reverse(out.dropped.begin(),out.dropped.end());
        out.text=text;return out;
    }

    // T3D struct text "(X=(Min=-20,Max=20),Z=5)" as nested objects; scalars stay strings.
    inline Json Value(const std::string& text,size_t& at,int depth=0)
    {
        if(depth>16)throw std::runtime_error("T3D value nests too deeply.");
        if(at<text.size() && text[at]=='(')
        {
            Json out=Json::object();++at;
            while(at<text.size() && text[at]!=')')
            {
                auto equals=text.find('=',at);if(equals==std::string::npos)throw std::runtime_error("Malformed T3D structure.");
                auto key=Fold(Trim(text.substr(at,equals-at)));at=equals+1;
                out[key]=Value(text,at,depth+1);
                if(at<text.size() && text[at]==',')++at;
            }
            if(at>=text.size())throw std::runtime_error("Malformed T3D structure.");
            ++at;return out;
        }
        std::string scalar;bool quoted=false;
        for(;at<text.size();++at)
        {
            if(text[at]=='"')quoted=!quoted;
            if(!quoted && (text[at]==',' || text[at]==')'))break;
            scalar+=text[at];
        }
        return Trim(scalar);
    }
    inline Json Value(const std::string& text){size_t at=0;return Value(Trim(text),at);}
    inline double Number(const Json& value,double fallback)
    {
        if(!value.is_string())return fallback;
        try{size_t used=0;auto s=value.get<std::string>();double v=std::stod(s,&used);return std::isfinite(v)?v:fallback;}
        catch(const std::exception&){return fallback;}
    }
    inline const Json& Member(const Json& value,const char* key){static const Json none;return value.is_object() && value.contains(key)?value.at(key):none;}

    // Inline objects of one actor as folded key -> raw value ("sizescale(0)" keeps its index).
    struct Object { std::string type;std::map<std::string,std::string> values; };
    inline std::vector<Object> Objects(const std::string& actorText)
    {
        std::vector<Object> out;std::istringstream input(actorText);std::string line;int depth=0,objectDepth=0;
        while(std::getline(input,line))
        {
            auto t=Trim(line),l=Fold(t);
            if(l.rfind("begin ",0)==0)
            {
                ++depth;size_t at=0,length=0;
                if(l.rfind("begin object ",0)==0 && !objectDepth){objectDepth=depth;out.push_back({});if(Token(t,"Class",at,length))out.back().type=Fold(Unquote(t.substr(at,length)));}
                continue;
            }
            if(l.rfind("end ",0)==0){if(depth==objectDepth)objectDepth=0;--depth;continue;}
            if(!objectDepth || depth!=objectDepth)continue;
            auto equals=t.find('=');if(equals==std::string::npos)continue;
            out.back().values[Fold(Trim(t.substr(0,equals)))]=t.substr(equals+1);
        }
        return out;
    }

    struct Box { Vector min{},max{};bool valid=false; };
    inline void Include(Box& box,const Vector& p)
    {
        for(int i=0;i<3;++i){box.min[i]=box.valid?std::min(box.min[i],p[i]):p[i];box.max[i]=box.valid?std::max(box.max[i],p[i]):p[i];}
        box.valid=true;
    }
    // Where one sub-emitter's particles can reach, from its start box, velocity,
    // acceleration, velocity loss and lifetime, grown by the largest particle.
    // Ranges follow the native Range.Rand (Min..Max per axis); DrawScale only
    // grows the culling box natively, so it is ignored.
    inline Box Reach(const Object& object,const Rotation& rotation)
    {
        auto get=[&](const char* key){auto found=object.values.find(key);return found==object.values.end()?Json():Value(found->second);};
        auto range=[&](const Json& value,const char* axis,double lo,double hi){auto r=Member(value,axis);return std::pair<double,double>{Number(Member(r,"min"),lo),Number(Member(r,"max"),hi)};};
        auto vector=[&](const Json& value){return Vector{Number(Member(value,"x"),0),Number(Member(value,"y"),0),Number(Member(value,"z"),0)};};
        auto flag=[&](const char* key,bool fallback){auto found=object.values.find(key);return found==object.values.end()?fallback:Fold(Trim(found->second))=="true";};
        auto life=get("lifetimerange");double lifetime=std::clamp(std::max(Number(Member(life,"min"),4),Number(Member(life,"max"),4)),0.05,10.0);
        auto offset=vector(get("startlocationoffset")),accel=vector(get("acceleration"));
        auto start=get("startlocationrange"),velocity=get("startvelocityrange"),loss=get("velocitylossrange");
        const char* axes[]={"x","y","z"};Box box;Vector lo{},hi{};
        double maxAbs=0;{auto m=vector(get("maxabsvelocity"));for(double v:m)maxAbs=std::max(maxAbs,std::abs(v));}
        for(int i=0;i<3;++i)
        {
            auto [s0,s1]=range(start,axes[i],0,0);auto [v0,v1]=range(velocity,axes[i],0,0);auto [k0,k1]=range(loss,axes[i],0,0);
            double k=std::max(0.0,(k0+k1)*0.5);
            auto travel=[&](double v)
            {
                if(maxAbs>0)v=std::clamp(v,-maxAbs,maxAbs);
                double d=k>1e-4?v*(1-std::exp(-k*lifetime))/k:v*lifetime;
                d+=0.5*accel[i]*lifetime*lifetime*(k>1e-4?std::min(1.0,1/(k*lifetime)):1.0);
                return std::clamp(d,-4096.0,4096.0);
            };
            lo[i]=offset[i]+std::min(s0,s1)+std::min({0.0,travel(v0),travel(v1)});
            hi[i]=offset[i]+std::max(s0,s1)+std::max({0.0,travel(v0),travel(v1)});
        }
        auto radial=get("startvelocityradialrange");double spread=std::min(4096.0,std::max(std::abs(Number(Member(radial,"min"),0)),std::abs(Number(Member(radial,"max"),0)))*lifetime);
        const bool mesh=object.type=="meshemitter";
        auto size=get("startsizerange");double particle=std::max(std::abs(range(size,"x",mesh?1:100,mesh?1:100).first),std::abs(range(size,"x",mesh?1:100,mesh?1:100).second));
        if(mesh)particle*=64; // A mesh particle's Size scales the mesh; assume a 64 unit mesh.
        if(flag("usesizescale",false) && !flag("useregularsizescale",true))
        {
            double scale=1;for(const auto& [key,raw]:object.values)if(key.rfind("sizescale(",0)==0)scale=std::max(scale,Number(Member(Value(raw),"relativesize"),1));
            particle*=std::min(scale,64.0);
        }
        particle=std::min(particle,2048.0);
        const bool actor=Fold(Trim(object.values.count("userotationfrom")?object.values.at("userotationfrom"):std::string()))=="ptrs_actor";
        for(int corner=0;corner<8;++corner)
        {
            Vector p{corner&1?hi[0]:lo[0],corner&2?hi[1]:lo[1],corner&4?hi[2]:lo[2]};
            if(actor)p=TransformPoint(p,Pose{{},rotation});
            Include(box,p);
        }
        for(int i=0;i<3;++i){box.min[i]-=particle+spread;box.max[i]+=particle+spread;}
        return box;
    }
    // An entry actor as the preview places it: T3D text, rotation and position.
    struct Placed { std::string text;Rotation rotation{};Vector position{}; };
    struct Frame { Vector target{};double radius=0;Box box; };
    inline Frame Estimate(const std::vector<Placed>& actors)
    {
        Box all;
        for(const auto& actor:actors)for(const auto& object:Objects(actor.text))
        {
            auto box=Reach(object,actor.rotation);if(!box.valid)continue;
            for(int i=0;i<3;++i){box.min[i]+=actor.position[i];box.max[i]+=actor.position[i];}
            Include(all,box.min);Include(all,box.max);
        }
        Frame frame;
        if(!all.valid){frame.radius=128;Include(frame.box,{-64,-64,-64});Include(frame.box,{64,64,64});return frame;}
        double squared=0;
        for(int i=0;i<3;++i){frame.target[i]=(all.min[i]+all.max[i])*0.5;squared+=(all.max[i]-all.min[i])*(all.max[i]-all.min[i]);}
        frame.radius=std::clamp(std::sqrt(squared)*0.5,24.0,4096.0);frame.box=all;return frame;
    }
    // Distance at which a sphere fills the narrower of the horizontal
    // FovAngle (degrees, Unreal's convention) and the matching vertical angle.
    inline double Distance(double radius,double fovDegrees,double aspect)
    {
        const double pi=3.14159265358979323846;
        double horizontal=std::clamp(fovDegrees,10.0,170.0)*pi/180,vertical=2*std::atan(std::tan(horizontal*0.5)/std::max(aspect,0.1));
        double half=std::min(horizontal,vertical)*0.5;
        return std::max(radius*1.15/std::sin(half),16.0);
    }
    // One live particle: world centre, half-size and how visible it is now (0..1, from its
    // faded colour or alpha).
    struct Sample { Vector at{};double size=0,weight=1; };
    inline double Quantile(std::vector<double>& values,double q)
    {
        const size_t k=std::min(values.size()-1,static_cast<size_t>(std::clamp(q,0.0,1.0)*static_cast<double>(values.size()-1)+0.5));
        std::nth_element(values.begin(),values.begin()+static_cast<std::ptrdiff_t>(k),values.end());return values[k];
    }
    // Unreal view axes for pitch/yaw (65536 units a turn, no roll).
    struct Axes { Vector forward{},right{},up{}; };
    inline Axes ViewAxes(int pitch,int yaw)
    {
        const double unit=6.2831853071795864769/65536.0,p=pitch*unit,y=yaw*unit;
        return {{std::cos(p)*std::cos(y),std::cos(p)*std::sin(y),std::sin(p)},{-std::sin(y),std::cos(y),0},{-std::sin(p)*std::cos(y),-std::sin(p)*std::sin(y),std::cos(p)}};
    }
    // Where to look from pitch/yaw so the particles fill the view. Only visible particles
    // count (weight >= 0.1; all of them when fewer than three are). The target is the middle
    // of the 4..96% quantiles of their positions on the view axes, so strays neither widen
    // nor shift the frame. The distance puts 96% of them, each grown by its own half-size,
    // inside fill of the horizontal FovAngle and of the vertical angle for aspect
    // (width/height), and in front of the camera; it never makes the typical particle
    // narrower than minPixels in a view pixels wide, so sparks, snow and rain spread through
    // a large volume show up close instead of as a few dots. width/height are the half
    // extents on the view plane.
    struct View { Vector target{};double distance=0,width=0,height=0;bool valid=false; };
    inline View Fit(const std::vector<Sample>& samples,int pitch,int yaw,double fovDegrees,double aspect,double fill,double pixels,double minPixels)
    {
        constexpr double low=0.04,high=0.96,visible=0.1,pi=3.14159265358979323846;
        std::vector<const Sample*> seen;for(const auto& s:samples)if(s.weight>=visible)seen.push_back(&s);
        if(seen.size()<3){seen.clear();for(const auto& s:samples)seen.push_back(&s);}
        View view;if(seen.size()<3)return view;
        const auto axes=ViewAxes(pitch,yaw);
        auto dot=[](const Vector& a,const Vector& b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];};
        const size_t n=seen.size();std::vector<double> r(n),u(n),f(n),size(n),need(n);
        for(size_t i=0;i<n;++i){r[i]=dot(seen[i]->at,axes.right);u[i]=dot(seen[i]->at,axes.up);f[i]=dot(seen[i]->at,axes.forward);size[i]=std::min(std::abs(seen[i]->size),2048.0);}
        auto middle=[&](const std::vector<double>& values,double& half){auto copy=values;const double a=Quantile(copy,low),b=Quantile(copy,high);half=(b-a)*0.5;return (a+b)*0.5;};
        double depthHalf=0;const double rc=middle(r,view.width),uc=middle(u,view.height),fc=middle(f,depthHalf);
        for(int i=0;i<3;++i)view.target[i]=axes.right[i]*rc+axes.up[i]*uc+axes.forward[i]*fc;
        const double half=std::tan(std::clamp(fovDegrees,10.0,170.0)*pi/360),tx=half*std::clamp(fill,0.05,1.0),ty=tx/std::max(aspect,0.1);
        for(size_t i=0;i<n;++i){const double depth=f[i]-fc;need[i]=std::max({(std::abs(r[i]-rc)+size[i])/tx-depth,(std::abs(u[i]-uc)+size[i])/ty-depth,size[i]+8-depth});}
        double distance=Quantile(need,high);
        if(pixels>0 && minPixels>0)distance=std::min(distance,Quantile(size,0.5)*pixels/(half*minPixels));
        view.distance=std::clamp(distance,16.0,20000.0);view.valid=true;
        return view;
    }
    // Camera distance at which box, seen from pitch/yaw (Unreal units, 65536 a turn) while
    // looking at target, spans fill of the view: every corner inside fill of the horizontal
    // FovAngle and of the matching vertical angle for aspect (width/height), and in front
    // of the camera. Sized on the view axes, so a flat or tall effect is framed by its
    // silhouette rather than by a bounding sphere.
    inline double FitDistance(const Box& box,const Vector& target,int pitch,int yaw,double fovDegrees,double aspect,double fill)
    {
        constexpr double pi=3.14159265358979323846;
        const auto [forward,right,up]=ViewAxes(pitch,yaw);
        const double tx=std::tan(std::clamp(fovDegrees,10.0,170.0)*pi/360)*std::clamp(fill,0.05,1.0),ty=tx/std::max(aspect,0.1);
        double need=16;
        for(int corner=0;corner<8 && box.valid;++corner)
        {
            const Vector c{(corner&1?box.max[0]:box.min[0])-target[0],(corner&2?box.max[1]:box.min[1])-target[1],(corner&4?box.max[2]:box.min[2])-target[2]};
            auto dot=[&](const Vector& axis){return axis[0]*c[0]+axis[1]*c[1]+axis[2]*c[2];};
            const double depth=dot(forward);
            need=std::max({need,std::abs(dot(right))/tx-depth,std::abs(dot(up))/ty-depth,8-depth});
        }
        return std::min(need,20000.0);
    }
    // Camera location looking at target along Unreal pitch/yaw (65536 units/turn).
    inline Vector Eye(const Vector& target,double distance,int pitch,int yaw)
    {
        const double unit=6.2831853071795864769/65536.0;
        double p=pitch*unit,y=yaw*unit;
        return {target[0]-std::cos(p)*std::cos(y)*distance,target[1]-std::cos(p)*std::sin(y)*distance,target[2]-std::sin(p)*distance};
    }
    // Capture statistics of a 32-bit BGRA frame: pixels that differ from the empty
    // preview background by more than 10 in any channel ("nonBlackPixels"), mean luma
    // and an FNV-1a checksum of the RGB bytes.
    inline Json ImageStats(const std::vector<uint32_t>& pixels,int width,int height,uint32_t background)
    {
        if(width<=0 || height<=0 || pixels.size()!=static_cast<size_t>(width)*static_cast<size_t>(height))throw std::runtime_error("Invalid preview frame.");
        size_t lit=0;double luma=0;uint32_t hash=2166136261u;
        const int bb=static_cast<int>(background&0xff),bg=static_cast<int>((background>>8)&0xff),br=static_cast<int>((background>>16)&0xff);
        for(uint32_t p:pixels)
        {
            const int b=static_cast<int>(p&0xff),g=static_cast<int>((p>>8)&0xff),r=static_cast<int>((p>>16)&0xff);
            if(std::max({std::abs(r-br),std::abs(g-bg),std::abs(b-bb)})>10)++lit;
            luma+=0.299*r+0.587*g+0.114*b;
            for(int c:{r,g,b}){hash^=static_cast<uint32_t>(c);hash*=16777619u;}
        }
        return {{"width",width},{"height",height},{"nonBlackPixels",lit},{"meanLuma",luma/static_cast<double>(pixels.size())},{"checksum",hash}};
    }
}
