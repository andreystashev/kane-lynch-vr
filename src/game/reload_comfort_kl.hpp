// Portable reload port from kane-lynch2-vr f12d21f (2026-11-16).
// Native KL1 integration is tracked as KL1-088; no game addresses are imported.
#pragma once
#include "physical_reload_kl.hpp"
namespace ReloadComfortKL {
using namespace NativeCameraMath;
inline bool Seat(const CameraPose& tip,const CameraPose& mouth,float units,unsigned category,float bodyLength=.13f){
    if(!ValidatePose(tip,.05f)||!ValidatePose(mouth,.05f)||!std::isfinite(units)||units<1||units>1000)return false;
    const auto d=Scale(Subtract(tip.position,mouth.position),1.f/units);
    const float along=Dot(d,mouth.basis.forward);const auto lateral=Subtract(d,Scale(mouth.basis.forward,along));
    // Long magazines have a remote feed tip. Also admit their visible upper
    // body beside the opening, then let the existing guide align the mesh.
    if(category==0)return std::fabs(along)<.085f&&Dot(lateral,lateral)<.105f*.105f&&Dot(tip.basis.forward,mouth.basis.forward)>-.3421f;
    if(category!=1&&std::isfinite(bodyLength)&&bodyLength>.005f&&bodyLength<.5f){
        const auto fromTip=Scale(Subtract(mouth.position,tip.position),1.f/units);
        const auto body=Scale(tip.basis.forward,-std::clamp(-Dot(fromTip,tip.basis.forward),0.f,bodyLength));
        const auto gap=Subtract(fromTip,body);
        // A deliberate near contact by the visible body is enough. The guide
        // owns final orientation; approach angle/velocity need not be exact.
        if(Dot(gap,gap)<.115f*.115f&&Dot(d,d)<.35f*.35f)return true;
    }
    return std::fabs(along)<.16f&&Dot(lateral,lateral)<.16f*.16f&&Dot(tip.basis.forward,mouth.basis.forward)>-.85f;
}
}
