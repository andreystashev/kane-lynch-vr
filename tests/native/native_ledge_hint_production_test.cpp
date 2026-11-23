// Selected pinned CanStart/math/getters execute in a rebased private arena.
// No executable entry point, game/runtime/input or mission is launched.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
namespace LedgeFixture {inline DWORD tick=1000;inline DWORD Clock(){return tick;}}
#define GetTickCount LedgeFixture::Clock
#include "native_cover_kl.cpp"
#include "native_ledge_hint_kl.cpp"
#undef GetTickCount
#include "native_hud_test_fixture.hpp"
#include <cstdio>
#include <cstdlib>
extern "C" {
MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*){return MH_ERROR_NOT_INITIALIZED;}
MH_STATUS WINAPI MH_EnableHook(LPVOID){return MH_ERROR_NOT_INITIALIZED;}
MH_STATUS WINAPI MH_QueueEnableHook(LPVOID){return MH_ERROR_NOT_INITIALIZED;}
MH_STATUS WINAPI MH_QueueDisableHook(LPVOID){return MH_ERROR_NOT_INITIALIZED;}
MH_STATUS WINAPI MH_DisableHook(LPVOID){return MH_ERROR_NOT_INITIALIZED;}
MH_STATUS WINAPI MH_RemoveHook(LPVOID){return MH_ERROR_NOT_INITIALIZED;}
MH_STATUS WINAPI MH_ApplyQueued(){return MH_ERROR_NOT_INITIALIZED;}
}
namespace LedgeFixture {
using namespace NativeCameraMath;constexpr std::uintptr_t Base=0x20000000;
unsigned checks=0,rays=0;bool ready=true,cinema=false,team=false,inventory=false,host=true;
enum class Scene {Ledge,Miss,Headroom};Scene scene=Scene::Ledge;
std::uintptr_t player=0,climb=0,stand=0,geometry=0,dispatcher=0,backend=0,world=0,hitGeometry=0,hitOwner=0;
NativeActionKL::State state;
LONG WINAPI Exception(EXCEPTION_POINTERS* p){std::printf("private fixture exception=%lx pc=%p fault=%p rays=%u\n",p->ExceptionRecord->ExceptionCode,p->ExceptionRecord->ExceptionAddress,reinterpret_cast<void*>(p->ExceptionRecord->ExceptionInformation[1]),rays);return EXCEPTION_CONTINUE_SEARCH;}
void Check(bool okay,const char* text){++checks;if(!okay){std::printf("FAIL %s\n",text);std::exit(1);}}
template<class T>void Put(std::uintptr_t at,const T& value){std::memcpy(reinterpret_cast<void*>(at),&value,sizeof(value));}
std::uintptr_t Allocate(std::size_t n,DWORD access=PAGE_READWRITE){const auto at=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,n,MEM_RESERVE|MEM_COMMIT,access));Check(at!=0,"allocate private native-layout objects");return at;}
bool Reader(std::uintptr_t at,void* out,std::size_t n){SIZE_T got=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),out,n,&got)&&got==n;}
void Jump(std::uintptr_t at,std::uintptr_t to){Put(at,std::uint8_t(0xE9));Put(at+1,std::int32_t(to-at-5));}
bool __thiscall ClassFalse(void*){return false;}
bool __thiscall Ray(void*,void* output,std::uint32_t,const Vec3*,const Vec3*,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t){
    const auto index=rays++;if(scene==Scene::Miss)return false;
    const bool hit=index<4||(scene==Scene::Headroom)||index==6;
    if(!hit)return false;
    const auto at=reinterpret_cast<std::uintptr_t>(output);Put(at,1.f);Put(at+4,Vec3{0,60,50});Put(at+0x10,std::uint32_t(hitGeometry));
    Put(at+0x14,Vec3{0,0,0});Put(at+0x20,Vec3{1,0,0});Put(at+0x2C,Vec3{0,0,1});return true;
}
bool heldB=false;
void Reset(){heldB=false;rays=0;tick+=100;ready=host=true;cinema=team=inventory=false;scene=Scene::Ledge;
    Put(Base+0x8883A4,std::uint16_t(0));Put(dispatcher+4,std::uint32_t(0));Put(climb+0x54,std::uint8_t(0));Put(climb+0xE8,std::uint8_t(0));
    state=NativeActionKL::Snapshot(Base,Reader);NativeLedgeHintKL::SetGameplayContext(false);NativeLedgeHintKL::SetGameplayContext(true);}
