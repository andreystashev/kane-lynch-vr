#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "native_cover_kl.hpp"
#include "native_camera_kl.hpp"
#include "native_ledge_kl.hpp"
#include "vr_bridge.hpp"
#include "MinHook.h"
#include <atomic>

namespace NativeCoverKL {
namespace {
std::atomic<std::uintptr_t> verifiedImage{0};
bool Read(std::uintptr_t at,void* output,std::size_t size) {
    SIZE_T copied=0;
    return NativeActionKL::Address(at,size)&&ReadProcessMemory(GetCurrentProcess(),
        reinterpret_cast<const void*>(at),output,size,&copied)&&copied==size;
}
template<std::size_t N> bool Match(std::uintptr_t image,std::uintptr_t rva,const unsigned char (&expected)[N]) {
    unsigned char actual[N]{};return Read(image+rva,actual,N)&&!std::memcmp(actual,expected,N);
}
using Detector=void(__thiscall*)(void*);
using Eligibility=bool(__thiscall*)(void*,std::uintptr_t,std::uintptr_t,std::uintptr_t);
using Notify=void(__cdecl*)(void*,unsigned);
Detector detector=nullptr;
Eligibility eligibility=nullptr;
Notify notify=nullptr;
std::atomic<bool> scriptInstalled{false};
std::atomic<std::uint64_t> candidates{0},events{0},blockedEntries{0};
std::atomic<std::uint64_t> heldEntryRequests{0},nativeEntryAllowed{0},nativeEntryRejected{0};
std::atomic<std::uint64_t> detectorCalls{0},rawFreshCandidates{0},invalidStates{0},ineligible{0},readFailures{0},activeCredits{0},inactiveUpdates{0};
float scriptUnits=100.f;
// Only the native gameplay thread touches this latch; OnTrackedFire merely
// reads it on that same thread. Other threads cannot emit script events.
std::atomic<DWORD> gameplayThread{0};
std::atomic<SimulationObserver> simulationObserver{nullptr};
thread_local std::uintptr_t simulationPlayer=0;
ScriptPolicy scriptPolicy;
using MovementIntent=bool(__thiscall*)(void*);
MovementIntent movementIntent=nullptr,climbLatch=nullptr;
std::atomic<bool> actionClimbInstalled{false};
// Low bit is Game; the remaining bits advance on every context transition.
std::atomic<std::uint64_t> actionClimbContext{0};
std::atomic<std::uint64_t> climbCalls{0},climbRequests{0},climbAssisted{0},climbRejected{0};
thread_local std::uintptr_t passiveClimbPlayer=0;
bool ReadClimbHold(std::uintptr_t player,bool& held) {
    const auto image=verifiedImage.load();
    const auto context=actionClimbContext.load();
    if(!actionClimbInstalled.load()||!(context&1u)||!VRBridge::Ready()||NativeCameraKL::CinematicActive()||
       VRBridge::TeamHudVisible()||VRBridge::WeaponMenuVisible()||
       !NativeLedgeKL::EligiblePosture(NativeActionKL::Current(),player))return false;
    const auto recenter=VRBridge::RecenterAck();
    std::uint32_t controller=0;std::uint8_t paused[2]{0xff,0xff},blocked=1;
    if(!Read(image+0x8883A4,paused,sizeof(paused))||paused[0]||paused[1]||
       !Read(player+0x9B8,&controller,sizeof(controller))||
       !Read(controller+0x18,&blocked,sizeof(blocked))||blocked)return false;
    held=VRBridge::ClimbHeld();
    return actionClimbContext.load()==context&&VRBridge::RecenterAck()==recenter;
}
bool __fastcall MovementIntentHook(void* actor,void*) {
    const auto image=verifiedImage.load();
    const auto caller=reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
    if(caller!=image+NativeLedgeKL::HelperReturnRva)return movementIntent(actor);
    const auto player=reinterpret_cast<std::uintptr_t>(actor);
    if(player==passiveClimbPlayer&&simulationPlayer==player)return true;
    ++climbCalls;
    const bool native=movementIntent(actor);
    bool held=false;
    if(!ReadClimbHold(player,held))return native;
    if(held){++climbRequests;if(!native)++climbAssisted;}
    else if(native)++climbRejected;
    return held;
}
bool __fastcall ClimbLatchHook(void* actor,void*) {
    const auto caller=reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
    bool held=false;
    // Native Climb arms its E8 latch on one update and tests geometry on the
    // next. Only this getter call substitutes B for the game's TapUse.
    if(caller==verifiedImage.load()+NativeLedgeKL::LatchReturnRva&&
       ReadClimbHold(reinterpret_cast<std::uintptr_t>(actor),held))return held;
    return climbLatch(actor);
}
enum class ScriptReject {Inactive,InvalidState,Ineligible};
bool VrGameplay(std::uintptr_t expected,State& output,ScriptReject* reason=nullptr) {
    const auto reject=[&](ScriptReject value){if(reason)*reason=value;return false;};
    if(!scriptInstalled||!VRBridge::Ready()||NativeCameraKL::CinematicActive())return reject(ScriptReject::Inactive);
    output=Current();
    if(!output.valid||output.player!=expected)return reject(ScriptReject::InvalidState);
    if(!output.enabled||output.controllerBlocked)return reject(ScriptReject::Ineligible);
    return true;
}
void Credit(std::uintptr_t player,unsigned event) {
    // Native PlayerMonitor dispatcher retains enabled/selected-player tests,
    // linked output lists and per-monitor dedup. No tutorial/save bit writes.
    notify(reinterpret_cast<void*>(player),event);++events;
}
void __fastcall DetectorHook(void* actor,void*) {
    ++detectorCalls;
    detector(actor);
    const auto player=reinterpret_cast<std::uintptr_t>(actor);State state;ScriptReject reason{};
    if(const auto observer=simulationObserver.load()) {
        const auto local=NativeActionKL::Current();
        if(local.valid&&local.player==player) {
            const auto previous=simulationPlayer;simulationPlayer=player;
            observer(player);simulationPlayer=previous;
        }
    }
    if(!VrGameplay(player,state,&reason)) {
        if(reason==ScriptReject::Inactive)++inactiveUpdates;
        else if(reason==ScriptReject::InvalidState)++invalidStates;
        else ++ineligible;
        // An unrelated actor cannot retire the local player's observation.
        if(scriptPolicy.proximity.player==player)scriptPolicy.Reset();
        return;
    }
    gameplayThread=GetCurrentThreadId();
    const auto action=NativeActionKL::Current();
    std::uint32_t geometry=0,input=0;NativeCameraMath::CameraPose root;
    if(!Read(player+0x400,&input,sizeof(input))) {++readFailures;scriptPolicy.Reset();return;}
    if(state.candidateSide&&!((state.actorFlags|input)&0x40))++rawFreshCandidates;
    if(!ScriptPolicy::Eligible(player,state,action,true)&&!ScriptPolicy::ActiveEligible(player,state,action,true)) {
        ++ineligible;scriptPolicy.Reset();return;
    }
    if(!state.active&&(!Read(player+4,&geometry,sizeof(geometry))||!NativePlayerPose::World(geometry,Read,root))) {
        ++readFailures;scriptPolicy.Reset();return;
    }
    const auto credit=scriptPolicy.Update(player,state,action,root.position,input,scriptUnits,true);
    if(!credit.stance)return;
    if(credit.freshCandidate)++candidates;
    if(state.active)++activeCredits;
    // Cover update 60C490/60C4A3 emits 4/3 for crouched/standing cover;
    // 60C2F5 emits 6/5 after cover aiming. Give the script that aim credit
    // automatically in VR, after stance, without native RMB/camera changes.
    // Retry on subsequent updates: the next tutorial monitor can be enabled
    // later than this notification, and native dispatch owns deduplication.
    Credit(player,credit.stance);
    Credit(player,credit.aim);
}
bool __fastcall EligibilityHook(void* cover,void*,std::uintptr_t a,std::uintptr_t b,std::uintptr_t c) {
    const auto at=reinterpret_cast<std::uintptr_t>(cover),image=verifiedImage.load();
    std::uint32_t vt=0,owner=0,specific=0,registered=0;
    if(Read(at,&vt,sizeof(vt))&&vt==image+0x566EAC&&Read(at+0x38,&owner,sizeof(owner))&&
       Read(at+0xDC,&specific,sizeof(specific))&&owner==specific&&VRBridge::Ready()&&!NativeCameraKL::CinematicActive()&&
       Read(owner+0x5F8+7*4,&registered,sizeof(registered))&&registered==at) {
        const auto action=NativeActionKL::Current();
        if(!action.valid||action.player!=owner||action.movement==at)return eligibility(cover,a,b,c);
        State state;
        if(VRBridge::CoverHeld()&&VrGameplay(owner,state)&&HeldEntryEligible(owner,state,action,true,true)) {
            ++heldEntryRequests;
            const bool allowed=eligibility(cover,a,b,c);
            if(allowed)++nativeEntryAllowed;else ++nativeEntryRejected;
            return allowed;
        }
        // A transient invalid proximity sample cannot opt the VR player in
        // to native cover. Entry requires the explicit hold and native checks.
        ++blockedEntries;return false;
    }
    return eligibility(cover,a,b,c);
}
}
bool InitializeVerifiedImage(std::uintptr_t image) {
    if(image&&verifiedImage.load()==image)return true;
    const unsigned char yawRead[]={0xD9,0x86,0x88,0x12,0x00,0x00};
    const unsigned char yawWrite[]={0xD9,0x9A,0x88,0x12,0x00,0x00};
    const unsigned char coverSide[]={0x8A,0x44,0x24,0x04,0x88,0x81,0xDD,0x12,0x00,0x00,0xC2,0x04,0x00};
    const unsigned char coverClass[]={0xC7,0x06,0xAC,0x6E,0x96,0x00};
    const unsigned char nativeCoverId[]={0x6A,0x07,0x56,0x8B,0xC8,0xE8};
    const unsigned char guideBranch[]={0x8B,0x48,0x24,0xE9,0x2D,0x8C,0xF0,0xFF};
    const unsigned char vectorGeometry[]={0x8B,0x49,0x04,0xE9,0xB8,0x1E,0xF3,0xFF};
    if(!image||!Match(image,0x1EF76E,yawRead)||!Match(image,0x234A9A,yawWrite)||
       !Match(image,0x1E8BF0,coverSide)||!Match(image,0x203C25,coverClass)||
       !Match(image,0x1EF36F,nativeCoverId)||!Match(image,0x1EF74B,guideBranch)||
       !Match(image,0xF8380,vectorGeometry)||!NativeActionKL::InitializeVerifiedImage(image))return false;
    verifiedImage=image;return true;
}
void SetSimulationObserver(SimulationObserver observer) {simulationObserver=observer;}
bool SimulationCallbackActive(std::uintptr_t player) {return player&&simulationPlayer==player;}
bool ProbePrivateClimb(std::uintptr_t player,void* scratch) {
    const auto image=verifiedImage.load();const auto state=NativeActionKL::Current();std::uint32_t vt=0,owner=0,id=0,source=0;
    const auto at=reinterpret_cast<std::uintptr_t>(scratch);
    if(!image||!actionClimbInstalled||passiveClimbPlayer||!SimulationCallbackActive(player)||!state.valid||state.player!=player||state.action||
       (state.movementKind!=NativeActionKL::Movement::Stand&&state.movementKind!=NativeActionKL::Movement::BaseMove)||
       !Read(player+0x5F8+11*4,&source,4)||!source||source==at||
       !Read(at,&vt,4)||vt!=image+0x566C44||!Read(at+0x38,&owner,4)||owner!=player||!Read(at+0x44,&id,4)||id!=11)return false;
    passiveClimbPlayer=player;
    struct End {~End(){passiveClimbPlayer=0;}} end;
    const auto query=reinterpret_cast<Eligibility>(image+0x205B50);
    return query(scratch,0,0,0);
}
bool InputBasis(std::uintptr_t expectedPlayer,Basis& output) {
    const auto image=verifiedImage.load();return image&&InputBasisSnapshot(image,expectedPlayer,Read,output);
}
State Current() {
    const auto image=verifiedImage.load();return image?Snapshot(image,Read):State{};
}
bool InstallScriptProximity(bool enabled,float units,LogCallback log) {
    if(scriptInstalled)return true;
    const auto image=verifiedImage.load();
    if(!enabled||!image||!std::isfinite(units)||units<1||units>1000)return false;
    const unsigned char detectSig[]={0x55,0x8B,0xEC,0x83,0xE4,0xC0,0x81,0xEC,0xB4,3,0,0};
    const unsigned char eligibleSig[]={0x83,0xEC,0x40,0x83,0x3D,0xB4,0x44,0xA4,0,0,0x56,0x8B,0xF1};
    const unsigned char notifySig[]={0x53,0x8B,0x1D,0x0C,0x76,0xA8,0,0x8B,0x83,0x7C,0x0C,0,0};
    const unsigned char standEvent[]={0x8B,0x96,0xDC,0,0,0,0x6A,3,0x52,0xE8};
    const unsigned char crouchBit[]={0xC1,0xE8,0x0A,0x24,1,0x88,0x86,0xE1,0,0,0};
    if(!Match(image,0x1FB290,detectSig)||!Match(image,0x20B8B0,eligibleSig)||
       !Match(image,0x2B8F80,notifySig)||!Match(image,0x20C49A,standEvent)||!Match(image,0x20BAC0,crouchBit)) {
        if(log)log("VR script cover: signatures differ; proximity hooks disabled.");return false;
    }
    void* targets[]={reinterpret_cast<void*>(image+0x1FB290),reinterpret_cast<void*>(image+0x20B8B0)};
    void* hooks[]={reinterpret_cast<void*>(DetectorHook),reinterpret_cast<void*>(EligibilityHook)};
    void** originals[]={reinterpret_cast<void**>(&detector),reinterpret_cast<void**>(&eligibility)};
    unsigned created=0;for(;created<2;++created)if(MH_CreateHook(targets[created],hooks[created],originals[created])!=MH_OK)break;
    bool ok=created==2;
    if(ok)for(unsigned i=0;i<2;++i)if(MH_QueueEnableHook(targets[i])!=MH_OK){ok=false;break;}
    notify=reinterpret_cast<Notify>(image+0x2B8F80);scriptUnits=units;
    if(ok)ok=MH_ApplyQueued()==MH_OK;
    if(!ok){for(unsigned i=0;i<created;++i){MH_DisableHook(targets[i]);MH_RemoveHook(targets[i]);}return false;}
    scriptInstalled=true;
    if(log)log("VR script cover: verified PlayerMonitor proximity credit and qualified legacy cover intent; geometry/cooldown/collision retained.");
    return true;
}
void OnTrackedFire(std::uintptr_t player) {
    State state;std::uint32_t input=0;
    if(GetCurrentThreadId()!=gameplayThread||scriptPolicy.proximity.player!=player)return;
    std::uint32_t geometry=0;NativeCameraMath::CameraPose root;
    if(!VrGameplay(player,state)||!Read(player+0x400,&input,sizeof(input))||
       !Read(player+4,&geometry,sizeof(geometry))||!NativePlayerPose::World(geometry,Read,root)) {
        scriptPolicy.Reset();return;
    }
    // Native cover blind-fire events at 60C3BF/60C3D5. A real native shot
    // must occur; merely holding a controller cannot complete this step.
    const auto event=scriptPolicy.BlindFire(player,state,NativeActionKL::Current(),root.position,input,scriptUnits,true);
    if(event)Credit(player,event);
}
bool InstallActionClimb(bool enabled,LogCallback log) {
    if(actionClimbInstalled)return true;
    const auto image=verifiedImage.load();
    if(!enabled||!image||!NativeLedgeKL::VerifyImage(image,Read)) {
        if(log)log("VR action climb: native Use/Climb proof differs; action assist disabled.");
        return false;
    }
    void* targets[]{reinterpret_cast<void*>(image+NativeLedgeKL::HelperRva),reinterpret_cast<void*>(image+NativeLedgeKL::LatchRva)};
    void* hooks[]{reinterpret_cast<void*>(MovementIntentHook),reinterpret_cast<void*>(ClimbLatchHook)};
    void** originals[]{reinterpret_cast<void**>(&movementIntent),reinterpret_cast<void**>(&climbLatch)};
    unsigned created=0;
    for(;created<2;++created)if(MH_CreateHook(targets[created],hooks[created],originals[created])!=MH_OK)break;
    bool ok=created==2;
    if(ok)for(auto* target:targets)if(MH_QueueEnableHook(target)!=MH_OK){ok=false;break;}
    if(ok)ok=MH_ApplyQueued()==MH_OK;
    if(!ok){for(unsigned i=0;i<created;++i){MH_DisableHook(targets[i]);MH_RemoveHook(targets[i]);}return false;}
    actionClimbInstalled=true;
    if(log)log("VR action climb: held B supplies Climb intent; original ledge geometry, height and headroom checks retained.");
    return true;
}
void SetActionClimbContext(bool gameplay) {
    auto before=actionClimbContext.load();
    while(bool(before&1u)!=gameplay) {
        const auto next=((before+2u)&~std::uint64_t(1))|(gameplay?1u:0u);
        if(actionClimbContext.compare_exchange_weak(before,next))break;
    }
}
ScriptStats ScriptSnapshot(){return {scriptInstalled.load(),candidates.load(),events.load(),blockedEntries.load(),
    detectorCalls.load(),rawFreshCandidates.load(),invalidStates.load(),ineligible.load(),readFailures.load(),activeCredits.load(),inactiveUpdates.load(),
    heldEntryRequests.load(),nativeEntryAllowed.load(),nativeEntryRejected.load()};}
ActionClimbStats ActionClimbSnapshot(){return {actionClimbInstalled.load(),climbCalls.load(),climbRequests.load(),climbAssisted.load(),climbRejected.load()};}
}
