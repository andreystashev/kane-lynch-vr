#pragma once
#include "native_rig_math.hpp"
#include "native_weapon_type_kl.hpp"
#include <cstdint>
#include <cstring>

namespace NativeTwoHandMath {
using namespace NativeCameraMath;
struct Key {
    std::uintptr_t player=0,bodyModel=0,weapon=0,root=0,model=0,marker=0,markerGeometry=0;
    std::uint32_t reference=0,recenter=0;
};
inline bool Same(const Key& a,const Key& b) {
    return a.player==b.player&&a.bodyModel==b.bodyModel&&a.weapon==b.weapon&&a.root==b.root&&
        a.model==b.model&&a.marker==b.marker&&a.markerGeometry==b.markerGeometry&&
        a.reference==b.reference&&a.recenter==b.recenter;
}
inline bool LongGun(std::uint32_t category) {
    // Pinned native WEAPONTYPE metadata: SMG, MG, rifle, pump/shotgun, sniper.
    return (category>=2&&category<=6)||category==15;
}
template<class Reader> bool VerifyTypeImage(std::uintptr_t image,Reader read) {
    if(!NativeWeaponTypeKL::Verify(image,read))return false;
    const struct {std::uint32_t node,name,value;const char* text;} entries[]={
        {0x65E354,0x584E14,2,"WT_SUBMACHINEGUN"},{0x65E360,0x584E04,3,"WT_MACHINEGUN"},
        {0x65E36C,0x584DF8,4,"WT_RIFLE"},{0x65E378,0x584DEC,5,"WT_PUMPGUN"},
        {0x65E384,0x584DE0,6,"WT_SHOTGUN"},{0x65E3F0,0x584D5C,15,"WT_SNIPERRIFLE"}};
    for(const auto& entry:entries) {
        std::uint32_t value=0,name=0;char text[24]{};
        if(!read(image+entry.node+4,&value,sizeof(value))||value!=entry.value||
           !read(image+entry.node+8,&name,sizeof(name))||name!=image+entry.name||
           !read(name,text,std::strlen(entry.text)+1)||std::strcmp(text,entry.text))return false;
    }
    return true;
}
// The supporting native wrist and muzzle belong to the same original GPU
// draw. Require a separated contact in front of the grip and behind the real
// muzzle; a hanging/idle left hand cannot create a synthetic foregrip.
inline bool AuthoredSocket(const CameraPose& right,const CameraPose& left,Vec3 muzzle,
                           float units,CameraPose& output) {
    if(!ValidatePose(right,.05f)||!ValidatePose(left,.05f)||!Finite(muzzle)||
       !std::isfinite(units)||units<1||units>1000)return false;
    const auto contact=Subtract(left.position,right.position),barrel=Subtract(muzzle,right.position);
    const float length=NativeArmMath::Length(contact),barrelLength=NativeArmMath::Length(barrel);
    if(length<.16f*units||length>.75f*units||barrelLength<.20f*units||barrelLength>1.5f*units)return false;
    const auto axis=Scale(barrel,1.f/barrelLength);
    const float forward=Dot(contact,axis);
    if(forward<.08f*units||forward>barrelLength-.025f*units||
       NativeArmMath::Length(Subtract(contact,Scale(axis,forward)))>.18f*units)return false;
    const auto socket=NativeRigMath::ToModel(right,left);
    if(!ValidatePose(socket,.05f))return false;
    output=socket;return true;
}
inline void SeparateRifleSocket(CameraPose& socket,Vec3 muzzle,Vec3 bolt,Vec3 axis,float units,float separation=.14f,float maximumShift=.12f){
    const float ahead=Dot(Subtract(socket.position,bolt),axis);
    const float muzzleRoom=Dot(Subtract(muzzle,socket.position),axis)-.10f*units;
    const float shift=std::min({separation*units-ahead,maximumShift*units,muzzleRoom});
    if(shift>0.f)socket.position=Add(socket.position,Scale(axis,shift));
}
enum class ReleaseReason {None,Input,Context,Identity,InvalidInput};
struct State {
    Key key{};
    CameraPose socket{},pairRight{},pairLeft{};
    Quat steeringCorrection{};
    CameraPose pairFrame{};bool pairFrameValid=false;
    std::uint64_t pair=0;
    std::uint32_t tick=0,lastAimTick=0;
    std::uint32_t lowGripStart=0,lowGripTick=0,lowGripSamples=0;
    float units=0;
    bool valid=false,pressed=false,held=false,pairValid=false,pairHeld=false,requireRelease=false,aimValid=false;
    ReleaseReason releaseReason=ReleaseReason::None;
    // Per-update diagnostics; cached eye/draw reuse produces no new event.
    bool acquired=false,released=false,aimRecovered=false,aimRejected=false,suspended=false;
};
struct Reach {
    Vec3 shoulder{};
    float minimum=0,maximum=0;
};
// Contact belongs to the rendered wrist, not an unreachable raw controller.
// Use the same analytic solver, original joints and elbow pole as SolveArm;
// only project translation, retaining the tracked controller's orientation.
inline bool RenderedWrist(const CameraPose& root,const NativeArmMath::Joints& joints,
                          const CameraPose& physical,Vec3 pole,CameraPose& output) {
    if(!ValidatePose(root,.05f)||!ValidatePose(physical,.05f))return false;
    const auto target=NativeRigMath::ToModel(root,physical);
    NativeArmMath::Joints solved;
    if(!NativeArmMath::Solve(joints,target.position,pole,solved))return false;
    CameraPose next=physical;
    next.position=Add(root.position,NativeRigMath::Transform(root.basis,solved.wrist));
    if(!ValidatePose(next,.05f))return false;
    output=next;return true;
}
// Acquisition follows the visible weapon, but its lever follows the actual
// controller-to-controller vector. Separately clamping both wrists against
// different shoulders must never bend the user's intended aiming direction.
inline bool SteeringController(const CameraPose& renderedPrimary,const CameraPose& physicalPrimary,
                               const CameraPose& physicalSupport,CameraPose& output) {
    if(!ValidatePose(renderedPrimary,.05f)||!ValidatePose(physicalPrimary,.05f)||
       !ValidatePose(physicalSupport,.05f))return false;
    CameraPose next=physicalSupport;
    next.position=Add(renderedPrimary.position,Subtract(physicalSupport.position,physicalPrimary.position));
    if(!ValidatePose(next,.05f))return false;
    output=next;return true;
}
// A fore-end is a short grip region, not a single exact animated wrist.
// Contact is still limited to the verified held long gun and a squeezed grip.
inline bool ContactNear(const CameraPose& primary,const CameraPose& socket,const CameraPose& controller,float units) {
    if(!std::isfinite(units)||units<1||units>1000||!ValidatePose(primary,.05f)||
       !ValidatePose(socket,.05f)||!ValidatePose(controller,.05f))return false;
    const auto offset=NativeRigMath::Transform(primary.basis,socket.position);
    Vec3 axis;if(!NativeArmMath::Unit(offset,axis))return false;
    const auto delta=Subtract(controller.position,Add(primary.position,offset));
    const auto nearest=Scale(axis,std::clamp(Dot(delta,axis),-.06f*units,.06f*units));
    return NativeArmMath::Length(Subtract(delta,nearest))<=.18f*units;
}
// The two grips remain a rigid pair. Move that pair slightly toward the
// native shoulders when a controller is just outside anatomical reach;
// never stretch a segment or independently detach the supporting wrist.
inline bool ArmReach(const NativeArmMath::Joints& arm,Reach& output) {
    if(!Finite(arm.shoulder)||!Finite(arm.elbow)||!Finite(arm.wrist))return false;
    const float a=NativeArmMath::Length(Subtract(arm.elbow,arm.shoulder));
    const float b=NativeArmMath::Length(Subtract(arm.wrist,arm.elbow)),sum=a+b;
    if(!std::isfinite(sum)||a<1e-4f||b<1e-4f||sum>100000.f)return false;
    const float margin=std::min(std::min(a,b)*.01f,std::max(sum*1e-5f,1e-6f));
    const float safeMargin=std::max(margin*4.f,sum*1e-4f);
    output={arm.shoulder,std::fabs(a-b)+safeMargin,sum-safeMargin};return true;
}
inline bool FitReach(CameraPose& right,CameraPose& left,const Reach& rightArm,const Reach& leftArm,
                     float units) {
    if(!ValidatePose(right,.05f)||!ValidatePose(left,.05f)||!std::isfinite(units)||units<1||units>1000)return false;
    const Reach arms[]={rightArm,leftArm};
    for(const auto& arm:arms)if(!Finite(arm.shoulder)||!std::isfinite(arm.minimum)||!std::isfinite(arm.maximum)||
        arm.minimum<0||arm.maximum<=arm.minimum)return false;
    const Vec3 start=right.position,offset=Subtract(left.position,right.position);
    const Vec3 centres[]={rightArm.shoulder,Subtract(leftArm.shoulder,offset)};
    Vec3 point=start;
    for(unsigned pass=0;pass<32;++pass) {
        bool inside=true;
        for(unsigned side=0;side<2;++side) {
            const auto delta=Subtract(point,centres[side]);const float length=NativeArmMath::Length(delta);
            if(!std::isfinite(length))return false;
            if(length>=arms[side].minimum&&length<=arms[side].maximum)continue;
            Vec3 axis;if(!NativeArmMath::Unit(delta,axis))return false;
            point=Add(centres[side],Scale(axis,std::clamp(length,arms[side].minimum,arms[side].maximum)));
            inside=false;
        }
        if(inside)break;
    }
    const float tolerance=.00002f*units;
    for(unsigned side=0;side<2;++side) {
        const float length=NativeArmMath::Length(Subtract(point,centres[side]));
        if(length<arms[side].minimum-tolerance||length>arms[side].maximum+tolerance)return false;
    }
    // Large tracking/stance disagreements must not drag the held weapon away
    // from the player. A later reachable sample can resume the same latch.
    if(NativeArmMath::Length(Subtract(point,start))>.18f*units)return false;
    CameraPose fittedRight=right,fittedLeft=left;fittedRight.position=point;fittedLeft.position=Add(point,offset);
    if(!ValidatePose(fittedRight,.05f)||!ValidatePose(fittedLeft,.05f))return false;
    right=fittedRight;left=fittedLeft;return true;
}
inline bool Aim(const CameraPose& primary,const CameraPose& supportSocket,const CameraPose& controller,
                float units,CameraPose& right,CameraPose& left) {
    if(!std::isfinite(units)||units<1||units>1000||!ValidatePose(primary,.05f)||
       !ValidatePose(supportSocket,.05f)||!ValidatePose(controller,.05f))return false;
    const Vec3 from=NativeRigMath::Transform(primary.basis,supportSocket.position);
    const Vec3 line=Subtract(controller.position,primary.position);
    const float distance=NativeArmMath::Length(line),authored=NativeArmMath::Length(from);
    if(!std::isfinite(distance)||!std::isfinite(authored)||authored<.16f*units||authored>.75f*units||
       distance<.08f*units||distance>1.25f*units)return false;
    // The offhand is the aiming lever even when the primary wrist bends more
    // than ninety degrees relative to it. Keep controller roll using shortest
    // swing; anatomical reach is handled separately without dropping contact.
    Quat swing;
    if(!NativeRigMath::Align(from,line,swing))return false;
    CameraPose aimed=primary;
    aimed.basis={Rotate(swing,primary.basis.forward),Rotate(swing,primary.basis.up),Rotate(swing,primary.basis.right)};
    const auto contact=NativeRigMath::ToWorld(aimed,{supportSocket.basis,supportSocket.position});
    if(!ValidatePose(aimed,.05f)||!ValidatePose(contact,.05f))return false;
    right=aimed;left=contact;return true;
}
// Hard UI/ownership/recenter transitions remember the held button, requiring
// release before re-acquisition. Missing poses/native animation can suspend
// output separately while the exact same physical grip retains its socket.
inline bool Targets(State& state,const Key& key,const CameraPose& authored,bool candidate,
                    const CameraPose& primary,const CameraPose& controller,float units,
                    std::uint32_t now,std::uint64_t pair,float grip,bool eligible,
                    CameraPose& right,CameraPose& left,const CameraPose* steeringController=nullptr,bool poseAvailable=true,
                    std::uint32_t inputTick=0,bool gripSampleAvailable=true,bool primaryAvailable=false,const CameraPose* movementFrame=nullptr) {
    right=primary;left=controller;
    state.acquired=false;state.released=false;state.aimRecovered=false;state.aimRejected=false;state.suspended=false;state.releaseReason=ReleaseReason::None;
    const auto& lever=steeringController?*steeringController:controller;
    const bool input=gripSampleAvailable&&std::isfinite(grip)&&grip>=0&&grip<=1&&std::isfinite(units)&&units>=1&&units<=1000&&
        (!poseAvailable||(ValidatePose(primary,.05f)&&ValidatePose(controller,.05f)&&ValidatePose(lever,.05f)));
    const bool known=key.player&&key.bodyModel&&key.weapon&&key.root&&key.model&&key.marker&&key.markerGeometry;
    // A renderer stall is not a grip release. Ownership/recenter/UI and the
    // current controller sample still decide whether this gesture survives.
    const bool reset=!state.valid||!Same(state.key,key)||state.units!=units;
    if(reset) {const bool wasHeld=state.held;state={};state.key=key;state.units=units;state.released=wasHeld;if(wasHeld)state.releaseReason=ReleaseReason::Identity;
        state.requireRelease=!input||grip>.08f;state.pressed=input&&grip>.55f;}
    state.valid=known;state.tick=now;
    if(!eligible||!known) {
        if(state.held)state.releaseReason=!known?ReleaseReason::Identity:ReleaseReason::Context;
        state.released=state.released||state.held;state.held=false;state.aimValid=false;state.pairValid=false;
        state.requireRelease=!input||grip>.08f;state.lowGripStart=state.lowGripTick=state.lowGripSamples=0;return false;
    }
    // A zero-filled failed controller read is not a physical release. Missing
    // or invalid samples suspend IK and break low-grip confirmation continuity.
    if(!input) {
        state.lowGripStart=state.lowGripTick=state.lowGripSamples=0;
        state.pairValid=false;state.suspended=state.held;return false;
    }
    if(state.requireRelease) {
        state.held=false;state.pairValid=false;
        if(grip>.08f)return false;
        state.requireRelease=false;
    }
    bool pressed=grip>(state.pressed?.08f:.55f);
    if(state.held) {
        pressed=true;
        if(grip<=.08f) {
            const auto sample=inputTick?inputTick:now;
            const auto gap=static_cast<std::int32_t>(sample-state.lowGripTick);
            if(!state.lowGripSamples||gap<0||gap>100) {
                state.lowGripStart=sample;state.lowGripTick=sample;state.lowGripSamples=1;
            } else if(sample!=state.lowGripTick) {state.lowGripTick=sample;++state.lowGripSamples;}
            // Require real fresh host publications, not many body/eye draws
            // of the same transient zero or a wall-clock gap after a stall.
            if(state.lowGripSamples>=3&&sample-state.lowGripStart>=120)pressed=false;
        } else state.lowGripStart=state.lowGripTick=state.lowGripSamples=0;
    }
    state.pressed=pressed;
    if(!pressed) {if(state.held)state.releaseReason=ReleaseReason::Input;state.released=state.released||state.held;state.held=false;state.aimValid=false;state.pairValid=false;return false;}
    if(!poseAvailable) {
        // Offhand optical loss is not a physical release. With a current
        // primary controller and a latched socket, keep the rendered pair
        // rigid instead of letting the visible left hand leave the fore-end.
        if(state.held&&state.aimValid&&primaryAvailable&&ValidatePose(primary,.05f)) {
            right=primary;left=NativeRigMath::ToWorld(right,{state.socket.basis,state.socket.position});
            state.pair=pair;state.pairValid=state.pairHeld=true;state.pairRight=right;state.pairLeft=left;
            state.pairFrameValid=movementFrame&&ValidatePose(*movementFrame,.05f);
            if(state.pairFrameValid)state.pairFrame=*movementFrame;
            state.aimRecovered=true;return true;
        }
        state.pairValid=false;state.suspended=state.held;return false;
    }
    if(state.pairValid&&state.pair==pair) {
        right=state.pairRight;left=state.pairLeft;
        // Repeated material/mask passes share this exact movement frame.
        // An identity world->local->world roundtrip still loses float bits at
        // large map coordinates, then feeds the rounded pose into arm IK and
        // depth. Only an actual frame change needs the transport below.
        if(state.pairFrameValid&&movementFrame&&ValidatePose(*movementFrame,.05f)&&
           std::memcmp(&state.pairFrame,movementFrame,sizeof(*movementFrame))) {
            const auto r=NativeRigMath::ToModel(state.pairFrame,right),l=NativeRigMath::ToModel(state.pairFrame,left);
            right=NativeRigMath::ToWorld(*movementFrame,{r.basis,r.position});left=NativeRigMath::ToWorld(*movementFrame,{l.basis,l.position});
            state.pairRight=right;state.pairLeft=left;state.pairFrame=*movementFrame;
        }
        return state.pairHeld;
    }
    // A squeeze begun a little away from the gun may acquire on entering the
    // contact zone. Context/ownership resets still require a full release.
    if(!state.held&&candidate&&ValidatePose(authored,.05f)) {
        const Vec3 line=Subtract(lever.position,primary.position);
        const float distance=NativeArmMath::Length(line);
        // Start with the already visible grip. The raw controller lever can
        // be outside the solved wrists, even at a valid contact.
        if(ContactNear(primary,authored,controller,units)&&distance>=.08f*units&&distance<=1.25f*units&&
           NativeRigMath::Align(NativeRigMath::Inverse(primary.basis,line),authored.position,state.steeringCorrection)) {
            state.socket=authored;state.held=true;state.acquired=true;
        }
    }
    if(state.held) {
        CameraPose corrected=lever;
        const Vec3 local=NativeRigMath::Inverse(primary.basis,Subtract(lever.position,primary.position));
        corrected.position=Add(primary.position,NativeRigMath::Transform(primary.basis,Rotate(state.steeringCorrection,local)));
        if(Aim(primary,state.socket,corrected,units,right,left)) {
            state.lastAimTick=now;state.aimValid=true;
        } else if(state.aimValid) {
            // A brief near-coincident lever must not turn one jitter sample
            // into a compulsory release/re-grab. Preserve only its last valid
            // rotation, anchored to today's primary position and rigid socket.
            // The physical grip/owner/context, not a timer, owns this latch.
            right=primary;right.basis=state.pairRight.basis;
            left=NativeRigMath::ToWorld(right,{state.socket.basis,state.socket.position});
            state.aimRecovered=true;
        } else {
            state.aimRejected=true;state.suspended=true;
            // No usable orientation yet: retain intent, render independent
            // hands and retry when the real lever becomes observable again.
        }
    }
    state.pair=pair;state.pairValid=true;state.pairHeld=state.held&&!state.suspended;
    state.pairRight=right;state.pairLeft=left;
    state.pairFrameValid=movementFrame&&ValidatePose(*movementFrame,.05f);
    if(state.pairFrameValid)state.pairFrame=*movementFrame;
    return state.pairHeld;
}
} // namespace NativeTwoHandMath
