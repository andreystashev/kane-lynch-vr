#include "native_frame_wait_kl.hpp"
#include "native_wait_trace_policy.hpp"
#include <atomic>
#include <algorithm>

namespace NativeFrameWaitKL {
namespace {
static_assert(sizeof(void*)==4,"The pinned game's IAT is PE32");
using SleepFunction=void(WINAPI*)(DWORD);
using SingleFunction=DWORD(WINAPI*)(HANDLE,DWORD);
using MultipleFunction=DWORD(WINAPI*)(DWORD,const HANDLE*,BOOL,DWORD);
std::atomic<SleepFunction> originalSleep{nullptr};
std::atomic<SingleFunction> originalSingle{nullptr};
std::atomic<MultipleFunction> originalMultiple{nullptr};
std::atomic<decltype(&SetEvent)> originalSignal{nullptr};
std::atomic<bool> installed{false},enabled{false},verified{false};
std::atomic<std::uintptr_t> imageBase{0};
std::atomic<std::uint32_t> imageSize{0};
std::atomic<std::uint64_t> frequency{1};
SRWLOCK installationLock=SRWLOCK_INIT,ringLock=SRWLOCK_INIT;
SRWLOCK signalLock=SRWLOCK_INIT;
RenderSignal signals[SignalCount];
std::atomic<bool> signalsInstalled{false};
constexpr std::uintptr_t SignalSlot=0x5320EC;
NativeWaitTracePolicy::Ring ring;
std::atomic<std::uint64_t> dropped{0},filteredFast{0},activeUntracked{0};
struct Counters {std::atomic<std::uint64_t> calls{0},totalUs{0},maxUs{0};} counters[SiteCount];
struct ActiveSlot {
    std::atomic<std::uint32_t> owner{0},generation{0},caller{0},api{0},site{0},timeout{0},count{0},all{0};
    std::atomic<std::uint32_t> handle0{0},handle1{0};
    std::atomic<std::uint64_t> started{0};
};
ActiveSlot active[ActiveWaitCapacity];
struct Observation {Event event;ActiveSlot* active=nullptr;};

std::uint64_t NowUs(){
    LARGE_INTEGER value{};QueryPerformanceCounter(&value);
    const auto ticks=static_cast<std::uint64_t>(value.QuadPart);
    const auto hz=frequency.load(std::memory_order_relaxed);
    return (ticks/hz)*1000000+(ticks%hz)*1000000/hz;
}
std::uint32_t CallerRva(void* caller){
    const auto at=reinterpret_cast<std::uintptr_t>(caller),base=imageBase.load(std::memory_order_relaxed);
    return at>=base&&at-base<imageSize.load(std::memory_order_relaxed)?static_cast<std::uint32_t>(at-base):0;
}
void Complete(Observation observation,DWORD error){
    auto event=observation.event;
    event.endUs=NowUs();event.wallUs=event.endUs>=event.startUs?event.endUs-event.startUs:0;
    if(auto* slot=observation.active){
        slot->generation.fetch_add(1,std::memory_order_acq_rel);
        slot->started.store(0,std::memory_order_relaxed);
        slot->generation.fetch_add(1,std::memory_order_release);
        slot->owner.store(0,std::memory_order_release);
    }
    event.tick=GetTickCount();event.lastError=error;
    if(event.site==WorkerDispatch&&event.wallUs>=20000&&TryAcquireSRWLockShared(&signalLock)){
        const auto signal=signals[0];ReleaseSRWLockShared(&signalLock);
        if(MatchesDrawStart(event,signal)){
            event.drawStartUs=signal.startUs;event.drawStartThread=signal.thread;
        }
    }
    auto& site=counters[event.site];site.calls.fetch_add(1,std::memory_order_relaxed);
    site.totalUs.fetch_add(event.wallUs,std::memory_order_relaxed);
    auto maximum=site.maxUs.load(std::memory_order_relaxed);
    while(maximum<event.wallUs&&!site.maxUs.compare_exchange_weak(maximum,event.wallUs,std::memory_order_relaxed)){}
    // Suppress detail for quick unknown polling while preserving complete
    // cumulative API counts/time above. Never queue a native thread behind
    // the diagnostics reader or another completing native wait.
    if(NativeWaitTracePolicy::Select(event)==NativeWaitTracePolicy::Filtered)
        filteredFast.fetch_add(1,std::memory_order_relaxed);
    else if(TryAcquireSRWLockExclusive(&ringLock)){
        ring.Push(event);
        ReleaseSRWLockExclusive(&ringLock);
    }else dropped.fetch_add(1,std::memory_order_relaxed);
    SetLastError(error);
}
Observation Begin(Api api,void* caller,DWORD timeout,DWORD count=0,BOOL all=FALSE,HANDLE object=nullptr,const HANDLE* objects=nullptr){
    Observation observation{};auto& event=observation.event;
    event.startUs=NowUs();event.thread=GetCurrentThreadId();event.callerRva=CallerRva(caller);
    event.api=api;event.site=Classify(api,event.callerRva);event.timeout=timeout;event.objectCount=count;event.waitAll=all;
    if(event.site<=WorkerStart&&object)event.handle0=reinterpret_cast<std::uintptr_t>(object);
    // The verified render worker passes its own two-element stack array.
    if(event.site==WorkerDispatch&&count==2&&objects){
        event.handle0=reinterpret_cast<std::uintptr_t>(objects[0]);
        event.handle1=reinterpret_cast<std::uintptr_t>(objects[1]);
    }
    // A claimed slot is released at completion, so transient worker threads
    // cannot exhaust the table over the lifetime of a game session.
    const unsigned preferred=event.thread%ActiveWaitCapacity;
    for(unsigned n=0;n<ActiveWaitCapacity;++n){
        const auto index=(preferred+n)%ActiveWaitCapacity;auto& slot=active[index];std::uint32_t empty=0;
        if(!slot.owner.compare_exchange_strong(empty,event.thread,std::memory_order_acq_rel))continue;
        slot.generation.fetch_add(1,std::memory_order_acq_rel);
        slot.caller=event.callerRva;slot.api=event.api;slot.site=event.site;slot.timeout=timeout;slot.count=count;slot.all=all;
        slot.handle0=event.handle0;slot.handle1=event.handle1;
        slot.started.store(event.startUs,std::memory_order_relaxed);
        slot.generation.fetch_add(1,std::memory_order_release);observation.active=&slot;break;
    }
    if(!observation.active)activeUntracked.fetch_add(1,std::memory_order_relaxed);
    return observation;
}
__declspec(noinline) void WINAPI HookSleep(DWORD timeout){
    const auto original=originalSleep.load(std::memory_order_acquire);
    if(!enabled.load(std::memory_order_relaxed)){original(timeout);return;}
    const DWORD before=GetLastError();auto event=Begin(SleepApi,__builtin_return_address(0),timeout);
    SetLastError(before);original(timeout);const DWORD after=GetLastError();Complete(event,after);
}
__declspec(noinline) DWORD WINAPI HookSingle(HANDLE object,DWORD timeout){
    const auto original=originalSingle.load(std::memory_order_acquire);
    if(!enabled.load(std::memory_order_relaxed))return original(object,timeout);
    const DWORD before=GetLastError();auto event=Begin(WaitSingleApi,__builtin_return_address(0),timeout,1,FALSE,object);
    SetLastError(before);event.event.result=original(object,timeout);const DWORD after=GetLastError();Complete(event,after);return event.event.result;
}
__declspec(noinline) DWORD WINAPI HookMultiple(DWORD count,const HANDLE* objects,BOOL all,DWORD timeout){
    const auto original=originalMultiple.load(std::memory_order_acquire);
    if(!enabled.load(std::memory_order_relaxed))return original(count,objects,all,timeout);
    const DWORD before=GetLastError();auto event=Begin(WaitMultipleApi,__builtin_return_address(0),timeout,count,all,nullptr,objects);
    SetLastError(before);event.event.result=original(count,objects,all,timeout);const DWORD after=GetLastError();Complete(event,after);return event.event.result;
}
__declspec(noinline) BOOL WINAPI HookSignal(HANDLE object){
    const auto original=originalSignal.load(std::memory_order_acquire);
    if(!enabled.load(std::memory_order_relaxed))return original(object);
    unsigned site=SignalCount;
    switch(CallerRva(__builtin_return_address(0))){
        case 0xB74CA:site=0;break;
        case 0xB5317:site=1;break;
        case 0xB6200:site=2;break;
    }
    if(site==SignalCount)return original(object);
    const DWORD before=GetLastError();
    const auto start=NowUs();
    SetLastError(before);
    const BOOL result=original(object);
    const DWORD after=GetLastError();
    const auto end=NowUs();
    if(TryAcquireSRWLockExclusive(&signalLock)){
        auto& signal=signals[site];
        ++signal.calls;if(!result)++signal.failures;
        signal.startUs=start;signal.endUs=end;signal.thread=GetCurrentThreadId();
        signal.handle=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(object));
        signal.result=result;signal.lastError=after;
        ReleaseSRWLockExclusive(&signalLock);
    }
    SetLastError(after);
    return result;
}
std::uintptr_t Hook(unsigned api){
    const std::uintptr_t hooks[]={reinterpret_cast<std::uintptr_t>(&HookSleep),reinterpret_cast<std::uintptr_t>(&HookSingle),reinterpret_cast<std::uintptr_t>(&HookMultiple)};
    return hooks[api];
}
bool Read(std::uintptr_t at,void* output,std::size_t bytes){
    SIZE_T count=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),output,bytes,&count)&&count==bytes;
}
bool Exchange(std::uintptr_t slot,std::uintptr_t desired,std::uintptr_t expected){
    DWORD old=0;if(!VirtualProtect(reinterpret_cast<void*>(slot),4,PAGE_READWRITE,&old))return false;
    const auto previous=InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(slot),static_cast<LONG>(desired),static_cast<LONG>(expected));
    DWORD ignored=0;const bool restored=VirtualProtect(reinterpret_cast<void*>(slot),4,old,&ignored)!=FALSE;
    return static_cast<std::uintptr_t>(static_cast<DWORD>(previous))==expected&&restored;
}
void Log(LogCallback log,const char* message){if(log)log(message);}
void InstallSignals(std::uintptr_t image) {
    const auto import=static_cast<std::uint32_t>(image+SignalSlot);
    const auto directCall=[&](std::uintptr_t rva){
        unsigned char bytes[6]{};
        return Read(image+rva,bytes,sizeof(bytes))&&bytes[0]==0xff&&bytes[1]==0x15&&
            !std::memcmp(bytes+2,&import,4);
    };
    const auto loadsSetEvent=[&](std::uintptr_t rva){
        unsigned char bytes[6]{};
        return Read(image+rva,bytes,sizeof(bytes))&&bytes[0]==0x8b&&bytes[1]==0x3d&&
            !std::memcmp(bytes+2,&import,4);
    };
    unsigned char call[2]{};
    if(!directCall(0xB74C4)||!directCall(0xB61FA)||!loadsSetEvent(0xB5289)||!loadsSetEvent(0xB6200)||
       !Read(image+0xB5315,call,sizeof(call))||call[0]!=0xff||call[1]!=0xd7)return;
    auto* kernel=GetModuleHandleW(L"kernel32.dll");
    const auto original=reinterpret_cast<decltype(&SetEvent)>(GetProcAddress(kernel,"SetEvent"));
    std::uint32_t current=0;
    if(!original||!Read(image+SignalSlot,&current,4)||current!=reinterpret_cast<std::uintptr_t>(original))return;
    originalSignal.store(original,std::memory_order_release);
    signalsInstalled=Exchange(image+SignalSlot,reinterpret_cast<std::uintptr_t>(&HookSignal),current);
}
}

