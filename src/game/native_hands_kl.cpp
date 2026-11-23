#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include "MinHook.h"
#include "native_hands_kl.hpp"
#include "native_hands_math.hpp"
#include "reload_palm_kl.hpp"
#include "native_weapon_kl.hpp"
#include "native_two_hand_math.hpp"
#include "native_rig_math.hpp"
#include "native_skin_math.hpp"
#include "native_shadow_pass_kl.hpp"
#include "native_skin_palette_lease.hpp"
#include "native_held_grenade_visual_kl.hpp"
#include "native_bones_light_cache.hpp"
#include "native_body_kl.hpp"
#include "native_body_math.hpp"
#include "native_passenger_actor_kl.hpp"
#include "native_passenger_pose_math.hpp"
#include "native_camera_kl.hpp"
#include "native_action_kl.hpp"
#include "native_visual_policy.hpp"
#include "rappel_look_math.hpp"
#include "hands_ipc.hpp"
#include "hands_math.hpp"
#include "native_hands_render_pair_kl.hpp"
#include "vr_bridge.hpp"
#include "profile.hpp"
#include "profile_timing.hpp"
#include "native_bind_pose_cache.hpp"
#include "native_skin_identity_reads.hpp"
#include <array>
#include <atomic>
#include <cstring>
#include <vector>

