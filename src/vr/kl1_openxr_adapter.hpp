#pragma once
#include "host_frame_timing.hpp"
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#endif
// OpenXR transport adapted from the released Contracts compositor. The OVR-shaped
// boundary below is an internal adapter, not a second LibOVR runtime/session.
// The controller, UI and recording code is shared by both KL1 hosts.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#define XR_NO_PROTOTYPES
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include "openxr/openxr.h"
#include "openxr/openxr_platform.h"
#include "OVR_CAPI.h"
#include "OVR_CAPI_D3D.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <limits>
#include "openxr_controller_pose.hpp"

namespace KL1OpenXR {
using Logger=void(*)(const char*);
inline Logger logger=nullptr;
inline void Log(const char* text){if(logger)logger(text);}
inline void SetLog(Logger value){logger=value;}
template<class T>inline void Release(T*& object){if(object){object->Release();object=nullptr;}}
inline XrPosef Identity(){XrPosef p{};p.orientation.w=1;return p;}
inline XrQuaternionf Conjugate(XrQuaternionf q){return {-q.x,-q.y,-q.z,q.w};}
inline XrQuaternionf Multiply(XrQuaternionf a,XrQuaternionf b){return {
    a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
    a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};}
inline XrVector3f Rotate(XrQuaternionf q,XrVector3f v){const XrVector3f t{
    2*(q.y*v.z-q.z*v.y),2*(q.z*v.x-q.x*v.z),2*(q.x*v.y-q.y*v.x)};
    return {v.x+q.w*t.x+q.y*t.z-q.z*t.y,v.y+q.w*t.y+q.z*t.x-q.x*t.z,v.z+q.w*t.z+q.x*t.y-q.y*t.x};}
inline XrPosef Pose(const ovrPosef& p){return {{p.Orientation.x,p.Orientation.y,p.Orientation.z,p.Orientation.w},{p.Position.x,p.Position.y,p.Position.z}};}
inline ovrPosef Pose(const XrPosef& p){return {{p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w},{p.position.x,p.position.y,p.position.z}};}
inline bool Valid(const XrPosef& p){const auto& q=p.orientation;const float n=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
    return std::isfinite(n)&&n>.9f&&n<1.1f&&std::isfinite(p.position.x)&&std::isfinite(p.position.y)&&std::isfinite(p.position.z);}
inline float Unit(float v){return std::isfinite(v)?std::clamp(v,0.f,1.f):0.f;}
inline float Axis(float v){return std::isfinite(v)?std::clamp(v,-1.f,1.f):0.f;}
inline XrFovf Fov(const ovrFovPort& f){return {-std::atan(f.LeftTan),std::atan(f.RightTan),std::atan(f.UpTan),-std::atan(f.DownTan)};}
inline XrPosef Compose(XrPosef a,XrPosef b){const auto p=Rotate(a.orientation,b.position);return {Multiply(a.orientation,b.orientation),
    {a.position.x+p.x,a.position.y+p.y,a.position.z+p.z}};}
inline std::wstring Directory(){wchar_t p[32768]{};const DWORD n=GetModuleFileNameW(nullptr,p,32768);if(!n||n>=32768)return {};
    std::wstring s(p,n);const auto slash=s.find_last_of(L"\\/");return slash==std::wstring::npos?L"":s.substr(0,slash+1);}

#define CONTRACTS_XR_FUNCTIONS(X) \
 X(EnumerateInstanceExtensionProperties) X(CreateInstance) X(DestroyInstance) X(GetInstanceProperties) \
 X(GetSystem) X(GetSystemProperties) X(EnumerateViewConfigurationViews) X(EnumerateEnvironmentBlendModes) \
 X(GetD3D11GraphicsRequirementsKHR) X(CreateSession) X(DestroySession) X(PollEvent) X(BeginSession) X(EndSession) \
 X(EnumerateReferenceSpaces) X(CreateReferenceSpace) X(DestroySpace) X(LocateSpace) X(LocateViews) \
 X(EnumerateSwapchainFormats) X(CreateSwapchain) X(DestroySwapchain) X(EnumerateSwapchainImages) \
 X(AcquireSwapchainImage) X(WaitSwapchainImage) X(ReleaseSwapchainImage) X(WaitFrame) X(BeginFrame) X(EndFrame) \
 X(StringToPath) X(CreateActionSet) X(DestroyActionSet) X(CreateAction) X(CreateActionSpace) \
 X(SuggestInteractionProfileBindings) X(AttachSessionActionSets) X(SyncActions) X(ApplyHapticFeedback) X(StopHapticFeedback) \
 X(GetActionStatePose) X(GetActionStateFloat) X(GetActionStateBoolean) X(GetActionStateVector2f)
struct Api {
    HMODULE library=nullptr;PFN_xrGetInstanceProcAddr GetInstanceProcAddr=nullptr;
#define DECLARE(name) PFN_xr##name name=nullptr;
    CONTRACTS_XR_FUNCTIONS(DECLARE)
#undef DECLARE
    bool Load(){library=LoadLibraryExW((Directory()+L"openxr_loader.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if(!library)return false;GetInstanceProcAddr=reinterpret_cast<PFN_xrGetInstanceProcAddr>(GetProcAddress(library,"xrGetInstanceProcAddr"));
        return GetInstanceProcAddr&&XR_SUCCEEDED(GetInstanceProcAddr(XR_NULL_HANDLE,"xrEnumerateInstanceExtensionProperties",reinterpret_cast<PFN_xrVoidFunction*>(&EnumerateInstanceExtensionProperties)))&&
            XR_SUCCEEDED(GetInstanceProcAddr(XR_NULL_HANDLE,"xrCreateInstance",reinterpret_cast<PFN_xrVoidFunction*>(&CreateInstance)));}
    bool Resolve(XrInstance instance){
#define RESOLVE(name) if(XR_FAILED(GetInstanceProcAddr(instance,"xr" #name,reinterpret_cast<PFN_xrVoidFunction*>(&name))))return false;
        CONTRACTS_XR_FUNCTIONS(RESOLVE)
#undef RESOLVE
        return true;}
    void Close(){if(library)FreeLibrary(library);library=nullptr;}
};

// Small host-owned blitter, also used for the complete rectilinear recording
// mirror. It never samples an OpenXR-owned image after xrReleaseSwapchainImage.
class Blitter {
    HMODULE compiler=nullptr;ID3D11VertexShader* vs=nullptr;ID3D11PixelShader* ps=nullptr;
    ID3D11InputLayout* layout=nullptr;ID3D11Buffer* vertices=nullptr;ID3D11Buffer* fullVertices=nullptr;ID3D11SamplerState* sampler=nullptr;
    ID3D11BlendState* blend=nullptr;ID3D11RasterizerState* raster=nullptr;ID3D11DepthStencilState* depth=nullptr;
public:
    struct Vertex {float x,y,z,w,u,v;};
    ~Blitter(){Close();}
    void Close(){Release(vs);Release(ps);Release(layout);Release(vertices);Release(fullVertices);Release(sampler);Release(blend);Release(raster);Release(depth);
        if(compiler)FreeLibrary(compiler);compiler=nullptr;}
    bool Initialize(ID3D11Device* device){if(vs)return true;
        compiler=LoadLibraryExW(L"d3dcompiler_47.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);if(!compiler)return false;
        const auto compile=reinterpret_cast<decltype(&D3DCompile)>(GetProcAddress(compiler,"D3DCompile"));if(!compile)return false;
        const char shader[]="struct V{float4 p:POSITION;float2 t:TEXCOORD;};struct O{float4 p:SV_POSITION;float2 t:TEXCOORD;};"
            "O vertex(V v){O o;o.p=v.p;o.t=v.t;return o;}Texture2D image:register(t0);SamplerState smp:register(s0);"
            "float4 pixel(O v):SV_TARGET{return image.Sample(smp,v.t);}";
        ID3DBlob *vertex=nullptr,*pixel=nullptr,*errors=nullptr;
        HRESULT hr=compile(shader,sizeof(shader),nullptr,nullptr,nullptr,"vertex","vs_4_0",0,0,&vertex,&errors);Release(errors);
        if(SUCCEEDED(hr))hr=compile(shader,sizeof(shader),nullptr,nullptr,nullptr,"pixel","ps_4_0",0,0,&pixel,&errors);Release(errors);
        if(SUCCEEDED(hr))hr=device->CreateVertexShader(vertex->GetBufferPointer(),vertex->GetBufferSize(),nullptr,&vs);
        if(SUCCEEDED(hr))hr=device->CreatePixelShader(pixel->GetBufferPointer(),pixel->GetBufferSize(),nullptr,&ps);
        const D3D11_INPUT_ELEMENT_DESC elements[]={{"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,16,D3D11_INPUT_PER_VERTEX_DATA,0}};
        if(SUCCEEDED(hr))hr=device->CreateInputLayout(elements,2,vertex->GetBufferPointer(),vertex->GetBufferSize(),&layout);
        Release(vertex);Release(pixel);
        D3D11_BUFFER_DESC vb{};vb.ByteWidth=sizeof(Vertex)*6;vb.Usage=D3D11_USAGE_DYNAMIC;vb.BindFlags=D3D11_BIND_VERTEX_BUFFER;vb.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        if(SUCCEEDED(hr))hr=device->CreateBuffer(&vb,nullptr,&vertices);
        // Ordinary eye commits always use the same full-image rectangle.
        // Keep it immutable; only perspective/cropped recording quads need Map.
        const auto rectangle=Full();D3D11_SUBRESOURCE_DATA fullData{rectangle.data(),0,0};
        vb.Usage=D3D11_USAGE_IMMUTABLE;vb.CPUAccessFlags=0;
        if(SUCCEEDED(hr))hr=device->CreateBuffer(&vb,&fullData,&fullVertices);
        D3D11_SAMPLER_DESC smp{};smp.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;smp.AddressU=smp.AddressV=smp.AddressW=D3D11_TEXTURE_ADDRESS_BORDER;
        smp.MaxLOD=D3D11_FLOAT32_MAX;if(SUCCEEDED(hr))hr=device->CreateSamplerState(&smp,&sampler);
        D3D11_BLEND_DESC bd{};bd.RenderTarget[0].BlendEnable=TRUE;bd.RenderTarget[0].SrcBlend=D3D11_BLEND_ONE;bd.RenderTarget[0].DestBlend=D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOp=bd.RenderTarget[0].BlendOpAlpha=D3D11_BLEND_OP_ADD;bd.RenderTarget[0].SrcBlendAlpha=D3D11_BLEND_ONE;bd.RenderTarget[0].DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;if(SUCCEEDED(hr))hr=device->CreateBlendState(&bd,&blend);
        D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;
        if(SUCCEEDED(hr))hr=device->CreateRasterizerState(&rd,&raster);
        D3D11_DEPTH_STENCIL_DESC dd{};dd.DepthEnable=FALSE;dd.StencilEnable=FALSE;if(SUCCEEDED(hr))hr=device->CreateDepthStencilState(&dd,&depth);
        if(FAILED(hr)){Close();return false;}return true;}
    bool DrawGeometry(ID3D11DeviceContext* context,ID3D11RenderTargetView* target,unsigned width,unsigned height,
                      ID3D11ShaderResourceView* image,ID3D11Buffer* geometry,bool alpha){
        D3D11_VIEWPORT viewport{0,0,static_cast<float>(width),static_cast<float>(height),0,1};
        context->RSSetViewports(1,&viewport);context->RSSetState(raster);context->OMSetDepthStencilState(depth,0);
        context->OMSetRenderTargets(1,&target,nullptr);context->OMSetBlendState(alpha?blend:nullptr,nullptr,0xffffffff);
        const UINT stride=sizeof(Vertex),offset=0;context->IASetInputLayout(layout);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->IASetVertexBuffers(0,1,&geometry,&stride,&offset);context->VSSetShader(vs,nullptr,0);context->PSSetShader(ps,nullptr,0);
        context->PSSetSamplers(0,1,&sampler);context->PSSetShaderResources(0,1,&image);context->Draw(6,0);
        ID3D11ShaderResourceView* empty=nullptr;context->PSSetShaderResources(0,1,&empty);context->OMSetRenderTargets(0,nullptr,nullptr);return true;}
    bool DrawFull(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11RenderTargetView* target,unsigned width,unsigned height,
                  ID3D11ShaderResourceView* image){
        return target&&image&&width&&height&&Initialize(device)&&DrawGeometry(context,target,width,height,image,fullVertices,false);}
    bool Draw(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11RenderTargetView* target,unsigned width,unsigned height,
              ID3D11ShaderResourceView* image,const Vertex (&quad)[6],bool alpha){
        if(!target||!image||!width||!height||!Initialize(device))return false;
        D3D11_MAPPED_SUBRESOURCE mapped{};if(FAILED(context->Map(vertices,0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))return false;
        std::memcpy(mapped.pData,quad,sizeof(quad));context->Unmap(vertices,0);
        return DrawGeometry(context,target,width,height,image,vertices,alpha);}
    static std::array<Vertex,6> Full(float u0=0,float v0=0,float u1=1,float v1=1){return {{{-1,1,0,1,u0,v0},{1,1,0,1,u1,v0},{1,-1,0,1,u1,v1},
        {-1,1,0,1,u0,v0},{1,-1,0,1,u1,v1},{-1,-1,0,1,u0,v1}}};}
};

struct Chain {
    XrSwapchain handle=XR_NULL_HANDLE;std::vector<XrSwapchainImageD3D11KHR> images;
    // Views borrow each acquired runtime image for the chain's lifetime.
    // They must be released before xrDestroySwapchain retires those images.
    std::vector<ID3D11RenderTargetView*> targets;
    ID3D11Texture2D* texture=nullptr;ID3D11ShaderResourceView* view=nullptr;
    unsigned width=0,height=0;bool committed=false,bridgeCurrent=false;
};
struct Mirror {ID3D11Texture2D* texture=nullptr;ID3D11RenderTargetView* target=nullptr;unsigned width=0,height=0,eye=0;bool ready=false;};
struct State {
    Api api;XrInstance instance=XR_NULL_HANDLE;XrSystemId system=XR_NULL_SYSTEM_ID;XrSession session=XR_NULL_HANDLE;
    XrSpace world=XR_NULL_HANDLE,view=XR_NULL_HANDLE,base=XR_NULL_HANDLE;
    XrReferenceSpaceType worldType=XR_REFERENCE_SPACE_TYPE_STAGE;float localFloor=0;
    XrSessionState status=XR_SESSION_STATE_UNKNOWN;XrEnvironmentBlendMode blendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    bool running=false,frameBegun=false,shouldRender=false,quit=false,configured=false,everConfigured=false,lastLayers=false;
    bool Renderable()const{return running&&frameBegun&&shouldRender&&!quit;}
    XrTime displayTime=0,earliestCalibration=0;XrGraphicsRequirementsD3D11KHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
    XrView views[2]={{XR_TYPE_VIEW},{XR_TYPE_VIEW}};XrViewConfigurationView dimensions[2]={{XR_TYPE_VIEW_CONFIGURATION_VIEW},{XR_TYPE_VIEW_CONFIGURATION_VIEW}};
    ovrHmdDesc hmd{};ovrEyeRenderDesc eyes[2]{};ovrTrackingState tracking{};ovrInputState input{};
    bool gripInputActive[2]{},triggerInputActive[2]{};
    std::int64_t format=0;ID3D11Device* device=nullptr;ID3D11DeviceContext* context=nullptr;
    std::vector<Chain*> chains;std::vector<Mirror*> mirrors;Blitter blitter;
    ovrResult error=ovrSuccess;char message[512]="OpenXR is starting.",runtimeName[XR_MAX_RUNTIME_NAME_SIZE]{};
    std::wstring audioGuid;
    struct Actions {XrActionSet set=XR_NULL_HANDLE;XrAction pose=XR_NULL_HANDLE,aimPose=XR_NULL_HANDLE,trigger=XR_NULL_HANDLE,grip=XR_NULL_HANDLE,gripClick=XR_NULL_HANDLE,
        stick=XR_NULL_HANDLE,primary=XR_NULL_HANDLE,secondary=XR_NULL_HANDLE,click=XR_NULL_HANDLE,menu=XR_NULL_HANDLE,haptic=XR_NULL_HANDLE;
        XrPath hands[2]{};XrSpace spaces[2]{},aimSpaces[2]{};} actions;
    bool Failure(int code,const char* operation){error=code<0?code:-92100;std::snprintf(message,sizeof(message),"%s (%d).",operation,code);std::fprintf(stderr,"%s\n",message);Log(message);return false;}
    bool Check(XrResult result,const char* operation){
        if(result==XR_ERROR_FORM_FACTOR_UNAVAILABLE)return Failure(static_cast<int>(result),
            "No headset is connected to the selected OpenXR runtime; connect it through Virtual Desktop, SteamVR or Link and retry");
        if(result==XR_ERROR_RUNTIME_UNAVAILABLE)return Failure(static_cast<int>(result),
            "The selected OpenXR runtime is unavailable; install or start its PC software and reconnect the headset");
        return XR_SUCCEEDED(result)||Failure(static_cast<int>(result),operation);}
    bool Initialize();bool Attach(ID3D11Device* value);bool CreateActions();bool Events();bool Begin();bool End(const XrCompositionLayerBaseHeader* const* layers,unsigned count);
    void Sample();bool Calibrate(const XrPosef& head);bool Recenter();bool Commit(Chain& chain);bool Submit(const ovrLayerHeader* const* layers,unsigned count);
    bool CanUploadProjection(const Chain& chain,ID3D11Texture2D* source)const;
    template<class Queued> bool CommitProjection(Chain& chain,const unsigned char* pixels,unsigned stride,
        ID3D11Texture2D* source,bool& sourceRead,Queued afterCopy);
    template<class Write> bool CommitImage(Chain& chain,Write write);
    bool MirrorLayers(const ovrLayerHeader* const* layers,unsigned count);void Close();
    bool Path(const char* text,XrPath& result){return Check(api.StringToPath(instance,text,&result),"xrStringToPath");}
    bool Action(const char* name,XrActionType type,XrAction& handle){XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};info.actionType=type;
        info.countSubactionPaths=2;info.subactionPaths=actions.hands;std::snprintf(info.actionName,sizeof(info.actionName),"%s",name);
        std::snprintf(info.localizedActionName,sizeof(info.localizedActionName),"%s",name);return Check(api.CreateAction(actions.set,&info,&handle),"xrCreateAction");}
    bool Boolean(XrAction action,unsigned hand,bool* active=nullptr){XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};info.action=action;info.subactionPath=actions.hands[hand];
        XrActionStateBoolean value{XR_TYPE_ACTION_STATE_BOOLEAN};const bool valid=XR_SUCCEEDED(api.GetActionStateBoolean(session,&info,&value))&&value.isActive;
        if(active)*active=valid;return valid&&value.currentState;}
    float Float(XrAction action,unsigned hand,bool* active=nullptr){XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};info.action=action;info.subactionPath=actions.hands[hand];
        XrActionStateFloat value{XR_TYPE_ACTION_STATE_FLOAT};const bool valid=XR_SUCCEEDED(api.GetActionStateFloat(session,&info,&value))&&value.isActive&&
            std::isfinite(value.currentState)&&value.currentState>=-.01f&&value.currentState<=1.01f;
        if(active)*active=valid;return valid?Unit(value.currentState):0;}
    bool Suggest(const char* profile,const char* grip,const char* gripButton,const char* stick,const char* leftA,const char* leftB,const char* rightA,const char* rightB,const char* click,const char* menu);
};
inline State state;

inline bool State::Initialize(){
    if(!api.Load())return Failure(-92001,"Cannot load bundled x64 openxr_loader.dll");
    std::uint32_t count=0;if(!Check(api.EnumerateInstanceExtensionProperties(nullptr,0,&count,nullptr),"xrEnumerateInstanceExtensionProperties")||count>4096)return false;
    std::vector<XrExtensionProperties> available(count,{XR_TYPE_EXTENSION_PROPERTIES});if(!Check(api.EnumerateInstanceExtensionProperties(nullptr,count,&count,available.data()),"xrEnumerateInstanceExtensionProperties"))return false;
    const auto has=[&](const char* name){for(const auto& e:available)if(!std::strcmp(e.extensionName,name))return true;return false;};
    if(!has(XR_KHR_D3D11_ENABLE_EXTENSION_NAME))return Failure(-92003,"The runtime lacks XR_KHR_D3D11_enable");
    std::vector<const char*> extensions{XR_KHR_D3D11_ENABLE_EXTENSION_NAME};
    for(const char* optional:{XR_OCULUS_AUDIO_DEVICE_GUID_EXTENSION_NAME,XR_EXT_HP_MIXED_REALITY_CONTROLLER_EXTENSION_NAME})if(has(optional))extensions.push_back(optional);
    XrInstanceCreateInfo create{XR_TYPE_INSTANCE_CREATE_INFO};std::strcpy(create.applicationInfo.applicationName,"Kane and Lynch Dead Men VR");create.applicationInfo.applicationVersion=319;
    std::strcpy(create.applicationInfo.engineName,"KL1 VR");create.applicationInfo.engineVersion=1;create.applicationInfo.apiVersion=XR_MAKE_VERSION(1,0,0);
    create.enabledExtensionCount=static_cast<unsigned>(extensions.size());create.enabledExtensionNames=extensions.data();
    if(!Check(api.CreateInstance(&create,&instance),"xrCreateInstance"))return false;
    if(!api.Resolve(instance))return Failure(-92001,"Cannot resolve required OpenXR functions");
    XrInstanceProperties properties{XR_TYPE_INSTANCE_PROPERTIES};if(!Check(api.GetInstanceProperties(instance,&properties),"xrGetInstanceProperties"))return false;
    std::snprintf(runtimeName,sizeof(runtimeName),"%s",properties.runtimeName);Log(runtimeName);
    XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};systemInfo.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if(!Check(api.GetSystem(instance,&systemInfo,&system),"xrGetSystem(HMD)"))return false;
    if(!Check(api.GetD3D11GraphicsRequirementsKHR(instance,system,&requirements),"xrGetD3D11GraphicsRequirementsKHR"))return false;
    count=0;if(!Check(api.EnumerateViewConfigurationViews(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,0,&count,nullptr),"xrEnumerateViewConfigurationViews")||count!=2)
        return Failure(-92003,"The runtime must expose exactly two primary stereo views");
    if(!Check(api.EnumerateViewConfigurationViews(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,2,&count,dimensions),"xrEnumerateViewConfigurationViews"))return false;
    count=0;if(!Check(api.EnumerateEnvironmentBlendModes(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,0,&count,nullptr),"xrEnumerateEnvironmentBlendModes")||count>128)return false;
    std::vector<XrEnvironmentBlendMode> modes(count);if(!Check(api.EnumerateEnvironmentBlendModes(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,count,&count,modes.data()),"xrEnumerateEnvironmentBlendModes"))return false;
    if(std::find(modes.begin(),modes.end(),XR_ENVIRONMENT_BLEND_MODE_OPAQUE)==modes.end())return Failure(-92003,"An opaque PC VR composition mode is required");
    if(has(XR_OCULUS_AUDIO_DEVICE_GUID_EXTENSION_NAME)){
        PFN_xrGetAudioOutputDeviceGuidOculus audio=nullptr;
        if(XR_SUCCEEDED(api.GetInstanceProcAddr(instance,"xrGetAudioOutputDeviceGuidOculus",reinterpret_cast<PFN_xrVoidFunction*>(&audio)))&&audio){
            wchar_t guid[XR_MAX_AUDIO_DEVICE_STR_SIZE_OCULUS]{};if(XR_SUCCEEDED(audio(instance,guid)))audioGuid=guid;}}
    hmd.Type=ovrHmd_CV1;std::snprintf(hmd.ProductName,sizeof(hmd.ProductName),"%.63s",runtimeName);hmd.DisplayRefreshRate=90.f;
    // These placeholders are NEVER published as a ready eye calibration.
    for(unsigned eye=0;eye<2;++eye){hmd.DefaultEyeFov[eye]={1,1,1,1};eyes[eye].Eye=static_cast<ovrEyeType>(eye);eyes[eye].Fov=hmd.DefaultEyeFov[eye];}
    error=0;std::snprintf(message,sizeof(message),"OpenXR runtime and HMD found: %.128s.",runtimeName);return true;
}
inline bool State::Attach(ID3D11Device* value){
    device=value;device->AddRef();device->GetImmediateContext(&context);
    XrGraphicsBindingD3D11KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};binding.device=device;
    XrSessionCreateInfo create{XR_TYPE_SESSION_CREATE_INFO};create.next=&binding;create.systemId=system;
    if(!Check(api.CreateSession(instance,&create,&session),"xrCreateSession"))return false;
    std::uint32_t count=0;if(!Check(api.EnumerateReferenceSpaces(session,0,&count,nullptr),"xrEnumerateReferenceSpaces")||count>128)return false;
    std::vector<XrReferenceSpaceType> spaces(count);if(!Check(api.EnumerateReferenceSpaces(session,count,&count,spaces.data()),"xrEnumerateReferenceSpaces"))return false;
    worldType=std::find(spaces.begin(),spaces.end(),XR_REFERENCE_SPACE_TYPE_STAGE)!=spaces.end()?XR_REFERENCE_SPACE_TYPE_STAGE:XR_REFERENCE_SPACE_TYPE_LOCAL;
    XrReferenceSpaceCreateInfo reference{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};reference.referenceSpaceType=worldType;reference.poseInReferenceSpace=Identity();
    if(!Check(api.CreateReferenceSpace(session,&reference,&base),"xrCreateReferenceSpace(base)"))return false;
    if(worldType==XR_REFERENCE_SPACE_TYPE_LOCAL){localFloor=std::clamp(GetPrivateProfileIntW(L"VR",L"OpenXRLocalFloorMillimeters",1700,(Directory()+L"vr.ini").c_str()),0u,2500u)/1000.f;
        reference.poseInReferenceSpace.position.y=-localFloor;Log("OpenXR origin: LOCAL with common floor offset for head and controllers.");}
    else Log("OpenXR origin: STAGE floor.");
    if(!Check(api.CreateReferenceSpace(session,&reference,&world),"xrCreateReferenceSpace(world)"))return false;
    reference.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_VIEW;reference.poseInReferenceSpace=Identity();
    if(!Check(api.CreateReferenceSpace(session,&reference,&view),"xrCreateReferenceSpace(VIEW)")||!CreateActions())return false;
    count=0;if(!Check(api.EnumerateSwapchainFormats(session,0,&count,nullptr),"xrEnumerateSwapchainFormats")||count>4096)return false;
    std::vector<std::int64_t> formats(count);if(!Check(api.EnumerateSwapchainFormats(session,count,&count,formats.data()),"xrEnumerateSwapchainFormats"))return false;
    for(auto preferred:{DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,DXGI_FORMAT_R8G8B8A8_UNORM})
        if(std::find(formats.begin(),formats.end(),preferred)!=formats.end()){format=preferred;break;}
    if(!format)return Failure(-92003,"No supported BGRA/RGBA 8-bit OpenXR swapchain format");
    return true;
}
inline bool State::Suggest(const char* profile,const char* grip,const char* gripButton,const char* stick,const char* leftA,const char* leftB,const char* rightA,const char* rightB,const char* click,const char* menu){
    XrPath profilePath{};if(!Path(profile,profilePath))return false;std::vector<XrActionSuggestedBinding> bindings;
    const auto bind=[&](XrAction action,unsigned hand,const char* suffix){if(!suffix)return true;XrPath path{};
        const std::string name=std::string(hand?"/user/hand/right":"/user/hand/left")+suffix;if(!Path(name.c_str(),path))return false;bindings.push_back({action,path});return true;};
    for(unsigned hand=0;hand<2;++hand)if(!bind(actions.pose,hand,"/input/grip/pose")||!bind(actions.aimPose,hand,"/input/aim/pose")||!bind(actions.trigger,hand,"/input/trigger/value")||
        !bind(actions.haptic,hand,"/output/haptic")||!bind(actions.grip,hand,grip)||!bind(actions.gripClick,hand,gripButton)||!bind(actions.stick,hand,stick)||
        !bind(actions.primary,hand,hand?rightA:leftA)||!bind(actions.secondary,hand,hand?rightB:leftB)||!bind(actions.click,hand,click)||(!hand&&!bind(actions.menu,hand,menu)))return false;
    XrInteractionProfileSuggestedBinding suggest{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};suggest.interactionProfile=profilePath;
    suggest.countSuggestedBindings=static_cast<unsigned>(bindings.size());suggest.suggestedBindings=bindings.data();
    const auto result=api.SuggestInteractionProfileBindings(instance,&suggest);if(XR_FAILED(result)){char text[256];std::snprintf(text,sizeof(text),"Optional controller profile not supported: %s (%d).",profile,result);Log(text);}return XR_SUCCEEDED(result);
}
inline bool State::CreateActions(){
    if(!Path("/user/hand/left",actions.hands[0])||!Path("/user/hand/right",actions.hands[1]))return false;
    XrActionSetCreateInfo create{XR_TYPE_ACTION_SET_CREATE_INFO};std::strcpy(create.actionSetName,"kane_lynch");std::strcpy(create.localizedActionSetName,"Kane and Lynch Dead Men VR");
    if(!Check(api.CreateActionSet(instance,&create,&actions.set),"xrCreateActionSet"))return false;
    if(!Action("hand_pose",XR_ACTION_TYPE_POSE_INPUT,actions.pose)||!Action("hand_aim_pose",XR_ACTION_TYPE_POSE_INPUT,actions.aimPose)||!Action("trigger",XR_ACTION_TYPE_FLOAT_INPUT,actions.trigger)||
        !Action("grip",XR_ACTION_TYPE_FLOAT_INPUT,actions.grip)||!Action("grip_click",XR_ACTION_TYPE_BOOLEAN_INPUT,actions.gripClick)||
        !Action("stick",XR_ACTION_TYPE_VECTOR2F_INPUT,actions.stick)||!Action("primary",XR_ACTION_TYPE_BOOLEAN_INPUT,actions.primary)||
        !Action("secondary",XR_ACTION_TYPE_BOOLEAN_INPUT,actions.secondary)||!Action("stick_click",XR_ACTION_TYPE_BOOLEAN_INPUT,actions.click)||!Action("menu",XR_ACTION_TYPE_BOOLEAN_INPUT,actions.menu)||!Action("reload_feedback",XR_ACTION_TYPE_VIBRATION_OUTPUT,actions.haptic))return false;
    unsigned accepted=0;
    accepted+=Suggest("/interaction_profiles/oculus/touch_controller","/input/squeeze/value",nullptr,"/input/thumbstick","/input/x/click","/input/y/click","/input/a/click","/input/b/click","/input/thumbstick/click","/input/menu/click");
    accepted+=Suggest("/interaction_profiles/valve/index_controller","/input/squeeze/value",nullptr,"/input/thumbstick","/input/a/click","/input/b/click","/input/a/click","/input/b/click","/input/thumbstick/click",nullptr);
    accepted+=Suggest("/interaction_profiles/htc/vive_controller",nullptr,"/input/squeeze/click","/input/trackpad","/input/trackpad/click","/input/menu/click","/input/trackpad/click","/input/menu/click",nullptr,nullptr);
    accepted+=Suggest("/interaction_profiles/microsoft/motion_controller",nullptr,"/input/squeeze/click","/input/thumbstick","/input/trackpad/click","/input/menu/click","/input/trackpad/click","/input/menu/click","/input/thumbstick/click",nullptr);
    accepted+=Suggest("/interaction_profiles/hp/mixed_reality_controller","/input/squeeze/value",nullptr,"/input/thumbstick","/input/x/click","/input/y/click","/input/a/click","/input/b/click","/input/thumbstick/click","/input/menu/click");
    if(!accepted)return Failure(-92004,"No supported OpenXR controller profile was accepted");
    for(unsigned hand=0;hand<2;++hand){XrActionSpaceCreateInfo space{XR_TYPE_ACTION_SPACE_CREATE_INFO};space.action=actions.pose;space.subactionPath=actions.hands[hand];space.poseInActionSpace=Identity();
        if(!Check(api.CreateActionSpace(session,&space,&actions.spaces[hand]),"xrCreateActionSpace(grip)"))return false;
        space.action=actions.aimPose;if(!Check(api.CreateActionSpace(session,&space,&actions.aimSpaces[hand]),"xrCreateActionSpace(aim)"))return false;}
    Log("OpenXR controller basis: grip position with aim orientation.");
    XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};attach.countActionSets=1;attach.actionSets=&actions.set;
    return Check(api.AttachSessionActionSets(session,&attach),"xrAttachSessionActionSets");
}
inline void Pulse(unsigned side,float amplitude,unsigned milliseconds){
 if(side>1||!state.session||!state.running||!state.actions.haptic)return;
 XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO};info.action=state.actions.haptic;info.subactionPath=state.actions.hands[side];
 if(amplitude<=0){state.api.StopHapticFeedback(state.session,&info);return;}
 if(state.status!=XR_SESSION_STATE_FOCUSED)return;
 XrHapticVibration vibration{XR_TYPE_HAPTIC_VIBRATION};vibration.amplitude=Unit(amplitude);vibration.frequency=XR_FREQUENCY_UNSPECIFIED;
 vibration.duration=static_cast<XrDuration>(std::clamp(milliseconds,1u,150u))*1000000;
 state.api.ApplyHapticFeedback(state.session,&info,reinterpret_cast<const XrHapticBaseHeader*>(&vibration));
}
inline bool State::End(const XrCompositionLayerBaseHeader* const* layers,unsigned count){
    if(!frameBegun)return true;XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.displayTime=displayTime;end.environmentBlendMode=blendMode;
    end.layerCount=shouldRender?count:0;end.layers=end.layerCount?layers:nullptr;
    XrResult result;
    {HostFrameTiming::Scope timing(HostFrameTiming::EndFrame,end.layerCount!=0);result=api.EndFrame(session,&end);}
    frameBegun=false;lastLayers=XR_SUCCEEDED(result)&&end.layerCount!=0;return Check(result,"xrEndFrame");
}
inline bool State::Events(){
    for(;;){XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};const auto result=api.PollEvent(instance,&event);
        if(result==XR_EVENT_UNAVAILABLE)return true;if(!Check(result,"xrPollEvent"))return false;
        if(event.type==XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING){quit=true;return true;}
        if(event.type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED){const auto& change=*reinterpret_cast<XrEventDataSessionStateChanged*>(&event);if(change.session!=session)continue;
            status=change.state;char text[100];std::snprintf(text,sizeof(text),"OpenXR session state: %d.",status);Log(text);
            if(status==XR_SESSION_STATE_READY&&!running){XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};begin.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                if(!Check(api.BeginSession(session,&begin),"xrBeginSession"))return false;running=true;}
            if(status==XR_SESSION_STATE_STOPPING&&running){if(!End(nullptr,0)||!Check(api.EndSession(session),"xrEndSession"))return false;running=configured=false;}
            if(status==XR_SESSION_STATE_EXITING||status==XR_SESSION_STATE_LOSS_PENDING)quit=true;
        }
        if(event.type==XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING){const auto& change=*reinterpret_cast<XrEventDataReferenceSpaceChangePending*>(&event);
            if(!everConfigured){earliestCalibration=std::max(earliestCalibration,change.changeTime);continue;}
            quit=true;configured=false;return Failure(-92010,"OpenXR tracking origin changed; press F9 to restart calibration");}
    }
}
inline bool State::Begin(){
    if(!End(nullptr,0)||!Events())return false;
    tracking={};input={};configured=false;if(!running||quit)return true;
    XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};XrFrameState frame{XR_TYPE_FRAME_STATE};
    XrResult waited;
    {HostFrameTiming::Scope timing(HostFrameTiming::WaitFrame);waited=api.WaitFrame(session,&wait,&frame);}
    if(!Check(waited,"xrWaitFrame"))return false;
    XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
    XrResult begun;
    {HostFrameTiming::Scope timing(HostFrameTiming::BeginFrame);begun=api.BeginFrame(session,&begin);}
    if(!Check(begun,"xrBeginFrame"))return false;
    frameBegun=true;shouldRender=frame.shouldRender!=XR_FALSE;displayTime=frame.predictedDisplayTime;
    if(frame.predictedDisplayPeriod>0)hmd.DisplayRefreshRate=static_cast<float>(1e9/frame.predictedDisplayPeriod);
    {HostFrameTiming::Scope timing(HostFrameTiming::TrackingSample);Sample();}
    return error==0;
}
inline bool State::Calibrate(const XrPosef& head){
    // IPC8 has immutable session eye configuration, not a configuration
    // seqlock. Freeze the first valid head-relative calibration; F9 creates
    // a new host if the user changes runtime IPD/FOV settings.
    if(displayTime<earliestCalibration)return false;if(everConfigured)return true;
    XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};locate.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locate.displayTime=displayTime;locate.space=world;XrViewState viewState{XR_TYPE_VIEW_STATE};unsigned count=0;
    if(XR_FAILED(api.LocateViews(session,&locate,&viewState,2,&count,views))||count!=2||
       (viewState.viewStateFlags&(XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_POSITION_VALID_BIT))!=(XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_POSITION_VALID_BIT))return false;
    const auto inverse=Conjugate(head.orientation);
    for(unsigned eye=0;eye<2;++eye){const auto& source=views[eye];if(!Valid(source.pose))return false;
        const auto delta=Rotate(inverse,{source.pose.position.x-head.position.x,source.pose.position.y-head.position.y,source.pose.position.z-head.position.z});
        if(std::fabs(delta.x)>.2f||std::fabs(delta.y)>.2f||std::fabs(delta.z)>.2f)return false;
        const auto rotation=Multiply(inverse,source.pose.orientation);float minX=10,maxX=-10,minY=10,maxY=-10;
        for(float x:{std::tan(source.fov.angleLeft),std::tan(source.fov.angleRight)})for(float y:{std::tan(source.fov.angleDown),std::tan(source.fov.angleUp)}){
            const auto ray=Rotate(rotation,{x,y,-1});if(!std::isfinite(ray.z)||ray.z>=-.001f)return false;
            minX=std::min(minX,ray.x/-ray.z);maxX=std::max(maxX,ray.x/-ray.z);minY=std::min(minY,ray.y/-ray.z);maxY=std::max(maxY,ray.y/-ray.z);}
        const ovrFovPort fov{maxY,-minY,-minX,maxX};if(fov.UpTan<=.01f||fov.DownTan<=.01f||fov.LeftTan<=.01f||fov.RightTan<=.01f||
            !std::isfinite(fov.UpTan+fov.DownTan+fov.LeftTan+fov.RightTan))return false;
        eyes[eye].Eye=static_cast<ovrEyeType>(eye);eyes[eye].HmdToEyeOffset={delta.x,delta.y,delta.z};eyes[eye].Fov=fov;hmd.DefaultEyeFov[eye]=fov;
    }
    if(!everConfigured)Log("OpenXR exact per-eye IPD and parallel FOV calibrated from tracked runtime views.");everConfigured=true;return true;
}
inline void State::Sample(){
    for(unsigned side=0;side<2;++side){gripInputActive[side]=false;triggerInputActive[side]=false;}
    XrSpaceLocation head{XR_TYPE_SPACE_LOCATION};
    if(XR_SUCCEEDED(api.LocateSpace(view,world,displayTime,&head))&&(head.locationFlags&XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)&&Valid(head.pose)){
        tracking.HeadPose.ThePose=Pose(head.pose);tracking.HeadPose.TimeInSeconds=displayTime*1e-9;tracking.StatusFlags=ovrStatus_OrientationTracked;
        if(head.locationFlags&XR_SPACE_LOCATION_POSITION_VALID_BIT)tracking.StatusFlags|=ovrStatus_PositionTracked;
        configured=(head.locationFlags&XR_SPACE_LOCATION_POSITION_VALID_BIT)&&Calibrate(head.pose);
    }
    if(status!=XR_SESSION_STATE_FOCUSED)return;
    XrActiveActionSet active{actions.set,XR_NULL_PATH};XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};sync.countActiveActionSets=1;sync.activeActionSets=&active;
    // XR_SESSION_NOT_FOCUSED is a positive status, not a fatal error. It
    // nevertheless means the runtime did not synchronize actions this frame.
    // Begin cleared the prior hands/buttons; never query stale action values.
    if(api.SyncActions(session,&sync)!=XR_SUCCESS)return;
    input.TimeInSeconds=displayTime*1e-9;
    for(unsigned hand=0;hand<2;++hand){XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};get.action=actions.pose;get.subactionPath=actions.hands[hand];
        XrActionStatePose activePose{XR_TYPE_ACTION_STATE_POSE};XrSpaceLocation grip{XR_TYPE_SPACE_LOCATION},aim{XR_TYPE_SPACE_LOCATION};
        XrSpaceVelocity velocity{XR_TYPE_SPACE_VELOCITY};grip.next=&velocity;
        constexpr XrSpaceLocationFlags gripMask=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
        constexpr XrSpaceLocationFlags aimMask=XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
        if(XR_FAILED(api.GetActionStatePose(session,&get,&activePose))||!activePose.isActive||
           XR_FAILED(api.LocateSpace(actions.spaces[hand],world,displayTime,&grip))||(grip.locationFlags&gripMask)!=gripMask)continue;
        get.action=actions.aimPose;XrActionStatePose activeAim{XR_TYPE_ACTION_STATE_POSE};
        if(XR_FAILED(api.GetActionStatePose(session,&get,&activeAim))||!activeAim.isActive||
           XR_FAILED(api.LocateSpace(actions.aimSpaces[hand],world,displayTime,&aim))||(aim.locationFlags&aimMask)!=aimMask)continue;
        // The game interprets controller -Z as its pointing direction. OpenXR
        // grip orientation follows the handle/fist, which can differ by 90
        // degrees on SteamVR and VDXR. Keep the grip position but take the
        // pointing orientation from the runtime's aim space for each hand.
        OpenXRControllerPose::Pose combined{};
        if(!OpenXRControllerPose::Combine(true,{grip.pose.position.x,grip.pose.position.y,grip.pose.position.z},
            true,{aim.pose.orientation.x,aim.pose.orientation.y,aim.pose.orientation.z,aim.pose.orientation.w},combined))continue;
        const XrPosef pointing{{combined.orientation.x,combined.orientation.y,combined.orientation.z,combined.orientation.w},
            {combined.position.x,combined.position.y,combined.position.z}};
        tracking.HandPoses[hand].ThePose=Pose(pointing);tracking.HandPoses[hand].TimeInSeconds=displayTime*1e-9;
        const auto& v=velocity.linearVelocity;
        const bool velocityKnown=(velocity.velocityFlags&XR_SPACE_VELOCITY_LINEAR_VALID_BIT)&&std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);
        const float unknown=std::numeric_limits<float>::quiet_NaN();
        tracking.HandPoses[hand].LinearVelocity=velocityKnown?ovrVector3f{v.x,v.y,v.z}:ovrVector3f{unknown,unknown,unknown};
        tracking.HandStatusFlags[hand]=ovrStatus_OrientationTracked|ovrStatus_PositionTracked;
        input.ControllerType=static_cast<ovrControllerType>(input.ControllerType|(hand?ovrControllerType_RTouch:ovrControllerType_LTouch));
        bool analogGrip=false,buttonGrip=false;
        input.IndexTrigger[hand]=Float(actions.trigger,hand,&triggerInputActive[hand]);
        const float gripValue=Float(actions.grip,hand,&analogGrip);
        const bool gripPressed=Boolean(actions.gripClick,hand,&buttonGrip);
        gripInputActive[hand]=analogGrip||buttonGrip;
        input.HandTrigger[hand]=std::max(gripValue,gripPressed?1.f:0.f);
        get.action=actions.stick;XrActionStateVector2f stick{XR_TYPE_ACTION_STATE_VECTOR2F};if(XR_SUCCEEDED(api.GetActionStateVector2f(session,&get,&stick))&&stick.isActive)
            input.Thumbstick[hand]={Axis(stick.currentState.x),Axis(stick.currentState.y)};
        if(Boolean(actions.primary,hand))input.Buttons|=hand?ovrButton_A:ovrButton_X;
        if(Boolean(actions.secondary,hand))input.Buttons|=hand?ovrButton_B:ovrButton_Y;
        if(Boolean(actions.click,hand))input.Buttons|=hand?ovrButton_RThumb:ovrButton_LThumb;
        if(!hand&&Boolean(actions.menu,hand))input.Buttons|=ovrButton_Enter;
    }
}
inline bool State::Recenter(){
    if(!running||!displayTime)return false;XrSpaceLocation head{XR_TYPE_SPACE_LOCATION};
    if(!Check(api.LocateSpace(view,base,displayTime,&head),"xrLocateSpace(recenter)")||!Valid(head.pose)||
       (head.locationFlags&(XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_VALID_BIT))!=
           (XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_VALID_BIT))return false;
    const auto& q=head.pose.orientation;const float yaw=std::atan2(2*(q.w*q.y+q.x*q.z),1-2*(q.y*q.y+q.x*q.x));
    XrReferenceSpaceCreateInfo reference{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};reference.referenceSpaceType=worldType;reference.poseInReferenceSpace=Identity();
    reference.poseInReferenceSpace.orientation={0,std::sin(yaw*.5f),0,std::cos(yaw*.5f)};
    reference.poseInReferenceSpace.position={head.pose.position.x,-localFloor,head.pose.position.z};XrSpace replacement=XR_NULL_HANDLE;
    if(!Check(api.CreateReferenceSpace(session,&reference,&replacement),"xrCreateReferenceSpace(recenter)"))return false;
    if(world)api.DestroySpace(world);world=replacement;configured=false;tracking={};input={};return true;
}
inline bool State::Commit(Chain& chain){
    chain.bridgeCurrent=CommitImage(chain,[&](unsigned index){
        return index<chain.targets.size()&&blitter.DrawFull(device,context,chain.targets[index],chain.width,chain.height,chain.view);
    });
    return chain.bridgeCurrent;
}
inline bool State::CanUploadProjection(const Chain& chain,ID3D11Texture2D* source)const{
    if(format!=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB||!mirrors.empty()||!chain.handle)return false;
    if(!source)return true;
    D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);
    return desc.Width==chain.width&&desc.Height==chain.height&&desc.MipLevels==1&&desc.ArraySize==1&&desc.SampleDesc.Count==1&&
        (desc.Format==DXGI_FORMAT_B8G8R8A8_TYPELESS||desc.Format==DXGI_FORMAT_B8G8R8A8_UNORM||desc.Format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB);
}
template<class Queued> inline bool State::CommitProjection(Chain& chain,const unsigned char* pixels,unsigned stride,
    ID3D11Texture2D* source,bool& sourceRead,Queued afterCopy){
    sourceRead=false;
    if(!CanUploadProjection(chain,source)||(!source&&(!pixels||stride<chain.width*4)))return false;
    const bool committed=CommitImage(chain,[&](unsigned index){
        auto* target=chain.images[index].texture;
        if(source){context->CopyResource(target,source);sourceRead=true;}
        else context->UpdateSubresource(target,0,nullptr,pixels,stride,0);
        const bool valid=SUCCEEDED(device->GetDeviceRemovedReason());
        if(valid&&sourceRead)afterCopy();
        return valid;
    });
    if(committed)chain.bridgeCurrent=false;
    return committed;
}
template<class Write> inline bool State::CommitImage(Chain& chain,Write write){
    if(!running)return false;
    if(!chain.handle||!chain.texture)return Failure(-92005,"Invalid OpenXR swapchain commit");
    unsigned index=0;XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    XrResult acquired;
    {HostFrameTiming::Scope timing(HostFrameTiming::AcquireImage);acquired=api.AcquireSwapchainImage(chain.handle,&acquire,&index);}
    if(!Check(acquired,"xrAcquireSwapchainImage"))return false;
    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wait.timeout=XR_INFINITE_DURATION;
    XrResult result;
    {HostFrameTiming::Scope timing(HostFrameTiming::WaitImage);result=api.WaitSwapchainImage(chain.handle,&wait);}
    if(result!=XR_SUCCESS)return Failure(static_cast<int>(result),"xrWaitSwapchainImage");
    const bool drawn=index<chain.images.size()&&chain.images[index].texture&&write(index);
    if(drawn){HostFrameTiming::Scope timing(HostFrameTiming::FlushCopy);context->Flush();}
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};XrResult released;
    {HostFrameTiming::Scope timing(HostFrameTiming::ReleaseImage);released=api.ReleaseSwapchainImage(chain.handle,&release);}
    if(!Check(released,"xrReleaseSwapchainImage"))return false;
    if(!drawn)return Failure(-92002,"D3D11 copy into the acquired OpenXR image failed");
    chain.committed=true;return true;
}
inline XrRect2Di Rectangle(const ovrRecti& value){return {{value.Pos.x,value.Pos.y},{value.Size.w,value.Size.h}};}
inline bool ValidRectangle(const ovrRecti& rectangle,const Chain& chain){return rectangle.Pos.x>=0&&rectangle.Pos.y>=0&&rectangle.Size.w>0&&rectangle.Size.h>0&&
    static_cast<unsigned>(rectangle.Pos.x)<=chain.width&&static_cast<unsigned>(rectangle.Pos.y)<=chain.height&&
    static_cast<unsigned>(rectangle.Size.w)<=chain.width-static_cast<unsigned>(rectangle.Pos.x)&&static_cast<unsigned>(rectangle.Size.h)<=chain.height-static_cast<unsigned>(rectangle.Pos.y);}
