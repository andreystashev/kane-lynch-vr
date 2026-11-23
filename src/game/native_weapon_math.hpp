#pragma once
#include "native_rig_math.hpp"
#include <cstring>

namespace NativeWeaponMath {
// GPU entries store the child model's world frame, whereas the template
// muzzle is in the item frame. Remove the complete authored child chain.
inline bool ParentFrame(const NativeCameraMath::CameraPose& childWorld,
                        const NativeCameraMath::CameraPose& childToParent,
                        NativeCameraMath::CameraPose& output) {
    using namespace NativeCameraMath;
    if(!ValidatePose(childWorld,.05f)||!ValidatePose(childToParent,.05f))return false;
    CameraPose inverse;
    inverse.basis={NativeRigMath::Inverse(childToParent.basis,{0,0,1}),
                   NativeRigMath::Inverse(childToParent.basis,{0,1,0}),
                   NativeRigMath::Inverse(childToParent.basis,{1,0,0})};
    inverse.position=NativeRigMath::Inverse(childToParent.basis,Scale(childToParent.position,-1));
    const auto result=NativeRigMath::ToWorld(childWorld,{inverse.basis,inverse.position});
    if(!ValidatePose(result,.05f))return false;
    output=result;return true;
}
// ZFFItemWeapon::Fire has a one-shot WORLD-space origin override. Native
// Fire consumes it by clearing all components to zero. A scoped tracked
// replacement must preserve that consumption and never overwrite a newer
// origin written during the shot.
inline bool ZeroMuzzle(const NativeCameraMath::Vec3& value) {
    return value.x==0.f&&value.y==0.f&&value.z==0.f;
}
struct PendingMuzzle {
    NativeCameraMath::Vec3 previous{},temporary{};
    bool active=false;
    enum class Exit { Inactive,Consumed,Restore,Changed };
    bool Prepare(const NativeCameraMath::Vec3& pending,const NativeCameraMath::Vec3& tracked) {
        if(!NativeCameraMath::Finite(pending)||!NativeCameraMath::Finite(tracked)||
           ZeroMuzzle(pending)||ZeroMuzzle(tracked))return false;
        previous=pending;temporary=tracked;active=true;return true;
    }
    Exit Resolve(const NativeCameraMath::Vec3& current) const {
        if(!active)return Exit::Inactive;
        if(ZeroMuzzle(current))return Exit::Consumed;
        return !std::memcmp(&current,&temporary,sizeof(current))?Exit::Restore:Exit::Changed;
    }
};
// Carry the native mesh's authored grip offset with the solved wrist. The
// source draw and both wrist poses stay read-only; failure preserves output.
inline bool Remap(const NativeCameraMath::CameraPose& rawWrist,
                  const NativeCameraMath::CameraPose& finalWrist,
                  const NativeCameraMath::CameraPose& rawMesh,
                  NativeCameraMath::CameraPose& output) {
    using namespace NativeCameraMath;
    if(!ValidatePose(rawWrist,.05f)||!ValidatePose(finalWrist,.05f)||!ValidatePose(rawMesh,.05f))return false;
    if(std::memcmp(&rawWrist,&finalWrist,sizeof(rawWrist))==0){output=rawMesh;return true;}
    const auto local=NativeRigMath::ToModel(rawWrist,rawMesh);
    const auto result=NativeRigMath::ToWorld(finalWrist,{local.basis,local.position});
    if(!ValidatePose(result,.05f))return false;
    output=result;return true;
}
// Immutable grip certificate. Rebasing the previous rebased world matrix
// every frame compounds even accepted float roundoff into scale/shear.
// Always reconstruct this exact item/muzzle from its first proved wrist.
struct GripCalibration {
    NativeCameraMath::CameraPose wrist{},attachment{};
    NativeCameraMath::Vec3 origin{},target{};
    bool valid=false;
};
inline bool CaptureGrip(const NativeCameraMath::CameraPose& wrist,const NativeCameraMath::CameraPose& attachment,
                        NativeCameraMath::Vec3 origin,NativeCameraMath::Vec3 target,GripCalibration& output) {
    using namespace NativeCameraMath;
    if(!ValidatePose(wrist,.05f)||!ValidatePose(attachment,.05f)||!Finite(origin)||!Finite(target))return false;
    output={wrist,attachment,origin,target,true};return true;
}
inline bool RebuildGrip(const GripCalibration& source,const NativeCameraMath::CameraPose& wrist,
                        NativeCameraMath::CameraPose& attachment,NativeCameraMath::Vec3& origin,NativeCameraMath::Vec3& target) {
    using namespace NativeCameraMath;
    if(!source.valid)return false;
    CameraPose result;if(!Remap(source.wrist,wrist,source.attachment,result))return false;
    const auto point=[&](Vec3 value){return Add(wrist.position,NativeRigMath::Transform(wrist.basis,
        NativeRigMath::Inverse(source.wrist.basis,Subtract(value,source.wrist.position))));};
    const auto nextOrigin=point(source.origin),nextTarget=point(source.target);
    if(!Finite(nextOrigin)||!Finite(nextTarget))return false;
    attachment=result;origin=nextOrigin;target=nextTarget;return true;
}
// A pump/recoil animation can rotate the actual drawn model relative to its
// wrist. Carry that visible model +X with the SAME wrist correction instead
// of silently aiming along the unanimated wrist. Restrict this to an axis
// already close to the established gun direction; callers gate categories.
inline bool VisibleBarrel(const NativeCameraMath::CameraPose& rawWrist,
                          const NativeCameraMath::CameraPose& finalWrist,
                          const NativeCameraMath::CameraPose& nativeMesh,
                          NativeCameraMath::Vec3& output,float& deviationDegrees) {
    using namespace NativeCameraMath;
    if(!ValidatePose(rawWrist,.05f)||!ValidatePose(finalWrist,.05f)||!ValidatePose(nativeMesh,.05f))return false;
    Vec3 axis,wrist;
    if(!NativeArmMath::Unit(nativeMesh.basis.right,axis)||!NativeArmMath::Unit(rawWrist.basis.right,wrist))return false;
    const float dot=std::clamp(Dot(axis,wrist),-1.f,1.f);
    deviationDegrees=std::acos(dot)*(180.f/3.14159265358979323846f);
    if(dot<.9f)return false;
    Vec3 corrected;
    if(!NativeArmMath::Unit(NativeRigMath::Transform(finalWrist.basis,NativeRigMath::Inverse(rawWrist.basis,axis)),corrected))return false;
    output=corrected;return true;
}
// The item root and its drawn model do NOT have the same authored axes.
// Retained native captures show a +X mesh behind an approximately quarter-
// turn child matrix: its barrel is item +Z. Muzzle-marker right points along
// or AGAINST that barrel depending on the actual weapon. Resolve the sign
// with the real grip->muzzle displacement, never with an assumed item axis.
inline bool MuzzleFrame(const NativeCameraMath::CameraPose& localMuzzle,
                        NativeCameraMath::Vec3 grip,float units,
                        NativeCameraMath::Basis& output,unsigned* proofStage=nullptr) {
    using namespace NativeCameraMath;
    const auto stage=[&](unsigned value){if(proofStage)*proofStage=value;};stage(1);
    if(!ValidatePose(localMuzzle,.05f)||!Finite(grip)||!std::isfinite(units)||units<1||units>1000)return false;
    stage(2);
    if(Dot(grip,grip)>units*units||Dot(localMuzzle.position,localMuzzle.position)>9*units*units)return false;
    Vec3 line;stage(3);
    if(!NativeArmMath::Unit(Subtract(localMuzzle.position,grip),line))return false;
    const float alignment=Dot(line,localMuzzle.basis.right);stage(4);
    if(std::fabs(alignment)<.75f)return false;
    Vec3 barrel,up;
    if(!NativeArmMath::Unit(Scale(localMuzzle.basis.right,alignment<0?-1.f:1.f),barrel)||
       !NativeArmMath::Unit(Subtract(localMuzzle.basis.up,Scale(barrel,Dot(localMuzzle.basis.up,barrel))),up))return false;
    const Basis next{Cross(barrel,up),up,barrel};
    if(!ValidateBasis(next,.05f))return false;
    output=next;stage(5);return true;
}
// Remove the native item's animated rotation by mapping its PROVEN muzzle
// frame onto the physical wrist pointing frame. Preserve its actual grip
// and muzzle offsets; neither the item root nor a child mesh is assumed +X.
inline bool HeldAttachment(const NativeCameraMath::CameraPose& rawWrist,
                           const NativeCameraMath::CameraPose& finalWrist,
                           const NativeCameraMath::CameraPose& nativeItem,
                           const NativeCameraMath::CameraPose& localMuzzle,
                           int side,float units,NativeCameraMath::CameraPose& output,
                           unsigned* proofStage=nullptr,NativeCameraMath::Basis* localAim=nullptr,
                           const NativeCameraMath::Vec3* palmLocal=nullptr) {
    using namespace NativeCameraMath;
    if(side<0||side>1||!ValidatePose(rawWrist,.05f)||!ValidatePose(finalWrist,.05f)||
       !ValidatePose(nativeItem,.05f)||!ValidatePose(localMuzzle,.05f)||!std::isfinite(units)||units<1||units>1000)return false;
    const auto grip=NativeRigMath::ToModel(nativeItem,rawWrist).position;
    Basis source;
    if(!MuzzleFrame(localMuzzle,grip,units,source,proofStage))return false;
    CameraPose target=finalWrist,next;
    // Left native wrist anatomy mirrors thumb/palm axes; a weapon keeps its
    // authored up/lateral axes rather than becoming upside-down in that hand.
    if(side==0){target.basis.forward=Scale(target.basis.forward,-1);target.basis.up=Scale(target.basis.up,-1);}
    next.basis={NativeRigMath::Transform(target.basis,NativeRigMath::Inverse(source,{0,0,1})),
                NativeRigMath::Transform(target.basis,NativeRigMath::Inverse(source,{0,1,0})),
                NativeRigMath::Transform(target.basis,NativeRigMath::Inverse(source,{1,0,0}))};
    next.position=Subtract(finalWrist.position,NativeRigMath::Transform(next.basis,grip));
    if(palmLocal&&Finite(*palmLocal)&&Dot(*palmLocal,*palmLocal)<.15f*.15f*units*units) {
        // Keep the game's grip centred at the palm when correcting barrel
        // direction. Rotating about the wrist alone swings the handle away
        // from the fingers. Cap the correction for unusual wrist poses.
        const auto nativePalmOffset=NativeRigMath::Transform(rawWrist.basis,*palmLocal);
        const auto itemPalmOffset=NativeRigMath::Inverse(nativeItem.basis,nativePalmOffset);
        const auto solvedPalmOffset=NativeRigMath::Transform(finalWrist.basis,*palmLocal);
        const auto shift=Subtract(solvedPalmOffset,NativeRigMath::Transform(next.basis,itemPalmOffset));
        const auto distance=NativeArmMath::Length(shift);
        if(distance>0.f)next.position=Add(next.position,Scale(shift,std::min(1.f,.025f*units/distance)));
    }
    if(!ValidatePose(next,.05f))return false;
    if(localAim)*localAim=source;output=next;return true;
}
inline bool CarryPart(const NativeCameraMath::CameraPose& nativeItem,const NativeCameraMath::CameraPose& heldItem,
                      const NativeCameraMath::CameraPose& nativePart,NativeCameraMath::CameraPose& output) {
    return Remap(nativeItem,heldItem,nativePart,output);
}
// The native firearm composes its template's muzzle marker with its current
// attachment frame. Carry that exact offset with the same wrist correction
// as the mesh, then aim from the muzzle along the Touch pointing direction.
inline bool Shot(const NativeCameraMath::CameraPose& rawWrist,
                 const NativeCameraMath::CameraPose& finalWrist,
                 const NativeCameraMath::CameraPose& nativeAttachment,
                 const NativeCameraMath::CameraPose& localMuzzle,float units,
                 NativeCameraMath::CameraPose& attachment,
                 NativeCameraMath::Vec3& muzzle,NativeCameraMath::Vec3& target,
                 const NativeCameraMath::Vec3* visibleBarrel=nullptr) {
    using namespace NativeCameraMath;
    CameraPose corrected;
    if(!std::isfinite(units)||units<1.f||units>1000.f||!ValidatePose(localMuzzle,.05f)||
       Dot(localMuzzle.position,localMuzzle.position)>9.f*units*units||
       !Remap(rawWrist,finalWrist,nativeAttachment,corrected))return false;
    const auto barrel=NativeRigMath::ToWorld(corrected,{localMuzzle.basis,localMuzzle.position});
    if(!ValidatePose(barrel,.05f))return false;
    const auto direction=visibleBarrel?*visibleBarrel:finalWrist.basis.right;
    const float length=std::sqrt(Dot(direction,direction));
    if(!std::isfinite(length)||length<.9f||length>1.1f)return false;
    const auto aim=Add(barrel.position,Scale(direction,100.f*units/length));
    if(!Finite(aim))return false;
    attachment=corrected;muzzle=barrel.position;target=aim;return true;
}
}
