#pragma once
#include "native_camera_math.hpp"
#include <algorithm>
#include <cmath>

// Two-bone anatomical reach, independent of world/model-space conventions.
// Inputs and outputs must all use the same coordinate system and units.
namespace NativeArmMath {
using namespace NativeCameraMath;
struct Joints {Vec3 shoulder{},elbow{},wrist{};};
inline float Length(Vec3 vector){return std::sqrt(Dot(vector,vector));}
inline bool Unit(Vec3 vector,Vec3& result) {
    const float length=Length(vector);
    if(!std::isfinite(length)||length<1e-7f)return false;
    result=Scale(vector,1.f/length);return true;
}
// Use the animated anatomical shoulder line, rather than fixed model axes:
// the animation may turn the torso inside its render root. modelDown is the
// world downward direction expressed in that same model space. Side 0 is
// anatomical left, side 1 right. Failure leaves output untouched.
inline bool ElbowPole(Vec3 leftShoulder,Vec3 rightShoulder,Vec3 modelDown,
                      int side,Vec3& output) {
    if(side<0||side>1||!Finite(leftShoulder)||!Finite(rightShoulder)||!Finite(modelDown))return false;
    Vec3 outward,down,result;
    if(!Unit(Subtract(leftShoulder,rightShoulder),outward)||!Unit(modelDown,down))return false;
    if(side)outward=Scale(outward,-1.f);
    if(!Unit(Add(outward,Scale(down,.5f)),result))return false;
    output=result;return true;
}
// poleDirection points toward the preferred elbow side. Collinear poles
// fall back to the animated elbow plane, then a deterministic orthogonal axis.
// The shoulder and segment lengths remain fixed. Targets outside anatomical
// reach are clamped; neither a fully straight nor a fully folded singularity
// is emitted. Failure leaves output untouched.
inline bool Solve(const Joints& animated,Vec3 target,Vec3 poleDirection,
                  Joints& output,bool* reachClamped=nullptr) {
    if(!Finite(animated.shoulder)||!Finite(animated.elbow)||!Finite(animated.wrist)||
       !Finite(target)||!Finite(poleDirection))return false;
    const Vec3 upper=Subtract(animated.elbow,animated.shoulder);
    const Vec3 lower=Subtract(animated.wrist,animated.elbow);
    const float a=Length(upper),b=Length(lower),sum=a+b;
    if(!std::isfinite(sum)||a<1e-4f||b<1e-4f||sum>100000.f)return false;
    const Vec3 delta=Subtract(target,animated.shoulder);
    const float requested=Length(delta);
    if(!std::isfinite(requested))return false;
    Vec3 along;
    if(!Unit(delta,along)&&!Unit(Subtract(animated.wrist,animated.shoulder),along)&&!Unit(upper,along))return false;
    const float margin=std::min(std::min(a,b)*.01f,std::max(sum*1e-5f,1e-6f));
    const float distance=std::clamp(requested,std::fabs(a-b)+margin,sum-margin);
    const auto perpendicular=[&](Vec3 value){return Subtract(value,Scale(along,Dot(value,along)));};
    Vec3 bend;
    if(!Unit(perpendicular(poleDirection),bend)&&!Unit(perpendicular(upper),bend)) {
        const Vec3 axis=std::fabs(along.y)<.8f?Vec3{0,1,0}:Vec3{1,0,0};
        if(!Unit(perpendicular(axis),bend))return false;
    }
    const double axial=(double(a)*a-double(b)*b+double(distance)*distance)/(2.0*distance);
    const double radial=std::sqrt(std::max(0.0,double(a)*a-axial*axial));
    Joints result;
    result.shoulder=animated.shoulder;
    result.elbow=Add(result.shoulder,Add(Scale(along,static_cast<float>(axial)),Scale(bend,static_cast<float>(radial))));
    result.wrist=Add(result.shoulder,Scale(along,distance));
    if(!Finite(result.elbow)||!Finite(result.wrist))return false;
    output=result;
    if(reachClamped)*reachClamped=std::fabs(requested-distance)>margin*.5f;
    return true;
}
} // namespace NativeArmMath
