#include "background_diagnostic.hpp"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "MinHook.h"
#include "native_weapon_kl.hpp"
#include "native_team_commands_kl_image.hpp"
#include "native_hands_kl.hpp"
#include "native_camera_kl.hpp"
#include "native_cover_kl.hpp"
#include "native_camera_math.hpp"
#include "native_weapon_math.hpp"
#include "native_weapon_ownership.hpp"
#include "native_two_hand_math.hpp"
#include "native_grenade_kl_math.hpp"
#include "native_grenade_kl_image.hpp"
#include "native_held_grenade_visual_kl.hpp"
#include "native_weapon_world_source.hpp"
#include "native_bones_light_cache.hpp"
#include "native_shader_constant_map.hpp"
#include "native_throwback_kl.hpp"
#include "native_smoke_loop_kl.hpp"
#include "native_aim_kl.hpp"
#include "native_action_kl.hpp"
#include "native_body_kl.hpp"
#include "native_scope_gesture_kl.hpp"
#include "native_scope_overlay_kl.hpp"
#include "native_reload_kl.hpp"
#include "native_reload_ammo_kl.hpp"
#include "reload_parts_kl.hpp"
#include "reload_contact_geometry_kl.hpp"
#include "native_revolver_reload_kl.hpp"
#include "shotgun_reload_kl.hpp"
#include "reload_body_pocket_kl.hpp"
#include "reload_tracking_pose_kl.hpp"
#include "reload_palm_kl.hpp"
#include "reload_presentation_kl.hpp"
#include "d3d9_capture.hpp"
#include "reload_audio_kl.hpp"
#include "reload_feedback_ipc.hpp"
#include "native_weapon_calibration_kl.hpp"
#include "native_tutorial_kl.hpp"
#include "hands_math.hpp"
#include "vr_bridge.hpp"
#include "profile.hpp"
#include "profile_timing.hpp"
#include <atomic>
#include <cstring>
#include <limits>
#include <vector>
#include <string>
#include <d3d9.h>

