#include "d3d9_gpu_timing.hpp"
#include <cstdio>
#include <cmath>
#include <atomic>
#include <cstring>

namespace {
unsigned checks=0,failures=0;
void Check(bool okay,const char* name){++checks;if(!okay){++failures;std::printf("FAIL: %s\n",name);}}
std::atomic<unsigned> liveQueries{0},createdQueries{0},flushReads{0};
std::atomic<UINT64> timestamp{0};
std::atomic<bool> pending{false};
struct Query final:IDirect3DQuery9 {
    D3DQUERYTYPE type;UINT64 value=0;ULONG references=1;
    explicit Query(D3DQUERYTYPE t):type(t){++liveQueries;++createdQueries;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**) noexcept override{return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef() noexcept override{return ++references;}
    ULONG STDMETHODCALLTYPE Release() noexcept override{if(--references)return references;--liveQueries;delete this;return 0;}
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9**) noexcept override{return E_NOTIMPL;}
    D3DQUERYTYPE STDMETHODCALLTYPE GetType() noexcept override{return type;}
    DWORD STDMETHODCALLTYPE GetDataSize() noexcept override{return type==D3DQUERYTYPE_TIMESTAMPDISJOINT?sizeof(BOOL):sizeof(UINT64);}
    HRESULT STDMETHODCALLTYPE Issue(DWORD flags) noexcept override{
        if(type==D3DQUERYTYPE_TIMESTAMP&&flags==D3DISSUE_END)value=timestamp.fetch_add(10)+10;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetData(void* data,DWORD bytes,DWORD flags) noexcept override{
        if(flags)++flushReads;
        if(pending.load())return S_FALSE;
        if(!data||bytes!=GetDataSize())return D3DERR_INVALIDCALL;
        if(type==D3DQUERYTYPE_TIMESTAMPDISJOINT){const BOOL disjoint=FALSE;std::memcpy(data,&disjoint,bytes);}
        else{const UINT64 result=type==D3DQUERYTYPE_TIMESTAMPFREQ?1000000:value;std::memcpy(data,&result,bytes);}
        return S_OK;
    }
};
HRESULT STDMETHODCALLTYPE CreateQuery(IDirect3DDevice9*,D3DQUERYTYPE type,IDirect3DQuery9** output){
    if(!output)return D3DERR_INVALIDCALL;*output=new Query(type);return S_OK;
}
// Only CreateQuery is used by the production sampler. Slot118 is the actual
// IDirect3DDevice9 ABI; the test never opens a D3D device, DLL or GPU session.
struct Device {void* methods[119]{};void** table=methods;
    Device(){methods[118]=reinterpret_cast<void*>(&CreateQuery);}
    IDirect3DDevice9* Value(){return reinterpret_cast<IDirect3DDevice9*>(&table);}
};
struct Work {IDirect3DDevice9* device;UINT64 serial;bool finish=false,poll=false;DWORD thread=0;};
DWORD WINAPI Worker(void* pointer){auto& work=*static_cast<Work*>(pointer);work.thread=GetCurrentThreadId();
    D3D9GPUTiming::Begin(work.device,work.serial);
    if(work.finish)D3D9GPUTiming::End(work.device,work.serial);
    if(work.poll)D3D9GPUTiming::Begin(work.device,work.serial+1);
    return 0;
}
void Run(Work& work){HANDLE thread=CreateThread(nullptr,0,Worker,&work,0,nullptr);Check(thread!=nullptr,"private native diagnostic worker created");
    if(thread){Check(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0,"private diagnostic worker completes without waiting for GPU");CloseHandle(thread);}}
void ThreadHandoff(){
    using namespace D3D9GPUTiming;
    Reset();Configure(true);Device fixture;auto* device=fixture.Value();const DWORD first=GetCurrentThreadId();
    Begin(device,60);End(device,60);Begin(device,61);
    auto before=Snapshot();Check(before.ownerThread==first&&before.validSamples==1&&before.windowSamples==1,"menu worker publishes its own complete GPU span");
    Work same{device,61};Run(same);auto stats=Snapshot();
    Check(same.thread!=first&&stats.ownerThread==first&&stats.threadHandoffs==before.threadHandoffs,"same serial cannot transfer query ownership to a second worker");
    Work old{device,59};Run(old);stats=Snapshot();
    Check(stats.ownerThread==first&&stats.threadHandoffs==before.threadHandoffs,"late older shader callback cannot steal ownership");
    Work mission{device,90,true,true};Run(mission);stats=Snapshot();
    Check(stats.ownerThread==mission.thread&&stats.threadHandoffs==before.threadHandoffs+1,"new mission worker takes over on a newer native Present serial");
    Check(stats.validSamples==2&&stats.windowSamples==1&&stats.sampleAgeMs<2000,"new owner resumes fresh measurements instead of keeping menu-only timing");
    Check(liveQueries==4&&createdQueries==8,"handoff retires old query ring and allocates only the new owner's bounded slot");
    End(device,91);Check(Snapshot().validSamples==stats.validSamples,"another worker cannot finish/publish the active owner's span");
    Begin(device,120);pending=true;End(device,120);stats=Snapshot();const auto priorValid=stats.validSamples;
    Check(stats.pending==1,"old-owner pending GPU query fixture is retained until handoff");
    Work pendingOwner{device,150};Run(pendingOwner);stats=Snapshot();
    Check(stats.ownerThread==pendingOwner.thread&&stats.validSamples==priorValid&&stats.windowSamples==0&&stats.lastSampleTick==0,
          "handoff discards pending previous-worker intervals and clears stale rolling GPU statistics");
    Check(stats.pending==1&&liveQueries==4,"only the new owner's active bounded query survives a pending handoff");
    pending=false;
    Work unfinished{device,180,true,true};Run(unfinished);stats=Snapshot();
    Check(stats.validSamples==priorValid+1&&stats.windowSamples==1,"abandoned unmatched old interval is never counted as a new complete sample");
    Check(flushReads==0,"handoff/query collection never uses GetData FLUSH or GPU waits");
    Reset();Configure(false);Check(liveQueries==0&&!Snapshot().pending,"Reset releases all private query COM references after worker handoffs");
}
}

int main(){
    using namespace D3D9GPUTiming;
    const auto check=Check;
    double ms=-1;
    check(Resolve(S_OK,S_FALSE,S_OK,S_OK,10,0,1000,FALSE,ms)==Result::Pending&&ms==-1,"S_FALSE never becomes a zero-duration sample");
    check(Resolve(S_OK,S_OK,S_OK,S_FALSE,10,30,1000,FALSE,ms)==Result::Pending,"disjoint query must also finish");
    check(Resolve(S_FALSE,E_FAIL,S_OK,S_OK,10,30,1000,FALSE,ms)==Result::Failed,"failed query retires a slot even while another is pending");
    check(Resolve(S_OK,S_OK,S_OK,S_OK,10,30,1000,TRUE,ms)==Result::Disjoint,"frequency discontinuity invalidates the sample");
    check(Resolve(S_OK,S_OK,S_OK,S_OK,10,30,0,FALSE,ms)==Result::Failed,"zero frequency is invalid");
    check(Resolve(S_OK,S_OK,S_OK,S_OK,30,10,1000,FALSE,ms)==Result::Failed,"reversed timestamps are invalid");
    check(Resolve(S_OK,S_OK,S_OK,S_OK,10,30010,1000000,FALSE,ms)==Result::Valid&&std::fabs(ms-30)<1e-9,"GPU ticks convert with their own frequency");
    Configure(true);Begin(nullptr,30);End(nullptr,30);auto stats=Snapshot();
    check(stats.enabled&&!stats.supported&&!stats.pending&&!stats.attempted,"null device opens no query resources");
    Reset();Configure(false);stats=Snapshot();check(!stats.enabled&&!stats.pending&&stats.resets==1,"reset retires the bounded ring");
    ThreadHandoff();
    std::printf("Asynchronous D3D9 GPU timestamp policy: %u checks, %u failures.\n",checks,failures);return failures?1:0;
}
