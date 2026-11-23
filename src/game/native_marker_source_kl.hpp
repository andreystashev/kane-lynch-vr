#pragma once
#include "native_world_marker_math.hpp"
#include "native_weapon_ownership.hpp"
#include <array>
namespace NativeMarkerSourceKL {
struct CanonicalQuad {bool valid=false;unsigned kind=0;std::array<NativeCameraMath::Vec3,4> corners{};};
template<class Reader>NativeWorldMarkerMath::Layout Capture(std::uintptr_t image,std::uintptr_t camera,std::uintptr_t geometry,std::uintptr_t position,std::uint32_t kind,Reader read,CanonicalQuad* canonical=nullptr){
    if(canonical)*canonical={};
    using NativeWeaponOwnership::Read;NativeWorldMarkerMath::Layout out;std::uint32_t ui=0,vt=0,method=0,header[3]{},flags=0;
    const auto get=[&](std::uintptr_t at,auto& value){return Read(at,value,read);};
    float sx=0,sy=0,focal=0;NativeCameraMath::Vec3 centre;
    if(!get(camera+0x14C,ui)||!get(ui,vt)||!get(vt+0x244,method)||method!=image+0xF5BD0||
       !get(ui+0x38,sx)||!get(ui+0x3C,sy)||!get(ui+0x44,flags)||!get(ui+0x54,focal)||!get(position,centre)||!NativeCameraMath::Finite(centre)||
       !Read(geometry,header,read)||header[0]!=1||!header[2]||(kind!=5&&kind!=6))return out;
    std::array<float,29> payload{};if(!read(header[2],payload.data(),kind==5?40:116))return out;
    if(!std::isfinite(sx)||!std::isfinite(sy)||!std::isfinite(focal)||sx<=0||sy<=0||focal<=0)return out;
    const float depth=centre.z+payload[2];if(!std::isfinite(depth)||std::fabs(depth)<1e-4f)return out;
    // 4F5B50/4F5BD0 use normalized viewport fractions. 4F5F60 sets
    // +54=.5/tan(FOV/2), +3C=1/aspect. Twice that projection gives NDC.
    const float scaleX=(flags&1)?2*sx:2*focal/std::fabs(depth),scaleY=(flags&1)?2*sy:2*focal*sy/std::fabs(depth);
    float loX=0,hiX=0,loY=0,hiY=0;CanonicalQuad local;
    if(kind==5){loX=payload[0]-payload[7]*.5f;hiX=payload[0]+payload[7]*.5f;loY=payload[1]-payload[8]*.5f;hiY=payload[1]+payload[8]*.5f;}
    else {loX=hiX=payload[0]+payload[11]*payload[23];loY=hiY=payload[1]+payload[12]*payload[24];
        for(unsigned i=0;i<4;++i){const auto at=11+i*3;const float x=payload[0]+payload[at]*payload[23],y=payload[1]+payload[at+1]*payload[24];
            if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(payload[at+2])||std::fabs(payload[at+2])>.001f)return out;
            local.corners[i]={x,y,0};
            loX=std::min(loX,x);hiX=std::max(hiX,x);loY=std::min(loY,y);hiY=std::max(hiY,y);}}
    out.width=(hiX-loX)*scaleX;out.height=(hiY-loY)*scaleY;out.offsetX=(hiX+loX)*.5f*scaleX;out.offsetY=(hiY+loY)*.5f*scaleY;out.sourceDepth=depth;
    out.valid=std::isfinite(out.width)&&std::isfinite(out.height)&&std::isfinite(out.offsetX)&&std::isfinite(out.offsetY)&&out.width>0&&out.height>0;
    if(out.valid&&!(flags&1)){
        // Keep the authored extent before BoundedLayout caps its projected
        // monitor size. The source and HMD camera can have different depth
        // and orientation; dividing by one then multiplying by the other
        // must not redefine the marker's WORLD size or off-centre crop.
        out.sourceWidth=(hiX-loX)*2*focal;out.sourceHeight=(hiY-loY)*2*focal*sy;
        out.sourceOffsetX=(hiX+loX)*focal;out.sourceOffsetY=(hiY+loY)*focal*sy;
        out.sourceExtentValid=std::isfinite(out.sourceWidth)&&std::isfinite(out.sourceHeight)&&
            std::isfinite(out.sourceOffsetX)&&std::isfinite(out.sourceOffsetY)&&out.sourceWidth>0&&out.sourceHeight>0;
    }
    if(canonical&&out.valid&&hiX>loX&&hiY>loY){
        if(kind==5){
            // Exact 99551 writer corner order from the verified native table
            // A34728: (-.5,-.5),(.5,-.5),(.5,.5),(-.5,.5).
            local.corners={NativeCameraMath::Vec3{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}};
        }else for(auto& p:local.corners){p.x=2.f*((p.x-loX)/(hiX-loX))-1.f;p.y=2.f*((p.y-loY)/(hiY-loY))-1.f;}
        local.valid=true;local.kind=kind;for(const auto& p:local.corners)local.valid=local.valid&&NativeCameraMath::Finite(p);
        if(local.valid)*canonical=local;
    }
    return NativeWorldMarkerMath::BoundedLayout(out);
}
}
