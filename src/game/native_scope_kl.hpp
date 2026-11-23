#pragma once
#include "native_weapon_ownership.hpp"
#include "native_weapon_type_kl.hpp"
#include "native_camera_mover_kl.hpp"
#include <cstdint>
#include <cstring>

namespace NativeScopeKL {
// Scoped queries run only on an actual shot. Beyond the native visible far
// plane, a miss must not converge both rays at a nearby 100 m distance.
constexpr float MaximumAimRangeMetres=1000.f;
struct State {
    bool valid=false,active=false,precision=false;
    std::uintptr_t player=0,sourceCamera=0,aim=0,item=0,root=0,model=0;
    std::uint32_t category=~0u,mode=~0u,reference=0,tick=0,moverVtable=0,proofStage=0;
    int hand=-1;
};
template<class Reader> bool VerifyImage(std::uintptr_t image,Reader read) {
    const auto bytes=[&](std::uintptr_t rva,const unsigned char* expected,std::size_t count) {
        unsigned char actual[32]{};
        return count<=sizeof(actual)&&read(image+rva,actual,count)&&!std::memcmp(actual,expected,count);
    };
    const unsigned char aimGetter[]={0x8B,0x81,0xAC,9,0,0,0xC3};
    const unsigned char precisionStart[]={0x51,0x8A,0x54,0x24,8,0x84,0xD2,0x75,0x0A};
    const unsigned char precisionStore[]={0x88,0x91,0x9C,0,0,0,0x59,0xC2,4,0};
    const unsigned char displayPrecision[]={0x80,0xB8,0x9C,0,0,0,0};

    if(!NativeCameraMoverKL::VerifyImage(image,read)||!bytes(0x1E7910,aimGetter,sizeof(aimGetter))||!bytes(0x231DE0,precisionStart,sizeof(precisionStart))||
       !bytes(0x231E37,precisionStore,sizeof(precisionStore))||!bytes(0x254428,displayPrecision,sizeof(displayPrecision))||
       !NativeWeaponTypeKL::Verify(image,read))return false;
    std::uint32_t value=0,name=0;char text[24]{};
    return read(image+0x65E3F0+4,&value,4)&&value==15&&read(image+0x65E3F0+8,&name,4)&&name==image+0x584D5C&&
        read(name,text,sizeof("WT_SNIPERRIFLE"))&&!std::strcmp(text,"WT_SNIPERRIFLE");
}
template<class Reader> State Snapshot(std::uintptr_t image,std::uint32_t now,Reader read) {
    State result;std::uint32_t data=0,player=0,vt=0,camera=0,aim=0,owner=0;
    std::uint8_t precision=0,nativeMode=0xff;
    const auto value=[&](std::uintptr_t at,auto& out){return NativeWeaponOwnership::Read(at,out,read);};
    if(!value(image+0x8883A4,nativeMode)||nativeMode!=0)return result;
    result.proofStage=1;
    if(!value(image+0x68760C,data)||!value(data+0xA2C,player)||!value(player,vt)||vt!=image+0x56582C)return result;
    result.player=player;result.proofStage=2;
    if(!value(player+0x9A0,camera)||!value(camera,vt)||vt!=image+0x545A3C)return result;
    result.sourceCamera=camera;result.proofStage=3;
    if(!value(player+0x9AC,aim)||!value(aim,result.moverVtable)||!NativeCameraMoverKL::KnownVtable(image,result.moverVtable))return result;
    result.aim=aim;result.proofStage=4;
    if(!value(aim+8,owner)||owner!=player||!value(aim+0x9C,precision)||precision>1||!value(aim+0x68,result.mode))return result;
    result.precision=precision!=0;result.tick=now;result.proofStage=5;
    for(int side=1;side>=0;--side) {
        const auto hand=player+(side?0x358:0x2D8);NativeWeaponOwnership::HandState held;
        if(!value(hand,vt)||vt!=image+0x5508B4||!NativeWeaponOwnership::ReadHandState(hand,read,held)||held.enabled>1)return result;
        result.proofStage=6;
        if(!held.enabled||!held.item)continue;
        std::uint32_t root=0,model=0,category=0,reference=0;
        if(!value(held.item,vt)||vt!=image+0x577B74||!value(held.item+4,root)||!value(root+0x60,owner)||owner!=held.item||
           !value(held.item+0xC0,model)||!value(model,vt)||(vt!=image+0x577864&&vt!=image+0x54E6D4)||
           !value(model+NativeWeaponTypeKL::CategoryOffset,category)||!value(held.item+0x58,reference))continue;
        result.item=held.item;result.root=root;result.model=model;result.category=category;result.reference=reference;result.hand=side;
        result.proofStage=7;
        // Only a right-hand sniper owns the near-face optical view.
        result.active=result.precision&&category==15&&side==1;break;
    }
    std::uint32_t afterData=0,afterPlayer=0,afterCamera=0,afterAim=0;
    std::uint8_t afterPrecision=0;
    if(!value(image+0x68760C,afterData)||afterData!=data||!value(data+0xA2C,afterPlayer)||afterPlayer!=player||
       !value(player+0x9A0,afterCamera)||afterCamera!=camera||!value(player+0x9AC,afterAim)||afterAim!=aim||
       !value(aim+0x9C,afterPrecision)||afterPrecision!=precision){result.active=false;return result;}
    if(result.item) {
        NativeWeaponOwnership::HandState held;std::uint32_t root=0,model=0,reference=0;
        if(!NativeWeaponOwnership::ReadHandState(player+(result.hand?0x358:0x2D8),read,held)||
           !held.enabled||held.item!=result.item||!value(result.item+4,root)||root!=result.root||
           !value(result.item+0xC0,model)||model!=result.model||!value(result.item+0x58,reference)||reference!=result.reference){result.active=false;return result;}
    }
    result.valid=true;result.proofStage=8;return result;
}
} // namespace NativeScopeKL
