#pragma once
#include "d3d9_on12_api.hpp"
#include "on12_queue_policy.hpp"
#include "cpu_timer.hpp"
#include <atomic>

// Keep CPU-side dynamic uploads from running many frames ahead of this GPU.
// Two EVENT queries, bounded waiting and no GetData(FLUSH), frame discard,
// presentation/graphics change or device-wide synchronization API.
namespace D3D9On12Pacing {namespace Detail {
inline SRWLOCK lock=SRWLOCK_INIT;
inline std::atomic<bool> configured{true};
inline std::atomic<IDirect3DDevice9*> rejected{nullptr};
struct State {
    IDirect3DDevice9* device=nullptr;
    IDirect3DQuery9* events[On12QueuePolicy::Slots]{};
    On12QueuePolicy::Queue queue;
    bool checked=false,supported=false,faulted=false;
    DWORD thread=0;HRESULT lastResult=S_OK;
    HANDLE waitTimer=nullptr;
    bool timerAttempted=false;
    bool WaitForPoll(std::uint64_t delayUs){
        if(!timerAttempted){
            timerAttempted=true;
            waitTimer=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                             TIMER_MODIFY_STATE|SYNCHRONIZE);
        }
        if(!waitTimer)return false;
        LARGE_INTEGER due{};due.QuadPart=-static_cast<LONGLONG>(delayUs)*10;
        if(SetWaitableTimer(waitTimer,&due,0,nullptr,nullptr,FALSE)){
            // The timer controls the fractional-millisecond delay. The wait
            // timeout also bounds the call if no timer signal is delivered.
            const DWORD result=WaitForSingleObject(waitTimer,static_cast<DWORD>((delayUs+999)/1000));
            if(result==WAIT_OBJECT_0||result==WAIT_TIMEOUT)return true;
        }
        CloseHandle(waitTimer);waitTimer=nullptr;
        return false;
    }
    void Clear(){for(auto& query:events)if(query){query->Release();query=nullptr;}
        if(waitTimer){CloseHandle(waitTimer);waitTimer=nullptr;}timerAttempted=false;
        queue.Reset();device=nullptr;checked=supported=faulted=false;thread=0;lastResult=S_OK;}
};
inline State state;
inline void Fail(HRESULT result){++state.queue.stats.failures;state.lastResult=result;state.faulted=true;}
}
inline void Configure(bool value){Detail::configured=value;}
inline void Reset(){AcquireSRWLockExclusive(&Detail::lock);Detail::state.Clear();Detail::rejected=nullptr;ReleaseSRWLockExclusive(&Detail::lock);}
inline bool BeforePresent(IDirect3DDevice9* device,bool scene) {
    using namespace Detail;
    if(!scene||!device||!configured.load(std::memory_order_relaxed)||rejected.load(std::memory_order_relaxed)==device)return false;
    if(!TryAcquireSRWLockExclusive(&lock))return false;
    struct Unlock {~Unlock(){ReleaseSRWLockExclusive(&lock);}} unlock;
    const auto thread=GetCurrentThreadId();
    if(state.device!=device||state.thread!=thread){state.Clear();state.device=device;state.thread=thread;}
    if(!state.checked){
        Device9On12* bridge=nullptr;
        state.lastResult=device->QueryInterface(iid9on12,reinterpret_cast<void**>(&bridge));
        state.supported=SUCCEEDED(state.lastResult)&&bridge;state.checked=true;
        if(bridge)bridge->Release();
        if(!state.supported){rejected=device;return false;}
    }
    if(state.faulted)return false;
    const auto slot=state.queue.Available();
    // Never reset/reissue a query that still represents unfinished GPU work.
    if(slot==On12QueuePolicy::Slots){++state.queue.stats.skipped;return true;}
    if(!state.events[slot]){
        const auto result=device->CreateQuery(D3DQUERYTYPE_EVENT,&state.events[slot]);
        if(FAILED(result)||!state.events[slot]){Fail(FAILED(result)?result:E_FAIL);return false;}
    }
    const auto result=state.events[slot]->Issue(D3DISSUE_END);
    if(FAILED(result)){Fail(result);return false;}
    state.queue.Submitted(slot);return true;
}
inline std::uint64_t AfterPresent(IDirect3DDevice9* device,bool active) {
    using namespace Detail;
    if(!active||!configured.load(std::memory_order_relaxed)||!TryAcquireSRWLockExclusive(&lock))return 0;
    struct Unlock {~Unlock(){ReleaseSRWLockExclusive(&lock);}} unlock;
    if(state.device!=device||state.thread!=GetCurrentThreadId()||state.faulted)return 0;
    const auto before=state.queue.stats.failures;
    const auto waited=state.queue.Limit([](unsigned slot){
        const auto result=state.events[slot]->GetData(nullptr,0,0);state.lastResult=result;
        return result==S_OK?On12QueuePolicy::Readiness::Complete:result==S_FALSE?On12QueuePolicy::Readiness::Pending:On12QueuePolicy::Readiness::Failure;
    },[](std::uint64_t delayUs){return state.WaitForPoll(delayUs);},[]{return FrameMetrics::Microseconds(0);});
    if(state.queue.stats.failures!=before)state.faulted=true;
    return waited;
}
inline On12QueuePolicy::Stats Snapshot(){
    AcquireSRWLockShared(&Detail::lock);auto out=Detail::state.queue.stats;
    out.enabled=Detail::configured;out.supported=Detail::state.supported&&!Detail::state.faulted;
    out.pending=Detail::state.queue.Pending();out.thread=Detail::state.thread;out.lastResult=Detail::state.lastResult;
    ReleaseSRWLockShared(&Detail::lock);return out;
}
}
