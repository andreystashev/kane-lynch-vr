#pragma once
#include "hands_ipc.hpp"
#include "native_camera_math.hpp"

namespace HandsMath {
using namespace NativeCameraMath;
inline bool ValidPose(const HandsIPC::Pose& pose) {
    if((pose.flags&(HandsIPC::kOrientation|HandsIPC::kPosition))!=(HandsIPC::kOrientation|HandsIPC::kPosition))return false;
    const Quat q{pose.orientation[0],pose.orientation[1],pose.orientation[2],pose.orientation[3]};
    Quat normalized;
    return NormalizeQuaternion(q,normalized)&&Finite(Vec3{pose.position[0],pose.position[1],pose.position[2]})&&
        std::fabs(Dot(Vec3{q.x,q.y,q.z},Vec3{q.x,q.y,q.z})+q.w*q.w-1.f)<.1f;
}
inline bool ValidHeadReference(const HandsIPC::Pose& pose) {
    if(!(pose.flags&HandsIPC::kOrientation))return false;
    const Quat q{pose.orientation[0],pose.orientation[1],pose.orientation[2],pose.orientation[3]};
    Quat normalized;
    return NormalizeQuaternion(q,normalized)&&Finite(Vec3{pose.position[0],pose.position[1],pose.position[2]})&&
        std::fabs(Dot(Vec3{q.x,q.y,q.z},Vec3{q.x,q.y,q.z})+q.w*q.w-1.f)<.1f;
}
inline bool ValidFrame(const HandsIPC::Frame& frame,DWORD now,std::uint32_t recenterAck) {
    return frame.present&&frame.mounted&&HandsIPC::Fresh(now,frame.tick)&&
        frame.recenterAck==recenterAck&&ValidHeadReference(frame.head);
}
// Keep one sample (including a missing sample) for the complete camera pair.
// A new host/recenter publication between eyes must not move only one arm view.
inline HandsIPC::Frame PairedFrame(const HandsIPC::Frame& previous,bool samePair,
                                  const HandsIPC::Frame& latest,bool latestAvailable,
                                  DWORD now,std::uint32_t recenterAck) {
    if(samePair)return ValidFrame(previous,now,recenterAck)?previous:HandsIPC::Frame{};
    return latestAvailable&&ValidFrame(latest,now,recenterAck)?latest:HandsIPC::Frame{};
}
// Geometry local +X follows the fingers (-Z controller), thumb is toward
// controller +Y on BOTH hands; mirror only the local anatomy's lateral axes.
inline Vec3 ControllerLocal(Vec3 local,unsigned side) {
    const float sign=side==0?1.f:-1.f;
    return {sign*local.z,-sign*local.y,-local.x};
}
inline bool EyePoint(const HandsIPC::Pose& wrist,unsigned side,Vec3 local,
                     const float headOrientation[4],const float headPosition[3],
                     const float eyeOffset[3],Vec3& output) {
    if(side>1||!ValidPose(wrist)||!Finite(local)||!headOrientation||!headPosition||!eyeOffset)return false;
    Quat head,wristQ;
    if(!NormalizeQuaternion({headOrientation[0],headOrientation[1],headOrientation[2],headOrientation[3]},head)||
       !NormalizeQuaternion({wrist.orientation[0],wrist.orientation[1],wrist.orientation[2],wrist.orientation[3]},wristQ))return false;
    const Vec3 position{headPosition[0],headPosition[1],headPosition[2]},offset{eyeOffset[0],eyeOffset[1],eyeOffset[2]};
    if(!Finite(position)||!Finite(offset))return false;
    const Vec3 world=Add(Vec3{wrist.position[0],wrist.position[1],wrist.position[2]},Rotate(wristQ,ControllerLocal(local,side)));
    const Vec3 camera=Subtract(Rotate(Conjugate(head),Subtract(world,position)),offset);
    if(!Finite(camera))return false;
    output={camera.x,camera.y,-camera.z}; // D3D LH camera looks along +Z.
    return true;
}
} // namespace HandsMath
