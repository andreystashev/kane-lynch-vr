#include "native_buffer_flush_kl.hpp"
#include "cpu_timer.hpp"
#include "profile.hpp"
#include "MinHook.h"
#include "d3d9_on12_api.hpp"
#include "renderer_identity.hpp"
#include <atomic>
#include <algorithm>
#include <array>

namespace NativeBufferFlushKL {namespace {
static_assert(sizeof(void*)==4,"The native render worker is PE32");
using FlushFunction=void(__thiscall*)(void*);
using BeginFunction=HRESULT(__thiscall*)(void*);
using CreateVertexFunction=void*(__thiscall*)(void*,void*,unsigned,DWORD,D3DPOOL);
FlushFunction originalFlush=nullptr;BeginFunction originalBegin=nullptr;CreateVertexFunction originalCreateVertex=nullptr;
std::uintptr_t imageBase=0;
std::atomic<bool> installed{false},enabled{false},verified{false};
std::atomic<bool> batching{true},on12{false};
std::atomic<Backend> backend{Backend::Unknown};
std::atomic<bool> sparseBlocked{false};
std::atomic<std::uint64_t> batchedCalls{0},savedLocks{0};
std::atomic<std::uint64_t> singleMapCalls{0},lockFailures{0},unlockFailures{0},invalidMappings{0};
std::atomic<std::uint64_t> uploadDeferred{0},uploadRecovered{0},recoveryAttempts{0},recoveryTimeouts{0},blockedDraws{0};
std::atomic<std::uint64_t> dynamicVertexCreated{0},dynamicVertexFallback{0};
struct FailedUpload {std::uintptr_t control=0,resource=0;DWORD attemptedAt=0,firstAt=0;HRESULT result=S_OK;bool poisoned=false;};
SRWLOCK failureLock=SRWLOCK_INIT;
FailedUpload failures[2]{};
std::atomic<bool> uploadFailure{false};
std::atomic<DWORD> lastFailureTick{0};
std::atomic<HRESULT> lastUploadResult{S_OK};
struct AtomicTotals {
    std::atomic<std::uint64_t> calls{0},active{0},totalUs{0},maxUs{0},bytes{0},ranges{0};
} totals[PhaseCount];
SRWLOCK eventLock=SRWLOCK_INIT;
std::array<Event,EventCapacity> events;
std::uint64_t eventCount=0;
std::atomic<std::uint64_t> dropped{0};
SRWLOCK activeLock=SRWLOCK_INIT;
Event inProgress{};bool active=false;
bool Read(std::uintptr_t at,void* output,std::size_t size){SIZE_T got=0;return at&&
    ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),output,size,&got)&&got==size;}
void RefreshFailureFlag(){uploadFailure.store(failures[0].resource||failures[1].resource,std::memory_order_release);}
void DropFailure(FailedUpload& failure){auto* held=reinterpret_cast<IDirect3DResource9*>(failure.resource);failure={};if(held)held->Release();}
bool StillOwned(const FailedUpload& failure,unsigned kind){
    if(!failure.control||!failure.resource)return false;
    std::uint32_t owner=0,control=0,resource=0;
    return Read(imageBase+0x786670,&owner,4)&&owner&&Read(owner+(kind==StaticVertex?0xC:0x10),&control,4)&&
        control==failure.control&&Read(control,&resource,4)&&resource==failure.resource;
}
void ClearFailure(unsigned kind){AcquireSRWLockExclusive(&failureLock);auto& old=failures[kind-StaticVertex];
    if(old.resource)uploadRecovered.fetch_add(1,std::memory_order_relaxed);DropFailure(old);RefreshFailureFlag();ReleaseSRWLockExclusive(&failureLock);}