inline bool State::Submit(const ovrLayerHeader* const* layers,unsigned count){
    lastLayers=false;if(!frameBegun||!running)return true;
    if(!shouldRender)return End(nullptr,0);
    if(count>16)return Failure(-92005,"Too many application composition layers");
    std::array<XrCompositionLayerProjection,16> projections{};std::array<std::array<XrCompositionLayerProjectionView,2>,16> projectionViews{};
    std::array<XrCompositionLayerQuad,16> quads{};std::array<const XrCompositionLayerBaseHeader*,16> submitted{};unsigned used=0;
    for(unsigned n=0;n<count;++n){if(!layers[n])continue;const auto& header=*layers[n];
        const XrSpace space=(header.Flags&ovrLayerFlag_HeadLocked)?view:world;
        if(header.Type==ovrLayerType_EyeFov){const auto& source=*reinterpret_cast<const ovrLayerEyeFov*>(&header);
            auto& layer=projections[n];layer={XR_TYPE_COMPOSITION_LAYER_PROJECTION};layer.space=space;layer.viewCount=2;layer.views=projectionViews[n].data();
            for(unsigned eye=0;eye<2;++eye){auto* chain=reinterpret_cast<Chain*>(source.ColorTexture[eye]);
                if(!chain||!chain->committed||!ValidRectangle(source.Viewport[eye],*chain))return Failure(-92005,"Invalid projection swapchain or viewport");
                auto& target=projectionViews[n][eye];target={XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};target.pose=Pose(source.RenderPose[eye]);target.fov=Fov(source.Fov[eye]);
                target.subImage.swapchain=chain->handle;target.subImage.imageRect=Rectangle(source.Viewport[eye]);}
            submitted[used++]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer);
        }else if(header.Type==ovrLayerType_Quad){const auto& source=*reinterpret_cast<const ovrLayerQuad*>(&header);auto* chain=reinterpret_cast<Chain*>(source.ColorTexture);
            if(!chain||!chain->committed||!ValidRectangle(source.Viewport,*chain))return Failure(-92005,"Invalid quad swapchain or viewport");
            auto& layer=quads[n];layer={XR_TYPE_COMPOSITION_LAYER_QUAD};layer.space=space;layer.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
            layer.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;layer.pose=Pose(source.QuadPoseCenter);layer.size={source.QuadSize.x,source.QuadSize.y};
            layer.subImage.swapchain=chain->handle;layer.subImage.imageRect=Rectangle(source.Viewport);submitted[used++]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer);
        }else return Failure(-92005,"Unsupported application layer type");
    }
    // Mirror composition samples only our durable bridge textures, before the
    // next xrWaitFrame, and contains the same optional wrist/ammo/help quads.
    if(!mirrors.empty()&&!MirrorLayers(layers,count))Log("OpenXR recording mirror draw failed; headset rendering continues.");
    return End(submitted.data(),used);
}
inline bool State::MirrorLayers(const ovrLayerHeader* const* layers,unsigned count){
    // A failed composition may have cleared or only partially drawn a
    // texture. Its recorder export stays unavailable until a complete draw.
    for(auto* mirror:mirrors)if(mirror)mirror->ready=false;
    for(unsigned n=0;n<count;++n)if(layers[n]&&layers[n]->Type==ovrLayerType_EyeFov){
        const auto& layer=*reinterpret_cast<const ovrLayerEyeFov*>(layers[n]);
        for(auto handle:layer.ColorTexture){
            const auto* chain=reinterpret_cast<const Chain*>(handle);
            if(chain&&!chain->bridgeCurrent)return true; // Wait for the next durable image after a mirror attaches.
        }
    }
    for(auto* mirror:mirrors){if(!mirror||!mirror->texture)continue;if(!mirror->target)return false;
        const float black[4]={0,0,0,1};context->ClearRenderTargetView(mirror->target,black);
        // A symmetric90-degree horizontal camera makes the output a normal
        // window. Aspect enters the vertical angle, never stretches the world.
        XrPosef camera=Pose(tracking.HeadPose.ThePose);float tanH=1.f,tanV=static_cast<float>(mirror->height)/mirror->width;
        bool projection=false;
        for(unsigned n=0;n<count;++n)if(layers[n]&&layers[n]->Type==ovrLayerType_EyeFov){
            const auto& layer=*reinterpret_cast<const ovrLayerEyeFov*>(layers[n]);camera=Pose(layer.RenderPose[mirror->eye]);
            if(layer.Header.Flags&ovrLayerFlag_HeadLocked)camera=Compose(Pose(tracking.HeadPose.ThePose),camera);projection=true;break;}
        if(!projection){
            // Native4:3 menus/cutscenes are a complete screen, not peripheral
            // scene geometry. Fit the primary screen into the recording's
            // aspect without stretching or cutting off its top/bottom rows.
            const auto inverse=Conjugate(camera.orientation);
            for(unsigned n=0;n<count;++n)if(layers[n]&&layers[n]->Type==ovrLayerType_Quad){
                const auto& layer=*reinterpret_cast<const ovrLayerQuad*>(layers[n]);auto pose=Pose(layer.QuadPoseCenter);
                if(layer.Header.Flags&ovrLayerFlag_HeadLocked)pose=Compose(Pose(tracking.HeadPose.ThePose),pose);
                for(float x:{-.5f,.5f})for(float y:{-.5f,.5f}){
                    const auto worldPoint=Compose(pose,{{0,0,0,1},{x*layer.QuadSize.x,y*layer.QuadSize.y,0}}).position;
                    const auto local=Rotate(inverse,{worldPoint.x-camera.position.x,worldPoint.y-camera.position.y,worldPoint.z-camera.position.z});
                    if(local.z<-.01f)tanH=std::max({tanH,std::fabs(local.x)/-local.z,
                        std::fabs(local.y)/-local.z*mirror->width/mirror->height});}
                break;
            }
            if(tanH>1.f)tanH*=1.01f;tanV=tanH*mirror->height/mirror->width;
        }
        for(unsigned n=0;n<count;++n){if(!layers[n])continue;
            if(layers[n]->Type==ovrLayerType_EyeFov){const auto& layer=*reinterpret_cast<const ovrLayerEyeFov*>(layers[n]);
                auto* chain=reinterpret_cast<Chain*>(layer.ColorTexture[mirror->eye]);if(!chain||!chain->view)continue;
                const auto& fov=layer.Fov[mirror->eye];const auto& rect=layer.Viewport[mirror->eye];
                const float u0=rect.Pos.x/static_cast<float>(chain->width),v0=rect.Pos.y/static_cast<float>(chain->height),
                    us=rect.Size.w/static_cast<float>(chain->width),vs=rect.Size.h/static_cast<float>(chain->height);
                const auto vertices=Blitter::Full(u0+us*(fov.LeftTan-tanH)/(fov.LeftTan+fov.RightTan),
                    v0+vs*(fov.UpTan-tanV)/(fov.UpTan+fov.DownTan),u0+us*(fov.LeftTan+tanH)/(fov.LeftTan+fov.RightTan),
                    v0+vs*(fov.UpTan+tanV)/(fov.UpTan+fov.DownTan));
                if(!blitter.Draw(device,context,mirror->target,mirror->width,mirror->height,chain->view,*reinterpret_cast<const Blitter::Vertex(*)[6]>(vertices.data()),false))return false;
            }else if(layers[n]->Type==ovrLayerType_Quad){const auto& layer=*reinterpret_cast<const ovrLayerQuad*>(layers[n]);auto* chain=reinterpret_cast<Chain*>(layer.ColorTexture);
                if(!chain||!chain->view)continue;XrPosef quad=Pose(layer.QuadPoseCenter);
                if(layer.Header.Flags&ovrLayerFlag_HeadLocked)quad=Compose(Pose(tracking.HeadPose.ThePose),quad);
                const auto inverse=Conjugate(camera.orientation);std::array<Blitter::Vertex,4> corner{};
                const float xs[]={-.5f,.5f,.5f,-.5f},ys[]={.5f,.5f,-.5f,-.5f};
                const float u0=layer.Viewport.Pos.x/static_cast<float>(chain->width),v0=layer.Viewport.Pos.y/static_cast<float>(chain->height),
                    u1=(layer.Viewport.Pos.x+layer.Viewport.Size.w)/static_cast<float>(chain->width),v1=(layer.Viewport.Pos.y+layer.Viewport.Size.h)/static_cast<float>(chain->height);
                for(unsigned i=0;i<4;++i){const auto p=Compose(quad,{{0,0,0,1},{xs[i]*layer.QuadSize.x,ys[i]*layer.QuadSize.y,0}}).position;
                    const auto local=Rotate(inverse,{p.x-camera.position.x,p.y-camera.position.y,p.z-camera.position.z});
                    corner[i]={local.x/tanH,local.y/tanV,-local.z*.5f,-local.z,i==1||i==2?u1:u0,i>=2?v1:v0};}
                const Blitter::Vertex vertices[6]={corner[0],corner[1],corner[2],corner[0],corner[2],corner[3]};
                if(!blitter.Draw(device,context,mirror->target,mirror->width,mirror->height,chain->view,vertices,true))return false;
            }
        }
        mirror->ready=true;
    }
    return true;
}
inline void Destroy(Chain* chain){if(!chain)return;for(auto*& target:chain->targets)Release(target);
    if(chain->handle&&state.api.DestroySwapchain)state.api.DestroySwapchain(chain->handle);
    Release(chain->view);Release(chain->texture);delete chain;}
