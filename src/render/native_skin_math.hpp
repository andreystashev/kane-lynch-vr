#pragma once
#include "native_rig_math.hpp"
#include <cstring>

namespace NativeSkinMath {
using namespace NativeCameraMath;
struct BindLocal {Quat rotation{};Vec3 position{};};
// A seated native actor can move its shoulders while firing. Ordinary IK
// clamps to that animated reach; a passenger's independently tracked VR
// hands instead retain the exact controller endpoints. Keep the best elbow
// solve and carry the complete wrist/finger subtree to that endpoint.
inline bool PinTrackedWrist(NativeRigMath::Bone* pose,const std::uint32_t* parents,unsigned count,
                            unsigned wrist,const CameraPose& target) {
    if(!pose||!parents||!wrist||wrist>=count||count>NativeRigMath::MaximumBones||!ValidatePose(target,.05f))return false;
    for(unsigned i=1;i<count;++i)if(parents[i]>=i)return false;
    for(unsigned i=wrist;i<count;++i)if(NativeRigMath::Descendant(i,wrist,parents))
        if(!ValidatePose({pose[i].basis,pose[i].position},.05f))return false;
    const CameraPose source{pose[wrist].basis,pose[wrist].position};
    for(unsigned i=wrist;i<count;++i)if(NativeRigMath::Descendant(i,wrist,parents)) {
        const auto local=NativeRigMath::ToModel(source,{pose[i].basis,pose[i].position});
        const auto next=NativeRigMath::ToWorld(target,{local.basis,local.position});pose[i]={next.basis,next.position};
    }
    pose[wrist]={target.basis,target.position};return true;
}
// A stationary, uncrouched player has no locomotion to preserve below the
// pelvis. Native shooting overlays otherwise still lower the pelvis and
// put one foot forward even after the spine itself has been neutralized.
// This modifies only the render copy; the caller must prove that exact
// ordinary movement/input state before using it.
inline bool NeutralStandingLegs(const CameraPose* bind,NativeRigMath::Bone* pose,const std::uint32_t* parents,
                                unsigned count,unsigned pelvis,unsigned head,std::uint8_t* changed,const std::uint8_t* recovered=nullptr) {
    if(!bind||!pose||!parents||!changed||!pelvis||pelvis>=head||head>=count||count>NativeRigMath::MaximumBones||
       !NativeRigMath::Descendant(head,pelvis,parents))return false;
    for(unsigned i=1;i<count;++i)if(parents[i]>=i)return false;
    unsigned spine=head;
    for(unsigned steps=0;parents[spine]!=pelvis;++steps){if(steps>=count||!parents[spine]||parents[spine]>=spine)return false;spine=parents[spine];}
    // Validate the upper subtree too: it will be rebuilt from the same
    // source immediately afterwards, so never leave only half a neutral rig.
    for(unsigned i=pelvis;i<count;++i)if(NativeRigMath::Descendant(i,pelvis,parents))
        if((recovered&&!recovered[i])||!ValidatePose(bind[i],.05f)||!ValidatePose({pose[i].basis,pose[i].position},.05f))return false;
    for(unsigned i=pelvis;i<count;++i)if(NativeRigMath::Descendant(i,pelvis,parents)&&!NativeRigMath::Descendant(i,spine,parents)) {
        pose[i]={bind[i].basis,bind[i].position};changed[i]=1;
    }
    return true;
}
// Keep locomotion below the pelvis, while tracked gameplay owns the upper
// body. Native fire/stance overlays must not rotate the torso under VR IK.
inline bool StableUpperBody(const CameraPose* bind,const NativeRigMath::Bone* source,NativeRigMath::Bone* pose,const std::uint32_t* parents,
                            unsigned count,unsigned pelvis,unsigned head,std::uint8_t* changed,const std::uint8_t* recovered=nullptr) {
    if(!bind||!source||source==pose||!pose||!parents||!changed||!pelvis||pelvis>=head||head>=count||count>NativeRigMath::MaximumBones||
       !NativeRigMath::Descendant(head,pelvis,parents)||!ValidatePose(bind[pelvis],.05f)||!Finite(pose[pelvis].position))return false;
    unsigned spine=head;
    for(unsigned steps=0;parents[spine]!=pelvis;++steps){if(steps>=count||!parents[spine]||parents[spine]>=spine)return false;spine=parents[spine];}
    CameraPose anchor=bind[pelvis];anchor.position=pose[pelvis].position;
    for(unsigned i=spine;i<count;++i)if(NativeRigMath::Descendant(i,spine,parents)){
        if(parents[i]>=i||(recovered&&!recovered[i])||!ValidatePose(bind[i],.05f)||!ValidatePose({source[i].basis,source[i].position},.05f))return false;
    }
    for(unsigned i=spine;i<count;++i)if(NativeRigMath::Descendant(i,spine,parents)){
        const auto parent=parents[i];
        const bool torso=NativeRigMath::Descendant(head,i,parents);
        const CameraPose oldParent=torso?bind[parent]:CameraPose{source[parent].basis,source[parent].position};
        const NativeRigMath::Bone oldChild=torso?NativeRigMath::Bone{bind[i].basis,bind[i].position}:source[i];
        const auto local=NativeRigMath::ToModel(oldParent,{oldChild.basis,oldChild.position});
        const CameraPose nextParent=parent==pelvis?anchor:CameraPose{pose[parent].basis,pose[parent].position};
        const auto stable=NativeRigMath::ToWorld(nextParent,{local.basis,local.position});
        pose[i]={stable.basis,stable.position};changed[i]=1;
    }
    return true;
}
static_assert(sizeof(BindLocal)==28,"K&L bind quaternion/position stride");
// Render authorization lasts until the camera generation is retired. Sensor
// freshness controls IK/shot targeting separately; expiring a head mask or
// mesh pose halfway through a slow frame produces inconsistent draw passes.
inline bool DrawContextCurrent(bool valid,std::uint64_t generation,std::uint64_t activeGeneration,bool ready) {
    return valid&&generation&&generation==activeGeneration&&ready;
}
struct PaletteSource {
    std::uintptr_t palette=0,list=0,rows=0;
    CameraPose root{};
    std::uint32_t gpuIndex=0,index=0,instances=1;
    bool alternate=false;
};
inline bool SourceAddress(std::uintptr_t at,std::size_t bytes) {
    return at>=0x10000u&&bytes<=0xffffffffu&&at<=0xffffffffu-bytes;
}
inline bool SamePaletteSource(const PaletteSource& a,const PaletteSource& b) {
    return a.palette==b.palette&&a.list==b.list&&a.rows==b.rows&&a.gpuIndex==b.gpuIndex&&
        a.index==b.index&&a.instances==b.instances&&a.alternate==b.alternate&&
        !std::memcmp(&a.root,&b.root,sizeof(a.root));
}
// The native binder 471F80 always restores the native world/palette before
// this observation. Its primary cache is owned by the already-proven local
// body. Additional per-instance palettes are accepted only through the exact
// selected native 0x38-byte row, bounded by virtual471F60's native row count.
template<class Reader> bool ResolvePaletteSource(std::uintptr_t image,std::uintptr_t entry,std::uintptr_t state,
                                                std::uintptr_t cache,unsigned bones,Reader read,PaletteSource& output,
                                                bool alternateVerified=true) {
    if(!SourceAddress(image,0x7678A8)||!SourceAddress(entry,0x118)||!SourceAddress(state,0x3C8)||
       bones<2||bones>NativeRigMath::MaximumBones||!SourceAddress(cache,0x90+bones*48))return false;
    const auto get=[&](std::uintptr_t at,auto& value){return SourceAddress(at,sizeof(value))&&read(at,&value,sizeof(value));};
    PaletteSource next;std::uint32_t bound=0;
    if(!get(state+0x3BC,bound)||!get(state+0x8C,next.root)||!ValidatePose(next.root,.05f))return false;
    next.palette=bound;
    if(bound!=cache+0x90) {
        if(!alternateVerified||!get(image+0x7678A4,next.gpuIndex)||next.gpuIndex>1)return false;
        std::uint32_t list=0,rows=0;
        if(!get(entry+0x90+next.gpuIndex*4,list)||!SourceAddress(list,8)||!get(list,next.instances)||
           !next.instances||next.instances>1024||!get(std::uintptr_t(list)+4,rows)||
           !get(state+0x3C4,next.index)||next.index>=next.instances||
           !SourceAddress(rows,std::size_t(next.instances)*0x38))return false;
        struct Row {CameraPose root;std::uint32_t palette,auxiliary;};
        static_assert(sizeof(Row)==0x38,"native instance palette row stride");
        Row row{};
        if(!read(std::uintptr_t(rows)+next.index*0x38,&row,sizeof(row))||
           row.palette!=bound||std::memcmp(&row.root,&next.root,sizeof(row.root)))return false;
        next.list=list;next.rows=rows;next.alternate=true;
    }
    if(!SourceAddress(next.palette,bones*48))return false;
    output=next;return true;
}
template<class Reader> bool VerifyPaletteSourceImage(std::uintptr_t image,Reader read) {
    if(!SourceAddress(image,0x7678A8))return false;
    const auto match=[&](std::uintptr_t rva,const auto& expected){
        unsigned char actual[64]{};return sizeof(expected)<=sizeof(actual)&&read(image+rva,actual,sizeof(expected))&&
            !std::memcmp(actual,expected,sizeof(expected));
    };
    const unsigned char count[]={0x8B,0,0xC2,4,0};
    const unsigned char row[]={0x8B,0x74,0x24,0x14,0x8B,0x86,0xC4,3,0,0,
        0x8D,0x14,0xC5,0,0,0,0,0x2B,0xD0,0x8B,0x41,4,0x8D,0x3C,0xD0};
    const unsigned char palette[]={0x8B,0x4F,0x30,0x89,0x8E,0xBC,3,0,0};
    return match(0x71F78,count)&&match(0x7203E,row)&&match(0x72063,palette);
}
struct DrawPolicy {bool armTracking=true,bodyFollow=true,rootAlignment=false,headMask=false;};
inline DrawPolicy Policy(bool firstPerson,bool actorHeadOnly,bool nativeBodyAction) {
    // The rope and attached props use the native actor root. Moving only its
    // skinned render root during an authored action detaches them visibly.
    return actorHeadOnly?DrawPolicy{false,false,false,true}:
        DrawPolicy{true,!nativeBodyAction,firstPerson&&!nativeBodyAction,firstPerson};
}
inline bool ActorHeadNear(Vec3 nativeHeadWorld,Vec3 actualEyeCentre,float units) {
    if(!Finite(nativeHeadWorld)||!Finite(actualEyeCentre)||!std::isfinite(units)||units<1.f||units>1000.f)return false;
    const auto delta=Subtract(nativeHeadWorld,actualEyeCentre);
    return Dot(delta,delta)<=.35f*.35f*units*units;
}
// A zero 3x3 also zeros normals/tangents before normalization in skinned
// materials. A microscopic positive scale hides the same local head while
// keeping its material inputs finite and its winding non-reflected.
template<std::size_t capacity>
inline bool MaskHead(std::array<std::array<float,12>,capacity>& palette,unsigned count,unsigned head,const std::uint32_t* parents,Vec3 position) {
    if(!parents||count>capacity||count>NativeRigMath::MaximumBones||!head||head>=count||!Finite(position))return false;
    for(unsigned i=1;i<count;++i)if(parents[i]>=i)return false;
    constexpr float scale=.0001f;
    for(unsigned i=head;i<count;++i)if(NativeRigMath::Descendant(i,head,parents)) {
        auto* rows=palette[i].data();for(unsigned n=0;n<12;++n)rows[n]=0.f;
        rows[0]=rows[5]=rows[10]=scale;rows[3]=position.x;rows[7]=position.y;rows[11]=position.z;
    }
    return true;
}
inline bool AnatomicalEye(const CameraPose& bindHead,const CameraPose& bindEye,
                          const NativeRigMath::Bone& head,float units,Vec3& output) {
    if(!ValidatePose(bindHead,.05f)||!ValidatePose(bindEye,.05f)||
       !ValidatePose({head.basis,head.position},.05f)||!std::isfinite(units)||units<1.f||units>1000.f)return false;
    const auto offset=NativeRigMath::Inverse(bindHead.basis,Subtract(bindEye.position,bindHead.position));
    if(Dot(offset,offset)>.35f*.35f*units*units)return false;
    const auto next=Add(head.position,NativeRigMath::Transform(head.basis,offset));
    if(!Finite(next))return false;output=next;return true;
}
// Stance height follows native animation. Horizontal eye animation belongs
// to the visual body pivot; feeding it into the camera restores gait/turn bob.
inline bool StableEyeAnchor(const CameraPose& root,Vec3 anatomicalEye,Vec3& output) {
    if(!ValidatePose(root,.05f)||!Finite(anatomicalEye))return false;
    const auto next=Add(root.position,Scale(root.basis.up,anatomicalEye.y));
    if(!Finite(next))return false;output=next;return true;
}
inline bool EyeAnchor(const CameraPose& root,Vec3 anatomicalEye,bool animatedHead,Vec3& output) {
    if(!animatedHead)return StableEyeAnchor(root,anatomicalEye,output);
    if(!ValidatePose(root,.05f)||!Finite(anatomicalEye))return false;
    const auto next=Add(root.position,NativeRigMath::Transform(root.basis,anatomicalEye));
    if(!Finite(next))return false;output=next;return true;
}
inline bool AnimatedHeadAxis(const CameraPose& root,const CameraPose& bindHead,
                             const NativeRigMath::Bone& head,Vec3 modelAxis,Vec3& output) {
    if(!ValidatePose(root,.05f)||!ValidatePose(bindHead,.05f)||
       !ValidatePose({head.basis,head.position},.05f))return false;
    // Bone axes are costume-specific. Remove their bind orientation before
    // applying the animated palette, then carry model-up into world space.
    const auto localUp=NativeRigMath::Inverse(bindHead.basis,modelAxis);
    const auto worldUp=NativeRigMath::Transform(root.basis,NativeRigMath::Transform(head.basis,localUp));
    const float length2=Dot(worldUp,worldUp);
    if(!Finite(worldUp)||!std::isfinite(length2)||length2<.5f||length2>1.5f)return false;
    output=Scale(worldUp,1.f/std::sqrt(length2));return true;
}
inline bool AnimatedHeadUp(const CameraPose& root,const CameraPose& bindHead,
                           const NativeRigMath::Bone& head,Vec3& output) {
    return AnimatedHeadAxis(root,bindHead,head,{0,1,0},output);
}
inline bool AnimatedHeadForward(const CameraPose& root,const CameraPose& bindHead,
                                const NativeRigMath::Bone& head,Vec3& output) {
    // The pinned character model faces -Z (as in the shoulder-heading and
    // back-weapon proofs), not the entity root's nominal forward row.
    return AnimatedHeadAxis(root,bindHead,head,{0,0,-1},output);
}
// A native F/U/R basis stores conventional local XYZ axes as R/U/F. The
// resulting 3x4 palette was compared with live K&L cache+0x90: maximum error
// 1.9e-5 for torso, head, both wrists and finger roots.
inline bool BindPose(const BindLocal* local,const std::uint32_t* parents,unsigned count,CameraPose* output) {
    if(!local||!parents||!output||count<1||count>NativeRigMath::MaximumBones)return false;
    for(unsigned i=0;i<count;++i) {
        Quat q;if(!NormalizeQuaternion(local[i].rotation,q)||!Finite(local[i].position))return false;
        CameraPose next{{Rotate(q,{0,0,1}),Rotate(q,{0,1,0}),Rotate(q,{1,0,0})},local[i].position};
        if(i) {
            if(parents[i]>=i)return false;
            const auto& parent=output[parents[i]];
            next.position=Add(parent.position,NativeRigMath::Transform(parent.basis,next.position));
            next.basis={NativeRigMath::Transform(parent.basis,next.basis.forward),
                        NativeRigMath::Transform(parent.basis,next.basis.up),NativeRigMath::Transform(parent.basis,next.basis.right)};
        }
        if(!ValidatePose(next,.05f))return false;output[i]=next;
    }
    return true;
}
inline bool Palette(const CameraPose& bind,const NativeRigMath::Bone& pose,float output[12]) {
    if(!ValidatePose(bind,.05f)||!ValidatePose({pose.basis,pose.position},.05f)||!output)return false;
    const auto delta=[&](Vec3 point){return NativeRigMath::Transform(pose.basis,NativeRigMath::Inverse(bind.basis,point));};
    const Vec3 x=delta({1,0,0}),y=delta({0,1,0}),z=delta({0,0,1});
    const Vec3 offset=Subtract(pose.position,delta(bind.position));
    const float result[12]={x.x,y.x,z.x,offset.x,x.y,y.y,z.y,offset.y,x.z,y.z,z.z,offset.z};
    for(float value:result)if(!std::isfinite(value))return false;
    for(unsigned i=0;i<12;++i)output[i]=result[i];return true;
}
// Recover the ORIGINAL animated cumulative pose from the palette currently
// bound for this GPU draw. This avoids a CPU animation snapshot from a newer
// queued frame: P_skin * B_bind = P_animated.
inline bool PoseFromPalette(const CameraPose& bind,const float palette[12],NativeRigMath::Bone& output) {
    if(!palette||!ValidatePose(bind,.05f))return false;
    for(unsigned i=0;i<12;++i)if(!std::isfinite(palette[i]))return false;
    const auto rotate=[&](Vec3 p){return Vec3{
        palette[0]*p.x+palette[1]*p.y+palette[2]*p.z,
        palette[4]*p.x+palette[5]*p.y+palette[6]*p.z,
        palette[8]*p.x+palette[9]*p.y+palette[10]*p.z};};
    NativeRigMath::Bone result;
    result.basis={rotate(bind.basis.forward),rotate(bind.basis.up),rotate(bind.basis.right)};
    result.position=Add(rotate(bind.position),{palette[3],palette[7],palette[11]});
    if(!ValidatePose({result.basis,result.position},.05f))return false;
    output=result;return true;
}
} // namespace NativeSkinMath
