#pragma once
#include "native_camera_math.hpp"
#include "native_player_pose.hpp"
#include "native_action_kl.hpp"
#include "native_mounted_aim_kl.hpp"
#include <cstdint>

// A proved mount base transports a fixed anatomical seat. Actor-only fallback
// must keep the CURRENT anatomical position: its first frame can still be the
// standing entry pose, before the passenger has sat down inside the vehicle.
namespace NativeCarriedCamera {
using namespace NativeCameraMath;
inline Vec3 Local(const Basis& b,Vec3 v){return {Dot(v,b.right),Dot(v,b.up),Dot(v,b.forward)};}
inline Vec3 World(const Basis& b,Vec3 v){return NativePlayerPose::Transform(b,v);}
struct Reference {
    CameraPose root{};
    std::uintptr_t identity=0;
    unsigned kind=0; // 1: exact actor GPU root; 2: exact native mounted base.
};
struct EntryKey {
    std::uintptr_t player=0,movement=0,record=0,carrier=0;
};
inline bool Same(const EntryKey& a,const EntryKey& b){return a.player&&a.player==b.player&&a.movement==b.movement&&a.record==b.record&&a.carrier==b.carrier;}
// Small entry-controls clearance for the EXACT M03B van only. This is not a
// door-open flag. Once ordinary controls were seen, a later pause/block must
// never move the accepted gameplay seat. Camera/recenter are not entry keys.
struct EntryClearance {
    bool valid=false,released=false,active=false;
    EntryKey key{};
    static constexpr float Metres=.12f;
    void Reset(){*this={};}
    void Unavailable(){if(valid)released=true;active=false;}
    bool Select(EntryKey next,bool eligible,bool known,bool blocked,const CameraPose& seat,float units,CameraPose& output){
        if(!ValidatePose(seat,.05f)||!std::isfinite(units)||units<1||units>1000)return false;
        if(!next.player||!next.movement||!next.record||!next.carrier){Reset();output=seat;return true;}
        if(!valid||!Same(key,next)){Reset();valid=true;key=next;released=!eligible||!known||!blocked;}
        // A missing late graph/controller read fails closed without creating
        // another intro interval when it becomes readable again.
        if(!eligible||!known||!blocked)released=true;
        CameraPose result=seat;active=!released&&eligible&&known&&blocked;
        if(active)result.position=Subtract(seat.position,Scale(seat.basis.forward,Metres*units));
        if(!ValidatePose(result,.05f))return false;output=result;return true;
    }
};
// Actor/model forward is not anatomical forward in KL1: shoulders and toes
// face model -Z. Bone bind axes must never seed a passenger's camera yaw.
inline bool AnatomicalForward(const CameraPose& actor,CameraPose& eye) {
    if(!ValidatePose(actor,.05f)||!Finite(eye.position))return false;
    const Vec3 forward=Scale(actor.basis.forward,-1.f);
    Basis basis{forward,actor.basis.up,Cross(actor.basis.up,forward)};
    if(!ValidateBasis(basis,.05f))return false;
    eye.basis=basis;return true;
}
inline bool EyeBase(bool carried,bool reuse,const CameraPose& currentSeat,const CameraPose& firstEye,
                    const CameraPose& nativeCamera,CameraPose& output) {
    const CameraPose result=carried?currentSeat:(reuse?firstEye:nativeCamera);
    if(!ValidatePose(result,.05f))return false;
    output=result;return true;
}
// Native 6B5322..6B5337 (fixed MG) / 6DF0F2..6DF107 (car MG)
// link the player's exact record, controller and mount.
// The controller's +20 is the MOVING barrel/aim geometry and is excluded.
// 73B4E6 instead reads the Havok mount entity's base geometry via 4F7900.
template<class Reader> bool MountedReference(std::uintptr_t image,const NativeActionKL::State& action,
                                              Reader read,Reference& output) {
    if(!action.valid||action.movementKind!=NativeActionKL::Movement::Mounted)return false;
    const auto before=NativeMountedAimKL::SnapshotIdentity(image,action.player,read);
    if(!before.valid||before.movement!=action.movement||before.action!=action.action)return false;
    CameraPose root;
    if(!NativePlayerPose::World(before.geometry,read,root))return false;
    const auto after=NativeMountedAimKL::SnapshotIdentity(image,action.player,read);
    if(!NativeMountedAimKL::Same(before,after))return false;
    output={root,before.geometry,2};return true;
}
struct State {
    bool valid=false;
    std::uintptr_t player=0,camera=0,movement=0,carrier=0;
    unsigned kind=0;
    Basis local{};
    Vec3 localPosition{};
    // Actor-root fallback can receive a different head source for each eye.
    // Keep only its local offset for one stereo pair; the current root still
    // transports each eye, and a fresh pair can follow the seating animation.
    bool pairPositionValid=false;
    std::uint64_t positionPair=0;
    std::uintptr_t positionAction=0;
    Vec3 pairPosition{};
    void Reset(){*this={};}
    bool Select(const Reference& reference,const CameraPose& anatomical,std::uintptr_t nextPlayer,
                std::uintptr_t nextCamera,std::uintptr_t nextMovement,CameraPose& output,
                std::uint64_t pairId=UINT64_MAX,std::uintptr_t action=0) {
        if(!nextPlayer||!nextCamera||!nextMovement||!reference.identity||!reference.kind||
           !ValidatePose(reference.root,.05f)||!ValidatePose(anatomical,.05f))return false;
        const bool keep=valid&&player==nextPlayer&&camera==nextCamera&&movement==nextMovement&&carrier==reference.identity&&kind==reference.kind;
        Basis selected=keep?local:Basis{Local(reference.root.basis,anatomical.basis.forward),
            Local(reference.root.basis,anatomical.basis.up),Local(reference.root.basis,anatomical.basis.right)};
        const bool lockPosition=reference.kind==2;
        const Vec3 observed=Local(reference.root.basis,Subtract(anatomical.position,reference.root.position));
        const bool pairReuse=keep&&!lockPosition&&pairId!=UINT64_MAX&&pairPositionValid&&positionPair==pairId&&positionAction==action;
        const Vec3 position=keep&&lockPosition?localPosition:(pairReuse?pairPosition:observed);
        CameraPose result{{World(reference.root.basis,selected.forward),World(reference.root.basis,selected.up),
            World(reference.root.basis,selected.right)},Add(reference.root.position,World(reference.root.basis,position))};
        if(!ValidatePose(result,.05f))return false;
        if(!keep){local=selected;localPosition=position;player=nextPlayer;camera=nextCamera;movement=nextMovement;carrier=reference.identity;kind=reference.kind;valid=true;pairPositionValid=false;}
        if(!lockPosition&&pairId!=UINT64_MAX&&!pairReuse){pairPosition=position;positionPair=pairId;positionAction=action;pairPositionValid=true;}
        output=result;return true;
    }
};
}