inline void State::Close(){
    if(frameBegun&&api.EndFrame)End(nullptr,0);
    for(auto* mirror:mirrors){Release(mirror->target);Release(mirror->texture);delete mirror;}mirrors.clear();
    for(auto* chain:chains)Destroy(chain);chains.clear();blitter.Close();
    for(auto& space:actions.spaces){if(space&&api.DestroySpace)api.DestroySpace(space);space=XR_NULL_HANDLE;}
    for(auto& space:actions.aimSpaces){if(space&&api.DestroySpace)api.DestroySpace(space);space=XR_NULL_HANDLE;}
    if(world&&api.DestroySpace)api.DestroySpace(world);if(view&&api.DestroySpace)api.DestroySpace(view);if(base&&api.DestroySpace)api.DestroySpace(base);
    world=view=base=XR_NULL_HANDLE;
    if(actions.set&&api.DestroyActionSet)api.DestroyActionSet(actions.set);actions={};
    if(session&&api.DestroySession)api.DestroySession(session);session=XR_NULL_HANDLE;
    Release(context);Release(device);if(instance&&api.DestroyInstance)api.DestroyInstance(instance);instance=XR_NULL_HANDLE;api.Close();
    running=frameBegun=shouldRender=quit=configured=everConfigured=lastLayers=false;displayTime=earliestCalibration=0;status=XR_SESSION_STATE_UNKNOWN;
    format=0;error=0;audioGuid.clear();tracking={};input={};
}

