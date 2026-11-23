#pragma once
#include "native_action_kl.hpp"
#include <array>
#include <cstring>
namespace NativeCarriedPersonVisibilityKLImage {
// Native CarryPerson/BeingCarried establishes BOTH directions. A nearby
// actor, an inventory item, or an arbitrary geometry child is insufficient.
struct Owner {std::uintptr_t player=0,person=0,root=0,model=0;};
inline bool Same(const Owner& a,const Owner& b){return a.player==b.player&&a.person==b.person&&a.root==b.root&&a.model==b.model;}
template<class Reader>bool Verify(std::uintptr_t image,Reader read){
    const auto match=[&](std::uintptr_t rva,const auto& expected){unsigned char got[32]{};
        return sizeof(expected)<=sizeof(got)&&read(image+rva,got,sizeof(expected))&&!std::memcmp(got,expected,sizeof(expected));};
    const unsigned char reciprocal[]={0x8B,0xBC,0x24,0xE4,0,0,0,0x85,0xFF,0x75,1,0xCC,0x89,0xBE,0xFC,5,0,0};
    const unsigned char startCall[]={0x56,0x8B,0xCF,0xE8,0x3F,0xA3,1,0};
    const unsigned char carrier[]={0x8B,0x54,0x24,0x0C,0x5F,0x89,0x96,0x68,0x0C,0,0,0x5E,0xC2,4,0};
    const unsigned char stop[]={0x56,0x8B,0xF1,0x6A,7,0xC7,0x86,0x68,0x0C,0,0,0,0,0,0};
    const unsigned char stopCall[]={0x8B,0x8E,0xFC,5,0,0,0x56,0xE8,0x87,0xC0,0,0};
    const unsigned char clear[]={0xC7,0x86,0xFC,5,0,0,0,0,0,0};
    std::uint32_t actorClass=0,playerClass=0;
    return match(0x1E02D0,reciprocal)&&match(0x1E0359,startCall)&&match(0x1FA74A,carrier)&&
        match(0x1EECA0,stop)&&match(0x1E2C0D,stopCall)&&match(0x1E2C59,clear)&&
        read(image+0x56477C+0x30,&actorClass,4)&&actorClass==image+0x1DE400&&
        read(image+0x56582C+0x30,&playerClass,4)&&playerClass==image+0x1F1C60;
}
template<class Reader>Owner Capture(std::uintptr_t image,std::uintptr_t player,Reader read){
    const auto get=[&](std::uintptr_t at,auto& value){return NativeActionKL::Address(at,sizeof(value))&&read(at,&value,sizeof(value));};
    std::uint32_t data=0,local=0,vt=0,person=0,back=0,root=0,model=0,rootOwner=0;
    if(!player||!get(image+0x68760C,data)||!get(data+0xA2C,local)||local!=player||
       !get(player,vt)||vt!=image+0x56582C||!get(player+0xC68,person)||!person||person==player||
       !get(person,vt)||(vt!=image+0x56477C&&vt!=image+0x56582C)||
       !get(person+0x5FC,back)||back!=player||!get(person+4,root)||!root||
       !get(root+0x60,rootOwner)||rootOwner!=person||!get(person+0x70,model)||!model)return {};
    std::uint32_t after=0;
    if(!get(player+0xC68,after)||after!=person||!get(person+0x5FC,after)||after!=player||
       !get(data+0xA2C,after)||after!=player||!get(person+4,after)||after!=root||!get(person+0x70,after)||after!=model)return {};
    return {player,person,root,model};
}
template<class Reader>bool OwnEntry(std::uintptr_t image,std::uintptr_t entry,const Owner& owner,Reader read){
    const auto get=[&](std::uintptr_t at,auto& value){return NativeActionKL::Address(at,sizeof(value))&&read(at,&value,sizeof(value));};
    if(!owner.person||!owner.player)return false;
    std::uint32_t vt=0,geometry=0;
    if(!get(entry,vt)||(vt!=image+0x540EE0&&vt!=image+0x540FF8&&vt!=image+0x541034&&vt!=image+0x541070)||
       !get(entry+0xF4,geometry)||!geometry)return false;
    std::array<std::uint32_t,16> visited{};unsigned count=0;
    while(geometry&&count<visited.size()){
        for(unsigned i=0;i<count;++i)if(visited[i]==geometry)return false;
        visited[count++]=geometry;
        if(geometry==owner.root||geometry==owner.model)return Same(owner,Capture(image,owner.player,read));
        std::uint32_t parent=0;if(!get(geometry+0x50,parent)||parent==geometry)return false;geometry=parent;
    }
    return false;
}
}
