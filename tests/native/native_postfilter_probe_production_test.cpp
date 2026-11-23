// Actual x86 detour/trampoline in private executable memory; no game or GPU.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
static DWORD fakeTick=1000;
static DWORD WINAPI TestTick(){return fakeTick;}
#define GetTickCount TestTick
#include "native_postfilter_probe_kl.cpp"
#undef GetTickCount
#include "native_hud_test_fixture.hpp"

namespace {
unsigned checks=0;bool frameReady=true;unsigned selectedEye=0;
void Check(bool ok,const char* why){++checks;if(!ok){std::printf("FAIL %s\n",why);std::exit(1);}}
template<class T>void Put(std::uintptr_t at,T value){std::memcpy(reinterpret_cast<void*>(at),&value,sizeof(value));}
bool ProvideFrame(NativePostfilterProbeKL::Frame& out){if(!frameReady)return false;out.id=100+selectedEye;out.eye=selectedEye;return true;}
}
int main(int argc,char** argv){
    using namespace NativePostfilterProbeKL;
    if(argc<2){std::puts("SKIP pinned EXE path required");return 0;}
    wchar_t exePath[MAX_PATH]{};char digest[65]{};
    Check(MultiByteToWideChar(CP_UTF8,0,argv[1],-1,exePath,MAX_PATH)>0&&HashFile(exePath,digest)&&
          IsSupportedExecutableHash(digest),"actual supported EXE SHA256");
    NativeHudFixture::Image game;Check(game.Load(argv[1]),"open pinned PE read only");
    Check(VerifyStage(NativeHudFixture::Base,game.Reader()),"exact stage prologue and caller");
    const unsigned char history[]={0xd9,0xee,0x83,0xec,0x24,0x56,0x8b,0xf1};unsigned char actual[sizeof(history)]{};
    Check(game.Read(0x4b0620,actual,sizeof(actual))&&!std::memcmp(actual,history,sizeof(actual)),"independent existing history hook");
    Check((0x4b0620-0x4b00b0)>sizeof(history),"separate non-overlapping native hook sites");
    const auto base=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,0x800000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    const auto object=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,0x4000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    const auto renderer=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    const auto aux=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,0x2000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    const auto code=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    Check(base&&object&&renderer&&aux&&code,"private memory allocated");
    Put(base+0x7c1b0c,std::uint32_t(object));Put(base+0x6977e8,std::uint32_t(renderer));Put(renderer+0x88,2.f);
    Put(object+0x10,.75f);Put(object+0xe0,std::uint32_t(1));Put(object+0x30,std::uint32_t(0));
    Put(renderer+0xa4,std::uint32_t(1));Put(object+0x32ac,std::uint32_t(3));Put(object+0x3270,std::uint32_t(aux));
    Put(aux+0x16b4,std::uint32_t(8001));Put(aux+0x16b8,std::uint32_t(8002));Put(aux+0x16bc,std::uint32_t(8003));
    const unsigned offsets[]={0x32c4,0x32c8,0x32cc,0x32d0,0xe4,0xe8,0xec,0xf0,0x100,0x104,0x108,0x210,0x3254,0x3258,0x3260,0x3268,0x326c,0x3288,0x328c};
    for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);++i)Put(object+offsets[i],std::uint32_t(100+i));
    // Copy the verified prologue, then one observable native operation and
    // the exact epilogue shape. Real MinHook creates and executes trampoline.
    const unsigned char function[]={0x83,0xec,0x38,0x56,0x8b,0xf1,0x8b,0x8e,0xd0,0x32,0x00,0x00,
        0xff,0x46,0x30,0x5e,0x83,0xc4,0x38,0xc3};
    std::memcpy(reinterpret_cast<void*>(code),function,sizeof(function));FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(code),sizeof(function));
    image=base;frameProvider=ProvideFrame;Check(MH_Initialize()==MH_OK,"private MinHook initialized");
    Check(MH_CreateHook(reinterpret_cast<void*>(code),reinterpret_cast<void*>(&Hook),reinterpret_cast<void**>(&original))==MH_OK,"private hook and trampoline created");
    Check(MH_EnableHook(reinterpret_cast<void*>(code))==MH_OK,"private hook enabled");
    auto stage=reinterpret_cast<Stage>(code);stage(reinterpret_cast<void*>(object));
    auto s=Snapshot();Check(*reinterpret_cast<std::uint32_t*>(object+0x30)==1,"native trampoline executed once");
    Check(s.calls==1&&s.sampled==1&&s.rejected==0&&s.count==1&&s.written==1&&s.overwritten==0,"one observation for one native call");
    Check(s.events[0].sequence==1&&s.events[0].frameKnown&&s.events[0].beforeValid&&s.events[0].frame.id==100&&s.events[0].frame.eye==0&&s.events[0].frame.thread==GetCurrentThreadId(),"frame and executing thread captured");
    Check(s.events[0].before.lod==2.f&&s.events[0].afterValid&&s.events[0].after.lod==2.f,"before and after state, no settings mutation");
    Check(s.events[0].before.maskTexture==8001&&s.events[0].before.maskSurface==8002&&s.events[0].before.resolveSurface==8003,"mask resource identities from the renderer owning postfilter");
    fakeTick=1050;selectedEye=1;stage(reinterpret_cast<void*>(object));s=Snapshot();
    Check(*reinterpret_cast<std::uint32_t*>(object+0x30)==2&&s.calls==2&&s.sampled==1,"rate limit avoids duplicate but executes native stage");
    fakeTick=1101;stage(reinterpret_cast<void*>(object));s=Snapshot();
    Check(s.sampled==2&&s.count==2&&s.events[1].frame.eye==1,"next eye can be observed after interval");
    fakeTick=1202;frameReady=false;stage(reinterpret_cast<void*>(object));s=Snapshot();
    Check(s.frameUnavailable==1&&s.sampled==3&&s.written==3&&s.count==3&&s.events[2].beforeValid&&!s.events[2].frameKnown&&s.events[2].frame.id==0&&s.events[2].frame.eye==2&&*reinterpret_cast<std::uint32_t*>(object+0x30)==4,"unavailable eye retained as unknown and native still runs");
    fakeTick=1303;frameReady=true;Put(base+0x7c1b0c,std::uint32_t(object+4));stage(reinterpret_cast<void*>(object));s=Snapshot();
    Check(s.rejected==1&&s.sampled==3&&s.written==4&&!s.events[3].beforeValid&&*reinterpret_cast<std::uint32_t*>(object+0x30)==5,"foreign owner recorded as invalid, native still runs");
    Put(base+0x7c1b0c,std::uint32_t(object));frameReady=false;
    for(unsigned i=0;i<300;++i){fakeTick+=101;stage(reinterpret_cast<void*>(object));}
    s=Snapshot();Check(s.written==304&&s.count==Capacity&&s.overwritten==48&&s.events[0].sequence==49&&s.events[Capacity-1].sequence==304,"bounded ordered ring exposes overwrites and sequence gaps");
    Check(MH_DisableHook(reinterpret_cast<void*>(code))==MH_OK&&MH_RemoveHook(reinterpret_cast<void*>(code))==MH_OK,"private hook removed");
    MH_Uninitialize();VirtualFree(reinterpret_cast<void*>(code),0,MEM_RELEASE);VirtualFree(reinterpret_cast<void*>(aux),0,MEM_RELEASE);VirtualFree(reinterpret_cast<void*>(renderer),0,MEM_RELEASE);
    VirtualFree(reinterpret_cast<void*>(object),0,MEM_RELEASE);VirtualFree(reinterpret_cast<void*>(base),0,MEM_RELEASE);
    std::printf("Postfilter actual private MinHook/trampoline: %u checks PASS; no game or GPU.\n",checks);
}
