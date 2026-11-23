#pragma once
#include <array>
#include <cstdint>
struct IDirect3DDevice9;
#include "native_camera_math.hpp"
#include "native_scope_kl.hpp"
namespace NativeDrawContextKL {struct Context;}
namespace HandsIPC {struct Frame;}
namespace D3D9Capture {struct DrawCall;}
namespace NativeWeaponKL {
bool BackgroundReloadIntentReady();
void BackgroundReloadControls(HandsIPC::Frame& sample);
void ServiceReloadFeedback();
struct ReloadProp {
    NativeCameraMath::CameraPose pose{};
    NativeCameraMath::Vec3 half{};
    bool shell=false;
};
using ReloadProps=std::array<ReloadProp,4>;
bool ReloadPropsPending();
unsigned SnapshotReloadProps(std::uintptr_t player,ReloadProps& output);
void RecordReloadPropDraw(unsigned count);
void DrawDroppedMagazines(IDirect3DDevice9* device,const D3D9Capture::DrawCall& call);
bool ReloadHandTarget(std::uintptr_t player,unsigned side,const NativeCameraMath::CameraPose& gunWrist,NativeCameraMath::CameraPose& target);
using LogCallback=void(*)(const char*);
struct Stats {
    bool installed=false;
    std::uint64_t calls=0,tracked=0,passthrough=0;
    std::uintptr_t weapon=0;
    int hand=-1;
    std::uint64_t worldCalls=0,meshCandidates=0,meshOwned=0,meshTracked=0,meshLagged=0;
    std::uintptr_t meshOwner=0;
    std::uint64_t rigidCalls=0,rigidTracked=0;
    std::uint64_t meshFreshRejected=0;
    std::uint64_t stowedOwned=0,stowedTracked=0,stowedRejected=0,stowedHidden=0;
    std::uint64_t meshPoseRecovered=0;
    std::uint64_t fireCalls=0,fireTracked=0,fireRejected=0,muzzleCalls=0,muzzleTracked=0,aimRestored=0,aimRestoreRejected=0;
    std::uint32_t shotThread=0;
    float shotOrigin[3]{},shotDirection[3]{};
    std::uint64_t accessoryOwned=0,accessoryTracked=0,accessoryRejected=0;
    std::uint64_t hipAccessoryOwned=0,hipAccessoryTracked=0,hipAccessoryRejected=0;
    bool hipAttachmentVerified=false;std::uint64_t hipAccessorySlots=0;
    unsigned hipAccessoryPrimitive=255;
    std::uint64_t bodyChildCandidates=0,bodyChildOwned=0,bodyChildTracked=0,bodyChildRejected=0;
    std::uint64_t bodyGroupSlots=0,bodyGroupOwned=0,bodyGroupTracked=0,bodyGroupRejected=0;
    std::uint64_t hipRigidCandidates=0,hipRigidOwned=0,hipRigidTracked=0,hipRigidRejected=0;
    unsigned hipRigidBone=255;
    unsigned hipRigidStandard=255;
    bool hipRigidVerified=false;
    std::uint64_t cachedMuzzleTracked=0,cachedMuzzleConsumed=0,cachedMuzzleRestored=0,cachedMuzzleChanged=0;
    std::uint64_t viewRejected=0;
    std::uint64_t ownershipNegativeHits=0,ownershipNegativeStored=0;
    std::uint64_t barrelAxisAccepted=0,barrelAxisRejected=0;
    std::uint64_t calibrationRebuilds=0,calibrationRejected=0;
    std::uint64_t heldGrenadeMeshTracked=0,instanceWorldTracked=0,viewAlignedWorldTracked=0,bonesLightCacheInvalidated=0;
    std::uint64_t heldOwnerNonRoot=0,heldOwnerNonRootToRoot=0;
    bool worldSourceVerified=false,bonesLightCacheVerified=false;
    std::uint64_t materialSamples=0,materialWorldMismatches=0,materialShaderLoads=0,materialShaderCacheHits=0;
    float calibrationMaxScaleError=0;
    std::uint32_t barrelAxisCategory=0;
    float barrelAxisDeviationDegrees=0;
    std::uint32_t barrelAxisProofStage=0;
    float barrelAxisLocal[3]{};
    bool grenadeInstalled=false;
    std::uint64_t grenadeUpdates=0,grenadeLocalUpdates=0,grenadeTargets=0,grenadeGazeApplied=0,grenadeGazeRejected=0,grenadeContextRejected=0;
    std::uint64_t grenadeFireCalls=0,grenadeFireApplied=0,grenadeFireRejected=0;
    std::uint64_t grenadeFireBarrelAim=0,grenadeFireControllerAim=0;
    float grenadeRequestedDirection[3]{},grenadeNativeAim[3]{},grenadeNativeDelta[3]{},grenadeMappedDelta[3]{};
    std::uint32_t grenadeThread=0,grenadeProofStage=0,grenadeMoverVtable=0;
    float grenadeDirection[3]{};
    bool throwbackInstalled=false;
    bool smokeLoopInstalled=false;
    std::uint64_t smokeLoopReplacements=0,smokeLoopRetired=0,smokeLoopRejected=0;
    std::uint64_t throwbackUpdates=0,throwbackOwnedUpdates=0,throwbackLaunches=0,throwbackApplied=0,throwbackRejected=0;
    float throwbackNativeVelocity[3]{},throwbackMappedVelocity[3]{};
    bool scopeGestureEnabled=false,scopeGestureRequested=false;
    std::uint64_t scopeGestureTicks=0,scopeGestureEntered=0,scopeGestureExited=0,scopePrecisionForced=0,scopeGestureRejected=0;
    float scopeGestureDistanceMetres=0,scopeGestureAngleDegrees=0;
    std::uint64_t scopeFireCalls=0,scopeFireApplied=0,scopeFireRejected=0,scopeRayHits=0,scopeRayMisses=0,scopeRayUnavailable=0;
    std::uint64_t scopeDrawFrames=0,scopeDrawCandidates=0,scopeDrawSuppressed=0,scopeDrawRejected=0;
    bool scopeOverlayVerified=false;
    std::uint64_t scopeOverlayActivated=0,scopeOverlayRetained=0,scopeOverlayCleared=0,scopeOverlayRejected=0;
    unsigned scopeOverlayProofStage=0,scopeOverlayNativeMode=0;
    bool reloadInstalled=false;
    bool reloadAmmoProfile=false;std::uint64_t reloadAmmoObserved=0,reloadAmmoRejected=0;
    bool physicalDiagnostic=false,physicalLoaderReady=false;unsigned physicalLoader=0,physicalPhase=0,physicalEjected=0,physicalTaken=0,physicalInserted=0,physicalRejected=0,physicalRemapped=0,physicalHidden=0;
    unsigned physicalWorldDraws=0,physicalWorldRejected=0,physicalImpacts=0,physicalSounds=0,physicalNativeCues=0,physicalWorldActive=0,physicalSpareDraws=0,physicalDryClicks=0;
    int reloadAmmoCount=0,reloadAmmoReserve=0,reloadAmmoCapacity=0;unsigned reloadAmmoCategory=UINT32_MAX;
    std::uint64_t reloadRequests=0,reloadBlocked=0,reloadManual=0,reloadEmpty=0,reloadUnproved=0;
};
// Carries held firearm meshes with solved wrists, and exact native stowed
// body attachments with the rendered torso. Owned hand accessories follow
// the solved wrists; native firing uses the tracked muzzle/aim while keeping
// the original shot, spread and collision logic. No synthetic shots/input.
bool Install(bool enabled,LogCallback log);
bool InitializeScope(LogCallback log);
NativeScopeKL::State ReadScope();
bool ScopeOverlayActive();
// Render-thread only. Set once per eye before native WORLD binds. Only the
// current right-hand scoped rifle and its exact geometry descendants hide.
void SetScopedDrawFrame(std::uintptr_t player,std::uint64_t pairId,bool firstPerson);
void ClearScopedDrawFrame(bool preserveHeldPair=false);
bool SuppressScopedDraw();
// Exact local body-attached firearm meshes stay hidden in first person,
// including crouch; held weapons, other actors and native views are retained.
bool SuppressStowedDraw();
void SetScopeGestureEnabled(bool enabled);
// Restricts exact local autonomous reload requests even with an empty
// magazine. Explicit commands and unproved callers retain native logic.
void SetTrackedReloadEnabled(bool enabled);
void SetTrackedReloadContext(bool gameplay);
const char* ReloadActionText(std::uintptr_t player);
bool ReloadInstalled();
void SetPhysicalReloadEnabled(bool enabled);
bool ReloadHandBusy(std::uintptr_t player);
void ReloadSimulationTick(std::uintptr_t player);
enum class CalibrationContext { Gameplay,Transition,Unknown };
void SetWeaponCalibrationContext(CalibrationContext context);
// Throw animations deliberately hide firearm/scope visuals, but their
// exact incoming-grenade release still needs a fresh gameplay head frame.
void SetThrowbackContext(bool gameplay);
void SetScopeGestureContext(bool gameplay);
void InvalidateScopeGesture();
// Call only from NativeCover's owned simulation observer, before aim queries.
void ScopeSimulationTick(std::uintptr_t player);
using WorldBindObserver=void(*)(void* state,const NativeCameraMath::CameraPose* source,std::uint32_t callerRva);
// source=nullptr signals an inline rigid bind before its native WORLD math;
// observers retire previous per-draw tags and restore any temporary VIEW.
// callerRva=0x739E0 with source!=nullptr marks the final inline rigid WORLD.
bool SetWorldBindObserver(WorldBindObserver observer);
// Passive TLS binding tags only, including an explicit entry/state mismatch.
// Does not prove a draw or query camera/hands/held caches.
void AppendNativeDrawContext(NativeDrawContextKL::Context& output);
// Read-only diagnostics for successful tracked held-weapon WORLD bindings.
using ProjectionBindObserver=void(*)(std::uintptr_t,std::uintptr_t,unsigned);
void SetProjectionBindObserver(ProjectionBindObserver observer);
// Read-only current local held-item proof. This does not call back into the
// hands solver, so it is safe while preparing an owned GPU palette.
struct HeldSupport {
    std::uintptr_t player=0,weapon=0,root=0,model=0,marker=0,markerGeometry=0;
    std::uint32_t reference=0,category=0;
    NativeCameraMath::CameraPose socket{};
    bool candidate=false,active=false;
};
// allowInactive is for retaining a logical grip during native animation only;
// output.active normally gates visual IK; exact same-owned Reload is the
// deliberate visual-only exception. Native Fire still requires active hand.
bool CaptureHeldSupport(std::uintptr_t expectedPlayer,float units,HeldSupport& output,bool allowInactive=false);
bool PreferReloadSlide(const HeldSupport& held,const NativeCameraMath::CameraPose& gunWrist,
    const NativeCameraMath::CameraPose& freeWrist,const NativeCameraMath::CameraPose* palmLocal,std::uint32_t recenter);
struct AimFrame {
    std::uintptr_t player=0,weapon=0,root=0,model=0;
    std::uint64_t pairId=0;
    std::uint32_t tick=0,recenterAck=0;
    int hand=-1;
    float unitsPerMetre=100.f;
    NativeCameraMath::Vec3 origin{},direction{};
};
// Muzzle and aim of the visible owned gun, available before any shot.
bool SnapshotAim(AimFrame& output);
// Overlay-only muzzle of the firearm bound in the current owned GPU image.
// Unlike SnapshotAim this uses the admitted render pose, not a separately
// expiring simulation sample. It cannot authorize Fire or native queries.
bool SnapshotRenderAim(AimFrame& output);
// Presentation-only muzzle already bound into the current completed image.
// Remains readable after mesh/HUD cleanup, expires at the camera image
// boundary, and never permits Fire, collision queries or native pose edits.
bool SnapshotCaptureAim(AimFrame& output);
// Actual rendered HMD gaze, independent of controller/gun aim. The producer
// invalidates it for UI, tracking/recenter/runtime transitions, not each eye.
struct HeadGaze {
    std::uintptr_t player=0,sourceCamera=0;
    std::uint64_t pairId=0;
    std::uint32_t recenterAck=0,tick=0;
    float unitsPerMetre=100.f;
    NativeCameraMath::Vec3 origin{},direction{};
    bool mounted=false;
};
void PublishHeadGaze(const HeadGaze& gaze);
void InvalidateHeadGaze();
// Capability exists only on the native FF Fire callback while preparing an
// owned sniper shot. Rendering cannot authorize a native collision query.
bool ScopeQueryActive(std::uintptr_t player,std::uintptr_t weapon);
Stats Snapshot();
struct MaterialSample {
    bool valid=false,supportHeld=false,worldMatch=false;
    std::uint32_t tick=0,pass=0,vsHash=0,psHash=0,reference=0;
    std::uint64_t pair=0;
    std::uintptr_t entry=0,state=0,weapon=0,model=0,lightSource=0;
    NativeCameraMath::CameraPose sourceWorld{},trackedWorld{},actualWorld{},view{};
    float projection[16]{},mvp[16]{},eye[4]{},vsLights[28]{},psLights[28]{},nativeLights[28]{};
    unsigned mvpRegister=0,mvpCount=0,eyeRegister=0,eyeCount=0;
    unsigned vsLightRegister=0,vsLightCount=0,psLightRegister=0,psLightCount=0;
    bool vsLowLights=false,psLowLights=false;
    std::uint32_t renderState[9]{};
};
// Read-only owned held-firearm evidence,1 draw/500ms per held/unheld bucket.
// No palette lease assumption; actual pass WORLD/VS/PS/light/MVP are sampled.
void ObserveNativeDraw(IDirect3DDevice9* device);
bool SnapshotMaterial(bool supportHeld,MaterialSample& output);
// Current tracked held firearm only, native main-view PostFilterZPass.
// Stowed items, foreign objects, auxiliary views and old pairs cannot qualify.
bool PostfilterForegroundActive();
constexpr unsigned MaterialSequenceLimit=12;
struct MaterialSequenceDraw {
    std::uint32_t pass=0,vsHash=0,psHash=0,renderState[5]{};
    std::uintptr_t entry=0,geometry=0,renderTarget=0,depthTarget=0,textures[4]{};
    bool worldMatch=false;
};
struct MaterialSequence {
    bool valid=false,supportHeld=false;
    std::uint32_t tick=0,reference=0,count=0;
    std::uint64_t pair=0;
    std::uintptr_t weapon=0;
    MaterialSequenceDraw draws[MaterialSequenceLimit]{};
};
// At most 12 read-only COM samples from one pair per two seconds; captures
// the order/identities of certified held-gun draws, not all scene draws.
void ObserveMaterialSequence(IDirect3DDevice9* device);
bool SnapshotMaterialSequence(MaterialSequence& output);
void InvalidateMaterialProbe();
}
