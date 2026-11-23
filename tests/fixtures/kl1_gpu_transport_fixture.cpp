// Real cross-architecture transport test. No game, headset or input injection.
#include <windows.h>
#include <cstdio>
#include <vector>
#include <string>
#include <cstdlib>
#include "capture_frame_policy.hpp"
#if defined(_WIN64)
#define wmain KL1ProductionHostMain
#include "kane_lynch_vr_host.cpp"
#undef wmain
#else
#include "vr_bridge.cpp"
#include "background/renderer_broker.hpp"
// Transport fixture never enters the diagnostic weapon/control tick.
namespace NativeWeaponKL {
void ServiceReloadFeedback(){}
bool SnapshotAim(AimFrame&){return false;}
Stats Snapshot(){return {};}
NativeScopeKL::State ReadScope(){return {};}
bool BackgroundReloadIntentReady(){return false;}
void BackgroundReloadControls(HandsIPC::Frame&){}
}
#endif

struct FixtureControl {
    LUID adapter;
    volatile LONG ready, request, ack, stop, failures, pause, paused,emptyGenerationCleared;
    unsigned width, height, mask, color, cpu, consumerChecks, direct, rightFirst,scenePair,pendingPanel,halfScene,monoKind,cpuScenePair,pattern;
    unsigned highFrequencyFrames;
    std::uint64_t pairLeftId,panelExpectedId,highFrequencyPixels;
};
static unsigned checks=0;
static const char* lastCheck="startup";
static void (*beforeFailureExit)()=nullptr;
static void Check(bool value,const char* label) {
    lastCheck=label;
    ++checks;if(!value){std::fprintf(stderr,"FAIL: %s (Win32 %lu)\n",label,GetLastError());if(beforeFailureExit)beforeFailureExit();std::exit(1);}
}
template<class F> static bool Until(F f,DWORD milliseconds=10000) {
    const DWORD start=GetTickCount();do{if(f())return true;Sleep(1);}while(DWORD(GetTickCount()-start)<milliseconds);return false;
}
static void ControlName(wchar_t* name,DWORD pid){swprintf(name,100,L"Local\\KL1GPUFixture-%lu",pid);}
static unsigned PatternPixel(unsigned pattern,unsigned x,unsigned y,unsigned flat) {
    switch(pattern) {
    case 1:return ((x^y)&1)?0xffe14f23u:0xff1769b5u; // one-pixel checkerboard
    case 2:return (y&1)?0xffdb2763u:0xff31c784u;     // alternating one-pixel rows
    case 3:return (x&1)?0xff2541dfu:0xfff5c51bu;     // alternating one-pixel columns
    default:return flat;
    }
}

