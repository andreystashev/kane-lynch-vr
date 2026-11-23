// Real production hooks in a private CPU-only fixture. The sole substitutes
// are engine/runtime callbacks and owned objects; no game, HMD, GPU or input.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
namespace Fixture {inline DWORD tick=1000;inline DWORD Clock(){return tick;}}
#define GetTickCount Fixture::Clock
#include "native_mounted_aim_kl.cpp"
#include "native_hud_test_fixture.hpp"
#include <cstdio>
#include <cstdlib>

namespace Fixture {
using namespace NativeCameraMath;
unsigned checks=0,nativeCalls=0;bool ready=true,cinema=false,team=false,inventory=false,observer=false,tracking=true,manualStick=false;
bool mailbox=true;float stickX=0,stickY=0;HandsIPC::Producer physicalProducer;VehicleControlIPC::Certificate publishedReceiver;
bool contextValid=true;DWORD contextTick=1000;
std::uint32_t ack=7;std::uintptr_t player=0,movement=0,dispatcher=0,record=0,controller=0,entity=0,geometry=0,gun=0,input=0,otherPlayer=0;
NativeHandsKL::ControllerAim aim;Vec3 captured{},nativePoint{2,3,4};
const Vec3* nativePointPointer=nullptr;
void PublishPhysical(Quat orientation={}){HandsIPC::Frame frame;frame.present=frame.mounted=1;frame.tick=tick;frame.recenterAck=ack;
    frame.head.orientation[3]=1;frame.head.flags=3;frame.hands[1].flags=3;
    frame.hands[1].orientation[0]=orientation.x;frame.hands[1].orientation[1]=orientation.y;frame.hands[1].orientation[2]=orientation.z;frame.hands[1].orientation[3]=orientation.w;physicalProducer.Publish(frame);}
enum class Mode {Normal,NoObserver,OutsideCover,WrongObserver,Double,ForeignCaller,WrongObject,Delayed,Recenter,Pair,Driver,Action,NativeBlocked,Nested,HostContext,ForeignEpoch,CarrierRetired};
Mode mode=Mode::Normal;unsigned nesting=0;
using CallPoint=void(__thiscall*)(void*,const Vec3*);CallPoint callPoint=nullptr;
void Check(bool value,const char* label){++checks;if(!value){std::printf("FAIL: %s\n",label);std::exit(1);}}
template<class T>void Put(std::uintptr_t at,const T& value){std::memcpy(reinterpret_cast<void*>(at),&value,sizeof(value));}
std::uintptr_t Allocate(std::size_t size,DWORD access=PAGE_READWRITE){const auto at=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,size,MEM_RESERVE|MEM_COMMIT,access));Check(at!=0,"allocate private fixture objects");return at;}
bool Equal(Vec3 a,Vec3 b){return !std::memcmp(&a,&b,sizeof(a));}
void __thiscall NativePoint(void* object,const Vec3* point){++nativeCalls;nativePointPointer=point;if(point){captured=*point;Put(reinterpret_cast<std::uintptr_t>(object)+0x2C,*point);}}
void __thiscall NativeUpdate(void* object){using namespace NativeMountedAimKL;
    const auto at=reinterpret_cast<std::uintptr_t>(object);
    if(nesting){SimulationTick(at);return;}
    if(mode!=Mode::NoObserver){observer=mode!=Mode::OutsideCover;SimulationTick(mode==Mode::WrongObserver?otherPlayer:at);observer=false;}
    switch(mode){
    case Mode::Delayed:tick+=51;break;case Mode::Recenter:++ack;break;case Mode::Pair:++aim.pairId;break;
    case Mode::Driver:Put(controller+0x48,std::uint32_t(otherPlayer));break;case Mode::Action:Put(dispatcher+4,std::uint32_t(otherPlayer));break;
    case Mode::NativeBlocked:Put(input+0x18,std::uint8_t(1));break;
    case Mode::HostContext:contextValid=false;break;
    case Mode::ForeignEpoch:++publishedReceiver.epoch;break;
    case Mode::CarrierRetired:Put(geometry+0x60,std::uint32_t(entity+4));break;
    case Mode::ForeignCaller:PointHook(reinterpret_cast<void*>(controller),nullptr,&nativePoint);break;
    case Mode::WrongObject:callPoint(reinterpret_cast<void*>(controller+4),&nativePoint);break;
    case Mode::Nested:++nesting;UpdateHook(reinterpret_cast<void*>(otherPlayer),nullptr);--nesting;break;
    default:break;}
    callPoint(reinterpret_cast<void*>(controller),&nativePoint);
    if(mode==Mode::Double)callPoint(reinterpret_cast<void*>(controller),&nativePoint);
}
void Reset(){using namespace NativeMountedAimKL;tick=1000;ack=7;mode=Mode::Normal;ready=tracking=true;cinema=team=inventory=observer=manualStick=false;nativeCalls=0;captured={};
    SetMouseAim(false);reticleSample={};
    mailbox=true;stickX=stickY=0;relative.Reset();controlIdentity={};receiver={};publishedReceiver={};
    contextValid=true;contextTick=1000;
    Put(image+0x8883A4,std::uint16_t(0));Put(input+0x18,std::uint8_t(0));Put(dispatcher+4,std::uint32_t(0));Put(controller+0x48,std::uint32_t(player));Put(controller+0x44,std::uint8_t(1));Put(controller+0x24,std::uint32_t(0));Put(movement,std::uint32_t(image+0x566F54));
    Put(geometry+0x60,std::uint32_t(entity));
    aim={};aim.player=player;aim.pairId=11;aim.tick=1000;aim.mappingTick=950;aim.recenterAck=7;aim.unitsPerMetre=100;aim.origin={10,20,30};aim.direction={0,0,1};active={};PublishPhysical();}
