#pragma once
#include "native_action_kl.hpp"
#include "native_camera_math.hpp"

namespace NativeVisualPolicy {
struct ActionPolicy {bool nativeArms=false,nativeBody=false;};
inline ActionPolicy ForAction(const NativeActionKL::State& action,std::uintptr_t player) {
    if(!player||!action.valid||action.player!=player)return {};
    ActionPolicy result;
    const bool native=action.PreserveRig();
    // Manual/empty reload keeps its original hand animation. Only the
    // automatic request policy is changed, not the authored reload pose.
    result.nativeArms=native&&!action.PassengerWeapon()&&!action.TrackedWeaponGesture();
    result.nativeBody=(native&&!action.TrackedReload()&&!action.TrackedWeaponGesture())||
        action.movementKind==NativeActionKL::Movement::Cover||
        action.movementKind==NativeActionKL::Movement::Vehicle||action.movementKind==NativeActionKL::Movement::Mounted;
    return result;
}
inline bool HideHead(bool firstPerson,bool enabled,NativeCameraMath::Vec3 rawHeadWorld,
                     NativeCameraMath::Vec3 eyeCentre,float units) {
    using namespace NativeCameraMath;
    if(!firstPerson||!enabled||!Finite(rawHeadWorld)||!Finite(eyeCentre)||
       !std::isfinite(units)||units<1.f||units>1000.f)return false;
    const auto distance=Subtract(rawHeadWorld,eyeCentre);
    // Covers a normal head-local lean and IPD while retaining the head if a
    // native scripted camera has moved visibly away from this body.
    return Dot(distance,distance)<=.65f*.65f*units*units;
}
}
