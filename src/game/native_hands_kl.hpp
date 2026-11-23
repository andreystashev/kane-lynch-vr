#pragma once
#include <cstdint>
struct IDirect3DDevice9;
#include "native_camera_math.hpp"
#include "native_draw_context_kl.hpp"
namespace NativePassengerActorKL {struct Motion;}

// Native K&L skeletal hands: native animation is read-only. A copy of the
// current GPU skin palette receives arm IK; gameplay bones are never written.
namespace NativeHandsKL {
using LogCallback=void(*)(const char*);
using RenderContextProvider=bool(*)(std::uint64_t& epoch,unsigned& eye);
void ConfigureRenderContext(RenderContextProvider provider);
bool RenderPairCaptureReady();
void RetireRenderPairImage();
struct Stats {
    bool installed=false;
    std::uint64_t bindPoseCacheHits=0,bindPoseCacheMisses=0;
    std::uint64_t stableTorsoFrames=0;
    std::uint64_t neutralStandingFrames=0;
    std::uint64_t neutralStandingEyeFrames=0;
    std::uint64_t passengerHandsPinned[2]{},passengerNativeReachCorrected[2]{};
    std::uint64_t heldGrenadeArmFrames=0;
    std::uint64_t renderPairAdmitted=0,renderPairReused=0,renderPairSecondEye=0,renderPairRejected=0,renderPairExpired=0;
    std::uint64_t bonesLightCacheInvalidated=0;
    float passengerNativeReachErrorMetres[2]{};
    std::uint64_t passengerHeadsHidden=0;
    std::uint64_t cargoPoseCandidates=0,cargoPoseApplied=0,cargoEyeApplied=0,cargoPoseRejected=0;
    unsigned cargoRejectStage=0,cargoPelvis=128,cargoHead=128,cargoLeftFoot=128,cargoRightFoot=128;
    bool shadowPassInstalled=false;
    std::uint64_t shadowPassCalls=0,shadowPassScopes=0,shadowHeadPalettes=0;
    std::uint64_t calls=0,playerCalls=0,applied[2]{},clamped[2]{},rejected=0;
    std::uint64_t insideRenderCalls=0,outsideRenderCalls=0;
    std::uint64_t gpuPalettes=0,gpuRejected=0,headHidden=0;
    std::uint64_t gpuUploadInvalidated=0,gpuCacheRejected=0;
    std::uint64_t paletteLeaseRestored=0,paletteLeaseReplaced=0,paletteLeaseRejected=0,paletteLeaseResumed=0,paletteLeaseFallback=0;
    std::uint64_t skinDrawSamples=0,skinDrawRejected=0,skinConstantUploads=0,skinConstantMismatches=0;
    std::uint64_t ownPaletteUploads=0,collapsedHeadUploads=0,shaderRowsInspected=0;
    std::uint64_t gpuBinderCalls=0,gpuPlayerBinds=0,gpuNoContext=0;
    std::uint64_t otherViewPreserved=0;
    bool alternatePaletteProof=false;
    std::uint64_t alternatePaletteCandidates=0,alternatePaletteApplied=0,paletteSourceRejected=0;
    std::uint64_t helperApplied[2]{},lazyVisualPrepared=0,lazyVisualCacheHits=0,lazyVisualRejected=0;
    std::uint64_t rappelTorsoCandidates=0,rappelTorsoApplied=0,rappelTorsoRejected=0;
    std::uint32_t frameThread=0,bindThread=0;
    float requestedDistanceMetres[2]{},armReachMetres[2]{},targetErrorMetres[2]{};
    float residualTwistDegrees[2]{};
    float playerHeadOffsetMetres[3]{},playerHeadDistanceMetres=0.f;
    std::uintptr_t player=0,model=0;
    std::uint32_t ageMs=0;
    std::uint64_t supportCandidates=0,supportHeld=0,supportReachRejected=0,supportReachAdjusted=0;
    std::uint64_t supportAcquired=0,supportReleased=0,supportAimRecovered=0,supportAimRejected=0;
    std::uint64_t supportSuspended=0;
    std::uint64_t supportReleaseInput=0,supportReleaseContext=0,supportReleaseIdentity=0,supportReleaseInvalidInput=0,supportReleaseEpoch=0;
    bool supportGripSampleValid=false;
    float supportRawGrip=0,supportReleaseGrip=0;
    std::uint32_t supportGripSampleTick=0,supportReleaseSampleTick=0,supportReleaseLowMs=0,supportReleaseSamples=0;
};
// Opt-in colour/mask comparison from current owned draw leases. No GPU
// readback or state writes; counters describe actual prepared GPU palettes,
// not proof that a particular screenshot contains a visual defect.
struct SkinPassPairSample {
    bool valid=false,sourceEqual=false,paletteEqual=false,worldEqual=false;
    std::uint32_t tick=0,bones=0,firstPass=0,lastPass=0;
    std::uint64_t pair=0,epoch=0,firstGeneration=0,lastGeneration=0;
    std::uintptr_t entry=0,state=0;
    float maxPaletteDelta=0,maxWorldDelta=0;
};
struct SkinPassPairEvidence {
    std::uint64_t colours=0,compared=0,different=0,sourceChanged=0,rejected=0;
    SkinPassPairSample last{},lastDifferent{};
};
void SetSkinPassPairDiagnostics(bool enabled);
SkinPassPairEvidence SnapshotSkinPassPair();
struct SkinMaterialSample {
    bool valid=false,support=false,gestureHeld=false,coupled=false,shadow=false;
    float rawGrip=0;
    std::uint32_t tick=0,pass=0,bones=0,vsHash=0,psHash=0,declarationHash=0,renderStateHash=0;
    std::uintptr_t entry=0,state=0,package=0,technique=0,sourcePalette=0,renderPalette=0,textures[4]{};
    std::uint32_t textureShapes[4]{},paletteHash=0,sourceHash=0,constantHash=0;
    unsigned startRegister=0,registerCount=0;
    bool constantsCompared=false,constantsMatch=false;
    std::uint64_t pair=0;
};
// Bounded read-only material/skin evidence, at most1 draw/500ms for each of
// normal/shadow × held/unheld. It never changes a device state or binds a
// resource. Call only for a native draw which will actually be submitted.
void ObserveNativeDraw(IDirect3DDevice9* device);
bool SnapshotSkinMaterial(bool support,SkinMaterialSample& output,bool shadow=false);
void InvalidateMaterialProbe();
// Exact owned normal skin draw whose current arm solve couples the foregrip.
// Used only by the byte-authenticated TBN lighting shader scope.
bool CoupledSkinLightingActive();
bool Install(bool enabled,LogCallback log);
using PassengerMotionProvider=bool(*)(std::uintptr_t,std::uint32_t,NativePassengerActorKL::Motion&);
void SetPassengerMotionProvider(PassengerMotionProvider provider);
// Read-only diagnostics for successful tracked palette bindings. kind0 body.
using ProjectionBindObserver=void(*)(std::uintptr_t,std::uintptr_t,unsigned);
void SetProjectionBindObserver(ProjectionBindObserver observer);
// Existing world/rigid observer calls this BEFORE the original native binder.
// Source may be null at a rigid pre-bind. Cheap TLS check when no body lease.
void ObserveWorldBind(void* state,const NativeCameraMath::CameraPose* source,std::uint32_t callerRva=0);
// Read-only, opt-in recording metadata, including native draws rejected by
// every VR adapter. This never calls DrawStateActive or solves/publishes IK.
// Caller budgets graph reads; invoke on the GPU thread inside native Draw.
void SetDrawContextDiagnostics(bool enabled);
bool SnapshotNativeDrawContext(NativeDrawContextKL::Context& output);
// Closed native48F0A0 dispatch only: actual current record's +C provider.
// Used to prove non-inline instance/derived WORLD sources for owned meshes.
bool ReadCurrentNativeEntry(std::uintptr_t state,std::uintptr_t& entry,unsigned* pass=nullptr);
void SetFrame(const NativeCameraMath::CameraPose& trackedEye,
              const float rawHeadOrientation[4],const float rawHeadPosition[3],
              const float eyeOffset[3],float worldUnitsPerMetre,std::uint64_t pairId,bool firstPerson=true,
              std::uint32_t recenterAck=0,bool actorHeadOnly=false);
void ClearFrame();
// Input-context transitions invalidate the gesture independently of the
// per-eye draw gate. A held grip must be released before a new acquisition.
void InvalidateSupportGrip();
// Only a fresh owned GPU camera draw can authorize visual weapon changes.
// ClearFrame immediately closes this gate for UI and other camera passes.
bool DrawFrameActive(std::uint64_t& pairId,std::uint64_t* renderGeneration=nullptr);
// Read-only observation of the float4 arguments about to reach D3D. Counts
// only uploads matching the native palette gather buffer while it uses TLS.
void ObserveShaderConstants(const float* rows,unsigned count,unsigned startRegister=0);
// Read native stance height from the verified GPU head attacher, anchored
// over the native root X/Z so animated gait cannot sway the VR camera.
// Optional stableHead is a WORLD anatomical eye from the current head-parent
// palette plus bind head offset (no head-local animation). Its zero-axis
// basis fails ValidatePose when that separate proof is unavailable.
enum class StableHeadMode {Rappel,Carried};
bool GpuHeadAnchor(std::uintptr_t expectedPlayer,NativeCameraMath::Vec3& output,float worldUnitsPerMetre,
                   bool animatedHead=false,NativeCameraMath::Vec3* animatedUp=nullptr,
                   NativeCameraMath::Vec3* animatedForward=nullptr,
                     NativeCameraMath::CameraPose* nativeRoot=nullptr,
                     NativeCameraMath::CameraPose* stableHead=nullptr,
                     unsigned* rappelBodyPhase=nullptr,
                     NativeCameraMath::Vec3* rappelTowardHead=nullptr,
                     StableHeadMode stableMode=StableHeadMode::Rappel);
struct VisualFrame {
    std::uintptr_t player=0;
    // Current draw's body yaw, for exact player-owned stowed attachments.
    NativeCameraMath::CameraPose rawRoot{},renderRoot{};
    bool bodyAvailableForMesh=false;
    // Both come from the same owned GPU draw. rawWrist uses the untouched
    // native root/pose; finalWrist includes visual body yaw and solved IK.
    NativeCameraMath::CameraPose rawWrist[2]{},finalWrist[2]{};
    bool availableForMesh[2]{},tracked[2]{};
    std::uint64_t pairId=0;
    std::uint32_t recenterAck=0;
    // Compatibility aliases for native aiming: finalWrist and tracked.
    NativeCameraMath::CameraPose wrist[2]{};
    NativeCameraMath::CameraPose controllerWrist[2]{},palmLocal[2]{},centre{};
    bool palmValid[2]{};
    float trigger[2]{},grip[2]{};
    std::uint32_t inputValid[2]{};
    float unitsPerMetre=100.f;
    bool active[2]{};
    bool supportHeld=false,supportLatched=false;
    std::uintptr_t foregripItem=0,foregripModel=0;
    std::uint32_t foregripReference=0;
    NativeCameraMath::Vec3 foregripSocket{};
    std::uint32_t tick=0;
    // Admission belongs only to render consumers. Simulation readers must
    // still require the original physical pose sample to be fresh.
    std::uint32_t sourcePoseTick=0;
    bool renderPairPose=false;
};
bool SnapshotVisual(VisualFrame& output);
// Fresh controller targets in the most recent owned first-person camera.
// The optic can cull the player mesh, so its reload has no palette to read.
bool SnapshotReloadControllers(std::uintptr_t player,std::uint32_t recenter,VisualFrame& output);
// Exact current main-view player palette lease in native PostFilterZPass.
// This grants only a foreground mask override, never a camera/pose edit.
bool PostfilterForegroundActive();
// Current first-person local skin colour pass, excluding the depth mask and shadow.
bool ReloadPropDrawActive(std::uintptr_t player,std::uint64_t pair);
// Current owned GPU camera generation only. Lazily prepares fresh wrists if
// the held weapon draws before the body; never publishes a GPU palette.
bool SnapshotDrawVisual(VisualFrame& output);
struct DrawController {
    NativeCameraMath::CameraPose centre{},wrist{},coupledWrist{};
    std::uintptr_t coupledItem=0,coupledRoot=0,coupledModel=0;
    std::uint32_t coupledReference=0;
    std::uint32_t recenter=0;
    float units=0;
    bool tracked=false;
};
// Current render-pair tracking, independent of whether the actor's skin was drawn.
bool SnapshotDrawController(std::uintptr_t player,std::uint64_t pair,unsigned side,DrawController& output);
// Exact current primary body draw root, recomputed from its current native
// entry rather than a previous VisualFrame cache hit in the same eye. The
// Returned roots belong to the same freshly proved primary body entry.
bool FrameBodyRoot(std::uintptr_t expectedPlayer,std::uint64_t pairId,
                   NativeCameraMath::CameraPose& nativeRoot,NativeCameraMath::CameraPose& renderedRoot);
struct ControllerAim {
    std::uintptr_t player=0;
    std::uint64_t pairId=0;
    std::uint32_t tick=0,recenterAck=0;
    float unitsPerMetre=100.f;
    NativeCameraMath::Vec3 origin{},direction{};
    std::uint32_t mappingTick=0; // owned camera world-mapping age, separate from physical input
};
// Fresh physical pointing even while native throw animation owns visual IK.
// Retains a <=250ms world mapping between GPU eyes; this grants no draw access.
// Native callers must separately prove current gameplay + owned Fire/action.
// Uses the same calibrated controller target as the normal solved firearm.
bool SnapshotControllerAim(unsigned side,ControllerAim& output);
// Last successfully published tracked solve, retained when controllers go
// inactive for inspection. Bone roles: upper, lower, wrist, twist0, twist1,
// upper muscle. Bone poses are model-space; roots/target/centre are world.
struct ArmDiagnostic {
    bool valid=false;
    std::uintptr_t player=0,model=0;
    std::uint64_t pairId=0,generation=0;
    std::uint32_t tick=0,trackingAgeMs=0;
    unsigned bone[6]{UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX};
    NativeCameraMath::CameraPose original[6]{},solved[6]{};
    NativeCameraMath::CameraPose rawRoot{},renderRoot{},target{},centre{};
    float controllerPosition[3]{},controllerOrientation[4]{},headPosition[3]{},cameraRawHeadOrientation[4]{};
    float residualTwistRadians=0.f,unitsPerMetre=100.f;
};
bool SnapshotArmDiagnostic(unsigned side,ArmDiagnostic& output);
Stats Snapshot();
} // namespace NativeHandsKL