namespace NativeWeaponKL {
namespace {
using namespace NativeCameraMath;
// K&L ZItemWeapon vtable +0x2D4 points here. Its paired getters at +0x2D8
// (0x51E2C0) and +0x2DC (0x51DA90) confirm the native target at +0xC4.
constexpr std::uintptr_t kSetTarget=0x11DF40,kGameData=0x68760C,kHandVtable=0x5508B4;
constexpr std::uintptr_t kWorldBind=0x71770,kGpuIndex=0x7678A4,kRigidBind=0x739E0,kRigidVtable=0x541070;
constexpr std::uintptr_t kFireBase=0x123180,kFireFF=0x279D00,kAttachment=0x11F910;
constexpr std::uintptr_t kCachedMuzzleSetter=0x2795A0,kFFWeaponVtable=0x577B74;
using SetTarget=void(__thiscall*)(void*,const Vec3*);
using WorldBind=void(__thiscall*)(void*,const CameraPose*);
using RigidBind=void(__thiscall*)(void*,void*,std::uintptr_t);
using Fire=void(__thiscall*)(void*);
using Attachment=void(__thiscall*)(void*,Basis*,Vec3*);
using Owner=void*(__thiscall*)(void*);
using ThrowUpdate=bool(__thiscall*)(void*);
using LaunchProjectile=void(__thiscall*)(void*,const Vec3*,const Vec3*);
ThrowUpdate originalThrowbackUpdate=nullptr;
LaunchProjectile originalLaunchProjectile=nullptr;
thread_local NativeThrowbackKL::Owner throwbackScope;
std::atomic<bool> throwbackInstalled{false};
std::atomic<std::uint64_t> throwbackUpdates{0},throwbackOwnedUpdates{0},throwbackLaunches{0},throwbackApplied{0},throwbackRejected{0};
std::atomic<float> throwbackNativeVelocity[3]{},throwbackMappedVelocity[3]{};
using SmokeEvent=std::uint32_t(__thiscall*)(void*,std::uint32_t,void*);
using SmokeLookup=void*(__thiscall*)(void*,std::uint32_t);
using SmokeFade=bool(__thiscall*)(void*,float,float,float);
SmokeEvent originalSmokeEvent=nullptr;
std::atomic<bool> smokeLoopInstalled{false};
std::atomic<std::uint64_t> smokeLoopReplacements{0},smokeLoopRetired{0},smokeLoopRejected{0};
using Precision=void(__thiscall*)(void*,bool);
Precision originalPrecision=nullptr;
using OverlayActivate=void(__thiscall*)(void*,unsigned);
using OverlayClear=void(__thiscall*)(void*);
OverlayActivate activateScopeOverlay=nullptr;
OverlayClear originalOverlayClear=nullptr;
std::atomic<bool> scopeOverlayVerified{false};
std::atomic<std::uint64_t> scopeOverlayActivated{0},scopeOverlayRetained{0},scopeOverlayCleared{0},scopeOverlayRejected{0};
std::atomic<unsigned> scopeOverlayProofStage{0},scopeOverlayNativeMode{0};
std::atomic<bool> scopeGestureEnabled{false},scopeGestureContext{false};
std::atomic<bool> throwbackGameplayContext{false};
std::atomic<std::uint64_t> scopeGestureEpoch{1};
std::atomic<std::uint64_t> scopeGestureTicks{0},scopeGestureEntered{0},scopeGestureExited{0},scopePrecisionForced{0},scopeGestureRejected{0};
std::atomic<float> scopeGestureDistance{0},scopeGestureAngle{0};
std::atomic<bool> scopeGestureRequested{false};
struct ScopeGestureOwner {NativeScopeKL::State optic{};std::uint32_t recenter=0,tick=0;std::uint64_t epoch=0;bool requested=false;};
SRWLOCK scopeGestureLock=SRWLOCK_INIT;ScopeGestureOwner scopeGestureOwner;
struct ScopeOverlayOwner {ScopeGestureOwner gesture{};NativeScopeOverlayKL::State native{};bool ours=false;};
SRWLOCK scopeOverlayLock=SRWLOCK_INIT;ScopeOverlayOwner scopeOverlayOwner;
SetTarget original=nullptr;
SetTarget setCachedMuzzle=nullptr;
ThrowUpdate originalThrowUpdate=nullptr;
WorldBind originalWorld=nullptr;
std::atomic<WorldBindObserver> worldBindObserver{nullptr};
std::atomic<bool> worldHookInstalled{false};
std::atomic<std::uintptr_t> scopeImage{0};
RigidBind originalRigid=nullptr;
Fire originalFireBase=nullptr,originalFireFF=nullptr;
Attachment originalAttachment=nullptr;
std::uintptr_t image=0;
std::atomic<bool> installed{false};
using ReloadRequest=void(__thiscall*)(void*,void*);
ReloadRequest originalReload=nullptr;
std::atomic<bool> reloadInstalled{false},trackedReloadEnabled{false},trackedReloadContext{false};
std::atomic<std::uint64_t> reloadRequests{0},reloadBlocked{0},reloadManual{0},reloadEmpty{0},reloadUnproved{0};
NativeReloadKL::IntentArbiter reloadIntent;
std::atomic<bool> reloadAmmoProfile{false};
std::atomic<std::uint64_t> reloadAmmoObserved{0},reloadAmmoRejected{0};
std::atomic<int> reloadAmmoCount{0},reloadAmmoReserve{0},reloadAmmoCapacity{0};
std::atomic<unsigned> reloadAmmoCategory{UINT32_MAX};
PhysicalReloadRuntimeKL::Engine reloadEngine;
bool supportTypeProof=false;
bool hipAttachmentProof=false;
bool hipRigidProof=false;
std::atomic<std::uint64_t> calls{0},tracked{0},passthrough{0};
std::atomic<std::uintptr_t> lastWeapon{0};
std::atomic<int> lastHand{-1};
std::atomic<std::uint64_t> worldCalls{0},meshCandidates{0},meshOwned{0},meshTracked{0},meshLagged{0},meshPoseRecovered{0};
std::atomic<std::uintptr_t> lastMeshOwner{0};
std::atomic<std::uint64_t> rigidCalls{0},rigidTracked{0};
std::atomic<std::uint64_t> meshFreshRejected{0};
std::atomic<std::uint64_t> stowedOwned{0},stowedTracked{0},stowedRejected{0},stowedHidden{0};
std::atomic<std::uint64_t> accessoryOwned{0},accessoryTracked{0},accessoryRejected{0};
std::atomic<std::uint64_t> hipAccessoryOwned{0},hipAccessoryTracked{0},hipAccessoryRejected{0};
std::atomic<std::uint64_t> hipAccessorySlots{0};
std::atomic<unsigned> hipAccessoryPrimitive{255};
std::atomic<std::uint64_t> bodyChildCandidates{0},bodyChildOwned{0},bodyChildTracked{0},bodyChildRejected{0};
std::atomic<std::uint64_t> bodyGroupSlots{0},bodyGroupOwned{0},bodyGroupTracked{0},bodyGroupRejected{0};
std::atomic<std::uint64_t> hipRigidCandidates{0},hipRigidOwned{0},hipRigidTracked{0},hipRigidRejected{0};
std::atomic<unsigned> hipRigidBone{255};
std::atomic<unsigned> hipRigidStandard{255};
std::atomic<std::uint64_t> fireCalls{0},fireTracked{0},fireRejected{0},muzzleCalls{0},muzzleTracked{0},aimRestored{0},aimRestoreRejected{0};
std::atomic<std::uint64_t> cachedMuzzleTracked{0},cachedMuzzleConsumed{0},cachedMuzzleRestored{0},cachedMuzzleChanged{0};
std::atomic<std::uint64_t> viewRejected{0};
std::atomic<std::uint64_t> ownershipNegativeHits{0},ownershipNegativeStored{0};
std::atomic<std::uint64_t> barrelAxisAccepted{0},barrelAxisRejected{0};
std::atomic<std::uint64_t> calibrationRebuilds{0},calibrationRejected{0};
std::atomic<std::uint64_t> heldOwnerNonRoot{0},heldOwnerNonRootToRoot{0};
std::atomic<float> calibrationMaxScaleError{0};
bool worldSourceProof=false,bonesLightCacheProof=false;
std::atomic<std::uint64_t> instanceWorldTracked{0},viewAlignedWorldTracked{0},bonesLightCacheInvalidated{0};
std::atomic<std::uint64_t> heldGrenadeMeshTracked{0};
std::atomic<std::uint32_t> barrelAxisCategory{0};
std::atomic<float> barrelAxisDeviationDegrees{0};
std::atomic<std::uint32_t> barrelAxisProofStage{0};
std::atomic<float> barrelAxisLocal[3]{};
std::atomic<bool> grenadeInstalled{false};
std::atomic<std::uint64_t> grenadeUpdates{0},grenadeLocalUpdates{0},grenadeTargets{0},grenadeGazeApplied{0},grenadeGazeRejected{0},grenadeContextRejected{0};
std::atomic<std::uint64_t> grenadeFireCalls{0},grenadeFireApplied{0},grenadeFireRejected{0};
std::atomic<std::uint64_t> grenadeFireBarrelAim{0},grenadeFireControllerAim{0};
std::atomic<float> grenadeRequestedDirection[3]{},grenadeNativeAim[3]{},grenadeNativeDelta[3]{},grenadeMappedDelta[3]{};
std::atomic<DWORD> grenadeThread{0};
std::atomic<std::uint32_t> grenadeProofStage{0},grenadeMoverVtable{0};
std::atomic<float> grenadeDirection[3]{};
std::atomic<std::uint64_t> scopeFireCalls{0},scopeFireApplied{0},scopeFireRejected{0},scopeRayHits{0},scopeRayMisses{0},scopeRayUnavailable{0};
std::atomic<std::uint64_t> scopeDrawFrames{0},scopeDrawCandidates{0},scopeDrawSuppressed{0},scopeDrawRejected{0};
struct ScopedDraw {
    NativeScopeKL::State optic{};
    std::uint64_t pair=0,generation=0;
    std::uintptr_t state=0,entry=0,geometry=0;
    CameraPose world{};
    bool active=false,tag=false,candidate=false;
    unsigned draws=0;
};
thread_local ScopedDraw scopedDraw;
struct ScopeQuery {std::uintptr_t player=0,weapon=0;};
thread_local ScopeQuery scopeQuery;
SRWLOCK headGazeLock=SRWLOCK_INIT;
std::atomic<std::uint64_t> headGazeEpoch{1};
struct PublishedGaze {HeadGaze gaze{};Vec3 horizontal{};std::uint64_t epoch=0;};
PublishedGaze publishedHeadGaze;
struct GrenadeScope {
    bool active=false;
    std::uintptr_t action=0,player=0,item=0,root=0,model=0;
    std::uint32_t reference=0;
};
thread_local GrenadeScope grenadeScope;
std::atomic<DWORD> shotThread{0};
std::atomic<float> shotOrigin[3]{},shotDirection[3]{};
thread_local bool inside=false;
thread_local bool insideWorld=false;
thread_local bool insideRigid=false;
struct HeldCache {
    std::uintptr_t player=0,weapon[2]{},stowedWeapon[NativeWeaponOwnership::MaximumAttachments]{};
    std::uintptr_t handGroup[NativeWeaponOwnership::MaximumAttachments]{};
    std::uintptr_t hipGroup[NativeWeaponOwnership::MaximumAttachments]{};
    std::uintptr_t bodyGroup[NativeWeaponOwnership::MaximumAttachments]{};
    std::uintptr_t bodyRoot=0,bodyModel=0;
    NativeWeaponOwnership::Snapshot attachments{};
    NativeHeldGrenadeVisualKL::Owner grenade{};
    std::uint32_t weaponReference[2]{};
    std::uint64_t signature=0;
    DWORD tick=0;
};
thread_local HeldCache heldCache;
struct StowedDraw {
    bool valid=false;
    std::uintptr_t state=0,entry=0,player=0,item=0,root=0;
    std::uint32_t reference=0;
    unsigned slot=0;
    std::uint64_t pair=0,generation=0;
    NativeWeaponOwnership::Snapshot attachments{};
    CameraPose world{};
};
thread_local StowedDraw stowedPending,stowedBound;
struct HeldDrawPose {
    std::uint64_t pair=0;
    std::uintptr_t player=0,item=0,root=0,model=0;
    std::uint32_t reference=0,recenter=0;
    bool available=false,recovered=false,recoveredSupport=false;
    NativeHandsKL::VisualFrame hands{};
    CameraPose heldItem{};
};
thread_local HeldDrawPose heldDrawPose[2];
thread_local NativeWeaponCalibrationKL::SettledGrip settledGrip[2];
thread_local NativeWeaponCalibrationKL::Key settledBinding[2];
thread_local std::uint64_t settledCandidateGeneration[2]{};
// Ownership traversal below depends on geometry/held identities, not the
// material entry. Entry-specific rigid-hip handling runs before this memo.
thread_local NativeWeaponOwnership::NegativeDrawMemo negativeDrawMemo{true};
struct MaterialBinding {
    bool valid=false,supportHeld=false;std::uintptr_t state=0,entry=0,player=0,item=0,model=0,root=0;
    std::uint32_t reference=0;int side=-1;std::uint64_t pair=0,generation=0;
    CameraPose source{},tracked{};
};
thread_local MaterialBinding materialPending,materialBound;
std::atomic<ProjectionBindObserver> projectionBindObserver{nullptr};
void ObserveProjectionBinding(const MaterialBinding& bound){
    if(const auto observer=projectionBindObserver.load(std::memory_order_relaxed)){
        std::uintptr_t entry=0;unsigned pass=0;
        if(NativeHandsKL::ReadCurrentNativeEntry(bound.state,entry,&pass)&&entry==bound.entry&&pass==9)
            observer(bound.state,bound.entry,1);
    }
}
struct MaterialShader {IUnknown* shader=nullptr;std::uint32_t hash=0;NativeShaderConstantMap::Map map{};};
thread_local std::vector<MaterialShader> materialShaders;
thread_local IDirect3DDevice9* materialDevice=nullptr;
thread_local DWORD materialProbeTick[2]{};
SRWLOCK materialLock=SRWLOCK_INIT;
MaterialSample materialSamples[2];
SRWLOCK materialSequenceLock=SRWLOCK_INIT;
MaterialSequence materialSequence;
thread_local DWORD materialSequenceTick=0;
thread_local std::uint64_t materialSequencePair=0;
thread_local unsigned materialSequenceCount=0;
std::atomic<std::uint64_t> materialSampleCount{0},materialWorldMismatches{0},materialShaderLoads{0},materialShaderCacheHits{0};
struct ShotContext {
    bool active=false;
    bool headScope=false;
    std::uintptr_t weapon=0,root=0,player=0;
    int side=-1;
    NativeHandsKL::VisualFrame hands{};
    CameraPose attachment{};
    Vec3 savedTarget{},target{},origin{};
    NativeWeaponMath::PendingMuzzle cachedMuzzle{};
};
thread_local ShotContext shot;
struct RenderedGun {
    ShotContext shot{};
    NativeWeaponMath::GripCalibration calibration{};
    CameraPose attachment{},nativeAttachment{};
    bool aligned=false;
    std::uintptr_t model=0;
    std::uint32_t reference=0;
    std::uintptr_t marker=0,markerGeometry=0;
    std::uint32_t category=0;
    CameraPose localMuzzle{};
    DWORD tick=0;
    std::uint64_t generation=0,calibrationEpoch=0;
    // The draw lease can end before capture (HUD/secondary camera cleanup).
    // This separate certificate names only the image containing this gun.
    std::uint64_t imageEpoch=0;
    unsigned imageEye=2;
};
bool StampRenderedImage(RenderedGun& gun) {
    std::uint64_t epoch=0;unsigned eye=2;
    if(!NativeCameraKL::ReadRenderContext(epoch,eye)||!epoch||eye>1)return false;
    const bool changed=gun.imageEpoch!=epoch||gun.imageEye!=eye;
    gun.imageEpoch=epoch;gun.imageEye=eye;return changed;
}
std::atomic<std::uint64_t> calibrationEpoch{1};
std::atomic<std::uint64_t> calibrationCandidateEpoch{1};
std::atomic<bool> calibrationTransition{false};
std::atomic<bool> calibrationGameplay{true};
SRWLOCK renderedGunLock=SRWLOCK_INIT;
RenderedGun renderedGun[2];
template<class T> bool Read(std::uintptr_t address,T& value) {
    SIZE_T bytes=0;return address&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),
        &value,sizeof(value),&bytes)&&bytes==sizeof(value);
}
bool ReadBytes(std::uintptr_t address,void* value,std::size_t size) {
    SIZE_T bytes=0;return NativeWeaponOwnership::Address(address,size)&&
        ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),value,size,&bytes)&&bytes==size;
}
bool RecoverHeldItem(HeldDrawPose& frame,unsigned side) {
    const auto action=NativeActionKL::Current();
    using Movement=NativeActionKL::Movement;
    if(!calibrationGameplay.load()||calibrationTransition.load()||!action.valid||action.player!=frame.player||action.action||
       (action.movementKind!=Movement::Stand&&action.movementKind!=Movement::BaseMove&&action.movementKind!=Movement::Cover))
        return false;
    NativeHandsKL::DrawController controller;
    if(!NativeHandsKL::SnapshotDrawController(frame.player,frame.pair,side,controller)||controller.recenter!=frame.recenter)
        return false;
    AcquireSRWLockShared(&renderedGunLock);const auto previous=renderedGun[side];ReleaseSRWLockShared(&renderedGunLock);
    const auto& hands=previous.shot.hands;
    if(!previous.aligned||!previous.calibration.valid||previous.calibrationEpoch!=calibrationEpoch.load()||
       previous.shot.player!=frame.player||previous.shot.weapon!=frame.item||previous.shot.root!=frame.root||
       previous.model!=frame.model||previous.reference!=frame.reference||hands.recenterAck!=frame.recenter||
       hands.unitsPerMetre!=controller.units||!hands.tracked[side]||hands.pairId>=frame.pair)return false;
    CameraPose wrist=hands.finalWrist[side];
    const bool solvedSupport=controller.coupledItem==frame.item&&controller.coupledRoot==frame.root&&
        controller.coupledModel==frame.model&&controller.coupledReference==frame.reference;
    if(solvedSupport){
        wrist=controller.coupledWrist;
    }else if(controller.tracked&&hands.supportHeld){
        // A single controller can translate the last coupled pose, but its
        // rotation cannot replace the direction set by both hands.
        wrist.position=Add(wrist.position,Subtract(controller.wrist.position,hands.controllerWrist[side].position));
    }else if(controller.tracked){
        if(!NativeWeaponMath::Remap(hands.controllerWrist[side],controller.wrist,wrist,wrist))return false;
    }else{
        // During a brief loss of the controller, hold the last carried pose
        // relative to head translation. Never extend this with recovered draws.
        if(GetTickCount()-previous.tick>150)return false;
        wrist.position=Add(wrist.position,Subtract(controller.centre.position,hands.centre.position));
    }
    frame.recoveredSupport=solvedSupport||hands.supportHeld;
    return NativeWeaponMath::Remap(previous.calibration.wrist,wrist,previous.calibration.attachment,frame.heldItem);
}
bool SnapshotHeldDrawPose(std::uint64_t pair,std::uintptr_t player,std::uintptr_t item,
                         std::uintptr_t root,std::uintptr_t model,std::uint32_t reference,
                         unsigned side,NativeHandsKL::VisualFrame& hands,const CameraPose*& recovered) {
    std::uint64_t epoch=0;unsigned eye=2;
    if(!NativeCameraKL::ReadRenderContext(epoch,eye)||!epoch||eye>1)
        return NativeHandsKL::SnapshotDrawVisual(hands);
    const auto recenter=static_cast<std::uint32_t>(VRBridge::RecenterAck());
    auto& frame=heldDrawPose[side];
    if(frame.pair!=pair||frame.player!=player||
       frame.item!=item||frame.root!=root||frame.model!=model||frame.reference!=reference||frame.recenter!=recenter) {
        frame={pair,player,item,root,model,reference,recenter};
        // Use the first owned draw for the physical pair. Both eye renders
        // must agree even if a later wrist snapshot is unavailable.
        frame.available=NativeHandsKL::SnapshotDrawVisual(frame.hands)&&
            frame.hands.player==player&&frame.hands.pairId==pair&&frame.hands.availableForMesh[side];
        if(!frame.available||!frame.hands.tracked[side])
            frame.recovered=RecoverHeldItem(frame,side);
    }
    if(frame.available)hands=frame.hands;
    if(frame.recovered){recovered=&frame.heldItem;hands.supportHeld=frame.recoveredSupport;}
    return frame.available;
}
bool SameScopeOwner(const NativeScopeKL::State& a,const NativeScopeKL::State& b) {
    return a.valid&&b.valid&&a.player==b.player&&a.aim==b.aim&&a.sourceCamera==b.sourceCamera&&
        a.item==b.item&&a.root==b.root&&a.model==b.model&&a.reference==b.reference&&b.category==15&&b.hand==1;
}
bool ScopedEntry(std::uintptr_t entry,std::uintptr_t& observedGeometry) {
    std::uintptr_t vt=0,geometry=0;
    if(!Read(entry,vt)||!Read(entry+0xF4,geometry)||!geometry)return false;
    const auto rva=vt-image;
    if(rva!=5508832&&rva!=5509172&&rva!=5509232&&rva!=5509112)return false;
    observedGeometry=geometry;
    std::uintptr_t seen[8]{};
    for(unsigned depth=0;depth<8&&geometry;++depth) {
        for(unsigned previous=0;previous<depth;++previous)if(seen[previous]==geometry)return false;
        seen[depth]=geometry;NativeWeaponOwnership::GeometryLinks links;
        if(!NativeWeaponOwnership::ReadGeometryLinks(geometry,ReadBytes,links))return false;
        if(geometry==scopedDraw.optic.root)return links.owner==scopedDraw.optic.item;
        geometry=links.parent;
    }
    return false;
}
void ObserveScopedWorld(void* state,const CameraPose* source) {
    scopedDraw.tag=false;scopedDraw.draws=0;
    if(!scopedDraw.active||!source)return;
    const auto index=*reinterpret_cast<const volatile std::uint32_t*>(image+kGpuIndex);
    const auto address=reinterpret_cast<std::uintptr_t>(source);
    if(index>1||address<0x10000+0x10+index*sizeof(CameraPose))return;
    const auto entry=address-0x10-index*sizeof(CameraPose);std::uintptr_t geometry=0;
    // The normal held-mesh walk already identified each scene entry. Avoid
    // another RPM ancestry walk for unrelated objects while zoomed.
    if(installed.load()&&(!scopedDraw.candidate||scopedDraw.entry!=entry))return;
    if(!ScopedEntry(entry,geometry))return;
    ++scopeDrawCandidates;
    bool firstPerson=false;std::uint64_t pair=0,generation=0;CameraPose world;
    if(!NativeCameraKL::DrawStateActive(reinterpret_cast<std::uintptr_t>(state),&firstPerson)||!firstPerson||
       !NativeHandsKL::DrawFrameActive(pair,&generation)||pair!=scopedDraw.pair||
       !Read(reinterpret_cast<std::uintptr_t>(state)+0x8C,world)||!ValidatePose(world,.05f)) {
        ++scopeDrawRejected;return;
    }
    scopedDraw.entry=entry;scopedDraw.geometry=geometry;scopedDraw.state=reinterpret_cast<std::uintptr_t>(state);
    scopedDraw.generation=generation;scopedDraw.world=world;scopedDraw.tag=true;
}
bool SamePrecisionMover(const NativeScopeKL::State& previous) {
    std::uintptr_t data=0,player=0,mover=0,owner=0,vtable=0;
    return previous.valid&&previous.player&&previous.aim&&Read(image+kGameData,data)&&Read(data+0xA2C,player)&&
        player==previous.player&&Read(player+0x9AC,mover)&&mover==previous.aim&&
        Read(mover,vtable)&&NativeCameraMoverKL::KnownVtable(image,vtable)&&Read(mover+8,owner)&&owner==player;
}
bool RetainScopeOverlay(const ScopeOverlayOwner& owner,std::uintptr_t logic) {
    if(!owner.ours||owner.native.logic!=logic||!scopeGestureEnabled||!scopeGestureContext||
       owner.gesture.epoch!=scopeGestureEpoch||GetTickCount()-owner.gesture.tick>HandsIPC::kFreshMs||
       owner.gesture.recenter!=static_cast<std::uint32_t>(VRBridge::RecenterAck())||
       !VRBridge::Ready()||NativeCameraKL::CinematicActive())return false;
    const auto optic=ReadScope();
    if(!optic.active||!SameScopeOwner(owner.gesture.optic,optic))return false;
    const auto current=NativeScopeOverlayKL::Capture(image,optic,ReadBytes);
    return NativeScopeOverlayKL::Same(owner.native,current)&&current.active&&current.mode==1;
}
void __fastcall OverlayClearHook(void* logic,void*) {
    ScopeOverlayOwner owner;AcquireSRWLockShared(&scopeOverlayLock);owner=scopeOverlayOwner;ReleaseSRWLockShared(&scopeOverlayLock);
    // Native mode13 would remove the authored optic every update. Retain
    // only the exact variant that this still-fresh near-face request opened.
    // Other cameras, replaced native overlays and all retired requests pass.
    if(RetainScopeOverlay(owner,reinterpret_cast<std::uintptr_t>(logic))){++scopeOverlayRetained;return;}
    originalOverlayClear(logic);
}
void ReleaseScopeOverlay(const ScopeOverlayOwner& owner) {
    if(!owner.ours||!originalOverlayClear)return;
    const auto current=NativeScopeOverlayKL::Capture(image,owner.gesture.optic,ReadBytes);
    if(NativeScopeOverlayKL::Same(owner.native,current)&&current.active&&current.mode==1) {
        originalOverlayClear(reinterpret_cast<void*>(current.logic));++scopeOverlayCleared;
    }
}
void ScopeOverlayTick(const ScopeGestureOwner& gesture,bool requested) {
    if(!scopeOverlayVerified||!activateScopeOverlay||!originalOverlayClear)return;
    ScopeOverlayOwner previous;AcquireSRWLockExclusive(&scopeOverlayLock);previous=scopeOverlayOwner;scopeOverlayOwner={};ReleaseSRWLockExclusive(&scopeOverlayLock);
    const auto optic=ReadScope();
    if(!requested||!optic.active||!SameScopeOwner(gesture.optic,optic)) {ReleaseScopeOverlay(previous);return;}
    auto current=NativeScopeOverlayKL::Capture(image,optic,ReadBytes);
    scopeOverlayProofStage=current.stage;scopeOverlayNativeMode=current.mode;
    if(!current.valid){++scopeOverlayRejected;ReleaseScopeOverlay(previous);return;}
    if(previous.ours&&!NativeScopeOverlayKL::Same(previous.native,current))ReleaseScopeOverlay(previous);
    bool ours=previous.ours&&NativeScopeOverlayKL::Same(previous.native,current)&&current.active&&current.mode==1;
    if(current.mode==0&&!current.active) {
        // Execute the original native camera lifecycle on the simulation
        // thread. It installs the existing Scope_Dragunov geometry/filter;
        // no synthetic screen tint or replacement crosshair is rendered.
        activateScopeOverlay(reinterpret_cast<void*>(current.logic),1);
        const auto after=NativeScopeOverlayKL::Capture(image,optic,ReadBytes);
        if(!NativeScopeOverlayKL::Same(current,after)||!after.active||after.mode!=1) {++scopeOverlayRejected;return;}
        current=after;ours=true;++scopeOverlayActivated;
    }
    if(!ours)return; // An unrelated native effect is never claimed/cleared.
    ScopeOverlayOwner next;next.gesture=gesture;next.gesture.optic=optic;next.native=current;next.ours=true;
    AcquireSRWLockExclusive(&scopeOverlayLock);scopeOverlayOwner=next;ReleaseSRWLockExclusive(&scopeOverlayLock);
}
void __fastcall PrecisionHook(void* mover,void*,bool nativeValue) {
    ScopeGestureOwner owner;AcquireSRWLockShared(&scopeGestureLock);owner=scopeGestureOwner;ReleaseSRWLockShared(&scopeGestureLock);
    if(scopeGestureEnabled&&scopeGestureContext&&owner.epoch==scopeGestureEpoch&&owner.optic.aim==reinterpret_cast<std::uintptr_t>(mover)&&
       GetTickCount()-owner.tick<=HandsIPC::kFreshMs&&VRBridge::Ready()&&!NativeCameraKL::CinematicActive()&&
       static_cast<std::uint32_t>(VRBridge::RecenterAck())==owner.recenter&&SameScopeOwner(owner.optic,ReadScope())) {
        nativeValue=owner.requested;++scopePrecisionForced;
    }
    originalPrecision(mover,nativeValue);
}
void __fastcall WorldBindHook(void* state,void*,const CameraPose* source);
bool VerifiedImage() {
    if(image)return true;
    wchar_t path[MAX_PATH]{};char digest[65]{};const auto length=GetModuleFileNameW(nullptr,path,MAX_PATH);
    if(!length||length>=MAX_PATH||!HashFile(path,digest)||!IsSupportedExecutableHash(digest))return false;
    image=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));return image!=0;
}
bool EnsureWorldHook() {
    if(worldHookInstalled)return originalWorld!=nullptr;
    if(!VerifiedImage())return false;
    const unsigned char expected[]={0x53,0x8B,0xD9,0x56,0x8B,0x74,0x24,0x0C,0x57,
        0x8D,0x93,0x8C,0,0,0,0x8B,0xFA,0xB9,0x0C,0,0,0,0xF3,0xA5};
    if(std::memcmp(reinterpret_cast<const void*>(image+kWorldBind),expected,sizeof(expected)))return false;
    const auto initialized=MH_Initialize();if(initialized!=MH_OK&&initialized!=MH_ERROR_ALREADY_INITIALIZED)return false;
    void* target=reinterpret_cast<void*>(image+kWorldBind);
    if(MH_CreateHook(target,reinterpret_cast<void*>(&WorldBindHook),reinterpret_cast<void**>(&originalWorld))!=MH_OK)return false;
    if(MH_EnableHook(target)!=MH_OK){MH_RemoveHook(target);originalWorld=nullptr;return false;}
    worldHookInstalled=true;return true;
}
bool GrenadeItem(std::uintptr_t player,std::uintptr_t item,GrenadeScope& output,bool allowNativeOwner=false) {
    using namespace NativeGrenadeKLImage;
    std::uintptr_t vtable=0,root=0,owner=0,model=0,templateVtable=0;
    std::uint32_t reference=0,category=0;
    if(!Read(item,vtable)||vtable!=image+FFWeaponVtable||!Read(item+4,root)||
       !Read(root+0x60,owner)||owner!=item||!Read(item+0x58,reference)||
       !Read(item+0xC0,model)||!Read(model,templateVtable)||
       (templateVtable!=image+0x54E6D4&&templateVtable!=image+0x577864)||
       !Read(model+NativeWeaponTypeKL::CategoryOffset,category)||(category!=11&&category!=12))return false;
    bool held=false;
    for(unsigned side=0;side<2;++side) {
        const auto hand=player+(side?0x358:0x2D8);std::uintptr_t handVtable=0;
        NativeWeaponOwnership::HandState state;
        if(Read(hand,handVtable)&&handVtable==image+kHandVtable&&
           NativeWeaponOwnership::ReadHandState(hand,ReadBytes,state)&&state.enabled&&state.item==item)held=true;
    }
    if(!held) {
        std::uintptr_t ownerMethod=0;
        if(!allowNativeOwner||!Read(vtable+0x290,ownerMethod)||ownerMethod!=image+0x11D230||
           reinterpret_cast<Owner>(ownerMethod)(reinterpret_cast<void*>(item))!=reinterpret_cast<void*>(player))return false;
    }
    output.player=player;output.item=item;output.root=root;output.model=model;output.reference=reference;return true;
}
bool GrenadeAction(std::uintptr_t action,GrenadeScope& output) {
    const auto state=NativeActionKL::Current();std::uintptr_t actor=0,vtable=0;
    if(!state.valid||state.action!=action||state.actionVtable!=image+NativeGrenadeKLImage::ThrowVtable||
       !Read(action+8,actor)||actor!=state.player||!Read(actor,vtable)||vtable!=image+NativeGrenadeKLImage::PlayerVtable)return false;
    // Item setup can finish inside this update. Bind the actual native item
    // only at its proved target call, after the game selected it in 5DC2F0.
    output={};output.active=true;output.action=action;output.player=actor;return true;
}
bool ReadHeadGaze(PublishedGaze& output) {
    const auto epoch=headGazeEpoch.load();
    if(!TryAcquireSRWLockShared(&headGazeLock))return false;
    const auto next=publishedHeadGaze;ReleaseSRWLockShared(&headGazeLock);
    if(next.epoch!=epoch||headGazeEpoch.load()!=epoch||
       !NativeGrenadeKLMath::ValidGaze(next.gaze,GetTickCount())||
       !VRBridge::Ready()||static_cast<std::uint32_t>(VRBridge::RecenterAck())!=next.gaze.recenterAck)return false;
    output=next;return true;
}
bool __fastcall ThrowbackUpdateHook(void* object,void*) {
    ++throwbackUpdates;
    const auto previous=throwbackScope;throwbackScope={};
    struct Restore {NativeThrowbackKL::Owner previous;~Restore(){throwbackScope=previous;}} restore{previous};
    if(NativeThrowbackKL::Capture(image,NativeActionKL::Current(),reinterpret_cast<std::uintptr_t>(object),ReadBytes,throwbackScope))
        ++throwbackOwnedUpdates;
    return originalThrowbackUpdate(object);
}
void LaunchThrowback(void* object,const Vec3* velocity,const Vec3* angular,std::uintptr_t caller) {
    if(!throwbackScope.action||caller!=image+NativeThrowbackKL::LaunchReturn) {
        originalLaunchProjectile(object,velocity,angular);return;
    }
    ++throwbackLaunches;
    NativeThrowbackKL::Owner current,after;PublishedGaze view;Vec3 native,mapped;
    const bool valid=throwbackGameplayContext&&VRBridge::Ready()&&!VRBridge::TeamHudVisible()&&!VRBridge::WeaponMenuVisible()&&
        !NativeCameraKL::CinematicActive()&&reinterpret_cast<std::uintptr_t>(object)==throwbackScope.projectile&&
        NativeThrowbackKL::Capture(image,NativeActionKL::Current(),throwbackScope.action,ReadBytes,current)&&
        NativeThrowbackKL::Same(current,throwbackScope)&&ReadHeadGaze(view)&&view.gaze.player==current.player&&
        ReadBytes(reinterpret_cast<std::uintptr_t>(velocity),&native,sizeof(native))&&
        NativeThrowbackKL::Velocity(native,view.gaze,view.horizontal,mapped)&&
        NativeThrowbackKL::Capture(image,NativeActionKL::Current(),throwbackScope.action,ReadBytes,after)&&
        NativeThrowbackKL::Same(current,after)&&headGazeEpoch.load()==view.epoch&&
        static_cast<std::uint32_t>(VRBridge::RecenterAck())==view.gaze.recenterAck;
    if(!valid){++throwbackRejected;originalLaunchProjectile(object,velocity,angular);return;}
    const float before[]={native.x,native.y,native.z},result[]={mapped.x,mapped.y,mapped.z};
    for(unsigned i=0;i<3;++i){throwbackNativeVelocity[i]=before[i];throwbackMappedVelocity[i]=result[i];}
    // Stack-local argument replacement only. The original physics launcher
    // still consumes angular velocity/fuse/origin and performs every effect.
    originalLaunchProjectile(object,&mapped,angular);++throwbackApplied;
}
void __fastcall LaunchProjectileHook(void* object,void*,const Vec3* velocity,const Vec3* angular) {
    LaunchThrowback(object,velocity,angular,reinterpret_cast<std::uintptr_t>(__builtin_return_address(0)));
}
void InstallThrowback(LogCallback log) {
    if(!NativeThrowbackKL::Verify(image,ReadBytes)) {
        if(log)log("Native K&L throw-back: action/physics release signatures differ; incoming grenades remain native.");return;
    }
    void* update=reinterpret_cast<void*>(image+NativeThrowbackKL::Update);
    void* launch=reinterpret_cast<void*>(image+NativeThrowbackKL::Launch);
    if(MH_CreateHook(update,reinterpret_cast<void*>(&ThrowbackUpdateHook),reinterpret_cast<void**>(&originalThrowbackUpdate))!=MH_OK)return;
    if(MH_CreateHook(launch,reinterpret_cast<void*>(&LaunchProjectileHook),reinterpret_cast<void**>(&originalLaunchProjectile))!=MH_OK) {
        MH_RemoveHook(update);return;
    }
    if(MH_QueueEnableHook(update)!=MH_OK||MH_QueueEnableHook(launch)!=MH_OK||MH_ApplyQueued()!=MH_OK) {
        MH_DisableHook(update);MH_DisableHook(launch);MH_RemoveHook(update);MH_RemoveHook(launch);return;
    }
    throwbackInstalled=true;
    if(log)log("Native K&L throw-back: exact local incoming-grenade release follows fresh VR head gaze; native speed/loft/spin/fuse retained.");
}
std::uint32_t __fastcall SmokeEventHook(void* object,void*,std::uint32_t event,void* data) {
    const auto effect=reinterpret_cast<std::uintptr_t>(object);
    NativeSmokeLoopKL::Replacement before,after;
    if(NativeSmokeLoopKL::Capture(image,effect,event,ReadBytes,before)) {
        ++smokeLoopReplacements;
        const auto lookup=reinterpret_cast<SmokeLookup>(image+NativeSmokeLoopKL::Lookup);
        auto* sound=lookup(reinterpret_cast<void*>(std::uintptr_t(before.database)),before.handle);
        std::uint8_t retiring=0;
        const auto soundAt=reinterpret_cast<std::uintptr_t>(sound);
        if(sound&&ReadBytes(soundAt+0x13A,&retiring,1)&&
           NativeSmokeLoopKL::Capture(image,effect,event,ReadBytes,after)&&NativeSmokeLoopKL::Same(before,after)) {
            // Use the same immediate retirement that native smoke cleanup uses
            // before losing its handle. Never stop a manager-wide sound bus.
            const std::uint8_t marked=1;SIZE_T written=0;
            if(WriteProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(soundAt+0x13A),&marked,1,&written)&&written==1) {
                reinterpret_cast<SmokeFade>(image+NativeSmokeLoopKL::Fade)(sound,1.f,0.f,0.f);
                // The original event immediately writes the replacement into
                // this slot; do not alter its ID, definition, data or result.
                ++smokeLoopRetired;
            }else ++smokeLoopRejected;
        }else ++smokeLoopRejected;
    }
    return originalSmokeEvent(object,event,data);
}
void InstallSmokeLoop(LogCallback log) {
    if(!NativeSmokeLoopKL::Verify(image,ReadBytes)) {
        if(log)log("Native K&L smoke sound: lifecycle signatures differ; original handling retained.");return;
    }
    void* target=reinterpret_cast<void*>(image+NativeSmokeLoopKL::Event);
    if(MH_CreateHook(target,reinterpret_cast<void*>(&SmokeEventHook),reinterpret_cast<void**>(&originalSmokeEvent))!=MH_OK)return;
    if(MH_QueueEnableHook(target)!=MH_OK||MH_ApplyQueued()!=MH_OK){MH_DisableHook(target);MH_RemoveHook(target);return;}
    smokeLoopInstalled=true;
    if(log)log("Native K&L smoke sound: retire the exact previous loop before a repeated native start overwrites its handle.");
}
bool CurrentThrowAim(std::uintptr_t player,NativeHandsKL::ControllerAim& output,bool* fromBarrel=nullptr) {
    if(fromBarrel)*fromBarrel=false;
    if(!NativeHandsKL::SnapshotControllerAim(1,output)||output.player!=player)return false;
    // A visible two-handed firearm may constrain the primary wrist away from
    // the bare right controller. Prefer that exact current reticle/laser ray;
    // native throw animation can suspend visuals, then physical aim continues.
    AimFrame barrel;std::uint32_t category=~0u;
    if(SnapshotAim(barrel)&&barrel.player==player&&barrel.hand==1&&barrel.pairId==output.pairId&&
       barrel.recenterAck==output.recenterAck&&barrel.unitsPerMetre==output.unitsPerMetre&&
       Read(barrel.model+NativeWeaponTypeKL::CategoryOffset,category)&&(category<=6||category==15)) {
        output.origin=barrel.origin;output.direction=barrel.direction;
        if(fromBarrel)*fromBarrel=true;
    }
    return true;
}
bool GrenadeTarget(std::uintptr_t item,const Vec3* supplied,Vec3& output) {
    PublishedGaze view;Vec3 target,nativeOrigin,nativeAim;
    std::uintptr_t camera=0,aim=0,aimVtable=0,aimPlayer=0;
    GrenadeScope current,ownedItem;
    grenadeProofStage=1;
    if(!grenadeScope.active||!GrenadeAction(grenadeScope.action,current)||current.player!=grenadeScope.player||
       !GrenadeItem(current.player,item,ownedItem)||
       (grenadeScope.item&&(ownedItem.item!=grenadeScope.item||ownedItem.root!=grenadeScope.root||
                           ownedItem.model!=grenadeScope.model||ownedItem.reference!=grenadeScope.reference)))return false;
    ownedItem.active=true;ownedItem.action=current.action;grenadeScope=ownedItem;
    grenadeProofStage=2;
    NativeHandsKL::ControllerAim controller;
    if(!scopeGestureContext||!CurrentThrowAim(current.player,controller)||
       NativeCameraKL::CinematicActive())return false;
    view.gaze.player=controller.player;view.gaze.origin=controller.origin;view.gaze.direction=controller.direction;
    view.gaze.tick=controller.tick;view.gaze.recenterAck=controller.recenterAck;view.gaze.unitsPerMetre=controller.unitsPerMetre;
    view.epoch=headGazeEpoch.load();view.horizontal={controller.direction.x,0,controller.direction.z};
    grenadeProofStage=3;
    if(!Read(current.player+0x9A0,camera)||!Read(camera,aimVtable)||aimVtable!=image+0x545A3C)return false;
    grenadeProofStage=4;
    if(!Read(current.player+0x9AC,aim)||!Read(aim,aimVtable))return false;
    grenadeMoverVtable=static_cast<std::uint32_t>(aimVtable);
    if(!NativeCameraMoverKL::KnownVtable(image,aimVtable))return false;
    grenadeProofStage=5;
    if(!Read(aim+8,aimPlayer)||aimPlayer!=current.player||!Read(aim+0x34,nativeAim)||
       !Read(item+0x134,nativeOrigin)||NativeWeaponMath::ZeroMuzzle(nativeOrigin)||
       !ReadBytes(reinterpret_cast<std::uintptr_t>(supplied),&target,sizeof(target)))return false;
    grenadeProofStage=6;
    Vec3 remapped;
    if(!NativeGrenadeKLMath::RemapTarget(nativeOrigin,target,nativeAim,view.gaze,view.horizontal,remapped)||
       headGazeEpoch.load()!=view.epoch||static_cast<std::uint32_t>(VRBridge::RecenterAck())!=view.gaze.recenterAck)return false;
    output=remapped;
    Vec3 direction;
    if(NativeGrenadeKLMath::Unit(Subtract(remapped,nativeOrigin),direction)) {
        grenadeDirection[0]=direction.x;grenadeDirection[1]=direction.y;grenadeDirection[2]=direction.z;
    }
    grenadeProofStage=7;grenadeThread=GetCurrentThreadId();return true;
}
bool __fastcall ThrowUpdateHook(void* action,void*) {
    ++grenadeUpdates;
    if(grenadeScope.active)return originalThrowUpdate(action);
    GrenadeScope next;
    if(!GrenadeAction(reinterpret_cast<std::uintptr_t>(action),next)) {
        ++grenadeContextRejected;return originalThrowUpdate(action);
    }
    ++grenadeLocalUpdates;
    const auto previous=grenadeScope;grenadeScope=next;
    struct Restore {GrenadeScope previous;~Restore(){grenadeScope=previous;}} restore{previous};
    return originalThrowUpdate(action);
}
void InstallGrenade(LogCallback log) {
    if(!NativeGrenadeKLImage::Verify(image,ReadBytes)||!NativeActionKL::InitializeVerifiedImage(image)) {
        if(log)log("Native K&L grenade gaze: throw/action signatures differ; grenade targeting remains native.");return;
    }
    void* target=reinterpret_cast<void*>(image+NativeGrenadeKLImage::ThrowUpdate);
    if(MH_CreateHook(target,reinterpret_cast<void*>(&ThrowUpdateHook),reinterpret_cast<void**>(&originalThrowUpdate))!=MH_OK)return;
    if(MH_EnableHook(target)!=MH_OK){MH_RemoveHook(target);return;}
    grenadeInstalled=true;
    if(log)log("Native K&L grenade aim: legacy Throw callback and owned FF Fire use the current right-controller pointing frame; native charge/loft/origin retained.");
}
bool Firearm(std::uintptr_t weapon) {
    std::uintptr_t vtable=0,setter=0;
    return weapon&&Read(weapon,vtable)&&Read(vtable+0x2D4,setter)&&setter==image+kSetTarget;
}
bool HeldBy(std::uintptr_t player,std::uintptr_t weapon,int candidate,bool requireActive=true) {
    const auto hand=player+(candidate?0x358:0x2D8);
    std::uintptr_t vtable=0,weaponVtable=0,setter=0;
    NativeWeaponOwnership::HandState state;
    return Read(hand,vtable)&&vtable==image+kHandVtable&&
        NativeWeaponOwnership::ReadHandState(hand,ReadBytes,state)&&state.item==weapon&&state.enabled<=1&&(!requireActive||state.enabled)&&
        state.attacher==static_cast<unsigned>(candidate?31:32)&&Read(weapon,weaponVtable)&&
        Read(weaponVtable+0x2D4,setter)&&setter==image+kSetTarget;
}
bool HeldForVisual(std::uintptr_t player,std::uintptr_t weapon,int side,NativeHeldGrenadeVisualKL::Owner* grenadeOutput=nullptr) {
    const auto action=NativeActionKL::Current();
    const bool reload=action.player==player&&action.TrackedReload();
    if(HeldBy(player,weapon,side,!reload))return true;
    NativeHeldGrenadeVisualKL::Owner grenade;
    const bool owned=NativeHeldGrenadeVisualKL::Capture(image,action,player,ReadBytes,grenade)&&
        grenade.item==weapon&&grenade.side==unsigned(side);
    if(owned&&grenadeOutput)*grenadeOutput=grenade;
    return owned;
}
#include "physical_reload_diagnostic_kl.inl"
#include "background_weapon_equip_kl.inl"
void DispatchReload(void* player,void* weapon,std::uint32_t caller,bool manualR) {
    ++reloadRequests;
    // The verified weapon-update calls can run while the equipped hand is
    // still disabled during a swap. Identify the local actor before inspecting
    // any hand state. All three requests are automatic even with an empty gun.
    const bool localPlayer=NativeReloadKL::AutomaticCaller(caller)&&
        NativeReloadKL::IsLocalPlayer(image,reinterpret_cast<std::uintptr_t>(player),ReadBytes);
    const auto decision=NativeReloadKL::Decide(trackedReloadEnabled,trackedReloadContext,VRBridge::Ready(),
        NativeCameraKL::CinematicActive(),caller,localPlayer,manualR);
    if(decision==NativeReloadKL::Decision::Suppress||ReloadDiagnostic::Owns(reinterpret_cast<std::uintptr_t>(weapon))){
        ++reloadBlocked;return;
    }
    if(decision==NativeReloadKL::Decision::Manual)++reloadManual;
    else ++reloadUnproved;
    if(originalReload)originalReload(player,weapon);
}
void __fastcall ReloadHook(void* player,void*,void* weapon) {
    const auto caller=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(__builtin_return_address(0))-image);
    // Bit20 in player+7D0 also means contextual E/use. Only actual held R
    // authorizes a native weapon-update request; the immersive
    // controller intent uses the separate simulation dispatcher.
    DispatchReload(player,weapon,caller,(BackgroundDiagnostic::Key('R')&0x8000)!=0);
}
void InstallReload(LogCallback log) {
    if(reloadInstalled||!NativeReloadKL::Verify(image,ReadBytes))return;
    reloadAmmoProfile=NativeReloadAmmoKL::Verify(image,ReadBytes);
    if(log)log(reloadAmmoProfile?"KL1 manual reload native ammo profile verified; observer only until loader/contact qualification.":"KL1 manual reload ammo profile unavailable; original native reload retained.");
    auto* target=reinterpret_cast<void*>(image+NativeReloadKL::Request);
    if(MH_CreateHook(target,reinterpret_cast<void*>(&ReloadHook),reinterpret_cast<void**>(&originalReload))!=MH_OK)return;
    if(MH_EnableHook(target)!=MH_OK){MH_RemoveHook(target);originalReload=nullptr;return;}
    reloadInstalled=true;
    if(log)log("Native K&L reload: local weapon updates require an explicit reload input, including with an empty magazine.");
}
const HeldCache& CurrentHeld() {
    const DWORD now=GetTickCount();
    if(heldCache.tick&&now-heldCache.tick<=10)return heldCache;
    HeldCache next;next.tick=now;std::uintptr_t data=0;
    if(Read(image+kGameData,data)&&Read(data+0xA2C,next.player)&&next.player) {
        Read(next.player+4,next.bodyRoot);Read(next.player+0x70,next.bodyModel);
        for(int side=0;side<2;++side) {
            std::uintptr_t item=0;
            if(Read(next.player+(side?0x358:0x2D8)+0x60,item)&&item&&HeldForVisual(next.player,item,side)&&
               Read(item+0x58,next.weaponReference[side]))next.weapon[side]=item;
        }
        if(NativeWeaponOwnership::Capture(image,next.player,ReadBytes,next.attachments))
            for(unsigned i=0;i<next.attachments.count;++i) {
                const auto geometry=next.attachments.geometry[i];std::uintptr_t weapon=0,root=0;
                if(geometry&&Read(geometry+0x60,weapon)&&Firearm(weapon)&&Read(weapon+4,root)&&root==geometry)
                    next.stowedWeapon[i]=weapon;
                int side=-1;std::uintptr_t group=0;
                if(NativeWeaponOwnership::HandGroup(image,next.attachments,i,ReadBytes,side,group))next.handGroup[i]=group;
                if(next.attachments.records[i].bone==35||next.attachments.records[i].bone==2) {
                    ++hipAccessorySlots;
                    hipAccessoryPrimitive=next.attachments.records[i].bone;
                    if(hipAttachmentProof&&NativeWeaponOwnership::HipGroup(image,next.attachments,i,ReadBytes,group))next.hipGroup[i]=group;
                    else ++hipAccessoryRejected;
                }
                if(!next.stowedWeapon[i]&&!next.handGroup[i]&&!next.hipGroup[i]&&
                   NativeWeaponOwnership::BodyGroup(image,next.attachments,i,ReadBytes,group)) {next.bodyGroup[i]=group;++bodyGroupSlots;}
            }
    }
    if(next.player)NativeHeldGrenadeVisualKL::Capture(image,NativeActionKL::Current(),next.player,ReadBytes,next.grenade);
    next.signature=NativeWeaponOwnership::OwnershipSignature(next.attachments,next.weapon,next.weaponReference);
    next.signature^=next.grenade.item;next.signature*=1099511628211ull;
    next.signature^=next.grenade.reference;next.signature*=1099511628211ull;
    next.signature^=next.bodyRoot;next.signature*=1099511628211ull;
    next.signature^=next.bodyModel;next.signature*=1099511628211ull;
    heldCache=next;return heldCache;
}
bool Point(std::uintptr_t weapon,Vec3& output,int& side) {
    NativeHandsKL::VisualFrame hands;
    std::uintptr_t data=0,player=0,weaponVtable=0,setter=0;
    if(!NativeHandsKL::SnapshotVisual(hands)||!Read(image+kGameData,data)||
       !Read(data+0xA2C,player)||player!=hands.player||!Read(weapon,weaponVtable)||
       !Read(weaponVtable+0x2D4,setter)||setter!=image+kSetTarget)return false;
    // Prefer the right hand if a long gun is represented in both native
    // hands. These are verified K&L IKHAND direct pointers, not ZREF handles.
    for(int candidate=1;candidate>=0;--candidate) {
        const auto hand=player+(candidate?0x358:0x2D8);
        std::uintptr_t vtable=0,item=0;unsigned char enabled=0;std::uint32_t attacher=0;
        if(!hands.active[candidate]||!Read(hand,vtable)||vtable!=image+kHandVtable||
           !Read(hand+0x60,item)||item!=weapon||!Read(hand+0x64,enabled)||!enabled||
           !Read(hand+0x68,attacher)||attacher!=static_cast<unsigned>(candidate?31:32))continue;
        const auto& wrist=hands.wrist[candidate];
        if(!ValidatePose(wrist,.05f)||!std::isfinite(hands.unitsPerMetre)||hands.unitsPerMetre<=0)return false;
        // Native wrist +X/fingers is Basis::right (last stored row), which
        // the hand target maps to the controller's physical -Z pointing ray.
        output=Add(wrist.position,Scale(wrist.basis.right,100.f*hands.unitsPerMetre));
        side=candidate;return Finite(output);
    }
    return false;
}
void __fastcall SetTargetHook(void* item,void*,const Vec3* point) {
    ++calls;
    const auto caller=reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
    if(grenadeScope.active&&(caller==image+NativeGrenadeKLImage::ThrowTargetReturn||
                            grenadeScope.item==reinterpret_cast<std::uintptr_t>(item))) {
        Vec3 remapped;
        if(caller==image+NativeGrenadeKLImage::ThrowTargetReturn) {
            ++grenadeTargets;
            if(GrenadeTarget(reinterpret_cast<std::uintptr_t>(item),point,remapped)) {
                original(item,&remapped);++grenadeGazeApplied;++passthrough;return;
            }
            ++grenadeGazeRejected;
        }
        original(item,point);++passthrough;return;
    }
    // Fire computes its own native random spread, including shotguns. Once
    // our aim is installed at the start of that scope, preserve every native
    // target update instead of replacing its spread with the controller ray.
    if(shot.active&&shot.weapon==reinterpret_cast<std::uintptr_t>(item)) {
        original(item,point);++passthrough;return;
    }
    if(inside){original(item,point);return;}
    // Grenade charge/loft is authored before Fire. Never replace that native
    // target with an arbitrary 100m firearm point on an unobserved throw path.
    std::uintptr_t type=0;std::uint32_t category=~0u;
    if(Read(reinterpret_cast<std::uintptr_t>(item)+0xC0,type)&&Read(type+NativeWeaponTypeKL::CategoryOffset,category)&&
       (category==11||category==12)){original(item,point);++passthrough;return;}
    inside=true;
    Vec3 controllerPoint;int side=-1;
    if(Point(reinterpret_cast<std::uintptr_t>(item),controllerPoint,side)) {
        original(item,&controllerPoint);++tracked;
        lastWeapon.store(reinterpret_cast<std::uintptr_t>(item));lastHand.store(side);
    } else {original(item,point);++passthrough;}
    inside=false;
}
bool PrepareShot(std::uintptr_t weapon,ShotContext& output,unsigned* qualification=nullptr) {
    const auto stage=[&](unsigned value){if(qualification)*qualification=value;};stage(1);
    ShotContext candidate;candidate.weapon=weapon;
    std::uintptr_t data=0,model=0,templateVtable=0,marker=0,geometry=0,owner=0,playerVtable=0,weaponVtable=0,ownerMethod=0;
    if(!NativeHandsKL::SnapshotVisual(candidate.hands))return false;stage(11);
    if(!Read(image+kGameData,data)||
       !Read(data+0xA2C,candidate.player)||candidate.player!=candidate.hands.player||
       !Read(candidate.player,playerVtable)||playerVtable!=image+0x56582C||
       !Read(weapon,weaponVtable)||!Read(weaponVtable+0x290,ownerMethod)||ownerMethod!=image+0x11D230||
       !Firearm(weapon)||!Read(weapon+4,candidate.root)||
       !Read(candidate.root+0x60,owner)||owner!=weapon||!Read(weapon+0xC0,model)||
       !Read(model,templateVtable)||(templateVtable!=image+0x54E6D4&&templateVtable!=image+0x577864)||
       !Read(model+0x150,marker)||!marker||!Read(marker+4,geometry)||
       !Read(geometry+0x60,owner)||owner!=marker||!Read(weapon+0xC4,candidate.savedTarget)||
       !Finite(candidate.savedTarget))return false;
    // Bind the native Fire's owner/type branch to this exact local ZPlayer.
    // ZPlayer +0x30 returns the class ID at C860B8, so Fire's alternate +F8
    // remote-target branch is not eligible to overwrite the tracked aim.
    stage(2);if(reinterpret_cast<Owner>(ownerMethod)(reinterpret_cast<void*>(weapon))!=reinterpret_cast<void*>(candidate.player))return false;
    CameraPose localMuzzle;
    stage(3);if(!Read(geometry,localMuzzle)||!ValidatePose(localMuzzle,.05f))return false;stage(4);
    for(int side=1;side>=0;--side) {
        if(!candidate.hands.active[side]||!candidate.hands.availableForMesh[side]||
           !HeldBy(candidate.player,weapon,side))continue;
        // Use the SAME GPU attachment/wrists as the visible gun. A current
        // CPU animation attachment mixed with last-rendered wrists jumps
        // during gait/recoil and puts the shot beside the visible barrel.
        AcquireSRWLockShared(&renderedGunLock);const auto visible=renderedGun[side];ReleaseSRWLockShared(&renderedGunLock);
        std::uint32_t reference=0;
        const unsigned reason=(calibrationTransition.load()?1:0)|(visible.calibrationEpoch!=calibrationEpoch.load()?2:0)|
            (GetTickCount()-visible.tick>150?4:0)|(visible.model!=model?8:0)|(!Read(weapon+0x58,reference)||reference!=visible.reference?16:0)|
            (visible.shot.weapon!=weapon?32:0)|(visible.shot.root!=candidate.root?64:0)|
            (visible.shot.player!=candidate.player?128:0)|(!HeldBy(candidate.player,weapon,side)?256:0);
        if(reason){stage(1000+reason);continue;}
        candidate.hands=visible.shot.hands;candidate.origin=visible.shot.origin;candidate.target=visible.shot.target;
        candidate.attachment=visible.attachment;
        if(weaponVtable==image+kFFWeaponVtable) {
            Vec3 pending;
            if(!Read(weapon+0x134,pending)||!Finite(pending)){stage(5);continue;}
            if(!NativeWeaponMath::ZeroMuzzle(pending)&&!candidate.cachedMuzzle.Prepare(pending,candidate.origin)){stage(6);continue;}
        }
        candidate.side=side;candidate.active=true;output=candidate;stage(0);return true;
    }
    return false;
}
bool ScopedHeadFire(void* item,Fire native) {
    if(native!=originalFireFF)return false;
    const auto optic=ReadScope();
    if(!optic.valid||!optic.active||optic.item!=reinterpret_cast<std::uintptr_t>(item))return false;
    ++scopeFireCalls;
    PublishedGaze view;Vec3 savedTarget;
    if(!ReadHeadGaze(view)||view.gaze.player!=optic.player||view.gaze.sourceCamera!=optic.sourceCamera||
       !Read(optic.item+0xC4,savedTarget)||!Finite(savedTarget)) {++scopeFireRejected;return false;}
    AimFrame pose;pose.player=optic.player;pose.weapon=optic.item;pose.root=optic.root;pose.model=optic.model;
    pose.hand=optic.hand;pose.pairId=view.gaze.pairId;pose.recenterAck=view.gaze.recenterAck;
    pose.tick=view.gaze.tick;pose.unitsPerMetre=view.gaze.unitsPerMetre;pose.origin=view.gaze.origin;pose.direction=view.gaze.direction;
    const auto previousQuery=scopeQuery;scopeQuery={optic.player,optic.item};
    struct QueryRestore {ScopeQuery previous;~QueryRestore(){scopeQuery=previous;}} queryRestore{previousQuery};
    Vec3 target;
    const auto result=NativeAimKL::TraceGaze(view.gaze,pose,target);
    if(result==NativeAimKL::GazeTrace::Unavailable){++scopeRayUnavailable;++scopeFireRejected;return false;}
    if(result==NativeAimKL::GazeTrace::Miss) {
        target=Add(view.gaze.origin,Scale(view.gaze.direction,NativeScopeKL::MaximumAimRangeMetres*view.gaze.unitsPerMetre));++scopeRayMisses;
    } else ++scopeRayHits;
    const auto after=ReadScope();
    if(!Finite(target)||headGazeEpoch.load()!=view.epoch||
       static_cast<std::uint32_t>(VRBridge::RecenterAck())!=view.gaze.recenterAck||
       !after.valid||!after.active||after.item!=optic.item||after.root!=optic.root||
       after.model!=optic.model||after.reference!=optic.reference||after.sourceCamera!=optic.sourceCamera) {
        ++scopeFireRejected;return false;
    }
    const auto previous=shot;shot={};shot.active=true;shot.headScope=true;shot.weapon=optic.item;shot.root=optic.root;
    shot.player=optic.player;shot.side=optic.hand;shot.savedTarget=savedTarget;shot.target=target;
    struct Restore {ShotContext previous;NativeScopeKL::State optic;Vec3 target;
        ~Restore(){
            std::uintptr_t root=0,owner=0;std::uint32_t reference=0;
            if(Read(optic.item+4,root)&&root==optic.root&&Read(root+0x60,owner)&&owner==optic.item&&
               Read(optic.item+0x58,reference)&&reference==optic.reference) {
                original(reinterpret_cast<void*>(optic.item),&target);++aimRestored;
            }else ++aimRestoreRejected;
            shot=previous;
        }} restore{previous,optic,savedTarget};
    // Only the convergence target changes. Native one-shot origin +134 and
    // every attachment/muzzle path remain untouched during this scope.
    original(item,&target);++fireTracked;++scopeFireApplied;
    NativeReloadKL::Observation ammunition;
    const bool tutorialShot=NativeReloadKL::Capture(image,optic.player,optic.item,ReadBytes,ammunition)&&ammunition.magazine>0;
    native(item);NativeCoverKL::OnTrackedFire(optic.player);
    if(tutorialShot)NativeTutorialKL::NotifyShot(optic.player);
    return true;
}
bool ScopedGrenadeFire(void* item,Fire native) {
    if(native!=originalFireFF)return false;
    const auto at=reinterpret_cast<std::uintptr_t>(item);std::uintptr_t data=0,player=0,type=0;
    std::uint32_t category=~0u;
    if(!Read(at+0xC0,type)||!Read(type+NativeWeaponTypeKL::CategoryOffset,category)||(category!=11&&category!=12))return false;
    ++grenadeFireCalls;
    const auto reject=[&]{++grenadeFireRejected;native(item);return true;};
    const auto inputEpoch=headGazeEpoch.load();
    GrenadeScope owned;NativeHandsKL::ControllerAim controller;Vec3 saved,origin,nativeAim,target;bool fromBarrel=false;
    std::uintptr_t aim=0,vt=0,owner=0;
    if(!scopeGestureContext||!Read(image+kGameData,data)||!Read(data+0xA2C,player)||!GrenadeItem(player,at,owned,true)||
       !CurrentThrowAim(player,controller,&fromBarrel)||
       GetTickCount()-controller.tick>HandsIPC::kFreshMs||static_cast<std::uint32_t>(VRBridge::RecenterAck())!=controller.recenterAck||
       !Read(player+0x9AC,aim)||!Read(aim,vt)||!NativeCameraMoverKL::KnownVtable(image,vt)||
       !Read(aim+8,owner)||owner!=player||!Read(aim+0x34,nativeAim)||!Read(at+0xC4,saved)||
       !Read(at+0x134,origin))return reject();
    if(NativeWeaponMath::ZeroMuzzle(origin)) {
        std::uintptr_t marker=0,geometry=0,markerOwner=0;CameraPose local,attachment;
        if(!originalAttachment||!Read(type+0x150,marker)||!Read(marker+4,geometry)||
           !Read(geometry+0x60,markerOwner)||markerOwner!=marker||!Read(geometry,local)||!ValidatePose(local,.05f))return reject();
        originalAttachment(item,&attachment.basis,&attachment.position);
        if(!ValidatePose(attachment,.05f))return reject();
        origin=NativeRigMath::ToWorld(attachment,{local.basis,local.position}).position;
    }
    HeadGaze ray;ray.origin=controller.origin;ray.direction=controller.direction;ray.unitsPerMetre=controller.unitsPerMetre;
    if(!NativeGrenadeKLMath::RemapTarget(origin,saved,nativeAim,ray,{controller.direction.x,0,controller.direction.z},target)) {
        return reject();
    }
    GrenadeScope after;std::uintptr_t latestData=0,latestPlayer=0;
    if(!scopeGestureContext||headGazeEpoch!=inputEpoch||static_cast<std::uint32_t>(VRBridge::RecenterAck())!=controller.recenterAck||
       !Read(image+kGameData,latestData)||latestData!=data||!Read(latestData+0xA2C,latestPlayer)||latestPlayer!=player||
       !GrenadeItem(player,at,after,true)||after.root!=owned.root||after.model!=owned.model||after.reference!=owned.reference) {
        return reject();
    }
    const auto previous=shot;shot={};shot.active=true;shot.headScope=true;shot.weapon=at;shot.player=player;shot.root=owned.root;
    struct Restore {ShotContext previous;GrenadeScope owned;Vec3 saved;
        ~Restore(){std::uintptr_t root=0;std::uint32_t ref=0;
            if(Read(owned.item+4,root)&&root==owned.root&&Read(owned.item+0x58,ref)&&ref==owned.reference)
                original(reinterpret_cast<void*>(owned.item),&saved);
            shot=previous;}} restore{previous,owned,saved};
    original(item,&target);++grenadeFireApplied;++grenadeGazeApplied;grenadeThread=GetCurrentThreadId();grenadeProofStage=8;
    if(fromBarrel)++grenadeFireBarrelAim;else ++grenadeFireControllerAim;
    const Vec3 nativeDelta=Subtract(saved,origin),mappedDelta=Subtract(target,origin);
    const float requested[]={controller.direction.x,controller.direction.y,controller.direction.z};
    const float oldAim[]={nativeAim.x,nativeAim.y,nativeAim.z},oldDelta[]={nativeDelta.x,nativeDelta.y,nativeDelta.z};
    const float newDelta[]={mappedDelta.x,mappedDelta.y,mappedDelta.z};
    for(unsigned axis=0;axis<3;++axis){grenadeRequestedDirection[axis]=requested[axis];grenadeNativeAim[axis]=oldAim[axis];
        grenadeNativeDelta[axis]=oldDelta[axis];grenadeMappedDelta[axis]=newDelta[axis];}
    Vec3 direction;if(NativeGrenadeKLMath::Unit(Subtract(target,origin),direction)) {
        grenadeDirection[0]=direction.x;grenadeDirection[1]=direction.y;grenadeDirection[2]=direction.z;
    }
    native(item);return true;
}
#include "background_shot_audit_kl.inl"
void ScopedFire(void* item,Fire native) {
    ++fireCalls;
    BackgroundShotAudit audit(item);
    if(ReloadDiagnostic::FireBlocked(reinterpret_cast<std::uintptr_t>(item))){audit.Blocked();++fireRejected;return;}
    if(grenadeScope.active&&grenadeScope.item==reinterpret_cast<std::uintptr_t>(item)){native(item);return;}
    if(shot.active){native(item);return;}
    if(ScopedGrenadeFire(item,native))return;
    if(ScopedHeadFire(item,native))return;
    ShotContext candidate;
    unsigned qualification=0;const bool prepared=PrepareShot(reinterpret_cast<std::uintptr_t>(item),candidate,&qualification);
    if(BackgroundDiagnostic::Active()){
        wchar_t folder[32768]{};if(GetEnvironmentVariableW(L"KL1_BACKGROUND_RUN",folder,32768))if(FILE* file=_wfopen((std::wstring(folder)+L"\\fire-qualification.jsonl").c_str(),L"ab")){
            const auto action=NativeActionKL::Current();
            fprintf(file,"{\"ageMs\":%lu,\"prepared\":%s,\"qualification\":%u,\"movement\":%u,\"action\":%u,\"trackingAllowed\":%s}\n",BackgroundDiagnostic::Age(),prepared?"true":"false",qualification,unsigned(action.movementKind),unsigned(action.actionKind),action.WeaponTrackingAllowed()?"true":"false");fclose(file);}
    }
    if(!prepared) {
        ++fireRejected;native(item);return;
    }
    // Only the FF Fire body consumes +0x134. Base Fire has another layout/
    // origin path and must never receive this optional one-shot edit.
    if(native!=originalFireFF)candidate.cachedMuzzle={};
    const auto previous=shot;shot=candidate;
    struct Restore {
        ShotContext previous;
        ~Restore() {
            std::uintptr_t root=0,owner=0;
            // The native shot may unequip an emptied firearm. Restore its
            // target only while the exact original entity/geometry survives.
            if(Firearm(shot.weapon)&&Read(shot.weapon+4,root)&&root==shot.root&&
               Read(root+0x60,owner)&&owner==shot.weapon) {
                if(shot.cachedMuzzle.active) {
                    Vec3 current;std::uintptr_t currentVtable=0;
                    if(Read(shot.weapon,currentVtable)&&currentVtable==image+kFFWeaponVtable&&
                       Read(shot.weapon+0x134,current)) {
                        using Exit=NativeWeaponMath::PendingMuzzle::Exit;
                        switch(shot.cachedMuzzle.Resolve(current)) {
                        case Exit::Restore:
                            setCachedMuzzle(reinterpret_cast<void*>(shot.weapon),&shot.cachedMuzzle.previous);
                            ++cachedMuzzleRestored;break;
                        case Exit::Consumed:++cachedMuzzleConsumed;break;
                        default:++cachedMuzzleChanged;break;
                        }
                    } else ++cachedMuzzleChanged;
                }
                original(reinterpret_cast<void*>(shot.weapon),&shot.savedTarget);++aimRestored;
            } else ++aimRestoreRejected;
            shot=previous;
        }
    } restore{previous};
    original(item,&shot.target);
    if(shot.cachedMuzzle.active) {
        // Native player firing logic can supply a one-shot WORLD muzzle
        // which bypasses AttachmentHook entirely (679E70..679EB8). Use the
        // exact same tracked muzzle as the visible gun and aim target.
        setCachedMuzzle(item,&shot.cachedMuzzle.temporary);++cachedMuzzleTracked;
    }
    ++fireTracked;shotThread=GetCurrentThreadId();
    const auto direction=Subtract(shot.target,shot.origin);
    const float length=std::sqrt(Dot(direction,direction));
    const float originValues[3]={shot.origin.x,shot.origin.y,shot.origin.z};
    const float directionValues[3]={direction.x/length,direction.y/length,direction.z/length};
    for(unsigned axis=0;axis<3;++axis){shotOrigin[axis]=originValues[axis];shotDirection[axis]=directionValues[axis];}
    // Keep the complete native shot body: spread RNG, magazine consumption,
    // projectile creation, collision, impact effects, sound and networking.
    NativeReloadKL::Observation ammunition;
    const bool tutorialShot=NativeReloadKL::Capture(image,shot.player,shot.weapon,ReadBytes,ammunition)&&ammunition.magazine>0;
    native(item);
    NativeCoverKL::OnTrackedFire(shot.player);
    if(tutorialShot)NativeTutorialKL::NotifyShot(shot.player);
}
void __fastcall FireBaseHook(void* item,void*) {ScopedFire(item,originalFireBase);}
void __fastcall FireFFHook(void* item,void*) {ScopedFire(item,originalFireFF);}
void __fastcall AttachmentHook(void* item,void*,Basis* basis,Vec3* position) {
    originalAttachment(item,basis,position);++muzzleCalls;
    if(!shot.active||shot.headScope||shot.weapon!=reinterpret_cast<std::uintptr_t>(item)||shot.side<0||
       !basis||!position||!HeldBy(shot.player,shot.weapon,shot.side))return;
    // ShotContext retains the rendered attachment independently of newer GPU
    // frames. Never re-read the animated attachment partway through a shot.
    const CameraPose corrected=shot.attachment;
    *basis=corrected.basis;*position=corrected.position;++muzzleTracked;
}
struct SniperPartFrame {
    NativeWeaponCalibrationKL::Key binding{};
    std::uintptr_t chain[8]{};
    CameraPose childToItem{};
};
thread_local SniperPartFrame sniperPartFrame[2];

