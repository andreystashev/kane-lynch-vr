#include "native_action_kl.hpp"
#include "native_vehicle_camera_math.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

namespace {
unsigned checks=0;
void Check(bool ok,const char* what) {
    ++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}
}
constexpr std::uintptr_t Image=0x400000,Data=0x10000000,Player=0x11000000;
constexpr std::uintptr_t Move=0x12000000,Dispatcher=Player+0x198,Action=0x13000000;
struct Memory {
    std::map<std::uintptr_t,std::uint32_t> fields{
        {Image+0x68760C,Data},{Data+0xA2C,Player},{Player,Image+0x56582C},
        {Player+0x67C,Move},{Move,Image+0x566A44},{Move+0x38,Player},{Move+0x44,2},
        {Player+0x600,Move},{Player+0x194,Dispatcher},{Dispatcher,Image+0x54DE4C},
        {Dispatcher+8,Player},{Dispatcher+4,0},{Action,Image+0x5669CC}};
    bool Read(std::uintptr_t at,void* output,std::size_t bytes) const {
        auto it=fields.find(at);if(bytes!=4||it==fields.end())return false;
        std::memcpy(output,&it->second,4);return true;
    }
    NativeActionKL::State Get() const {
        return NativeActionKL::Snapshot(Image,[&](auto at,auto* output,auto bytes){return Read(at,output,bytes);});
    }
};
void MovementStates() {
    using namespace NativeActionKL;
    struct Case {std::uint32_t rva;Movement kind;bool native,axes;};
    const Case cases[]={
        {0x566A44,Movement::Stand,false,false},{0x566AEC,Movement::BaseMove,false,false},
        {0x566EAC,Movement::Cover,false,false},{0x5671FC,Movement::Guide,true,false},
        {0x566B9C,Movement::Roll,true,false},{0x567054,Movement::Fall,true,false},
        {0x566C44,Movement::Climb,true,true},{0x566D14,Movement::Vehicle,true,false},
        {0x566F54,Movement::Mounted,true,false},{0x567144,Movement::Rappel,true,true},
        {0x566DBC,Movement::Die,true,false}};
    for(const auto& test:cases) {
        Memory memory;memory.fields[Move]=Image+test.rva;
        const auto state=memory.Get();
        Check(state.valid&&state.player==Player&&state.movement==Move&&state.movementKind==test.kind,"exact owned native movement class is recognized");
        Check(state.PreserveRig()==test.native,"ordinary locomotion retains Touch arms while constrained movement retains native animation");
        Check(state.NativeMovementAxes()==test.axes,"only verified rope and climb states bypass head-relative movement axes");
        const bool anatomical=test.kind==Movement::Rappel||test.kind==Movement::Climb||
            test.kind==Movement::Fall||test.kind==Movement::Die||test.kind==Movement::Vehicle||test.kind==Movement::Mounted;
        Check(state.FollowAnimatedHead()==anatomical&&
              state.FollowNativeCameraTilt()==(test.kind==Movement::Rappel),
              "climb/fall/wounded/rappel follow anatomical eyes while only rappel owns animated camera tilt");
    }
}
void ActionLifetime() {
    using namespace NativeActionKL;
    Memory memory;
    Check(memory.Get().valid&&!memory.Get().PreserveRig(),"native dispatcher idle pointer does not disable ordinary VR hands");
    struct Case {std::uint32_t rva;NativeActionKL::Action kind;};
    const Case cases[]={{0x5669CC,NativeActionKL::Action::CloseCombat},{0x5667F4,NativeActionKL::Action::MiniAct},
        {0x566834,NativeActionKL::Action::MiniAnim},{0x566628,NativeActionKL::Action::Pickup},
        {0x566944,NativeActionKL::Action::Reload},{0x56665C,NativeActionKL::Action::Throw},
        {0x5668DC,NativeActionKL::Action::Throw},{0x56672C,NativeActionKL::Action::Vehicle},
        {0x566760,NativeActionKL::Action::Vehicle},{0x566998,NativeActionKL::Action::Heal},
        {0x566874,NativeActionKL::Action::Mounted},{0x5668A8,NativeActionKL::Action::Mounted},
        {0x566690,NativeActionKL::Action::PutBack},{0x5666C4,NativeActionKL::Action::GetBack},{0x5666F8,NativeActionKL::Action::Command},
        {0x566794,NativeActionKL::Action::Other}};
    memory.fields[Dispatcher+4]=::Action;
    for(const auto& test:cases) {
        memory.fields[::Action]=Image+test.rva;
        const auto state=memory.Get();
        Check(state.valid&&state.action==::Action&&state.actionKind==test.kind&&state.PreserveRig(),"current exact owned action retains its native lifecycle independently of selective visual arm policy");
        Check(!state.NativeMovementAxes(),"ordinary interaction action does not change walking input coordinates");
        Check(state.FollowAnimatedHead()==(test.kind==NativeActionKL::Action::Vehicle||test.kind==NativeActionKL::Action::Pickup||test.kind==NativeActionKL::Action::Heal||test.rva==0x5668DC||test.kind==NativeActionKL::Action::CloseCombat||
              test.kind==NativeActionKL::Action::MiniAct||test.kind==NativeActionKL::Action::MiniAnim)&&!state.FollowNativeCameraTilt(),
              "physical interaction and actor animation follow anatomical eyes without adding automatic camera rotation");
        Check(state.BodyAlignedInteraction()==(test.kind==NativeActionKL::Action::Pickup||test.kind==NativeActionKL::Action::Heal||test.rva==0x5668DC),
              "only owned pickup, revive or exact defensive grenade throw gets actor-centred alignment");
    }
    memory.fields[::Action]=Image+0x5668DC;memory.fields[Move]=Image+0x566D14;
    Check(!memory.Get().BodyAlignedInteraction(),"vehicle action cannot borrow standing interaction alignment");
    memory.fields[Move]=Image+0x566A44;
    memory.fields.erase(::Action);
    Check(memory.Get().valid&&memory.Get().PreserveRig(),"unreadable diagnostic action vtable cannot cancel the dispatcher's real active animation");
    memory.fields[Dispatcher+4]=0;
    Check(memory.Get().valid&&!memory.Get().PreserveRig(),"native dispatcher completion restores ordinary VR hands automatically");
}
void OwnershipAndBounds() {
    Memory source;
    const std::uintptr_t required[]={Image+0x68760C,Data+0xA2C,Player,Player+0x67C,Move,
        Move+0x38,Move+0x44,Player+0x600,Player+0x194,Dispatcher,Dispatcher+8,Dispatcher+4};
    for(auto address:required) {
        Memory copy=source;copy.fields.erase(address);const auto state=copy.Get();
        Check(!state.valid&&!state.PreserveRig()&&!state.NativeMovementAxes(),"missing ownership/state field cannot masquerade as a native player action");
    }
    Memory bad=source;bad.fields[Player]=Image+0x56477C;
    Check(!bad.Get().valid,"an NPC vtable cannot authorize local-player action changes");
    bad=source;bad.fields[Move+0x38]=Player+0x1000;
    Check(!bad.Get().valid,"another actor's movement object is rejected");
    bad=source;bad.fields[Dispatcher+8]=Player+0x1000;
    Check(!bad.Get().valid,"another actor's dispatcher is rejected");
    bad=source;bad.fields[Player+0x600]=Move+0x1000;
    Check(!bad.Get().valid,"unregistered stale movement pointer is rejected");
    bad=source;bad.fields[Move+0x44]=32;
    Check(!bad.Get().valid,"out-of-range movement IDs are rejected before table reads");
    bad=source;bad.fields[Player+0x67C]=0;
    Check(!bad.Get().valid,"uninitialized movement pointer is rejected");
    bad=source;bad.fields[Player+0x194]=0xfffffffcu;
    Check(!bad.Get().valid,"overflowing dispatcher address cannot be read");
}
}
void VehicleCameraScope() {
    using M=NativeActionKL::Movement;using A=NativeActionKL::Action;
    NativeVehicleCamera::Scope scope;NativeActionKL::State state;
    state.valid=true;state.player=Player;state.action=::Action;state.movementKind=M::BaseMove;state.actionKind=A::MiniAct;
    Check(!scope.Select(state),"ordinary upright MiniAct cannot borrow vehicle camera scope");
    state.movementKind=M::Vehicle;Check(scope.Select(state),"owned MiniAct in native vehicle mode uses current animated head camera");
    state.movementKind=M::BaseMove;Check(scope.Select(state),"same exact vehicle MiniAct retains eye camera through switch back to walking");
    ++state.action;Check(!scope.Select(state),"a different upright mini action cannot inherit a completed seat clip");
    state.actionKind=A::Vehicle;Check(scope.Select(state),"exact native entry/exit action is eligible before Vehicle movement starts");
    state.movementKind=M::Rappel;Check(!scope.Select(state),"accepted rappel camera is excluded from vehicle transition scope");
    state.movementKind=M::Mounted;Check(!scope.Select(state),"mounted weapon actions retain their own camera mode");
    state.movementKind=M::Vehicle;state.actionKind=A::Reload;Check(!scope.Select(state),"passenger reload cannot replace seated camera with animated head sway");
    state.actionKind=A::MiniAnim;Check(scope.Select(state),"vehicle MiniAnim shares exact native animated seat camera");
    state.movementKind=M::Stand;++state.player;Check(!scope.Select(state),"another player's upright action cannot retain vehicle scope");
    state.valid=false;Check(!scope.Select(state),"invalid ownership retires vehicle camera scope");
    NativeVehicleCamera::Tracking tracking;NativeCameraMath::Quat selected;
    const NativeCameraMath::Quat start{0,std::sin(.4f),0,std::cos(.4f)};
    Check(tracking.Select(start,Player,55,66,7,selected)&&std::fabs(selected.w-1)<.0001f,"vehicle action starts with its own neutral physical view instead of gameplay virtual turn");
    Check(tracking.Select({},Player,55,66,7,selected)&&std::fabs(selected.y)>.3f,"physical look remains free relative to the vehicle clip's entry neutral");
    Check(tracking.Select({},Player,55,67,7,selected)&&std::fabs(selected.w-1)<.0001f,"new entry/exit action gets a fresh physical reference");
}
int main() {
    MovementStates();ActionLifetime();OwnershipAndBounds();
    VehicleCameraScope();
    std::printf("Native player action/rope state: %u checks passed.\n",checks);return 0;
}
