#include "background_diagnostic.hpp"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <atomic>
#include <algorithm>
#include <cstring>
#include <cwchar>
#include <vector>
#include "MinHook.h"
#include "d3d9_capture.hpp"
#include "d3d9_slots.hpp"
#include "frame_copy.hpp"
#include "vr_ipc.hpp"
#include "cpu_timer.hpp"
#include "profile_timing.hpp"
#include "d3d9_gpu_timing.hpp"
#include "d3d9_on12_pacing.hpp"
#include "crash_diagnostics.hpp"

namespace {
static_assert(sizeof(void*)==4,"The game capture DLL must be x86");
// IDirect3DDevice9 vtable slots counted from the mingw-w64 d3d9.h this tree
// compiles against: Reset=16, Present=17, SetTransform=44,
// SetVertexShaderConstantF=94. IDirect3DSwapChain9::Present=3.
constexpr unsigned kResetSlot=16,kPresentSlot=17,kEndSceneSlot=42,kSetTransformSlot=44,kSetConstantSlot=94;
constexpr unsigned kDrawPrimitiveSlot=D3D9Slots::DrawPrimitive,kDrawIndexedPrimitiveSlot=D3D9Slots::DrawIndexedPrimitive;
constexpr unsigned kSwapPresentSlot=3;
constexpr unsigned kMaxShaderConstants=256;
// CPU fallback keeps the fixed IPC arena. GPU textures have separate limits.
constexpr UINT kMaxWidth=VRIPC::kMaxWidth,kMaxHeight=VRIPC::kMaxHeight;
constexpr UINT kMaxGPUWidth=VRIPC::kMaxGPUWidth,kMaxGPUHeight=VRIPC::kMaxGPUHeight;
std::atomic<IDirect3DDevice9*> presentationDevice{nullptr};
std::atomic<UINT> presentationInterval{0},presentationBackbuffers{0};
std::atomic<bool> presentationKnown{false},presentationWindowed{false};

using DevicePresent=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,const RECT*,const RECT*,HWND,const RGNDATA*);
using SwapPresent=HRESULT(STDMETHODCALLTYPE*)(IDirect3DSwapChain9*,const RECT*,const RECT*,HWND,const RGNDATA*,DWORD);
using DeviceReset=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*);
using CreateDevice=HRESULT(STDMETHODCALLTYPE*)(IDirect3D9*,UINT,D3DDEVTYPE,HWND,DWORD,D3DPRESENT_PARAMETERS*,IDirect3DDevice9**);
CreateDevice originalCreateDevice=nullptr;
std::atomic<bool> forceImmediate{false},createDeviceHook{false};
std::atomic<std::uint64_t> immediateApplied{0},immediateFallbacks{0};
std::atomic<std::uint64_t> resetCalls{0},resetFailures{0};
std::atomic<HRESULT> lastResetResult{S_OK};
std::atomic<UINT> resetWidth{0},resetHeight{0};
std::atomic<IDirect3DDevice9*> resetPendingDevice{nullptr};
using DeviceEndScene=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*);
using DeviceSetTransform=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DTRANSFORMSTATETYPE,const D3DMATRIX*);
using DeviceSetConstant=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,UINT,const float*,UINT);
using DeviceDrawPrimitive=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,UINT);
using DeviceDrawIndexedPrimitive=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,INT,UINT,UINT,UINT,UINT);
using DeviceDrawPrimitiveUP=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,const void*,UINT);
using DeviceDrawIndexedPrimitiveUP=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,UINT,UINT,const void*,D3DFORMAT,const void*,UINT);
std::atomic<D3D9Capture::DrawBeginCallback> drawBegin{nullptr};
std::atomic<D3D9Capture::DrawEndCallback> drawEnd{nullptr};
std::atomic<D3D9Capture::DrawFilter> drawFilter{nullptr};
std::atomic<D3D9Capture::TargetObserver> targetObserver{nullptr};
std::atomic<D3D9Capture::SkipSwapPresent> skipSwapPresent{nullptr};
std::atomic<bool> targetWatchReady{false},targetWatchAttempted{false};
constexpr unsigned targetSlots[]{37,43,34,35,30,31};
void* targetEntries[std::size(targetSlots)]{};
void** targetTable=nullptr;
HRESULT (STDMETHODCALLTYPE* originalBindTarget)(IDirect3DDevice9*,DWORD,IDirect3DSurface9*)=nullptr;
HRESULT (STDMETHODCALLTYPE* originalClearTarget)(IDirect3DDevice9*,DWORD,const D3DRECT*,DWORD,D3DCOLOR,float,DWORD)=nullptr;
HRESULT (STDMETHODCALLTYPE* originalStretch)(IDirect3DDevice9*,IDirect3DSurface9*,const RECT*,IDirect3DSurface9*,const RECT*,D3DTEXTUREFILTERTYPE)=nullptr;
HRESULT (STDMETHODCALLTYPE* originalColor)(IDirect3DDevice9*,IDirect3DSurface9*,const RECT*,D3DCOLOR)=nullptr;
HRESULT (STDMETHODCALLTYPE* originalUpdateSurface)(IDirect3DDevice9*,IDirect3DSurface9*,const RECT*,IDirect3DSurface9*,const POINT*)=nullptr;
HRESULT (STDMETHODCALLTYPE* originalUpdateTexture)(IDirect3DDevice9*,IDirect3DBaseTexture9*,IDirect3DBaseTexture9*)=nullptr;
void RecordTargetChange(IDirect3DDevice9* device,D3D9Capture::TargetChange change,void* dest=nullptr,unsigned slot=0){
    if(targetWatchReady.load(std::memory_order_acquire))
        if(const auto report=targetObserver.load(std::memory_order_relaxed))report(device,change,dest,slot);
}
HRESULT STDMETHODCALLTYPE BindTargetHook(IDirect3DDevice9* d,DWORD slot,IDirect3DSurface9* surface){
    const auto hr=originalBindTarget(d,slot,surface);
    if(SUCCEEDED(hr))RecordTargetChange(d,D3D9Capture::TargetChange::Bind,surface,slot);
    return hr;
}
HRESULT STDMETHODCALLTYPE ClearTargetHook(IDirect3DDevice9* d,DWORD count,const D3DRECT* rects,DWORD flags,D3DCOLOR color,float z,DWORD stencil){
    if(flags&D3DCLEAR_TARGET)RecordTargetChange(d,D3D9Capture::TargetChange::Clear);
    return originalClearTarget(d,count,rects,flags,color,z,stencil);
}
HRESULT STDMETHODCALLTYPE StretchHook(IDirect3DDevice9* d,IDirect3DSurface9* src,const RECT* from,IDirect3DSurface9* dst,const RECT* to,D3DTEXTUREFILTERTYPE filter){
    RecordTargetChange(d,D3D9Capture::TargetChange::SurfaceWrite,dst);
    return originalStretch(d,src,from,dst,to,filter);
}
HRESULT STDMETHODCALLTYPE ColorHook(IDirect3DDevice9* d,IDirect3DSurface9* dst,const RECT* rect,D3DCOLOR color){
    RecordTargetChange(d,D3D9Capture::TargetChange::SurfaceWrite,dst);
    return originalColor(d,dst,rect,color);
}
HRESULT STDMETHODCALLTYPE UpdateSurfaceHook(IDirect3DDevice9* d,IDirect3DSurface9* src,const RECT* from,IDirect3DSurface9* dst,const POINT* to){
    RecordTargetChange(d,D3D9Capture::TargetChange::SurfaceWrite,dst);
    return originalUpdateSurface(d,src,from,dst,to);
}
HRESULT STDMETHODCALLTYPE UpdateTextureHook(IDirect3DDevice9* d,IDirect3DBaseTexture9* src,IDirect3DBaseTexture9* dst){
    RecordTargetChange(d,D3D9Capture::TargetChange::TextureWrite,dst);
    return originalUpdateTexture(d,src,dst);
}
void InstallTargetWatch(IDirect3DDevice9* device) {
    if(!device||!targetObserver.load()||targetWatchAttempted.exchange(true))return;
    auto** table=*reinterpret_cast<void***>(device);
    void* hooks[]{reinterpret_cast<void*>(&BindTargetHook),reinterpret_cast<void*>(&ClearTargetHook),
        reinterpret_cast<void*>(&StretchHook),reinterpret_cast<void*>(&ColorHook),
        reinterpret_cast<void*>(&UpdateSurfaceHook),reinterpret_cast<void*>(&UpdateTextureHook)};
    void** originals[]{reinterpret_cast<void**>(&originalBindTarget),reinterpret_cast<void**>(&originalClearTarget),
        reinterpret_cast<void**>(&originalStretch),reinterpret_cast<void**>(&originalColor),
        reinterpret_cast<void**>(&originalUpdateSurface),reinterpret_cast<void**>(&originalUpdateTexture)};
    const auto replace=[](void** slot,void* expected,void* value) {
        DWORD old=0;
        if(!VirtualProtect(slot,sizeof(*slot),PAGE_READWRITE,&old))return false;
        const bool changed=InterlockedCompareExchangePointer(slot,value,expected)==expected;
        DWORD unused=0;
        const bool protectedAgain=VirtualProtect(slot,sizeof(*slot),old,&unused)!=FALSE;
        if(changed&&!protectedAgain)InterlockedCompareExchangePointer(slot,expected,value);
        return changed&&protectedAgain;
    };
    void* values[std::size(targetSlots)]{};
    for(unsigned i=0;i<std::size(targetSlots);++i){
        values[i]=table[targetSlots[i]];
        if(!values[i])return;
        *originals[i]=values[i];
    }
    // These methods can share driver stubs, and a discovery device can use
    // a different table. Watch the live presentation class by its typed slots.
    unsigned installedSlots=0;
    for(unsigned i=0;i<std::size(targetSlots);++i){
        if(!replace(&table[targetSlots[i]],values[i],hooks[i]))break;
        targetEntries[i]=hooks[i];++installedSlots;
    }
    if(installedSlots==std::size(targetSlots)){
        targetTable=table;targetWatchReady=true;return;
    }
    while(installedSlots){
        const unsigned i=--installedSlots;
        replace(&table[targetSlots[i]],hooks[i],values[i]);
    }
}
std::atomic<D3D9Capture::DrawTraceCallback> drawTrace{nullptr};
std::atomic<D3D9Capture::DrawCopyCallback> drawCopies{nullptr};