void Run(bool expect,const char* label){using namespace NativeMountedAimKL;const auto before=Snapshot().applied;UpdateHook(reinterpret_cast<void*>(player),nullptr);
    Check(Snapshot().applied==before+(expect?1:0),label);Check(nativeCalls>=1,"original native MG setter executes on every target attempt");
    Check(Equal(captured,expect?Vec3{10,20,10030}:nativePoint),"native setter receives only certified target or exact original point");
    Check(!active.player&&!active.capability.available,"full native update exit retires TLS player and one-shot capability");}
}
namespace VRBridge {bool MountedStickActive(){return Fixture::manualStick;}bool Ready(){return Fixture::ready;}bool TeamHudVisible(){return Fixture::team;}bool WeaponMenuVisible(){return Fixture::inventory;}long RecenterAck(){return Fixture::ack;}
void SetMountedReceiver(const VehicleControlIPC::Certificate& value){Fixture::publishedReceiver=value;}
MountedInput ReadMountedInput(const VehicleControlIPC::Certificate& value){return {Fixture::mailbox&&value.epoch&&VehicleControlIPC::Same(value,Fixture::publishedReceiver)&&value.recenter==Fixture::ack,Fixture::tick,Fixture::stickX,Fixture::stickY};}
SecondaryFireInput ReadSecondaryFire(){return {Fixture::contextValid&&Fixture::contextTick&&std::int32_t(Fixture::tick-Fixture::contextTick)>=0&&Fixture::tick-Fixture::contextTick<=100,false,Fixture::contextTick};}}
namespace NativeCameraKL {bool CinematicActive(){return Fixture::cinema;}}
namespace NativeCoverKL {bool SimulationCallbackActive(std::uintptr_t at){return Fixture::observer&&at==Fixture::player;}}
namespace NativeHandsKL {bool SnapshotControllerAim(unsigned side,ControllerAim& out){if(side!=1||!Fixture::tracking)return false;out=Fixture::aim;return true;}}