namespace NativeHandsKL {
namespace {
using namespace NativeCameraMath;
using Bone=NativeRigMath::Bone;
std::atomic<std::uint64_t> stableTorsoFrames{0},neutralStandingFrames{0},neutralStandingEyeFrames{0};
std::atomic<std::uint64_t> passengerHandsPinned[2]{},passengerNativeReachCorrected[2]{};
std::atomic<float> passengerNativeReachErrorMetres[2]{};
std::atomic<std::uint64_t> heldGrenadeArmFrames{0};
std::atomic<RenderContextProvider> renderContextProvider{nullptr};
std::atomic<std::uint64_t> renderPairAdmitted{0},renderPairReused{0},renderPairSecondEye{0},renderPairRejected{0},renderPairExpired{0};
thread_local NativeHandsRenderPairKL::State renderPair;
thread_local std::uint64_t renderReadinessEpoch=0;
thread_local bool renderReadinessFailed=false;
bool bonesLightCacheProof=false;
std::atomic<std::uint64_t> bonesLightCacheInvalidated{0};
std::atomic<std::uint64_t> passengerHeadsHidden{0};
std::atomic<PassengerMotionProvider> passengerMotionProvider{nullptr};
std::atomic<ProjectionBindObserver> projectionBindObserver{nullptr};
std::atomic<std::uint64_t> cargoPoseCandidates{0},cargoPoseApplied{0},cargoEyeApplied{0},cargoPoseRejected{0};
std::atomic<unsigned> cargoRejectStage{0},cargoPelvis{128},cargoHead{128},cargoLeftFoot{128},cargoRightFoot{128};
std::atomic<bool> shadowPassInstalled{false};
std::atomic<std::uint64_t> shadowPassCalls{0},shadowPassScopes{0},shadowHeadPalettes{0};
using NativeTechniqueDraw=void(__thiscall*)(void*,void*,void*,unsigned,void*);
NativeTechniqueDraw originalTechniqueDraw=nullptr;
thread_local NativeShadowPassKL::Context shadowPass;
thread_local unsigned techniqueDepth=0;
thread_local NativeSkinPaletteLease::Lease paletteLease;
std::atomic<std::uint64_t> paletteLeaseRestored{0},paletteLeaseReplaced{0},paletteLeaseRejected{0};
std::atomic<std::uint64_t> paletteLeaseResumed{0};
std::atomic<std::uint64_t> paletteLeaseFallback{0};
constexpr auto IdentityPalette() {
    // Native model resources/byte bone indices can address256 rows even
    // though this VR arm solver deliberately accepts only128 local bones.
    std::array<std::array<float,12>,256> result{};
    for(auto& matrix:result){matrix[0]=matrix[5]=matrix[10]=1.f;}return result;
}
const auto nativeIdentityPalette=IdentityPalette();
struct TechniqueObservation {std::uintptr_t package=0,technique=0;unsigned pass=0;std::uintptr_t state=0;std::uint64_t serial=0;};
thread_local TechniqueObservation techniqueObservation;
thread_local std::uint64_t diagnosticTechniqueSerial=0;
std::atomic<bool> drawContextDiagnostics{false};
thread_local NativeDrawContextKL::WorldBind diagnosticWorldBind,diagnosticNativeWorldBind;
thread_local std::uint32_t diagnosticWorldSerial=0;
struct DiagnosticTechniqueBind {NativeDrawContextKL::WorldBind world,nativeWorld;};
thread_local DiagnosticTechniqueBind diagnosticTechniqueBinds[8];
thread_local bool boundSupport=false,boundGestureHeld=false,boundCoupled=false,boundShadow=false;
thread_local float boundRawGrip=0;
thread_local std::uint64_t boundPair=0,boundGeneration=0;
struct BoneUpload {unsigned start=0,count=0;std::array<float,1024> rows{};bool valid=false;};
thread_local BoneUpload boneUpload;
struct ProbeBudget {DWORD tick=0;unsigned left=0;};
thread_local ProbeBudget skinProbeBudget[4];
struct SkinShaderIdentity {IUnknown* shader=nullptr;std::uint32_t hash=0;};
thread_local std::vector<SkinShaderIdentity> skinShaderCache;
thread_local IDirect3DDevice9* skinProbeDevice=nullptr;
std::atomic<std::uint64_t> skinDrawSamples{0},skinDrawRejected{0},skinConstantUploads{0},skinConstantMismatches{0};
SRWLOCK skinProbeLock=SRWLOCK_INIT;
SkinMaterialSample skinProbe[2][2];
static_assert(sizeof(void*)==4,"Native K&L bones require x86");
static_assert(sizeof(CameraPose)==0x30,"Native K&L GPU root stride");
// Pinned K&L executable. Independently established by static disassembly and
// live ZFFPlayer/model reads, not the addresses/layouts of other Glacier games.
constexpr std::uintptr_t kModelSpace=0x1BF220,kModelSpaceCaller=0x70A70;
constexpr std::uintptr_t kGameData=0x68760C,kGpuPoseIndex=0x7678A4;
constexpr std::uintptr_t kPlayerVtable=0x56582C,kBasePlayerVtable=0x552684;
constexpr std::uintptr_t kBindPalette=0x71F80,kBonesVtable=0x541034;
constexpr std::uintptr_t kSetWorld=0x71770;
constexpr std::uintptr_t kPaletteConsumer=0x90CF1,kPaletteConsumerStore=0x90D10,kLastUploadedPalette=0x7893B0;
constexpr std::uintptr_t kPaletteUploadScratch=0x7887B0;
using ModelSpace=void(__thiscall*)(void*);
using BindPalette=void(__thiscall*)(void*,void*,std::uintptr_t);
using SetWorld=void(__thiscall*)(void*,const CameraPose*);
ModelSpace original=nullptr;
BindPalette originalBind=nullptr;
std::uintptr_t image=0;
std::atomic<bool> installed{false};
std::atomic<std::uint64_t> calls{0},playerCalls{0},applied[2]{},clamped[2]{},rejected{0};
std::atomic<std::uint64_t> insideRenderCalls{0},outsideRenderCalls{0};
std::atomic<std::uint64_t> gpuPalettes{0},gpuRejected{0},headHidden{0};
std::atomic<std::uint64_t> gpuUploadInvalidated{0},gpuCacheRejected{0};
std::atomic<std::uint64_t> ownPaletteUploads{0},collapsedHeadUploads{0},shaderRowsInspected{0};
std::atomic<std::uint64_t> gpuBinderCalls{0},gpuPlayerBinds{0},gpuNoContext{0};
std::atomic<std::uint64_t> otherViewPreserved{0};
bool alternatePaletteProof=false;
std::atomic<std::uint64_t> alternatePaletteCandidates{0},alternatePaletteApplied{0},paletteSourceRejected{0};
std::atomic<std::uint64_t> helperApplied[2]{},lazyVisualPrepared{0},lazyVisualCacheHits{0},lazyVisualRejected{0};
std::atomic<std::uint64_t> rappelTorsoCandidates{0},rappelTorsoApplied{0},rappelTorsoRejected{0};
std::atomic<DWORD> frameThread{0},bindThread{0};
std::atomic<float> requestedDistance[2]{},armReach[2]{},targetError[2]{};
std::atomic<float> residualTwistDegrees[2]{};
std::atomic<float> playerHeadOffset[3]{},playerHeadDistance{0.f};
bool hideHead=true,supportGripEnabled=true;
std::atomic<std::uint64_t> supportCandidates{0},supportHeld{0},supportReachRejected{0},supportReachAdjusted{0};
std::atomic<std::uint64_t> supportAcquired{0},supportReleased{0},supportAimRecovered{0},supportAimRejected{0};
std::atomic<std::uint64_t> supportSuspended{0};
std::atomic<std::uint64_t> supportReleaseInput{0},supportReleaseContext{0},supportReleaseIdentity{0},supportReleaseInvalidInput{0},supportReleaseEpoch{0};
std::atomic<bool> supportGripSampleValid{false};
std::atomic<float> supportRawGrip{0},supportReleaseGrip{0};
std::atomic<std::uint32_t> supportGripSampleTick{0},supportReleaseSampleTick{0},supportReleaseLowMs{0},supportReleaseSamples{0};
std::atomic<std::uint32_t> supportEpoch{0};
std::atomic<std::uintptr_t> lastPlayer{0},lastModel{0};
std::atomic<std::uintptr_t> lastOwnedEntry{0},lastOwnedModel{0};
std::atomic<DWORD> lastOwnedEntryTick{0};
std::atomic<std::uint32_t> lastAge{0};
SRWLOCK visualLock=SRWLOCK_INIT;
VisualFrame visual;
std::uint64_t visualGeneration=0;
SRWLOCK diagnosticLock=SRWLOCK_INIT;
ArmDiagnostic lastDiagnostic[2];
struct CameraFrame {
    CameraPose centre{};
    float rawHeadOrientation[4]{},rawHeadPosition[3]{};
    float units=100.f;
    std::uint64_t pairId=0;
    std::uint64_t generation=0;
    std::uint32_t recenterAck=0;
    DWORD tick=0;
    bool valid=false;
    bool nativeAction=false;
    bool nativeBodyAction=false,firstPerson=false,actorHeadOnly=false;
    bool renderPairPose=false;
    bool neutralStanding=false;
    std::uintptr_t stanceMovement=0;
    std::uint32_t stanceMovementId=0;
    NativeActionKL::Movement stanceMovementKind=NativeActionKL::Movement::Unknown;
    DWORD renderAdmissionTick=0;
    HandsIPC::Frame hands{},input{};
    struct Rig {
        std::uintptr_t player=0,model=0,rest=0;
        std::uint32_t count=0;
        std::array<std::uint32_t,NativeRigMath::MaximumBones> parents{};
        std::array<std::uint8_t,128> lookup{};
        std::uint8_t head=255;
        DWORD tick=0;
        bool valid=false;
    } rig;
};
SRWLOCK frameLock=SRWLOCK_INIT;
CameraFrame publishedFrame;
SRWLOCK rawRigLock=SRWLOCK_INIT;
CameraFrame::Rig originalRig;
std::atomic<std::uint64_t> frameGeneration{0};
thread_local bool inHook=false;
thread_local HandsIPC::Consumer consumer;
thread_local std::array<std::array<float,12>,NativeRigMath::MaximumBones> gpuPalette,gpuShadowPalette;
thread_local unsigned uploadedHead=NativeRigMath::MaximumBones;
struct TwistHistory {
    std::uintptr_t player=0,model=0;
    std::uint32_t recenterAck=0;
    struct Side {bool valid=false;std::uint64_t pair=0;DWORD tick=0;float angle=0.f;} side[2];
};
thread_local TwistHistory twistHistory;
// CommitDraw publishes the current pair's residual for the NEXT pair.
// Re-reading it after every material/generation feeds an already rounded
// residual back into ref+remainder(principal-ref), changing FORETWIST skin
// rows even with identical native bones and controller input. Keep only the
// numerical reference immutable; every draw still proves its own source,
// camera generation, ownership and native palette lease as before.
struct TwistReferenceFrame {
    bool initialized=false;
    std::uintptr_t player=0,model=0;
    std::uint64_t pair=0;
    std::uint32_t recenterAck=0,supportEpoch=0,policy=0;
    float units=0;
    DWORD tick=0;
    TwistHistory history{};
};
thread_local TwistReferenceFrame twistReferenceFrame;
void CaptureTwistReference(const CameraFrame& camera,DWORD now){
    const unsigned policy=unsigned(camera.firstPerson)|(unsigned(camera.actorHeadOnly)<<1)|
        (unsigned(camera.nativeAction)<<2)|(unsigned(camera.nativeBodyAction)<<3);
    const auto epoch=supportEpoch.load(std::memory_order_relaxed);
    if(twistReferenceFrame.initialized&&twistReferenceFrame.player==camera.rig.player&&
       twistReferenceFrame.model==camera.rig.model&&twistReferenceFrame.pair==camera.pairId&&
       twistReferenceFrame.recenterAck==camera.recenterAck&&twistReferenceFrame.supportEpoch==epoch&&
       twistReferenceFrame.policy==policy&&twistReferenceFrame.units==camera.units)return;
    twistReferenceFrame={true,camera.rig.player,camera.rig.model,camera.pairId,camera.recenterAck,epoch,policy,
        camera.units,now,twistHistory};
}
thread_local NativeTwoHandMath::State supportState;
thread_local std::uint32_t supportSeenEpoch=0;
thread_local std::uintptr_t nativeActionPlayer=0;
struct PreparedDraw {
    std::array<std::array<float,12>,NativeRigMath::MaximumBones> palette{};
    std::array<std::array<float,12>,NativeRigMath::MaximumBones> headVisiblePalette{};
    CameraPose rawRoot{},renderRoot{};
    VisualFrame visual{};
    std::array<NativeRigMath::Result,2> armResult{};
    std::array<NativeRigMath::ArmHelpers,2> helpers{};
    ArmDiagnostic diagnostic[2]{};
    float requested[2]{},reach[2]{},error[2]{};
    std::uintptr_t entry=0,cache=0;
    std::uintptr_t paletteSource=0;
    std::uint64_t generation=0;
    unsigned count=0,head=NativeRigMath::MaximumBones;
    bool modified=false,hidden=false,rootModified=false;
};
SRWLOCK preparedLock=SRWLOCK_INIT;
PreparedDraw preparedCache;
bool preparedValid=false;
thread_local NativeBindPoseCache::Cache bindPoseCache;
std::atomic<std::uint64_t> bindPoseCacheHits{0},bindPoseCacheMisses{0};
bool ResolveBindPose(const NativeSkinMath::BindLocal* local,const std::uint32_t* parents,unsigned count,CameraPose* output){
    const auto before=bindPoseCache.hits;const bool ok=bindPoseCache.Resolve(local,parents,count,output);
    if(bindPoseCache.hits!=before)++bindPoseCacheHits;else ++bindPoseCacheMisses;return ok;
}

template<class T> bool Read(std::uintptr_t address,T& value) {
    SIZE_T bytes=0;return address&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),
        &value,sizeof(value),&bytes)&&bytes==sizeof(value);
}
bool ReadBytes(std::uintptr_t address,void* output,std::size_t count) {
    SIZE_T bytes=0;return address&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),
        output,count,&bytes)&&bytes==count;
}
bool Writable(std::uintptr_t address,std::size_t bytes) {
    MEMORY_BASIC_INFORMATION page{};
    if(!address||address+bytes<address||!VirtualQuery(reinterpret_cast<void*>(address),&page,sizeof(page))||
       page.State!=MEM_COMMIT||(page.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
    const auto protection=page.Protect&0xff;
    return (protection==PAGE_READWRITE||protection==PAGE_WRITECOPY||protection==PAGE_EXECUTE_READWRITE||protection==PAGE_EXECUTE_WRITECOPY)&&
        address+bytes<=reinterpret_cast<std::uintptr_t>(page.BaseAddress)+page.RegionSize;
}
void RetirePaletteLease() {
    if(!paletteLease.active)return;
    ProfileTiming::Scope lifetimeCost(ProfileTiming::HandsBind);
    boneUpload={};boundPair=boundGeneration=0;
    const auto staging=paletteLease.staging;
    const auto write=[](std::uintptr_t at,const void* data,std::size_t bytes) {
        if(!Writable(at,bytes))return false;std::memcpy(reinterpret_cast<void*>(at),data,bytes);return true;
    };
    const auto result=NativeSkinPaletteLease::Retire(paletteLease,ReadBytes,write,reinterpret_cast<std::uintptr_t>(nativeIdentityPalette.data()));
    if(result==NativeSkinPaletteLease::Result::Restored||result==NativeSkinPaletteLease::Result::Fallback) {
        if(result==NativeSkinPaletteLease::Result::Restored)++paletteLeaseRestored;
        else{++paletteLeaseFallback;++paletteLeaseRejected;}
        // Native pointer-only cache must not reuse constants from the retired
        // VR draw on a later material/instance with the same TLS address.
        auto* cached=reinterpret_cast<std::uintptr_t*>(image+kLastUploadedPalette);
        if(*cached==staging&&Writable(reinterpret_cast<std::uintptr_t>(cached),sizeof(*cached))){*cached=0;++gpuUploadInvalidated;}
    } else if(result==NativeSkinPaletteLease::Result::Replaced)++paletteLeaseReplaced;
    else if(result==NativeSkinPaletteLease::Result::Rejected)++paletteLeaseRejected;
}
bool Player(std::uintptr_t model,std::uintptr_t& player) {
    std::uintptr_t data=0;NativeSkinIdentityReads::Player binding;
    return Read(image+kGameData,data)&&Read(data+0xA2C,player)&&player&&NativeSkinIdentityReads::ReadPlayer(player,ReadBytes,binding)&&
        (binding.vtable==image+kPlayerVtable||binding.vtable==image+kBasePlayerVtable)&&binding.model==model;
}
void Rotation(const float q[4],float rows[9]) {
    const Quat quat{q[0],q[1],q[2],q[3]};
    const Vec3 axes[3]={Rotate(quat,{1,0,0}),Rotate(quat,{0,1,0}),Rotate(quat,{0,0,1})};
    for(unsigned i=0;i<3;++i){rows[i*3]=axes[i].x;rows[i*3+1]=axes[i].y;rows[i*3+2]=axes[i].z;}
}
bool FrameSnapshot(CameraFrame& output) {
    AcquireSRWLockShared(&frameLock);output=publishedFrame;ReleaseSRWLockShared(&frameLock);
    return NativeSkinMath::DrawContextCurrent(output.valid,output.generation,frameGeneration.load(),VRBridge::Ready());
}
bool GpuRoot(std::uintptr_t entry,CameraPose& root) {
    std::uint32_t poseIndex=0;std::uintptr_t instances=0;
    return Read(image+kGpuPoseIndex,poseIndex)&&poseIndex<=1&&
        Read(entry+0x90+poseIndex*sizeof(std::uintptr_t),instances)&&!instances&&
        Read(entry+0x10+poseIndex*sizeof(CameraPose),root)&&ValidatePose(root,.05f);
}
bool CaptureRigMetadata(std::uintptr_t model,std::uintptr_t player,CameraFrame::Rig& captured) {
    std::uint32_t count=0;std::uintptr_t parentsAt=0,lookupAt=0,rest=0;
    if(!Read(model+0x4A0,count)||count<2||count>NativeRigMath::MaximumBones||
       !Read(model+0x10,parentsAt)||!Read(model+0xC,lookupAt)||
       !Read(model+4,rest)||!rest)return false;
    std::array<std::uint8_t,NativeRigMath::MaximumBones> parentBytes{};
    if(!ReadBytes(parentsAt,parentBytes.data(),count)||
       !ReadBytes(lookupAt,captured.lookup.data(),captured.lookup.size()))return false;
    for(unsigned i=1;i<count;++i){captured.parents[i]=parentBytes[i];if(captured.parents[i]>=i)return false;}
    captured.player=player;captured.model=model;captured.rest=rest;captured.count=count;
    captured.head=captured.lookup[6];captured.tick=GetTickCount();captured.valid=true;return true;
}
NativeRigMath::ArmHelpers Helpers(const CameraFrame& camera,unsigned side,unsigned upper,unsigned lower,unsigned count) {
    NativeRigMath::ArmHelpers result;
    const auto& lookup=camera.rig.lookup;const auto& parents=camera.rig.parents;
    const unsigned twist0=lookup[side?25:19],twist1=lookup[side?26:20],muscle=lookup[side?66:65];
    if(twist0<count&&twist0!=lower&&parents[twist0]==upper) {
        result.foreTwist=twist0;
        if(twist1<count&&parents[twist1]==twist0)result.foreTwist1=twist1;
    }
    if(muscle>0&&muscle<count&&muscle!=upper&&parents[muscle]==parents[upper])result.upperMuscle=muscle;
    return result;
}
bool SolveGpuRig(const CameraFrame& camera,const std::array<Bone,NativeRigMath::MaximumBones>& originalPose,
                 Bone* pose,const std::array<std::uint8_t,NativeRigMath::MaximumBones>& recovered,
                 std::array<std::uint8_t,NativeRigMath::MaximumBones>& changed,PreparedDraw& draw) {
    const auto& pairedHands=camera.hands;
    const auto& parents=camera.rig.parents;const auto& lookup=camera.rig.lookup;
    auto& output=draw.visual;
    output.player=camera.rig.player;output.tick=GetTickCount();output.unitsPerMetre=camera.units;output.pairId=camera.pairId;
    output.sourcePoseTick=pairedHands.tick;output.renderPairPose=camera.renderPairPose;
    output.centre=camera.centre;
    const DWORD renderTick=camera.renderPairPose?camera.renderAdmissionTick:output.tick;
    CaptureTwistReference(camera,output.tick);
    output.recenterAck=camera.recenterAck;
    output.rawRoot=draw.rawRoot;output.renderRoot=draw.renderRoot;
    output.bodyAvailableForMesh=ValidatePose(draw.rawRoot,.05f)&&ValidatePose(draw.renderRoot,.05f);
    const auto policy=NativeSkinMath::Policy(camera.firstPerson,camera.actorHeadOnly,camera.nativeBodyAction);
    const auto liveAction=NativeActionKL::Current();
    const auto livePolicy=NativeVisualPolicy::ForAction(liveAction,camera.rig.player);
    NativeHeldGrenadeVisualKL::Owner grenade;
    const bool heldGrenade=camera.firstPerson&&!camera.actorHeadOnly&&liveAction.action&&
        liveAction.actionKind==NativeActionKL::Action::Throw&&
        NativeHeldGrenadeVisualKL::Capture(image,liveAction,camera.rig.player,ReadBytes,grenade);
    const auto afterGrenade=heldGrenade?NativeActionKL::Current():liveAction;
    const bool grenadeTracking=heldGrenade&&afterGrenade.valid&&afterGrenade.player==liveAction.player&&
        afterGrenade.action==liveAction.action&&afterGrenade.actionVtable==liveAction.actionVtable&&
        afterGrenade.movement==liveAction.movement&&afterGrenade.movementKind==liveAction.movementKind;
    const bool tracking=((policy.armTracking&&!camera.nativeAction&&!livePolicy.nativeArms)||grenadeTracking)&&
        HandsMath::ValidFrame(pairedHands,renderTick,camera.recenterAck);
    if(tracking&&grenadeTracking)++heldGrenadeArmFrames;
    const bool passenger=tracking&&camera.firstPerson&&!camera.actorHeadOnly&&liveAction.valid&&
        liveAction.player==camera.rig.player&&liveAction.PassengerWeapon();
    Vec3 elbowPole[2]={{1.f,-.5f,0},{-1.f,-.5f,0}};
    const unsigned leftShoulder=lookup[16],rightShoulder=lookup[22];
    if(leftShoulder<draw.count&&rightShoulder<draw.count&&recovered[leftShoulder]&&recovered[rightShoulder]) {
        const Vec3 down=NativeRigMath::Inverse(draw.renderRoot.basis,{0,-1,0});
        for(unsigned side=0;side<2;++side) {
            Vec3 anatomical;
            if(NativeArmMath::ElbowPole(originalPose[leftShoulder].position,originalPose[rightShoulder].position,down,side,anatomical))
                elbowPole[side]=anatomical;
        }
    }
    std::array<Bone,NativeRigMath::MaximumBones> next;
    CameraPose physical[2]{},targets[2]{};bool targetValid[2]{};
    for(unsigned side=0;side<2;++side) {
        const auto& hand=pairedHands.hands[side];
        if(!tracking||!HandsMath::ValidPose(hand))continue;
        float rotation[9];Rotation(hand.orientation,rotation);
        targetValid[side]=NativeHandsMath::ControllerTarget(camera.centre,camera.centre.position,
            {camera.rawHeadOrientation[0],camera.rawHeadOrientation[1],camera.rawHeadOrientation[2],camera.rawHeadOrientation[3]},rotation,
            {hand.position[0],hand.position[1],hand.position[2]},
            {pairedHands.head.position[0],pairedHands.head.position[1],pairedHands.head.position[2]},side,camera.units,1.5f,physical[side]);
        targets[side]=physical[side];
        output.controllerWrist[side]=physical[side];
        output.palmValid[side]=ReloadPalmKL::Capture(originalPose.data(),parents.data(),draw.count,lookup[side?24:18],camera.units,side,output.palmLocal[side]);
    }
    NativeWeaponKL::HeldSupport held;
    const auto epoch=supportEpoch.load();
    if(epoch!=supportSeenEpoch){if(supportState.held){++supportReleased;++supportReleaseEpoch;}supportState={};supportSeenEpoch=epoch;}
    const bool supportInput=HandsMath::ValidFrame(camera.input,renderTick,camera.recenterAck)&&
        (camera.input.inputValid[0]&HandsIPC::kGripInput)!=0&&
        std::isfinite(camera.input.grip[0])&&camera.input.grip[0]>=0&&camera.input.grip[0]<=1;
    supportGripSampleValid=supportInput;supportRawGrip=std::isfinite(camera.input.grip[0])?camera.input.grip[0]:-1.f;
    supportGripSampleTick=camera.input.tick;
    const bool supportContext=supportGripEnabled&&camera.firstPerson&&!camera.actorHeadOnly&&!NativeWeaponKL::ReloadHandBusy(camera.rig.player)&&
        !VRBridge::TeamHudVisible()&&!VRBridge::WeaponMenuVisible()&&
        NativeWeaponKL::CaptureHeldSupport(camera.rig.player,camera.units,held,true);
    const auto& supportAction=liveAction;
    const bool reloadHand=supportAction.player==camera.rig.player&&supportAction.TrackedReload();
    bool canSupport=supportContext&&supportInput&&tracking&&(held.active||reloadHand)&&targetValid[0]&&targetValid[1];
    const float physicalGrip=supportInput?camera.input.grip[0]:(supportState.held?1.f:0.f);
    CameraPose contactWrist[2]={physical[0],physical[1]};
    for(unsigned side=0;side<2&&canSupport&&!passenger;++side) {
        const unsigned upper=lookup[side?22:16],lower=lookup[side?23:17],wrist=lookup[side?24:18];
        canSupport=upper<draw.count&&lower<draw.count&&wrist<draw.count&&
            recovered[upper]&&recovered[lower]&&recovered[wrist]&&parents[lower]==upper&&parents[wrist]==lower;
        if(canSupport)canSupport=NativeTwoHandMath::RenderedWrist(draw.renderRoot,
            {pose[upper].position,pose[lower].position,pose[wrist].position},physical[side],elbowPole[side],contactWrist[side]);
    }
    NativeTwoHandMath::Key key{camera.rig.player,camera.rig.model,held.weapon,held.root,held.model,held.marker,held.markerGeometry,
        held.reference,camera.recenterAck};
    if(held.candidate){
        output.foregripItem=held.weapon;output.foregripModel=held.model;
        output.foregripReference=held.reference;output.foregripSocket=held.socket.position;
        if(canSupport)++supportCandidates;
    }
    const bool preferSlide=canSupport&&held.candidate&&!supportState.held&&physicalGrip>.55f&&
        NativeWeaponKL::PreferReloadSlide(held,contactWrist[1],physical[0],output.palmValid[0]?&output.palmLocal[0]:nullptr,output.recenterAck);
    CameraPose steering;
    if(canSupport)canSupport=NativeTwoHandMath::SteeringController(contactWrist[1],physical[1],physical[0],steering);
    const bool coupled=NativeTwoHandMath::Targets(supportState,key,held.socket,held.candidate&&!preferSlide,contactWrist[1],contactWrist[0],
        camera.units,output.tick,camera.pairId,physicalGrip,supportContext,targets[1],targets[0],canSupport?&steering:nullptr,canSupport,
        camera.input.tick,supportInput,tracking&&targetValid[1]&&held.active,&camera.centre);
    output.supportLatched=supportState.held;
    if(supportState.acquired)++supportAcquired;
    if(supportState.released){++supportReleased;
        switch(supportState.releaseReason){
            case NativeTwoHandMath::ReleaseReason::Input:++supportReleaseInput;supportReleaseGrip=physicalGrip;
                supportReleaseSampleTick=camera.input.tick;supportReleaseSamples=supportState.lowGripSamples;
                supportReleaseLowMs=supportState.lowGripTick-supportState.lowGripStart;break;
            case NativeTwoHandMath::ReleaseReason::Context:++supportReleaseContext;break;
            case NativeTwoHandMath::ReleaseReason::Identity:++supportReleaseIdentity;break;
            case NativeTwoHandMath::ReleaseReason::InvalidInput:++supportReleaseInvalidInput;break;
            default:break;
        }}
    if(supportState.aimRecovered)++supportAimRecovered;
    if(supportState.aimRejected)++supportAimRejected;
    if(supportState.suspended)++supportSuspended;
    if(!coupled){targets[0]=physical[0];targets[1]=physical[1];}
    if(coupled&&passenger) {
        // Keep one rigid physical gun/foregrip frame. A native seated fire
        // pose must not push either controller target through reach fitting.
        supportState.pairRight=targets[1];supportState.pairLeft=targets[0];
        targetValid[0]=targetValid[1]=true;output.supportHeld=true;++supportHeld;
    } else if(coupled) {
        // Contact is an anatomical constraint, never a reason to lengthen a
        // limb. Reject both coupled targets before editing either subtree.
        bool reachable=true;NativeTwoHandMath::Reach armReach[2];
        for(unsigned side=0;side<2&&reachable;++side) {
            const unsigned upper=lookup[side?22:16],lower=lookup[side?23:17],wrist=lookup[side?24:18];
            reachable=upper<draw.count&&lower<draw.count&&wrist<draw.count&&recovered[upper]&&recovered[lower]&&recovered[wrist]&&
                parents[lower]==upper&&parents[wrist]==lower;
            if(!reachable)break;
            const auto helpers=Helpers(camera,side,upper,lower,draw.count);
            for(unsigned i=1;i<draw.count;++i)
                if(NativeRigMath::AffectedByArm(i,upper,parents.data(),helpers)&&!recovered[i]){reachable=false;break;}
            if(reachable)reachable=NativeTwoHandMath::ArmReach({
                NativeRigMath::ToWorld(draw.renderRoot,pose[upper]).position,
                NativeRigMath::ToWorld(draw.renderRoot,pose[lower]).position,
                NativeRigMath::ToWorld(draw.renderRoot,pose[wrist]).position},armReach[side]);
        }
        const auto primaryBefore=targets[1].position;
        if(reachable)reachable=NativeTwoHandMath::FitReach(targets[1],targets[0],armReach[1],armReach[0],camera.units);
        for(unsigned side=0;side<2&&reachable;++side) {
            const unsigned upper=lookup[side?22:16],lower=lookup[side?23:17],wrist=lookup[side?24:18];
            NativeRigMath::Result check;NativeRigMath::ArmOptions options;options.helpers=Helpers(camera,side,upper,lower,draw.count);
            reachable=NativeRigMath::SolveArm(pose,parents.data(),draw.count,upper,lower,wrist,
                NativeRigMath::ToModel(draw.renderRoot,targets[side]),elbowPole[side],next.data(),check,options)&&!check.clamped;
        }
        if(!reachable) {
            targets[0]=physical[0];targets[1]=physical[1];supportState.pairValid=false;++supportReachRejected;
        } else {
            if(NativeArmMath::Length(Subtract(primaryBefore,targets[1].position))>.0001f*camera.units)++supportReachAdjusted;
            supportState.pairRight=targets[1];supportState.pairLeft=targets[0];
            targetValid[0]=targetValid[1]=true;output.supportHeld=true;++supportHeld;
        }
    }
    bool any=false;
    for(unsigned side=0;side<2;++side) {
        const auto& hand=pairedHands.hands[side];
        const unsigned upper=lookup[side?22:16],lower=lookup[side?23:17],wrist=lookup[side?24:18];
        if(wrist<draw.count&&recovered[wrist]) {
            output.rawWrist[side]=NativeRigMath::ToWorld(draw.rawRoot,originalPose[wrist]);
            output.finalWrist[side]=NativeRigMath::ToWorld(draw.renderRoot,pose[wrist]);
            output.wrist[side]=output.finalWrist[side];
            output.availableForMesh[side]=ValidatePose(output.rawWrist[side],.05f)&&ValidatePose(output.finalWrist[side],.05f);
        }
        if(!targetValid[side])continue;
        if(upper>=draw.count||lower>=draw.count||wrist>=draw.count||parents[lower]!=upper||parents[wrist]!=lower)continue;
        draw.helpers[side]=Helpers(camera,side,upper,lower,draw.count);
        bool complete=true;
        for(unsigned i=1;i<draw.count;++i)
            if(NativeRigMath::AffectedByArm(i,upper,parents.data(),draw.helpers[side])&&!recovered[i]){complete=false;break;}
        if(!complete)continue;
        NativeWeaponKL::ReloadHandTarget(camera.rig.player,side,targets[1],targets[side]);
        const CameraPose target=targets[side];
        const CameraPose modelTarget=NativeRigMath::ToModel(draw.renderRoot,target);
        draw.requested[side]=NativeArmMath::Length(Subtract(modelTarget.position,pose[upper].position))/camera.units;
        draw.reach[side]=(NativeArmMath::Length(Subtract(pose[lower].position,pose[upper].position))+
                          NativeArmMath::Length(Subtract(pose[wrist].position,pose[lower].position)))/camera.units;
        NativeRigMath::ArmOptions options;options.helpers=draw.helpers[side];
        const auto& reference=twistReferenceFrame.history;
        const auto& history=reference.side[side];
        options.twistReferenceValid=reference.player==camera.rig.player&&reference.model==camera.rig.model&&
            reference.recenterAck==pairedHands.recenterAck&&history.valid&&
            twistReferenceFrame.tick-history.tick<=300&&camera.pairId>=history.pair;
        options.twistReferenceRadians=options.twistReferenceValid?history.angle:0.f;
        auto& result=draw.armResult[side];
        if(!NativeRigMath::SolveArm(pose,parents.data(),draw.count,upper,lower,wrist,modelTarget,
                                   elbowPole[side],next.data(),result,options))continue;
        const bool freeStance=liveAction.valid&&liveAction.player==camera.rig.player&&!liveAction.action&&
            (liveAction.movementKind==NativeActionKL::Movement::Stand||
             liveAction.movementKind==NativeActionKL::Movement::BaseMove);
        if(passenger) {
            const float residual=NativeArmMath::Length(Subtract(next[wrist].position,modelTarget.position))/camera.units;
            if(!NativeSkinMath::PinTrackedWrist(next.data(),parents.data(),draw.count,wrist,modelTarget))continue;
            passengerNativeReachErrorMetres[side]=residual;++passengerHandsPinned[side];
            if(residual>.00001f)++passengerNativeReachCorrected[side];
            result.joints.wrist=modelTarget.position;
        } else if(side==1&&result.clamped&&freeStance&&!coupled&&camera.firstPerson) {
            // A crouched animation lowers the native shoulder. Keep a raised
            // trigger hand from slipping behind the obstacle as the IK hits
            // its reach limit. Use the same bounded correction while standing.
            const auto shoulder=NativeRigMath::ToWorld(draw.renderRoot,pose[upper]).position;
            const auto solved=NativeRigMath::ToWorld(draw.renderRoot,next[wrist]).position;
            const auto delta=Subtract(target.position,solved);
            const auto distance=NativeArmMath::Length(delta);
            if(target.position.y>shoulder.y&&std::isfinite(distance)&&distance>0.001f) {
                const auto position=Add(solved,Scale(delta,std::min(1.f,.10f*camera.units/distance)));
                auto adjusted=target;
                adjusted.position=position;
                const auto pin=NativeRigMath::ToModel(draw.renderRoot,adjusted);
                if(NativeSkinMath::PinTrackedWrist(next.data(),parents.data(),draw.count,wrist,pin))
                    result.joints.wrist=pin.position;
            }
        }
        std::memcpy(pose,next.data(),draw.count*sizeof(Bone));
        for(unsigned i=1;i<draw.count;++i)
            if(NativeRigMath::AffectedByArm(i,upper,parents.data(),draw.helpers[side]))changed[i]=1;
        draw.error[side]=NativeArmMath::Length(Subtract(pose[wrist].position,modelTarget.position))/camera.units;
        output.finalWrist[side]=NativeRigMath::ToWorld(draw.renderRoot,pose[wrist]);output.wrist[side]=output.finalWrist[side];
        output.availableForMesh[side]=ValidatePose(output.rawWrist[side],.05f)&&ValidatePose(output.finalWrist[side],.05f);
        output.tracked[side]=true;output.active[side]=true;
        output.trigger[side]=pairedHands.trigger[side];output.grip[side]=pairedHands.grip[side];
        output.inputValid[side]=pairedHands.inputValid[side];
        auto& diagnostic=draw.diagnostic[side];diagnostic.valid=true;
        diagnostic.player=camera.rig.player;diagnostic.model=camera.rig.model;
        diagnostic.pairId=camera.pairId;diagnostic.generation=camera.generation;
        diagnostic.tick=output.tick;diagnostic.trackingAgeMs=output.tick-pairedHands.tick;
        const unsigned roles[6]={upper,lower,wrist,options.helpers.foreTwist,options.helpers.foreTwist1,options.helpers.upperMuscle};
        for(unsigned role=0;role<6;++role) {
            diagnostic.bone[role]=roles[role];
            if(roles[role]<draw.count) {
                const auto& raw=originalPose[roles[role]];const auto& final=pose[roles[role]];
                diagnostic.original[role]={raw.basis,raw.position};diagnostic.solved[role]={final.basis,final.position};
            }
        }
        diagnostic.rawRoot=draw.rawRoot;diagnostic.renderRoot=draw.renderRoot;
        diagnostic.target=target;diagnostic.centre=camera.centre;
        std::memcpy(diagnostic.controllerPosition,hand.position,sizeof(diagnostic.controllerPosition));
        std::memcpy(diagnostic.controllerOrientation,hand.orientation,sizeof(diagnostic.controllerOrientation));
        std::memcpy(diagnostic.headPosition,pairedHands.head.position,sizeof(diagnostic.headPosition));
        std::memcpy(diagnostic.cameraRawHeadOrientation,camera.rawHeadOrientation,sizeof(diagnostic.cameraRawHeadOrientation));
        diagnostic.residualTwistRadians=result.residualTwistRadians;diagnostic.unitsPerMetre=camera.units;
        any=true;
    }
    return any;
}
bool OwnedEntry(const CameraFrame& camera,std::uintptr_t entry,std::uintptr_t& cache,unsigned& count) {
    std::uintptr_t vtable=0,geometry=0,owner=0,model=0,player=0,rest=0;
    std::uint32_t modelCount=0,paletteCount=0;
    NativeSkinIdentityReads::Bones binding,after;
    if(!entry||!Read(entry,vtable)||vtable!=image+kBonesVtable||
       !NativeSkinIdentityReads::ReadBones(entry,ReadBytes,binding)||!(geometry=binding.geometry)||!Read(geometry+0x60,owner)||owner!=camera.rig.player||
       !Read(owner+0x70,model)||model!=camera.rig.model||!Player(model,player)||player!=owner||
       !(cache=binding.cache)||!(paletteCount=binding.count)||
       !Read(model+0x4A0,modelCount)||modelCount!=camera.rig.count||
       paletteCount<2||paletteCount>modelCount||modelCount>NativeRigMath::MaximumBones||
       !Read(model+4,rest)||rest!=camera.rig.rest||!NativeSkinIdentityReads::ReadBones(entry,ReadBytes,after)||
       !NativeSkinIdentityReads::Same(binding,after))return false;
    count=paletteCount;return true;
}
bool CurrentGeneration(const CameraFrame& camera) {
    AcquireSRWLockShared(&frameLock);
    const bool valid=publishedFrame.valid&&publishedFrame.generation==camera.generation;
    ReleaseSRWLockShared(&frameLock);return valid;
}
bool StationaryStandingInput(std::uintptr_t player) {
    // Existing pinned native cover/movement observations: +400 is the
    // player input bitfield (crouch bit 10), +96C/+970 are movement axes.
    // No native actor flag or animation controller is written here.
    std::uint32_t input=0,after=0,controller=0;std::uint8_t blocked=1;float forward=0,right=0;
    return Read(player+0x400,input)&&!(input&0x400)&&Read(player+0x96C,forward)&&Read(player+0x970,right)&&
        std::isfinite(forward)&&std::isfinite(right)&&std::fabs(forward)<.001f&&std::fabs(right)<.001f&&
        Read(player+0x9B8,controller)&&NativeActionKL::Address(controller,0x19)&&
        Read(controller+0x18,blocked)&&blocked==0&&Read(player+0x400,after)&&after==input;
}
bool StationaryOrdinaryPlayer(std::uintptr_t player,const NativeActionKL::State& action) {
    if(!player||!action.valid||action.player!=player||action.action||
       (action.movementKind!=NativeActionKL::Movement::Stand&&action.movementKind!=NativeActionKL::Movement::BaseMove)||
       !StationaryStandingInput(player))return false;
    // Both the GPU body and eye-height consumers use this same policy. A
    // movement/action replacement during native input reads retires it.
    const auto after=NativeActionKL::Current();
    return after.valid&&after.player==player&&!after.action&&after.movement==action.movement&&
        after.movementKind==action.movementKind&&after.movementId==action.movementId;
}
bool PassengerHeadMaskContext(const CameraFrame& camera,const NativeActionKL::State& action) {
    // Authorization is retained for the complete published draw, including a
    // slow second eye. It is not extended to another actor or authored scene.
    // Manual reload keeps its original arms while this same seated VR head
    // remains hidden after a bounded roomscale/virtual crouch displacement.
    Quat mapped;
    return camera.valid&&camera.firstPerson&&!camera.actorHeadOnly&&camera.rig.valid&&camera.rig.player&&
        action.valid&&action.player==camera.rig.player&&action.movementKind==NativeActionKL::Movement::Vehicle&&
        (!action.action||action.TrackedReload())&&camera.tick&&ValidatePose(camera.centre,.05f)&&
        NormalizeQuaternion({camera.rawHeadOrientation[0],camera.rawHeadOrientation[1],camera.rawHeadOrientation[2],camera.rawHeadOrientation[3]},mapped);
}
bool StabilizeGameplayPose(const CameraFrame& camera,const NativeActionKL::State& action,
                          const CameraPose* bind,const Bone* original,Bone* pose,unsigned count,
                          std::uint8_t* changed,const std::uint8_t* recovered) {
    if(!camera.firstPerson||camera.actorHeadOnly||camera.nativeAction||
       !HandsMath::ValidFrame(camera.hands,camera.renderPairPose?camera.renderAdmissionTick:GetTickCount(),camera.recenterAck)||!action.valid||
       action.player!=camera.rig.player||action.action)return false;
    const bool ordinary=action.movementKind==NativeActionKL::Movement::Stand||action.movementKind==NativeActionKL::Movement::BaseMove;
    if(!ordinary&&action.movementKind!=NativeActionKL::Movement::Cover&&action.movementKind!=NativeActionKL::Movement::Vehicle)return false;
    const unsigned pelvis=camera.rig.lookup[0],head=camera.rig.head;
    // Simulation may update native axes while colour and mask still visit
    // this same rendered image. Keep this numerical stance policy with its
    // admitted input, while revalidating the current action/movement below.
    const bool neutral=camera.renderPairPose?
        ordinary&&camera.neutralStanding&&action.movement==camera.stanceMovement&&action.movementId==camera.stanceMovementId&&
        action.movementKind==camera.stanceMovementKind:
        StationaryOrdinaryPlayer(camera.rig.player,action);
    if(neutral&&
       NativeSkinMath::NeutralStandingLegs(bind,pose,camera.rig.parents.data(),count,pelvis,head,changed,recovered))++neutralStandingFrames;
    if(!NativeSkinMath::StableUpperBody(bind,original,pose,camera.rig.parents.data(),count,pelvis,head,changed,recovered))return false;
    ++stableTorsoFrames;return true;
}
struct CargoPose {NativePassengerPoseMath::Rig rig;NativePassengerPoseMath::Sample sample;};
bool CargoSource(std::uintptr_t player,std::uintptr_t model,unsigned modelCount,unsigned paletteCount,unsigned head,
                 const std::uint32_t* parents,std::uint32_t recenter,CargoPose& output){
    const auto provider=passengerMotionProvider.load(std::memory_order_acquire);NativePassengerActorKL::Motion motion;
    if(!provider||!provider(player,recenter,motion))return false;++cargoPoseCandidates;
    const auto reject=[](unsigned stage){cargoRejectStage=stage;++cargoPoseRejected;return false;};
    const auto now=GetTickCount();const auto action=NativeActionKL::Current();
    if(!motion.active||motion.player!=player||!motion.epoch||!motion.tick||std::int32_t(now-motion.tick)<0||now-motion.tick>250||
       (recenter!=0xFFFFFFFFu&&motion.recenter!=recenter)||!action.valid||action.player!=player||action.movement!=motion.movement||
       action.movementKind!=NativeActionKL::Movement::Vehicle||(action.action&&!action.TrackedReload()&&!action.TrackedWeaponGesture()))return reject(7);
    CargoPose next;
    if(!NativePassengerPoseMath::CaptureRig(image,player,model,modelCount,ReadBytes,next.rig))return reject(1);
    cargoPelvis=next.rig.pelvis;cargoHead=next.rig.head;cargoLeftFoot=next.rig.legs[0].foot;cargoRightFoot=next.rig.legs[1].foot;
    if(next.rig.head!=head)return reject(2);
    if(!NativePassengerPoseMath::Topology(next.rig,parents,paletteCount))return reject(3);
    next.sample={motion.phase,motion.speed,motion.crouchMetres,motion.velocityMetres};
    if(!NativePassengerPoseMath::Valid(next.sample))return reject(4);output=next;return true;
}
bool PrepareDraw(const CameraFrame& camera,std::uintptr_t entry,PreparedDraw& draw,const NativeSkinMath::PaletteSource* source=nullptr) {
    ProfileTiming::Scope profile(ProfileTiming::RigPrepare);
    std::uintptr_t cache=0;unsigned count=0;
    if(!camera.rig.valid||!OwnedEntry(camera,entry,cache,count))return false;
    const auto paletteSource=source?source->palette:cache+0x90;
    CameraPose rawRoot;
    if(source)rawRoot=source->root;else if(!GpuRoot(entry,rawRoot))return false;
    AcquireSRWLockShared(&preparedLock);
    const bool cached=preparedValid&&preparedCache.generation==camera.generation&&preparedCache.entry==entry&&
        preparedCache.cache==cache&&preparedCache.count==count&&preparedCache.paletteSource==paletteSource&&
        !std::memcmp(&preparedCache.rawRoot,&rawRoot,sizeof(rawRoot));
    if(cached)draw=preparedCache;
    ReleaseSRWLockShared(&preparedLock);
    if(cached)return CurrentGeneration(camera);
    PreparedDraw next;next.entry=entry;next.cache=cache;next.count=count;next.generation=camera.generation;
    next.paletteSource=paletteSource;next.rawRoot=rawRoot;
    std::array<NativeSkinMath::BindLocal,NativeRigMath::MaximumBones> local;
    std::array<CameraPose,NativeRigMath::MaximumBones> bind;
    if(!ReadBytes(camera.rig.rest,local.data(),count*sizeof(local[0]))||
       !ReadBytes(paletteSource,next.palette.data(),count*sizeof(next.palette[0]))||
       !ResolveBindPose(local.data(),camera.rig.parents.data(),count,bind.data()))return false;
    std::array<Bone,NativeRigMath::MaximumBones> pose{};
    std::array<std::uint8_t,NativeRigMath::MaximumBones> recovered{},changed{};
    for(unsigned i=0;i<count;++i)recovered[i]=NativeSkinMath::PoseFromPalette(bind[i],next.palette[i].data(),pose[i]);
    const auto originalPose=pose;next.renderRoot=next.rawRoot;
    const unsigned attacher=camera.rig.lookup[64],head=camera.rig.head;
    const auto currentAction=NativeActionKL::Current();
    CargoPose cargo;const bool cargoSource=camera.firstPerson&&!camera.actorHeadOnly&&
        CargoSource(camera.rig.player,camera.rig.model,camera.rig.count,count,head,camera.rig.parents.data(),camera.recenterAck,cargo);
    const bool cargoApplied=cargoSource&&NativePassengerPoseMath::Apply(bind.data(),originalPose.data(),pose.data(),camera.rig.parents.data(),count,
        cargo.rig,cargo.sample,camera.units,changed.data(),recovered.data());
    if(cargoSource&&!cargoApplied){cargoRejectStage=5;++cargoPoseRejected;return false;}
    if(cargoApplied){cargoRejectStage=0;++cargoPoseApplied;}
    else StabilizeGameplayPose(camera,currentAction,bind.data(),originalPose.data(),pose.data(),count,changed.data(),recovered.data());
    const bool eyeMarker=attacher<count&&head<count&&attacher>head&&
        NativeRigMath::Descendant(attacher,head,camera.rig.parents.data());
    Vec3 anatomicalEye;
    if(head>=count||!recovered[head]||
       !NativeSkinMath::AnatomicalEye(bind[head],bind[eyeMarker?attacher:head],pose[head],camera.units,anatomicalEye))return false;
    Vec3 rawLine{},bindLine{};const unsigned left=camera.rig.lookup[16],right=camera.rig.lookup[22];
    if(left<count&&right<count&&recovered[left]&&recovered[right]) {
        rawLine=Subtract(pose[right].position,pose[left].position);
        bindLine=Subtract(bind[right].position,bind[left].position);
    }
    const auto policy=NativeSkinMath::Policy(camera.firstPerson,camera.actorHeadOnly,camera.nativeBodyAction);
    const bool followAction=camera.firstPerson&&!camera.actorHeadOnly&&NativeBodyKL::ActionFollowEligible(currentAction,camera.rig.player);
    if(followAction) {
        if(!NativeBodyKL::AdjustAction(camera.rig.player,currentAction,next.rawRoot,anatomicalEye,rawLine,bindLine,next.renderRoot,camera.pairId))return false;
    } else if(policy.bodyFollow&&!camera.nativeBodyAction&&
       !NativeBodyKL::Adjust(camera.rig.player,next.rawRoot,anatomicalEye,rawLine,bindLine,next.renderRoot,camera.pairId))return false;
    if(policy.rootAlignment||followAction) {
        CameraPose aligned;
        if(!NativeBodyMath::AlignHead(next.renderRoot,anatomicalEye,camera.centre.position,camera.units,aligned))return false;
        next.renderRoot=aligned;
    }
    next.rootModified=std::memcmp(&next.rawRoot,&next.renderRoot,sizeof(next.rawRoot))!=0;
    next.modified=SolveGpuRig(camera,originalPose,pose.data(),recovered,changed,next)||cargoApplied;
    if(camera.firstPerson) {
        const auto headWorld=NativeRigMath::ToWorld(next.renderRoot,{pose[head].basis,anatomicalEye}).position;
        const auto offset=Scale(Subtract(headWorld,camera.centre.position),1.f/camera.units);
        if(Finite(offset)) {
            playerHeadOffset[0]=offset.x;playerHeadOffset[1]=offset.y;playerHeadOffset[2]=offset.z;
            playerHeadDistance=std::sqrt(Dot(offset,offset));
        }
    }
    const bool passengerHead=PassengerHeadMaskContext(camera,currentAction);
    const bool collapseHead=head>0&&head<count&&recovered[head]&&policy.headMask&&hideHead&&
        (passengerHead||NativeVisualPolicy::HideHead(policy.headMask,hideHead,
            NativeRigMath::ToWorld(next.renderRoot,pose[head]).position,camera.centre.position,camera.units))&&
        (!camera.actorHeadOnly||NativeSkinMath::ActorHeadNear(NativeRigMath::ToWorld(next.rawRoot,pose[head]).position,
                                                            camera.centre.position,camera.units));
    for(unsigned i=0;i<count;++i) {
        if(changed[i]&&!NativeSkinMath::Palette(bind[i],pose[i],next.palette[i].data()))return false;
    }
    if(collapseHead){next.headVisiblePalette=next.palette;next.hidden=NativeSkinMath::MaskHead(next.palette,count,head,camera.rig.parents.data(),pose[head].position);}
    if(next.hidden&&passengerHead)++passengerHeadsHidden;
    next.head=next.hidden?head:NativeRigMath::MaximumBones;next.modified=next.modified||next.hidden;
    AcquireSRWLockShared(&frameLock);
    if(!publishedFrame.valid||publishedFrame.generation!=camera.generation) {ReleaseSRWLockShared(&frameLock);return false;}
    AcquireSRWLockExclusive(&preparedLock);preparedCache=next;preparedValid=true;ReleaseSRWLockExclusive(&preparedLock);
    ReleaseSRWLockShared(&frameLock);draw=next;return true;
}
void CommitDraw(const CameraFrame& camera,const PreparedDraw& draw) {
    const DWORD now=GetTickCount();lastAge=camera.hands.tick?now-camera.hands.tick:UINT32_MAX;
    if(twistHistory.player!=camera.rig.player||twistHistory.model!=camera.rig.model||
       twistHistory.recenterAck!=camera.hands.recenterAck) {
        twistHistory={};twistHistory.player=camera.rig.player;twistHistory.model=camera.rig.model;
        twistHistory.recenterAck=camera.hands.recenterAck;
    }
    for(unsigned side=0;side<2;++side)if(draw.visual.active[side]) {
        ++applied[side];if(draw.armResult[side].clamped)++clamped[side];
        requestedDistance[side]=draw.requested[side];armReach[side]=draw.reach[side];targetError[side]=draw.error[side];
        residualTwistDegrees[side]=draw.armResult[side].residualTwistRadians*57.2957795131f;
        const auto& helpers=draw.helpers[side];
        if(helpers.foreTwist<draw.count||helpers.upperMuscle<draw.count)++helperApplied[side];
        auto& history=twistHistory.side[side];
        if(!history.valid||now-history.tick>300||camera.pairId!=history.pair)
            history={true,camera.pairId,now,draw.armResult[side].residualTwistRadians};
        AcquireSRWLockExclusive(&diagnosticLock);
        auto& previous=lastDiagnostic[side];
        if(!previous.valid||previous.player!=draw.diagnostic[side].player||previous.model!=draw.diagnostic[side].model||
           previous.pairId!=draw.diagnostic[side].pairId)previous=draw.diagnostic[side];
        ReleaseSRWLockExclusive(&diagnosticLock);
    }
}
void __fastcall ModelSpaceHook(void* modelPointer,void*) {
    ++calls;
    original(modelPointer);
    ProfileTiming::Scope profile(ProfileTiming::HandsModel);
    if(inHook||!installed.load())return;
    const auto model=reinterpret_cast<std::uintptr_t>(modelPointer);
    std::uintptr_t player=0;
    if(!Player(model,player))return;
    ++playerCalls;lastPlayer.store(player);lastModel.store(model);
    CameraFrame camera;const bool active=FrameSnapshot(camera);
    if(active)++insideRenderCalls;else ++outsideRenderCalls;
    inHook=true;
    CameraFrame::Rig captured;
    // Observe immutable rig metadata only, after the native conversion. The
    // animated model+0x4D0 buffer is neither read nor written by this backend.
    if(CaptureRigMetadata(model,player,captured)) {
        AcquireSRWLockExclusive(&rawRigLock);originalRig=captured;ReleaseSRWLockExclusive(&rawRigLock);
    } else ++rejected;
    inHook=false;
}
void RunTechniqueDraw(void* package,void* contexts,void* entries,unsigned count,void* state) {
    ++shadowPassCalls;
    const auto previous=shadowPass;const auto previousTechnique=techniqueObservation;shadowPass={};
    const auto depth=++techniqueDepth;
    const bool diagnostic=depth<=8&&drawContextDiagnostics.load(std::memory_order_relaxed);
    if(diagnostic){diagnosticTechniqueBinds[depth-1]={diagnosticWorldBind,diagnosticNativeWorldBind};diagnosticWorldBind={};diagnosticNativeWorldBind={};}
    struct Restore {NativeShadowPassKL::Context previous;TechniqueObservation previousTechnique;unsigned depth;bool diagnostic;~Restore(){
        if(paletteLease.active&&paletteLease.depth==depth)RetirePaletteLease();--techniqueDepth;shadowPass=previous;techniqueObservation=previousTechnique;
        if(diagnostic){diagnosticWorldBind=diagnosticTechniqueBinds[depth-1].world;diagnosticNativeWorldBind=diagnosticTechniqueBinds[depth-1].nativeWorld;}
    }} restore{previous,previousTechnique,depth,diagnostic};
    // The native callee reads this same live byte immediately. Normal passes
    // avoid any extra graph reads; only named shadow passes need the proof.
    const auto at=reinterpret_cast<std::uintptr_t>(package);
    const auto pass=*reinterpret_cast<const std::uint8_t*>(at+0x62);
    // The authenticated original Draw immediately dereferences this exact
    // package table too. These are native-call-stack metadata, not a guessed
    // classification inferred from the current GPU colour/depth format.
    techniqueObservation={at,*reinterpret_cast<const std::uint32_t*>(at+0xC+pass*4),pass,reinterpret_cast<std::uintptr_t>(state),++diagnosticTechniqueSerial};
    if((pass==2||pass==3)&&NativeShadowPassKL::Snapshot(image,at,reinterpret_cast<std::uintptr_t>(state),ReadBytes,shadowPass))++shadowPassScopes;
    originalTechniqueDraw(package,contexts,entries,count,state);
}
// Only genuinely nested package draws allocate this snapshot. Normal scene
// draws keep their former small stack footprint. The two staging buffers can
// otherwise be reused by a nested normal/shadow body before the outer model
// has finished submitting all of its materials.
void NestedTechniqueDraw(void* package,void* contexts,void* entries,unsigned count,void* state) {
    const auto previous=paletteLease;const auto previousUpload=boneUpload;
    const bool support=boundSupport,gesture=boundGestureHeld,coupled=boundCoupled,shadow=boundShadow;
    const auto rawGrip=boundRawGrip;const auto pair=boundPair,generation=boundGeneration;const auto previousHead=uploadedHead;
    std::array<std::array<float,12>,NativeRigMath::MaximumBones> saved{};
    std::memcpy(saved.data(),reinterpret_cast<const void*>(previous.staging),previous.count*48);
    RunTechniqueDraw(package,contexts,entries,count,state);
    RetirePaletteLease();
    std::uint64_t currentPair=0,currentGeneration=0;
    if(!DrawFrameActive(currentPair,&currentGeneration)||currentPair!=pair||currentGeneration!=generation)return;
    const auto write=[](std::uintptr_t at,const void* data,std::size_t bytes) {
        if(!Writable(at,bytes))return false;std::memcpy(reinterpret_cast<void*>(at),data,bytes);return true;
    };
    if(!NativeSkinPaletteLease::Resume(previous,ReadBytes,write)){++paletteLeaseRejected;return;}
    std::memcpy(reinterpret_cast<void*>(previous.staging),saved.data(),previous.count*48);
    auto* cached=reinterpret_cast<std::uintptr_t*>(image+kLastUploadedPalette);
    if((*cached==reinterpret_cast<std::uintptr_t>(gpuPalette.data())||*cached==reinterpret_cast<std::uintptr_t>(gpuShadowPalette.data()))&&
       Writable(reinterpret_cast<std::uintptr_t>(cached),sizeof(*cached))){*cached=0;++gpuUploadInvalidated;}
    paletteLease=previous;boneUpload=previousUpload;boundSupport=support;boundGestureHeld=gesture;boundCoupled=coupled;boundShadow=shadow;
    boundRawGrip=rawGrip;boundPair=pair;boundGeneration=generation;++paletteLeaseResumed;
    uploadedHead=previousHead;
}
void __fastcall TechniqueDrawHook(void* package,void*,void* contexts,void* entries,unsigned count,void* state) {
    if(techniqueDepth>=8&&paletteLease.active){RetirePaletteLease();++paletteLeaseRejected;}
    if(paletteLease.active&&techniqueDepth)NestedTechniqueDraw(package,contexts,entries,count,state);
    else RunTechniqueDraw(package,contexts,entries,count,state);
}
// This main-view colour submission is the source for its matching mask.
struct ColourSkin {
    PreparedDraw draw;
    NativeSkinMath::PaletteSource nativeSource{};
    std::uintptr_t state=0;
    std::uint64_t epoch=0;
    unsigned eye=2;
    bool Same(const CameraFrame& camera,std::uintptr_t entry,std::uintptr_t renderState,
              std::uintptr_t cache,unsigned count,const NativeSkinMath::PaletteSource& source,
              std::uint64_t renderEpoch,unsigned renderEye)const {
        return epoch&&epoch==renderEpoch&&eye==renderEye&&state==renderState&&draw.entry==entry&&
            draw.visual.player==camera.rig.player&&draw.visual.pairId==camera.pairId&&
            draw.visual.recenterAck==camera.recenterAck&&draw.cache==cache&&draw.count==count&&
            NativeSkinMath::SamePaletteSource(nativeSource,source);
    }
};
thread_local ColourSkin colourSkin;

void __fastcall BindPaletteHook(void* entryPointer,void*,void* statePointer,std::uintptr_t extra) {
    ++gpuBinderCalls;bindThread.store(GetCurrentThreadId());
    RetirePaletteLease();
    {ProfileTiming::Scope nativeProfile(ProfileTiming::NativePaletteBind);originalBind(entryPointer,statePointer,extra);}
    ProfileTiming::Scope profile(ProfileTiming::HandsBind);
    const auto entry=reinterpret_cast<std::uintptr_t>(entryPointer);
    const auto state=reinterpret_cast<std::uintptr_t>(statePointer);
    std::uintptr_t data=0,player=0,vtable=0,geometry=0,owner=0,model=0,cache=0,bound=0;
    if(!Read(image+kGameData,data)||!Read(data+0xA2C,player)||!player||
       !Read(entry,vtable)||vtable!=image+kBonesVtable||!Read(entry+0xF4,geometry)||
       !Read(geometry+0x60,owner)||owner!=player||!Read(player+0x70,model)||!model||
       !Read(entry+0x108,cache)||!cache||!Read(state+0x3BC,bound)||!bound)return;
    std::uintptr_t verifiedPlayer=0;
    if(!Player(model,verifiedPlayer)||verifiedPlayer!=player)return;
    const bool shadow=NativeShadowPassKL::Matches(image,shadowPass,state,ReadBytes);
    if(bound==cache+0x90&&!shadow) {
        // The stance/eye-anchor observer still describes the primary native
        // entry cache. Additional instance draws may use different roots;
        // accepting those must not change this independent camera authority.
        lastOwnedEntry.store(entry);lastOwnedModel.store(model);lastOwnedEntryTick.store(GetTickCount());
        ++gpuPlayerBinds; // Preserve the old direct-path counter definition.
    }
    CameraFrame camera;
    if(!NativeCameraKL::DrawStateActive(state,nullptr,true)){++otherViewPreserved;return;}
    if(!FrameSnapshot(camera)||frameThread.load()!=GetCurrentThreadId()||!camera.rig.valid||
       camera.rig.player!=player||camera.rig.model!=model) {++gpuNoContext;return;}
    // PrepareDraw performs the full owned-entry revalidation below. Resolve
    // only the bounded native count here instead of traversing that graph a
    // second time for every body pass.
    std::uint32_t boneCount=0;
    if(!Read(entry+0x114,boneCount)||boneCount<2||boneCount>camera.rig.count||
       boneCount>NativeRigMath::MaximumBones){++paletteSourceRejected;return;}
    const auto readSource=[&](std::uintptr_t at,void* output,std::size_t bytes){
        // The original binder and DrawStateActive have just consumed this
        // exact live state. Reading its own fields does not need RPM calls.
        if((at==state+0x8C&&bytes==sizeof(CameraPose))||
           ((at==state+0x3BC||at==state+0x3C4)&&bytes==4)) {
            std::memcpy(output,reinterpret_cast<const void*>(at),bytes);return true;
        }
        return ReadBytes(at,output,bytes);
    };
    NativeSkinMath::PaletteSource source;
    if(bound!=cache+0x90)++alternatePaletteCandidates;
    if(!NativeSkinMath::ResolvePaletteSource(image,entry,state,cache,boneCount,readSource,source,alternatePaletteProof)) {
        ++paletteSourceRejected;return;
    }
    PreparedDraw draw;
    const auto pass=techniqueObservation.pass;
    std::uint64_t renderEpoch=0;unsigned renderEye=2;
    const auto context=renderContextProvider.load(std::memory_order_relaxed);
    const bool imageKnown=!shadow&&(pass==1||pass==9)&&context&&context(renderEpoch,renderEye)&&renderEpoch&&renderEye<2;
    const bool sameColour=imageKnown&&colourSkin.Same(camera,entry,state,cache,boneCount,source,renderEpoch,renderEye);
    if(!Writable(state+0x3BC,sizeof(std::uintptr_t))||
       (!(sameColour&&pass==9)&&!PrepareDraw(camera,entry,draw,&source))) {++gpuRejected;return;}
    if(sameColour&&pass==9){draw=colourSkin.draw;draw.generation=camera.generation;}
    if(draw.cache!=cache||draw.count!=boneCount) {++gpuRejected;return;}
    // Preview and actual binding share immutable staging for this generation.
    // Only the body binder may replace the palette consumed by the renderer.
    AcquireSRWLockShared(&frameLock);
    if(!publishedFrame.valid||publishedFrame.generation!=camera.generation) {
        ReleaseSRWLockShared(&frameLock);++gpuNoContext;return;
    }
    std::uintptr_t currentCache=0,currentBound=0;
    NativeSkinMath::PaletteSource currentSource;
    if(!Read(entry+0x108,currentCache)||currentCache!=draw.cache||
       !Read(state+0x3BC,currentBound)||currentBound!=bound||
       !NativeSkinMath::ResolvePaletteSource(image,entry,state,cache,boneCount,readSource,currentSource,alternatePaletteProof)||
       !NativeSkinMath::SamePaletteSource(source,currentSource)) {
        ReleaseSRWLockShared(&frameLock);++gpuRejected;return;
    }
    if(imageKnown&&pass==1&&!sameColour)colourSkin={draw,source,state,renderEpoch,renderEye};
    auto& staging=shadow?gpuShadowPalette:gpuPalette;
    const std::uintptr_t paletteAddress=reinterpret_cast<std::uintptr_t>(staging.data());
    std::uintptr_t lastUploaded=0;
    if(draw.modified&&(!Read(image+kLastUploadedPalette,lastUploaded)||
       (lastUploaded==paletteAddress&&!Writable(image+kLastUploadedPalette,sizeof(std::uintptr_t))))) {
        ReleaseSRWLockShared(&frameLock);++gpuCacheRejected;return;
    }
    if(draw.rootModified) {
        reinterpret_cast<SetWorld>(image+kSetWorld)(statePointer,&draw.renderRoot);
        if(bonesLightCacheProof&&*reinterpret_cast<const std::uint32_t*>(state+0x3C4)) {
            const auto read=[&](std::uintptr_t at,void* bytes,std::size_t count){
                if((at==state+0x8C&&count==sizeof(CameraPose))||
                   ((at==state+0x3C0||at==state+0x3C4||at==image+NativeBonesLightCache::CacheRva)&&count==4)){
                    std::memcpy(bytes,reinterpret_cast<const void*>(at),count);return true;}return false;};
            const auto write=[](std::uintptr_t at,const void* bytes,std::size_t count){SIZE_T wrote=0;return
                WriteProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),bytes,count,&wrote)&&wrote==count;};
            if(NativeBonesLightCache::Invalidate(image,state,draw.rawRoot,draw.renderRoot,read,write,bonesLightCacheProof)==NativeBonesLightCache::Result::Invalidated)
                ++bonesLightCacheInvalidated;
        }
    }
    if(draw.modified) {
        // 0x490CF1 consumes state+0x3BC after this binder returns. The native
        // B893B0 pointer cache would skip changed data at the same TLS address.
        // Retire only our address, leaving all native resource caches intact.
        const auto& selected=shadow&&draw.hidden?draw.headVisiblePalette:draw.palette;
        std::memcpy(staging.data(),selected.data(),draw.count*sizeof(staging[0]));
        if(lastUploaded==paletteAddress) {
            *reinterpret_cast<std::uintptr_t*>(image+kLastUploadedPalette)=0;++gpuUploadInvalidated;
        }
        *reinterpret_cast<std::uintptr_t*>(state+0x3BC)=paletteAddress;
        paletteLease={image,state,entry,geometry,player,model,cache,paletteAddress,source,boneCount,techniqueDepth,true};
        boundGestureHeld=supportState.held;boundCoupled=draw.visual.supportHeld;
        boundRawGrip=std::isfinite(camera.input.grip[0])?camera.input.grip[0]:-1.f;
        boundSupport=boundGestureHeld||(HandsMath::ValidFrame(camera.input,camera.renderPairPose?camera.renderAdmissionTick:GetTickCount(),camera.recenterAck)&&
            (camera.input.inputValid[0]&HandsIPC::kGripInput)&&boundRawGrip>.55f&&boundRawGrip<=1.f);
        boundShadow=shadow;boundPair=camera.pairId;boundGeneration=camera.generation;boneUpload={};
        uploadedHead=shadow?NativeRigMath::MaximumBones:draw.head;
    }
    if(!shadow){AcquireSRWLockExclusive(&visualLock);visual=draw.visual;visualGeneration=camera.generation;ReleaseSRWLockExclusive(&visualLock);}
    ReleaseSRWLockShared(&frameLock);
    if(!shadow)CommitDraw(camera,draw);
    if(source.alternate)++alternatePaletteApplied;
    if(draw.modified){++gpuPalettes;if(!shadow&&techniqueObservation.pass==9)
        if(const auto observer=projectionBindObserver.load(std::memory_order_relaxed))observer(state,entry,0);}
    if(shadow&&draw.hidden)++shadowHeadPalettes;else if(draw.hidden)++headHidden;
}
} // namespace
void SetDrawContextDiagnostics(bool enabled) {drawContextDiagnostics.store(enabled,std::memory_order_relaxed);}
void ObserveWorldBind(void* state,const CameraPose* source,std::uint32_t callerRva) {
    RetirePaletteLease();
    if(!drawContextDiagnostics.load(std::memory_order_relaxed))return;
    NativeDrawContextKL::WorldBind next;next.state=reinterpret_cast<std::uintptr_t>(state);
    next.source=reinterpret_cast<std::uintptr_t>(source);next.callerRva=callerRva;next.serial=++diagnosticWorldSerial;
    next.techniqueSerial=techniqueObservation.serial;
    // The native caller/observer owns this exact source for the duration of
    // this call. Copy it now: stack instance/derived WORLD expires on return.
    if(source){std::memcpy(&next.world,source,sizeof(next.world));next.valid=true;}
    ReadCurrentNativeEntry(next.state,next.entry);
    diagnosticWorldBind=next;
    // These are proven native SetWorld caller/rigid-observer positions,
    // distinct from subsequent adapter SetWorld calls for the same entry.
    if(callerRva==0x72001||callerRva==0x7205f||callerRva==0x72f6a||callerRva==0x739e0)
        diagnosticNativeWorldBind=next;
}
bool SnapshotNativeDrawContext(NativeDrawContextKL::Context& output) {
    const bool active=NativeDrawContextKL::Capture(image,techniqueObservation.state,techniqueObservation.package,
        techniqueObservation.technique,techniqueObservation.pass,techniqueDepth,ReadBytes,output);
    output.sourceBind=diagnosticWorldBind;output.nativeSourceBind=diagnosticNativeWorldBind;
    output.techniqueSerial=techniqueObservation.serial;
    output.sourceBindMatchesState=diagnosticWorldBind.valid&&diagnosticWorldBind.state==output.state&&
        diagnosticWorldBind.entry==output.entry&&diagnosticWorldBind.techniqueSerial==output.techniqueSerial;
    output.nativeSourceMatchesEntry=diagnosticNativeWorldBind.valid&&diagnosticNativeWorldBind.state==output.state&&
        diagnosticNativeWorldBind.entry==output.entry&&diagnosticNativeWorldBind.techniqueSerial==output.techniqueSerial;
    if(output.sourceBindMatchesState||output.nativeSourceMatchesEntry)output.evidence|=NativeDrawContextKL::SourceWorldRead;
    output.skinLease=paletteLease.active;
    output.skinLeaseMatchesEntry=paletteLease.active&&paletteLease.state==output.state&&paletteLease.entry==output.entry;
    if(output.skinLeaseMatchesEntry) {
        output.support=boundSupport;output.coupled=boundCoupled;output.shadow=boundShadow;
        output.pair=boundPair;output.generation=boundGeneration;
        output.leaseSourcePalette=paletteLease.source.palette;output.leaseStaging=paletteLease.staging;
        output.leaseSourceRows=paletteLease.source.rows;output.leaseSourceIndex=paletteLease.source.index;
        output.leaseSourceInstances=paletteLease.source.instances;
    }
    return active;
}
bool CoupledSkinLightingActive() {
    return paletteLease.active&&boundCoupled&&!boundShadow&&boundPair&&boundGeneration==frameGeneration.load()&&
        *reinterpret_cast<const std::uintptr_t*>(paletteLease.state+0x3BC)==paletteLease.staging&&
        NativeCameraKL::DrawStateActive(paletteLease.state,nullptr,true);
}
bool ReloadPropDrawActive(std::uintptr_t player,std::uint64_t pair){
    if(!paletteLease.active||boundShadow||techniqueObservation.pass!=1||paletteLease.player!=player||
       !pair||boundPair!=pair||boundGeneration!=frameGeneration.load()||
       *reinterpret_cast<const std::uintptr_t*>(paletteLease.state+0x3BC)!=paletteLease.staging)return false;
    std::uintptr_t entry=0;bool firstPerson=false;
    return ReadCurrentNativeEntry(paletteLease.state,entry)&&entry==paletteLease.entry&&
        NativeCameraKL::DrawStateActive(paletteLease.state,&firstPerson)&&firstPerson;
}
bool PostfilterForegroundActive() {
    if(!paletteLease.active||boundShadow||!boundPair||boundGeneration!=frameGeneration.load()||
       *reinterpret_cast<const std::uintptr_t*>(paletteLease.state+0x3BC)!=paletteLease.staging||
       !NativeCameraKL::DrawStateActive(paletteLease.state))return false;
    std::uintptr_t entry=0;unsigned pass=0;
    return ReadCurrentNativeEntry(paletteLease.state,entry,&pass)&&entry==paletteLease.entry&&pass==9;
}
bool ReadCurrentNativeEntry(std::uintptr_t state,std::uintptr_t& entry,unsigned* pass) {
    if(!techniqueDepth||techniqueDepth>8||techniqueObservation.state!=state||!techniqueObservation.package)return false;
    std::uint32_t record=0,provider=0,after=0;
    // Actual48F0A0 writes the current material record at state+14, then calls
    // its +C provider's vtable+18 binder. Never infer that provider from a
    // WORLD pointer: instances and view-aligned parts use other addresses.
    // Both are exact live addresses just written/dereferenced by48F0A0 on
    // this same closed native call stack, as with DrawStateActive matrices.
    std::memcpy(&record,reinterpret_cast<const void*>(state+0x14),4);
    if(record<0x10000u||record>0xfffffff0u)return false;
    std::memcpy(&provider,reinterpret_cast<const void*>(std::uintptr_t(record)+0xC),4);
    std::memcpy(&after,reinterpret_cast<const void*>(state+0x14),4);
    if(!provider||after!=record)return false;
    entry=provider;if(pass)*pass=techniqueObservation.pass;return true;
}
namespace {
std::uint32_t Fingerprint(const void* data,std::size_t bytes) {
    const auto* p=static_cast<const unsigned char*>(data);std::uint32_t hash=2166136261u;
    for(std::size_t i=0;i<bytes;++i){hash^=p[i];hash*=16777619u;}return hash;
}
template<class Shader>std::uint32_t ShaderFingerprint(Shader* shader) {
    if(!shader)return 0;
    for(const auto& entry:skinShaderCache)if(entry.shader==shader)return entry.hash;
    UINT bytes=0;std::uint32_t hash=0;
    if(SUCCEEDED(shader->GetFunction(nullptr,&bytes))&&bytes&&bytes<=65536) {
        std::vector<unsigned char> code(bytes);
        if(SUCCEEDED(shader->GetFunction(code.data(),&bytes)))hash=Fingerprint(code.data(),bytes);
    }
    if(skinShaderCache.size()>=64){skinShaderCache.front().shader->Release();skinShaderCache.erase(skinShaderCache.begin());}
    shader->AddRef();skinShaderCache.push_back({shader,hash});return hash;
}
}
void InvalidateMaterialProbe(){for(auto& entry:skinShaderCache)entry.shader->Release();skinShaderCache.clear();skinProbeDevice=nullptr;
    for(auto& budget:skinProbeBudget)budget={};}
