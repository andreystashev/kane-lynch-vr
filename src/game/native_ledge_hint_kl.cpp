#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "native_ledge_hint_kl.hpp"
#include "native_cover_kl.hpp"
#include "native_camera_kl.hpp"
#include "vr_bridge.hpp"
#include "profile.hpp"
#include <atomic>
namespace NativeLedgeHintKL {namespace {
static_assert(sizeof(void*)==4,"native Climb hint is x86");
std::uintptr_t image=0;std::atomic<bool> installed{false};std::atomic<std::uint64_t> probes{0},hits{0},rejected{0};
std::atomic<std::uint32_t> contextTick{0},contextEpoch{0};
struct Result {std::uintptr_t player=0,movement=0;std::uint32_t tick=0,epoch=0,recenter=0;bool hit=false;};
SRWLOCK lock=SRWLOCK_INIT;Result result;thread_local std::uint32_t previousTick=0;
bool Read(std::uintptr_t at,void* out,std::size_t n){SIZE_T got=0;return NativeActionKL::Address(at,n)&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),out,n,&got)&&got==n;}
void Clear(){AcquireSRWLockExclusive(&lock);result={};ReleaseSRWLockExclusive(&lock);}
bool Gameplay(std::uintptr_t player,std::uint32_t epoch,std::uint32_t now){const auto tick=contextTick.load();const auto a=NativeActionKL::Current();
    return installed&&(epoch&1)&&contextEpoch==epoch&&tick&&std::int32_t(now-tick)>=0&&now-tick<=250&&
        VRBridge::Ready()&&!NativeCameraKL::CinematicActive()&&!VRBridge::TeamHudVisible()&&!VRBridge::WeaponMenuVisible()&&
        VRBridge::ReadSecondaryFire().valid&&a.valid&&a.player==player&&!a.action&&
        (a.movementKind==NativeActionKL::Movement::Stand||a.movementKind==NativeActionKL::Movement::BaseMove);}
}
bool Install(LogCallback log){
    if(installed)return true;wchar_t path[MAX_PATH]{};char hash[65]{};const auto n=GetModuleFileNameW(nullptr,path,MAX_PATH);
    if(!n||n>=MAX_PATH||!HashFile(path,hash)||!IsSupportedExecutableHash(hash))return false;
    image=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if(!Verify(image,Read)){if(log)log("KL1 passive Climb proof differs; near-ledge hint disabled.");return false;}
    installed=true;if(log)log("KL1 near-ledge hold-B hint queries exact native Climb geometry on a private registered-class copy; no native input or movement transition.");return true;
}
void SetGameplayContext(bool gameplay){auto before=contextEpoch.load();while(bool(before&1)!=gameplay){const auto next=((before+2u)&~1u)|(gameplay?1u:0u);if(contextEpoch.compare_exchange_weak(before,next))break;}
    contextTick=gameplay?GetTickCount():0;if(!gameplay)Clear();}
void SimulationTick(std::uintptr_t player){
    if(!installed||!NativeCoverKL::SimulationCallbackActive(player))return;const auto now=GetTickCount();const auto epoch=contextEpoch.load();
    if(!Gameplay(player,epoch,now)){Clear();return;}
    if(previousTick&&std::int32_t(now-previousTick)>=0&&now-previousTick<80)return;previousTick=now;Clear();
    const auto probe=Capture(image,Read);if(!probe.valid||probe.action.player!=player){++rejected;return;}
    const auto ack=static_cast<std::uint32_t>(VRBridge::RecenterAck());auto scratch=probe.native;scratch[0xE8]=1;++probes;
    const bool hit=NativeCoverKL::ProbePrivateClimb(player,scratch.data());
    const auto after=Capture(image,Read);
    if(!after.valid||after.climb!=probe.climb||after.action.movement!=probe.action.movement||after.native!=probe.native||after.clip!=probe.clip||
       after.backend!=probe.backend||!Gameplay(player,epoch,GetTickCount())||static_cast<std::uint32_t>(VRBridge::RecenterAck())!=ack){++rejected;return;}
    if(hit)++hits;AcquireSRWLockExclusive(&lock);result={player,probe.action.movement,now,epoch,ack,hit};ReleaseSRWLockExclusive(&lock);
}
bool Available(std::uintptr_t player){Result copy;AcquireSRWLockShared(&lock);copy=result;ReleaseSRWLockShared(&lock);const auto now=GetTickCount();const auto a=NativeActionKL::Current();
    return copy.hit&&copy.player==player&&copy.tick&&std::int32_t(now-copy.tick)>=0&&now-copy.tick<=150&&
        Gameplay(player,copy.epoch,now)&&a.movement==copy.movement&&static_cast<std::uint32_t>(VRBridge::RecenterAck())==copy.recenter;}
Stats Snapshot(){Result copy;AcquireSRWLockShared(&lock);copy=result;ReleaseSRWLockShared(&lock);return {installed,copy.player&&Available(copy.player),probes,hits,rejected};}
}
