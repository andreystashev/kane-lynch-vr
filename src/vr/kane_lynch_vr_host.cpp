// KaneLynchVRHost.exe (x64): Meta compositor host for Kane & Lynch: Dead Men.
// Reads alternating-eye kSceneFrame pairs, mono UI panels or native scope images
// from the game's VRIPC shared mapping, publishes live LibOVR tracking back
// into the mapping for the game's transform hook, and submits either a
// tracked stereo EyeFov layer or a head-locked quad, mirroring the Blood
// Money / Contracts host.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>
#include "release_version.hpp"
#define OVR_CAPI_NO_UTILS
#include "OVR_CAPI.h"
#include "OVR_CAPI_D3D.h"
#include "vr_ipc.hpp"
#include "reload_feedback_ipc.hpp"
#include "cpu_frame_ipc.hpp"
#include "frame_metrics.hpp"
#include "frame_pacing.hpp"
#include "meta_submit_pacing.hpp"
#include "frame_delivery_metrics.hpp"
#include "cpu_timer.hpp"
#include "host_frame_timing.hpp"
#include "controller_input.hpp"
#include "team_command_ipc.hpp"
#include "gpu_frame_consumer.hpp"
#include "hands_ipc.hpp"
#include "mono_frame_policy.hpp"
#ifdef KL1_OPENXR_HOST
#include "kl1_openxr_adapter.hpp"
#define GetProcAddress KL1OpenXR::MirrorExport
#endif
#include "recording_mirror.hpp"
#ifdef KL1_OPENXR_HOST
#undef GetProcAddress
#endif
#include "meta_runtime.hpp"
#include "crash_diagnostics.hpp"

static_assert(sizeof(void*)==8,"The Meta compositor host must be compiled for x64.");
static_assert(sizeof(ovrTrackingState)==312,"Unexpected LibOVR tracking ABI.");

namespace {
[[maybe_unused]] constexpr int kRuntimeMissing=-91001,kExportMissing=-91002;
constexpr int kGraphicsFailed=-91003;
constexpr int kBadFrame=-91004,kSourceMissing=-91005,kSourceProtocol=-91006;
FILE* logFile=nullptr;
std::wstring outputDirectory;
bool releaseMode=false,statusLogging=true;
#ifdef KL1_OPENXR_HOST
constexpr const char* kBackend="OpenXR";
#else
constexpr const char* kBackend="Meta";
#endif

void Log(const char* message) {
    if(releaseMode)return;
    std::printf("%s\n",message);std::fflush(stdout);
    if(logFile){std::fprintf(logFile,"[%s pid=%lu tick=%lu] %s\n",DiagnosticsSession::Utc().c_str(),GetCurrentProcessId(),GetTickCount(),message);std::fflush(logFile);}
}

template<class T> void Release(T*& resource) {
    if(resource){resource->Release();resource=nullptr;}
}
template<class T> bool Resolve(HMODULE module,const char* name,T& function) {
    function=reinterpret_cast<T>(GetProcAddress(module,name));return function!=nullptr;
}

bool FrameSubmissionDue(bool updated,DWORD now,const MetaSubmitPacing::Cadence& cadence,DWORD interval) {
#ifdef KL1_OPENXR_HOST
    // xrWaitFrame already paces the host. Every begun XR frame must keep the
    // committed scene/panel layer, even when the source game has no new pair.
    // A second millisecond throttle falls through to FinishIdle's zero-layer
    // EndFrame and blanks one display frame (SteamVR and VDXR alike).
    (void)updated;(void)now;(void)cadence;(void)interval;
    return KL1OpenXR::state.frameBegun;
#else
    return cadence.Due(updated,now,interval);
#endif
}

// K&L renders nothing while its window is unfocused, and compositor pacing
// benefits from short sleeps; request 1 ms timer resolution for the run.
class ScopedTimerResolution {
public:
    ScopedTimerResolution() {
        module_=LoadLibraryExW(L"winmm.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        TimerFunction begin=nullptr;
        if(module_&&Resolve(module_,"timeBeginPeriod",begin)&&
           Resolve(module_,"timeEndPeriod",end_)&&begin(1)==0) {
            active_=true;
            Log("Host scheduling: 1 ms timer resolution requested for compositor capture.");
        }
    }
    ~ScopedTimerResolution() {
        if(active_)end_(1);
        if(module_)FreeLibrary(module_);
    }
    ScopedTimerResolution(const ScopedTimerResolution&)=delete;
    ScopedTimerResolution& operator=(const ScopedTimerResolution&)=delete;
private:
    using TimerFunction=UINT(WINAPI*)(UINT);
    HMODULE module_=nullptr;
    TimerFunction end_=nullptr;
    bool active_=false;
};

bool FinitePose(const ovrPosef& pose) {
    const auto& q=pose.Orientation;const auto& p=pose.Position;
    const float length=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
    return std::isfinite(length)&&length>0.9f&&length<1.1f&&
        std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);
}

ovrVector3f Rotate(const ovrQuatf& q,const ovrVector3f& value) {
    const ovrVector3f twiceCross{2*(q.y*value.z-q.z*value.y),
        2*(q.z*value.x-q.x*value.z),2*(q.x*value.y-q.y*value.x)};
    return {value.x+q.w*twiceCross.x+q.y*twiceCross.z-q.z*twiceCross.y,
        value.y+q.w*twiceCross.y+q.z*twiceCross.x-q.x*twiceCross.z,
        value.z+q.w*twiceCross.z+q.x*twiceCross.y-q.y*twiceCross.x};
}

class Renderer {
public:
    HMODULE module=nullptr;
    ovrSession session=nullptr;
    bool initialized=false;
    ovrHmdDesc hmd{};
    ovrEyeRenderDesc eyeDescription[2]{};
    ovrSizei recommendedSize[2]{};
    ovrTextureSwapChain chains[2]{};
    ID3D11Device* device=nullptr;
    ID3D11DeviceContext* context=nullptr;
    std::vector<ID3D11Texture2D*> textures[2];
    std::vector<std::shared_ptr<GPUFrame::UploadTarget>> gpuTargets[2];
    unsigned width=0,height=0;
    unsigned runtimeEyeWidth[2]{},runtimeEyeHeight[2]{};
    int error=0;
    char errorText[512]="Starting Meta compositor.";
    ovrResult lastSubmit=ovrSuccess;
    ovrSessionStatus previousStatus{};
    bool statusKnown=false;
    bool gpuActive=false;
    const char* gpuReason="disabled";
    std::uint64_t gpuCopies=0,gpuBytesAvoided=0;
    std::uint64_t gpuDirectUploads=0,gpuUploadFallbacks=0,xrDirectImageWrites=0,newScenePairs=0,unavailableUploads=0;
    bool gpuCompletePairs=true;std::uint64_t gpuScenePairsRead=0,gpuPanelFramesRead=0,gpuPanelOlderFramesRead=0;
    bool releaseUploadedSources=true;std::uint64_t gpuSourceLeasesReleased=0;
    FrameMetrics::RateWindow pairRate;
    unsigned long long submitted=0,xrNoRenderFrames=0;
    FrameMetrics::RateWindow submitRate;
    std::uint64_t uploadCalls=0,uploadTotalUs=0,submitTotalUs=0;
    MetaSubmitPacing::Cadence submitCadence;
    FramePacing::Samples submitStartIntervals,submitCallDurations;
    FrameDelivery::Metrics sceneDelivery;
    std::uint64_t previousSubmitCounter=0;
    ovrLayerQuad quad{};
    ovrLayerEyeFov layer{};
    float panelWidth=3.6f,panelDistance=2.0f;
    const char* renderMode="waiting";
    decltype(&ovr_Initialize) initialize=nullptr;
    decltype(&ovr_Shutdown) shutdown=nullptr;
    decltype(&ovr_Create) create=nullptr;
    decltype(&ovr_Destroy) destroy=nullptr;
    decltype(&ovr_SetTrackingOriginType) setOrigin=nullptr;
    decltype(&ovr_GetHmdDesc) getHmd=nullptr;
    decltype(&ovr_GetRenderDesc) getEye=nullptr;
    decltype(&ovr_GetFovTextureSize) getSize=nullptr;
    decltype(&ovr_GetTrackingState) getTracking=nullptr;
    decltype(&ovr_GetTimeInSeconds) getTime=nullptr;
    decltype(&ovr_GetPredictedDisplayTime) getDisplayTime=nullptr;
    decltype(&ovr_GetSessionStatus) getStatus=nullptr;
    decltype(&ovr_GetLastErrorInfo) lastError=nullptr;
    decltype(&ovr_RecenterTrackingOrigin) recenter=nullptr;
    decltype(&ovr_GetInputState) getInput=nullptr;
    decltype(&ovr_SetControllerVibration) setVibration=nullptr;
    decltype(&ovr_CreateTextureSwapChainDX) createChain=nullptr;
    decltype(&ovr_GetTextureSwapChainLength) chainLength=nullptr;
    decltype(&ovr_GetTextureSwapChainCurrentIndex) chainIndex=nullptr;
    decltype(&ovr_GetTextureSwapChainBufferDX) chainBuffer=nullptr;
    decltype(&ovr_CommitTextureSwapChain) commitChain=nullptr;
    decltype(&ovr_DestroyTextureSwapChain) destroyChain=nullptr;
    decltype(&ovr_SubmitFrame) submit=nullptr;

