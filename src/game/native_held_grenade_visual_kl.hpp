#pragma once
#include "native_action_kl.hpp"
#include "native_weapon_ownership.hpp"
#include "native_weapon_type_kl.hpp"

namespace NativeHeldGrenadeVisualKL {
struct Owner {
    std::uintptr_t player=0,action=0,item=0,root=0,model=0;
    std::uint32_t reference=0,category=0;
    unsigned side=2;
};
// FF grenades can be temporarily attached to the player's weapon attacher
// while IKHAND still names the stowed firearm. Inventory/native-owner alone
// is insufficient: require this exact generation-checked wrist attachment.
// Legacy5FE810 also sets action+24 immediately before release; incoming
// ThrowBack has a different action and never receives this certificate.
template<class Reader> bool Capture(std::uintptr_t image,const NativeActionKL::State& action,
                                    std::uintptr_t expectedPlayer,Reader read,Owner& output) {
    using namespace NativeWeaponOwnership;
    const bool legacyThrow=action.action&&action.actionKind==NativeActionKL::Action::Throw&&action.actionVtable==image+0x56665C;
    if(!action.valid||action.player!=expectedPlayer||!expectedPlayer||(action.action&&!legacyThrow)||
       (action.movementKind!=NativeActionKL::Movement::Stand&&action.movementKind!=NativeActionKL::Movement::BaseMove&&
        action.movementKind!=NativeActionKL::Movement::Cover&&action.movementKind!=NativeActionKL::Movement::Vehicle))return false;
    std::uint32_t data=0,player=0,vt=0,actor=0;std::uint8_t released=0;
    if(!Read(image+0x68760C,data,read)||!Read(data+0xA2C,player,read)||player!=expectedPlayer||
       !Read(player,vt,read)||vt!=image+0x56582C)return false;
    if(legacyThrow&&(!Read(action.action,vt,read)||vt!=action.actionVtable||
       !Read(action.action+8,actor,read)||actor!=player||!Read(action.action+0x24,released,read)||released))return false;
    Snapshot attached;
    if(!NativeWeaponOwnership::Capture(image,player,read,attached))return false;
    for(int side=1;side>=0;--side) {
        const auto hand=player+(side?0x358:0x2D8);HandState held;
        if(!Read(hand,vt,read)||vt!=image+0x5508B4||!ReadHandState(hand,read,held)||held.enabled>1||
           held.attacher!=unsigned(side?31:32))continue;
        for(unsigned slot=0;slot<attached.count;++slot) {
            if(!attached.geometry[slot])ResolveGeometry(image,attached.records[slot].geometryRef,read,attached.geometry[slot]);
            if(!attached.geometry[slot])continue;
            const bool legacyAttacher=attached.records[slot].bone==held.attacher;
            // The live held-grenade snapshot proves only primitive 25 on the
            // right; the model still has to prove its named wrist below.
            const bool candidatePrimitive=side==1&&attached.records[slot].bone==25;
            if(!legacyAttacher&&!candidatePrimitive)continue;
            Owner next;next.player=player;next.action=action.action;next.root=attached.geometry[slot];next.side=unsigned(side);
            std::uint32_t root=0,model=0,item=0;
            if(!Read(next.root+0x60,item,read)||!Read(item,vt,read)||vt!=image+0x577B74||!Read(item+4,root,read)||root!=next.root||
               !Read(item+0xC0,model,read)||!Read(model,vt,read)||(vt!=image+0x577864&&vt!=image+0x54E6D4)||
               !Read(model+NativeWeaponTypeKL::CategoryOffset,next.category,read)||(next.category!=11&&next.category!=12)||
               !Read(item+0x58,next.reference,read)||!StillAttached(image,attached,slot,read))continue;
            int primitiveSide=-1;
            if(!legacyAttacher&&(!NativeWeaponOwnership::HandPrimitive(image,attached,slot,read,primitiveSide)||primitiveSide!=side))continue;
            next.item=item;next.model=model;
            std::uint32_t currentPlayer=0,dispatcher=0,dispatcherVtable=0,dispatcherOwner=0,currentAction=0;
            std::uint32_t afterItem=0,afterRoot=0,afterModel=0,afterVtable=0,afterModelVtable=0,afterCategory=0,afterReference=0;
            std::uint8_t launched=0;
            // The model/bone dictionary lookup may span several reads. A
            // detached, recycled or re-owned grenade must not inherit a pose
            // merely because the attachment was valid before that lookup.
            if(!Read(data+0xA2C,currentPlayer,read)||currentPlayer!=player||
               !Read(player+0x194,dispatcher,read)||!Read(dispatcher,dispatcherVtable,read)||dispatcherVtable!=image+0x54DE4C||
               !Read(dispatcher+8,dispatcherOwner,read)||dispatcherOwner!=player||
               !Read(dispatcher+4,currentAction,read)||currentAction!=action.action||
               !StillAttached(image,attached,slot,read)||!Read(next.root+0x60,afterItem,read)||afterItem!=item||
               !Read(item,afterVtable,read)||afterVtable!=image+0x577B74||!Read(item+4,afterRoot,read)||afterRoot!=next.root||
               !Read(item+0xC0,afterModel,read)||afterModel!=model||!Read(model,afterModelVtable,read)||
               (afterModelVtable!=image+0x577864&&afterModelVtable!=image+0x54E6D4)||
               !Read(model+NativeWeaponTypeKL::CategoryOffset,afterCategory,read)||afterCategory!=next.category||
               !Read(item+0x58,afterReference,read)||afterReference!=next.reference||
               (legacyThrow&&(!Read(action.action,afterVtable,read)||afterVtable!=action.actionVtable||
                !Read(action.action+8,actor,read)||actor!=player||!Read(action.action+0x24,launched,read)||launched)))return false;
            output=next;return true;
        }
    }
    return false;
}
}
