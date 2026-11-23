#include "native_mounted_aim_kl.hpp"
#include "native_hud_test_fixture.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
namespace {unsigned checks=0;void Check(bool value,const char* label){++checks;if(!value){std::printf("FAIL: %s\n",label);std::exit(1);}}
using namespace NativeHudFixture;
constexpr std::uint32_t Movement=0x15000000,Dispatcher=0x15100000,Controller=0x15200000,Entity=0x15300000,Geometry=0x15400000,Gun=0x15500000;
Memory Mounted(){Memory m;m.Set(Base+0x68760C,Data);m.Set(Data+0xA2C,Player);m.Set(Player,Base+0x56582C);
    m.Set(Player+0x67C,Movement);m.Set(Movement,Base+0x566F54);m.Set(Movement+0x38,Player);m.Set(Movement+0x44,std::uint32_t(13));m.Set(Player+0x5F8+13*4,Movement);
    m.Set(Player+0x194,Dispatcher);m.Set(Dispatcher,Base+0x54DE4C);m.Set(Dispatcher+8,Player);m.Set(Dispatcher+4,std::uint32_t(0));
    m.Set(Player+0xB04,Records);m.Set(Player+0xB10,Controller);m.Set(Records+0x3C,Player);m.Set(Records+0x40,Controller);
    m.Set(Controller+0xF0,Records);m.Set(Controller+0x4C,Entity);m.Set(Entity,Base+0x57F7EC);m.Set(Entity+4,Geometry);m.Set(Geometry+0x60,Entity);m.Set(Geometry+0x3C,std::uint32_t(0));
    m.Set(Entity+0x7AC,Records);m.Set(Entity+0x7B0,std::uint32_t(Records+0x44));m.Set(Controller+0x20,Gun);m.Set(Controller+0x44,std::uint8_t(1));
    m.Set(Controller+0x48,Player);m.Set(Controller+0x24,std::uint32_t(0));return m;}
}
int main(int argc,char** argv){using namespace NativeMountedAimKL;
    if(argc>1){Image exe;Check(exe.Load(argv[1]),"load supported PE without running it");Check(Verify(Base,exe.Reader()),"actual MG point/update/driver/ownership dataflow bytes and player update slot");
        const std::uintptr_t offsets[]={PlayerUpdate,PointSetter,0x1FD143,0x1FD56F,0x2B5328,0x2DF0F8,0x2CFCA6,
            0x2B728C,0x2B728E,0x2D3556,0x2D3558,0x57F7EC+0x30,0x582D14+0x30,0x2007AB,0x33B4D6,0x33AE62,0x33A937,0x33BD63,0x56582C+0x160};
        for(const auto offset:offsets){exe.mapped[offset]^=1;Check(!Verify(Base,exe.Reader()),"different supported ownership or caller bytes cannot install MG hook");exe.mapped[offset]^=1;}}
    auto m=Mounted();const auto valid=SnapshotIdentity(Base,Player,m.Reader());
    Check(valid.valid&&Eligible(valid)&&valid.controller==Controller&&valid.entity==Entity&&valid.geometry==Geometry&&valid.gunObject==Gun,"exact native mounted13 ownership graph exposes static base separately from moving gun");
    Check(Same(valid,SnapshotIdentity(Base,Player,m.Reader())),"unchanged native reciprocal graph repeats exactly");
    auto car=m;car.Set(Entity,Base+0x582D14);car.Set(Entity+0x838,Records);car.Set(Entity+0x83C,std::uint32_t(Records+0x44));
    car.Set(Entity+0x7AC,std::uint32_t(0));car.Set(Entity+0x7B0,std::uint32_t(0));
    IdentityProbe probe;unsigned stage=0;const auto vehicle=SnapshotIdentity(Base,Player,car.Reader(),&stage,&probe);
    Check(vehicle.valid&&Eligible(vehicle)&&vehicle.recordsOffset==0x838&&stage==100&&probe.vtable==Base+0x582D14&&
        probe.entity==Entity&&probe.controller==Controller&&probe.record==Records,"actual ZVehicleWeapon ownership uses exact HavokCar type and its own +838 vector");
    Check(!Same(valid,vehicle),"car and fixed MG proofs cannot be reused across type/layout replacement");
    for(unsigned i=0;i<6;++i){auto broken=car;switch(i){case 0:broken.Set(Entity,Base+0x583074);break;
        case 1:broken.Set(Entity+0x838,std::uint32_t(Records+0x44));break;case 2:broken.Set(Entity+0x83C,std::uint32_t(Records+0x45));break;
        case 3:broken.Set(Controller+0xF0,std::uint32_t(0));break;case 4:broken.Set(Records+0x40,std::uint32_t(Controller+4));break;
        default:broken.Set(Geometry+0x60,std::uint32_t(Entity+4));break;}
        Check(!SnapshotIdentity(Base,Player,broken.Reader()).valid,"heli, wrong car span and broken reciprocal owners never authorize vehicle MG aim");}
    auto noEntity=m;noEntity.Set(Controller+0x4C,std::uint32_t(0));
    Check(!SnapshotIdentity(Base,Player,noEntity.Reader(),&stage,&probe).valid&&stage==8&&!probe.entity&&!probe.vtable,
        "stage8 exposes unreadable entity separately through raw probe rather than guessing a derived class");
    auto wrongEntity=m;wrongEntity.Set(Entity,Base+0x583074);
    Check(!SnapshotIdentity(Base,Player,wrongEntity.Reader(),&stage,&probe).valid&&stage==8&&probe.entity==Entity&&probe.vtable==Base+0x583074&&!probe.recordsOffset,
        "stage8 raw probe identifies readable unsupported class without accepting helicopter steering");
    Check(!SnapshotIdentity(Base,Player+4,m.Reader()).valid,"foreign actor cannot borrow local mounted seat");
    auto hidden=m;hidden.Set(Geometry+0x3C,std::uint32_t(0x400));
    Check(SnapshotIdentity(Base,Player,hidden.Reader()).valid,"a hidden mount base still owns its record and steering controller");
    for(unsigned i=0;i<23;++i){if(i==19)continue;auto bad=m;switch(i){
        case 0:bad.Set(Player+0x67C,Movement+4);break;case 1:bad.Set(Movement,Base+0x566D14);break;
        case 2:bad.Set(Movement+0x44,std::uint32_t(8));break;case 3:bad.Set(Movement+0x38,Player+4);break;
        case 4:bad.Set(Player+0x5F8+13*4,Movement+4);break;case 5:bad.Set(Dispatcher+8,Player+4);break;
        case 6:bad.Set(Records+0x3C,Player+4);break;case 7:bad.Set(Records+0x40,Controller+4);break;
        case 8:bad.Set(Controller+0xF0,Records+4);break;case 9:bad.Set(Controller+0x4C,Entity+4);break;
        case 10:bad.Set(Entity,Base+0x57F644);break;case 11:bad.Set(Geometry+0x60,Entity+4);break;
        case 12:bad.Set(Entity+0x7AC,std::uint32_t(Records+0x44));break;case 13:bad.Set(Entity+0x7B0,Records);break;
        case 14:bad.Set(Entity+0x7B0,std::uint32_t(Records+0x45));break;case 15:bad.Set(Entity+0x7B0,std::uint32_t(Records+65*0x44));break;
        case 16:bad.Set(Player+0xB04,std::uint32_t(Records+1));break;case 17:bad.Set(Controller+0x20,std::uint32_t(0));break;
        case 18:bad.Set(Controller+0x44,std::uint8_t(2));break;case 19:bad.Set(Geometry+0x3C,std::uint32_t(0x400));break;
        case 20:bad.Set(Entity+4,std::uint32_t(0));break;case 21:bad.Set(Player+0xB10,std::uint32_t(0));break;
        default:bad.bytes.erase(Controller+0x48);break;}
        Check(!SnapshotIdentity(Base,Player,bad.Reader()).valid,"broken live mounted ownership cannot authorize a steering or camera base");}
    for(unsigned i=0;i<4;++i){auto bad=m;switch(i){case 0:bad.Set(Dispatcher+4,std::uint32_t(0x16000000));break;case 1:bad.Set(Controller+0x44,std::uint8_t(0));break;
        case 2:bad.Set(Controller+0x48,Player+4);break;default:bad.Set(Controller+0x24,std::uint32_t(0x16000000));break;}
        const auto s=SnapshotIdentity(Base,Player,bad.Reader());Check(s.valid&&!Eligible(s),"action, automatic, foreign driver or AI target preserves native point even with valid camera carrier");}
    unsigned reads=0;const auto changing=[&](auto at,void* out,auto size){if(at==Controller+0xF0&&++reads==2)return false;return m.Read(at,out,size);};
    Check(!SnapshotIdentity(Base,Player,changing).valid,"ownership retirement during snapshot cannot authorize a target");
    reads=0;const auto gunChanges=[&](auto at,void* out,auto size){if(at==Controller+0x20&&++reads==2){std::uint32_t replacement=Gun+4;std::memcpy(out,&replacement,size);return true;}return m.Read(at,out,size);};
    Check(!SnapshotIdentity(Base,Player,gunChanges).valid,"moving native gun object replacement during graph read retires target proof");
    NativeHandsKL::ControllerAim aim;aim.player=Player;aim.pairId=11;aim.tick=1000;aim.mappingTick=900;aim.recenterAck=7;aim.unitsPerMetre=100;aim.origin={10,20,30};aim.direction={0,0,1};
    Check(Fresh(aim,Player,1000,7)&&Fresh(aim,Player,1050,7),"fresh physical controller and150ms mapping accepted at boundaries");
    Check(!Fresh(aim,Player,1051,7)&&!Fresh(aim,Player,999,7)&&!Fresh(aim,Player,1000,8),"stale/future/recenter changes retire mounted aim");
    auto old=aim;old.mappingTick=849;Check(!Fresh(old,Player,1000,7),"old world mapping rejected independently from fresh hand input");
    old=aim;old.mappingTick=0;Check(!Fresh(old,Player,1000,7),"missing mapping certificate cannot be inferred from a fresh controller");
    old=aim;old.player+=4;Check(!Fresh(old,Player,1000,7),"controller from different player rejected");
    old=aim;old.pairId=0;Check(!Fresh(old,Player,1000,7),"no rendered world mapping pair rejected");
    NativeCameraMath::Vec3 target;Check(Target(aim,target)&&target.x==10&&target.y==20&&target.z==10030,"native world target follows100metres from controller origin without camera/body writes");
    aim.direction={1,0,0};Check(Target(aim,target)&&target.x==10010&&target.z==30,"physical controller yaw changes native target direction");
    aim.direction={0,1,0};Check(Target(aim,target)&&target.y==10020,"physical controller pitch changes native target direction");
    aim.direction={0,0,0};Check(!Target(aim,target),"degenerate controller ray never reaches MG setter");
    aim.direction={std::numeric_limits<float>::quiet_NaN(),0,0};Check(!Target(aim,target),"nonfinite direction rejected");
    aim.direction={0,0,1};aim.unitsPerMetre=0;Check(!Target(aim,target),"unknown world scale rejected");
    using namespace NativeCameraMath;
    MountPose pose;pose.carrier.basis={{0,0,1},{0,1,0},{1,0,0}};pose.origin={10,20,30};pose.nativeLocal={0,0,1};RelativeAim relative;
    const auto near=[](Vec3 a,Vec3 b){return Dot(Subtract(a,b),Subtract(a,b))<.01f;};
    Check(relative.Select(pose,{},1,7,1000,1000,0,0,100,target)&&near(target,{10,20,10030}),"physical takeover starts at native mount-local direction without an absolute-world snap");
    const float turn=.5f;const Quat hand{0,-std::sin(turn/2),0,std::cos(turn/2)};
    auto coarseClock=relative;Vec3 coarseTarget;
    Check(coarseClock.Select(pose,hand,1,7,1000,1000,0,0,100,coarseTarget)&&near(coarseTarget,{10,20,10030}),
        "hand movement at the same tick does not steer the gun");
    Check(relative.Select(pose,hand,1,7,1020,1020,0,0,100,target)&&near(target,{10,20,10030}),"hand yaw does not steer the gun");
    const auto heldLocal=Vec3{std::sin(relative.yaw)*std::cos(relative.pitch),std::sin(relative.pitch),std::cos(relative.yaw)*std::cos(relative.pitch)};
    const Quat carTurn{0,std::sin(.6f),0,std::cos(.6f)};
    pose.carrier.basis={Rotate(carTurn,pose.carrier.basis.forward),Rotate(carTurn,pose.carrier.basis.up),Rotate(carTurn,pose.carrier.basis.right)};pose.origin={700,800,900};
    Check(relative.Select(pose,hand,1,7,1040,1040,0,0,100,target)&&near(target,Add(pose.origin,Scale(NativePlayerPose::Transform(pose.carrier.basis,heldLocal),10000))),
        "held controller transports through current car heading and translation instead of maintaining a world-fixed target");
    const auto beforeStick=relative.yaw;
    Check(relative.Select(pose,hand,1,7,1090,1090,1,0,100,target)&&std::fabs(relative.yaw-beforeStick-.04537856f)<.0001f,
        "stick entry adds only bounded mount-local rate rather than switching to the third-person native point");
    const auto stickTarget=target;
    Check(relative.Select(pose,hand,1,7,1110,1110,0,0,100,target)&&near(target,stickTarget),"stick release retains adjusted local aim with no handoff jump");
    const Quat up{std::sin(.15f),0,0,std::cos(.15f)};
    Check(relative.Select(pose,Multiply(up,hand),1,7,1130,1130,0,0,100,target)&&std::fabs(relative.pitch)<.0001f,"hand elevation does not steer the gun");
    const auto previousTarget=target;const auto previousYaw=relative.yaw;
    Check(!relative.Select(pose,{},1,7,1181,1130,0,0,100,target)&&near(target,previousTarget)&&relative.yaw==previousYaw,"stale raw physical input cannot advance or alter mounted state");
    auto stalled=relative;const auto heldAfterStall=target;const auto oldNative=pose.nativeLocal;pose.nativeLocal={1,0,0};
    Check(stalled.Select(pose,hand,1,7,1500,1500,0,0,100,target)&&near(target,heldAfterStall),
        "fresh input after a long stall preserves the same mount-local aim instead of reseeding a third-person direction");
    const float stalledYaw=stalled.yaw;
    Check(stalled.Select(pose,hand,1,7,2000,2000,1,0,100,target)&&stalled.yaw==stalledYaw,
        "resuming stick input cannot integrate an unobserved stall interval");
    Check(stalled.Select(pose,hand,1,7,2020,2020,1,0,100,target)&&std::fabs(stalled.yaw-stalledYaw-.01815142f)<.0001f,
        "mounted stick runs at52 degrees per second after resuming");pose.nativeLocal=oldNative;
    Check(relative.Select(pose,hand,2,8,1200,1200,0,0,100,target)&&std::fabs(relative.yaw)<.0001f&&std::fabs(relative.pitch)<.0001f,"new mount epoch or recenter rebases to current native direction and fresh physical reference");
    auto snapshots=Mounted();constexpr std::uint32_t gunGeometry=Gun+0x100;
    NativePlayerPose::Geometry geometry;geometry.pose.basis={{0,0,1},{0,1,0},{1,0,0}};snapshots.Set(Geometry,geometry);snapshots.Set(Geometry+0x60,Entity);
    geometry.pose.position={10,20,30};snapshots.Set(gunGeometry,geometry);snapshots.Set(gunGeometry+0x60,Gun);snapshots.Set(Gun+4,gunGeometry);snapshots.Set(Controller+0x14,Vec3{0,0,1});MountPose mountedPose;
    Check(SnapshotMountPose(Base,SnapshotIdentity(Base,Player,snapshots.Reader()),snapshots.Reader(),mountedPose)&&near(mountedPose.origin,{10,20,30}),"read-only mount pose uses current reciprocal carrier and gun geometry WORLD");
    snapshots.Set(gunGeometry+0x60,std::uint32_t(Gun+4));Check(!SnapshotMountPose(Base,valid,snapshots.Reader(),mountedPose),"foreign barrel geometry cannot supply a steering origin");
    std::printf("Mounted native identity/aim: %u checks PASS.\n",checks);return 0;
}
