#pragma once
#include "reload_math_kl.hpp"
#include <algorithm>
namespace ReloadPresentationKL {
using namespace NativeCameraMath;
inline bool Unit(Vec3 v,Vec3& out){const float n=Dot(v,v);if(!Finite(v)||n<1e-8f)return false;out=Scale(v,1/std::sqrt(n));return true;}
inline bool Gun(const CameraPose& wrist,Vec3 barrel,CameraPose& out){
 Vec3 f,u;if(!ValidatePose(wrist,.05f)||!Unit(barrel,f))return false;
 if(!Unit(Subtract(wrist.basis.up,Scale(f,Dot(f,wrist.basis.up))),u)&&!Unit(Subtract(wrist.basis.right,Scale(f,Dot(f,wrist.basis.right))),u))return false;
 out={{f,u,Cross(u,f)},wrist.position};return ValidatePose(out,.05f);
}
inline CameraPose Blend(const CameraPose& a,const CameraPose& b,float fraction){
 const float t=std::clamp(fraction,0.f,1.f);if(t>=1)return b;if(t<=0)return a;
 Vec3 f,u;const auto mix=[&](Vec3 x,Vec3 y){return Add(Scale(x,1-t),Scale(y,t));};
 if(!Unit(mix(a.basis.forward,b.basis.forward),f))f=b.basis.forward;
 auto up=mix(a.basis.up,b.basis.up);if(!Unit(Subtract(up,Scale(f,Dot(up,f))),u))u=b.basis.up;
 CameraPose out{{f,u,Cross(u,f)},mix(a.position,b.position)};return ValidatePose(out,.05f)?out:b;
}
inline Vec3 PalmContact(Vec3 half,float units){
 return {-half.x-.003f*units,std::min(half.y*.25f,.01f*units),-std::min(half.z*.18f,.018f*units)};
}
inline CameraPose HeldGrip(const CameraPose& palm,Vec3 half,float units,bool mp5=false){
 // F=thumb (feed), U=fingers (broad magazine face), R=palm normal.
 CameraPose out{{palm.basis.up,palm.basis.right,palm.basis.forward},palm.position};
 if(mp5){
    constexpr float cosine=.978148f,sine=.207912f;
    out.basis.forward=Add(Scale(palm.basis.up,cosine),Scale(palm.basis.right,sine));
    out.basis.up=Subtract(Scale(palm.basis.right,cosine),Scale(palm.basis.up,sine));
 }
 // Keep the solid body's near face against the anatomical palm, rather
 // than putting half of the broad face through the hand at the knuckles.
 out.position=Add(out.position,ReloadMathKL::Transform(out.basis,PalmContact(half,units)));return out;
}
inline CameraPose PalmFromGrip(const CameraPose& grip,Vec3 half,float units,bool mp5=false){
 CameraPose out{{grip.basis.right,grip.basis.forward,grip.basis.up},Subtract(grip.position,ReloadMathKL::Transform(grip.basis,PalmContact(half,units)))};
 if(mp5){
    constexpr float cosine=.978148f,sine=.207912f;
    out.basis.up=Subtract(Scale(grip.basis.forward,cosine),Scale(grip.basis.up,sine));
    out.basis.right=Add(Scale(grip.basis.up,cosine),Scale(grip.basis.forward,sine));
 }
 return out;
}
// Guide translations in the loading zone while retaining the actual wrist rotation.
inline bool GuidePalm(CameraPose& wrist,const CameraPose& palmLocal,Vec3 goal,float maxDistance){
 if(!ValidatePose(wrist,.05f)||!ValidatePose(palmLocal,.05f)||!Finite(goal))return false;
 const auto delta=Subtract(goal,ReloadMathKL::ToWorld(wrist,palmLocal).position);
 const float distance=std::sqrt(Dot(delta,delta));
 if(!std::isfinite(distance)||distance<.001f)return false;
 wrist.position=Add(wrist.position,Scale(delta,std::min(1.f,maxDistance/distance)));
 return true;
}
// Standard20's mul(position, m44ModelViewProj), MATRIX_COLUMNS CTAB, uploads
// four output-coordinate vectors. Compose the actual admitted GPU matrix
// with oldWorld^-1 * newWorld; native cached WVP may belong to another eye.
inline bool Reproject(const float* current,const CameraPose& before,const CameraPose& after,float* output){
 if(!current||!output||!ValidatePose(before,.05f)||!ValidatePose(after,.05f))return false;
 const Vec3 oldAxes[]{before.basis.right,before.basis.up,before.basis.forward},newAxes[]{after.basis.right,after.basis.up,after.basis.forward};
 float delta[16]{};for(unsigned row=0;row<3;++row){for(unsigned col=0;col<3;++col)delta[row*4+col]=Dot(oldAxes[row],newAxes[col]);delta[row*4+3]=Dot(oldAxes[row],Subtract(after.position,before.position));}delta[15]=1;
 float next[16]{};for(unsigned row=0;row<4;++row)for(unsigned col=0;col<4;++col){for(unsigned k=0;k<4;++k)next[row*4+col]+=current[row*4+k]*delta[k*4+col];if(!std::isfinite(next[row*4+col]))return false;}
 std::copy(next,next+16,output);return true;
}
}
