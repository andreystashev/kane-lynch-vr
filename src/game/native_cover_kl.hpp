#pragma once
#include "native_action_kl.hpp"
#include "native_camera_math.hpp"
#include "native_player_pose.hpp"
#include <cmath>
#include <cstring>

// Native cover observations and script-only VR proximity credit. No fake
// actor cover flags, cover protection, collision, health or save edits.
namespace NativeCoverKL {
using NativeCameraMath::Basis;
struct State {
    bool valid=false,inputBasisValid=false,enabled=false,active=false;
    bool controllerBlocked=false,hasGuideBase=false,crouched=false;
    std::uintptr_t player=0,cover=0;
    std::int8_t candidateSide=0;
    std::int32_t phase=-1,exposure=0;
    std::uint32_t actorFlags=0,movementId=0;
    float inputYawRadians=0.f,moveForward=0.f,moveRight=0.f;
    NativeCameraMath::Vec3 normal{};
    Basis inputBasis{};
};
struct Proximity {
    std::uintptr_t player=0;
    NativeCameraMath::Vec3 position{};
    void Reset(){*this={};}
    bool Update(std::uintptr_t actor,NativeCameraMath::Vec3 current,int side,
                NativeCameraMath::Vec3 normal,float units,bool eligible) {
        using namespace NativeCameraMath;
        if(!eligible||!actor||!Finite(current)||!std::isfinite(units)||units<1||units>1000||
           (side!=-1&&side!=0&&side!=1)){Reset();return false;}
        if(player!=actor)Reset();
        if(side) {
            const float n=Dot(normal,normal);
            if(!Finite(normal)||n<.5f||n>1.5f){Reset();return false;}
            player=actor;position=current;
        }
        const auto delta=Subtract(current,position);
        if(!player||delta.x*delta.x+delta.z*delta.z>.65f*.65f*units*units||
           std::fabs(delta.y)>.5f*units){Reset();return false;}
        return true;
    }
};
struct ScriptCredit {
    unsigned stance=0,aim=0;
    bool freshCandidate=false;
};
// This policy emits PlayerMonitor notifications only. In particular its aim
// credit does not set native input/actor bits or enter the third-person aim
// camera. The dispatcher keeps its own enabled-monitor and dedup checks.
struct ScriptPolicy {
    Proximity proximity;
    void Reset(){proximity.Reset();}
    static bool ContextEligible(std::uintptr_t player,const State& cover,const NativeActionKL::State& action,
                                bool vrGameplay) {
        return vrGameplay&&player&&cover.valid&&cover.player==player&&cover.enabled&&
            !cover.controllerBlocked&&action.valid&&action.player==player&&!action.PreserveRig();
    }
    static bool Eligible(std::uintptr_t player,const State& cover,const NativeActionKL::State& action,
                         bool vrGameplay) {
        return ContextEligible(player,cover,action,vrGameplay)&&!cover.active&&
            (action.movementKind==NativeActionKL::Movement::Stand||action.movementKind==NativeActionKL::Movement::BaseMove);
    }
    static bool ActiveEligible(std::uintptr_t player,const State& cover,const NativeActionKL::State& action,
                               bool vrGameplay) {
        return ContextEligible(player,cover,action,vrGameplay)&&cover.active&&cover.cover&&
            action.movementKind==NativeActionKL::Movement::Cover&&action.movement==cover.cover&&action.movementId==7;
    }
    ScriptCredit Update(std::uintptr_t player,const State& cover,const NativeActionKL::State& action,
                        NativeCameraMath::Vec3 position,std::uint32_t input,float units,bool vrGameplay) {
        if(ActiveEligible(player,cover,action,vrGameplay)) {
            // An owned active native Cover is already geometric proof. Its
            // +E1 posture latch selects events in native 60C2D8/60C47E; input
            // crouch may differ while the cover animation changes stance.
            // Retire free-movement proximity so leaving Cover cannot retain it.
            Reset();return {cover.crouched?4u:3u,cover.crouched?6u:5u,false};
        }
        if(!Eligible(player,cover,action,vrGameplay)){Reset();return {};}
        // Native detector skips geometry while aim bit 6 is set, leaving a
        // stale side behind. An existing point may survive, but cannot move.
        const int freshSide=((cover.actorFlags|input)&0x40)?0:cover.candidateSide;
        if(!proximity.Update(player,position,freshSide,cover.normal,units,true))return {};
        const bool crouch=(input&0x400)!=0;
        return {crouch?4u:3u,crouch?6u:5u,freshSide!=0};
    }
    unsigned BlindFire(std::uintptr_t player,const State& cover,const NativeActionKL::State& action,
                       NativeCameraMath::Vec3 position,std::uint32_t input,float units,bool vrGameplay) {
        if(!Eligible(player,cover,action,vrGameplay)){Reset();return 0;}
        // A shot never establishes or refreshes a geometric proximity point.
        if(!proximity.Update(player,position,0,{},units,true)||(input&0x40))return 0;
        return (input&0x400)?10u:9u;
    }
};
// Deliberate B hold only removes our VR entry veto. The native eligibility
// function still owns candidate geometry, cooldown, actor flags and collision.
inline bool HeldEntryEligible(std::uintptr_t player,const State& cover,
                              const NativeActionKL::State& action,bool held,bool vrGameplay) {
    return held&&cover.cover&&action.movement&&cover.cover!=action.movement&&
        ScriptPolicy::Eligible(player,cover,action,vrGameplay);
}
inline bool BasisFromYaw(float yaw,Basis& output) {
    // Native 5EF730 -> identity42EDD0 -> Euler(0,yaw,0)42F3D0.
    // 42F0C0 imports _CIsin/_CIcos, so +1288 is radians, not degrees.
    if(!std::isfinite(yaw)||std::fabs(yaw)>1000000.f)return false;
    const float s=std::sin(yaw),c=std::cos(yaw);
    const Basis result{{s,0.f,c},{0.f,1.f,0.f},{c,0.f,-s}};
    if(!NativeCameraMath::ValidateBasis(result))return false;
    output=result;return true;
}
template<class Reader> bool InputBasisSnapshot(std::uintptr_t image,std::uintptr_t expectedPlayer,
                                               Reader read,Basis& output) {
    const auto state=NativeActionKL::Snapshot(image,read);
    if(!state.valid||!expectedPlayer||state.player!=expectedPlayer)return false;
    std::uint32_t guide=0,guideAfter=0,entity=0,geometry=0,owner=0;
    float yaw=0.f,yawAfter=0.f;Basis candidate;
    if(!read(state.player+0x9B0,&guide,sizeof(guide)))return false;
    const auto guideWorld=[&](NativeCameraMath::CameraPose& world) {
        std::uint32_t currentEntity=0,currentGeometry=0,currentOwner=0;
        if(!read(guide+0x24,&currentEntity,sizeof(currentEntity))||!currentEntity||
           !read(currentEntity+4,&currentGeometry,sizeof(currentGeometry))||!currentGeometry||
           !read(currentGeometry+0x60,&currentOwner,sizeof(currentOwner))||currentOwner!=currentEntity)return false;
        if(entity&&(currentEntity!=entity||currentGeometry!=geometry||currentOwner!=owner))return false;
        entity=currentEntity;geometry=currentGeometry;owner=currentOwner;
        return NativePlayerPose::World(geometry,read,world);
    };
    if(guide) {
        // Native 5EF730 -> [guide+24] -> 4F8380 -> geometry 42A240.
        // This is also the ordinary camera-relative WASD path. Transforming
        // vectors through its complete geometry ancestry ignores translation.
        NativeCameraMath::CameraPose world;
        if(!guideWorld(world))return false;
        candidate=world.basis;
    } else if(!read(state.player+0x1288,&yaw,sizeof(yaw))||!BasisFromYaw(yaw,candidate))return false;
    const auto after=NativeActionKL::Snapshot(image,read);
    if(!after.valid||after.player!=state.player||
       !read(state.player+0x9B0,&guideAfter,sizeof(guideAfter))||guideAfter!=guide)return false;
    if(guide) {
        NativeCameraMath::CameraPose current;
        if(!guideWorld(current)||std::memcmp(&candidate,&current.basis,sizeof(candidate)))return false;
    } else if(!read(state.player+0x1288,&yawAfter,sizeof(yawAfter))||
              std::memcmp(&yaw,&yawAfter,sizeof(yaw)))return false;
    output=candidate;return true;
}
template<class Reader> State Snapshot(std::uintptr_t image,Reader read) {
    State output;const auto state=NativeActionKL::Snapshot(image,read);
    if(!state.valid)return output;
    output.player=state.player;output.movementId=state.movementId;
    std::uint32_t enabled=0,cover=0,vt=0,owner=0,specificOwner=0,id=0,guide=0,controller=0;
    const auto get=[&](std::uintptr_t at,auto& value){return NativeActionKL::Address(at,sizeof(value))&&read(at,&value,sizeof(value));};
    if(!get(image+0x6444B4,enabled)||enabled>1||
       !get(state.player+0x7D0,output.actorFlags)||!get(state.player+0x9B0,guide)||
       !get(state.player+0x9B8,controller)||!NativeActionKL::Address(controller,0x19)||
       !get(state.player+0x12DD,output.candidateSide)||
       (output.candidateSide!=-1&&output.candidateSide!=0&&output.candidateSide!=1)||
       !get(state.player+0x1288,output.inputYawRadians)||
       !get(state.player+0x96C,output.moveForward)||!get(state.player+0x970,output.moveRight)||
       !get(state.player+0x5F8+7*4,cover)||!get(cover,vt)||vt!=image+0x566EAC||
       !get(cover+0x38,owner)||owner!=state.player||
       !get(cover+0xDC,specificOwner)||specificOwner!=state.player||!get(cover+0x44,id)||id!=7||
       !std::isfinite(output.moveForward)||!std::isfinite(output.moveRight)||
       std::fabs(output.moveForward)>1.1f||std::fabs(output.moveRight)>1.1f)return State{};
    std::uint8_t blocked=0;
    if(!get(controller+0x18,blocked)||blocked>1)return State{};
    output.enabled=enabled!=0;output.controllerBlocked=blocked!=0;output.hasGuideBase=guide!=0;
    output.cover=cover;output.active=state.movementKind==NativeActionKL::Movement::Cover&&
        state.movement==cover&&state.movementId==7;
    if(output.active) {
        std::uint8_t crouched=0;
        if(!get(cover+0xD4,output.phase)||output.phase < -1||output.phase>32||
           !get(cover+0x1AC,output.exposure)||output.exposure<0||output.exposure>3||
           !get(cover+0xE1,crouched)||crouched>1)return State{};
        output.crouched=crouched!=0;
    }
    // This is the normal produced by native geometry queries; it is meaningful
    // only when a real candidate or active cover exists, never synthesized.
    if(output.candidateSide||output.active) {
        if(!get(state.player+0x12F8,output.normal)||!NativeCameraMath::Finite(output.normal))return State{};
    }
    output.inputBasisValid=InputBasisSnapshot(image,state.player,read,output.inputBasis);
    if(!std::isfinite(output.inputYawRadians))return State{};
    const auto after=NativeActionKL::Snapshot(image,read);
    std::uint32_t coverAfter=0;
    if(!after.valid||after.player!=state.player||after.movement!=state.movement||
       !get(state.player+0x5F8+7*4,coverAfter)||coverAfter!=cover)return State{};
    output.valid=true;return output;
}
bool InitializeVerifiedImage(std::uintptr_t image);
bool InputBasis(std::uintptr_t expectedPlayer,Basis& output);
State Current();
using LogCallback=void(*)(const char*);
bool InstallScriptProximity(bool enabled,float units,LogCallback log);
// A deliberate native Use tap can supply the missing movement-intent gate
// for Climb. The game's subsequent ledge collision/height checks stay native.
bool InstallActionClimb(bool enabled,LogCallback log);
void SetActionClimbContext(bool gameplay);
// Observer runs only after the native local-player detector, on its simulation
// thread. Collision previews must reject every call outside this exact scope.
using SimulationObserver=void(*)(std::uintptr_t player);
void SetSimulationObserver(SimulationObserver observer);
bool SimulationCallbackActive(std::uintptr_t player);
// Synchronous exact native CanStart on a caller-owned Climb clone. The caller
// must prove/copy its registered class and exclude authored state11 first.
// Only the native input-permission helper is substituted in this TLS scope.
bool ProbePrivateClimb(std::uintptr_t player,void* scratch);
void OnTrackedFire(std::uintptr_t player);
struct ScriptStats {bool installed=false;std::uint64_t candidates=0,events=0,blockedEntries=0;
    std::uint64_t detectorCalls=0,rawFreshCandidates=0,invalidStates=0,ineligible=0,readFailures=0,activeCredits=0,inactiveUpdates=0;
    std::uint64_t heldEntryRequests=0,nativeEntryAllowed=0,nativeEntryRejected=0;};
ScriptStats ScriptSnapshot();
struct ActionClimbStats {bool installed=false;std::uint64_t calls=0,requests=0,assisted=0,rejected=0;};
ActionClimbStats ActionClimbSnapshot();
}
