#pragma once
#include "native_action_kl.hpp"
#include "native_ledge_kl.hpp"
#include "native_aim_kl_image.hpp"
#include "native_player_pose.hpp"
#include <array>
#include <cstring>
namespace NativeLedgeHintKL {
constexpr std::size_t ClimbBytes=0xEC;
struct Probe {bool valid=false;NativeActionKL::State action;std::uint32_t climb=0,geometry=0,stand=0,clip=0,backend=0;
    std::array<unsigned char,ClimbBytes> native{};};
template<class Reader> Probe Capture(std::uintptr_t image,Reader read){
    Probe out;out.action=NativeActionKL::Snapshot(image,read);const auto& a=out.action;
    if(!a.valid||a.action||(a.movementKind!=NativeActionKL::Movement::Stand&&a.movementKind!=NativeActionKL::Movement::BaseMove))return {};
    const auto get=[&](std::uintptr_t at,auto& v){return NativeActionKL::Address(at,sizeof(v))&&read(at,&v,sizeof(v));};
    std::uint32_t vt=0,owner=0,id=0,blockedAction=0,extra=0,method=0;std::uint8_t used=1,standBusy=1,geometryPause[2]{1,1},climbActive=1;
    if(!get(a.player+0x5F8+11*4,out.climb)||!get(out.climb,vt)||vt!=image+0x566C44||
       !get(out.climb+0x38,owner)||owner!=a.player||!get(out.climb+0x44,id)||id!=11||!get(out.climb+0x54,climbActive)||climbActive||
       !get(a.player+4,out.geometry)||!get(out.geometry+0x60,owner)||owner!=a.player||
       !get(a.player+0x604,out.stand)||!get(out.stand+0x137C,standBusy)||standBusy||
       !get(a.player+0x238,used)||used||!get(a.player+0xC68,blockedAction)||blockedAction||!get(a.player+0x132C,extra)||extra||
       !read(image+0x8883A4,geometryPause,2)||geometryPause[0]||geometryPause[1]||!get(a.player+0x11E4,out.clip))return {};
    NativePlayerPose::CameraPose pose;NativeCameraMath::Vec3 offset;float height=0;
    if(!NativePlayerPose::World(out.geometry,read,pose)||!get(a.player+0x5A8,offset)||!NativeCameraMath::Finite(offset)||
       !get(a.player+0x5C4,height)||!std::isfinite(height))return {};
    // Native74B6C0→760060 decodes the authored state from WORD+44C.
    // Exclude state11 before native CanStart can take its animation-lookup path.
    if(out.clip){std::uint16_t encoded=0;if(!get(out.clip+0x44C,encoded)||std::uint16_t(encoded^0xDEADu)==11)return {};}
    if(!get(image+0x7C1D34,out.backend)||!get(out.backend,vt)||(vt!=image+0x5411C4&&vt!=image+0x541274)||
       !get(vt+0x2C,method)||method!=image+0xC3B20||!read(out.climb,out.native.data(),out.native.size()))return {};
    std::uint32_t nativeVt=0,nativeOwner=0,nativeId=0;std::memcpy(&nativeVt,out.native.data(),4);std::memcpy(&nativeOwner,out.native.data()+0x38,4);std::memcpy(&nativeId,out.native.data()+0x44,4);
    if(nativeVt!=image+0x566C44||nativeOwner!=a.player||nativeId!=11||out.native[0x54])return {};
    const auto after=NativeActionKL::Snapshot(image,read);std::array<unsigned char,ClimbBytes> checked{};std::uint32_t afterClimb=0,afterGeometry=0,afterBackend=0,afterClip=0;
    if(!after.valid||after.player!=a.player||after.movement!=a.movement||after.action||after.movementVtable!=a.movementVtable||
       !get(a.player+0x5F8+11*4,afterClimb)||afterClimb!=out.climb||!get(a.player+4,afterGeometry)||afterGeometry!=out.geometry||
       !get(a.player+0x11E4,afterClip)||afterClip!=out.clip||!get(image+0x7C1D34,afterBackend)||afterBackend!=out.backend||
       !read(out.climb,checked.data(),checked.size())||checked!=out.native)return {};
    out.valid=true;return out;
}
template<class Reader> bool Verify(std::uintptr_t image,Reader read){
    if(!NativeLedgeKL::VerifyImage(image,read)||!NativeAimKLImage::Verify(image,read))return false;
    const auto match=[&](std::uintptr_t rva,const unsigned char* bytes,std::size_t n){std::array<unsigned char,40> actual{};return n<=actual.size()&&read(image+rva,actual.data(),n)&&!std::memcmp(actual.data(),bytes,n);};
    const unsigned char allocate[]={0x68,0xEC,0,0,0,0xE8,0xBD,0x1B,0xE5,0xFF};
    const unsigned char query[]={0x55,0x8B,0xEC,0x83,0xE4,0xF8,0x81,0xEC,0xB8,1,0,0,0x53,0x55,0x56,0x8B,0xF1,0x57,0x8B,0x7E,0x38};
    const unsigned char height[]={0xD9,0x81,0xC4,5,0,0,0xC3},decode[]={0x0F,0xB7,0x41,4,0x35,0xAD,0xDE,0,0,0xC3};
    const unsigned char stand[]={0x0F,0xBE,0x81,0x7C,0x13,0,0,0xC3},blocked[]={0x33,0xC0,0x39,0x81,0x2C,0x13,0,0,0x0F,0x95,0xC0,0xC3};
    const unsigned char treeFlags[]={0x8B,0x44,0x24,8,0xF7,0xD0,0x23,0x81,0x10,1,0,0,0x0B,0x44,0x24,4,0x89,0x81,0x10,1,0,0,0xC2,8,0};
    const unsigned char name[]={'Z','T','r','e','e','G','r','o','u','p',0};
    return match(0x1EF229,allocate,sizeof(allocate))&&match(0x205B50,query,sizeof(query))&&match(0x136710,height,sizeof(height))&&
        match(0x360060,decode,sizeof(decode))&&match(0x202E90,stand,sizeof(stand))&&match(0x1E6810,blocked,sizeof(blocked))&&
        match(0x10A5F0,treeFlags,sizeof(treeFlags))&&match(0x54AD58,name,sizeof(name));
}
using LogCallback=void(*)(const char*);
bool Install(LogCallback log);
void SetGameplayContext(bool gameplay);
void SimulationTick(std::uintptr_t player);
bool Available(std::uintptr_t player);
struct Stats {bool installed=false,available=false;std::uint64_t probes=0,hits=0,rejected=0;};
Stats Snapshot();
}
