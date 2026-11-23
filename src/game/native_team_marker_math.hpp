#pragma once
#include "native_hand_hud_kl.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace NativeTeamMarkerMath {
using Icon=NativeTeamMarkerKL::Icon;
// POD storage avoids zeroing the maximum marker buffer on ordinary HUD draws.
struct Vertex {float x,y,z,rhw;std::uint32_t color;float u,v;};
static_assert(sizeof(Vertex)==sizeof(NativeHandHudKL::Vertex),"Marker FVF must match HUD vertices");
struct Point {float x=0,y=0;};
struct Outline {std::array<Point,17> points{};unsigned count=0;};
inline Outline Shape(Icon icon) {
    Outline value;
    if(icon==Icon::Circle) {
        value.count=17;
        for(unsigned i=0;i<17;++i){const float angle=float(i)*6.28318530718f/16.f;value.points[i]={std::cos(angle)*.8f,std::sin(angle)*.8f};}
    } else if(icon==Icon::Shield) {
        value.count=6;value.points[0]={-.75f,.8f};value.points[1]={.75f,.8f};value.points[2]={.65f,-.15f};
        value.points[3]={0,-.9f};value.points[4]={-.65f,-.15f};value.points[5]=value.points[0];
    } else if(icon==Icon::Lightning) {
        value.count=7;value.points[0]={.3f,.95f};value.points[1]={-.75f,-.05f};value.points[2]={-.05f,-.05f};
        value.points[3]={-.3f,-.95f};value.points[4]={.75f,.25f};value.points[5]={.05f,.25f};value.points[6]=value.points[0];
    }
    return value;
}
constexpr unsigned MaximumVertices=NativeTeamMarkerKL::MaximumMarkers*16*12;
struct Geometry {std::array<Vertex,MaximumVertices> vertices;unsigned count=0,markers=0;};
inline bool Stroke(Geometry& output,Vertex a,Vertex b,float radius,std::uint32_t color) {
    const float dx=b.x-a.x,dy=b.y-a.y,length=std::hypot(dx,dy);
    if(!std::isfinite(length)||length<.001f||output.count+6>output.vertices.size())return false;
    const float x=-dy/length*radius,y=dx/length*radius;
    Vertex p=a,q=a,r=b,s=b;p.x+=x;p.y+=y;q.x-=x;q.y-=y;r.x-=x;r.y-=y;s.x+=x;s.y+=y;
    p.color=q.color=r.color=s.color=color;
    for(const auto& vertex:{p,q,r,p,r,s})output.vertices[output.count++]=vertex;
    return true;
}
inline bool Build(const NativeHandHudKL::Frame& frame,unsigned width,unsigned height,std::uint32_t now,Geometry& output) {
    if(frame.nativeCommandMarkers){output.count=output.markers=0;return true;}
    if(!NativeTeamMarkerKL::Fresh(frame.commandMarkers,frame.player,now)||!width||!height||
       !NativeCameraMath::ValidatePose(frame.actualEye,.05f)||!std::isfinite(frame.unitsPerMetre)||frame.unitsPerMetre<1||frame.unitsPerMetre>1000)return false;
    Geometry next;
    const float radius=std::clamp(float(height)*.014f,12.f,34.f),line=std::max(1.2f,radius*.075f);
    for(unsigned i=0;i<frame.commandMarkers.count;++i) {
        const auto& marker=frame.commandMarkers.markers[i];
        if(NativeCameraMath::Dot(NativeCameraMath::Subtract(marker.position,frame.actualEye.position),frame.actualEye.basis.forward)<.05f*frame.unitsPerMetre)continue;
        NativeHandHudKL::Vertex projected;
        if(!NativeHandHudKL::Project(frame.actualEye,frame.projection,width,height,marker.position,0,0,projected)||
           projected.x<-radius||projected.x>float(width)+radius||projected.y<-radius||projected.y>float(height)+radius)continue;
        const Vertex centre{projected.x,projected.y,projected.z,projected.rhw,projected.color,0,0};
        const auto shape=Shape(marker.icon);if(shape.count<2)continue;
        const auto color=marker.icon==Icon::Lightning?0xFFFFD150u:marker.icon==Icon::Shield?0xFF70E5FFu:0xFFD8EFFFu;
        for(unsigned pass=0;pass<2;++pass)for(unsigned n=1;n<shape.count;++n) {
            Vertex a=centre,b=centre;
            a.x+=shape.points[n-1].x*radius;a.y-=shape.points[n-1].y*radius;
            b.x+=shape.points[n].x*radius;b.y-=shape.points[n].y*radius;
            if(!Stroke(next,a,b,pass?line:line+2.f,pass?color:0xD0000000u))return false;
        }
        ++next.markers;
    }
    output.count=next.count;output.markers=next.markers;
    std::memcpy(output.vertices.data(),next.vertices.data(),next.count*sizeof(Vertex));return true;
}
} // namespace NativeTeamMarkerMath
