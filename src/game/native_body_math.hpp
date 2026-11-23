#pragma once

#include "native_camera_math.hpp"
#include <algorithm>
#include <cstdint>

// Visual avatar root only. No gameplay transform or animated bone is changed.
namespace NativeBodyMath {
using namespace NativeCameraMath;
constexpr float Pi=3.14159265358979323846f;
constexpr float Radians(float degrees){return degrees*Pi/180.f;}

inline float Wrap(float angle){return std::atan2(std::sin(angle),std::cos(angle));}
// A render context ends explicitly at a camera/UI boundary. Wall-clock age
// is not authorization: a slow scene must not restore its native full body
// halfway through the same current GPU draw.
inline bool DrawContextCurrent(bool valid,std::uint64_t contextPair,std::uint64_t requestedPair) {
    return valid&&contextPair&&contextPair==requestedPair;
}
inline bool Heading(Vec3 forward,float& output) {
    if(!Finite(forward)||forward.x*forward.x+forward.z*forward.z<1e-6f)return false;
    output=std::atan2(forward.z,forward.x);return true;
}
inline Vec3 Transform(const Basis& basis,Vec3 point) {
    return Add(Add(Scale(basis.right,point.x),Scale(basis.up,point.y)),Scale(basis.forward,point.z));
}
inline Vec3 RotateHeading(Vec3 point,float radians) {
    const float cosine=std::cos(radians),sine=std::sin(radians);
    return {point.x*cosine-point.z*sine,point.y,point.x*sine+point.z*cosine};
}

// Follow the eyes horizontally while retaining the native ground height.
// Vertical head tracking belongs to the eyes/arm targets, never the feet:
// translating the complete root in Y makes the avatar float above the floor.
inline bool AlignHead(const CameraPose& root,Vec3 headLocal,Vec3 headCentre,float units,CameraPose& output) {
    if(!ValidatePose(root,.05f)||!Finite(headLocal)||!Finite(headCentre)||
       !std::isfinite(units)||units<1.f||units>1000.f)return false;
    const Vec3 current=Add(root.position,Transform(root.basis,headLocal));
    Vec3 offset=Subtract(headCentre,current);
    if(!Finite(offset)||Dot(offset,offset)>2.25f*units*units)return false;
    offset.y=0.f;
    CameraPose result=root;result.position=Add(result.position,offset);
    if(!ValidatePose(result,.05f))return false;
    output=result;return true;
}

struct FollowState {
    bool valid=false;
    std::uintptr_t player=0;
    std::uint64_t pairId=0;
    std::uint32_t tick=0;
    float yaw=0.f;
    float animationYaw=0.f,rawTorsoYaw=0.f;
    float bindYawOffset=0.f,actualYawOffset=0.f;
    bool bindValid=false;
    bool shoulderValid=false,shoulderMeasured=false;
};

// Named K&L shoulder bones establish anatomical RIGHT-minus-LEFT. Its cross
// product with world UP points FORWARD, independently of entity root.forward.
// In the pinned model LEFT is +X and the face/toes point -Z: entity forward
// must not silently replace this anatomical direction.
inline bool ShoulderHeading(const CameraPose& root,Vec3 rightMinusLeft,float& actualYaw) {
    if(!ValidatePose(root,.05f)||!Finite(rightMinusLeft))return false;
    const Vec3 worldRight=Transform(root.basis,rightMinusLeft);
    const float length2=Dot(worldRight,worldRight);
    const float horizontal2=worldRight.x*worldRight.x+worldRight.z*worldRight.z;
    // A nearly vertical line has unstable azimuth even when its absolute
    // projection is nonzero. Require at least 20% of its length horizontally.
    if(!std::isfinite(length2)||length2<1e-8f||horizontal2<length2*.04f)return false;
    return Heading(Cross(worldRight,{0,1,0}),actualYaw);
}
inline bool TorsoHeading(const CameraPose& root,Vec3 rawShoulderLine,Vec3 bindShoulderLine,
                         float& actualYaw,float& animationYaw) {
    float rawYaw=0.f,bindYaw=0.f;
    if(!ShoulderHeading(root,rawShoulderLine,rawYaw)||!ShoulderHeading(root,bindShoulderLine,bindYaw))return false;
    actualYaw=rawYaw;animationYaw=Wrap(rawYaw-bindYaw);return true;
}

inline bool FacingView(std::uintptr_t player,const FollowState& state,const CameraPose& rawRoot,
                       Vec3 rawShoulderLine,Vec3 bindShoulderLine,const CameraPose& trackedEye,
                       CameraPose& output) {
    if(!player||!ValidatePose(rawRoot,.05f)||!ValidatePose(trackedEye,.05f))return false;
    const auto forward=trackedEye.basis.forward;
    if(forward.x*forward.x+forward.z*forward.z>=.05f*.05f) {output=trackedEye;return true;}
    float yaw=0.f;
    if(state.valid&&state.player==player&&std::isfinite(state.yaw))yaw=state.yaw;
    else if(!ShoulderHeading(rawRoot,rawShoulderLine,yaw)&&!ShoulderHeading(rawRoot,bindShoulderLine,yaw))return false;
    CameraPose next=trackedEye;const float cosine=std::cos(yaw),sine=std::sin(yaw);
    next.basis={{cosine,0,sine},{0,1,0},{sine,0,-cosine}};
    if(!ValidatePose(next,.05f))return false;
    output=next;return true;
}

// Cache the visual world heading once per stereo pair, then preserve each
// draw's original animated head pivot. The native skeleton keeps animating;
// the same pair never takes a second smoothing step for its other eye.
inline bool Adjust(FollowState& state,std::uintptr_t player,const CameraPose& rawRoot,
                   Vec3 rawHeadLocal,Vec3 rawShoulderLine,Vec3 bindShoulderLine,
                   const CameraPose& trackedEye,std::uint64_t pairId,
                   std::uint32_t tick,CameraPose& output,bool faceView=false) {
    float rawYaw=0.f,targetYaw=0.f;
    if(!player||!pairId||!ValidatePose(rawRoot,.05f)||!ValidatePose(trackedEye,.05f)||
       !Finite(rawHeadLocal)||!Finite(rawShoulderLine)||!Finite(bindShoulderLine)||
       !Heading(rawRoot.basis.forward,rawYaw)||
       !Heading(trackedEye.basis.forward,targetYaw))return false;
    const bool reset=!state.valid||state.player!=player;
    FollowState next=reset?FollowState{}:state;
    float bindYaw=0.f;
    if(ShoulderHeading(rawRoot,bindShoulderLine,bindYaw)) {
        next.bindYawOffset=Wrap(bindYaw-rawYaw);next.bindValid=true;
    }
    next.shoulderMeasured=ShoulderHeading(rawRoot,rawShoulderLine,next.rawTorsoYaw);
    if(next.shoulderMeasured) {
        next.shoulderValid=true;next.actualYawOffset=Wrap(next.rawTorsoYaw-rawYaw);
        if(next.bindValid)next.animationYaw=Wrap(next.actualYawOffset-next.bindYawOffset);
    } else if(next.bindValid) {
        next.rawTorsoYaw=Wrap(rawYaw+next.bindYawOffset+next.animationYaw);
    } else if(next.shoulderValid) {
        next.rawTorsoYaw=Wrap(rawYaw+next.actualYawOffset);
    } else {
        return false;
    }
    if(!std::isfinite(next.animationYaw)||!std::isfinite(next.rawTorsoYaw)||!std::isfinite(next.bindYawOffset))return false;
    if(reset) {
        next.valid=true;next.player=player;next.pairId=pairId;next.tick=tick;next.yaw=faceView?targetYaw:next.rawTorsoYaw;
    } else if(next.pairId!=pairId) {
        if(!std::isfinite(next.yaw))return false;
        const float elapsed=std::min(static_cast<float>(tick-next.tick)*.001f,.1f);
        const float error=Wrap(targetYaw-next.yaw);
        constexpr float deadzone=Radians(35.f);
        if(faceView)next.yaw=targetYaw;
        else if(std::fabs(error)>deadzone) {
            const float rate=Radians(std::fabs(error)>Radians(90.f)?240.f:180.f);
            const float correction=error-std::copysign(deadzone,error);
            next.yaw=Wrap(next.yaw+std::clamp(correction,-rate*elapsed,rate*elapsed));
        }
        next.pairId=pairId;next.tick=tick;
    }
    if(!std::isfinite(next.yaw))return false;
    const float delta=Wrap(next.yaw-next.rawTorsoYaw);
    CameraPose result=rawRoot;
    if(delta!=0.f) {
        const Vec3 headWorld=Add(rawRoot.position,Transform(rawRoot.basis,rawHeadLocal));
        result.basis={RotateHeading(rawRoot.basis.forward,delta),RotateHeading(rawRoot.basis.up,delta),
                      RotateHeading(rawRoot.basis.right,delta)};
        result.position=Subtract(headWorld,Transform(result.basis,rawHeadLocal));
    }
    if(!ValidatePose(result,.05f))return false;
    output=result;state=next;return true;
}
}
