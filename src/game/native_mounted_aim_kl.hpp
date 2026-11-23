#pragma once
#include "native_action_kl.hpp"
#include "native_hands_kl.hpp"
#include "native_player_pose.hpp"
#include <array>
#include <cmath>
#include <cstring>

// Native mounted gun steering. The original MG point setter retains its
// local-space limits, traverse rate, animation and firing implementation.
namespace NativeMountedAimKL {
constexpr std::uintptr_t PlayerUpdate=0x1FCD30,PointSetter=0x33AE10,PointCaller=0x1FD57F;
struct Identity {
    bool valid=false,manual=false;
    std::uintptr_t player=0,movement=0,action=0,record=0,controller=0,entity=0,geometry=0,gunObject=0;
    std::uint32_t driver=0,trackedActor=0,recordsBegin=0,recordsEnd=0,entityVtable=0,recordsOffset=0;
};
// Both native factories use the same steering controller and reciprocal
// player record. The car's vector is a DIFFERENT field from a fixed MG.
// The helicopter factory has no player record and is deliberately excluded.
inline unsigned RecordsOffset(std::uintptr_t image,std::uint32_t vtable) {
    return vtable==image+0x57F7EC?0x7AC:vtable==image+0x582D14?0x838:0;
}
struct IdentityProbe {std::uint32_t record=0,controller=0,entity=0,vtable=0,recordsOffset=0;};
inline bool Same(const Identity& a,const Identity& b) {
    return a.valid&&b.valid&&a.player==b.player&&a.movement==b.movement&&a.action==b.action&&
        a.record==b.record&&a.controller==b.controller&&a.entity==b.entity&&a.geometry==b.geometry&&
        a.gunObject==b.gunObject&&a.driver==b.driver&&a.trackedActor==b.trackedActor&&a.manual==b.manual&&
        a.recordsBegin==b.recordsBegin&&a.recordsEnd==b.recordsEnd&&a.entityVtable==b.entityVtable&&a.recordsOffset==b.recordsOffset;
}
template<class Reader>Identity SnapshotIdentity(std::uintptr_t image,std::uintptr_t expectedPlayer,Reader read,unsigned* stage=nullptr,IdentityProbe* probe=nullptr) {
    if(stage)*stage=1;
    if(probe)*probe={};
    Identity next;const auto state=NativeActionKL::Snapshot(image,read);
    // Native movement IDs are not the diagnostic enum ordinals. Live retail
    // registry: Roll=9, Mounted=13; Snapshot already proves the registry slot.
    if(!state.valid||!expectedPlayer||state.player!=expectedPlayer||state.movementKind!=NativeActionKL::Movement::Mounted||state.movementId!=13)return next;
    const auto get=[&](std::uintptr_t at,auto& value){if(stage)++*stage;return NativeActionKL::Address(at,sizeof(value))&&read(at,&value,sizeof(value));};
    std::uint32_t record=0,controller=0,entity=0,geometry=0,gun=0,owner=0,other=0,vt=0;std::uint8_t manual=0;
    const auto observed=[&](std::uintptr_t at,std::uint32_t& value,std::uint32_t IdentityProbe::*member){const bool okay=get(at,value);if(probe)probe->*member=value;return okay;};
    if(!observed(expectedPlayer+0xB04,record,&IdentityProbe::record)||!observed(expectedPlayer+0xB10,controller,&IdentityProbe::controller)||
       !get(record+0x3C,owner)||owner!=expectedPlayer||!get(record+0x40,other)||other!=controller||
       !get(controller+0xF0,other)||other!=record||!observed(controller+0x4C,entity,&IdentityProbe::entity)||
       !observed(entity,vt,&IdentityProbe::vtable))return Identity{};
    next.recordsOffset=RecordsOffset(image,vt);if(probe)probe->recordsOffset=next.recordsOffset;
    if(!next.recordsOffset||!get(entity+next.recordsOffset,next.recordsBegin)||!get(entity+next.recordsOffset+4,next.recordsEnd)||
       next.recordsEnd<next.recordsBegin||!next.recordsBegin||(next.recordsEnd-next.recordsBegin)%0x44||
       (next.recordsEnd-next.recordsBegin)/0x44>64||record<next.recordsBegin||record>=next.recordsEnd||(record-next.recordsBegin)%0x44||
       !get(entity+4,geometry)||!get(geometry+0x60,owner)||owner!=entity||
       !get(controller+0x20,gun)||!NativeActionKL::Address(gun,8)||!get(controller+0x44,manual)||manual>1||
       !get(controller+0x48,next.driver)||!get(controller+0x24,next.trackedActor))return Identity{};
    // Re-read every reciprocal pointer after the complete graph; camera users
    // must additionally repeat this snapshot after walking geometry ancestry.
    const auto after=NativeActionKL::Snapshot(image,read);
    if(!after.valid||after.player!=state.player||after.movement!=state.movement||after.action!=state.action||
       !get(expectedPlayer+0xB04,other)||other!=record||!get(expectedPlayer+0xB10,other)||other!=controller||
       !get(record+0x3C,other)||other!=expectedPlayer||!get(record+0x40,other)||other!=controller||
       !get(controller+0xF0,other)||other!=record||!get(controller+0x4C,other)||other!=entity||
       !get(entity+4,other)||other!=geometry||!get(geometry+0x60,other)||other!=entity||
       !get(entity,other)||other!=vt||
       !get(entity+next.recordsOffset,other)||other!=next.recordsBegin||!get(entity+next.recordsOffset+4,other)||other!=next.recordsEnd||
       !get(controller+0x20,other)||other!=gun)return Identity{};
    next.player=expectedPlayer;next.movement=state.movement;next.action=state.action;next.record=record;next.controller=controller;
    next.entity=entity;next.entityVtable=vt;next.geometry=geometry;next.gunObject=gun;next.manual=manual!=0;next.valid=true;if(stage)*stage=100;return next;
}
inline bool Eligible(const Identity& identity) {
    return identity.valid&&!identity.action&&identity.manual&&identity.driver==identity.player&&!identity.trackedActor;
}
inline bool Fresh(const NativeHandsKL::ControllerAim& aim,std::uintptr_t player,std::uint32_t now,std::uint32_t recenter) {
    const float n=NativeCameraMath::Dot(aim.direction,aim.direction);
    return player&&aim.player==player&&aim.pairId&&aim.tick&&std::int32_t(now-aim.tick)>=0&&now-aim.tick<=50&&
        aim.mappingTick&&std::int32_t(now-aim.mappingTick)>=0&&now-aim.mappingTick<=150&&
        aim.recenterAck==recenter&&NativeCameraMath::Finite(aim.origin)&&NativeCameraMath::Finite(aim.direction)&&
        std::isfinite(n)&&n>.5f&&n<1.5f&&std::isfinite(aim.unitsPerMetre)&&aim.unitsPerMetre>=1&&aim.unitsPerMetre<=1000;
}
inline bool Target(const NativeHandsKL::ControllerAim& aim,NativeCameraMath::Vec3& point) {
    const float n=NativeCameraMath::Dot(aim.direction,aim.direction);
    if(!NativeCameraMath::Finite(aim.origin)||!NativeCameraMath::Finite(aim.direction)||!std::isfinite(n)||n<.5f||n>1.5f||
       !std::isfinite(aim.unitsPerMetre)||aim.unitsPerMetre<1||aim.unitsPerMetre>1000)return false;
    point=NativeCameraMath::Add(aim.origin,NativeCameraMath::Scale(aim.direction,100.f*aim.unitsPerMetre/std::sqrt(n)));
    return NativeCameraMath::Finite(point);
}
struct MountPose {NativeCameraMath::CameraPose carrier{};NativeCameraMath::Vec3 origin{},nativeLocal{};};
template<class Reader>bool SnapshotMountPose(std::uintptr_t image,const Identity& identity,Reader read,MountPose& output){
    using namespace NativeCameraMath;std::uint32_t geometry=0,owner=0,afterGeometry=0;
    MountPose next;CameraPose gun;
    if(!identity.valid||!NativePlayerPose::World(identity.geometry,read,next.carrier)||
       !read(identity.controller+0x14,&next.nativeLocal,sizeof(next.nativeLocal))||
       !read(identity.gunObject+4,&geometry,4)||!read(geometry+0x60,&owner,4)||owner!=identity.gunObject||
       !NativePlayerPose::World(geometry,read,gun)||!read(identity.gunObject+4,&afterGeometry,4)||afterGeometry!=geometry||
       !read(geometry+0x60,&owner,4)||owner!=identity.gunObject||!Same(identity,SnapshotIdentity(image,identity.player,read)))return false;
    const float n=Dot(next.nativeLocal,next.nativeLocal);if(!Finite(next.nativeLocal)||!std::isfinite(n)||n<.5f||n>1.5f)return false;
    next.nativeLocal=Scale(next.nativeLocal,1.f/std::sqrt(n));next.origin=gun.position;output=next;return true;
}
inline float Wrapped(float angle){constexpr float pi=3.14159265358979323846f;return std::remainder(angle,2*pi);}
struct RelativeAim {
    bool valid=false;std::uint32_t epoch=0,recenter=0,tick=0,inputTick=0;
    float yaw=0,pitch=0,handYaw=0,handPitch=0;
    void Reset(){*this={};}
    bool Select(const MountPose& pose,NativeCameraMath::Quat physical,std::uint32_t nextEpoch,std::uint32_t nextRecenter,
                std::uint32_t now,std::uint32_t sampleTick,float stickX,float stickY,float units,NativeCameraMath::Vec3& output){
        using namespace NativeCameraMath;Quat normalized;
        if(!ValidatePose(pose.carrier,.05f)||!Finite(pose.origin)||!Finite(pose.nativeLocal)||!nextEpoch||!now||!sampleTick||
           std::int32_t(now-sampleTick)<0||now-sampleTick>50||!NormalizeQuaternion(physical,normalized)||
           !std::isfinite(stickX)||!std::isfinite(stickY)||std::fabs(stickX)>1||std::fabs(stickY)>1||!std::isfinite(units)||units<1||units>1000)return false;
        const bool keep=valid&&epoch==nextEpoch&&recenter==nextRecenter&&std::int32_t(now-tick)>=0;
        RelativeAim next=*this;float dt=0;
        if(!keep){next={};next.yaw=std::atan2(pose.nativeLocal.x,pose.nativeLocal.z);next.pitch=std::atan2(pose.nativeLocal.y,std::hypot(pose.nativeLocal.x,pose.nativeLocal.z));}
        // A rendering/simulation stall does not replace the exact mount's
        // local aim with the native third-person target. Fresh current input
        // may resume it, without integrating the unobserved interval.
        else if(now-tick<=150)dt=static_cast<float>(now-tick>50?50:now-tick)/1000.f;
        constexpr float rate=52.f*3.14159265358979323846f/180.f;
        next.yaw=Wrapped(next.yaw+stickX*rate*dt);next.pitch=std::fmax(-1.50f,std::fmin(1.50f,next.pitch+stickY*rate*dt));
        const Vec3 local{std::sin(next.yaw)*std::cos(next.pitch),std::sin(next.pitch),std::cos(next.yaw)*std::cos(next.pitch)};
        const auto target=Add(pose.origin,Scale(NativePlayerPose::Transform(pose.carrier.basis,local),100.f*units));
        if(!Finite(target))return false;
        next.valid=true;next.epoch=nextEpoch;next.recenter=nextRecenter;next.tick=now;next.inputTick=sampleTick;
        *this=next;output=target;return true;
    }
};
template<class Reader>bool Verify(std::uintptr_t image,Reader read) {
    const auto match=[&](std::uintptr_t rva,const auto& expected){std::array<unsigned char,sizeof(expected)> actual{};
        return read(image+rva,actual.data(),actual.size())&&!std::memcmp(actual.data(),expected,actual.size());};
    const unsigned char update[]={0x55,0x8B,0xEC,0x83,0xE4,0xF0,0x6A,0xFF};
    const unsigned char setter[]={0x55,0x8B,0xEC,0x83,0xEC,0x0C,0x8B,0x45,8,0xD9,0,0x56};
    const unsigned char detectorCall[]={0xE8,0x48,0xE1,0xFF,0xFF};
    const unsigned char pointCall[]={0x8B,0x8E,0x10,0x0B,0,0,0x8D,0x44,0x24,0x44,0x50,0xE8,0x91,0xD8,0x13,0};
    const unsigned char ownership[]={0x89,0x42,0x4C,0x8B,0x4E,0x64,0x89,0xB9,0xF0,0,0,0,0x8B,0x56,0x64,0x89,0x57,0x40};
    const unsigned char vehicleVector[]={0x8B,0x8E,0x3C,8,0,0,0x2B,0x8E,0x38,8,0,0};
    const unsigned char constructorStore[]={0xC7,0x06};
    const unsigned char nativeLocalDirection[]={0x8B,0x4E,0x4C,0x57,0xE8,0xD1,0xCE,0xDB,0xFF};
    const unsigned char manual[]={0x88,0x58,0x44,0x8B,0x8E,0x10,0x0B,0,0,0x56,0xE8,0x26,0x9E,0x13,0};
    const unsigned char nativeManualTarget[]={0xD9,0x45,0xF4,0xD9,0x5E,0x2C,0xD9,0x45,0xF8,0xD9,0x5E,0x30,0xD9,0x45,0xFC,0xD9,0x5E,0x34};
    const unsigned char nativeManualFire[]={0xD9,0x46,0x30,0x8B,0x45,8,0xD9,0x5D,0xFC,0xD9,0x46,0x34,0xD9,0x5D,0xF8,0xD9,0x46,0x2C};
    const unsigned char nativeMuzzle[]={0x8B,0x4E,0x20,0x8B,1,0x8B,0x80,0x98,0,0,0,0x8D,0x96,0xB0,0,0,0,0x52,0x8D,0x96,0xBC,0,0,0,0x52,0xFF,0xD0};
    std::uint32_t updateSlot=0,fixedType=0,carType=0,fixedVtable=0,carVtable=0;
    return match(PlayerUpdate,update)&&match(PointSetter,setter)&&match(0x1FD143,detectorCall)&&match(0x1FD56F,pointCall)&&
        match(0x2B5328,ownership)&&match(0x2DF0F8,ownership)&&match(0x2CFCA6,vehicleVector)&&match(0x2007AB,manual)&&match(0x33B4D6,nativeLocalDirection)&&
        match(0x33AE62,nativeManualTarget)&&match(0x33A937,nativeManualFire)&&match(0x33BD63,nativeMuzzle)&&
        match(0x2B728C,constructorStore)&&read(image+0x2B728E,&fixedVtable,4)&&fixedVtable==image+0x57F7EC&&
        match(0x2D3556,constructorStore)&&read(image+0x2D3558,&carVtable,4)&&carVtable==image+0x582D14&&
        read(image+0x57F7EC+0x30,&fixedType,4)&&fixedType==image+0x2B7380&&
        read(image+0x582D14+0x30,&carType,4)&&carType==image+0x2D3840&&
        read(image+0x56582C+0x160,&updateSlot,sizeof(updateSlot))&&updateSlot==image+PlayerUpdate;
}
using LogCallback=void(*)(const char*);
bool Install(LogCallback log);
// Explicit fallback: preserve the game's own mouse target setter while VR
// camera rendering continues. Switching modes retires old VR steering state.
void SetMouseAim(bool enabled);
struct ReticleAim {
    std::uintptr_t player=0,controller=0,entity=0,gunObject=0;
    NativeCameraMath::Vec3 origin{},target{};
    std::uint32_t tick=0,recenterAck=0;
};
// Draw-only snapshot of the actual native manual MG target (controller+2C),
// observed after the real setter. It never uses a hand ray or issues Fire/Ray.
bool SnapshotReticle(std::uintptr_t expectedPlayer,float unitsPerMetre,ReticleAim& output);
// A single simulation observer shares the already verified PlayerUpdate
// detour. The exact callback sampled at entry receives its matching exit,
// including C++ unwinding; it cannot acquire the MG steering capability.
enum class UpdatePhase { Before,After,Aborted };
using UpdateObserver=void(*)(UpdatePhase,std::uintptr_t);
bool SetUpdateObserver(UpdateObserver observer);
// Only the existing post-detector NativeCover simulation observer may seed a
// one-shot target, inside this same native player's full update call.
void SimulationTick(std::uintptr_t player);
struct Stats {bool installed=false,mouseAim=false;std::uint64_t updates=0,seeds=0,seedRejected=0,setterCalls=0,applied=0,rejected=0,foreignCalls=0;
    std::uint64_t mouseNativeTargets=0,reticleSamples=0,reticleReads=0,reticleRejected=0;
    std::uint32_t thread=0,ageMs=0,identityStage=0,seedStage=0;std::uintptr_t player=0,controller=0;NativeCameraMath::Vec3 target{};
    IdentityProbe identityProbe{};
    std::uint32_t controlEpoch=0,physicalTick=0,stickTick=0;float localYaw=0,localPitch=0,stickX=0,stickY=0;
    NativeCameraMath::Vec3 carrierForward{};};
Stats Snapshot();
}
