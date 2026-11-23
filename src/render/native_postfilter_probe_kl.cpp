#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "MinHook.h"
#include "profile.hpp"
#include "native_postfilter_probe_kl.hpp"
#include <atomic>
#include <cstring>

namespace NativePostfilterProbeKL {
namespace {
static_assert(sizeof(void*)==4,"game postfilter probe requires x86");
using Stage=void(__thiscall*)(void*);
Stage original=nullptr;
FrameProvider frameProvider=nullptr;
std::uintptr_t image=0;
std::atomic<bool> installed{false};
std::atomic<std::uint64_t> calls{0},sampled{0},rejected{0},frameUnavailable{0};
std::atomic<DWORD> lastTick{0};
SRWLOCK lock=SRWLOCK_INIT;
Event ring[Capacity]{};std::uint64_t written=0;
bool Read(std::uintptr_t address,void* out,std::size_t size){
    SIZE_T done=0;return address>=0x10000u&&size&&address<=0xffffffffu-size&&
        ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),out,size,&done)&&done==size;
}
void __fastcall Hook(void* object,void*){
    ++calls;
    Frame frame;const auto now=GetTickCount();auto previous=lastTick.load(std::memory_order_relaxed);
    const bool shouldSample=!previous||now-previous>=100;
    const bool due=shouldSample&&lastTick.compare_exchange_strong(previous,now,std::memory_order_relaxed);
    Event event;
    if(due){
        event.frameKnown=frameProvider&&frameProvider(frame)&&frame.eye<2;
        if(!event.frameKnown){frame={};++frameUnavailable;}
        event.frame=frame;event.frame.tick=now;event.frame.thread=GetCurrentThreadId();
        event.beforeValid=Capture(image,reinterpret_cast<std::uintptr_t>(object),Read,event.before);
    }
    original(object);
    if(!due)return;
    event.afterValid=Capture(image,reinterpret_cast<std::uintptr_t>(object),Read,event.after);
    AcquireSRWLockExclusive(&lock);event.sequence=++written;ring[(written-1)%Capacity]=event;ReleaseSRWLockExclusive(&lock);
    if(event.beforeValid)++sampled;else ++rejected;
}
}
bool Install(FrameProvider provider,LogCallback log){
    if(installed.load())return true;
    if(!provider)return false;
    wchar_t path[MAX_PATH]{};char digest[65]{};const auto length=GetModuleFileNameW(nullptr,path,MAX_PATH);
    if(!length||length>=MAX_PATH||!HashFile(path,digest)||!IsSupportedExecutableHash(digest)){
        if(log)log("Opt-in postfilter probe: unsupported EXE.");return false;
    }
    image=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if(!VerifyStage(image,Read)){
        if(log)log("Opt-in postfilter probe: stage/caller signature differs.");return false;
    }
    frameProvider=provider;void* target=reinterpret_cast<void*>(image+0xb00b0);
    if(MH_CreateHook(target,reinterpret_cast<void*>(&Hook),reinterpret_cast<void**>(&original))!=MH_OK){frameProvider=nullptr;return false;}
    if(MH_EnableHook(target)!=MH_OK){MH_RemoveHook(target);frameProvider=nullptr;return false;}
    installed=true;if(log)log("Opt-in postfilter high-stage state probe enabled; no render state modified.");return true;
}
Stats Snapshot(){
    Stats out;out.installed=installed.load();out.calls=calls.load();out.sampled=sampled.load();out.rejected=rejected.load();out.frameUnavailable=frameUnavailable.load();
    AcquireSRWLockShared(&lock);out.written=written;out.count=written<Capacity?static_cast<unsigned>(written):Capacity;out.overwritten=written-out.count;
    const auto first=written-out.count;for(unsigned i=0;i<out.count;++i)out.events[i]=ring[(first+i)%Capacity];
    ReleaseSRWLockShared(&lock);return out;
}
}
