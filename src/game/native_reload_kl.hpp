#pragma once
#include "native_weapon_ownership.hpp"
#include <atomic>
#include <cstdint>
#include <cstring>

// Only the three native weapon-update callers initiate an autonomous reload.
// Hook the player's +4C0 request BEFORE it ends/allocates an actor action;
// never reject Reload.Start after the dispatcher has installed that action.
namespace NativeReloadKL {
// Consume each explicit controller press at most once. Zero is the IPC's
// no-event value; tick ordering is wrap aware.
constexpr std::uint32_t IntentMaximumAgeMs=500;
class IntentArbiter {
public:
    // Advisory fast path only; Simulation claims the tick after eligibility.
    bool AlreadySeen(std::uint32_t tick)const {
        if(!tick)return true;
        const auto previous=lastTick_.load(std::memory_order_acquire);
        return previous&&static_cast<std::int32_t>(tick-previous)<=0;
    }
    bool Simulation(bool valid,std::uint32_t tick,std::uint32_t now,bool eligible) {
        if(!valid||!tick||static_cast<std::int32_t>(now-tick)<0)return false;
        const bool dispatch=eligible&&now-tick<=IntentMaximumAgeMs;
        auto previous=lastTick_.load(std::memory_order_acquire);
        while(!previous||static_cast<std::int32_t>(tick-previous)>0) {
            if(lastTick_.compare_exchange_weak(previous,tick,std::memory_order_acq_rel,std::memory_order_acquire))return dispatch;
        }
        return false;
    }
private:
    std::atomic<std::uint32_t> lastTick_{0};
};
constexpr std::uintptr_t Request=0x1F7390;
inline bool AutomaticCaller(std::uint32_t rva) {
    return rva==0x1F5C12||rva==0x1F5C42||rva==0x1F6136;
}
template<class Reader> bool Verify(std::uintptr_t image,Reader read) {
    const auto match=[&](std::uintptr_t rva,const auto& expected) {
        unsigned char actual[sizeof(expected)]{};
        return read(image+rva,actual,sizeof(actual))&&!std::memcmp(actual,expected,sizeof(actual));
    };
    const unsigned char request[]={0x55,0x8B,0xE9,0xF6,0x85,0xBC,0x11,0,0,2,0x74,0x57,0x53,0x56,
        0x8B,0x74,0x24,0x10,0x57,0x56,0xE8,0xE7,0xC7,0xFF,0xFF};
    const unsigned char update1[]={0xF7,0xC7,0x40,0,0,0,0x75,0x47,0x8B,0x16,0x8B,0x82,0xC0,4,0,0,0x55,0x8B,0xCE,0xFF,0xD0};
    const unsigned char update2[]={0xF7,0xC7,0x40,0,0,0,0x75,0x17,0x8B,0x16,0x8B,0x82,0xC0,4,0,0,0x55,0x8B,0xCE,0xFF,0xD0};
    const unsigned char update3[]={0x8B,0x16,0x8B,0x82,0xC0,4,0,0,0x8B,0xCE,0x57,0xFF,0xD0};
    const unsigned char manual[]={0x8B,0x16,0x8B,0x44,0x24,0x10,0x8B,0x92,0xC0,4,0,0,0x50,0x8B,0xCE,0xFF,0xD2};
    unsigned char magazine[]={0x83,0x3D,0,0,0,0,0,0x74,6,0xB8,0xE7,3,0,0,0xC3,
        0x8B,0x81,0xD8,0,0,0,0x35,0xEB,0xBE,0xEB,0xBE,0xC3};
    const auto cheat=static_cast<std::uint32_t>(image+0x873680);std::memcpy(magazine+2,&cheat,4);
    std::uint32_t method=0;
    return read(image+0x56582C+0x4C0,&method,4)&&method==image+Request&&
        match(Request,request)&&match(0x1F5BFD,update1)&&match(0x1F5C2D,update2)&&
        match(0x1F6129,update3)&&match(0x12B324,manual)&&match(0x11DDB0,magazine);
}
struct Observation {
    std::uintptr_t player=0,weapon=0;
    unsigned side=0,magazine=0;
};
template<class Reader> bool Capture(std::uintptr_t image,std::uintptr_t expectedPlayer,
                                     std::uintptr_t expectedWeapon,Reader read,Observation& output) {
    if(!expectedPlayer||!expectedWeapon)return false;
    const auto get=[&](std::uintptr_t at,auto& value) {
        return NativeWeaponOwnership::Address(at,sizeof(value))&&read(at,&value,sizeof(value));
    };
    std::uint32_t data=0,player=0,playerVtable=0;
    if(!get(image+0x68760C,data)||!get(std::uintptr_t(data)+0xA2C,player)||player!=expectedPlayer||
       !get(player,playerVtable)||playerVtable!=image+0x56582C)return false;
    for(unsigned side=0;side<2;++side) {
        const auto hand=std::uintptr_t(player)+(side?0x358:0x2D8);
        NativeWeaponOwnership::HandState held,after;
        std::uint32_t handVtable=0,vtable=0,getter=0,setter=0,encoded=0,cheat=0,reference=0;
        if(!get(hand,handVtable)||handVtable!=image+0x5508B4||
           !NativeWeaponOwnership::ReadHandState(hand,read,held)||held.item!=expectedWeapon||held.enabled!=1||
           held.attacher!=(side?31u:32u)||!get(expectedWeapon,vtable)||
           (vtable!=image+0x54EA4C&&vtable!=image+0x577B74)||
           !get(std::uintptr_t(vtable)+0x308,getter)||getter!=image+0x11DDB0||
           !get(std::uintptr_t(vtable)+0x2D4,setter)||setter!=image+0x11DF40||
           !get(expectedWeapon+0x58,reference)||!get(expectedWeapon+0xD8,encoded)||!get(image+0x873680,cheat))continue;
        const unsigned magazine=cheat?999u:(encoded^0xBEEBBEEBu);
        if(magazine>10000)return false;
        std::uint32_t afterData=0,afterPlayer=0,afterVtable=0,afterEncoded=0,afterCheat=0,afterReference=0;
        if(!get(image+0x68760C,afterData)||afterData!=data||!get(std::uintptr_t(data)+0xA2C,afterPlayer)||afterPlayer!=player||
           !NativeWeaponOwnership::ReadHandState(hand,read,after)||after.item!=held.item||after.enabled!=held.enabled||after.attacher!=held.attacher||
           !get(expectedWeapon,afterVtable)||afterVtable!=vtable||!get(expectedWeapon+0x58,afterReference)||afterReference!=reference||
           !get(expectedWeapon+0xD8,afterEncoded)||afterEncoded!=encoded||!get(image+0x873680,afterCheat)||afterCheat!=cheat)return false;
        output={player,expectedWeapon,side,magazine};return true;
    }
    return false;
}
template<class Reader> bool IsLocalPlayer(std::uintptr_t image,std::uintptr_t actor,Reader read) {
    std::uint32_t data=0,player=0,vtable=0;
    return actor&&read(image+0x68760C,&data,sizeof(data))&&NativeWeaponOwnership::Address(data,0xA30)&&
        read(data+0xA2C,&player,sizeof(player))&&player==actor&&
        read(actor,&vtable,sizeof(vtable))&&vtable==image+0x56582C;
}
enum class Decision { Pass,Manual,Suppress };
inline Decision Decide(bool enabled,bool game,bool ready,bool cinematic,std::uint32_t caller,
                       bool localPlayer,bool manualR) {
    if(!enabled||!game||!ready||cinematic||!AutomaticCaller(caller)||!localPlayer)return Decision::Pass;
    return manualR?Decision::Manual:Decision::Suppress;
}
}