void DeferUpload(unsigned kind,void* control,void* resource,HRESULT result,bool poisoned){const auto now=GetTickCount();
    AcquireSRWLockExclusive(&failureLock);auto& old=failures[kind-StaticVertex];
    const auto first=old.control==reinterpret_cast<std::uintptr_t>(control)&&old.resource==reinterpret_cast<std::uintptr_t>(resource)?old.firstAt:now;
    // Keep COM identity alive while a failure is remembered. Pointer recycling
    // across mission unload cannot make a new resource inherit the old gate.
    if(old.resource!=reinterpret_cast<std::uintptr_t>(resource)){DropFailure(old);static_cast<IDirect3DResource9*>(resource)->AddRef();}
    old={reinterpret_cast<std::uintptr_t>(control),reinterpret_cast<std::uintptr_t>(resource),now,first,result,poisoned};
    RefreshFailureFlag();ReleaseSRWLockExclusive(&failureLock);lastFailureTick=now;lastUploadResult=result;++uploadDeferred;
}
bool DelayRetry(unsigned kind,void* control,HRESULT& result){if(!uploadFailure.load(std::memory_order_acquire))return false;
    AcquireSRWLockExclusive(&failureLock);auto& old=failures[kind-StaticVertex];bool delay=false;
    if(old.resource){if(old.control!=reinterpret_cast<std::uintptr_t>(control)||!StillOwned(old,kind))DropFailure(old);
        else{result=old.result;delay=old.poisoned||DWORD(GetTickCount()-old.attemptedAt)<250;}}
    RefreshFailureFlag();ReleaseSRWLockExclusive(&failureLock);return delay;
}
// Recovery-only: the usual successful sparse upload does no query or wait.
// The query belongs to this resource's device. The polling budget includes
// setup time; individual driver calls themselves have no hard wall deadline.
HRESULT DrainDevice(void* resource,unsigned kind){IDirect3DDevice9* device=nullptr;
    const auto start=GetTickCount();HRESULT hr=kind==StaticIndex?
        static_cast<IDirect3DIndexBuffer9*>(resource)->GetDevice(&device):static_cast<IDirect3DVertexBuffer9*>(resource)->GetDevice(&device);
    IDirect3DQuery9* query=nullptr;
    if(SUCCEEDED(hr)&&device)hr=device->CreateQuery(D3DQUERYTYPE_EVENT,&query);
    else if(SUCCEEDED(hr))hr=E_POINTER;
    if(SUCCEEDED(hr)&&query)hr=query->Issue(D3DISSUE_END);else if(SUCCEEDED(hr))hr=E_POINTER;
    while(SUCCEEDED(hr)&&query){if(DWORD(GetTickCount()-start)>=8){++recoveryTimeouts;hr=HRESULT_FROM_WIN32(WAIT_TIMEOUT);break;}
        hr=query->GetData(nullptr,0,D3DGETDATA_FLUSH);if(hr!=S_FALSE)break;Sleep(0);}
    if(query)query->Release();if(device)device->Release();return hr;
}
HMODULE ImplementationModule(IDirect3DDevice9* device,unsigned slot){
    std::uintptr_t table=0,function=0;
    if(!Read(reinterpret_cast<std::uintptr_t>(device),&table,sizeof(table))||
       !Read(table+slot*sizeof(void*),&function,sizeof(function))||!function)return nullptr;
    HMODULE module=nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<const wchar_t*>(function),&module)?module:nullptr;
}
bool SameFile(const wchar_t* first,const wchar_t* second){
    constexpr DWORD share=FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE;
    HANDLE a=CreateFileW(first,FILE_READ_ATTRIBUTES,share,nullptr,OPEN_EXISTING,0,nullptr);
    if(a==INVALID_HANDLE_VALUE)return false;
    HANDLE b=CreateFileW(second,FILE_READ_ATTRIBUTES,share,nullptr,OPEN_EXISTING,0,nullptr);
    BY_HANDLE_FILE_INFORMATION x{},y{};
    const bool same=b!=INVALID_HANDLE_VALUE&&GetFileInformationByHandle(a,&x)&&GetFileInformationByHandle(b,&y)&&
        x.dwVolumeSerialNumber==y.dwVolumeSerialNumber&&x.nFileIndexHigh==y.nFileIndexHigh&&x.nFileIndexLow==y.nFileIndexLow;
    if(b!=INVALID_HANDLE_VALUE)CloseHandle(b);CloseHandle(a);return same;
}
bool SystemModule(HMODULE module,const wchar_t* filename){
    if(!module)return false;wchar_t actual[MAX_PATH]{},expected[MAX_PATH]{};
    const DWORD a=GetModuleFileNameW(module,actual,MAX_PATH),n=GetSystemDirectoryW(expected,MAX_PATH);
    const auto length=std::wcslen(filename);
    if(!a||a>=MAX_PATH||!n||n>=MAX_PATH||n+1+length>=MAX_PATH)return false;
    expected[n]=L'\\';std::memcpy(expected+n+1,filename,(length+1)*sizeof(wchar_t));
    // Compare file identities, not System32/SysWOW64 spelling: this module is
    // PE32 and Windows can expose either alias for the same loaded system DLL.
    return SameFile(actual,expected);
}
Backend IdentifyBackend(IDirect3DDevice9* device,bool on12Capability){
    if(!device)return Backend::Unknown;
    if(on12Capability)return Backend::On12;
    const auto qi=ImplementationModule(device,DeviceQueryInterfaceSlot);
    const auto caps=ImplementationModule(device,DeviceCapsSlot);
    const auto creation=ImplementationModule(device,DeviceCreationParametersSlot);
    if(!qi||!caps||caps!=creation)return Backend::Unknown;
    // Windows application compatibility may replace QueryInterface with a
    // thunk from system apphelp.dll. Two unhooked device getters still have
    // to prove the actual implementation. An arbitrary wrapper cannot opt in.
    if(qi!=caps&&!SystemModule(qi,L"apphelp.dll"))return Backend::Unknown;
    if(SystemModule(caps,L"d3d9.dll"))return Backend::Original;
    wchar_t path[MAX_PATH]{};char digest[65]{};const DWORD count=GetModuleFileNameW(caps,path,MAX_PATH);
    if(!count||count>=MAX_PATH)return Backend::Unknown;
    const wchar_t* name=std::wcsrchr(path,L'\\');name=name?name+1:path;
    if(_wcsicmp(name,L"d3d9.dll")||!HashFile(path,digest)||std::strcmp(digest,RendererIdentity::DXVKDllSha256))return Backend::Unknown;
    return Backend::DXVK; // Recognized for diagnostics; wide sparse maps regress DXVK.
}
Metadata Observe(void* control){
    Metadata result;std::uint32_t header[4]{};
    const auto at=reinterpret_cast<std::uintptr_t>(control);
    if(!Read(at,header,sizeof(header)))return result;
    result.pending=header[1]!=0;result.capacity=header[2];
    result.shadowReady=header[1]&&header[1]==header[3];
    if(!result.pending)return result;
    std::uint32_t owner=0,controls[2]{};
    if(Read(imageBase+0x786670,&owner,4)&&Read(owner+0xC,controls,sizeof(controls)))
        result.resource=at==controls[0]?StaticVertex:at==controls[1]?StaticIndex:Unknown;
    if(!Read(at+0x4010,&result.ranges,4)||!header[3]||!result.capacity||
       result.capacity>512u*1024u*1024u)return result;
    result.submittedRanges=result.ranges;
    if(result.ranges>=2048){result.bytes=result.spanBytes=result.capacity;result.valid=true;return result;}
    // The original function owns this exact control and immediately reads
    // these same <=2048 entries. No resource Lock/descriptor call is added.
    const auto* ranges=reinterpret_cast<const std::uint32_t*>(at+0x10);
    std::uint32_t first=result.capacity,last=0;
    for(unsigned i=0;i<result.ranges;++i){const auto offset=ranges[i*2],bytes=ranges[i*2+1];
        if(offset>result.capacity||bytes>result.capacity-offset)return result;
        result.bytes+=bytes;first=std::min(first,offset);last=std::max(last,offset+bytes);}
    result.spanBytes=result.ranges?last-first:0;result.valid=true;return result;
}
void Complete(Event event,DWORD originalError){
    event.endUs=FrameMetrics::Microseconds(0);event.wallUs=event.endUs-event.startUs;
    event.tick=GetTickCount();auto& target=totals[event.phase];
    target.calls.fetch_add(1,std::memory_order_relaxed);
    target.active.fetch_add(event.phase==BeginScene||event.metadata.pending?1:0,std::memory_order_relaxed);
    target.totalUs.fetch_add(event.wallUs,std::memory_order_relaxed);
    auto maximum=target.maxUs.load(std::memory_order_relaxed);
    while(maximum<event.wallUs&&!target.maxUs.compare_exchange_weak(maximum,event.wallUs,std::memory_order_relaxed)){}
    if(event.metadata.valid){target.bytes.fetch_add(event.metadata.bytes,std::memory_order_relaxed);
        target.ranges.fetch_add(event.metadata.ranges,std::memory_order_relaxed);}
    if(TryAcquireSRWLockExclusive(&eventLock)){
        event.sequence=++eventCount;events[(event.sequence-1)%events.size()]=event;ReleaseSRWLockExclusive(&eventLock);
    }else dropped.fetch_add(1,std::memory_order_relaxed);
    AcquireSRWLockExclusive(&activeLock);
    if(active&&inProgress.thread==event.thread&&inProgress.startUs==event.startUs)active=false;
    ReleaseSRWLockExclusive(&activeLock);
    SetLastError(originalError);
}
void Start(Event& event){event.startUs=FrameMetrics::Microseconds(0);
    AcquireSRWLockExclusive(&activeLock);inProgress=event;active=true;ReleaseSRWLockExclusive(&activeLock);}