    ~Renderer(){Close();}
    void Error(int code,const char* text) {
        error=code;std::snprintf(errorText,sizeof(errorText),"%s",text);std::fprintf(stderr,"%s\n",errorText);Log(errorText);
    }
    void ApiError(ovrResult result,const char* operation) {
        ovrErrorInfo info{};if(lastError)lastError(&info);
        char message[512];std::snprintf(message,sizeof(message),"%s failed (%d): %.420s",operation,result,info.ErrorString);
        Error(result,message);
    }
    bool Hr(HRESULT result,const char* operation) {
        if(SUCCEEDED(result))return true;
        char message[256];std::snprintf(message,sizeof(message),"%s failed (HRESULT 0x%08lx).",operation,static_cast<unsigned long>(result));
        Error(kGraphicsFailed,message);return false;
    }
    void CloseChains() {
        for(unsigned eye=0;eye<2;++eye) {
            gpuTargets[eye].clear();
            for(auto& texture:textures[eye])Release(texture);
            textures[eye].clear();
            if(chains[eye]&&destroyChain&&session)destroyChain(session,chains[eye]);
            chains[eye]=nullptr;
        }
        width=height=0;quad={};layer={};
    }
    void Close() {
        CloseChains();Release(context);Release(device);
        if(session&&destroy)destroy(session);session=nullptr;
        if(initialized&&shutdown)shutdown();initialized=false;
        if(module)FreeLibrary(module);module=nullptr;
    }
    bool Connect() {
#ifdef KL1_OPENXR_HOST
        KL1OpenXR::SetLog(&Log);
        initialize=&KL1OpenXR::Initialize;shutdown=&KL1OpenXR::Shutdown;
        create=&KL1OpenXR::Create;destroy=&KL1OpenXR::DestroySession;setOrigin=&KL1OpenXR::SetOrigin;
        getHmd=&KL1OpenXR::Hmd;getEye=&KL1OpenXR::Eye;getSize=&KL1OpenXR::Size;
        getTracking=&KL1OpenXR::Tracking;getTime=&KL1OpenXR::Time;getDisplayTime=&KL1OpenXR::DisplayTime;
        getStatus=&KL1OpenXR::Status;lastError=&KL1OpenXR::ErrorInfo;recenter=&KL1OpenXR::Recenter;getInput=&KL1OpenXR::Input;
        createChain=&KL1OpenXR::CreateChain;chainLength=&KL1OpenXR::ChainLength;chainIndex=&KL1OpenXR::ChainIndex;
        chainBuffer=&KL1OpenXR::ChainBuffer;commitChain=&KL1OpenXR::CommitChain;destroyChain=&KL1OpenXR::DestroyChain;submit=&KL1OpenXR::Submit;
#else
        const auto runtime=MetaRuntime::Load();module=runtime.module;
        if(!module){Error(kRuntimeMissing,runtime.message.c_str());return false;}
        Log(runtime.message.c_str());
        const char* missing=nullptr;
#define GET(member,name) if(!missing&&!Resolve(module,name,member))missing=name
        GET(initialize,"ovr_Initialize");GET(shutdown,"ovr_Shutdown");
        GET(create,"ovr_Create");GET(destroy,"ovr_Destroy");
        GET(setOrigin,"ovr_SetTrackingOriginType");
        GET(getHmd,"ovr_GetHmdDesc");GET(getEye,"ovr_GetRenderDesc");
        GET(getSize,"ovr_GetFovTextureSize");GET(getTracking,"ovr_GetTrackingState");
        GET(getTime,"ovr_GetTimeInSeconds");GET(getDisplayTime,"ovr_GetPredictedDisplayTime");
        GET(getStatus,"ovr_GetSessionStatus");GET(lastError,"ovr_GetLastErrorInfo");
        GET(recenter,"ovr_RecenterTrackingOrigin");
        GET(getInput,"ovr_GetInputState");Resolve(module,"ovr_SetControllerVibration",setVibration);
        GET(createChain,"ovr_CreateTextureSwapChainDX");
        GET(chainLength,"ovr_GetTextureSwapChainLength");
        GET(chainIndex,"ovr_GetTextureSwapChainCurrentIndex");
        GET(chainBuffer,"ovr_GetTextureSwapChainBufferDX");
        GET(commitChain,"ovr_CommitTextureSwapChain");
        GET(destroyChain,"ovr_DestroyTextureSwapChain");GET(submit,"ovr_SubmitFrame");
#undef GET
        if(missing){char message[160];std::snprintf(message,sizeof(message),"Meta runtime is missing export %s.",missing);Error(kExportMissing,message);return false;}
#endif
        ovrInitParams params{};params.Flags=ovrInit_RequestVersion;
        params.RequestedMinorVersion=OVR_MINOR_VERSION;params.ConnectionTimeoutMS=2000;
        ovrResult result=initialize(&params);
        if(OVR_FAILURE(result)){ApiError(result,"ovr_Initialize");return false;}
        initialized=true;
        ovrGraphicsLuid luid{};result=create(&session,&luid);
        if(OVR_FAILURE(result)){ApiError(result,"ovr_Create");return false;}
        result=setOrigin(session,ovrTrackingOrigin_FloorLevel);
        if(OVR_FAILURE(result)){ApiError(result,"ovr_SetTrackingOriginType(FloorLevel)");return false;}
        hmd=getHmd(session);
        for(unsigned eye=0;eye<2;++eye){
            eyeDescription[eye]=getEye(session,static_cast<ovrEyeType>(eye),hmd.DefaultEyeFov[eye]);
            recommendedSize[eye]=getSize(session,static_cast<ovrEyeType>(eye),hmd.DefaultEyeFov[eye],1.0f);
        }
        IDXGIFactory1* factory=nullptr;
        if(!Hr(CreateDXGIFactory1(__uuidof(IDXGIFactory1),reinterpret_cast<void**>(&factory)),"CreateDXGIFactory1"))return false;
        IDXGIAdapter1* selected=nullptr;
        for(unsigned index=0;;++index){
            IDXGIAdapter1* adapter=nullptr;
            if(factory->EnumAdapters1(index,&adapter)==DXGI_ERROR_NOT_FOUND)break;
            if(!adapter)break;
            DXGI_ADAPTER_DESC1 desc{};
            if(SUCCEEDED(adapter->GetDesc1(&desc))&&std::memcmp(&desc.AdapterLuid,luid.Reserved,sizeof(luid.Reserved))==0){selected=adapter;break;}
            adapter->Release();
        }
        factory->Release();
        if(!selected){Error(kGraphicsFailed,"The runtime's graphics adapter LUID is unavailable. The host must use the headset's GPU.");return false;}
        D3D_FEATURE_LEVEL level{};
#ifdef KL1_OPENXR_HOST
        const D3D_FEATURE_LEVEL choices[]={D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_1,D3D_FEATURE_LEVEL_10_0};
        std::vector<D3D_FEATURE_LEVEL> levels;for(auto choice:choices)if(choice>=KL1OpenXR::state.requirements.minFeatureLevel)levels.push_back(choice);
        HRESULT hr=levels.empty()?E_FAIL:D3D11CreateDevice(selected,D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            levels.data(),static_cast<unsigned>(levels.size()),D3D11_SDK_VERSION,&device,&level,&context);
#else
        HRESULT hr=D3D11CreateDevice(selected,D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr,0,D3D11_SDK_VERSION,&device,&level,&context);
#endif
        selected->Release();
        if(!Hr(hr,"D3D11CreateDevice on headset adapter"))return false;
#ifdef KL1_OPENXR_HOST
        if(!KL1OpenXR::AttachDevice(device)){ApiError(KL1OpenXR::state.error,"OpenXR graphics/session setup");return false;}
#endif
        char message[240];std::snprintf(message,sizeof(message),"PC VR compositor connected: %.64s; IPD %.4f m; refresh %.1f Hz.",
            hmd.ProductName,eyeDescription[1].HmdToEyeOffset.x-eyeDescription[0].HmdToEyeOffset.x,hmd.DisplayRefreshRate);Log(message);
        error=0;std::snprintf(errorText,sizeof(errorText),"Meta compositor connected; waiting for game frames.");
        return true;
    }
    bool Status(ovrSessionStatus& out) {
        // The installed runtime may append status fields beyond SDK 1.10.
        struct Storage{ovrSessionStatus value;unsigned char extra[64];} storage{};
        ovrResult result=getStatus(session,&storage.value);
        if(OVR_FAILURE(result)){ApiError(result,"ovr_GetSessionStatus");return false;}
        out=storage.value;
#ifdef KL1_OPENXR_HOST
        if(KL1OpenXR::Configured()){
            hmd=getHmd(session);
            for(unsigned eye=0;eye<2;++eye){eyeDescription[eye]=getEye(session,static_cast<ovrEyeType>(eye),hmd.DefaultEyeFov[eye]);
                recommendedSize[eye]=getSize(session,static_cast<ovrEyeType>(eye),hmd.DefaultEyeFov[eye],1.f);}
        }
#endif
        if(!statusKnown||std::memcmp(&previousStatus,&out,sizeof(out))!=0){
            char message[200];std::snprintf(message,sizeof(message),"PC VR session: visible=%d present=%d mounted=%d displayLost=%d shouldQuit=%d.",
                out.IsVisible,out.HmdPresent,out.HmdMounted,out.DisplayLost,out.ShouldQuit);Log(message);
            if(!out.IsVisible)Log("Headset output is not visible yet. Enter the PC VR environment and close its desktop/dashboard overlay.");
            previousStatus=out;statusKnown=true;
        }
        return true;
    }
    bool EnsureChains(unsigned imageWidth,unsigned imageHeight) {
        if(width==imageWidth&&height==imageHeight&&chains[0]&&chains[1])return true;
        if(!VRIPC::ValidImageDimensions(imageWidth,imageHeight,VRIPC::kGPU)){
            Error(kBadFrame,"Capture dimensions are outside the shared-frame limits.");return false;
        }
        CloseChains();
        for(unsigned eye=0;eye<2;++eye){
            ovrTextureSwapChainDesc desc{};
            desc.Type=ovrTexture_2D;desc.Format=OVR_FORMAT_B8G8R8A8_UNORM_SRGB;
            desc.ArraySize=1;desc.Width=static_cast<int>(imageWidth);desc.Height=static_cast<int>(imageHeight);
            desc.MipLevels=1;desc.SampleCount=1;desc.StaticImage=ovrFalse;
            desc.MiscFlags=ovrTextureMisc_DX_Typeless;desc.BindFlags=ovrTextureBind_DX_RenderTarget;
            ovrResult result=createChain(session,device,&desc,&chains[eye]);
            if(OVR_FAILURE(result)){ApiError(result,"ovr_CreateTextureSwapChainDX");return false;}
            int count=0;result=chainLength(session,chains[eye],&count);
            if(OVR_FAILURE(result)||count<1||count>16){ApiError(OVR_FAILURE(result)?result:ovrError_InvalidParameter,"ovr_GetTextureSwapChainLength");return false;}
            textures[eye].resize(static_cast<unsigned>(count),nullptr);
            for(int index=0;index<count;++index){
                result=chainBuffer(session,chains[eye],index,__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&textures[eye][static_cast<unsigned>(index)]));
                if(OVR_FAILURE(result)){ApiError(result,"ovr_GetTextureSwapChainBufferDX");return false;}
                if(index==0){D3D11_TEXTURE2D_DESC actual{};textures[eye][0]->GetDesc(&actual);
                    runtimeEyeWidth[eye]=actual.Width;runtimeEyeHeight[eye]=actual.Height;}
            }
        }
        width=imageWidth;height=imageHeight;
        layer.Header.Type=ovrLayerType_EyeFov;layer.Header.Flags=ovrLayerFlag_HighQuality;
        for(unsigned eye=0;eye<2;++eye){layer.ColorTexture[eye]=chains[eye];layer.Viewport[eye].Size={static_cast<int>(width),static_cast<int>(height)};}
        quad.Header.Type=ovrLayerType_Quad;quad.Header.Flags=ovrLayerFlag_HighQuality|ovrLayerFlag_HeadLocked;
        quad.ColorTexture=chains[0];
        quad.Viewport={};quad.Viewport.Size={static_cast<int>(width),static_cast<int>(height)};
        quad.QuadPoseCenter={};quad.QuadPoseCenter.Orientation.w=1;
        quad.QuadPoseCenter.Position.z=-panelDistance;
        quad.QuadSize={panelWidth,panelWidth*static_cast<float>(height)/static_cast<float>(width)};
        char message[128];std::snprintf(message,sizeof(message),"Two eye swapchains created: %u x %u BGRA sRGB.",width,height);Log(message);
        return true;
    }
    bool Upload(unsigned eye,const unsigned char* pixels,unsigned stride,ID3D11Texture2D* gpuImage=nullptr,
                const std::shared_ptr<GPUFrame::ImageLease>& lease={},[[maybe_unused]] bool projection=false,
                [[maybe_unused]] bool finishSourceInCopyFlush=false) {
        const auto uploadStart=FrameMetrics::Counter();
#ifdef KL1_OPENXR_HOST
        // Output already contains display-encoded BGRA. A projection does
        // not use alpha, so the runtime image can receive these bytes directly.
        auto& xr=KL1OpenXR::state;
        if(projection&&commitChain==&KL1OpenXR::CommitChain&&context==xr.context&&(gpuImage||pixels)){
            auto& chain=*reinterpret_cast<KL1OpenXR::Chain*>(chains[eye]);
            if(xr.CanUploadProjection(chain,gpuImage)){
                bool sourceRead=false,finishQueued=false;
                const bool finishHere=finishSourceInCopyFlush&&lease&&lease->ReadContextIdentity()==context;
                const bool ok=xr.CommitProjection(chain,pixels,stride,gpuImage,sourceRead,[&]{
                    if(!finishHere)return;
                    // This is the last read of the texture. Its event/fence
                    // travels in the same submission as the XR image copy.
                    lease->completion.used=true;
                    lease->QueueFinishRead();
                    finishQueued=true;
                });
                if(lease&&sourceRead)lease->completion.used=true;
                // Once afterCopy has run, CommitImage flushes before trying
                // xrReleaseSwapchainImage, including a runtime release error.
                if(finishQueued)lease->CompleteFinishRead();
                if(!ok){ApiError(xr.error,"OpenXR direct projection upload");return false;}
                ++xrDirectImageWrites;if(lease)++gpuDirectUploads;
                uploadTotalUs+=FrameMetrics::Microseconds(uploadStart);++uploadCalls;return true;
            }
        }
#endif
        int index=0;ovrResult result=chainIndex(session,chains[eye],&index);
        if(OVR_FAILURE(result)){ApiError(result,"ovr_GetTextureSwapChainCurrentIndex");return false;}
        if(index<0||static_cast<unsigned>(index)>=textures[eye].size()){Error(kBadFrame,"Meta returned an invalid swapchain texture index.");return false;}
        auto* targetImage=textures[eye][static_cast<unsigned>(index)];
        if(gpuImage){
            if(lease){
                gpuTargets[eye].resize(textures[eye].size());auto& target=gpuTargets[eye][static_cast<unsigned>(index)];
                if(!target){auto next=std::make_shared<GPUFrame::UploadTarget>();
                    if(!next->Create(device,targetImage)){Error(kGraphicsFailed,"Cannot create Meta GPU upload target.");return false;}
                    target=std::move(next);}
                lease->Draw(target->rtv.p,width,height);
                if(target->intermediate.p){context->CopyResource(targetImage,target->intermediate.p);++gpuUploadFallbacks;}
                else ++gpuDirectUploads;
            }else context->CopyResource(targetImage,gpuImage);
            context->Flush();
            if(!Hr(device->GetDeviceRemovedReason(),"Meta GPU upload"))return false;
        }
        else if(pixels)context->UpdateSubresource(textures[eye][static_cast<unsigned>(index)],0,nullptr,pixels,stride,0);
        else return false;
        result=commitChain(session,chains[eye]);
        if(OVR_FAILURE(result)){ApiError(result,"ovr_CommitTextureSwapChain");return false;}
        uploadTotalUs+=FrameMetrics::Microseconds(uploadStart);++uploadCalls;return true;
    }
    bool CanHoldStereo()const{
        return chains[0]&&chains[1]&&(!std::strcmp(renderMode,"scene")||!std::strcmp(renderMode,"scene_hold"));
    }
    void SetRenderMode(const char* mode) {
        if(std::strcmp(renderMode,mode)==0)return;
        const bool ordinaryPairWait=(std::strcmp(renderMode,"scene")==0&&std::strcmp(mode,"scene_hold")==0)||
            (std::strcmp(renderMode,"scene_hold")==0&&std::strcmp(mode,"scene")==0);
        const bool ordinaryLoadingWait=(std::strcmp(renderMode,"loading")==0&&std::strcmp(mode,"loading_hold")==0)||
            (std::strcmp(renderMode,"loading_hold")==0&&std::strcmp(mode,"loading")==0);
        renderMode=mode;
        if(ordinaryPairWait||ordinaryLoadingWait)return;
        char message[160];std::snprintf(message,sizeof(message),"Compositor render mode: %s.",mode);Log(message);
        if(std::strcmp(mode,"stale")==0)Log("Source frames stopped updating; holding the last committed layer. Return focus to the game window.");
    }
    std::uint64_t BeginSubmit() {
        CrashDiagnostics::Breadcrumb("host_submit_begin",submitted+1);
        const auto counter=FrameMetrics::Counter();
        if(previousSubmitCounter)submitStartIntervals.Add(FrameMetrics::Microseconds(previousSubmitCounter));
        previousSubmitCounter=counter;submitCadence.Begin(GetTickCount());return counter;
    }
    bool SubmitQuad(double /*sampleTime*/,bool scope=false) {
        MonoFrame::Extent panel;
        if(!MonoFrame::PanelExtent(width,height,panelWidth,panelDistance,panel))return false;
        quad.QuadPoseCenter.Orientation={0,0,0,1};
        quad.QuadPoseCenter.Position={0,0,-panel.distance};
        quad.QuadSize={panel.width,panel.height};
        if(scope){
            quad.Header.Type=ovrLayerType_Quad;quad.Header.Flags=ovrLayerFlag_HighQuality|ovrLayerFlag_HeadLocked;
            quad.ColorTexture=chains[0];quad.Viewport={};quad.Viewport.Size={static_cast<int>(width),static_cast<int>(height)};
            float fov[2][4]{};
            for(unsigned eye=0;eye<2;++eye){const auto& view=eyeDescription[eye].Fov;
                fov[eye][0]=view.LeftTan;fov[eye][1]=view.RightTan;fov[eye][2]=view.UpTan;fov[eye][3]=view.DownTan;}
            MonoFrame::Extent extent;
            if(!MonoFrame::ScopeExtent(fov,width,height,panelDistance,extent)){
                Error(kBadFrame,"Cannot size native full-view image for the actual HMD FOV.");return false;
            }
            quad.QuadPoseCenter.Orientation={0,0,0,1};
            quad.QuadPoseCenter.Position={0,0,-extent.distance};quad.QuadSize={extent.width,extent.height};
        }
        const ovrLayerHeader* layers[]={&quad.Header};
        ovrViewScaleDesc scale{};scale.HmdSpaceToWorldScaleInMeters=1.0f;
        const auto submitStart=BeginSubmit();
        lastSubmit=submit(session,static_cast<long long>(submitted+1),&scale,layers,1);
        const auto submitElapsed=FrameMetrics::Microseconds(submitStart);submitCallDurations.Add(submitElapsed);
        if(OVR_FAILURE(lastSubmit)){ApiError(lastSubmit,"ovr_SubmitFrame");return false;}
#ifdef KL1_OPENXR_HOST
        if(!KL1OpenXR::LastSubmissionHadLayers())return true;
#endif
        submitTotalUs+=submitElapsed;
        submitRate.Observe(GetTickCount());++submitted;return true;
    }
    bool SubmitScope(){return SubmitQuad(0,true);}
    // Loading dialogue needs the whole image inside the same bounded panel as
    // menus. Scope overscan deliberately crops edges and loses its subtitles.
    bool SubmitLoading(){return SubmitQuad(0);}
    bool SubmitClear(){
        // A menu/context/stale scope proof must retire the previously held
        // optic layer even when the game has not produced its next image.
        const auto submitStart=BeginSubmit();
        lastSubmit=submit(session,static_cast<long long>(submitted+1),nullptr,nullptr,0);
        const auto submitElapsed=FrameMetrics::Microseconds(submitStart);submitCallDurations.Add(submitElapsed);
        if(OVR_FAILURE(lastSubmit)){ApiError(lastSubmit,"ovr_SubmitFrame(clear scope)");return false;}
        submitTotalUs+=submitElapsed;submitRate.Observe(GetTickCount());++submitted;return true;
    }
    bool SubmitStereo(double sampleTime) {
        // A held layer still describes its original tracking sample. Clearing
        // this timestamp during the half-pair interval obscures source latency.
        if(std::isfinite(sampleTime)&&sampleTime>0)layer.SensorSampleTime=sampleTime;
        const ovrLayerHeader* layers[]={&layer.Header};
        ovrViewScaleDesc scale{};scale.HmdSpaceToWorldScaleInMeters=1.0f;
        for(unsigned eye=0;eye<2;++eye)scale.HmdToEyeOffset[eye]=eyeDescription[eye].HmdToEyeOffset;
        const auto submitStart=BeginSubmit();
        lastSubmit=submit(session,static_cast<long long>(submitted+1),&scale,layers,1);
        const auto submitElapsed=FrameMetrics::Microseconds(submitStart);submitCallDurations.Add(submitElapsed);
        if(OVR_FAILURE(lastSubmit)){ApiError(lastSubmit,"ovr_SubmitFrame");return false;}
#ifdef KL1_OPENXR_HOST
        if(!KL1OpenXR::LastSubmissionHadLayers())return true;
#endif
        submitTotalUs+=submitElapsed;
        submitRate.Observe(GetTickCount());sceneDelivery.SceneSubmit(GetTickCount());++submitted;return true;
    }
};

