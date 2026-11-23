#pragma once
#include "native_world_marker_math.hpp"
#include <array>
namespace NativeMarkerViewMath {
using namespace NativeCameraMath;
// Native icon dimensions describe monitor NDC. Preserve their authored PIXEL
// aspect when converting to the common SDK eye's angular/world billboard.
inline NativeWorldMarkerMath::Layout InEyeAspect(NativeWorldMarkerMath::Layout value,unsigned width,unsigned height,const float* reference) {
    if(!value.valid||!width||!height||width>16384||height>16384||!reference||
       !std::isfinite(reference[0])||!std::isfinite(reference[5])||reference[0]<=0||reference[5]<=0)return {};
    const float scale=(float(width)/height)*(reference[0]/reference[5]);
    if(!std::isfinite(scale)||scale<=0||scale>32)return {};
    value.width*=scale;value.offsetX*=scale;
    if(value.sourceExtentValid){value.sourceWidth*=scale;value.sourceOffsetX*=scale;}
    return NativeWorldMarkerMath::BoundedLayout(value);
}
struct Key {
    std::uintptr_t player=0,record=0;
    std::uint32_t type=0,texture=0,caller=0;
    std::uintptr_t entity=0,geometry=0;
    bool fixed=false;Vec3 fixedPoint{};
};
inline bool Same(const Key& a,const Key& b){return a.player==b.player&&a.record==b.record&&a.type==b.type&&a.texture==b.texture&&a.caller==b.caller&&a.entity==b.entity&&a.geometry==b.geometry&&
    a.fixed==b.fixed&&(!a.fixed||(a.fixedPoint.x==b.fixedPoint.x&&a.fixedPoint.y==b.fixedPoint.y&&a.fixedPoint.z==b.fixedPoint.z));}
struct PairCache {
    struct Entry {Key key{};std::uint64_t pair=0;std::uint32_t tick=0;Vec3 point{};NativeWorldMarkerMath::Layout layout{};};
    std::array<Entry,128> entries{};
    void Reset(){*this={};}
    bool Select(Key key,std::uint64_t pair,std::uint32_t now,Vec3& point,NativeWorldMarkerMath::Layout& layout,bool& reused) {
        reused=false;if(!key.player||!key.record||!pair||!now||!Finite(point)||!layout.valid)return false;
        Entry* replace=&entries[0];
        for(auto& entry:entries){
            if(Same(entry.key,key)){
                if(entry.pair==pair&&entry.tick&&std::int32_t(now-entry.tick)>=0&&now-entry.tick<=150){point=entry.point;layout=entry.layout;reused=true;return true;}
                replace=&entry;break;
            }
            if(!entry.key.record||now-entry.tick>now-replace->tick)replace=&entry;
        }
        *replace={key,pair,now,point,layout};return true;
    }
};
// Native perspective source dimensions are in UI camera coordinates. Pick a
// safe WORLD presentation once for this exact owned source/style. Moving away
// must not repeatedly enlarge its WORLD corners or its asymmetric crop pivot.
// Moving closer may shrink that presentation to preserve the same safety cap.
struct WorldScaleCache {
    static constexpr std::uint32_t MaximumAge=30000;
    struct Entry {Key key{};std::uint32_t tick=0;float style[4]{},scale=0;};
    std::array<Entry,128> entries{};
    void Reset(){*this={};}
    bool Select(Key key,std::uint32_t now,float distance,NativeWorldMarkerMath::Layout& layout,bool& reused){
        reused=false;
        const float style[]={layout.sourceWidth,layout.sourceHeight,layout.sourceOffsetX,layout.sourceOffsetY};
        if(!key.player||!key.record||!now||!layout.valid||!layout.sourceExtentValid||!std::isfinite(distance)||distance<=1e-4f||(key.fixed&&!Finite(key.fixedPoint)))return false;
        for(float value:style)if(!std::isfinite(value))return false;
        if(style[0]<=0||style[1]<=0)return false;
        const float safe=std::min(1.f,.06f*distance/std::max(style[0],style[1]));
        Entry* replace=&entries[0];float selected=safe;
        for(auto& entry:entries){
            if(Same(entry.key,key)){
                if(entry.tick&&std::int32_t(now-entry.tick)<0)return false;
                if(entry.tick&&now-entry.tick<=MaximumAge&&!std::memcmp(entry.style,style,sizeof(style))){selected=std::min(entry.scale,safe);reused=true;}
                replace=&entry;break;
            }
            if(!entry.key.record||now-entry.tick>now-replace->tick)replace=&entry;
        }
        if(!std::isfinite(selected)||selected<=0)return false;
        *replace={key,now,{style[0],style[1],style[2],style[3]},selected};
        layout.sourceWidth*=selected;layout.sourceHeight*=selected;layout.sourceOffsetX*=selected;layout.sourceOffsetY*=selected;
        return true;
    }
};
// Native type26 -> texture27h #Objective. Types18/19 are Loot/Money.
inline bool Objective(std::uint32_t type){return type==26;}
}
