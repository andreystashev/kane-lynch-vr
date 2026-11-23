#pragma once
#include <d3d9.h>
#include <cstdint>
#include "native_camera_kl.hpp"
namespace D3D9Capture {enum class TargetChange;struct DrawCall;}
// The retail Mirror shader adds a tangent-space normal directly to screen UV.
// In VR those two screen axes rotate with the headset. Only its exact captured
// native programs on the current authorized main-eye projection are scoped.
namespace NativeMirrorBumpKL {
using ReadView=bool(*)(NativeCameraKL::EffectsView&);
using ReadReflection=bool(*)(NativeCameraKL::ReflectionTargetView&);
using QualifyReflector=void(*)(std::uintptr_t,bool);
struct MatrixEvidence {
    bool valid=false,read=false,mainMatch=false,viewportRead=false;
    unsigned tick=0,bump=0,eyeIndex=0,viewport[6]{};
    std::uintptr_t state=0;std::uint64_t frame=0;
    float modelAdjustment[2]{1.f,0.f};
    float eye[12]{},world[12]{},projection[16]{},actual[32]{},expected[32]{},bounds[32]{};
};
struct Stats {
    std::uint64_t seen=0,recognized=0,applied=0,restored=0;
    std::uint64_t viewRejected=0,matrixRejected=0,zeroPreserved=0,failed=0,restoreFailed=0;
    std::uint64_t viewportApplied=0;
    MatrixEvidence lastRejected;
    std::uint64_t targetCaptured=0,targetRejected=0,targetEligible=0,targetApplied=0,targetRestored=0;
    std::uint64_t cachedTargetApplied=0,targetInvalidated=0;
    std::uint64_t cachedOtherEyeApplied=0;
    std::uint64_t invalidationReasons[6]{};
    std::uint64_t historyRetained=0,historySceneRetires=0,historyForeignWrites=0,historyIneligible=0;
    std::uint64_t tokyoFloorEye[2]{},tokyoFloorUntracked=0,tokyoFloorRestored=0;
    std::uint64_t tokyoFloorLive=0,tokyoFloorHistory=0,tokyoFloorFallback=0,tokyoFloorCopies=0,tokyoFloorCopyFailed=0;
    std::uint64_t retomotoFloorLive=0,retomotoFloorHistory=0,retomotoFloorFallback=0,retomotoFloorCopies=0;
    std::uint64_t towerCubeMatched=0,towerCubeDraws=0;
};
void Configure(ReadView reader,ReadReflection reflection=nullptr,unsigned tokyoFloorMode=0,
               QualifyReflector qualifyReflector=nullptr);
void ConfigureTargetHistory(bool (*covered)(IDirect3DDevice9*),std::uint64_t (*activeSerial)()=nullptr);
void ObserveTargetChange(IDirect3DDevice9*,D3D9Capture::TargetChange,void* destination,unsigned slot);
void* BeginDraw(IDirect3DDevice9* device,const D3D9Capture::DrawCall* call=nullptr);
HRESULT FinishDraw(void* token,HRESULT result);
// Call on the GPU consumer at every physical image boundary, before loading.
void BeforeCapture(IDirect3DDevice9* device);
void WarmTowerEnvironment(const NativeCameraMath::Vec3& position);
void RetireFrameTargets(bool keepSceneHistory=false,bool keepTokyoFloor=false,bool bridgeRetomoto=false);
void InvalidateDevice();
Stats Snapshot();
}
