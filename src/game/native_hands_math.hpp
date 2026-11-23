#pragma once
#include "native_camera_math.hpp"

namespace NativeHandsMath {
using NativeCameraMath::Basis;
using NativeCameraMath::CameraPose;
using NativeCameraMath::Quat;
using NativeCameraMath::Vec3;

// hands_ipc.hpp::RotationFromQuat stores device axes as ROWS. Read each
// complete row here; transposing these values reverses controller rotations.
inline bool ControllerTarget(const CameraPose& base,const Vec3& headWorld,
                             Quat reference,const float* rotation,
                             Vec3 handTracking,Vec3 headTracking,int hand,
                             float unitsPerMetre,float maximumReachMetres,
                             CameraPose& output) {
    using namespace NativeCameraMath;
    if(hand<0||hand>1||!rotation||!ValidatePose(base)||!Finite(headWorld)||
       !Finite(handTracking)||!Finite(headTracking)||
       !std::isfinite(unitsPerMetre)||unitsPerMetre<.001f||unitsPerMetre>100000.f||
       !std::isfinite(maximumReachMetres)||maximumReachMetres<=0.f||maximumReachMetres>2.f)return false;
    Quat normalized;
    if(!NormalizeQuaternion(reference,normalized))return false;
    const Quat inverse=Conjugate(normalized);
    const Vec3 right{rotation[0],rotation[1],rotation[2]};
    const Vec3 up{rotation[3],rotation[4],rotation[5]};
    const Vec3 back{rotation[6],rotation[7],rotation[8]};
    // This is a proper device-to-tracking matrix, in contrast to the
    // reflected native forward/up/right matrix below.
    if(!Finite(right)||!Finite(up)||!Finite(back)||
       std::fabs(Dot(right,right)-1.f)>.025f||std::fabs(Dot(up,up)-1.f)>.025f||
       std::fabs(Dot(back,back)-1.f)>.025f||std::fabs(Dot(right,up))>.025f||
       Dot(Cross(right,up),back)<.975f)return false;
    Vec3 relative=Subtract(handTracking,headTracking);
    const float distance=std::sqrt(Dot(relative,relative));
    if(!std::isfinite(distance))return false;
    if(distance>maximumReachMetres)relative=Scale(relative,maximumReachMetres/distance);
    const auto map=[&](Vec3 vector){return MapTracking(base.basis,Rotate(inverse,vector));};
    CameraPose result;
    result.position=Add(headWorld,Scale(map(relative),unitsPerMetre));
    // Glacier wrist-local +X is fingers, mirrored +/-Y is thumb, +Z palm.
    // Keep thumbs toward controller +Y and fingers toward its -Z ray. These
    // are full absolute wrist targets, never deltas from an animated wrist.
    const float sign=hand==0?1.f:-1.f;
    result.basis.forward=Scale(map(right),sign);
    result.basis.up=Scale(map(up),-sign);
    result.basis.right=Scale(map(back),-1.f);
    if(!ValidatePose(result))return false;
    output=result;
    return true;
}
} // namespace NativeHandsMath
