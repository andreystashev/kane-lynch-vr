#pragma once
#include "native_weapon_kl.hpp"
#include "native_weapon_ownership.hpp"
#include <array>
#include <cstring>

namespace NativeCommandRayKL {
using NativeWeaponKL::HeadGaze;
constexpr std::uint32_t HeadMaximumAgeMs=50;
inline bool Fresh(const HeadGaze& head,std::uint32_t now,std::uint32_t recenter) {
    const float length=NativeCameraMath::Dot(head.direction,head.direction);
    return head.player>=0x10000&&head.sourceCamera>=0x10000&&head.pairId&&head.tick&&
        std::int32_t(now-head.tick)>=0&&now-head.tick<=HeadMaximumAgeMs&&head.recenterAck==recenter&&head.mounted&&
        std::isfinite(head.unitsPerMetre)&&head.unitsPerMetre>=1&&head.unitsPerMetre<=1000&&
        NativeCameraMath::Finite(head.origin)&&NativeCameraMath::Finite(head.direction)&&length>.5f&&length<1.5f;
}
inline bool Same(const HeadGaze& a,const HeadGaze& b) {
    return a.player==b.player&&a.sourceCamera==b.sourceCamera&&a.pairId==b.pairId&&a.tick==b.tick&&a.recenterAck==b.recenterAck&&
        a.mounted==b.mounted&&a.unitsPerMetre==b.unitsPerMetre&&
        !std::memcmp(&a.origin,&b.origin,sizeof(a.origin))&&!std::memcmp(&a.direction,&b.direction,sizeof(a.direction));
}
struct Hands {
    bool valid=false,empty=false;
    std::uintptr_t player=0,camera=0;
    std::array<NativeWeaponOwnership::HandState,2> hand{};
};
// Empty is a positive native ownership observation, never inferred from a
// missing/stale VR gun pose. A retained item in an inactive hand still blocks
// head fallback. Unknown held-object classes are conservatively occupied.
template<class Reader> Hands ReadHands(std::uintptr_t image,std::uintptr_t expected,Reader read,bool inventory=false) {
    using namespace NativeWeaponOwnership;Hands result;std::uint32_t data=0,player=0,vt=0,camera=0;std::uint8_t paused[2]{};
    const auto get=[&](std::uintptr_t at,auto& out){return Read(at,out,read);};
    if(!read(image+0x8883A4,paused,sizeof(paused))||paused[1]||(paused[0]&&!(inventory&&paused[0]==3))||
       !get(image+0x68760C,data)||!get(data+0xA2C,player)||
       player!=expected||!get(player,vt)||vt!=image+0x56582C||!get(player+0x9A0,camera)||!get(camera,vt)||vt!=image+0x545A3C)return result;
    std::array<HandState,2> first{},second{};
    const auto collect=[&](auto& hands){for(unsigned side=0;side<2;++side){const auto hand=player+(side?0x358:0x2D8);
        if(!get(hand,vt)||vt!=image+0x5508B4||!ReadHandState(hand,read,hands[side])||hands[side].enabled>1)return false;}return true;};
    std::uint32_t afterData=0,afterPlayer=0,afterCamera=0;
    std::uint8_t afterPause[2]{};
    if(!collect(first)||!collect(second)||std::memcmp(first.data(),second.data(),sizeof(first))||
       !read(image+0x8883A4,afterPause,sizeof(afterPause))||std::memcmp(paused,afterPause,sizeof(paused))||
       !get(image+0x68760C,afterData)||afterData!=data||!get(data+0xA2C,afterPlayer)||afterPlayer!=player||
       !get(player+0x9A0,afterCamera)||afterCamera!=camera)return result;
    result.valid=true;result.player=player;result.camera=camera;result.hand=first;result.empty=!first[0].item&&!first[1].item;return result;
}
inline bool Matches(const Hands& hands,const NativeWeaponKL::AimFrame& weapon) {
    return hands.valid&&!hands.empty&&weapon.player==hands.player&&weapon.hand>=0&&weapon.hand<2&&
        weapon.weapon&&hands.hand[weapon.hand].item==weapon.weapon;
}
}
