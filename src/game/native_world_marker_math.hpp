#pragma once
#include "native_hud_math.hpp"
#include <cstddef>
#include <cstring>

namespace NativeWorldMarkerMath {
using namespace NativeCameraMath;
struct Observation {float minZ=0,maxZ=0,minW=0,maxW=0;bool normalizedNegativeW=false;};
struct Layout {
    bool valid=false;float width=0,height=0,offsetX=0,offsetY=0,sourceDepth=0;
    // Native perspective extents BEFORE division by source-camera depth and
    // the monitor-NDC safety cap. Reusing NDC at a different HMD distance
    // makes WORLD corners/pivots move as the player walks away.
    bool sourceExtentValid=false;
    float sourceWidth=0,sourceHeight=0,sourceOffsetX=0,sourceOffsetY=0;
};
// A mission goal is fixed in WORLD. Facing its quad along headset FORWARD
// rotates cropped sprite offsets around that point when only the head turns.
// Build the plane from the head POSITION instead; WORLD up also keeps the
// native upright icon independent of headset roll.
inline bool AnchoredBillboard(Vec3 target,Vec3 head,Basis& basis,float& distance) {
    const auto delta=Subtract(target,head);const float length2=Dot(delta,delta);
    if(!Finite(delta)||!std::isfinite(length2)||length2<1e-8f)return false;
    const float length=std::sqrt(length2);const auto forward=Scale(delta,1.f/length);
    auto right=Cross({0,1,0},forward);float right2=Dot(right,right);
    // A goal directly overhead has no horizontal bearing. Use a fixed WORLD
    // reference at that pole rather than borrowing a moving headset axis.
    if(right2<1e-8f){right=Cross({0,0,1},forward);right2=Dot(right,right);}
    if(!std::isfinite(right2)||right2<1e-8f)return false;
    right=Scale(right,1.f/std::sqrt(right2));const Basis next{forward,Cross(forward,right),right};
    if(!ValidateBasis(next,.05f))return false;
    basis=next;distance=length;return true;
}
inline Layout BoundedLayout(Layout value) {
    if(!value.valid)return value;
    // A near/behind native third-person camera must not enlarge a world
    // icon across the headset. Preserve authored aspect/crop as one unit.
    const float largest=std::max(value.width,value.height);
    if(!std::isfinite(largest)||largest<=0){value.valid=false;return value;}
    if(largest>.06f){const float scale=.06f/largest;
        value.width*=scale;value.height*=scale;value.offsetX*=scale;value.offsetY*=scale;}
    return value;
}
// Actual4996BF image writer emits centre+corner, including variable Z.
// This contract is only for a positively captured ActionMarker packet, never
// generic text/crosshair/world draws. Keep original atlas/UV/colour/shape while
// centring the native sprite on the tracked WORLD point in valid overlay clip.
inline bool Translate(const float* native,const NativeHudMath::Vertex* vertices,std::size_t count,
                      const CameraPose& eye,const float* projection,Vec3 target,float* output,Observation& observed,const Layout* layout=nullptr,
                      const CameraPose* sharedCentre=nullptr,const float* referenceProjection=nullptr,bool worldAnchored=false) {
    // ActionMarker653C6A submits header.count=1, i.e. exactly one native
    // four-corner sprite. An unrelated batched emitter is not authorized.
    if(!native||!vertices||count!=4||!output||!projection||!ValidatePose(eye,.05f)||!Finite(target))return false;
    for(unsigned i=0;i<16;++i)if(!std::isfinite(native[i])||!std::isfinite(projection[i]))return false;
    if(projection[0]<=0||projection[5]<=0||std::fabs(projection[11]-1)>.0001f||std::fabs(projection[15])>.0001f)return false;
    const auto delta=Subtract(target,eye.position);const Vec3 camera{Dot(delta,eye.basis.right),Dot(delta,eye.basis.up),Dot(delta,eye.basis.forward)};
    const float x=camera.x*projection[0]+camera.y*projection[4]+camera.z*projection[8]+projection[12];
    const float y=camera.x*projection[1]+camera.y*projection[5]+camera.z*projection[9]+projection[13];
    const float w=camera.x*projection[3]+camera.y*projection[7]+camera.z*projection[11]+projection[15];
    if(!std::isfinite(w)||w<=1e-4f||!std::isfinite(x)||!std::isfinite(y)||std::fabs(x/w)>4||std::fabs(y/w)>4)return false;
    Observation next;double centreX=0,centreY=0;float sign=0;
    for(std::size_t i=0;i<count;++i){const auto& p=vertices[i];
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)||!std::isfinite(p.u)||!std::isfinite(p.v))return false;
        const float nx=p.x*native[0]+p.y*native[1]+p.z*native[2]+native[3];
        const float ny=p.x*native[4]+p.y*native[5]+p.z*native[6]+native[7];
        const float nw=p.x*native[12]+p.y*native[13]+p.z*native[14]+native[15];
        if(!std::isfinite(nx)||!std::isfinite(ny)||!std::isfinite(nw)||(!layout&&std::fabs(nw)<1e-4f))return false;
        const float current=nw<0?-1.f:1.f;if(!sign)sign=current;else if(current!=sign&&!layout)return false;
        if(!i){next.minZ=next.maxZ=p.z;next.minW=next.maxW=nw;}
        else {next.minZ=std::min(next.minZ,p.z);next.maxZ=std::max(next.maxZ,p.z);next.minW=std::min(next.minW,nw);next.maxW=std::max(next.maxW,nw);}
        if(!layout){centreX+=nx/std::fabs(nw);centreY+=ny/std::fabs(nw);}
    }
    float rows[16];
    if(layout){
        if(!layout->valid||!std::isfinite(layout->width)||!std::isfinite(layout->height)||!std::isfinite(layout->offsetX)||!std::isfinite(layout->offsetY)||
           layout->width<=0||layout->height<=0||layout->width>2||layout->height>2||std::fabs(layout->offsetX)>2||std::fabs(layout->offsetY)>2)return false;
        float loX=vertices[0].x,hiX=loX,loY=vertices[0].y,hiY=loY;
        for(std::size_t i=1;i<count;++i){loX=std::min(loX,vertices[i].x);hiX=std::max(hiX,vertices[i].x);loY=std::min(loY,vertices[i].y);hiY=std::max(hiY,vertices[i].y);}
        if(hiX-loX<1e-5f||hiY-loY<1e-5f)return false;
        // One shared WORLD billboard: unequal SDK eye frusta must project
        // the same corners, not receive equal independent NDC rectangles.
        const auto& centre=sharedCentre?*sharedCentre:eye;const auto* reference=referenceProjection?referenceProjection:projection;
        if(!ValidatePose(centre,.05f)||!std::isfinite(reference[0])||!std::isfinite(reference[5])||reference[0]<=0||reference[5]<=0)return false;
        float depth=Dot(Subtract(target,centre.position),centre.basis.forward);Basis billboard=centre.basis;
        if(worldAnchored&&!AnchoredBillboard(target,centre.position,billboard,depth))return false;
        if(!std::isfinite(depth)||depth<=1e-4f)return false;
        const bool sourceExtent=worldAnchored&&layout->sourceExtentValid;
        if(sourceExtent&&(!std::isfinite(layout->sourceWidth)||!std::isfinite(layout->sourceHeight)||
           !std::isfinite(layout->sourceOffsetX)||!std::isfinite(layout->sourceOffsetY)||layout->sourceWidth<=0||layout->sourceHeight<=0))return false;
        // Retain the existing close-icon angular safety cap, but measure it
        // at the actual head distance rather than an unrelated native view Z.
        const float sourceScale=sourceExtent?std::min(1.f,.06f*depth/std::max(layout->sourceWidth,layout->sourceHeight)):1.f;
        const float width=sourceExtent?layout->sourceWidth*sourceScale:layout->width*depth,height=sourceExtent?layout->sourceHeight*sourceScale:layout->height*depth;
        const float offsetX=sourceExtent?layout->sourceOffsetX*sourceScale:layout->offsetX*depth,offsetY=sourceExtent?layout->sourceOffsetY*sourceScale:layout->offsetY*depth;
        const Vec3 axisX=Scale(billboard.right,width/(reference[0]*(hiX-loX)));
        const Vec3 axisY=Scale(billboard.up,height/(reference[5]*(hiY-loY)));
        const Vec3 offset=Add(Scale(billboard.right,offsetX/reference[0]),Scale(billboard.up,offsetY/reference[5]));
        const Vec3 base=Subtract(Subtract(Subtract(Add(target,offset),Scale(axisX,(loX+hiX)*.5f)),Scale(axisY,(loY+hiY)*.5f)),eye.position);
        const Vec3 columns[4]={axisX,axisY,{},base};
        for(unsigned i=0;i<4;++i){const auto& vector=columns[i];const float cx=Dot(vector,eye.basis.right),cy=Dot(vector,eye.basis.up),cz=Dot(vector,eye.basis.forward);
            rows[i]=cx*projection[0]+cy*projection[4]+cz*projection[8]+(i==3?projection[12]:0);
            rows[4+i]=cx*projection[1]+cy*projection[5]+cz*projection[9]+(i==3?projection[13]:0);
            rows[12+i]=cx*projection[3]+cy*projection[7]+cz*projection[11]+(i==3?projection[15]:0);rows[8+i]=.5f*rows[12+i];}
        for(float value:rows)if(!std::isfinite(value))return false;
        for(std::size_t i=0;i<count;++i)if(vertices[i].x*rows[12]+vertices[i].y*rows[13]+vertices[i].z*rows[14]+rows[15]<=1e-4f)return false;
        next.normalizedNegativeW=sign<0;observed=next;std::memcpy(output,rows,sizeof(rows));return true;
    }
    const float dx=x/w-float(centreX/count),dy=y/w-float(centreY/count);
    for(unsigned i=0;i<4;++i){const float positiveW=native[12+i]*sign;
        rows[i]=native[i]+positiveW*dx;rows[4+i]=native[4+i]+positiveW*dy;
        rows[8+i]=positiveW*.5f;rows[12+i]=positiveW;}
    for(float value:rows)if(!std::isfinite(value))return false;
    next.normalizedNegativeW=sign<0;observed=next;std::memcpy(output,rows,sizeof(rows));return true;
}
}