namespace {
struct SkinPassPairPending {
    bool valid=false,compared=false;
    DWORD sampledTick=0;
    std::uintptr_t state=0,entry=0,player=0,model=0,source=0;
    std::uint64_t pair=0,epoch=0,generation=0;
    unsigned count=0;
    CameraPose world{};
    std::array<std::array<float,12>,NativeRigMath::MaximumBones> native{},rendered{};
};
thread_local SkinPassPairPending skinPassPairPending;
std::atomic<std::uint64_t> skinPassColours{0},skinPassCompared{0},skinPassDifferent{0},skinPassSourceChanged{0},skinPassRejected{0};
SRWLOCK skinPassLock=SRWLOCK_INIT;
std::atomic<bool> skinPassDiagnostics{false};
SkinPassPairSample skinPassLast,skinPassLastDifferent;
void ObserveSkinPassPair(){
    // A disabled diagnostic performs this one flag read only. No clocks,
    // native graph reads, hashing, device getters or locks on that path.
    if(!skinPassDiagnostics.load(std::memory_order_relaxed))return;
    if(!paletteLease.active||boundShadow||!boundPair||
       (techniqueObservation.pass!=1&&techniqueObservation.pass!=9))return;
    const auto provider=renderContextProvider.load(std::memory_order_relaxed);
    std::uint64_t epoch=0;unsigned eye=2;
    if(!provider||!provider(epoch,eye)||!epoch||eye>1)return;
    auto& pending=skinPassPairPending;
    const bool same=pending.valid&&pending.state==paletteLease.state&&pending.entry==paletteLease.entry&&
        pending.player==paletteLease.player&&pending.model==paletteLease.model&&pending.source==paletteLease.source.palette&&
        pending.pair==boundPair&&pending.epoch==epoch&&pending.count==paletteLease.count;
    const auto now=GetTickCount();
    if(techniqueObservation.pass==1){
        if(same||(pending.sampledTick&&now-pending.sampledTick<500))return;
    }else if(!same||pending.compared)return;
    if(!paletteLease.count||paletteLease.count>NativeRigMath::MaximumBones||
       boundGeneration!=frameGeneration.load()||
       *reinterpret_cast<const std::uintptr_t*>(paletteLease.state+0x3BC)!=paletteLease.staging||
       !NativeCameraKL::DrawStateActive(paletteLease.state)||
       !NativeSkinPaletteLease::OriginalCurrent(paletteLease,ReadBytes)){
        ++skinPassRejected;return;
    }
    std::array<std::array<float,12>,NativeRigMath::MaximumBones> native;
    const auto bytes=paletteLease.count*48;
    if(!ReadBytes(paletteLease.source.palette,native.data(),bytes)){++skinPassRejected;return;}
    CameraPose world;std::memcpy(&world,reinterpret_cast<const void*>(paletteLease.state+0x8C),sizeof(world));
    if(techniqueObservation.pass==1){
        pending.valid=true;pending.compared=false;pending.sampledTick=now;pending.state=paletteLease.state;pending.entry=paletteLease.entry;
        pending.player=paletteLease.player;pending.model=paletteLease.model;pending.source=paletteLease.source.palette;
        pending.pair=boundPair;pending.epoch=epoch;pending.generation=boundGeneration;pending.count=paletteLease.count;
        pending.world=world;std::memcpy(pending.native.data(),native.data(),bytes);
        std::memcpy(pending.rendered.data(),reinterpret_cast<const void*>(paletteLease.staging),bytes);++skinPassColours;return;
    }
    pending.compared=true;SkinPassPairSample sample;
    sample.valid=true;sample.tick=now;sample.bones=pending.count;sample.firstPass=1;sample.lastPass=9;
    sample.pair=boundPair;sample.epoch=epoch;sample.firstGeneration=pending.generation;sample.lastGeneration=boundGeneration;
    sample.entry=paletteLease.entry;sample.state=paletteLease.state;
    sample.sourceEqual=!std::memcmp(pending.native.data(),native.data(),bytes);
    sample.paletteEqual=!std::memcmp(pending.rendered.data(),reinterpret_cast<const void*>(paletteLease.staging),bytes);
    sample.worldEqual=!std::memcmp(&pending.world,&world,sizeof(world));
    const auto current=reinterpret_cast<const float*>(paletteLease.staging);
    for(unsigned i=0;i<pending.count;++i)for(unsigned j=0;j<12;++j){
        const auto delta=std::fabs(pending.rendered[i][j]-current[i*12+j]);
        if(std::isfinite(delta))sample.maxPaletteDelta=std::max(sample.maxPaletteDelta,delta);
    }
    const auto first=reinterpret_cast<const float*>(&pending.world),last=reinterpret_cast<const float*>(&world);
    for(unsigned i=0;i<12;++i){const auto delta=std::fabs(first[i]-last[i]);if(std::isfinite(delta))sample.maxWorldDelta=std::max(sample.maxWorldDelta,delta);}
    ++skinPassCompared;if(!sample.sourceEqual)++skinPassSourceChanged;
    if(!sample.paletteEqual||!sample.worldEqual)++skinPassDifferent;
    AcquireSRWLockExclusive(&skinPassLock);skinPassLast=sample;
    if(!sample.paletteEqual||!sample.worldEqual)skinPassLastDifferent=sample;
    ReleaseSRWLockExclusive(&skinPassLock);
}
}
void SetSkinPassPairDiagnostics(bool enabled){skinPassDiagnostics.store(enabled,std::memory_order_relaxed);}
SkinPassPairEvidence SnapshotSkinPassPair(){
    SkinPassPairEvidence output;output.colours=skinPassColours;output.compared=skinPassCompared;output.different=skinPassDifferent;
    output.sourceChanged=skinPassSourceChanged;output.rejected=skinPassRejected;
    AcquireSRWLockShared(&skinPassLock);output.last=skinPassLast;output.lastDifferent=skinPassLastDifferent;ReleaseSRWLockShared(&skinPassLock);return output;
}
void ObserveNativeDraw(IDirect3DDevice9* device) {
    if(!device||!paletteLease.active||!boundPair)return;
    ObserveSkinPassPair();
    if(skinProbeDevice!=device){InvalidateMaterialProbe();skinProbeDevice=device;}
    auto& budget=skinProbeBudget[(boundSupport?1:0)+(boundShadow?2:0)];
    const auto now=GetTickCount();
    if(!budget.tick||now-budget.tick>=500){budget.tick=now;budget.left=1;}
    if(!budget.left)return;--budget.left;
    ProfileTiming::Scope probeCost(ProfileTiming::SkinMaterialEvidenceOwn);
    std::uint32_t bound=0,geometry=0;std::uint64_t pair=0,generation=0;
    if(!Read(paletteLease.state+0x3BC,bound)||bound!=paletteLease.staging||
       !Read(paletteLease.entry+0xF4,geometry)||geometry!=paletteLease.geometry||
       !DrawFrameActive(pair,&generation)||pair!=boundPair||generation!=boundGeneration||
       !NativeCameraKL::DrawStateActive(paletteLease.state,nullptr,true)){++skinDrawRejected;return;}
    SkinMaterialSample sample;sample.valid=true;sample.support=boundSupport;sample.gestureHeld=boundGestureHeld;
    sample.coupled=boundCoupled;sample.rawGrip=boundRawGrip;sample.shadow=boundShadow;sample.tick=now;
    sample.entry=paletteLease.entry;sample.state=paletteLease.state;sample.pair=pair;sample.bones=paletteLease.count;
    sample.sourcePalette=paletteLease.source.palette;sample.renderPalette=paletteLease.staging;
    sample.package=techniqueObservation.package;sample.technique=techniqueObservation.technique;sample.pass=techniqueObservation.pass;
    sample.paletteHash=Fingerprint(reinterpret_cast<const void*>(bound),sample.bones*48);
    std::array<std::array<float,12>,NativeRigMath::MaximumBones> native{};
    if(ReadBytes(sample.sourcePalette,native.data(),sample.bones*48))sample.sourceHash=Fingerprint(native.data(),sample.bones*48);
    IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;IDirect3DVertexDeclaration9* declaration=nullptr;
    if(SUCCEEDED(device->GetVertexShader(&vs))&&vs){sample.vsHash=ShaderFingerprint(vs);vs->Release();}
    if(SUCCEEDED(device->GetPixelShader(&ps))&&ps){sample.psHash=ShaderFingerprint(ps);ps->Release();}
    if(SUCCEEDED(device->GetVertexDeclaration(&declaration))&&declaration) {
        D3DVERTEXELEMENT9 elements[MAXD3DDECLLENGTH+1]{};UINT count=MAXD3DDECLLENGTH+1;
        if(SUCCEEDED(declaration->GetDeclaration(elements,&count))&&count<=MAXD3DDECLLENGTH+1)
            sample.declarationHash=Fingerprint(elements,count*sizeof(elements[0]));
        declaration->Release();
    }
    for(unsigned i=0;i<4;++i) {
        IDirect3DBaseTexture9* texture=nullptr;
        if(SUCCEEDED(device->GetTexture(i,&texture))&&texture) {
            sample.textures[i]=reinterpret_cast<std::uintptr_t>(texture);
            if(texture->GetType()==D3DRTYPE_TEXTURE){D3DSURFACE_DESC desc{};
                if(SUCCEEDED(static_cast<IDirect3DTexture9*>(texture)->GetLevelDesc(0,&desc))){
                    const std::uint32_t shape[]={desc.Width,desc.Height,static_cast<std::uint32_t>(desc.Format)};
                    sample.textureShapes[i]=Fingerprint(shape,sizeof(shape));}}
            texture->Release();
        }
    }
    DWORD render[4]{};device->GetRenderState(D3DRS_CULLMODE,&render[0]);device->GetRenderState(D3DRS_ALPHABLENDENABLE,&render[1]);
    device->GetRenderState(D3DRS_COLORWRITEENABLE,&render[2]);device->GetRenderState(D3DRS_ZENABLE,&render[3]);sample.renderStateHash=Fingerprint(render,sizeof(render));
    if(boneUpload.valid&&boneUpload.count&&boneUpload.start+boneUpload.count<=256) {
        std::array<float,1024> actual{};sample.startRegister=boneUpload.start;sample.registerCount=boneUpload.count;
        sample.constantHash=Fingerprint(boneUpload.rows.data(),boneUpload.count*16);
        if(SUCCEEDED(device->GetVertexShaderConstantF(boneUpload.start,actual.data(),boneUpload.count))) {
            sample.constantsCompared=true;sample.constantsMatch=!std::memcmp(actual.data(),boneUpload.rows.data(),boneUpload.count*16);
            if(!sample.constantsMatch)++skinConstantMismatches;
        }
    }
    AcquireSRWLockExclusive(&skinProbeLock);skinProbe[boundSupport?1:0][boundShadow?1:0]=sample;ReleaseSRWLockExclusive(&skinProbeLock);++skinDrawSamples;
}
bool SnapshotSkinMaterial(bool support,SkinMaterialSample& output,bool shadow) {
    AcquireSRWLockShared(&skinProbeLock);output=skinProbe[support?1:0][shadow?1:0];ReleaseSRWLockShared(&skinProbeLock);return output.valid;
}

