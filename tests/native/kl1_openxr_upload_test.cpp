// The production KL1 OpenXR bridge, with private fake swapchains and real WARP.
// No XR loader, headset, game process or user input is opened.
#define WIN32_LEAN_AND_MEAN
#define OVR_CAPI_NO_UTILS
#include "kl1_openxr_adapter.hpp"
#include "gpu_frame_consumer.hpp"
#include <map>
#include <cstdlib>
#include <cstdint>
#include <limits>

namespace {
using namespace KL1OpenXR;
unsigned checks=0,created=0,destroyed=0,acquires=0,waits=0,releases=0;
std::uintptr_t next=100;
unsigned imageCount=3;
DWORD waitDelay=0;
DXGI_FORMAT imageFormat=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
UINT imageBind=D3D11_BIND_RENDER_TARGET;
struct Images {std::vector<ID3D11Texture2D*> textures;unsigned index=0;bool acquired=false,waited=false;};
std::map<XrSwapchain,Images> images;
std::map<XrSwapchain,Chain*> watched;
void Check(bool value,const char* message){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
bool actionActive=true,actionError=false;
float actionValue=0;
XrResult XRAPI_PTR ActionFloat(XrSession,const XrActionStateGetInfo*,XrActionStateFloat* output){
    output->isActive=actionActive;output->currentState=actionValue;
    return actionError?XR_ERROR_SESSION_LOST:XR_SUCCESS;
}
XrResult XRAPI_PTR ActionBoolean(XrSession,const XrActionStateGetInfo*,XrActionStateBoolean* output){
    output->isActive=actionActive;output->currentState=actionValue>0;
    return actionError?XR_ERROR_SESSION_LOST:XR_SUCCESS;
}
bool requestsImage=false;
unsigned finalLayers=0;
XrResult XRAPI_PTR NoEvents(XrInstance,XrEventDataBuffer*){return XR_EVENT_UNAVAILABLE;}
XrResult XRAPI_PTR WaitPrivateFrame(XrSession,const XrFrameWaitInfo*,XrFrameState* frame){
    frame->shouldRender=requestsImage;
    frame->predictedDisplayPeriod=8333333;
    return XR_SUCCESS;
}
XrResult XRAPI_PTR BeginPrivateFrame(XrSession,const XrFrameBeginInfo*){return XR_SUCCESS;}
XrResult XRAPI_PTR EndPrivateFrame(XrSession,const XrFrameEndInfo* frame){
    finalLayers=frame->layerCount;
    Check((frame->layers!=nullptr)==(frame->layerCount>0),"layer-free OpenXR frame carries a null list");
    return XR_SUCCESS;
}
XrResult XRAPI_PTR TrackPrivateFrame(XrSpace,XrSpace,XrTime,XrSpaceLocation* head){
    head->locationFlags=XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    head->pose=Identity();
    return XR_SUCCESS;
}
void RuntimeFrameAvailability(){
    State xr;
    xr.running=true;xr.api.PollEvent=NoEvents;xr.api.WaitFrame=WaitPrivateFrame;
    xr.api.BeginFrame=BeginPrivateFrame;xr.api.EndFrame=EndPrivateFrame;xr.api.LocateSpace=TrackPrivateFrame;
    Check(!xr.Renderable(),"runtime has no work before it begins a frame");
    const XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION,nullptr,0,XR_NULL_HANDLE,0,nullptr};
    const XrCompositionLayerBaseHeader* layers[]={reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer)};
    Check(xr.Begin()&&xr.frameBegun&&!xr.Renderable()&&xr.tracking.StatusFlags==ovrStatus_OrientationTracked,
        "a skipped runtime frame still updates tracking without requesting new images");
    Check(xr.End(layers,1)&&!xr.lastLayers&&finalLayers==0&&!xr.Renderable(),"non-renderable frames end empty");
    requestsImage=true;
    Check(xr.Begin()&&xr.Renderable(),"a fresh renderable runtime frame resumes texture work");
    Check(xr.End(layers,1)&&xr.lastLayers&&finalLayers==1&&!xr.Renderable(),"renderable frames submit the committed layer and retire the request");
}
void EndFrameBursts(){
    using namespace HostFrameTiming;
    ObserveEndFrame(93000,193,true);
    ObserveEndFrame(95000,296,true);
    ObserveEndFrame(53000,355,false);
    Check(activeEndFrameBurst.firstTick==100&&activeEndFrameBurst.lastTick==355&&activeEndFrameBurst.calls==3&&
          activeEndFrameBurst.layeredCalls==2&&activeEndFrameBurst.totalUs==241000&&activeEndFrameBurst.maxUs==95000,
          "slow EndFrame burst includes exact call interval, cumulative time and submitted layers");
    ObserveEndFrame(8000,364,true);
    Check(!activeEndFrameBurst.calls&&lastEndFrameBurst.calls==3&&lastEndFrameBurst.lastTick==355,
          "normal submit retires the active burst while keeping it visible in status");
    ObserveEndFrame(50000,20,false);
    Check(activeEndFrameBurst.firstTick==DWORD(20-50)&&activeEndFrameBurst.lastTick==20&&lastEndFrameBurst.calls==3,
          "new burst preserves the previous episode and supports Windows tick wraparound");
    activeEndFrameBurst={};lastEndFrameBurst={};
}
void ActionAvailability(){
    state.api.GetActionStateFloat=ActionFloat;state.api.GetActionStateBoolean=ActionBoolean;
    bool valid=false;
    Check(state.Float(XR_NULL_HANDLE,0,&valid)==0&&valid,"active analog zero is a real released input");
    Check(!state.Boolean(XR_NULL_HANDLE,0,&valid)&&valid,"active unpressed grip click is still available");
    actionValue=.9f;
    Check(state.Float(XR_NULL_HANDLE,0,&valid)==.9f&&valid&&state.Boolean(XR_NULL_HANDLE,0),"active held inputs preserve their physical values");
    actionActive=false;
    Check(state.Float(XR_NULL_HANDLE,0,&valid)==0&&!valid,"inactive analog action is unavailable rather than a physical release");
    Check(!state.Boolean(XR_NULL_HANDLE,0,&valid)&&!valid,"inactive click does not override an unavailable analog grip");
    actionActive=true;actionError=true;
    Check(state.Float(XR_NULL_HANDLE,0,&valid)==0&&!valid&&!state.Boolean(XR_NULL_HANDLE,0),"runtime action failure cannot publish held or released input");
    actionError=false;actionValue=std::numeric_limits<float>::quiet_NaN();
    Check(state.Float(XR_NULL_HANDLE,0,&valid)==0&&!valid,"non-finite grip cannot become release input");
    actionValue=2;
    Check(state.Float(XR_NULL_HANDLE,0,&valid)==0&&!valid,"out-of-range grip is unavailable");
    actionValue=0;
}
XrResult XRAPI_PTR Create(XrSession,const XrSwapchainCreateInfo* request,XrSwapchain* output){
    Check(request->sampleCount==1&&request->arraySize==1&&request->mipCount==1,"single-sample full-image XR contract");
    *output=reinterpret_cast<XrSwapchain>(++next);auto& entry=images[*output];
    D3D11_TEXTURE2D_DESC desc{};desc.Width=request->width;desc.Height=request->height;
    desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;desc.Format=imageFormat;desc.BindFlags=imageBind;
    for(unsigned i=0;i<imageCount;++i){ID3D11Texture2D* texture=nullptr;
        Check(SUCCEEDED(state.device->CreateTexture2D(&desc,nullptr,&texture)),"private runtime creates actual GPU texture");
        entry.textures.push_back(texture);}
    ++created;return XR_SUCCESS;
}
XrResult XRAPI_PTR Enumerate(XrSwapchain chain,unsigned capacity,unsigned* count,XrSwapchainImageBaseHeader* output){
    const auto& entry=images.at(chain);*count=static_cast<unsigned>(entry.textures.size());
    if(capacity){Check(capacity>=*count,"enough image enumeration capacity");
        auto* typed=reinterpret_cast<XrSwapchainImageD3D11KHR*>(output);
        for(unsigned i=0;i<*count;++i)typed[i].texture=entry.textures[i];}
    return XR_SUCCESS;
}
XrResult XRAPI_PTR DestroySwapchain(XrSwapchain chain){
    const auto found=watched.find(chain);
    if(found!=watched.end())for(auto* target:found->second->targets)Check(!target,"all cached RTVs released before runtime images are destroyed");
    for(auto*& texture:images.at(chain).textures)Release(texture);
    watched.erase(chain);images.erase(chain);++destroyed;return XR_SUCCESS;
}
XrResult XRAPI_PTR Acquire(XrSwapchain chain,const XrSwapchainImageAcquireInfo*,unsigned* output){
    auto& entry=images.at(chain);Check(!entry.acquired,"acquire cannot overlap prior runtime image ownership");
    *output=entry.index;entry.acquired=true;++acquires;return XR_SUCCESS;
}
XrResult XRAPI_PTR Wait(XrSwapchain chain,const XrSwapchainImageWaitInfo* wait){
    auto& entry=images.at(chain);Check(entry.acquired&&!entry.waited&&wait->timeout==XR_INFINITE_DURATION,"wait follows acquisition");
    if(waitDelay){Sleep(waitDelay);waitDelay=0;}
    entry.waited=true;++waits;return XR_SUCCESS;
}
XrResult XRAPI_PTR ReleaseImage(XrSwapchain chain,const XrSwapchainImageReleaseInfo*){
    auto& entry=images.at(chain);Check(entry.acquired&&entry.waited,"release follows wait and image write");
    entry.acquired=entry.waited=false;entry.index=(entry.index+1)%imageCount;++releases;return XR_SUCCESS;
}
std::vector<std::uint8_t> Read(ID3D11Texture2D* texture){
    D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;
    desc.BindFlags=desc.MiscFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D* staging=nullptr;Check(SUCCEEDED(state.device->CreateTexture2D(&desc,nullptr,&staging)),"allocate private readback");
    state.context->CopyResource(staging,texture);D3D11_MAPPED_SUBRESOURCE map{};
    Check(SUCCEEDED(state.context->Map(staging,0,D3D11_MAP_READ,0,&map)),"read actual GPU output");
    std::vector<std::uint8_t> bytes(std::size_t(desc.Width)*desc.Height*4);
    for(unsigned row=0;row<desc.Height;++row)std::memcpy(bytes.data()+std::size_t(row)*desc.Width*4,
        static_cast<const std::uint8_t*>(map.pData)+std::size_t(row)*map.RowPitch,desc.Width*4);
    state.context->Unmap(staging,0);Release(staging);return bytes;
}
void CheckColour(const std::vector<std::uint8_t>& bytes,bool rgba){
    Check(!bytes.empty(),"image is not empty");
    for(std::size_t i=0;i<bytes.size();i+=4){
        const unsigned blue=bytes[i+(rgba?2:0)],red=bytes[i+(rgba?0:2)];
        Check(blue>=62&&blue<=66&&bytes[i+1]>=126&&bytes[i+1]<=130&&red>=190&&red<=194,
            "GPU blit preserves game display bytes without extra sRGB encoding");}
}
void UploadPath(){
    ovrTextureSwapChainDesc desc{};desc.Width=32;desc.Height=24;ovrTextureSwapChain handle=nullptr;
    Check(OVR_SUCCESS(CreateChain(nullptr,nullptr,&desc,&handle)),"production CreateChain initializes cached RTVs");
    auto* chain=reinterpret_cast<Chain*>(handle);watched[chain->handle]=chain;
    Check(chain->targets.size()==imageCount,"cache has one RTV per runtime image");
    const auto targets=chain->targets;
    D3D11_TEXTURE2D_DESC bridge{};chain->texture->GetDesc(&bridge);
    Check((bridge.BindFlags&(D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE))==(D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE),
        "durable bridge is direct GPU upload target and shader source");
    GPUFrame::UploadTarget upload;
    Check(upload.Create(state.device,chain->texture)&&upload.rtv.p&&!upload.intermediate.p,
        "GPU transfer writes bridge directly without temporary texture or CopyResource");
    std::vector<std::uint8_t> pixels(std::size_t(desc.Width)*desc.Height*4);
    for(std::size_t i=0;i<pixels.size();i+=4){pixels[i]=64;pixels[i+1]=128;pixels[i+2]=192;pixels[i+3]=255;}
    // Exercise the actual consumer shader path: display-encoded source bytes
    // enter an UNORM RTV and are decoded/encoded only by the final XR blit.
    D3D11_TEXTURE2D_DESC sourceDesc=bridge;sourceDesc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
    sourceDesc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial{pixels.data(),unsigned(desc.Width)*4,0};
    GPUFrame::Object<ID3D11Texture2D> source;GPUFrame::Object<ID3D11ShaderResourceView> sourceView;
    Check(SUCCEEDED(state.device->CreateTexture2D(&sourceDesc,&initial,source.Put()))&&
        SUCCEEDED(state.device->CreateShaderResourceView(source.p,nullptr,sourceView.Put())),"create real GPU source image");
    GPUFrame::OpaqueCopy copy;Check(copy.Create(state.device),"production opaque upload shaders compile");
    copy.Draw(state.context,sourceView.p,upload.rtv.p,desc.Width,desc.Height);
    for(unsigned n=0;n<imageCount*3;++n){
        Check(state.Commit(*chain),"commits cycle through all cached runtime RTVs");
        Check(chain->targets==targets,"commits preserve cached RTV identities");
    }
    Check(chain->committed&&chain->bridgeCurrent,"released swapchain remains a committed layer");
    for(auto* texture:images.at(chain->handle).textures)CheckColour(Read(texture),imageFormat==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
    if(imageFormat==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB){
        auto& runtime=images.at(chain->handle);bool readSource=false;
        const auto gpuIndex=runtime.index;
        GPUFrame::Object<ID3D11Query> sourceComplete;
        const D3D11_QUERY_DESC eventDesc{D3D11_QUERY_EVENT,0};
        Check(SUCCEEDED(state.device->CreateQuery(&eventDesc,sourceComplete.Put())),"create real GPU source completion query");
        unsigned queued=0;
        const auto afterCopy=[&]{
            Check(readSource&&runtime.acquired&&runtime.waited&&runtime.index==gpuIndex,
                "GPU source completion queues after the copy and before releasing its XR image");
            state.context->End(sourceComplete.p);++queued;
        };
        HostFrameTiming::enabled=true;waitDelay=60;
        Check(state.CommitProjection(*chain,nullptr,0,source.p,readSource,afterCopy)&&readSource&&!chain->bridgeCurrent&&queued==1,
            "GPU image is copied to the acquired XR projection and records the use of its lease");
        HostFrameTiming::enabled=false;
        const auto& waitStats=HostFrameTiming::stats[HostFrameTiming::WaitImage];
        Check(waitStats.calls==1&&waitStats.slowCalls==1&&waitStats.lastSlowUs>=50000&&waitStats.lastSlowTick,
            "host diagnostics retain a measured slow runtime image wait and its timestamp");
        HRESULT eventResult=S_FALSE;
        for(unsigned attempt=0;attempt<200&&eventResult==S_FALSE;++attempt){
            eventResult=state.context->GetData(sourceComplete.p,nullptr,0,D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if(eventResult==S_FALSE)Sleep(1);
        }
        Check(eventResult==S_OK,"XR image flush also submits source completion without another context flush");
        Check(Read(runtime.textures[gpuIndex])==pixels,"direct GPU path preserves encoded BGRA bytes exactly");
        const auto cpuIndex=runtime.index;readSource=true;
        Check(state.CommitProjection(*chain,pixels.data(),desc.Width*4,nullptr,readSource,afterCopy)&&!readSource&&queued==1,
            "CPU fallback projection writes directly with no GPU source lease");
        Check(Read(runtime.textures[cpuIndex])==pixels,"direct CPU upload preserves encoded BGRA bytes exactly");
        Check(state.Commit(*chain)&&chain->bridgeCurrent,"return to durable path refreshes the recording surface");
    }else Check(!state.CanUploadProjection(*chain,source.p),"non-BGRA runtime must keep the shader conversion path");
    MirrorDesc mirrorDesc{OVR_FORMAT_B8G8R8A8_UNORM_SRGB,64,48,0,2};ovrMirrorTexture mirrorHandle=nullptr;
    Check(OVR_SUCCESS(CreateMirror(nullptr,nullptr,&mirrorDesc,&mirrorHandle)),"create mirror and its cached RTV");
    auto* mirror=reinterpret_cast<Mirror*>(mirrorHandle);auto* target=mirror->target;
    Check(target!=nullptr&&!state.CanUploadProjection(*chain,source.p),"recording mirror keeps the durable texture path");
    ID3D11Texture2D* exported=reinterpret_cast<ID3D11Texture2D*>(std::uintptr_t(1));
    Check(OVR_FAILURE(MirrorBuffer(nullptr,mirrorHandle,__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&exported)))&&!exported,
        "new mirror is not exportable before its first complete composition");
    ovrLayerQuad panel{};panel.Header.Type=ovrLayerType_Quad;panel.Header.Flags=ovrLayerFlag_HeadLocked;
    panel.ColorTexture=handle;panel.Viewport.Size={desc.Width,desc.Height};panel.QuadPoseCenter.Orientation.w=1;
    panel.QuadPoseCenter.Position.z=-2;panel.QuadSize={4,3};state.tracking.HeadPose.ThePose.Orientation.w=1;
    const ovrLayerHeader* layers[]={&panel.Header};
    for(unsigned i=0;i<4;++i)Check(state.MirrorLayers(layers,1)&&mirror->target==target,"native panel preserves mirror RTV across redraws");
    Check(OVR_SUCCESS(MirrorBuffer(nullptr,mirrorHandle,__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&exported)))&&exported,
        "successful first composition makes the persistent mirror available");Release(exported);
    const auto sessionBefore=state.session;const auto errorBefore=state.error;
    mirror->target=nullptr;
    Check(!state.MirrorLayers(layers,1)&&!mirror->ready,
        "a composition resource failure retires previously ready mirror pixels");
    Check(OVR_FAILURE(MirrorBuffer(nullptr,mirrorHandle,__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&exported)))&&!exported&&
        state.session==sessionBefore&&state.error==errorBefore,
        "failed recording composition refuses buffer export without failing the headset session");
    mirror->target=target;
    Check(state.MirrorLayers(layers,1)&&OVR_SUCCESS(MirrorBuffer(nullptr,mirrorHandle,__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&exported)))&&exported,
        "next successful composition recovers the same mirror and export");Release(exported);
    const auto bytes=Read(mirror->texture);
    const auto center=(std::size_t(mirror->height/2)*mirror->width+mirror->width/2)*4;
    Check(bytes[center]>50&&bytes[center+1]>110&&bytes[center+2]>175,"native panel remains visible in mirror");
    DestroyMirror(nullptr,mirrorHandle);Check(state.mirrors.empty(),"mirror removed and views released");
    upload.rtv.Reset();DestroyChain(nullptr,handle);Check(state.chains.empty(),"bridge owner destroyed");
}
}
int main(){
    ActionAvailability();
    EndFrameBursts();
    RuntimeFrameAvailability();
    D3D_FEATURE_LEVEL level{};Check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr,0,D3D11_SDK_VERSION,&state.device,&level,&state.context)),"software D3D11 device available");
    state.api.CreateSwapchain=Create;state.api.EnumerateSwapchainImages=Enumerate;state.api.DestroySwapchain=DestroySwapchain;
    state.api.AcquireSwapchainImage=Acquire;state.api.WaitSwapchainImage=Wait;state.api.ReleaseSwapchainImage=ReleaseImage;
    state.session=reinterpret_cast<XrSession>(1);state.running=true;
    for(const auto format:{DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB}){
        imageFormat=format;state.format=format;UploadPath();}
    Check(acquires==waits&&waits==releases&&acquires==21,"every GPU commit respects runtime acquire/wait/release");
    // An XR image that cannot expose a target view must fail before publication.
    imageBind=D3D11_BIND_SHADER_RESOURCE;ovrTextureSwapChainDesc desc{};desc.Width=32;desc.Height=24;ovrTextureSwapChain handle=nullptr;
    Check(OVR_FAILURE(CreateChain(nullptr,nullptr,&desc,&handle))&&!handle&&state.chains.empty()&&images.empty(),
        "cached target creation failure destroys partial runtime chain without a live dangling owner");
    Check(created==destroyed,"all fake runtime allocations balanced");
    state.session=XR_NULL_HANDLE;state.Close();
    std::printf("KL1 OpenXR cached-view/direct-upload WARP: %u checks PASS; no game, headset or input session.\n",checks);
    return 0;
}
