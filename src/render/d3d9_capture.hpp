#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <cstdint>
#include "frame_pacing.hpp"
#include "frame_event_trace.hpp"
#include "on12_queue_policy.hpp"

// D3D9 capture + stereo steering hooks for Kane & Lynch: Dead Men (Glacier,
// Direct3D 9). The capture path follows the proven Blood Money module
// (swapchain/device Present, Reset, GetRenderTargetData readback with a
// StretchRect fallback) and the transform hooks follow the proven Contracts
// module (SetTransform + SetVertexShaderConstantF steering).
namespace D3D9Capture {
using FrameCallback=bool(*)(const std::uint8_t* bgra,bool forceOpaque,std::uint32_t width,
    std::uint32_t height,std::uint32_t stride,std::uint64_t frameId,std::uint32_t tick,void* user);
using FrameBoundaryCallback=void(*)(void* user);
// Called on the render thread before readback, with the actual backbuffer.
using BeforeCaptureCallback=void(*)(IDirect3DDevice9*,IDirect3DSurface9*,void* user);
// kind: 0=D3DTS_VIEW, 1=D3DTS_PROJECTION, 2=D3DTS_WORLD. Return true and the
// (possibly modified) matrix is forwarded to the device.
using TransformCallback=bool(*)(int kind,float matrix[16],void* user);
// Rows of a SetVertexShaderConstantF call (registerCount 4-float vectors).
// Return true and the (possibly modified) registers are forwarded.
using ConstantCallback=bool(*)(IDirect3DDevice9*,unsigned startRegister,float* rows,unsigned registerCount,void* user);
// Optional read-only dispatch. False means observation is complete and no
// writable copy/mutator is needed; true preserves the original mutable path.
using ConstantInspector=bool(*)(IDirect3DDevice9*,unsigned,const float*,unsigned,void*);
using BeforeResetCallback=void(*)(IDirect3DDevice9*);
// -1 requests CPU compatibility capture, 0 drops a busy GPU frame, 1 publishes.
using GPUCallback=int(*)(IDirect3DDevice9*,IDirect3DSurface9*,unsigned,unsigned,std::uint64_t,std::uint32_t,void*);
void SetGPUCallback(GPUCallback callback);
void SetRenderProgressCallback(void(*callback)(IDirect3DDevice9*));
// Optional per-draw adapter. A non-null token is finished after the original
// draw; the adapter owns any temporary state and restores it on that path.
struct DrawCall;
using DrawBeginCallback=void*(*)(IDirect3DDevice9*,const DrawCall&);
using DrawEndCallback=HRESULT(*)(void*,HRESULT);
void SetDrawCallbacks(DrawBeginCallback begin,DrawEndCallback end);
enum class TargetChange {Bind,Clear,SurfaceWrite,TextureWrite};
using TargetObserver=void(*)(IDirect3DDevice9*,TargetChange,void* destination,unsigned slot);
void SetTargetObserver(TargetObserver observer);
bool TargetWritesCovered(IDirect3DDevice9* device);
// Passive diagnostics see the exact state after adapters, before restoration.
// UP paths are covered too; user pointers are evidence only, never retained.
struct DrawCall {
    unsigned kind=0,type=0; // 0 primitive,1 indexed,2 UP,3 indexed UP
    int baseVertex=0;
    unsigned minVertex=0,vertices=0,first=0,count=0,stride=0,indexFormat=0;
    std::uintptr_t vertexData=0,indexData=0;
};
using DrawTraceCallback=void(*)(IDirect3DDevice9*,const DrawCall&,bool after,HRESULT result);
void SetDrawTraceCallback(DrawTraceCallback callback);
using DrawCopyCallback=void(*)(IDirect3DDevice9*,const DrawCall&);
void SetDrawCopyCallback(DrawCopyCallback callback);
HRESULT ReplayNativeDraw(IDirect3DDevice9* device,const DrawCall& call);
// Narrow owned-object suppression. True skips only this native world draw;
// overlays bypass it. The filter must leave all device state unchanged.
using DrawFilter=bool(*)(IDirect3DDevice9*,bool indexed);
void SetDrawFilter(DrawFilter filter);
// Before Install: request IMMEDIATE on native HAL creation/reset, with retry
// using the game's original parameters if the override is rejected.
void SetForceImmediate(bool enabled);
// On12 scene/scope only: bound CPU render-ahead using two GPU EVENT queries.
// No frame-rate limit, display/graphics mutation or unbounded synchronization.
void ConfigureOn12QueuePacing(bool enabled);
enum class CPUStage:unsigned {Resolve,Readback,Map,Convert,Publish,Unmap,LockableMap,Count};
constexpr unsigned CPUStageCount=static_cast<unsigned>(CPUStage::Count);
inline const char* CPUStageNames[CPUStageCount]={"resolve","readback","map","convert","publish","unmap","lockableMap"};
struct CPUStageCost {std::uint64_t calls=0,totalUs=0,maxUs=0;};
struct Stats {
    bool installed,enabled,presentHook,resetHook,transformHook,constantHook;
    // EndScene is a discovery bootstrap until Poll binds the real Present.
    bool endSceneTrigger;
    std::int32_t halCreateResult,nullrefCreateResult,swapchainResult,promotionResult;
    std::uint64_t presents,swapPresents,devicePresents,resets,captured,callbackDrops,failures;
    std::uint64_t endScenes;
    unsigned presentProbeSamples,presentProbeSwapChanges,presentProbeDeviceChanges;
    std::int32_t presentProbeResult;
    std::uint32_t width,height,formatCode;
    // Actual main backbuffer before transport downscaling; native HUD viewport gate.
    std::uint32_t backbufferWidth,backbufferHeight;
    std::uint32_t renderWidth=0,renderHeight=0,multisampleType=0,multisampleQuality=0;
    double readbackMeanMs=0;
    std::uint64_t readbackCalls=0;
    // Disjoint synchronous CPU capture stages. Map can wait for an earlier
    // asynchronous driver readback; publish includes the existing IPC copy.
    CPUStageCost cpuStages[CPUStageCount]{};
    std::uint32_t presentationInterval,backbufferCount;
    bool presentationKnown,windowed;
    bool forceImmediate,createDeviceHook;
    std::uint64_t immediateApplied,immediateFallbacks;
    std::int32_t lastCaptureResult,lastPresentResult,installResult;
    std::uint64_t resetCalls,resetFailures;
    std::int32_t lastResetResult;
    std::uint32_t resetWidth,resetHeight;
    bool resetPending;
    FramePacing::Summary pacing;
    On12QueuePolicy::Stats queuePacing;
};
bool Install(FrameCallback onFrame,void* user,FrameBoundaryCallback onBoundary,
             TransformCallback onTransform,void* onTransformUser,
             ConstantCallback onConstants,void* onConstantUser,
             BeforeCaptureCallback onBeforeCapture=nullptr,void* onBeforeCaptureUser=nullptr,
             BeforeResetCallback beforeReset=nullptr,ConstantInspector inspectConstants=nullptr);
void SetEnabled(bool value);
// Exact native swapchain caller only. Called after the eye is captured.
using SkipSwapPresent=bool(*)(std::uintptr_t returnAddress);
void SetSkipSwapPresent(SkipSwapPresent callback);
void ConfigureFrameTrace(bool enabled);
// First native shader of each Present serial; overlay uploads are excluded.
void ObserveFrameShader();
// Exact native camera-packet consumer only, before its original setter.
// Complements the first-shader marker without adding a wait or GPU query.
void ObserveNativeCamera();
void SetFrameTraceSource(std::uint64_t frameId,unsigned eye,unsigned kind,unsigned gate);
std::size_t ReadFrameTrace(std::uint64_t after,FrameEventTrace::Event* output,std::size_t capacity,std::uint64_t& overwritten);
// Worker thread only: promote NULLREF bootstrap to the game's real Present.
void Poll();
// Render/HUD callers need counters only. Sorting the diagnostic distributions
// is explicit and reserved for the once-per-second status worker.
Stats Snapshot(bool includePacing=false);
}
