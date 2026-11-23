#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "native_team_commands_kl.hpp"
#include "native_team_commands_kl_image.hpp"
#include "native_aim_kl.hpp"
#include "native_aim_kl_math.hpp"
#include "native_command_ray_kl.hpp"
#include "native_team_assist_kl.hpp"
#include "native_action_kl.hpp"
#include "native_camera_kl.hpp"
#include "native_cover_kl.hpp"
#include "vr_bridge.hpp"
#include "profile.hpp"
#include "MinHook.h"
#include <atomic>

namespace NativeTeamCommandsKL {namespace {
namespace T=TeamCommandIPC;
namespace I=NativeTeamCommandsImage;
using Vec3=NativeCameraMath::Vec3;
using Recall=void(__thiscall*)(void*,bool);
using Move=void(__thiscall*)(void*,bool,const Vec3*);
using Attack=void(__thiscall*)(void*,bool,float);
using Target=std::uint32_t(__thiscall*)(void*,Vec3*);
using Fallback=std::uint32_t(__thiscall*)(void*);
using Select=void(__thiscall*)(void*,bool);
using AssistPicker=std::uint32_t(__thiscall*)(void*,unsigned);
using AssistPoint=void*(__thiscall*)(void*,void*,void*);
using AssistOrigin=void*(__thiscall*)(void*,void*);
Recall recall=nullptr;Move move=nullptr;Attack attack=nullptr;Target target=nullptr;Fallback fallback=nullptr;
Select selectReceiver=nullptr;
AssistPicker assistPicker=nullptr;AssistPoint assistPoint=nullptr;AssistOrigin assistOrigin=nullptr;
std::uintptr_t image=0;
std::atomic<bool> installed{false};
std::atomic<std::uint64_t> requests{0},accepted{0},rejected{0},noTarget{0},selectionChanged{0},stale{0},queries{0},targetOverrides{0};
std::atomic<std::uint64_t> weaponRayRequests{0},headRayRequests{0},heldRayUnavailable{0},headRayUnavailable{0};
std::atomic<std::uint64_t> assistRequests{0},assistSelected{0},assistRejected{0},assistOccluded{0};
std::atomic<float> assistLastAngle{0};
SRWLOCK headLock=SRWLOCK_INIT;
NativeWeaponKL::HeadGaze commandHead;
std::atomic<std::uint32_t> lastSerial{0},lastCommand{0},lastResult{0},lastReceiver{0},lastTarget{0},lastThread{0};
std::atomic<float> lastX{0},lastY{0},lastZ{0};
struct AttackTarget {bool active=false;std::uintptr_t player=0,mover=0;std::uint32_t reference=0;Vec3 point{};};
thread_local AttackTarget overrideTarget;
thread_local bool processing=false;
thread_local std::uintptr_t dispatchControl=0;
struct AssistScope {std::uintptr_t snapshot=0,player=0;Vec3 origin{};};
thread_local AssistScope assisted;
std::atomic<std::uint32_t> consumedSerial{0};
DWORD consumedSelectionTick=0;
bool Read(std::uintptr_t at,void* out,std::size_t size) {
    SIZE_T got=0;return NativeWeaponOwnership::Address(at,size)&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),out,size,&got)&&got==size;
}
template<class V> bool Value(std::uintptr_t at,V& out){return NativeWeaponOwnership::Read(at,out,Read);}
void __fastcall SelectHook(void* control,void*,bool previous){
    const auto caller=reinterpret_cast<std::uintptr_t>(__builtin_return_address(0))-image;
    if(dispatchControl&&reinterpret_cast<std::uintptr_t>(control)==dispatchControl&&I::OrderAdvance(caller))return;
    selectReceiver(control,previous);
}
void* __fastcall AssistOriginHook(void* object,void*,void* output){
    if(output&&assisted.snapshot==reinterpret_cast<std::uintptr_t>(object)&&assisted.player&&
       NativeCoverKL::SimulationCallbackActive(assisted.player)&&reinterpret_cast<std::uintptr_t>(__builtin_return_address(0))==image+0x231EE0){
        std::memcpy(output,&assisted.origin,sizeof(assisted.origin));return output;}
    return assistOrigin(object,output);
}
bool OverrideAllowed(void* mover,std::uintptr_t caller) {
    return overrideTarget.active&&reinterpret_cast<std::uintptr_t>(mover)==overrideTarget.mover&&
        NativeCoverKL::SimulationCallbackActive(overrideTarget.player)&&caller==image+0x213ECC;
}
std::uint32_t __fastcall TargetHook(void* mover,void*,Vec3* point) {
    if(OverrideAllowed(mover,reinterpret_cast<std::uintptr_t>(__builtin_return_address(0)))) {
        if(!point)return 0;
        *point=overrideTarget.point;++targetOverrides;return overrideTarget.reference;
    }
    return target(mover,point);
}
std::uint32_t __fastcall FallbackHook(void* mover,void*) {
    if(overrideTarget.active&&reinterpret_cast<std::uintptr_t>(mover)==overrideTarget.mover&&
       NativeCoverKL::SimulationCallbackActive(overrideTarget.player)&&
       reinterpret_cast<std::uintptr_t>(__builtin_return_address(0))==image+0x213F08) {
        ++targetOverrides;return overrideTarget.reference;
    }
    return fallback(mover);
}
bool Gameplay(std::uintptr_t player) {
    std::uint8_t paused[2]{1,1};
    const auto action=NativeActionKL::Current();
    return installed&&VRBridge::Ready()&&VRBridge::TeamHudVisible()&&
        !NativeCameraKL::CinematicActive()&&action.valid&&action.player==player&&action.WeaponTrackingAllowed()&&
        Read(image+0x8883A4,paused,sizeof(paused))&&!paused[1]&&(!paused[0]||(paused[0]==3&&VRBridge::WeaponMenuVisible()));
}
NativeCommandRayKL::Hands CommandHands(std::uintptr_t player) {
    return NativeCommandRayKL::ReadHands(image,player,Read,VRBridge::TeamHudVisible()&&VRBridge::WeaponMenuVisible());
}
void Finish(const T::Request& request,T::ResultCode code,std::uintptr_t actor=0,Vec3 point={}) {
    T::Result result;result.serial=request.serial;result.command=request.command;result.code=code;
    result.tick=GetTickCount();result.nativeId=request.selected.nativeId;result.targetActor=static_cast<std::uint32_t>(actor);
    result.point[0]=point.x;result.point[1]=point.y;result.point[2]=point.z;
    lastSerial=request.serial;lastCommand=static_cast<std::uint32_t>(request.command);lastResult=static_cast<std::uint32_t>(code);
    lastReceiver=request.selected.nativeId;lastTarget=result.targetActor;lastThread=GetCurrentThreadId();
    lastX=point.x;lastY=point.y;lastZ=point.z;
    if(code==T::ResultCode::Accepted)++accepted;else ++rejected;
    if(code==T::ResultCode::NoTarget)++noTarget;
    if(code==T::ResultCode::SelectionChanged)++selectionChanged;
    if(code==T::ResultCode::Stale)++stale;
    VRBridge::PublishTeamCommandResult(result);
}
struct Ray {bool head=false;NativeWeaponKL::AimFrame weapon;NativeWeaponKL::HeadGaze gaze;
    std::uintptr_t Player()const{return head?gaze.player:weapon.player;}
    Vec3 Origin()const{return head?gaze.origin:weapon.origin;}
    Vec3 Direction()const{return head?gaze.direction:weapon.direction;}
    bool Current()const {if(head){NativeWeaponKL::HeadGaze after;return ReadHeadRay(gaze.player,after)&&NativeCommandRayKL::Same(gaze,after);}
        NativeWeaponKL::AimFrame after;return Gameplay(weapon.player)&&NativeWeaponKL::SnapshotAim(after)&&NativeAimKLMath::Valid(after,GetTickCount())&&
            after.recenterAck==static_cast<std::uint32_t>(VRBridge::RecenterAck())&&NativeAimKLMath::Same(weapon,after)&&
            NativeCommandRayKL::Matches(CommandHands(weapon.player),after);}
    NativeAimKL::GazeTrace Trace(Vec3 direction,NativeAimKL::Endpoint& out,float range=0)const {
        return head?NativeAimKL::TraceHeadCommand(gaze,direction,out,range):NativeAimKL::TraceCommand(weapon,direction,out,range);}};
