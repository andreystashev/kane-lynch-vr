#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "native_mounted_aim_kl.hpp"
#include "native_cover_kl.hpp"
#include "native_camera_kl.hpp"
#include "vr_bridge.hpp"
#include "profile.hpp"
#include "hands_math.hpp"
#include "MinHook.h"
#include <atomic>

namespace NativeMountedAimKL {namespace {
using Update=void(__thiscall*)(void*);
using Point=void(__thiscall*)(void*,const NativeCameraMath::Vec3*);
Update update=nullptr;Point point=nullptr;
std::uintptr_t image=0;
std::atomic<bool> installed{false};
std::atomic<bool> mouseAim{false};
std::atomic<std::uint64_t> mouseNativeTargets{0},reticleSamples{0},reticleReads{0},reticleRejected{0};
std::atomic<UpdateObserver> updateObserver{nullptr};
std::atomic<std::uint64_t> updates{0},seeds{0},seedRejected{0},setterCalls{0},applied{0},rejected{0},foreignCalls{0};
std::atomic<std::uint32_t> lastThread{0},lastAge{0};
std::atomic<unsigned> identityStage{0},seedStage{0};
std::atomic<std::uint32_t> probeRecord{0},probeController{0},probeEntity{0},probeVtable{0},probeRecordsOffset{0};
std::atomic<std::uintptr_t> lastPlayer{0},lastController{0};
std::atomic<float> lastX{0},lastY{0},lastZ{0};
std::atomic<std::uint32_t> lastControlEpoch{0},lastPhysicalTick{0},lastStickTick{0};
std::atomic<float> lastLocalYaw{0},lastLocalPitch{0},lastStickX{0},lastStickY{0},lastCarrierX{0},lastCarrierY{0},lastCarrierZ{0};
struct Capability {bool available=false;Identity identity;NativeHandsKL::ControllerAim aim;VehicleControlIPC::Certificate receiver{};std::uint32_t tick=0;};
struct UpdateScope {std::uintptr_t player=0;Capability capability;};
thread_local UpdateScope active;
SRWLOCK controlLock=SRWLOCK_INIT;
Identity controlIdentity;VehicleControlIPC::Certificate receiver;
struct ReticleSample {Identity identity;NativeCameraMath::Vec3 target{};std::uint32_t tick=0,recenter=0;};
SRWLOCK reticleLock=SRWLOCK_INIT;ReticleSample reticleSample;
std::uint32_t receiverEpoch=0;RelativeAim relative;
struct Physical {NativeCameraMath::Quat orientation{};std::uint32_t tick=0,recenter=0;};
bool ReadPhysical(std::uint32_t now,std::uint32_t ack,Physical& output){
    thread_local HandsIPC::Consumer input;HandsIPC::Frame frame;
    if(!input.Open(GetCurrentProcessId())||!input.Read(frame,now)||!HandsMath::ValidFrame(frame,now,ack)||
       !HandsMath::ValidPose(frame.hands[1])||now-frame.tick>50)return false;
    output={{frame.hands[1].orientation[0],frame.hands[1].orientation[1],frame.hands[1].orientation[2],frame.hands[1].orientation[3]},frame.tick,frame.recenterAck};return true;
}
void Retire(bool identity=true){
    AcquireSRWLockExclusive(&controlLock);relative.Reset();if(identity){controlIdentity={};receiver={};}ReleaseSRWLockExclusive(&controlLock);
    if(identity)VRBridge::SetMountedReceiver({});
}
VehicleControlIPC::Certificate Receiver(const Identity& identity,std::uint32_t now,std::uint32_t ack){
    AcquireSRWLockExclusive(&controlLock);
    if(!Same(controlIdentity,identity)||receiver.recenter!=ack){relative.Reset();controlIdentity=identity;if(!++receiverEpoch)++receiverEpoch;
        receiver={static_cast<std::uint32_t>(identity.player),static_cast<std::uint32_t>(identity.movement),static_cast<std::uint32_t>(identity.controller),
            static_cast<std::uint32_t>(identity.entity),static_cast<std::uint32_t>(identity.record),receiverEpoch,ack,now};}
    receiver.tick=now;const auto copy=receiver;ReleaseSRWLockExclusive(&controlLock);VRBridge::SetMountedReceiver(copy);return copy;
}
bool Read(std::uintptr_t at,void* out,std::size_t size) {
    SIZE_T got=0;return NativeActionKL::Address(at,size)&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),out,size,&got)&&got==size;
}
bool Gameplay(std::uintptr_t player) {
    std::uint8_t paused[2]{1,1},blocked=1;std::uint32_t controller=0;
    return installed&&player&&VRBridge::Ready()&&!NativeCameraKL::CinematicActive()&&
        !VRBridge::TeamHudVisible()&&!VRBridge::WeaponMenuVisible()&&
        // This filtered mailbox certifies fresh host Game context even when
        // its secondary trigger is released. It is never a fire request here.
        VRBridge::ReadSecondaryFire().valid&&
        Read(image+0x8883A4,paused,sizeof(paused))&&!paused[0]&&!paused[1]&&
        Read(player+0x9B8,&controller,sizeof(controller))&&Read(controller+0x18,&blocked,sizeof(blocked))&&!blocked;
}
bool ReticleGameplay(std::uintptr_t player) {
    std::uint8_t paused[2]{1,1},blocked=1;std::uint32_t controller=0;
    return installed&&player&&VRBridge::Ready()&&!NativeCameraKL::CinematicActive()&&
        !VRBridge::TeamHudVisible()&&!VRBridge::WeaponMenuVisible()&&
        Read(image+0x8883A4,paused,sizeof(paused))&&!paused[0]&&!paused[1]&&
        Read(player+0x9B8,&controller,sizeof(controller))&&Read(controller+0x18,&blocked,sizeof(blocked))&&!blocked;
}
void ObserveReticleTarget(void* object,std::uintptr_t caller) {
    const auto player=active.player;
    if(caller!=image+PointCaller||!player||!ReticleGameplay(player))return;
    const auto identity=SnapshotIdentity(image,player,Read);NativeCameraMath::Vec3 target,after;
    const auto now=GetTickCount();const auto ack=static_cast<std::uint32_t>(VRBridge::RecenterAck());
    if(!Eligible(identity)||identity.controller!=reinterpret_cast<std::uintptr_t>(object)||
       !Read(identity.controller+0x2C,&target,sizeof(target))||!NativeCameraMath::Finite(target)||
       !Same(identity,SnapshotIdentity(image,player,Read))||!Read(identity.controller+0x2C,&after,sizeof(after))||
       std::memcmp(&target,&after,sizeof(target))||ack!=static_cast<std::uint32_t>(VRBridge::RecenterAck()))return;
    AcquireSRWLockExclusive(&reticleLock);reticleSample={identity,target,now,ack};ReleaseSRWLockExclusive(&reticleLock);++reticleSamples;
}
void __fastcall UpdateHook(void* object,void*) {
    ++updates;
    const auto previous=active;active={};active.player=reinterpret_cast<std::uintptr_t>(object);
    // A re-entrant update receives its own capability; neither a nested actor
    // nor any later frame can borrow the outer player's post-detector seed.
    struct Restore {UpdateScope previous;~Restore(){active=previous;}} restore{previous};
    const auto observer=updateObserver.load(std::memory_order_acquire);
    struct Observe {
        UpdateObserver callback;std::uintptr_t player;bool completed=false;
        ~Observe(){if(callback)callback(completed?UpdatePhase::After:UpdatePhase::Aborted,player);}
    } observe{observer,reinterpret_cast<std::uintptr_t>(object)};
    if(observer)observer(UpdatePhase::Before,observe.player);
    update(object);
    observe.completed=true;
}
bool Substitute(void* object,std::uintptr_t caller,NativeCameraMath::Vec3& target) {
    ++setterCalls;
    if(caller!=image+PointCaller){++foreignCalls;return false;}
    const auto capability=active.capability;active.capability={}; // consume on the first verified native setter attempt
    const auto player=active.player;const auto now=static_cast<std::uint32_t>(GetTickCount());
    const auto reject=[](){++rejected;return false;};
    const auto ack=static_cast<std::uint32_t>(VRBridge::RecenterAck());
    if(mouseAim.load(std::memory_order_relaxed)||!capability.available||!player||capability.identity.player!=player||
       reinterpret_cast<std::uintptr_t>(object)!=capability.identity.controller||
       std::int32_t(now-capability.tick)<0||now-capability.tick>50||!Gameplay(player)||
       !Fresh(capability.aim,player,now,ack))return reject();
    const auto current=SnapshotIdentity(image,player,Read);NativeHandsKL::ControllerAim aim;Physical physical;MountPose pose;
    const auto stick=VRBridge::ReadMountedInput(capability.receiver);
    if(!Eligible(current)||!Same(capability.identity,current)||!NativeHandsKL::SnapshotControllerAim(1,aim)||
       !Fresh(aim,player,now,ack)||aim.pairId!=capability.aim.pairId||aim.recenterAck!=capability.aim.recenterAck||
       !ReadPhysical(now,ack,physical)||!stick.valid||!SnapshotMountPose(image,current,Read,pose)||
       !Gameplay(player)||!Same(current,SnapshotIdentity(image,player,Read))||
       static_cast<std::uint32_t>(VRBridge::RecenterAck())!=ack||!Fresh(aim,player,GetTickCount(),ack))return reject();
    AcquireSRWLockExclusive(&controlLock);auto proposed=relative;
    const bool selected=VehicleControlIPC::Same(receiver,capability.receiver)&&Same(controlIdentity,current)&&
        proposed.Select(pose,physical.orientation,capability.receiver.epoch,ack,now,physical.tick,stick.x,stick.y,aim.unitsPerMetre,target);
    if(selected)relative=proposed;ReleaseSRWLockExclusive(&controlLock);if(!selected||mouseAim.load(std::memory_order_relaxed))return reject();
    lastControlEpoch=capability.receiver.epoch;lastPhysicalTick=physical.tick;lastStickTick=stick.tick;
    lastLocalYaw=proposed.yaw;lastLocalPitch=proposed.pitch;lastStickX=stick.x;lastStickY=stick.y;
    lastCarrierX=pose.carrier.basis.forward.x;lastCarrierY=pose.carrier.basis.forward.y;lastCarrierZ=pose.carrier.basis.forward.z;
    lastThread=GetCurrentThreadId();lastAge=now-aim.tick;lastPlayer=player;lastController=current.controller;
    lastX=target.x;lastY=target.y;lastZ=target.z;++applied;return true;
}
void __fastcall PointHook(void* object,void*,const NativeCameraMath::Vec3* original) {
    NativeCameraMath::Vec3 target;const auto caller=reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
    if(mouseAim.load(std::memory_order_relaxed)){
        active.capability={};point(object,original);if(caller==image+PointCaller)++mouseNativeTargets;
    }else if(original&&Substitute(object,caller,target))point(object,&target);
    else point(object,original);
    if(original)ObserveReticleTarget(object,caller);
}
}
void SetMouseAim(bool enabled){
    if(mouseAim.exchange(enabled,std::memory_order_acq_rel)==enabled)return;
    Retire();active.capability={};AcquireSRWLockExclusive(&reticleLock);reticleSample={};ReleaseSRWLockExclusive(&reticleLock);
}
bool SnapshotReticle(std::uintptr_t player,float units,ReticleAim& output){
    const auto reject=[](){++reticleRejected;return false;};
    if(!player||!std::isfinite(units)||units<1||units>1000||!ReticleGameplay(player))return reject();
    ReticleSample sample;AcquireSRWLockShared(&reticleLock);sample=reticleSample;ReleaseSRWLockShared(&reticleLock);
    const auto now=GetTickCount();const auto ack=static_cast<std::uint32_t>(VRBridge::RecenterAck());
    NativeCameraMath::Vec3 target,origin,after;const auto identity=SnapshotIdentity(image,player,Read);
    if(!sample.tick||std::int32_t(now-sample.tick)<0||now-sample.tick>150||sample.recenter!=ack||
       !Eligible(identity)||!Same(sample.identity,identity)||!Read(identity.controller+0x2C,&target,sizeof(target))||
       std::memcmp(&target,&sample.target,sizeof(target))||!Read(identity.controller+0xB0,&origin,sizeof(origin))||
       !NativeCameraMath::Finite(origin)||!NativeCameraMath::Finite(target)||
       !Same(identity,SnapshotIdentity(image,player,Read))||!Read(identity.controller+0x2C,&after,sizeof(after))||
       std::memcmp(&target,&after,sizeof(target))||!ReticleGameplay(player)||ack!=static_cast<std::uint32_t>(VRBridge::RecenterAck()))return reject();
    const auto delta=NativeCameraMath::Subtract(target,origin);const float squared=NativeCameraMath::Dot(delta,delta);
    if(!std::isfinite(squared)||squared<units*units*.0001f)return reject();
    output={player,identity.controller,identity.entity,identity.gunObject,origin,target,sample.tick,ack};++reticleReads;return true;
}
bool Install(LogCallback log) {
    if(installed)return true;
    wchar_t path[MAX_PATH]{};char digest[65]{};const auto n=GetModuleFileNameW(nullptr,path,MAX_PATH);
    if(!n||n>=MAX_PATH||!HashFile(path,digest)||!IsSupportedExecutableHash(digest))return false;
    image=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if(!Verify(image,Read)){if(log)log("KL1 mounted aim: native update/setter/ownership signatures differ; native controls retained.");return false;}
    void* destinations[]={reinterpret_cast<void*>(image+PlayerUpdate),reinterpret_cast<void*>(image+PointSetter)};
    void* hooks[]={reinterpret_cast<void*>(UpdateHook),reinterpret_cast<void*>(PointHook)};
    void** originals[]={reinterpret_cast<void**>(&update),reinterpret_cast<void**>(&point)};
    unsigned created=0;for(;created<2;++created)if(MH_CreateHook(destinations[created],hooks[created],originals[created])!=MH_OK)break;
    bool okay=created==2;
    if(okay)for(unsigned i=0;i<2;++i)if(MH_QueueEnableHook(destinations[i])!=MH_OK){okay=false;break;}
    if(okay)okay=MH_ApplyQueued()==MH_OK;
    if(!okay){for(unsigned i=0;i<created;++i){MH_DisableHook(destinations[i]);MH_RemoveHook(destinations[i]);}return false;}
    installed=true;
    if(log)log("KL1 mounted aim: certificate-bound stick only -> current mount-local target; native limits/fire retained.");
    return true;
}
bool SetUpdateObserver(UpdateObserver observer) {
    if(!installed)return false;
    auto expected=static_cast<UpdateObserver>(nullptr);
    if(!observer){updateObserver.store(nullptr,std::memory_order_release);return true;}
    return updateObserver.compare_exchange_strong(expected,observer,std::memory_order_acq_rel)||expected==observer;
}
void SimulationTick(std::uintptr_t player) {
    if(!installed||!NativeCoverKL::SimulationCallbackActive(player)||active.player!=player)return;
    active.capability={};const auto now=static_cast<std::uint32_t>(GetTickCount());
    if(mouseAim.load(std::memory_order_relaxed)){Retire();seedStage=9;return;}
    const auto ack=static_cast<std::uint32_t>(VRBridge::RecenterAck());
    unsigned proof=0;IdentityProbe probe;const auto identity=SnapshotIdentity(image,player,Read,&proof,&probe);identityStage=proof;
    probeRecord=probe.record;probeController=probe.controller;probeEntity=probe.entity;probeVtable=probe.vtable;probeRecordsOffset=probe.recordsOffset;
    NativeHandsKL::ControllerAim aim;Physical physical;MountPose pose;
    if(!Gameplay(player)){Retire();seedStage=1;++seedRejected;return;}
    if(!Eligible(identity)){Retire();seedStage=2;++seedRejected;return;}
    if(!NativeHandsKL::SnapshotControllerAim(1,aim)){Retire();seedStage=3;++seedRejected;return;}
    if(!Fresh(aim,player,now,ack)){Retire();seedStage=4;++seedRejected;return;}
    const auto certificate=Receiver(identity,now,ack);
    if(!SnapshotMountPose(image,identity,Read,pose)){Retire();seedStage=6;++seedRejected;return;}
    if(!ReadPhysical(now,ack,physical)){Retire(false);seedStage=7;++seedRejected;return;}
    if(!VRBridge::ReadMountedInput(certificate).valid){Retire(false);seedStage=8;++seedRejected;return;}
    if(
       !Gameplay(player)||!Same(identity,SnapshotIdentity(image,player,Read))||
       static_cast<std::uint32_t>(VRBridge::RecenterAck())!=ack||!Fresh(aim,player,GetTickCount(),ack)){Retire();seedStage=5;++seedRejected;return;}
    active.capability={true,identity,aim,certificate,now};seedStage=0;++seeds;
}
Stats Snapshot() {
    Stats out;out.installed=installed;out.mouseAim=mouseAim;out.updates=updates;out.seeds=seeds;out.seedRejected=seedRejected;out.setterCalls=setterCalls;
    out.mouseNativeTargets=mouseNativeTargets;out.reticleSamples=reticleSamples;out.reticleReads=reticleReads;out.reticleRejected=reticleRejected;
    out.applied=applied;out.rejected=rejected;out.foreignCalls=foreignCalls;out.thread=lastThread;out.ageMs=lastAge;
    out.player=lastPlayer;out.controller=lastController;out.target={lastX,lastY,lastZ};out.identityStage=identityStage;out.seedStage=seedStage;
    out.identityProbe={probeRecord,probeController,probeEntity,probeVtable,probeRecordsOffset};
    out.controlEpoch=lastControlEpoch;out.physicalTick=lastPhysicalTick;out.stickTick=lastStickTick;out.localYaw=lastLocalYaw;out.localPitch=lastLocalPitch;
    out.stickX=lastStickX;out.stickY=lastStickY;out.carrierForward={lastCarrierX,lastCarrierY,lastCarrierZ};return out;
}
}