bool Install(bool enabled,LogCallback log) {
    if(installed.load())return true;
    if(!enabled)return false;
    wchar_t path[MAX_PATH]{};char digest[65]{};
    const DWORD length=GetModuleFileNameW(nullptr,path,MAX_PATH);
    if(!length||length>=MAX_PATH||!HashFile(path,digest)||!IsSupportedExecutableHash(digest)) {
        if(log)log("Native K&L hands: EXE hash differs; native skeleton remains unchanged.");return false;
    }
    image=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    bonesLightCacheProof=NativeBonesLightCache::Verify(image,ReadBytes);
    alternatePaletteProof=NativeSkinMath::VerifyPaletteSourceImage(image,ReadBytes);
    if(!alternatePaletteProof&&log)log("Native K&L alternate palette layout differs; retaining unqualified instance draws unchanged.");
    if(!NativeActionKL::InitializeVerifiedImage(image)) {
        if(log)log("Native K&L actions: current-state signatures differ; native rig retained.");return false;
    }
    HMODULE module=nullptr;wchar_t settingsPath[MAX_PATH]{};
    if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCWSTR>(&Install),&module)&&GetModuleFileNameW(module,settingsPath,MAX_PATH)) {
        wchar_t* tail=wcsrchr(settingsPath,L'\\');
        if(tail){wcscpy(tail+1,L"vr.ini");hideHead=GetPrivateProfileIntW(L"Hands",L"HideHead",1,settingsPath)!=0;
            supportGripEnabled=GetPrivateProfileIntW(L"Weapons",L"SupportGrip",1,settingsPath)!=0&&
                GetPrivateProfileIntW(L"Controls",L"Enabled",1,settingsPath)!=0&&
                GetPrivateProfileIntW(L"Controls",L"ImmersiveLayout",1,settingsPath)!=0;}
    }
    const unsigned char signature[]={0x51,0x53,0x8B,0xD9,0x8B,0x83,0xD0,0x04,0x00,0x00,0x8D,0x48,0x30,0x55,0x8B,0xD1};
    const unsigned char caller[]={0xE8,0xAB,0xE7,0x14,0x00};
    const unsigned char bindSignature[]={0xA1,0xA4,0x78,0xB6,0x00,0x83,0xEC,0x08,0x56,0x8B,0xF1};
    const unsigned char consumerSignature[]={0x8B,0xB5,0xBC,0x03,0x00,0x00,0x39,0x35};
    const unsigned char consumerStore[]={0x89,0x35};
    std::uintptr_t bindMethod=0,consumerCache=0,storeCache=0;
    if(std::memcmp(reinterpret_cast<const void*>(image+kModelSpace),signature,sizeof(signature))||
       std::memcmp(reinterpret_cast<const void*>(image+kModelSpaceCaller),caller,sizeof(caller))||
       std::memcmp(reinterpret_cast<const void*>(image+kBindPalette),bindSignature,sizeof(bindSignature))||
       std::memcmp(reinterpret_cast<const void*>(image+kPaletteConsumer),consumerSignature,sizeof(consumerSignature))||
       std::memcmp(reinterpret_cast<const void*>(image+kPaletteConsumerStore),consumerStore,sizeof(consumerStore))||
       !Read(image+kPaletteConsumer+sizeof(consumerSignature),consumerCache)||consumerCache!=image+kLastUploadedPalette||
       !Read(image+kPaletteConsumerStore+sizeof(consumerStore),storeCache)||storeCache!=image+kLastUploadedPalette||
       !Read(image+kBonesVtable+0x18,bindMethod)||bindMethod!=image+kBindPalette) {
        if(log)log("Native K&L hands: skeleton signatures differ; native animation remains unchanged.");return false;
    }
    void* target=reinterpret_cast<void*>(image+kModelSpace);
    void* bindTarget=reinterpret_cast<void*>(image+kBindPalette);
    if(MH_CreateHook(target,reinterpret_cast<void*>(&ModelSpaceHook),reinterpret_cast<void**>(&original))!=MH_OK) {
        if(log)log("Native K&L hands: model-space hook installation failed.");return false;
    }
    if(MH_CreateHook(bindTarget,reinterpret_cast<void*>(&BindPaletteHook),reinterpret_cast<void**>(&originalBind))!=MH_OK||
       MH_QueueEnableHook(target)!=MH_OK||MH_QueueEnableHook(bindTarget)!=MH_OK||MH_ApplyQueued()!=MH_OK) {
        MH_DisableHook(target);MH_DisableHook(bindTarget);MH_RemoveHook(target);MH_RemoveHook(bindTarget);
        if(log)log("Native K&L hands: GPU palette hook installation failed.");return false;
    }
    installed=true;
    if(NativeShadowPassKL::Verify(image,ReadBytes)) {
        auto* draw=reinterpret_cast<void*>(image+NativeShadowPassKL::DrawRva);
        const bool created=MH_CreateHook(draw,reinterpret_cast<void*>(&TechniqueDrawHook),reinterpret_cast<void**>(&originalTechniqueDraw))==MH_OK;
        if(created) {
            if(MH_QueueEnableHook(draw)==MH_OK&&MH_ApplyQueued()==MH_OK)shadowPassInstalled=true;
            else {MH_DisableHook(draw);MH_RemoveHook(draw);}
        }
    }
    if(log&&!shadowPassInstalled)log("Native K&L shadow pass identity unavailable; existing visible head policy retained.");
    if(log)log("Native K&L hands: player-only GPU palette arm IK enabled; native gameplay bones remain unchanged.");
    return true;
}
namespace {
bool RenderPairKey(const CameraFrame& camera,const NativeActionKL::State& action,NativeHandsRenderPairKL::Key& output){
    if(!camera.rig.valid||!action.valid||action.player!=camera.rig.player)return false;
    std::uintptr_t player=0,rest=0;std::uint32_t count=0;
    if(!Player(camera.rig.model,player)||player!=camera.rig.player||!Read(camera.rig.model+4,rest)||rest!=camera.rig.rest||
       !Read(camera.rig.model+0x4A0,count)||count!=camera.rig.count)return false;
    NativeHandsRenderPairKL::Key key;auto& w=key.words;std::uint32_t units=0;std::memcpy(&units,&camera.units,4);
    w[0]=player;w[1]=camera.rig.model;w[2]=rest;w[3]=count;w[4]=camera.recenterAck;w[5]=units;
    w[6]=unsigned(camera.firstPerson)|(unsigned(camera.actorHeadOnly)<<1)|(unsigned(camera.nativeAction)<<2)|
        (unsigned(camera.nativeBodyAction)<<3)|(unsigned(VRBridge::TeamHudVisible())<<4)|(unsigned(VRBridge::WeaponMenuVisible())<<5);
    w[7]=action.movement;w[8]=action.action;w[9]=action.movementVtable;w[10]=action.actionVtable;
    w[11]=unsigned(action.movementKind);w[12]=unsigned(action.actionKind);w[13]=action.movementId;
    w[14]=unsigned(action.throwBack)|(unsigned(action.explosionCover)<<1);
    w[25]=supportEpoch.load();
    for(unsigned side=0;side<2;++side){
        const auto hand=player+(side?0x358:0x2D8);std::uintptr_t vt=0;NativeWeaponOwnership::HandState before,after;
        if(!Read(hand,vt)||vt!=image+0x5508B4||!NativeWeaponOwnership::ReadHandState(hand,ReadBytes,before)||
           before.enabled>1||before.attacher!=(side?31u:32u))return false;
        const unsigned at=15+side*5;w[at]=before.item;w[at+4]=std::uint64_t(before.enabled)|(std::uint64_t(before.attacher)<<8);
        if(before.item){
            std::uintptr_t root=0,owner=0,model=0;std::uint32_t reference=0;
            if(!Read(std::uintptr_t(before.item)+4,root)||!Read(root+0x60,owner)||owner!=before.item||
               !Read(std::uintptr_t(before.item)+0xC0,model)||!model||!Read(std::uintptr_t(before.item)+0x58,reference))return false;
            w[at+1]=root;w[at+2]=model;w[at+3]=reference;
        }
        if(!NativeWeaponOwnership::ReadHandState(hand,ReadBytes,after)||before.item!=after.item||before.enabled!=after.enabled||before.attacher!=after.attacher)return false;
    }
    const auto current=NativeActionKL::Current();std::uintptr_t afterPlayer=0;
    if(!Player(camera.rig.model,afterPlayer)||afterPlayer!=player||!current.valid||current.player!=action.player||
       current.movement!=action.movement||current.action!=action.action||current.movementKind!=action.movementKind||current.actionKind!=action.actionKind)return false;
    output=key;return true;
}
}
void ConfigureRenderContext(RenderContextProvider provider){renderContextProvider.store(provider);}
bool RenderPairCaptureReady(){return !renderContextProvider.load()||!renderReadinessFailed;}
void RetireRenderPairImage(){renderReadinessEpoch=0;renderReadinessFailed=false;}
void SetFrame(const CameraPose& trackedEye,const float rawHeadOrientation[4],const float rawHeadPosition[3],
              const float eyeOffset[3],float units,std::uint64_t pairId,bool firstPerson,std::uint32_t recenterAck,bool actorHeadOnly) {
    if(!installed.load()||!ValidatePose(trackedEye)||!rawHeadOrientation||!rawHeadPosition||!eyeOffset||
       !pairId||!std::isfinite(units)||units<=0||units>100000.f){ClearFrame();return;}
    Quat normalizedHead;
    if(!NormalizeQuaternion({rawHeadOrientation[0],rawHeadOrientation[1],rawHeadOrientation[2],rawHeadOrientation[3]},normalizedHead)||
       !Finite(Vec3{rawHeadPosition[0],rawHeadPosition[1],rawHeadPosition[2]})||
       !Finite(Vec3{eyeOffset[0],eyeOffset[1],eyeOffset[2]})){ClearFrame();return;}
    const DWORD now=GetTickCount();
    HandsIPC::Frame latest{};
    const bool handsAvailable=consumer.Open(GetCurrentProcessId())&&consumer.Read(latest,now);
    CameraFrame previous,camera;
    AcquireSRWLockShared(&frameLock);previous=publishedFrame;ReleaseSRWLockShared(&frameLock);
    camera.centre=trackedEye;
    camera.centre.position=Subtract(trackedEye.position,Scale(MapTracking(trackedEye.basis,{eyeOffset[0],eyeOffset[1],eyeOffset[2]}),units));
    camera.rawHeadOrientation[0]=normalizedHead.x;camera.rawHeadOrientation[1]=normalizedHead.y;
    camera.rawHeadOrientation[2]=normalizedHead.z;camera.rawHeadOrientation[3]=normalizedHead.w;
    std::memcpy(camera.rawHeadPosition,rawHeadPosition,sizeof(camera.rawHeadPosition));
    camera.units=units;camera.pairId=pairId;camera.tick=now;camera.valid=true;camera.firstPerson=firstPerson;
    camera.recenterAck=recenterAck;camera.actorHeadOnly=actorHeadOnly;
    const auto nativeAction=NativeActionKL::Current();
    // Only immutable topology is published here. The original animated pose
    // is recovered later from the palette of the actual owned GPU draw.
    std::uintptr_t data=0,player=0,model=0;
    if(Read(image+kGameData,data)&&Read(data+0xA2C,player)&&Read(player+0x70,model)&&Player(model,player)) {
        AcquireSRWLockShared(&rawRigLock);camera.rig=originalRig;ReleaseSRWLockShared(&rawRigLock);
        std::uintptr_t rest=0;std::uint32_t count=0;
        if(!camera.rig.valid||camera.rig.player!=player||camera.rig.model!=model||
           !Read(model+4,rest)||rest!=camera.rig.rest||!Read(model+0x4A0,count)||count!=camera.rig.count) {
            camera.rig={};
            CaptureRigMetadata(model,player,camera.rig);
        }
    }
    // ClearFrame closes draw authorization between eyes without discarding
    // this pair's tracking. Player/model and origin changes start a new sample.
    const bool samePair=previous.pairId==pairId&&previous.recenterAck==recenterAck&&
        previous.firstPerson==firstPerson&&previous.actorHeadOnly==actorHeadOnly&&previous.units==units&&
        previous.rig.player==camera.rig.player&&previous.rig.model==camera.rig.model&&
        HandsIPC::Fresh(now,previous.tick);
    camera.hands=HandsMath::PairedFrame(previous.hands,samePair,latest,handsAvailable,now,recenterAck);
    // Physical grip lifetime must use the newest input, even while both eyes
    // deliberately reuse one older pose. Missing input suspends coupling;
    // explicit input-context invalidation still retires the logical gesture.
    camera.input=handsAvailable&&HandsMath::ValidFrame(latest,now,recenterAck)?latest:HandsIPC::Frame{};
    const auto actionPolicy=NativeVisualPolicy::ForAction(nativeAction,camera.rig.player);
    camera.nativeAction=actionPolicy.nativeArms;
    // A native animation does not release the user's physical grip. Keep only
    // its logical latch while the SAME verified long gun remains in the hand;
    // normal IK is still disabled for the complete native action above.
    if(camera.actorHeadOnly)InvalidateSupportGrip();
    // Reload owns the original hand animation, not the accumulated visual
    // torso heading. Dropping both at entry made the body spring back beside
    // the first-person camera. Native wall-cover/body-constrained actions
    // still retain their authored root orientation.
    camera.nativeBodyAction=actionPolicy.nativeBody;
    if(camera.nativeBodyAction&&nativeActionPlayer!=camera.rig.player)NativeBodyKL::ResetFollow();
    nativeActionPlayer=camera.nativeBodyAction?camera.rig.player:0;
    if(const auto context=renderContextProvider.load()){
        NativeHandsRenderPairKL::Request request;request.context=context(request.epoch,request.eye);
        request.pair=pairId;request.now=now;request.recenter=recenterAck;request.latestAvailable=handsAvailable;request.latest=latest;
        request.identity=RenderPairKey(camera,nativeAction,request.key);
        // Only a new physical pair consumes live native axes. Repeated draws
        // must not let newer simulation input change its render-only stance.
        if(request.context&&request.identity&&(!renderPair.initialized||renderPair.pair!=pairId))
            request.neutralStanding=StationaryOrdinaryPlayer(camera.rig.player,nativeAction);
        if(request.context&&request.epoch!=renderReadinessEpoch){renderReadinessEpoch=request.epoch;renderReadinessFailed=false;}
        const auto result=NativeHandsRenderPairKL::Update(renderPair,request);
        if(NativeHandsRenderPairKL::Accepted(result)){
            camera.hands=renderPair.pose;camera.input=renderPair.input;camera.renderPairPose=true;camera.renderAdmissionTick=renderPair.admittedTick;
            camera.neutralStanding=renderPair.neutralStanding;camera.stanceMovement=nativeAction.movement;camera.stanceMovementId=nativeAction.movementId;camera.stanceMovementKind=nativeAction.movementKind;
            if(result==NativeHandsRenderPairKL::Result::Admitted)++renderPairAdmitted;
            else if(result==NativeHandsRenderPairKL::Result::SecondEye)++renderPairSecondEye;else ++renderPairReused;
        }else{camera.hands={};camera.input={};++renderPairRejected;if(result==NativeHandsRenderPairKL::Result::Expired)++renderPairExpired;
            if(renderPair.eyes)renderReadinessFailed=true;}
    }
    camera.generation=frameGeneration.fetch_add(1)+1;
    AcquireSRWLockExclusive(&frameLock);publishedFrame=camera;ReleaseSRWLockExclusive(&frameLock);
    frameThread.store(GetCurrentThreadId());
}
void SetPassengerMotionProvider(PassengerMotionProvider provider){passengerMotionProvider.store(provider,std::memory_order_release);}
void SetProjectionBindObserver(ProjectionBindObserver observer){projectionBindObserver.store(observer,std::memory_order_release);}
void ClearFrame(){RetirePaletteLease();AcquireSRWLockExclusive(&frameLock);publishedFrame.valid=false;
    publishedFrame.generation=frameGeneration.fetch_add(1)+1;ReleaseSRWLockExclusive(&frameLock);}