D3D9Capture::FrameCallback callback=nullptr;
D3D9Capture::FrameBoundaryCallback boundaryCallback=nullptr;
D3D9Capture::BeforeCaptureCallback overlayCallback=nullptr;
void* overlayUser=nullptr;
void* callbackUser=nullptr;
D3D9Capture::TransformCallback transformCallback=nullptr;
void* transformUser=nullptr;
D3D9Capture::ConstantCallback constantCallback=nullptr;
D3D9Capture::ConstantInspector constantInspector=nullptr;
D3D9Capture::BeforeResetCallback beforeResetCallback=nullptr;
std::atomic<void(*)(IDirect3DDevice9*)> renderProgress{nullptr};
void* constantUser=nullptr;
SwapPresent originalSwapPresent=nullptr;
DevicePresent originalDevicePresent=nullptr;
DeviceReset originalReset=nullptr;
DeviceEndScene originalEndScene=nullptr;
DeviceSetTransform originalSetTransform=nullptr;
DeviceSetConstant originalSetConstant=nullptr;
DeviceDrawPrimitive originalDrawPrimitive=nullptr;
DeviceDrawIndexedPrimitive originalDrawIndexedPrimitive=nullptr;
DeviceDrawPrimitiveUP originalDrawPrimitiveUP=nullptr;
DeviceDrawIndexedPrimitiveUP originalDrawIndexedPrimitiveUP=nullptr;
std::atomic<bool> installed{false},enabled{false};
std::atomic<bool> cpuScratchRetirementPending{false};
std::atomic<bool> endSceneTrigger{false},discoveringSwapchain{false};
std::atomic<void*> pendingSwapPresent{nullptr},pendingDevicePresent{nullptr};
std::atomic<std::int32_t> halCreateResult{E_FAIL},nullrefCreateResult{E_FAIL},
    swapchainResult{E_FAIL},promotionResult{S_FALSE};
std::atomic<std::uint64_t> endScenes{0};
DWORD lastPromotionTick=0; // worker-owned
std::atomic<bool> presentProbeRequested{false};
std::atomic<unsigned> presentProbeSamples{0},presentProbeSwapChanges{0},presentProbeDeviceChanges{0};
std::atomic<std::int32_t> presentProbeResult{S_FALSE};
DWORD presentProbeWindow=0; // worker-owned
std::uint64_t presentProbeStartPresents=0,presentProbeStartEndScenes=0;
std::atomic<bool> hasSwapHook{false},hasDeviceHook{false},hasResetHook{false},
                  hasTransformHook{false},hasConstantHook{false};