// Only query the already-owned runtime here. In SteamVR, calling OpenVR
// Init/Shutdown in this process invalidates its live OpenXR connection.
// The separate launcher audio guard owns any Background OpenVR query.
struct AudioEndpointQuery {
    std::wstring runtime;
#ifdef KL1_OPENXR_HOST
    XrInstance instance=XR_NULL_HANDLE;
    PFN_xrGetAudioOutputDeviceGuidOculus getGuid=nullptr;
    std::wstring initialGuid;
    explicit AudioEndpointQuery(Renderer&){
        wchar_t name[128]{};MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,KL1OpenXR::state.runtimeName,-1,name,128);
        runtime=name;instance=KL1OpenXR::state.instance;initialGuid=KL1OpenXR::state.audioGuid;
        if(KL1OpenXR::state.api.GetInstanceProcAddr&&
            KL1OpenXR::state.api.GetInstanceProcAddr(instance,"xrGetAudioOutputDeviceGuidOculus",reinterpret_cast<PFN_xrVoidFunction*>(&getGuid))!=XR_SUCCESS)
            getGuid=nullptr;
    }
    static std::wstring Read(void* context){auto& self=*static_cast<AudioEndpointQuery*>(context);wchar_t guid[128]{};
        if(self.getGuid){if(self.getGuid(self.instance,guid)==XR_SUCCESS&&!guid[127]&&guid[0])return guid;}
        else if(!self.initialGuid.empty())return self.initialGuid;
        return {};
    }
#else
    using GetGuid=ovrResult(OVR_CDECL*)(wchar_t*);
    GetGuid getGuid=nullptr;
    explicit AudioEndpointQuery(Renderer& renderer):runtime(L"Meta"){
        getGuid=reinterpret_cast<GetGuid>(GetProcAddress(renderer.module,"ovr_GetAudioDeviceOutGuidStr"));
    }
    static std::wstring Read(void* context){auto& self=*static_cast<AudioEndpointQuery*>(context);wchar_t guid[128]{};
        return self.getGuid&&OVR_SUCCESS(self.getGuid(guid))&&!guid[127]?std::wstring(guid):std::wstring{};
    }
#endif
};

struct Source {
    HANDLE mapping=nullptr,process=nullptr;
    VRIPC::SharedFrames* shared=nullptr;
    DWORD pid=0;
    HandsIPC::Producer hands;
    DWORD handsRetryTick=0;
    HandsIPC::Frame lastHands{};
    ovrResult handsInputResult=ovrSuccess;
    unsigned handsControllerType=0;
    ~Source() {
        if(shared){InterlockedExchange(&shared->hostReady,0);UnmapViewOfFile(shared);}
        if(mapping)CloseHandle(mapping);if(process)CloseHandle(process);
    }
    bool Open(DWORD gamePid) {
        pid=gamePid;
        process=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,pid);
        if(!process)process=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
        if(!process){Log("Cannot open the source game process.");return false;}
        wchar_t name[100];VRIPC::MappingName(name,100,pid);
        const DWORD deadline=GetTickCount()+5000;
        do {
            mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,name);
            if(mapping)break;
            if(WaitForSingleObject(process,0)!=WAIT_TIMEOUT)break;
            Sleep(50);
        }while(static_cast<LONG>(deadline-GetTickCount())>0);
        if(!mapping){Log("The game's VR capture mapping is absent. Enable capture with F9 first.");return false;}
        shared=static_cast<VRIPC::SharedFrames*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(VRIPC::SharedFrames)));
        if(!shared){Log("Cannot map the game's VR frame buffers.");return false;}
        if(shared->magic!=VRIPC::kMagic||shared->version!=VRIPC::kVersion||shared->bytes!=sizeof(VRIPC::SharedFrames)||shared->gamePid!=pid){
            InterlockedExchange(&shared->hostError,kSourceProtocol);
            Log("Game capture and VR host use incompatible IPC versions.");return false;
        }
        InterlockedExchange(&shared->hostReady,0);InterlockedExchange(&shared->hostError,0);
        InterlockedExchange(&shared->hostSubmittedFrames,0);
        InterlockedExchange(&shared->trackingTick,0);
        // Preserve the sequence and origin generation across host restarts.
        // An idle game may not render while hostReady is zero and must still
        // reject an eye sample from the previous host when rendering resumes.
        const auto prior=static_cast<DWORD>(InterlockedCompareExchange(&shared->trackingSequence,0,0));
        InterlockedExchange(&shared->trackingSequence,static_cast<LONG>((prior+2u)&~1u));
        InterlockedExchange(&shared->recenterRequest,0);
        InterlockedIncrement(&shared->recenterAck);
        if(!hands.Open(pid))Log("Hands tracking channel could not be claimed; head tracking remains available.");
        return true;
    }
    bool Running() const {
        return shared&&InterlockedCompareExchange(&shared->captureEnabled,0,0)!=0&&
            InterlockedCompareExchange(&shared->shutdown,0,0)==0&&
            WaitForSingleObject(process,0)==WAIT_TIMEOUT;
    }
    void PublishConfiguration(const Renderer& renderer) {
#ifdef KL1_OPENXR_HOST
        // Placeholder symmetric FOV cannot authorize stereo game rendering.
        if(!KL1OpenXR::Configured()){InterlockedExchange(&shared->hostReady,0);return;}
#endif
        if(InterlockedCompareExchange(&shared->hostReady,0,0))return;
        for(unsigned eye=0;eye<2;++eye){
            const auto& offset=renderer.eyeDescription[eye].HmdToEyeOffset;
            const auto& fov=renderer.hmd.DefaultEyeFov[eye];
            shared->eyeOffset[eye][0]=offset.x;shared->eyeOffset[eye][1]=offset.y;shared->eyeOffset[eye][2]=offset.z;
            shared->eyeFov[eye][0]=fov.LeftTan;shared->eyeFov[eye][1]=fov.RightTan;
            shared->eyeFov[eye][2]=fov.UpTan;shared->eyeFov[eye][3]=fov.DownTan;
            shared->recommendedWidth[eye]=static_cast<unsigned>(renderer.recommendedSize[eye].w);
            shared->recommendedHeight[eye]=static_cast<unsigned>(renderer.recommendedSize[eye].h);
        }
        MemoryBarrier();InterlockedExchange(&shared->hostReady,1);
    }
    static HandsIPC::Pose HandPose(const ovrPosef& source,unsigned flags) {
        HandsIPC::Pose pose{};
        if(!FinitePose(source))return pose;
        pose.orientation[0]=source.Orientation.x;pose.orientation[1]=source.Orientation.y;
        pose.orientation[2]=source.Orientation.z;pose.orientation[3]=source.Orientation.w;
        pose.position[0]=source.Position.x;pose.position[1]=source.Position.y;pose.position[2]=source.Position.z;
        if(flags&ovrStatus_OrientationTracked)pose.flags|=HandsIPC::kOrientation;
        if(flags&ovrStatus_PositionTracked)pose.flags|=HandsIPC::kPosition;
        return pose;
    }
    void PublishHands(const Renderer& renderer,const ovrTrackingState& tracking,
                      const ovrSessionStatus& sessionStatus,double sampledAt) {
        const DWORD now=GetTickCount();
        // Another host may still be releasing this channel during F9/Link
        // recovery. A one-time claim failure must not disable hands forever.
        if(!hands.Ready()&&(!handsRetryTick||DWORD(now-handsRetryTick)>=1000)){
            handsRetryTick=now;
            if(hands.Open(pid))Log("Hands tracking channel claimed/recovered.");
        }
        HandsIPC::Frame frame{};
        frame.tick=GetTickCount();frame.sensorTime=sampledAt;
        frame.recenterAck=static_cast<std::uint32_t>(InterlockedCompareExchange(&shared->recenterAck,0,0));
        frame.present=sessionStatus.HmdPresent!=ovrFalse;
        frame.mounted=sessionStatus.HmdMounted&&sessionStatus.IsVisible&&!sessionStatus.DisplayLost&&!sessionStatus.ShouldQuit;
        frame.head=HandPose(tracking.HeadPose.ThePose,tracking.StatusFlags);
        // The installed runtime can append fields beyond the vendored SDK.
        struct Storage {ovrInputState input;unsigned char extra[64];} storage{};
        handsInputResult=renderer.getInput?renderer.getInput(renderer.session,ovrControllerType_Touch,&storage.input):ovrError_Unsupported;
        const bool inputValid=OVR_SUCCESS(handsInputResult);handsControllerType=storage.input.ControllerType;
        for(unsigned side=0;side<2;++side) {
            if(!inputValid||(storage.input.ControllerType&(1u<<side))==0)continue;
            frame.hands[side]=HandPose(tracking.HandPoses[side].ThePose,tracking.HandStatusFlags[side]);
            const auto& velocity=tracking.HandPoses[side].LinearVelocity;
            if((frame.hands[side].flags&HandsIPC::kPosition)&&std::isfinite(velocity.x)&&std::isfinite(velocity.y)&&
               std::isfinite(velocity.z)&&velocity.x*velocity.x+velocity.y*velocity.y+velocity.z*velocity.z<=900.f){
                frame.linearVelocity[side][0]=velocity.x;frame.linearVelocity[side][1]=velocity.y;frame.linearVelocity[side][2]=velocity.z;
                frame.velocityValid[side]=1;
            }
            const float grip=storage.input.HandTrigger[side],trigger=storage.input.IndexTrigger[side];
            bool gripValid=std::isfinite(grip)&&grip>=-.01f&&grip<=1.01f;
            bool triggerValid=std::isfinite(trigger)&&trigger>=-.01f&&trigger<=1.01f;
#ifdef KL1_OPENXR_HOST
            gripValid=gripValid&&KL1OpenXR::state.status==XR_SESSION_STATE_FOCUSED&&KL1OpenXR::state.gripInputActive[side];
            triggerValid=triggerValid&&KL1OpenXR::state.status==XR_SESSION_STATE_FOCUSED&&KL1OpenXR::state.triggerInputActive[side];
#endif
            if(gripValid){frame.grip[side]=std::clamp(grip,0.f,1.f);frame.inputValid[side]|=HandsIPC::kGripInput;}
            if(triggerValid){frame.trigger[side]=std::clamp(trigger,0.f,1.f);frame.inputValid[side]|=HandsIPC::kTriggerInput;}
        }
        lastHands=frame;hands.Publish(frame);
        static ReloadFeedbackIPC::Channel feedback;feedback.Open(pid,false);
        feedback.Poll(now,frame.mounted&&sessionStatus.IsVisible&&inputValid&&shared->inputContext==VRIPC::kInputGame,[&](unsigned side,float amplitude,unsigned duration){
#ifdef KL1_OPENXR_HOST
            KL1OpenXR::Pulse(side,amplitude,duration);
#else
            (void)duration;if(renderer.setVibration)renderer.setVibration(renderer.session,side?ovrControllerType_RTouch:ovrControllerType_LTouch,amplitude>0?1.f:0.f,amplitude);
#endif
        });
    }
    // Latest predicted head pose for the game's transform hook; also serves
    // F7/stick-chord recenter requests coming from the game.
    void PublishTracking(Renderer& renderer,const ovrSessionStatus& sessionStatus) {
        if(sessionStatus.ShouldRecenter)InterlockedExchange(&shared->recenterRequest,1);
        if(InterlockedCompareExchange(&shared->recenterRequest,0,0)!=0) {
            // Never let the game adopt the old origin after acknowledging a
            // recenter. Ack and invalidation belong to the tracking seqlock,
            // so a reader cannot combine an old pose with the new origin ID.
            InterlockedIncrement(&shared->trackingSequence);
            InterlockedExchange(&shared->trackingTick,0);
            ovrResult result=renderer.recenter?renderer.recenter(renderer.session):ovrSuccess;
            bool deferred=false;
#ifdef KL1_OPENXR_HOST
            deferred=result==ovrError_InvalidOperation&&!KL1OpenXR::state.error;
#endif
            if(OVR_FAILURE(result)){if(!deferred)renderer.ApiError(result,"ovr_RecenterTrackingOrigin");}
            else {
                turnYaw=0;
                InterlockedExchange(&shared->recenterRequest,0);
                InterlockedIncrement(&shared->recenterAck);
                Log("Tracking origin recentered at the game's request.");
            }
            MemoryBarrier();InterlockedIncrement(&shared->trackingSequence);
        }
        // Pose requests otherwise run once per idle compositor iteration,
        // contending with the game's render thread between eye-pair updates.
        // QPC gives a real 2 ms limit even when GetTickCount has a coarse timer.
        if(lastTrackingPoll&&FrameMetrics::Microseconds(lastTrackingPoll)<2000)return;
        lastTrackingPoll=FrameMetrics::Counter();
        const double displayTime=renderer.getDisplayTime(renderer.session,static_cast<long long>(renderer.submitted+1));
        const double sampledAt=renderer.getTime();
        const ovrTrackingState tracking=renderer.getTracking(renderer.session,displayTime,ovrFalse);
        PublishHands(renderer,tracking,sessionStatus,sampledAt);
        if((tracking.StatusFlags&ovrStatus_OrientationTracked)==0||!FinitePose(tracking.HeadPose.ThePose)) {
            InterlockedExchange(&shared->trackingTick,0);return;
        }
        const auto& q=tracking.HeadPose.ThePose.Orientation;
        const auto& p=tracking.HeadPose.ThePose.Position;
        InterlockedIncrement(&shared->trackingSequence);
        shared->trackingOrientation[0]=q.x;shared->trackingOrientation[1]=q.y;
        shared->trackingOrientation[2]=q.z;shared->trackingOrientation[3]=q.w;
        shared->trackingPosition[0]=p.x;shared->trackingPosition[1]=p.y;shared->trackingPosition[2]=p.z;
        shared->trackingMounted=sessionStatus.HmdMounted?1u:0u;
        shared->trackingSensorTime=sampledAt;
        shared->trackingTurnYaw=turnYaw;
        MemoryBarrier();
        InterlockedExchange(&shared->trackingTick,static_cast<LONG>(GetTickCount()));
        InterlockedIncrement(&shared->trackingSequence);
    }
    std::uint64_t lastTrackingPoll=0;
    float turnYaw=0;
};

