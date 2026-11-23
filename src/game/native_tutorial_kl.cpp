#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "native_tutorial_kl.hpp"
#include "native_cover_kl.hpp"
#include "native_camera_kl.hpp"
#include "vr_bridge.hpp"
#include "profile.hpp"
#include <atomic>

namespace NativeTutorialKL {namespace {
std::uintptr_t image=0;using Signal=void(__thiscall*)(void*,unsigned);Signal signal=nullptr;
std::atomic<bool> installed{false};std::atomic<std::uint32_t> contextEpoch{0},contextTick{0},thread{0};
std::atomic<std::uint64_t> ticks{0},tokens{0},shotCredits{0},timeCredits{0},rejected{0},ambiguous{0};
std::atomic<unsigned> currentStage{StageCount},currentEvent{0},elapsed{0},listed{0},named{0},active{0};
std::atomic<std::uintptr_t> lastPlayer{0},lastMonitor{0},lastRoot{0};
struct Shot {Stage stage{};std::uint32_t tick=0;std::uint64_t serial=0;};
SRWLOCK shotLock=SRWLOCK_INIT;Shot pending;std::uint64_t shotSerial=0,consumed=0;
Policy policy;std::uint32_t policyEpoch=0;
bool Read(std::uintptr_t at,void* output,std::size_t size){SIZE_T got=0;return NativeActionKL::Address(at,size)&&
    ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),output,size,&got)&&got==size;}
bool Gameplay(std::uintptr_t player,std::uint32_t epoch,std::uint32_t now){
    const auto tick=contextTick.load();std::uint8_t pause[2]{1,1},blocked=1;std::uint32_t controller=0;
    const auto action=NativeActionKL::Current();
    return installed&&player&&(epoch&1)&&epoch==contextEpoch&&tick&&std::int32_t(now-tick)>=0&&now-tick<=250&&
        VRBridge::Ready()&&!NativeCameraKL::CinematicActive()&&!VRBridge::TeamHudVisible()&&!VRBridge::WeaponMenuVisible()&&
        VRBridge::ReadSecondaryFire().valid&&action.valid&&action.player==player&&
        (action.movementKind==NativeActionKL::Movement::Stand||action.movementKind==NativeActionKL::Movement::BaseMove||action.movementKind==NativeActionKL::Movement::Cover)&&
        Read(image+0x8883A4,pause,sizeof(pause))&&!pause[0]&&!pause[1]&&
        Read(player+0x9B8,&controller,4)&&Read(controller+0x18,&blocked,1)&&!blocked;
}
}
bool Install(LogCallback log){
    if(installed)return true;wchar_t path[MAX_PATH]{};char hash[65]{};const auto n=GetModuleFileNameW(nullptr,path,MAX_PATH);
    if(!n||n>=MAX_PATH||!HashFile(path,hash)||!IsSupportedExecutableHash(hash))return false;
    image=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if(!Verify(image,Read)){if(log)log("KL1 tutorial: native monitor/name/selection signatures differ; tutorial assist disabled.");return false;}
    signal=reinterpret_cast<Signal>(image+Dispatch);installed=true;
    if(log)log("KL1 tutorial: native M02 triggers retained; exact Kane cover monitors also accept a real player shot or 3 active seconds.");return true;
}
void SetGameplayContext(bool gameplay){
    auto before=contextEpoch.load();while(bool(before&1)!=gameplay){const auto next=((before+2u)&~1u)|(gameplay?1u:0u);if(contextEpoch.compare_exchange_weak(before,next))break;}
    contextTick=gameplay?GetTickCount():0;
    if(!gameplay){AcquireSRWLockExclusive(&shotLock);pending={};ReleaseSRWLockExclusive(&shotLock);}
}
void NotifyShot(std::uintptr_t player){
    if(!installed||GetCurrentThreadId()!=thread)return;
    const auto now=GetTickCount();const auto epoch=contextEpoch.load();if(!Gameplay(player,epoch,now))return;
    const auto stage=NativeTutorialKL::Snapshot(image,player,Read);if(!stage.valid||!Gameplay(player,epoch,GetTickCount()))return;
    AcquireSRWLockExclusive(&shotLock);pending={stage,now,++shotSerial};ReleaseSRWLockExclusive(&shotLock);++tokens;
}
void SimulationTick(std::uintptr_t player){
    if(!installed||!NativeCoverKL::SimulationCallbackActive(player))return;++ticks;
    const auto epoch=contextEpoch.load();const auto now=GetTickCount();
    if(epoch!=policyEpoch){policy.Reset();policyEpoch=epoch;}
    if(!Gameplay(player,epoch,now)){policy.Reset();currentStage=StageCount;currentEvent=elapsed=0;++rejected;return;}
    thread=GetCurrentThreadId();Probe probe;const auto stage=NativeTutorialKL::Snapshot(image,player,Read,&probe);
    listed=probe.listed;named=probe.named;active=probe.active;
    if(!stage.valid){if(probe.active>1)++ambiguous;policy.Reset();currentStage=StageCount;currentEvent=elapsed=0;return;}
    lastPlayer=player;lastMonitor=stage.monitor;lastRoot=stage.root;currentStage=stage.index;currentEvent=stage.event;
    Shot shot;AcquireSRWLockShared(&shotLock);shot=pending;ReleaseSRWLockShared(&shotLock);
    const bool hit=shot.serial&&shot.serial!=consumed&&Same(shot.stage,stage)&&shot.tick&&std::int32_t(now-shot.tick)>=0&&now-shot.tick<=500;
    auto proposed=policy;const auto reason=proposed.Update(stage,now,hit,true);elapsed=proposed.elapsed;
    if(reason==Reason::None){policy=proposed;return;}
    // Source/mission/pause changes between deciding and executing cannot
    // continue a different script. The original handler owns dedup + outputs.
    if(!Gameplay(player,epoch,GetTickCount())||!Same(stage,NativeTutorialKL::Snapshot(image,player,Read))){policy.Reset();++rejected;return;}
    policy=proposed;if(hit)consumed=shot.serial;
    signal(reinterpret_cast<void*>(static_cast<std::uintptr_t>(stage.monitor)),stage.event);
    if(reason==Reason::Shot)++shotCredits;else ++timeCredits;
}
Stats Snapshot(){Stats out;out.installed=installed;out.ticks=ticks;out.shotTokens=tokens;out.shotCredits=shotCredits;out.timeCredits=timeCredits;
    out.rejected=rejected;out.ambiguous=ambiguous;out.stage=currentStage;out.event=currentEvent;out.elapsedMs=elapsed;out.listed=listed;out.named=named;out.active=active;
    out.player=lastPlayer;out.monitor=lastMonitor;out.root=lastRoot;return out;}
}
