#pragma once
#include "native_action_kl.hpp"
#include "stereo_render_math.hpp"

namespace NativeVehicleCamera {
// Scripted seat entry/exit is a MiniAct in the observed M03B vehicle mode.
// Remember only that exact owned action across its switch back to walking;
// unrelated upright mini scenes keep their established camera policy.
struct Scope {
    std::uintptr_t player=0,action=0;
    void Reset(){*this={};}
    bool Select(const NativeActionKL::State& state){
        using M=NativeActionKL::Movement;using A=NativeActionKL::Action;
        if(!state.valid||!state.player||!state.action){Reset();return false;}
        const bool upright=state.movementKind==M::Stand||state.movementKind==M::BaseMove;
        const bool vehicle=state.movementKind==M::Vehicle;
        const bool mini=state.actionKind==A::MiniAct||state.actionKind==A::MiniAnim;
        const bool eligible=(state.actionKind==A::Vehicle&&(upright||vehicle))||
            (mini&&(vehicle||(upright&&player==state.player&&action==state.action)));
        if(!eligible){Reset();return false;}
        player=state.player;action=state.action;return true;
    }
};
struct Tracking {
    std::uintptr_t player=0,camera=0,action=0;
    StereoRenderMath::CinematicLook look;
    void Reset(){*this={};}
    bool Select(NativeCameraMath::Quat head,std::uintptr_t owner,std::uintptr_t source,
                std::uintptr_t clip,long ack,NativeCameraMath::Quat& output){
        if(!owner||!source||!clip)return false;
        if(player!=owner||camera!=source||action!=clip){Reset();player=owner;camera=source;action=clip;}
        return look.SelectFree(head,source,ack,output);
    }
};
}
