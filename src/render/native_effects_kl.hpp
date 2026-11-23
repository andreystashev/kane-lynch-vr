#pragma once
#include <d3d9.h>
#include <cstdint>
#include <cstddef>
#include <array>
#include "native_camera_kl.hpp"
#include "native_effects_visual_source_kl.hpp"
namespace NativeEffectsKL {
using ReadView=bool(*)(NativeCameraKL::EffectsView&);
using LogCallback=void(*)(const char*);
struct ParticleNormalEvidence {
    bool valid=false;
    std::uint64_t sequence=0,drawScopeId=0;
    std::uint32_t tick=0,mode=~0u,reason=0,eye=0;
    std::uintptr_t sourceState=0,sourceEntry=0,sourceGeometry=0,normalData=0;
    float authored[3]{},tracked[3]{},reference[3]{},eyeBasis[9]{},referenceBasis[9]{};
};
struct Stats {
    bool installed=false;
    bool geometryInstalled=false;
    std::uint64_t shaderSeen=0,shaderRecognized=0,shaderDraws=0,shaderRestored=0,shaderFailed=0;
    std::uint64_t viewRejected=0,spriteWrites=0,spriteQuads=0,spriteRejected=0;
    std::uint64_t restoreFailed=0,projectionRejected=0,viewportProbeSamples=0;
    unsigned lastViewportWidth=0,lastViewportHeight=0,lastTargetWidth=0,lastTargetHeight=0;
    unsigned maxViewportWidth=0,maxViewportHeight=0,maxTargetWidth=0,maxTargetHeight=0;
    unsigned lastProbeFamily=0,lastTargetFormat=0,lastColourWriteMask=0;
    // 1..5 corrected shader families; 6/7 CPU ManualScatter Ambient/Z,
    // 8 ordinary CPU Sprite/particle, 9 unchanged SpriteBox Rain. Slot0 unknown.
    std::array<std::uint64_t,10> familySeen{},familyCalls{},familyDraws{},familyViewRejected{},familyProjectionRejected{};
    std::uint64_t manualWrites=0,manualQuads=0,manualRejected=0,particleWrites=0,particleQuadsObserved=0;
    std::uint64_t particleQuads=0,particleRejected=0,particleAlignedPreserved=0,particlePrefixRejected=0,sourceStateRejected=0;
    std::uint64_t radialFadeDraws=0,radialFogDraws=0,fogSourceRejected=0;
    std::uint64_t modelViewSamples=0,modelViewMatches=0,modelViewTransposeMatches=0,modelViewOther=0;
    float modelViewMaxError=0;
    std::array<float,16> lastActualModelView{},lastExpectedModelView{};
    std::uint64_t billboardCalls=0,billboardEntered=0,billboardBound=0,billboardRejected=0,billboardUnexpected=0;
    std::uint64_t reflectedBillboardBound=0;
    std::uint64_t reflectedScatterDraws=0;
    unsigned lastBillboardMode=0;
    std::uint64_t cinematicViewportExpanded=0,cinematicViewportRestoreFailed=0;
    std::uint64_t cinematicViewportAttempts=0;
    unsigned cinematicViewportStage=0;
    std::array<unsigned,4> cinematicLastViewport{};
    std::uintptr_t lastBillboardDescriptor=0;
    std::uint64_t worldSprite5Writes=0,worldSprite6Writes=0,worldSpriteQuads=0,worldSpriteRejected=0;
    bool particleNormalInstalled=false;
    std::uint64_t particleNormalCalls=0,particleNormalApplied=0,particleNormalAlignedPreserved=0,particleNormalRejected=0;
    ParticleNormalEvidence lastParticleNormalApplied,lastParticleNormalRejected;
    bool particleDeferredInstalled=false;
    std::uint64_t particleDeferredCalls=0,particleDeferredQuads=0,particleDeferredAlignedPreserved=0,particleDeferredRejected=0;
};
bool Install(LogCallback log,bool worldObserverAvailable=false);
// Installed into the existing native weapon WorldBind interceptor. Restore the
// tracked view BEFORE that interceptor inspects the state or calls native WV.
void ObserveWorldBind(void* state,const NativeCameraMath::CameraPose* source,std::uint32_t callerRva);
void Configure(ReadView reader);
void SetCinematicViewport(bool enabled,unsigned width=0,unsigned height=0,const float* projection=nullptr);
void InvalidateDevice();
void* BeginDraw(IDirect3DDevice9* device);
HRESULT FinishDraw(void* token,HRESULT result);
// Callback for the checked native mapped allocation before original Unlock.
void FilterMapped(std::uint32_t writer,void* mapped,std::size_t bytes);
Stats Snapshot();
}
