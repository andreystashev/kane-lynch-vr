#pragma once
#include "native_player_pose.hpp"
#include "native_action_kl.hpp"
#include <array>
#include <cstring>

// Native 5A1B40 append / 5A18C0 refresh / 5A1940 child transform consumer.
// localPosition is added in the SAME native working space as cached bone
// position. When carrier geometry has no parent the cache is already world;
// otherwise 4F83A0 applies the carrier geometry chain. Never rotate it by the
// cached bone a second time or unconditionally apply carrier WORLD again.
namespace NativePassengerAttachmentKL {
using namespace NativeCameraMath;
struct Entry {
    std::uint32_t child=0,slot=0;
    Vec3 localPosition{};
    Basis localBasis{};
    Vec3 bonePosition{};
    Basis boneBasis{};
};
static_assert(offsetof(Entry,localPosition)==8&&offsetof(Entry,localBasis)==0x14&&
              offsetof(Entry,bonePosition)==0x38&&offsetof(Entry,boneBasis)==0x44&&sizeof(Entry)==0x68,"Native carrier attachment ABI");
struct Identity {
    std::uintptr_t player=0,movement=0,geometry=0,geometryOwner=0,parent=0,record=0,carrier=0,carrierGeometry=0,entry=0,child=0;
    std::uint32_t slot=0,count=0,index=0;
};
struct Probe {
    unsigned stage=0;
    std::uint32_t player=0,movement=0,movementVtable=0,movementId=0,action=0,actionVtable=0,actorAlias=0;
    std::uint32_t geometry=0,geometryOwner=0,parent=0,record=0,recordCarrier=0,playerCarrier=0,recordSlot=0;
    std::uint32_t carrierGeometry=0,carrierOwner=0,count=0,child=0,queueSlot=0,entry=0,matches=0;
};
inline bool Same(const Identity& a,const Identity& b) {
    // Other children may be removed and compact this native inline queue.
    // The owned child's semantic identity survives that bounded relocation.
    return a.player&&a.player==b.player&&a.movement==b.movement&&a.geometry==b.geometry&&a.geometryOwner==b.geometryOwner&&
        a.parent==b.parent&&a.record==b.record&&a.carrier==b.carrier&&a.carrierGeometry==b.carrierGeometry&&a.child==b.child&&a.slot==b.slot;
}
inline bool Valid(const Entry& entry) {
    return entry.child&&Finite(entry.localPosition)&&Finite(entry.bonePosition)&&
        ValidateBasis(entry.localBasis,.05f)&&ValidateBasis(entry.boneBasis,.05f);
}
inline Basis ActorInCarrier(const Entry& entry) {
    // Exact 42ED20: each cached basis row is transformed by localBasis.
    return {NativePlayerPose::Transform(entry.localBasis,entry.boneBasis.forward),
            NativePlayerPose::Transform(entry.localBasis,entry.boneBasis.up),
            NativePlayerPose::Transform(entry.localBasis,entry.boneBasis.right)};
}
inline bool Target(const Entry& entry,Vec3 original,Vec3 actorLocalMetres,float units,Vec3& target) {
    if(!Valid(entry)||!Finite(original)||!Finite(actorLocalMetres)||!std::isfinite(units)||units<1||units>1000)return false;
    const auto basis=ActorInCarrier(entry);if(!ValidateBasis(basis,.05f))return false;
    const auto next=Add(original,NativePlayerPose::Transform(basis,Scale(actorLocalMetres,units)));
    if(!Finite(next))return false;target=next;return true;
}
template<class Reader>bool Find(std::uintptr_t carrier,std::uintptr_t player,std::uintptr_t geometry,std::uintptr_t geometryOwner,
                               Reader read,Identity& identity,Entry& output,Probe* probe=nullptr) {
    std::uint32_t count=0;if(!read(carrier+0x124,&count,4)||count>16){if(probe)probe->stage=35;return false;}
    if(probe)probe->count=count;
    unsigned matches=0;Identity selected=identity;Entry found;
    for(unsigned i=0;i<count;++i){Entry item;const auto at=carrier+0x128+i*sizeof(Entry);
        if(!read(at,&item,sizeof(item))){if(probe)probe->stage=36;return false;}
        if(item.child!=player&&item.child!=geometryOwner)continue;
        std::uint32_t childGeometry=0;if(!read(item.child+4,&childGeometry,4)||childGeometry!=geometry||!Valid(item))continue;
        ++matches;selected.entry=at;selected.child=item.child;selected.slot=item.slot;selected.count=count;selected.index=i;found=item;
    }
    if(probe){probe->matches=matches;probe->entry=static_cast<std::uint32_t>(selected.entry);probe->child=static_cast<std::uint32_t>(selected.child);probe->queueSlot=selected.slot;}
    if(matches!=1){if(probe)probe->stage=37;return false;}
    std::uint32_t afterCount=0;Entry after;
    if(!read(carrier+0x124,&afterCount,4)||afterCount!=count||!read(selected.entry,&after,sizeof(after))||std::memcmp(&after,&found,sizeof(after))){if(probe)probe->stage=38;return false;}
    identity=selected;output=found;return true;
}
template<class Reader>bool Snapshot(std::uintptr_t image,std::uintptr_t player,Reader read,Identity& output,Entry& entry,Probe* probe=nullptr) {
    if(probe)*probe={};const auto action=NativeActionKL::Snapshot(image,read);
    if(probe){probe->stage=10;probe->player=static_cast<std::uint32_t>(action.player);probe->movement=static_cast<std::uint32_t>(action.movement);
        probe->movementVtable=action.movementVtable;probe->movementId=action.movementId;probe->action=static_cast<std::uint32_t>(action.action);probe->actionVtable=action.actionVtable;}
    if(!action.valid||action.player!=player)return false;
    if(action.movementKind!=NativeActionKL::Movement::Vehicle){if(probe)probe->stage=11;return false;}
    if(action.action&&!action.TrackedReload()&&!action.TrackedWeaponGesture()){if(probe)probe->stage=12;return false;}
    Identity next;next.player=player;next.movement=action.movement;
    const auto get=[&](std::uintptr_t at,std::uint32_t& value,unsigned stage,std::uint32_t Probe::*member){
        if(probe)probe->stage=stage;const bool okay=NativeActionKL::Address(at,4)&&read(at,&value,4);if(probe)probe->*member=value;return okay;};
    std::uint32_t alias=0,geometry=0,owner=0,parent=0,record=0,carrier=0,carrierGeometry=0,carrierOwner=0,again=0;
    if(!get(action.movement+0x70,alias,20,&Probe::actorAlias)||alias!=player||
       !get(player+4,geometry,21,&Probe::geometry)||!get(geometry+0x60,owner,22,&Probe::geometryOwner)||
       !get(geometry+0x50,parent,23,&Probe::parent))return false;
    if(owner!=player&&(!read(owner+4,&again,4)||again!=geometry)){if(probe)probe->stage=24;return false;}
    CameraPose actor;if(!NativePlayerPose::World(geometry,read,actor)){if(probe)probe->stage=25;return false;}
    if(!get(player+0xB04,record,30,&Probe::record)||!get(record+0x18,carrier,31,&Probe::recordCarrier)||
       !NativeActionKL::Address(carrier,0x128+16*sizeof(Entry)))return false;
    // These fields were incorrectly unconditional gates in .42. Native entry
    // skips recordSlot entirely when this child already has a queue record.
    if(probe){read(player+0x564,&probe->playerCarrier,4);read(record+0x30,&probe->recordSlot,4);}
    if(!get(carrier+4,carrierGeometry,32,&Probe::carrierGeometry)||
       !get(carrierGeometry+0x60,carrierOwner,33,&Probe::carrierOwner))return false;
    if(carrierOwner!=carrier&&(!read(carrierOwner+4,&again,4)||again!=carrierGeometry)){if(probe)probe->stage=34;return false;}
    CameraPose world;if(!NativePlayerPose::World(carrierGeometry,read,world)){if(probe)probe->stage=34;return false;}
    next.geometry=geometry;next.geometryOwner=owner;next.parent=parent;next.record=record;next.carrier=carrier;next.carrierGeometry=carrierGeometry;
    if(!Find(carrier,player,geometry,owner,read,next,entry,probe))return false;
    const auto after=NativeActionKL::Snapshot(image,read);std::uint32_t afterGeometry=0,afterRecord=0,afterCarrier=0,afterAlias=0;
    if(!after.valid||after.player!=player||after.movement!=action.movement||after.action!=action.action||
       !read(player+4,&afterGeometry,4)||afterGeometry!=geometry||!read(player+0xB04,&afterRecord,4)||afterRecord!=record||
       !read(record+0x18,&afterCarrier,4)||afterCarrier!=carrier||!read(action.movement+0x70,&afterAlias,4)||afterAlias!=player){if(probe)probe->stage=39;return false;}
    output=next;if(probe)probe->stage=100;return true;
}
}
