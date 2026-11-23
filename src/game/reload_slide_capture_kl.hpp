// Portable reload port from kane-lynch2-vr f12d21f (2026-11-16).
// Native KL1 integration is tracked as KL1-088; no game addresses are imported.
#pragma once
#include "physical_reload_kl.hpp"
#include <limits>

// One acquisition volume shared by the mechanism and foregrip arbitration.
// Pistol handling keeps its accepted capsule; long guns use the whole real
// moving part plus hand clearance, rather than a point beside the foregrip.
namespace ReloadSlideCaptureKL {
using namespace NativeCameraMath;
inline float BoxDistanceSquared(Vec3 point,float units,const CameraPose& box,Vec3 half){
    if(!Finite(point)||!std::isfinite(units)||units<1||units>1000||!ValidatePose(box,.05f)||
       !Finite(half)||half.x<=0||half.y<=0||half.z<=0)return std::numeric_limits<float>::infinity();
    const auto p=Scale(ReloadMathKL::Inverse(box.basis,Subtract(point,box.position)),1.f/units);
    half=Scale(half,1.f/units);
    const Vec3 outside{std::max(0.f,std::fabs(p.x)-half.x),std::max(0.f,std::fabs(p.y)-half.y),std::max(0.f,std::fabs(p.z)-half.z)};
    return Dot(outside,outside);
}
inline bool Contains(Vec3 point,float units,unsigned category,bool needsChamber,
    Vec3 a,Vec3 b,float radius,const CameraPose& box,Vec3 half,bool boxValid){
    if(!Finite(point)||!Finite(a)||!Finite(b)||!std::isfinite(units)||units<1||units>1000||!std::isfinite(radius))return false;
    if(boxValid&&(category!=0||needsChamber)){
        const float clearance=needsChamber?.085f:.05f;
        return BoxDistanceSquared(point,units,box,half)<=clearance*clearance;
    }
    const auto ab=Subtract(b,a);const float length=Dot(ab,ab);
    const float t=length>.0001f?std::clamp(Dot(Subtract(point,a),ab)/length,0.f,1.f):0.f;
    const auto d=Scale(Subtract(point,Add(a,Scale(ab,t))),1.f/units);
    radius=std::clamp(radius,.02f,.08f);return Dot(d,d)<radius*radius;
}
inline bool HandNear(Vec3 palm,Vec3 wrist,float units,unsigned category,bool needsChamber,
    Vec3 a,Vec3 b,float radius,const CameraPose& box,Vec3 half,bool boxValid){
    return Contains(palm,units,category,needsChamber,a,b,radius,box,half,boxValid)||
        Contains(Scale(Add(palm,wrist),.5f),units,category,needsChamber,a,b,radius,box,half,boxValid);
}
}