struct CapturedEye {
    FrameMetrics::RateWindow sourceRate;
    std::uint64_t readCalls=0,readTotalUs=0,rejectedReads=0;
    LONG sequence=0;
    VRIPC::EyeMetadata metadata{};
    std::vector<unsigned char> pixels;
    std::vector<unsigned char> readScratch;
    std::shared_ptr<ID3D11Texture2D> gpuImage;
    std::shared_ptr<GPUFrame::ImageLease> gpuLease;
    std::uint32_t transportEpoch=0;
    std::uint32_t gpuSlot=0;
    std::uint32_t transport=VRIPC::kCPU;
    LONG uploadedSequence=0;
    std::uint64_t uploadedFrameId=0;
    std::uint32_t uploadedKind=0,uploadedEpoch=0;
    bool uploaded=false;
    bool AlreadyUploaded(const CapturedEye& source)const{
        return uploaded&&uploadedSequence==source.sequence&&uploadedFrameId==source.metadata.frameId&&
            uploadedKind==source.metadata.poseValid&&uploadedEpoch==source.transportEpoch;
    }
    bool HasPixels()const{
        return gpuImage||(!pixels.empty()&&pixels.size()>=std::size_t(metadata.stride)*metadata.height);
    }
};

// Meta owns the destination texture after Upload. After the LAST queued
// source read (including explicit capture), release direct source leases now,
// instead of pinning one of only two slots per eye until another pair arrives.
// ImageLease queues its done fence before making the slot reusable.
unsigned ReleaseUploadedSources(CapturedEye (&eyes)[2]){
    GPUFrame::ImageLease* leases[2]={eyes[0].gpuLease.get(),eyes[1].gpuLease.get()};
    // All uploads and an optional diagnostic source read have been queued.
    // Signal both source fences, submit once per context, then free slots.
    GPUFrame::ImageLease::BatchRelease(leases,2);
    unsigned released=0;for(auto& eye:eyes)if(eye.gpuLease){eye.gpuImage.reset();eye.gpuLease.reset();++released;}return released;
}

// Explicit one-shot diagnostic only. Normal GPU transport never maps pixels.
bool SaveEyeBMP(Renderer& renderer,const CapturedEye& eye,const std::wstring& path){
    const auto& m=eye.metadata;if(!VRIPC::ValidImageDimensions(m.width,m.height,eye.gpuImage?VRIPC::kGPU:VRIPC::kCPU))return false;
    std::vector<unsigned char> pixels;
    if(eye.gpuImage){
        D3D11_TEXTURE2D_DESC desc{};eye.gpuImage->GetDesc(&desc);
        if(desc.Width!=m.width||desc.Height!=m.height)return false;
        desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=desc.MiscFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        GPUFrame::Object<ID3D11Texture2D> staging;GPUFrame::Object<ID3D11Query> event;
        D3D11_QUERY_DESC query{D3D11_QUERY_EVENT,0};
        if(FAILED(renderer.device->CreateTexture2D(&desc,nullptr,staging.Put()))||FAILED(renderer.device->CreateQuery(&query,event.Put())))return false;
        renderer.context->CopyResource(staging.p,eye.gpuImage.get());renderer.context->End(event.p);renderer.context->Flush();
        const DWORD start=GetTickCount();HRESULT ready;
        while((ready=renderer.context->GetData(event.p,nullptr,0,D3D11_ASYNC_GETDATA_DONOTFLUSH))==S_FALSE&&DWORD(GetTickCount()-start)<1000)Sleep(1);
        if(ready!=S_OK)return false;
        D3D11_MAPPED_SUBRESOURCE mapped{};if(FAILED(renderer.context->Map(staging.p,0,D3D11_MAP_READ,0,&mapped)))return false;
        pixels.resize(std::size_t(m.width)*m.height*4);
        for(unsigned y=0;y<m.height;++y)std::memcpy(pixels.data()+std::size_t(y)*m.width*4,static_cast<unsigned char*>(mapped.pData)+std::size_t(y)*mapped.RowPitch,m.width*4);
        renderer.context->Unmap(staging.p,0);
        // A direct lease exposes native alpha (often zero). Meta's opaque
        // draw sets it to one; diagnostic BMPs must show the same bytes.
        for(std::size_t alpha=3;alpha<pixels.size();alpha+=4)pixels[alpha]=255;
    }else{if(eye.pixels.size()<std::size_t(m.stride)*m.height)return false;pixels=eye.pixels;}
    BITMAPFILEHEADER header{};BITMAPINFOHEADER info{};header.bfType=0x4D42;header.bfOffBits=sizeof(header)+sizeof(info);header.bfSize=header.bfOffBits+static_cast<DWORD>(pixels.size());
    info.biSize=sizeof(info);info.biWidth=m.width;info.biHeight=-static_cast<LONG>(m.height);info.biPlanes=1;info.biBitCount=32;info.biCompression=BI_RGB;
    FILE* file=_wfopen(path.c_str(),L"wb");if(!file)return false;
    const bool ok=std::fwrite(&header,1,sizeof(header),file)==sizeof(header)&&std::fwrite(&info,1,sizeof(info),file)==sizeof(info)&&std::fwrite(pixels.data(),1,pixels.size(),file)==pixels.size();
    std::fclose(file);return ok;
}
std::wstring PendingEyeCapture(const CapturedEye eyes[2],bool stereo,unsigned monoEye){
    // A stale research request must not trigger GPU readback, consume the
    // request file or create screenshots in an installed release.
    if(releaseMode)return {};
    // A held compositor layer can outlive its released source. Defer a new
    // capture request until fresh source images are acquired; do not consume
    // the request and misreport this expected lifetime as a failed readback.
    for(unsigned side=0;side<(stereo?2u:1u);++side){const auto& e=eyes[stereo?side:monoEye];
        if(e.transport==VRIPC::kGPU&&!e.gpuImage)return {};}
    static DWORD last=0;const DWORD now=GetTickCount();if(last&&DWORD(now-last)<200)return {};last=now;
    auto request=outputDirectory+L"KaneLynchVR-capture.request";
    if(GetFileAttributesW(request.c_str())==INVALID_FILE_ATTRIBUTES)return {};
    return request;
}
void CaptureRequestedEyes(Renderer& renderer,const CapturedEye eyes[2],bool stereo,unsigned monoEye,const std::wstring& request){
    if(!DeleteFileW(request.c_str()))return;
    const DWORD now=GetTickCount();
    const auto directory=outputDirectory+L"captures";CreateDirectoryW(directory.c_str(),nullptr);
    const auto prefix=L"capture-"+std::to_wstring(now);bool ok=true;
    for(unsigned eye=0;eye<2;++eye)ok=SaveEyeBMP(renderer,eyes[stereo?eye:monoEye],directory+L"\\"+prefix+(eye?L"-right.bmp":L"-left.bmp"))&&ok;
    const auto& left=eyes[stereo?0:monoEye].metadata;const auto& right=eyes[stereo?1:monoEye].metadata;
    const auto report=[&](const std::wstring& path){FILE* f=_wfopen(path.c_str(),L"w");if(!f)return;
        std::fprintf(f,"{\"ok\":%s,\"tick\":%lu,\"stereo\":%s,\"leftFrameId\":%llu,\"rightFrameId\":%llu,\"leftKind\":%u,\"rightKind\":%u,\"gpu\":%s,\"prefix\":\"capture-%lu\"}\n",
            ok?"true":"false",now,stereo?"true":"false",(unsigned long long)left.frameId,(unsigned long long)right.frameId,left.poseValid,right.poseValid,
            eyes[stereo?0:monoEye].gpuImage?"true":"false",now);std::fclose(f);};
    report(directory+L"\\"+prefix+L".json");report(outputDirectory+L"KaneLynchVR-capture-result.json");
    Log(ok?"Explicit diagnostic eye capture saved (one-shot GPU readback).":"Explicit eye capture failed; see capture result.");
}

bool ValidMetadata(const VRIPC::EyeMetadata& metadata,bool stereo,unsigned transport=VRIPC::kCPU) {
    return MonoFrame::ValidMetadata(metadata,stereo,transport);
}

CPUFrame::Reader cpuPixelReader;
bool ReadEye(const VRIPC::EyeFrame& shared,CapturedEye& eye,DWORD minimumTick,GPUFrame::Consumer* gpu=nullptr,unsigned side=0,
             const CapturedEye* monoCache=nullptr,bool allowGPUScene=true,const CPUFrame::Reader* cpuArena=nullptr) {
    auto* sequence=const_cast<volatile LONG*>(&shared.sequence);
    const LONG before=InterlockedCompareExchange(sequence,0,0);
    if(before==0||(before&1)!=0)return false;
    const auto transport=shared.transport,epoch=shared.transportEpoch,slot=shared.gpuSlot;
    if(before==eye.sequence&&epoch==eye.transportEpoch)return false;
    const auto readStart=FrameMetrics::Counter();
    VRIPC::EyeMetadata metadata{};std::memcpy(&metadata,&shared.metadata,sizeof(metadata));
    MemoryBarrier();if(before!=InterlockedCompareExchange(sequence,0,0)||shared.transportEpoch!=epoch)return false;
    if(!ValidMetadata(metadata,false,transport)||static_cast<LONG>(metadata.tick-minimumTick)<0)return false;
    // Complete-pair mode must not pin a lone scene source during UI->scene
    // publication. ReadLatestScenePair can claim only Ready slots; borrowing
    // one eye here would leave it Reading and shrink that eye's ring to one.
    if(!allowGPUScene&&transport==VRIPC::kGPU&&metadata.poseValid==VRIPC::kSceneFrame)return false;
    if(transport==VRIPC::kGPU) {
        ID3D11Texture2D* image=nullptr;std::shared_ptr<GPUFrame::ImageLease> lease;
        // Both UI eyes can refer to one LEFT-ring slot. Reuse a right-first
        // lease rather than trying to reclaim Reading or pinning both slots.
        if(MonoFrame::Kind(metadata.poseValid)&&monoCache&&monoCache->gpuImage&&
           monoCache->transportEpoch==epoch&&monoCache->gpuSlot==slot&&
           !std::memcmp(&monoCache->metadata,&metadata,sizeof(metadata))){
            lease=monoCache->gpuLease;image=monoCache->gpuImage.get();
        }else if(!gpu||!gpu->Read(side,slot,epoch,metadata,image,eye.gpuImage.get(),&lease))return false;
        MemoryBarrier();if(before!=InterlockedCompareExchange(sequence,0,0)||shared.transportEpoch!=epoch)return false;
        if(lease)eye.gpuImage={lease,image};
        else {image->AddRef();eye.gpuImage={image,[](auto* p){p->Release();}};}
        eye.gpuLease=std::move(lease);eye.pixels.clear();
    } else if(transport==VRIPC::kCPU) {
        const std::size_t bytes=static_cast<std::size_t>(metadata.stride)*metadata.height;
        CPUFrame::Snapshot publication;
        if(side>1||!cpuArena||cpuArena->epoch!=epoch||!cpuArena->SnapshotPublished(publication)||
           std::memcmp(&publication.metadata[side],&metadata,sizeof(metadata)))return false;
        const auto* cpuData=cpuArena->Pixels(publication,side);
        if(!cpuData)return false;
        try{eye.readScratch.resize(bytes);}catch(...){++eye.rejectedReads;return false;}
        std::memcpy(eye.readScratch.data(),cpuData,bytes);
        MemoryBarrier();const LONG after=InterlockedCompareExchange(sequence,0,0);
        if(before!=after||(after&1)!=0||shared.transportEpoch!=epoch||!cpuArena->StillPublished(publication)){++eye.rejectedReads;return false;}
        eye.pixels.swap(eye.readScratch);eye.gpuImage.reset();eye.gpuLease.reset();
    } else return false;
    eye.sequence=before;eye.transportEpoch=epoch;eye.gpuSlot=slot;eye.metadata=metadata;
    eye.transport=transport;
    eye.readTotalUs+=FrameMetrics::Microseconds(readStart);++eye.readCalls;
    eye.sourceRate.Observe(GetTickCount());return true;
}