void InvalidateSupportGrip(){supportEpoch.fetch_add(1);}
bool SnapshotControllerAim(unsigned side,ControllerAim& output) {
    if(side>1||!installed||!VRBridge::Ready()||NativeCameraKL::CinematicActive())return false;
    CameraFrame camera;AcquireSRWLockShared(&frameLock);camera=publishedFrame;ReleaseSRWLockShared(&frameLock);
    const auto now=GetTickCount();
    const auto expectedPlayer=camera.rig.player;std::uintptr_t currentPlayer=0;
    if(!camera.firstPerson||camera.actorHeadOnly||!camera.rig.valid||now-camera.tick>250||
       !expectedPlayer||camera.generation!=frameGeneration||!Player(camera.rig.model,currentPlayer)||currentPlayer!=expectedPlayer)return false;
    thread_local HandsIPC::Consumer aimInput;HandsIPC::Frame input;
    if(!aimInput.Open(GetCurrentProcessId())||!aimInput.Read(input,now)||
       !HandsMath::ValidFrame(input,now,camera.recenterAck)||!HandsMath::ValidPose(input.hands[side]))return false;
    float rotation[9];Rotation(input.hands[side].orientation,rotation);
    CameraPose target;
    if(!NativeHandsMath::ControllerTarget(camera.centre,camera.centre.position,
        {camera.rawHeadOrientation[0],camera.rawHeadOrientation[1],camera.rawHeadOrientation[2],camera.rawHeadOrientation[3]},rotation,
        {input.hands[side].position[0],input.hands[side].position[1],input.hands[side].position[2]},
        {input.head.position[0],input.head.position[1],input.head.position[2]},side,camera.units,1.5f,target))return false;
    ControllerAim next;next.player=camera.rig.player;next.pairId=camera.pairId;next.recenterAck=camera.recenterAck;
    next.tick=input.tick;next.unitsPerMetre=camera.units;next.origin=target.position;next.direction=target.basis.right;next.mappingTick=camera.tick;
    // ClearFrame retires GPU draw authorization between every eye. Simulation
    // may retain this short-lived world mapping, never its draw permission.
    // A source/recenter/actor replacement during the read still rejects it.
    if(camera.generation!=frameGeneration||!Player(camera.rig.model,currentPlayer)||currentPlayer!=expectedPlayer)return false;
    output=next;return true;
}
bool DrawFrameActive(std::uint64_t& pairId,std::uint64_t* renderGeneration) {
    ProfileTiming::Scope profile(ProfileTiming::HandsSnapshot);
    AcquireSRWLockShared(&frameLock);
    const bool valid=publishedFrame.valid&&!publishedFrame.actorHeadOnly;
    const auto generation=publishedFrame.generation;
    const std::uint64_t activePair=publishedFrame.pairId;
    ReleaseSRWLockShared(&frameLock);
    if(!NativeSkinMath::DrawContextCurrent(valid,generation,frameGeneration.load(),VRBridge::Ready()))return false;
    pairId=activePair;if(renderGeneration)*renderGeneration=generation;return true;
}
void ObserveShaderConstants(const float* rows,unsigned count,unsigned startRegister) {
    // The pinned consumer gathers at most 0x300 scalar floats at B887B0,
    // then hands that data to its native shader-constant uploader. A single
    // cached-pointer comparison keeps unrelated shader calls inexpensive.
    if(!installed.load()||!rows||count<3||count>0x300/4)return;
    const auto lastUploaded=*reinterpret_cast<const volatile std::uintptr_t*>(image+kLastUploadedPalette);
    if(lastUploaded!=reinterpret_cast<std::uintptr_t>(gpuPalette.data())&&lastUploaded!=reinterpret_cast<std::uintptr_t>(gpuShadowPalette.data()))return;
    if(std::memcmp(rows,reinterpret_cast<const void*>(image+kPaletteUploadScratch),count*4*sizeof(float)))return;
    ++ownPaletteUploads;shaderRowsInspected.fetch_add(count);
    if(paletteLease.active&&paletteLease.staging==lastUploaded&&startRegister+count<=256) {
        boneUpload.start=startRegister;boneUpload.count=count;boneUpload.valid=true;
        std::memcpy(boneUpload.rows.data(),rows,count*16);++skinConstantUploads;
    }
    if(uploadedHead>=NativeRigMath::MaximumBones)return;
    // Gathering is a byte copy: match the exact three float4 head rows in
    // the actual D3D arguments, rather than counting a palette write attempt.
    for(unsigned row=0;row+3<=count;++row)
        if(!std::memcmp(rows+row*4,gpuPalette[uploadedHead].data(),12*sizeof(float))) {
            ++collapsedHeadUploads;break;
        }
}
bool GpuHeadAnchor(std::uintptr_t expectedPlayer,Vec3& output,float units,bool animatedHead,Vec3* animatedUp,Vec3* animatedForward,CameraPose* nativeRoot,CameraPose* stableHead,unsigned* rappelBodyPhase,Vec3* rappelTowardHead,StableHeadMode stableMode) {
    ProfileTiming::Scope profile(ProfileTiming::HeadAnchor);
    if(stableHead)*stableHead={{{},{},{}},{}};
    if(rappelBodyPhase)*rappelBodyPhase=0;
    if(rappelTowardHead)*rappelTowardHead={};
    if(!installed.load()||!expectedPlayer||!HandsIPC::Fresh(GetTickCount(),lastOwnedEntryTick.load()))return false;
    const std::uintptr_t entry=lastOwnedEntry.load();
    std::uintptr_t vtable=0,geometry=0,owner=0,model=0,player=0,cache=0,lookup=0,rest=0,parentsAt=0;
    std::uint32_t modelCount=0,paletteCount=0;
    if(!entry||!Read(entry,vtable)||vtable!=image+kBonesVtable||
       !Read(entry+0xF4,geometry)||!Read(geometry+0x60,owner)||owner!=expectedPlayer||
       !Read(owner+0x70,model)||model!=lastOwnedModel.load()||!Player(model,player)||player!=expectedPlayer||
       !Read(entry+0x108,cache)||!cache||!Read(entry+0x114,paletteCount)||
       !Read(model+0x4A0,modelCount)||paletteCount<2||paletteCount>modelCount||modelCount>NativeRigMath::MaximumBones||
       !Read(model+0xC,lookup)||!Read(model+4,rest)||!Read(model+0x10,parentsAt))return false;
    CameraPose root;
    if(!GpuRoot(entry,root))return false;
    CameraFrame::Rig metadata;
    if(!CaptureRigMetadata(model,expectedPlayer,metadata))return false;
    const unsigned head=metadata.lookup[6],attacher=metadata.lookup[64];
    if(!head||head>=paletteCount)return false;
    const bool eyeMarker=attacher<paletteCount&&attacher>head&&
        NativeRigMath::Descendant(attacher,head,metadata.parents.data());
    unsigned count=(eyeMarker?attacher:head)+1;
    const unsigned leftShoulder=metadata.lookup[16],rightShoulder=metadata.lookup[22];
    const bool torsoRequested=stableMode==StableHeadMode::Rappel&&animatedHead&&(stableHead||rappelBodyPhase||rappelTowardHead);
    const bool shoulderBounds=torsoRequested&&leftShoulder>0&&rightShoulder>0&&leftShoulder!=rightShoulder&&
        leftShoulder<paletteCount&&rightShoulder<paletteCount;
    if(shoulderBounds)count=std::max(count,std::max(leftShoulder,rightShoulder)+1);
    std::array<NativeSkinMath::BindLocal,NativeRigMath::MaximumBones> local;
    std::array<CameraPose,NativeRigMath::MaximumBones> bind;
    float palette[12];Bone headPose;
    if(!ReadBytes(rest,local.data(),count*sizeof(local[0]))||
       !ReadBytes(cache+0x90+static_cast<std::uintptr_t>(head)*sizeof(palette),palette,sizeof(palette))||
       !ResolveBindPose(local.data(),metadata.parents.data(),count,bind.data())||
       !NativeSkinMath::PoseFromPalette(bind[head],palette,headPose))return false;
    CargoPose cargo;const bool cargoSource=animatedHead&&stableMode==StableHeadMode::Carried&&
        CargoSource(expectedPlayer,model,modelCount,paletteCount,head,metadata.parents.data(),0xFFFFFFFFu,cargo);
    if(cargoSource)headPose={bind[head].basis,Subtract(bind[head].position,{0,cargo.sample.crouchMetres*units,0})};
    Vec3 eyeLocal;
    if(cargoSource){
        if(!NativePassengerPoseMath::Eye(bind.data(),count,head,eyeMarker?attacher:head,cargo.sample,units,eyeLocal)){
            cargoRejectStage=6;++cargoPoseRejected;return false;
        }
        ++cargoEyeApplied;
    }else if(!NativeSkinMath::AnatomicalEye(bind[head],bind[eyeMarker?attacher:head],headPose,units,eyeLocal))return false;
    if(!animatedHead){
      const auto action=NativeActionKL::Current();
      const unsigned pelvis=metadata.lookup[0];
      if(action.valid&&action.player==expectedPlayer&&!action.action&&
       (action.movementKind==NativeActionKL::Movement::Stand||action.movementKind==NativeActionKL::Movement::BaseMove)&&
       pelvis>0&&pelvis<head&&pelvis<paletteCount){
        float rows[12];Bone pelvisPose;
        if(ReadBytes(cache+0x90+static_cast<std::uintptr_t>(pelvis)*sizeof(rows),rows,sizeof(rows))&&
           NativeSkinMath::PoseFromPalette(bind[pelvis],rows,pelvisPose)) {
            // Retain the accepted ordinary eye baseline and all authored
            // moving/crouched heights. Only a proved stationary standing
            // native fire-stance pelvis offset is omitted, matching the GPU
            // body's neutral pelvis; X/Z and orientation are untouched.
            float height=bind[eyeMarker?attacher:head].position.y+pelvisPose.position.y-bind[pelvis].position.y;
            if(StationaryOrdinaryPlayer(expectedPlayer,action)) {
                height=bind[eyeMarker?attacher:head].position.y;++neutralStandingEyeFrames;
            }
            eyeLocal={0,height,0};
        }
      }
    }
    Vec3 nextUp,nextForward;
    if(animatedHead&&animatedUp&&!NativeSkinMath::AnimatedHeadUp(root,bind[head],headPose,nextUp))return false;
    if(animatedHead&&animatedForward&&!NativeSkinMath::AnimatedHeadForward(root,bind[head],headPose,nextForward))return false;
    Vec3 nextEye;
    if(!NativeSkinMath::EyeAnchor(root,eyeLocal,animatedHead,nextEye))return false;
    if(cargoSource){if(stableHead)*stableHead={root.basis,nextEye};}
    else if(animatedHead&&(stableHead||rappelBodyPhase||rappelTowardHead)) {
        const unsigned parent=metadata.parents[head];
        float rows[12];Bone parentPose;CameraPose selected;
        std::uintptr_t afterGeometry=0,afterOwner=0,afterModel=0,afterCache=0,afterPlayer=0;
        if(parent>0&&parent<head&&parent<paletteCount&&
           ReadBytes(cache+0x90+static_cast<std::uintptr_t>(parent)*sizeof(rows),rows,sizeof(rows))&&
           NativeSkinMath::PoseFromPalette(bind[parent],rows,parentPose)&&
           RappelLookMath::ParentHead(root,bind[parent],bind[head],bind[eyeMarker?attacher:head],parentPose,units,selected)) {
            const unsigned pelvis=metadata.lookup[0];unsigned phase=0;Bone pelvisPose;Vec3 towardHead{};
            if(stableMode==StableHeadMode::Rappel&&pelvis>0&&pelvis<parent&&pelvis<paletteCount&&
               NativeRigMath::Descendant(parent,pelvis,metadata.parents.data())&&
               ReadBytes(cache+0x90+static_cast<std::uintptr_t>(pelvis)*sizeof(rows),rows,sizeof(rows))&&
               NativeSkinMath::PoseFromPalette(bind[pelvis],rows,pelvisPose))
                phase=RappelLookMath::BodyPhase(root,pelvisPose,parentPose,units,&towardHead);
            bool torsoBasisProven=false;
            if(phase==2) {
                ++rappelTorsoCandidates;Bone leftPose,rightPose;Basis torsoBasis;
                const bool torso=shoulderBounds&&NativeRigMath::Descendant(leftShoulder,pelvis,metadata.parents.data())&&
                    NativeRigMath::Descendant(rightShoulder,pelvis,metadata.parents.data())&&
                    ReadBytes(cache+0x90+static_cast<std::uintptr_t>(leftShoulder)*sizeof(rows),rows,sizeof(rows))&&
                    NativeSkinMath::PoseFromPalette(bind[leftShoulder],rows,leftPose)&&
                    ReadBytes(cache+0x90+static_cast<std::uintptr_t>(rightShoulder)*sizeof(rows),rows,sizeof(rows))&&
                    NativeSkinMath::PoseFromPalette(bind[rightShoulder],rows,rightPose)&&
                    RappelLookMath::TorsoScreen(root,pelvisPose,parentPose,leftPose,rightPose,units,torsoBasis);
                if(torso){selected.basis=torsoBasis;torsoBasisProven=true;}
            }
            if(Read(entry+0xF4,afterGeometry)&&afterGeometry==geometry&&Read(afterGeometry+0x60,afterOwner)&&afterOwner==expectedPlayer&&
               Read(afterOwner+0x70,afterModel)&&afterModel==model&&Read(entry+0x108,afterCache)&&afterCache==cache&&
               Player(model,afterPlayer)&&afterPlayer==expectedPlayer) {
                if(stableHead)*stableHead=selected;
                if(rappelBodyPhase)*rappelBodyPhase=phase;
                if(rappelTowardHead)*rappelTowardHead=towardHead;
                if(phase==2){if(torsoBasisProven)++rappelTorsoApplied;else ++rappelTorsoRejected;}
            } else if(phase==2)++rappelTorsoRejected;
        }
    }
    // The queued skin entry can be replaced while its head/parent palettes
    // are being read. Optional parent failure must not turn an old actor or
    // old queue buffer into an apparently valid current-head fallback.
    std::uintptr_t afterGeometry=0,afterOwner=0,afterModel=0,afterCache=0,afterPlayer=0;
    CameraPose afterRoot;float afterHead[12];
    if(lastOwnedEntry.load()!=entry||lastOwnedModel.load()!=model||
       !Read(entry+0xF4,afterGeometry)||afterGeometry!=geometry||!Read(geometry+0x60,afterOwner)||afterOwner!=expectedPlayer||
       !Read(expectedPlayer+0x70,afterModel)||afterModel!=model||!Read(entry+0x108,afterCache)||afterCache!=cache||
       !Player(model,afterPlayer)||afterPlayer!=expectedPlayer||!GpuRoot(entry,afterRoot)||std::memcmp(&root,&afterRoot,sizeof(root))||
       !ReadBytes(cache+0x90+static_cast<std::uintptr_t>(head)*sizeof(afterHead),afterHead,sizeof(afterHead))||
       std::memcmp(palette,afterHead,sizeof(palette))) {
        if(stableHead)*stableHead={{{},{},{}},{}};
        if(rappelBodyPhase)*rappelBodyPhase=0;
        if(rappelTowardHead)*rappelTowardHead={};
        return false;
    }
    output=nextEye;
    if(animatedHead&&animatedUp)*animatedUp=nextUp;
    if(animatedHead&&animatedForward)*animatedForward=nextForward;
    if(nativeRoot)*nativeRoot=root;
    return true;
}
bool SnapshotVisual(VisualFrame& output) {
    AcquireSRWLockShared(&visualLock);const VisualFrame copy=visual;ReleaseSRWLockShared(&visualLock);
    const auto action=NativeActionKL::Current();
    if(!copy.player||!HandsIPC::Fresh(GetTickCount(),copy.tick)||
       (copy.renderPairPose&&!HandsIPC::Fresh(GetTickCount(),copy.sourcePoseTick))||!VRBridge::Ready()||NativeCameraKL::CinematicActive()||
       !action.WeaponTrackingAllowed()||(action.valid&&action.player!=copy.player))return false;
    output=copy;return true;
}
bool SnapshotReloadControllers(std::uintptr_t player,std::uint32_t recenter,VisualFrame& output) {
    CameraFrame camera;
    AcquireSRWLockShared(&frameLock);camera=publishedFrame;ReleaseSRWLockShared(&frameLock);
    const auto now=GetTickCount();
    if(!player||camera.rig.player!=player||!camera.firstPerson||camera.actorHeadOnly||camera.nativeAction||
       camera.recenterAck!=recenter||!HandsIPC::Fresh(now,camera.tick))return false;
    thread_local HandsIPC::Consumer input;
    HandsIPC::Frame sample;
    if(!input.Open(GetCurrentProcessId())||!input.Read(sample,now)||!HandsMath::ValidFrame(sample,now,camera.recenterAck)||
       !(sample.inputValid[0]&HandsIPC::kGripInput))return false;

    VisualFrame next;
    next.player=player;next.centre=camera.centre;next.pairId=camera.pairId;next.tick=now;
    next.sourcePoseTick=sample.tick;next.recenterAck=camera.recenterAck;next.unitsPerMetre=camera.units;
    const auto* head=sample.head.position;
    const auto* orientation=camera.rawHeadOrientation;
    for(unsigned side=0;side<2;++side){
        const auto& hand=sample.hands[side];
        if(!HandsMath::ValidPose(hand))return false;
        float rotation[9];Rotation(hand.orientation,rotation);
        if(!NativeHandsMath::ControllerTarget(camera.centre,camera.centre.position,
            {orientation[0],orientation[1],orientation[2],orientation[3]},rotation,
            {hand.position[0],hand.position[1],hand.position[2]},{head[0],head[1],head[2]},
            side,camera.units,1.5f,next.controllerWrist[side]))return false;
        next.finalWrist[side]=next.controllerWrist[side];
        next.tracked[side]=true;next.inputValid[side]=sample.inputValid[side];next.grip[side]=sample.grip[side];
    }
    output=next;return true;
}
bool SnapshotDrawVisual(VisualFrame& output) {
    CameraFrame camera;std::uintptr_t player=0;
    if(!FrameSnapshot(camera)||frameThread.load()!=GetCurrentThreadId()||!camera.rig.valid||camera.actorHeadOnly) {
        ++lazyVisualRejected;return false;
    }
    VisualFrame cached;
    AcquireSRWLockShared(&visualLock);
    const bool hit=visualGeneration==camera.generation&&visual.player==camera.rig.player&&
        visual.pairId==camera.pairId;
    if(hit)cached=visual;
    ReleaseSRWLockShared(&visualLock);
    if(hit) {
        if(!CurrentGeneration(camera)) {++lazyVisualRejected;return false;}
        output=cached;++lazyVisualCacheHits;return true;
    }
    if(!Player(camera.rig.model,player)||player!=camera.rig.player){++lazyVisualRejected;return false;}
    const auto entry=lastOwnedEntry.load();
    // This timestamp describes the last body DRAW, not controller input.
    // A slow frame may draw its firearm before that body for >100ms; rejecting
    // only this early material left native color/depth and tracked later mask
    // silhouettes in the same eye. Revalidate the current native entry below
    // rather than applying the controller-sample timeout to metadata lifetime.
    if(lastOwnedModel.load()!=camera.rig.model) {
        ++lazyVisualRejected;return false;
    }
    PreparedDraw draw;
    if(!PrepareDraw(camera,entry,draw)) {++lazyVisualRejected;return false;}
    // A retained address is never authority by itself. The actor/model/rest,
    // palette/cache/count and current queued root must still match staging
    // after preparation; a changed source cannot borrow the old wrist solve.
    std::uintptr_t currentCache=0;unsigned currentCount=0;CameraPose currentRoot;
    if(lastOwnedEntry.load()!=entry||lastOwnedModel.load()!=camera.rig.model||
       !OwnedEntry(camera,entry,currentCache,currentCount)||currentCache!=draw.cache||currentCount!=draw.count||
       !GpuRoot(entry,currentRoot)||std::memcmp(&currentRoot,&draw.rawRoot,sizeof(currentRoot))||!CurrentGeneration(camera)) {
        ++lazyVisualRejected;return false;
    }
    // No TLS palette, renderer state, native source, uploader cache or twist
    // history is changed here. The body binder can publish this exact staging.
    AcquireSRWLockShared(&frameLock);
    if(!publishedFrame.valid||publishedFrame.generation!=camera.generation) {
        ReleaseSRWLockShared(&frameLock);++lazyVisualRejected;return false;
    }
    AcquireSRWLockExclusive(&visualLock);visual=draw.visual;visualGeneration=camera.generation;ReleaseSRWLockExclusive(&visualLock);
    ReleaseSRWLockShared(&frameLock);
    output=draw.visual;++lazyVisualPrepared;return true;
}
bool SnapshotDrawController(std::uintptr_t player,std::uint64_t pair,unsigned side,DrawController& output) {
    CameraFrame camera;
    if(side>1||!player||!pair||!FrameSnapshot(camera)||frameThread.load()!=GetCurrentThreadId()||
       !camera.firstPerson||camera.actorHeadOnly||camera.nativeAction||!camera.rig.valid||
       camera.rig.player!=player||camera.pairId!=pair||!CurrentGeneration(camera))return false;
    DrawController next;next.centre=camera.centre;next.recenter=camera.recenterAck;next.units=camera.units;
    const auto& hand=camera.hands.hands[side];
    const auto now=camera.renderPairPose?camera.renderAdmissionTick:GetTickCount();
    if(HandsMath::ValidFrame(camera.hands,now,camera.recenterAck)&&HandsMath::ValidPose(hand)){
        float rotation[9];Rotation(hand.orientation,rotation);
        next.tracked=NativeHandsMath::ControllerTarget(camera.centre,camera.centre.position,
            {camera.rawHeadOrientation[0],camera.rawHeadOrientation[1],camera.rawHeadOrientation[2],camera.rawHeadOrientation[3]},rotation,
            {hand.position[0],hand.position[1],hand.position[2]},
            {camera.hands.head.position[0],camera.hands.head.position[1],camera.hands.head.position[2]},side,camera.units,1.5f,next.wrist);
    }
    if(side==1&&supportSeenEpoch==supportEpoch.load()&&supportState.held&&supportState.pairHeld&&
       supportState.pairValid&&supportState.pair==pair&&supportState.key.player==player&&
       supportState.key.bodyModel==camera.rig.model&&supportState.key.recenter==camera.recenterAck) {
        const auto& key=supportState.key;
        next.coupledWrist=supportState.pairRight;
        next.coupledItem=key.weapon;next.coupledRoot=key.root;next.coupledModel=key.model;next.coupledReference=key.reference;
    }
    output=next;return true;
}
bool FrameBodyRoot(std::uintptr_t expectedPlayer,std::uint64_t pairId,CameraPose& nativeRoot,CameraPose& renderedRoot) {
    CameraFrame camera;
    if(!expectedPlayer||!pairId||!FrameSnapshot(camera)||
       frameThread.load()!=GetCurrentThreadId()||!camera.rig.valid||camera.actorHeadOnly||
       camera.rig.player!=expectedPlayer||camera.pairId!=pairId)return false;
    const auto entry=lastOwnedEntry.load();
    if(lastOwnedModel.load()!=camera.rig.model||!HandsIPC::Fresh(GetTickCount(),lastOwnedEntryTick.load()))return false;
    PreparedDraw draw;
    if(!PrepareDraw(camera,entry,draw))return false;
    // Unlike SnapshotDrawVisual's fast wrist path, this also proves that the
    // current primary source/root still matches AFTER staging. An unrelated
    // instance or native root update must never borrow a cached body delta.
    CameraPose afterRoot;std::uintptr_t cache=0;unsigned count=0;
    if(!OwnedEntry(camera,entry,cache,count)||cache!=draw.cache||count!=draw.count||
       !GpuRoot(entry,afterRoot)||std::memcmp(&afterRoot,&draw.rawRoot,sizeof(afterRoot))||!CurrentGeneration(camera))return false;
    nativeRoot=draw.rawRoot;renderedRoot=draw.renderRoot;return true;
}
bool SnapshotArmDiagnostic(unsigned side,ArmDiagnostic& output) {
    if(side>=2)return false;
    AcquireSRWLockShared(&diagnosticLock);const auto copy=lastDiagnostic[side];ReleaseSRWLockShared(&diagnosticLock);
    if(!copy.valid)return false;
    output=copy;return true;
}
Stats Snapshot() {
    Stats result{};result.installed=installed.load();result.calls=calls.load();result.playerCalls=playerCalls.load();
    result.stableTorsoFrames=stableTorsoFrames;
    result.neutralStandingFrames=neutralStandingFrames;
    result.neutralStandingEyeFrames=neutralStandingEyeFrames;
    result.heldGrenadeArmFrames=heldGrenadeArmFrames;
    result.renderPairAdmitted=renderPairAdmitted;result.renderPairReused=renderPairReused;result.renderPairSecondEye=renderPairSecondEye;
    result.renderPairRejected=renderPairRejected;result.renderPairExpired=renderPairExpired;
    result.bonesLightCacheInvalidated=bonesLightCacheInvalidated;
    result.passengerHeadsHidden=passengerHeadsHidden;
    result.cargoPoseCandidates=cargoPoseCandidates;result.cargoPoseApplied=cargoPoseApplied;result.cargoEyeApplied=cargoEyeApplied;
    result.cargoPoseRejected=cargoPoseRejected;result.cargoRejectStage=cargoRejectStage;
    result.cargoPelvis=cargoPelvis;result.cargoHead=cargoHead;result.cargoLeftFoot=cargoLeftFoot;result.cargoRightFoot=cargoRightFoot;
    result.shadowPassInstalled=shadowPassInstalled;result.shadowPassCalls=shadowPassCalls;
    result.shadowPassScopes=shadowPassScopes;result.shadowHeadPalettes=shadowHeadPalettes;
    for(unsigned side=0;side<2;++side) {
        result.passengerHandsPinned[side]=passengerHandsPinned[side];
        result.passengerNativeReachCorrected[side]=passengerNativeReachCorrected[side];
        result.passengerNativeReachErrorMetres[side]=passengerNativeReachErrorMetres[side];
    }
    for(unsigned side=0;side<2;++side){result.applied[side]=applied[side].load();result.clamped[side]=clamped[side].load();
        result.requestedDistanceMetres[side]=requestedDistance[side].load();result.armReachMetres[side]=armReach[side].load();
        result.targetErrorMetres[side]=targetError[side].load();
        result.helperApplied[side]=helperApplied[side].load();result.residualTwistDegrees[side]=residualTwistDegrees[side].load();}
    result.rejected=rejected.load();result.player=lastPlayer.load();result.model=lastModel.load();result.ageMs=lastAge.load();
    result.insideRenderCalls=insideRenderCalls.load();result.outsideRenderCalls=outsideRenderCalls.load();
    result.gpuPalettes=gpuPalettes.load();result.gpuRejected=gpuRejected.load();result.headHidden=headHidden.load();
    result.paletteLeaseRestored=paletteLeaseRestored;result.paletteLeaseReplaced=paletteLeaseReplaced;result.paletteLeaseRejected=paletteLeaseRejected;
    result.paletteLeaseResumed=paletteLeaseResumed;
    result.paletteLeaseFallback=paletteLeaseFallback;
    result.skinDrawSamples=skinDrawSamples;result.skinDrawRejected=skinDrawRejected;result.skinConstantUploads=skinConstantUploads;result.skinConstantMismatches=skinConstantMismatches;
    result.gpuUploadInvalidated=gpuUploadInvalidated.load();result.gpuCacheRejected=gpuCacheRejected.load();
    result.ownPaletteUploads=ownPaletteUploads.load();result.collapsedHeadUploads=collapsedHeadUploads.load();
    result.shaderRowsInspected=shaderRowsInspected.load();
    result.bindPoseCacheHits=bindPoseCacheHits.load();result.bindPoseCacheMisses=bindPoseCacheMisses.load();
    result.gpuBinderCalls=gpuBinderCalls.load();result.gpuPlayerBinds=gpuPlayerBinds.load();result.gpuNoContext=gpuNoContext.load();
    result.otherViewPreserved=otherViewPreserved;
    result.alternatePaletteProof=alternatePaletteProof;
    result.alternatePaletteCandidates=alternatePaletteCandidates;
    result.alternatePaletteApplied=alternatePaletteApplied;result.paletteSourceRejected=paletteSourceRejected;
    for(unsigned axis=0;axis<3;++axis)result.playerHeadOffsetMetres[axis]=playerHeadOffset[axis];
    result.playerHeadDistanceMetres=playerHeadDistance;
    result.lazyVisualPrepared=lazyVisualPrepared.load();result.lazyVisualCacheHits=lazyVisualCacheHits.load();
    result.lazyVisualRejected=lazyVisualRejected.load();
    result.rappelTorsoCandidates=rappelTorsoCandidates;result.rappelTorsoApplied=rappelTorsoApplied;result.rappelTorsoRejected=rappelTorsoRejected;
    result.frameThread=frameThread.load();result.bindThread=bindThread.load();
    result.supportCandidates=supportCandidates;result.supportHeld=supportHeld;result.supportReachRejected=supportReachRejected;
    result.supportReachAdjusted=supportReachAdjusted;result.supportAcquired=supportAcquired;result.supportReleased=supportReleased;
    result.supportAimRecovered=supportAimRecovered;result.supportAimRejected=supportAimRejected;result.supportSuspended=supportSuspended;
    result.supportReleaseInput=supportReleaseInput;result.supportReleaseContext=supportReleaseContext;
    result.supportReleaseIdentity=supportReleaseIdentity;result.supportReleaseInvalidInput=supportReleaseInvalidInput;result.supportReleaseEpoch=supportReleaseEpoch;
    result.supportGripSampleValid=supportGripSampleValid;result.supportRawGrip=supportRawGrip;result.supportReleaseGrip=supportReleaseGrip;
    result.supportGripSampleTick=supportGripSampleTick;result.supportReleaseSampleTick=supportReleaseSampleTick;
    result.supportReleaseLowMs=supportReleaseLowMs;result.supportReleaseSamples=supportReleaseSamples;
    return result;
}
} // namespace NativeHandsKL
