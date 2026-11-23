#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <atomic>
#include <cstdint>
// Sampled mod wall timings; nested scopes overlap. No per-draw I/O or pixels.
namespace ProfileTiming {
enum Region {GPUBridge,RigPrepare,WeaponMesh,CameraParameter,PostprocessDraw,HudDraw,HudOverlay,ShadowPalette,DualControl,AimPreview,
    CameraVisibility,HandsBind,HandsModel,HandsSnapshot,HudShadowRegister,HudShadowUnlock,ShaderObserve,CinematicRead,RenderGuard,HeadAnchor,WeaponOwnership,HudRestore,
    NativeWorldBind,NativePaletteBind,NativeShaderUpload,NativeDraw,EffectsOwn,NativeVertexLock,NativeIndexLock,NativeBufferUnlock,On12QueuePacing,SkinMaterialEvidenceOwn,
    LaserPose,AimOverlay,ReticleProjection,WristProjection,ReloadOverlay,Count};
inline const char* Names[Count]={"gpuBridge","rigPrepare","weaponMeshOwn","cameraParameterOwn","postprocessDrawInclusive","hudDrawOwn","handHudOverlay","trackedShadowPalette","dualControlInclusive","aimPreview",
    "cameraVisibilityOwn","handsBindOwn","handsModelOwn","handsSnapshot","hudShadowRegister","hudShadowUnlock","shaderObserve","cinematicRead","renderViewGuard","headAnchor","weaponOwnershipTraversal","hudRestore",
    "nativeWorldBindInclusive","nativePaletteBindInclusive","nativeShaderUploadInclusive","nativeDrawInclusive","effectsDrawOwn",
    "nativeVertexLockInclusive","nativeIndexLockInclusive","nativeBufferUnlockInclusive","on12QueuePacingOwn","skinMaterialEvidenceOwn",
    "laserPoseOwn","aimOverlayInclusive","reticleProjectionInclusive","wristProjectionInclusive","reloadOverlayInclusive"};
struct Counter {std::atomic<std::uint64_t> ticks{0},samples{0};};
inline Counter counters[Count];inline std::atomic<bool> enabled{false};
inline unsigned Stride(Region region){return region==GPUBridge||region==HudOverlay||region==ShadowPalette||region==DualControl||region==AimPreview||
    region==NativeVertexLock||region==NativeIndexLock||region==NativeBufferUnlock||region==On12QueuePacing||region==SkinMaterialEvidenceOwn||
    region>=LaserPose?1:61;}
struct Scope {
    Region region;LARGE_INTEGER start{};
    explicit Scope(Region r,bool active=true):region(r){
        if(!active||!enabled.load(std::memory_order_relaxed))return;
        thread_local unsigned sequence[Count]{};
        if(sequence[r]++%Stride(r))return;QueryPerformanceCounter(&start);
    }
    void Stop(){if(!start.QuadPart)return;LARGE_INTEGER end;QueryPerformanceCounter(&end);
        counters[region].ticks.fetch_add(static_cast<std::uint64_t>(end.QuadPart-start.QuadPart),std::memory_order_relaxed);
        counters[region].samples.fetch_add(1,std::memory_order_relaxed);start.QuadPart=0;}
    ~Scope(){Stop();}
};
inline double EstimatedMs(Region region){LARGE_INTEGER frequency;QueryPerformanceFrequency(&frequency);
    return double(counters[region].ticks.load(std::memory_order_relaxed))*Stride(region)*1000.0/double(frequency.QuadPart);}
}
