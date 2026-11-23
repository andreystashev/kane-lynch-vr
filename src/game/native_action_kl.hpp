#pragma once
#include <cstddef>
#include <cstdint>
#include "native_blast_action_kl.hpp"

// Native actor actions are distinct from cinematic camera ownership: retain
// the tracked head view while the game's current action owns arms/body.
namespace NativeActionKL {
enum class Movement { Unknown,Stand,BaseMove,Guide,Cover,Roll,Fall,Climb,Vehicle,Mounted,Rappel,Die };
enum class Action { None,CloseCombat,MiniAct,MiniAnim,Pickup,Reload,Throw,Vehicle,Heal,Mounted,Other,PutBack,GetBack,Command };
struct State {
    bool valid=false;
    std::uintptr_t player=0,movement=0,action=0;
    std::uint32_t movementVtable=0,actionVtable=0,movementId=0;
    Movement movementKind=Movement::Unknown;
    Action actionKind=Action::None;
    // Both grenade classes retain the public Throw classification. Keep the
    // verified image-relative class separately for the defensive throw-back.
    bool throwBack=false;
    bool explosionCover=false;
    bool BodyAlignedInteraction() const {
        return valid&&action&&(movementKind==Movement::Stand||movementKind==Movement::BaseMove)&&
            (actionKind==Action::Pickup||actionKind==Action::Heal||(actionKind==Action::Throw&&throwBack)||
             (actionKind==Action::MiniAct&&explosionCover));
    }
    bool PreserveRig() const {
        if(!valid)return false;
        if(action)return true;
        switch(movementKind) {
        case Movement::Guide:case Movement::Roll:case Movement::Fall:case Movement::Climb:
        case Movement::Vehicle:case Movement::Mounted:case Movement::Rappel:case Movement::Die:return true;
        default:return false;
        }
    }
    // Vehicle seating still owns the body root. With no authored action,
    // a passenger's separately held firearm may follow tracked controllers.
    // Mounted weapons retain their native anchored gun/animation path.
    bool PassengerWeapon() const {return valid&&movementKind==Movement::Vehicle&&!action;}
    // Reload still owns ammunition/action timing, but its authored hand pose
    // must not replace tracked wrists or detach an existing foregrip. Keep
    // physical traversal/mounted interactions on their native rig.
    bool TrackedReload() const {
        return valid&&action&&actionKind==Action::Reload&&
            (movementKind==Movement::Stand||movementKind==Movement::BaseMove||
             movementKind==Movement::Cover||movementKind==Movement::Vehicle);
    }
    // Exact weapon put-back/get-back and squad pointing gestures only own
    // the authored pose. Their original item attach/detach and command
    // callbacks still run; no inactive-hand exception is granted here.
    bool TrackedWeaponGesture() const {
        return valid&&action&&(actionKind==Action::PutBack||actionKind==Action::GetBack||actionKind==Action::Command)&&
            (movementKind==Movement::Stand||movementKind==Movement::BaseMove||
             movementKind==Movement::Cover||movementKind==Movement::Vehicle);
    }
    bool WeaponTrackingAllowed() const {return !PreserveRig()||PassengerWeapon()||TrackedReload()||TrackedWeaponGesture();}
    bool NativeMovementAxes() const {return valid&&(movementKind==Movement::Rappel||movementKind==Movement::Climb);}
    // These interactions require current animated anatomical height. Some
    // additionally align their draw-local root under a stable eye; traversal
    // and collapse retain the full native anatomical attachment instead.
    bool FollowAnimatedHead() const {
        return valid&&(movementKind==Movement::Rappel||movementKind==Movement::Climb||
            movementKind==Movement::Fall||movementKind==Movement::Die||movementKind==Movement::Vehicle||
            movementKind==Movement::Mounted||BodyAlignedInteraction()||(action&&(actionKind==Action::Vehicle||actionKind==Action::Pickup||actionKind==Action::CloseCombat||
            actionKind==Action::MiniAct||actionKind==Action::MiniAnim)));
    }
    bool FollowNativeCameraTilt() const {return valid&&movementKind==Movement::Rappel;}
};
inline bool Address(std::uintptr_t at,std::size_t size) {
    return at>=0x10000u&&size<=0xffffffffu&&at<=0xffffffffu-size;
}
inline Movement ClassifyMovement(std::uint32_t rva) {
    switch(rva) {
    case 0x566A44:return Movement::Stand;case 0x566AEC:return Movement::BaseMove;
    case 0x5671FC:return Movement::Guide;case 0x566EAC:return Movement::Cover;
    case 0x566B9C:return Movement::Roll;case 0x567054:return Movement::Fall;
    case 0x566C44:return Movement::Climb;case 0x566D14:return Movement::Vehicle;
    case 0x566F54:return Movement::Mounted;case 0x567144:return Movement::Rappel;
    case 0x566DBC:return Movement::Die;default:return Movement::Unknown;
    }
}
inline Action ClassifyAction(std::uint32_t rva) {
    switch(rva) {
    case 0x5669CC:return Action::CloseCombat;case 0x5667F4:return Action::MiniAct;
    case 0x566834:return Action::MiniAnim;case 0x566628:return Action::Pickup;
    case 0x566944:return Action::Reload;case 0x56665C:case 0x5668DC:return Action::Throw;
    case 0x56672C:case 0x566760:return Action::Vehicle;case 0x566998:return Action::Heal;
    case 0x566874:case 0x5668A8:return Action::Mounted;
    case 0x566690:return Action::PutBack;case 0x5666C4:return Action::GetBack;
    case 0x5666F8:return Action::Command;default:return Action::Other;
    }
}
template<class Reader> State Snapshot(std::uintptr_t image,Reader read,std::uint32_t tick=0) {
    State state;std::uint32_t data=0,player=0,playerVtable=0,movement=0,owner=0,registered=0;
    std::uint32_t dispatcher=0,dispatcherVtable=0,dispatcherOwner=0,action=0;
    const auto get=[&](std::uintptr_t at,std::uint32_t& output) {
        return Address(at,sizeof(output))&&read(at,&output,sizeof(output));
    };
    // 0x537BD0 is the native current-movement getter; 0x537B60 registers the
    // movement by its +0x44 ID in player+0x5F8[id]. +0x38 owns the player.
    if(!get(image+0x68760C,data)||!get(data+0xA2C,player)||
       !get(player,playerVtable)||playerVtable!=image+0x56582C||
       !get(player+0x67C,movement)||!get(movement,state.movementVtable)||
       !get(movement+0x38,owner)||owner!=player||!get(movement+0x44,state.movementId)||
       state.movementId>=32||!get(player+0x5F8+state.movementId*4,registered)||registered!=movement||
       !get(player+0x194,dispatcher)||!get(dispatcher,dispatcherVtable)||dispatcherVtable!=image+0x54DE4C||
       !get(dispatcher+8,dispatcherOwner)||dispatcherOwner!=player||!get(dispatcher+4,action))return state;
    state.player=player;state.movement=movement;state.action=action;
    state.movementKind=ClassifyMovement(static_cast<std::uint32_t>(state.movementVtable-image));
    // Dispatcher sets its current +4 pointer in 0x51C2CE and clears it in
    // 0x51C3BB. Its exact owned active action is enough to preserve animation;
    // the vtable classification is diagnostic, never an ownership shortcut.
    if(action) {
        get(action,state.actionVtable);
        state.actionKind=ClassifyAction(static_cast<std::uint32_t>(state.actionVtable-image));
        state.throwBack=state.actionVtable==image+0x5668DC;
        if(state.actionKind==Action::MiniAct&&(state.movementKind==Movement::Stand||state.movementKind==Movement::BaseMove))
            state.explosionCover=NativeBlastActionKL::Matches(image,player,dispatcher,action,read,tick);
    }
    state.valid=true;return state;
}
bool InitializeVerifiedImage(std::uintptr_t image);
State Current();
bool PreserveRig();
bool UsesNativeMovementAxes();
}