std::atomic<std::uint64_t> presents{0},swapPresents{0},devicePresents{0},resets{0};
std::atomic<std::uint64_t> captured{0},callbackDrops{0},failures{0};
std::atomic<std::uint32_t> lastWidth{0},lastHeight{0},lastFormatCode{0};
std::atomic<std::uint32_t> lastBackbufferWidth{0},lastBackbufferHeight{0};
std::atomic<UINT> lastMultisampleType{0},lastMultisampleQuality{0};
struct CPUStageCounter {std::atomic<std::uint64_t> calls{0},totalUs{0},maxUs{0};};
CPUStageCounter cpuStages[D3D9Capture::CPUStageCount];
auto& readbackCalls=cpuStages[static_cast<unsigned>(D3D9Capture::CPUStage::Readback)].calls;
auto& readbackTotalUs=cpuStages[static_cast<unsigned>(D3D9Capture::CPUStage::Readback)].totalUs;
struct CPUStageTimer {
    D3D9Capture::CPUStage stage;
    std::uint64_t start=0;
    explicit CPUStageTimer(D3D9Capture::CPUStage value):stage(value),start(FrameMetrics::Counter()){}
    ~CPUStageTimer(){
        auto& counter=cpuStages[static_cast<unsigned>(stage)];const auto elapsed=FrameMetrics::Microseconds(start);
        counter.totalUs.fetch_add(elapsed,std::memory_order_relaxed);counter.calls.fetch_add(1,std::memory_order_relaxed);
        auto maximum=counter.maxUs.load(std::memory_order_relaxed);
        while(maximum<elapsed&&!counter.maxUs.compare_exchange_weak(maximum,elapsed,std::memory_order_relaxed)){}
    }
};
template<class Work> decltype(auto) TimedCPU(D3D9Capture::CPUStage stage,Work&& work) {
    CPUStageTimer timer(stage);return work();
}
std::atomic<std::int32_t> lastCaptureResult{S_OK},lastPresentResult{S_OK},installResult{S_OK};
SRWLOCK resourceLock=SRWLOCK_INIT;
SRWLOCK pacingLock=SRWLOCK_INIT;
FramePacing::Window pacing;
FrameEventTrace::Ring frameTrace;
std::atomic<bool> frameTraceEnabled{true};
thread_local std::uint64_t traceFrameId=0;
thread_local unsigned traceEye=0,traceKind=0,traceGate=0;
struct ShaderMarker {std::uint64_t serial=0,timeUs=0,cpuUs=UINT64_MAX;DWORD thread=0;};
ShaderMarker shaderMarker;
struct CameraMarker {std::uint64_t serial=0,timeUs=0;DWORD thread=0;};
CameraMarker cameraMarker;
SRWLOCK shaderMarkerLock=SRWLOCK_INIT;
std::atomic<std::uint64_t> markedShaderSerial{UINT64_MAX};
std::atomic<std::uint64_t> markedCameraSerial{UINT64_MAX};
std::uint64_t ThreadCPU(){FILETIME created{},exited{},kernel{},user{};
    if(!GetThreadTimes(GetCurrentThread(),&created,&exited,&kernel,&user))return UINT64_MAX;
    const auto ticks=[](FILETIME t){return (std::uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime;};
    return (ticks(kernel)+ticks(user))/10;}
void RecordPacing(std::uint64_t start,std::uint64_t nativeStart,std::uint64_t cpu){
    const auto end=FrameMetrics::Microseconds(0);
    ShaderMarker shader;CameraMarker camera;
    AcquireSRWLockShared(&shaderMarkerLock);shader=shaderMarker;camera=cameraMarker;ReleaseSRWLockShared(&shaderMarkerLock);
    AcquireSRWLockExclusive(&pacingLock);pacing.Observe(start,nativeStart,end,cpu);
    if(frameTraceEnabled.load(std::memory_order_relaxed)) {
        FrameEventTrace::Event event;event.startUs=start;event.tick=GetTickCount();event.thread=GetCurrentThreadId();
        event.present=presents.load();event.captured=captured.load();event.drops=callbackDrops.load();event.failures=failures.load();
        event.captureResult=lastCaptureResult.load();event.presentResult=lastPresentResult.load();
        event.sourceFrameId=traceFrameId;event.eye=traceEye;event.kind=traceKind;event.gate=traceGate;
        if(camera.serial==event.present){event.firstCameraUs=camera.timeUs;event.cameraThread=camera.thread;}
        if(shader.serial==event.present){
            event.firstShaderUs=shader.timeUs;event.shaderThread=shader.thread;
            event.shaderCpuKnown=event.thread==shader.thread&&cpu!=UINT64_MAX&&shader.cpuUs!=UINT64_MAX&&cpu>=shader.cpuUs;
            if(event.shaderCpuKnown)event.shaderCpuUs=cpu-shader.cpuUs;
        }
        frameTrace.Push(event,nativeStart,end,cpu);
    }
    ReleaseSRWLockExclusive(&pacingLock);
}
thread_local bool insidePresent=false;
thread_local bool drawingOverlay=false;
IDirect3DDevice9* resourceDevice=nullptr; // owned indirectly by the surfaces
DWORD resourceThread=0; // protected by resourceLock
IDirect3DSurface9* resolveSurface=nullptr;
IDirect3DSurface9* systemSurface=nullptr;
IDirect3DSurface9* lockableResolveSurface=nullptr;
UINT resourceWidth=0,resourceHeight=0;
D3DFORMAT resourceFormat=D3DFMT_UNKNOWN;
D3DMULTISAMPLE_TYPE resourceSamples=D3DMULTISAMPLE_NONE;
DWORD resourceSampleQuality=0;
bool directReadbackDisabled=false,lockableResolveDisabled=false;
std::vector<std::uint8_t> pixels; // non-BGRA8 format conversion scratch
D3D9Capture::GPUCallback gpuCallback=nullptr;

void ObserveDevice(IDirect3DDevice9* device) {
    if(!device||!endSceneTrigger.load()||pendingSwapPresent.load()||
       discoveringSwapchain.exchange(true))return;
    IDirect3DSwapChain9* swapchain=nullptr;
    const HRESULT result=device->GetSwapChain(0,&swapchain);
    swapchainResult=static_cast<std::int32_t>(result);
    if(SUCCEEDED(result)&&swapchain) {
        auto** swapTable=*reinterpret_cast<void***>(swapchain);
        auto** deviceTable=*reinterpret_cast<void***>(device);
        pendingDevicePresent.store(deviceTable[kPresentSlot]);
        pendingSwapPresent.store(swapTable[kSwapPresentSlot]);
    }
    if(swapchain)swapchain->Release();
    discoveringSwapchain=false;
}

template<class T> void Release(T*& resource) {
    if(resource){resource->Release();resource=nullptr;}
}
void ReleaseCaptureResources() {
    Release(systemSurface);
    Release(resolveSurface);
    Release(lockableResolveSurface);
    resourceDevice=nullptr;resourceThread=0;
    resourceWidth=resourceHeight=0;
    resourceFormat=D3DFMT_UNKNOWN;
    resourceSamples=D3DMULTISAMPLE_NONE;
    resourceSampleQuality=0;
    directReadbackDisabled=lockableResolveDisabled=false;
}
void ReleaseResources() {
    presentationDevice=nullptr;presentationKnown=false;
    ReleaseCaptureResources();
}
bool OriginalCaptureDevice(IDirect3DDevice9* device) {
    // These getters are untouched by the capture and reflection hooks.
    constexpr unsigned getCaps=7,getCreationParameters=9;
    auto** table=*reinterpret_cast<void***>(device);
    HMODULE caps=nullptr,creation=nullptr;
    constexpr DWORD flags=GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
    if(!GetModuleHandleExW(flags,reinterpret_cast<LPCWSTR>(table[getCaps]),&caps)||
       !GetModuleHandleExW(flags,reinterpret_cast<LPCWSTR>(table[getCreationParameters]),&creation)||caps!=creation)return false;
    wchar_t path[MAX_PATH]{},directory[MAX_PATH]{};
    const auto length=GetModuleFileNameW(caps,path,MAX_PATH);
    if(!length||length>=MAX_PATH)return false;
    const auto* name=std::wcsrchr(path,L'\\');
    if(!name||_wcsicmp(name+1,L"d3d9.dll"))return false;
    const auto prefix=static_cast<int>(name-path);
    const auto matches=[&](UINT size){return size==static_cast<UINT>(prefix)&&
        CompareStringOrdinal(path,prefix,directory,prefix,TRUE)==CSTR_EQUAL;};
    if(!matches(GetSystemDirectoryW(directory,MAX_PATH))&&!matches(GetSystemWow64DirectoryW(directory,MAX_PATH)))return false;
    // The system DLL can itself dispatch to an On12 device.
    Device9On12* on12=nullptr;
    const auto result=device->QueryInterface(iid9on12,reinterpret_cast<void**>(&on12));
    if(on12){on12->Release();return false;}
    return result==E_NOINTERFACE;
}
unsigned BytesPerPixel(D3DFORMAT format) {
    switch(format) {
    case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8:
    case D3DFMT_A8B8G8R8: case D3DFMT_X8B8G8R8: case D3DFMT_A2R10G10B10: return 4;
    case D3DFMT_R5G6B5: case D3DFMT_A1R5G5B5: case D3DFMT_X1R5G5B5: return 2;
    case D3DFMT_R8G8B8: return 3;
    default: return 0;
    }
}
HRESULT Readback(IDirect3DDevice9* device,IDirect3DSurface9* source,IDirect3DSurface9* destination) {
    return TimedCPU(D3D9Capture::CPUStage::Readback,[&]{return device->GetRenderTargetData(source,destination);});
}
HRESULT Resolve(IDirect3DDevice9* device,IDirect3DSurface9* source,IDirect3DSurface9* destination) {
    return TimedCPU(D3D9Capture::CPUStage::Resolve,[&]{return device->StretchRect(source,nullptr,destination,nullptr,D3DTEXF_NONE);});
}
HRESULT MapSurface(IDirect3DSurface9* surface,D3D9Capture::CPUStage stage,UINT rowBytes,D3DLOCKED_RECT& lock) {
    const auto result=TimedCPU(stage,[&]{return surface->LockRect(&lock,nullptr,D3DLOCK_READONLY);});
    if(FAILED(result))return result;
    if(lock.pBits&&lock.Pitch>=static_cast<INT>(rowBytes))return result;
    const auto unlocked=TimedCPU(D3D9Capture::CPUStage::Unmap,[&]{return surface->UnlockRect();});
    return FAILED(unlocked)?unlocked:D3DERR_INVALIDCALL;
}
HRESULT MapLockableResolve(IDirect3DDevice9* device,IDirect3DSurface9* source,const D3DSURFACE_DESC& description,D3DLOCKED_RECT& lock) {
    if(!lockableResolveSurface){
        if(!OriginalCaptureDevice(device))return D3DERR_NOTAVAILABLE;
        const auto created=device->CreateRenderTarget(description.Width,description.Height,description.Format,
            D3DMULTISAMPLE_NONE,0,TRUE,&lockableResolveSurface,nullptr);
        // An optional lockable allocation must not prevent the established capture path.
        if(created==E_OUTOFMEMORY||created==D3DERR_OUTOFVIDEOMEMORY)return D3DERR_NOTAVAILABLE;
        if(FAILED(created)||!lockableResolveSurface)return FAILED(created)?created:D3DERR_INVALIDCALL;
    }
    const auto result=Resolve(device,source,lockableResolveSurface);
    return FAILED(result)?result:MapSurface(lockableResolveSurface,D3D9Capture::CPUStage::LockableMap,description.Width*4,lock);
}
bool UnsupportedLockableResolve(HRESULT result) {
    return result==D3DERR_INVALIDCALL||result==D3DERR_NOTAVAILABLE||result==E_NOTIMPL;
}
// Normalize every supported backbuffer to opaque BGRA8. Alpha in X8 formats is
// undefined and cannot safely be forwarded to a compositor that blends layers.
void ConvertRow(std::uint8_t* output,const std::uint8_t* input,UINT width,D3DFORMAT format) {
    for(UINT x=0;x<width;++x,output+=4) {
        switch(format) {
        case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8:
            output[0]=input[0];output[1]=input[1];output[2]=input[2];input+=4;break;
        case D3DFMT_A8B8G8R8: case D3DFMT_X8B8G8R8:
            output[0]=input[2];output[1]=input[1];output[2]=input[0];input+=4;break;
        case D3DFMT_R8G8B8:
            output[0]=input[0];output[1]=input[1];output[2]=input[2];input+=3;break;
        case D3DFMT_R5G6B5: {
            std::uint16_t value;std::memcpy(&value,input,sizeof(value));input+=2;
            output[0]=static_cast<std::uint8_t>(((value&31)*255+15)/31);
            output[1]=static_cast<std::uint8_t>((((value>>5)&63)*255+31)/63);
            output[2]=static_cast<std::uint8_t>((((value>>11)&31)*255+15)/31);
            break;
        }
        case D3DFMT_A1R5G5B5: case D3DFMT_X1R5G5B5: {
            std::uint16_t value;std::memcpy(&value,input,sizeof(value));input+=2;
            output[0]=static_cast<std::uint8_t>(((value&31)*255+15)/31);
            output[1]=static_cast<std::uint8_t>((((value>>5)&31)*255+15)/31);
            output[2]=static_cast<std::uint8_t>((((value>>10)&31)*255+15)/31);
            break;
        }
        case D3DFMT_A2R10G10B10: {
            std::uint32_t value;std::memcpy(&value,input,sizeof(value));input+=4;
            output[0]=static_cast<std::uint8_t>(((value&1023)*255+511)/1023);
            output[1]=static_cast<std::uint8_t>((((value>>10)&1023)*255+511)/1023);
            output[2]=static_cast<std::uint8_t>((((value>>20)&1023)*255+511)/1023);
            break;
        }
        default: output[0]=output[1]=output[2]=0;break;
        }
        output[3]=255;
    }
}
HRESULT MapReadback(IDirect3DDevice9* device,IDirect3DSurface9* source,const D3DSURFACE_DESC& description,D3DLOCKED_RECT& lock) {
    const auto width=description.Width,height=description.Height;
    if(!systemSurface){
        const auto created=device->CreateOffscreenPlainSurface(width,height,description.Format,D3DPOOL_SYSTEMMEM,&systemSurface,nullptr);
        if(FAILED(created)||!systemSurface){ReleaseResources();return FAILED(created)?created:D3DERR_INVALIDCALL;}
    }
    const bool direct=!directReadbackDisabled&&description.MultiSampleType==D3DMULTISAMPLE_NONE;
    auto result=direct?Readback(device,source,systemSurface):D3DERR_NOTAVAILABLE;
    if(FAILED(result)){
        if(direct)directReadbackDisabled=true;
        if(!resolveSurface){
            result=device->CreateRenderTarget(width,height,description.Format,D3DMULTISAMPLE_NONE,0,FALSE,&resolveSurface,nullptr);
            if(FAILED(result)||!resolveSurface){Release(resolveSurface);return FAILED(result)?result:D3DERR_INVALIDCALL;}
        }
        result=Resolve(device,source,resolveSurface);
        if(SUCCEEDED(result))result=Readback(device,resolveSurface,systemSurface);
    }
    return FAILED(result)?result:MapSurface(systemSurface,D3D9Capture::CPUStage::Map,width*BytesPerPixel(description.Format),lock);
}

// GPU gets native pixels first; only CPU fallback uses the fixed pixel arena.
HRESULT Capture(IDirect3DDevice9* device,IDirect3DSurface9* backbuffer) {
    const DWORD captureTick=GetTickCount();
    if(presentationDevice.load()!=device){
        IDirect3DSwapChain9* swap=nullptr;D3DPRESENT_PARAMETERS parameters{};
        if(SUCCEEDED(device->GetSwapChain(0,&swap))&&swap){
            if(SUCCEEDED(swap->GetPresentParameters(&parameters))){presentationInterval=parameters.PresentationInterval;
                presentationBackbuffers=parameters.BackBufferCount;presentationWindowed=parameters.Windowed!=FALSE;presentationKnown=true;presentationDevice=device;}
            swap->Release();
        }
    }
    D3DSURFACE_DESC description{};
    HRESULT result=backbuffer->GetDesc(&description);
    if(FAILED(result))return result;
    if(!description.Width||!description.Height||!BytesPerPixel(description.Format))return D3DERR_NOTAVAILABLE;
    lastBackbufferWidth=description.Width;lastBackbufferHeight=description.Height;
    lastMultisampleType=description.MultiSampleType;lastMultisampleQuality=description.MultiSampleQuality;
    lastFormatCode=static_cast<std::uint32_t>(description.Format);
    if(gpuCallback&&description.Width<=kMaxGPUWidth&&description.Height<=kMaxGPUHeight) {
        const FrameCopy::Extent extent{description.Width,description.Height};
        const int published=gpuCallback(device,backbuffer,extent.width,extent.height,captured.load()+1,captureTick,callbackUser);
        if(published>=0) {
            if(published&&(resourceDevice||!pixels.empty())){
                // CPU fallback may have preceded native GPU availability.
                // Once GPU publication succeeds, its cached readback surfaces
                // and conversion scratch must not occupy the x86 heap/VA.
                ReleaseCaptureResources();
                std::vector<std::uint8_t>().swap(pixels);
            }
            lastWidth=extent.width;lastHeight=extent.height;
            if(published)++captured;else ++callbackDrops;
            return published?S_OK:S_FALSE;
        }
    }
    // Native resolution belongs to the game. Never silently scale/crop a
    // source to a transport cap; both paths publish its complete pixel grid.
    if(description.Width>kMaxWidth||description.Height>kMaxHeight)return D3DERR_NOTAVAILABLE;
    const UINT width=description.Width,height=description.Height;
    if(resourceDevice!=device||resourceWidth!=width||resourceHeight!=height||resourceFormat!=description.Format||
       resourceSamples!=description.MultiSampleType||resourceSampleQuality!=description.MultiSampleQuality){
        ReleaseResources();
        resourceDevice=device;resourceThread=GetCurrentThreadId();
        resourceWidth=width;resourceHeight=height;resourceFormat=description.Format;
        resourceSamples=description.MultiSampleType;resourceSampleQuality=description.MultiSampleQuality;
    }
    D3DLOCKED_RECT lock{};
    IDirect3DSurface9* mapped=nullptr;
    const bool bgra=description.Format==D3DFMT_A8R8G8B8||description.Format==D3DFMT_X8R8G8B8;
    if(bgra&&!lockableResolveDisabled){
        result=MapLockableResolve(device,backbuffer,description,lock);
        if(SUCCEEDED(result))mapped=lockableResolveSurface;
        else{
            Release(lockableResolveSurface);
            if(!UnsupportedLockableResolve(result)){ReleaseCaptureResources();return result;}
            lockableResolveDisabled=true;
            lock={};
        }
    }
    if(!mapped){
        result=MapReadback(device,backbuffer,description,lock);
        if(FAILED(result))return result;
        mapped=systemSurface;
    }
    if(bgra) {
        // Borrow the locked surface: the callback copies under the publish
        // lock and returns before UnlockRect.
        lastWidth=width;lastHeight=height;
        const auto frameId=captured.fetch_add(1)+1;
        if(!TimedCPU(D3D9Capture::CPUStage::Publish,[&]{return callback(static_cast<const std::uint8_t*>(lock.pBits),true,width,height,
                     static_cast<std::uint32_t>(lock.Pitch),frameId,captureTick,callbackUser);}))++callbackDrops;
        return TimedCPU(D3D9Capture::CPUStage::Unmap,[&]{return mapped->UnlockRect();});
    }
    try { pixels.resize(static_cast<std::size_t>(width)*height*4); }
    catch(...) { TimedCPU(D3D9Capture::CPUStage::Unmap,[&]{return mapped->UnlockRect();});return E_OUTOFMEMORY; }
    TimedCPU(D3D9Capture::CPUStage::Convert,[&]{for(UINT y=0;y<height;++y) {
        auto* output=pixels.data()+static_cast<std::size_t>(y)*width*4;
        auto* input=static_cast<const std::uint8_t*>(lock.pBits)+static_cast<std::size_t>(y)*lock.Pitch;
        ConvertRow(output,input,width,description.Format);
    }});
    result=TimedCPU(D3D9Capture::CPUStage::Unmap,[&]{return mapped->UnlockRect();});
    if(FAILED(result))return result;
    lastWidth=width;lastHeight=height;
    const auto frameId=captured.fetch_add(1)+1;
    if(!TimedCPU(D3D9Capture::CPUStage::Publish,[&]{return callback(pixels.data(),false,width,height,width*4,frameId,captureTick,callbackUser);}))++callbackDrops;
    return S_OK;
}
void CaptureDevice(IDirect3DDevice9* device,IDirect3DSwapChain9* swapchain) {
    D3D9GPUTiming::End(device,presents.load());
    CrashDiagnostics::Breadcrumb("before_capture",presents.load());
    if(!enabled.load()||!callback)return;
    if(targetObserver.load(std::memory_order_relaxed)&&!targetWatchAttempted.load(std::memory_order_relaxed))
        InstallTargetWatch(device);
    // Failed Reset leaves a lost device. Only the game's successful Reset
    // (or a genuinely new device) can resume capture, never more readbacks.
    AcquireSRWLockExclusive(&resourceLock);
    if(resetPendingDevice.load()==device){lastCaptureResult=lastResetResult.load();ReleaseSRWLockExclusive(&resourceLock);return;}
    IDirect3DSurface9* backbuffer=nullptr;
    HRESULT result=swapchain?swapchain->GetBackBuffer(0,D3DBACKBUFFER_TYPE_MONO,&backbuffer)
                            :device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&backbuffer);
    if(SUCCEEDED(result)&&backbuffer) {
        if(overlayCallback) {
            drawingOverlay=true;
            overlayCallback(device,backbuffer,overlayUser);
            drawingOverlay=false;
        }
        result=Capture(device,backbuffer);
    }
    else if(SUCCEEDED(result))result=D3DERR_INVALIDCALL;
    Release(backbuffer);
    lastCaptureResult=result;
    if(FAILED(result))++failures;
    ReleaseSRWLockExclusive(&resourceLock);
}
void AfterPresent(HRESULT result) {
    lastPresentResult=result;
    const bool deviceLost=result==D3DERR_DEVICELOST||result==D3DERR_DEVICENOTRESET;
    if(deviceLost)D3D9On12Pacing::Reset();
    if(deviceLost||cpuScratchRetirementPending.load(std::memory_order_relaxed)){
        AcquireSRWLockExclusive(&resourceLock);
        if(deviceLost)ReleaseResources();
        // The worker requests retirement. The capture thread owns the D3D9
        // surfaces and releases them after native Present returns.
        if(cpuScratchRetirementPending.load()&&!enabled.load()&&
           (!resourceThread||resourceThread==GetCurrentThreadId())){
            ReleaseCaptureResources();
            std::vector<std::uint8_t>().swap(pixels);
            cpuScratchRetirementPending=false;
        }
        ReleaseSRWLockExclusive(&resourceLock);
    }
}
}

