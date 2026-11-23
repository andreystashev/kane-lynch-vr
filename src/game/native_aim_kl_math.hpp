#pragma once
#include "native_weapon_kl.hpp"
#include <array>
#include <cstddef>
#include <cstring>

namespace NativeAimKLMath {
using namespace NativeCameraMath;
using Pose=NativeWeaponKL::AimFrame;
constexpr std::uint32_t MaximumAgeMs=50,MinimumQueryIntervalMs=16;
constexpr float MaximumRangeMetres=100.f,RayToleranceMetres=.01f;
// KL1 ZROOM::Ray (510830), not the similarly shaped KL2 vtable slot.
struct Query {
    Vec3 start{},delta{},resultVector{};
    float fraction=1.f;
    std::array<unsigned char,0x14> unknown28{};
    std::uint32_t reference=0;
    std::array<unsigned char,0x24> unknown40{};
    std::uint8_t flags=1;
    std::array<unsigned char,3> unknown65{};
    std::uint32_t reference68=0,resultFlags=0;
};
static_assert(sizeof(Query)==0x70&&offsetof(Query,fraction)==0x24&&offsetof(Query,reference)==0x3c&&offsetof(Query,flags)==0x64,"KL1 native ROOM ray ABI");
inline bool Unit(Vec3 value,Vec3& output) {
    const float length2=Dot(value,value);
    if(!Finite(value)||!std::isfinite(length2)||length2<1.e-8f)return false;
    output=Scale(value,1.f/std::sqrt(length2));return Finite(output);
}
inline bool Valid(const Pose& pose,std::uint32_t now) {
    Vec3 unit;return pose.player&&pose.weapon&&pose.root&&pose.model&&pose.pairId&&pose.tick&&
        now-pose.tick<=MaximumAgeMs&&pose.hand>=0&&pose.hand<2&&
        std::isfinite(pose.unitsPerMetre)&&pose.unitsPerMetre>=1.f&&pose.unitsPerMetre<=1000.f&&
        Finite(pose.origin)&&Unit(pose.direction,unit);
}
inline bool Prepare(const Pose& pose,std::uint32_t now,Query& output) {
    Vec3 unit;if(!Valid(pose,now)||!Unit(pose.direction,unit))return false;
    Query query;query.start=pose.origin;query.delta=Scale(unit,MaximumRangeMetres*pose.unitsPerMetre);
    if(!Finite(query.delta))return false;output=query;return true;
}
inline bool Hit(const Query& before,const Query& after,std::uintptr_t result,Vec3& output) {
    if(!(result&0xff)||std::memcmp(&before.start,&after.start,sizeof(Vec3))||
       std::memcmp(&before.delta,&after.delta,sizeof(Vec3))||!Finite(after.start)||!Finite(after.delta)||
       !std::isfinite(after.fraction)||after.fraction<0.f||after.fraction>1.f)return false;
    const auto point=Add(after.start,Scale(after.delta,after.fraction));
    if(!Finite(point))return false;output=point;return true;
}
inline bool SameSource(const Pose& a,const Pose& b) {
    return a.player==b.player&&a.weapon==b.weapon&&a.root==b.root&&a.model==b.model&&
        a.hand==b.hand&&a.recenterAck==b.recenterAck&&a.unitsPerMetre==b.unitsPerMetre;
}
inline bool Same(const Pose& a,const Pose& b) {
    return SameSource(a,b)&&a.pairId==b.pairId&&a.tick==b.tick&&
        !std::memcmp(&a.origin,&b.origin,sizeof(Vec3))&&!std::memcmp(&a.direction,&b.direction,sizeof(Vec3));
}
inline bool Match(const Pose& queried,std::uint32_t queryTick,Vec3 point,const Pose& current,std::uint32_t now) {
    if(!Valid(queried,now)||!Valid(current,now)||!queryTick||now-queryTick>MaximumAgeMs||
       !SameSource(queried,current)||!Finite(point))return false;
    Vec3 unit;if(!Unit(current.direction,unit))return false;
    const float tolerance=RayToleranceMetres*current.unitsPerMetre;
    const auto shift=Subtract(queried.origin,current.origin),delta=Subtract(point,current.origin);
    const float distance=Dot(delta,unit);
    const auto offRay=Subtract(delta,Scale(unit,distance));
    return Dot(shift,shift)<=tolerance*tolerance&&distance>=0.f&&
        distance<=MaximumRangeMetres*current.unitsPerMetre+tolerance&&Dot(offRay,offRay)<=tolerance*tolerance;
}
// Visual contact is a real recent ROOM hit, not a physical Fire certificate.
// Permit bounded sub-degree controller motion between simulation and draw;
// retain the hit's WORLD surface point instead of inventing a distant dot.
inline bool MatchVisual(const Pose& queried,std::uint32_t tick,Vec3 point,const Pose& current,std::uint32_t now) {
    if(!current.player||!current.weapon||!current.root||!current.model||!current.pairId||
       current.hand<0||current.hand>1||!std::isfinite(current.unitsPerMetre)||current.unitsPerMetre<1||current.unitsPerMetre>1000||
       !Finite(queried.origin)||!Finite(current.origin))return false;
    if(!tick||std::int32_t(now-tick)<0||now-tick>150||!queried.tick||!current.tick||
       std::int32_t(now-queried.tick)<0||now-queried.tick>200||std::int32_t(now-current.tick)<0||now-current.tick>150||
       !SameSource(queried,current)||!Finite(point))return false;
    Vec3 a,b;if(!Unit(queried.direction,a)||!Unit(current.direction,b)||Dot(a,b)<.9848077f)return false; // 10 degrees, visual only
    const auto shift=Subtract(queried.origin,current.origin),delta=Subtract(point,current.origin);
    const float limit=.3f*current.unitsPerMetre,distance=Dot(delta,b);
    return Dot(shift,shift)<=limit*limit&&distance>=0&&distance<=MaximumRangeMetres*current.unitsPerMetre+limit;
}
}
