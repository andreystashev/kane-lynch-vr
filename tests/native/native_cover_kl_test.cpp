#include "native_cover_kl.hpp"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <limits>

namespace {
unsigned checks=0;
void Check(bool okay,const char* name){++checks;if(!okay){std::fprintf(stderr,"FAIL %s\n",name);std::exit(1);}}
constexpr std::uintptr_t Image=0x400000,Data=0x10000000,Player=0x11000000;
constexpr std::uintptr_t Move=0x12000000,Cover=0x12100000,Controller=0x13000000,Dispatcher=0x14000000;
struct Memory {
    std::map<std::uintptr_t,unsigned char> bytes;
    template<class T>void Set(std::uintptr_t at,const T& value){const auto* source=reinterpret_cast<const unsigned char*>(&value);for(std::size_t i=0;i<sizeof(T);++i)bytes[at+i]=source[i];}
    bool Read(std::uintptr_t at,void* output,std::size_t size)const {
        auto* target=static_cast<unsigned char*>(output);for(std::size_t i=0;i<size;++i){const auto found=bytes.find(at+i);if(found==bytes.end())return false;target[i]=found->second;}return true;
    }
    Memory() {
        const std::pair<std::uintptr_t,std::uint32_t> fields[]={
            {Image+0x68760C,Data},{Data+0xA2C,Player},{Player,Image+0x56582C},
            {Player+0x67C,Move},{Move,Image+0x566A44},{Move+0x38,Player},{Move+0x44,2},{Player+0x600,Move},
            {Player+0x194,Dispatcher},{Dispatcher,Image+0x54DE4C},{Dispatcher+8,Player},{Dispatcher+4,0},
            {Image+0x6444B4,1},{Player+0x7D0,0},{Player+0x9B0,0},{Player+0x9B8,Controller},
            {Player+0x5F8+7*4,Cover},{Cover,Image+0x566EAC},{Cover+0x38,Player},{Cover+0xDC,Player},{Cover+0x44,7}};
        for(const auto& field:fields)Set(field.first,field.second);
        Set(Player+0x1288,0.f);Set(Player+0x96C,0.f);Set(Player+0x970,0.f);
        Set(Player+0x12DD,std::int8_t(0));Set(Controller+0x18,std::uint8_t(0));
        Set(Cover+0xD4,std::int32_t(-1));Set(Cover+0x1AC,std::int32_t(0));
        Set(Cover+0xE1,std::uint8_t(0));
        Set(Player+0x12F8,NativeCameraMath::Vec3{0,0,-1});
    }
    auto Reader()const{return [&](auto address,auto* output,auto size){return Read(address,output,size);};}
    NativeCoverKL::State Get()const{return NativeCoverKL::Snapshot(Image,Reader());}
};
void Heading() {
    using namespace NativeCameraMath;Basis basis;
    Check(NativeCoverKL::BasisFromYaw(0,basis)&&Dot(basis.forward,{0,0,1})>.99999f&&Dot(basis.right,{1,0,0})>.99999f,"native zero-yaw moves W +Z and D +X");
    Check(NativeCoverKL::BasisFromYaw(1.57079632679f,basis)&&Dot(basis.forward,{1,0,0})>.99999f&&Dot(basis.right,{0,0,-1})>.99999f,"native positive quarter turn moves W +X and D -Z");
    for(int turn=-36;turn<=36;++turn) {
        const float yaw=turn*.1745329252f;
        Check(NativeCoverKL::BasisFromYaw(yaw,basis)&&ValidateBasis(basis),"native input basis remains orthonormal and reflected FUR");
        // A world-space desired heading reconstructed from its axes must be
        // invariant as the original third-person input camera orbits.
        for(int aim=-8;aim<=8;++aim) {
            const Vec3 target{std::sin(aim*.3926990817f),0,std::cos(aim*.3926990817f)};
            const float right=Dot(target,basis.right),forward=Dot(target,basis.forward);
            const auto reconstructed=Add(Scale(basis.right,right),Scale(basis.forward,forward));
            Check(Dot(Subtract(target,reconstructed),Subtract(target,reconstructed))<1e-10f,"head-relative stick reconstructs the same world direction through native input yaw");
        }
    }
    const auto saved=basis;
    Check(!NativeCoverKL::BasisFromYaw(std::numeric_limits<float>::quiet_NaN(),basis)&&!std::memcmp(&saved,&basis,sizeof(basis)),"invalid native yaw preserves caller output");
}
void Ownership() {
    Memory memory;NativeCameraMath::Basis basis;
    Check(NativeCoverKL::InputBasisSnapshot(Image,Player,memory.Reader(),basis),"owned local input heading accepted");
    Check(!NativeCoverKL::InputBasisSnapshot(Image,Player+4,memory.Reader(),basis),"another player cannot supply input heading");
    memory.Set(Player+0x9B0,std::uint32_t(0x15000000));
    Check(!NativeCoverKL::InputBasisSnapshot(Image,Player,memory.Reader(),basis),"missing guide-base metadata cannot be misread as yaw-only walking");
    memory=Memory{};memory.Set(Player+0x1288,std::numeric_limits<float>::infinity());
    Check(!NativeCoverKL::InputBasisSnapshot(Image,Player,memory.Reader(),basis),"nonfinite native input yaw rejected");
    memory=Memory{};unsigned reads=0;
    const auto changing=[&](std::uintptr_t at,void* output,std::size_t size){if(at==Player+0x1288&&++reads==2)memory.Set(at,1.f);return memory.Read(at,output,size);};
    Check(!NativeCoverKL::InputBasisSnapshot(Image,Player,changing,basis),"inconsistent input heading snapshot rejected");
    memory=Memory{};memory.Set(Player,std::uint32_t(Image+0x56477C));
    Check(!NativeCoverKL::InputBasisSnapshot(Image,Player,memory.Reader(),basis),"NPC vtable cannot supply local movement basis");
}
void GuideFrame() {
    using namespace NativeCameraMath;
    constexpr std::uint32_t Guide=0x15000000,Entity=0x15100000,Geometry=0x15200000,Parent=0x15300000;
    Memory memory;memory.Set(Player+0x9B0,Guide);memory.Set(Guide+0x24,Entity);
    memory.Set(Entity+4,Geometry);memory.Set(Geometry+0x60,Entity);
    NativePlayerPose::Geometry geometry{},parent{};
    NativeCoverKL::BasisFromYaw(.7f,geometry.pose.basis);geometry.pose.position={100,200,300};geometry.parent=Parent;
    NativeCoverKL::BasisFromYaw(-1.2f,parent.pose.basis);parent.pose.position={-400,500,600};
    memory.Set(Geometry,geometry);memory.Set(Parent,parent);
    Basis output,expected;NativeCoverKL::BasisFromYaw(-.5f,expected);
    Check(NativeCoverKL::InputBasisSnapshot(Image,Player,memory.Reader(),output),"ordinary camera guide transform supplies real input basis");
    Check(Dot(output.forward,expected.forward)>.99999f&&Dot(output.right,expected.right)>.99999f,"guide vector transform includes parent rotations in native order");
    const auto saved=output;geometry.pose.position={9000,-8000,7000};parent.pose.position={2000,-3000,4000};
    memory.Set(Geometry,geometry);memory.Set(Parent,parent);
    Check(NativeCoverKL::InputBasisSnapshot(Image,Player,memory.Reader(),output)&&!std::memcmp(&saved,&output,sizeof(output)),"native movement-vector frame ignores all geometry translations");
    memory.Set(Player+0x1288,std::numeric_limits<float>::quiet_NaN());
    Check(NativeCoverKL::InputBasisSnapshot(Image,Player,memory.Reader(),output),"active guide frame does not use unrelated yaw-only fallback");
    memory.Set(Geometry+0x60,std::uint32_t(Entity+4));
    Check(!NativeCoverKL::InputBasisSnapshot(Image,Player,memory.Reader(),output),"stale guide geometry owner is rejected");
    memory.Set(Geometry+0x60,Entity);parent.parent=Geometry;memory.Set(Parent,parent);
    Check(!NativeCoverKL::InputBasisSnapshot(Image,Player,memory.Reader(),output),"cyclic guide ancestry is rejected");
    parent.parent=0;memory.Set(Parent,parent);geometry.pose.basis.up={0,3,0};memory.Set(Geometry,geometry);
    Check(!NativeCoverKL::InputBasisSnapshot(Image,Player,memory.Reader(),output),"scaled malformed guide basis is rejected");
    NativeCoverKL::BasisFromYaw(.7f,geometry.pose.basis);memory.Set(Geometry,geometry);
    unsigned guideReads=0;
    const auto replacing=[&](std::uintptr_t at,void* destination,std::size_t size){if(at==Player+0x9B0&&++guideReads==2)memory.Set(at,std::uint32_t(0));return memory.Read(at,destination,size);};
    Check(!NativeCoverKL::InputBasisSnapshot(Image,Player,replacing,output),"guide replacement across native player snapshots is rejected");
}
void CoverState() {
    Memory memory;auto state=memory.Get();
    Check(state.valid&&state.enabled&&!state.active&&state.candidateSide==0&&state.inputBasisValid,"ordinary movement exposes native automatic-cover diagnostic only");
    memory.Set(Player+0x12DD,std::int8_t(-1));state=memory.Get();
    Check(state.valid&&state.candidateSide==-1&&!state.active,"native candidate is not reported as actual cover entry");
    memory.Set(Player+0x67C,std::uint32_t(Cover));memory.Set(Cover+0xD4,std::int32_t(10));state=memory.Get();
    Check(state.valid&&state.active&&state.movementId==7&&state.phase==10,"native cover ID7 is distinct from diagnostic Movement::Cover enum4");
    memory.Set(Cover+0xE1,std::uint8_t(1));state=memory.Get();
    Check(state.valid&&state.active&&state.crouched,"active cover exposes its actual native crouch latch");
    memory.Set(Cover+0xE1,std::uint8_t(2));Check(!memory.Get().valid,"malformed active cover crouch latch is rejected");
    memory.Set(Cover+0xE1,std::uint8_t(0));
    memory.Set(Cover+0xDC,std::uint32_t(Player+4));Check(!memory.Get().valid,"foreign cover-specific owner rejected");
    memory=Memory{};memory.Set(Player+0x12DD,std::int8_t(2));Check(!memory.Get().valid,"unknown native cover candidate rejected");
    memory=Memory{};memory.Set(Image+0x6444B4,std::uint32_t(0));state=memory.Get();
    Check(state.valid&&!state.enabled&&!state.active,"native disabled-cover setting remains disabled");
    memory=Memory{};memory.Set(Controller+0x18,std::uint8_t(1));state=memory.Get();
    Check(state.valid&&state.controllerBlocked,"native controller block is visible and never cleared");
}
void ScriptProximity() {
    NativeCoverKL::Proximity p;
    using NativeCameraMath::Vec3;
    Check(!p.Update(Player,{10,20,30},0,{},100,true),"standing by an unverified wall cannot invent a cover candidate");
    Check(p.Update(Player,{10,20,30},1,{0,0,1},100,true),"native geometric cover candidate establishes script proximity");
    Check(p.Update(Player,{30,20,30},0,{},100,true),"stopping the stick near a real cover retains proximity without sticky locomotion");
    Check(!p.Update(Player,{76,20,30},0,{},100,true)&&!p.player,"walking away retires script cover instead of granting it everywhere");
    Check(!p.Update(Player,{10,20,30},0,{},100,true),"returning to an expired point requires native geometry confirmation");
    Check(p.Update(Player,{10,20,30},-1,{1,0,0},100,true)&&
          !p.Update(Player+4,{10,20,30},0,{},100,true),"a different actor cannot inherit cover proximity");
    Check(p.Update(Player,{10,20,30},1,{0,0,1},100,true)&&
          !p.Update(Player,{10,80,30},0,{},100,true),"another floor cannot inherit the old cover point");
    Check(!p.Update(Player,{},1,{},100,true)&&!p.Update(Player,{},1,{0,0,1},100,false),"invalid geometry, blocked input and native actions cannot generate cover credit");
    Check(!p.Update(Player,{},2,{0,0,1},100,true)&&!p.Update(Player,{},1,{0,0,1},0,true),"invalid candidate and units are rejected");
    Check(p.Update(Player,{},1,{0,0,1},100,true),"a new native candidate recovers proximity");
    const auto saved=p;
    Check(saved.player==Player&&saved.position.x==0,"script proximity stores observations only, not actor movement or protection flags");
}
void AutomaticAimCredit() {
    using namespace NativeCoverKL;
    using NativeCameraMath::Vec3;
    Memory memory;memory.Set(Player+0x12DD,std::int8_t(1));
    const auto cover=memory.Get();const auto action=NativeActionKL::Snapshot(Image,memory.Reader());
    const auto untouchedMemory=memory.bytes;
    const Vec3 wall{10,20,30};ScriptPolicy policy;
    auto credit=policy.Update(Player,cover,action,wall,0,100,true);
    Check(credit.stance==3&&credit.aim==5&&credit.freshCandidate,
          "verified standing VR proximity automatically supplies stance then aim without RMB");
    unsigned tutorial=0;
    for(unsigned event:{credit.stance,credit.aim}) {
        if(tutorial==0&&event==3)tutorial=1;
        else if(tutorial==1&&event==5)tutorial=2;
    }
    Check(tutorial==2,"standing cover notification precedes aim so a synchronously enabled tutorial stage can receive it");
    auto stopped=cover;stopped.candidateSide=0;stopped.normal={};
    credit=policy.Update(Player,stopped,action,wall,0,100,true);
    Check(credit.stance==3&&credit.aim==5&&!credit.freshCandidate,
          "stationary proximity retries aim for monitors enabled on a later native update");
    credit=policy.Update(Player,stopped,action,wall,0x400,100,true);
    Check(credit.stance==4&&credit.aim==6,
          "native crouch input automatically supplies matching crouched cover and aim events");
    Check(memory.bytes==untouchedMemory&&cover.actorFlags==0,
          "script credit does not write native aim, actor state, camera, damage or tutorial memory");
    Check(!ScriptPolicy{}.Update(Player,stopped,action,wall,0,100,true).aim,
          "a wall without a confirmed native geometric candidate cannot grant automatic aim");
    Check(!ScriptPolicy{}.Update(Player,cover,action,wall,0x40,100,true).aim,
          "native input aim leaves a stale candidate and cannot establish a new proximity point");
    auto actorAiming=cover;actorAiming.actorFlags=0x40;
    Check(!ScriptPolicy{}.Update(Player,actorAiming,action,wall,0,100,true).aim,
          "actor aim flags also prevent an unrefreshed candidate from establishing proximity");
    credit=policy.Update(Player,cover,action,{40,20,30},0x40,100,true);
    Check(credit.aim==5&&policy.proximity.position.x==wall.x,
          "holding native aim retains nearby credit without dragging its recorded geometric point");
    Check(!policy.Update(Player,cover,action,{76,20,30},0x40,100,true).aim&&!policy.proximity.player,
          "walking away while aiming retires both automatic stance and aim credit");
    Check(!policy.Update(Player,stopped,action,wall,0,100,true).aim,
          "returning after leaving requires a new native candidate");

    const auto reject=[&](State changed,NativeActionKL::State changedAction,bool vr,const char* reason) {
        ScriptPolicy current;
        Check(current.Update(Player,cover,action,wall,0,100,true).aim==5,"lifecycle fixture establishes real proximity");
        Check(!current.Update(Player,changed,changedAction,wall,0,100,vr).aim&&!current.proximity.player,reason);
        Check(!current.Update(Player,stopped,action,wall,0,100,true).aim,
              "lifecycle recovery cannot reuse a retired proximity point");
    };
    reject(cover,action,false,"VR suspension or cinematic entry clears automatic credit");
    auto changed=cover;changed.valid=false;reject(changed,action,true,"invalid local cover snapshot clears automatic credit");
    changed=cover;changed.enabled=false;reject(changed,action,true,"native disabled-cover setting cannot grant script credit");
    changed=cover;changed.controllerBlocked=true;reject(changed,action,true,"blocked native input clears automatic credit");
    changed=cover;changed.active=true;reject(changed,action,true,"real native cover retains ownership of its notifications");
    changed=cover;changed.player=Player+4;reject(changed,action,true,"foreign player cover state clears automatic credit");
    auto changedAction=action;changedAction.valid=false;reject(cover,changedAction,true,"invalid action snapshot clears automatic credit");
    changedAction=action;changedAction.player=Player+4;reject(cover,changedAction,true,"action ownership changes cannot inherit proximity");
    changedAction=action;changedAction.action=0x15000000;changedAction.actionKind=NativeActionKL::Action::Reload;
    reject(cover,changedAction,true,"native reload or constrained actions clear automatic cover credit");
    for(auto movement:{NativeActionKL::Movement::Unknown,NativeActionKL::Movement::Cover,
                       NativeActionKL::Movement::Guide,NativeActionKL::Movement::Roll,
                       NativeActionKL::Movement::Fall,NativeActionKL::Movement::Climb,
                       NativeActionKL::Movement::Vehicle,NativeActionKL::Movement::Mounted,
                       NativeActionKL::Movement::Rappel,NativeActionKL::Movement::Die}) {
        changedAction=action;changedAction.movementKind=movement;
        reject(cover,changedAction,true,"only native Stand and BaseMove can create automatic proximity credit");
    }
    changedAction=action;changedAction.movementKind=NativeActionKL::Movement::BaseMove;
    Check(policy.Update(Player,cover,changedAction,wall,0,100,true).aim==5,
          "normal BaseMove remains eligible for automatic cover aim");
    changed=cover;changed.player=Player+4;changed.candidateSide=0;changedAction.player=Player+4;
    Check(!policy.Update(Player+4,changed,changedAction,wall,0,100,true).aim,
          "a replacement local player must confirm its own geometric cover point");
    changed.candidateSide=-1;
    Check(policy.Update(Player+4,changed,changedAction,wall,0x400,100,true).aim==6,
          "new player and either native candidate side recover crouched automatic credit");

    ScriptPolicy shooting;
    Check(!shooting.BlindFire(Player,cover,action,wall,0,100,true),
          "a shot cannot invent cover proximity from an unconsumed candidate");
    shooting.Update(Player,cover,action,wall,0,100,true);
    Check(shooting.BlindFire(Player,cover,action,wall,0,100,true)==9&&
          shooting.BlindFire(Player,cover,action,wall,0x400,100,true)==10,
          "real native shots retain independent standing and crouched blind-fire credit");
    Check(!shooting.BlindFire(Player,cover,action,wall,0x40,100,true)&&shooting.proximity.player==Player,
          "native aimed shots are not converted into blind fire by automatic script aim");
    Check(!shooting.BlindFire(Player,cover,action,{76,20,30},0,100,true)&&!shooting.proximity.player,
          "a shot outside the last confirmed cover radius cannot receive stale blind-fire credit");
    shooting.Update(Player,cover,action,wall,0,100,true);
    Check(!shooting.BlindFire(Player,cover,action,wall,0,100,false)&&!shooting.proximity.player,
          "VR lifecycle failure before a shot retires its script proximity");
}
void ActiveCoverAimCredit() {
    using namespace NativeCoverKL;
    Memory memory;memory.Set(Player+0x67C,std::uint32_t(Cover));
    auto cover=memory.Get();auto action=NativeActionKL::Snapshot(Image,memory.Reader());
    const auto original=memory.bytes;ScriptPolicy policy;
    Check(cover.active&&cover.candidateSide==0,"active native fixture has no free-movement candidate");
    auto credit=policy.Update(Player,cover,action,{},0x400,100,true);
    Check(credit.stance==3&&credit.aim==5&&!credit.freshCandidate&&!policy.proximity.player,
          "actual standing Cover credits aim without proximity and ignores a pending crouch button");
    cover.crouched=true;credit=policy.Update(Player,cover,action,{},0,100,true);
    Check(credit.stance==4&&credit.aim==6,"actual crouched Cover uses native posture even with crouch input released");
    Check(!policy.BlindFire(Player,cover,action,{},0,100,true),"native active cover retains ownership of blind-fire notifications");
    auto inactive=cover;inactive.active=false;action.movementKind=NativeActionKL::Movement::BaseMove;
    Check(!policy.Update(Player,inactive,action,{},0,100,true).aim,
          "leaving active Cover requires new native geometry before free-movement credit");
    action=NativeActionKL::Snapshot(Image,memory.Reader());
    const auto reject=[&](State changed,NativeActionKL::State changedAction,bool vr,const char* reason){
        Check(!ScriptPolicy{}.Update(Player,changed,changedAction,{},0,100,vr).aim,reason);
    };
    reject(cover,action,false,"suspended VR cannot auto-credit native cover");
    auto changed=cover;changed.controllerBlocked=true;reject(changed,action,true,"controller block suppresses active cover credit");
    changed=cover;changed.enabled=false;reject(changed,action,true,"disabled native cover suppresses active credit");
    changed=cover;changed.player=Player+4;reject(changed,action,true,"foreign active Cover cannot credit local player");
    auto changedAction=action;changedAction.movement=Cover+4;reject(cover,changedAction,true,"active movement must be the exact registered Cover");
    changedAction=action;changedAction.movementId=4;reject(cover,changedAction,true,"diagnostic enum value cannot substitute for native Cover ID7");
    changedAction=action;changedAction.action=0x15000000;changedAction.actionKind=NativeActionKL::Action::Reload;
    reject(cover,changedAction,true,"native reload retains ownership during active Cover");
    changedAction=action;changedAction.movementKind=NativeActionKL::Movement::Guide;
    reject(cover,changedAction,true,"an unrelated movement cannot use an old active Cover snapshot");
    Check(memory.bytes==original,"active auto-credit changes no native actor input or state");
}
void DeliberateEntry() {
    using namespace NativeCoverKL;
    Memory memory;const auto original=memory.bytes;
    const auto cover=memory.Get();const auto action=NativeActionKL::Snapshot(Image,memory.Reader());
    Check(HeldEntryEligible(Player,cover,action,true,true),"held cover permits only calling native geometry eligibility");
    Check(!HeldEntryEligible(Player,cover,action,false,true),"short B or release does not allow sticky cover entry");
    Check(!HeldEntryEligible(Player,cover,action,true,false),"suspended VR cannot enter cover from old input");
    auto changed=cover;changed.active=true;
    Check(!HeldEntryEligible(Player,changed,action,true,true),"active cover retains native lifecycle rather than a second entry");
    changed=cover;changed.cover=0;
    Check(!HeldEntryEligible(Player,changed,action,true,true),"missing owned cover object rejected");
    changed=cover;changed.player=Player+4;
    Check(!HeldEntryEligible(Player,changed,action,true,true),"foreign cover cannot consume local controller hold");
    changed=cover;changed.enabled=false;
    Check(!HeldEntryEligible(Player,changed,action,true,true),"native disabled cover remains disabled");
    changed=cover;changed.controllerBlocked=true;
    Check(!HeldEntryEligible(Player,changed,action,true,true),"blocked native controller cannot start cover");
    auto altered=action;altered.movement=0;
    Check(!HeldEntryEligible(Player,cover,altered,true,true),"missing movement proof cannot enter cover");
    altered=action;altered.movementKind=NativeActionKL::Movement::Rappel;
    Check(!HeldEntryEligible(Player,cover,altered,true,true),"rappel cannot change to cover from B hold");
    altered=action;altered.action=0x15000000;altered.actionKind=NativeActionKL::Action::Reload;
    Check(!HeldEntryEligible(Player,cover,altered,true,true),"reload action owns eligibility until it completes");
    Check(memory.bytes==original,"entry permission never edits actor state or geometry");
}
}
int main(){Heading();Ownership();GuideFrame();CoverState();ScriptProximity();AutomaticAimCredit();ActiveCoverAimCredit();DeliberateEntry();std::printf("Native cover/control heading: %u checks passed.\n",checks);return 0;}