void ReadEyes(const VRIPC::SharedFrames& shared,CapturedEye (&eyes)[2],DWORD minimumTick,bool (&changed)[2],GPUFrame::Consumer* gpu,bool completePairs=true){
    const auto epoch=shared.eyes[0].transportEpoch;
    const bool wantsCPU=shared.eyes[0].transport==VRIPC::kCPU||shared.eyes[1].transport==VRIPC::kCPU;
    const bool cpuReady=wantsCPU&&cpuPixelReader.Open(shared.gamePid,epoch);
    if(!wantsCPU)cpuPixelReader.Close();
    if(eyes[0].transportEpoch!=epoch){eyes[0]={};eyes[1]={};eyes[0].transportEpoch=eyes[1].transportEpoch=epoch;}
    if(cpuReady&&shared.eyes[0].transport==VRIPC::kCPU&&shared.eyes[1].transport==VRIPC::kCPU&&
       shared.eyes[1].transportEpoch==epoch){
        changed[0]=changed[1]=false;CPUFrame::Snapshot publication;
        if(!cpuPixelReader.SnapshotPublished(publication)||publication.publication==eyes[0].sequence)return;
        const bool scene=publication.metadata[0].poseValid==VRIPC::kSceneFrame;
        if((scene&&(publication.mask!=3||!StereoPairPolicy::Compatible(publication.metadata[0],publication.metadata[1])))||
           (!scene&&publication.mask!=1))return;
        const auto begin=FrameMetrics::Counter();const unsigned count=scene?2:1;
        for(unsigned side=0;side<count;++side){const auto& metadata=publication.metadata[side];
            if(!ValidMetadata(metadata,scene,VRIPC::kCPU)||static_cast<LONG>(metadata.tick-minimumTick)<0)return;
            const auto* pixels=cpuPixelReader.Pixels(publication,side);if(!pixels)return;
            const auto bytes=static_cast<std::size_t>(metadata.stride)*metadata.height;
            try{eyes[side].readScratch.resize(bytes);}catch(...){++eyes[side].rejectedReads;return;}
            std::memcpy(eyes[side].readScratch.data(),pixels,bytes);
        }
        MemoryBarrier();
        if(!cpuPixelReader.StillPublished(publication)||shared.eyes[0].transport!=VRIPC::kCPU||
           shared.eyes[1].transport!=VRIPC::kCPU||shared.eyes[0].transportEpoch!=epoch||shared.eyes[1].transportEpoch!=epoch){
            ++eyes[0].rejectedReads;return;
        }
        // Commit both CPU images only after one bank/epoch proof succeeds.
        // A later LEFT is staged in the other bank and cannot tear this pair.
        for(unsigned side=0;side<count;++side){auto& eye=eyes[side];eye.pixels.swap(eye.readScratch);
            eye.gpuImage.reset();eye.gpuLease.reset();eye.metadata=publication.metadata[side];
            eye.sequence=publication.publication;eye.transport=VRIPC::kCPU;eye.transportEpoch=epoch;eye.gpuSlot=publication.bank;
            eye.readTotalUs+=FrameMetrics::Microseconds(begin);++eye.readCalls;eye.sourceRate.Observe(GetTickCount());changed[side]=true;
        }
        if(!scene){eyes[1].gpuImage.reset();eyes[1].gpuLease.reset();eyes[1].sequence=0;}
        return;
    }
    bool gpuScene=completePairs&&gpu;LONG sequences[2]{};
    for(unsigned side=0;side<2&&gpuScene;++side){const auto& e=shared.eyes[side];
        sequences[side]=InterlockedCompareExchange(const_cast<volatile LONG*>(&e.sequence),0,0);
        const auto transport=e.transport,kind=e.metadata.poseValid,eyeEpoch=e.transportEpoch;MemoryBarrier();
        gpuScene=sequences[side]&&!(sequences[side]&1)&&transport==VRIPC::kGPU&&kind==VRIPC::kSceneFrame&&eyeEpoch==epoch&&
            sequences[side]==InterlockedCompareExchange(const_cast<volatile LONG*>(&e.sequence),0,0);
    }
    if(gpuScene){
        changed[0]=changed[1]=false;VRIPC::EyeMetadata metadata[2]{};ID3D11Texture2D* images[2]{};
        std::shared_ptr<GPUFrame::ImageLease> leases[2];ID3D11Texture2D* preserve[2]={eyes[0].gpuImage.get(),eyes[1].gpuImage.get()};
        const auto after=eyes[1].metadata.poseValid==VRIPC::kSceneFrame?eyes[1].metadata.frameId:0;
        const auto begin=FrameMetrics::Counter();
        if(!gpu->ReadLatestScenePair(after,minimumTick,epoch,metadata,images,leases,preserve))return;
        for(unsigned side=0;side<2;++side){const auto& e=shared.eyes[side];
            // A newly published UI frame/Reset wins over an older scene pair.
            const auto before=InterlockedCompareExchange(const_cast<volatile LONG*>(&e.sequence),0,0);
            const auto eyeEpoch=e.transportEpoch,transport=e.transport,kind=e.metadata.poseValid;MemoryBarrier();
            if(!before||(before&1)||before!=InterlockedCompareExchange(const_cast<volatile LONG*>(&e.sequence),0,0)||
                eyeEpoch!=epoch||transport!=VRIPC::kGPU||kind!=VRIPC::kSceneFrame||!ValidMetadata(metadata[side],true,VRIPC::kGPU))return;
        }
        for(unsigned side=0;side<2;++side){auto& eye=eyes[side];
            if(leases[side])eye.gpuImage={leases[side],images[side]};
            else {images[side]->AddRef();eye.gpuImage={images[side],[](auto* p){p->Release();}};}
            eye.gpuLease=std::move(leases[side]);eye.pixels.clear();eye.metadata=metadata[side];eye.transportEpoch=epoch;
            eye.transport=VRIPC::kGPU;
            eye.sequence=static_cast<LONG>((static_cast<unsigned>(eye.sequence)+2)&0x7fffffff);if(!eye.sequence)eye.sequence=2;
            eye.sourceRate.Observe(GetTickCount());eye.readTotalUs+=FrameMetrics::Microseconds(begin);++eye.readCalls;changed[side]=true;
        }
        return;
    }
    VRIPC::EyeMetadata announced{};
    if(gpu&&GPUFrame::ReadPanelAnnouncement(shared,gpu->pid,epoch,announced)){
        changed[0]=changed[1]=false;
        VRIPC::EyeMetadata metadata{};ID3D11Texture2D* image=nullptr;std::shared_ptr<GPUFrame::ImageLease> lease;unsigned slot=0;
        const auto after=eyes[0].metadata.poseValid==announced.poseValid?eyes[0].metadata.frameId:0;
        DWORD monoMinimum=minimumTick;
        if(eyes[0].metadata.poseValid!=announced.poseValid)for(const auto& old:eyes)
            if(old.sequence&&static_cast<LONG>(old.metadata.tick-monoMinimum)>0)monoMinimum=old.metadata.tick;
        const auto begin=FrameMetrics::Counter();
        if(gpu->ReadLatestPanel(after,monoMinimum,epoch,announced,metadata,image,lease,eyes[0].gpuImage.get(),&slot)){
            VRIPC::EyeMetadata current{};
            // Recheck published kind/epoch/producer after claiming the slot.
            // Destroying an unused direct lease returns it to Ready safely.
            if(!GPUFrame::ReadPanelAnnouncement(shared,gpu->pid,epoch,current)||
               !GPUFrame::PanelWithinAnnouncement(metadata,current))return;
            auto& eye=eyes[0];
            if(lease)eye.gpuImage={lease,image};
            else{image->AddRef();eye.gpuImage={image,[](auto* p){p->Release();}};}
            eye.gpuLease=std::move(lease);eye.pixels.clear();eye.metadata=metadata;eye.transportEpoch=epoch;eye.gpuSlot=slot;
            eye.transport=VRIPC::kGPU;
            eye.sequence=static_cast<LONG>((static_cast<unsigned>(eye.sequence)+2)&0x7fffffff);if(!eye.sequence)eye.sequence=2;
            eye.sourceRate.Observe(GetTickCount());eye.readTotalUs+=FrameMetrics::Microseconds(begin);++eye.readCalls;
            eyes[1].gpuImage.reset();eyes[1].gpuLease.reset();eyes[1].sequence=0;
            changed[0]=true;changed[1]=false;return;
        }
    }
    changed[0]=ReadEye(shared.eyes[0],eyes[0],minimumTick,gpu,0,&eyes[1],!completePairs,cpuReady?&cpuPixelReader:nullptr);changed[1]=false;
    const auto& right=shared.eyes[1];
    const LONG sequence=InterlockedCompareExchange(const_cast<volatile LONG*>(&right.sequence),0,0);
    VRIPC::EyeMetadata metadata{};std::memcpy(&metadata,&right.metadata,sizeof(metadata));
    const auto rightEpoch=right.transportEpoch;
    MemoryBarrier();
    const bool duplicate=sequence&&!(sequence&1)&&eyes[0].sequence&&MonoFrame::Kind(metadata.poseValid)&&
        rightEpoch==eyes[0].transportEpoch&&!std::memcmp(&metadata,&eyes[0].metadata,sizeof(metadata))&&
        sequence==InterlockedCompareExchange(const_cast<volatile LONG*>(&right.sequence),0,0)&&right.transportEpoch==rightEpoch;
    if(duplicate){
        // The already committed Meta texture survives; omitted source leases
        // must not keep a second left-ring slot in Reading indefinitely.
        eyes[1].gpuImage.reset();eyes[1].gpuLease.reset();eyes[1].sequence=0;
    }else changed[1]=ReadEye(right,eyes[1],minimumTick,gpu,1,nullptr,!completePairs,cpuReady?&cpuPixelReader:nullptr);
    MemoryBarrier();
    if(shared.eyes[0].transportEpoch!=epoch){changed[0]=changed[1]=false;eyes[0].sequence=eyes[1].sequence=0;}
}

ovrPosef EyePose(const VRIPC::EyeMetadata& frame,const ovrVector3f& offset) {
    ovrPosef pose{{frame.headOrientation[0],frame.headOrientation[1],frame.headOrientation[2],frame.headOrientation[3]},
        {frame.headPosition[0],frame.headPosition[1],frame.headPosition[2]}};
    const auto rotated=Rotate(pose.Orientation,offset);
    pose.Position.x+=rotated.x;pose.Position.y+=rotated.y;pose.Position.z+=rotated.z;return pose;
}

#include "kl1_touch_input.hpp"

