#pragma once
#include "native_arm_math.hpp"
#include <array>
#include <cstdint>
#include <cstring>

namespace NativeRigMath {
using namespace NativeCameraMath;
struct Bone {Basis basis{};Vec3 position{};};
static_assert(sizeof(Bone)==48,"Native cumulative bone stride");
constexpr unsigned MaximumBones=128;
inline Vec3 Transform(const Basis& basis,Vec3 point) {
    return Add(Add(Scale(basis.right,point.x),Scale(basis.up,point.y)),Scale(basis.forward,point.z));
}
inline Vec3 Inverse(const Basis& basis,Vec3 point) {
    return {Dot(point,basis.right),Dot(point,basis.up),Dot(point,basis.forward)};
}
inline CameraPose ToModel(const CameraPose& root,const CameraPose& world) {
    CameraPose result;
    result.position=Inverse(root.basis,Subtract(world.position,root.position));
    result.basis={Inverse(root.basis,world.basis.forward),Inverse(root.basis,world.basis.up),Inverse(root.basis,world.basis.right)};
    return result;
}
inline CameraPose ToWorld(const CameraPose& root,const Bone& bone) {
    CameraPose result;
    result.position=Add(root.position,Transform(root.basis,bone.position));
    result.basis={Transform(root.basis,bone.basis.forward),Transform(root.basis,bone.basis.up),Transform(root.basis,bone.basis.right)};
    return result;
}
inline bool Align(Vec3 from,Vec3 to,Quat& rotation) {
    Vec3 a,b;
    if(!NativeArmMath::Unit(from,a)||!NativeArmMath::Unit(to,b))return false;
    const float dot=std::clamp(Dot(a,b),-1.f,1.f);
    if(dot<-.99999f) {
        Vec3 axis=Cross(a,std::fabs(a.x)<.8f?Vec3{1,0,0}:Vec3{0,1,0});
        if(!NativeArmMath::Unit(axis,axis))return false;
        rotation={axis.x,axis.y,axis.z,0};return true;
    }
    const Vec3 cross=Cross(a,b);
    const float w=1.f+dot,length=std::sqrt(Dot(cross,cross)+w*w);
    if(!std::isfinite(length)||length<1e-7f)return false;
    rotation={cross.x/length,cross.y/length,cross.z/length,w/length};return true;
}
inline bool Descendant(unsigned index,unsigned ancestor,const std::uint32_t* parents) {
    while(index>ancestor)index=parents[index];return index==ancestor;
}
inline void RotateTree(Bone* bones,const std::uint32_t* parents,unsigned count,unsigned ancestor,Vec3 pivot,Quat rotation) {
    for(unsigned i=ancestor;i<count;++i)if(Descendant(i,ancestor,parents)) {
        auto& bone=bones[i];
        bone.position=Add(pivot,Rotate(rotation,Subtract(bone.position,pivot)));
        bone.basis={Rotate(rotation,bone.basis.forward),Rotate(rotation,bone.basis.up),Rotate(rotation,bone.basis.right)};
    }
}
// K&L FORETWIST is a sibling of FOREARM, and upper-arm muscle is a sibling
// of UPPER_ARM. Their skin weights still belong to those anatomical segments.
struct ArmHelpers {
    unsigned foreTwist=MaximumBones,foreTwist1=MaximumBones,upperMuscle=MaximumBones;
};
struct ArmOptions {
    ArmHelpers helpers{};
    bool distributeTwist=true;
    bool twistReferenceValid=false;
    float twistReferenceRadians=0.f;
};
inline bool AffectedByArm(unsigned index,unsigned upper,const std::uint32_t* parents,const ArmHelpers& helpers) {
    return Descendant(index,upper,parents)||
        (helpers.upperMuscle<MaximumBones&&Descendant(index,helpers.upperMuscle,parents));
}
// Map two proper local XYZ frames without interpreting reflected F/U/R
// storage as a quaternion matrix. Swing matches X, then roll matches Y.
inline bool RelativeRotation(const Basis& from,const Basis& to,Quat& output) {
    if(!ValidateBasis(from,.05f)||!ValidateBasis(to,.05f))return false;
    Quat swing;
    if(!Align(from.right,to.right,swing))return false;
    const Vec3 swungUp=Rotate(swing,from.up);
    const float angle=std::atan2(Dot(to.right,Cross(swungUp,to.up)),Dot(swungUp,to.up));
    const float sine=std::sin(angle*.5f);
    const Quat roll{to.right.x*sine,to.right.y*sine,to.right.z*sine,std::cos(angle*.5f)};
    Quat result;
    if(!NormalizeQuaternion(Multiply(roll,swing),result))return false;
    if(result.w<0)result={-result.x,-result.y,-result.z,-result.w};
    output=result;return true;
}
// Residual tracked rotation relative to the ORIGINAL wrist after arm swing.
// This cancels the native wrist's authored ~90-degree bind-frame offset.
// A half-turn swing perpendicular to the forearm has undefined twist: keep
// the last continuous value (or zero), rather than inventing axial rotation.
inline bool ResidualTwist(const Basis& from,const Basis& target,Vec3 forearmAxis,
                          bool referenceValid,float reference,float& output,bool& defined) {
    if(referenceValid&&!std::isfinite(reference))return false;
    Vec3 axis;Quat rotation;
    if(!NativeArmMath::Unit(forearmAxis,axis)||!RelativeRotation(from,target,rotation))return false;
    const float projection=rotation.x*axis.x+rotation.y*axis.y+rotation.z*axis.z;
    const float norm2=projection*projection+rotation.w*rotation.w;
    if(!std::isfinite(norm2))return false;
    if(norm2<1e-10f) {output=referenceValid?reference:0.f;defined=false;return true;}
    constexpr float twoPi=6.2831853071795864769f;
    const float principal=2.f*std::atan2(projection,rotation.w);
    const float result=referenceValid?reference+std::remainder(principal-reference,twoPi):principal;
    if(!std::isfinite(result))return false;
    output=result;defined=true;return true;
}
// Pronation belongs to the forearm, rather than entirely to the wrist's
// skinning joint. Keep the remaining wrist roll bounded AND periodic: the
// same tracked orientation after a complete revolution must produce the same
// skin pose, regardless of the unwrapped diagnostic/history angle. Fractional
// interpolation of that unwrapped angle winds helpers around a stationary
// wrist and can collapse non-adjacent LBS influences.
inline bool PronationAngles(float twist,float& forearm,float& residual) {
    if(!std::isfinite(twist))return false;
    constexpr float pi=3.14159265358979323846f;
    const float principal=std::remainder(twist,2.f*pi);
    const float remaining=(pi*.25f)*std::sin(principal);
    forearm=principal-remaining;residual=remaining;return true;
}
struct Result {
    NativeArmMath::Joints joints{};
    bool clamped=false;
    float residualTwistRadians=0.f;
    bool twistDefined=false;
};
// Input/output may alias. Only the actual arm subtree is changed; fingertips
// retain their animation-local pose while following the absolute tracked wrist.
// Failure leaves output untouched, including invalid parents or wrist bases.
inline bool SolveArm(const Bone* input,const std::uint32_t* parents,unsigned count,
                     unsigned upper,unsigned lower,unsigned wrist,const CameraPose& target,
                     Vec3 pole,Bone* output,Result& result,const ArmOptions& options={}) {
    if(!input||!parents||!output||count<2||count>MaximumBones||upper==0||upper>=lower||lower>=wrist||wrist>=count||
       parents[lower]!=upper||parents[wrist]!=lower||!ValidatePose(target,.05f))return false;
    for(unsigned i=1;i<count;++i)if(parents[i]>=i)return false;
    const auto& helpers=options.helpers;
    if((helpers.foreTwist!=MaximumBones&&(helpers.foreTwist>=count||helpers.foreTwist==lower||parents[helpers.foreTwist]!=upper))||
       (helpers.foreTwist1!=MaximumBones&&(helpers.foreTwist>=count||helpers.foreTwist1>=count||parents[helpers.foreTwist1]!=helpers.foreTwist))||
       (helpers.upperMuscle!=MaximumBones&&(helpers.upperMuscle==0||helpers.upperMuscle>=count||helpers.upperMuscle==upper||
                                           parents[helpers.upperMuscle]!=parents[upper]))||
       (options.twistReferenceValid&&!std::isfinite(options.twistReferenceRadians)))return false;
    for(unsigned i=1;i<count;++i)if(AffectedByArm(i,upper,parents,helpers))
        if(!ValidatePose({input[i].basis,input[i].position},.05f))return false;
    NativeArmMath::Joints animated{input[upper].position,input[lower].position,input[wrist].position},solved;
    bool clamped=false;
    if(!NativeArmMath::Solve(animated,target.position,pole,solved,&clamped))return false;
    std::array<Bone,MaximumBones> next;
    std::memcpy(next.data(),input,count*sizeof(Bone));
    Quat rotateUpper,rotateLower;
    if(!Align(Subtract(animated.elbow,animated.shoulder),Subtract(solved.elbow,solved.shoulder),rotateUpper))return false;
    RotateTree(next.data(),parents,count,upper,animated.shoulder,rotateUpper);
    if(helpers.upperMuscle<count)
        RotateTree(next.data(),parents,count,helpers.upperMuscle,animated.shoulder,rotateUpper);
    if(!Align(Subtract(next[wrist].position,next[lower].position),Subtract(solved.wrist,solved.elbow),rotateLower))return false;
    RotateTree(next.data(),parents,count,lower,next[lower].position,rotateLower);
    if(helpers.foreTwist<count)
        RotateTree(next.data(),parents,count,helpers.foreTwist,solved.elbow,rotateLower);
    const Basis swungWrist=next[wrist].basis;
    float twist=0.f;bool twistDefined=false;
    const Vec3 forearm=Subtract(solved.wrist,solved.elbow);
    if(!ResidualTwist(swungWrist,target.basis,forearm,options.twistReferenceValid,
                      options.twistReferenceRadians,twist,twistDefined))return false;
    if(options.distributeTwist&&helpers.foreTwist<count&&helpers.foreTwist1<count&&std::fabs(twist)>1e-7f) {
        Vec3 axis;if(!NativeArmMath::Unit(forearm,axis))return false;
        float pronation=0.f,residual=0.f;
        if(!PronationAngles(twist,pronation,residual))return false;
        const Vec3 animatedUpper=Subtract(animated.elbow,animated.shoulder);
        const Vec3 solvedUpper=Subtract(solved.elbow,solved.shoulder);
        Quat baseLowerMotion,baseUpperConstraint,baseUpperMotion;
        if(!RelativeRotation(input[lower].basis,next[lower].basis,baseLowerMotion)||
           !Align(Rotate(baseLowerMotion,animatedUpper),solvedUpper,baseUpperConstraint)||
           !NormalizeQuaternion(Multiply(baseUpperConstraint,baseLowerMotion),baseUpperMotion))return false;
        const float rollSine=std::sin(pronation*.5f);
        const Quat roll{axis.x*rollSine,axis.y*rollSine,axis.z*rollSine,std::cos(pronation*.5f)};
        RotateTree(next.data(),parents,count,lower,solved.elbow,roll);
        RotateTree(next.data(),parents,count,helpers.foreTwist,solved.elbow,roll);
        // Choose humeral axial rotation nearest the final forearm motion,
        // while preserving the solved shoulder/elbow endpoints. Otherwise
        // transferring roll to FOREARM merely moves the collapse to the
        // upper-arm/elbow blend. The native muscle follows this same delta.
        Quat lowerMotion,upperConstraint;
        if(!RelativeRotation(input[lower].basis,next[lower].basis,lowerMotion)||
           !Align(Rotate(lowerMotion,animatedUpper),solvedUpper,upperConstraint))return false;
        Quat upperMotion;
        if(!NormalizeQuaternion(Multiply(upperConstraint,lowerMotion),upperMotion))return false;
        Quat upperRoll;
        // Carry only the change caused by pronation, retaining the authored
        // humeral roll that the existing positional IK chose before it.
        if(!NormalizeQuaternion(Multiply(upperMotion,Conjugate(baseUpperMotion)),upperRoll))return false;
        for(unsigned i=upper;i<count;++i)
            if(Descendant(i,upper,parents)&&!Descendant(i,lower,parents)&&
               !Descendant(i,helpers.foreTwist,parents)) {
                auto& bone=next[i];
                bone.position=Add(solved.shoulder,Rotate(upperRoll,Subtract(bone.position,solved.shoulder)));
                bone.basis={Rotate(upperRoll,bone.basis.forward),Rotate(upperRoll,bone.basis.up),Rotate(upperRoll,bone.basis.right)};
            }
        if(helpers.upperMuscle<count)
            RotateTree(next.data(),parents,count,helpers.upperMuscle,solved.shoulder,upperRoll);
        // Only the remaining <=45 degree wrist roll is apportioned between
        // helper regions. Preserve native mirrored bind/animated offsets.
        const float sine=std::sin(residual/6.f);
        const Quat third{axis.x*sine,axis.y*sine,axis.z*sine,std::cos(residual/6.f)};
        RotateTree(next.data(),parents,count,helpers.foreTwist,solved.elbow,third);
        RotateTree(next.data(),parents,count,helpers.foreTwist1,solved.elbow,third);
    }
    const Basis originalWrist=next[wrist].basis;
    const Vec3 pivot=next[wrist].position;
    for(unsigned i=wrist;i<count;++i)if(Descendant(i,wrist,parents)) {
        auto& bone=next[i];
        bone.position=Add(solved.wrist,Transform(target.basis,Inverse(originalWrist,Subtract(bone.position,pivot))));
        bone.basis={Transform(target.basis,Inverse(originalWrist,bone.basis.forward)),
                    Transform(target.basis,Inverse(originalWrist,bone.basis.up)),
                    Transform(target.basis,Inverse(originalWrist,bone.basis.right))};
    }
    next[upper].position=solved.shoulder;next[lower].position=solved.elbow;
    next[wrist].position=solved.wrist;next[wrist].basis=target.basis;
    for(unsigned i=1;i<count;++i)if(AffectedByArm(i,upper,parents,helpers))
        if(!ValidatePose({next[i].basis,next[i].position},.05f))return false;
    std::memcpy(output,next.data(),count*sizeof(Bone));result={solved,clamped,twist,twistDefined};return true;
}
} // namespace NativeRigMath
