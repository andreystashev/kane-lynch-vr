#pragma once

#include <cstdint>
#include "native_camera_math.hpp"
#include "native_reflection_packets_kl.hpp"
#include "native_secondary_mask_projection_kl.hpp"
#include "native_camera_diagnostics_kl.hpp"

// Native camera hooks for the pinned KANEANDLYNCH.EXE build. The provider
// supplies a row-vector VIEW delta in the ordinary D3D LH camera axes
// (+X right, +Y up, +Z forward), in game units.
namespace NativeCameraKL {
struct Frame {
    float delta[16]{};
    float fov[4]{};
    float cullingFov[4]{};
    float nearUnits=0.f;
    float anchorPosition[3]{};
    // The controller IPC uses raw tracking coordinates. Pair these with the
    // actual rendered eye pose so native arms share the camera's reference.
    float rawHeadOrientation[4]{};
    float rawHeadPosition[3]{};
    float eyeOffset[3]{};
    float worldUnitsPerMetre=100.f;
    std::uintptr_t anchorCamera=0;
    std::uintptr_t anchorPlayer=0;
    unsigned eye=0;
    std::uint64_t pairId=0;
    std::uint32_t trackingTick=0;
    double sensorTime=0;
    long recenterAck=0;
    std::uintptr_t sourceCamera=0;
    bool anchorValid=false;
    // Provider sets this only for unavailable current anatomical pose after
    // UI/runtime/tracking gates. It never authorizes an old eye position.
    bool anchorUnavailable=false;
    bool animatedEye=false;
    bool nativeMounted=false;
    bool stableFirstPersonYaw=false;
    // Classified by the checked source-camera provider once per eye frame;
    // hot per-object render guards must not re-walk native cinematic state.
    bool nativeCinematic=false;
    // Owned scripted actor action using the verified local native camera.
    // Its authored origin is retained; only a nearby own head may be masked.
    bool actorCinematic=false;
    // Only an owned upright MiniAct/MiniAnim scene may add deliberate actor
    // root yaw to the stable first-person base. Never use a head/neck pose or
    // third-person camera forward. HMD/virtual yaw remain in delta once.
    bool authoredHeading=false;
    std::uintptr_t authoredHeadingAction=0;
    NativeCameraMath::Vec3 authoredActorForward{};
    // Current queued actor root, used once when leaving an anatomical rappel
    // camera. The actor model's face points along -forward.
    bool actorForwardValid=false;
    NativeCameraMath::Vec3 actorForward{};
    // Owned floor pickup: anatomical eye posture is expressed relative to
    // the actor ROOT and the stable gameplay heading, never the third-person
    // pickup camera. Physical HMD look is still applied exactly once.
    bool nativePickup=false;
    std::uintptr_t pickupAction=0;
    NativeCameraMath::Vec3 pickupActorForward{};
    // Checked local rappel action: keep the authored camera's pitch/roll
    // instead of forcing the physically horizontal actor into an upright view.
    bool nativeActionTilt=false;
    // Current seat/mounted anatomical eye. Its carrier-local orientation is
    // retained separately from gameplay heading; position updates each eye.
    bool nativeCarried=false;
    std::uintptr_t carriedReference=0;
    unsigned carriedReferenceKind=0;
    NativeCameraMath::CameraPose carriedBase{};
    // Exact owned vehicle entry/exit animation. Unlike the seated carrier
    // mode, its CURRENT head orientation and position both supply the base.
    bool nativeVehicleTransition=false;
    std::uintptr_t vehicleAction=0;
    NativeCameraMath::CameraPose vehicleBase{};
    // Exact owned Rappel/Climb input: preserve the engine's full W/S axis
    // rather than applying ordinary walking-speed modifiers in the host.
    bool nativeMovementAxes=false;
    bool nativeScope=false;
    bool nativeActionUpValid=false;
    float nativeActionUp[3]{};
    float nativeActionForward[3]{};
    // Right eye crossed a native camera/action/mode boundary after its left
    // eye. The caller must restart the pair instead of publishing a mix.
    bool pairDiscontinuity=false;
    bool movementYawValid=false;
    float movementYawRadians=0.f;
    bool valid=false;
};
using ProvideFrame=bool(*)(Frame& frame,void* user);
using AppliedCallback=void(*)(const Frame& frame,const NativeCameraMath::CameraPose& actualEye,const float* projection,void* user);
using LogCallback=void(*)(const char* message);

struct ReflectionEvidence {
    bool valid=false;
    // 0 accepted, 1 unexpected lens/crop, 2 invalid mirror/clip math.
    unsigned reason=0,phase=0,eye=0;
    std::uint32_t tick=0;
    std::uint64_t sequence=0,pair=0;
    std::uintptr_t packet=0,queue=0,sourceEntry=0,reflector=0,sourceCamera=0,viewId=0;
    float originalView[12]{},reflectedView[12]{},eyeView[12]{};
    float originalProjection[16]{},obliqueProjection[16]{},finalNativeProjection[16]{},vrProjection[16]{},resultProjection[16]{};
};
struct ReflectionProducerEvidence {
    bool valid=false;
    std::uint32_t tick=0;
    std::uint64_t sequence=0;
    NativeReflectionPacketsKL::Trace trace;
};
struct SecondaryProjectionEvidence {
    bool valid=false;unsigned reason=0,eye=0;std::uint32_t tick=0;
    std::uint64_t sequence=0,pair=0;
    std::uintptr_t state=0,packet=0,queue=0,sourceEntry=0,sourceCamera=0;
    float nativeProjection[16]{},vrProjection[16]{};
};
struct SecondaryProducerEvidence {
    bool valid=false;unsigned reason=0;std::uint32_t tick=0;std::uint64_t sequence=0;
    std::uintptr_t queue=0,sourceEntry=0,sourceCamera=0,cameraPacket=0,projectionPacket=0,drawPacket=0,popPacket=0;
    std::uint32_t cameraHeader[6]{},projectionHeader[6]{},drawHeader[6]{},popHeader[6]{};
    float mainProjection[16]{},alternateProjection[16]{};
};
struct Stats {
    bool installed=false;
    std::uint64_t cinematicDrawReused=0,cinematicNativeReads=0;
    std::uint64_t cinematicCameraDrawReused=0,cinematicCameraNativeReads=0;
    std::uint64_t cinematicPanelDrawReused=0,cinematicPanelNativeReads=0;
    bool pipGuardVerified=false;
    std::uint64_t copies=0,ownedCopies=0,rejected=0,projections=0,renderPoses=0,geometryPoses=0;
    std::uint64_t shaderCallsInsideRender=0,shaderCallsOutsideRender=0;
    std::uint64_t restoredPoses=0,restoreFailures=0;
    std::uint32_t cameraRenderThreadId=0,shaderThreadId=0,cleanupThreadId=0;
    std::uintptr_t lastSource=0;
    float lastFarUnits=0.f;
    std::uint64_t gpuCameraCalls=0,gpuCameraViews=0,gpuCameraRejected=0;
    std::uint64_t nativeActionTiltViews=0;
    std::uint64_t nativeAnimatedUpViews=0;
    std::uint64_t queuedProjectionRestored=0,queuedProjectionRowsRestored=0;
    std::uint64_t actionHeadingGaps=0,actionHeadingRetained=0,actionHeadingRejected=0;
    std::uint64_t authoredHeadingViews=0,authoredHeadingTurns=0;
    std::uint64_t pickupHeadViews=0;
    std::uint64_t vehicleTransitionViews=0,captureCompromisedFrames=0,unclassifiedPrimaryStatePasses=0;
    std::uint64_t reflectionScopes=0,reflectionCertified=0,reflectionRejected=0,reflectionViews=0,reflectionRestores=0;
    std::uint64_t reflectionLensRejected=0;
    ReflectionEvidence lastReflectionAccepted,lastReflectionRejected;
    std::uint64_t reflectionEmptyScopes=0,tokyoReflectionCameraPreparations=0,retomotoReflectionCameraPreparations=0;
    ReflectionProducerEvidence lastReflectionProducerAccepted,lastReflectionProducerRejected;
    std::uint64_t secondaryProducerScopes=0,secondaryCertified=0,secondaryApplied=0,secondaryPopped=0,secondaryRejected=0;
    std::uint64_t secondaryBodyBinds=0,secondaryWeaponBinds=0;
    SecondaryProjectionEvidence lastSecondaryAccepted,lastSecondaryRejected;
    SecondaryProducerEvidence lastSecondaryProducerRejected;
    std::uint64_t gpuPipPassesSkipped=0,gpuPipCompositeFrames=0;
    std::uintptr_t lastPipCamera=0,lastPipGeometry=0;
    std::uint32_t gpuCameraThreadId=0;
    unsigned lastGpuEye=0;
    std::uint64_t lastGpuPair=0;
    float lastGpuPosition[3]{};
};

bool Install(ProvideFrame provider,AppliedCallback applied,void* user,LogCallback log);
bool Installed();
// A single verified owner of BF600 dispatches optional producer suppression.
// Registration requires that this module's exact detour still owns the native
// target; it never chains an unknown hook or authorizes a second MinHook patch.
using CameraProducerFilter=bool(*)(void*,std::uintptr_t,std::uintptr_t,std::uintptr_t,
                                  std::uintptr_t,std::uintptr_t,std::uintptr_t);
bool RegisterCameraProducerFilter(std::uintptr_t verifiedImage,CameraProducerFilter filter);
// Default native cinematics stay on a head-independent screen. Changes are
// observed at the next owned camera packet, before any VR camera/body edits.
void SetCinematicVR(bool enabled);
struct EffectsView {
    std::uintptr_t state=0;
    NativeCameraMath::CameraPose eye{};
    NativeCameraMath::CameraPose reference{};
    NativeCameraMath::Vec3 centre{};
    float projection[16]{};
    float world[12]{};
    bool worldKnown=false;
    // Semantic1 modifies WORLD*VIEW before P. Semantic7 (projected mirror
    // coordinates) deliberately does not use these material depth controls.
    float modelViewScale=1.f,modelViewDepthBias=0.f;
    std::uint64_t frame=0;
    unsigned eyeIndex=0;
};
// Current owned GPU camera only, including tracked authored cameras. Other
// camera packets retire this authorization even when they use another state.
bool ReadEffectsView(EffectsView& output);
// Reflected RT production is separate from main-eye ownership. This receipt
// authorizes no player/body transforms and retires at the next camera packet.
struct ReflectionTargetView {
    std::uintptr_t state=0;
    std::uintptr_t reflector=0;
    std::uint64_t serial=0,frame=0,pair=0;
    unsigned eyeIndex=0;
    NativeCameraMath::CameraPose view{}; // packed inverse reflected VIEW
    float projection[16]{};
    bool horizontal=false;float floorHeight=0;
};
bool ReadReflectionTargetView(ReflectionTargetView& output);
void QualifyFloorReflector(std::uintptr_t reflector,bool retomoto);
// Only the native billboard basis builder may borrow this reflected reference.
bool ReadReflectionBillboardReference(std::uintptr_t state,NativeCameraMath::CameraPose& view);
bool ReadReflectionScatter(ReflectionTargetView& camera,float (&columns)[8]);
std::uint64_t ActiveReflectionTargetSerial();
// Native full and mini cutscenes own camera/actor animation. Missing or
// corrupt native context conservatively retains the original rendering.
bool CinematicActive();
std::uintptr_t CinematicCamera();
bool CinematicPanelActive();
// Only the exact live renderer state bearing the authorized VR view and
// projection may consume player visual overrides. Shadow/reflection cameras
// that replace either matrix retain the complete native player palette.
bool DrawStateActive(std::uintptr_t state,bool* firstPerson=nullptr,bool allowActorCinematic=false);
// Read-only receipt for owned bind diagnostics. Authorization still requires
// the ordinary exact main VIEW/P guards; never permits another camera.
bool IsSecondaryProjectionDraw(std::uintptr_t state);
void ObserveSecondaryMaskBind(std::uintptr_t state,std::uintptr_t entry,unsigned kind);
// Diagnostic snapshots are passive: failed camera guards remain visible and
// never grant permission to change an otherwise native draw.
enum DrawCameraFailure : std::uint32_t {
    CameraNotInstalled=1,CameraMissingState=2,CameraForeignState=4,
    CameraNoFrame=8,CameraOldGeneration=16,CameraForeignThread=32,
    CameraCinematic=64,CameraStateUnreadable=128,CameraExpectedViewInvalid=256,
    CameraViewMismatch=512,CameraProjectionMismatch=1024
};
struct DrawCameraContext {
    std::uint32_t version=1,tick=0,thread=0,gpuThread=0,failures=0;
    std::uint64_t generation=0,gpuGeneration=0,pair=0;
    std::uintptr_t state=0,gpuState=0,currentState=0,sourceCamera=0,anchorCamera=0,anchorPlayer=0;
    bool installed=false,frameValid=false,anchorValid=false,nativeCinematic=false,actorCinematic=false;
    bool stateRead=false,expectedViewValid=false,viewMatches=false,projectionMatches=false;
    bool pairValid=false,secondaryActive=false;
    unsigned eye=0;
    std::uintptr_t pairSource=0;std::uint64_t cachedPair=0;
    float actualWorld[12]{},actualView[12]{},actualProjection[16]{};
    float expectedView[12]{},vrProjection[16]{},nativeProjection[16]{};
    std::uintptr_t packet=0,packetState=0,packetEntry=0,packetEntryVtable=0,packetGeometry=0,packetCamera=0,packetCameraVtable=0;
    std::uint64_t packetGeneration=0;
    std::uint32_t packetHeader[6]{},packetCameraFlags=0;
    bool packetHeaderRead=false,packetSourceRead=false,packetCameraFlagsRead=false;
    float packetView[12]{},packetProjection[16]{};
};
bool SnapshotDrawCamera(std::uintptr_t state,DrawCameraContext& output);
using DiagnosticsActive=bool(*)();
using ProducerBatchObserver=void(*)(const NativeCameraDiagnosticsKL::ProducerBatchContext&);
using ProducerBytesObserver=void(*)(std::uint64_t sequence,const void* bytes,std::size_t size);
// Raw bytes point to the immutable closed CPU copy only during callback.
// The sequence joins raw bytes to their preceding bounded context record.
void ConfigureDiagnostics(DiagnosticsActive active,ProducerBatchObserver observer,ProducerBytesObserver bytesObserver=nullptr);
void FrameBoundary();
// GPU-consumer-only main-image lifetime token. Repeated camera packets in one
// Present return the same epoch; a new eye/Present receives a new epoch.
// Invalid for foreign/reflection/cinematic/retired scene state. Outputs are
// unchanged on failure and the token alone never authorizes a draw or input.
bool ReadRenderContext(std::uint64_t& epoch,unsigned& eye);
// Capture rejects an image if any owned main-camera pass fell back to native,
// including recovery later in that same already partially rendered image.
bool CaptureReady(std::uint64_t pairId,unsigned eye);
// Completed main image only, on its GPU thread. Remains valid after FinishScene
// retires draw/arm leases and is revoked by frame/pair/owner/capture rejection.
// This read-only token NEVER authorizes a native draw, write or simulation aim.
bool ReadCaptureContext(std::uint64_t& epoch,unsigned& eye,std::uint64_t& pairId);
// Call after all deferred draws, before capture; FrameBoundary is a fallback.
void FinishScene();
// Render-thread diagnostics: aggregate per frame, without per-upload atomics.
void ObserveShaderConstants();
// A projection-only queued command can restore the exact native camera P
// after kind1. Repair only that same authored VR camera and its known c0 P.
bool RepairProjectionConstants(unsigned startRegister,float* rows,unsigned registerCount);
bool MayRepairProjectionConstants();
Stats Snapshot();
}
