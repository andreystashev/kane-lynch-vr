#pragma once
#include "native_skin_math.hpp"
#include "native_weapon_ownership.hpp"
#include <array>
#include <cmath>
#include <cstring>

// A VR cargo avatar pose, separate from the native Vehicle state/clip.
// Every caller works on its OWN skin copy. Original queued palettes, native
// bones, animation clocks, physics and the accepted shadow source stay intact.
namespace NativePassengerPoseMath {
using namespace NativeCameraMath;
using Bone=NativeRigMath::Bone;
struct Leg {unsigned thigh=128,calf=128,foot=128,toe=128;};
struct Rig {unsigned pelvis=128,head=128;Leg legs[2]{};};
struct Sample {float phase=0,speed=0,crouchMetres=.45f;Vec3 velocityMetres{};};
inline bool Valid(const Sample& sample) {
    return std::isfinite(sample.phase)&&std::isfinite(sample.speed)&&sample.speed>=0&&sample.speed<=2&&
        std::isfinite(sample.crouchMetres)&&sample.crouchMetres>=.3f&&sample.crouchMetres<=.65f&&Finite(sample.velocityMetres);
}
template<class Reader>bool CaptureRig(std::uintptr_t image,std::uintptr_t player,std::uintptr_t expectedModel,
                                     unsigned expectedCount,Reader read,Rig& output) {
    namespace W=NativeWeaponOwnership;
    std::uint32_t model=0,count=0,geometry=0,handle=0,partsHandle=0,headerHandle=0,definitionsHandle=0;
    std::uintptr_t resource=0,parts=0,header=0,definitions=0;std::uint16_t type=0;std::uint8_t flags=0;
    if(!player||!expectedModel||expectedCount<2||expectedCount>128||!W::Read(player+0x70,model,read)||model!=expectedModel||
       !W::Read(model+0x4A0,count,read)||count!=expectedCount||!W::Read(player+4,geometry,read)||!W::Read(geometry+0x68,handle,read)||
       !W::Resource(image,handle,read,resource)||!W::Read(resource+2,type,read)||type!=7||!W::Read(resource+0xC,flags,read)||!(flags&1)||
       !W::Read(resource+0x10,partsHandle,read)||!W::Resource(image,partsHandle,read,parts)||!W::Read(parts+4,headerHandle,read)||
       !W::Resource(image,headerHandle,read,header)||!W::Read(header,count,read)||count!=expectedCount||
       !W::Read(header+4,definitionsHandle,read)||!W::Resource(image,definitionsHandle,read,definitions))return false;
    std::array<std::array<unsigned char,64>,128> rows{};
    if(!read(definitions,rows.data(),expectedCount*64))return false;
    const char* names[] = {"PELVIS","HEAD","LEFT_THIGH","LEFT_CALF","LEFT_FOOT","LEFT_TOE","RIGHT_THIGH","RIGHT_CALF","RIGHT_FOOT","RIGHT_TOE"};
    unsigned found[10];std::fill(std::begin(found),std::end(found),128u);
    for(unsigned bone=0;bone<expectedCount;++bone)for(unsigned key=0;key<10;++key)
        if(!std::memcmp(rows[bone].data()+0x1C,names[key],std::strlen(names[key])+1)){
            if(found[key]!=128)return false;found[key]=bone;
        }
    for(auto bone:found)if(!bone||bone>=expectedCount)return false;
    std::uint32_t afterModel=0,afterGeometry=0,afterHandle=0,afterCount=0,afterDefinitions=0;
    if(!W::Read(player+0x70,afterModel,read)||afterModel!=model||!W::Read(player+4,afterGeometry,read)||afterGeometry!=geometry||
       !W::Read(geometry+0x68,afterHandle,read)||afterHandle!=handle||!W::Read(model+0x4A0,afterCount,read)||afterCount!=expectedCount||
       !W::Read(header+4,afterDefinitions,read)||afterDefinitions!=definitionsHandle)return false;
    output={found[0],found[1],{{found[2],found[3],found[4],found[5]},{found[6],found[7],found[8],found[9]}}};return true;
}
inline bool Topology(const Rig& rig,const std::uint32_t* parents,unsigned count){
    if(!parents||count<2||count>128||!rig.pelvis||rig.pelvis>=rig.head||rig.head>=count)return false;
    for(unsigned i=1;i<count;++i)if(parents[i]>=i)return false;
    if(!NativeRigMath::Descendant(rig.head,rig.pelvis,parents))return false;
    for(const auto& leg:rig.legs)
        if(!leg.thigh||leg.thigh>=leg.calf||leg.calf>=leg.foot||leg.foot>=leg.toe||leg.toe>=count||
           parents[leg.thigh]!=rig.pelvis||parents[leg.calf]!=leg.thigh||parents[leg.foot]!=leg.calf||
           !NativeRigMath::Descendant(leg.toe,leg.foot,parents))return false;
    return true;
}
inline bool Eye(const CameraPose* bind,unsigned count,unsigned head,unsigned marker,const Sample& sample,float units,Vec3& output){
    if(!bind||head>=count||marker>=count||count>128||!Valid(sample)||!std::isfinite(units)||units<1||units>1000)return false;
    Bone pose{bind[head].basis,Subtract(bind[head].position,{0,sample.crouchMetres*units,0})};
    return NativeSkinMath::AnatomicalEye(bind[head],bind[marker],pose,units,output);
}
inline Vec3 FootOffset(const Sample& sample,unsigned side,float units){
    constexpr float pi=3.14159265358979323846f;
    const float moving=std::clamp(sample.speed/.25f,0.f,1.f);
    if(moving<=0)return {};
    Vec3 direction{sample.velocityMetres.x,0,sample.velocityMetres.z};
    const float n=Dot(direction,direction);if(n<1e-6f)return {};direction=Scale(direction,1/std::sqrt(n));
    float cycle=std::fmod(sample.phase/(2*pi)+(side?.5f:0.f),1.f);if(cycle<0)cycle+=1;
    constexpr float stride=.65f,stance=.6f;
    float along=0,lift=0;
    if(cycle<stance)along=.3f*stride-cycle*stride;
    else {const float t=(cycle-stance)/(1-stance),ease=(1-std::cos(pi*t))*.5f;
        along=-.3f*stride+ease*.6f*stride;lift=.07f*std::sin(pi*t);}
    return Add(Scale(direction,along*units*moving),{0,lift*units*moving,0});
}
inline bool Apply(const CameraPose* bind,const Bone* source,Bone* output,const std::uint32_t* parents,unsigned count,
                  const Rig& rig,const Sample& sample,float units,std::uint8_t* changed,const std::uint8_t* recovered=nullptr){
    if(!bind||!source||!output||!changed||!Topology(rig,parents,count)||!Valid(sample)||!std::isfinite(units)||units<1||units>1000)return false;
    for(unsigned i=0;i<count;++i)if(!ValidatePose(bind[i],.05f)||
        (NativeRigMath::Descendant(i,rig.pelvis,parents)&&((recovered&&!recovered[i])||!ValidatePose({source[i].basis,source[i].position},.05f))))return false;
    std::array<Bone,128> next;std::memcpy(next.data(),source,count*sizeof(Bone));
    const Vec3 drop{0,sample.crouchMetres*units,0};
    for(unsigned i=rig.pelvis;i<count;++i)if(NativeRigMath::Descendant(i,rig.pelvis,parents))next[i]={bind[i].basis,Subtract(bind[i].position,drop)};
    std::array<std::uint8_t,128> modified{};
    if(!NativeSkinMath::StableUpperBody(bind,source,next.data(),parents,count,rig.pelvis,rig.head,modified.data(),recovered))return false;
    for(unsigned side=0;side<2;++side){const auto& leg=rig.legs[side];CameraPose target=bind[leg.foot];target.position=Add(target.position,FootOffset(sample,side,units));
        NativeRigMath::Result result;const Vec3 pole{side?-.1f:.1f,0,-1};
        if(!NativeRigMath::SolveArm(next.data(),parents,count,leg.thigh,leg.calf,leg.foot,target,pole,next.data(),result))return false;
        if(result.clamped)return false;
    }
    for(unsigned i=rig.pelvis;i<count;++i)if(NativeRigMath::Descendant(i,rig.pelvis,parents))modified[i]=1;
    std::memcpy(output,next.data(),count*sizeof(Bone));for(unsigned i=0;i<count;++i)changed[i]|=modified[i];return true;
}
}