int main(int argc,char** argv){using namespace Fixture;using namespace NativeMountedAimKL;
    image=Allocate(0x900000,PAGE_EXECUTE_READWRITE);player=Allocate(0x2000);otherPlayer=Allocate(0x2000);movement=Allocate(0x100);dispatcher=Allocate(0x20);
    record=Allocate(0x88);controller=Allocate(0x200);entity=Allocate(0x900);geometry=Allocate(0x100);gun=Allocate(0x100);input=Allocate(0x100);const auto data=Allocate(0x1000),gunGeometry=Allocate(0x100);
    Check(physicalProducer.Open(GetCurrentProcessId()),"private physical HandsIPC producer opens without any headset/runtime");
    Put(image+0x68760C,std::uint32_t(data));Put(data+0xA2C,std::uint32_t(player));Put(player,std::uint32_t(image+0x56582C));
    Put(player+0x67C,std::uint32_t(movement));Put(movement+0x38,std::uint32_t(player));Put(movement+0x44,std::uint32_t(13));Put(player+0x5F8+13*4,std::uint32_t(movement));
    Put(player+0x194,std::uint32_t(dispatcher));Put(dispatcher,std::uint32_t(image+0x54DE4C));Put(dispatcher+8,std::uint32_t(player));
    Put(player+0xB04,std::uint32_t(record));Put(player+0xB10,std::uint32_t(controller));Put(record+0x3C,std::uint32_t(player));Put(record+0x40,std::uint32_t(controller));
    Put(controller+0xF0,std::uint32_t(record));Put(controller+0x4C,std::uint32_t(entity));Put(controller+0x20,std::uint32_t(gun));
    Put(entity,std::uint32_t(image+0x57F7EC));Put(entity+4,std::uint32_t(geometry));Put(geometry+0x60,std::uint32_t(entity));Put(entity+0x7AC,std::uint32_t(record));Put(entity+0x7B0,std::uint32_t(record+0x44));
    NativePlayerPose::Geometry carrierGeometry;carrierGeometry.pose.basis={{0,0,1},{0,1,0},{1,0,0}};
    Put(geometry,carrierGeometry);Put(geometry+0x60,std::uint32_t(entity));carrierGeometry.pose.position={10,20,30};
    carrierGeometry.parent=geometry;Put(gunGeometry,carrierGeometry);Put(gunGeometry+0x60,std::uint32_t(gun));Put(gun+4,std::uint32_t(gunGeometry));Put(controller+0x14,Vec3{0,0,1});
    Put(player+0x9B8,std::uint32_t(input));Put(controller+0xB0,Vec3{10,20,30});
    // The CPU thunk produces the EXACT supported caller return PC, so the
    // actual PointHook's builtin_return_address guard runs, not a test bypass.
    const std::uintptr_t at=image+PointCaller-10;const unsigned char code[]={0x8B,0x44,0x24,0x04,0x50,0xE8,0,0,0,0,0xC2,0x04,0};
    std::memcpy(reinterpret_cast<void*>(at),code,sizeof(code));Put(at+6,std::int32_t(reinterpret_cast<std::uintptr_t>(PointHook)-(image+PointCaller)));
    FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(at),sizeof(code));callPoint=reinterpret_cast<CallPoint>(at);
    update=NativeUpdate;point=NativePoint;installed=true;Reset();Run(true,"real PointHook accepts same native update + post-detector physical right-controller aim");
    Put(entity,std::uint32_t(image+0x582D14));Put(entity+0x838,std::uint32_t(record));Put(entity+0x83C,std::uint32_t(record+0x44));
    Put(entity+0x7AC,std::uint32_t(0));Put(entity+0x7B0,std::uint32_t(0));
    Reset();Run(true,"actual production point hook accepts vehicle MG through pinned HavokCar layout instead of fixed-gun vector");
    Check(Snapshot().identityProbe.entity==entity&&Snapshot().identityProbe.vtable==image+0x582D14&&Snapshot().identityProbe.recordsOffset==0x838,
        "production diagnostics retain actual native MG entity, class and record layout");
    Put(entity,std::uint32_t(image+0x583074));Reset();Run(false,"native helicopter class cannot borrow car/fixed MG records even with fake matching reciprocal fields");
    Check(Snapshot().identityStage==8&&Snapshot().identityProbe.vtable==image+0x583074,"production failure reports actual unsupported class rather than a silent stage number");
    Put(entity,std::uint32_t(image+0x57F7EC));Put(entity+0x7AC,std::uint32_t(record));Put(entity+0x7B0,std::uint32_t(record+0x44));
    Reset();UpdateHook(reinterpret_cast<void*>(player),nullptr);tick=1020;aim.tick=aim.mappingTick=tick;PublishPhysical({0,-std::sin(.2f),0,std::cos(.2f)});
    const auto before=Snapshot().applied;UpdateHook(reinterpret_cast<void*>(player),nullptr);Check(Snapshot().applied==before+1&&std::fabs(captured.x-10)<.01f&&std::fabs(captured.z-10030)<.01f,"actual setter hook ignores RAW physical hand yaw");
    for(const auto m:{Mode::NoObserver,Mode::OutsideCover,Mode::WrongObserver,Mode::WrongObject,Mode::Delayed,Mode::Recenter,Mode::Pair,Mode::Driver,Mode::Action,Mode::NativeBlocked,Mode::HostContext,Mode::ForeignEpoch,Mode::CarrierRetired}){
        Reset();mode=m;Run(false,"missing/foreign/stale/changed same-update proof preserves native MG point");}
    Reset();mode=Mode::ForeignCaller;Run(true,"foreign script caller passes through and cannot consume local certified setter capability");Check(nativeCalls==2,"both foreign and native setter calls retained");
    Reset();mode=Mode::Double;const auto count=Snapshot().applied;UpdateHook(reinterpret_cast<void*>(player),nullptr);Check(Snapshot().applied==count+1&&nativeCalls==2&&Equal(captured,nativePoint),"one seed authorizes exactly one point substitute; second call remains native");
    Reset();mode=Mode::Nested;Run(true,"nested foreign actor update cannot steal or replace local capability");
    Reset();manualStick=true;stickX=1;Run(true,"stick entry uses the mount-local target and never exposes unrelated native camera target");
    Reset();mailbox=false;Run(false,"missing current-mount host certificate retains exact native fallback instead of discarding all control");
    Reset();PublishPhysical({0,0,0,0});Run(false,"invalid RAW controller quaternion cannot drive native MG even with a valid mapped aim stub");
    Reset();physicalProducer.Close();Run(false,"closed raw HandsIPC channel cannot borrow the mapped camera proof");Check(physicalProducer.Open(GetCurrentProcessId()),"private hand channel can reopen after source retirement");
    // Real producer -> raw HandsIPC consumer -> native production point hook,
    // while both the proved car WORLD and the gun's parent ancestry change.
    Put(entity,std::uint32_t(image+0x582D14));Put(entity+0x838,std::uint32_t(record));Put(entity+0x83C,std::uint32_t(record+0x44));
    Reset();Run(true,"vehicle entry retains current native direction independent of controller's absolute world pointing");
    auto turnedCarrier=carrierGeometry;turnedCarrier.parent=0;turnedCarrier.pose.position={500,40,700};
    const Quat heading{0,std::sin(.6f),0,std::cos(.6f)};const Basis originalBasis{{0,0,1},{0,1,0},{1,0,0}};
    turnedCarrier.pose.basis={Rotate(heading,originalBasis.forward),Rotate(heading,originalBasis.up),Rotate(heading,originalBasis.right)};
    Put(geometry,turnedCarrier);Put(geometry+0x60,std::uint32_t(entity));
    tick=1020;aim.tick=aim.mappingTick=tick;PublishPhysical();UpdateHook(reinterpret_cast<void*>(player),nullptr);
    const auto turnedOrigin=Add(turnedCarrier.pose.position,NativePlayerPose::Transform(turnedCarrier.pose.basis,Vec3{10,20,30}));
    const auto expectedTurn=Add(turnedOrigin,Scale(turnedCarrier.pose.basis.forward,10000));
    Check(Dot(Subtract(captured,expectedTurn),Subtract(captured,expectedTurn))<.01f,"actual mounted production target follows current car turn and WORLD ancestry with held raw hand and unchanged world-mapped aim");
    const auto beforeLocalStick=captured;tick=1040;aim.tick=aim.mappingTick=tick;stickX=1;manualStick=true;PublishPhysical();UpdateHook(reinterpret_cast<void*>(player),nullptr);
    Check(std::sqrt(Dot(Subtract(captured,beforeLocalStick),Subtract(captured,beforeLocalStick)))<190&&
        !Equal(captured,beforeLocalStick),"actual stick entry makes only a small local angular increment, never the unrelated third-person native target");
    const auto heldAfterStick=captured;tick=1060;aim.tick=aim.mappingTick=tick;stickX=0;manualStick=false;PublishPhysical();UpdateHook(reinterpret_cast<void*>(player),nullptr);
    Check(Equal(captured,heldAfterStick),"actual stick release keeps new mount-local heading without jumping back to physical absolute ray");
    Put(controller+0x14,Vec3{1,0,0});tick=1500;contextTick=tick;aim.tick=aim.mappingTick=tick;PublishPhysical();UpdateHook(reinterpret_cast<void*>(player),nullptr);
    Check(Equal(captured,heldAfterStick),"actual setter preserves local aim after a long native stall instead of reseeding a different native direction");
    Put(controller+0x14,Vec3{0,0,1});
    turnedCarrier.pose={originalBasis,{0,0,0}};Put(geometry,turnedCarrier);Put(geometry+0x60,std::uint32_t(entity));Put(entity,std::uint32_t(image+0x57F7EC));
    for(unsigned i=0;i<17;++i){Reset();switch(i){case 0:ready=false;break;case 1:cinema=true;break;case 2:team=true;break;case 3:inventory=true;break;case 4:tracking=false;break;
        case 5:aim.tick=949;break;case 6:aim.mappingTick=849;break;case 7:aim.recenterAck=6;break;case 8:aim.direction={};break;
        case 9:Put(controller+0x44,std::uint8_t(0));break;case 10:Put(controller+0x24,std::uint32_t(otherPlayer));break;
        case 11:Put(movement,std::uint32_t(image+0x566D14));break;case 12:Put(image+0x8883A4,std::uint16_t(1));break;case 13:Put(input+0x18,std::uint8_t(1));break;
        case 14:contextValid=false;break;case 15:contextTick=899;break;default:contextTick=1001;break;}
        Run(false,"VR/UI/cinema/input/ownership rejection always retains native steering fallback");}
    Reset();observer=true;SimulationTick(player);observer=false;callPoint(reinterpret_cast<void*>(controller),&nativePoint);
    Check(Equal(captured,nativePoint)&&!active.capability.available,"post-detector callback outside exact whole-update TLS cannot seed target");
    Reset();UpdateHook(reinterpret_cast<void*>(player),nullptr);captured={};callPoint(reinterpret_cast<void*>(controller),&nativePoint);
    Check(Equal(captured,nativePoint),"completed native update cannot leak target to a later native call");
    // Reticle follows the actual native setter, including explicit mouse mode.
    // No hand ray or previously substituted target is used by the reader.
    Reset();ReticleAim reticle;
    Check(!SnapshotReticle(player,100,reticle),"MG reticle requires a completed verified native target setter");
    UpdateHook(reinterpret_cast<void*>(player),nullptr);
    Check(SnapshotReticle(player,100,reticle)&&reticle.player==player&&reticle.controller==controller&&reticle.gunObject==gun&&
        Equal(reticle.target,captured)&&Equal(reticle.origin,{10,20,30}),"MG dot source is the real native manual target and muzzle in normal controller mode");
    Put(controller+0x2C,Vec3{99,88,77});
    Check(!SnapshotReticle(player,100,reticle),"foreign target replacement cannot borrow an older reticle observation");
    UpdateHook(reinterpret_cast<void*>(player),nullptr);tick+=151;
    Check(!SnapshotReticle(player,100,reticle),"old native target certificate cannot keep a reticle indefinitely");
    Reset();UpdateHook(reinterpret_cast<void*>(player),nullptr);++ack;
    Check(!SnapshotReticle(player,100,reticle),"recenter retires the old MG display certificate");
    Reset();UpdateHook(reinterpret_cast<void*>(player),nullptr);Put(controller+0x48,std::uint32_t(otherPlayer));
    Check(!SnapshotReticle(player,100,reticle),"another native driver cannot inherit the local MG dot");
    Reset();UpdateHook(reinterpret_cast<void*>(player),nullptr);const auto firstEpoch=receiver.epoch;
    SetMouseAim(true);const auto beforeMouse=Snapshot().applied;
    UpdateHook(reinterpret_cast<void*>(player),nullptr);
    Check(Snapshot().mouseAim&&Snapshot().applied==beforeMouse&&Equal(captured,nativePoint)&&nativePointPointer==&nativePoint,
        "explicit mouse mode preserves the exact native input pointer even while VR controller tracking is available");
    Check(!relative.valid&&!publishedReceiver.epoch&&!active.capability.available,
        "mouse mode retires the VR rate receiver and cannot overwrite native aiming later in the update");
    Check(SnapshotReticle(player,100,reticle)&&Equal(reticle.target,nativePoint),
        "the MG dot follows native mouse aiming instead of the previous VR-controller target");
    tracking=false;tick=1020;nativePoint={9,7,6};UpdateHook(reinterpret_cast<void*>(player),nullptr);
    Check(Snapshot().applied==beforeMouse&&Equal(captured,nativePoint)&&nativePointPointer==&nativePoint,
        "native mouse aiming remains available after controller tracking disappears");
    Check(SnapshotReticle(player,100,reticle)&&Equal(reticle.target,nativePoint),
        "a new actual mouse target updates the MG dot without a controller pose or Fire request");nativePoint={2,3,4};
    SetMouseAim(false);tracking=true;Put(controller+0x14,Vec3{1,0,0});
    Check(!SnapshotReticle(player,100,reticle),"mode change retires the old display certificate until the next actual native setter");
    UpdateHook(reinterpret_cast<void*>(player),nullptr);
    Check(!Snapshot().mouseAim&&receiver.epoch>firstEpoch&&Snapshot().applied==beforeMouse+1&&
        std::fabs(captured.x-10010)<.01f&&std::fabs(captured.z-30)<.01f,
        "return to VR starts a new receiver epoch at the current native mount heading, not its old pre-mouse heading");
    Check(SnapshotReticle(player,100,reticle)&&Equal(reticle.target,captured),"reticle follows the newly restored controller target");
    Put(controller+0x14,Vec3{0,0,1});
    // Exercise real MinHook relocation over the supported prologues, including
    // playerUpdate's ESP alignment. Only the private bodies call CPU fakes.
    unsigned char updateBytes[]={0x55,0x8B,0xEC,0x83,0xE4,0xF0,0x6A,0xFF};
    unsigned char pointBytes[]={0x55,0x8B,0xEC,0x83,0xEC,0x0C};
    if(argc>1){NativeHudFixture::Image actual;Check(actual.Load(argv[1]),"read pinned EXE without execution for actual hook prologues");
        Check(Verify(NativeHudFixture::Base,actual.Reader()),"actual pinned player update and point setter signatures agree with production");
        Check(actual.Read(NativeHudFixture::Base+PlayerUpdate,updateBytes,sizeof(updateBytes))&&actual.Read(NativeHudFixture::Base+PointSetter,pointBytes,sizeof(pointBytes)),"copy actual entry bytes to private executable fixture only");}
    const auto body=[&](std::uintptr_t location,const void* bytes,std::size_t size,std::uintptr_t callback){
        std::memcpy(reinterpret_cast<void*>(location),bytes,size);const unsigned char restore[]={0x8B,0xE5,0x5D,0xE9,0,0,0,0};
        std::memcpy(reinterpret_cast<void*>(location+size),restore,sizeof(restore));Put(location+size+4,std::int32_t(callback-(location+size+8)));};
    body(image+PlayerUpdate,updateBytes,sizeof(updateBytes),reinterpret_cast<std::uintptr_t>(NativeUpdate));
    body(image+PointSetter,pointBytes,sizeof(pointBytes),reinterpret_cast<std::uintptr_t>(NativePoint));
    Put(at+6,std::int32_t((image+PointSetter)-(image+PointCaller)));FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(image),0x900000);
    Check(MH_Initialize()==MH_OK,"initialize private MinHook relocation fixture");
    Check(MH_CreateHook(reinterpret_cast<void*>(image+PlayerUpdate),reinterpret_cast<void*>(UpdateHook),reinterpret_cast<void**>(&update))==MH_OK,"actual aligned native playerUpdate prologue supports a trampoline");
    Check(MH_CreateHook(reinterpret_cast<void*>(image+PointSetter),reinterpret_cast<void*>(PointHook),reinterpret_cast<void**>(&point))==MH_OK,"actual native MG setter prologue supports a trampoline");
    Check(MH_QueueEnableHook(reinterpret_cast<void*>(image+PlayerUpdate))==MH_OK&&MH_QueueEnableHook(reinterpret_cast<void*>(image+PointSetter))==MH_OK&&MH_ApplyQueued()==MH_OK,"enable both production detours on private copies only");
    Reset();const auto appliedBefore=Snapshot().applied,updatesBefore=Snapshot().updates,setterBefore=Snapshot().setterCalls;
    reinterpret_cast<Update>(image+PlayerUpdate)(reinterpret_cast<void*>(player));
    Check(Snapshot().updates==updatesBefore+1&&Snapshot().setterCalls==setterBefore+1&&Snapshot().applied==appliedBefore+1,"actual paired MinHook detours and exact caller-PC reach production same-update steering path");
    Check(nativeCalls==1&&Equal(captured,{10,20,10030})&&!active.player,"native prologue relocation preserves arguments, stack return and original setter execution");
    Reset();team=true;reinterpret_cast<Update>(image+PlayerUpdate)(reinterpret_cast<void*>(player));
    Check(nativeCalls==1&&Equal(captured,nativePoint),"actual paired trampoline path fails open to exact native point in VR menu");
    Check(MH_DisableHook(reinterpret_cast<void*>(image+PlayerUpdate))==MH_OK&&MH_DisableHook(reinterpret_cast<void*>(image+PointSetter))==MH_OK,"disable private detours after regression fixture");
    Check(MH_RemoveHook(reinterpret_cast<void*>(image+PlayerUpdate))==MH_OK&&MH_RemoveHook(reinterpret_cast<void*>(image+PointSetter))==MH_OK&&MH_Uninitialize()==MH_OK,"remove private trampolines without touching game code");
    std::printf("Mounted native production hooks: %u checks PASS.\n",checks);return 0;
}
