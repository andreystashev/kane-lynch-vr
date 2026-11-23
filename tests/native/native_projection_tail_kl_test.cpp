#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "MinHook.h"
#include "native_projection_tail_kl.hpp"
#include "native_hud_test_fixture.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace {
unsigned checks=0,calls=0;std::uintptr_t expectedState=0;bool change=true;
void Check(bool ok,const char* message){++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
void Observe(std::uintptr_t state){
    Check(state==expectedState,"actual post-REP EDI reconstructs exact native renderer state");++calls;
    if(change){const float value=7.25f;std::memcpy(reinterpret_cast<void*>(state+0x3F8),&value,4);}
    // A real C++ observer is allowed to clobber all these ABI-volatile inputs.
    __asm__ __volatile__("fninit\n\tpxor %xmm0,%xmm0\n\tclc\n\t");
}
}
int main(int argc,char** argv){
    static_assert(sizeof(void*)==4,"exact decoder tail is x86");
    const unsigned char tail[]={0x8B,0x7C,0x24,0x10,0xDD,0xD8};
    const unsigned char count[]={0xB9,0x10,0,0,0};
    if(argc>1){NativeHudFixture::Image image;Check(image.Load(argv[1]),"read pinned EXE without game launch");
        unsigned char bytes[sizeof(tail)]{};Check(image.Read(0x4B54D7,bytes,sizeof(bytes))&&!std::memcmp(bytes,tail,sizeof(tail)),"exact native MOV/FSTP decoder tail is unchanged");}
    if(argc>1){NativeHudFixture::Image image;Check(image.Load(argv[1]),"read pinned decoder branch image");
        unsigned char bytes[5]{};Check(image.Read(0x4B54D0,bytes,sizeof(bytes))&&!std::memcmp(bytes,count,sizeof(bytes)),"safe hook owns exactly MOV ECX,16");
        unsigned char branch[6]{};Check(image.Read(0x4B5398,branch,sizeof(branch))&&branch[0]==0x0F&&branch[1]==0x87,"native unknown packet has external default branch");
        std::int32_t delta=0;std::memcpy(&delta,branch+2,4);Check(0x4B539E + delta==0x4B54DB,"default branch enters shared FSTP inside rejected old patch");
        unsigned char selectors[61]{};Check(image.Read(0x4B62D8,selectors,sizeof(selectors)),"read all native packet selectors");
        bool defaultEntry=false;
        for(auto selector:selectors){std::uint32_t target=0;
            Check(image.Read(0x4B622C+4u*selector,&target,sizeof(target))&&!(target>=0x4B54D0&&target<0x4B54D5),"native switch target never enters replaced copy-count instruction");
            defaultEntry|=target==0x4B54DB;}
        Check(defaultEntry,"ordinary in-range packets also enter shared FSTP");}
    const unsigned char code[]={
        0x53,0x56,0x57,0x8B,0x7C,0x24,0x10,0x8B,0x74,0x24,0x14,
        0x8B,0x44,0x24,0x18,0xD9,0xE8,0x85,0xC0,0x75,0x12,
        0x81,0xC7,0xF8,0x03,0,0,0xF9,0xB9,0x10,0,0,0,0xF3,0xA5,
        0x8B,0x7C,0x24,0x10,0xDD,0xD8,
        0x9C,0x58,0x89,0x87,0x80,0x04,0,0,0x5F,0x5E,0x5B,0xC3};
    auto* executable=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    Check(executable!=nullptr,"allocate private decoder fixture");std::memcpy(executable,code,sizeof(code));
    FlushInstructionCache(GetCurrentProcess(),executable,sizeof(code));
    using Decode=void(__cdecl*)(void*,const float*,unsigned);const auto decode=reinterpret_cast<Decode>(executable);
    alignas(16)std::array<unsigned char,0x500> state{};std::array<float,16> projection{};projection[0]=3.5f;
    expectedState=reinterpret_cast<std::uintptr_t>(state.data());
    decode(state.data(),projection.data(),0);float copied=0;std::memcpy(&copied,state.data()+0x3F8,4);
    Check(copied==3.5f,"unhooked copied native projection is baseline");
    Check(MH_Initialize()==MH_OK,"private MinHook initialised");
    Check(!std::memcmp(executable+35,tail,sizeof(tail))&&!std::memcmp(executable+28,count,sizeof(count)),"private fixture executes exact copy/count and shared tail bytes");
    NativeProjectionTailKL::observer=Observe;
    const bool legacy=argc>2&&!std::strcmp(argv[2],"--legacy");
    auto* hookTarget=executable+(legacy?35:28);
    KL1ProjectionTailOriginal=executable+35;
    Check(MH_CreateHook(hookTarget,reinterpret_cast<void*>(&KL1ProjectionTailHook),nullptr)==MH_OK,"real MinHook owns only projection copy count");
    Check(MH_EnableHook(hookTarget)==MH_OK,"private copy hook enabled");
    Check(!std::memcmp(executable+39,tail+4,2),"external default FSTP entry stays byte-exact after hook enable");
    for(unsigned i=0;i<10;++i){
        const double previous=123.25;double after=0;
        alignas(16)const std::uint32_t sentinel[4]={0x11223344,0x55667788,0x99aabbcc,0xddeeff00};
        alignas(16)std::uint32_t xmm[4]{};
        __asm__ __volatile__("fldl %0\n\tmovdqu %1,%%xmm0"::"m"(previous),"m"(sentinel):"xmm0");
        decode(state.data(),projection.data(),0);
        __asm__ __volatile__("fstpl %0\n\tmovdqu %%xmm0,%1":"=m"(after),"=m"(xmm));
        Check(after==previous,"live x87 value survives callback clobber and original native FSTP");
        Check(!std::memcmp(xmm,sentinel,sizeof(xmm)),"all live XMM bytes survive callback clobber");
        std::uint32_t flags=0;std::memcpy(&flags,state.data()+0x480,4);
        Check((flags&1)!=0,"original carry flag survives callback clobber");
        std::memcpy(&copied,state.data()+0x3F8,4);Check(copied==7.25f,"observer repair occurs before decoder returns to later world binds");
    }
    for(unsigned i=0;i<10;++i){
        const auto before=calls;const double previous=456.5;double after=0;
        __asm__ __volatile__("fldl %0"::"m"(previous));
        decode(state.data(),projection.data(),1);
        __asm__ __volatile__("fstpl %0":"=m"(after));
        Check(after==previous,"unknown packet executes shared original FSTP with intact stack");
        Check(calls==before,"default packet bypasses projection observer");
    }
    change=false;decode(state.data(),projection.data(),0);std::memcpy(&copied,state.data()+0x3F8,4);
    Check(copied==3.5f&&calls==11,"unrelated observation leaves original lens bytes untouched");
    Check(MH_DisableHook(hookTarget)==MH_OK&&MH_RemoveHook(hookTarget)==MH_OK,"private native hook removed cleanly");
    MH_Uninitialize();NativeProjectionTailKL::observer=nullptr;VirtualFree(executable,0,MEM_RELEASE);
    std::printf("Native projection-only tail adapter: %u checks PASS; private CPU decoder only.\n",checks);
}