void RefreshActive(const Event& event){AcquireSRWLockExclusive(&activeLock);
    if(active&&inProgress.thread==event.thread&&inProgress.startUs==event.startUs)inProgress=event;
    ReleaseSRWLockExclusive(&activeLock);}
struct Control {
    void* resource;void* pending;unsigned capacity;const unsigned char* shadow;
    Range ranges[2048];unsigned count;
};
static_assert(offsetof(Control,ranges)==0x10&&offsetof(Control,count)==0x4010&&sizeof(Control)==0x4014,"pinned native static buffer control");
bool ResourceMatches(const Control& control,unsigned kind){
    if(!control.resource)return false;
    if(kind==StaticVertex){D3DVERTEXBUFFER_DESC d{};
        return SUCCEEDED(static_cast<IDirect3DVertexBuffer9*>(control.resource)->GetDesc(&d))&&
            d.Type==D3DRTYPE_VERTEXBUFFER&&d.Pool==D3DPOOL_DEFAULT&&d.Size==control.capacity&&
            (d.Usage==D3DUSAGE_WRITEONLY||d.Usage==(D3DUSAGE_WRITEONLY|D3DUSAGE_SOFTWAREPROCESSING)||
             d.Usage==(D3DUSAGE_WRITEONLY|D3DUSAGE_DYNAMIC)||
             d.Usage==(D3DUSAGE_WRITEONLY|D3DUSAGE_SOFTWAREPROCESSING|D3DUSAGE_DYNAMIC));}
    if(kind==StaticIndex){D3DINDEXBUFFER_DESC d{};
        return SUCCEEDED(static_cast<IDirect3DIndexBuffer9*>(control.resource)->GetDesc(&d))&&
            d.Type==D3DRTYPE_INDEXBUFFER&&d.Pool==D3DPOOL_DEFAULT&&d.Size==control.capacity&&d.Format==D3DFMT_INDEX16&&
            (d.Usage==(D3DUSAGE_WRITEONLY|D3DUSAGE_DONOTCLIP)||d.Usage==(D3DUSAGE_WRITEONLY|D3DUSAGE_SOFTWAREPROCESSING));}
    return false;
}
void FlushPrepared(Control& control,Event event,bool trace,DWORD error,bool merge){
    auto& m=event.metadata;Range span;
    const unsigned alignment=m.resource==StaticIndex?2:1;
    const bool singleOrFull=control.count>=2048||(control.count==1&&control.ranges[0].bytes&&
        control.ranges[0].offset<control.capacity&&control.ranges[0].bytes<=control.capacity-control.ranges[0].offset&&
        !(control.ranges[0].offset%alignment)&&!(control.ranges[0].bytes%alignment));
    if((!singleOrFull&&!EnclosingRange(control.ranges,control.count,control.capacity,span,alignment))||!ResourceMatches(control,m.resource)){
        if(trace)Start(event);SetLastError(error);originalFlush(&control);const auto originalError=GetLastError();
        if(trace)Complete(event,originalError);else SetLastError(originalError);return;
    }
    m.submittedRanges=merge||control.count>=2048?1:control.count;if(trace)Start(event);SetLastError(error);
    // Both pinned static VB/IB interfaces share Lock slot11 and Unlock slot12.
    // Use the correctly typed interface; output is the native pending field,
    // which must retain the mapping until Unlock just as in 0x48E030.
    const auto lock=[&](UINT offset,UINT bytes,void** output,DWORD flags){
        return m.resource==StaticIndex?static_cast<IDirect3DIndexBuffer9*>(control.resource)->Lock(offset,bytes,output,flags):
            static_cast<IDirect3DVertexBuffer9*>(control.resource)->Lock(offset,bytes,output,flags);};
    const auto unlock=[&](){return m.resource==StaticIndex?static_cast<IDirect3DIndexBuffer9*>(control.resource)->Unlock():
            static_cast<IDirect3DVertexBuffer9*>(control.resource)->Unlock();};
    const auto upload=[&](){return merge?CheckedWorkerUpload(control.ranges,control.count,control.capacity,control.shadow,control.pending,lock,unlock,alignment):
        CheckedNativeUpload(control.ranges,control.count,control.capacity,control.shadow,control.pending,lock,unlock,alignment);};
    auto outcome=upload();
    if(outcome.lockFailed){
        ++lockFailures;++recoveryAttempts;
        // The native 0x48E030 ignores Lock's HRESULT and memcpy's a null
        // output. Never hand a failed map back to that function. Drain only
        // for allocation pressure; then retry this same all-or-none map once.
        if(outcome.result==E_OUTOFMEMORY||outcome.result==D3DERR_OUTOFVIDEOMEMORY)DrainDevice(control.resource,m.resource);
        outcome=upload();
    }
    auto originalError=GetLastError();event.result=outcome.result;
    if(outcome.lockFailed)lockFailures.fetch_add(1,std::memory_order_relaxed);
    if(outcome.unlockFailed)unlockFailures.fetch_add(1,std::memory_order_relaxed);
    if(outcome.invalidMapping)invalidMappings.fetch_add(1,std::memory_order_relaxed);
    if(outcome.completed){
        control.count=0; // pending was cleared after the one native Unlock.
        if(merge&&m.ranges>1&&m.ranges<2048&&!outcome.invalidMapping){singleMapCalls.fetch_add(1,std::memory_order_relaxed);
            batchedCalls.fetch_add(1,std::memory_order_relaxed);savedLocks.fetch_add(m.ranges-1,std::memory_order_relaxed);}
        // Failed Unlock leaves ownership uncertain. Do not retry this queue or
        // attempt another batching operation until a fresh device is bound.
        if(outcome.unlockFailed){sparseBlocked.store(true,std::memory_order_release);
            DeferUpload(m.resource,&control,control.resource,outcome.result,true);}
        else ClearFailure(m.resource);
    }else{
        if(outcome.attempted){
            // No complete upload was committed. A checked native per-range
            // retry can have copied a prefix; repeating that prefix is safe.
            // The CPU shadow, original pending
            // sentinel and exact dirty table stay native-owned until retry.
            // Draws referencing this resource are gated, rather than showing
            // uninitialized/stale geometry. UI and unrelated resources run.
            DeferUpload(m.resource,&control,control.resource,outcome.result,false);
        }else{
            m.submittedRanges=m.ranges;if(trace)RefreshActive(event);SetLastError(error);originalFlush(&control);originalError=GetLastError();
        }
    }
    if(trace)Complete(event,originalError);else SetLastError(originalError);
}
void __fastcall FlushHook(void* control,void*){
    const bool trace=enabled.load(std::memory_order_acquire);
    const bool optimize=batching.load(std::memory_order_relaxed)&&SupportsBatching(backend.load(std::memory_order_acquire))&&!sparseBlocked.load(std::memory_order_relaxed);
    const auto error=GetLastError();Event event;event.phase=StaticFlush;event.thread=GetCurrentThreadId();
    event.metadata=Observe(control);
    const auto caller=reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
    auto& m=event.metadata;const bool exactWorker=(m.resource==StaticVertex&&caller==imageBase+0xB52F8&&m.capacity==0x2400000)||
        (m.resource==StaticIndex&&caller==imageBase+0xB5305&&m.capacity==0x300000);
    HRESULT deferredResult=S_OK;
    if(exactWorker&&DelayRetry(m.resource,control,deferredResult)){
        event.result=deferredResult;
        if(trace){Start(event);Complete(event,error);}else SetLastError(error);return;
    }
    if(exactWorker&&
       m.valid&&m.shadowReady&&m.ranges){
        FlushPrepared(*static_cast<Control*>(control),event,trace,error,optimize);return;
    }
    if(trace)Start(event);SetLastError(error);originalFlush(control);const auto originalError=GetLastError();
    if(trace)Complete(event,originalError);else SetLastError(originalError);
}
HRESULT __fastcall BeginHook(void* control,void*){
    if(!enabled.load(std::memory_order_acquire))return originalBegin(control);
    const auto error=GetLastError();Event event;event.phase=BeginScene;event.thread=GetCurrentThreadId();
    Start(event);SetLastError(error);
    const auto result=originalBegin(control);const auto originalError=GetLastError();event.result=result;
    Complete(event,originalError);return result;
}
void* __fastcall CreateVertexHook(void* control,void*,void* renderer,unsigned capacity,DWORD usage,D3DPOOL pool){
    const auto error=GetLastError();const auto caller=reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
    IDirect3DDevice9* device=nullptr;Device9On12* bridge=nullptr;
    const bool candidate=caller==imageBase+0x87878&&capacity==0x2400000&&pool==D3DPOOL_DEFAULT&&
        (usage==D3DUSAGE_WRITEONLY||usage==(D3DUSAGE_WRITEONLY|D3DUSAGE_SOFTWAREPROCESSING))&&
        Read(reinterpret_cast<std::uintptr_t>(renderer)+4,&device,sizeof(device))&&device;
    const bool dynamic=candidate&&SUCCEEDED(device->QueryInterface(iid9on12,reinterpret_cast<void**>(&bridge)))&&bridge;
    if(bridge)bridge->Release();
    // This exact caller is a fresh Control allocation. The native ctor does
    // not initialize its resource field before CreateVertexBuffer; a failed
    // runtime is allowed to leave the output untouched.
    if(dynamic)*static_cast<IDirect3DVertexBuffer9**>(control)=nullptr;
    SetLastError(error);
    auto* result=originalCreateVertex(control,renderer,capacity,dynamic?usage|D3DUSAGE_DYNAMIC:usage,pool);
    auto originalError=GetLastError();
    if(dynamic){std::uintptr_t resource=0;
        if(Read(reinterpret_cast<std::uintptr_t>(control),&resource,4)&&resource)++dynamicVertexCreated;
        else{
            // The original ctor already owns its one CPU shadow. Retry just
            // resource creation, preserving that shadow and the native ABI.
            IDirect3DVertexBuffer9* fallback=nullptr;const auto hr=device->CreateVertexBuffer(capacity,usage,0,pool,&fallback,nullptr);
            if(SUCCEEDED(hr)&&fallback){*static_cast<IDirect3DVertexBuffer9**>(control)=fallback;++dynamicVertexFallback;}
            else if(fallback)fallback->Release();
        }
    }
    SetLastError(originalError);return result;
}
void Log(LogCallback log,const char* text){if(log)log(text);}
}
bool Install(LogCallback log){
    wchar_t path[MAX_PATH]{};char digest[65]{};
    if(!GetModuleFileNameW(nullptr,path,MAX_PATH)||!HashFile(path,digest)||!IsSupportedExecutableHash(digest))return false;
    return InstallAt(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)),log);
}
bool InstallAt(std::uintptr_t image,LogCallback log){
    if(installed.load(std::memory_order_acquire))return imageBase==image;
    if(!VerifyImage(image,Read)){Log(log,"Static buffer flush diagnostics: pinned worker proof rejected.");return false;}
    void* flush=reinterpret_cast<void*>(image+FlushRva);void* begin=reinterpret_cast<void*>(image+BeginSceneRva);
    void* createVertex=reinterpret_cast<void*>(image+StaticVertexCreateRva);
    if(MH_CreateHook(flush,reinterpret_cast<void*>(&FlushHook),reinterpret_cast<void**>(&originalFlush))!=MH_OK)return false;
    if(MH_CreateHook(begin,reinterpret_cast<void*>(&BeginHook),reinterpret_cast<void**>(&originalBegin))!=MH_OK){MH_RemoveHook(flush);return false;}
    if(MH_CreateHook(createVertex,reinterpret_cast<void*>(&CreateVertexHook),reinterpret_cast<void**>(&originalCreateVertex))!=MH_OK){MH_RemoveHook(flush);MH_RemoveHook(begin);return false;}
    imageBase=image;enabled=false;
    if(MH_EnableHook(flush)!=MH_OK||MH_EnableHook(begin)!=MH_OK||MH_EnableHook(createVertex)!=MH_OK){
        MH_DisableHook(flush);MH_DisableHook(begin);MH_DisableHook(createVertex);
        MH_RemoveHook(flush);MH_RemoveHook(begin);MH_RemoveHook(createVertex);imageBase=0;return false;}
    verified=true;installed.store(true,std::memory_order_release);enabled.store(true,std::memory_order_release);
    Log(log,"Static buffer flush diagnostics: verified pre-camera static VB/IB upload and BeginScene scopes installed.");return true;
}
void SetEnabled(bool value){enabled.store(value&&installed.load(std::memory_order_acquire),std::memory_order_release);}
void ConfigureBatching(bool value){batching.store(value,std::memory_order_release);}
void SetDevice(IDirect3DDevice9* device){
    Device9On12* bridge=nullptr;
    const bool value=device&&SUCCEEDED(device->QueryInterface(iid9on12,reinterpret_cast<void**>(&bridge)))&&bridge;
    if(bridge)bridge->Release();const auto identified=IdentifyBackend(device,value);
    AcquireSRWLockExclusive(&failureLock);for(auto& old:failures)DropFailure(old);RefreshFailureFlag();ReleaseSRWLockExclusive(&failureLock);
    sparseBlocked.store(false,std::memory_order_release);on12.store(value,std::memory_order_release);backend.store(identified,std::memory_order_release);
}
bool ShouldBlockDraw(IDirect3DDevice9* device,bool indexed){
    if(!uploadFailure.load(std::memory_order_acquire)||!device)return false;
    const auto error=GetLastError();std::uintptr_t vertex=0,index=0;
    AcquireSRWLockExclusive(&failureLock);
    for(unsigned kind:{unsigned(StaticVertex),unsigned(StaticIndex)}){auto& old=failures[kind-StaticVertex];
        if(old.resource&&!StillOwned(old,kind))DropFailure(old);
        if(old.resource){if(kind==StaticVertex)vertex=old.resource;else index=old.resource;}}
    RefreshFailureFlag();ReleaseSRWLockExclusive(&failureLock);bool blocked=false;
    // Native geometry can bind more than one stream. These getters exist
    // only during an actual failed upload, never on the normal draw path.
    D3DCAPS9 caps{};unsigned streams=0;
    if(vertex){const auto hr=device->GetDeviceCaps(&caps);if(FAILED(hr)||!caps.MaxStreams||caps.MaxStreams>16)blocked=true;else streams=caps.MaxStreams;}
    for(unsigned stream=0;stream<streams&&!blocked;++stream){IDirect3DVertexBuffer9* bound=nullptr;UINT offset=0,stride=0;
        const auto hr=device->GetStreamSource(stream,&bound,&offset,&stride);
        if(SUCCEEDED(hr)&&bound){blocked=reinterpret_cast<std::uintptr_t>(bound)==vertex;bound->Release();}
        else{if(bound)bound->Release();if(FAILED(hr)){blocked=true;break;}}}
    if(index&&indexed&&!blocked){IDirect3DIndexBuffer9* bound=nullptr;const auto hr=device->GetIndices(&bound);
        if(FAILED(hr))blocked=true;
        else if(bound)blocked=reinterpret_cast<std::uintptr_t>(bound)==index;
        if(bound)bound->Release();}
    if(blocked)++blockedDraws;SetLastError(error);return blocked;
}
Stats Snapshot(){Stats out;out.installed=installed;out.enabled=enabled;out.verified=verified;out.dropped=dropped;
    out.batching=batching;out.on12=on12;out.batchedCalls=batchedCalls;out.savedLocks=savedLocks;
    out.singleMapCalls=singleMapCalls;out.lockFailures=lockFailures;out.unlockFailures=unlockFailures;out.invalidMappings=invalidMappings;
    out.uploadDeferred=uploadDeferred;out.uploadRecovered=uploadRecovered;out.recoveryAttempts=recoveryAttempts;out.recoveryTimeouts=recoveryTimeouts;out.blockedDraws=blockedDraws;
    out.dynamicVertexCreated=dynamicVertexCreated;out.dynamicVertexFallback=dynamicVertexFallback;
    out.lastFailureTick=lastFailureTick;out.lastUploadResult=lastUploadResult;
    AcquireSRWLockShared(&failureLock);for(const auto& old:failures)if(old.resource){++out.pendingUploads;
        if(!out.firstFailureTick||DWORD(out.firstFailureTick-old.firstAt)<0x80000000u)out.firstFailureTick=old.firstAt;}
    ReleaseSRWLockShared(&failureLock);
    out.batchingFaulted=sparseBlocked;
    out.backend=backend.load(std::memory_order_acquire);out.batchingSupported=SupportsBatching(out.backend);
    AcquireSRWLockShared(&activeLock);out.active=active;out.inProgress=inProgress;ReleaseSRWLockShared(&activeLock);
    AcquireSRWLockShared(&eventLock);out.events=eventCount;ReleaseSRWLockShared(&eventLock);
    for(unsigned i=0;i<PhaseCount;++i){const auto& t=totals[i];out.phases[i]={t.calls.load(),t.active.load(),t.totalUs.load(),t.maxUs.load(),t.bytes.load(),t.ranges.load()};}
    return out;}
