#include "native_cinematic_kl.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

namespace {
unsigned checks=0;
void Check(bool ok,const char* what) {
    ++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}
}
constexpr std::uintptr_t Image=0x400000,System=0x10000000,Manager=0x11000000;
struct Memory {
    std::map<std::uintptr_t,std::uint32_t> fields{
        {Image+0x6875F0,System},{System+0x96C,Manager},
        {Manager+0x300,0},{Image+0x88E8B0,0}};
    bool Read(std::uintptr_t at,void* output,std::size_t bytes) const {
        auto it=fields.find(at);
        if(bytes!=4||it==fields.end())return false;
        std::memcpy(output,&it->second,4);return true;
    }
    NativeCinematicKL::State Get() const {
        return NativeCinematicKL::Snapshot(Image,[&](auto at,auto* output,auto bytes){return Read(at,output,bytes);});
    }
};
void NativeTransitions() {
    Memory memory;
    Check(memory.Get().valid&&!memory.Get().PreserveNative(),"idle mission permits VR camera and rig");
    memory.fields[Manager+0x300]=1;
    Check(memory.Get().valid&&memory.Get().PreserveNative(),"full scripted camera retains original camera and faces");
    memory.fields[Manager+0x300]=2;
    Check(memory.Get().PreserveNative(),"nested full sequences remain native");
    memory.fields[Manager+0x300]=0;memory.fields[Image+0x88E8B0]=1;
    Check(memory.Get().valid&&!memory.Get().PreserveNative()&&memory.Get().mini==1,"mini dialogue is diagnostic and keeps the full VR head view");
    memory.fields[Manager+0x300]=1;memory.fields[Image+0x88E8B0]=2;
    Check(memory.Get().PreserveNative(),"mixed full and mini sequences remain native");
    memory.fields[Image+0x88E8B0]=0;
    Check(memory.Get().PreserveNative(),"ending mini sequence cannot resume VR inside a full sequence");
    memory.fields[Manager+0x300]=0;
    Check(memory.Get().valid&&!memory.Get().PreserveNative(),"last native sequence ending automatically permits VR again");
}
void CorruptState() {
    const std::uintptr_t addresses[]={Image+0x6875F0,System+0x96C,Manager+0x300};
    for(auto address:addresses) {
        Memory memory;memory.fields.erase(address);
        Check(!memory.Get().valid&&memory.Get().PreserveNative(),"unreadable native state cannot authorize rig modifications");
    }
    for(auto invalid:{0u,1u,0xfffffff0u}) {
        Memory memory;memory.fields[Image+0x6875F0]=invalid;
        Check(!memory.Get().valid&&memory.Get().PreserveNative(),"invalid system address is rejected before dereference");
        memory=Memory{};memory.fields[System+0x96C]=invalid;
        Check(!memory.Get().valid&&memory.Get().PreserveNative(),"invalid manager address is rejected before dereference");
    }
    Memory memory;memory.fields[Manager+0x300]=0xffffffffu;
    Check(!memory.Get().valid&&memory.Get().PreserveNative(),"negative full count retains original rendering");
    memory=Memory{};memory.fields[Image+0x88E8B0]=0xffffffffu;
    Check(memory.Get().valid&&!memory.Get().PreserveNative(),"invalid diagnostic mini count cannot flatten an ordinary VR scene");
    memory=Memory{};memory.fields[Manager+0x300]=33;
    Check(!memory.Get().valid&&memory.Get().PreserveNative(),"unbounded full count retains original rendering");
    memory=Memory{};memory.fields[Image+0x88E8B0]=1025;
    Check(memory.Get().valid&&!memory.Get().PreserveNative(),"unbounded diagnostic mini count cannot change camera mode");
    memory=Memory{};memory.fields.erase(Image+0x88E8B0);
    Check(memory.Get().valid&&!memory.Get().PreserveNative(),"unavailable mini state cannot change a verified full-camera mode");
}
void ExactStereoCamera() {
    constexpr std::uintptr_t Controller=0x12000000,Camera=0x13000000,Geometry=0x14000000;
    Memory memory;
    Check(!memory.Get().PanelRequired()&&!memory.Get().StereoCamera(),"ordinary mission does not become an external cinematic camera");
    memory.fields[Manager+0x300]=1;
    Check(memory.Get().PanelRequired(),"full camera without a verified authored camera retains panel fallback");
    memory.fields[Image+0x87750C]=Controller;memory.fields[Controller+0x24]=Camera;
    memory.fields[Camera]=Image+0x545A3C;memory.fields[Camera+4]=Geometry;memory.fields[Geometry+0x60]=Camera;
    Check(memory.Get().StereoCamera()&&!memory.Get().PanelRequired()&&memory.Get().camera==Camera,
          "only exact current full-cut controller camera permits authored stereo rendering");
    Check(memory.Get().PreserveNative(),"authored stereo cinematic still retains native head/body/face animation");
    memory.fields[Geometry+0x60]=Camera+4;
    Check(!memory.Get().StereoCamera()&&memory.Get().PanelRequired(),"foreign camera geometry backlink cannot authorize a scene pass");
    memory.fields[Geometry+0x60]=Camera;memory.fields[Camera]=Image+0x123456;
    Check(!memory.Get().StereoCamera()&&memory.Get().PanelRequired(),"invalid camera type retains native cinematic panel");
    memory.fields[Camera]=Image+0x545A3C;memory.fields[Manager+0x300]=0;memory.fields[Image+0x88E8B0]=1;
    Check(!memory.Get().StereoCamera()&&!memory.Get().PanelRequired()&&!memory.Get().camera,
          "mini-dialogue never reuses a stale full-cut camera or switches ordinary gameplay externally");
}
void ActorCameraOwnership() {
    using namespace NativeActionKL;
    State action;action.valid=true;action.player=0x15000000;
    action.action=0x16000000;action.actionKind=Action::MiniAct;
    Check(!NativeCinematicKL::ActorCameraAction(action,action.player),"local MiniAct alone never authorizes an external third-person camera");
    action.actionKind=Action::MiniAnim;
    Check(!NativeCinematicKL::ActorCameraAction(action,action.player),"MiniAnim alone retains anatomical interaction camera");
    Check(!NativeCinematicKL::ActorScenePanel(action,action.player,false),"actor animation alone cannot flatten physical gameplay");
    Check(!NativeCinematicKL::ActorScenePanel(action,action.player,true),"VR cinema opt-in retains authored tracked MiniAnim view");
    for(auto kind:{Action::None,Action::Reload,Action::Pickup,Action::CloseCombat,Action::Other}) {
        action.actionKind=kind;
        Check(!NativeCinematicKL::ActorCameraAction(action,action.player),"dialogue counter, reload and ordinary interactions cannot independently switch camera mode");
        Check(!NativeCinematicKL::ActorScenePanel(action,action.player,false),"ordinary actions cannot retire a gameplay stereo eye as an actor cinema panel");
    }
    action.actionKind=Action::MiniAct;
    Check(!NativeCinematicKL::ActorCameraAction(action,action.player+4)&&!NativeCinematicKL::ActorCameraAction(action,0),
          "another actor's scripted action cannot take ownership of the local camera");
    action.action=0;Check(!NativeCinematicKL::ActorCameraAction(action,action.player),"stale action classification without a live action pointer cannot switch camera mode");
    Check(!NativeCinematicKL::ActorScenePanel(action,action.player,false),"retired MiniAct cannot keep the actor panel active");
    action.action=0x16000000;action.valid=false;
    Check(!NativeCinematicKL::ActorCameraAction(action,action.player),"unverified dispatcher/action state cannot authorize authored local framing");
    action.valid=true;
    for(auto movement:{Movement::Guide,Movement::Cover,Movement::Roll,Movement::Fall,Movement::Climb,Movement::Vehicle,Movement::Mounted,Movement::Rappel,Movement::Die}) {
        action.movementKind=movement;
        for(auto kind:{Action::MiniAct,Action::MiniAnim}) {
            action.actionKind=kind;
            Check(NativeCinematicKL::PhysicalActorCamera(action,action.player)&&
                  !NativeCinematicKL::ActorCameraAction(action,action.player)&&
                  !NativeCinematicKL::ActorScenePanel(action,action.player,false),
                  "physical movement accompanying MiniAct/MiniAnim retains current anatomical first-person camera");
            Check(NativeCinematicKL::FullScenePanel(NativeCinematicKL::State{true,1,0,0x19000000},false),
                  "verified full cut counter remains authoritative even while an owned physical movement exists");
        }
    }
    action.movementKind=Movement::BaseMove;
    for(auto kind:{Action::CloseCombat,Action::Pickup,Action::Reload,Action::Throw,Action::Vehicle,Action::Heal,Action::Mounted}) {
        action.actionKind=kind;
        Check(NativeCinematicKL::PhysicalActorCamera(action,action.player)&&
              !NativeCinematicKL::ActorScenePanel(action,action.player,false),
              "authored physical interactions never imply an external cinematic camera");
    }
    for(auto movement:{Movement::Unknown,Movement::Stand,Movement::BaseMove}) {
        action.movementKind=movement;action.actionKind=Action::MiniAnim;
        Check(NativeCinematicKL::PhysicalActorCamera(action,action.player)&&!NativeCinematicKL::ActorCameraAction(action,action.player),
              "local actor animation without a verified full-cut owner keeps first-person attachment");
    }
    action.movementKind=Movement::Climb;
    Check(!NativeCinematicKL::PhysicalActorCamera(action,action.player+4)&&!NativeCinematicKL::PhysicalActorCamera(action,0),
          "foreign physical movement cannot retain local first-person heading");
}
void AuxiliaryPipOwnership() {
    using namespace NativeCinematicKL;
    NativePipKL::State pip;pip.valid=true;pip.active=true;pip.camera=0x17000000;pip.geometry=0x18000000;
    Check(PipCameraPass(pip,pip.camera,pip.geometry),"exact active PIP geometry and its ordinary ZCAMERA source remain native");
    Check(!PipCameraPass(pip,pip.camera,pip.geometry+4)&&!PipCameraPass(pip,pip.camera+4,pip.geometry),
          "sharing a camera class or only one owner link cannot exclude an unrelated main camera pass");
    State full{true,1,2,pip.camera};
    Check(CompositePipPanel(full,pip),"a full controller explicitly selecting the active PIP source preserves its full native composition on a panel");
    full.camera+=4;Check(!CompositePipPanel(full,pip),"an active auxiliary PIP cannot flatten a different full main-scene camera");
    full={true,0,4,0};Check(!CompositePipPanel(full,pip),"ordinary gameplay/mini-dialogue stays stereo while another PIP is active");
    pip.active=false;
    Check(!PipCameraPass(pip,pip.camera,pip.geometry)&&!CompositePipPanel(State{true,1,0,pip.camera},pip),
          "inactive PIP identity cannot affect a later native camera lifetime");
    pip.active=true;pip.valid=false;
    Check(!PipCameraPass(pip,pip.camera,pip.geometry)&&!CompositePipPanel(State{true,1,0,pip.camera},pip),
          "unverified or changing PIP ownership cannot change main-camera handling");
}
void OptionalVRCinematics() {
    using namespace NativeCinematicKL;
    const State idle{true,0,0,0},dialogue{true,0,3,0};
    const State full{true,1,0,0x17000000},unowned{true,1,0,0};
    Check(!FullScenePanel(idle,false)&&!FullScenePanel(dialogue,false),"cinema preference preserves first-person gameplay and ordinary dialogue");
    Check(FullScenePanel(full,false)&&!FullScenePanel(full,true),"verified authored shot stays flat by default and permits VR only when opted in");
    Check(FullScenePanel(unowned,false)&&FullScenePanel(unowned,true),"opt-in cannot authorize a missing cinematic camera");
    Check(FullScenePanel(State{},false)&&FullScenePanel(State{},true),"unverified native state always retains conservative panel fallback");
}
void PauseOwnership() {
    unsigned char flags[2]{};
    const auto read=[&](std::uintptr_t at,void* output,std::size_t size){
        if(at!=Image+0x8883A4||size!=sizeof(flags))return false;
        std::memcpy(output,flags,size);return true;
    };
    Check(!NativeCinematicKL::PauseMenu(Image,read),"ordinary and resumed shots do not gain a pause panel");
    flags[0]=1;Check(NativeCinematicKL::PauseMenu(Image,read),"first native pause byte blocks culling and cached VR camera during authored shots");
    flags[0]=0;flags[1]=1;Check(NativeCinematicKL::PauseMenu(Image,read),"second native pause byte has the same priority");
    Check(!NativeCinematicKL::PauseMenu(Image+4,read),"missing native pause proof cannot manufacture a menu");
}
}
int main() {
    NativeTransitions();CorruptState();ExactStereoCamera();ActorCameraOwnership();AuxiliaryPipOwnership();OptionalVRCinematics();PauseOwnership();
    {NativeCinematicKL::State scene;scene.valid=true;scene.mini=1;
        NativeActionKL::State action;action.valid=true;action.player=7;action.action=8;
        action.actionKind=NativeActionKL::Action::MiniAct;action.movementKind=NativeActionKL::Movement::BaseMove;
        Check(NativeCinematicKL::FirstPersonSkip(scene,action,7),"local first-person MiniAct exposes native Enter skip without an external camera");
        action.movementKind=NativeActionKL::Movement::Climb;Check(!NativeCinematicKL::FirstPersonSkip(scene,action,7),"traversal actions never become movie-skip input");
        action.movementKind=NativeActionKL::Movement::BaseMove;scene.mini=0;Check(!NativeCinematicKL::FirstPersonSkip(scene,action,7),"actor animation alone cannot consume A as skip");}
    {using A=NativeActionKL::Action;using M=NativeActionKL::Movement;
        NativeCinematicKL::State scene{true,0,1,0};NativeActionKL::State action;
        action.valid=true;action.player=7;action.action=8;
        for(auto kind:{A::MiniAct,A::MiniAnim})for(auto move:{M::Stand,M::BaseMove}){
            action.actionKind=kind;action.movementKind=move;
            Check(NativeCinematicKL::AuthoredActorHeading(scene,action,7),"only verified upright mini action authorizes actor-root heading follow");}
        for(auto move:{M::Unknown,M::Guide,M::Cover,M::Climb,M::Fall,M::Rappel,M::Vehicle,M::Mounted,M::Die}){
            action.movementKind=move;Check(!NativeCinematicKL::AuthoredActorHeading(scene,action,7),"traversal seats mounted and unknown movement never gain MiniAnim root yaw");}
        action.movementKind=M::BaseMove;action.actionKind=A::Pickup;
        Check(!NativeCinematicKL::AuthoredActorHeading(scene,action,7),"ordinary pickup cannot turn stable gameplay heading");
        action.actionKind=A::MiniAct;scene.full=1;
        Check(!NativeCinematicKL::AuthoredActorHeading(scene,action,7),"accepted full VR cinema remains outside actor heading policy");
        scene.full=0;scene.mini=0;Check(!NativeCinematicKL::AuthoredActorHeading(scene,action,7),"ending or skipped mini counter retires authored heading owner");
        scene.mini=1;Check(!NativeCinematicKL::AuthoredActorHeading(scene,action,9),"foreign actor cannot turn local stable gameplay heading");}
    std::printf("Native cinematic camera/rig gate: %u checks passed.\n",checks);return 0;
}
