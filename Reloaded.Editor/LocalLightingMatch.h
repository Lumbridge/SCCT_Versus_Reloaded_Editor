#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// Local residual matching in stored lightmap colour space. This is an editing
// approximation, not a reconstruction of the original lighting solver.
namespace LocalLightingMatch {
struct Point {
    double x{}, y{}, z{};
    Point operator+(Point b) const { return {x+b.x,y+b.y,z+b.z}; }
    Point operator-(Point b) const { return {x-b.x,y-b.y,z-b.z}; }
    Point operator*(double s) const { return {x*s,y*s,z*s}; }
};
inline double Dot(Point a, Point b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
struct BspNode { Point normal; double distance; int front,back; bool csg; };
inline bool ClearSegment(const std::vector<BspNode>& nodes, bool rootOutside, Point a, Point b) {
    if(nodes.empty()) return rootOutside;
    struct Part { int index; Point a,b; bool outside; };
    std::vector<Part> pending{{0,a,b,rootOutside}};
    size_t visited=0;
    while(!pending.empty()) {
        const auto part=pending.back();pending.pop_back();
        if(++visited>nodes.size()*4+64) return false;
        if(part.index==-1) {if(!part.outside) return false;continue;}
        if(part.index<0 || size_t(part.index)>=nodes.size()) return false;
        const auto& node=nodes[part.index];
        const double da=Dot(node.normal,part.a)-node.distance,db=Dot(node.normal,part.b)-node.distance;
        const bool fa=da>=0,fb=db>=0;
        auto next=[&](bool front,Point start,Point end) {
            pending.push_back({front?node.front:node.back,start,end,front?(part.outside||node.csg):(part.outside&&!node.csg)});
        };
        if(fa==fb) next(fa,part.a,part.b);
        else {
            const auto middle=part.a+(part.b-part.a)*(da/(da-db));
            next(fa,part.a,middle);next(fb,middle,part.b);
        }
    }
    return true;
}
// Nearest point on a triangle, expressed as weights for vertices b and c.
inline double Nearest(Point p, Point a, Point b, Point c, double& wb, double& wc) {
    const Point e=b-a, f=c-a, q=p-a;
    const double ee=Dot(e,e), ef=Dot(e,f), ff=Dot(f,f), det=ee*ff-ef*ef;
    wb=wc=0;
    if(det>1e-12) {
        wb=(Dot(q,e)*ff-Dot(q,f)*ef)/det;
        wc=(Dot(q,f)*ee-Dot(q,e)*ef)/det;
        if(wb>=0 && wc>=0 && wb+wc<=1) return Dot(q-e*wb-f*wc,q-e*wb-f*wc);
    }
    double best=1e100;
    const Point starts[]={a,a,b}, ends[]={b,c,c};
    for(int i=0;i<3;++i) {
        const Point edge=ends[i]-starts[i];
        const double length=Dot(edge,edge);
        const double t=length>1e-12 ? std::clamp(Dot(p-starts[i],edge)/length,0.0,1.0) : 0;
        const Point delta=p-(starts[i]+edge*t);
        const double d=Dot(delta,delta);
        if(d<best) { best=d; wb=i==0?t:(i==2?1-t:0); wc=i==0?0:t; }
    }
    return best;
}
inline uint32_t Correct(uint32_t target, uint32_t original, uint32_t native) {
    uint32_t result=target & 0xff000000u;
    for(int channel=0;channel<3;++channel) {
        const int shift=channel*8;
        const int value=int((target>>shift)&255)+int((original>>shift)&255)-int((native>>shift)&255);
        result|=uint32_t(std::clamp(value,0,255))<<shift;
    }
    return result;
}
}
