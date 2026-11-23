// Production hooks and the exact native map thunk/callsite execute solely in
// private CPU arenas. No game/runtime/GUI/input or saved files are opened.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
namespace ObjectiveFixture {inline DWORD tick=1000;inline DWORD Clock(){return tick;}}
#define GetTickCount ObjectiveFixture::Clock
#include "native_objective_visibility_kl.cpp"
#undef GetTickCount
#include "native_hud_test_fixture.hpp"
#include <cstdio>
#include <cstdlib>
extern "C" {
MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*){return MH_ERROR_NOT_INITIALIZED;}
MH_STATUS WINAPI MH_QueueEnableHook(LPVOID){return MH_ERROR_NOT_INITIALIZED;}
MH_STATUS WINAPI MH_QueueDisableHook(LPVOID){return MH_ERROR_NOT_INITIALIZED;}
MH_STATUS WINAPI MH_DisableHook(LPVOID){return MH_ERROR_NOT_INITIALIZED;}
MH_STATUS WINAPI MH_RemoveHook(LPVOID){return MH_ERROR_NOT_INITIALIZED;}
MH_STATUS WINAPI MH_ApplyQueued(){return MH_ERROR_NOT_INITIALIZED;}
}
namespace NativeHudKL {void SetNativeMarkerObserver(NativeMarkerObserver){}}
namespace ObjectiveFixture {
using namespace NativeObjectiveVisibilityKL;
unsigned checks=0,colorOriginals=0,mapOriginals=0;std::uintptr_t data=0,player=0,list=0,records=0,camera=0;
std::uint32_t originalValue=0x0055AA11;
void Check(bool okay,const char* label){++checks;if(!okay){std::printf("FAIL %s\n",label);std::exit(1);}}
template<class T>void Put(std::uintptr_t at,const T& value){std::memcpy(reinterpret_cast<void*>(at),&value,sizeof(value));}
std::uintptr_t Allocate(std::size_t n,DWORD access=PAGE_READWRITE){const auto at=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,n,MEM_COMMIT|MEM_RESERVE,access));Check(at!=0,"allocate private native-layout objects");return at;}
void Jump(std::uintptr_t at,std::uintptr_t to){Put(at,std::uint8_t(0xE9));Put(at+1,std::int32_t(to-at-5));}
std::uint32_t __thiscall NativeColor(void*,void*){++colorOriginals;return originalValue;}
void __thiscall Request(void* manager,int pressed,int slot,const char* name,int mode,int dialog){
    ++mapOriginals;Check(reinterpret_cast<std::uintptr_t>(manager)==image+0x888390&&pressed==1&&slot==0,"actual native map thunk forwards its manager/input");
    Check(mode==2&&dialog==3&&reinterpret_cast<std::uintptr_t>(name)==image+0x576DA8,"actual native map thunk requests ONLY native mode2/IngameMap dialog3");}
}
int main(int argc,char** argv){using namespace ObjectiveFixture;using namespace NativeObjectiveVisibilityKL;
    if(argc<2){std::puts("SKIP provide supported EXE read-only.");return 0;}
    NativeHudFixture::Image exe;Check(exe.Load(argv[1])&&Verify(NativeHudFixture::Base,exe.Reader()),"actual native map/goal proof checked before private code copy");
    image=Allocate(0x900000,PAGE_EXECUTE_READWRITE);data=Allocate(0x2000);player=Allocate(0x2000);list=Allocate(0x20);records=Allocate(0x64*3);camera=Allocate(0x20);
    Put(image+0x68760C,std::uint32_t(data));Put(data+0xA2C,std::uint32_t(player));Put(player,std::uint32_t(image+0x56582C));Put(player+0x804,std::int8_t(0));Put(player+0x805,std::int8_t(0));Put(player+0x9A0,std::uint32_t(camera));
    Put(data+0x1550,std::uint32_t(list));Put(list+8,std::uint32_t(3));Put(list+0x14,std::uint32_t(records));
    for(unsigned i=0;i<3;++i){Put(records+i*0x64+0x1C,std::uint32_t(i==2?7:26));Put(records+i*0x64+0x44,std::uint32_t(0));Put(records+i*0x64+0x48,std::uint32_t(0xE055AA11));}
    originalColor=NativeColor;installed=true;Jump(image+ColorRva,reinterpret_cast<std::uintptr_t>(ColorHook));
    // Enter the exact native ActionMarkers call bytes with its EDI record.
    const unsigned char enter[]={0x57,0x8B,0x7C,0x24,8,0x8B,0x44,0x24,0x0C};
    std::memcpy(reinterpret_cast<void*>(image+0x253C36-sizeof(enter)),enter,sizeof(enter));
    Check(exe.Read(NativeHudFixture::Base+0x253C36,reinterpret_cast<void*>(image+0x253C36),8),"copy native push/mov/call producer bytes without changing its return PC");
    const unsigned char leave[]={0x5F,0xC2,8,0};std::memcpy(reinterpret_cast<void*>(image+ColorCallerRva),leave,sizeof(leave));
    using Caller=std::uint32_t(__stdcall*)(void*,void*);const auto drawColor=reinterpret_cast<Caller>(image+0x253C36-sizeof(enter));
    Check(exe.Read(NativeHudFixture::Base+MapRequestRva,reinterpret_cast<void*>(image+MapRequestRva),28),"copy exact original native map request thunk");
    Put(image+MapRequestRva+14,std::uint32_t(image+0x576DA8));Jump(image+0x274620,reinterpret_cast<std::uintptr_t>(Request));originalMap=reinterpret_cast<MapRequest>(image+MapRequestRva);
    FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(image),0x900000);
    std::array<unsigned char,0x64*3> before{};std::memcpy(before.data(),reinterpret_cast<void*>(records),before.size());
    SetContext(true,false);Check(drawColor(reinterpret_cast<void*>(records),reinterpret_cast<void*>(camera))==originalValue,"released X returns original native temporary-show alpha");
    SetContext(true,true);for(unsigned i=0;i<2;++i)Check(drawColor(reinterpret_cast<void*>(records+i*0x64),reinterpret_cast<void*>(camera))==originalValue,"unscoped native calls retain original goal visibility even while X is held");
    Check(drawColor(reinterpret_cast<void*>(records+2*0x64),reinterpret_cast<void*>(camera))==originalValue,"command shield gets original alpha even while X is held");
    Check(ColorHook(reinterpret_cast<void*>(records),nullptr,nullptr)==originalValue,"unrelated native Color caller retains original visibility");
    Check(drawColor(reinterpret_cast<void*>(records),reinterpret_cast<void*>(camera+4))==originalValue,"actual wrong owner camera retains native alpha");
    Check(colorOriginals==6,"all forced/rejected/released branches still execute original native Color");
    std::array<unsigned char,0x64*3> after{};std::memcpy(after.data(),reinterpret_cast<void*>(records),after.size());Check(before==after,"actual hooks leave all native objective records bit-exact");
    MapHook(reinterpret_cast<void*>(image+0x888390),nullptr,1,0);Check(!mapOriginals,"fresh local map request is blocked before ANY native modal dispatcher side effect");
    SetContext(false,false);MapHook(reinterpret_cast<void*>(image+0x888390),nullptr,1,0);Check(mapOriginals==1,"outside VR gameplay the exact original native map thunk remains usable");
    SetContext(true,true);tick+=251;Check(drawColor(reinterpret_cast<void*>(records),reinterpret_cast<void*>(camera))==originalValue,"stale held context cannot leave a goal forced visible");
    MapHook(reinterpret_cast<void*>(image+0x888390),nullptr,1,0);Check(mapOriginals==2,"expired gameplay certificate preserves original map fallback");
    SetContext(true,true);Put(image+0x8883A4,std::uint8_t(3));Check(drawColor(reinterpret_cast<void*>(records),reinterpret_cast<void*>(camera))==originalValue,"native inventory is preserved independently of team-held state");
    Put(image+0x8883A4,std::uint8_t(0));SetContext(true,false);originalValue=0x7F55AA11;
    Check(drawColor(reinterpret_cast<void*>(records),reinterpret_cast<void*>(camera))==originalValue,"release immediately restores current native authored temporary visibility instead of hiding all objectives");
    std::printf("Objective visibility native producer/map thunk: %u checks PASS.\n",checks);
}
