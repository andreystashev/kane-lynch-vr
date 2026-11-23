// Actual production scanner/policy and pinned native PlayerMonitor dispatcher
// execute only on private CPU objects. No game, HMD, input or saved files.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
namespace TutorialFixture {inline DWORD fixtureTick=1000;inline DWORD Clock(){return fixtureTick;}}
#define GetTickCount TutorialFixture::Clock
#include "native_tutorial_kl.cpp"
#undef GetTickCount
#include "native_hud_test_fixture.hpp"
#include <cstdio>
#include <cstdlib>

namespace TutorialFixture {
using namespace NativeTutorialKL;
unsigned checks=0,signals=0;bool ready=true,cinema=false,team=false,inventory=false,hostContext=true,observer=false,chain=false;
std::uintptr_t player=0,input=0,data=0;unsigned currentBlock=0;
NativeActionKL::State action;std::array<std::uintptr_t,6> objects{},owners{},nodes{},names{},outputs{};
void Check(bool value,const char* label){++checks;if(!value){std::printf("FAIL %s\n",label);std::exit(1);}}
template<class T>void Put(std::uintptr_t at,const T& value){std::memcpy(reinterpret_cast<void*>(at),&value,sizeof(value));}
std::uintptr_t Allocate(std::size_t bytes,DWORD access=PAGE_READWRITE){const auto p=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,bytes,MEM_RESERVE|MEM_COMMIT,access));Check(p!=0,"allocate private native-layout fixture");return p;}
void String(std::uintptr_t at,const char* text){std::array<char,192> buffer{};std::memcpy(buffer.data(),text,std::strlen(text)+1);Put(at,buffer);}
int __thiscall Count(void*){return 1;}
std::uint32_t __thiscall Element(void*,unsigned index){Check(index==0,"actual native list indexes its legitimate sole linked output");return 0x11223344;}
void __thiscall Send(void* owner,std::uint32_t target,std::uint32_t message,std::uint32_t argument){
    Check(target==0x11223344&&message==0x1234&&!argument,"actual pinned dispatcher retains linked reference, native message id and argument");
    Check(reinterpret_cast<std::uintptr_t>(owner)==owners[currentBlock],"only current exact monitor's owner receives its script continuation");++signals;
    if(chain&&currentBlock+1<6){++currentBlock;Put(objects[currentBlock]+0x34,std::uint8_t(1));}
}
void Jump(std::uintptr_t at,std::uintptr_t callback){const unsigned char jmp=0xE9;Put(at,jmp);Put(at+1,std::int32_t(callback-(at+5)));}
void Reset(unsigned block=0){using namespace NativeTutorialKL;SetGameplayContext(false);policy.Reset();fixtureTick=1000;ready=hostContext=true;cinema=team=inventory=observer=chain=false;signals=0;currentBlock=block;
    Put(image+0x8883A4,std::uint16_t(0));Put(input+0x18,std::uint8_t(0));Put(data+0xC7C,std::uint32_t(6));
    for(unsigned i=0;i<6;++i){Monitor monitor;monitor.vtable=static_cast<std::uint32_t>(image+MonitorVtable);monitor.enabled=i==block;monitor.outputs[Definitions[i].event]=static_cast<std::uint32_t>(outputs[i]);
        Put(objects[i],monitor);Put(objects[i]+0x24,std::uint32_t(owners[i]));}
    action.valid=true;action.player=player;action.movement=player+0x200;action.movementKind=NativeActionKL::Movement::Stand;
}
void Tick(){SetGameplayContext(true);observer=true;SimulationTick(player);observer=false;}
void Fire(){NotifyShot(player);}
}
namespace NativeActionKL {State Current(){return TutorialFixture::action;}}
namespace NativeCoverKL {bool SimulationCallbackActive(std::uintptr_t player){return TutorialFixture::observer&&player==TutorialFixture::player;}}
namespace NativeCameraKL {bool CinematicActive(){return TutorialFixture::cinema;}}
namespace VRBridge {
bool Ready(){return TutorialFixture::ready;}bool TeamHudVisible(){return TutorialFixture::team;}bool WeaponMenuVisible(){return TutorialFixture::inventory;}
SecondaryFireInput ReadSecondaryFire(){return {TutorialFixture::hostContext,false,TutorialFixture::fixtureTick};}
}
int main(int argc,char** argv){using namespace TutorialFixture;using namespace NativeTutorialKL;
    if(argc<2){std::puts("SKIP native tutorial dispatch proof: provide pinned EXE read-only.");return 0;}
    NativeHudFixture::Image exe;Check(exe.Load(argv[1])&&Verify(NativeHudFixture::Base,exe.Reader()),"actual supported EXE verifies monitor/name/selection/output bytes before copying private CPU code");
    image=Allocate(0x900000,PAGE_EXECUTE_READWRITE);data=Allocate(0x2000);player=Allocate(0x2000);input=Allocate(0x100);
    Put(image+0x68760C,std::uint32_t(data));Put(data+0xA2C,std::uint32_t(player));Put(data+0xA6C,std::int8_t(0));Put(player,std::uint32_t(image+0x56582C));Put(player+0x9B8,std::uint32_t(input));
    for(unsigned i=0;i<6;++i){objects[i]=Allocate(0xA8);owners[i]=Allocate(0x100);nodes[i]=Allocate(0x80);names[i]=Allocate(192);outputs[i]=Allocate(0x20);
        Put(data+0xC80+i*4,std::uint32_t(objects[i]));Put(owners[i],std::uint32_t(image+0x541000));Put(owners[i]+4,std::uint32_t(nodes[i]));
        Put(nodes[i]+0x50,std::uint32_t(0));Put(nodes[i]+0x60,std::uint32_t(owners[i]));Put(nodes[i]+0x6C,std::uint32_t(names[i]));
        char full[192]{};std::strcpy(full,Prefix);std::strcat(full,Definitions[i].name);String(names[i],full);Put(outputs[i],std::uint32_t(image+0x540000));}
    Put(image+0x540000+0x14,std::uint32_t(image+0x1200));Put(image+0x540000+0x34,std::uint32_t(image+0x1300));Put(image+0x541000+0x130,std::uint32_t(image+0x1400));
    Jump(image+0x1200,reinterpret_cast<std::uintptr_t>(Count));Jump(image+0x1300,reinterpret_cast<std::uintptr_t>(Element));Jump(image+0x1400,reinterpret_cast<std::uintptr_t>(Send));
    // Copy the exact native body and its 22-entry jump table, relocating only
    // two absolute data operands and the table's native code addresses.
    Check(exe.Read(NativeHudFixture::Base+Dispatch,reinterpret_cast<void*>(image+Dispatch),0x1A4),"copy actual native monitor dispatcher to private executable arena");
    Put(image+Dispatch+0x38,std::uint32_t(image+0x2B8DDC));Put(image+Dispatch+0x75,std::uint32_t(image+0x88CA64));
    for(unsigned i=0;i<22;++i){std::uint32_t target=0;Check(exe.Read(NativeHudFixture::Base+0x2B8DDC+i*4,&target,4)&&target>=NativeHudFixture::Base+Dispatch&&target<NativeHudFixture::Base+Dispatch+0x149,"native event table entry stays inside proven dispatcher body");
        Put(image+0x2B8DDC+i*4,std::uint32_t(image+target-NativeHudFixture::Base));}
    Put(image+0x88CA64,std::uint16_t(0x1234));FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(image),0x900000);
    signal=reinterpret_cast<Signal>(image+Dispatch);installed=true;
    for(unsigned i=0;i<6;++i){Reset(i);Tick();Fire();Check(signals==0,"shot observer only records a scoped token, never mutates scripts within a weapon callback");fixtureTick+=20;Tick();
        Check(signals==1&&*reinterpret_cast<unsigned char*>(objects[i]+0x90+Definitions[i].event)==1,"actual production shot path executes exact native output and native dedup for each authored tutorial stage");
        fixtureTick+=20;Tick();Check(signals==1,"native completed stage and consumed shot cannot repeat");}
    Reset();Tick();for(fixtureTick=1050;fixtureTick<4000;fixtureTick+=50)Tick();Check(signals==0,"actual native output remains untouched during the first 2999 active milliseconds");Tick();
    Check(signals==1,"actual native output fires at three eligible seconds without any shot");
    Reset();Tick();for(fixtureTick=1050;fixtureTick<=2500;fixtureTick+=50)Tick();SetGameplayContext(false);fixtureTick=30000;Tick();
    Check(signals==0&&policy.elapsed==0,"actual scene suspension discards previous timer rather than counting pause or loading time");
    Reset();chain=true;Tick();Fire();fixtureTick+=20;Tick();Check(signals==1&&currentBlock==1,"a legitimate first output may enable the next native teaching monitor");
    fixtureTick+=20;Tick();Check(signals==1&&policy.elapsed==0,"one physical shot does not cascade through a newly enabled tutorial stage");
    Fire();fixtureTick+=20;Tick();Check(signals==2,"next newly enabled stage accepts its own later player shot");
    Reset();Tick();signal(reinterpret_cast<void*>(objects[0]),Definitions[0].event);
    Check(signals==1,"original native tutorial event can fire immediately without a shot or fallback timeout");
    fixtureTick+=20;Tick();Fire();fixtureTick+=20;Tick();
    Check(signals==1,"native event dedup prevents later fallback/shot from replaying an already completed block");
    Reset();chain=true;Tick();Fire();signal(reinterpret_cast<void*>(objects[0]),Definitions[0].event);
    fixtureTick+=20;Tick();Check(signals==1&&currentBlock==1&&!policy.elapsed,
        "native completion after a pending shot wins and cannot carry that old shot into the next block");
    for(fixtureTick+=50;fixtureTick<4020;fixtureTick+=50)Tick();
    Check(signals==1,"new block receives its own full three-second wait after native completion");Tick();
    Check(signals==2,"fallback remains additive after native completion enables a different exact block");
    for(unsigned mode=0;mode<9;++mode){Reset();Tick();switch(mode){case 0:ready=false;break;case 1:cinema=true;break;case 2:team=true;break;case 3:inventory=true;break;case 4:hostContext=false;break;
        case 5:Put(input+0x18,std::uint8_t(1));break;case 6:Put(image+0x8883A4,std::uint8_t(1));break;case 7:action.movementKind=NativeActionKL::Movement::Vehicle;break;default:SetGameplayContext(false);break;}
        Fire();fixtureTick+=20;observer=true;SimulationTick(player);observer=false;Check(signals==0,"actual gameplay/UI/pause/vehicle/suspension gate cannot accept shot or timeout");}
    Reset();Tick();NotifyShot(player+4);fixtureTick+=20;Tick();Check(signals==0,"foreign actor shot cannot satisfy Kane's lesson");
    Reset();Tick();Fire();Put(objects[0]+0x34,std::uint8_t(0));Put(objects[1]+0x34,std::uint8_t(1));fixtureTick+=20;Tick();
    Check(signals==0,"a shot bound to the previous teaching monitor cannot skip a different active stage");
    Reset();Tick();Put(objects[1]+0x34,std::uint8_t(1));fixtureTick+=20;Tick();Fire();fixtureTick=11000;Tick();Check(signals==0,"ambiguous simultaneous tutorial stages stay native instead of inventing an order");
    std::printf("M02 tutorial actual native dispatcher: %u checks PASS.\n",checks);
}
