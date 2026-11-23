#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "native_grenade_visual_kl.hpp"
#include "native_camera_kl.hpp"
#include "native_hands_kl.hpp"
#include "native_hud_test_fixture.hpp"
#include <cstdio>
#include <cstdlib>
namespace {
NativeHudFixture::Memory memory;unsigned checks=0;bool cameraValid=true;std::uint64_t activePair=7,activeGeneration=11;
constexpr std::uintptr_t Base=0x400000,Data=0x10000000,Player=0x10001000,Component=0x10002000,Manager=0x10003000,
    Pool=0x10004000,Root=0x10005000,Generations=0x10006000,Child=0x10007000,Item=0x10008000,Model=0x10009000,Entry=0x1000A000,State=0x1000B000;
void Check(bool okay,const char* what){++checks;if(!okay){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}}
BOOL WINAPI PrivateRead(HANDLE,LPCVOID at,LPVOID output,SIZE_T count,SIZE_T* got){const bool ok=memory.Read(reinterpret_cast<std::uintptr_t>(at),output,count);if(got)*got=ok?count:0;return ok;}
void Bind(){NativeGrenadeVisualKL::ObserveWorldBind(reinterpret_cast<void*>(State),reinterpret_cast<const NativeCameraMath::CameraPose*>(Entry+0x10));}
}
namespace NativeCameraKL {bool DrawStateActive(std::uintptr_t state,bool* firstPerson,bool){if(firstPerson)*firstPerson=true;return cameraValid&&state==State;}}
namespace NativeHandsKL {bool DrawFrameActive(std::uint64_t& pair,std::uint64_t* generation){pair=activePair;if(generation)*generation=activeGeneration;return true;}}
#define ReadProcessMemory PrivateRead
#include "native_grenade_visual_kl.cpp"
#undef ReadProcessMemory
int main(){
    using namespace NativeGrenadeVisualKL;using namespace NativeWeaponOwnership;
    static_assert(sizeof(void*)==4,"native object fixture is x86");
    image=Base;memory.Set(Base+0x68760C,std::uint32_t(Data));memory.Set(Data+0xA2C,std::uint32_t(Player));
    memory.Set(Player,std::uint32_t(Base+0x56582C));memory.Set(Player+0x88,std::uint32_t(Component));
    memory.Set(Component+0x14,std::uint32_t(1));Record record{};record.geometryRef=1;record.bone=31;memory.Set(Component+0x18,record);
    memory.Set(Base+0x6C99B8,std::uint32_t(Manager));memory.Set(Manager+0xC,std::uint32_t(Pool));
    memory.Set(Pool+4,std::uint32_t(0x70));memory.Set(Pool+8,std::uint32_t(2));memory.Set(Pool+0xC,std::uint32_t(Root));memory.Set(Pool+0x10,std::uint32_t(Generations));memory.Set(Generations,std::uint16_t(0));
    GeometryLinks links{};links.owner=Item;memory.Set(Root+0x50,links);links={};links.parent=Root;memory.Set(Child+0x50,links);
    memory.Set(Item,std::uint32_t(Base+0x577B74));memory.Set(Item+4,std::uint32_t(Root));memory.Set(Item+0xC0,std::uint32_t(Model));memory.Set(Item+0x58,std::uint32_t(123));
    memory.Set(Model,std::uint32_t(Base+0x577864));memory.Set(Model+0xA4,std::uint32_t(5));
    memory.Set(Entry,std::uint32_t(Base+5508832));memory.Set(Entry+0xF4,std::uint32_t(Child));memory.Set(Base+0x7678A4,std::uint32_t(0));
    NativeCameraMath::CameraPose world;memory.Set(State+0x8C,world);
    SetFrame(Player,7,false);Bind();Check(!SuppressDraw(),"ordinary gameplay retains the complete held gun");
    SetFrame(Player,7,true);Bind();Check(SuppressDraw(),"grenade visual phase suppresses an exact owned held firearm descendant");
    Check(SuppressDraw(),"subsequent unchanged material pass stays hidden");
    world.position.x=1;memory.Set(State+0x8C,world);Check(!SuppressDraw(),"unobserved WORLD replacement cannot inherit the old gun suppression");
    world.position.x=0;memory.Set(State+0x8C,world);
    const auto intermediate=std::uintptr_t(0x1000C000);
    links={};links.parent=intermediate;memory.Set(Child+0x50,links);links.parent=Root;memory.Set(intermediate+0x50,links);
    SetFrame(Player,7,true);Bind();links.parent=Root+0x100;memory.Set(intermediate+0x50,links);
    Check(!SuppressDraw(),"an intermediate reparent after observation cannot hide an unrelated descendant");
    links.parent=Root;memory.Set(intermediate+0x50,links);SetFrame(Player,7,true);Bind();
    Check(SuppressDraw(),"fresh full ancestry qualifies the owned suppressor or gun part");
    links.parent=Root+0x100;memory.Set(Child+0x50,links);Check(!SuppressDraw(),"late leaf reparent retires a multipass certificate even when inventory owner stayed unchanged");
    links.parent=Root;memory.Set(Child+0x50,links);
    record.bone=34;memory.Set(Component+0x18,record);SetFrame(Player,7,true);Bind();
    Check(SuppressDraw(),"the same local gun remains hidden after native throw animation moves it to a stowed attachment");
    record.bone=35;memory.Set(Component+0x18,record);Check(!SuppressDraw(),"attachment identity mutation retires a previously tagged material pass");
    record.bone=34;memory.Set(Component+0x18,record);SetFrame(Player,7,true);Bind();
    memory.Set(Item+0x58,std::uint32_t(124));Check(!SuppressDraw(),"recycled weapon reference cannot inherit hidden state");memory.Set(Item+0x58,std::uint32_t(123));
    for(unsigned category:{11u,12u}){memory.Set(Model+0xA4,category);SetFrame(Player,7,true);Bind();Check(!SuppressDraw(),"the actual grenade item remains visible");}
    memory.Set(Model+0xA4,std::uint32_t(5));SetFrame(Player,7,true);Bind();cameraValid=false;Check(!SuppressDraw(),"native menus or a foreign VIEW never suppress scene objects");cameraValid=true;
    SetFrame(Player,7,true);Bind();activePair=8;Check(!SuppressDraw(),"an eye-pair mismatch cannot hide an old gun");activePair=7;
    SetFrame(Player,7,true);Bind();Check(SuppressDraw(),"fresh bound generation starts a new suppression certificate");++activeGeneration;Check(!SuppressDraw(),"retired native draw generation closes the certificate");
    SetFrame(Player,7,true);Bind();for(unsigned i=0;i<64;++i)Check(SuppressDraw(),"bounded multipass draw retains an exact owned gun");Check(!SuppressDraw(),"draw budget prevents unbounded late tag reuse");
    SetFrame(Player,7,true);Bind();memory.Set(Data+0xA2C,std::uint32_t(Player+0x100));Check(!SuppressDraw(),"another local-player identity cannot reuse this gesture");memory.Set(Data+0xA2C,std::uint32_t(Player));
    SetFrame(Player,7,true);Bind();ClearFrame();Check(!SuppressDraw(),"capture/reset retirement restores native visibility without altering native item state");
    Check(draw.item==0&&memory.bytes.at(Item)==static_cast<unsigned char>((Base+0x577B74)&255),"visual suppression never moves or deletes the native gun");
    std::printf("Grenade owned-firearm draw policy: %u checks PASS; actual adapter/private native memory only.\n",checks);
}
