#pragma once
#include "native_hands_math.hpp"
// Inverse of ControllerTarget for private-desktop input replay. The replay
// publishes tracking poses through HandsIPC; the gameplay adapter is unchanged.
namespace ReloadTrackingPoseKL {
inline bool Encode(const NativeCameraMath::CameraPose& base,NativeCameraMath::Quat reference,
                   NativeCameraMath::Vec3 headTracking,const NativeCameraMath::CameraPose& wrist,
                   unsigned side,float units,NativeCameraMath::Vec3& position,NativeCameraMath::Quat& orientation){
 using namespace NativeCameraMath;
 Quat ref;if(side>1||!ValidatePose(base)||!ValidatePose(wrist)||!Finite(headTracking)||!std::isfinite(units)||units<1||units>1000||!NormalizeQuaternion(reference,ref))return false;
 const auto inverse=[&](Vec3 v){return Rotate(ref,{Dot(v,base.basis.right),Dot(v,base.basis.up),-Dot(v,base.basis.forward)});};
 const float sign=side? -1.f:1.f;
 const auto r=inverse(Scale(wrist.basis.forward,sign)),u=inverse(Scale(wrist.basis.up,-sign)),b=inverse(Scale(wrist.basis.right,-1.f));
 const float m00=r.x,m01=u.x,m02=b.x,m10=r.y,m11=u.y,m12=b.y,m20=r.z,m21=u.z,m22=b.z;
 Quat q;const float trace=m00+m11+m22;
 if(trace>0){const float s=2*std::sqrt(trace+1);q={(m21-m12)/s,(m02-m20)/s,(m10-m01)/s,s/4};}
 else if(m00>m11&&m00>m22){const float s=2*std::sqrt(1+m00-m11-m22);q={s/4,(m01+m10)/s,(m02+m20)/s,(m21-m12)/s};}
 else if(m11>m22){const float s=2*std::sqrt(1+m11-m00-m22);q={(m01+m10)/s,s/4,(m12+m21)/s,(m02-m20)/s};}
 else {const float s=2*std::sqrt(1+m22-m00-m11);q={(m02+m20)/s,(m12+m21)/s,s/4,(m10-m01)/s};}
 Quat normalized;if(!NormalizeQuaternion(q,normalized))return false;
 const auto p=Add(headTracking,inverse(Scale(Subtract(wrist.position,base.position),1/units)));
 if(!Finite(p)||Dot(Subtract(p,headTracking),Subtract(p,headTracking))>1.5f*1.5f)return false;
 position=p;orientation=normalized;return true;
}
}