namespace D3D9Capture {
void SetSkipSwapPresent(SkipSwapPresent value){skipSwapPresent.store(value,std::memory_order_release);}
void ConfigureFrameTrace(bool value){frameTraceEnabled.store(value,std::memory_order_relaxed);}
void ConfigureOn12QueuePacing(bool value){D3D9On12Pacing::Configure(value);}
void ObserveNativeCamera(){
    if(!frameTraceEnabled.load(std::memory_order_relaxed))return;
    const auto serial=presents.load(std::memory_order_relaxed)+1;
    if(markedCameraSerial.load(std::memory_order_relaxed)==serial||!TryAcquireSRWLockExclusive(&shaderMarkerLock))return;
    if(markedCameraSerial.load(std::memory_order_relaxed)!=serial){
        cameraMarker={serial,FrameMetrics::Microseconds(0),GetCurrentThreadId()};
        markedCameraSerial.store(serial,std::memory_order_relaxed);
    }
    ReleaseSRWLockExclusive(&shaderMarkerLock);
}
void ObserveFrameShader(){
    if(!frameTraceEnabled.load(std::memory_order_relaxed))return;
    const auto serial=presents.load(std::memory_order_relaxed)+1;
    if(markedShaderSerial.load(std::memory_order_relaxed)==serial||!TryAcquireSRWLockExclusive(&shaderMarkerLock))return;
    if(markedShaderSerial.load(std::memory_order_relaxed)!=serial){
        shaderMarker={serial,FrameMetrics::Microseconds(0),ThreadCPU(),GetCurrentThreadId()};
        markedShaderSerial.store(serial,std::memory_order_relaxed);
    }
    ReleaseSRWLockExclusive(&shaderMarkerLock);
}
void SetFrameTraceSource(std::uint64_t frameId,unsigned eye,unsigned kind,unsigned gate){
    traceFrameId=frameId;traceEye=eye;traceKind=kind;traceGate=gate;
}
std::size_t ReadFrameTrace(std::uint64_t after,FrameEventTrace::Event* output,std::size_t capacity,std::uint64_t& overwritten){
    AcquireSRWLockShared(&pacingLock);const auto count=frameTrace.CopySince(after,output,capacity,overwritten);
    ReleaseSRWLockShared(&pacingLock);return count;
}
void SetGPUCallback(GPUCallback callback){gpuCallback=callback;}
void SetRenderProgressCallback(void(*value)(IDirect3DDevice9*)){renderProgress.store(value,std::memory_order_release);}
void SetDrawCallbacks(DrawBeginCallback begin,DrawEndCallback end){drawEnd.store(end);drawBegin.store(begin);}
void SetTargetObserver(TargetObserver observer){targetObserver=observer;}
bool TargetWritesCovered(IDirect3DDevice9* device){
    if(!device||!targetWatchReady.load(std::memory_order_acquire))return false;
    auto** table=*reinterpret_cast<void***>(device);
    if(table!=targetTable)return false;
    for(unsigned i=0;i<std::size(targetSlots);++i)if(table[targetSlots[i]]!=targetEntries[i])return false;
    return true;
}
void SetDrawTraceCallback(DrawTraceCallback value){drawTrace.store(value);}
void SetDrawCopyCallback(DrawCopyCallback value){drawCopies=value;}
HRESULT ReplayNativeDraw(IDirect3DDevice9* device,const DrawCall& call){
 if(call.kind==0&&originalDrawPrimitive)return originalDrawPrimitive(device,D3DPRIMITIVETYPE(call.type),call.first,call.count);
 if(call.kind==1&&originalDrawIndexedPrimitive)return originalDrawIndexedPrimitive(device,D3DPRIMITIVETYPE(call.type),call.baseVertex,call.minVertex,call.vertices,call.first,call.count);
 return D3DERR_INVALIDCALL;
}
void SetDrawFilter(DrawFilter filter){drawFilter.store(filter);}

HRESULT STDMETHODCALLTYPE DrawPrimitiveHook(IDirect3DDevice9* device,D3DPRIMITIVETYPE type,UINT first,UINT count) {
    if(drawingOverlay)return originalDrawPrimitive(device,type,first,count);
    DrawCall call;call.type=type;call.first=first;call.count=count;
    if(const auto copies=drawCopies.load(std::memory_order_relaxed))copies(device,call);
    const auto filter=drawFilter.load(std::memory_order_relaxed);if(filter&&filter(device,false))return D3D_OK;
    const auto begin=drawBegin.load(std::memory_order_relaxed);const auto end=drawEnd.load(std::memory_order_relaxed);
    void* token=begin&&end?begin(device,call):nullptr;HRESULT result;
    const auto trace=drawTrace.load(std::memory_order_relaxed);if(trace)trace(device,call,false,S_OK);
    {ProfileTiming::Scope native(ProfileTiming::NativeDraw);result=originalDrawPrimitive(device,type,first,count);}
    if(trace)trace(device,call,true,result);
    return token?end(token,result):result;
}
HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveHook(IDirect3DDevice9* device,D3DPRIMITIVETYPE type,INT base,UINT min,UINT vertices,UINT first,UINT count) {
    if(drawingOverlay)return originalDrawIndexedPrimitive(device,type,base,min,vertices,first,count);
    DrawCall call;call.kind=1;call.type=type;call.baseVertex=base;call.minVertex=min;call.vertices=vertices;call.first=first;call.count=count;
    if(const auto copies=drawCopies.load(std::memory_order_relaxed))copies(device,call);
    const auto filter=drawFilter.load(std::memory_order_relaxed);if(filter&&filter(device,true))return D3D_OK;
    const auto begin=drawBegin.load(std::memory_order_relaxed);const auto end=drawEnd.load(std::memory_order_relaxed);
    void* token=begin&&end?begin(device,call):nullptr;HRESULT result;
    const auto trace=drawTrace.load(std::memory_order_relaxed);if(trace)trace(device,call,false,S_OK);
    {ProfileTiming::Scope native(ProfileTiming::NativeDraw);result=originalDrawIndexedPrimitive(device,type,base,min,vertices,first,count);}
    if(trace)trace(device,call,true,result);
    return token?end(token,result):result;
}

HRESULT STDMETHODCALLTYPE DrawPrimitiveUPHook(IDirect3DDevice9* device,D3DPRIMITIVETYPE type,UINT count,const void* data,UINT stride) {
    if(drawingOverlay)return originalDrawPrimitiveUP(device,type,count,data,stride);
    // UP previously had no adapters/filter. Keep that rendering behavior.
    HRESULT result;
    DrawCall call;call.kind=2;call.type=type;call.count=count;call.stride=stride;call.vertexData=reinterpret_cast<std::uintptr_t>(data);
    const auto trace=drawTrace.load(std::memory_order_relaxed);if(trace)trace(device,call,false,S_OK);
    {ProfileTiming::Scope native(ProfileTiming::NativeDraw);result=originalDrawPrimitiveUP(device,type,count,data,stride);}
    if(trace)trace(device,call,true,result);
    return result;
}
HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUPHook(IDirect3DDevice9* device,D3DPRIMITIVETYPE type,UINT min,UINT vertices,UINT count,const void* indices,D3DFORMAT format,const void* data,UINT stride) {
    if(drawingOverlay)return originalDrawIndexedPrimitiveUP(device,type,min,vertices,count,indices,format,data,stride);
    HRESULT result;
    DrawCall call;call.kind=3;call.type=type;call.minVertex=min;call.vertices=vertices;call.count=count;call.stride=stride;call.indexFormat=format;
    call.vertexData=reinterpret_cast<std::uintptr_t>(data);call.indexData=reinterpret_cast<std::uintptr_t>(indices);
    const auto trace=drawTrace.load(std::memory_order_relaxed);if(trace)trace(device,call,false,S_OK);
    {ProfileTiming::Scope native(ProfileTiming::NativeDraw);result=originalDrawIndexedPrimitiveUP(device,type,min,vertices,count,indices,format,data,stride);}
    if(trace)trace(device,call,true,result);
    return result;
}

HRESULT STDMETHODCALLTYPE SetTransformHook(IDirect3DDevice9* device,D3DTRANSFORMSTATETYPE state,
                                           const D3DMATRIX* matrix) {
    ObserveDevice(device);
    // K&L's Glacier may drive the camera either through D3DTS_VIEW or through
    // per-object WORLD matrices (as Contracts does); forward VIEW, PROJECTION
    // and WORLD so the module can steer whichever carries the camera.
    if(!drawingOverlay&&transformCallback&&matrix&&(state==D3DTS_VIEW||state==D3DTS_PROJECTION||state==D3DTS_WORLD)) {
        const int kind=state==D3DTS_VIEW?0:state==D3DTS_PROJECTION?1:2;
        D3DMATRIX modified=*matrix;
        if(transformCallback(kind,reinterpret_cast<float*>(&modified),transformUser))
            return originalSetTransform(device,state,&modified);
    }
    return originalSetTransform(device,state,matrix);
}

HRESULT STDMETHODCALLTYPE SetConstantHook(IDirect3DDevice9* device,UINT startRegister,
                                          const float* data,UINT count) {
    ObserveDevice(device);
    const auto native=[&](const float* values){
        ProfileTiming::Scope nativeProfile(ProfileTiming::NativeShaderUpload);
        return originalSetConstant(device,startRegister,values,count);
    };
    if(!drawingOverlay){
        if(const auto progress=renderProgress.load(std::memory_order_relaxed))progress(device);
        D3D9Capture::ObserveFrameShader();D3D9GPUTiming::Begin(device,presents.load()+1);
    }
    if(!drawingOverlay&&constantCallback&&data&&count>=4&&count<=kMaxShaderConstants&&
       (!constantInspector||constantInspector(device,startRegister,data,count,constantUser))) {
        float rows[kMaxShaderConstants*4];
        std::memcpy(rows,data,count*16);
        if(constantCallback(device,startRegister,rows,static_cast<unsigned>(count),constantUser))
            return native(rows);
    }
    return native(data);
}

void ObservePresentTarget(IDirect3DDevice9* device) {
    if(!presentProbeRequested.load(std::memory_order_relaxed)||!presentProbeRequested.exchange(false))return;
    IDirect3DSwapChain9* swapchain=nullptr;
    const auto result=device->GetSwapChain(0,&swapchain);
    presentProbeResult=SUCCEEDED(result)&&!swapchain?D3DERR_INVALIDCALL:result;
    if(SUCCEEDED(result)&&swapchain){
        if((*reinterpret_cast<void***>(swapchain))[kSwapPresentSlot]!=pendingSwapPresent.load())++presentProbeSwapChanges;
        if((*reinterpret_cast<void***>(device))[kPresentSlot]!=pendingDevicePresent.load())++presentProbeDeviceChanges;
    }
    Release(swapchain);
    ++presentProbeSamples;
}

void SchedulePresentTargetProbe(DWORD now) {
    // Failed exclusive-mode HAL bootstrap only. Probe a live EndScene thread
    // after sustained drawing without Present, without replacing active hooks.
    if(!enabled.load()||!frameTraceEnabled.load(std::memory_order_relaxed)||
       promotionResult.load()!=S_OK||presentProbeSamples.load()>=8){
        presentProbeRequested=false;presentProbeWindow=0;return;
    }
    if(presentProbeWindow&&now-presentProbeWindow<2000)return;
    const auto frames=presents.load(),scenes=endScenes.load();
    presentProbeRequested=presentProbeWindow&&frames==presentProbeStartPresents&&scenes>presentProbeStartEndScenes;
    presentProbeWindow=now;presentProbeStartPresents=frames;presentProbeStartEndScenes=scenes;
}

HRESULT STDMETHODCALLTYPE EndSceneHook(IDirect3DDevice9* device) {
    ++endScenes;
    ObservePresentTarget(device);
    ObserveDevice(device);
    // Discovery only. EndScene may occur several times per final game frame;
    // capturing here would advance an eye twice when Poll promotes Present.
    return originalEndScene(device);
}

HRESULT STDMETHODCALLTYPE DevicePresentHook(IDirect3DDevice9* device,const RECT* source,const RECT* dest,
                                            HWND window,const RGNDATA* dirty) {
    ++devicePresents;
    if(insidePresent)return originalDevicePresent(device,source,dest,window,dirty);
    insidePresent=true;++presents;traceFrameId=0;traceEye=traceKind=traceGate=0;
    const auto start=FrameMetrics::Microseconds(0);
    const auto cpu=ThreadCPU();
    CaptureDevice(device,nullptr);
    const bool queuePaced=D3D9On12Pacing::BeforePresent(device,enabled.load()&&(traceKind==VRIPC::kSceneFrame||traceKind==VRIPC::kScopeFrame));
    const auto nativeStart=FrameMetrics::Microseconds(0);
    CrashDiagnostics::Breadcrumb("native_present_begin",presents.load());
    const HRESULT result=originalDevicePresent(device,source,dest,window,dirty);
    CrashDiagnostics::Breadcrumb("native_present_end",presents.load(),static_cast<unsigned>(result));
    RecordPacing(start,nativeStart,cpu);
    if(SUCCEEDED(result)&&queuePaced){ProfileTiming::Scope pacing(ProfileTiming::On12QueuePacing);D3D9On12Pacing::AfterPresent(device,true);}
    if(boundaryCallback)boundaryCallback(callbackUser);
    AfterPresent(result);insidePresent=false;
    return result;
}

HRESULT STDMETHODCALLTYPE SwapPresentHook(IDirect3DSwapChain9* swapchain,const RECT* source,const RECT* dest,
                                         HWND window,const RGNDATA* dirty,DWORD flags) {
    ++swapPresents;
    if(insidePresent)return originalSwapPresent(swapchain,source,dest,window,dirty,flags);
    insidePresent=true;++presents;traceFrameId=0;traceEye=traceKind=traceGate=0;
    const auto start=FrameMetrics::Microseconds(0);
    const auto cpu=ThreadCPU();
    const auto suppress=skipSwapPresent.load(std::memory_order_acquire);
    const auto caller=reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
    IDirect3DDevice9* pacingDevice=nullptr;bool queuePaced=false,present=true;
    if(enabled.load()) {
        IDirect3DDevice9* device=nullptr;
        const HRESULT getDevice=swapchain->GetDevice(&device);
        if(SUCCEEDED(getDevice)&&device){CaptureDevice(device,swapchain);
            present=!suppress||!suppress(caller);
            if(present)queuePaced=D3D9On12Pacing::BeforePresent(device,traceKind==VRIPC::kSceneFrame||traceKind==VRIPC::kScopeFrame);
            if(queuePaced){pacingDevice=device;pacingDevice->AddRef();}}
        else {lastCaptureResult=FAILED(getDevice)?getDevice:D3DERR_INVALIDCALL;++failures;}
        Release(device);
    }
    const auto nativeStart=FrameMetrics::Microseconds(0);
    CrashDiagnostics::Breadcrumb("native_present_begin",presents.load());
    const HRESULT result=present?originalSwapPresent(swapchain,source,dest,window,dirty,flags):D3D_OK;
    CrashDiagnostics::Breadcrumb("native_present_end",presents.load(),static_cast<unsigned>(result));
    RecordPacing(start,nativeStart,cpu);
    if(SUCCEEDED(result)&&queuePaced){ProfileTiming::Scope pacing(ProfileTiming::On12QueuePacing);D3D9On12Pacing::AfterPresent(pacingDevice,true);}
    Release(pacingDevice);
    if(boundaryCallback)boundaryCallback(callbackUser);
    AfterPresent(result);insidePresent=false;
    return result;
}

HRESULT STDMETHODCALLTYPE CreateDeviceHook(IDirect3D9* api,UINT adapter,D3DDEVTYPE type,HWND window,
                                           DWORD flags,D3DPRESENT_PARAMETERS* parameters,IDirect3DDevice9** output) {
    if(!forceImmediate||type!=D3DDEVTYPE_HAL||!parameters||parameters->PresentationInterval==D3DPRESENT_INTERVAL_IMMEDIATE)
        return originalCreateDevice(api,adapter,type,window,flags,parameters,output);
    auto requested=*parameters;requested.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    const auto result=originalCreateDevice(api,adapter,type,window,flags,&requested,output);
    if(SUCCEEDED(result)){*parameters=requested;++immediateApplied;return result;}
    ++immediateFallbacks;return originalCreateDevice(api,adapter,type,window,flags,parameters,output);
}
HRESULT STDMETHODCALLTYPE ResetHook(IDirect3DDevice9* device,D3DPRESENT_PARAMETERS* parameters) {
    ++resets;
    resetPendingDevice=device;
    resetWidth=parameters?parameters->BackBufferWidth:0;resetHeight=parameters?parameters->BackBufferHeight:0;
    AcquireSRWLockExclusive(&resourceLock);
    D3D9On12Pacing::Reset();
    D3D9GPUTiming::Reset();
    // Wait for the ENTIRE in-flight capture before retiring its GPU source.
    // Keep the same resource->GPU lock order as CaptureDevice. Otherwise a
    // late capture can recreate a generation after the reset callback.
    if(beforeResetCallback)beforeResetCallback(device);
    // Default-pool surfaces must be released before Reset or Reset fails.
    ReleaseResources();
    ReleaseSRWLockExclusive(&resourceLock);
    presentationDevice=nullptr;presentationKnown=false;
    HRESULT result;
    if(forceImmediate&&parameters&&parameters->PresentationInterval!=D3DPRESENT_INTERVAL_IMMEDIATE) {
        auto requested=*parameters;requested.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
        ++resetCalls;result=originalReset(device,&requested);
        if(SUCCEEDED(result)){*parameters=requested;++immediateApplied;}
        // Only an unsupported interval warrants retry. Do not perform a
        // second Reset after OOM/device removal or hide its first HRESULT.
        else if(result==D3DERR_INVALIDCALL){++immediateFallbacks;++resetCalls;result=originalReset(device,parameters);}
    }else{++resetCalls;result=originalReset(device,parameters);}
    lastResetResult=result;
    if(SUCCEEDED(result))resetPendingDevice=nullptr;else ++resetFailures;
    return result;
}

bool Install(FrameCallback onFrame,void* user,FrameBoundaryCallback onBoundary,
             TransformCallback onTransform,void* onTransformUser,
             ConstantCallback onConstants,void* onConstantUser,BeforeCaptureCallback onOverlay,void* onOverlayUser,
             BeforeResetCallback beforeReset,ConstantInspector inspectConstants) {
    if(installed.load())return true;
    if(!onFrame){installResult=E_INVALIDARG;return false;}
    installResult=S_OK;
    // The game imports d3d9.dll directly, so it is already loaded; resolve
    // Direct3DCreate9 dynamically to avoid an import-library dependency.
    HMODULE module=GetModuleHandleW(L"d3d9.dll");
    if(BackgroundDiagnostic::Active()){
        wchar_t root[32768]{};
        wchar_t renderer[32]{};GetEnvironmentVariableW(L"KL1_BACKGROUND_RENDERER",renderer,32);
        if(!_wcsicmp(renderer,L"Original"))module=LoadLibraryExW(L"d3d9.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        else if(GetEnvironmentVariableW(L"KL1_BACKGROUND_RUN",root,32768))module=LoadLibraryW((std::wstring(root)+L"\\d3d9.dll").c_str());
        else module=nullptr;
        if(!module){installResult=E_FAIL;return false;}
    }
    if(!module)module=LoadLibraryW(L"d3d9.dll");
    if(!module){installResult=HRESULT_FROM_WIN32(GetLastError());return false;}
    const auto create9=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(module,"Direct3DCreate9"));
    if(!create9){installResult=E_FAIL;return false;}
    IDirect3DDevice9* actualBackgroundDevice=nullptr;
    if(BackgroundDiagnostic::Active()){
        wchar_t broker[4]{};
        if(GetEnvironmentVariableW(L"KL1_BACKGROUND_RENDERER_BROKER",broker,4)&&broker[0]==L'1'){
            const auto shim=GetModuleHandleW(L"KL1Background.dll");
            const auto take=shim?reinterpret_cast<IDirect3DDevice9*(*)()>(GetProcAddress(shim,"KL1BackgroundTakeDevice")):nullptr;
            actualBackgroundDevice=take?take():nullptr;
            if(!actualBackgroundDevice){installResult=E_PENDING;return false;}
        }
    }
    const wchar_t* className=L"KaneLynchVRD3D9Probe";
    HINSTANCE instance=GetModuleHandleW(nullptr);
    bool registered=false;HWND window=nullptr;
    if(!actualBackgroundDevice){
        WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=instance;wc.lpszClassName=className;
        registered=RegisterClassW(&wc)!=0;
        if(!registered&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS){installResult=HRESULT_FROM_WIN32(GetLastError());return false;}
        window=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,className,L"",WS_OVERLAPPED,0,0,16,16,nullptr,nullptr,instance,nullptr);
    }
    IDirect3D9* d3d=nullptr;
    IDirect3DDevice9* device=actualBackgroundDevice;
    IDirect3DSwapChain9* swapchain=nullptr;
    HRESULT result=actualBackgroundDevice||window?S_OK:HRESULT_FROM_WIN32(GetLastError());
    if(SUCCEEDED(result)&&!device)d3d=create9(D3D_SDK_VERSION);
    if(device){halCreateResult=S_OK;result=device->GetSwapChain(0,&swapchain);swapchainResult=static_cast<std::int32_t>(result);}
    else if(d3d) {
        D3DDISPLAYMODE mode{};
        result=d3d->GetAdapterDisplayMode(D3DADAPTER_DEFAULT,&mode);
        if(FAILED(result))mode.Format=D3DFMT_X8R8G8B8; // early-startup fallback
        {
            D3DPRESENT_PARAMETERS pp{};
            pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
            pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=16;
            pp.BackBufferFormat=mode.Format;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
            result=d3d->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,
                                     D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED|D3DCREATE_NOWINDOWCHANGES,
                                     &pp,&device);
            halCreateResult=static_cast<std::int32_t>(result);
            if(FAILED(result)) {
                // A NULLREF device does not use the adapter/display format.
                // Reusing the fullscreen desktop format fails INVALIDCALL
                // on the user's live KL1 session; UNKNOWN permits bootstrap.
                pp.BackBufferFormat=D3DFMT_UNKNOWN;
                result=d3d->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_NULLREF,window,
                                         D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED|D3DCREATE_NOWINDOWCHANGES,
                                         &pp,&device);
                nullrefCreateResult=static_cast<std::int32_t>(result);
            }
        }
        if(SUCCEEDED(result)&&device) {
            const HRESULT swapResult=device->GetSwapChain(0,&swapchain);
            swapchainResult=static_cast<std::int32_t>(swapResult);
            // NULLREF intentionally has no swapchain. Its device methods
            // bootstrap discovery of the real game's HAL Present methods.
            if(FAILED(swapResult))Release(swapchain);
        }
    } else if(SUCCEEDED(result))result=E_FAIL;
    void* targets[10]{};
    void* detours[10]{};
    void** originals[10]{};
    unsigned hookCount=0;
    bool created[10]{},queued[10]{};
    if(SUCCEEDED(result)&&device&&swapchain) {
        auto** deviceTable=*reinterpret_cast<void***>(device);
        auto** swapTable=*reinterpret_cast<void***>(swapchain);
        targets[0]=swapTable[kSwapPresentSlot];detours[0]=reinterpret_cast<void*>(&SwapPresentHook);
        originals[0]=reinterpret_cast<void**>(&originalSwapPresent);
        targets[1]=deviceTable[kPresentSlot];detours[1]=reinterpret_cast<void*>(&DevicePresentHook);
        originals[1]=reinterpret_cast<void**>(&originalDevicePresent);
        targets[2]=deviceTable[kResetSlot];detours[2]=reinterpret_cast<void*>(&ResetHook);
        originals[2]=reinterpret_cast<void**>(&originalReset);
        targets[3]=deviceTable[kSetTransformSlot];detours[3]=reinterpret_cast<void*>(&SetTransformHook);
        originals[3]=reinterpret_cast<void**>(&originalSetTransform);
        targets[4]=deviceTable[kSetConstantSlot];detours[4]=reinterpret_cast<void*>(&SetConstantHook);
        originals[4]=reinterpret_cast<void**>(&originalSetConstant);
        hookCount=5;endSceneTrigger=false;
    } else if(SUCCEEDED(result)&&device) {
        auto** deviceTable=*reinterpret_cast<void***>(device);
        targets[0]=deviceTable[kEndSceneSlot];detours[0]=reinterpret_cast<void*>(&EndSceneHook);
        originals[0]=reinterpret_cast<void**>(&originalEndScene);
        targets[1]=deviceTable[kResetSlot];detours[1]=reinterpret_cast<void*>(&ResetHook);
        originals[1]=reinterpret_cast<void**>(&originalReset);
        targets[2]=deviceTable[kSetTransformSlot];detours[2]=reinterpret_cast<void*>(&SetTransformHook);
        originals[2]=reinterpret_cast<void**>(&originalSetTransform);
        targets[3]=deviceTable[kSetConstantSlot];detours[3]=reinterpret_cast<void*>(&SetConstantHook);
        originals[3]=reinterpret_cast<void**>(&originalSetConstant);
        hookCount=4;endSceneTrigger=true;
    } else if(SUCCEEDED(result))result=E_FAIL;
    if(SUCCEEDED(result)&&hookCount) {
        auto** deviceTable=*reinterpret_cast<void***>(device);
        targets[hookCount]=deviceTable[kDrawPrimitiveSlot];detours[hookCount]=reinterpret_cast<void*>(&DrawPrimitiveHook);
        originals[hookCount++]=reinterpret_cast<void**>(&originalDrawPrimitive);
        targets[hookCount]=deviceTable[kDrawIndexedPrimitiveSlot];detours[hookCount]=reinterpret_cast<void*>(&DrawIndexedPrimitiveHook);
        originals[hookCount++]=reinterpret_cast<void**>(&originalDrawIndexedPrimitive);
        targets[hookCount]=deviceTable[D3D9Slots::DrawPrimitiveUP];detours[hookCount]=reinterpret_cast<void*>(&DrawPrimitiveUPHook);
        originals[hookCount++]=reinterpret_cast<void**>(&originalDrawPrimitiveUP);
        targets[hookCount]=deviceTable[D3D9Slots::DrawIndexedPrimitiveUP];detours[hookCount]=reinterpret_cast<void*>(&DrawIndexedPrimitiveUPHook);
        originals[hookCount++]=reinterpret_cast<void**>(&originalDrawIndexedPrimitiveUP);
    }
    if(SUCCEEDED(result)&&hookCount&&forceImmediate&&d3d) {
        targets[hookCount]=(*reinterpret_cast<void***>(d3d))[16];
        detours[hookCount]=reinterpret_cast<void*>(&CreateDeviceHook);
        originals[hookCount]=reinterpret_cast<void**>(&originalCreateDevice);++hookCount;
    }
    if(SUCCEEDED(result)&&hookCount) {
        for(unsigned i=0;i<hookCount;++i) {
            MH_STATUS status=MH_CreateHook(targets[i],detours[i],originals[i]);
            if(status!=MH_OK){result=E_FAIL;installResult=static_cast<std::int32_t>(status);break;}
            created[i]=true;
        }
        if(SUCCEEDED(result)) {
            callback=onFrame;callbackUser=user;boundaryCallback=onBoundary;
            overlayCallback=onOverlay;overlayUser=onOverlayUser;
            transformCallback=onTransform;transformUser=onTransformUser;
            constantCallback=onConstants;constantUser=onConstantUser;constantInspector=inspectConstants;beforeResetCallback=beforeReset;
            for(unsigned i=0;i<hookCount;++i) {
                MH_STATUS status=MH_QueueEnableHook(targets[i]);
                if(status!=MH_OK){result=E_FAIL;installResult=static_cast<std::int32_t>(status);break;}
                queued[i]=true;
            }
            if(SUCCEEDED(result)) {
                MH_STATUS status=MH_ApplyQueued();
                if(status!=MH_OK){result=E_FAIL;installResult=static_cast<std::int32_t>(status);}
            }
        }
        if(FAILED(result)) {
            for(unsigned i=0;i<hookCount;++i) {
                if(created[i]){if(queued[i])MH_DisableHook(targets[i]);MH_RemoveHook(targets[i]);}
                if(originals[i])*originals[i]=nullptr;
            }
            callback=nullptr;callbackUser=nullptr;boundaryCallback=nullptr;
            overlayCallback=nullptr;
            transformCallback=nullptr;transformUser=nullptr;
            constantCallback=nullptr;constantUser=nullptr;constantInspector=nullptr;beforeResetCallback=nullptr;overlayCallback=nullptr;overlayUser=nullptr;
        }
    }
    Release(swapchain);Release(device);Release(d3d);
    if(window)DestroyWindow(window);
    if(registered)UnregisterClassW(className,instance);
    if(FAILED(result)){if(installResult.load()==S_OK)installResult=result;return false;}
    hasSwapHook=hasDeviceHook=!endSceneTrigger.load();hasResetHook=true;
    hasTransformHook=hasConstantHook=true;
    createDeviceHook=originalCreateDevice!=nullptr;
    installResult=S_OK;installed=true;
    return true;
}

