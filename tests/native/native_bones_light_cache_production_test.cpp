// Actual native semantic68 light consumer + original matrix math in an
// isolated CPU arena. Only the renderer upload callback is substituted.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "native_bones_light_cache.hpp"
#include "native_hud_test_fixture.hpp"
#include "profile.hpp"
#include <cstdio>
#include <cstdlib>
namespace {
using namespace NativeCameraMath;namespace N=NativeBonesLightCache;
unsigned checks=0,uploads=0,writes=0;std::array<float,12> uploaded{};std::uintptr_t base=0;
void Check(bool value,const char* why){++checks;if(!value){std::printf("FAIL: %s\n",why);std::exit(1);}}
template<class T>void Put(std::uintptr_t at,const T& value){std::memcpy(reinterpret_cast<void*>(at),&value,sizeof(value));}
bool Read(std::uintptr_t at,void* out,std::size_t n){SIZE_T got=0;return N::Address(at,n)&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),out,n,&got)&&got==n;}
bool Write(std::uintptr_t at,const void* data,std::size_t n){if(at!=base+N::CacheRva||n!=4)return false;++writes;std::memcpy(reinterpret_cast<void*>(at),data,n);return true;}
void __thiscall Upload(void*,const float* data,unsigned count){Check(count==12,"actual native light consumer uploads exactly three float4 rows");std::memcpy(uploaded.data(),data,48);++uploads;}
bool Near(Vec3 a,Vec3 b){const auto d=Subtract(a,b);return Dot(d,d)<1e-6f;}
Vec3 Direction(){return {uploaded[4],uploaded[5],uploaded[6]};}
}
int main(int argc,char** argv){
    static_assert(sizeof(void*)==4,"native light consumer ABI is x86");if(argc<2)return 2;
    const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,argv[1],-1,nullptr,0);Check(n>1,"EXE proof path UTF8");
    std::vector<wchar_t> path(n);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,argv[1],-1,path.data(),n);char hash[65]{};
    Check(HashFile(path.data(),hash)&&IsSupportedExecutableHash(hash),"whole EXE hash pinned before copied native CPU execution");
    NativeHudFixture::Image source;Check(source.Load(argv[1]),"supported original PE opened read-only");Check(N::Verify(0x400000,source.Reader()),"actual low BonesLights cache consumer byte proof");
    auto* memory=VirtualAlloc(nullptr,0x900000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);Check(memory!=nullptr,"private CPU native arena");base=reinterpret_cast<std::uintptr_t>(memory);
    for(const auto span:{std::pair<std::uintptr_t,unsigned>{N::ConsumerRva,0x7B},{0x8F8DD,0x17},{0x91865,0xD},{0x2E700,0x73},{0x2E6C0,0x3B}})
        Check(source.Read(0x400000+span.first,reinterpret_cast<void*>(base+span.first),span.second),"copy bounded original consumer/tail or exact native transform");
    unsigned rebased=0;for(unsigned i=0;i+4<=0x7B;++i){std::uint32_t value=0;std::memcpy(&value,reinterpret_cast<void*>(base+N::ConsumerRva+i),4);
        if(value==0xB887A8){Put(base+N::ConsumerRva+i,static_cast<std::uint32_t>(base+N::CacheRva));++rebased;}}
    Check(rebased==3&&N::Verify(base,Read),"only three cache absolute operands rebased; native math/calls/skip remain original");
    // Recreate the native dispatch prologue ABI; both original upload and
    // cache-hit epilogues restore these four registers and ret4 themselves.
    const auto entry=base+0x1000;
    unsigned char prologue[]={0x81,0xEC,0xF8,8,0,0,0x53,0x55,0x56,0x57,0x89,0xCB,0x8B,0xAC,0x24,0x0C,9,0,0,0xE9,0,0,0,0};
    const auto relative=static_cast<std::int32_t>(base+N::ConsumerRva-(entry+sizeof(prologue)));std::memcpy(prologue+20,&relative,4);std::memcpy(reinterpret_cast<void*>(entry),prologue,sizeof(prologue));
    FlushInstructionCache(GetCurrentProcess(),memory,0x900000);
    const auto state=base+0x10000,renderer=base+0x20000,vtable=base+0x21000,lights=base+0x30000;
    Put(renderer,static_cast<std::uint32_t>(vtable));Put(vtable+0x14,reinterpret_cast<std::uintptr_t>(Upload));
    const std::array<float,4> ambient{{.05f,.05f,.05f,1}},direction{{1,0,0,0}},color{{.6f,.25f,.8f,1}};
    Put(lights,direction);Put(lights+0x10,color);Put(lights+0x60,ambient);Put(state+0x3C0,static_cast<std::uint32_t>(lights));Put(state+0x3C4,std::uint32_t(1));
    CameraPose original;original.basis={{0,0,1},{0,1,0},{1,0,0}};Put(state+0x8C,original);
    using Consume=void(__thiscall*)(void*,void*);const auto consume=reinterpret_cast<Consume>(entry);
    consume(reinterpret_cast<void*>(renderer),reinterpret_cast<void*>(state));Check(uploads==1&&Near(Direction(),{1,0,0}),"first actual native consumer uploads current object-space light direction");
    auto tracked=original;tracked.basis={{1,0,0},{0,1,0},{0,0,-1}};Put(state+0x8C,tracked);
    consume(reinterpret_cast<void*>(renderer),reinterpret_cast<void*>(state));Check(uploads==1&&Near(Direction(),{1,0,0}),"same table/index with changed tracked WORLD reproduces stale native light constants");
    const auto stale=uploaded;
    Check(N::Invalidate(base,state,original,tracked,Read,Write,true)==N::Result::Invalidated&&writes==1,"owned current WORLD echo invalidates only exact matching native low light cache word");
    consume(reinterpret_cast<void*>(renderer),reinterpret_cast<void*>(state));Check(uploads==2&&Near(Direction(),{0,0,1}),"actual original consumer now recomputes light in tracked WORLD frame");
    Check(uploaded[0]==stale[0]&&uploaded[1]==stale[1]&&uploaded[8]==stale[8]&&uploaded[9]==stale[9]&&uploaded[10]==stale[10],"ambient and light colors are untouched; only native object-space direction follows tracked pose");
    const auto beforeWrites=writes;
    Check(N::Invalidate(base,state,tracked,tracked,Read,Write,true)==N::Result::Unchanged&&writes==beforeWrites,"identical WORLD needs no cache mutation");
    Check(N::Invalidate(base,state,tracked,original,Read,Write,true)==N::Result::Rejected&&writes==beforeWrites,"foreign/stale WORLD echo cannot invalidate another draw");
    Check(N::Invalidate(base,state,original,tracked,Read,Write,false)==N::Result::Rejected,"missing image proof cannot mutate cache");
    Put(state+0x3C4,std::uint32_t(0));Check(N::Invalidate(base,state,original,tracked,Read,Write,true)==N::Result::Unchanged,"native index0 reset remains untouched");
    Put(state+0x3C4,std::uint32_t(1));Put(base+N::CacheRva,std::uint32_t(lights+0x100));
    Check(N::Invalidate(base,state,original,tracked,Read,Write,true)==N::Result::Unchanged&&writes==beforeWrites,"another cached light table is never clobbered");
    Put(base+N::CacheRva,std::uint32_t(lights));unsigned worldReads=0;
    const auto race=[&](std::uintptr_t at,void* output,std::size_t size){if(at==state+0x8C&&++worldReads==2)Put(state+0x8C,original);return Read(at,output,size);};
    Check(N::Invalidate(base,state,original,tracked,race,Write,true)==N::Result::Rejected&&writes==beforeWrites,"late WORLD replacement before invalidation fails closed");Put(state+0x8C,tracked);
    Put(state+0x3C4,std::uint32_t(1024));Check(N::Invalidate(base,state,original,tracked,Read,Write,true)==N::Result::Rejected,"unbounded stale instance index rejected");
    VirtualFree(memory,0,MEM_RELEASE);std::printf("Actual native low BonesLights cache: %u checks PASS.\n",checks);return 0;
}
