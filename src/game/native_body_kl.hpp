#pragma once

#include "native_camera_math.hpp"
#include "native_action_kl.hpp"
#include <cstdint>

namespace NativeBodyKL {
// Only called by the actual GPU camera consumer. ClearFrame retires context
// after a scene/UI switch, retaining smoothing history for the next pair.
void SetFrame(const NativeCameraMath::CameraPose& trackedEye,std::uint64_t pairId,float units,
              bool faceView=true,std::uint32_t recenterAck=0);
void ClearFrame();
// GPU-thread-only: a native action can rotate the actor independently of
// tracked torso follow. Resume from its new native heading after the action.
void ResetFollow();
// Called only for the exact local-player GPU palette binder. rawRoot and the
// reconstructed original head/shoulders remain read-only; output is draw-local.
// Shoulder lines are right-minus-left positions in model space, before IK;
// bindShoulderLine uses the same bones in their immutable bind pose.
bool Adjust(std::uintptr_t player,const NativeCameraMath::CameraPose& rawRoot,
            NativeCameraMath::Vec3 rawHeadLocal,NativeCameraMath::Vec3 rawShoulderLine,
            NativeCameraMath::Vec3 bindShoulderLine,NativeCameraMath::CameraPose& correctedRoot,std::uint64_t pairId);
// These physical actions keep their native bone animation, while the VR
// avatar root faces and stays horizontally attached to the rendered eye.
// External authored scenes and constrained carrier/rappel rigs are excluded.
inline bool ActionFollowEligible(const NativeActionKL::State& action,std::uintptr_t player) {
    using M=NativeActionKL::Movement;using A=NativeActionKL::Action;
    if(!player||!action.valid||action.player!=player)return false;
    return action.BodyAlignedInteraction()||
        (action.movementKind==M::Fall&&!action.action)||
        (action.movementKind==M::Climb&&(!action.action||action.actionKind==A::MiniAct||action.actionKind==A::MiniAnim));
}
bool AdjustAction(std::uintptr_t player,const NativeActionKL::State& action,const NativeCameraMath::CameraPose& rawRoot,
                  NativeCameraMath::Vec3 rawHeadLocal,NativeCameraMath::Vec3 rawShoulderLine,
                  NativeCameraMath::Vec3 bindShoulderLine,NativeCameraMath::CameraPose& correctedRoot,std::uint64_t pairId);

struct Stats {
    std::uint64_t frames=0,adjusted=0,rotated=0,rejected=0;
    std::uint64_t shoulderMeasured=0,shoulderFallback=0;
    std::uint64_t actionAdjusted=0,actionRejected=0;
    std::uintptr_t player=0;
    std::uint64_t pairId=0;
    std::uint32_t threadId=0;
    float yawDegrees=0.f,correctionDegrees=0.f;
    float rawTorsoYawDegrees=0.f,animationYawDegrees=0.f,bindYawOffsetDegrees=0.f;
};
Stats Snapshot();
}