std::size_t ReadEvents(std::uint64_t after,Event* output,std::size_t capacity,std::uint64_t& overwritten){
    overwritten=0;if(!output||!capacity)return 0;AcquireSRWLockShared(&eventLock);std::size_t count=0;
    if(after<eventCount){const auto oldest=eventCount>events.size()?eventCount-events.size()+1:1;
        const auto first=std::max(after+1,oldest);overwritten=first-after-1;
        count=static_cast<std::size_t>(std::min<std::uint64_t>(capacity,eventCount-first+1));
        for(std::size_t i=0;i<count;++i)output[i]=events[(first+i-1)%events.size()];}
    ReleaseSRWLockShared(&eventLock);return count;
}
bool Shutdown(){enabled=false;if(!installed)return true;
    bool ok=true;for(const auto rva:{FlushRva,BeginSceneRva,StaticVertexCreateRva}){void* target=reinterpret_cast<void*>(imageBase+rva);
        const auto disabled=MH_DisableHook(target);
        const bool safe=disabled==MH_OK||disabled==MH_ERROR_DISABLED||disabled==MH_ERROR_NOT_CREATED;
        if(!safe){ok=false;continue;}
        const auto removed=MH_RemoveHook(target);ok=(removed==MH_OK||removed==MH_ERROR_NOT_CREATED)&&ok;}
    if(ok)installed=false;return ok;
}
}
