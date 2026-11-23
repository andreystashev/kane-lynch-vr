#pragma once
#include "native_weapon_kl.hpp"
#include <algorithm>
#include <cmath>

namespace NativeGrenadeKLMath {
using namespace NativeCameraMath;
constexpr std::uint32_t GazeMaxAgeMs=250;
inline bool Unit(Vec3 input,Vec3& output) {
    const float length2=Dot(input,input);
    if(!Finite(input)||!std::isfinite(length2)||length2<1e-8f)return false;
    output=Scale(input,1.f/std::sqrt(length2));return true;
}
inline bool ValidGaze(const NativeWeaponKL::HeadGaze& gaze,std::uint32_t now) {
    const float length2=Dot(gaze.direction,gaze.direction);
    return gaze.player>=0x10000&&gaze.sourceCamera>=0x10000&&gaze.tick&&now-gaze.tick<=GazeMaxAgeMs&&
        gaze.mounted&&std::isfinite(gaze.unitsPerMetre)&&gaze.unitsPerMetre>=1&&gaze.unitsPerMetre<=1000&&
        Finite(gaze.origin)&&Finite(gaze.direction)&&length2>.5f&&length2<1.5f;
}
// Ignore device roll for the native gravity/loft plane. At a vertical ray use
// the last useful horizontal heading supplied with the certificate.
inline bool AimBasis(Vec3 direction,Vec3 horizontalFallback,Basis& output) {
    Vec3 forward;if(!Unit(direction,forward))return false;
    Vec3 horizontal{forward.x,0,forward.z};
    if(Dot(horizontal,horizontal)<1e-6f)horizontal={horizontalFallback.x,0,horizontalFallback.z};
    if(!Unit(horizontal,horizontal))return false;
    Vec3 right=Cross({0,1,0},horizontal);
    right=Subtract(right,Scale(forward,Dot(right,forward)));
    if(!Unit(right,right))return false;
    const Basis next{forward,Cross(forward,right),right};
    if(!ValidateBasis(next,.05f))return false;output=next;return true;
}
// Native ZFFActionThrow supplies a world target, not an arbitrary new speed.
// Keep charge length and ballistic loft. The native target may contain
// a lateral correction for the third-person throw animation; folding
// that into range keeps the release on the physical pointing plane. The legacy
// HeadGaze carrier also accepts the current right-controller pointing ray;
// native actor heading never supplies the requested direction. Origin stays.
inline bool RemapTarget(Vec3 nativeOrigin,Vec3 nativeTarget,Vec3 nativeAim,
                        const NativeWeaponKL::HeadGaze& gaze,Vec3 horizontalHeading,
                        Vec3& output) {
    if(!Finite(nativeOrigin)||!Finite(nativeTarget)||!Finite(nativeAim)||
       !std::isfinite(gaze.unitsPerMetre)||gaze.unitsPerMetre<1||gaze.unitsPerMetre>1000)return false;
    const auto delta=Subtract(nativeTarget,nativeOrigin);
    const float length2=Dot(delta,delta),bodyDistance2=Dot(Subtract(nativeOrigin,gaze.origin),Subtract(nativeOrigin,gaze.origin));
    const float units2=gaze.unitsPerMetre*gaze.unitsPerMetre;
    if(!std::isfinite(length2)||length2<.02f*.02f*units2||length2>500.f*500.f*units2||
       !std::isfinite(bodyDistance2)||bodyDistance2>3.f*3.f*units2)return false;
    Basis oldBasis,newBasis;
    const Vec3 oldHorizontal{nativeAim.x,0,nativeAim.z};
    if(!AimBasis(nativeAim,oldHorizontal,oldBasis)||!AimBasis(gaze.direction,horizontalHeading,newBasis))return false;
    const Vec3 local{Dot(delta,oldBasis.right),Dot(delta,oldBasis.up),Dot(delta,oldBasis.forward)};
    const float range=std::copysign(std::hypot(local.x,local.z),local.z);
    const auto mapped=Add(Scale(newBasis.up,local.y),Scale(newBasis.forward,range));
    const auto next=Add(nativeOrigin,mapped);
    if(!Finite(next)||std::fabs(Dot(mapped,mapped)-length2)>std::max(.001f,length2*.0001f))return false;
    output=next;return true;
}
} // namespace NativeGrenadeKLMath
