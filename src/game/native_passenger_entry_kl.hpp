#pragma once
#include "native_action_kl.hpp"
#include "native_player_pose.hpp"
#include <array>
#include <cstring>

// M03B startup archive names its passenger carrier and separate door proxy.
// We do not treat the Vehicle clip loop as door state. This read-only scope
// exposes the native input lock during the first entry dialogue; the camera
// policy permanently retires clearance after the first unlocked observation.
namespace NativePassengerEntryKL {
struct State {
    bool valid=false,known=false,blocked=false;
    unsigned stage=0;
    std::uintptr_t player=0,movement=0,record=0,carrier=0,geometry=0,controller=0,camera=0;
};
struct PairCache {
    State first{};std::uint64_t pair=0;std::uint32_t tick=0;
    void Reset(){*this={};}
    State Select(const State& current,std::uint64_t nextPair,std::uint32_t now){
        const bool same=current.valid&&first.valid&&current.player==first.player&&current.movement==first.movement&&
            current.record==first.record&&current.carrier==first.carrier&&current.geometry==first.geometry&&
            current.controller==first.controller&&current.camera==first.camera;
        if(same&&nextPair&&pair==nextPair&&tick&&std::int32_t(now-tick)>=0&&now-tick<=150){
            auto output=current;output.known=first.known;output.blocked=first.blocked;return output;
        }
        first=current;pair=nextPair;tick=now;return current;
    }
};
template<class Reader>bool Name(std::uintptr_t node,Reader read,std::array<char,192>& result){
    std::uint32_t pointer=0;if(!NativeActionKL::Address(node+0x6C,4)||!read(node+0x6C,&pointer,4)||!NativeActionKL::Address(pointer,192))return false;
    std::array<char,192> text{};
    if(!read(pointer,text.data(),text.size()))for(unsigned i=0;i<text.size();++i){if(!read(pointer+i,&text[i],1))return false;if(!text[i])break;}
    for(unsigned i=0;i<text.size();++i){if(!text[i]){if(!i)return false;result=text;return true;}
        if(static_cast<unsigned char>(text[i])<32||static_cast<unsigned char>(text[i])>126)return false;}
    return false;
}
template<class Reader>bool Scope(std::uintptr_t node,Reader read){
    constexpr char full[]="ROOT\\Hero_Allies!Mercs!MercSquad_Driver\\GetawayVan_01";
    std::array<char,192> name{};if(!Name(node,read,name))return false;
    if(!std::strcmp(name.data(),full))return true;
    if(std::strcmp(name.data(),"GetawayVan_01"))return false;
    const auto parent=[&](){std::uint32_t next=0;if(!read(node+0x50,&next,4)||!next||next==node)return false;node=next;return Name(node,read,name);};
    if(!parent())return false;
    if(!std::strcmp(name.data(),"ROOT\\Hero_Allies!Mercs!MercSquad_Driver"))return true;
    if(!std::strcmp(name.data(),"Hero_Allies!Mercs!MercSquad_Driver"))return parent()&&!std::strcmp(name.data(),"ROOT");
    if(!std::strcmp(name.data(),"Mercs!MercSquad_Driver"))return parent()&&
        (!std::strcmp(name.data(),"ROOT\\Hero_Allies")||(!std::strcmp(name.data(),"Hero_Allies")&&parent()&&!std::strcmp(name.data(),"ROOT")));
    return !std::strcmp(name.data(),"MercSquad_Driver")&&parent()&&!std::strcmp(name.data(),"Mercs")&&parent()&&
        (!std::strcmp(name.data(),"ROOT\\Hero_Allies")||(!std::strcmp(name.data(),"Hero_Allies")&&parent()&&!std::strcmp(name.data(),"ROOT")));
}
template<class Reader>State Snapshot(std::uintptr_t image,std::uintptr_t player,std::uintptr_t movement,std::uintptr_t camera,Reader read){
    State out;out.stage=1;const auto action=NativeActionKL::Snapshot(image,read);
    if(!action.valid||action.player!=player||action.movement!=movement||action.movementKind!=NativeActionKL::Movement::Vehicle)return out;
    std::uint32_t record=0,carrier=0,geometry=0,owner=0,back=0,currentCamera=0,controller=0;NativeCameraMath::CameraPose world;
    out.stage=2;
    if(!read(player+0xB04,&record,4)||!NativeActionKL::Address(record,0x1C)||!read(record+0x18,&carrier,4)||
       !NativeActionKL::Address(carrier,8)||!read(carrier+4,&geometry,4)||!read(geometry+0x60,&owner,4)||
       (owner!=carrier&&(!read(owner+4,&back,4)||back!=geometry))||!NativePlayerPose::World(geometry,read,world))return out;
    out.stage=3;if(!Scope(geometry,read))return out;
    out.stage=4;if(!read(player+0x9A0,&currentCamera,4)||currentCamera!=camera||!camera)return out;
    out.player=player;out.movement=movement;out.record=record;out.carrier=carrier;out.geometry=geometry;out.camera=camera;
    std::uint8_t blocked=2;
    const bool known=read(player+0x9B8,&controller,4)&&NativeActionKL::Address(controller,0x19)&&read(controller+0x18,&blocked,1)&&blocked<=1;
    const auto after=NativeActionKL::Snapshot(image,read);std::uint32_t afterRecord=0,afterCarrier=0,afterGeometry=0,afterCamera=0,afterController=0,afterOwner=0,afterBack=0;
    std::uint8_t afterBlocked=2;
    out.stage=5;
    if(!after.valid||after.player!=player||after.movement!=movement||after.action!=action.action||
       !read(player+0xB04,&afterRecord,4)||afterRecord!=record||!read(record+0x18,&afterCarrier,4)||afterCarrier!=carrier||
       !read(carrier+4,&afterGeometry,4)||afterGeometry!=geometry||!Scope(geometry,read)||
       !read(geometry+0x60,&afterOwner,4)||afterOwner!=owner||
       (owner!=carrier&&(!read(owner+4,&afterBack,4)||afterBack!=geometry))||
       !read(player+0x9A0,&afterCamera,4)||afterCamera!=camera)return out;
    if(known&&(!read(player+0x9B8,&afterController,4)||afterController!=controller||!read(controller+0x18,&afterBlocked,1)||afterBlocked!=blocked))return out;
    out.valid=true;out.known=known;out.blocked=known&&blocked;out.controller=controller;out.stage=known?100:90;return out;
}
}