bool HeldItemFrame(std::uintptr_t entry,const CameraPose* selectedModelRoot,const std::uintptr_t (&chain)[8],
                   std::uintptr_t root,CameraPose& item,const NativeWeaponCalibrationKL::Key* sniper=nullptr) {
    std::uint32_t index=2;CameraPose childWorld,childToItem;
    childToItem.basis={{0,0,1},{0,1,0},{1,0,0}};
    if(!Read(image+kGpuIndex,index)||index>1||
       !(selectedModelRoot?(childWorld=*selectedModelRoot,true):Read(entry+0x10+index*0x30,childWorld))||
       !ValidatePose(childWorld,.05f))return false;
    auto* saved=sniper&&chain[0]!=root?&sniperPartFrame[sniper->side]:nullptr;
    for(unsigned i=0;i<8&&chain[i]&&chain[i]!=root;++i){
        CameraPose local;
        if(!Read(chain[i],local)||!ValidatePose(local,.05f)){
            // The validated native graph and current GPU entry still name
            // this exact part. Carry its last complete local chain through
            // the current child WORLD, including after a new eye/pair.
            return saved&&NativeWeaponCalibrationKL::Same(saved->binding,*sniper)&&
                !std::memcmp(saved->chain,chain,sizeof(chain))&&
                NativeWeaponMath::ParentFrame(childWorld,saved->childToItem,item);
        }
        childToItem=NativeRigMath::ToWorld(local,{childToItem.basis,childToItem.position});
    }
    // The native entry is the item/child root, whereas WORLD at the rigid
    // binder also contains the selected part's own bone offset.
    if(!NativeWeaponMath::ParentFrame(childWorld,childToItem,item))return false;
    if(saved){
        saved->binding=*sniper;saved->childToItem=childToItem;
        std::memcpy(saved->chain,chain,sizeof(chain));
    }
    return true;
}
bool MeshFromEntry(std::uintptr_t entry,const CameraPose* source,CameraPose& output,std::uintptr_t rigidExtra=0,
                   const CameraPose* selectedModelRoot=nullptr,std::uintptr_t state=0) {
    ProfileTiming::Scope profile(ProfileTiming::WeaponOwnership);
    // WorldBind also runs for unrelated/non-VR views. Reject those before
    // refreshing native attachment snapshots or traversing geometry parents.
    std::uint64_t pair=0,generation=0;
    if(!NativeHandsKL::DrawFrameActive(pair,&generation))return false;
    std::uintptr_t vtable=0,geometry=0;
    const auto address=reinterpret_cast<std::uintptr_t>(source);
    static const auto pageBytes=[] {
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        return info.dwPageSize;
    }();
    NativeWeaponOwnership::DrawEntryFields fields;
    const bool combined=NativeWeaponOwnership::ReadDrawEntryOnPage(entry,pageBytes,ReadBytes,fields);
    if(combined)vtable=fields.vtable;
    else if(!Read(entry,vtable))return false;
    const auto rva=vtable-image;
    if(rva!=5508832&&rva!=5509172&&rva!=5509232&&rva!=5509112)return false;
    ++meshCandidates;
    if(combined)geometry=fields.geometry;
    else if(!Read(entry+0xF4,geometry))return false;
    if(!geometry)return false;
    // WorldBind runs for the entire scene. Ownership is cached briefly, then
    // revalidated against the exact native hand/attachment before any edit.
    const HeldCache& held=CurrentHeld();
    if(!held.player)return false;
    const auto& heldGrenade=held.grenade;
    const bool grenadeAttachment=heldGrenade.item!=0;
    NativeWeaponOwnership::GeometryLinks rootLinks;
    if(!NativeWeaponOwnership::ReadGeometryLinks(geometry,ReadBytes,rootLinks))return false;
    NativeWeaponOwnership::HipRigidIdentity rigidHip;
    bool directHip=false;
    if(rigidExtra&&rva==kRigidVtable&&rootLinks.owner==held.player) {
        ++hipRigidCandidates;unsigned observed=255;
        directHip=hipRigidProof&&NativeWeaponOwnership::HipRigid(image,held.player,entry,rigidExtra,ReadBytes,rigidHip,&observed);
        hipRigidBone=observed;
        hipRigidStandard=directHip?rigidHip.standard:255;
    }
    bool hasAttachment=held.weapon[0]||held.weapon[1]||grenadeAttachment;
    for(unsigned i=0;i<held.attachments.count;++i)hasAttachment=hasAttachment||held.stowedWeapon[i]||held.handGroup[i]||held.hipGroup[i]||held.bodyGroup[i];
    if(!hasAttachment&&!directHip&&!held.bodyRoot)return false;
    if(directHip) {
        ++hipRigidOwned;++hipAccessoryOwned;
        CameraPose raw,nativeRoot,renderedRoot,partRoot;std::uint32_t index=2;
        NativeWeaponOwnership::HipRigidIdentity after;
        std::uint64_t currentPair=0,currentGeneration=0;
        if(!Read(address,raw)||!Read(image+kGpuIndex,index)||index>1||
           !Read(entry+0x10+index*sizeof(CameraPose),partRoot)||!ValidatePose(partRoot,.05f)||
           !NativeHandsKL::FrameBodyRoot(held.player,pair,nativeRoot,renderedRoot)||
           std::memcmp(&nativeRoot,&partRoot,sizeof(nativeRoot))||
           !NativeWeaponOwnership::HipRigid(image,held.player,entry,rigidExtra,ReadBytes,after)||
           after.player!=rigidHip.player||after.geometry!=rigidHip.geometry||after.model!=rigidHip.model||
           after.part!=rigidHip.part||after.resource!=rigidHip.resource||after.cache!=rigidHip.cache||after.bone!=rigidHip.bone||after.standard!=rigidHip.standard||
           !NativeHandsKL::DrawFrameActive(currentPair,&currentGeneration)||currentPair!=pair||currentGeneration!=generation||
           !NativeWeaponMath::Remap(nativeRoot,renderedRoot,raw,output)) {
            ++hipRigidRejected;++hipAccessoryRejected;return false;
        }
        ++hipRigidTracked;++hipAccessoryTracked;++meshOwned;lastMeshOwner=held.player;return true;
    }
    const NativeWeaponOwnership::NegativeDrawKey negativeKey{generation,held.signature,held.player,entry,geometry,rootLinks};
    if(negativeDrawMemo.Contains(negativeKey)){++ownershipNegativeHits;return false;}
    // A mesh can be a child geometry of the held item. Walk only this
    // verified geometry chain, with a hard bound and cycle detection.
    const auto drawnGeometry=geometry;
    std::uintptr_t seen[8]{};int side=-1;std::uintptr_t weapon=0,childWeaponOwner=0;bool bodyChild=false,ownerOnChild=false;
    unsigned stowed=NativeWeaponOwnership::MaximumAttachments;
    unsigned accessory=NativeWeaponOwnership::MaximumAttachments;
    unsigned hipAccessory=NativeWeaponOwnership::MaximumAttachments;
    unsigned bodyAccessory=NativeWeaponOwnership::MaximumAttachments;
    for(unsigned depth=0;depth<8&&geometry;++depth) {
        for(unsigned previous=0;previous<depth;++previous)if(seen[previous]==geometry)return false;
        seen[depth]=geometry;
        NativeWeaponOwnership::GeometryLinks links=rootLinks;
        if(depth&&!NativeWeaponOwnership::ReadGeometryLinks(geometry,ReadBytes,links))return false;
        const std::uintptr_t owner=links.owner;
        lastMeshOwner.store(owner);
        for(int candidate=1;candidate>=0;--candidate) {
            if(!owner||held.weapon[candidate]!=owner)continue;
            // A child may itself carry the weapon as owner. Its pose is not
            // the item root: continue through its parent chain and accept
            // only the same weapon's actual root, otherwise different parts
            // can receive inconsistent canonical/wrist remaps.
            std::uint32_t weaponRoot=0;
            if(Read(owner+4,weaponRoot)) {
                if(weaponRoot==geometry){if(ownerOnChild&&childWeaponOwner!=owner)return false;side=candidate;weapon=owner;break;}
                if(ownerOnChild&&childWeaponOwner!=owner)return false;
                if(!ownerOnChild){ownerOnChild=true;childWeaponOwner=owner;++heldOwnerNonRoot;}
            }
        }
        if(side<0&&grenadeAttachment&&owner==heldGrenade.item&&geometry==heldGrenade.root) {
            side=int(heldGrenade.side);weapon=heldGrenade.item;
        }
        if(side>=0){if(ownerOnChild)++heldOwnerNonRootToRoot;break;}
        for(unsigned i=0;i<held.attachments.count;++i)
            if(owner&&held.stowedWeapon[i]==owner&&held.attachments.geometry[i]==geometry) {
                weapon=owner;stowed=i;break;
            } else if(owner&&held.handGroup[i]==owner&&held.attachments.geometry[i]==geometry) {
                accessory=i;break;
            } else if(owner&&held.hipGroup[i]==owner&&held.attachments.geometry[i]==geometry) {
                hipAccessory=i;break;
            } else if(owner&&held.bodyGroup[i]==owner&&held.attachments.geometry[i]==geometry) {
                bodyAccessory=i;break;
            }
        if(depth&&geometry==held.bodyRoot&&owner==held.player)bodyChild=true;
        if(bodyChild)break;
        if(stowed<NativeWeaponOwnership::MaximumAttachments||accessory<NativeWeaponOwnership::MaximumAttachments||hipAccessory<NativeWeaponOwnership::MaximumAttachments||bodyAccessory<NativeWeaponOwnership::MaximumAttachments)break;
        geometry=links.parent;
    }
    if(side<0&&!bodyChild&&stowed==NativeWeaponOwnership::MaximumAttachments&&accessory==NativeWeaponOwnership::MaximumAttachments&&hipAccessory==NativeWeaponOwnership::MaximumAttachments&&bodyAccessory==NativeWeaponOwnership::MaximumAttachments) {
        negativeDrawMemo.Store(negativeKey);++ownershipNegativeStored;return false;
    }
    if(bodyChild) {
        ++bodyChildCandidates;NativeWeaponOwnership::BodyChild identity;
        if(!NativeWeaponOwnership::CaptureBodyChild(image,held.player,drawnGeometry,ReadBytes,identity)) {++bodyChildRejected;return false;}
        ++bodyChildOwned;CameraPose nativeRoot,renderedRoot,raw;std::uintptr_t afterGeometry=0;
        std::uint64_t currentPair=0,currentGeneration=0;
        if(!Read(address,raw)||!NativeHandsKL::FrameBodyRoot(held.player,pair,nativeRoot,renderedRoot)||
           !Read(entry+0xF4,afterGeometry)||afterGeometry!=drawnGeometry||
           !NativeWeaponOwnership::StillBodyChild(image,identity,ReadBytes)||
           !NativeHandsKL::DrawFrameActive(currentPair,&currentGeneration)||currentPair!=pair||currentGeneration!=generation||
           !NativeWeaponMath::Remap(nativeRoot,renderedRoot,raw,output)) {++bodyChildRejected;return false;}
        ++bodyChildTracked;++meshOwned;lastMeshOwner=held.player;return true;
    }
    if(scopedDraw.active&&side==1&&held.player==scopedDraw.optic.player&&weapon==scopedDraw.optic.item&&geometry==scopedDraw.optic.root) {
        scopedDraw.entry=entry;scopedDraw.candidate=true;
    }
    ++meshOwned;
    if(bodyAccessory<NativeWeaponOwnership::MaximumAttachments)++bodyGroupOwned;
    else if(hipAccessory<NativeWeaponOwnership::MaximumAttachments)++hipAccessoryOwned;
    else if(accessory<NativeWeaponOwnership::MaximumAttachments)++accessoryOwned;
    else if(stowed<NativeWeaponOwnership::MaximumAttachments)++stowedOwned;
    std::uint64_t currentPair=0,currentGeneration=0;NativeHandsKL::VisualFrame hands;
    // A camera transition while reading ownership must not authorize a pose
    // from a newly active pair. Keep the original late proof as well.
    if(!NativeHandsKL::DrawFrameActive(currentPair,&currentGeneration)||currentPair!=pair)return false;
    if(stowed<NativeWeaponOwnership::MaximumAttachments) {
        // Hiding the exact body attachment needs no controller or IK pose.
        // A failed pose snapshot still leaves an ordinary native WORLD bind.
        std::uintptr_t root=0,owner=0;
        if(!Firearm(weapon)||!Read(weapon+4,root)||root!=held.attachments.geometry[stowed]||
           !Read(root+0x60,owner)||owner!=weapon||
           !NativeWeaponOwnership::StillAttached(image,held.attachments,stowed,ReadBytes)) {
            ++stowedRejected;return false;
        }
        std::uint32_t reference=0;
        if(Read(weapon+0x58,reference))
            stowedPending={true,0,entry,held.player,weapon,root,reference,stowed,pair,currentGeneration,held.attachments,{}};
    }
    NativeHeldGrenadeVisualKL::Owner grenade;
    std::uintptr_t probeModel=0;std::uint32_t probeCategory=0,probeReference=0;
    bool haveProbeCategory=false,firearm=false;
    if(side>=0){
        if(!HeldForVisual(held.player,weapon,side,&grenade))return false;
        haveProbeCategory=Read(weapon+0xC0,probeModel)&&Read(probeModel+NativeWeaponTypeKL::CategoryOffset,probeCategory);
        firearm=haveProbeCategory&&(probeCategory<=6||probeCategory==15)&&Read(weapon+0x58,probeReference);
    }
    const CameraPose* recovered=nullptr;
    const bool haveHands=state&&firearm?
        SnapshotHeldDrawPose(pair,held.player,weapon,geometry,probeModel,probeReference,unsigned(side),hands,recovered):
        NativeHandsKL::SnapshotDrawVisual(hands);
    NativeWeaponCalibrationKL::Key sniperBinding;
    if(firearm&&probeCategory==15&&!calibrationTransition.load()){
        sniperBinding={calibrationEpoch.load(),held.player,weapon,probeModel,probeReference,
            recovered?heldDrawPose[side].recenter:hands.recenterAck,unsigned(side),geometry};
    }
    const auto* sniper=sniperBinding.item?&sniperBinding:nullptr;
    if(recovered){
        CameraPose raw,item;
        if(!Read(address,raw)||!HeldItemFrame(entry,selectedModelRoot,seen,geometry,item,sniper)||
           !NativeWeaponMath::CarryPart(item,*recovered,raw,output))return false;
        // This draw-only continuity neither refreshes an aim certificate nor
        // supplies a hand pose to Fire, support, reload or animation logic.
        materialPending={true,hands.supportHeld,0,entry,held.player,weapon,probeModel,geometry,probeReference,side,pair,currentGeneration,{},{}};
        ++meshPoseRecovered;return true;
    }
    if(!haveHands||hands.pairId!=pair) {++meshFreshRejected;return false;}
    if(hands.player!=held.player)return false;
    CameraPose raw;
    if(!Read(address,raw))return false;
    if(bodyAccessory<NativeWeaponOwnership::MaximumAttachments) {
        std::uintptr_t owner=0;CameraPose nativeRoot,renderedRoot;
        if(!NativeHandsKL::FrameBodyRoot(held.player,pair,nativeRoot,renderedRoot)||
           !NativeWeaponOwnership::BodyGroup(image,held.attachments,bodyAccessory,ReadBytes,owner)||owner!=held.bodyGroup[bodyAccessory]||
           !NativeWeaponMath::Remap(nativeRoot,renderedRoot,raw,output)) {++bodyGroupRejected;return false;}
        weapon=owner;++bodyGroupTracked;
    } else if(hipAccessory<NativeWeaponOwnership::MaximumAttachments) {
        // Tool GROUPs on the exact native named hip attacher must receive
        // the same raw-body -> visual-body delta as the torso. They are not
        // firearms, hand accessories, or the separate rappelling cable.
        std::uintptr_t owner=0;
        CameraPose nativeRoot,renderedRoot;
        if(!hipAttachmentProof||!NativeHandsKL::FrameBodyRoot(held.player,pair,nativeRoot,renderedRoot)||
           !NativeWeaponOwnership::HipGroup(image,held.attachments,hipAccessory,ReadBytes,owner)||
           owner!=held.hipGroup[hipAccessory]||
           !NativeWeaponMath::Remap(nativeRoot,renderedRoot,raw,output)) {
            ++hipAccessoryRejected;return false;
        }
        weapon=owner;++hipAccessoryTracked;
    } else if(accessory<NativeWeaponOwnership::MaximumAttachments) {
        // Cuffs and other named-wrist GROUP attachments do not occupy
        // IKHAND+60. Revalidate the exact native slot/generation and model's
        // LEFT_HAND/RIGHT_HAND mapping before carrying this descendant draw
        // with the wrist. Native link poses/physics and source stay read-only.
        std::uintptr_t owner=0;
        if(!NativeWeaponOwnership::HandGroup(image,held.attachments,accessory,ReadBytes,side,owner)||
           owner!=held.handGroup[accessory]||side<0||!hands.availableForMesh[side]||
           !NativeWeaponMath::Remap(hands.rawWrist[side],hands.finalWrist[side],raw,output)) {
            ++accessoryRejected;return false;
        }
        weapon=owner;++accessoryTracked;
    } else if(side>=0) {
        if(!hands.availableForMesh[side]||
           !NativeWeaponMath::Remap(hands.rawWrist[side],hands.finalWrist[side],raw,output))return false;
        if(firearm) {
            if(BackgroundDiagnostic::Active()&&rigidExtra){
                static DWORD probeTick=0;static std::uintptr_t seen[128]{};static unsigned seenCount=0,total=0;
                ReloadPartsKL::Part part;
                std::uint32_t extraVt=0,extraFns[5]{};Read(rigidExtra,extraVt);if(extraVt)ReadBytes(extraVt,extraFns,sizeof(extraFns));
                const bool inspected=ReloadPartsKL::Inspect(image,entry,rigidExtra,drawnGeometry,ReadBytes,part);
                const auto key=inspected?part.resource:rigidExtra;bool known=false;for(unsigned i=0;i<seenCount;++i)known=known||seen[i]==key;
                if(total<256&&(!known||GetTickCount()-probeTick>1000)){
                    if(!known&&seenCount<128)seen[seenCount++]=key;probeTick=GetTickCount();++total;
                    for(char& c:part.name)if(c&&((unsigned char)c<32||c=='"'||c=='\\'))c='_';
                    wchar_t folder[32768]{};if(GetEnvironmentVariableW(L"KL1_BACKGROUND_RUN",folder,32768)){
                        if(FILE* f=_wfopen((std::wstring(folder)+L"\\reload-parts.jsonl").c_str(),L"ab")){
                            fprintf(f,"{\"extraVtable\":%u,\"methods\":[%u,%u,%u,%u,%u]}\n",extraVt,extraFns[0],extraFns[1],extraFns[2],extraFns[3],extraFns[4]);
                            fprintf(f,"{\"tick\":%lu,\"item\":%u,\"reference\":%u,\"category\":%u,\"entry\":%u,\"extra\":%u,\"geometry\":%u,\"inspected\":%s,\"bone\":%u,\"bones\":%u,\"name\":\"%s\",\"resource\":%u,\"sourcePosition\":[%.4f,%.4f,%.4f]}\n",GetTickCount(),unsigned(weapon),probeReference,probeCategory,unsigned(entry),unsigned(rigidExtra),unsigned(drawnGeometry),inspected?"true":"false",part.bone,part.count,part.name,unsigned(part.resource),raw.position.x,raw.position.y,raw.position.z); fprintf(f,"{\"metadata\":true,\"bone\":%u,\"boundsValid\":%s,\"bindValid\":%s,\"centre\":[%.4f,%.4f,%.4f],\"half\":[%.4f,%.4f,%.4f],\"bindPosition\":[%.4f,%.4f,%.4f]}\n",part.bone,part.boundsValid?"true":"false",part.bindValid?"true":"false",part.centre.x,part.centre.y,part.centre.z,part.half.x,part.half.y,part.half.z,part.bind.position.x,part.bind.position.y,part.bind.position.z);fclose(f);
                        }
                    }
                }
            }
            materialPending={true,hands.supportHeld,0,entry,held.player,weapon,probeModel,geometry,probeReference,
                side,pair,currentGeneration,{},{}};
        }
        // Preserve the original manual/empty reload animation, carrying its
        // weapon with the animated wrist instead of cancelling that pose.
        const auto action=NativeActionKL::Current();
        if(action.player==held.player&&(action.TrackedReload()||NativeBodyKL::ActionFollowEligible(action,held.player))) {
            settledGrip[side].Reset();return true;
        }
        // An actively held IKHAND grenade follows the normal HeldBy path,
        // which need not populate the optional temporary-attachment proof.
        if(!grenade.item&&haveProbeCategory&&(probeCategory==11||probeCategory==12))
            NativeHeldGrenadeVisualKL::Capture(image,action,held.player,ReadBytes,grenade);
        if(grenade.item==weapon&&grenade.root==geometry&&grenade.side==unsigned(side)){++heldGrenadeMeshTracked;return true;}
        CameraPose itemFrame;
        const bool chainValid=HeldItemFrame(entry,selectedModelRoot,seen,geometry,itemFrame,sniper);
        AcquireSRWLockShared(&renderedGunLock);auto cachedGun=renderedGun[side];ReleaseSRWLockShared(&renderedGunLock);
        const auto epoch=calibrationEpoch.load();
        const auto candidateEpoch=calibrationCandidateEpoch.load();
        std::uintptr_t currentModel=0,currentModelVtable=0,currentMarker=0,currentMarkerGeometry=0,currentMarkerOwner=0;
        std::uint32_t currentCategory=0;CameraPose currentMuzzle;
        const bool sameBinding=cachedGun.aligned&&Read(weapon+0xC0,currentModel)&&currentModel==cachedGun.model&&
            Read(currentModel,currentModelVtable)&&(currentModelVtable==image+0x54E6D4||currentModelVtable==image+0x577864)&&
            Read(currentModel+NativeWeaponTypeKL::CategoryOffset,currentCategory)&&currentCategory==cachedGun.category&&
            Read(currentModel+0x150,currentMarker)&&currentMarker==cachedGun.marker&&
            Read(currentMarker+4,currentMarkerGeometry)&&currentMarkerGeometry==cachedGun.markerGeometry&&
            Read(currentMarkerGeometry+0x60,currentMarkerOwner)&&currentMarkerOwner==currentMarker&&
            Read(currentMarkerGeometry,currentMuzzle)&&!std::memcmp(&currentMuzzle,&cachedGun.localMuzzle,sizeof(currentMuzzle));
        std::uint32_t currentReference=0;
        // The geometric grip/barrel certificate belongs to this exact held
        // item/template, not to a sensor sample. A slow frame may retire a
        // shot after 150 ms, but must not make us recalibrate from an idle
        // animation whose wrist and item no longer describe the same grip.
        // Revalidate identity and the immutable muzzle frame on every draw;
        // then rebase the proved attachment onto this draw's fresh wrist.
        const DWORD cachedAge=GetTickCount()-cachedGun.tick;
        if(!calibrationTransition.load()&&cachedGun.calibrationEpoch==epoch&&cachedGun.shot.weapon==weapon&&cachedGun.shot.player==held.player&&
           Read(weapon+0x58,currentReference)&&currentReference==cachedGun.reference&&
           ((!cachedGun.aligned&&cachedGun.shot.hands.pairId==hands.pairId&&cachedAge<=150)||sameBinding)&&
           cachedGun.shot.hands.recenterAck==hands.recenterAck&&cachedGun.shot.hands.unitsPerMetre==hands.unitsPerMetre&&
           cachedGun.shot.root==geometry) {
            if(cachedGun.aligned&&!chainValid){
                if(probeCategory!=15)return false;
                // Preserve the rigid part's own native WORLD even if its
                // GPU entry has no usable frame. Rebase the proved assembly
                // onto both current wrists, without refreshing the aim proof.
                CameraPose nativeItem,carriedItem;Vec3 nextOrigin,nextTarget;
                if(!NativeWeaponMath::Remap(cachedGun.shot.hands.rawWrist[side],hands.rawWrist[side],
                        cachedGun.nativeAttachment,nativeItem)||
                   !NativeWeaponMath::RebuildGrip(cachedGun.calibration,hands.finalWrist[side],carriedItem,nextOrigin,nextTarget))
                    return true;
                const auto partOffset=Subtract(raw.position,nativeItem.position);
                if(Dot(partOffset,partOffset)<=4.f*hands.unitsPerMetre*hands.unitsPerMetre)
                    NativeWeaponMath::CarryPart(nativeItem,carriedItem,raw,output);
                return true;
            }
            bool rebuilt=false;
            if(cachedGun.generation!=currentGeneration||cachedGun.shot.hands.pairId!=hands.pairId||cachedAge>150) {
                CameraPose carried;Vec3 nextOrigin,nextTarget;
                if(!NativeWeaponMath::RebuildGrip(cachedGun.calibration,hands.finalWrist[side],carried,nextOrigin,nextTarget)){++calibrationRejected;return false;}
                ++calibrationRebuilds;
                const float error=std::max({std::fabs(Dot(carried.basis.forward,carried.basis.forward)-1.f),
                    std::fabs(Dot(carried.basis.up,carried.basis.up)-1.f),std::fabs(Dot(carried.basis.right,carried.basis.right)-1.f)});
                auto previous=calibrationMaxScaleError.load();
                while(error>previous&&!calibrationMaxScaleError.compare_exchange_weak(previous,error)){}
                cachedGun.attachment=carried;cachedGun.shot.attachment=carried;
                cachedGun.shot.origin=nextOrigin;cachedGun.shot.target=nextTarget;
                cachedGun.shot.hands=hands;cachedGun.nativeAttachment=itemFrame;cachedGun.generation=currentGeneration;
                cachedGun.tick=GetTickCount();
                rebuilt=true;
            }
            if(cachedGun.aligned&&(!chainValid||!NativeWeaponMath::CarryPart(itemFrame,cachedGun.attachment,raw,output)))return false;
            const bool newImage=StampRenderedImage(cachedGun);
            if(rebuilt||newImage){AcquireSRWLockExclusive(&renderedGunLock);renderedGun[side]=cachedGun;ReleaseSRWLockExclusive(&renderedGunLock);}
            if(side==1&&rigidExtra&&ReloadDiagnostic::Enabled()){
        std::uint32_t model=0,ref=0,category=~0u;ReloadPartsKL::Part part;
        if(Read(weapon+0xC0,model)&&Read(weapon+0x58,ref)&&Read(model+0xA4,category)&&PhysicalReloadKL::Supported(category)&&ReloadPartsKL::Inspect(image,entry,rigidExtra,drawnGeometry,ReadBytes,part))ReloadDiagnostic::Part(weapon,model,ref,category,part,hands,output,output);
    }
    lastWeapon.store(weapon);lastHand.store(side);return true;
        }
        // Authored actions and explicit scene transitions cannot establish a
        // new immutable grip. Retain the already-computed native wrist remap.
        const bool calibrate=calibrationGameplay.load()&&!calibrationTransition.load()&&!NativeCameraKL::CinematicActive()&&action.valid&&
            action.player==held.player&&!action.action&&
            (action.movementKind==NativeActionKL::Movement::Stand||action.movementKind==NativeActionKL::Movement::BaseMove||
             action.movementKind==NativeActionKL::Movement::Cover||action.movementKind==NativeActionKL::Movement::Vehicle);
        if(!calibrate){settledGrip[side].Reset();return true;}
        if(settledCandidateGeneration[side]!=candidateEpoch){settledGrip[side].Reset();settledCandidateGeneration[side]=candidateEpoch;}
        std::uint32_t observedModel=0,observedReference=0;
        if(!chainValid||!Read(weapon+0xC0,observedModel)||!Read(weapon+0x58,observedReference)) {settledGrip[side].Reset();return true;}
        const NativeWeaponCalibrationKL::Key gripKey{epoch,held.player,weapon,observedModel,observedReference,hands.recenterAck,unsigned(side),geometry};
        // Each newly observed binding must settle independently. A previous
        // weapon on this hand in the same loading epoch is not evidence for
        // the next rifle/shotgun's native wrist-to-item grip.
        if(epoch>1&&!NativeWeaponCalibrationKL::Same(gripKey,settledBinding[side])) {
            if(!settledGrip[side].Select(true,gripKey,pair,hands.rawWrist[side],itemFrame,hands.unitsPerMetre))return true;
        }
        CameraPose attachment,markerPose;
        std::uintptr_t model=0,marker=0,markerGeometry=0,owner=0,root=0;
        Vec3 origin,target;RenderedGun visible;
        std::uint32_t reference=0,category=0;Vec3 visualDirection;bool visualAxis=false;float axisDeviation=0;
        std::uintptr_t modelVtable=0;
        const bool identity=Read(weapon+0x58,reference)&&Read(weapon+0xC0,model)&&Read(model,modelVtable)&&
            (modelVtable==image+0x54E6D4||modelVtable==image+0x577864);
        const bool firearmCategory=identity&&Read(model+NativeWeaponTypeKL::CategoryOffset,category)&&
            (category<=6||category==15);
        if(chainValid&&
           identity&&
           Read(weapon+4,root)&&root==geometry&&Read(model+0x150,marker)&&
           Read(marker+4,markerGeometry)&&Read(markerGeometry+0x60,owner)&&owner==marker&&
           Read(markerGeometry,markerPose)) {
            CameraPose fixedItem;Basis localAim;unsigned axisProof=0;
            visualAxis=firearmCategory&&NativeWeaponMath::HeldAttachment(hands.rawWrist[side],hands.finalWrist[side],itemFrame,
                markerPose,side,hands.unitsPerMetre,fixedItem,&axisProof,&localAim,
                hands.palmValid[side]?&hands.palmLocal[side].position:nullptr);
            if(firearmCategory) {
                barrelAxisCategory=category;
                barrelAxisProofStage=axisProof;
                if(visualAxis){barrelAxisLocal[0]=localAim.right.x;barrelAxisLocal[1]=localAim.right.y;barrelAxisLocal[2]=localAim.right.z;}
                if(visualAxis)axisDeviation=std::acos(std::clamp(Dot(NativeRigMath::Transform(itemFrame.basis,localAim.right),
                    hands.rawWrist[side].basis.right),-1.f,1.f))*57.2957795131f;
                barrelAxisDeviationDegrees=axisDeviation;
                if(!visualAxis)++barrelAxisRejected;
            }
            if(visualAxis) {
                if(!NativeWeaponMath::CarryPart(itemFrame,fixedItem,raw,output))return false;
                visualDirection=NativeRigMath::Transform(fixedItem.basis,localAim.right);
                if(!NativeWeaponMath::Shot(hands.finalWrist[side],hands.finalWrist[side],fixedItem,markerPose,
                    hands.unitsPerMetre,attachment,origin,target,&visualDirection))return false;
            } else {
                // Preserve the pre-.34 physical pointing ray on unproved
                // assets. Item+X can be ninety degrees from its child model's
                // barrel and must never replace the controller direction.
                visualDirection=hands.finalWrist[side].basis.right;
                if(!NativeWeaponMath::Shot(hands.rawWrist[side],hands.finalWrist[side],itemFrame,markerPose,
                    hands.unitsPerMetre,attachment,origin,target,&visualDirection))return false;
            }
            std::uint32_t afterReference=0;std::uintptr_t afterModel=0;
            if(!HeldForVisual(held.player,weapon,side)||!Read(weapon+0x58,afterReference)||afterReference!=reference||
               !Read(weapon+0xC0,afterModel)||afterModel!=model)return true;
            visible.shot.player=held.player;visible.shot.weapon=weapon;visible.shot.root=root;visible.shot.side=side;
            visible.shot.hands=hands;visible.shot.origin=origin;visible.shot.target=target;
            visible.attachment=attachment;visible.nativeAttachment=itemFrame;visible.aligned=visualAxis;
            visible.model=model;visible.reference=reference;visible.tick=GetTickCount();visible.generation=currentGeneration;visible.calibrationEpoch=epoch;
            visible.marker=marker;visible.markerGeometry=markerGeometry;visible.category=category;visible.localMuzzle=markerPose;
            if(!calibrationGameplay.load()||calibrationTransition.load()||calibrationEpoch.load()!=epoch||
               calibrationCandidateEpoch.load()!=candidateEpoch||
               !NativeWeaponMath::CaptureGrip(hands.finalWrist[side],attachment,origin,target,visible.calibration))return true;
            if(visualAxis)++barrelAxisAccepted;
            StampRenderedImage(visible);
            AcquireSRWLockExclusive(&renderedGunLock);renderedGun[side]=visible;ReleaseSRWLockExclusive(&renderedGunLock);
            settledBinding[side]=gripKey;
        }
    } else {
        // Inventory ownership alone is insufficient: the firearm's exact
        // root geometry must still occupy this player's native body slot.
        // Apply the same raw-root -> visual-root delta as the rendered torso;
        // native animated spine/grip offsets remain part of the source pose.
        if(!hands.bodyAvailableForMesh||!NativeWeaponMath::Remap(hands.rawRoot,hands.renderRoot,raw,output)) {
            ++stowedRejected;return false;
        }
        ++stowedTracked;
    }
    if(side==1&&rigidExtra&&ReloadDiagnostic::Enabled()){
        std::uint32_t model=0,ref=0,category=~0u;ReloadPartsKL::Part part;
        if(Read(weapon+0xC0,model)&&Read(weapon+0x58,ref)&&Read(model+0xA4,category)&&PhysicalReloadKL::Supported(category)&&ReloadPartsKL::Inspect(image,entry,rigidExtra,drawnGeometry,ReadBytes,part))ReloadDiagnostic::Part(weapon,model,ref,category,part,hands,output,output);
    }
    lastWeapon.store(weapon);lastHand.store(side);return true;
}
bool Mesh(const CameraPose* source,CameraPose& output,std::uintptr_t state=0,std::uintptr_t caller=0) {
    // Reject stack temporaries (including the corrected body root supplied
    // by the hands hook) before any hands locks. Generic draws use inline
    // entry poses. Rigid skinned weapon parts have a separate binder below.
    std::uint32_t index=2;
    const auto address=reinterpret_cast<std::uintptr_t>(source);
    // Hash/signature-pinned, module-owned DWORD on this GPU thread.
    // Reading it does not require a kernel transition for every world object.
    index=*reinterpret_cast<const volatile std::uint32_t*>(image+kGpuIndex);
    if(index>1||address<0x10000)return false;
    if(MeshFromEntry(address-0x10-index*0x30,source,output,0,nullptr,state))return true;
    if(stowedPending.valid)return false;
    // Only the exact native instance branch is enabled. View-aligned stack
    // geometry remains native: gun usage is unproved and probing every world
    // billboard would add work to a very hot foreign-object path.
    if(!worldSourceProof||caller!=image+0x7205F)return false;
    std::uintptr_t provider=0;NativeWeaponWorldSource::Source proof;
    if(!NativeHandsKL::ReadCurrentNativeEntry(state,provider)||
       !NativeWeaponWorldSource::Resolve(image,state,provider,source,caller,false,ReadBytes,proof)||
       !MeshFromEntry(provider,source,output,0,proof.kind==NativeWeaponWorldSource::Kind::Instance?source:nullptr,state))return false;
    if(proof.kind==NativeWeaponWorldSource::Kind::Instance)++instanceWorldTracked;
    return true;
}
void BindStowedWorld(void* state) {
    if(!stowedPending.valid)return;
    stowedBound=stowedPending;
    stowedBound.state=reinterpret_cast<std::uintptr_t>(state);
    std::memcpy(&stowedBound.world,static_cast<const unsigned char*>(state)+0x8C,sizeof(CameraPose));
}
void InvalidateOwnedLightSpace(void* state,const CameraPose& before,const CameraPose& after) {
    if(!bonesLightCacheProof)return;
    const auto at=reinterpret_cast<std::uintptr_t>(state);
    // Native index0 already resets this cache. This is the usual path and
    // remains free of graph/RPM calls on every tracked body/weapon draw.
    if(!*reinterpret_cast<const std::uint32_t*>(at+0x3C4))return;
    const auto read=[&](std::uintptr_t address,void* bytes,std::size_t count){
        if((address==at+0x8C&&count==sizeof(CameraPose))||
           ((address==at+0x3C0||address==at+0x3C4||address==image+NativeBonesLightCache::CacheRva)&&count==4)){
            std::memcpy(bytes,reinterpret_cast<const void*>(address),count);return true;}return false;};
    const auto write=[](std::uintptr_t address,const void* bytes,std::size_t count){SIZE_T wrote=0;return
        WriteProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),bytes,count,&wrote)&&wrote==count;};
    if(NativeBonesLightCache::Invalidate(image,at,before,after,read,write,bonesLightCacheProof)==NativeBonesLightCache::Result::Invalidated)
        ++bonesLightCacheInvalidated;
}
void RetireDrawBindings() {
    // Each producer replaces the full packet before making it valid again.
    // Most world binds are foreign geometry, so retire only the draw tags.
    ReloadDiagnostic::clipBound.valid=false;ReloadDiagnostic::clipPending.valid=false;
    ReloadDiagnostic::hiddenEntry=ReloadDiagnostic::hiddenState=0;ReloadDiagnostic::hiddenRecord=0;ReloadDiagnostic::hidePending=false;
    materialPending.valid=false;materialBound.valid=false;
    stowedPending.valid=false;stowedBound.valid=false;
    scopedDraw.tag=false;scopedDraw.candidate=false;scopedDraw.draws=0;
}
void __fastcall WorldBindHook(void* state,void*,const CameraPose* source) {
    RetireDrawBindings();
    const auto observer=worldBindObserver.load(std::memory_order_relaxed);
    const auto caller=reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
    if(observer)observer(state,source,static_cast<std::uint32_t>(caller-image));
    ++worldCalls;
    if(!installed){originalWorld(state,source);ObserveScopedWorld(state,source);return;}
    if(insideWorld){originalWorld(state,source);return;}
    ProfileTiming::Scope profile(ProfileTiming::WeaponMesh);
    const auto native=[&](const CameraPose* pose){
        profile.Stop();ProfileTiming::Scope nativeProfile(ProfileTiming::NativeWorldBind);originalWorld(state,pose);
    };
    if(!NativeCameraKL::DrawStateActive(reinterpret_cast<std::uintptr_t>(state))) {
        ++viewRejected;native(source);return;
    }
    insideWorld=true;CameraPose remapped;
    if(Mesh(source,remapped,reinterpret_cast<std::uintptr_t>(state),caller)){
        native(&remapped);InvalidateOwnedLightSpace(state,*source,remapped);++meshTracked;
        if(materialPending.valid){materialBound=materialPending;materialBound.state=reinterpret_cast<std::uintptr_t>(state);
            materialBound.source=*source;materialBound.tracked=remapped;
            ObserveProjectionBinding(materialBound);}
    }
    else native(source);
    BindStowedWorld(state);
    ObserveScopedWorld(state,source);
    insideWorld=false;
}
void __fastcall RigidBindHook(void* entry,void*,void* state,std::uintptr_t extra) {
    RetireDrawBindings();
    ++rigidCalls;
    // This binder writes WORLD inline instead of going through WorldBindHook.
    // Retire the preceding geometry tag, and restore any temporary VIEW,
    // before the native binder computes an unrelated rigid object's matrices.
    if(const auto observer=worldBindObserver.load(std::memory_order_relaxed))
        observer(state,nullptr,static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(__builtin_return_address(0))-image));
    originalRigid(entry,state,extra);
    ReloadDiagnostic::Survey(reinterpret_cast<std::uintptr_t>(entry),extra,reinterpret_cast<std::uintptr_t>(state));
    if(insideRigid)return;
    if(!NativeCameraKL::DrawStateActive(reinterpret_cast<std::uintptr_t>(state))) {++viewRejected;return;}
    insideRigid=true;
    // 0x4739E0 derives the rigid bone's world pose and inlines WorldBind;
    // it never calls 0x471770. Use the actual derived draw pose at +0x8C,
    // preserving the authored per-part offset instead of the entry root.
    CameraPose remapped;
    const auto raw=reinterpret_cast<const CameraPose*>(reinterpret_cast<std::uintptr_t>(state)+0x8C);
    if(MeshFromEntry(reinterpret_cast<std::uintptr_t>(entry),raw,remapped,extra,nullptr,reinterpret_cast<std::uintptr_t>(state))) {
        if(ReloadDiagnostic::clipPending.valid){ReloadDiagnostic::clipBound=ReloadDiagnostic::clipPending;ReloadDiagnostic::clipBound.state=reinterpret_cast<std::uintptr_t>(state);ReloadDiagnostic::clipBound.entry=reinterpret_cast<std::uintptr_t>(entry);}
        const auto before=*raw;originalWorld(state,&remapped);InvalidateOwnedLightSpace(state,before,remapped);++meshTracked;++rigidTracked;
        if(materialPending.valid){materialBound=materialPending;materialBound.state=reinterpret_cast<std::uintptr_t>(state);
            materialBound.source=before;materialBound.tracked=remapped;
            ObserveProjectionBinding(materialBound);}
    }
    const auto binderEntry=reinterpret_cast<std::uintptr_t>(entry);
    const bool missingClip=!ReloadDiagnostic::clipBound.valid&&!ReloadDiagnostic::hidePending&&
        ReloadDiagnostic::DetachedClip(binderEntry,extra);
    if(ReloadDiagnostic::hidePending||missingClip){
        ReloadDiagnostic::hiddenEntry=binderEntry;ReloadDiagnostic::hiddenState=reinterpret_cast<std::uintptr_t>(state);
        std::uintptr_t provider=0;
        // The material provider can wrap this rigid binder. Record the
        // enclosing native material so later passes cannot borrow the tag.
        if(NativeHandsKL::ReadCurrentNativeEntry(ReloadDiagnostic::hiddenState,provider)){
            ReloadDiagnostic::hiddenEntry=provider;
            Read(ReloadDiagnostic::hiddenState+0x14,ReloadDiagnostic::hiddenRecord);
        }
        if(missingClip)++ReloadDiagnostic::hidden;
    }
    BindStowedWorld(state);
    // The owned rigid draw has its final WORLD now. Retain the PRE null
    // boundary above; this POST marker supplies the exact inline entry so a
    // rope observer can independently prove its ancestry and snapshot WORLD.
    const auto index=*reinterpret_cast<const volatile std::uint32_t*>(image+kGpuIndex);
    if(index<=1)if(const auto observer=worldBindObserver.load(std::memory_order_relaxed))
        observer(state,reinterpret_cast<const CameraPose*>(reinterpret_cast<std::uintptr_t>(entry)+0x10+index*0x30),kRigidBind);
    if(index<=1)ObserveScopedWorld(state,reinterpret_cast<const CameraPose*>(reinterpret_cast<std::uintptr_t>(entry)+0x10+index*0x30));
    insideRigid=false;
}
}
namespace {
std::uint32_t MaterialHash(const void* bytes,std::size_t count){const auto* p=static_cast<const unsigned char*>(bytes);std::uint32_t h=2166136261u;
    for(std::size_t i=0;i<count;++i){h^=p[i];h*=16777619u;}return h;}
template<class Shader>MaterialShader DescribeMaterialShader(Shader* shader) {
    for(const auto& cached:materialShaders)if(cached.shader==shader){++materialShaderCacheHits;return cached;}
    MaterialShader next;next.shader=shader;UINT bytes=0;
    if(SUCCEEDED(shader->GetFunction(nullptr,&bytes))&&bytes>=8&&bytes<=65536&&!(bytes%4)) {
        std::vector<std::uint32_t> code(bytes/4);
        if(SUCCEEDED(shader->GetFunction(code.data(),&bytes))){next.hash=MaterialHash(code.data(),bytes);NativeShaderConstantMap::Parse(code.data(),bytes,next.map);}
    }
    ++materialShaderLoads;
    if(materialShaders.size()>=64){materialShaders.front().shader->Release();materialShaders.erase(materialShaders.begin());}
    shader->AddRef();materialShaders.push_back(next);return next;
}
}
void InvalidateMaterialProbe() {
    materialBound={};materialPending={};materialDevice=nullptr;
    for(auto& cached:materialShaders)if(cached.shader)cached.shader->Release();materialShaders.clear();
    materialProbeTick[0]=materialProbeTick[1]=0;
    materialSequenceTick=0;materialSequencePair=0;materialSequenceCount=0;
    AcquireSRWLockExclusive(&materialSequenceLock);materialSequence={};ReleaseSRWLockExclusive(&materialSequenceLock);
}
void AppendNativeDrawContext(NativeDrawContextKL::Context& output) {
    const auto& bound=materialBound;output.weaponBinding=bound.valid;
    output.weaponBindingMatchesEntry=bound.valid&&bound.state==output.state&&bound.entry==output.entry;
    output.weaponPlayer=output.weaponItem=output.weaponRoot=output.weaponModel=0;
    output.weaponReference=0;output.weaponSide=-1;output.weaponSupport=false;
    output.weaponPair=output.weaponGeneration=0;output.weaponSource={};output.weaponTracked={};
    if(!output.weaponBindingMatchesEntry)return;
    output.weaponPlayer=bound.player;output.weaponItem=bound.item;output.weaponRoot=bound.root;output.weaponModel=bound.model;
    output.weaponReference=bound.reference;output.weaponSide=bound.side;output.weaponSupport=bound.supportHeld;
    output.weaponPair=bound.pair;output.weaponGeneration=bound.generation;
    output.weaponSource=bound.source;output.weaponTracked=bound.tracked;
}
void ObserveMaterialSequence(IDirect3DDevice9* device) {
    if(!device||!materialBound.valid)return;
    const auto bound=materialBound;const auto now=GetTickCount();
    std::uint64_t pair=0,generation=0;std::uintptr_t provider=0;unsigned pass=0;std::uint32_t geometry=0,ref=0;
    if(!NativeHandsKL::DrawFrameActive(pair,&generation)||pair!=bound.pair||generation!=bound.generation)return;
    const bool begin=!materialSequenceTick||now-materialSequenceTick>=2000;
    if(!begin&&(pair!=materialSequencePair||materialSequenceCount>=MaterialSequenceLimit))return;
    if(!NativeHandsKL::ReadCurrentNativeEntry(bound.state,provider,&pass)||provider!=bound.entry||
       !NativeCameraKL::DrawStateActive(bound.state)||!HeldForVisual(bound.player,bound.item,bound.side)||
       !Read(bound.entry+0xF4,geometry)||!geometry||!Read(bound.item+0x58,ref)||ref!=bound.reference)return;
    if(begin){materialSequenceTick=now;materialSequencePair=pair;materialSequenceCount=0;}
    if(materialDevice!=device){for(auto& cached:materialShaders)if(cached.shader)cached.shader->Release();
        materialShaders.clear();materialDevice=device;}
    MaterialSequenceDraw next;next.pass=pass;next.entry=bound.entry;next.geometry=geometry;
    CameraPose world;if(Read(bound.state+0x8C,world))next.worldMatch=!std::memcmp(&world,&bound.tracked,sizeof(world));
    IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;
    if(SUCCEEDED(device->GetVertexShader(&vs))&&vs){next.vsHash=DescribeMaterialShader(vs).hash;vs->Release();}
    if(SUCCEEDED(device->GetPixelShader(&ps))&&ps){next.psHash=DescribeMaterialShader(ps).hash;ps->Release();}
    for(unsigned i=0;i<4;++i){IDirect3DBaseTexture9* texture=nullptr;if(SUCCEEDED(device->GetTexture(i,&texture))&&texture){next.textures[i]=reinterpret_cast<std::uintptr_t>(texture);texture->Release();}}
    IDirect3DSurface9* surface=nullptr;if(SUCCEEDED(device->GetRenderTarget(0,&surface))&&surface){next.renderTarget=reinterpret_cast<std::uintptr_t>(surface);surface->Release();}
    surface=nullptr;if(SUCCEEDED(device->GetDepthStencilSurface(&surface))&&surface){next.depthTarget=reinterpret_cast<std::uintptr_t>(surface);surface->Release();}
    const D3DRENDERSTATETYPE states[]={D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ZFUNC,D3DRS_ALPHABLENDENABLE,D3DRS_COLORWRITEENABLE};
    for(unsigned i=0;i<5;++i){DWORD value=0;if(SUCCEEDED(device->GetRenderState(states[i],&value)))next.renderState[i]=value;}
    AcquireSRWLockExclusive(&materialSequenceLock);
    if(!materialSequenceCount){materialSequence={};materialSequence.valid=true;materialSequence.supportHeld=bound.supportHeld;
        materialSequence.tick=now;materialSequence.reference=ref;materialSequence.pair=pair;materialSequence.weapon=bound.item;}
    materialSequence.draws[materialSequenceCount++]=next;materialSequence.count=materialSequenceCount;
    ReleaseSRWLockExclusive(&materialSequenceLock);
}
bool PostfilterForegroundActive() {
    if(!materialBound.valid)return false;
    const auto& bound=materialBound;
    std::uint64_t pair=0,generation=0;std::uintptr_t provider=0;
    unsigned pass=0;std::uint32_t root=0,model=0,reference=0,owner=0,geometry=0,data=0,player=0;
    if(!NativeHandsKL::DrawFrameActive(pair,&generation)||pair!=bound.pair||generation!=bound.generation||
       !NativeHandsKL::ReadCurrentNativeEntry(bound.state,provider,&pass)||provider!=bound.entry||pass!=9||
       !NativeCameraKL::DrawStateActive(bound.state)||!HeldForVisual(bound.player,bound.item,bound.side)||
       !Read(image+kGameData,data)||!Read(data+0xA2C,player)||player!=bound.player||
       !Read(bound.item+4,root)||root!=bound.root||!Read(bound.item+0xC0,model)||model!=bound.model||
       !Read(root+0x60,owner)||owner!=bound.item||!Read(bound.entry+0xF4,geometry)||
       !Read(bound.item+0x58,reference)||reference!=bound.reference)return false;
    CameraPose world;std::memcpy(&world,reinterpret_cast<const void*>(bound.state+0x8C),sizeof(world));
    if(std::memcmp(&world,&bound.tracked,sizeof(world)))return false;
    std::uintptr_t seen[8]{};
    for(unsigned depth=0;depth<8&&geometry;++depth){
        for(unsigned i=0;i<depth;++i)if(seen[i]==geometry)return false;
        seen[depth]=geometry;
        NativeWeaponOwnership::GeometryLinks links;
        if(!NativeWeaponOwnership::ReadGeometryLinks(geometry,ReadBytes,links))return false;
        if(geometry==bound.root&&links.owner==bound.item)return true;
        geometry=static_cast<std::uint32_t>(links.parent);
    }
    return false;
}
bool SnapshotMaterialSequence(MaterialSequence& output){AcquireSRWLockShared(&materialSequenceLock);output=materialSequence;
    ReleaseSRWLockShared(&materialSequenceLock);return output.valid;}