// ABI-compatible callbacks used by the common host Renderer.
inline ovrResult OVR_CDECL Initialize(const ovrInitParams*){state.Close();return state.Initialize()?ovrSuccess:state.error;}
inline void OVR_CDECL Shutdown(){state.Close();}
inline ovrResult OVR_CDECL Create(ovrSession* session,ovrGraphicsLuid* luid){if(!state.instance||!session||!luid)return ovrError_InvalidParameter;
    *session=reinterpret_cast<ovrSession>(&state);std::memcpy(luid->Reserved,&state.requirements.adapterLuid,sizeof(luid->Reserved));return ovrSuccess;}
inline void OVR_CDECL DestroySession(ovrSession){}
inline ovrResult OVR_CDECL SetOrigin(ovrSession,ovrTrackingOrigin){return ovrSuccess;}
inline ovrHmdDesc OVR_CDECL Hmd(ovrSession){return state.hmd;}
inline ovrEyeRenderDesc OVR_CDECL Eye(ovrSession,ovrEyeType eye,ovrFovPort){return state.eyes[eye==ovrEye_Right?1:0];}
inline ovrSizei OVR_CDECL Size(ovrSession,ovrEyeType eye,ovrFovPort,float){const auto& d=state.dimensions[eye==ovrEye_Right?1:0];return {static_cast<int>(d.recommendedImageRectWidth),static_cast<int>(d.recommendedImageRectHeight)};}
inline ovrTrackingState OVR_CDECL Tracking(ovrSession,double,ovrBool){return state.tracking;}
inline double OVR_CDECL Time(){if(state.displayTime)return state.displayTime*1e-9;LARGE_INTEGER n{},f{};QueryPerformanceCounter(&n);QueryPerformanceFrequency(&f);return static_cast<double>(n.QuadPart)/f.QuadPart;}
inline double OVR_CDECL DisplayTime(ovrSession,long long){return Time();}
inline ovrResult OVR_CDECL Status(ovrSession,ovrSessionStatus* result){if(!result)return ovrError_InvalidParameter;*result={};if(!state.Begin())return state.error;
    result->HmdPresent=state.running&&!state.quit;result->IsVisible=state.status==XR_SESSION_STATE_VISIBLE||state.status==XR_SESSION_STATE_FOCUSED;
    result->HmdMounted=result->IsVisible&&state.configured;result->ShouldQuit=state.quit;return ovrSuccess;}
