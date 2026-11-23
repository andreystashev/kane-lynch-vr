#pragma once
#include "native_camera_mover_kl.hpp"
#include "native_weapon_ownership.hpp"
#include "team_command_types.hpp"
#include <array>
#include <cstring>

namespace NativeTeamCommandsImage {
using namespace NativeWeaponOwnership;
constexpr std::uintptr_t Recall=0x213BC0,Move=0x2130E0,Attack=0x213E20;
constexpr std::uintptr_t SelectReceiver=0x212100;
inline constexpr std::array<std::uintptr_t,4> OrderAdvanceReturns{0x2133EE,0x213A76,0x213D7C,0x214018};
inline bool OrderAdvance(std::uintptr_t caller){
    for(const auto address:OrderAdvanceReturns)if(address==caller)return true;
    return false;
}
template<class Reader> bool VerifyOrderAdvance(std::uintptr_t image,Reader read){
    constexpr unsigned char head[]={0x83,0xEC,0x68,0x53,0x56,0x8B,0xF1};
    unsigned char actual[sizeof(head)]{};
    if(!read(image+SelectReceiver,actual,sizeof(actual))||std::memcmp(head,actual,sizeof(head)))return false;
    for(const auto address:OrderAdvanceReturns){
        unsigned char opcode=0;std::int32_t displacement=0;
        if(!read(image+address-5,&opcode,1)||opcode!=0xE8||
           !read(image+address-4,&displacement,4)||static_cast<std::int64_t>(address)+displacement!=SelectReceiver)return false;
    }
    return true;
}
constexpr std::uintptr_t Target=0x231C00,FallbackTarget=0x232E30;
constexpr std::uintptr_t ControlVtable=0x568374,ActorVtable=0x56477C;
constexpr std::uint32_t MaximumMembers=4,MaximumActors=2048;
template<class Reader> bool Verify(std::uintptr_t image,Reader read) {
    const auto match=[&](std::uintptr_t rva,const auto& expected) {
        unsigned char actual[64]{};
        return sizeof(expected)<=sizeof(actual)&&read(image+rva,actual,sizeof(expected))&&!std::memcmp(actual,expected,sizeof(expected));
    };
    const unsigned char recall[]={0x83,0xEC,0x24,0x56,0x8B,0xF1,0x8B,0x4E,4,0xE8};
    const unsigned char move[]={0x81,0xEC,0x24,1,0,0,0x56,0x8B,0xF1,0xB8,1,0,0,0};
    const unsigned char attack[]={0x81,0xEC,0xDC,0,0,0,0x56,0x8B,0xF1,0x8B,0x4E,4,0xE8};
    const unsigned char target[]={0x83,0xEC,8,0x53,0x55,0x56,0x8B,0xF1};
    const unsigned char fallback[]={0x80,0xB9,0x9C,0,0,0,0,0xD9,5};
    const unsigned char targetCall[]={0x8B,0xC8,0xE8,0x34,0xDD,1,0};
    const unsigned char fallbackCall[]={0x8B,0xC8,0xE8,0x28,0xEF,1,0};
    const unsigned char members[]={0x8B,0x41,0x14,0xC3};
    const unsigned char selected[]={0x8B,0x41,0x10,0x8B,0x51,0x14};
    const unsigned char life[]={0x33,0xC0,0x38,0x81,0x36,5,0,0,0x0F,0x94,0xC0,0xC3};
    const unsigned char activeLife[]={0x33,0xC0,0x80,0xB9,0x36,5,0,0,1,0x0F,0x94,0xC0,0xC3};
    const unsigned char faction[]={0x8B,0x42,0x54,0xBB,1,0,0,0,0xD3,0xE3,0x85,0x58,0x0C,0x74};
    const unsigned char npcList[]={0xA1,0x0C,0x76,0xA8,0,0x8B,0x48,4,0x8D,0x78,8};
    std::uint32_t value=0;float duration=0;
    return NativeCameraMoverKL::VerifyImage(image,read)&&match(Recall,recall)&&match(Move,move)&&match(Attack,attack)&&
        match(Target,target)&&match(FallbackTarget,fallback)&&match(0x213EC5,targetCall)&&match(0x213F01,fallbackCall)&&
        match(0x3531D0,members)&&match(0x2120E0,selected)&&match(0x1167F0,life)&&match(0x116800,activeLife)&&match(0x23252A,faction)&&match(0x232760,npcList)&&
        Read(image+0x53C9A4,duration,read)&&duration==1000.f&&
        Read(image+ActorVtable+0x3F0,value,read)&&value==image+0x1167F0&&
        Read(image+ActorVtable+0x30,value,read)&&value==image+0x1DE400;
}
template<class Reader> TeamCommandIPC::ReceiverCertificate Receiver(std::uintptr_t image,std::uint32_t now,std::uint32_t ack,Reader read,bool inventory=false) {
    TeamCommandIPC::ReceiverCertificate result;
    std::uint32_t data=0,player=0,vt=0,control=0,owner=0,count=0,selection=0;
    std::uint8_t paused[2]{};
    if(!now||!read(image+0x8883A4,paused,sizeof(paused))||(paused[0]&&!(inventory&&paused[0]==3))||paused[1]||
       !Read(image+0x68760C,data,read)||!Read(data+0xA2C,player,read)||!Read(player,vt,read)||vt!=image+0x56582C||
       !Read(player+0xAA8,control,read)||!Read(control,vt,read)||vt!=image+ControlVtable||
       !Read(control+4,owner,read)||owner!=player||!Read(control+0x14,count,read)||!count||count>MaximumMembers||
       !Read(control+0x10,selection,read)||selection<control+0x18||selection>control+0x18+4*count||(selection-control-0x18)%4)return result;
    std::uint8_t multiplayer=1;if(!Read(control+0x9C,multiplayer,read)||multiplayer)return result;
    result.player=player;result.control=control;result.tick=now;result.recenter=ack;
    result.all=selection==control+0x18+4*count;
    if(!result.all) {
        std::uint32_t member=0,memberOwner=0,squad=0,expectedSquad=0;std::uint8_t enabled=0;
        if(!Read(selection,member,read)||!member||!Read(member+0x238,memberOwner,read)||memberOwner!=player||
           !Read(control+0xA0,expectedSquad,read)||!Read(member+0xE8,squad,read)||squad!=expectedSquad||
           !Read(member+0x261,enabled,read)||enabled!=1||!Read(member+0x2DC,result.nativeId,read))return {};
        result.receiver=member;
    }
    std::uint32_t after=0;
    if(!Read(player+0xAA8,after,read)||after!=control||!Read(control+0x10,after,read)||after!=selection||
       !Read(control+0x14,after,read)||after!=count)return {};
    result.valid=true;return result;
}
inline bool SameReceiver(const TeamCommandIPC::ReceiverCertificate& a,const TeamCommandIPC::ReceiverCertificate& b) {
    return a.valid&&b.valid&&a.player==b.player&&a.control==b.control&&a.receiver==b.receiver&&a.nativeId==b.nativeId&&a.all==b.all&&a.recenter==b.recenter;
}
template<class Reader> bool Reference(std::uintptr_t image,std::uintptr_t geometry,Reader read,std::uint32_t& reference) {
    std::uint32_t manager=0,pool=0,stride=0,count=0,data=0,generations=0;std::uint16_t generation=0;
    if(!Read(image+0x6C99B8,manager,read)||!Read(manager+0xC,pool,read)||!Read(pool+4,stride,read)||stride!=0x70||!Read(pool+8,count,read)||
       !count||count>0x100000||!Read(pool+0xC,data,read)||!Read(pool+0x10,generations,read)||geometry<data||
       (geometry-data)%stride||geometry-data>=std::size_t(count)*stride)return false;
    const auto index=(geometry-data)/stride;
    if(!Read(generations+index*2,generation,read)||generation>=0x8000)return false;
    const std::uint64_t value=1+index+std::uint64_t(generation)*count;
    if(value>=0x40000000)return false;
    reference=static_cast<std::uint32_t>(value);std::uintptr_t checked=0;
    return ResolveGeometry(image,reference,read,checked)&&checked==geometry;
}
// Same NPC array and faction-mask direction used by native target picker
// 632620/6324C0. Geometry owners must also belong to that authoritative array.
template<class Reader> bool HostileActor(std::uintptr_t image,std::uintptr_t player,std::uint32_t hitReference,Reader read,
                                        std::uintptr_t& actor,std::uint32_t& actorReference) {
    std::uint32_t data=0,count=0;std::array<std::uint32_t,MaximumActors> actors{};
    if(!Read(image+0x68760C,data,read)||!Read(data+4,count,read)||count>MaximumActors||
       !read(data+8,actors.data(),count*4))return false;
    std::uintptr_t geometry=0;if(!ResolveGeometry(image,hitReference,read,geometry))return false;
    for(unsigned depth=0;geometry&&depth<16;++depth) {
        std::uint32_t owner=0,parent=0;
        if(!Read(geometry+0x60,owner,read)||!Read(geometry+0x50,parent,read)||parent==geometry)return false;
        bool listed=false;for(unsigned i=0;i<count;++i)if(actors[i]==owner){listed=true;break;}
        if(listed&&owner!=player) {
            std::uint32_t vt=0,root=0,back=0,flags=0,targetFaction=0,playerFaction=0,id=0,mask=0;std::uint8_t life=0;
            // Native +3F0 rejects state0 (5167F0); the separate active getter
            // 516800 and squad eligibility 611F25 select exactly state1. This
            // command picker deliberately excludes wounded/transition states.
            if(!Read(owner,vt,read)||vt!=image+ActorVtable||!Read(owner+4,root,read)||!Read(root+0x60,back,read)||back!=owner||
               !Read(root+0x3C,flags,read)||(flags&0x400)||!Read(owner+0x536,life,read)||life!=1||
               !Read(owner+0x54,targetFaction,read)||!Read(targetFaction+4,id,read)||id>=32||
               !Read(player+0x54,playerFaction,read)||!Read(playerFaction+0xC,mask,read)||!(mask&(1u<<id))||
               !Reference(image,root,read,actorReference))return false;
            actor=owner;return true;
        }
        geometry=parent;
    }
    return false;
}
}
