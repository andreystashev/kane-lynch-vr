// Exercise the production Capture function with a bounded COM ABI fixture.
// No real graphics allocation, game, host or headset is opened.
#include "capture_frame_policy.hpp"
#include "d3d9_capture.cpp"
#include <cstdio>
namespace {
unsigned checks=0,errors=0,allocationAttempts=0,gpuCalls=0;
int outcome=0;
unsigned drawBegins=0,drawEnds=0,nativeDraws=0;bool useToken=true,activeToken=false;
unsigned filterCalls=0;bool suppressDraw=false;
IDirect3DDevice9* expectedDevice=nullptr;HRESULT drawResult=S_OK;
void Check(bool value,const char* what){++checks;if(!value){++errors;std::printf("FAIL: %s\n",what);}}
HRESULT STDMETHODCALLTYPE NoSwap(IDirect3DDevice9*,UINT,IDirect3DSwapChain9** out){*out=nullptr;return E_FAIL;}
HRESULT STDMETHODCALLTYPE Allocation(IDirect3DDevice9*,UINT width,UINT height,D3DFORMAT format,D3DPOOL pool,IDirect3DSurface9**,HANDLE*){
    ++allocationAttempts;Check(width==3840&&height==2160&&format==D3DFMT_A8R8G8B8&&pool==D3DPOOL_SYSTEMMEM,
        "a genuine compatibility fallback requests its complete native CPU surface");return E_OUTOFMEMORY;
}
HRESULT STDMETHODCALLTYPE Description(IDirect3DSurface9*,D3DSURFACE_DESC* out){
    *out={};out->Width=3840;out->Height=2160;out->Format=D3DFMT_A8R8G8B8;out->MultiSampleType=D3DMULTISAMPLE_8_SAMPLES;return S_OK;
}
int GPU(IDirect3DDevice9*,IDirect3DSurface9*,unsigned width,unsigned height,std::uint64_t,std::uint32_t,void*){
    ++gpuCalls;Check(width==3840&&height==2160,"GPU disposition sees the actual 4K image");return outcome;
}
void* DrawBegin(IDirect3DDevice9* device,const D3D9Capture::DrawCall&){++drawBegins;Check(device==expectedDevice,"draw scope receives the actual device");activeToken=useToken;return useToken?&activeToken:nullptr;}
bool Filter(IDirect3DDevice9* device,bool){++filterCalls;Check(device==expectedDevice,"draw filter sees the actual native device");return suppressDraw;}
HRESULT DrawEnd(void* token,HRESULT result){++drawEnds;Check(token==&activeToken&&activeToken,"scope finishes its own active token");Check(result==drawResult,"original draw result reaches restoration");activeToken=false;return result;}
HRESULT STDMETHODCALLTYPE Primitive(IDirect3DDevice9* device,D3DPRIMITIVETYPE type,UINT first,UINT count){
    ++nativeDraws;Check(device==expectedDevice&&type==D3DPT_TRIANGLELIST&&first==7&&count==11,"DrawPrimitive preserves all native arguments");return drawResult;
}
HRESULT STDMETHODCALLTYPE Indexed(IDirect3DDevice9* device,D3DPRIMITIVETYPE type,INT base,UINT min,UINT vertices,UINT first,UINT count){
    ++nativeDraws;Check(device==expectedDevice&&type==D3DPT_TRIANGLELIST&&base==-3&&min==17&&vertices==9&&first==23&&count==5,
        "DrawIndexedPrimitive preserves signed base and all index arguments");return drawResult;
}
struct TinySurface {void** table;};
TinySurface *tinySystem=nullptr,*tinyResolve=nullptr;
D3DFORMAT tinyFormat=D3DFMT_A8R8G8B8;D3DMULTISAMPLE_TYPE tinyMultisample=D3DMULTISAMPLE_NONE;
unsigned char tinySource[128]{};bool tinyPublish=true,tinyReadback=true,tinyMap=true;
ULONG STDMETHODCALLTYPE TinyRelease(IDirect3DSurface9*){return 1;}
HRESULT STDMETHODCALLTYPE TinyDescription(IDirect3DSurface9*,D3DSURFACE_DESC* out){
    *out={};out->Width=16;out->Height=2;out->Format=tinyFormat;out->MultiSampleType=tinyMultisample;return S_OK;
}
HRESULT STDMETHODCALLTYPE TinyAllocation(IDirect3DDevice9*,UINT width,UINT height,D3DFORMAT format,D3DPOOL pool,IDirect3DSurface9** out,HANDLE*){
    Check(width==16&&height==2&&format==tinyFormat&&pool==D3DPOOL_SYSTEMMEM,"CPU stage fixture preserves exact native extent and format");
    *out=reinterpret_cast<IDirect3DSurface9*>(tinySystem);return S_OK;
}
HRESULT STDMETHODCALLTYPE TinyTarget(IDirect3DDevice9*,UINT width,UINT height,D3DFORMAT format,D3DMULTISAMPLE_TYPE samples,DWORD quality,BOOL lockable,IDirect3DSurface9** out,HANDLE*){
    Check(width==16&&height==2&&format==tinyFormat&&samples==D3DMULTISAMPLE_NONE&&!quality&&!lockable,"resolve target remains unscaled and single sample");
    *out=reinterpret_cast<IDirect3DSurface9*>(tinyResolve);return S_OK;
}
HRESULT STDMETHODCALLTYPE TinyReadback(IDirect3DDevice9*,IDirect3DSurface9*,IDirect3DSurface9*){return tinyReadback?S_OK:E_FAIL;}
HRESULT STDMETHODCALLTYPE TinyStretch(IDirect3DDevice9*,IDirect3DSurface9*,const RECT*,IDirect3DSurface9*,const RECT*,D3DTEXTUREFILTERTYPE filter){
    Check(filter==D3DTEXF_NONE,"capture resolve never filters or shrinks the source");return S_OK;
}
HRESULT STDMETHODCALLTYPE TinyLock(IDirect3DSurface9*,D3DLOCKED_RECT* out,const RECT*,DWORD flags){
    Check(flags==D3DLOCK_READONLY,"capture stage maps only borrowed source pixels");Sleep(2);
    if(!tinyMap)return E_FAIL;out->pBits=tinySource;out->Pitch=tinyFormat==D3DFMT_R5G6B5?32:64;return S_OK;
}
HRESULT STDMETHODCALLTYPE TinyUnlock(IDirect3DSurface9*){return S_OK;}
bool TinyCallback(const std::uint8_t* source,bool opaque,unsigned width,unsigned height,unsigned stride,std::uint64_t,std::uint32_t,void*){
    Check(width==16&&height==2&&((tinyFormat==D3DFMT_R5G6B5&&!opaque)||(tinyFormat==D3DFMT_A8R8G8B8&&opaque)),
          "capture stage callback retains native layout and alpha-normalization contract");
    unsigned char out[128]{};Check(FrameCopy::BGRA(out,source,width,height,stride,opaque),"publish stage executes the actual frame copier");
    bool correct=true;for(unsigned i=0;i<32;++i){const unsigned p=i*4;correct=correct&&out[p]==(tinyFormat==D3DFMT_R5G6B5?0:0x44)&&
        out[p+1]==(tinyFormat==D3DFMT_R5G6B5?0:0x55)&&out[p+2]==(tinyFormat==D3DFMT_R5G6B5?255:0x66)&&out[p+3]==255;}
    Check(correct,"BGRA and 565 compatibility paths reach publication with exact RGB and opaque alpha");Sleep(2);return tinyPublish;
}
DWORD WINAPI DisableCaptureFromWorker(void*){
    D3D9Capture::SetEnabled(false);
    AfterPresent(D3D_OK);
    return 0;
}
void CaptureStages(){
    void* deviceTable[119]{};void* surfaceTable[17]{};
    deviceTable[14]=reinterpret_cast<void*>(&NoSwap);deviceTable[28]=reinterpret_cast<void*>(&TinyTarget);
    deviceTable[32]=reinterpret_cast<void*>(&TinyReadback);deviceTable[34]=reinterpret_cast<void*>(&TinyStretch);
    deviceTable[36]=reinterpret_cast<void*>(&TinyAllocation);
    surfaceTable[2]=reinterpret_cast<void*>(&TinyRelease);surfaceTable[12]=reinterpret_cast<void*>(&TinyDescription);
    surfaceTable[13]=reinterpret_cast<void*>(&TinyLock);surfaceTable[14]=reinterpret_cast<void*>(&TinyUnlock);
    TinySurface deviceObject{deviceTable},backObject{surfaceTable},systemObject{surfaceTable},resolveObject{surfaceTable};
    auto* device=reinterpret_cast<IDirect3DDevice9*>(&deviceObject);auto* back=reinterpret_cast<IDirect3DSurface9*>(&backObject);
    tinySystem=&systemObject;tinyResolve=&resolveObject;ReleaseResources();callback=TinyCallback;
    for(unsigned i=0;i<32;++i){tinySource[i*4]=0x44;tinySource[i*4+1]=0x55;tinySource[i*4+2]=0x66;tinySource[i*4+3]=0;}
    Check(Capture(device,back)==S_OK,"borrowed BGRA capture completes before unlocking its source");
    tinyFormat=D3DFMT_R5G6B5;for(unsigned i=0;i<32;++i){tinySource[i*2]=0;tinySource[i*2+1]=0xf8;}
    Check(Capture(device,back)==S_OK,"non-BGRA compatibility conversion reaches publication");
    tinyFormat=D3DFMT_A8R8G8B8;tinyMultisample=D3DMULTISAMPLE_8_SAMPLES;
    for(unsigned i=0;i<32;++i){tinySource[i*4]=0x44;tinySource[i*4+1]=0x55;tinySource[i*4+2]=0x66;tinySource[i*4+3]=0;}
    Check(Capture(device,back)==S_OK,"MSAA capture resolves without changing source extent");
    tinyMultisample=D3DMULTISAMPLE_NONE;tinyPublish=false;
    Check(Capture(device,back)==S_OK,"rejected publication still unlocks the borrowed source");
    tinyMap=false;Check(Capture(device,back)==E_FAIL,"failed mapping preserves its native HRESULT and never calls publication");
    tinyMap=true;tinyReadback=false;Check(Capture(device,back)==E_FAIL,"failed direct and resolved readbacks never attempt to map stale pixels");
    const auto snapshot=D3D9Capture::Snapshot();const auto& costs=snapshot.cpuStages;
    const auto stage=[&](D3D9Capture::CPUStage s){return costs[static_cast<unsigned>(s)];};
    Check(stage(D3D9Capture::CPUStage::Resolve).calls==2&&stage(D3D9Capture::CPUStage::Readback).calls==7&&
          stage(D3D9Capture::CPUStage::Map).calls==5&&stage(D3D9Capture::CPUStage::Convert).calls==1&&
          stage(D3D9Capture::CPUStage::Publish).calls==4&&stage(D3D9Capture::CPUStage::Unmap).calls==4,
          "production telemetry counts each successful, rejected and failed capture stage without inventing later stages");
    Check(stage(D3D9Capture::CPUStage::Map).maxUs>=1000&&stage(D3D9Capture::CPUStage::Publish).maxUs>=1000&&
          stage(D3D9Capture::CPUStage::Map).totalUs>=stage(D3D9Capture::CPUStage::Map).maxUs&&
          stage(D3D9Capture::CPUStage::Publish).totalUs>=stage(D3D9Capture::CPUStage::Publish).maxUs,
          "mapping and publication wall times remain separate and retain their own stalls");
    installed=true;D3D9Capture::SetEnabled(true);
    Check(systemSurface&&resolveSurface&&pixels.capacity()&&resourceThread==GetCurrentThreadId(),
          "CPU capture owns both temporary surfaces and a conversion buffer");
    const auto worker=CreateThread(nullptr,0,DisableCaptureFromWorker,nullptr,0,nullptr);
    Check(worker!=nullptr,"start separate fixture worker");
    if(worker){WaitForSingleObject(worker,INFINITE);CloseHandle(worker);}
    Check(!enabled.load()&&cpuScratchRetirementPending.load()&&systemSurface&&resolveSurface&&pixels.capacity(),
          "another thread requests retirement but cannot dispose of capture-thread scratch");
    AfterPresent(D3D_OK);
    Check(!cpuScratchRetirementPending.load()&&!systemSurface&&!resolveSurface&&!pixels.capacity(),
          "the owner retires CPU surfaces and conversion memory after disabled Present");
    D3D9Capture::SetEnabled(true);tinyReadback=true;tinyPublish=true;
    Check(Capture(device,back)==S_OK&&systemSurface,"resumed capture recreates usable scratch");
    installed=false;enabled=false;
    ReleaseResources();callback=nullptr;
}
IDirect3DSwapChain9* probeSwap=nullptr;
HRESULT STDMETHODCALLTYPE ProbeSwapChain(IDirect3DDevice9*,UINT index,IDirect3DSwapChain9** out){
    *out=index==0?probeSwap:nullptr;return *out?S_OK:D3DERR_NOTAVAILABLE;
}
ULONG STDMETHODCALLTYPE ProbeRelease(IDirect3DSwapChain9*){return 1;}
void PresentProbe(){
    using D3D9Capture::ObservePresentTarget;using D3D9Capture::SchedulePresentTargetProbe;
    void* deviceTable[119]{};void* swapTable[10]{};
    TinySurface deviceObject{deviceTable},swapObject{swapTable};
    auto* device=reinterpret_cast<IDirect3DDevice9*>(&deviceObject);
    probeSwap=reinterpret_cast<IDirect3DSwapChain9*>(&swapObject);
    deviceTable[14]=reinterpret_cast<void*>(&ProbeSwapChain);
    swapTable[2]=reinterpret_cast<void*>(&ProbeRelease);
    swapTable[kSwapPresentSlot]=reinterpret_cast<void*>(&ProbeSwapChain);
    deviceTable[kPresentSlot]=reinterpret_cast<void*>(&NoSwap);
    pendingSwapPresent=swapTable[kSwapPresentSlot];pendingDevicePresent=deviceTable[kPresentSlot];
    enabled=true;presents=0;endScenes=0;
    SchedulePresentTargetProbe(100);
    endScenes=90;SchedulePresentTargetProbe(2100);
    Check(!presentProbeRequested.load(),"non-promoted renderers never arm the target probe");
    promotionResult=S_OK;SchedulePresentTargetProbe(2200);
    endScenes=180;++presents;SchedulePresentTargetProbe(4200);
    Check(!presentProbeRequested.load(),"drawing with continuing Present does not arm the target probe");
    endScenes=330;SchedulePresentTargetProbe(6100);
    Check(!presentProbeRequested.load(),"short pauses cannot arm the target probe");
    SchedulePresentTargetProbe(6200);ObservePresentTarget(device);
    Check(presentProbeSamples==1&&presentProbeSwapChanges==0&&presentProbeDeviceChanges==0&&presentProbeResult==S_OK,
          "drawing without Present samples the promoted target on the render thread");
    swapTable[kSwapPresentSlot]=reinterpret_cast<void*>(&NoSwap);
    endScenes=440;SchedulePresentTargetProbe(8200);ObservePresentTarget(device);
    Check(presentProbeSamples==2&&presentProbeSwapChanges==1&&presentProbeDeviceChanges==0,
          "a changed live swap target is distinguished from the unchanged device target");
    probeSwap=nullptr;
    endScenes=560;SchedulePresentTargetProbe(10200);ObservePresentTarget(device);
    Check(presentProbeSamples==3&&presentProbeResult==D3DERR_NOTAVAILABLE&&presentProbeSwapChanges==1,
          "a failed live GetSwapChain retains its HRESULT without counting a different target");
    D3D9Capture::ConfigureFrameTrace(false);
    endScenes=620;SchedulePresentTargetProbe(12200);ObservePresentTarget(device);
    Check(presentProbeSamples==3&&!presentProbeRequested.load(),"diagnostic disable cancels pending probing");
}
}
int main(){
    void* deviceTable[119]{};void* surfaceTable[17]{};
    deviceTable[14]=reinterpret_cast<void*>(&NoSwap);deviceTable[36]=reinterpret_cast<void*>(&Allocation);
    surfaceTable[12]=reinterpret_cast<void*>(&Description);
    struct Object {void** table;};Object d{deviceTable},s{surfaceTable};
    auto* device=reinterpret_cast<IDirect3DDevice9*>(&d);auto* surface=reinterpret_cast<IDirect3DSurface9*>(&s);
    D3D9Capture::SetGPUCallback(GPU);
    for(unsigned i=0;i<12;++i){
        Check(CaptureFramePolicy::Select(false,true,true,false)==CaptureFramePolicy::Action::Hold,"expired stereo remains a scene hold");
        outcome=CaptureFramePolicy::HeldGPUResult();Check(Capture(device,surface)==S_FALSE,"production capture drops a held frame");
    }
    Check(!allocationAttempts&&readbackCalls.load()==0&&callbackDrops.load()==12,
        "repeated stale frames never allocate, resolve or read back rejected 4K8x images");
    outcome=-1;Check(Capture(device,surface)==E_OUTOFMEMORY&&allocationAttempts==1,
        "an actually unsupported GPU transport still enters the compatibility allocator");
    outcome=1;Check(Capture(device,surface)==S_OK&&allocationAttempts==1&&captured.load()==1,
        "fresh GPU publication recovers without CPU allocations");
    Check(gpuCalls==14,"one GPU decision is made per production capture attempt");
    D3D9Capture::ConfigureFrameTrace(true);presents=0;
    D3D9Capture::ObserveNativeCamera();const auto firstCamera=cameraMarker;
    Sleep(2);D3D9Capture::ObserveNativeCamera();
    Check(firstCamera.serial==1&&cameraMarker.timeUs==firstCamera.timeUs,"multiple native camera packets retain only the first consumer entry in a frame");
    D3D9Capture::ObserveFrameShader();const auto first=shaderMarker;
    Sleep(2);D3D9Capture::ObserveFrameShader();
    Check(first.serial==1&&shaderMarker.timeUs==first.timeUs,"multiple uploads mark only the first native shader of a frame");
    presents=1;const auto atPresent=FrameMetrics::Microseconds(0);
    RecordPacing(atPresent,atPresent,ThreadCPU());
    FrameEventTrace::Event events[4];std::uint64_t lost=0;
    Check(D3D9Capture::ReadFrameTrace(0,events,4,lost)==1&&events[0].shaderKnown&&events[0].shaderCpuKnown&&events[0].shaderSpanUs>=1000,
        "production Present records matching first-shader wall and CPU phases");
    Check(events[0].cameraKnown&&events[0].cameraThread==events[0].thread&&events[0].firstCameraUs==firstCamera.timeUs&&
          events[0].firstCameraUs<events[0].firstShaderUs,"native camera entry is separately recorded before the first shader with matching thread provenance");
    presents=2;const auto noShader=FrameMetrics::Microseconds(0);RecordPacing(noShader,noShader,ThreadCPU());
    Check(D3D9Capture::ReadFrameTrace(1,events,4,lost)==1&&!events[0].shaderKnown,
        "a frame with no shader cannot borrow an older frame's marker");
    Check(!events[0].cameraKnown,"a frame with no camera packet cannot borrow the previous frame's consumer marker");
    D3D9Capture::ConfigureFrameTrace(false);D3D9Capture::ObserveNativeCamera();
    Check(cameraMarker.serial==1,"disabled diagnostics do not sample a new camera packet");D3D9Capture::ConfigureFrameTrace(true);
    expectedDevice=device;originalDrawPrimitive=Primitive;originalDrawIndexedPrimitive=Indexed;
    D3D9Capture::SetDrawCallbacks(DrawBegin,DrawEnd);
    Check(D3D9Capture::DrawPrimitiveHook(device,D3DPT_TRIANGLELIST,7,11)==S_OK&&drawBegins==1&&drawEnds==1&&!activeToken,
        "draw scope restores after a successful native primitive");
    drawResult=E_FAIL;
    Check(D3D9Capture::DrawIndexedPrimitiveHook(device,D3DPT_TRIANGLELIST,-3,17,9,23,5)==E_FAIL&&drawEnds==2&&!activeToken,
        "failed indexed draw still restores and preserves its HRESULT");
    drawingOverlay=true;D3D9Capture::DrawPrimitiveHook(device,D3DPT_TRIANGLELIST,7,11);drawingOverlay=false;
    Check(drawBegins==2&&drawEnds==2,"VR overlay cannot enter native world-effect correction");
    useToken=false;D3D9Capture::DrawPrimitiveHook(device,D3DPT_TRIANGLELIST,7,11);
    Check(drawBegins==3&&drawEnds==2,"a rejected effect scope leaves native draw untouched");
    D3D9Capture::SetDrawCallbacks(DrawBegin,nullptr);D3D9Capture::DrawPrimitiveHook(device,D3DPT_TRIANGLELIST,7,11);
    Check(drawBegins==3&&drawEnds==2&&nativeDraws==5,"incomplete callbacks never begin a scope that cannot be restored");
    D3D9Capture::SetDrawCallbacks(nullptr,nullptr);
    D3D9Capture::SetDrawCallbacks(DrawBegin,DrawEnd);D3D9Capture::SetDrawFilter(Filter);
    const auto beforeNative=nativeDraws,beforeBegin=drawBegins,beforeEnd=drawEnds;
    suppressDraw=true;
    Check(D3D9Capture::DrawPrimitiveHook(device,D3DPT_TRIANGLELIST,7,11)==D3D_OK&&
        D3D9Capture::DrawIndexedPrimitiveHook(device,D3DPT_TRIANGLELIST,-3,17,9,23,5)==D3D_OK,
        "proven owned-object suppression completes either primitive API without issuing a GPU draw");
    Check(nativeDraws==beforeNative&&drawBegins==beforeBegin&&drawEnds==beforeEnd&&filterCalls==2,
        "suppressed draw cannot run effects preparation or native rendering");
    drawingOverlay=true;
    Check(D3D9Capture::DrawPrimitiveHook(device,D3DPT_TRIANGLELIST,7,11)==E_FAIL,"VR overlay keeps the native result even while an owned-object filter is active");
    drawingOverlay=false;Check(filterCalls==2&&nativeDraws==beforeNative+1,"overlay bypasses owned native world suppression");
    suppressDraw=false;useToken=true;
    Check(D3D9Capture::DrawIndexedPrimitiveHook(device,D3DPT_TRIANGLELIST,-3,17,9,23,5)==E_FAIL&&
        nativeDraws==beforeNative+2&&drawBegins==beforeBegin+1&&drawEnds==beforeEnd+1,
        "unmatched native objects retain the original draw and effect restoration");
    D3D9Capture::SetDrawFilter(nullptr);D3D9Capture::SetDrawCallbacks(nullptr,nullptr);
    D3D9Capture::SetGPUCallback(nullptr);
    CaptureStages();
    PresentProbe();
    std::printf("Production capture disposition: %u checks, %u failures; no graphics device allocated.\n",checks,errors);return errors?1:0;
}