inline void OVR_CDECL ErrorInfo(ovrErrorInfo* result){if(result){*result={};result->Result=state.error;std::snprintf(result->ErrorString,sizeof(result->ErrorString),"%s",state.message);}}
inline ovrResult OVR_CDECL Recenter(ovrSession){
    if(state.Recenter())return ovrSuccess;
    if(!state.error)std::snprintf(state.message,sizeof(state.message),"Recenter requires a current tracked headset position and orientation; restore tracking and retry.");
    return state.error?state.error:ovrError_InvalidOperation;}
inline ovrResult OVR_CDECL Input(ovrSession,ovrControllerType requested,ovrInputState* output){if(!output)return ovrError_InvalidParameter;*output={};
    if(state.status!=XR_SESSION_STATE_FOCUSED)return ovrSuccess;*output=state.input;output->ControllerType=static_cast<ovrControllerType>(output->ControllerType&requested);return ovrSuccess;}
inline ovrResult OVR_CDECL CreateChain(ovrSession,IUnknown*,const ovrTextureSwapChainDesc* desc,ovrTextureSwapChain* result){
    if(!desc||!result||!state.device||desc->Width<1||desc->Height<1||desc->Width>8192||desc->Height>8192)return ovrError_InvalidParameter;
    auto* chain=new Chain;chain->width=static_cast<unsigned>(desc->Width);chain->height=static_cast<unsigned>(desc->Height);
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};info.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
    info.format=state.format;info.sampleCount=info.faceCount=info.arraySize=info.mipCount=1;info.width=chain->width;info.height=chain->height;
    if(!state.Check(state.api.CreateSwapchain(state.session,&info,&chain->handle),"xrCreateSwapchain")){Destroy(chain);return state.error;}
    unsigned count=0;if(!state.Check(state.api.EnumerateSwapchainImages(chain->handle,0,&count,nullptr),"xrEnumerateSwapchainImages")||!count||count>32){Destroy(chain);return ovrError_InvalidParameter;}
    chain->images.resize(count,{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
    if(!state.Check(state.api.EnumerateSwapchainImages(chain->handle,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(chain->images.data())),"xrEnumerateSwapchainImages")){Destroy(chain);return state.error;}
    chain->targets.resize(chain->images.size(),nullptr);
    D3D11_RENDER_TARGET_VIEW_DESC target{};target.Format=static_cast<DXGI_FORMAT>(state.format);target.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
    for(unsigned index=0;index<chain->images.size();++index)if(!chain->images[index].texture||
       FAILED(state.device->CreateRenderTargetView(chain->images[index].texture,&target,&chain->targets[index]))){Destroy(chain);return ovrError_MemoryAllocationFailure;}
    D3D11_TEXTURE2D_DESC bridge{};bridge.Width=chain->width;bridge.Height=chain->height;bridge.MipLevels=bridge.ArraySize=1;bridge.Format=DXGI_FORMAT_B8G8R8A8_TYPELESS;
    // This host-owned image is both the GPU upload target and the durable
    // sRGB source for XR commits/held layers. Allow the upload's UNORM RTV so
    // it can write here directly instead of allocating and copying an intermediate.
    bridge.SampleDesc.Count=1;bridge.Usage=D3D11_USAGE_DEFAULT;bridge.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
    D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.Format=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;view.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;view.Texture2D.MipLevels=1;
    HRESULT hr=state.device->CreateTexture2D(&bridge,nullptr,&chain->texture);if(SUCCEEDED(hr))hr=state.device->CreateShaderResourceView(chain->texture,&view,&chain->view);
    if(FAILED(hr)){Destroy(chain);return ovrError_MemoryAllocationFailure;}
    state.chains.push_back(chain);*result=reinterpret_cast<ovrTextureSwapChain>(chain);return ovrSuccess;
}
inline ovrResult OVR_CDECL ChainLength(ovrSession,ovrTextureSwapChain chain,int* length){if(!chain||!length)return ovrError_InvalidParameter;*length=1;return ovrSuccess;}
inline ovrResult OVR_CDECL ChainIndex(ovrSession,ovrTextureSwapChain chain,int* index){if(!chain||!index)return ovrError_InvalidParameter;*index=0;return ovrSuccess;}
inline ovrResult OVR_CDECL ChainBuffer(ovrSession,ovrTextureSwapChain value,int index,IID iid,void** output){auto* chain=reinterpret_cast<Chain*>(value);
    if(!chain||index||!output)return ovrError_InvalidParameter;return SUCCEEDED(chain->texture->QueryInterface(iid,output))?ovrSuccess:ovrError_InvalidParameter;}
inline ovrResult OVR_CDECL CommitChain(ovrSession,ovrTextureSwapChain value){auto* chain=reinterpret_cast<Chain*>(value);return chain&&state.Commit(*chain)?ovrSuccess:state.error?state.error:ovrError_InvalidParameter;}
inline void OVR_CDECL DestroyChain(ovrSession,ovrTextureSwapChain value){auto* chain=reinterpret_cast<Chain*>(value);state.chains.erase(std::remove(state.chains.begin(),state.chains.end(),chain),state.chains.end());Destroy(chain);}
inline ovrResult OVR_CDECL Submit(ovrSession,long long,const ovrViewScaleDesc*,const ovrLayerHeader* const* layers,unsigned count){return state.Submit(layers,count)?ovrSuccess:state.error;}
inline bool AttachDevice(ID3D11Device* device){return state.Attach(device);}
inline bool Configured(){return state.configured;}
inline bool LastSubmissionHadLayers(){return state.lastLayers;}
inline bool FinishIdle(){return state.End(nullptr,0);}

struct MirrorDesc {ovrTextureFormat Format;int Width,Height;unsigned MiscFlags,MirrorOptions;};
inline ovrResult OVR_CDECL CreateMirror(ovrSession,IUnknown*,const MirrorDesc* desc,ovrMirrorTexture* output){
    if(!desc||!output||!state.device||desc->Width<1||desc->Width>2048||desc->Height<1||desc->Height>1536)return ovrError_InvalidParameter;
    auto* mirror=new Mirror;mirror->width=static_cast<unsigned>(desc->Width);mirror->height=static_cast<unsigned>(desc->Height);mirror->eye=(desc->MirrorOptions&4)?1:0;
    D3D11_TEXTURE2D_DESC texture{};texture.Width=mirror->width;texture.Height=mirror->height;texture.MipLevels=texture.ArraySize=1;texture.Format=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    texture.SampleDesc.Count=1;texture.Usage=D3D11_USAGE_DEFAULT;texture.BindFlags=D3D11_BIND_RENDER_TARGET;
    if(FAILED(state.device->CreateTexture2D(&texture,nullptr,&mirror->texture))){delete mirror;return ovrError_MemoryAllocationFailure;}
    if(FAILED(state.device->CreateRenderTargetView(mirror->texture,nullptr,&mirror->target))){Release(mirror->texture);delete mirror;return ovrError_MemoryAllocationFailure;}
    const float black[4]={0,0,0,1};state.context->ClearRenderTargetView(mirror->target,black);
    state.mirrors.push_back(mirror);*output=reinterpret_cast<ovrMirrorTexture>(mirror);return ovrSuccess;
}
inline ovrResult OVR_CDECL MirrorBuffer(ovrSession,ovrMirrorTexture value,IID iid,void** output){auto* mirror=reinterpret_cast<Mirror*>(value);
    if(output)*output=nullptr;
    if(!mirror||!output||!mirror->texture)return ovrError_InvalidParameter;
    if(!mirror->ready)return ovrError_InvalidOperation;
    return SUCCEEDED(mirror->texture->QueryInterface(iid,output))?ovrSuccess:ovrError_InvalidParameter;}
inline void OVR_CDECL DestroyMirror(ovrSession,ovrMirrorTexture value){auto* mirror=reinterpret_cast<Mirror*>(value);state.mirrors.erase(std::remove(state.mirrors.begin(),state.mirrors.end(),mirror),state.mirrors.end());
    if(mirror){Release(mirror->target);Release(mirror->texture);delete mirror;}}
inline void* MirrorExport(HMODULE,const char* name){
    if(!std::strcmp(name,"ovr_CreateMirrorTextureWithOptionsDX"))return reinterpret_cast<void*>(&CreateMirror);
    if(!std::strcmp(name,"ovr_GetMirrorTextureBufferDX"))return reinterpret_cast<void*>(&MirrorBuffer);
    if(!std::strcmp(name,"ovr_DestroyMirrorTexture"))return reinterpret_cast<void*>(&DestroyMirror);return nullptr;
}
inline std::string JsonString(const std::wstring& text){std::string out="\"";for(wchar_t c:text){if(c=='"'||c=='\\'){out+='\\';out+=static_cast<char>(c);}
    else if(c>=32&&c<127)out+=static_cast<char>(c);else{char escaped[8];std::snprintf(escaped,sizeof(escaped),"\\u%04x",static_cast<unsigned>(c));out+=escaped;}}return out+'"';}
inline std::wstring Wide(const char* utf8){if(!utf8||!*utf8)return {};const int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,utf8,-1,nullptr,0);
    if(length<=0)return L"Invalid runtime text";std::wstring text(static_cast<std::size_t>(length),0);
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,utf8,-1,text.data(),length);text.pop_back();return text;}
inline int Probe(const std::wstring& report){
    const bool initialized=state.Initialize();bool okay=initialized;
    if(okay){IDXGIFactory1* factory=nullptr;IDXGIAdapter1* selected=nullptr;ID3D11Device* device=nullptr;ID3D11DeviceContext* context=nullptr;
        okay=SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1),reinterpret_cast<void**>(&factory)));
        if(okay)for(unsigned n=0;;++n){IDXGIAdapter1* adapter=nullptr;if(factory->EnumAdapters1(n,&adapter)==DXGI_ERROR_NOT_FOUND)break;if(!adapter)break;
            DXGI_ADAPTER_DESC1 desc{};if(SUCCEEDED(adapter->GetDesc1(&desc))&&!std::memcmp(&desc.AdapterLuid,&state.requirements.adapterLuid,sizeof(LUID))){selected=adapter;break;}Release(adapter);}
        Release(factory);okay=selected!=nullptr;
        if(okay){const D3D_FEATURE_LEVEL choices[]={D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_1,D3D_FEATURE_LEVEL_10_0};std::vector<D3D_FEATURE_LEVEL> levels;
            for(auto level:choices)if(level>=state.requirements.minFeatureLevel)levels.push_back(level);D3D_FEATURE_LEVEL level{};
            okay=!levels.empty()&&SUCCEEDED(D3D11CreateDevice(selected,D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,levels.data(),static_cast<unsigned>(levels.size()),D3D11_SDK_VERSION,&device,&level,&context));}
        Release(selected);Release(context);Release(device);if(!okay)state.Failure(-92002,"The OpenXR HMD D3D11 adapter is unavailable");}
    const auto runtime=Wide(state.runtimeName);
    const std::string text=std::string("{\"ready\":")+(okay?"true":"false")+",\"runtimeName\":"+JsonString(runtime)+",\"audioGuid\":"+JsonString(state.audioGuid)+
        ",\"error\":"+std::to_string(okay?0:state.error)+",\"message\":"+JsonString(Wide(state.message))+"}\n";
    if(!report.empty()){
        if(report.size()>32700||report.find_first_of(L"\r\n\"")!=std::wstring::npos||!((report.size()>2&&report[1]==L':'&&(report[2]==L'\\'||report[2]==L'/'))||report.substr(0,2)==L"\\\\")){state.Close();return 2;}
        HANDLE file=CreateFileW(report.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);DWORD written=0;
        if(file==INVALID_HANDLE_VALUE||!WriteFile(file,text.data(),static_cast<DWORD>(text.size()),&written,nullptr)||written!=text.size()){if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);state.Close();return 2;}CloseHandle(file);}
    Log(okay?"OpenXR runtime/HMD/D3D11 probe passed; no session created.":state.message);state.Close();return okay?0:3;
}
} // namespace KL1OpenXR

#ifdef __clang__
#pragma clang diagnostic pop
#endif