void SetEnabled(bool value) {
    const bool next=value&&installed.load();
    if(enabled.exchange(next)&&!next)cpuScratchRetirementPending=true;
    if(next)cpuScratchRetirementPending=false;
}
void SetForceImmediate(bool value) {forceImmediate=value;}

void Poll() {
    if(!installed.load())return;
    if(!endSceneTrigger.load()){
        if(pendingSwapPresent.load(std::memory_order_relaxed))SchedulePresentTargetProbe(GetTickCount());
        return;
    }
    void* swap=pendingSwapPresent.load();
    void* present=pendingDevicePresent.load();
    if(!swap||!present)return;
    const DWORD now=GetTickCount();
    if(lastPromotionTick&&now-lastPromotionTick<500)return;
    lastPromotionTick=now;
    MH_STATUS status=MH_CreateHook(swap,reinterpret_cast<void*>(&SwapPresentHook),
        reinterpret_cast<void**>(&originalSwapPresent));
    if(status!=MH_OK){promotionResult=static_cast<std::int32_t>(status);return;}
    status=MH_CreateHook(present,reinterpret_cast<void*>(&DevicePresentHook),
        reinterpret_cast<void**>(&originalDevicePresent));
    if(status!=MH_OK) {
        MH_RemoveHook(swap);originalSwapPresent=nullptr;
        promotionResult=static_cast<std::int32_t>(status);return;
    }
    status=MH_QueueEnableHook(swap);
    if(status==MH_OK)status=MH_QueueEnableHook(present);
    if(status==MH_OK)status=MH_ApplyQueued();
    if(status!=MH_OK) {
        MH_DisableHook(swap);MH_DisableHook(present);
        MH_RemoveHook(swap);MH_RemoveHook(present);
        originalSwapPresent=nullptr;originalDevicePresent=nullptr;
        promotionResult=static_cast<std::int32_t>(status);return;
    }
    hasSwapHook=hasDeviceHook=true;promotionResult=S_OK;
    endSceneTrigger=false;
}