void WriteStatus(const Renderer& renderer,const Source* source,const ovrSessionStatus& sessionStatus,
    const CapturedEye* eyes,const char* phase) {
    if(!statusLogging||releaseMode)return;
    HostFrameTiming::Scope timing(HostFrameTiming::StatusWrite);
    FILE* status=_wfopen((outputDirectory+L"KaneLynchVRHost-status.json").c_str(),L"wb");
    if(!status)return;
    std::fprintf(status,"{\"backend\":\"%s\",",kBackend);
    const DWORD now=GetTickCount();
    const unsigned ageLeft=eyes&&eyes[0].uploaded?now-eyes[0].metadata.tick:0;
    const unsigned ageRight=eyes&&eyes[1].uploaded?now-eyes[1].metadata.tick:0;
    float movementYaw=0;
    const bool movementYawValid=source&&source->shared&&ReadMovementYaw(source->shared,now,movementYaw);
    std::fprintf(status,"  \"gameplayHudHeld\":%s,\n",source&&source->shared&&VRIPC::ReadHudIntent(*source->shared,now)?"true":"false");
    const bool ready=source&&source->shared?InterlockedCompareExchange(&source->shared->hostReady,0,0)!=0:
        renderer.session&&renderer.device&&std::strstr(phase,"finished")==nullptr&&std::strstr(phase,"error")==nullptr;
    std::fprintf(status,"\n  \"gpuDirectUploads\":%llu,\"gpuUploadFallbacks\":%llu,\"xrDirectImageWrites\":%llu,\"unavailableUploads\":%llu,"
        "\"newScenePairs\":%llu,\"newScenePairHz\":%.2f,\n",
        static_cast<unsigned long long>(renderer.gpuDirectUploads),static_cast<unsigned long long>(renderer.gpuUploadFallbacks),
        static_cast<unsigned long long>(renderer.xrDirectImageWrites),static_cast<unsigned long long>(renderer.unavailableUploads),
        static_cast<unsigned long long>(renderer.newScenePairs),renderer.pairRate.Hz(now));
    if(source)std::fprintf(status,"\"handsChannelReady\":%s,\"handsTick\":%u,\"handsPresent\":%u,\"handsMounted\":%u,\"handsHeadFlags\":%u,\"handsFlags\":[%u,%u],\"handsInputValid\":[%u,%u],\"handsInputResult\":%d,\"handsControllerType\":%u,\n",
        source->hands.Ready()?"true":"false",source->lastHands.tick,source->lastHands.present,source->lastHands.mounted,
        source->lastHands.head.flags,source->lastHands.hands[0].flags,source->lastHands.hands[1].flags,
        source->lastHands.inputValid[0],source->lastHands.inputValid[1],int(source->handsInputResult),source->handsControllerType);
    std::fprintf(status,"  \"recommendedEyeSize\":[[%d,%d],[%d,%d]],\"hmdPanelSize\":[%d,%d],\n",
        renderer.recommendedSize[0].w,renderer.recommendedSize[0].h,renderer.recommendedSize[1].w,renderer.recommendedSize[1].h,
        renderer.hmd.Resolution.w,renderer.hmd.Resolution.h);
    std::fprintf(status,"\"runtimeEyeTextureSize\":[[%u,%u],[%u,%u]],\"sourceEyeSizeAndStride\":[[%u,%u,%u],[%u,%u,%u]],\n",
        renderer.runtimeEyeWidth[0],renderer.runtimeEyeHeight[0],renderer.runtimeEyeWidth[1],renderer.runtimeEyeHeight[1],
        eyes?eyes[0].metadata.width:0,eyes?eyes[0].metadata.height:0,eyes?eyes[0].metadata.stride:0,
        eyes?eyes[1].metadata.width:0,eyes?eyes[1].metadata.height:0,eyes?eyes[1].metadata.stride:0);
    std::fprintf(status,"  \"gpuCompletePairs\":%s,\"gpuScenePairsRead\":%llu,\n",renderer.gpuCompletePairs?"true":"false",static_cast<unsigned long long>(renderer.gpuScenePairsRead));
    std::fprintf(status,"  \"gpuPanelFramesRead\":%llu,\"gpuPanelOlderFramesRead\":%llu,\n",
        static_cast<unsigned long long>(renderer.gpuPanelFramesRead),static_cast<unsigned long long>(renderer.gpuPanelOlderFramesRead));
    std::fprintf(status,"  \"releaseUploadedSources\":%s,\"gpuSourceLeasesReleased\":%llu,\n",renderer.releaseUploadedSources?"true":"false",static_cast<unsigned long long>(renderer.gpuSourceLeasesReleased));
    std::fprintf(status,"  \"crashLogging\":%s,\"sessionDirectory\":%s,\"crashLog\":%s,\n",
        CrashDiagnostics::Active()?"true":"false",DiagnosticsSession::Quote(DiagnosticsSession::Utf8(CrashDiagnostics::Directory())).c_str(),
        DiagnosticsSession::Quote(DiagnosticsSession::Utf8(CrashDiagnostics::Path())).c_str());
    const auto submitIntervals=renderer.submitStartIntervals.Describe(),submitDurations=renderer.submitCallDurations.Describe();
    const auto deliveryIntervals=renderer.sceneDelivery.freshPairIntervals.Describe();
    std::fprintf(status,"\"xrNoRenderFrames\":%llu,\n",renderer.xrNoRenderFrames);
    std::fputs("\"frameDeliveryTiming\":{",status);
    for(unsigned i=0;i<HostFrameTiming::Count;++i){
        const auto& sample=HostFrameTiming::stats[i];
        std::fprintf(status,"%s\"%s\":{\"calls\":%llu,\"maxUs\":%llu,\"slowCalls\":%llu,\"lastSlowUs\":%llu,\"lastSlowTick\":%lu}",
            i?",":"",HostFrameTiming::Names[i],static_cast<unsigned long long>(sample.calls),static_cast<unsigned long long>(sample.maxUs),
            static_cast<unsigned long long>(sample.slowCalls),static_cast<unsigned long long>(sample.lastSlowUs),sample.lastSlowTick);
    }
    std::fputs("},\n",status);
    std::fputs("\"xrEndFrameBursts\":{",status);
    const auto writeBurst=[&](const char* name,const HostFrameTiming::EndFrameBurst& burst){
        std::fprintf(status,"\"%s\":{\"firstTick\":%lu,\"lastTick\":%lu,\"calls\":%llu,\"layeredCalls\":%llu,\"totalUs\":%llu,\"maxUs\":%llu}",
            name,burst.firstTick,burst.lastTick,static_cast<unsigned long long>(burst.calls),
            static_cast<unsigned long long>(burst.layeredCalls),static_cast<unsigned long long>(burst.totalUs),
            static_cast<unsigned long long>(burst.maxUs));
    };
    writeBurst("active",HostFrameTiming::activeEndFrameBurst);
    std::fputc(',',status);
    writeBurst("previous",HostFrameTiming::lastEndFrameBurst);
    std::fputs("},\n",status);
    std::fprintf(status,"\"sceneDelivery\":{\"freshPairs\":%llu,\"sceneSubmits\":%llu,\"repeatedSceneSubmits\":%llu,\"sourcePairIdGaps\":%llu,\"holdEpisodes50Ms\":%llu,\"holdEpisodes100Ms\":%llu,\"holdEpisodes250Ms\":%llu,\"maxRepeatAgeMs\":%u,\"lastNewPairAgeMs\":%u,\"pairIntervalMedianMs\":%.3f,\"pairIntervalP95Ms\":%.3f,\"pairIntervalP99Ms\":%.3f,\"pairIntervalMaxMs\":%.3f},\n",
        static_cast<unsigned long long>(renderer.sceneDelivery.freshPairs),static_cast<unsigned long long>(renderer.sceneDelivery.sceneSubmits),
        static_cast<unsigned long long>(renderer.sceneDelivery.repeatedSceneSubmits),static_cast<unsigned long long>(renderer.sceneDelivery.sourcePairIdGaps),
        static_cast<unsigned long long>(renderer.sceneDelivery.holds50),static_cast<unsigned long long>(renderer.sceneDelivery.holds100),static_cast<unsigned long long>(renderer.sceneDelivery.holds250),
        renderer.sceneDelivery.maxRepeatAgeMs,renderer.sceneDelivery.LastPairAge(now),deliveryIntervals.medianUs/1000.,deliveryIntervals.p95Us/1000.,deliveryIntervals.p99Us/1000.,deliveryIntervals.maxUs/1000.);
    std::fprintf(status,"  \"submitStartIntervalMeanMs\":%.3f,\"submitStartIntervalP95Ms\":%.3f,\"submitCallP95Ms\":%.3f,\"submitCallMaxMs\":%.3f,\n",
        renderer.submitStartIntervals.Sum()/(1000.0*std::max(1u,renderer.submitStartIntervals.Count())),
        submitIntervals.p95Us/1000.0,submitDurations.p95Us/1000.0,submitDurations.maxUs/1000.0);
    std::fprintf(status,"  \"version\":\"" KL1_RELEASE_VERSION "-kane-lynch-vr-host\",\"hostPid\":%lu,\"gamePid\":%lu,\"phase\":\"%s\",\n"
        "  \"hostReady\":%s,\"hmdPresent\":%s,\"hmdMounted\":%s,\"visible\":%s,\n"
        "  \"framesSubmitted\":%llu,\"submitResult\":%d,\"error\":%d,\n"
        "  \"sourceWidth\":%u,\"sourceHeight\":%u,\"leftFrameAgeMs\":%u,\"rightFrameAgeMs\":%u,\"renderMode\":\"%s\",\"inputContext\":%ld,\n"
        "  \"movementYawValid\":%s,\"movementYawDeg\":%.2f,\"virtualTurnYawDeg\":%.2f,\n"
        "  \"transport\":\"%s\",\"gpuReason\":\"%s\",\"gpuCopies\":%llu,\"gpuBytesAvoided\":%llu,\"runtimeRefreshHz\":%.2f,\"hostSubmitHz\":%.2f,"
        "\"observedLeftHz\":%.2f,\"observedRightHz\":%.2f,\n"
        "  \"leftFrameId\":%llu,\"rightFrameId\":%llu,\"leftPoseValid\":%u,\"rightPoseValid\":%u,"
        "\"stereoPairAvailable\":%s,\n"
        "  \"ipcReadCpuMeanMs\":%.3f,\"uploadCpuMeanMs\":%.3f,\"submitCpuMeanMs\":%.3f,\"ipcRejectedReads\":%llu\n}\n",
        GetCurrentProcessId(),source?source->pid:0,phase,ready?"true":"false",
        sessionStatus.HmdPresent?"true":"false",sessionStatus.HmdMounted?"true":"false",sessionStatus.IsVisible?"true":"false",
        renderer.submitted,renderer.lastSubmit,renderer.error,renderer.width,renderer.height,ageLeft,ageRight,renderer.renderMode,
        source&&source->shared?InterlockedCompareExchange(&source->shared->inputContext,0,0):0,
        movementYawValid?"true":"false",movementYawValid?movementYaw*57.2957795f:0,
        source?source->turnYaw*57.2957795f:0,
        renderer.gpuActive?"gpu_shared_nt_fences":"cpu_shared_memory",renderer.gpuReason,
        static_cast<unsigned long long>(renderer.gpuCopies),static_cast<unsigned long long>(renderer.gpuBytesAvoided),
        renderer.hmd.DisplayRefreshRate,renderer.submitRate.Hz(now),
        eyes?eyes[0].sourceRate.Hz(now):0,eyes?eyes[1].sourceRate.Hz(now):0,
        static_cast<unsigned long long>(eyes?eyes[0].metadata.frameId:0),
        static_cast<unsigned long long>(eyes?eyes[1].metadata.frameId:0),
        eyes?eyes[0].metadata.poseValid:0,eyes?eyes[1].metadata.poseValid:0,
        eyes&&eyes[0].sequence&&eyes[1].sequence&&
            eyes[0].metadata.poseValid==VRIPC::kSceneFrame&&eyes[1].metadata.poseValid==VRIPC::kSceneFrame&&
            (eyes[0].metadata.frameId&1)==0&&(eyes[1].metadata.frameId&1)==1&&
            eyes[0].metadata.frameId/2==eyes[1].metadata.frameId/2?"true":"false",
        eyes?(eyes[0].readTotalUs+eyes[1].readTotalUs)/(1000.0*std::max<std::uint64_t>(1,eyes[0].readCalls+eyes[1].readCalls)):0,
        renderer.uploadTotalUs/(1000.0*std::max<std::uint64_t>(1,renderer.uploadCalls)),
        renderer.submitTotalUs/(1000.0*std::max<unsigned long long>(1,renderer.submitted)),
        static_cast<unsigned long long>(eyes?eyes[0].rejectedReads+eyes[1].rejectedReads:0));
    std::fclose(status);
}

