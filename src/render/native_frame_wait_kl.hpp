#pragma once
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>

// Diagnostics only. The pinned game's own import slots are redirected; no
// global API detour, timeout change, skipped wait, or render-thread I/O.
namespace NativeFrameWaitKL {
enum Api : unsigned {SleepApi,WaitSingleApi,WaitMultipleApi,ApiCount};
enum Site : unsigned {ProducerCamera,WorkerDispatch,RenderJoin,WorkerStart,SystemTimerSleep,TaskPollSleep,OtherSleep,OtherWait,SiteCount};
inline constexpr const char* SiteNames[SiteCount]={"producer_camera_wait","worker_dispatch_wait","render_join","worker_start",
    "system_timer_sleep","task_poll_sleep","other_native_sleep","other_native_wait"};
inline constexpr std::uintptr_t Slots[ApiCount]={0x5320A0,0x5321C8,0x5320F0};
struct Event {
    std::uint64_t sequence=0,startUs=0,endUs=0,wallUs=0;
    std::uint32_t tick=0,thread=0,callerRva=0,api=0,site=0,timeout=0,objectCount=0,waitAll=0,result=0,lastError=0;
    std::uint32_t handle0=0,handle1=0;
    std::uint64_t drawStartUs=0;
    std::uint32_t drawStartThread=0;
};
struct SiteStats {std::uint64_t calls=0,totalUs=0,maxUs=0;};
struct RenderSignal {
    std::uint64_t calls=0,failures=0,startUs=0,endUs=0;
    std::uint32_t thread=0,handle=0,result=0,lastError=0;
};
inline bool MatchesDrawStart(const Event& wait,const RenderSignal& signal) {
    return wait.site==WorkerDispatch&&wait.objectCount==2&&!wait.waitAll&&
        wait.result==WAIT_OBJECT_0&&wait.handle0&&wait.handle0==signal.handle&&signal.result&&signal.thread&&
        signal.startUs>=wait.startUs&&signal.startUs<=wait.endUs;
}
inline constexpr const char* SignalNames[]{"DrawStart","DrawUpdate","DrawEnd"};
inline constexpr unsigned SignalCount=3;
inline constexpr unsigned ActiveWaitCapacity=16;
struct ActiveWait {Event event{};std::uint64_t ageUs=0;};
struct Stats {bool installed=false,enabled=false,verified=false;std::uint64_t events=0,droppedEvents=0,filteredFastEvents=0,activeUntracked=0;
    SiteStats sites[SiteCount]{};};
using LogCallback=void(*)(const char*);
bool Install(LogCallback log=nullptr);
// Same verified installer, exposed for private mapped-image diagnostics tests.
bool InstallAt(std::uintptr_t image,LogCallback log=nullptr);
void SetEnabled(bool enabled);
Stats Snapshot();
std::size_t ReadEvents(std::uint64_t after,Event* output,std::size_t capacity,std::uint64_t& overwritten);
// Best-effort snapshot of waits that have begun but not completed. No thread
// suspension or stack walk; only the game's verified native import wrappers.
std::size_t ReadActiveWaits(ActiveWait* output,std::size_t capacity);
bool ReadSignals(RenderSignal (&output)[SignalCount]);
// Restores only slots still owned by this module; originals/state remain
// valid for an already executing wrapper. This never authorizes FreeLibrary.
bool Shutdown();

inline Site Classify(Api api,std::uint32_t callerRva){
    if(api==WaitSingleApi){if(callerRva==0xB7320)return ProducerCamera;
        if(callerRva==0xB47B4)return RenderJoin;if(callerRva==0xB7108)return WorkerStart;}
    if(api==WaitMultipleApi&&callerRva==0xB52D9)return WorkerDispatch;
    if(api==SleepApi){if(callerRva==0x4D2DE)return SystemTimerSleep;if(callerRva==0x351E76)return TaskPollSleep;return OtherSleep;}
    return OtherWait;
}
template<class Reader> bool VerifyImage(std::uintptr_t image,Reader read,std::uint32_t* imageBytes=nullptr){
    const auto get=[&](std::uintptr_t at,auto& value){return read(at,&value,sizeof(value));};
    std::uint16_t mz=0,machine=0,magic=0;std::uint32_t nt=0,signature=0,size=0,imports=0,importBytes=0;
    if(image<0x10000||image>0xffffffffu-0x800000u||!get(image,mz)||mz!=0x5A4D||!get(image+0x3C,nt)||nt>0x10000||
       !get(image+nt,signature)||signature!=0x4550||!get(image+nt+4,machine)||machine!=0x14C||
       !get(image+nt+24,magic)||magic!=0x10B||!get(image+nt+0x50,size)||size<0x687610||size>0x1000000||image>0xffffffffu-size||
       !get(image+nt+0x80,imports)||!get(image+nt+0x84,importBytes)||!imports||importBytes<40)return false;
    const auto bounds=[&](std::uint32_t rva,std::size_t count){return rva<size&&count<=size-rva;};
    if(!bounds(imports,importBytes))return false;
    const auto name=[&](std::uint32_t rva,const char* expected,bool insensitive){
        const auto count=std::strlen(expected)+1;if(!bounds(rva,count)||count>64)return false;char actual[64]{};
        if(!read(image+rva,actual,count))return false;
        for(std::size_t i=0;i<count;++i){char a=actual[i],b=expected[i];
            if(insensitive){if(a>='A'&&a<='Z')a+=32;if(b>='A'&&b<='Z')b+=32;}
            if(a!=b)return false;}return true;
    };
    bool found[ApiCount]{};bool terminated=false;
    for(unsigned d=0;d<64;++d){struct Descriptor {std::uint32_t lookup,time,forward,name,iat;} desc{};
        const auto offset=d*sizeof(desc);const auto at=std::uint64_t(imports)+offset;
        if(offset>importBytes||sizeof(desc)>importBytes-offset||at>0xffffffffu||!bounds(static_cast<std::uint32_t>(at),sizeof(desc))||
           !read(image+at,&desc,sizeof(desc)))return false;
        if(!desc.lookup&&!desc.time&&!desc.forward&&!desc.name&&!desc.iat){terminated=true;break;}
        if(!name(desc.name,"KERNEL32.dll",true))continue;
        if(!desc.lookup||!desc.iat)return false;
        bool thunkTerminated=false;
        for(unsigned i=0;i<2048;++i){std::uint32_t thunk=0;const auto lookup=std::uint64_t(desc.lookup)+4ull*i;
            if(lookup>0xffffffffu||!bounds(static_cast<std::uint32_t>(lookup),4)||!get(image+lookup,thunk))return false;
            if(!thunk){thunkTerminated=true;break;}if(thunk&0x80000000u)continue;
            for(unsigned api=0;api<ApiCount;++api){const char* names[ApiCount]={"Sleep","WaitForSingleObject","WaitForMultipleObjects"};
                if(thunk<=0xfffffffdu&&name(thunk+2,names[api],false)){
                    if(found[api]||std::uint64_t(desc.iat)+4ull*i!=Slots[api]||!bounds(Slots[api],4))return false;found[api]=true;}}
        }
        if(!thunkTerminated)return false;
    }
    if(!terminated||!found[0]||!found[1]||!found[2])return false;
    struct Call {std::uint32_t rva;Api api;};
    for(const auto call:{Call{0xB731A,WaitSingleApi},Call{0xB52D3,WaitMultipleApi},Call{0xB47AE,WaitSingleApi},
        Call{0xB7102,WaitSingleApi},Call{0x4D2D8,SleepApi},Call{0x351E70,SleepApi}}){
        unsigned char bytes[6]{};const auto slot=static_cast<std::uint32_t>(image+Slots[call.api]);
        if(!bounds(call.rva,sizeof(bytes))||!read(image+call.rva,bytes,sizeof(bytes))||bytes[0]!=0xFF||bytes[1]!=0x15||
           std::memcmp(bytes+2,&slot,4))return false;
    }
    if(imageBytes)*imageBytes=size;return true;
}
}
