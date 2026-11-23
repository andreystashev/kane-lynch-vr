#pragma once
#include <cstdint>
#include <cstddef>
#include <array>
#include "native_camera_math.hpp"
#include "native_objective_trace_kl.hpp"
#include "native_effects_visual_source_kl.hpp"
#include "native_team_hud_kl.hpp"

namespace NativeHudKL {
struct Frame {
    float fov[2][4]{};
    float eyeOffset[3]{};
    float aspect=16.f/9.f,distanceMetres=2.f,coverage=.8f;
    unsigned eye=0;
    unsigned width=0,height=0;
    std::uint64_t pairId=0;
};
using ProvideFrame=bool(*)(Frame&);
struct CinemaTextFrame {unsigned width=0,height=0;float scale=1.f;};
using CinemaTextProvider=bool(*)(CinemaTextFrame&);
void SetCinemaTextProvider(CinemaTextProvider provider);
std::uint64_t CinemaTextDraws();
using LogCallback=void(*)(const char*);
using LetterboxProvider=bool(*)();
using MappedVertexFilter=void(*)(std::uint32_t writer,void* mapped,std::size_t bytes);
struct MappedVertexContext {
    std::uintptr_t sourceState=0;
    unsigned particleMode=~0u,liveQuads=0;
    bool liveKnown=false;
    // Borrowed only within the exact native SpriteDraw call. Type5/6 records
    // supply the authored anchor, which need not equal the quad centroid.
    std::uintptr_t sourceEntry=0,sourceGeometry=0,spriteRecords=0;
    unsigned spriteType=0,spriteCount=0,spriteFirst=0;
    std::uint64_t spriteDrawId=0;
};
// Valid only during the existing pre-Unlock callback. No borrowed pointers
// escape that call. Type7 liveQuads excludes its allocated/unwritten tail.
bool ReadMappedVertexContext(MappedVertexContext& output);
// Exact type7 or qualified world type5/6 producer scope and shader draw.
// UI/other producers have no context. Valid only on the original draw thread.
bool ReadParticleDrawContext(MappedVertexContext& output);
void SetVisualSourceTrace(NativeEffectsKL::VisualTraceActive active);
bool ReadSpriteVisualSource(std::uintptr_t entry,std::uintptr_t state,NativeEffectsKL::VisualSourceDiagnostic& output);
struct ReticleFrame {
    std::uintptr_t player=0;
    std::uint64_t pairId=0;
    std::uint32_t tick=0;
    NativeCameraMath::CameraPose actualEye{};
    NativeCameraMath::Vec3 target{};
    float projection[16]{};
    NativeCameraMath::CameraPose centre{};
    float referenceProjection[16]{};
    unsigned width=0,height=0;
    unsigned eye=2;
};
using ReticleProvider=bool(*)(ReticleFrame&);
using NativeScopeProvider=bool(*)();
void SetReticleProvider(ReticleProvider provider);
void SetNativeMarkerProvider(ReticleProvider provider);
// Exact owned sniper optics retain their authored reticle. The completed
// native image is composed once by the dedicated mono-scope host path.
void SetNativeScopeProvider(NativeScopeProvider provider);
// True only for the original authored scope camera overlay in the current
// applied optic frame. Its geometry already contains the native crosshair.
bool OriginalScopeOverlayActive();
void SetReticleEnabled(bool enabled);
bool ReticleEnabled();
// Disabled by default. Bounded evidence only; never influences drawing.
void SetObjectiveTraceEnabled(bool enabled);
bool ObjectiveTraceEnabled();
NativeObjectiveTraceKL::Snapshot SnapshotObjectiveTrace();
// Called only around the exact owned gameplay crosshair producer. The native
// sprite command is identified here, then translated at its deferred GPU draw.
void BeginReticle(std::uintptr_t player,void* renderer);
void EndReticle();
// Exact owned ActionMarkerDisplay producer scope. Preserve original sprite
// resources/geometry, observe native WORLD position, then relocate its centre
// for the actual eye at the deferred GPU draw without changing native icons.
void BeginNativeMarkers(std::uintptr_t player,void* renderer,void* nativeCamera=nullptr);
void EndNativeMarkers();
enum class WristHudElement : unsigned { Team, Minimap, Weapon, Action, Instruction, Speech, WeaponWheel, ClimbAction, Count };
struct WristHudSource {
    void* renderer=nullptr;
    std::uintptr_t player=0,display=0;
    WristHudElement element=WristHudElement::Team;
    NativeCameraMath::Vec3 anchor{};
    NativeTeamHudKL::Roster roster;
    bool caption=false;
};
inline bool ScreenText(WristHudElement element){return element==WristHudElement::Instruction||element==WristHudElement::Speech;}
struct WristHudEye {
    float x=0,y=0,scale=1;
    unsigned width=0,height=0,eye=0;
    std::uint64_t pair=0;
    float aspectCorrection=1.f;
};
using WristHudProvider=bool(*)(WristHudElement,std::uintptr_t,WristHudEye&);
void SetWristHudProvider(WristHudProvider provider);
bool WristHudReady();
WristHudSource ReplaceWristHudSource(const WristHudSource& source);
NativeTeamHudKL::Roster PresentedTeamRoster(std::uintptr_t player,std::uint64_t pair,unsigned eye);
struct TeamPanelStats {std::uint64_t left=0,right=0,dropped=0;unsigned lastRejection=0;};
TeamPanelStats SnapshotTeamPanel();
std::uint64_t WristHudDraws(WristHudElement element,unsigned eye);
std::uint64_t WristAmmoDraws(unsigned eye);
std::uint64_t WristCaptionDraws(unsigned eye);
std::uint64_t SelectionMarkerDraws(unsigned eye);
enum class NativeMarkerEvent {Begin,End,Reset};
using NativeMarkerObserver=void(*)(NativeMarkerEvent event,std::uintptr_t player);
// Observer runs only in the existing exact local ActionMarkers producer scope.
void SetNativeMarkerObserver(NativeMarkerObserver observer);
// Existing checked Lock/Unlock owns this span. Exact world-quad 99360,
// mode-qualified particle 9997E, and ManualScatter A37D3 spans are offered;
// their layouts are fixed by provenance. UI writers remain separate.
void SetMappedVertexFilter(MappedVertexFilter filter);
struct MarkerSourceEvidence {
    std::uint64_t captures=0;std::uint32_t tick=0,worldTick=0,type=0,texture=0,caller=0;
    std::uintptr_t record=0,entity=0,entityVtable=0,geometry=0,parent=0;
    bool worldValid=false;std::uint8_t fixed=0,bob=0,timed=0;
    NativeCameraMath::Vec3 target{},world{};
    float width=0,height=0,offsetX=0,offsetY=0,sourceDepth=0,maxPointStep=0;
    bool sourceExtentValid=false;
    float sourceWidth=0,sourceHeight=0,sourceOffsetX=0,sourceOffsetY=0;
};
struct Stats {
    bool installed=false;
    std::uint64_t calls=0,scopes=0,uploads=0,rejected=0,fullScreenPreserved=0;
    std::uint32_t threadId=0;
    std::uint64_t markerCanonicalized=0,markerCanonicalRejected=0,markerCanonicalNonfinite=0;
    float markerCanonicalLastMagnitude=0,markerCanonicalMaxMagnitude=0,markerCanonicalSpanX=0,markerCanonicalSpanY=0;
    float lastProjection[16]{};
    std::uint64_t indexedCalls=0,genericCandidates=0,genericUploads=0;
    std::uint64_t cameraRejected=0,viewportRejected=0,constantsRejected=0,bufferRejected=0;
    std::uint32_t lastCaller=0,lastStride=0;
    std::uint32_t lastVertexUsage=0,lastIndexUsage=0,lastReadStage=0;
    std::uint64_t constantReadRejected=0,backendRejected=0,provenanceRejected=0,depthRejected=0,geometryRejected=0;
    std::uint64_t nativeLocks=0,nativeUnlocks=0,shadowCaptured=0,shadowRejected=0,shadowReads=0,shadowMisses=0;
    std::uint64_t shadowSkippedWrites=0,shadowCopiedBytes=0,shadowTrackedInvalidations=0;
    std::uint32_t lastWriter=0,lastZWrite=0,lastZFunc=0,lastRawCaller=0;
    std::int32_t lastConstantsResult=0;
    float lastConstants[20]{};
    float lastRawConstants[20]{};
    float lastQuadMinZ=0.f,lastQuadMaxZ=0.f;
    std::uint64_t scissorAdjusted=0,scissorRejected=0;
    std::uint64_t letterboxCalls=0,letterboxHidden=0,letterboxRejected=0;
    std::uintptr_t lastLetterbox=0;
    std::uint32_t lastLetterboxMini=0,lastLetterboxFull=0;
    bool lastLetterboxFirstPerson=false;
    float lastLetterboxAmount=0.f;
    std::uint64_t reticleCaptured=0,reticleRouted=0,reticleDrawn=0,reticleRejected=0,reticleRestoreFailures=0;
    std::uint64_t nativeScopeReticlePreserved=0;
    std::uint64_t nativeMarkerEmitted=0,nativeMarkerCaptured=0,nativeMarkerRouted=0,nativeMarkerDrawn=0,nativeMarkerRejected=0,nativeMarkerReprojected=0;
    std::uint64_t nativeDestinationCaptured=0,nativeDestinationDrawn=0;
    std::uint64_t nativeObjectiveBobSuppressed=0,nativeObjectivePairsReused=0,nativeMarkerAspectCorrected=0;
    std::uint32_t nativeMarkerRejectStage=0;
    std::uint64_t nativeMarkerAmbiguous=0,nativeMarkerNegativeW=0;
    std::uint32_t nativeMarkerVertexCount=0;
    float nativeMarkerMinZ=0,nativeMarkerMaxZ=0,nativeMarkerMinW=0,nativeMarkerMaxW=0;
    std::uint64_t nativeMarkerCalibrated=0,nativeMarkerCalibrationRejected=0;
    float nativeMarkerWidthNdc=0,nativeMarkerHeightNdc=0,nativeMarkerSourceDepth=0;
    std::uint64_t spriteMarkerRetired=0,spriteReticleRetired=0;
    std::uint64_t worldSprite5Sources=0,worldSprite6Sources=0,worldSpriteRelativePreserved=0,worldSpriteHudPreserved=0;
    struct WorldSpriteSourceSnapshot {
        bool valid=false,anchorKnown=false;
        std::uint64_t sequence=0,drawScopeId=0;
        std::uint32_t tick=0,type=0,flags=~0u,slot=2,count=0,reason=0;
        std::uintptr_t entry=0,geometry=0,records=0,sourceState=0;
        float anchor[3]{};
    };
    WorldSpriteSourceSnapshot lastWorldSpriteEligible,lastWorldSpritePreserved;
    std::array<std::uint64_t,9> spriteSourceTypes{};
    MarkerSourceEvidence markerSource{};
};
// Independent fresh first-person VR authorization on the UI producer thread.
// The exact native draw hook additionally verifies MiniCutscene/full counters.
void SetLetterboxProvider(LetterboxProvider provider);
// Retire dynamic-buffer identities before the game destroys/recreates them.
void InvalidateDevice();
bool Install(ProvideFrame provider,LogCallback log);
Stats Snapshot();
}