void ObserveNativeDraw(IDirect3DDevice9* device) {
    if(!device||!materialBound.valid)return;
    const auto bound=materialBound;const unsigned bucket=bound.supportHeld?1:0;const auto now=GetTickCount();
    if(materialProbeTick[bucket]&&now-materialProbeTick[bucket]<500)return;
    ProfileTiming::Scope profile(ProfileTiming::SkinMaterialEvidenceOwn);
    std::uint64_t pair=0,generation=0;std::uintptr_t provider=0;unsigned pass=0;std::uint32_t root=0,model=0,reference=0,data=0,player=0,owner=0,geometry=0;
    if(!NativeHandsKL::DrawFrameActive(pair,&generation)||pair!=bound.pair||generation!=bound.generation||
       !NativeHandsKL::ReadCurrentNativeEntry(bound.state,provider,&pass)||provider!=bound.entry||
       !NativeCameraKL::DrawStateActive(bound.state)||!HeldForVisual(bound.player,bound.item,bound.side)||
       !Read(image+kGameData,data)||!Read(data+0xA2C,player)||player!=bound.player||
       !Read(bound.item+4,root)||root!=bound.root||!Read(bound.item+0xC0,model)||model!=bound.model||
       !Read(root+0x60,owner)||owner!=bound.item||!Read(bound.entry+0xF4,geometry)||
       !Read(bound.item+0x58,reference)||reference!=bound.reference)return;
    bool ownedGeometry=false;std::uintptr_t seen[8]{};
    for(unsigned depth=0;depth<8&&geometry;++depth){
        for(unsigned i=0;i<depth;++i)if(seen[i]==geometry)return;seen[depth]=geometry;
        NativeWeaponOwnership::GeometryLinks links;if(!NativeWeaponOwnership::ReadGeometryLinks(geometry,ReadBytes,links))return;
        if(geometry==bound.root&&links.owner==bound.item){ownedGeometry=true;break;}geometry=std::uint32_t(links.parent);
    }
    if(!ownedGeometry)return;
    materialProbeTick[bucket]=now;
    MaterialSample sample;sample.valid=true;sample.supportHeld=bound.supportHeld;sample.tick=now;sample.pair=pair;sample.pass=pass;
    sample.entry=bound.entry;sample.state=bound.state;sample.weapon=bound.item;sample.model=bound.model;sample.reference=reference;
    sample.sourceWorld=bound.source;sample.trackedWorld=bound.tracked;
    std::memcpy(&sample.actualWorld,reinterpret_cast<const void*>(bound.state+0x8C),sizeof(CameraPose));
    std::memcpy(&sample.view,reinterpret_cast<const void*>(bound.state+0xBC),sizeof(CameraPose));
    std::memcpy(sample.projection,reinterpret_cast<const void*>(bound.state+0x3F8),sizeof(sample.projection));
    sample.worldMatch=!std::memcmp(&sample.actualWorld,&bound.tracked,sizeof(CameraPose));if(!sample.worldMatch)++materialWorldMismatches;
    std::uint32_t lights=0;if(Read(bound.state+0x3C0,lights)){sample.lightSource=lights;ReadBytes(lights,sample.nativeLights,sizeof(sample.nativeLights));}
    if(materialDevice!=device){
        for(auto& cached:materialShaders)if(cached.shader)cached.shader->Release();
        materialShaders.clear();materialDevice=device;
    }
    const auto readRange=[&](const NativeShaderConstantMap::Range& range,float* output,unsigned maximum,bool pixel){
        const auto n=std::min(range.count,maximum);if(!n)return 0u;
        const auto result=pixel?device->GetPixelShaderConstantF(range.start,output,n):device->GetVertexShaderConstantF(range.start,output,n);
        return SUCCEEDED(result)?n:0u;};
    IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;
    if(SUCCEEDED(device->GetVertexShader(&vs))&&vs) {
        const auto desc=DescribeMaterialShader(vs);sample.vsHash=desc.hash;
        sample.mvpRegister=desc.map.mvp.start;sample.mvpCount=readRange(desc.map.mvp,sample.mvp,4,false);
        sample.eyeRegister=desc.map.eye.start;sample.eyeCount=readRange(desc.map.eye,sample.eye,1,false);
        sample.vsLowLights=!desc.map.lights.count&&desc.map.lights11.count;
        const auto range=sample.vsLowLights?desc.map.lights11:desc.map.lights;sample.vsLightRegister=range.start;
        sample.vsLightCount=readRange(range,sample.vsLights,7,false);vs->Release();
    }
    if(SUCCEEDED(device->GetPixelShader(&ps))&&ps) {
        const auto desc=DescribeMaterialShader(ps);sample.psHash=desc.hash;sample.psLowLights=!desc.map.lights.count&&desc.map.lights11.count;
        const auto range=sample.psLowLights?desc.map.lights11:desc.map.lights;sample.psLightRegister=range.start;
        sample.psLightCount=readRange(range,sample.psLights,7,true);ps->Release();
    }
    const D3DRENDERSTATETYPE states[]={D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ZFUNC,D3DRS_ALPHABLENDENABLE,
        D3DRS_SRCBLEND,D3DRS_DESTBLEND,D3DRS_COLORWRITEENABLE,D3DRS_DEPTHBIAS,D3DRS_SLOPESCALEDEPTHBIAS};
    for(unsigned i=0;i<9;++i){DWORD value=0;if(SUCCEEDED(device->GetRenderState(states[i],&value)))sample.renderState[i]=value;}
    AcquireSRWLockExclusive(&materialLock);materialSamples[bucket]=sample;ReleaseSRWLockExclusive(&materialLock);++materialSampleCount;
}
bool SnapshotMaterial(bool supportHeld,MaterialSample& output){AcquireSRWLockShared(&materialLock);output=materialSamples[supportHeld?1:0];
    ReleaseSRWLockShared(&materialLock);return output.valid;}
