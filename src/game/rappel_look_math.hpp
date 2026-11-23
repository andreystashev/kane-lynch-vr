#pragma once
#include "native_camera_math.hpp"
#include "native_rig_math.hpp"
#include "native_skin_math.hpp"
#include <algorithm>
#include <cstdint>

namespace RappelLookMath {
using namespace NativeCameraMath;
constexpr float Pi=3.14159265358979323846f;
constexpr float YawLimit=75.f*Pi/180.f,PitchLimit=55.f*Pi/180.f;
// Derived only from the current native torso geometry, never from animated
// head gaze. 0 is unavailable, 1 is the entrance/standing pose, 2 is an
// anatomically horizontal torso (within about20 degrees of horizontal).
inline unsigned BodyPhase(const CameraPose& root,const NativeRigMath::Bone& pelvis,
                          const NativeRigMath::Bone& neck,float units,Vec3* towardHead=nullptr) {
    if(!ValidatePose(root,.05f)||!ValidatePose({pelvis.basis,pelvis.position},.05f)||
       !ValidatePose({neck.basis,neck.position},.05f)||!std::isfinite(units)||units<1.f||units>1000.f)return 0;
    const auto delta=Subtract(neck.position,pelvis.position);const float length2=Dot(delta,delta);
    if(!std::isfinite(length2)||length2<.20f*.20f*units*units||length2>2.5f*2.5f*units*units)return 0;
    const auto world=NativeRigMath::Transform(root.basis,delta);
    if(towardHead)*towardHead=Scale(world,1.f/std::sqrt(length2));
    return std::fabs(world.y)/std::sqrt(length2)<=.35f?2u:1u;
}
// A face-up view has no gravity-defined screen top. The original torso's
// pelvis -> neck axis defines head/city above the screen and feet below it.
// Keep the already accepted view direction; fix only its roll around that
// direction, using the current native anatomy once at horizontal settlement.
inline bool AnatomicalScreen(const Basis& view,Vec3 towardHead,Basis& output) {
    if(!ValidateBasis(view)||!Finite(towardHead))return false;
    Vec3 up,right;
    if(!NativeArmMath::Unit(Subtract(towardHead,Scale(view.forward,Dot(towardHead,view.forward))),up)||
       !NativeArmMath::Unit(Cross(up,view.forward),right))return false;
    const Basis result{view.forward,Cross(view.forward,right),right};
    if(!ValidateBasis(result))return false;
    output=result;return true;
}
// The entry animation can face a different wall heading from its eventual
// horizontal body. Neck gaze is not a torso reference either. Original
// shoulder positions and pelvis->neck establish the actual anatomical plane
// once the body is horizontal: right x up points out of the character's face.
inline bool TorsoScreen(const CameraPose& root,const NativeRigMath::Bone& pelvis,
                        const NativeRigMath::Bone& neck,const NativeRigMath::Bone& left,
                        const NativeRigMath::Bone& right,float units,Basis& output) {
    Vec3 towardHead;
    if(BodyPhase(root,pelvis,neck,units,&towardHead)!=2||
       !ValidatePose({left.basis,left.position},.05f)||!ValidatePose({right.basis,right.position},.05f))return false;
    const auto line=NativeRigMath::Transform(root.basis,Subtract(right.position,left.position));
    const float length2=Dot(line,line);
    if(!std::isfinite(length2)||length2<.1f*.1f*units*units||length2>1.2f*1.2f*units*units)return false;
    Vec3 forward,up;
    if(!NativeArmMath::Unit(Cross(line,towardHead),forward)||
       // Rappel is face-up; do not turn a missing/reversed shoulder mapping
       // into a new downward-looking reference.
       forward.y<.35f||!NativeArmMath::Unit(Subtract(towardHead,Scale(forward,Dot(towardHead,forward))),up))return false;
    const Basis result{forward,up,Cross(up,forward)};
    if(!ValidateBasis(result,.05f))return false;
    output=result;return true;
}
// An actor-root latch freezes the ENTRY animation: that root does not include
// the torso rotating from standing into the horizontal rappel pose. Follow
// the current parent of the head instead, with the head's bind-relative eye.
// This removes animated head-local nod/yaw without losing the authored body
// transition, descent or the anatomical eye height.
inline bool ParentHead(const CameraPose& root,const CameraPose& bindParent,
                       const CameraPose& bindHead,const CameraPose& bindEye,
                       const NativeRigMath::Bone& parent,float units,CameraPose& output) {
    if(!ValidatePose(root,.05f)||!ValidatePose(bindParent,.05f)||!ValidatePose(bindHead,.05f)||
       !ValidatePose(bindEye,.05f)||!ValidatePose({parent.basis,parent.position},.05f)||
       !std::isfinite(units)||units<1.f||units>1000.f)return false;
    const auto relative=NativeRigMath::ToModel(bindParent,bindHead);
    // A costume's neck/head relation must be anatomical, not an unrelated
    // model root or a recycled palette bone.
    if(Dot(relative.position,relative.position)>.45f*.45f*units*units)return false;
    const auto selected=NativeRigMath::ToWorld({parent.basis,parent.position},{relative.basis,relative.position});
    const NativeRigMath::Bone head{selected.basis,selected.position};
    Vec3 eye,up,forward;
    if(!NativeSkinMath::AnatomicalEye(bindHead,bindEye,head,units,eye)||
       !NativeSkinMath::EyeAnchor(root,eye,true,eye)||
       !NativeSkinMath::AnimatedHeadUp(root,bindHead,head,up)||
       !NativeSkinMath::AnimatedHeadForward(root,bindHead,head,forward))return false;
    const CameraPose next{{forward,up,Cross(up,forward)},eye};
    if(!ValidatePose(next,.05f))return false;
    output=next;return true;
}
// Select a proven parent-anchored head when available. Missing parent data
// keeps the CURRENT anatomical eye instead of freezing an entry-animation
// offset in the actor root (which can put the eye inside the wall).
struct Attachment {
    bool valid=false,basisValid=false,orientationValid=false,orientationLocked=false,phaseKnown=false,anatomicalUpLocked=false;
    std::uintptr_t player=0,camera=0,movement=0;
    long ack=0;
    CameraPose local{};
    Vec3 referenceRight{},referenceForward{};
    Basis anatomicalBasis{};
    Basis entranceBasis{};
    float upwardPitch=0.f;
    void Reset(){*this={};}
    bool Select(const CameraPose& root,Vec3 eye,const Basis* head,
                std::uintptr_t owner,std::uintptr_t source,std::uintptr_t action,
                long recenter,CameraPose& output,bool& outputBasis,
                const CameraPose* authoredHead=nullptr,unsigned bodyPhase=0,const Vec3* towardHead=nullptr) {
        if(!owner||!source||!action||!ValidatePose(root)||!Finite(eye)||
           (head&&!ValidateBasis(*head))||(authoredHead&&!ValidatePose(*authoredHead,.05f))||bodyPhase>2)return false;
        Attachment next=*this;
        if(!next.valid||next.player!=owner||next.camera!=source||next.movement!=action)next={};
        const CameraPose native{head?*head:root.basis,eye};
        auto selected=authoredHead?*authoredHead:native;
        if(!ValidatePose(selected))return false;
        const Basis currentAnatomy=selected.basis;
        const bool observedBasis=authoredHead||head;
        if(observedBasis) {
            const auto horizontal=[](Vec3 axis){axis.y=0.f;return axis;};
            if(!next.orientationValid) {
                Vec3 right;
                // The forward projection degenerates when the character is
                // face-up. Its right axis still defines the wall heading.
                if(!NativeArmMath::Unit(horizontal(selected.basis.right),right)&&
                   !NativeArmMath::Unit(horizontal(root.basis.right),right)&&
                   !NativeArmMath::Unit(Cross({0,1,0},horizontal(root.basis.forward)),right))return false;
                next.referenceRight=right;next.referenceForward=Cross(right,{0,1,0});
                next.upwardPitch=std::asin(std::clamp(selected.basis.forward.y,-1.f,1.f));
                next.orientationValid=true;
            }
            if(bodyPhase&&!next.phaseKnown) {
                // A first CPU-only fallback may precede the first proven
                // GPU torso sample. It must not freeze a standing entrance.
                next.phaseKnown=true;next.orientationLocked=false;
                next.upwardPitch=std::asin(std::clamp(selected.basis.forward.y,-1.f,1.f));
            }
            if(!next.orientationLocked) {
                // Until settlement the current parent-head follows the
                // character tying the rope, turning and leaning back. Keeping
                // the entrance azimuth here caused a second abrupt turn when
                // the final anatomical frame was installed.
                if(bodyPhase){next.entranceBasis=currentAnatomy;next.upwardPitch=std::asin(std::clamp(selected.basis.forward.y,-1.f,1.f));}
                if(bodyPhase==2||!next.phaseKnown)next.orientationLocked=true;
            }
        }
        if(next.orientationValid) {
            const auto forward=Add(Scale(next.referenceForward,std::cos(next.upwardPitch)),Vec3{0,std::sin(next.upwardPitch),0});
            selected.basis={forward,Cross(forward,next.referenceRight),next.referenceRight};
            if(next.phaseKnown)selected.basis=next.entranceBasis;
            if(bodyPhase==2&&towardHead&&!next.anatomicalUpLocked) {
                Basis anatomical;
                // At settlement use the CURRENT anatomical body reference,
                // not the early entry right/pitch rebuilt above. Otherwise a
                // 90-degree entrance turn can be locked as sideways forever.
                if(AnatomicalScreen(currentAnatomy,*towardHead,anatomical)) {
                    next.anatomicalBasis=anatomical;next.anatomicalUpLocked=true;
                    next.upwardPitch=std::asin(std::clamp(anatomical.forward.y,-1.f,1.f));
                }
            }
            if(next.anatomicalUpLocked)selected.basis=next.anatomicalBasis;
        }
        if(!ValidatePose(selected))return false;
        next.valid=true;next.basisValid=next.orientationValid;
        next.player=owner;next.camera=source;next.movement=action;next.ack=recenter;
        next.local=NativeRigMath::ToModel(root,selected);
        *this=next;output=selected;outputBasis=next.basisValid;return true;
    }
};
struct Look {
    bool valid=false,haveYaw=false;
    std::uintptr_t player=0,camera=0,movement=0;
    long ack=0;
    Quat origin{};
    float previousYaw=0,lastYaw=0,lastPitch=0;
    double continuousYaw=0;
    void Reset(){*this={};}
    bool Select(Quat physical,std::uintptr_t owner,std::uintptr_t source,
                std::uintptr_t action,long recenter,Quat& output) {
        Quat normalized;
        if(!owner||!source||!action||!NormalizeQuaternion(physical,normalized))return false;
        Look next=*this;
        if(!next.valid||next.player!=owner||next.camera!=source||next.movement!=action||next.ack!=recenter) {
            next={};next.valid=true;next.player=owner;next.camera=source;next.movement=action;
            next.ack=recenter;next.origin=normalized;
        }
        Quat relative;
        if(!NormalizeQuaternion(Multiply(Conjugate(next.origin),normalized),relative))return false;
        const auto forward=Rotate(relative,{0,0,-1});
        // Unwrap before clamping: atan2 alone jumps from +75 to -75 when
        // the physical headset crosses 180 degrees. At a vertical pole yaw
        // is undefined, so retain the last observable heading.
        if(forward.x*forward.x+forward.z*forward.z>1e-5f) {
            const float yaw=std::atan2(forward.x,-forward.z);
            if(next.haveYaw)next.continuousYaw+=std::remainder(yaw-next.previousYaw,2.f*Pi);
            else {next.continuousYaw=yaw;next.haveYaw=true;}
            next.previousYaw=yaw;
        }
        next.lastYaw=static_cast<float>(std::clamp(next.continuousYaw,-double(YawLimit),double(YawLimit)));
        next.lastPitch=std::clamp(std::asin(std::clamp(forward.y,-1.f,1.f)),-PitchLimit,PitchLimit);
        const Quat yaw{0,-std::sin(next.lastYaw*.5f),0,std::cos(next.lastYaw*.5f)};
        const Quat pitch{std::sin(next.lastPitch*.5f),0,0,std::cos(next.lastPitch*.5f)};
        Quat selected;
        if(!NormalizeQuaternion(Multiply(yaw,pitch),selected))return false;
        *this=next;output=selected;return true;
    }
};
}
