#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d9.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include "frame_pacing.hpp"

// Sample the GPU queue span from the first shader upload to pre-capture.
// It includes intervening native draws and any gaps in queued GPU work;
// it is not a measurement of GPU utilization or exclusive shader execution.
// Queries: https://learn.microsoft.com/en-us/windows/win32/direct3d9/queries
// Collection runs on the owning D3D9 render thread, with no FLUSH or waits.
namespace D3D9GPUTiming {
enum class Result {Pending,Valid,Disjoint,Failed};
inline Result Resolve(HRESULT beginResult,HRESULT endResult,HRESULT frequencyResult,HRESULT disjointResult,
                      UINT64 begin,UINT64 end,UINT64 frequency,BOOL disjoint,double& milliseconds){
    if(FAILED(beginResult)||FAILED(endResult)||FAILED(frequencyResult)||FAILED(disjointResult))return Result::Failed;
    if(beginResult==S_FALSE||endResult==S_FALSE||frequencyResult==S_FALSE||disjointResult==S_FALSE)return Result::Pending;
    if(disjoint)return Result::Disjoint;
    if(!frequency||end<begin)return Result::Failed;
    const double elapsed=double(end-begin)*1000.0/double(frequency);
    if(!std::isfinite(elapsed)||elapsed<0||elapsed>60000)return Result::Failed;
    milliseconds=elapsed;return Result::Valid;
}
struct Stats {
    bool enabled=false,supported=false;
    unsigned pending=0,windowSamples=0;
    std::uint64_t attempted=0,validSamples=0,skippedSamples=0,disjointSamples=0,failures=0,resets=0,threadHandoffs=0;
    double lastMs=0,medianMs=0,p95Ms=0,maxMs=0;
    DWORD ownerThread=0;
    DWORD lastSampleTick=0,sampleAgeMs=MAXDWORD;
    HRESULT lastResult=S_OK;
};
namespace Detail {
constexpr unsigned RingSize=4,SampleStride=30;
struct Slot {
    IDirect3DQuery9* begin=nullptr,*end=nullptr,*frequency=nullptr,*disjoint=nullptr;
    bool pending=false,abandoned=false;
    std::uint64_t serial=0;
    void Clear(){
        for(auto** query:{&begin,&end,&frequency,&disjoint})if(*query){(*query)->Release();*query=nullptr;}
        pending=abandoned=false;serial=0;
    }
    HRESULT Create(IDirect3DDevice9* device){
        if(begin&&end&&frequency&&disjoint)return S_OK;
        HRESULT hr=device->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&begin);
        if(SUCCEEDED(hr))hr=device->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end);
        if(SUCCEEDED(hr))hr=device->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&frequency);
        if(SUCCEEDED(hr))hr=device->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint);
        if(FAILED(hr))Clear();return hr;
    }
};
struct State {
    Slot slots[RingSize];IDirect3DDevice9* device=nullptr;
    DWORD ownerThread=0;int active=-1;bool unsupported=false,seen=false;
    std::uint64_t seenSerial=0;Stats stats;FramePacing::Samples samples;
    void Clear(){for(auto& slot:slots)slot.Clear();device=nullptr;ownerThread=0;active=-1;unsupported=seen=false;
        samples={};stats.supported=false;stats.lastMs=0;stats.lastSampleTick=0;}
};
inline State state;
inline SRWLOCK lock=SRWLOCK_INIT;
inline std::atomic<bool> enabled{false};
inline std::atomic<IDirect3DDevice9*> fastDevice{nullptr};
inline std::atomic<std::uint64_t> fastSerial{UINT64_MAX};
inline std::atomic<DWORD> fastThread{0};
inline void Handoff(DWORD thread){
    // KL1 replaces the menu's native render worker when a mission starts.
    // Device identity alone does not establish the current command thread.
    // Retire old query spans rather than ending/publishing them on another
    // worker; no GetData(FLUSH), wait or native rendering call is introduced.
    if(state.active>=0)++state.stats.skippedSamples;
    for(auto& slot:state.slots){if(slot.pending)++state.stats.skippedSamples;slot.Clear();}
    state.active=-1;state.ownerThread=thread;state.seen=false;
    state.samples={};state.stats.supported=false;state.stats.lastMs=0;state.stats.lastSampleTick=0;
    ++state.stats.threadHandoffs;
}
inline void Poll(){
    for(auto& slot:state.slots){
        if(!slot.pending)continue;
        UINT64 begin=0,end=0,frequency=0;BOOL disjoint=FALSE;double milliseconds=0;
        const auto beginResult=slot.begin->GetData(&begin,sizeof(begin),0);
        const auto endResult=slot.end->GetData(&end,sizeof(end),0);
        const auto frequencyResult=slot.frequency->GetData(&frequency,sizeof(frequency),0);
        const auto disjointResult=slot.disjoint->GetData(&disjoint,sizeof(disjoint),0);
        const auto result=Resolve(beginResult,endResult,frequencyResult,disjointResult,begin,end,frequency,disjoint,milliseconds);
        if(result==Result::Pending)continue;
        slot.pending=false;
        if(result==Result::Failed){++state.stats.failures;state.stats.lastResult=FAILED(beginResult)?beginResult:FAILED(endResult)?endResult:
                FAILED(frequencyResult)?frequencyResult:FAILED(disjointResult)?disjointResult:E_FAIL;}
        else if(result==Result::Disjoint)++state.stats.disjointSamples;
        else if(!slot.abandoned){++state.stats.validSamples;state.stats.lastMs=milliseconds;state.stats.lastSampleTick=GetTickCount();
            state.samples.Add(static_cast<std::uint64_t>(milliseconds*1000.0));}
    }
}
inline void Finish(Slot& slot){
    const auto ended=slot.end->Issue(D3DISSUE_END);
    const auto frequency=slot.frequency->Issue(D3DISSUE_END);
    const auto disjoint=slot.disjoint->Issue(D3DISSUE_END);
    if(FAILED(ended)||FAILED(frequency)||FAILED(disjoint)){
        ++state.stats.failures;state.stats.lastResult=FAILED(ended)?ended:FAILED(frequency)?frequency:disjoint;
        // A failed query cannot be reused until its owning device resets.
        state.unsupported=true;slot.pending=false;
    }else slot.pending=true;
}
}
inline void Configure(bool enabled){Detail::enabled.store(enabled,std::memory_order_relaxed);}
// Call on the D3D9 thread before the game's native Reset, after in-flight
// capture completes. Configure itself never releases render-thread objects.
inline void Reset(){
    AcquireSRWLockExclusive(&Detail::lock);Detail::state.Clear();++Detail::state.stats.resets;
    Detail::fastDevice=nullptr;Detail::fastSerial=UINT64_MAX;Detail::fastThread=0;ReleaseSRWLockExclusive(&Detail::lock);
}
inline void Begin(IDirect3DDevice9* device,std::uint64_t frameSerial){
    using namespace Detail;
    if(!enabled.load(std::memory_order_relaxed)||!device)return;
    const auto thread=GetCurrentThreadId();
    if(fastDevice.load(std::memory_order_relaxed)==device&&fastSerial.load(std::memory_order_relaxed)==frameSerial&&
       fastThread.load(std::memory_order_relaxed)==thread)return;
    if(!TryAcquireSRWLockExclusive(&lock))return;
    struct Unlock {~Unlock(){ReleaseSRWLockExclusive(&lock);}} unlock;
    if(state.device!=device){state.Clear();state.device=device;state.ownerThread=thread;}
    if(state.ownerThread!=thread){
        // Another worker inside an already observed native frame may not
        // steal an active span. The first shader of a NEW Present serial may.
        if(state.seen&&frameSerial<=state.seenSerial)return;
        Handoff(thread);
    }
    if(state.seen&&state.seenSerial==frameSerial)return;
    state.seen=true;state.seenSerial=frameSerial;fastDevice.store(device,std::memory_order_relaxed);
    fastSerial.store(frameSerial,std::memory_order_relaxed);fastThread.store(thread,std::memory_order_relaxed);
    Poll();
    if(state.active>=0){auto& old=state.slots[state.active];old.abandoned=true;Finish(old);state.active=-1;++state.stats.skippedSamples;}
    if(state.unsupported||frameSerial%SampleStride)return;
    ++state.stats.attempted;
    int selected=-1;for(unsigned i=0;i<RingSize;++i)if(!state.slots[i].pending){selected=static_cast<int>(i);break;}
    if(selected<0){++state.stats.skippedSamples;return;}
    auto& slot=state.slots[selected];const auto created=slot.Create(device);
    if(FAILED(created)){++state.stats.failures;state.stats.lastResult=created;state.unsupported=true;return;}
    const auto disjoint=slot.disjoint->Issue(D3DISSUE_BEGIN);
    const auto began=SUCCEEDED(disjoint)?slot.begin->Issue(D3DISSUE_END):disjoint;
    if(FAILED(began)){
        if(SUCCEEDED(disjoint))slot.disjoint->Issue(D3DISSUE_END);
        ++state.stats.failures;state.stats.lastResult=began;state.unsupported=true;return;
    }
    slot.abandoned=false;slot.serial=frameSerial;state.active=selected;state.stats.supported=true;
}
inline void End(IDirect3DDevice9* device,std::uint64_t frameSerial){
    using namespace Detail;
    if(!device||fastDevice.load(std::memory_order_relaxed)!=device||!TryAcquireSRWLockExclusive(&lock))return;
    struct Unlock {~Unlock(){ReleaseSRWLockExclusive(&lock);}} unlock;
    if(state.ownerThread!=GetCurrentThreadId())return;
    Poll();if(state.active<0)return;
    auto& slot=state.slots[state.active];
    if(slot.serial!=frameSerial){slot.abandoned=true;++state.stats.skippedSamples;}
    Finish(slot);state.active=-1;
}
inline Stats Snapshot(){
    using namespace Detail;
    AcquireSRWLockShared(&lock);auto out=state.stats;out.enabled=enabled.load(std::memory_order_relaxed);out.ownerThread=state.ownerThread;
    out.sampleAgeMs=out.lastSampleTick?GetTickCount()-out.lastSampleTick:MAXDWORD;
    out.pending=state.active>=0?1u:0u;for(const auto& slot:state.slots)if(slot.pending)++out.pending;
    out.windowSamples=state.samples.Count();const auto distribution=state.samples.Describe();out.medianMs=distribution.medianUs/1000.0;out.p95Ms=distribution.p95Us/1000.0;out.maxMs=distribution.maxUs/1000.0;
    ReleaseSRWLockShared(&lock);return out;
}
}
