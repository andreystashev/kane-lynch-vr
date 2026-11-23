// Actual native command callback, receiver proof, hostile-contact selection,
// TLS getter guards and result delivery. Only native AI/ray/runtime entry
// points are substituted; no game, headset, input or foreign process opened.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
namespace FClock {inline DWORD tick=1000;inline DWORD Clock(){return tick;}}
#define GetTickCount FClock::Clock
#include "native_aim_kl.cpp"
#include "native_team_commands_kl.cpp"
#include <cstdio>
#include <cstdlib>

namespace F {
namespace T=TeamCommandIPC;
using Vec3=NativeCameraMath::Vec3;
unsigned checks=0,nativeRecall=0,nativeMove=0,nativeAttack=0,originalTargets=0,originalFallbacks=0,rayQueries=0;
unsigned queryMutation=0;
unsigned nativeAssistCalls=0,originalOriginCalls=0;bool targetLod=true,assistPoseChanges=false;
std::uint32_t roomReference=1;
Vec3 assistedPoint{0,0,1000};
bool ready=true,team=true,wheel=false,sim=true,rayHit=true,lastAll=false,poseAvailable=true,equipDuringRay=false,headMovesDuringRay=false;
long ack=7;
std::uintptr_t image=0,player=0,control=0,mover=0,actor=0,member=0;
T::Request request;T::Result result;
NativeWeaponKL::AimFrame pose;
NativeWeaponKL::HeadGaze head;
NativeActionKL::State action;
Vec3 nativePoint{};
void Check(bool okay,const char* text){++checks;if(!okay){std::printf("FAIL %s\n",text);std::exit(1);}}
template<class V>void Put(std::uintptr_t at,const V& value){std::memcpy(reinterpret_cast<void*>(at),&value,sizeof(value));}
void Advance(void* object,bool all){auto at=reinterpret_cast<std::uintptr_t>(object);std::uint32_t n=0;std::memcpy(&n,reinterpret_cast<void*>(at+8),4);Put(at+8,n+1);lastAll=all;}
void __thiscall Recall(void* object,bool all){++nativeRecall;Advance(object,all);}
void __thiscall Move(void* object,bool all,const Vec3* point){++nativeMove;Advance(object,all);nativePoint=*point;}
std::uintptr_t __thiscall Room(void*,NativeAimKLMath::Query* ray,unsigned a,unsigned b,unsigned c,unsigned d,unsigned e,unsigned f){
    ++rayQueries;Check(a==0&&b==6&&c==UINT32_MAX&&d==1&&e==1&&f==UINT32_MAX,"actual native ROOM query preserves exact native flags");
    if(!rayHit)return 0;
    ray->fraction=NativeTeamCommandsKL::assisted.snapshot?1.f:.1f;ray->reference=roomReference;
    if(equipDuringRay)Put(player+0x358+0x60,std::uint32_t(pose.weapon));
    if(headMovesDuringRay){auto changed=head;changed.origin.x+=1;NativeTeamCommandsKL::PublishHeadRay(changed);}
    if(queryMutation==1)ray->start.x+=1;
    if(queryMutation==2)Put(image+0xE3000+0x3C,std::uint32_t(0x400));
    if(queryMutation==3)Put(control+0x10,std::uint32_t(control+0x1C));
    return 1;
}
std::uint32_t __thiscall NativeTarget(void*,Vec3*){++originalTargets;return 0xBAD;}
std::uint32_t __thiscall NativeFallback(void*){++originalFallbacks;return 0xBAD;}
void __thiscall Attack(void* object,bool all,float duration){
    ++nativeAttack;Advance(object,all);Check(duration==1000.f,"native single-press duration is preserved");
    using Target=std::uint32_t(__thiscall*)(void*,Vec3*);
    using Fallback=std::uint32_t(__thiscall*)(void*);
    Vec3 point{};const auto reference=reinterpret_cast<Target>(image+0x213EB8)(reinterpret_cast<void*>(mover),&point);
    Check(reference==1&&std::fabs(point.z-assistedPoint.z)<.01f,"actual TargetHook supplies selected native actor contact only at exact native Attack caller");
    Check(reinterpret_cast<Fallback>(image+0x213EFC)(reinterpret_cast<void*>(mover))==1,"actual fallback getter guard stays in scoped attack target");
}
void* __thiscall OriginalOrigin(void*,void* output){++originalOriginCalls;const Vec3 original{-1,-1,-1};std::memcpy(output,&original,sizeof(original));return output;}
std::uint32_t __thiscall Assist(void* snapshot,unsigned mode){++nativeAssistCalls;
    Check(mode==1&&reinterpret_cast<std::uintptr_t>(snapshot)!=mover,"native picker receives mode1 on private camera snapshot only");
    Vec3 origin{},forward{};std::memcpy(&forward,static_cast<unsigned char*>(snapshot)+0x34,sizeof(forward));
    using Origin=void*(__thiscall*)(void*,void*);
    Check(reinterpret_cast<Origin>(image+0x231ECC)(snapshot,&origin)==&origin&&origin.x==pose.origin.x&&origin.z==pose.origin.z,
          "exact native GetOrigin return caller uses current rendered ray instead of native camera");
    float angle=0;std::memcpy(&angle,static_cast<unsigned char*>(snapshot)+0x288,sizeof(angle));
    const auto delta=NativeCameraMath::Subtract(assistedPoint,origin);Vec3 direction;NativeAimKLMath::Unit(delta,direction);
    const float radians=std::acos(std::max(-1.f,std::min(1.f,NativeCameraMath::Dot(direction,forward))));
    Check(angle==NativeTeamAssistKL::CommandCone||angle==NativeTeamAssistKL::PrecisionCommandCone,
          "the private native picker receives the widened order cone");
    if(assistPoseChanges)pose.origin.x+=1;
    return targetLod&&radians<angle?1:0;
}
void* __thiscall Point(void*,void* output,void* selected){Check(reinterpret_cast<std::uintptr_t>(selected)==actor,"native point getter receives verified hostile actor");
    std::memcpy(output,&assistedPoint,sizeof(assistedPoint));return output;}
void Code(std::uintptr_t at,std::uintptr_t hook,bool argument){
    // Prefix supplies EAX as the actual wrapper does; the call ends at the
    // pinned native return RVA. This lets the production return guard run.
    const unsigned prefix=argument?13:5,total=prefix+7;
    std::memset(reinterpret_cast<void*>(at),0x90,total+3);
    Put(at,std::uint8_t(0x8B));Put(at+1,std::uint8_t(0xC1));
    if(argument){const unsigned char push[]={0xFF,0x74,0x24,4};std::memcpy(reinterpret_cast<void*>(at+2),push,sizeof(push));}
    const unsigned char call[]={0x8B,0xC8,0xE8};std::memcpy(reinterpret_cast<void*>(at+prefix),call,sizeof(call));
    const auto displacement=static_cast<std::int32_t>(hook-(at+total));Put(at+prefix+3,displacement);
    if(argument){const unsigned char ret[]={0xC2,4,0};std::memcpy(reinterpret_cast<void*>(at+total),ret,sizeof(ret));}
    else Put(at+total,std::uint8_t(0xC3));
}
LONG WINAPI Crash(EXCEPTION_POINTERS* info){std::printf("FAIL: private callback exception %08lx at %p (fixture image=%p, checks=%u)\n",info->ExceptionRecord->ExceptionCode,info->ExceptionRecord->ExceptionAddress,reinterpret_cast<void*>(image),checks);return EXCEPTION_EXECUTE_HANDLER;}
void Setup(){
    image=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,0x900000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));Check(image!=0,"private 32-bit mapped fixture allocated");
    const auto data=image+0x10000,squad=image+0x50000,manager=image+0x60000,pool=image+0x61000,geo=image+0x62000,gen=image+0x63000;
    player=image+0x20000;control=image+0x30000;member=image+0x40000;mover=image+0x70000;actor=image+0x80000;
    Put(image+0x68760C,std::uint32_t(data));Put(data+0xA2C,std::uint32_t(player));Put(player,std::uint32_t(image+0x56582C));
    Put(player+0xAA8,std::uint32_t(control));Put(control,std::uint32_t(image+NativeTeamCommandsImage::ControlVtable));Put(control+4,std::uint32_t(player));
    Put(control+0x14,std::uint32_t(1));Put(control+0x10,std::uint32_t(control+0x18));Put(control+0x18,std::uint32_t(member));Put(control+0xA0,std::uint32_t(squad));
    Put(member+0x238,std::uint32_t(player));Put(member+0xE8,std::uint32_t(squad));Put(member+0x261,std::uint8_t(1));Put(member+0x2DC,std::uint32_t(19));
    Put(data+4,std::uint32_t(1));Put(data+8,std::uint32_t(actor));Put(actor,std::uint32_t(image+NativeTeamCommandsImage::ActorVtable));Put(actor+4,std::uint32_t(geo));
    Put(actor+0x536,std::uint8_t(1));Put(geo+0x60,std::uint32_t(actor));Put(actor+0x54,std::uint32_t(image+0x90000));Put(image+0x90004,std::uint32_t(3));
    Put(player+0x54,std::uint32_t(image+0xA0000));Put(image+0xA000C,std::uint32_t(1u<<3));
    Put(image+0x6C99B8,std::uint32_t(manager));Put(manager+0xC,std::uint32_t(pool));Put(pool+4,std::uint32_t(0x70));Put(pool+8,std::uint32_t(8));Put(pool+0xC,std::uint32_t(geo));Put(pool+0x10,std::uint32_t(gen));
    Put(player+0x9AC,std::uint32_t(mover));Put(mover,std::uint32_t(image+NativeCameraMoverKL::MouseVtable));Put(mover+8,std::uint32_t(player));
    Put(mover+0xC,std::uint32_t(image+0xF0000));Put(image+0x56AC7C,.418879032f);Put(image+0x56AC84,.0872664601f);Put(image+0x646288,625000000.f);
    Put(image+0x53C9A4,1000.f);
    Code(image+0x213EB8,reinterpret_cast<std::uintptr_t>(NativeTeamCommandsKL::TargetHook),true);
    Code(image+0x213EFC,reinterpret_cast<std::uintptr_t>(NativeTeamCommandsKL::FallbackHook),false);
    Code(image+0x231ECC,reinterpret_cast<std::uintptr_t>(NativeTeamCommandsKL::AssistOriginHook),true);
    FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(image+0x231ECC),0x30);
    FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(image+0x213EB8),0x58);
    NativeTeamCommandsKL::image=image;NativeTeamCommandsKL::installed=true;NativeTeamCommandsKL::recall=Recall;NativeTeamCommandsKL::move=Move;NativeTeamCommandsKL::attack=Attack;
    NativeTeamCommandsKL::target=NativeTarget;NativeTeamCommandsKL::fallback=NativeFallback;
    NativeTeamCommandsKL::assistPicker=Assist;NativeTeamCommandsKL::assistPoint=Point;NativeTeamCommandsKL::assistOrigin=OriginalOrigin;
    pose.player=player;pose.weapon=image+0xB0000;pose.root=geo+0x70;pose.model=image+0xC0000;pose.pairId=1;pose.hand=1;pose.recenterAck=ack;pose.unitsPerMetre=100;pose.direction={0,0,1};
    const auto camera=image+0xE5000,app=image+0xE0000,engine=image+0xE1000,room=image+0xE2000,roomGeo=image+0xE3000,backend=image+0xE4000;
    Put(player+0x9A0,std::uint32_t(camera));Put(camera,std::uint32_t(image+0x545A3C));
    for(unsigned side=0;side<2;++side){const auto hand=player+(side?0x358:0x2D8);Put(hand,std::uint32_t(image+0x5508B4));
        NativeWeaponOwnership::HandState held;held.item=side?static_cast<std::uint32_t>(pose.weapon):0;held.enabled=1;Put(hand+0x60,held);}
    Put(image+0x6875F0,std::uint32_t(app));Put(app+0xA8,std::uint32_t(engine));Put(engine+0x8C,std::uint32_t(room));
    Put(room,std::uint32_t(image+0x54A72C));Put(room+4,std::uint32_t(roomGeo));Put(image+0x54A72C+0x26C,std::uint32_t(image+NativeAimKLImage::RoomRay));
    Put(image+0x7C1D34,std::uint32_t(backend));Put(backend,std::uint32_t(image+0x5411C4));Put(image+0x5411C4+0x2C,std::uint32_t(image+NativeAimKLImage::BackendRay));
    Put(image+NativeAimKLImage::RoomRay,std::uint8_t(0xE9));Put(image+NativeAimKLImage::RoomRay+1,
        static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(&Room)-(image+NativeAimKLImage::RoomRay+5)));
    FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(image+NativeAimKLImage::RoomRay),5);
    NativeAimKL::image=image;NativeAimKL::installed=true;
    action.valid=true;action.player=player;action.movement=image+0xD0000;action.movementKind=NativeActionKL::Movement::BaseMove;
    head.player=player;head.sourceCamera=camera;head.pairId=2;head.recenterAck=ack;head.unitsPerMetre=100;head.origin={0,200,0};head.direction={0,0,1};head.mounted=true;
}
void New(T::Command command){const auto serial=request.serial+1;request={};request.serial=serial;request.command=command;request.tick=GetTickCount();request.recenter=ack;request.selected=NativeTeamCommandsKL::ReceiverSnapshot(request.tick);pose.tick=request.tick;result={};}
}
namespace VRBridge {
bool Ready(){return F::ready;}long RecenterAck(){return F::ack;}bool TeamHudVisible(){return F::team;}bool WeaponMenuVisible(){return F::wheel;}
DWORD TeamNextTick(){return 0;}
bool ReadTeamCommand(TeamCommandIPC::Request& output){output=F::request;return true;}
void PublishTeamCommandResult(const TeamCommandIPC::Result& value){F::result=value;}
}
namespace NativeCoverKL {bool SimulationCallbackActive(std::uintptr_t player){return F::sim&&player==F::player;}}
namespace NativeCameraKL {bool CinematicActive(){return false;}}
namespace NativeActionKL {State Current(){return F::action;}}
namespace NativeWeaponKL {bool SnapshotAim(AimFrame& output){if(!F::poseAvailable||!F::action.WeaponTrackingAllowed())return false;output=F::pose;return true;}
bool ScopeQueryActive(std::uintptr_t,std::uintptr_t){return false;}}
MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*){std::abort();}
MH_STATUS WINAPI MH_EnableHook(LPVOID){std::abort();}
MH_STATUS WINAPI MH_QueueEnableHook(LPVOID){std::abort();}
MH_STATUS WINAPI MH_ApplyQueued(){std::abort();}
MH_STATUS WINAPI MH_DisableHook(LPVOID){std::abort();}
MH_STATUS WINAPI MH_RemoveHook(LPVOID){std::abort();}

