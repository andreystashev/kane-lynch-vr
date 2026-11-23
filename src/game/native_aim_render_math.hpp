#pragma once
#include "native_hand_hud_kl.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace NativeAimRenderKL {
using namespace NativeCameraMath;
using Vertex=NativeHandHudKL::Vertex;
enum class Prop {None,Round,ShotgunShell};
struct Frame {CameraPose eye{},propPose{};Vec3 propHalf{};Prop prop=Prop::None;float projection[16]{};Vec3 origin{},contact{};float units=100.f;bool contactValid=true;
    bool dotOnly=false;std::uint32_t dotColor=0xe0ff5030u;};
struct Geometry {std::array<Vertex,168> vertices{};unsigned count=0;};
inline bool BuildDot(const Frame& frame,unsigned width,unsigned height,Geometry& output){
    if(!frame.contactValid||!ValidatePose(frame.eye,.05f)||!Finite(frame.contact)||
       !std::isfinite(frame.units)||frame.units<1||frame.units>1000||
       Dot(Subtract(frame.contact,frame.eye.position),frame.eye.basis.forward)<=.02f*frame.units)return false;
    Vertex centre;
    if(!NativeHandHudKL::Project(frame.eye,frame.projection,width,height,frame.contact,0,0,centre))return false;
    Geometry next;const float radius=std::clamp(float(height)/720.f,2.5f,4.f);
    constexpr float pi=3.14159265358979323846f;
    // A small outlined aiming dot remains legible on sky and dark surfaces.
    // Both eyes project the same native mounted-gun target independently.
    for(unsigned layer=0;layer<2;++layer){
        const float r=layer?radius*.58f:radius;
        const auto color=layer?frame.dotColor:0xc0000000u;
        Vertex c=centre;c.color=color;
        for(unsigned i=0;i<8;++i){Vertex p=c,q=c;const float a=2*pi*float(i)/8,b=2*pi*float(i+1)/8;
            p.x+=std::cos(a)*r;p.y+=std::sin(a)*r;q.x+=std::cos(b)*r;q.y+=std::sin(b)*r;
            next.vertices[next.count++]=c;next.vertices[next.count++]=p;next.vertices[next.count++]=q;}
    }
    output=next;return true;
}
struct PropGeometry {std::array<Vertex,612> vertices;unsigned count=0;};