int RunCapture(DWORD pid,unsigned seconds) {
    ScopedTimerResolution timerResolution;
    Source source;
    if(!source.Open(pid)){Renderer unused;unused.error=kSourceMissing;CapturedEye none[2]{};WriteStatus(unused,&source,{},none,"source_missing");return 2;}
    Renderer renderer;
    // Recorder demand is separate from game IPC; destroy before Renderer.
    RecordingMirror::Capture recording(pid);
    const auto settingsPath=outputDirectory+L"vr.ini";
    const unsigned panelWidth=GetPrivateProfileIntW(L"VR",L"PanelWidthMm",3600,settingsPath.c_str());
    const unsigned panelDistance=GetPrivateProfileIntW(L"VR",L"PanelDistanceMm",2000,settingsPath.c_str());
    renderer.panelWidth=(panelWidth>=1500&&panelWidth<=6000?panelWidth:3600)/1000.f;
    renderer.panelDistance=(panelDistance>=750&&panelDistance<=4000?panelDistance:2000)/1000.f;
    if(!renderer.Connect()){
        InterlockedExchange(&source.shared->hostError,renderer.error);
        CapturedEye none[2]{};
        WriteStatus(renderer,&source,{},none,"runtime_error");return 3;
    }
    source.PublishConfiguration(renderer);
    GPUFrame::Consumer gpu;
    gpu.Open(pid,renderer.device,renderer.context,GetPrivateProfileIntW(L"VR",L"GPUTransport",1,settingsPath.c_str())!=0,
             GetPrivateProfileIntW(L"VR",L"MetaDirectGPUUpload",1,settingsPath.c_str())!=0,
             GetPrivateProfileIntW(L"VR",L"GPUSharedD3D9",0,settingsPath.c_str())!=0);
    bool completePairs=GetPrivateProfileIntW(L"VR",L"GPUCompletePairs",1,settingsPath.c_str())!=0;
    renderer.releaseUploadedSources=GetPrivateProfileIntW(L"VR",L"ReleaseUploadedGPUSources",1,settingsPath.c_str())!=0;
    Controls controls;
    controls.leftTriggerAim=true;
    controls.yOpensMenu=true;
    controls.immersiveLayout=GetPrivateProfileIntW(L"Controls",L"ImmersiveLayout",1,settingsPath.c_str())!=0;
    controls.supportGrip=GetPrivateProfileIntW(L"Weapons",L"SupportGrip",1,settingsPath.c_str())!=0;
    const unsigned turnMode=GetPrivateProfileIntW(L"Controls",L"TurnMode",2,settingsPath.c_str());
    controls.turnMode=turnMode==0?ControllerInput::TurnMode::Mouse:
        turnMode==1?ControllerInput::TurnMode::Snap:ControllerInput::TurnMode::Smooth;
    const unsigned snapDegrees=GetPrivateProfileIntW(L"Controls",L"SnapTurnDegrees",30,settingsPath.c_str());
    const unsigned smoothSpeed=GetPrivateProfileIntW(L"Controls",L"SmoothTurnDegreesPerSecond",90,settingsPath.c_str());
    controls.snapTurnDegrees=static_cast<float>(snapDegrees>=15&&snapDegrees<=90?snapDegrees:30);
    controls.smoothTurnDegreesPerSecond=static_cast<float>(smoothSpeed>=30&&smoothSpeed<=240?smoothSpeed:90);
    controls.enabled=GetPrivateProfileIntW(L"Controls",L"Enabled",1,settingsPath.c_str())!=0;
    controls.focusOnAction=GetPrivateProfileIntW(L"Controls",L"FocusOnAction",1,settingsPath.c_str())!=0;
    controls.mouseSensitivity=static_cast<float>(GetPrivateProfileIntW(L"Controls",L"MouseSensitivity",14,settingsPath.c_str()));
    const unsigned deadzone=GetPrivateProfileIntW(L"Controls",L"DeadzonePercent",35,settingsPath.c_str());
    const unsigned trigger=GetPrivateProfileIntW(L"Controls",L"TriggerPercent",55,settingsPath.c_str());
    controls.deadzone=(deadzone>=5&&deadzone<=70?deadzone:35)/100.f;
    controls.trigger=(trigger>=20&&trigger<=95?trigger:55)/100.f;
    if(controls.mouseSensitivity<1||controls.mouseSensitivity>100)controls.mouseSensitivity=14;
    controls.rightStickPitch=GetPrivateProfileIntW(L"Controls",L"RightStickPitch",0,settingsPath.c_str())!=0;
    controls.menuWasd=GetPrivateProfileIntW(L"Controls",L"MenuWASD",0,settingsPath.c_str())!=0;
    // Direct selection lets either stick navigate lists and change sliders.
    // Cursor steering remains an optional compatibility setting.
    controls.menuMouse=GetPrivateProfileIntW(L"Controls",L"MenuMouse",0,settingsPath.c_str())!=0;
    controls.menuBothSticks=GetPrivateProfileIntW(L"Controls",L"MenuBothSticks",1,settingsPath.c_str())!=0;
    controls.menuXSpace=GetPrivateProfileIntW(L"Controls",L"MenuXSpace",1,settingsPath.c_str())!=0;
    controls.movementHeadRelative=GetPrivateProfileIntW(L"Controls",L"MovementHeadRelative",1,settingsPath.c_str())!=0;
    const unsigned menuSensitivity=GetPrivateProfileIntW(L"Controls",L"MenuMouseSensitivity",10,settingsPath.c_str());
    const unsigned menuThreshold=GetPrivateProfileIntW(L"Controls",L"MenuThresholdPercent",55,settingsPath.c_str());
    controls.menuMouseSensitivity=(menuSensitivity>=1&&menuSensitivity<=100?menuSensitivity:10);
    controls.menuThreshold=(menuThreshold>=35&&menuThreshold<=90?menuThreshold:55)/100.f;
    if(controls.enabled) {
        Log("Touch game: left stick head-relative movement; right stick selects direct VR smooth/snap turning (TurnMode 2/1), native mouse compatibility is TurnMode 0. Right trigger fires. Immersive layout disables native left-trigger aim.");
        Log(controls.immersiveLayout?"VR gestures: short A release reloads R / hold350ms while moving sprints Space; X release acts E. B tap crouches / hold350ms climbs. Y tap switches weapon F / hold350ms opens inventory, map and team; right stick selects, release Y equips. While open, right grip cycles recipients, A recalls, B moves to aiming point, RT attacks. Left grip otherwise controls foregrip. Right grip hold350ms prepares grenade, release throws; X cancels / Y changes type. Left stick click toggles walk/run; right stick click recentres immediately. Menu pauses.":
            "Legacy layout: A/right grip action E, B reload R, left grip walk Shift, X map M, Y pause Esc, both stick clicks recenter.");
        Log("Touch menus: either stick selects/changes options, right trigger or A confirms, B/Y goes back with the KL2 layout, X Space; MenuMouse=1 enables the optional cursor. Release controls after focus changes.");
    }
    TouchInput touch;
    CapturedEye eyes[2]{};
    ovrSessionStatus status{};
    DWORD start=GetTickCount(),lastStatus=start,lastWaitingMessage=start,committedScopeTick=0;
    MonoFrame::ScopeExitGrace scopeExit;
    const DWORD submitInterval=std::max<DWORD>(1,static_cast<DWORD>(1000.f/
        std::max(45.f,renderer.hmd.DisplayRefreshRate)));
    int exitCode=0;
    while(source.Running()&&(!seconds||GetTickCount()-start<seconds*1000)){
        [[maybe_unused]] bool submittedThisIteration=false;
        if(!renderer.Status(status)){exitCode=4;break;}
        source.PublishConfiguration(renderer);
        InterlockedExchange(&source.shared->hostHeartbeatTick,static_cast<LONG>(GetTickCount()));
        InterlockedExchange(&source.shared->hostVisible,status.IsVisible?1:0);
        InterlockedExchange(&source.shared->hostHmdPresent,status.HmdPresent?1:0);
        if(status.ShouldQuit||status.DisplayLost){
            renderer.Error(ovrError_DisplayLost,"Meta requested compositor shutdown or the Link display was lost. Toggle F9 after reconnecting.");exitCode=4;break;
        }
        source.PublishTracking(renderer,status);
#ifdef KL1_OPENXR_HOST
        const bool renderable=KL1OpenXR::state.Renderable();
        if(KL1OpenXR::state.frameBegun&&!renderable)++renderer.xrNoRenderFrames;
#else
        constexpr bool renderable=true;
#endif
        bool changed[2]{};
        {
            HostFrameTiming::Scope timing(HostFrameTiming::TransportRead);
            gpu.Tick();
            if(renderable)ReadEyes(*source.shared,eyes,start,changed,&gpu,completePairs);
        }
        renderer.gpuCompletePairs=completePairs;renderer.gpuScenePairsRead=gpu.scenePairsRead;
        renderer.gpuPanelFramesRead=gpu.panelFramesRead;renderer.gpuPanelOlderFramesRead=gpu.olderPanelFramesRead;
        const DWORD now=GetTickCount();
        const bool fresh[2]={eyes[0].sequence&&StereoPairPolicy::Fresh(now,eyes[0].metadata.tick),
                             eyes[1].sequence&&StereoPairPolicy::Fresh(now,eyes[1].metadata.tick)};
        const LONG inputContext=InterlockedCompareExchange(&source.shared->inputContext,0,0);
        LONG gameplayFlags=0;const bool gameplayFresh=MonoFrame::ReadGameplay(*source.shared,now,gameplayFlags);
        const auto nativeMenuTick=static_cast<DWORD>(InterlockedCompareExchange(&source.shared->nativeMenuReadyTick,0,0));
        const bool nativeMenu=nativeMenuTick&&now-nativeMenuTick<=250;
        const bool scopeProof=MonoFrame::ScopeProof(inputContext,gameplayFresh,gameplayFlags,nativeMenu);
        const unsigned latestEye=fresh[0]&&(!fresh[1]||
            static_cast<LONG>(eyes[0].metadata.tick-eyes[1].metadata.tick)>=0)?0:1;
        renderer.gpuActive=eyes[latestEye].transport==VRIPC::kGPU;renderer.gpuReason=gpu.reason;
        renderer.gpuCopies=gpu.copies;renderer.gpuBytesAvoided=gpu.bytesAvoided;
        const auto latestKind=fresh[latestEye]?eyes[latestEye].metadata.poseValid:0;
        ControllerInput::Context controllerContext=ControllerInput::Context::Suspended;
        if(inputContext==VRIPC::kInputMenu)controllerContext=ControllerInput::Context::Menu;
        else if(inputContext==VRIPC::kInputGame)controllerContext=ControllerInput::Context::Game;
        else if(inputContext==VRIPC::kInputInventory)controllerContext=ControllerInput::Context::Inventory;
        else if(inputContext==VRIPC::kInputMap)controllerContext=ControllerInput::Context::Map;
        else if(inputContext==VRIPC::kInputCinematic)controllerContext=ControllerInput::Context::Cinematic;
        else if(inputContext==VRIPC::kInputLoading)controllerContext=ControllerInput::Context::Loading;
        else if(inputContext==VRIPC::kInputUnknown) {
            if(latestKind==VRIPC::kSceneFrame)controllerContext=ControllerInput::Context::Game;
            else if(latestKind==VRIPC::kUiFrame)controllerContext=ControllerInput::Context::Menu;
        }
        touch.Poll(renderer,controls,now,source,controllerContext,status);
        const bool hudHeld=controllerContext==ControllerInput::Context::Game&&controls.enabled&&
            status.IsVisible&&status.HmdPresent&&status.HmdMounted&&
            InterlockedCompareExchange(&source.shared->captureEnabled,0,0)!=0&&touch.GameplayHudHeld();
        // Renew every tick, including an explicit false while controls/session
        // are suspended. A lost host cannot leave the display held indefinitely.
        const bool teamHeld=controllerContext==ControllerInput::Context::Game&&controls.enabled&&
            status.IsVisible&&status.HmdPresent&&status.HmdMounted&&
            InterlockedCompareExchange(&source.shared->captureEnabled,0,0)!=0&&touch.TeamHudHeld();
        const bool weaponMenuHeld=controllerContext==ControllerInput::Context::Game&&controls.enabled&&
            status.IsVisible&&status.HmdPresent&&status.HmdMounted&&
            InterlockedCompareExchange(&source.shared->captureEnabled,0,0)!=0&&touch.WeaponMenuHeld();
        const bool climbHeld=controllerContext==ControllerInput::Context::Game&&controls.enabled&&
            status.IsVisible&&status.HmdPresent&&status.HmdMounted&&
            InterlockedCompareExchange(&source.shared->captureEnabled,0,0)!=0&&touch.ClimbHeld();
        const bool grenadeVisual=controllerContext==ControllerInput::Context::Game&&controls.enabled&&status.IsVisible&&status.HmdPresent&&status.HmdMounted&&touch.GrenadeVisualHeld(now);
        const bool mountedStick=controllerContext==ControllerInput::Context::Game&&controls.enabled&&status.IsVisible&&status.HmdPresent&&status.HmdMounted&&touch.MountedStickActive();
        const DWORD reloadTick=controllerContext==ControllerInput::Context::Game&&controls.enabled&&status.IsVisible&&status.HmdPresent&&status.HmdMounted?touch.ManualReloadTick(now):0;
        VRIPC::PublishHudIntent(*source.shared,hudHeld,now,false,2,teamHeld,weaponMenuHeld,false,grenadeVisual,mountedStick,reloadTick,climbHeld,
                               teamHeld&&weaponMenuHeld?touch.TeamNextTick():0);
        // Alternating eyes normally differ by one game frame; a moving scene
        // must not pair eyes captured from different tracked-pose pairs. A new
        // panel frame also takes priority over the previous mission's eyes.
        const bool stereo=fresh[0]&&fresh[1]&&
            inputContext!=VRIPC::kInputSuspended&&inputContext!=VRIPC::kInputLoading&&
            latestKind==VRIPC::kSceneFrame&&
            eyes[0].metadata.poseValid==VRIPC::kSceneFrame&&eyes[1].metadata.poseValid==VRIPC::kSceneFrame&&
            eyes[0].transportEpoch==eyes[1].transportEpoch&&
            StereoPairPolicy::Compatible(eyes[0].metadata,eyes[1].metadata);
        const unsigned monoEye=latestEye;
        const bool scope=fresh[monoEye]&&latestKind==VRIPC::kScopeFrame&&scopeProof&&now-eyes[monoEye].metadata.tick<=250;
        const bool loading=fresh[monoEye]&&MonoFrame::LoadingProof(inputContext,latestKind);
        const bool mono=fresh[monoEye]&&((latestKind==VRIPC::kUiFrame&&inputContext!=VRIPC::kInputLoading)||scope||loading);
        const bool committedScene=std::strcmp(renderer.renderMode,"scene")==0||std::strcmp(renderer.renderMode,"scene_hold")==0;
        renderer.sceneDelivery.SetActive(renderable&&(latestKind==VRIPC::kSceneFrame||committedScene)&&
            latestKind!=VRIPC::kUiFrame&&latestKind!=VRIPC::kScopeFrame&&latestKind!=VRIPC::kContextFrame&&
            inputContext!=VRIPC::kInputMenu&&inputContext!=VRIPC::kInputMap&&
            inputContext!=VRIPC::kInputInventory&&inputContext!=VRIPC::kInputSuspended&&inputContext!=VRIPC::kInputLoading);
        const auto& selected=eyes[stereo?0:monoEye].metadata;
        const bool dimensionsChanged=renderer.width!=selected.width||renderer.height!=selected.height;
        const auto uploadable=[&](unsigned side,unsigned sourceSide){
            return (!dimensionsChanged&&eyes[side].AlreadyUploaded(eyes[sourceSide]))||eyes[sourceSide].HasPixels();
        };
        const bool uploadableFrame=stereo?uploadable(0,0)&&uploadable(1,1):mono&&uploadable(0,monoEye);
        if(renderable&&(stereo||mono)&&!uploadableFrame)++renderer.unavailableUploads;
        // xrWaitFrame explicitly asked for no image. Keep tracking and input
        // live, leave the committed layer untouched and finish the empty frame.
        if(renderable&&uploadableFrame) {
            const bool modeChanged=stereo?
                (std::strcmp(renderer.renderMode,"scene")!=0&&std::strcmp(renderer.renderMode,"scene_hold")!=0):
                std::strcmp(renderer.renderMode,loading?"loading":scope?"scope":"panel")!=0&&
                    ((!scope||std::strcmp(renderer.renderMode,"scope_hold")!=0)&&
                     (!loading||std::strcmp(renderer.renderMode,"loading_hold")!=0));
            if(!renderer.EnsureChains(selected.width,selected.height)){exitCode=5;break;}
            if(dimensionsChanged){eyes[0].uploaded=eyes[1].uploaded=false;changed[0]=changed[1]=true;}
            const auto captureRequest=PendingEyeCapture(eyes,stereo,monoEye);
            const bool finishSourceInCopyFlush=renderer.releaseUploadedSources&&captureRequest.empty();
            bool uploadFailed=false,pairUploaded=false;
            for(unsigned eye=0;eye<(stereo?2u:1u);++eye) {
                const unsigned sourceEye=stereo?eye:monoEye;
                const auto& f=eyes[sourceEye].metadata;
                // Layer/mode changes do not change a committed swapchain image.
                // Its GPU lease may already have been returned to the game.
                if(eyes[eye].AlreadyUploaded(eyes[sourceEye]))continue;
                const auto& image=eyes[sourceEye];
                if(!renderer.Upload(eye,image.pixels.empty()?nullptr:image.pixels.data(),f.stride,
                    image.gpuImage.get(),image.gpuLease,stereo,finishSourceInCopyFlush)){
                    uploadFailed=true;break;
                }
                pairUploaded=true;
                if(stereo) {
                    renderer.layer.RenderPose[eye]=EyePose(f,renderer.eyeDescription[eye].HmdToEyeOffset);
                    renderer.layer.Fov[eye]=ovrFovPort{f.fov[2],f.fov[3],f.fov[0],f.fov[1]};
                }
                eyes[eye].uploaded=true;eyes[eye].uploadedSequence=eyes[sourceEye].sequence;
                eyes[eye].uploadedFrameId=f.frameId;eyes[eye].uploadedKind=f.poseValid;
                eyes[eye].uploadedEpoch=eyes[sourceEye].transportEpoch;
            }
            if(uploadFailed){exitCode=5;break;}
            if(stereo&&pairUploaded&&renderer.sceneDelivery.NewPair(eyes[0].metadata.frameId,eyes[0].transportEpoch,GetTickCount())){
                ++renderer.newScenePairs;renderer.pairRate.Observe(GetTickCount());
            }
            renderer.SetRenderMode(stereo?"scene":loading?"loading":scope?"scope":"panel");
            committedScopeTick=scope?selected.tick:0;
            scopeExit.Reset();
            if(!captureRequest.empty())CaptureRequestedEyes(renderer,eyes,stereo,monoEye,captureRequest);
            if(renderer.releaseUploadedSources)renderer.gpuSourceLeasesReleased+=ReleaseUploadedSources(eyes);
            if(FrameSubmissionDue(changed[0]||changed[1]||modeChanged,GetTickCount(),renderer.submitCadence,submitInterval)) {
                const double sampleTime=stereo?std::min(eyes[0].metadata.sensorTime,eyes[1].metadata.sensorTime):0;
                const bool ok=stereo?renderer.SubmitStereo(sampleTime):loading?renderer.SubmitLoading():scope?renderer.SubmitScope():renderer.SubmitQuad(sampleTime);
                if(!ok){exitCode=6;break;}
                submittedThisIteration=true;
                InterlockedExchange(&source.shared->hostSubmittedFrames,static_cast<LONG>(renderer.submitted));
            }
        } else if(renderable) {
            // Hold the last committed layer while the game delivers its replacement.
            const bool captureEnabled=InterlockedCompareExchange(&source.shared->captureEnabled,0,0)!=0;
            const bool hadScope=std::strcmp(renderer.renderMode,"scope")==0||std::strcmp(renderer.renderMode,"scope_hold")==0;
            const bool scopeCommitted=hadScope&&renderer.chains[0]&&captureEnabled;
            const bool scopeExitHold=scopeExit.Hold(scopeCommitted&&
                MonoFrame::ScopeExitProof(inputContext,gameplayFresh,gameplayFlags,nativeMenu,latestKind),committedScopeTick,now);
            const bool hadLoading=std::strcmp(renderer.renderMode,"loading")==0||std::strcmp(renderer.renderMode,"loading_hold")==0;
            if(MonoFrame::HoldLoading(hadLoading&&renderer.chains[0],inputContext,captureEnabled)) {
                renderer.SetRenderMode("loading_hold");
                if(FrameSubmissionDue(false,now,renderer.submitCadence,submitInterval)) {
                    if(!renderer.SubmitLoading()){exitCode=6;break;}
                    submittedThisIteration=true;
                    InterlockedExchange(&source.shared->hostSubmittedFrames,static_cast<LONG>(renderer.submitted));
                }
            }else if(hadLoading){
                if(!renderer.SubmitClear()){exitCode=6;break;}
                submittedThisIteration=true;renderer.SetRenderMode("waiting");
                InterlockedExchange(&source.shared->hostSubmittedFrames,static_cast<LONG>(renderer.submitted));
            }else if(scopeExitHold||(scopeCommitted&&MonoFrame::HoldScope(true,scopeProof&&latestKind==VRIPC::kScopeFrame,
                committedScopeTick,now))){
                renderer.SetRenderMode("scope_hold");
                if(FrameSubmissionDue(false,now,renderer.submitCadence,submitInterval)){
                    if(!renderer.SubmitScope()){exitCode=6;break;}
                    submittedThisIteration=true;
                    InterlockedExchange(&source.shared->hostSubmittedFrames,static_cast<LONG>(renderer.submitted));
                }
            }else if(hadScope){
                if(!renderer.SubmitClear()){exitCode=6;break;}
                submittedThisIteration=true;renderer.SetRenderMode("waiting");
                InterlockedExchange(&source.shared->hostSubmittedFrames,static_cast<LONG>(renderer.submitted));
            }else if(MonoFrame::HoldScene(renderer.CanHoldStereo(),inputContext,captureEnabled)) {
                renderer.SetRenderMode("scene_hold");
                if(FrameSubmissionDue(false,now,renderer.submitCadence,submitInterval)) {
                    if(!renderer.SubmitStereo(0)){exitCode=6;break;}
                    submittedThisIteration=true;
                    InterlockedExchange(&source.shared->hostSubmittedFrames,static_cast<LONG>(renderer.submitted));
                }
            } else if(renderer.chains[0]&&
                      (std::strcmp(renderer.renderMode,"panel")==0||std::strcmp(renderer.renderMode,"stale")==0)) {
                renderer.SetRenderMode("stale");
                if(FrameSubmissionDue(false,now,renderer.submitCadence,submitInterval)) {
                    if(!renderer.SubmitQuad(0)){exitCode=6;break;}
                    submittedThisIteration=true;
                    InterlockedExchange(&source.shared->hostSubmittedFrames,static_cast<LONG>(renderer.submitted));
                }
            } else renderer.SetRenderMode("waiting");
            if(GetTickCount()-lastWaitingMessage>5000){Log("Waiting for game frames. The game window must stay focused to render.");lastWaitingMessage=GetTickCount();}
        }
        const bool recordingScene=std::strcmp(renderer.renderMode,"scene")==0||std::strcmp(renderer.renderMode,"scene_hold")==0;
        const bool recordingScope=std::strcmp(renderer.renderMode,"scope")==0||std::strcmp(renderer.renderMode,"scope_hold")==0;
        const bool recordingLoading=std::strcmp(renderer.renderMode,"loading")==0||std::strcmp(renderer.renderMode,"loading_hold")==0;
        const bool recordingPanel=std::strcmp(renderer.renderMode,"panel")==0||std::strcmp(renderer.renderMode,"stale")==0;
        recording.Tick(renderer.module,renderer.session,renderer.device,renderer.context,GetTickCount(),
            recordingScene?VRIPC::kSceneFrame:recordingScope?VRIPC::kScopeFrame:recordingLoading?VRIPC::kContextFrame:
            recordingPanel?VRIPC::kUiFrame:0u,renderer.submitted,submittedThisIteration);
        if(GetTickCount()-lastStatus>1000){
            completePairs=GetPrivateProfileIntW(L"VR",L"GPUCompletePairs",1,settingsPath.c_str())!=0;
            renderer.releaseUploadedSources=GetPrivateProfileIntW(L"VR",L"ReleaseUploadedGPUSources",1,settingsPath.c_str())!=0;
            WriteStatus(renderer,&source,status,eyes,renderer.submitted?"submitting":"waiting_for_frames");lastStatus=GetTickCount();}
#ifdef KL1_OPENXR_HOST
        // Even the initial no-image interval must finish the acquired XR
        // frame, or the runtime can never deliver a calibrated first view.
        if(!KL1OpenXR::FinishIdle()){renderer.ApiError(KL1OpenXR::state.error,"OpenXR idle frame");exitCode=6;break;}
        if(!KL1OpenXR::state.running)Sleep(5);
#else
        if(!changed[0]&&!changed[1]&&!submittedThisIteration)Sleep(1);
#endif
    }
    VRIPC::PublishHudIntent(*source.shared,false,GetTickCount());
    if(renderer.error)InterlockedExchange(&source.shared->hostError,renderer.error);
    InterlockedExchange(&source.shared->hostReady,0);
    WriteStatus(renderer,&source,status,eyes,exitCode?"stopped_error":"stopped");
    Log("Meta compositor host stopped.");return exitCode;
}