void PublishHeadGaze(const HeadGaze& gaze) {
    const auto epoch=headGazeEpoch.load();const auto now=GetTickCount();
    if(!NativeGrenadeKLMath::ValidGaze(gaze,now)){InvalidateHeadGaze();return;}
    PublishedGaze next;next.gaze=gaze;next.epoch=epoch;
    if(!NativeGrenadeKLMath::Unit(gaze.direction,next.gaze.direction)){InvalidateHeadGaze();return;}
    const Vec3 horizontal{next.gaze.direction.x,0,next.gaze.direction.z};
    const bool horizontalValid=Dot(horizontal,horizontal)>=1e-6f&&NativeGrenadeKLMath::Unit(horizontal,next.horizontal);
    AcquireSRWLockExclusive(&headGazeLock);
    if(!horizontalValid&&publishedHeadGaze.epoch==epoch&&publishedHeadGaze.gaze.player==gaze.player&&
       publishedHeadGaze.gaze.sourceCamera==gaze.sourceCamera&&publishedHeadGaze.gaze.recenterAck==gaze.recenterAck)
        next.horizontal=publishedHeadGaze.horizontal;
    publishedHeadGaze=next;ReleaseSRWLockExclusive(&headGazeLock);
}
void InvalidateHeadGaze(){headGazeEpoch.fetch_add(1);}
bool ScopeQueryActive(std::uintptr_t player,std::uintptr_t weapon) {
    return player&&weapon&&scopeQuery.player==player&&scopeQuery.weapon==weapon;
}
bool SetWorldBindObserver(WorldBindObserver observer) {
    worldBindObserver.store(observer,std::memory_order_relaxed);
    return observer?EnsureWorldHook():worldHookInstalled.load();
}
void SetProjectionBindObserver(ProjectionBindObserver observer){projectionBindObserver.store(observer,std::memory_order_release);}
bool InitializeScope(LogCallback log) {
    if(scopeImage)return true;
    if(!VerifiedImage()||!NativeScopeKL::VerifyImage(image,ReadBytes)) {
        if(log)log("Native K&L scope: sniper/precision/owner signatures differ; optics proof unavailable.");return false;
    }
    const auto mh=MH_Initialize();
    if(mh!=MH_OK&&mh!=MH_ERROR_ALREADY_INITIALIZED)return false;
    auto* precision=reinterpret_cast<void*>(image+0x231DE0);
    if(MH_CreateHook(precision,reinterpret_cast<void*>(&PrecisionHook),reinterpret_cast<void**>(&originalPrecision))!=MH_OK)return false;
    if(MH_EnableHook(precision)!=MH_OK){MH_RemoveHook(precision);originalPrecision=nullptr;return false;}
    if(NativeScopeOverlayKL::Verify(image,ReadBytes)) {
        auto* clear=reinterpret_cast<void*>(image+NativeScopeOverlayKL::ClearRva);
        if(MH_CreateHook(clear,reinterpret_cast<void*>(&OverlayClearHook),reinterpret_cast<void**>(&originalOverlayClear))==MH_OK) {
            if(MH_EnableHook(clear)==MH_OK) {
                activateScopeOverlay=reinterpret_cast<OverlayActivate>(image+NativeScopeOverlayKL::ActivateRva);
                scopeOverlayVerified=true;
            } else {MH_RemoveHook(clear);originalOverlayClear=nullptr;}
        }
    }
    scopeImage=image;
    if(log)log("Native K&L scope: owned current sniper category and native precision latch verified independently of weapon-aim settings.");
    return true;
}
NativeScopeKL::State ReadScope() {
    const auto base=scopeImage.load();return base?NativeScopeKL::Snapshot(base,GetTickCount(),ReadBytes):NativeScopeKL::State{};
}
bool ScopeOverlayActive() {
    if(!scopeOverlayVerified||!scopeGestureContext||!VRBridge::Ready()||NativeCameraKL::CinematicActive())return false;
    const auto optic=ReadScope();if(!optic.valid||!optic.active)return false;
    const auto overlay=NativeScopeOverlayKL::Capture(image,optic,ReadBytes);
    return overlay.valid&&overlay.active&&overlay.mode==1;
}
void SetScopedDrawFrame(std::uintptr_t player,std::uint64_t pairId,bool firstPerson) {
    scopedDraw={};
    if(!scopeImage||!player||!pairId||!firstPerson)return;
    const auto optic=ReadScope();
    if(!optic.valid||!optic.active||optic.player!=player||optic.hand!=1||optic.category!=15)return;
    scopedDraw.optic=optic;scopedDraw.pair=pairId;scopedDraw.active=true;++scopeDrawFrames;
}
void ClearScopedDrawFrame(bool preserveHeldPair){
    scopedDraw={};stowedPending={};stowedBound={};
    if(!preserveHeldPair){heldDrawPose[0]={};heldDrawPose[1]={};}
}
bool SuppressStowedDraw() {
    if(!stowedBound.valid)return false;
    const auto& bound=stowedBound;bool firstPerson=false;std::uint64_t pair=0,generation=0;
    std::uint32_t root=0,owner=0,reference=0,data=0,player=0,model=0,category=~0u;CameraPose world;
    // Only the current first-person local draw may hide a firearm. Native
    // shadow/reflection/cutscene views and a newly equipped item stay visible.
    if(!NativeCameraKL::DrawStateActive(bound.state,&firstPerson)||!firstPerson||
       !NativeHandsKL::DrawFrameActive(pair,&generation)||pair!=bound.pair||generation!=bound.generation||
       !Read(image+kGameData,data)||!Read(data+0xA2C,player)||player!=bound.player||
       !Read(bound.item+4,root)||root!=bound.root||!Read(root+0x60,owner)||owner!=bound.item||
       !Read(bound.item+0x58,reference)||reference!=bound.reference||!Firearm(bound.item)||
       !Read(bound.item+0xC0,model)||!Read(model+NativeWeaponTypeKL::CategoryOffset,category)||(category>6&&category!=15)||
       !NativeWeaponOwnership::StillAttached(image,bound.attachments,bound.slot,ReadBytes)||
       HeldBy(bound.player,bound.item,0,false)||HeldBy(bound.player,bound.item,1,false)||
       !Read(bound.state+0x8C,world)||std::memcmp(&world,&bound.world,sizeof(world))) {
        stowedBound={};return false;
    }
    ++stowedHidden;
    return true;
}
bool SuppressScopedDraw() {
    if(ReloadDiagnostic::Suppress())return true;
    if(!scopedDraw.tag)return false;
    bool firstPerson=false;std::uint64_t pair=0,generation=0;std::uintptr_t geometry=0;CameraPose world;
    const auto optic=ReadScope();
    if(!scopedDraw.active||scopedDraw.draws>=64||GetTickCount()-scopedDraw.optic.tick>150||
       !NativeCameraKL::DrawStateActive(scopedDraw.state,&firstPerson)||!firstPerson||
       !NativeHandsKL::DrawFrameActive(pair,&generation)||pair!=scopedDraw.pair||generation!=scopedDraw.generation||
       !SameScopeOwner(scopedDraw.optic,optic)||!optic.active||
       !ScopedEntry(scopedDraw.entry,geometry)||geometry!=scopedDraw.geometry||
       !Read(scopedDraw.state+0x8C,world)||std::memcmp(&world,&scopedDraw.world,sizeof(world))) {
        scopedDraw.tag=false;++scopeDrawRejected;return false;
    }
    ++scopedDraw.draws;++scopeDrawSuppressed;return true;
}
void SetScopeGestureEnabled(bool enabled) {scopeGestureEnabled=enabled;if(!enabled)InvalidateScopeGesture();}
void SetTrackedReloadEnabled(bool enabled){trackedReloadEnabled=enabled;}
void SetTrackedReloadContext(bool gameplay){trackedReloadContext=gameplay;}
void SetPhysicalReloadEnabled(bool enabled){ReloadDiagnostic::configured=enabled;}
bool ReloadHandBusy(std::uintptr_t player){if(!ReloadDiagnostic::Enabled())return false;auto v=ReloadDiagnostic::Current();return v.supportBusy&&v.player==player&&ReloadDiagnostic::Owns(v.item);}
bool ReloadPropsPending(){return ReloadDiagnostic::renderProp.load(std::memory_order_relaxed);}
unsigned SnapshotReloadProps(std::uintptr_t player,ReloadProps& output){
    const auto v=ReloadDiagnostic::Current();
    if(!v.ready||v.player!=player||v.scoped||GetTickCount()-v.tick>150||!ReloadDiagnostic::Owns(v.item))return 0;
    unsigned count=0;
    if(v.ak74&&v.roundTick){
        const auto age=GetTickCount()-v.roundTick;
        if(age<=360){
            const float seconds=float(age)*.001f;
            auto round=v.roundPose;
            round.position=Add(round.position,Scale(round.basis.right,v.units*(.018f+1.3f*seconds)));
            round.position=Add(round.position,Scale(Vec3{0,1,0},v.units*(.8f*seconds-4.905f*seconds*seconds)));
            output[count++]={round,Scale({.0055f,.0055f,.026f},v.units),false};
        }
    }
    if(!v.held||v.loader!=PhysicalReloadKL::Loader::Shell)return count;
    auto pose=v.magazine;
    auto socket=v.reloadSocket;
    NativeHandsKL::VisualFrame hands;
    bool attachedToPalm=false;
    if(NativeHandsKL::SnapshotDrawVisual(hands)&&hands.player==player){
        const auto* local=ReloadDiagnostic::HeldPalm(v,hands);
        if(v.phase==unsigned(PhysicalReloadKL::Phase::Holding)&&local){
            const auto palm=ReloadMathKL::ToWorld(hands.finalWrist[0],*local);
            pose=ShotgunReloadKL::ShellGrip(palm,v.magHalf,v.units,v.breech);
            attachedToPalm=ValidatePose(pose,.05f);
            if(!attachedToPalm)pose=v.magazine;
        }else if(hands.tracked[1]){
            const auto& wrist=hands.finalWrist[1];
            pose=ReloadMathKL::ToWorld(wrist,ReloadMathKL::ToLocal(v.wrist,pose));
            socket=ReloadMathKL::ToWorld(wrist,ReloadMathKL::ToLocal(v.wrist,socket));
        }
    }
    if(v.phase==unsigned(PhysicalReloadKL::Phase::Finishing)){
        if(Dot(Subtract(pose.position,socket.position),socket.basis.up)<-.014f*v.units)return count;
    }
    output[count++]={pose,v.magHalf,!v.breech};
    for(unsigned i=1;i<v.shellRounds&&count<output.size();++i){
        auto spare=attachedToPalm?ShotgunReloadKL::HeldShell(pose,i,v.magHalf,v.units):v.spareShells[i-1];
        if(!attachedToPalm&&hands.player==player&&hands.tracked[1]){
            spare=ReloadMathKL::ToWorld(hands.finalWrist[1],ReloadMathKL::ToLocal(v.wrist,spare));
        }
        output[count++]={spare,v.magHalf,!v.breech};
    }
    return count;
}
void RecordReloadPropDraw(unsigned count){ReloadDiagnostic::spareDraws+=count;}
void DrawDroppedMagazines(IDirect3DDevice9* device,const D3D9Capture::DrawCall& call){ReloadDiagnostic::DrawCopies(device,call);}
void ServiceReloadFeedback(){ReloadDiagnostic::FeedbackWorker();}
bool PreferReloadSlide(const HeldSupport& held,const CameraPose& gunWrist,const CameraPose& freeWrist,const CameraPose* palmLocal,std::uint32_t recenter){
    if(!ReloadDiagnostic::Enabled())return false;
    const auto v=ReloadDiagnostic::Current();
    if(!v.ready||!v.slideAllowed||!ReloadDiagnostic::SameOwner(v,held.player,held.weapon,held.model,held.reference,recenter)||
       GetTickCount()-v.tick>150)return false;
    const auto* local=palmLocal?palmLocal:v.palmValid?&v.palmLocal:nullptr;
    const auto palm=local?ReloadMathKL::ToWorld(freeWrist,*local).position:freeWrist.position;
    const auto& slide=v.slideLocal;
    if(held.category==4&&!v.needsChamber){
        const auto support=ReloadMathKL::ToWorld(gunWrist,held.socket);
        const auto distance=Subtract(freeWrist.position,support.position);
        if(Dot(distance,distance)<.065f*.065f*v.units*v.units)return false;
    }
    const auto palmInGun=ReloadMathKL::Inverse(gunWrist.basis,Subtract(palm,gunWrist.position));
    if(v.loader==PhysicalReloadKL::Loader::Shell){
        Vec3 anchor;
        return ShotgunReloadKL::PumpContact(slide,palmInGun,v.units,anchor,v.breech,v.integratedTube);
    }
    return ReloadSlideCaptureKL::HandNear(palmInGun,
        ReloadMathKL::Inverse(gunWrist.basis,Subtract(freeWrist.position,gunWrist.position)),v.units,held.category,v.needsChamber,
        slide.a,slide.b,slide.radius/v.units,slide.box.world,slide.box.half,true);
}
bool ReloadHandTarget(std::uintptr_t player,unsigned side,const CameraPose& gunWrist,CameraPose& target){return ReloadDiagnostic::HandTarget(player,side,gunWrist,target);}
bool BackgroundReloadIntentReady(){
    const auto view=ReloadDiagnostic::Current();
    if(view.breech)return !BackgroundDiagnostic::Rack()&&view.ready&&(!BackgroundDiagnostic::ScopedReload()||view.scoped)&&
        !view.boltOpen&&ReloadDiagnostic::Owns(view.item);
    return view.ready&&view.inserted&&ReloadDiagnostic::Owns(view.item);
}
void BackgroundReloadControls(HandsIPC::Frame& sample){
    if(!BackgroundDiagnostic::Controls())return;
    const auto rest=sample.hands[0];static HandsIPC::Pose previous{};static float previousGrip=0;static bool remembered=false;
    if(remembered){sample.hands[0]=previous;sample.grip[0]=previousGrip;}
    struct Save {HandsIPC::Frame& sample;HandsIPC::Pose& pose;float& grip;bool& valid;~Save(){pose=sample.hands[0];grip=sample.grip[0];valid=true;}} save{sample,previous,previousGrip,remembered};
    const auto v=ReloadDiagnostic::Current();NativeHandsKL::VisualFrame hands;NativeHandsKL::ArmDiagnostic arm;
    if(!v.ready)return;
    if(v.scoped){
        if(!NativeHandsKL::SnapshotReloadControllers(v.player,static_cast<std::uint32_t>(VRBridge::RecenterAck()),hands))return;
        hands.palmLocal[0]=v.palmLocal;hands.palmValid[0]=v.palmValid;
        std::memcpy(arm.cameraRawHeadOrientation,sample.head.orientation,sizeof(arm.cameraRawHeadOrientation));
    }else if(!NativeHandsKL::SnapshotVisual(hands)||hands.player!=v.player||GetTickCount()-hands.tick>150||
              !NativeHandsKL::SnapshotArmDiagnostic(0,arm))return;
    const auto rebase=[&](const CameraPose& pose){
        return ReloadMathKL::ToWorld(hands.finalWrist[1],ReloadMathKL::ToLocal(v.wrist,pose));
    };
    static unsigned phase=UINT32_MAX;static DWORD began=0;const auto now=GetTickCount();if(phase!=v.phase){phase=v.phase;began=now;}
    CameraPose palm=hands.palmValid[0]?ReloadMathKL::ToWorld(hands.controllerWrist[0],hands.palmLocal[0]):hands.controllerWrist[0];
    if(v.loader==PhysicalReloadKL::Loader::Shell){
        if(!ReloadDiagnostic::ShotgunReload::ReplayPose(v,hands,palm,sample.grip[0]))return;
    }else if(v.phase==unsigned(PhysicalReloadKL::Phase::Supply)||(v.loader!=PhysicalReloadKL::Loader::Cylinder&&
        BackgroundDiagnostic::PreGrab()&&BackgroundDiagnostic::Age()>=30000&&BackgroundDiagnostic::Age()<32000&&v.inserted&&!v.held)){
        ReloadBodyPocketKL::Input in;in.player=v.player;in.scene=v.reference;in.reference=hands.recenterAck;in.pairId=hands.pairId;in.tick=now;in.sample=hands.sourcePoseTick;in.units=hands.unitsPerMetre;in.head=hands.centre;in.rootAnchor=hands.rawRoot.position;in.rootAnchorValid=hands.bodyAvailableForMesh;
        ReloadBodyPocketKL::State state;ReloadBodyPocketKL::Volume volume;if(!state.Step(in,volume))return;
        palm.position=Subtract(volume.pose.position,Scale(hands.centre.basis.right,.29f*in.units));
        const auto elapsed=now-began;
        // A lost pose can require a new open-hand sample before pickup.
        sample.grip[0]=(v.loader==PhysicalReloadKL::Loader::Cylinder?elapsed%1200:elapsed)>=250?1.f:0.f;
    }else if(v.held){
        if(v.inserted&&BackgroundDiagnostic::PreGrab()&&BackgroundDiagnostic::Age()<32000){sample.hands[0]=rest;sample.grip[0]=1;return;}
        if(v.phase==unsigned(PhysicalReloadKL::Phase::Holding)&&now-began<400){sample.hands[0]=rest;sample.grip[0]=1;return;}
        auto grip=rebase(v.reloadSocket);
        grip.position=Subtract(grip.position,Scale(grip.basis.forward,v.feedLength));
        palm=ReloadPresentationKL::PalmFromGrip(grip,v.magHalf,hands.unitsPerMetre,v.mp5);
        sample.grip[0]=v.phase==unsigned(PhysicalReloadKL::Phase::Finishing)&&now-began>=200?0.f:1.f;
    }else if(v.loader==PhysicalReloadKL::Loader::MagazineAndSlide&&BackgroundDiagnostic::Rack()&&BackgroundDiagnostic::Age()>=33700&&BackgroundDiagnostic::Age()<39650){
        static DWORD approached=0,released=0;
        static unsigned gripsCompleted=0;
        if(gripsCompleted==2){sample.hands[0]=rest;sample.grip[0]=0;return;}
        palm.position=rebase(v.slideAnchor).position;
        if(released&&now-released<260)sample.grip[0]=0.f;
        else if(v.phase!=unsigned(PhysicalReloadKL::Phase::Slide)){
            // Wait at the actual contact until a native poll sees the grasp.
            if(!approached)approached=now;
            sample.grip[0]=(now-approached)%1050>=250?1.f:0.f;
        }else{
            const float t=float(now-began);
            const float first=std::clamp(std::min((t-80.f)/190.f,(660.f-t)/220.f),0.f,1.f);
            const float second=gripsCompleted?0.f:std::clamp(std::min((t-750.f)/190.f,(1330.f-t)/220.f),0.f,1.f);
            const float nearForward=v.category!=0&&t>=270.f?.016f:0.f;
            const float pull=std::max({nearForward,first*.10f,second*.10f});
            const auto barrel=ReloadMathKL::Transform(hands.finalWrist[1].basis,v.barrelLocal);
            palm.position=Subtract(palm.position,Scale(barrel,pull*hands.unitsPerMetre));
            const bool done=t>=(gripsCompleted?890.f:1560.f);
            sample.grip[0]=done?0.f:1.f;
            if(done){++gripsCompleted;released=now;approached=0;}
        }
    }else return;
    CameraPose wrist=palm;if(hands.palmValid[0]&&!NativeWeaponMath::ParentFrame(palm,hands.palmLocal[0],wrist))return;
    Vec3 position;Quat orientation;
    if(!ReloadTrackingPoseKL::Encode(hands.centre,{arm.cameraRawHeadOrientation[0],arm.cameraRawHeadOrientation[1],arm.cameraRawHeadOrientation[2],arm.cameraRawHeadOrientation[3]},
       {sample.head.position[0],sample.head.position[1],sample.head.position[2]},wrist,0,hands.unitsPerMetre,position,orientation))return;
    sample.hands[0].position[0]=position.x;sample.hands[0].position[1]=position.y;sample.hands[0].position[2]=position.z;
    sample.hands[0].orientation[0]=orientation.x;sample.hands[0].orientation[1]=orientation.y;sample.hands[0].orientation[2]=orientation.z;sample.hands[0].orientation[3]=orientation.w;
}
const char* ReloadActionText(std::uintptr_t player){
    if(!player||!ReloadDiagnostic::Enabled()||!trackedReloadContext||!VRBridge::Ready())return nullptr;
    const auto v=ReloadDiagnostic::Current();
    if(!v.ready||v.player!=player||GetTickCount()-v.tick>150||v.recenter!=std::uint32_t(VRBridge::RecenterAck()))return nullptr;
    const auto phase=static_cast<PhysicalReloadKL::Phase>(v.phase);
    if(phase==PhysicalReloadKL::Phase::Finishing||phase==PhysicalReloadKL::Phase::Slide)return nullptr;
    using Loader=PhysicalReloadKL::Loader;
    if(v.loader==Loader::Shell){
        if(v.held)return v.breech?"Load round":"Load shells";
        if(v.needsChamber&&v.rounds&&!v.breech)return "Left Grip  Pump";
        if(v.rounds||!v.reserve)return nullptr;
        if(v.breech)return v.boltOpen?"Left Grip  Take round":"\xEE\x84\x80" "A" "\xEE\x84\x80" " Open bolt";
        return "Left Grip  Take shells";
    }
    if(v.inserted&&!v.rounds&&v.reserve)return v.loader==Loader::Cylinder?
        "\xEE\x84\x80" "A" "\xEE\x84\x80" " Open cylinder":"\xEE\x84\x80" "A" "\xEE\x84\x80" " Eject magazine";
    if(!v.inserted)return v.held?(v.loader==Loader::Cylinder?"Load rounds":"Insert magazine"):
        v.reserve?(v.loader==Loader::Cylinder?"Left Grip  Take rounds":"Left Grip  Take magazine"):nullptr;
    return v.needsChamber&&v.rounds?"Left Grip  Rack bolt":nullptr;
}
bool ReloadInstalled(){return reloadInstalled.load()&&trackedReloadEnabled.load();}
void SetWeaponCalibrationContext(CalibrationContext context){
    if(context==CalibrationContext::Unknown){if(calibrationGameplay.exchange(false))calibrationCandidateEpoch.fetch_add(1);return;}
    if(context==CalibrationContext::Transition){if(calibrationGameplay.exchange(false))calibrationCandidateEpoch.fetch_add(1);if(!calibrationTransition.exchange(true))calibrationEpoch.fetch_add(1);}
    else {calibrationTransition=false;calibrationGameplay=true;}
}
void ReloadSimulationTick(std::uintptr_t player) {
    if(!NativeCoverKL::SimulationCallbackActive(player))return;
    ReloadDiagnostic::RestoreBlockedFire(player);
    BackgroundShotAudit::Flush();
    BackgroundWeaponEquip::Tick(player);
    NativeReloadAmmoKL::Snapshot source{};bool handling=false;
    if((BackgroundDiagnostic::Active()||ReloadDiagnostic::Enabled())&&reloadAmmoProfile&&trackedReloadContext&&VRBridge::Ready()&&!NativeCameraKL::CinematicActive()){
        NativeWeaponOwnership::HandState held{};
        const auto capacity=[](std::uintptr_t item,std::uintptr_t method){using Get=int(__thiscall*)(void*);return reinterpret_cast<Get>(method)(reinterpret_cast<void*>(item));};
        if(NativeWeaponOwnership::ReadHandState(player+0x358,ReadBytes,held)&&held.item&&NativeReloadAmmoKL::Capture(image,player,held.item,ReadBytes,capacity,source)){
            reloadEngine.Select(player,source.Token(),source.model,source.category,source.ammo,100.f);
            ReloadDiagnostic::Tick(source);handling=true;
            reloadAmmoCount=source.ammo.count;reloadAmmoReserve=source.ammo.reserve;reloadAmmoCapacity=source.ammo.capacity;reloadAmmoCategory=source.category;++reloadAmmoObserved;
        }else {++reloadAmmoRejected;ReloadDiagnostic::Invalidate();}
    }
    const auto input=VRBridge::ReadSecondaryFire();
    const auto manual=ReloadDiagnostic::Current();
    if(handling)ReloadDiagnostic::HandlingInput(source,manual);else ReloadDiagnostic::dryTrigger={};
    if(ReloadDiagnostic::Owns(manual.item)){
        if(manual.loader==PhysicalReloadKL::Loader::Shell)
            reloadIntent.Simulation(input.valid,input.reloadTick,GetTickCount(),false);
        return;
    }
    if(!input.valid||reloadIntent.AlreadySeen(input.reloadTick))return;
    bool eligible=reloadInstalled&&originalReload&&trackedReloadEnabled&&trackedReloadContext&&VRBridge::Ready()&&
       !NativeCameraKL::CinematicActive()&&!VRBridge::TeamHudVisible()&&!VRBridge::WeaponMenuVisible();
    NativeWeaponOwnership::HandState hand{};NativeReloadKL::Observation observed{};
    if(eligible) {
        const auto action=NativeActionKL::Current();
        eligible=action.valid&&action.player==player&&!action.action&&
           (action.movementKind==NativeActionKL::Movement::Stand||action.movementKind==NativeActionKL::Movement::BaseMove||
            action.movementKind==NativeActionKL::Movement::Cover||action.movementKind==NativeActionKL::Movement::Vehicle)&&
           NativeWeaponOwnership::ReadHandState(player+0x358,ReadBytes,hand)&&hand.item&&
           NativeReloadKL::Capture(image,player,hand.item,ReadBytes,observed)&&observed.side==1;
    }
    // Retire ineligible events so they cannot reload a later weapon.
    if(!reloadIntent.Simulation(input.valid,input.reloadTick,GetTickCount(),eligible))return;
    ++reloadManual;
    originalReload(reinterpret_cast<void*>(player),reinterpret_cast<void*>(hand.item));
}
void SetThrowbackContext(bool gameplay){throwbackGameplayContext=gameplay;}
void SetScopeGestureContext(bool gameplay) {scopeGestureContext=gameplay;if(!gameplay)InvalidateScopeGesture();}
void InvalidateScopeGesture(){scopeGestureEpoch.fetch_add(1);scopeGestureRequested=false;}
void ScopeSimulationTick(std::uintptr_t player) {
    if(!scopeImage||!originalPrecision||!NativeCoverKL::SimulationCallbackActive(player))return;
    ++scopeGestureTicks;
    thread_local HandsIPC::Consumer input;
    thread_local NativeScopeGestureKL::Gesture gesture;
    thread_local ScopeGestureOwner previous;
    const auto epoch=scopeGestureEpoch.load();const auto now=GetTickCount();
    const auto optic=ReadScope();HandsIPC::Frame hands;
    const auto prior=previous;
    const bool same=SameScopeOwner(previous.optic,optic)&&previous.epoch==epoch&&previous.recenter==static_cast<std::uint32_t>(VRBridge::RecenterAck());
    if(!same)gesture.Reset();
    const bool allowed=scopeGestureEnabled&&scopeGestureContext&&optic.valid&&optic.player==player&&optic.hand==1&&optic.category==15&&
        VRBridge::Ready()&&!NativeCameraKL::CinematicActive()&&!NativeActionKL::PreserveRig()&&
        !VRBridge::TeamHudVisible()&&!VRBridge::WeaponMenuVisible();
    const bool fresh=allowed&&input.Open(GetCurrentProcessId())&&input.Read(hands,now)&&
        HandsMath::ValidFrame(hands,now,static_cast<std::uint32_t>(VRBridge::RecenterAck()))&&
        HandsMath::ValidPose(hands.head)&&HandsMath::ValidPose(hands.hands[1]);
    NativeScopeGestureKL::Proximity proximity;
    if(fresh) {
        const auto& h=hands.head;const auto& r=hands.hands[1];
        proximity=NativeScopeGestureKL::Evaluate({h.position[0],h.position[1],h.position[2]},
            {h.orientation[0],h.orientation[1],h.orientation[2],h.orientation[3]},
            {r.position[0],r.position[1],r.position[2]},{r.orientation[0],r.orientation[1],r.orientation[2],r.orientation[3]});
        scopeGestureDistance=proximity.distance;scopeGestureAngle=proximity.angle;
    }
    const auto command=gesture.Tick(allowed,fresh,proximity,now);
    if(!allowed||!fresh)++scopeGestureRejected;
    ScopeGestureOwner next;next.optic=optic;next.epoch=epoch;next.recenter=static_cast<std::uint32_t>(VRBridge::RecenterAck());
    next.tick=now;next.requested=gesture.requested;previous=next;
    AcquireSRWLockExclusive(&scopeGestureLock);scopeGestureOwner=next;ReleaseSRWLockExclusive(&scopeGestureLock);
    scopeGestureRequested=gesture.requested;
    if(command==NativeScopeGestureKL::Command::Enter)++scopeGestureEntered;
    if(command==NativeScopeGestureKL::Command::Exit)++scopeGestureExited;
    // The native setter owns its camera synchronization side effects. No
    // precision byte is written directly; subsequent native input writes
    // pass through the same exact owned gesture certificate above.
    if(allowed&&fresh&&SameScopeOwner(optic,ReadScope()))
        originalPrecision(reinterpret_cast<void*>(optic.aim),gesture.requested);
    else if(prior.requested&&SamePrecisionMover(prior.optic))
        originalPrecision(reinterpret_cast<void*>(prior.optic.aim),false);
    ScopeOverlayTick(next,allowed&&fresh&&gesture.requested);
}
bool CaptureHeldSupport(std::uintptr_t expectedPlayer,float units,HeldSupport& output,bool allowInactive) {
    if(!installed.load()||!supportTypeProof||!expectedPlayer||!std::isfinite(units)||units<1||units>1000)return false;
    HeldSupport next;next.player=expectedPlayer;
    std::uintptr_t data=0,player=0,vtable=0,leftVtable=0,templateVtable=0,owner=0;
    NativeWeaponOwnership::HandState left,right;
    if(!Read(image+kGameData,data)||!Read(data+0xA2C,player)||player!=expectedPlayer||
       !Read(player,vtable)||vtable!=image+0x56582C||
       !Read(player+0x2D8,leftVtable)||leftVtable!=image+kHandVtable||
       !NativeWeaponOwnership::ReadHandState(player+0x2D8,ReadBytes,left)||
       !NativeWeaponOwnership::ReadHandState(player+0x358,ReadBytes,right)||!right.item||
       left.enabled>1||right.enabled>1||
       !HeldBy(player,right.item,1,!allowInactive)||(left.enabled&&left.item&&left.item!=right.item))return false;
    next.active=right.enabled!=0;
    next.weapon=right.item;
    if(!Read(next.weapon+4,next.root)||!Read(next.root+0x60,owner)||owner!=next.weapon||
       !Read(next.weapon+0x58,next.reference)||!Read(next.weapon+0xC0,next.model)||
       !Read(next.model,templateVtable)||(templateVtable!=image+0x54E6D4&&templateVtable!=image+0x577864)||
       !Read(next.model+NativeWeaponTypeKL::CategoryOffset,next.category)||!NativeTwoHandMath::LongGun(next.category)||
       !Read(next.model+0x150,next.marker)||!next.marker||!Read(next.marker+4,next.markerGeometry)||
       !Read(next.markerGeometry+0x60,owner)||owner!=next.marker)return false;
    // +A4 is the actual native category getter; +A0 is the fire-mode mask.
    // A fresh visible draw supplies both original wrists and the original
    // muzzle, avoiding a CPU attachment mixed with another animation sample.
    AcquireSRWLockShared(&renderedGunLock);const auto visible=renderedGun[1];ReleaseSRWLockShared(&renderedGunLock);
    if(!calibrationTransition.load()&&visible.calibrationEpoch==calibrationEpoch.load()&&GetTickCount()-visible.tick<=100&&visible.reference==next.reference&&visible.shot.player==player&&visible.shot.weapon==next.weapon&&
       visible.shot.root==next.root&&visible.model==next.model&&visible.shot.hands.unitsPerMetre==units&&
       visible.shot.hands.availableForMesh[0]&&visible.shot.hands.availableForMesh[1]) {
        const auto& h=visible.shot.hands;
        Vec3 localMuzzle{};
        if(visible.aligned) {
            const auto localSupport=NativeRigMath::ToModel(visible.nativeAttachment,h.rawWrist[0]);
            const auto alignedSupport=NativeRigMath::ToWorld(visible.attachment,{localSupport.basis,localSupport.position});
            next.candidate=NativeTwoHandMath::AuthoredSocket(h.finalWrist[1],alignedSupport,visible.shot.origin,units,next.socket);
            localMuzzle=NativeRigMath::Inverse(h.finalWrist[1].basis,Subtract(visible.shot.origin,h.finalWrist[1].position));
        } else {
            localMuzzle=NativeRigMath::Inverse(h.finalWrist[1].basis,Subtract(visible.shot.origin,h.finalWrist[1].position));
            const auto rawMuzzle=Add(h.rawWrist[1].position,NativeRigMath::Transform(h.rawWrist[1].basis,localMuzzle));
            next.candidate=NativeTwoHandMath::AuthoredSocket(h.rawWrist[1],h.rawWrist[0],rawMuzzle,units,next.socket);
        }
        if(next.candidate&&next.category==4&&ReloadDiagnostic::Enabled()){
            const auto reload=ReloadDiagnostic::Current();
            if(reload.ready&&GetTickCount()-reload.tick<=150&&
               ReloadDiagnostic::SameOwner(reload,player,next.weapon,next.model,next.reference,h.recenterAck)&&
               reload.loader==PhysicalReloadKL::Loader::MagazineAndSlide)
                NativeTwoHandMath::SeparateRifleSocket(next.socket,localMuzzle,reload.slideLocal.box.world.position,
                    reload.barrelLocal,units,reload.ak74?.20f:.14f,reload.ak74?.17f:.12f);
        }
    }
    std::uintptr_t afterRoot=0,afterModel=0,afterMarker=0,afterGeometry=0;std::uint32_t afterReference=0;
    NativeWeaponOwnership::HandState afterLeft;
    if(!HeldBy(player,next.weapon,1,!allowInactive)||!Read(next.weapon+4,afterRoot)||afterRoot!=next.root||
       !Read(next.weapon+0xC0,afterModel)||afterModel!=next.model||
       !Read(next.weapon+0x58,afterReference)||afterReference!=next.reference||
       !Read(next.model+0x150,afterMarker)||afterMarker!=next.marker||
       !Read(next.marker+4,afterGeometry)||afterGeometry!=next.markerGeometry||
       !NativeWeaponOwnership::ReadHandState(player+0x2D8,ReadBytes,afterLeft)||
       afterLeft.enabled>1||
       (afterLeft.enabled&&afterLeft.item&&afterLeft.item!=next.weapon))return false;
    output=next;return true;
}
namespace {
bool SnapshotVisibleAim(AimFrame& output,bool render) {
    const auto action=NativeActionKL::Current();
    if(!installed.load()||!VRBridge::Ready()||NativeCameraKL::CinematicActive()||!action.WeaponTrackingAllowed())return false;
    NativeHandsKL::VisualFrame hands;
    std::uint64_t pair=0,generation=0;
    if(render) {
        if(!NativeHandsKL::DrawFrameActive(pair,&generation)||!generation||
           !NativeHandsKL::SnapshotDrawVisual(hands)||hands.pairId!=pair)return false;
    } else if(!NativeHandsKL::SnapshotVisual(hands))return false;
    std::uintptr_t data=0,player=0;
    if(!Read(image+kGameData,data)||!Read(data+0xA2C,player)||player!=hands.player||(action.valid&&action.player!=player))return false;
    for(int side=1;side>=0;--side) {
        AcquireSRWLockShared(&renderedGunLock);const auto visible=renderedGun[side];ReleaseSRWLockShared(&renderedGunLock);
        std::uintptr_t root=0,model=0,owner=0;std::uint32_t reference=0;
        if(calibrationTransition.load()||visible.calibrationEpoch!=calibrationEpoch.load()||!visible.tick||
           (render?visible.generation!=generation:GetTickCount()-visible.tick>150)||visible.shot.player!=player||
           visible.shot.hands.pairId!=hands.pairId||!hands.active[side]||
           !HeldForVisual(player,visible.shot.weapon,side)||!Read(visible.shot.weapon+4,root)||root!=visible.shot.root||
           !Read(root+0x60,owner)||owner!=visible.shot.weapon||
           !Read(visible.shot.weapon+0xC0,model)||model!=visible.model)continue;
        if(render&&(visible.shot.hands.recenterAck!=hands.recenterAck||visible.shot.hands.unitsPerMetre!=hands.unitsPerMetre||
           !Read(visible.shot.weapon+0x58,reference)||reference!=visible.reference))continue;
        Vec3 direction;
        if(!Finite(visible.shot.origin)||!NativeArmMath::Unit(Subtract(visible.shot.target,visible.shot.origin),direction))continue;
        AimFrame next;next.player=player;next.weapon=visible.shot.weapon;next.root=root;next.model=model;
        next.pairId=hands.pairId;next.tick=visible.tick;next.recenterAck=hands.recenterAck;next.hand=side;next.unitsPerMetre=hands.unitsPerMetre;
        next.origin=visible.shot.origin;next.direction=direction;
        if(render) {
            std::uint64_t afterPair=0,afterGeneration=0;
            if(!NativeHandsKL::DrawFrameActive(afterPair,&afterGeneration)||afterPair!=pair||afterGeneration!=generation)return false;
        }
        output=next;return true;
    }
    return false;
}
}
bool SnapshotAim(AimFrame& output){return SnapshotVisibleAim(output,false);}
bool SnapshotRenderAim(AimFrame& output){return SnapshotVisibleAim(output,true);}
bool SnapshotCaptureAim(AimFrame& output) {
    // Capture is deliberately later than the last mesh's palette/draw lease.
    // Read only its already-rendered muzzle; never solve a new hand pose or
    // authorize a native ROOM/Fire call from this presentation certificate.
    std::uint64_t epoch=0,pair=0;unsigned eye=2;
    if(!installed.load()||!VRBridge::Ready()||!NativeCameraKL::ReadCaptureContext(epoch,eye,pair))return false;
    const auto action=NativeActionKL::Current();
    if(!action.WeaponTrackingAllowed())return false;
    std::uintptr_t data=0,player=0;
    if(!Read(image+kGameData,data)||!Read(data+0xA2C,player)||!player||(action.valid&&action.player!=player))return false;
    for(int side=1;side>=0;--side) {
        AcquireSRWLockShared(&renderedGunLock);const auto visible=renderedGun[side];ReleaseSRWLockShared(&renderedGunLock);
        const auto& hands=visible.shot.hands;
        std::uintptr_t root=0,model=0,owner=0;std::uint32_t reference=0;
        if(calibrationTransition.load()||visible.calibrationEpoch!=calibrationEpoch.load()||!visible.tick||
           visible.imageEpoch!=epoch||visible.imageEye!=eye||visible.shot.player!=player||
           hands.player!=player||hands.pairId!=pair||!hands.active[side]||
           hands.recenterAck!=static_cast<std::uint32_t>(VRBridge::RecenterAck())||
           !HeldForVisual(player,visible.shot.weapon,side)||!Read(visible.shot.weapon+4,root)||root!=visible.shot.root||
           !Read(root+0x60,owner)||owner!=visible.shot.weapon||!Read(visible.shot.weapon+0xC0,model)||model!=visible.model||
           !Read(visible.shot.weapon+0x58,reference)||reference!=visible.reference)continue;
        Vec3 direction;
        if(!Finite(visible.shot.origin)||!NativeArmMath::Unit(Subtract(visible.shot.target,visible.shot.origin),direction))continue;
        AimFrame next;next.player=player;next.weapon=visible.shot.weapon;next.root=root;next.model=model;
        next.pairId=pair;next.tick=GetTickCount();next.recenterAck=hands.recenterAck;next.hand=side;
        next.unitsPerMetre=hands.unitsPerMetre;next.origin=visible.shot.origin;next.direction=direction;
        std::uint64_t afterEpoch=0,afterPair=0;unsigned afterEye=2;
        if(!NativeCameraKL::ReadCaptureContext(afterEpoch,afterEye,afterPair)||afterEpoch!=epoch||afterPair!=pair||afterEye!=eye)return false;
        output=next;return true;
    }
    return false;
}
bool Install(bool enabled,LogCallback log) {
    if(installed.load())return true;
    if(!enabled)return false;
    wchar_t path[MAX_PATH]{};char digest[65]{};
    const DWORD length=GetModuleFileNameW(nullptr,path,MAX_PATH);
    if(!length||length>=MAX_PATH||!HashFile(path,digest)||!IsSupportedExecutableHash(digest)) {
        if(log)log("Native K&L weapon aim: EXE hash differs; weapon targeting remains native.");return false;
    }
    image=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    worldSourceProof=NativeWeaponWorldSource::Verify(image,ReadBytes);
    bonesLightCacheProof=NativeBonesLightCache::Verify(image,ReadBytes);
    supportTypeProof=NativeTwoHandMath::VerifyTypeImage(image,ReadBytes);
    hipAttachmentProof=NativeWeaponOwnership::VerifyHipImage(image,ReadBytes);
    hipRigidProof=hipAttachmentProof&&NativeWeaponOwnership::VerifyHipRigidImage(image,ReadBytes);
    if(!supportTypeProof&&log)log("Native K&L foregrip: weapon category metadata differs; support grip disabled.");
    const unsigned char signature[]={0x51,0x8B,0x44,0x24,0x08,0xD9,0x40,0x04,0xD9,0x5C,0x24,0x08,
                                    0xD9,0x40,0x08,0xD9,0x1C,0x24};
    const std::uintptr_t expected=image+kSetTarget;
    const unsigned char worldSignature[]={0x53,0x8B,0xD9,0x56,0x8B,0x74,0x24,0x0C,0x57,
        0x8D,0x93,0x8C,0x00,0x00,0x00,0x8B,0xFA,0xB9,0x0C,0x00,0x00,0x00,0xF3,0xA5};
    const unsigned char rigidSignature[]={0x8B,0x44,0x24,0x08,0x81,0xEC,0xBC,0x00,0x00,0x00,
        0x53,0x55,0x8B,0xE9,0x8B,0x48,0x04,0x8B,0x41,0x14};
    const unsigned char rigidReturn[]={0x81,0xC4,0xBC,0x00,0x00,0x00,0xC2,0x08,0x00};
    const unsigned char fireBaseSignature[]={0x81,0xEC,0xE0,0,0,0,0x83,0x3D,0x88,0x36,0xC7,0,0,0x53,0x55,0x56,0x57};
    const unsigned char fireFFSignature[]={0x81,0xEC,0x3C,1,0,0,0x53,0x55,0x56,0x8B,0xF1,0x8B,0x06,0x8B,0x90,0xD0,2,0,0};
    const unsigned char attachmentSignature[]={0x57,0x8B,0xF9,0x8B,0x4F,4,0xF7,0x41,0x3C,0,0,0x20,0};
    const unsigned char attachmentReturn[]={0xC2,8,0};
    const unsigned char cachedMuzzleReturn[]={0xC2,4,0};
    const unsigned char cachedMuzzleSignature[]={0x51,0x8B,0x44,0x24,0x08,0xD9,0x40,0x04,
        0xD9,0x5C,0x24,0x08,0xD9,0x40,0x08,0xD9,0x1C,0x24,0xD9,0x00,
        0xD9,0x99,0x34,0x01,0,0};
    const unsigned char cachedMuzzleConsumedSignature[]={0xD9,0x96,0x3C,0x01,0,0,
        0xD9,0x96,0x38,0x01,0,0,0xD9,0x9E,0x34,0x01,0,0};
    std::uintptr_t actual=0,rigidMethod=0;
    if(std::memcmp(reinterpret_cast<const void*>(expected),signature,sizeof(signature))||
       !Read(image+0x54EA4C+0x2D4,actual)||actual!=expected||
       (!worldHookInstalled&&std::memcmp(reinterpret_cast<const void*>(image+kWorldBind),worldSignature,sizeof(worldSignature)))||
       std::memcmp(reinterpret_cast<const void*>(image+kRigidBind),rigidSignature,sizeof(rigidSignature))||
       std::memcmp(reinterpret_cast<const void*>(image+0x73BDE),rigidReturn,sizeof(rigidReturn))||
       !Read(image+kRigidVtable+0x18,rigidMethod)||rigidMethod!=image+kRigidBind) {
        if(log)log("Native K&L weapon aim: target setter/vtable signatures differ; hook disabled.");return false;
    }
    std::uintptr_t fireBaseMethod=0,fireFFMethod=0,attachmentMethod=0;
    if(std::memcmp(reinterpret_cast<const void*>(image+kFireBase),fireBaseSignature,sizeof(fireBaseSignature))||
       std::memcmp(reinterpret_cast<const void*>(image+kFireFF),fireFFSignature,sizeof(fireFFSignature))||
       std::memcmp(reinterpret_cast<const void*>(image+kAttachment),attachmentSignature,sizeof(attachmentSignature))||
       std::memcmp(reinterpret_cast<const void*>(image+kCachedMuzzleSetter),cachedMuzzleSignature,sizeof(cachedMuzzleSignature))||
       std::memcmp(reinterpret_cast<const void*>(image+0x2795CE),cachedMuzzleReturn,sizeof(cachedMuzzleReturn))||
       std::memcmp(reinterpret_cast<const void*>(image+0x279EA6),cachedMuzzleConsumedSignature,sizeof(cachedMuzzleConsumedSignature))||
       std::memcmp(reinterpret_cast<const void*>(image+0x11F9FC),attachmentReturn,sizeof(attachmentReturn))||
       *reinterpret_cast<const unsigned char*>(image+0x123971)!=0xC3||
       *reinterpret_cast<const unsigned char*>(image+0x27A9C4)!=0xC3||
       !Read(image+0x54EA4C+0x344,fireBaseMethod)||fireBaseMethod!=image+kFireBase||
       !Read(image+0x577B74+0x344,fireFFMethod)||fireFFMethod!=image+kFireFF||
       !Read(image+0x577B74+0x25C,attachmentMethod)||attachmentMethod!=image+kAttachment) {
        if(log)log("Native K&L weapon: actual Fire/muzzle signatures differ; hooks disabled.");return false;
    }
    setCachedMuzzle=reinterpret_cast<SetTarget>(image+kCachedMuzzleSetter);
    void* targets[]={reinterpret_cast<void*>(expected),reinterpret_cast<void*>(image+kWorldBind),reinterpret_cast<void*>(image+kRigidBind),
        reinterpret_cast<void*>(image+kFireBase),reinterpret_cast<void*>(image+kFireFF),reinterpret_cast<void*>(image+kAttachment)};
    void* detours[]={reinterpret_cast<void*>(&SetTargetHook),reinterpret_cast<void*>(&WorldBindHook),reinterpret_cast<void*>(&RigidBindHook),
        reinterpret_cast<void*>(&FireBaseHook),reinterpret_cast<void*>(&FireFFHook),reinterpret_cast<void*>(&AttachmentHook)};
    void** originals[]={reinterpret_cast<void**>(&original),reinterpret_cast<void**>(&originalWorld),reinterpret_cast<void**>(&originalRigid),
        reinterpret_cast<void**>(&originalFireBase),reinterpret_cast<void**>(&originalFireFF),reinterpret_cast<void**>(&originalAttachment)};
    const bool existing[]={false,worldHookInstalled.load(),false,false,false,false};
    unsigned created=0;bool okay=true;
    for(;created<6;++created)if(!existing[created]&&MH_CreateHook(targets[created],detours[created],originals[created])!=MH_OK){okay=false;break;}
    if(okay)for(unsigned i=0;i<6;++i)if(!existing[i]&&MH_QueueEnableHook(targets[i])!=MH_OK){okay=false;break;}
    if(okay)okay=MH_ApplyQueued()==MH_OK;
    if(!okay) {
        for(unsigned i=0;i<created;++i)if(!existing[i]){MH_QueueDisableHook(targets[i]);MH_DisableHook(targets[i]);}
        MH_ApplyQueued();for(unsigned i=0;i<created;++i)if(!existing[i])MH_RemoveHook(targets[i]);
        if(!existing[1])originalWorld=nullptr;
        if(log)log("Native K&L weapon aim: target hook installation failed.");return false;
    }
    installed=true;worldHookInstalled=true;
    if(log)log("Native K&L weapon: meshes follow wrists/torso; actual native Fire uses tracked muzzle/controller ray with native spread, ammo and collision preserved.");
    InstallGrenade(log);
    InstallThrowback(log);
    InstallSmokeLoop(log);
    InstallReload(log);
    return true;
}
Stats Snapshot(){Stats result{installed.load(),calls.load(),tracked.load(),passthrough.load(),lastWeapon.load(),lastHand.load(),
    worldCalls.load(),meshCandidates.load(),meshOwned.load(),meshTracked.load(),meshLagged.load(),lastMeshOwner.load(),
    rigidCalls.load(),rigidTracked.load(),meshFreshRejected.load(),stowedOwned.load(),stowedTracked.load(),stowedRejected.load()};
    result.stowedHidden=stowedHidden.load();result.meshPoseRecovered=meshPoseRecovered.load();
    result.fireCalls=fireCalls.load();result.fireTracked=fireTracked.load();result.fireRejected=fireRejected.load();
    result.muzzleCalls=muzzleCalls.load();result.muzzleTracked=muzzleTracked.load();result.aimRestored=aimRestored.load();
    result.aimRestoreRejected=aimRestoreRejected.load();result.shotThread=shotThread.load();
    for(unsigned axis=0;axis<3;++axis){result.shotOrigin[axis]=shotOrigin[axis].load();result.shotDirection[axis]=shotDirection[axis].load();}
    result.accessoryOwned=accessoryOwned.load();result.accessoryTracked=accessoryTracked.load();result.accessoryRejected=accessoryRejected.load();
    result.hipAccessoryOwned=hipAccessoryOwned.load();result.hipAccessoryTracked=hipAccessoryTracked.load();result.hipAccessoryRejected=hipAccessoryRejected.load();
    result.hipAttachmentVerified=hipAttachmentProof;result.hipAccessorySlots=hipAccessorySlots.load();
    result.hipRigidCandidates=hipRigidCandidates;result.hipRigidOwned=hipRigidOwned;
    result.hipRigidTracked=hipRigidTracked;result.hipRigidRejected=hipRigidRejected;result.hipRigidBone=hipRigidBone;
    result.hipRigidVerified=hipRigidProof;
    result.hipRigidStandard=hipRigidStandard;
    result.hipAccessoryPrimitive=hipAccessoryPrimitive;
    result.bodyChildCandidates=bodyChildCandidates;result.bodyChildOwned=bodyChildOwned;
    result.bodyChildTracked=bodyChildTracked;result.bodyChildRejected=bodyChildRejected;
    result.bodyGroupSlots=bodyGroupSlots;result.bodyGroupOwned=bodyGroupOwned;result.bodyGroupTracked=bodyGroupTracked;result.bodyGroupRejected=bodyGroupRejected;
    result.cachedMuzzleTracked=cachedMuzzleTracked.load();result.cachedMuzzleConsumed=cachedMuzzleConsumed.load();
    result.cachedMuzzleRestored=cachedMuzzleRestored.load();result.cachedMuzzleChanged=cachedMuzzleChanged.load();
    result.viewRejected=viewRejected.load();
    result.ownershipNegativeHits=ownershipNegativeHits.load();result.ownershipNegativeStored=ownershipNegativeStored.load();
    result.barrelAxisAccepted=barrelAxisAccepted;result.barrelAxisRejected=barrelAxisRejected;
    result.calibrationRebuilds=calibrationRebuilds;result.calibrationRejected=calibrationRejected;result.calibrationMaxScaleError=calibrationMaxScaleError;
    result.heldOwnerNonRoot=heldOwnerNonRoot;result.heldOwnerNonRootToRoot=heldOwnerNonRootToRoot;
    result.heldGrenadeMeshTracked=heldGrenadeMeshTracked;result.instanceWorldTracked=instanceWorldTracked;
    result.viewAlignedWorldTracked=viewAlignedWorldTracked;result.bonesLightCacheInvalidated=bonesLightCacheInvalidated;
    result.worldSourceVerified=worldSourceProof;result.bonesLightCacheVerified=bonesLightCacheProof;
    result.materialSamples=materialSampleCount;result.materialWorldMismatches=materialWorldMismatches;
    result.materialShaderLoads=materialShaderLoads;result.materialShaderCacheHits=materialShaderCacheHits;
    result.barrelAxisCategory=barrelAxisCategory;result.barrelAxisDeviationDegrees=barrelAxisDeviationDegrees;
    result.barrelAxisProofStage=barrelAxisProofStage;for(unsigned i=0;i<3;++i)result.barrelAxisLocal[i]=barrelAxisLocal[i];
    result.grenadeInstalled=grenadeInstalled;result.grenadeUpdates=grenadeUpdates;result.grenadeLocalUpdates=grenadeLocalUpdates;
    result.grenadeTargets=grenadeTargets;result.grenadeGazeApplied=grenadeGazeApplied;result.grenadeGazeRejected=grenadeGazeRejected;
    result.grenadeFireCalls=grenadeFireCalls;result.grenadeFireApplied=grenadeFireApplied;result.grenadeFireRejected=grenadeFireRejected;
    result.grenadeFireBarrelAim=grenadeFireBarrelAim;result.grenadeFireControllerAim=grenadeFireControllerAim;
    result.throwbackInstalled=throwbackInstalled;result.throwbackUpdates=throwbackUpdates;result.throwbackOwnedUpdates=throwbackOwnedUpdates;
    result.smokeLoopInstalled=smokeLoopInstalled;result.smokeLoopReplacements=smokeLoopReplacements;
    result.smokeLoopRetired=smokeLoopRetired;result.smokeLoopRejected=smokeLoopRejected;
    result.throwbackLaunches=throwbackLaunches;result.throwbackApplied=throwbackApplied;result.throwbackRejected=throwbackRejected;
    for(unsigned i=0;i<3;++i){result.throwbackNativeVelocity[i]=throwbackNativeVelocity[i];result.throwbackMappedVelocity[i]=throwbackMappedVelocity[i];}
    for(unsigned i=0;i<3;++i){result.grenadeRequestedDirection[i]=grenadeRequestedDirection[i];result.grenadeNativeAim[i]=grenadeNativeAim[i];
        result.grenadeNativeDelta[i]=grenadeNativeDelta[i];result.grenadeMappedDelta[i]=grenadeMappedDelta[i];}
    result.grenadeContextRejected=grenadeContextRejected;result.grenadeThread=grenadeThread;
    result.grenadeProofStage=grenadeProofStage;result.grenadeMoverVtable=grenadeMoverVtable;
    for(unsigned axis=0;axis<3;++axis)result.grenadeDirection[axis]=grenadeDirection[axis];
    result.scopeGestureEnabled=scopeGestureEnabled;result.scopeGestureRequested=scopeGestureRequested;
    result.scopeGestureTicks=scopeGestureTicks;result.scopeGestureEntered=scopeGestureEntered;result.scopeGestureExited=scopeGestureExited;
    result.scopePrecisionForced=scopePrecisionForced;result.scopeGestureRejected=scopeGestureRejected;
    result.scopeGestureDistanceMetres=scopeGestureDistance;result.scopeGestureAngleDegrees=scopeGestureAngle;
    result.scopeFireCalls=scopeFireCalls;result.scopeFireApplied=scopeFireApplied;result.scopeFireRejected=scopeFireRejected;
    result.scopeRayHits=scopeRayHits;result.scopeRayMisses=scopeRayMisses;result.scopeRayUnavailable=scopeRayUnavailable;
    result.scopeDrawFrames=scopeDrawFrames;result.scopeDrawCandidates=scopeDrawCandidates;
    result.scopeDrawSuppressed=scopeDrawSuppressed;result.scopeDrawRejected=scopeDrawRejected;
    result.reloadInstalled=reloadInstalled;result.reloadRequests=reloadRequests;result.reloadBlocked=reloadBlocked;
    result.reloadManual=reloadManual;result.reloadEmpty=reloadEmpty;result.reloadUnproved=reloadUnproved;
    const auto physical=ReloadDiagnostic::Current();
    for(const auto& world:physical.world)if(world.valid)++result.physicalWorldActive;
    result.physicalDryClicks=ReloadDiagnostic::dryClicks;
    result.physicalSpareDraws=ReloadDiagnostic::spareDraws;result.physicalWorldDraws=ReloadDiagnostic::worldDraws;result.physicalWorldRejected=ReloadDiagnostic::worldRejected;result.physicalImpacts=ReloadDiagnostic::impacts;result.physicalSounds=ReloadDiagnostic::sounds;result.physicalNativeCues=ReloadDiagnostic::nativeCues;
    result.physicalDiagnostic=ReloadDiagnostic::Enabled();result.physicalLoaderReady=physical.ready;result.physicalLoader=unsigned(physical.loader);result.physicalPhase=physical.phase;result.physicalEjected=ReloadDiagnostic::ejected;result.physicalTaken=ReloadDiagnostic::taken;result.physicalInserted=ReloadDiagnostic::inserted;result.physicalRejected=ReloadDiagnostic::rejected;result.physicalRemapped=ReloadDiagnostic::remapped;result.physicalHidden=ReloadDiagnostic::hidden;
    result.reloadAmmoProfile=reloadAmmoProfile;result.reloadAmmoObserved=reloadAmmoObserved;result.reloadAmmoRejected=reloadAmmoRejected;
    result.reloadAmmoCount=reloadAmmoCount;result.reloadAmmoReserve=reloadAmmoReserve;result.reloadAmmoCapacity=reloadAmmoCapacity;result.reloadAmmoCategory=reloadAmmoCategory;
    result.scopeOverlayVerified=scopeOverlayVerified;result.scopeOverlayActivated=scopeOverlayActivated;
    result.scopeOverlayRetained=scopeOverlayRetained;result.scopeOverlayCleared=scopeOverlayCleared;result.scopeOverlayRejected=scopeOverlayRejected;
    result.scopeOverlayProofStage=scopeOverlayProofStage;result.scopeOverlayNativeMode=scopeOverlayNativeMode;
    return result;
}
}
