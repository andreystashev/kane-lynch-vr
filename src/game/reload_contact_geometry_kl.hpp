#pragma once
#include "reload_math_kl.hpp"
#include <algorithm>
#include <cmath>

namespace ReloadContactGeometryKL {
using namespace NativeCameraMath;
namespace M=ReloadMathKL;
// Bone definitions contain a local bounding-box centre and HALF extents.
// Keep the authored bone frame: inferred barrel axes lose magazine rake.
struct Bounds {CameraPose world{};Vec3 half{};};
// The native rigid WORLD acts on vertices in model space. Bone metadata is
// in the cumulative bind frame: WORLD_skin * B_bind = WORLD_bone. Applying
// metadata directly to WORLD_skin puts contacts near the weapon root.
inline bool BoneWorld(const CameraPose& skinWorld,const CameraPose& cumulativeBind,CameraPose& output){
    if(!ValidatePose(skinWorld,.05f)||!ValidatePose(cumulativeBind,.05f))return false;
    const auto next=M::ToWorld(skinWorld,cumulativeBind);if(!ValidatePose(next,.05f))return false;output=next;return true;
}
inline bool BuildBounds(const CameraPose& rendered,Vec3 centre,Vec3 half,Bounds& output){
    if(!ValidatePose(rendered,.05f)||!Finite(centre)||!Finite(half)||half.x<0||half.y<0||half.z<0||
       half.x>1000||half.y>1000||half.z>1000)return false;
    Bounds next;next.world=rendered;next.world.position=Add(rendered.position,M::Transform(rendered.basis,centre));next.half=half;
    if(!ValidatePose(next.world,.05f))return false;output=next;return true;
}
inline float Extent(const Bounds& bounds,Vec3 axis){
    return std::fabs(Dot(bounds.world.basis.right,axis))*bounds.half.x+
        std::fabs(Dot(bounds.world.basis.up,axis))*bounds.half.y+
        std::fabs(Dot(bounds.world.basis.forward,axis))*bounds.half.z;
}
inline bool AlignBounds(const Bounds& bounds,const CameraPose& gun,Bounds& output){
    if(!ValidatePose(bounds.world,.05f)||!ValidatePose(gun,.05f)||!Finite(bounds.half)||bounds.half.x<0||bounds.half.y<0||bounds.half.z<0)return false;
    // Bone X/Y/Z are authored independently for every weapon. Only the
    // contact box is expressed along its current barrel/up; native vertices,
    // skin WORLD, material replay and animation axes remain untouched.
    Bounds next;next.world.position=bounds.world.position;next.world.basis=gun.basis;
    next.half={Extent(bounds,gun.basis.right),Extent(bounds,gun.basis.up),Extent(bounds,gun.basis.forward)};
    if(!Finite(next.half))return false;output=next;return true;
}
// P90 frame and horizontal magazine are distinct in the installed KL1 rig.
inline bool MP5Frame(Vec3 half){
    return std::fabs(half.x-3.11173f)<.01f&&std::fabs(half.z-33.25946f)<.01f;
}
inline bool P90Frame(Vec3 half){
    return std::fabs(half.x-3.490474f)<.02f&&std::fabs(half.y-10.322670f)<.02f&&
        std::fabs(half.z-25.078236f)<.02f;
}
inline bool P90Magazine(Vec3 half){
    return std::fabs(half.x-2.511590f)<.02f&&std::fabs(half.y-1.664529f)<.02f&&
        std::fabs(half.z-13.148788f)<.02f;
}
// Category 4 is shared by several rifles. The AK uses a real clip but
// its authored slide is empty. Require both owned parts before using its loader.
inline bool AK74Frame(Vec3 half){
    return std::fabs(half.x-3.133173f)<.01f&&std::fabs(half.y-12.738232f)<.01f&&
        std::fabs(half.z-47.082691f)<.01f;
}
inline bool AK74Magazine(Vec3 half){
    return std::fabs(half.x-.950064f)<.01f&&std::fabs(half.y-8.944397f)<.01f&&
        std::fabs(half.z-5.835742f)<.01f;
}
struct MagazineContact {CameraPose seatedTip{},mouth{},grip{};Vec3 gripHalf{};float feedLength=0,seatTravel=0;bool frameMouth=false,meshMouth=false;};
inline CameraPose GuidedTip(const CameraPose& mouth,const CameraPose& seated,float units,unsigned category){
    auto guide=mouth;
    if(!ValidatePose(mouth,.05f)||!ValidatePose(seated,.05f)||!std::isfinite(units)||units<1||units>1000)return guide;
    const float travel=std::max(0.f,Dot(Subtract(seated.position,mouth.position),mouth.basis.forward));
    // A long magazine must visibly enter the well while the hand is held.
    // Leave only the last centimetre for release/latch, rather than leaving
    // almost its entire height suspended below the opening until release.
    // Keep the accepted pistol staging distance exactly as before.
    const float advance=category==0?std::min(travel*.22f,.012f*units):
        std::max(0.f,travel-std::min(travel*.2f,.01f*units));
    guide.position=Add(guide.position,Scale(guide.basis.forward,advance));return guide;
}
inline bool Magazine(const CameraPose& rendered,Vec3 centre,Vec3 half,float units,MagazineContact& output,const Bounds* frame=nullptr,unsigned category=0,const CameraPose* measuredMouth=nullptr,unsigned measuredFeedAxis=3){
    if(!std::isfinite(units)||units<1||units>1000)return false;
    Bounds bounds;if(!BuildBounds(rendered,centre,half,bounds))return false;
    const Vec3 axes[]{rendered.basis.right,rendered.basis.up,rendered.basis.forward};
    const float extent[]{half.x,half.y,half.z};unsigned feed=1;
    // Native rifles/SMGs do not share the pistol's authored bone axes. Some
    // installed magazines are longest along local X (6.74/4.27/.88 cm), while
    // others retain local Y. Preserve the accepted pistol fit exactly; for
    // other native clips use their own long box axis towards the frame.
    if(category!=0){feed=0;for(unsigned i=1;i<3;++i)if(extent[i]>extent[feed])feed=i;}
    // The P90's long magazine is seated by bringing its underside down
    // toward the receiver. Its long axis is virtually perpendicular to feed.
    const bool topLoaded=category==2&&frame&&P90Frame(frame->half)&&P90Magazine(half);
    if(topLoaded)feed=1;
    const bool haveMeasured=category!=0&&measuredMouth&&ValidatePose(*measuredMouth,.05f);
    // A wide ammunition box can be wider than it is tall. Its measured port
    // proves the feed axis; the broad frame's stock cannot choose its sign.
    if(haveMeasured&&measuredFeedAxis<3)feed=measuredFeedAxis;
    const float length=extent[feed];if(length<.005f*units||length>.3f*units)return false;
    auto axis=axes[feed];
    const bool haveFrame=frame&&ValidatePose(frame->world,.05f)&&Finite(frame->half)&&frame->half.x>=0&&frame->half.y>=0&&frame->half.z>=0;
    if(haveMeasured&&measuredFeedAxis<3){if(Dot(Subtract(measuredMouth->position,bounds.world.position),axis)<0)axis=Scale(axis,-1);}
    else if(category!=0&&haveFrame&&Dot(Subtract(frame->world.position,bounds.world.position),axis)<0)axis=Scale(axis,-1);
    unsigned across=feed==0?1:0;for(unsigned i=0;i<3;++i)if(i!=feed&&extent[i]>extent[across])across=i;
    // Broad face lies along the fingers; thickness lies along the palm
    // normal. The old fixed Z cross-axis rolled the long gun clip sideways.
    MagazineContact next;const auto up=axes[across];
    next.grip.basis={axis,up,Cross(up,axis)};next.grip.position=bounds.world.position;
    next.gripHalf={Extent(bounds,next.grip.basis.right),Extent(bounds,next.grip.basis.up),length};
    next.feedLength=length;next.seatedTip=next.grip;next.seatedTip.position=Add(next.grip.position,Scale(axis,length));
    const float bottom=Dot(bounds.world.position,axis)-length;
    // Most pistol floor plates protrude several millimetres below the grip.
    float mouth=bottom+std::clamp(length*.2f,.005f*units,.012f*units);
    if(topLoaded){
        // Keep the approach outside the authored top face for the short seat.
        mouth=bottom;
        next.frameMouth=true;
    }else if(haveFrame){
        const float projected=Dot(frame->world.position,axis)-Extent(*frame,axis);
        // A whole-weapon frame box can extend below an inclined magazine.
        // Retain the local floor-plate estimate if that plane is implausible.
        const float highest=category==0?bottom+length:bottom+2*length-.005f*units;
        if(std::isfinite(projected)&&projected>=bottom-.005f*units&&projected<=highest){mouth=projected;next.frameMouth=true;}
    }
    if(category!=0&&measuredMouth&&ValidatePose(*measuredMouth,.05f)){
        const float projected=Dot(measuredMouth->position,axis);
        if(std::isfinite(projected)&&projected>=bottom+.005f*units&&projected<=bottom+2*length-.005f*units){mouth=projected;next.frameMouth=next.meshMouth=true;}
    }
    next.mouth=next.grip;next.mouth.position=Add(bounds.world.position,Scale(axis,mouth-Dot(bounds.world.position,axis)));
    next.seatTravel=Dot(Subtract(next.seatedTip.position,next.mouth.position),axis);
    if(!ValidatePose(next.seatedTip,.05f)||!ValidatePose(next.mouth,.05f)||next.seatTravel<.005f*units)return false;
    output=next;return true;
}
struct SlideContact {CameraPose top{};Vec3 a{},b{};float radius=0;Bounds box{};};
inline bool Slide(const CameraPose& rendered,Vec3 centre,Vec3 half,float units,SlideContact& output,unsigned category=0,const CameraPose* gun=nullptr){
    if(!std::isfinite(units)||units<1||units>1000)return false;
    Bounds bounds;if(!BuildBounds(rendered,centre,half,bounds))return false;
    if(gun){Bounds aligned;if(!AlignBounds(bounds,*gun,aligned))return false;bounds=aligned;}
    const Vec3 axes[]{bounds.world.basis.right,bounds.world.basis.up,bounds.world.basis.forward};
    const float extent[]{bounds.half.x,bounds.half.y,bounds.half.z};unsigned along=2;
    if(category!=0){along=0;for(unsigned i=1;i<3;++i)if(extent[i]>extent[along])along=i;}
    const float length=extent[along];if(length<(category==0?.01f:.001f)*units||length>.4f*units)return false;
    SlideContact next;next.box=bounds;next.top=bounds.world;next.top.position=Add(bounds.world.position,Scale(bounds.world.basis.up,bounds.half.y));
    // Acquisition passes through the actual rigid box centre, not a narrow
    // capsule suspended above it. Both lateral faces can be gripped naturally.
    next.a=Subtract(bounds.world.position,Scale(axes[along],length));next.b=Add(bounds.world.position,Scale(axes[along],length));
    float cross=0;for(unsigned i=0;i<3;++i)if(i!=along)cross=std::max(cross,extent[i]);
    next.radius=std::clamp(cross+.03f*units,.04f*units,.08f*units);
    output=next;return true;
}
inline bool AK74Bolt(const CameraPose& authoredAnchor,const CameraPose& gun,float units,SlideContact& output){
    if(!ValidatePose(authoredAnchor,.05f)||!ValidatePose(gun,.05f)||!std::isfinite(units)||units<1||units>1000)return false;
    CameraPose handle=gun;
    handle.position=Add(Add(authoredAnchor.position,Scale(gun.basis.forward,.055f*units)),
        Scale(gun.basis.right,.025f*units));
    return Slide(handle,{},Scale({.015f,.014f,.022f},units),units,output,4);
}
}