int RunDiagnostic(bool pattern,unsigned seconds) {
    Renderer renderer;
    CapturedEye none[2]{};
    if(!renderer.Connect()){WriteStatus(renderer,nullptr,{},none,"runtime_error");return 3;}
    ovrSessionStatus status{};
    if(!renderer.Status(status)){WriteStatus(renderer,nullptr,status,none,"runtime_error");return 4;}
    if(!pattern){
        char message[180];std::snprintf(message,sizeof(message),"Native rendering check: HMD present=%d, mounted=%d; Meta D3D11 device ready.",status.HmdPresent,status.HmdMounted);Log(message);
        WriteStatus(renderer,nullptr,status,none,"check_passed");return 0;
    }
    renderer.SetRenderMode("panel");
    constexpr unsigned width=1024,height=1024;
    if(!renderer.EnsureChains(width,height)){WriteStatus(renderer,nullptr,status,none,"swapchain_error");return 5;}
    std::vector<unsigned char> pixels(width*height*4);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
        unsigned char* color=pixels.data()+(y*width+x)*4;
        const bool line=(x%128)<4||(y%128)<4;
        color[0]=line?90:12;color[1]=line?90:12;color[2]=line?90:12;color[3]=255;
    }
    Log("Submitting a native mono grid diagnostic to the headset.");
    const DWORD start=GetTickCount();
    int exitCode=0;
    while(GetTickCount()-start<seconds*1000){
        if(!renderer.Status(status)||status.ShouldQuit||status.DisplayLost){exitCode=4;break;}
#ifdef KL1_OPENXR_HOST
        if(!KL1OpenXR::state.Renderable()||!KL1OpenXR::Configured()){
            if(!KL1OpenXR::FinishIdle()){renderer.ApiError(KL1OpenXR::state.error,"OpenXR diagnostic startup");exitCode=4;break;}
            if(!KL1OpenXR::state.running)Sleep(5);
            continue;
        }
#endif
        if(!renderer.Upload(0,pixels.data(),width*4)){exitCode=5;break;}
        if(!renderer.SubmitQuad(0)){exitCode=6;break;}
        Sleep(10);
    }
#ifdef KL1_OPENXR_HOST
    if(!exitCode&&!renderer.submitted){renderer.Error(-92011,"The OpenXR diagnostic did not become visible before its time limit. Enter the headset PC VR environment and retry.");exitCode=4;}
#endif
    WriteStatus(renderer,nullptr,status,none,exitCode?"diagnostic_error":"diagnostic_finished");
    char message[160];std::snprintf(message,sizeof(message),"Native diagnostic finished: %llu frames submitted; result %d.",renderer.submitted,renderer.lastSubmit);Log(message);return exitCode;
}
} // namespace

int wmain(int argc,wchar_t** argv) {
    DWORD pid=0;bool check=false,pattern=false;unsigned seconds=0;
#ifdef KL1_OPENXR_HOST
    std::wstring probeStatus;
#endif
    for(int i=1;i<argc;++i){
        if(std::wcscmp(argv[i],L"--pid")==0&&i+1<argc)pid=std::wcstoul(argv[++i],nullptr,10);
        else if(std::wcscmp(argv[i],L"--seconds")==0&&i+1<argc)seconds=std::min(30u,static_cast<unsigned>(std::wcstoul(argv[++i],nullptr,10)));
        else if(std::wcscmp(argv[i],L"--check")==0)check=true;
#ifdef KL1_OPENXR_HOST
        else if(std::wcscmp(argv[i],L"--probe-status")==0&&i+1<argc)probeStatus=argv[++i];
#endif
        else if(std::wcscmp(argv[i],L"--test-pattern")==0)pattern=true;
        else {std::fprintf(stderr,"Usage: KaneLynchVRHost --pid GAME_PID [--seconds N] | --check | --test-pattern [--seconds N]\n");return 1;}
    }
    if(!pid&&!check&&!pattern){std::fprintf(stderr,"Enable VR output with F9 in the game, or use --check / --test-pattern.\n");return 1;}
    wchar_t path[MAX_PATH];GetModuleFileNameW(nullptr,path,MAX_PATH);
    outputDirectory=path;const auto slash=outputDirectory.find_last_of(L"\\/");
    outputDirectory=slash==std::wstring::npos?L"":outputDirectory.substr(0,slash+1);
    const auto ini=outputDirectory+L"vr.ini";
    statusLogging=DiagnosticsSession::Enabled(outputDirectory);
    HostFrameTiming::enabled=statusLogging&&!releaseMode;
    releaseMode=!statusLogging;
    if(statusLogging&&GetPrivateProfileIntW(L"Diagnostics",L"CrashLogging",1,ini.c_str()))CrashDiagnostics::Install(outputDirectory,KL1_RELEASE_VERSION "-host");
    struct FinishDiagnostics {~FinishDiagnostics(){CrashDiagnostics::Close("host_exit");}} finishDiagnostics;
    CrashDiagnostics::Breadcrumb("host_start");
    if(statusLogging)logFile=_wfopen((outputDirectory+L"KaneLynchVRHost.log").c_str(),L"ab");
#ifdef KL1_OPENXR_HOST
    Log("Kane & Lynch OpenXR compositor host " KL1_RELEASE_VERSION " (x64).");KL1OpenXR::SetLog(&Log);
    const int result=check?KL1OpenXR::Probe(probeStatus):pattern?RunDiagnostic(true,seconds?seconds:5):RunCapture(pid,seconds);
#else
    Log("Kane & Lynch Meta compositor host " KL1_RELEASE_VERSION " (x64), native player rig tracking.");
    const int result=(check||pattern)?RunDiagnostic(pattern,seconds?seconds:5):RunCapture(pid,seconds);
#endif
    if(logFile){std::fclose(logFile);logFile=nullptr;}return result;
}