bool EnemyTarget(const Ray& pose,NativeAimKL::Endpoint& hit,std::uintptr_t& actor,std::uint32_t& reference) {
    using namespace NativeCameraMath;
    ++assistRequests;const auto reject=[](){++assistRejected;return false;};Vec3 forward;
    NativeTeamAssistKL::Snapshot snapshot;
    if(!assistPicker||!assistPoint||!NativeAimKLMath::Unit(pose.Direction(),forward)||!pose.Current()||
       !NativeTeamAssistKL::Prepare(image,pose.Player(),pose.Origin(),forward,Read,snapshot))return reject();
    // The real native picker reads an isolated camera snapshot. Its referenced
    // actors/controllers stay native and read-only; no live camera is patched.
    assisted={reinterpret_cast<std::uintptr_t>(snapshot.bytes.data()),pose.Player(),pose.Origin()};
    struct End {~End(){assisted={};}} end;
    assistLastAngle=snapshot.angle;
    const auto selected=assistPicker(snapshot.bytes.data(),1);std::uintptr_t selectedActor=0;std::uint32_t selectedReference=0;
    if(!pose.Current()||!NativeTeamAssistKL::Current(snapshot,Read)||!I::HostileActor(image,pose.Player(),selected,Read,selectedActor,selectedReference))return reject();
    Vec3 point{};assistPoint(snapshot.bytes.data(),&point,reinterpret_cast<void*>(selectedActor));
    if(!pose.Current()||!Finite(point))return reject();
    const auto delta=Subtract(point,pose.Origin());const float distance2=Dot(delta,delta);
    if(!std::isfinite(distance2)||distance2<1e-4f||distance2>snapshot.rangeSquared)return reject();
    NativeAimKL::Endpoint candidate;++queries;
    std::uintptr_t contactActor=0;std::uint32_t contactReference=0;
    if(pose.Trace(delta,candidate,std::sqrt(distance2))!=NativeAimKL::GazeTrace::Hit||
       !I::HostileActor(image,pose.Player(),candidate.reference,Read,contactActor,contactReference)||contactActor!=selectedActor){++assistOccluded;return reject();}
    if(!pose.Current()||!NativeTeamAssistKL::Current(snapshot,Read)||!I::HostileActor(image,pose.Player(),selected,Read,selectedActor,selectedReference))return reject();
    actor=selectedActor;reference=selectedReference;hit=candidate;++assistSelected;return true;
}
}
void PublishHeadRay(const NativeWeaponKL::HeadGaze& head) {
    if(!NativeCommandRayKL::Fresh(head,GetTickCount(),static_cast<std::uint32_t>(VRBridge::RecenterAck()))){InvalidateHeadRay();return;}
    AcquireSRWLockExclusive(&headLock);commandHead=head;ReleaseSRWLockExclusive(&headLock);
}
void InvalidateHeadRay(){AcquireSRWLockExclusive(&headLock);commandHead={};ReleaseSRWLockExclusive(&headLock);}
bool ReadHeadRay(std::uintptr_t player,NativeWeaponKL::HeadGaze& output) {
    if(!Gameplay(player))return false;
    NativeWeaponKL::HeadGaze head;AcquireSRWLockShared(&headLock);head=commandHead;ReleaseSRWLockShared(&headLock);
    const auto hands=CommandHands(player);
    if(!hands.valid||!hands.empty||head.player!=player||head.sourceCamera!=hands.camera||
       !NativeCommandRayKL::Fresh(head,GetTickCount(),static_cast<std::uint32_t>(VRBridge::RecenterAck())))return false;
    output=head;return true;
}
bool Install(LogCallback log) {
    if(installed)return true;
    wchar_t path[MAX_PATH]{};char digest[65]{};const auto n=GetModuleFileNameW(nullptr,path,MAX_PATH);
    if(!n||n>=MAX_PATH||!HashFile(path,digest)||!IsSupportedExecutableHash(digest))return false;
    image=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if(!I::Verify(image,Read)||!NativeTeamAssistKL::Verify(image,Read)) {if(log)log("KL1 team commands: native squad/target/faction/assist signatures differ; order dispatch disabled.");return false;}
    void* destinations[]={reinterpret_cast<void*>(image+I::Target),reinterpret_cast<void*>(image+I::FallbackTarget),reinterpret_cast<void*>(image+NativeTeamAssistKL::Origin)};
    void* hooks[]={reinterpret_cast<void*>(TargetHook),reinterpret_cast<void*>(FallbackHook),reinterpret_cast<void*>(AssistOriginHook)};
    void** originals[]={reinterpret_cast<void**>(&target),reinterpret_cast<void**>(&fallback),reinterpret_cast<void**>(&assistOrigin)};
    unsigned created=0;for(;created<3;++created)if(MH_CreateHook(destinations[created],hooks[created],originals[created])!=MH_OK)break;
    bool okay=created==3;
    if(okay)for(unsigned i=0;i<3;++i)if(MH_QueueEnableHook(destinations[i])!=MH_OK){okay=false;break;}
    if(okay)okay=MH_ApplyQueued()==MH_OK;
    if(!okay){for(unsigned i=0;i<created;++i){MH_DisableHook(destinations[i]);MH_RemoveHook(destinations[i]);}return false;}
    recall=reinterpret_cast<Recall>(image+I::Recall);move=reinterpret_cast<Move>(image+I::Move);attack=reinterpret_cast<Attack>(image+I::Attack);
    assistPicker=reinterpret_cast<AssistPicker>(image+NativeTeamAssistKL::Picker);assistPoint=reinterpret_cast<AssistPoint>(image+NativeTeamAssistKL::Point);
    if(I::VerifyOrderAdvance(image,Read)){
        auto* selection=reinterpret_cast<void*>(image+I::SelectReceiver);
        if(MH_CreateHook(selection,reinterpret_cast<void*>(SelectHook),reinterpret_cast<void**>(&selectReceiver))==MH_OK&&
           MH_EnableHook(selection)!=MH_OK){MH_RemoveHook(selection);selectReceiver=nullptr;}
    }
    installed=true;
    if(log)log("KL1 team commands: native selected/all orders; owned gun or positively unarmed centre-head ray; native camera target assist with verified hostile actor and ROOM line of sight.");
    return true;
}
T::ReceiverCertificate ReceiverSnapshot(std::uint32_t now) {
    return installed?I::Receiver(image,now,static_cast<std::uint32_t>(VRBridge::RecenterAck()),Read,
                                 VRBridge::TeamHudVisible()&&VRBridge::WeaponMenuVisible()):T::ReceiverCertificate{};
}
void SimulationTick(std::uintptr_t player) {
    if(!installed||processing||!NativeCoverKL::SimulationCallbackActive(player))return;
    const auto selectionTick=VRBridge::TeamNextTick();
    if(selectionTick&&selectionTick!=consumedSelectionTick) {
        consumedSelectionTick=selectionTick;
        if(selectReceiver&&Gameplay(player)) {
            const auto receiver=ReceiverSnapshot(GetTickCount());
            if(receiver.valid&&receiver.player==player)selectReceiver(reinterpret_cast<void*>(receiver.control),false);
        }
    }
    T::Request request;if(!VRBridge::ReadTeamCommand(request)||!request.serial||request.serial==consumedSerial.load())return;
    consumedSerial=request.serial;++requests;
    struct Scope {Scope(){processing=true;}~Scope(){processing=false;overrideTarget={};dispatchControl=0;}} scope;
    const auto now=static_cast<std::uint32_t>(GetTickCount());
    const auto ack=static_cast<std::uint32_t>(VRBridge::RecenterAck());
    if(!T::Known(request.command)){Finish(request,T::ResultCode::Unsupported);return;}
    if(!request.tick||std::int32_t(now-request.tick)<0||now-request.tick>300||request.recenter!=ack||!T::Fresh(request.selected,now,ack)) {Finish(request,T::ResultCode::Stale);return;}
    if(!Gameplay(player)){Finish(request,T::ResultCode::Rejected);return;}
    const auto receiver=ReceiverSnapshot(now);
    if(!I::SameReceiver(request.selected,receiver)||receiver.player!=player){Finish(request,T::ResultCode::SelectionChanged);return;}
    Ray pose;NativeAimKL::Endpoint endpoint;std::uintptr_t actor=0;std::uint32_t reference=0,mover=0;
    if(request.command!=T::Command::Recall) {
        const auto hands=CommandHands(player);
        if(hands.valid&&hands.empty){pose.head=true;
            if(!ReadHeadRay(player,pose.gaze)){++headRayUnavailable;Finish(request,T::ResultCode::NoTarget);return;}++headRayRequests;
        } else {
            if(!NativeWeaponKL::SnapshotAim(pose.weapon)||!NativeCommandRayKL::Matches(hands,pose.weapon)||
               !NativeAimKLMath::Valid(pose.weapon,now)||pose.weapon.recenterAck!=ack){++heldRayUnavailable;Finish(request,T::ResultCode::NoTarget);return;}
            ++weaponRayRequests;
        }
        if(request.command==T::Command::Move) {
            ++queries;if(pose.Trace(pose.Direction(),endpoint)!=NativeAimKL::GazeTrace::Hit){Finish(request,T::ResultCode::NoTarget);return;}
        } else {
            std::uint32_t vt=0,owner=0;
            if(!EnemyTarget(pose,endpoint,actor,reference)||!Value(player+0x9AC,mover)||!Value(mover,vt)||
               !NativeCameraMoverKL::KnownVtable(image,vt)||!Value(mover+8,owner)||owner!=player){Finish(request,T::ResultCode::NoTarget);return;}
        }
    }
    const auto checked=ReceiverSnapshot(GetTickCount());
    if(request.command!=T::Command::Recall&&!pose.Current()){Finish(request,T::ResultCode::NoTarget);return;}
    if(!Gameplay(player)||static_cast<std::uint32_t>(VRBridge::RecenterAck())!=ack||!I::SameReceiver(receiver,checked)) {Finish(request,T::ResultCode::SelectionChanged);return;}
    std::uint32_t before=0,after=0;if(!Value(receiver.control+8,before)){Finish(request,T::ResultCode::Rejected);return;}
    // Defer the native selector until dispatch confirms the order was accepted.
    dispatchControl=receiver.control;
    if(request.command==T::Command::Recall)recall(reinterpret_cast<void*>(receiver.control),receiver.all);
    else if(request.command==T::Command::Move)move(reinterpret_cast<void*>(receiver.control),receiver.all,&endpoint.point);
    else {
        overrideTarget={true,player,mover,reference,endpoint.point};
        // Native PC single-press FireAt uses this duration constant. The
        // wrapper still owns receiver eligibility, command objects and AI.
        float duration=0;if(!Value(image+0x53C9A4,duration)||!std::isfinite(duration)||duration<=0||duration>60000){Finish(request,T::ResultCode::Rejected);return;}
        attack(reinterpret_cast<void*>(receiver.control),receiver.all,duration);
        overrideTarget={};
    }
    const bool dispatched=Value(receiver.control+8,after)&&after!=before;
    dispatchControl=0;
    if(dispatched&&!receiver.all&&selectReceiver)
        selectReceiver(reinterpret_cast<void*>(receiver.control),false);
    Finish(request,dispatched?T::ResultCode::Accepted:T::ResultCode::Rejected,actor,endpoint.point);
}
Stats Snapshot() {
    Stats s;s.installed=installed;s.requests=requests;s.accepted=accepted;s.rejected=rejected;s.noTarget=noTarget;s.selectionChanged=selectionChanged;s.stale=stale;
    s.queries=queries;s.targetOverrides=targetOverrides;s.lastSerial=lastSerial;s.lastCommand=lastCommand;s.lastResult=lastResult;s.lastReceiver=lastReceiver;s.lastTarget=lastTarget;s.lastThread=lastThread;
    s.weaponRayRequests=weaponRayRequests;s.headRayRequests=headRayRequests;s.heldRayUnavailable=heldRayUnavailable;s.headRayUnavailable=headRayUnavailable;
    s.assistRequests=assistRequests;s.assistSelected=assistSelected;s.assistRejected=assistRejected;s.assistOccluded=assistOccluded;s.assistLastAngle=assistLastAngle;
    s.lastPoint[0]=lastX;s.lastPoint[1]=lastY;s.lastPoint[2]=lastZ;return s;
}
}
