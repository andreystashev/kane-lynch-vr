#pragma once
#include "native_rig_math.hpp"
// Derive a palm centre from actual direct finger-root descendants of the
// qualified native wrist. No world-space or weapon-specific wrist offset.
namespace ReloadPalmKL {
inline NativeCameraMath::CameraPose Grasp(const NativeCameraMath::CameraPose& palm){
 // Magazine long axis follows the thumb, fingers wrap its cross section.
 return {{palm.basis.up,palm.basis.right,palm.basis.forward},palm.position};
}
inline NativeCameraMath::CameraPose FromGrasp(const NativeCameraMath::CameraPose& grip){return {{grip.basis.right,grip.basis.forward,grip.basis.up},grip.position};}
inline bool Capture(const NativeRigMath::Bone* bones,const std::uint32_t* parents,unsigned count,unsigned wrist,float units,unsigned side,NativeCameraMath::CameraPose& output){
 using namespace NativeCameraMath;
 if(!bones||!parents||wrist>=count||side>1||!std::isfinite(units)||units<1||units>1000)return false;
 const CameraPose root{bones[wrist].basis,bones[wrist].position};if(!ValidatePose(root,.05f))return false;
 Vec3 centre{};unsigned found=0;
 for(unsigned i=wrist+1;i<count;++i)if(parents[i]==wrist){
  const auto d=Subtract(bones[i].position,root.position);const float length=std::sqrt(Dot(d,d))/units;
  if(!Finite(d)||length<.01f||length>.15f)continue;
  // Attachments/helpers above the knuckles cannot locate a palm.
  if(Dot(d,root.basis.right)<.005f*units)continue;
  centre=Add(centre,bones[i].position);++found;
 }
 if(!found)return false;const auto knuckles=Scale(centre,1.f/found);
 Vec3 fingers=Subtract(knuckles,root.position);const float reach=std::sqrt(Dot(fingers,fingers));if(reach<.005f*units)return false;fingers=Scale(fingers,1.f/reach);
 auto thumb=Scale(root.basis.up,side?1.f:-1.f);thumb=Subtract(thumb,Scale(fingers,Dot(thumb,fingers)));const float width=std::sqrt(Dot(thumb,thumb));if(width<.01f)return false;thumb=Scale(thumb,1.f/width);
 // Place the grasp in the finger-pad half of the measured palm. The old
 // midpoint plus wrist axes pushed a tilted magazine through the heel.
 centre=Add(root.position,Scale(Subtract(knuckles,root.position),.72f));
 CameraPose palm{{Cross(fingers,thumb),thumb,fingers},centre};
 if(!ValidatePose(palm,.05f))return false;
 auto local=NativeRigMath::ToModel(root,palm);if(!ValidatePose(local,.05f))return false;output=local;return true;
}
}