inline bool BuildProp(const Frame& frame,unsigned width,unsigned height,PropGeometry& output){
    output.count=0;
    const auto& pose=frame.propPose;
    const auto& size=frame.propHalf;
    if(frame.prop==Prop::None||!width||!height||width>16384||height>16384||
       !ValidatePose(frame.eye,.05f)||!ValidatePose(pose,.05f)||!Finite(size)||
       size.x<=0||size.y<=0||size.z<=0)return false;
    for(float value:frame.projection)if(!std::isfinite(value))return false;
    const auto camera=[&](Vec3 world){
        return Vec3{Dot(world,frame.eye.basis.right),Dot(world,frame.eye.basis.up),Dot(world,frame.eye.basis.forward)};
    };
    const Vec3 eyeDelta=Subtract(pose.position,frame.eye.position);
    const Vec3 origin=camera(eyeDelta);
    const Vec3 right=camera(Scale(pose.basis.right,size.x));
    const Vec3 up=camera(Scale(pose.basis.up,size.y));
    const Vec3 forward=camera(Scale(pose.basis.forward,size.z));
    const Vec3 eyeLocal{Dot(eyeDelta,pose.basis.right),Dot(eyeDelta,pose.basis.up),Dot(eyeDelta,pose.basis.forward)};
    const auto project=[&](float x,float y,float z,Vertex& vertex){
        const auto point=Add(origin,Add(Scale(right,x),Add(Scale(up,y),Scale(forward,z))));
        float clip[4];
        for(unsigned row=0;row<4;++row)
            clip[row]=point.x*frame.projection[row]+point.y*frame.projection[4+row]+
                point.z*frame.projection[8+row]+frame.projection[12+row];
        if(!std::isfinite(clip[3])||clip[3]<=1e-4f)return false;
        const float invW=1.f/clip[3];
        const float depth=clip[2]*invW;
        if(!std::isfinite(depth)||depth<0.f||depth>1.f)return false;
        vertex={(1.f+clip[0]*invW)*float(width)*.5f-.5f,(1.f-clip[1]*invW)*float(height)*.5f-.5f,
            depth,invW,0xffffffffu,0.f,0.f};
        return std::isfinite(vertex.x)&&std::isfinite(vertex.y);
    };

    // The low, broad rim and rolled mouth define the shell. The rifle round
    // uses a separate case shoulder, narrow neck and tapered copper tip.
    // D3D writes these linear diffuse colours before the game's scene grading.
    struct Station {float z,radius;std::uint32_t colour;};
    constexpr Station shell[]={{-1.f,.43f,0xff110f0au},{-1.f,.91f,0xff20180cu},{-.94f,1.f,0xff20180du},
        {-.84f,.95f,0xff171109u},{-.63f,.95f,0xff191109u},{-.59f,.92f,0xff1e0908u},
        {.89f,.92f,0xff1e0908u},{.98f,.79f,0xff100807u},{.90f,.42f,0xff0d0807u}};
    constexpr Station round[]={{-1.f,.43f,0xff110f0au},{-1.f,.92f,0xff19120au},{-.93f,1.f,0xff20170bu},
        {-.81f,.89f,0xff181108u},{.12f,.90f,0xff1e160au},{.30f,.60f,0xff191209u},
        {.43f,.56f,0xff170d07u},{.74f,.40f,0xff1d0e09u},{1.f,.09f,0xff150b08u}};
    const bool shotgun=frame.prop==Prop::ShotgunShell;
    const auto& stations=shotgun?shell:round;
    constexpr float i=.5f,j=.8660254038f;
    constexpr Vec3 ring[]={{1,0,0},{j,i,0},{i,j,0},{0,1,0},{-i,j,0},{-j,i,0},{-1,0,0},
        {-j,-i,0},{-i,-j,0},{0,-1,0},{i,-j,0},{j,-i,0}};
    constexpr unsigned sides=std::size(ring),rings=std::size(shell);
    Vertex vertices[rings][sides];
    const auto colour=[](std::uint32_t rgb,float shade){
        const auto component=[&](unsigned shift){return std::uint32_t(float((rgb>>shift)&255)*shade)<<shift;};
        return 0xff000000u|component(16)|component(8)|component(0);
    };
    for(unsigned s=0;s<rings;++s)for(unsigned k=0;k<sides;++k){
        const auto& station=stations[s];
        const auto& radial=ring[k];
        auto& vertex=vertices[s][k];
        if(!project(radial.x*station.radius,radial.y*station.radius,station.z,vertex))return false;
        vertex.color=colour(station.colour,.91f+.07f*radial.y+.045f*radial.x);
    }
    const auto triangle=[&](Vertex a,Vertex b,Vertex c){
        if(output.count+3>output.vertices.size())return;
        output.vertices[output.count++]=a;output.vertices[output.count++]=b;output.vertices[output.count++]=c;
    };
    for(unsigned k=0;k<sides;++k){
        const auto n=(k+1)%sides;
        const Vec3 radial=Add(ring[k],ring[n]);
        const bool wallFacing=-(eyeLocal.x*radial.x/size.x+eyeLocal.y*radial.y/size.y)>0.f;
        const bool frontFacing=-eyeLocal.z>0.f;
        for(unsigned s=0;s+1<rings;++s){
            // Flat rim, crimp and tapered shoulder face the corresponding
            // end as well as the cylindrical silhouette seen from the side.
            const float taper=stations[s].radius-stations[s+1].radius;
            if(!wallFacing&&((frontFacing?taper:-taper)<=0.f))continue;
            triangle(vertices[s][k],vertices[s][n],vertices[s+1][n]);
            triangle(vertices[s][k],vertices[s+1][n],vertices[s+1][k]);
        }
    }
    const unsigned cap=eyeLocal.z<0.f?rings-1:0;
    Vertex centre;
    if(!project(0,0,stations[cap].z,centre))return false;
    centre.color=stations[cap].colour;
    for(unsigned k=0;k<sides;++k)triangle(vertices[cap][k],vertices[cap][(k+1)%sides],centre);
    return output.count!=0;
}
inline bool Build(const Frame& frame,unsigned width,unsigned height,Geometry& output){
    if(frame.prop!=Prop::None)return false;
    if(frame.dotOnly)return BuildDot(frame,width,height,output);
    if(!ValidatePose(frame.eye,.05f)||!Finite(frame.origin)||!Finite(frame.contact)||
       !std::isfinite(frame.units)||frame.units<1||frame.units>1000)return false;
    Vec3 start=frame.origin;const auto delta=Subtract(frame.contact,start);
    const float length=std::sqrt(Dot(delta,delta));
    if(!std::isfinite(length)||length<.01f*frame.units||length>300.f*frame.units)return false;
    const auto depth=[&](Vec3 p){return Dot(Subtract(p,frame.eye.position),frame.eye.basis.forward);};
    for(float value:frame.projection)if(!std::isfinite(value))return false;
    const float nearDepth=.02f*frame.units,from=depth(start),to=depth(frame.contact);
    if(to<=nearDepth)return false;
    float begin=from<nearDepth?(nearDepth-from)/(to-from):0.f;
    const auto clip=[&](Vec3 world,unsigned row){const auto d=Subtract(world,frame.eye.position);
        return Dot(d,frame.eye.basis.right)*frame.projection[row]+Dot(d,frame.eye.basis.up)*frame.projection[4+row]+
            Dot(d,frame.eye.basis.forward)*frame.projection[8+row]+frame.projection[12+row];};
    // Clip the near end of a beam entering the eye frustum. A muzzle behind
    // the eye can otherwise project to infinity before the visible segment.
    const float startW=clip(start,3),endW=clip(frame.contact,3);
    for(unsigned row=0;row<2;++row)for(float sign:{-1.f,1.f}){
        const float a=7.9f*startW+sign*clip(start,row),b=7.9f*endW+sign*clip(frame.contact,row);
        if(b<0)return false;if(a<0)begin=std::max(begin,-a/(b-a));
    }
    if(begin>0)start=Add(start,Scale(delta,begin));
    Vertex a,b;
    if(!NativeHandHudKL::Project(frame.eye,frame.projection,width,height,start,0,0,a)||
       !NativeHandHudKL::Project(frame.eye,frame.projection,width,height,frame.contact,0,0,b))return false;
    Geometry result;
    const auto triangle=[&](Vertex x,Vertex y,Vertex z){result.vertices[result.count++]=x;result.vertices[result.count++]=y;result.vertices[result.count++]=z;};
    const float dx=b.x-a.x,dy=b.y-a.y,screenLength=std::sqrt(dx*dx+dy*dy);
    if(screenLength>.5f){
        const float nx=-dy/screenLength*.55f,ny=dx/screenLength*.55f;
        Vertex p=a,q=a,r=b,s=b;p.x+=nx;p.y+=ny;q.x-=nx;q.y-=ny;r.x+=nx;r.y+=ny;s.x-=nx;s.y-=ny;
        p.color=q.color=r.color=s.color=0x38ff3020u;
        triangle(p,q,r);triangle(q,s,r);
    }
    // Contact is supplied only by the native collision query. Never draw an
    // arbitrary maximum-range dot when a query misses or its proof expires.
    if(!frame.contactValid){output=result;return result.count!=0;}
    const float radius=std::clamp(.0035f*frame.units*frame.projection[5]*float(height)*.5f/to,1.3f,4.f);
    constexpr unsigned sides=16;constexpr float pi=3.14159265358979323846f;
    b.color=0xe0ff5030u;
    for(unsigned i=0;i<sides;++i){Vertex p=b,q=b;const float t=2*pi*float(i)/sides,u=2*pi*float(i+1)/sides;
        p.x+=std::cos(t)*radius;p.y+=std::sin(t)*radius;q.x+=std::cos(u)*radius;q.y+=std::sin(u)*radius;
        p.color=q.color=0x60ff3020u;triangle(b,p,q);
    }
    output=result;return true;
}
}
