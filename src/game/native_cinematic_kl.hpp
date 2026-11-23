#pragma once
#include <cstddef>
#include <cstdint>
#include "native_action_kl.hpp"
#include "native_pip_kl.hpp"

// Read-only native cinematic state in the hash-pinned K&L executable.
// Full cut-sequence cameras increment GameSystem->manager+0x300 in 0x558586
// and decrement it in 0x558879. MiniCutscene Start/End maintain 0xC8E8B0
// at 0x6F719E / 0x6F7260. Camera ownership alone is insufficient: the local
// ZCAMERA can be reused while scripted animation owns the scene.
namespace NativeCinematicKL {
// A MiniAct/MiniAnim can accompany a physical movement. That action class
// alone must not turn a climb/fall/seat into an external authored camera.
// Full cut counters are classified independently and retain priority.
inline bool PhysicalActorCamera(const NativeActionKL::State& action,std::uintptr_t localPlayer) {
    if(!localPlayer||!action.valid||action.player!=localPlayer)return false;
    using M=NativeActionKL::Movement;using A=NativeActionKL::Action;
    switch(action.movementKind) {
    case M::Guide:case M::Cover:case M::Roll:case M::Fall:case M::Climb:
    case M::Vehicle:case M::Mounted:case M::Rappel:case M::Die:return true;
    default:break;
    }
    if(action.action)switch(action.actionKind) {
    case A::CloseCombat:case A::Pickup:case A::Reload:case A::Throw:case A::Vehicle:case A::Heal:case A::Mounted:
    case A::MiniAct:case A::MiniAnim:return true;
    default:break;
    }
    return false;
}
// A local MiniAct/MiniAnim is an actor animation, not proof of an external
// camera. Keep physical interactions attached; full-cut ownership is checked
// separately. This helper retains the call-site contract for future classes.
inline bool ActorCameraAction(const NativeActionKL::State& action,std::uintptr_t localPlayer) {
    return localPlayer&&action.valid&&action.player==localPlayer&&action.action&&
        !PhysicalActorCamera(action,localPlayer)&&
        (action.actionKind==NativeActionKL::Action::MiniAct||action.actionKind==NativeActionKL::Action::MiniAnim);
}
inline bool ActorScenePanel(const NativeActionKL::State& action,std::uintptr_t localPlayer,bool allowVR) {
    return !allowVR&&ActorCameraAction(action,localPlayer);
}
struct State {
    bool valid=false;
    std::uint32_t full=0,mini=0;
    std::uintptr_t camera=0;
    // MiniCutscene also covers ordinary in-world dialogue and interactions;
    // it does not establish camera ownership and must not flatten VR.
    bool PreserveNative() const {return !valid||full!=0;}
    bool StereoCamera() const {return valid&&full&&camera;}
    bool PanelRequired() const {return !valid||(full&&!camera);}
};
inline bool FirstPersonSkip(const State& scene,const NativeActionKL::State& action,std::uintptr_t player){
    using A=NativeActionKL::Action;using M=NativeActionKL::Movement;
    return scene.valid&&scene.mini&&!scene.full&&player&&action.valid&&action.player==player&&action.action&&
        (action.actionKind==A::MiniAct||action.actionKind==A::MiniAnim)&&
        (action.movementKind==M::Stand||action.movementKind==M::BaseMove);
}
// This is a local, upright authored actor sequence, not a full cut camera or
// an arbitrary MiniAnim used for climbing, falling, a seat or a weapon pose.
// Only that narrow owner may turn the retained gameplay heading with its root.
inline bool AuthoredActorHeading(const State& scene,const NativeActionKL::State& action,std::uintptr_t player){
    return FirstPersonSkip(scene,action,player);
}
// A verified native shot is an opt-in VR camera. In cinema mode neither
// its view nor its culling/actor pose may inherit a tracked gameplay eye.
// The mini counter alone also covers normal dialogue and never flattens it.
inline bool FullScenePanel(const State& state,bool allowVR) {
    return state.PanelRequired()||(!allowVR&&state.PreserveNative());
}
inline bool PipCameraPass(const NativePipKL::State& pip,std::uintptr_t camera,std::uintptr_t geometry) {
    return pip.valid&&pip.active&&camera&&geometry&&camera==pip.camera&&geometry==pip.geometry;
}
inline bool CompositePipPanel(const State& cinematic,const NativePipKL::State& pip) {
    return cinematic.valid&&cinematic.full&&cinematic.camera&&pip.valid&&pip.active&&
        cinematic.camera==pip.camera;
}
inline bool Address(std::uintptr_t at,std::size_t bytes) {
    return at>=0x10000u&&bytes<=0xffffffffu&&at<=0xffffffffu-bytes;
}
template<class Reader> bool PauseMenu(std::uintptr_t image,Reader read) {
    std::uint8_t paused[2]{};
    return Address(image+0x8883A4,sizeof(paused))&&read(image+0x8883A4,paused,sizeof(paused))&&
        (paused[0]==1||paused[1]==1);
}
template<class Reader> State Snapshot(std::uintptr_t image,Reader read) {
    State state;
    std::uint32_t system=0,manager=0;
    const auto get=[&](std::uintptr_t at,std::uint32_t& out) {
        return Address(at,sizeof(out))&&read(at,&out,sizeof(out));
    };
    if(!get(image+0x6875F0,system)||!Address(system,0x970)||
       !get(system+0x96C,manager)||!Address(manager,0x304)||
       !get(manager+0x300,state.full)||state.full>32)return state;
    // Optional diagnostic only. Its absence/corruption must never disable
    // a valid first-person view whose full camera counter is idle.
    if(!get(image+0x88E8B0,state.mini)||state.mini>1024)state.mini=0;
    // Full cut-camera Start writes its controller to C7750C at 55858D;
    // controller+24 is passed to renderer SetCamera at 558541/55855E.
    // Authorize only that exact native camera, never another kind1 pass.
    if(state.full) {
        std::uint32_t controller=0,camera=0,vtable=0,geometry=0,owner=0,stillController=0,stillCamera=0;
        if(get(image+0x87750C,controller)&&Address(controller,0x28)&&get(controller+0x24,camera)&&
           Address(camera,8)&&get(camera,vtable)&&vtable==image+0x545A3C&&get(camera+4,geometry)&&
           Address(geometry,0x64)&&get(geometry+0x60,owner)&&owner==camera&&
           get(image+0x87750C,stillController)&&stillController==controller&&
           get(controller+0x24,stillCamera)&&stillCamera==camera)state.camera=camera;
    }
    state.valid=true;return state;
}
}