#if defined(_WIN64)
static ovrResult OVR_CDECL FakeIndex(ovrSession,ovrTextureSwapChain,int* index){*index=0;return ovrSuccess;}
static ovrResult OVR_CDECL FakeCommit(ovrSession,ovrTextureSwapChain){return ovrSuccess;}
static bool VerifyTexture(Renderer& r,ID3D11Texture2D* image,unsigned width,unsigned height,unsigned flat,unsigned pattern) {
    D3D11_TEXTURE2D_DESC desc{};image->GetDesc(&desc);
    if(desc.Width!=width||desc.Height!=height||desc.MipLevels!=1||desc.ArraySize!=1||desc.SampleDesc.Count!=1)return false;
    desc.Usage=D3D11_USAGE_STAGING;
    desc.BindFlags=desc.MiscFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    GPUFrame::Object<ID3D11Texture2D> staging;
    if(FAILED(r.device->CreateTexture2D(&desc,nullptr,staging.Put())))return false;
    r.context->CopyResource(staging.p,image);D3D11_MAPPED_SUBRESOURCE mapped{};
    if(FAILED(r.context->Map(staging.p,0,D3D11_MAP_READ,0,&mapped)))return false;
    bool ok=true;for(unsigned y=0;y<desc.Height;++y)for(unsigned x=0;x<desc.Width;++x)
        if(const auto pixel=reinterpret_cast<const unsigned*>(static_cast<const unsigned char*>(mapped.pData)+y*mapped.RowPitch)[x];
           pixel!=PatternPixel(pattern,x,y,flat)){
            if(ok)std::fprintf(stderr,"pixel %u,%u actual=%08x expected=%08x pattern=%u\n",x,y,pixel,PatternPixel(pattern,x,y,flat),pattern);ok=false;}
    r.context->Unmap(staging.p,0);return ok;
}
static bool VerifyCpuPattern(const CapturedEye& eye,unsigned pattern,unsigned flat) {
    if(eye.metadata.stride!=eye.metadata.width*4||eye.pixels.size()!=std::size_t(eye.metadata.stride)*eye.metadata.height)return false;
    for(unsigned y=0;y<eye.metadata.height;++y)for(unsigned x=0;x<eye.metadata.width;++x) {
        unsigned pixel=0;std::memcpy(&pixel,eye.pixels.data()+std::size_t(y)*eye.metadata.stride+x*4,4);
        if(pixel!=PatternPixel(pattern,x,y,flat))return false;
    }
    return true;
}
int wmain(int argc,wchar_t** argv) {
    wchar_t logName[100];swprintf(logName,100,L"kl1-gpu-consumer-%lu.log",GetCurrentProcessId());
    _wfreopen(logName,L"w",stderr);setvbuf(stderr,nullptr,_IONBF,0);
    Check(argc==2,"consumer PID argument");const DWORD pid=wcstoul(argv[1],nullptr,10);wchar_t name[100];ControlName(name,pid);
    GPUFrame::Handle controlMap;controlMap.p=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,name);
    Check(controlMap.p!=nullptr,"consumer control mapping");
    auto* control=static_cast<FixtureControl*>(MapViewOfFile(controlMap.p,FILE_MAP_ALL_ACCESS,0,0,sizeof(FixtureControl)));
    Check(control!=nullptr,"consumer control view");
    VRIPC::MappingName(name,100,pid);GPUFrame::Handle frameMap;frameMap.p=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,name);
    Check(frameMap.p!=nullptr,"production frame mapping");
    auto* frames=static_cast<VRIPC::SharedFrames*>(MapViewOfFile(frameMap.p,FILE_MAP_ALL_ACCESS,0,0,sizeof(VRIPC::SharedFrames)));
    Check(frames&&frames->version==VRIPC::kVersion,"production ABI");
    GPUFrame::Object<IDXGIFactory4> factory;GPUFrame::Object<IDXGIAdapter1> adapter;
    Check(SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory4),reinterpret_cast<void**>(factory.Put()))),"DXGI factory");
    Check(SUCCEEDED(factory->EnumAdapterByLuid(control->adapter,__uuidof(IDXGIAdapter1),reinterpret_cast<void**>(adapter.Put()))),"same GPU LUID");
    Renderer renderer;D3D_FEATURE_LEVEL level;
    Check(SUCCEEDED(D3D11CreateDevice(adapter.p,D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr,0,D3D11_SDK_VERSION,&renderer.device,&level,&renderer.context)),"consumer D3D11 device");
    renderer.chainIndex=FakeIndex;renderer.commitChain=FakeCommit;
    GPUFrame::Consumer gpu;Check(gpu.Open(pid,renderer.device,renderer.context,true,control->direct!=0),"production GPU consumer");
    CapturedEye eyes[2];LONG last=0;InterlockedExchange(&control->ready,1);
    while(!control->stop) {
        InterlockedExchange(&frames->hostHeartbeatTick,GetTickCount());
        if(control->pause){
            for(auto& eye:eyes){eye.gpuImage.reset();eye.gpuLease.reset();}
            InterlockedExchange(&control->paused,1);Sleep(1);continue;
        }
        InterlockedExchange(&control->paused,0);gpu.Tick();
        GPUFrame::Header currentHeader{};
        if(GPUFrame::ReadHeader(*gpu.shared,currentHeader)&&!currentHeader.generation&&!gpu.generation&&!gpu.texture[0].p&&!gpu.ready.p)
            InterlockedExchange(&control->emptyGenerationCleared,1);
        const LONG request=InterlockedCompareExchange(&control->request,0,0);
        if(request!=last) {
            bool complete=true;
            if(control->halfScene==1){
                const auto previous=eyes[0].metadata;bool changed[2]{};
                ReadEyes(*frames,eyes,0,changed,&gpu);
                Check(!changed[0]&&!changed[1]&&!std::memcmp(&eyes[0].metadata,&previous,sizeof(previous)),
                      "UI->scene keeps prior display while only LEFT scene is announced");
                Check(!eyes[0].gpuLease&&!eyes[1].gpuLease,"complete-pair mode never pins an independent half-scene lease");
                const auto slot=frames->eyes[0].gpuSlot;
                Check(slot<GPUFrame::SlotCount&&GPUFrame::Atomic(gpu.shared->slots[slot].state)==GPUFrame::Ready&&
                      gpu.shared->slots[slot].metadata.frameId==control->pairLeftId,
                      "first completed LEFT scene remains Ready for the subsequent RIGHT instead of Reading");
                last=request;InterlockedExchange(&control->ack,request);Sleep(1);continue;
            }else if(control->halfScene==2){
                bool changed[2]{};ReadEyes(*frames,eyes,0,changed,&gpu);
                if(!changed[0]||!changed[1]){Sleep(1);continue;}
                Check(eyes[0].metadata.frameId==control->pairLeftId&&eyes[1].metadata.frameId==control->pairLeftId+1,
                      "first full scene pair is recovered immediately after the UI->scene partial publication");
            }else if(control->cpuScenePair){
                bool changed[2]{};ReadEyes(*frames,eyes,0,changed,&gpu);
                if(!changed[0]||!changed[1]){Sleep(1);continue;}
                Check(eyes[0].metadata.frameId==control->pairLeftId&&eyes[1].metadata.frameId==control->pairLeftId+1,
                      "CPU slow host recovers one complete pair after the next left capture is staged");
                Check(eyes[0].transport==VRIPC::kCPU&&eyes[1].transport==VRIPC::kCPU,
                      "CPU completed-pair test executes the actual CPU reader and compositor upload");
            }else if(control->scenePair){
                bool changed[2]{};ReadEyes(*frames,eyes,0,changed,&gpu);
                if(!changed[0]||!changed[1]){Sleep(1);continue;}
                Check(eyes[0].metadata.frameId==control->pairLeftId&&eyes[1].metadata.frameId==control->pairLeftId+1,"ring consumer recovers exact matching pair");
                Check(eyes[0].metadata.frameId!=frames->eyes[0].metadata.frameId,"complete pair survives an unmatched newer left eye");
            }else if(control->pendingPanel){
                bool changed[2]{};ReadEyes(*frames,eyes,0,changed,&gpu);
                if(!changed[0]||eyes[0].metadata.frameId!=control->panelExpectedId){Sleep(1);continue;}
                Check(eyes[0].metadata.frameId<frames->eyes[0].metadata.frameId,"completed UIA is read while newer published UIB fence is pending");
                Check(gpu.panelFramesRead&&gpu.olderPanelFramesRead,"completed older-panel recovery is visible in source counters");
                Check(!eyes[1].sequence&&!eyes[1].gpuLease&&!eyes[1].gpuImage,"recovered mono panel uses one source lease");
            }else if(control->mask==3){
                const auto* cpu=frames->eyes[1].transport==VRIPC::kCPU&&cpuPixelReader.Open(frames->gamePid,frames->eyes[1].transportEpoch)?&cpuPixelReader:nullptr;
                if(control->rightFirst&&!ReadEye(frames->eyes[1],eyes[1],0,&gpu,1,nullptr,true,cpu)){Sleep(1);continue;}
                bool changed[2]{};ReadEyes(*frames,eyes,0,changed,&gpu);
                if(!eyes[0].sequence||eyes[0].metadata.frameId!=frames->eyes[0].metadata.frameId){Sleep(1);continue;}
                Check(!eyes[1].sequence&&!eyes[1].gpuLease&&!eyes[1].gpuImage,"duplicate mono releases right-first lease");
            }
            for(unsigned side=0;side<2;++side)if(control->mask&(1u<<side)) {
                if(!control->scenePair&&control->mask==3&&side==1)continue;
                const auto* cpu=frames->eyes[side].transport==VRIPC::kCPU&&cpuPixelReader.Open(frames->gamePid,frames->eyes[side].transportEpoch)?&cpuPixelReader:nullptr;
                if(!control->scenePair&&control->mask!=3&&!ReadEye(frames->eyes[side],eyes[side],0,&gpu,side,nullptr,true,cpu)){complete=false;continue;}
                if(control->monoKind&&!control->scenePair&&control->mask==3&&!control->halfScene)
                    Check(eyes[side].metadata.poseValid==control->monoKind,"production mono transport preserves exact UI/scope kind instead of relabelling an older image");
                D3D11_TEXTURE2D_DESC desc{};desc.Width=control->width;desc.Height=control->height;
                desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT_B8G8R8A8_TYPELESS;
                desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
                renderer.gpuTargets[side].clear();
                for(auto* t:renderer.textures[side])t->Release();renderer.textures[side].clear();
                ID3D11Texture2D* target=nullptr;
                Check(SUCCEEDED(renderer.device->CreateTexture2D(&desc,nullptr,&target)),"fake compositor destination");
                renderer.textures[side].push_back(target);renderer.chains[side]=reinterpret_cast<ovrTextureSwapChain>(1);
                renderer.width=control->width;renderer.height=control->height;
                const bool kind=control->cpu?!eyes[side].gpuImage:bool(eyes[side].gpuImage);
                if(control->pattern) {
                    Check(eyes[side].metadata.width==3840&&eyes[side].metadata.height==2160&&eyes[side].metadata.stride==3840*4,
                          "one-pixel pattern keeps exact full4K native metadata through production ReadEyes");
                    if(control->cpu)Check(VerifyCpuPattern(eyes[side],control->pattern,control->color),
                          "all full4K CPU source pixels survive actual production ReadEyes before compositor upload");
                    else {
                        D3D11_TEXTURE2D_DESC source{};eyes[side].gpuImage->GetDesc(&source);
                        Check(source.Width==3840&&source.Height==2160&&source.MipLevels==1&&source.ArraySize==1&&source.SampleDesc.Count==1,
                              "actual D3D11 GPU source descriptor remains3840x2160 without lower-resolution intermediate");
                    }
                }
                if(control->cpu&&control->width==96&&side==0){
                    auto& frame=frames->eyes[side];const auto metadata=frame.metadata;const auto epoch=frame.transportEpoch;
                    InterlockedIncrement(&frame.sequence);frame.metadata.width+=16;frame.metadata.stride=frame.metadata.width*4;
                    MemoryBarrier();InterlockedIncrement(&frame.sequence);CapturedEye rejected;
                    Check(!ReadEye(frame,rejected,0,&gpu,side,nullptr,true,cpu)&&rejected.pixels.empty(),
                          "CPU image dimensions must match the immutable arena before pixel copy");
                    InterlockedIncrement(&frame.sequence);frame.metadata=metadata;++frame.transportEpoch;
                    MemoryBarrier();InterlockedIncrement(&frame.sequence);
                    Check(!ReadEye(frame,rejected,0,&gpu,side,nullptr,true,cpu)&&rejected.pixels.empty(),
                          "CPU token with a different arena epoch is rejected before pixel copy");
                    InterlockedIncrement(&frame.sequence);frame.transportEpoch=epoch;MemoryBarrier();InterlockedIncrement(&frame.sequence);
                }
                const bool uploaded=kind&&renderer.Upload(side,eyes[side].pixels.data(),eyes[side].metadata.stride,eyes[side].gpuImage.get(),eyes[side].gpuLease);
                const bool exact=uploaded&&VerifyTexture(renderer,target,control->width,control->height,control->color,control->pattern);
                if(!exact){
                    std::fprintf(stderr,"request=%ld side=%u direct=%u uploaded=%u GPU=%u lease=%u size=%u expected=%08x reason=%s error=%s\n",
                        request,side,control->direct,unsigned(uploaded),unsigned(bool(eyes[side].gpuImage)),unsigned(bool(eyes[side].gpuLease)),control->width,control->color,gpu.reason,renderer.errorText);
                    InterlockedIncrement(&control->failures);
                }
                if(control->pattern&&exact) {
                    D3D11_TEXTURE2D_DESC actual{};target->GetDesc(&actual);
                    ++control->highFrequencyFrames;control->highFrequencyPixels+=std::uint64_t(actual.Width)*actual.Height;
                    std::fprintf(stderr,"4K pattern%u %s: source3840x2160 -> actual D3D11 target%ux%u, all%llu pixels exact.\n",
                        control->pattern,control->cpu?"CPU ReadEyes/Upload":"GPU opaqueCopy",actual.Width,actual.Height,
                        static_cast<unsigned long long>(std::uint64_t(actual.Width)*actual.Height));
                }
                if(request==1){
                    wchar_t folder[MAX_PATH],path[MAX_PATH];GetTempPathW(MAX_PATH,folder);Check(GetTempFileNameW(folder,L"K2G",0,path)!=0,"diagnostic fixture path");
                    Check(SaveEyeBMP(renderer,eyes[side],path),"production one-shot GPU capture");
                    FILE* image=_wfopen(path,L"rb");unsigned char bytes[58]{};Check(image&&fread(bytes,1,sizeof(bytes),image)==sizeof(bytes),"BMP header and first pixel");
                    if(image)fclose(image);unsigned color=0;std::memcpy(&color,bytes+54,4);
                    Check(bytes[0]=='B'&&bytes[1]=='M'&&color==control->color,"captured BGRA colour");DeleteFileW(path);
                }
            }
            // Requests select one eye; mono tests read the shared left slot once.
            if(complete){
                ReleaseUploadedSources(eyes);
                Check(!eyes[0].gpuLease&&!eyes[1].gpuLease,"last queued upload/capture releases direct source leases");
                last=request;InterlockedExchange(&control->ack,request);}
        }
        Sleep(1);
    }
    Check(gpu.copies>=6&&gpu.bytesAvoided>0,"GPU copies and avoided CPU bytes");
    Check(control->direct?(gpu.directReads>=6&&renderer.gpuDirectUploads>=6&&!renderer.gpuUploadFallbacks):
          (!gpu.directReads&&!renderer.gpuDirectUploads),"selected direct/baseline path actually executes");
    control->consumerChecks=checks;
    std::printf("x64 production ReadEye/Upload: %u checks, failures=%ld, copies=%llu, bytesAvoided=%llu\n",
        checks,control->failures,(unsigned long long)gpu.copies,(unsigned long long)gpu.bytesAvoided);
    const int result=control->failures?1:0;UnmapViewOfFile(frames);UnmapViewOfFile(control);return result;
}
#else
using FixtureCreate9On12=IDirect3D9*(WINAPI*)(UINT,Args9On12*,UINT);
static FixtureCreate9On12 fixtureCreateOn12=nullptr;
static IDirect3D9* WINAPI FixtureOn12Factory(UINT sdk){Args9On12 args{};args.enable=TRUE;return fixtureCreateOn12(sdk,&args,1);}
// The blocked queue is always released on scope exit. Check uses std::exit,
// so it invokes the same cleanup explicitly before leaving a failed test;
// otherwise even a failed wait/assert could strand an unsignalled GPU wait.
struct QueuedFenceBlock {
    GPUFrame::Object<ID3D12Fence> fence;
    DXVKGPU::Bridge* vk=nullptr;
    GPUFrame::Handle exported;
    VkSemaphore semaphore=VK_NULL_HANDLE;VkFence completion=VK_NULL_HANDLE;
    PFN_vkGetFenceStatus status=nullptr;PFN_vkDestroyFence destroy=nullptr;
    static inline ID3D12Fence* pending=nullptr;
    static void ReleasePending(){if(pending)pending->Signal(1);}
    QueuedFenceBlock(ID3D12Device* device,ID3D12CommandQueue* queue,DXVKGPU::Bridge* dxvk=nullptr):vk(dxvk){
        Check(!pending&&!beforeFailureExit,"only one fixture queue block may be armed");
        Check(SUCCEEDED(device->CreateFence(0,vk?D3D12_FENCE_FLAG_SHARED:D3D12_FENCE_FLAG_NONE,__uuidof(ID3D12Fence),reinterpret_cast<void**>(fence.Put()))),"pending panel gate fence");
        pending=fence.p;beforeFailureExit=ReleasePending;
        if(!vk)Check(SUCCEEDED(queue->Wait(fence.p,1)),"enqueue asynchronous pending-panel gate without CPU wait");
        else{
            Check(SUCCEEDED(device->CreateSharedHandle(fence.p,nullptr,GENERIC_ALL,nullptr,&exported.p)),"export Vulkan gate fence");
            Check(vk->ImportFence(exported.p,semaphore),"import Vulkan queue gate");
            auto gipa=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(GetModuleHandleW(L"vulkan-1.dll"),"vkGetInstanceProcAddr"));
            auto gdpa=reinterpret_cast<PFN_vkGetDeviceProcAddr>(gipa(vk->instance,"vkGetDeviceProcAddr"));
            auto create=reinterpret_cast<PFN_vkCreateFence>(gdpa(vk->device,"vkCreateFence"));
            status=reinterpret_cast<PFN_vkGetFenceStatus>(gdpa(vk->device,"vkGetFenceStatus"));
            destroy=reinterpret_cast<PFN_vkDestroyFence>(gdpa(vk->device,"vkDestroyFence"));
            VkFenceCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            Check(create(vk->device,&info,nullptr,&completion)==VK_SUCCESS,"Vulkan gate completion fence");
            const uint64_t value=1;VkD3D12FenceSubmitInfoKHR timeline{};timeline.sType=VK_STRUCTURE_TYPE_D3D12_FENCE_SUBMIT_INFO_KHR;timeline.waitSemaphoreValuesCount=1;timeline.pWaitSemaphoreValues=&value;
            const VkPipelineStageFlags wait=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            VkSubmitInfo submit{};submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;submit.pNext=&timeline;submit.waitSemaphoreCount=1;submit.pWaitSemaphores=&semaphore;submit.pWaitDstStageMask=&wait;
            vk->interop->FlushRenderingCommands();vk->interop->LockSubmissionQueue();const auto result=vk->QueueSubmit(vk->queue,1,&submit,completion);vk->interop->ReleaseSubmissionQueue();
            Check(result==VK_SUCCESS,"enqueue asynchronous Vulkan gate without CPU wait");
        }
    }
    void Signal(){Check(SUCCEEDED(fence->Signal(1)),"release pending-panel GPU queue gate");}
    ~QueuedFenceBlock(){ReleasePending();if(vk){Check(Until([&]{return status(vk->device,completion)==VK_SUCCESS;}),"Vulkan gate completed before destruction");destroy(vk->device,completion,nullptr);vk->DestroySemaphore(vk->device,semaphore,nullptr);}pending=nullptr;beforeFailureExit=nullptr;}
};
// The fixture intentionally has no visible Present. Submit the native D3D9
// software batch before replacing its surfaces or simulating a Reset, just as
// a real presentation boundary would. This queues work only; it never waits
// for the GPU or changes the production transport's synchronization.
static void SubmitNativeCommands(IDirect3DDevice9* device){
    GPUFrame::Object<IDirect3DQuery9> event;
    Check(SUCCEEDED(device->CreateQuery(D3DQUERYTYPE_EVENT,event.Put())),"fixture native submission query");
    Check(SUCCEEDED(event->Issue(D3DISSUE_END)),"fixture native submission marker");
    Check(SUCCEEDED(event->GetData(nullptr,0,D3DGETDATA_FLUSH)),"fixture submits native commands without waiting for GPU");
}
static void FillPattern(IDirect3DDevice9* device,IDirect3DSurface9* target,unsigned width,unsigned height,unsigned pattern,unsigned flat) {
    D3DSURFACE_DESC desc{};Check(SUCCEEDED(target->GetDesc(&desc))&&desc.Width==width&&desc.Height==height,
                              "native D3D9 pattern render target has exact requested full4K dimensions");
    GPUFrame::Object<IDirect3DSurface9> upload;
    Check(SUCCEEDED(device->CreateOffscreenPlainSurface(width,height,desc.Format,D3DPOOL_SYSTEMMEM,upload.Put(),nullptr)),
          "fixture creates an exact-size system-memory pattern upload surface");
    D3DLOCKED_RECT locked{};Check(SUCCEEDED(upload->LockRect(&locked,nullptr,0)),"fixture locks native pattern upload");
    for(unsigned y=0;y<height;++y){auto* row=reinterpret_cast<unsigned*>(static_cast<unsigned char*>(locked.pBits)+std::size_t(y)*locked.Pitch);
        for(unsigned x=0;x<width;++x)row[x]=PatternPixel(pattern,x,y,flat);}
    Check(SUCCEEDED(upload->UnlockRect()),"fixture finishes native pattern upload");
    Check(SUCCEEDED(device->UpdateSurface(upload.p,nullptr,target,nullptr)),"fixture uploads one-pixel pattern into the real D3D9 render target without scaling");
}
int wmain(int argc,wchar_t** argv) {
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* p) __attribute__((stdcall))->LONG{
        HMODULE module=nullptr;wchar_t path[MAX_PATH]{};
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(p->ExceptionRecord->ExceptionAddress),&module);GetModuleFileNameW(module,path,MAX_PATH);
        std::fprintf(stderr,"Fixture exception %08lx at EIP=%08lx, address=%08llx, checks=%u, moduleRVA=%08lx\n",p->ExceptionRecord->ExceptionCode,p->ContextRecord->Eip,
            static_cast<unsigned long long>(p->ExceptionRecord->ExceptionInformation[1]),checks,p->ContextRecord->Eip-reinterpret_cast<DWORD>(module));
        std::fwprintf(stderr,L"Module: %ls\n",path);
        std::fprintf(stderr,"Last check: %s\n",lastCheck);
        if(beforeFailureExit)beforeFailureExit();
        if(auto* gpu=VRBridge::gpu.load())std::fprintf(stderr,"GPU operation: %s\n",gpu->reason);
        std::fflush(stderr);return EXCEPTION_EXECUTE_HANDLER;});
    Check(argc>=2&&argc<=5,"x64 consumer executable argument");
    const wchar_t* dxvkPath=nullptr;bool baseline=false;
    for(int i=2;i<argc;++i){if(!wcscmp(argv[i],L"baseline"))baseline=true;else if(!wcscmp(argv[i],L"--dxvk")&&i+1<argc)dxvkPath=argv[++i];else Check(false,"known GPU fixture option");}
    const std::wstring desktopName=L"KL1-GPU-Fixture-"+std::to_wstring(GetCurrentProcessId());
    const auto desktop=CreateDesktopW(desktopName.c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);
    Check(desktop&&SetThreadDesktop(desktop),"private desktop without switching the input desktop");
    SetEnvironmentVariableW(L"KL1_DXVK_GPU_TRANSPORT",L"0");
    Check(VRBridge::OpenMapping(),"production frame allocation");
    auto* frames=VRBridge::mapping.load();VRBridge::requested=true;
    frames->captureEnabled=frames->hostReady=1;frames->hostHeartbeatTick=GetTickCount();
    auto* producer=new GPUFrame::Producer;Check(producer->Open(GetCurrentProcessId()),"production GPU allocation");VRBridge::gpu=producer;
    HMODULE system=dxvkPath?LoadLibraryExW(dxvkPath,nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32):LoadLibraryExW(L"d3d9.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    using Create9=IDirect3D9*(WINAPI*)(UINT,Args9On12*,UINT);
    auto create=reinterpret_cast<Create9>(GetProcAddress(system,"Direct3DCreate9On12"));
    using CreateDXVK=IDirect3D9*(WINAPI*)(UINT);auto createDXVK=reinterpret_cast<CreateDXVK>(GetProcAddress(system,"Direct3DCreate9"));
    Check(dxvkPath?createDXVK!=nullptr:create!=nullptr,"selected renderer entry point");
    fixtureCreateOn12=create;GPUFrame::Object<IDirect3D9> d3d;
    d3d.p=dxvkPath?createDXVK(D3D_SDK_VERSION):BackgroundRendererBroker::Api(FixtureOn12Factory,D3D_SDK_VERSION);Check(d3d.p!=nullptr,"selected D3D9 factory");
    HWND window=CreateWindowExW(0,L"STATIC",L"KL1 transport fixture",WS_POPUP,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    Check(window!=nullptr,"hidden source window");D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    GPUFrame::Object<IDirect3DDevice9> device;
    const auto requested=pp;
    constexpr DWORD deviceFlags=D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED;
    SetLastError(ERROR_SUCCESS);
    const auto nativeCreate=reinterpret_cast<BackgroundRendererBroker::CreateFn>((*reinterpret_cast<void***>(d3d.p))[16]);
    const HRESULT deviceResult=dxvkPath?d3d->CreateDevice(0,D3DDEVTYPE_HAL,window,deviceFlags,&pp,device.Put()):
        BackgroundRendererBroker::Create(nativeCreate,d3d.p,0,D3DDEVTYPE_HAL,deviceFlags,&pp,device.Put());
    const DWORD deviceLastError=GetLastError();
    if(FAILED(deviceResult)) {
        std::fprintf(stderr,"On12 CreateDevice HRESULT=0x%08lx; Win32=%lu (supplemental only); adapter=0 type=%u flags=0x%08lx window=%p device=%p\n",
            static_cast<unsigned long>(deviceResult),deviceLastError,unsigned(D3DDEVTYPE_HAL),deviceFlags,window,device.p);
        const auto describe=[](const char* kind,const D3DPRESENT_PARAMETERS& value){
            std::fprintf(stderr,"%s: size=%ux%u format=%u backbuffers=%u msaa=%u quality=%lu swap=%u windowed=%u autoDepth=%u depthFormat=%u flags=0x%08lx fullscreenHz=%u interval=0x%08x deviceWindow=%p\n",
                kind,value.BackBufferWidth,value.BackBufferHeight,unsigned(value.BackBufferFormat),value.BackBufferCount,
                unsigned(value.MultiSampleType),value.MultiSampleQuality,unsigned(value.SwapEffect),unsigned(value.Windowed),
                unsigned(value.EnableAutoDepthStencil),unsigned(value.AutoDepthStencilFormat),value.Flags,
                value.FullScreen_RefreshRateInHz,value.PresentationInterval,value.hDeviceWindow);
        };
        describe("requested",requested);describe("returned",pp);
    }
    Check(SUCCEEDED(deviceResult),"On12 device");
    GPUFrame::Object<Device9On12> bridge;GPUFrame::Object<ID3D12Device> d12;
    DXVKGPU::Bridge dxvk;
    if(dxvkPath){Check(dxvk.Open(device.p),"real DXVK interface");Check(SUCCEEDED(dxvk.CreateD3D12Device(d12.Put())),"native D3D12 device matches DXVK LUID");}
    else{Check(SUCCEEDED(device->QueryInterface(iid9on12,reinterpret_cast<void**>(bridge.Put()))),"real On12 interface");
    Check(SUCCEEDED(bridge->GetD3D12Device(__uuidof(ID3D12Device),reinterpret_cast<void**>(d12.Put()))),"real D3D12 source");}
    wchar_t name[100];ControlName(name,GetCurrentProcessId());GPUFrame::Handle controlMap;
    controlMap.p=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(FixtureControl),name);
    auto* control=static_cast<FixtureControl*>(MapViewOfFile(controlMap.p,FILE_MAP_ALL_ACCESS,0,0,sizeof(FixtureControl)));
    Check(control!=nullptr,"fixture control allocation");*control={};control->adapter=d12->GetAdapterLuid();control->direct=!baseline;
    GPUFrame::Handle job;job.p=CreateJobObjectW(nullptr,nullptr);JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit{};
    limit.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    Check(job.p&&SetInformationJobObject(job.p,JobObjectExtendedLimitInformation,&limit,sizeof(limit)),"fixture child lifetime");
    std::wstring command=L"\""+std::wstring(argv[1])+L"\" "+std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW startup{};startup.cb=sizeof(startup);std::wstring childDesktop=L"WinSta0\\"+desktopName;startup.lpDesktop=childDesktop.data();PROCESS_INFORMATION child{};
    Check(CreateProcessW(argv[1],command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child),"x64 consumer process");
    GPUFrame::Handle process;process.p=child.hProcess;CloseHandle(child.hThread);
    Check(AssignProcessToJobObject(job.p,process.p),"consumer joins fixture lifetime");
    Check(Until([&]{return control->ready||WaitForSingleObject(process.p,0)==WAIT_OBJECT_0;}),"consumer startup timeout");Check(control->ready,"consumer ready");
    GPUFrame::Object<IDirect3DSurface9> surface;
    auto invalidate=[&]{SubmitNativeCommands(device.p);VRBridge::InvalidateGPU();};
    auto makeSurface=[&](unsigned size,D3DFORMAT format,unsigned height=0){SubmitNativeCommands(device.p);surface.Reset();Check(SUCCEEDED(device->CreateRenderTarget(size,height?height:size,format,D3DMULTISAMPLE_NONE,0,FALSE,surface.Put(),nullptr)),"source texture");};
    makeSurface(64,D3DFMT_A8R8G8B8);
    if(dxvkPath){D3DSURFACE_DESC desc{};Check(SUCCEEDED(surface->GetDesc(&desc)),"DXVK opt-in source descriptor");
        Check(!producer->Create(device.p,desc)&&!producer->active,"DXVK GPU bridge is disabled unless explicitly selected");producer->dxvkEnabled=true;}
    std::uint64_t id=0;LONG request=0;
    auto send=[&](unsigned side,unsigned size,unsigned color,bool cpu=false,unsigned mask=0,unsigned height=0,unsigned monoKind=VRIPC::kUiFrame,unsigned pattern=0){
        control->width=size;control->height=height?height:size;control->mask=mask?mask:1u<<side;control->color=color;control->cpu=cpu;
        control->monoKind=monoKind;control->pattern=pattern;
        VRIPC::EyeMetadata metadata{};metadata.width=size;metadata.height=control->height;metadata.stride=size*4;metadata.tick=GetTickCount();
        metadata.frameId=++id;metadata.poseValid=monoKind;
        if(!cpu) {
            if(pattern)FillPattern(device.p,surface.p,size,control->height,pattern,color);
            else Check(SUCCEEDED(device->ColorFill(surface.p,nullptr,color&0xffffffu)),"source colour fill");
        }
        if(cpu){std::vector<unsigned> pixels(size*control->height,color);
            if(pattern)for(unsigned y=0;y<control->height;++y)for(unsigned x=0;x<size;++x)pixels[std::size_t(y)*size+x]=PatternPixel(pattern,x,y,color);
            for(unsigned eye=0;eye<2;++eye)if(control->mask&(1u<<eye))
                Check(VRBridge::Publish(eye,metadata,reinterpret_cast<const std::uint8_t*>(pixels.data())),"CPU fallback publish");}
        else {int result=0;Check(Until([&]{result=VRBridge::PublishGPU(device.p,surface.p,mask?mask:1u<<side,metadata);
                return result==1;}),"GPU publication recovers within bounded startup/Reset window");
            if(result<0)std::fprintf(stderr,"GPU reason: %s source=%ux%u checks=%u gen=%u consumer=%ld enabled=%ld age=%lu cpu=%ld\n",producer->reason,size,control->height,checks,producer->generation,
                GPUFrame::Atomic(producer->shared->consumerPid),GPUFrame::Atomic(producer->shared->consumerEnabled),
                GetTickCount()-DWORD(GPUFrame::Atomic(producer->shared->consumerTick)),GPUFrame::Atomic(producer->shared->cpuRequested));
            Check(result==1,"GPU publish without fallback");}
        MemoryBarrier();InterlockedExchange(&control->request,++request);
        Check(Until([&]{return control->ack==request||WaitForSingleObject(process.p,0)==WAIT_OBJECT_0;}),"consumer read timeout");
        Check(control->ack==request&&!control->failures,"exact opaque BGRA through production compositor upload");
    };
    for(unsigned i=0;i<4;++i)send(i&1,64,0xff123456u+i*0x10203u);
    Check(producer->active->physicalTextures==4&&VRBridge::Snapshot().gpuPhysicalTextures==4&&
        VRBridge::Snapshot().gpuSharedTextureBytes>0,
        "production status counts all four physical GPU allocations and their D3D12 allocation bytes");
    if(dxvkPath)Check(producer->active->intermediateTextures==4&&VRBridge::Snapshot().gpuIntermediateTextures==4&&
        VRBridge::Snapshot().gpuIntermediateTextureBytes>0,"DXVK native D3D12 relay allocations are counted separately from IPC textures");
    send(0,64,0xffa15423,true);
    const auto oldCPUEpoch=frames->eyes[0].transportEpoch;
    send(0,96,0xff517a32,true,3);
    CPUFrame::Layout smallCPU{};Check(CPUFrame::Capacity(96,96,smallCPU),"resized CPU capacity");
    Check(frames->eyes[0].transportEpoch!=oldCPUEpoch&&VRBridge::Snapshot().cpuPixelMappingBytes==smallCPU.mappingBytes,
          "CPU resize creates a new exact-size epoch instead of reusing old pixel offsets");
    InterlockedExchange(&control->pause,1);Check(Until([&]{return control->paused!=0;}),"pause host before batched CPU captures");
    control->cpuScenePair=control->scenePair=1;control->mask=3;control->cpu=1;control->color=0xff426781;
    control->pairLeftId=(id+2)&~std::uint64_t{1};
    std::vector<unsigned> cpuPairPixels(96*96,control->color);
    for(unsigned n=0;n<3;++n){VRIPC::EyeMetadata pair{};pair.width=pair.height=96;pair.stride=384;pair.tick=GetTickCount();
        pair.frameId=control->pairLeftId+n;pair.poseValid=VRIPC::kSceneFrame;pair.sensorTime=3.;pair.headOrientation[3]=1;
        for(auto& f:pair.fov)f=1;
        if(n==2)std::fill(cpuPairPixels.begin(),cpuPairPixels.end(),0xff815a29);
        Check(VRBridge::Publish(n&1,pair,reinterpret_cast<const std::uint8_t*>(cpuPairPixels.data())),
              "batch CPU left/right/newer-left through production publication");
    }
    Check(frames->eyes[0].metadata.frameId==control->pairLeftId&&frames->eyes[1].metadata.frameId==control->pairLeftId+1,
          "new CPU left leaves both announced complete-pair metadata records unchanged");
    id=control->pairLeftId+2;MemoryBarrier();InterlockedExchange(&control->request,++request);InterlockedExchange(&control->pause,0);
    Check(Until([&]{return control->ack==request||WaitForSingleObject(process.p,0)==WAIT_OBJECT_0;}),"completed CPU pair host read timeout");
    Check(control->ack==request&&!control->failures,"both retained CPU images survive x86 publish/x64 read/opaque upload after newer-left staging");
    control->cpuScenePair=control->scenePair=0;
    send(0,64,0xff112233,false,3);
    // Repeated UI publications and right-first acquisition must not pin both
    // left slots. Each request is real cross-process producer/host code.
    for(unsigned i=0;i<8;++i){control->rightFirst=i&1;send(0,64,0xff18324au+i,false,3);}
    for(unsigned i=0;i<GPUFrame::SlotCount;++i)Check(GPUFrame::Atomic(producer->shared->slots[i].state)!=GPUFrame::Reading,"uploaded source does not pin either ring slot while waiting for the next pair");
    control->rightFirst=0;
    Check(VRBridge::Snapshot().cpuPixelMappingBytes==0,"GPU frame releases the entire unused x86 CPU pixel mapping");
    send(0,64,0xff436b29,true,3,0,VRIPC::kScopeFrame);
    for(unsigned i=0;i<4;++i){control->rightFirst=i&1;send(0,64,0xff542a67u+i,false,3,0,VRIPC::kScopeFrame);}
    control->rightFirst=0;send(0,64,0xff2f496b,false,3);
    invalidate();makeSurface(3840,D3DFMT_X8R8G8B8,2160);
    send(0,3840,0xff6f4931,false,0,2160);send(1,3840,0xff294c81,false,0,2160);
    send(0,3840,0xff326b49,false,3,2160,VRIPC::kScopeFrame);
    send(0,3840,0xff516c83,true,3,2160);
    Check(frames->eyes[0].transport==VRIPC::kCPU&&frames->eyes[1].transport==VRIPC::kCPU&&
          frames->eyes[0].metadata.width==3840&&frames->eyes[1].metadata.height==2160,
          "full4K CPU left/right pixels survive the actual x86 producer and x64 compositor upload");
    CPUFrame::Layout full4k{};Check(CPUFrame::Capacity(3840,2160,full4k),"4K CPU arena capacity");
    Check(VRBridge::Snapshot().cpuPixelMappingBytes==full4k.mappingBytes,"CPU fallback lazily maps exact full-resolution published and pending pairs");
    for(unsigned pattern=1;pattern<=3;++pattern) {
        send(0,3840,0xff123456,false,3,2160,VRIPC::kUiFrame,pattern);
        send(0,3840,0xff123456,true,3,2160,VRIPC::kUiFrame,pattern);
    }
    Check(control->highFrequencyFrames==6&&control->highFrequencyPixels==std::uint64_t(6)*3840*2160,
          "all full4K one-pixel checkerboard, rows and columns survive actual CPU/GPU production upload with every target pixel verified");
    std::printf("4K high-frequency production transport: %u CPU/GPU targets, %llu exact pixels PASS.\n",
                control->highFrequencyFrames,static_cast<unsigned long long>(control->highFrequencyPixels));
    control->pattern=0;
    auto oversized=frames->eyes[0].metadata;oversized.width=VRIPC::kMaxWidth+1;oversized.stride=oversized.width*4;
    Check(!VRBridge::Publish(0,oversized,reinterpret_cast<const std::uint8_t*>(&oversized)),"producer rejects CPU dimensions beyond native transport bound before copy");
    invalidate();makeSurface(96,D3DFMT_X8R8G8B8);send(1,96,0xffb12a53);send(0,96,0xff3f4a79);
    Check(producer->generation>=2,"Reset creates new shared generation");
    Check(VRBridge::Snapshot().cpuPixelMappingBytes==0,"returning to GPU releases the CPU arena after full4K fallback");
    InterlockedExchange(&control->emptyGenerationCleared,0);
    invalidate();surface.Reset();pp.BackBufferWidth=pp.BackBufferHeight=128;
    Check(Until([&]{return control->emptyGenerationCleared!=0;}),"stable Reset generation0 releases host duplicate textures/fences before producer recreation");
    Check(SUCCEEDED(device->Reset(&pp)),"actual D3D9 Reset after releasing DEFAULT resources");
    producer->rejected=device.p;producer->creationRetryable=true;producer->rejectedAt=GetTickCount()-2000;
    makeSurface(128,D3DFMT_X8R8G8B8);send(0,128,0xff7f4b19);send(1,128,0xff523486);
    Check(producer->active&&!producer->rejected,"transient rejected-device state recovers GPU publication without host restart");
    InterlockedExchange(&control->pause,1);Check(Until([&]{return control->paused!=0;}),"release source leases before pair batching");
    control->scenePair=1;control->pairLeftId=(id+2)&~std::uint64_t{1};control->mask=3;control->color=0xff426781;
    Check(SUCCEEDED(device->ColorFill(surface.p,nullptr,control->color)),"scene pair GPU colour");
    for(unsigned n=0;n<3;++n){VRIPC::EyeMetadata pair{};pair.width=pair.height=128;pair.stride=512;pair.tick=GetTickCount();
        pair.frameId=control->pairLeftId+n;pair.poseValid=VRIPC::kSceneFrame;pair.headOrientation[3]=1;for(auto& f:pair.fov)f=1;
        Check(Until([&]{return VRBridge::PublishGPU(device.p,surface.p,1u<<(n&1),pair)==1;}),"batch left/right/newer-left GPU frames");}
    id=control->pairLeftId+2;MemoryBarrier();InterlockedExchange(&control->request,++request);InterlockedExchange(&control->pause,0);
    Check(Until([&]{return control->ack==request||WaitForSingleObject(process.p,0)==WAIT_OBJECT_0;}),"complete pair read timeout");
    Check(control->ack==request&&!control->failures,"both matching scene eyes pass production opaque upload and pixel validation");
    control->scenePair=0;
    // Start a fresh ring while the consumer is still ticking: generation ACK
    // must not wait on a paused consumer. No new request is posted, so it can
    // acknowledge resources without acquiring panelA before the race starts.
    invalidate();
    control->width=control->height=128;control->mask=3;control->cpu=0;control->rightFirst=0;
    constexpr unsigned panelA=0xff315579,panelB=0xff794631;
    VRIPC::EyeMetadata panel{};panel.width=panel.height=128;panel.stride=512;panel.poseValid=VRIPC::kUiFrame;
    panel.frameId=++id;panel.tick=GetTickCount();control->panelExpectedId=panel.frameId;
    Check(SUCCEEDED(device->ColorFill(surface.p,nullptr,panelA&0xffffffu)),"completed panelA source colour");
    Check(Until([&]{return VRBridge::PublishGPU(device.p,surface.p,3,panel)==1;}),"publish completed panelA before queue gate");
    Check(Until([&]{return producer->active->ready->GetCompletedValue()>=producer->active->next;}),"panelA GPU fence completes before pending panelB");
    InterlockedExchange(&control->pause,1);Check(Until([&]{return control->paused!=0;}),"release leases before pending UI fence race");
    {
        QueuedFenceBlock gate(producer->active->device.p,producer->active->queue.p,producer->active->dxvk.get());
        Check(SUCCEEDED(device->ColorFill(surface.p,nullptr,panelB&0xffffffu)),"pending panelB source colour");
        panel.frameId=++id;panel.tick=GetTickCount();
        Check(Until([&]{return VRBridge::PublishGPU(device.p,surface.p,3,panel)==1;}),"publish panelB token while its GPU queue is gated");
        Check(producer->active->ready->GetCompletedValue()<producer->active->next,"latest IPC panelB token is genuinely GPU-incomplete");
        control->pendingPanel=1;control->color=panelA;MemoryBarrier();
        InterlockedExchange(&control->request,++request);InterlockedExchange(&control->pause,0);
        Check(Until([&]{return control->ack==request||WaitForSingleObject(process.p,0)==WAIT_OBJECT_0;}),"host recovers completed panelA without waiting for panelB");
        Check(control->ack==request&&!control->failures,"panelA pixels pass actual production ReadEyes and opaque Upload while panelB is blocked");
        Check(producer->active->ready->GetCompletedValue()<producer->active->next,"successful panelA read never signals pending panelB fence");
        control->pendingPanel=0;control->color=panelB;gate.Signal();MemoryBarrier();
        InterlockedExchange(&control->request,++request);
        Check(Until([&]{return control->ack==request||WaitForSingleObject(process.p,0)==WAIT_OBJECT_0;}),"host advances to panelB after its real GPU fence completes");
        Check(control->ack==request&&!control->failures,"newest panelB pixels pass actual production upload after unblock");
    }
    // Repeat the exact real pending-fence race for native optic kind3. Its
    // recovery must never select a leftover UI slot or lose the mono kind.
    InterlockedExchange(&control->pause,1);Check(Until([&]{return control->paused!=0;}),"pause before pending scope source");
    panel.poseValid=VRIPC::kScopeFrame;panel.frameId=++id;panel.tick=GetTickCount();
    constexpr unsigned scopeA=0xff385e71,scopeB=0xff714e38;
    Check(SUCCEEDED(device->ColorFill(surface.p,nullptr,scopeA&0xffffffu)),"scopeA source colour");
    // The UI->scope layout change needs the consumer's header acknowledgement.
    // No new read request is outstanding, so let Tick open the generation
    // before pausing again for the independent pending-fence race below.
    InterlockedExchange(&control->pause,0);
    Check(Until([&]{return VRBridge::PublishGPU(device.p,surface.p,3,panel)==1;}),"publish completed native scopeA");
    Check(Until([&]{return producer->active->ready->GetCompletedValue()>=producer->active->next;}),"scopeA fence completes before scopeB gate");
    InterlockedExchange(&control->pause,1);
    Check(Until([&]{return control->paused!=0;}),"pause acknowledged scope generation before pending-fence race");
    control->panelExpectedId=panel.frameId;control->monoKind=VRIPC::kScopeFrame;
    {
        QueuedFenceBlock gate(producer->active->device.p,producer->active->queue.p,producer->active->dxvk.get());
        Check(SUCCEEDED(device->ColorFill(surface.p,nullptr,scopeB&0xffffffu)),"pending scopeB source colour");
        panel.frameId=++id;panel.tick=GetTickCount();
        Check(Until([&]{return VRBridge::PublishGPU(device.p,surface.p,3,panel)==1;}),"publish scopeB while its ready fence is genuinely pending");
        Check(producer->active->ready->GetCompletedValue()<producer->active->next,"scopeB fence is not accidentally complete");
        control->pendingPanel=1;control->color=scopeA;MemoryBarrier();InterlockedExchange(&control->request,++request);InterlockedExchange(&control->pause,0);
        Check(Until([&]{return control->ack==request||WaitForSingleObject(process.p,0)==WAIT_OBJECT_0;}),"host recovers completed native scopeA without waiting");
        Check(control->ack==request&&!control->failures,"scopeA exact opaque pixels pass actual production ReadEyes/Upload with kind3");
        Check(producer->active->ready->GetCompletedValue()<producer->active->next,"reading scopeA cannot signal pending scopeB");
        control->pendingPanel=0;control->color=scopeB;gate.Signal();MemoryBarrier();InterlockedExchange(&control->request,++request);
        Check(Until([&]{return control->ack==request||WaitForSingleObject(process.p,0)==WAIT_OBJECT_0;}),"host advances to native scopeB once its fence completes");
        Check(control->ack==request&&!control->failures,"scopeB pixels and kind3 remain exact through the real GPU transport");
    }
    send(0,128,0xff315579,false,3); // preserve the original UI->partial scene test below
    // First publish only the LEFT scene, with the prior RIGHT still UI. The
    // host must keep that source Ready until a complete pair is available.
    InterlockedExchange(&control->pause,1);Check(Until([&]{return control->paused!=0;}),"pause before partial UI->scene transition");
    control->pairLeftId=(id+2)&~std::uint64_t{1};control->halfScene=1;control->color=0xff284c73;
    Check(SUCCEEDED(device->ColorFill(surface.p,nullptr,control->color&0xffffffu)),"partial scene source colour");
    VRIPC::EyeMetadata firstScene{};firstScene.width=firstScene.height=128;firstScene.stride=512;
    firstScene.tick=GetTickCount();firstScene.frameId=control->pairLeftId;firstScene.poseValid=VRIPC::kSceneFrame;
    firstScene.headOrientation[3]=1;for(auto& f:firstScene.fov)f=1;
    // Reproduce the first-scene handshake from the installed black-screen
    // session: good camera frames alternate with rejected native retries,
    // before the host has acknowledged the new four-texture generation.
    // An incomplete tracked frame must not publish a mono image and replace
    // that generation with a compact two-texture pool before it can be used.
    Check(Until([&]{
        const int result=VRBridge::PublishGPU(device.p,surface.p,1,firstScene);
        return result==0&&producer->active&&!producer->active->compactPanel&&
            std::strcmp(producer->reason,"waiting_for_gpu_generation_ack")==0;
    }),"first tracked scene reaches a real delayed generation handshake");
    const auto firstSceneGeneration=producer->generation;
    const auto firstSceneCopies=producer->copies;
    for(unsigned retry=0;retry<16;++retry){
        const auto missing=CaptureFramePolicy::Select(false,true,false,false,true);
        Check(missing==CaptureFramePolicy::Action::Hold,
            "incomplete first native scene holds instead of requesting a compact UI generation");
        if(missing==CaptureFramePolicy::Action::Panel){
            auto accidentalUi=firstScene;accidentalUi.poseValid=VRIPC::kUiFrame;
            VRBridge::PublishGPU(device.p,surface.p,3,accidentalUi);
        }
        Check(VRBridge::PublishGPU(device.p,surface.p,1,firstScene)==0&&
            producer->generation==firstSceneGeneration&&producer->active&&
            !producer->active->compactPanel&&producer->active->physicalTextures==4&&
            producer->copies==firstSceneCopies,
            "delayed host acknowledgement preserves the same full generation across camera retries");
    }
    Check(VRBridge::Snapshot().cpuPixelMappingBytes==0,
        "first scene retry handshake does not allocate a CPU fallback arena");
    InterlockedExchange(&control->pause,0);
    Check(Until([&]{return VRBridge::PublishGPU(device.p,surface.p,1,firstScene)==1;}),"publish first LEFT scene while RIGHT retains UI");
    Check(producer->generation==firstSceneGeneration,
        "host acknowledgement recovers the first scene without replacing its retained generation");
    Check(Until([&]{return producer->active->ready->GetCompletedValue()>=producer->active->next;}),"first partial LEFT is genuinely GPU completed");
    MemoryBarrier();InterlockedExchange(&control->request,++request);InterlockedExchange(&control->pause,0);
    Check(Until([&]{return control->ack==request||WaitForSingleObject(process.p,0)==WAIT_OBJECT_0;}),"host checks partial scene without claiming it");
    Check(control->ack==request&&!control->failures,"partial LEFT remains available for complete-pair reader");
    control->halfScene=2;control->scenePair=1;control->mask=3;firstScene.frameId++;
    Check(Until([&]{return VRBridge::PublishGPU(device.p,surface.p,2,firstScene)==1;}),"publish matching RIGHT scene after partial transition check");
    MemoryBarrier();InterlockedExchange(&control->request,++request);
    Check(Until([&]{return control->ack==request||WaitForSingleObject(process.p,0)==WAIT_OBJECT_0;}),"host recovers the FIRST complete UI->scene pair");
    Check(control->ack==request&&!control->failures,"first scene pair passes exact pixel upload after unclaimed half");
    id=firstScene.frameId;control->halfScene=0;control->scenePair=0;
    InterlockedExchange(&control->pause,1);Check(Until([&]{return control->paused!=0;}),"pause consumer");
    VRIPC::EyeMetadata m{};m.width=m.height=128;m.stride=512;m.poseValid=VRIPC::kUiFrame;m.tick=GetTickCount();
    for(unsigned i=0;i<2;++i){m.frameId=++id;Check(Until([&]{return VRBridge::PublishGPU(device.p,surface.p,1,m)==1;}),"fill eye ring after asynchronous lease fence");}
    for(unsigned i=0;i<2;++i)InterlockedExchange(&producer->shared->slots[i].state,GPUFrame::Reading);
    m.frameId=++id;const DWORD busyStart=GetTickCount();Check(VRBridge::PublishGPU(device.p,surface.p,1,m)==0,"busy ring drops frame");
    Check(DWORD(GetTickCount()-busyStart)<100,"busy ring does not block rendering");
    producer->DeviceUnavailable(device.p,HRESULT(0x887A0005L));
    const auto copiesBeforeRemoval=producer->copies;const auto generationBeforeRemoval=producer->generation;
    for(unsigned i=0;i<8;++i)Check(VRBridge::PublishGPU(device.p,surface.p,1,m)==0,"removed device holds output instead of requesting CPU allocations");
    const auto removedState=VRBridge::Snapshot();
    Check(producer->unavailable==device.p&&producer->copies==copiesBeforeRemoval&&producer->generation==generationBeforeRemoval,"removed device never recreates its GPU generation");
    Check(removedState.cpuPixelMappingBytes==0&&removedState.gpuCreateResult==HRESULT(0x887A0005L),"removal keeps exact HRESULT without allocating a CPU arena");
    invalidate();Check(!producer->unavailable,"native Reset explicitly unlocks the removed-device gate");
    InterlockedExchange(&control->pause,0);send(0,128,0xff518327);
    Check(producer->active&&producer->generation>generationBeforeRemoval,"GPU recovers after qualified reset; removed-device injection did not poison healthy transport");
    InterlockedExchange(&control->pause,0);InterlockedExchange(&control->stop,1);
    Check(WaitForSingleObject(process.p,10000)==WAIT_OBJECT_0,"consumer shutdown");DWORD code=1;GetExitCodeProcess(process.p,&code);Check(code==0,"consumer success");
    const auto copies=producer->copies,busy=producer->busy;const auto generations=producer->generation,consumerChecks=control->consumerChecks;
    invalidate();surface.Reset();
    Check(Until([&]{producer->Collect();return producer->retired.empty();}),"completed retired generations release before teardown");
    VRBridge::cpuPixels.Close();VRBridge::gpu=nullptr;
    UnmapViewOfFile(producer->shared);producer->shared=nullptr;delete producer;
    bridge.Reset();dxvk.interop.Reset();d12.Reset();device.Reset();d3d.Reset();
    UnmapViewOfFile(frames);VRBridge::mapping=nullptr;CloseHandle(VRBridge::mapHandle);VRBridge::mapHandle=nullptr;
    DestroyWindow(window);UnmapViewOfFile(control);FreeLibrary(system);
    std::printf("x86 production PublishGPU/CPU: %u checks; x64 ReadEye/Upload/capture: %u checks; copies=%llu, busy=%llu, generations=%u PASS\n",checks,consumerChecks,
        (unsigned long long)copies,(unsigned long long)busy,generations);return 0;
}
#endif