int main(){using namespace F;namespace C=NativeTeamCommandsKL;std::setvbuf(stdout,nullptr,_IONBF,0);SetUnhandledExceptionFilter(Crash);Setup();
    New(T::Command::Recall);C::SimulationTick(player);Check(nativeRecall==1&&result.code==T::ResultCode::Accepted&&!lastAll,"actual callback recalls the selected receiver without requiring a gun ray");
    C::SimulationTick(player);Check(nativeRecall==1,"same serial cannot issue duplicate AI orders");
    New(T::Command::Move);C::SimulationTick(player);Check(nativeMove==1&&nativePoint.z==1000&&result.code==T::ResultCode::Accepted,"actual Move dispatch uses the native gun contact point");
    New(T::Command::Attack);C::SimulationTick(player);Check(nativeAttack==1&&result.code==T::ResultCode::Accepted&&result.targetActor==actor,"actual Attack dispatch uses certified hostile actor");
    Check(C::targetOverrides==2,"only native Attack target getters were overridden");
    Vec3 point{};using Target=std::uint32_t(__thiscall*)(void*,Vec3*);
    Check(reinterpret_cast<Target>(image+0x213EB8)(reinterpret_cast<void*>(mover),&point)==0xBAD&&originalTargets==1,"same exact native caller preserves original target outside command scope");
    New(T::Command::Recall);Put(control+0x10,std::uint32_t(control+0x1C));C::SimulationTick(player);Check(result.code==T::ResultCode::SelectionChanged&&nativeRecall==1,"receiver selection race cannot redirect command to everyone");
    New(T::Command::Recall);C::SimulationTick(player);Check(nativeRecall==2&&lastAll,"native all-receivers selection is explicit");Put(control+0x10,std::uint32_t(control+0x18));
    New(T::Command::Attack);Put(image+0xA000C,std::uint32_t(0));const auto queries=rayQueries;C::SimulationTick(player);Check(result.code==T::ResultCode::NoTarget&&nativeAttack==1&&rayQueries==queries,"native assist candidate never bypasses hostile-only certificate");Put(image+0xA000C,std::uint32_t(1u<<3));
    New(T::Command::Attack);rayHit=false;C::SimulationTick(player);Check(result.code==T::ResultCode::NoTarget&&nativeAttack==1,"missing enemy does not become blind FireAt or a player shot");rayHit=true;
    New(T::Command::Move);request.tick-=301;C::SimulationTick(player);Check(result.code==T::ResultCode::Stale&&nativeMove==1,"stale request never invokes native AI");
    New(T::Command::Recall);team=false;C::SimulationTick(player);Check(result.code==T::ResultCode::Rejected&&nativeRecall==2,"closed team panel cancels command");team=true;
    New(T::Command::Recall);sim=false;C::SimulationTick(player);Check(result.serial==0&&nativeRecall==2,"render or other thread has no command dispatch capability");sim=true;
    New(T::Command::Recall);++ack;C::SimulationTick(player);Check(result.code==T::ResultCode::Stale&&nativeRecall==2,"recenter invalidates old command and receiver");
    ack=7;Put(player+0x358+0x60,std::uint32_t(0));poseAvailable=false;
    New(T::Command::Move);head.tick=request.tick;C::PublishHeadRay(head);const auto headMove=nativeMove;C::SimulationTick(player);
    Check(result.code==T::ResultCode::Accepted&&nativeMove==headMove+1&&nativePoint.y==200&&nativePoint.z==1000,
          "actual coupled native ROOM query moves unarmed receiver to the fresh centre-head ray contact");
    assistedPoint.y=200;New(T::Command::Attack);head.tick=request.tick;C::PublishHeadRay(head);const auto headAttack=nativeAttack;C::SimulationTick(player);
    Check(result.code==T::ResultCode::Accepted&&nativeAttack==headAttack+1&&result.targetActor==actor,
          "actual coupled unarmed hostile query attacks through the same native selected receiver and getter scope");
    Check(C::Snapshot().headRayRequests==2,"head requests are identified separately from firearm requests");
    Put(player+0x358+0x60,std::uint32_t(pose.weapon));Put(player+0x358+0x64,std::uint8_t(0));
    New(T::Command::Move);head.tick=request.tick;C::PublishHeadRay(head);const auto beforeMissing=rayQueries;C::SimulationTick(player);
    Check(result.code==T::ResultCode::NoTarget&&rayQueries==beforeMissing&&C::Snapshot().heldRayUnavailable==1,
          "inactive retained firearm with missing ray cannot silently fall back to head aim");
    Put(player+0x358+0x60,std::uint32_t(0));Put(player+0x358+0x64,std::uint8_t(1));
    New(T::Command::Move);head.tick=request.tick-51;C::PublishHeadRay(head);C::SimulationTick(player);
    Check(result.code==T::ResultCode::NoTarget,"expired centre-head tracking cannot authorize unarmed collision or movement");
    New(T::Command::Move);head.tick=request.tick;C::PublishHeadRay(head);equipDuringRay=true;const auto beforeEquip=nativeMove;C::SimulationTick(player);equipDuringRay=false;
    Check(result.code==T::ResultCode::NoTarget&&nativeMove==beforeEquip,"equipping a firearm during actual ROOM ray invalidates unarmed command before native AI");Put(player+0x358+0x60,std::uint32_t(0));
    New(T::Command::Move);head.tick=request.tick;C::PublishHeadRay(head);headMovesDuringRay=true;C::SimulationTick(player);headMovesDuringRay=false;
    Check(result.code==T::ResultCode::NoTarget&&nativeMove==beforeEquip,"changed head source during actual collision query cannot dispatch a mismatched point");
    New(T::Command::Move);head.tick=request.tick;head.sourceCamera+=4;C::PublishHeadRay(head);const auto beforeWrongCamera=rayQueries;C::SimulationTick(player);head.sourceCamera-=4;
    Check(result.code==T::ResultCode::NoTarget&&rayQueries==beforeWrongCamera,"other native camera cannot authorize head query even with fresh mounted tracking");
    New(T::Command::Move);head.tick=request.tick;C::PublishHeadRay(head);sim=false;C::SimulationTick(player);sim=true;
    Check(!result.serial&&nativeMove==beforeEquip,"unarmed command retains actual simulation-callback capability");
    New(T::Command::Move);head.tick=request.tick;C::PublishHeadRay(head);rayHit=false;C::SimulationTick(player);rayHit=true;
    Check(result.code==T::ResultCode::NoTarget&&nativeMove==beforeEquip,"unarmed miss never fabricates a move destination");
    for(unsigned mutation=1;mutation<=3;++mutation){New(T::Command::Move);head.tick=request.tick;C::PublishHeadRay(head);queryMutation=mutation;C::SimulationTick(player);queryMutation=0;
        Check(result.code==(mutation==3?T::ResultCode::SelectionChanged:T::ResultCode::NoTarget)&&nativeMove==beforeEquip,
              "modified native ray, unloading ROOM or changed receiver cancels unarmed dispatch after collision");
        Put(image+0xE3000+0x3C,std::uint32_t(0));Put(control+0x10,std::uint32_t(control+0x18));}
    Put(image+0x62000+0x60,std::uint32_t(player));New(T::Command::Move);head.tick=request.tick;C::PublishHeadRay(head);C::SimulationTick(player);
    Check(result.code==T::ResultCode::NoTarget&&nativeMove==beforeEquip,"unarmed head ray rejects player-owned contact ancestry");Put(image+0x62000+0x60,std::uint32_t(actor));
    New(T::Command::Recall);C::InvalidateHeadRay();const auto beforeRecall=nativeRecall;C::SimulationTick(player);
    Check(result.code==T::ResultCode::Accepted&&nativeRecall==beforeRecall+1,"unarmed Recall needs neither firearm pose nor head target");
    Put(player+0x358+0x60,std::uint32_t(pose.weapon));poseAvailable=true;pose.origin={};assistedPoint={577,0,1000};
    std::array<unsigned char,NativeTeamAssistKL::MoverBytes> moverBefore{};std::memcpy(moverBefore.data(),reinterpret_cast<void*>(mover),moverBefore.size());
    New(T::Command::Attack);const auto nearAttacks=nativeAttack;C::SimulationTick(player);
    Check(result.code==T::ResultCode::Accepted&&nativeAttack==nearAttacks+1,"native assist selects a visible enemy 30 degrees off aim, outside the old cone");
    Check(!std::memcmp(moverBefore.data(),reinterpret_cast<void*>(mover),moverBefore.size()),"native assist leaves entire original camera mover bit exact");
    Put(mover+0x9C,std::uint8_t(1));New(T::Command::Attack);C::SimulationTick(player);Put(mover+0x9C,std::uint8_t(0));
    Check(result.code==T::ResultCode::NoTarget&&nativeAttack==nearAttacks+1,"precision rejects a candidate 30 degrees off aim");
    New(T::Command::Attack);targetLod=false;const auto beforeLod=rayQueries;C::SimulationTick(player);targetLod=true;
    Check(result.code==T::ResultCode::NoTarget&&rayQueries==beforeLod,"native picker noneligible or unloaded candidate never authorizes a collision or AI order");
    New(T::Command::Attack);roomReference=0;const auto beforeOccluded=C::Snapshot().assistOccluded;C::SimulationTick(player);roomReference=1;
    Check(result.code==T::ResultCode::NoTarget&&C::Snapshot().assistOccluded==beforeOccluded+1&&nativeAttack==nearAttacks+1,"native selected hostile behind static occluder is rejected by exact LOS to same actor");
    New(T::Command::Attack);assistPoseChanges=true;const auto beforePose=rayQueries;C::SimulationTick(player);assistPoseChanges=false;pose.origin={};
    Check(result.code==T::ResultCode::NoTarget&&rayQueries==beforePose,"armed source changed during native picker is rechecked before LOS and dispatch");
    Vec3 outside{};using Origin=void*(__thiscall*)(void*,void*);const auto beforeOrigin=originalOriginCalls;
    reinterpret_cast<Origin>(image+0x231ECC)(reinterpret_cast<void*>(mover),&outside);
    Check(originalOriginCalls==beforeOrigin+1&&outside.x==-1,"same native GetOrigin caller outside private scoped snapshot preserves original function");
    // Integration regression: a visual action exception must also reach the
    // real ROOM preview/order consumers, without authorizing physical actions.
    NativeAimKL::SetEnabled(true);assistedPoint={0,0,1000};pose.origin={};
    using A=NativeActionKL::Action;using M=NativeActionKL::Movement;
    const auto preview=[&](bool allowed){FClock::tick+=20;pose.tick=GetTickCount();const auto before=rayQueries;
        NativeAimKL::SimulationTick(player);NativeAimKL::Endpoint hit;
        Check((rayQueries==before+1)==allowed,"actual ROOM preview query follows the native SnapshotAim tracked-action contract");
        Check(NativeAimKL::ReadEndpoint(pose,hit)==allowed,"ROOM endpoint freshness survives allowed gesture/reload/passenger and is retired for physical actions");};
    const std::pair<M,A> tracked[]={{M::BaseMove,A::Reload},{M::Cover,A::Reload},{M::Vehicle,A::Reload},
        {M::BaseMove,A::PutBack},{M::BaseMove,A::GetBack},{M::BaseMove,A::Command},{M::Vehicle,A::None}};
    for(const auto& state:tracked){action.movementKind=state.first;action.actionKind=state.second;action.action=state.second==A::None?0:image+0xD1000;
        preview(true);const auto beforeMove=nativeMove;New(T::Command::Move);C::SimulationTick(player);
        Check(result.code==T::ResultCode::Accepted&&nativeMove==beforeMove+1,"selected receiver still accepts Move during tracked reload/weapon gesture/FFCommand/passenger");
        const auto beforeAttack=nativeAttack;New(T::Command::Attack);C::SimulationTick(player);
        Check(result.code==T::ResultCode::Accepted&&nativeAttack==beforeAttack+1,"native hostile assist and Attack remain available across tracked action transition");}
    const std::pair<M,A> physical[]={{M::Mounted,A::None},{M::Rappel,A::None},{M::Climb,A::None},
        {M::Vehicle,A::Vehicle},{M::BaseMove,A::Throw},{M::BaseMove,A::MiniAct},{M::BaseMove,A::MiniAnim},{M::BaseMove,A::Other}};
    for(const auto& state:physical){action.movementKind=state.first;action.actionKind=state.second;action.action=state.second==A::None?0:image+0xD1000;
        preview(false);const auto before=rayQueries,beforeMove=nativeMove;New(T::Command::Move);C::SimulationTick(player);
        Check(result.code==T::ResultCode::Rejected&&rayQueries==before&&nativeMove==beforeMove,"mounted/throw/cinema/unknown physical native rig cannot borrow tracked order permission");}
    action={};action.valid=true;action.player=player;action.movementKind=M::BaseMove;
    wheel=true;Put(image+0x8883A4,std::uint8_t(3));
    const auto beforeWheelMove=nativeMove,beforeWheelAttack=nativeAttack;
    New(T::Command::Move);C::SimulationTick(player);
    Check(result.code==T::ResultCode::Accepted&&nativeMove==beforeWheelMove+1&&nativePoint.z==1000,
          "Move in the held inventory mode dispatches the native gun contact point");
    New(T::Command::Attack);C::SimulationTick(player);
    Check(result.code==T::ResultCode::Accepted&&nativeAttack==beforeWheelAttack+1&&result.targetActor==actor,
          "Attack in the held inventory mode dispatches the certified hostile actor");
    New(T::Command::Move);wheel=false;C::SimulationTick(player);
    Check(result.code==T::ResultCode::Rejected&&nativeMove==beforeWheelMove+1,
          "mode 3 without the held inventory never dispatches a command");
    VirtualFree(reinterpret_cast<void*>(image),0,MEM_RELEASE);std::printf("Production native team commands: %u checks PASS; private CPU callbacks only.\n",checks);return 0;}