bool Install(LogCallback log){return InstallAt(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)),log);}
bool InstallAt(std::uintptr_t image,LogCallback log){
    AcquireSRWLockExclusive(&installationLock);
    if(installed.load(std::memory_order_acquire)){
        const bool same=imageBase.load(std::memory_order_relaxed)==image;
        ReleaseSRWLockExclusive(&installationLock);return same;
    }
    std::uint32_t bytes=0;
    if(!VerifyImage(image,Read,&bytes)){
        ReleaseSRWLockExclusive(&installationLock);Log(log,"NativeFrameWaitKL: pinned PE import/call proof rejected");return false;
    }
    const char* names[ApiCount]={"Sleep","WaitForSingleObject","WaitForMultipleObjects"};
    std::uintptr_t originals[ApiCount]{};bool valid=true;
    for(unsigned api=0;api<ApiCount;++api){
        originals[api]=reinterpret_cast<std::uintptr_t>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),names[api]));
        std::uint32_t current=0;
        valid=valid&&originals[api]&&Read(image+Slots[api],&current,4)&&current==originals[api];
    }
    if(!valid){ReleaseSRWLockExclusive(&installationLock);Log(log,"NativeFrameWaitKL: unknown or already hooked IAT target rejected");return false;}
    LARGE_INTEGER qpc{};
    if(!QueryPerformanceFrequency(&qpc)||qpc.QuadPart<=0){ReleaseSRWLockExclusive(&installationLock);return false;}
    frequency.store(static_cast<std::uint64_t>(qpc.QuadPart),std::memory_order_relaxed);
    originalSleep.store(reinterpret_cast<SleepFunction>(originals[SleepApi]),std::memory_order_release);
    originalSingle.store(reinterpret_cast<SingleFunction>(originals[WaitSingleApi]),std::memory_order_release);
    originalMultiple.store(reinterpret_cast<MultipleFunction>(originals[WaitMultipleApi]),std::memory_order_release);
    imageBase.store(image,std::memory_order_relaxed);imageSize.store(bytes,std::memory_order_relaxed);
    bool patched=true;
    for(unsigned api=0;api<ApiCount&&patched;++api)patched=Exchange(image+Slots[api],Hook(api),originals[api]);
    if(!patched){
        // A concurrent third-party writer is never overwritten during rollback.
        for(unsigned api=0;api<ApiCount;++api){std::uint32_t current=0;
            if(Read(image+Slots[api],&current,4)&&current==Hook(api))Exchange(image+Slots[api],originals[api],Hook(api));}
        ReleaseSRWLockExclusive(&installationLock);Log(log,"NativeFrameWaitKL: transactional IAT install failed; owned slots restored");return false;
    }
    verified.store(true,std::memory_order_relaxed);installed.store(true,std::memory_order_release);enabled.store(true,std::memory_order_release);
    InstallSignals(image);
    ReleaseSRWLockExclusive(&installationLock);Log(log,"NativeFrameWaitKL: verified native IAT wait diagnostics installed");return true;
}
void SetEnabled(bool value){
    AcquireSRWLockExclusive(&installationLock);
    enabled.store(value&&installed.load(std::memory_order_acquire),std::memory_order_release);
    ReleaseSRWLockExclusive(&installationLock);
}
Stats Snapshot(){
    Stats stats{};stats.installed=installed.load(std::memory_order_acquire);stats.enabled=enabled.load(std::memory_order_relaxed);
    stats.verified=verified.load(std::memory_order_relaxed);stats.droppedEvents=dropped.load(std::memory_order_relaxed);
    stats.filteredFastEvents=filteredFast.load(std::memory_order_relaxed);stats.activeUntracked=activeUntracked.load(std::memory_order_relaxed);
    AcquireSRWLockShared(&ringLock);stats.events=ring.Total();ReleaseSRWLockShared(&ringLock);
    for(unsigned site=0;site<SiteCount;++site){stats.sites[site].calls=counters[site].calls.load(std::memory_order_relaxed);
        stats.sites[site].totalUs=counters[site].totalUs.load(std::memory_order_relaxed);stats.sites[site].maxUs=counters[site].maxUs.load(std::memory_order_relaxed);}
    return stats;
}
std::size_t ReadEvents(std::uint64_t after,Event* output,std::size_t capacity,std::uint64_t& overwritten){
    overwritten=0;if(!output||!capacity)return 0;
    NativeWaitTracePolicy::Ring copy;
    AcquireSRWLockShared(&ringLock);copy=ring;ReleaseSRWLockShared(&ringLock);
    return copy.CopySince(after,output,capacity,overwritten);
}
std::size_t ReadActiveWaits(ActiveWait* output,std::size_t capacity){
    if(!output||!capacity)return 0;std::size_t count=0;
    const auto now=NowUs();
    for(const auto& slot:active){
        if(count==capacity)break;
        const auto generation=slot.generation.load(std::memory_order_acquire);
        if(generation&1u)continue;
        ActiveWait value{};value.event.thread=slot.owner.load(std::memory_order_acquire);
        value.event.startUs=slot.started.load(std::memory_order_relaxed);
        value.event.callerRva=slot.caller.load(std::memory_order_relaxed);
        value.event.api=slot.api.load(std::memory_order_relaxed);value.event.site=slot.site.load(std::memory_order_relaxed);
        value.event.timeout=slot.timeout.load(std::memory_order_relaxed);value.event.objectCount=slot.count.load(std::memory_order_relaxed);
        value.event.waitAll=slot.all.load(std::memory_order_relaxed);
        value.event.handle0=slot.handle0.load(std::memory_order_relaxed);value.event.handle1=slot.handle1.load(std::memory_order_relaxed);
        if(generation!=slot.generation.load(std::memory_order_acquire)||!value.event.thread||!value.event.startUs||now<value.event.startUs)continue;
        value.ageUs=now-value.event.startUs;output[count++]=value;
    }
    return count;
}
bool ReadSignals(RenderSignal (&output)[SignalCount]){
    if(!signalsInstalled.load(std::memory_order_acquire))return false;
    AcquireSRWLockShared(&signalLock);
    std::memcpy(output,signals,sizeof(signals));
    ReleaseSRWLockShared(&signalLock);
    return true;
}
bool Shutdown(){
    AcquireSRWLockExclusive(&installationLock);enabled.store(false,std::memory_order_release);
    if(!installed.load(std::memory_order_acquire)){ReleaseSRWLockExclusive(&installationLock);return true;}
    const auto image=imageBase.load(std::memory_order_relaxed);
    const std::uintptr_t originals[]={reinterpret_cast<std::uintptr_t>(originalSleep.load(std::memory_order_acquire)),
        reinterpret_cast<std::uintptr_t>(originalSingle.load(std::memory_order_acquire)),reinterpret_cast<std::uintptr_t>(originalMultiple.load(std::memory_order_acquire))};
    bool restored=true;
    if(signalsInstalled.exchange(false)){
        std::uint32_t current=0;
        const auto original=reinterpret_cast<std::uintptr_t>(originalSignal.load(std::memory_order_acquire));
        if(!Read(image+SignalSlot,&current,4)||
           (current==reinterpret_cast<std::uintptr_t>(&HookSignal)?
            !Exchange(image+SignalSlot,original,current):current!=original))restored=false;
    }
    for(unsigned api=0;api<ApiCount;++api){std::uint32_t current=0;
        if(!Read(image+Slots[api],&current,4)){restored=false;continue;}
        if(current==Hook(api)){if(!Exchange(image+Slots[api],originals[api],Hook(api)))restored=false;}
        else if(current!=originals[api])restored=false;
    }
    installed.store(false,std::memory_order_release);ReleaseSRWLockExclusive(&installationLock);return restored;
}
}