Stats Snapshot(bool includePacing) {
    Stats stats{};
    stats.installed=installed.load();
    stats.enabled=enabled.load();
    stats.presentHook=hasDeviceHook.load()||hasSwapHook.load();
    stats.resetHook=hasResetHook.load();
    stats.transformHook=hasTransformHook.load();
    stats.constantHook=hasConstantHook.load();
    stats.endSceneTrigger=endSceneTrigger.load();
    stats.halCreateResult=halCreateResult.load();stats.nullrefCreateResult=nullrefCreateResult.load();
    stats.swapchainResult=swapchainResult.load();stats.promotionResult=promotionResult.load();
    stats.endScenes=endScenes.load();
    stats.presentProbeSamples=presentProbeSamples.load();stats.presentProbeResult=presentProbeResult.load();
    stats.presentProbeSwapChanges=presentProbeSwapChanges.load();stats.presentProbeDeviceChanges=presentProbeDeviceChanges.load();
    stats.presents=presents.load();
    stats.swapPresents=swapPresents.load();
    stats.devicePresents=devicePresents.load();
    stats.resets=resets.load();
    stats.resetCalls=resetCalls.load();stats.resetFailures=resetFailures.load();stats.lastResetResult=lastResetResult.load();
    stats.resetWidth=resetWidth.load();stats.resetHeight=resetHeight.load();stats.resetPending=resetPendingDevice.load()!=nullptr;
    stats.captured=captured.load();
    stats.callbackDrops=callbackDrops.load();
    stats.failures=failures.load();
    stats.width=lastWidth.load();
    stats.height=lastHeight.load();
    stats.formatCode=lastFormatCode.load();
    stats.backbufferWidth=lastBackbufferWidth.load();stats.backbufferHeight=lastBackbufferHeight.load();
    stats.renderWidth=stats.backbufferWidth;stats.renderHeight=stats.backbufferHeight;
    stats.multisampleType=lastMultisampleType.load();stats.multisampleQuality=lastMultisampleQuality.load();
    stats.readbackCalls=readbackCalls.load(std::memory_order_relaxed);
    stats.readbackMeanMs=stats.readbackCalls?double(readbackTotalUs.load(std::memory_order_relaxed))/stats.readbackCalls/1000.:0.;
    for(unsigned stage=0;stage<D3D9Capture::CPUStageCount;++stage) {
        stats.cpuStages[stage]={cpuStages[stage].calls.load(std::memory_order_relaxed),
            cpuStages[stage].totalUs.load(std::memory_order_relaxed),cpuStages[stage].maxUs.load(std::memory_order_relaxed)};
    }
    stats.forceImmediate=forceImmediate;stats.createDeviceHook=createDeviceHook;
    stats.immediateApplied=immediateApplied;stats.immediateFallbacks=immediateFallbacks;
    stats.presentationKnown=presentationKnown;stats.presentationInterval=presentationInterval;
    stats.backbufferCount=presentationBackbuffers;stats.windowed=presentationWindowed;
    stats.lastCaptureResult=lastCaptureResult.load();
    stats.lastPresentResult=lastPresentResult.load();
    stats.installResult=installResult.load();
    stats.queuePacing=D3D9On12Pacing::Snapshot();
    if(includePacing){
        AcquireSRWLockShared(&pacingLock);const auto window=pacing;ReleaseSRWLockShared(&pacingLock);
        stats.pacing=window.Snapshot();
    }
    return stats;
}
}