bool Tick(){NativeCoverKL::simulationPlayer=player;NativeLedgeHintKL::SimulationTick(player);NativeCoverKL::simulationPlayer=0;return NativeLedgeHintKL::Available(player);}
}
namespace NativeActionKL {bool InitializeVerifiedImage(std::uintptr_t){return true;}State Current(){return LedgeFixture::state;}}
namespace NativeCameraKL {bool CinematicActive(){return LedgeFixture::cinema;}}
namespace VRBridge {bool Ready(){return LedgeFixture::ready;}bool TeamHudVisible(){return LedgeFixture::team;}bool WeaponMenuVisible(){return LedgeFixture::inventory;}long RecenterAck(){return 1;}bool CoverHeld(){return false;}bool ClimbHeld(){return LedgeFixture::heldB;}
SecondaryFireInput ReadSecondaryFire(){return {LedgeFixture::host,false,LedgeFixture::tick};}}
int main(int argc,char** argv){using namespace LedgeFixture;std::setvbuf(stdout,nullptr,_IONBF,0);
    AddVectoredExceptionHandler(1,Exception);
    if(argc<2){std::puts("SKIP provide pinned EXE read-only.");return 0;}
    const auto arena=VirtualAlloc(reinterpret_cast<void*>(Base),0x94C000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);
    NativeHudFixture::Image exe;Check(exe.Load(argv[1])&&NativeLedgeHintKL::Verify(NativeHudFixture::Base,exe.Reader()),"pinned native class, eligibility, helper, getters and query backend verify");

    if(arena!=reinterpret_cast<void*>(Base)){MEMORY_BASIC_INFORMATION info{};VirtualQuery(reinterpret_cast<void*>(Base),&info,sizeof(info));std::printf("arena error=%lu exe=%p area=%p size=%lu state=%lx\n",GetLastError(),GetModuleHandleW(nullptr),info.AllocationBase,info.RegionSize,info.State);}
    Check(arena==reinterpret_cast<void*>(Base),"private rebased native CPU arena allocated without loading an executable module");std::memcpy(arena,exe.mapped.data(),exe.mapped.size());
    // Rebase only the proven selected CPU path's absolute operands; all native
    // relative calls/branches stay byte-identical. Vtable slots below are the
    // exact owner getters actually consumed by CanStart.
    constexpr std::uintptr_t operands[]={0x1F696,0x1F6B2,0x2FBD8,0x2FC27,0x1E8A9E,0x205C0A,0x205C17,0x205C2B,0x205C3E,0x205DE5,0x205E0C,0x205E56,0x205F3F,0x205F7A,0x205FAC,0x205FC2,0x20607A,0x206103,0x20611C,0x206135,0x206164,0x2061AE,0x2061B4,0x2061CE,0x20621B,0x206234,0x20624F,0x20625D,0x206277,0x2062B3,0x2062C9,0x2062CF,0x20635E,0x206393};
    for(auto rva:operands){std::uint32_t value=0;std::memcpy(&value,exe.mapped.data()+rva,4);Check(value>=NativeHudFixture::Base&&value<NativeHudFixture::Base+exe.mapped.size(),"relocated operand remains inside supported native image");Put(Base+rva,std::uint32_t(value+Base-NativeHudFixture::Base));}
    for(auto slot:{0x3A8u,0x584u,0x588u,0x5A0u,0x5C4u}){std::uint32_t value=0;std::memcpy(&value,exe.mapped.data()+0x56582C+slot,4);Put(Base+0x56582C+slot,std::uint32_t(value+Base-NativeHudFixture::Base));}
    Put(Base+0x541274+0x2C,std::uint32_t(Base+0xC3B20));

    player=Allocate(0x1600);climb=Allocate(0xEC);stand=Allocate(0x1400);geometry=Allocate(0x100);dispatcher=Allocate(0x20);backend=Allocate(0x100);world=Allocate(0x100);hitOwner=Allocate(0x900);hitGeometry=Allocate(0x100);
    const auto data=Allocate(0x1600),system=Allocate(0x200),engine=Allocate(0x200);
    Put(Base+0x68760C,std::uint32_t(data));Put(data+0xA2C,std::uint32_t(player));Put(player,std::uint32_t(Base+0x56582C));Put(player+4,std::uint32_t(geometry));Put(player+0x67C,std::uint32_t(stand));
    Put(player+0x604,std::uint32_t(stand));Put(player+0x5F8+11*4,std::uint32_t(climb));Put(stand,std::uint32_t(Base+0x566A44));Put(stand+0x38,std::uint32_t(player));Put(stand+0x44,std::uint32_t(3));
    Put(player+0x194,std::uint32_t(dispatcher));Put(dispatcher,std::uint32_t(Base+0x54DE4C));Put(dispatcher+8,std::uint32_t(player));
    Put(climb,std::uint32_t(Base+0x566C44));Put(climb+0x38,std::uint32_t(player));Put(climb+0x44,std::uint32_t(11));
    for(unsigned i=0;i<3;++i){Put(climb+0x68+i*16,std::array<std::uint16_t,4>{10,11,12,13});Put(climb+0x70+i*16,200.f);Put(climb+0x74+i*16,30.f);}
    NativePlayerPose::Geometry pose;pose.pose.basis={{0,0,1},{0,1,0},{1,0,0}};Put(geometry,pose);Put(geometry+0x60,std::uint32_t(player));Put(hitGeometry,pose);Put(hitGeometry+0x60,std::uint32_t(hitOwner));
    Put(hitOwner+4,std::uint32_t(hitGeometry));Put(Base+0x7C1D34,std::uint32_t(backend));Put(backend,std::uint32_t(Base+0x541274));
    Put(Base+0x6875F0,std::uint32_t(system));Put(system+0x48,std::uint32_t(1000));Put(system+0xA8,std::uint32_t(engine));
    Jump(Base+0xC3B20,reinterpret_cast<std::uintptr_t>(Ray));Jump(Base+0x66CC0,reinterpret_cast<std::uintptr_t>(ClassFalse));Jump(Base+0x196ED0,reinterpret_cast<std::uintptr_t>(ClassFalse));
    const auto savedIntent=Allocate(0x40,PAGE_EXECUTE_READWRITE);std::memcpy(reinterpret_cast<void*>(savedIntent),reinterpret_cast<void*>(Base+0x1E8A90),0x32);
    NativeCoverKL::movementIntent=reinterpret_cast<NativeCoverKL::MovementIntent>(savedIntent);Jump(Base+0x1E8A90,reinterpret_cast<std::uintptr_t>(NativeCoverKL::MovementIntentHook));
    const auto savedLatch=Allocate(0x20,PAGE_EXECUTE_READWRITE);
    std::memcpy(reinterpret_cast<void*>(savedLatch),reinterpret_cast<void*>(Base+NativeLedgeKL::LatchRva),12);
    NativeCoverKL::climbLatch=reinterpret_cast<NativeCoverKL::MovementIntent>(savedLatch);
    Jump(Base+NativeLedgeKL::LatchRva,reinterpret_cast<std::uintptr_t>(NativeCoverKL::ClimbLatchHook));
    const auto inputController=Allocate(0x20);Put(player+0x9B8,std::uint32_t(inputController));
    NativeCoverKL::verifiedImage=Base;NativeCoverKL::actionClimbInstalled=true;NativeLedgeHintKL::image=Base;NativeLedgeHintKL::installed=true;
    FlushInstructionCache(GetCurrentProcess(),arena,exe.mapped.size());Reset();
    std::array<unsigned char,0x1600> playerBefore{};std::array<unsigned char,0xEC> climbBefore{};std::array<unsigned char,0x100> geometryBefore{};
    Reader(player,playerBefore.data(),playerBefore.size());Reader(climb,climbBefore.data(),climbBefore.size());Reader(geometry,geometryBefore.data(),geometryBefore.size());
    auto unassisted=NativeLedgeHintKL::Capture(Base,Reader).native;unassisted[0xE8]=1;
    const auto direct=reinterpret_cast<NativeCoverKL::Eligibility>(Base+0x205B50);
    Check(!direct(unassisted.data(),0,0,0)&&!rays,"actual native input gate rejects stationary no-Use actor before geometry when TLS probe is absent");
    NativeCoverKL::simulationPlayer=player;Check(!NativeCoverKL::ProbePrivateClimb(player,reinterpret_cast<void*>(climb)),"private probe refuses the live registered Climb object");NativeCoverKL::simulationPlayer=0;
    const bool available=Tick();std::printf("native query rays=%u available=%u\n",rays,unsigned(available));
    Check(available&&rays>=7,"actual pinned native eligibility accepts a geometric ledge with clear headroom without forward or Use input");
    Check(!std::memcmp(playerBefore.data(),reinterpret_cast<void*>(player),playerBefore.size())&&!std::memcmp(climbBefore.data(),reinterpret_cast<void*>(climb),climbBefore.size())&&
        !std::memcmp(geometryBefore.data(),reinterpret_cast<void*>(geometry),geometryBefore.size()),"actual native query leaves source actor/input, registered Climb and geometry bit-exact");
    tick+=151;NativeLedgeHintKL::SetGameplayContext(true);Check(!NativeLedgeHintKL::Available(player),"positive ledge certificate expires without running native queries on the render thread");
    using SetFlags=void(__thiscall*)(void*,unsigned,unsigned);using GetFlags=unsigned(__thiscall*)(void*);
    Put(hitOwner+0x110,std::uint32_t(0xAA0));const auto setFlags=reinterpret_cast<SetFlags>(Base+0x10A5F0);const auto getFlags=reinterpret_cast<GetFlags>(Base+0x10A610);
    setFlags(reinterpret_cast<void*>(hitOwner),1,0);Check(getFlags(reinterpret_cast<void*>(hitOwner))==0xAA1,"actual TreeGroup collision mark changes only flags110 bit0");
    setFlags(reinterpret_cast<void*>(hitOwner),0,1);Check(getFlags(reinterpret_cast<void*>(hitOwner))==0xAA0,"actual native collision unmark restores prequery TreeGroup flags exactly");
    Reset();NativeCoverKL::SetActionClimbContext(true);
    Put(player+0x7D0,std::uint32_t(0x20));
    Check(!direct(reinterpret_cast<void*>(climb),0,0,0)&&!rays,"ordinary X alone cannot climb");
    Put(player+0x7D0,std::uint32_t(0));heldB=true;
    Check(!direct(reinterpret_cast<void*>(climb),0,0,0)&&!rays,"first held-B update lets native Climb arm its own latch");
    const auto latched=*reinterpret_cast<const std::uint8_t*>(climb+0xE8);
    Check(latched==1&&direct(reinterpret_cast<void*>(climb),0,0,0)&&rays>=7,
          "second held-B update reaches original ledge and headroom queries without E");
    Check(!*reinterpret_cast<const std::uint32_t*>(player+0x7D0),"the actor TapUse flags stay untouched");
    NativeCoverKL::SetActionClimbContext(false);
    Reset();scene=Scene::Miss;Check(!Tick()&&rays==4,"actual native eligibility rejects absent ledge instead of fabricating wall proximity");
    Reset();scene=Scene::Headroom;Check(!Tick()&&rays>4,"actual native eligibility rejects obstructed climb/headroom");
    Reset();team=true;Check(!Tick()&&!rays,"team selection does not run a native geometry probe");
    Reset();host=false;Check(!Tick()&&!rays,"lost input focus does not run a native geometry probe");
    Reset();NativeLedgeHintKL::SetGameplayContext(false);Check(!Tick()&&!rays,"scene suspension does not run a native geometry probe");
    std::printf("Actual native Climb hint: %u checks PASS.\n",checks);
}
