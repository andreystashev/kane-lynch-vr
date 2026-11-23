#include "background_diagnostic.hpp"
// KaneLynchVR.dll: in-game capture side of the Kane & Lynch: Dead Men VR
// output. Native CameraSpace/projection hooks steer the camera and culling
// before shader uploads. A guarded D3D9 shader/fixed-function path remains
// available when native hooks are disabled. Both paths publish alternating
// eye pairs with one tracking sample for the x64 LibOVR host.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include "MinHook.h"
#include "d3d9_capture.hpp"
#include "native_visual_trace_kl.hpp"
#include "native_effects_visual_source_kl.hpp"
#include "vr_bridge.hpp"
#include "vr_ipc.hpp"
#include "native_camera_math.hpp"
#include "native_camera_kl_math.hpp"
#include "native_camera_kl.hpp"
#include "native_reflection_camera_json.hpp"
#include "native_reflection_diagnostics_json.hpp"
#include "native_hands_kl.hpp"
#include "native_skin_lighting_kl.hpp"
#include "native_mirror_bump_kl.hpp"
#include "native_laser_pose_kl.hpp"
#include "native_carried_person_visibility_kl.hpp"
#include "native_draw_scopes.hpp"
#include "native_body_kl.hpp"
#include "native_action_kl.hpp"
#include "native_cover_kl.hpp"
#include "native_cinematic_kl.hpp"
#include "rappel_look_math.hpp"
#include "native_carried_camera_math.hpp"
#include "native_vehicle_camera_math.hpp"
#include "native_passenger_entry_kl.hpp"
#include "native_passenger_actor_kl.hpp"
#include "native_passenger_collision_kl.hpp"
#include "native_tutorial_kl.hpp"
#include "native_ledge_hint_kl.hpp"
#include "native_objective_visibility_kl.hpp"
#include "native_loading_lifecycle.hpp"
#include "loading_memory_headroom.hpp"
#include "native_movie_kl.hpp"
#include "native_weapon_kl.hpp"
#include "native_tracer_visual_kl.hpp"
#include "native_aim_kl.hpp"
#include "native_aim_target_policy.hpp"
#include "native_postprocess_kl.hpp"
#include "native_postfilter_probe_kl.hpp"
#include "native_reflection_probe_kl.hpp"
#include "native_hud_kl.hpp"
#include "native_hud_math.hpp"
#include "native_gameplay_hud_kl.hpp"
#include "native_team_hud_kl.hpp"
#include "native_team_commands_kl.hpp"
#include "native_rope_kl.hpp"
#include "native_grenade_visual_kl.hpp"
#include "native_mounted_aim_kl.hpp"
#include "native_invulnerability_kl.hpp"
#include "native_vr_menu_kl.hpp"
#include "native_frame_wait_kl.hpp"
#include "native_hid_refresh_kl.hpp"
#include "release_version.hpp"
#include "native_buffer_flush_kl.hpp"
#include "cpu_timer.hpp"
#include "native_effects_kl.hpp"
#include "native_pip_render_kl.hpp"
#include "native_context_hud_kl.hpp"
#include "native_player_anchor.hpp"
#include "stereo_render_math.hpp"
#include "stereo_pair_policy.hpp"
#include "capture_frame_policy.hpp"
#include "mono_frame_policy.hpp"
#include "hands_renderer.hpp"
#include "performance_log.hpp"
#include "profile_timing.hpp"
#include "crash_diagnostics.hpp"
#include "diagnostics_launch.hpp"
#include "sdk_own_cache_12.hpp"
#include "launcher_native_startup.hpp"
#include "d3d9_gpu_timing.hpp"
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>

namespace {
wchar_t folder[MAX_PATH]{};
FILE* logFile=nullptr;
std::atomic<bool> diagnosticLogging{false};
PerformanceLog::Recorder performanceRecorder;
std::uint64_t frameTraceAfter=0;
std::uint64_t waitTraceAfter=0;
std::uint64_t bufferFlushTraceAfter=0;
std::uint64_t objectiveTraceAfter=0,postfilterTraceAfter=0,reflectionTraceAfter=0;
IDirect3DDevice9* bufferFlushDevice=nullptr; // observed only on the render/reset path; no ownership
bool monitorLaunchSucceeded=false;
std::wstring monitorLaunchError;
enum class SceneGate {Ready,Panel,StereoOff,HostMissing,EyeConfigMissing,PlayerAnchorMissing,TrackingInvalid,Unmounted,
    TrackingStale,RecenterMismatch,PoseInvalid,CinematicCameraMismatch,FrameNotTracked,NativeViewMissing,EyeInvalid,PairMismatch,ProjectionMissing,Count};
const char* const sceneGateNames[]={"ready","native_panel_or_menu","stereo_disabled","host_not_ready","eye_config_missing","player_anchor_missing",
    "tracking_invalid","hmd_unmounted","tracking_stale","recenter_mismatch","pose_math_invalid","cinematic_camera_mismatch",
    "frame_not_tracked","native_view_not_applied","eye_invalid","pair_mismatch","projection_missing"};
std::atomic<std::uint64_t> sceneGates[static_cast<unsigned>(SceneGate::Count)]{};
std::atomic<unsigned> lastSceneGate{0};
CaptureFramePolicy::SceneIntent captureSceneIntent; // presentation-thread lifecycle only
CaptureFramePolicy::ReflectionContinuity reflectionContinuity;
bool reflectionHistoryEnabled=false;
std::atomic<std::uint64_t> reflectionHistoryKeptAfterMiss{0};
void SuspendPassengerNavigation();
bool SceneDecision(SceneGate gate){const auto index=static_cast<unsigned>(gate);++sceneGates[index];lastSceneGate=index;
    if(gate!=SceneGate::Ready){SuspendPassengerNavigation();NativeTutorialKL::SetGameplayContext(false);NativeLedgeHintKL::SetGameplayContext(false);NativeObjectiveVisibilityKL::SetContext(false,false);}
    return gate==SceneGate::Ready;}
std::atomic<bool> workerRunning{false};
std::atomic<DWORD> gamePid{0};
struct Settings {
    unsigned autoStart:1;
    unsigned stereoMode:1;
    unsigned nativeCamera:1;
    unsigned firstPerson:1;
    unsigned nativeArms:1;
    unsigned weaponAim:1;
    unsigned captureWithoutHost:1;
    unsigned stereoHud:1;
    unsigned cameraForwardMm,nearPlaneMm,maxLeanMm,playerEyeHeightMm;
    unsigned panelWidthMm,panelDistanceMm;
    unsigned renderFovPercent;
    float worldUnitsPerMetre;
    bool readOnlyShaderConstants=false;
    unsigned hudDistanceMm=2000,hudScalePercent=80;
    bool stableFirstPersonYaw=true;
    bool immersiveHud=true;
    std::atomic<bool> gameHints{true};
    bool hideAuxiliaryPip=true;
    bool supportGrip=true;
    std::atomic<bool> nativeReticle{true},mountedReticle{true},laserSight{false},cinematicVr{false};
    std::atomic<unsigned> reticleStyle{1};
    bool hideLocalRopes=false,on12QueuePacing=true;
    bool nativeStereoViews=false;
} settings{1,1,1,1,0,1,0,1,1500,50,350,1700,3600,2000,118,100.f};
std::atomic<bool> playerAnchorValid{false},playerAnchorHeadBone{false};
std::atomic<bool> actorCinematicApplied{false},movieVerified{false},movieActive{false};
std::atomic<unsigned> movieWidth{0},movieHeight{0};
std::atomic<bool> ammoProof{false},contextProof{false};
NativeHandHudKL::Frame handHudCamera;
std::atomic<unsigned> visualBenchMask{7};
std::atomic<std::uint64_t> renderEpoch{0};
struct PostfilterFrameEvidence {NativePostfilterProbeKL::Frame frame{};std::uint64_t epoch=UINT64_MAX;};
thread_local PostfilterFrameEvidence postfilterFrameEvidence;
bool ProvidePostfilterFrame(NativePostfilterProbeKL::Frame& output){
    const auto evidence=postfilterFrameEvidence;
    if(evidence.epoch!=renderEpoch.load(std::memory_order_acquire)||evidence.frame.eye>1||!evidence.frame.tick||
       GetTickCount()-evidence.frame.tick>250)return false;
    output=evidence.frame;return true;
}
std::atomic<bool> nativeWeaponReady{false};
std::atomic<bool> nativeSimulationReady{false};
std::atomic<bool> materialSequenceEnabled{false};
NativeActionKL::State SceneAction(bool refresh=false) {
    static thread_local std::uint64_t epoch=UINT64_MAX;
    static thread_local NativeActionKL::State action;
    const auto current=renderEpoch.load();
    if(refresh||epoch!=current||!action.valid){epoch=current;action=NativeActionKL::Current();}
    return action;
}
NativeScopeKL::State SceneScope(bool refresh=false) {
    static thread_local std::uint64_t epoch=UINT64_MAX;
    static thread_local NativeScopeKL::State scope;
    const auto current=renderEpoch.load();
    if(refresh||epoch!=current||!scope.valid){epoch=current;scope=NativeWeaponKL::ReadScope();}
    // Tracked scope pixels and scoped native Fire must be enabled together.
    // A disabled/unavailable VR weapon module retains its old native path.
    if(!nativeWeaponReady.load())scope.active=false;
    return scope;
}
bool ReadNativeBytes(std::uintptr_t address,void* output,std::size_t size){
    SIZE_T got=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),output,size,&got)&&got==size;
}
bool MovieActive(bool force=false){
    if(!movieVerified.load())return false;
    static thread_local std::uint64_t epoch=UINT64_MAX;
    static thread_local NativeMovieKL::State movie;
    const auto current=renderEpoch.load();
    if(force||epoch!=current){epoch=current;movie=NativeMovieKL::Snapshot(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)),ReadNativeBytes);
        movieActive=movie.valid&&movie.active;movieWidth=movie.width;movieHeight=movie.height;}
    return movie.valid&&movie.active;
}

void Log(const char* text) {
    if(logFile){std::fprintf(logFile,"[%s pid=%lu tick=%lu] %s\n",DiagnosticsSession::Utc().c_str(),GetCurrentProcessId(),GetTickCount(),text);std::fflush(logFile);}
}

void ApplyVRModifier(Kl1Modifiers::Id id,bool enabled) {
    using Kl1Modifiers::Id;
    switch(id) {
    case Id::Cinematics: settings.cinematicVr=enabled;NativeCameraKL::SetCinematicVR(enabled);break;
    case Id::Reload: NativeWeaponKL::SetPhysicalReloadEnabled(enabled);break;
    case Id::Reticle: settings.nativeReticle=enabled;break;
    case Id::MountedReticle: settings.mountedReticle=enabled;break;
    case Id::ReticleStyle: settings.reticleStyle=enabled;NativeGameplayHudKL::SetReticleStyle(enabled);break;
    case Id::MountedMouseAim: NativeMountedAimKL::SetMouseAim(enabled);break;
    case Id::Laser: settings.laserSight=enabled;break;
    case Id::Hints: settings.gameHints=enabled;NativeGameplayHudKL::SetGameHintsEnabled(enabled);break;
    case Id::Invulnerable: NativeInvulnerabilityKL::SetEnabled(enabled);break;
    case Id::Count: return;
    }
    if(id==Id::Reticle||id==Id::MountedReticle)
        NativeHudKL::SetReticleEnabled(settings.nativeReticle||settings.mountedReticle);
    if(id==Id::Reticle||id==Id::Laser)
        NativeAimKL::SetEnabled(settings.nativeArms&&settings.weaponAim&&(settings.nativeReticle||settings.laserSight));
}

void LoadSettings() {
    const std::wstring path=std::wstring(folder)+L"vr.ini";
    settings.autoStart=GetPrivateProfileIntW(L"VR",L"AutoStart",1,path.c_str())!=0;
    settings.nativeStereoViews=GetPrivateProfileIntW(L"VR",L"NativeStereoViews",0,path.c_str())!=0;
    settings.stereoMode=GetPrivateProfileIntW(L"VR",L"StereoMode",1,path.c_str())!=0;
    settings.nativeCamera=GetPrivateProfileIntW(L"VR",L"NativeCamera",1,path.c_str())!=0;
    settings.firstPerson=GetPrivateProfileIntW(L"VR",L"FirstPerson",1,path.c_str())!=0;
    settings.hideLocalRopes=GetPrivateProfileIntW(L"VR",L"HideLocalRopes",0,path.c_str())!=0;
    settings.on12QueuePacing=GetPrivateProfileIntW(L"VR",L"On12QueuePacing",1,path.c_str())!=0;
    D3D9Capture::ConfigureOn12QueuePacing(settings.on12QueuePacing);
    NativeBufferFlushKL::ConfigureBatching(GetPrivateProfileIntW(L"VR",L"StaticBufferBatching",
        GetPrivateProfileIntW(L"VR",L"On12StaticBufferBatching",1,path.c_str()),path.c_str())!=0);
    settings.stableFirstPersonYaw=GetPrivateProfileIntW(L"Controls",L"TurnMode",2,path.c_str())!=0;
    settings.nativeArms=GetPrivateProfileIntW(L"Hands",L"NativeArms",0,path.c_str())!=0;
    settings.readOnlyShaderConstants=GetPrivateProfileIntW(L"Diagnostics",L"ReadOnlyShaderConstants",0,path.c_str())!=0;
    settings.weaponAim=GetPrivateProfileIntW(L"Hands",L"WeaponAim",1,path.c_str())!=0;
    settings.supportGrip=GetPrivateProfileIntW(L"Weapons",L"SupportGrip",1,path.c_str())!=0;
    for(std::size_t i=0;i<Kl1Modifiers::Index(Kl1Modifiers::Id::Invulnerable);++i){
        const auto& option=Kl1Modifiers::Options[i];
        ApplyVRModifier(static_cast<Kl1Modifiers::Id>(i),GetPrivateProfileIntW(option.section,option.key,option.fallback,path.c_str())!=0);
    }
    settings.stereoHud=GetPrivateProfileIntW(L"HUD",L"Stereo",1,path.c_str())!=0;
    settings.immersiveHud=GetPrivateProfileIntW(L"HUD",L"Immersive",1,path.c_str())!=0;
    settings.hideAuxiliaryPip=GetPrivateProfileIntW(L"HUD",L"HideAuxiliaryPip",1,path.c_str())!=0;
    settings.hudDistanceMm=GetPrivateProfileIntW(L"HUD",L"DistanceMm",2000,path.c_str());
    settings.hudScalePercent=GetPrivateProfileIntW(L"HUD",L"ScalePercent",80,path.c_str());
    if(settings.hudDistanceMm<500||settings.hudDistanceMm>10000)settings.hudDistanceMm=2000;
    if(settings.hudScalePercent<25||settings.hudScalePercent>95)settings.hudScalePercent=80;
    settings.playerEyeHeightMm=GetPrivateProfileIntW(L"VR",L"PlayerEyeHeightMm",1700,path.c_str());
    if(settings.playerEyeHeightMm<500||settings.playerEyeHeightMm>2500)settings.playerEyeHeightMm=1700;
    // Diagnostic only: capture frames without a headset host.
    settings.captureWithoutHost=GetPrivateProfileIntW(L"VR",L"CaptureWithoutHost",0,path.c_str())!=0;
    settings.cameraForwardMm=GetPrivateProfileIntW(L"VR",L"CameraForwardMm",1500,path.c_str());
    settings.nearPlaneMm=GetPrivateProfileIntW(L"VR",L"NearPlaneMm",50,path.c_str());
    settings.maxLeanMm=GetPrivateProfileIntW(L"VR",L"HeadPositionMm",350,path.c_str());
    settings.panelWidthMm=GetPrivateProfileIntW(L"VR",L"PanelWidthMm",3600,path.c_str());
    settings.panelDistanceMm=GetPrivateProfileIntW(L"VR",L"PanelDistanceMm",2000,path.c_str());
    settings.renderFovPercent=GetPrivateProfileIntW(L"VR",L"RenderFovPercent",118,path.c_str());
    if(settings.cameraForwardMm>4000)settings.cameraForwardMm=1500;
    if(settings.nearPlaneMm<10||settings.nearPlaneMm>1000)settings.nearPlaneMm=50;
    if((settings.maxLeanMm&&settings.maxLeanMm<100)||settings.maxLeanMm>1500)settings.maxLeanMm=350;
    if(settings.panelWidthMm<1500||settings.panelWidthMm>6000)settings.panelWidthMm=3600;
    if(settings.panelDistanceMm<750||settings.panelDistanceMm>4000)settings.panelDistanceMm=2000;
    if(settings.renderFovPercent<100||settings.renderFovPercent>200)settings.renderFovPercent=118;
    // Glacier world geometry is expected in centimetres like the Hitman
    // titles; the stereo delta is authored in metres. Confirm from the WORLD
    // matrix dump in the log once a mission loads.
    settings.worldUnitsPerMetre=static_cast<float>(GetPrivateProfileIntW(L"VR",L"WorldUnitsPerMetre",100,path.c_str()));
    if(settings.worldUnitsPerMetre<1||settings.worldUnitsPerMetre>1000)settings.worldUnitsPerMetre=100;
}

// ---- Row-major D3D matrix helpers (v' = v * M, translation in row 4) ----
using Mat4=float[4][4];

void MatMultiply(const Mat4 a,const Mat4 b,Mat4 out) {
    Mat4 result{};
    for(int r=0;r<4;++r)for(int c=0;c<4;++c)
        result[r][c]=a[r][0]*b[0][c]+a[r][1]*b[1][c]+a[r][2]*b[2][c]+a[r][3]*b[3][c];
    std::memcpy(out,result,sizeof(Mat4));
}
// ---- Stereo steering state (game render thread) ----
struct EyeSnapshot {
    bool tracked=false;
    bool nativeGpuApplied=false;
    float orientation[4]{},position[3]{},fov[4]{};
    std::uint32_t tick=0;
    bool projectionApplied=false;
    unsigned eye=0;
    std::uint64_t frameId=0;
    double sensorTime=0;
    long recenterAck=0;
    std::uintptr_t nativePlayer=0,nativeSourceCamera=0;
    bool actorCinematic=false;
    bool nativeCinematic=false;
    bool rappel=false;
    bool nativeMovementAxes=false;
    bool nativeCarried=false;
    bool nativeMounted=false;
    bool nativeScope=false;
    NativeCameraMath::Vec3 worldEye{};
};
VRBridge::Tracking activeSample{};   // late-latched before the first native camera of a pair
long activeRecenterAck=0;            // fixed together with both eyes' sample
EyeSnapshot frameSnapshot{};         // filled while transforms are modified
bool projectionAppliedThisFrame=false; // PROJECTION replaced after the gates
float eyeOffsetCache[2][3]{},eyeFovCache[2][4]{}; // host-published, per frame
bool eyeConfigValid=false;
bool lastProjectionPerspective=false;
bool projectionInfiniteFar=false;    // engine frustum is infinite-far
float currentProjectionFar=0.f;      // game units; 0 = unparsed
Mat4 rawProjection{};                // engine's own frustum before replacement
Mat4 vrProjection{};                 // frustum the PROJECTION hook installed
bool rawProjectionValid=false;
bool viewCarriesCamera=false;        // raw VIEW selects VIEW or WORLD, never both
unsigned backbufferWidth=0,backbufferHeight=0;
bool nativeMenuActive=true;          // render thread, refreshed at capture/boundary
NativeLoadingLifecycle::State nativeLoading;
LoadingMemoryHeadroom::Observer loadingMemoryHeadroom;
NativeAimTargetPolicy::PairTarget aimPairTarget;
NativeLaserPoseKL::PairBeam laserPairBeam;
// Yaw recenter reference (F7 / host ShouldRecenter): zeros physical heading
// while keeping absolute head pitch and roll, as in the Contracts camera.
NativeCameraMath::Quat referenceOrientation;
NativeCameraMath::Vec3 referencePosition;
StereoRenderMath::HeadPositionReference headPositionReference;
StereoRenderMath::CinematicLook cinematicLook;
StereoRenderMath::CinematicLook carriedLook;
NativeVehicleCamera::Scope vehicleCameraScope;
NativeVehicleCamera::Tracking vehicleCameraTracking;
NativeCarriedCamera::State carriedAttachment;
NativeCarriedCamera::EntryClearance passengerEntryClearance;
NativePassengerEntryKL::PairCache passengerEntryPair;
std::atomic<std::uint64_t> passengerEntryFrames{0},passengerEntryApplied{0};
std::atomic<unsigned> passengerEntryStage{0};
std::atomic<bool> passengerEntryKnown{false},passengerEntryBlocked{false},passengerEntryActive{false};
void SuspendPassengerNavigation(){
    NativePassengerActorKL::Suspend();
    VRBridge::SetPassengerReceiver({});
}
std::atomic<std::uint64_t> carriedFrames{0},carriedParentHeadFrames{0},carriedCurrentHeadFrames{0};
std::atomic<std::uint64_t> carriedMountedBaseFrames{0},carriedActorBaseFrames{0};
std::atomic<unsigned> carriedReferenceKind{0};
RappelLookMath::Look rappelLook;
RappelLookMath::Attachment rappelAttachment;
std::atomic<float> rappelYawDegrees{0.f},rappelPitchDegrees{0.f};
std::atomic<std::uint64_t> rappelLookFrames{0};
std::atomic<std::uint64_t> rappelAttachmentFrames{0};
std::atomic<std::uint64_t> rappelParentHeadFrames{0},rappelCurrentHeadFrames{0};
std::atomic<bool> rappelOrientationLocked{false},rappelAnatomicalUpLocked{false};
std::atomic<float> rappelEntryTiltDegrees{0.f};
std::atomic<unsigned> rappelBodyPhase{0};
std::atomic<float> rappelNativeHeadLocal[3]{},rappelStableHeadLocal[3]{};
std::atomic<std::uint64_t> scopeFrames{0},scopePublished{0};
bool referenceValid=false;
long referenceAck=0;
std::atomic<unsigned> currentEye{0};
StereoRenderMath::PairSequence pairSequence;
std::atomic<bool> publishedThisFrame{false};
std::atomic<std::uint64_t> viewModified{0},projectionModified{0},worldModified{0},
                           sceneFrames{0},panelFrames{0},incompleteFrames{0},
                           vsMatrixPatched{0},movementYawPublished{0};
std::atomic<std::uint64_t> shaderProjectionsRecovered{0};
std::array<std::atomic<std::uint64_t>,2> laserEyeAttempts{},laserEyeAimReady{},laserEyeContacts{},
    laserEyeDrawn{},laserEyeDrawRejected{},laserEyeMetadataRejected{};
std::array<std::atomic<std::uint64_t>,2> laserContextRejected{},laserCaptureRejected{},laserIdentityRejected{},laserBeamRejected{};
std::atomic<float> lastMovementYaw{0.f};
std::atomic<bool> nativeMovementYawValid{false};
std::atomic<DWORD> lastSceneTick{0};
std::atomic<std::uint64_t> incompleteReadbacksAvoided{0};
std::atomic<std::uint64_t> absentConsumerReadbacksAvoided{0};
std::atomic<bool> trackingValidForStatus{false};
std::atomic<unsigned> totalViews{0},identityViews{0},totalWorlds{0},rotatedWorlds{0},
                      totalProjections{0},identityProjections{0},totalConstants{0};

bool IsIdentityView(const float* m) {
    for(int r=0;r<4;++r)for(int c=0;c<4;++c) {
        const float expect=(r==c)?1.f:0.f;
        if(std::fabs(m[r*4+c]-expect)>1e-4f)return false;
    }
    return true;
}

bool UsableTangents(const float* fov) {
    for(int i=0;i<4;++i)if(!std::isfinite(fov[i])||fov[i]<=0.05f||fov[i]>10.0f)return false;
    return true;
}

bool ReadNativeMenu(CaptureFramePolicy::ReflectionWorld* world=nullptr) {
    // ZGameplayInput::Update (RVA 0x237C10) queries controller pause bytes
    // through RVA 0x2740D0 and skips gameplay input when either is 1. A live
    // mission -> Esc/PAUSE check confirmed byte +0x14 changing 0 -> 1.
    const auto image=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    unsigned char paused[2]{};SIZE_T got=0;
    if(ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(image+0x8883A4),
                         paused,sizeof(paused),&got)&&got==sizeof(paused)&&
       (paused[0]==1||paused[1]==1)){nativeLoading.ObserveMenu(true);return true;}
    std::uint32_t data=0,player=0;
    // ZGameData::GetPlayer(0), RVA 0x26FBD0, reads the array at +0xA2C.
    // A valid GameData without a player is the title/menu/loading context.
    const bool loaded=ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(image+0x68760C),
                             &data,sizeof(data),&got)&&got==sizeof(data)&&data&&
        ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(data+0xA2C),
                          &player,sizeof(player),&got)&&got==sizeof(player);
    if(world&&loaded){world->data=data;world->player=player;}
    const bool menu=loaded&&!player;
    nativeLoading.ObserveMenu(menu);return menu;
}

void RefreshPairTracking() {
    if(VRBridge::RecenterAck()!=activeRecenterAck) {aimPairTarget.Reset();laserPairBeam.Reset();NativeGrenadeVisualKL::ClearFrame();NativeWeaponKL::ClearScopedDrawFrame();NativeWeaponKL::InvalidateHeadGaze();NativeTeamCommandsKL::InvalidateHeadRay();NativeWeaponKL::InvalidateScopeGesture();}
    activeRecenterAck=VRBridge::RecenterAck();
    activeSample=VRBridge::HeadPose();
    float offsets[2][3]{},fov[2][4]{};
    eyeConfigValid=VRBridge::EyeConfig(0,offsets[0],fov[0])&&
                   VRBridge::EyeConfig(1,offsets[1],fov[1]);
    if(eyeConfigValid) {
        std::memcpy(eyeOffsetCache,offsets,sizeof(offsets));
        std::memcpy(eyeFovCache,fov,sizeof(fov));
    }
}

// The camera-space delta applied between the engine's camera and the rendered
// eye, in the Contracts-camera model: relative (yaw-referenced) head rotation,
// reference-frame lean clamped to MaxLean, SDK per-eye offset rotated by the
// relative head, plus the forward dolly toward the player's head. Returns
// false when tracking is unsafe.
bool BuildCameraDelta(unsigned eye,Mat4 out,float dollyMetres=-1.f,
                      std::uintptr_t player=0,std::uintptr_t camera=0,bool cinematic=false,std::uintptr_t rappelMovement=0,bool nativeScope=false,bool nativeCarried=false,std::uintptr_t vehicleAction=0) {
    using namespace NativeCameraMath;
    if(activeSample.valid&&!activeSample.mounted) {
        referenceValid=false;headPositionReference.Reset();cinematicLook.Reset();carriedLook.Reset();rappelLook.Reset();vehicleCameraTracking.Reset();
    }
    if(eye>1)return SceneDecision(SceneGate::EyeInvalid);
    if(!activeSample.valid)return SceneDecision(SceneGate::TrackingInvalid);
    if(!activeSample.mounted)return SceneDecision(SceneGate::Unmounted);
    if(GetTickCount()-activeSample.tick>250)return SceneDecision(SceneGate::TrackingStale);
    if(VRBridge::RecenterAck()!=activeRecenterAck)return SceneDecision(SceneGate::RecenterMismatch);
    if(!UsableTangents(eyeFovCache[eye]))return SceneDecision(SceneGate::EyeConfigMissing);
    Quat head{activeSample.orientation[0],activeSample.orientation[1],
              activeSample.orientation[2],activeSample.orientation[3]};
    Vec3 position{activeSample.position[0],activeSample.position[1],activeSample.position[2]};
    const long ack=VRBridge::RecenterAck();
    if(!vehicleAction)vehicleCameraTracking.Reset();
    if(vehicleAction) {
        headPositionReference.Reset();cinematicLook.Reset();carriedLook.Reset();rappelLook.Reset();
        Quat relative;
        if(!vehicleCameraTracking.Select(head,player,camera,vehicleAction,ack,relative))return false;
        const Vec3 offset{eyeOffsetCache[eye][0],eyeOffsetCache[eye][1],eyeOffsetCache[eye][2]};
        return StereoRenderMath::CameraViewDelta(relative,{},offset,0.f,settings.worldUnitsPerMetre,out);
    }
    if(!nativeCarried)carriedLook.Reset();
    if(rappelMovement) {
        headPositionReference.Reset();cinematicLook.Reset();
        Quat bounded;
        if(!rappelLook.Select(head,player,camera,rappelMovement,ack,bounded))return false;
        rappelYawDegrees=rappelLook.lastYaw*57.2957795f;rappelPitchDegrees=rappelLook.lastPitch*57.2957795f;++rappelLookFrames;
        const Vec3 offset{eyeOffsetCache[eye][0],eyeOffsetCache[eye][1],eyeOffsetCache[eye][2]};
        // The animated actor already owns this reference. Applying the
        // accumulated gameplay turn again made the rappel face backwards.
        lastMovementYaw=0.f;
        return StereoRenderMath::CameraViewDelta(bounded,{},offset,0.f,settings.worldUnitsPerMetre,out);
    }
    rappelLook.Reset();
    if(cinematic||nativeCarried) {
        headPositionReference.Reset();
        Quat cinematicHead;
        const Vec3 eyeOffset=nativeScope?Vec3{}:Vec3{eyeOffsetCache[eye][0],eyeOffsetCache[eye][1],eyeOffsetCache[eye][2]};
        // Retain the native authored path while allowing ordinary physical
        // yaw/pitch/roll. No virtual turn, lean, body offset or dolly is added.
        if(nativeCarried)cinematicLook.Reset();
        auto& look=nativeCarried?carriedLook:cinematicLook;
        if(!look.SelectFree(head,camera,ack,cinematicHead)){SuspendPassengerNavigation();return false;}
        return StereoRenderMath::CameraViewDelta(cinematicHead,{},eyeOffset,0.f,settings.worldUnitsPerMetre,out);
    }
    cinematicLook.Reset();
    if(!referenceValid||referenceAck!=ack) {
        if(!MakeYawReference(head,referenceOrientation))return false;
        referencePosition=position;referenceAck=ack;referenceValid=true;
    }
    if(!headPositionReference.Select(position,activeSample.mounted,player,camera,cinematic,ack,referencePosition))return false;
    Quat relative;Vec3 displacement;
    if(!RelativeTrackingPose(head,position,referenceOrientation,referencePosition,
                             relative,displacement))return false;
    if(!StereoRenderMath::ApplyVirtualYaw(relative,displacement,activeSample.turnYaw,
                                         relative,displacement))return false;
    if(!StereoRenderMath::LimitHeadTranslation(displacement,settings.maxLeanMm/1000.f,displacement))return false;
    const Vec3 eyeOffset=nativeScope?Vec3{}:Vec3{eyeOffsetCache[eye][0],eyeOffsetCache[eye][1],eyeOffsetCache[eye][2]};
    const float dolly=dollyMetres>=0.f?dollyMetres:settings.cameraForwardMm/1000.f;
    const float scale=settings.worldUnitsPerMetre;
    if(!StereoRenderMath::CameraViewDelta(relative,displacement,eyeOffset,dolly,scale,out))return false;
    // Where the user looks relative to the game camera: host rotates the
    // Touch walking stick into this heading.
    const Vec3 look=Rotate(relative,{0.f,0.f,-1.f});
    const float yaw=std::atan2(look.x,-look.z);
    if(std::isfinite(yaw))lastMovementYaw.store(NativeActionKL::UsesNativeMovementAxes()?0.f:yaw);
    return true;
}

// Post-multiplies the camera delta onto a world/view matrix the engine feeds
// the device. Returns false when the result would be unsafe or the matrix
// must pass through untouched.
void SnapshotEye(unsigned eye) {
    // One snapshot per rendered eye frame: what the host must pair. The FOV
    // stored here is the one actually rendered (RenderFovPercent included).
    if(!frameSnapshot.tracked) {
        frameSnapshot.tracked=true;
        std::memcpy(frameSnapshot.orientation,activeSample.orientation,sizeof(frameSnapshot.orientation));
        std::memcpy(frameSnapshot.position,activeSample.position,sizeof(frameSnapshot.position));
        const float fovScale=settings.renderFovPercent/100.f;
        for(int side=0;side<4;++side)frameSnapshot.fov[side]=eyeFovCache[eye][side]*fovScale;
        frameSnapshot.tick=GetTickCount();
        frameSnapshot.eye=eye;frameSnapshot.frameId=pairSequence.FrameId();
        frameSnapshot.sensorTime=activeSample.sensorTime;frameSnapshot.recenterAck=activeRecenterAck;
    }
}

bool ApplyCameraDelta(float matrix[16],unsigned eye) {
    Mat4 delta;
    if(!BuildCameraDelta(eye,delta))return false;
    Mat4 view;std::memcpy(view,matrix,sizeof(Mat4));
    Mat4 stepped;
    MatMultiply(view,delta,stepped);
    for(int r=0;r<4;++r)for(int c=0;c<4;++c)if(!std::isfinite(stepped[r][c]))return false;
    std::memcpy(matrix,stepped,sizeof(Mat4));
    SnapshotEye(eye);
    return true;
}

bool NativeLoadingActive() {
    return nativeLoading.Active(nativeMenuActive,MovieActive()||NativeCameraKL::CinematicActive());
}
bool GrenadeVisualsHidden() {
    const auto action=SceneAction();
    return VRBridge::GrenadeVisualActive()||(action.valid&&action.action&&action.actionKind==NativeActionKL::Action::Throw);
}
bool CinemaPanelActive() {
    if(MovieActive()||NativeCameraKL::CinematicPanelActive())return true;
    if(!settings.cinematicVr) {
        const auto action=SceneAction();
        if(NativeCinematicKL::ActorCameraAction(action,action.player))return true;
    }
    return false;
}
bool ScenePanelActive() {
    return nativeMenuActive||NativeLoadingActive()||CinemaPanelActive();
}

bool ProvideNativeFrame(NativeCameraKL::Frame& frame,void*) {
    // This callback is GPU-consumer-only; no queue-building thread may sample
    // tracking, change a recenter reference or select an eye.
    nativeMenuActive=ReadNativeMenu();
    nativeLoading.ObserveOwnedCamera(nativeMenuActive);
    // The GPU consumer may retry this provider after an action changed inside
    // one Present epoch. Refresh once and use one consistent action snapshot.
    const auto sourceAction=SceneAction(true);
    const bool sourceVehicleTransition=vehicleCameraScope.Select(sourceAction);
    const bool sourceCarried=!sourceVehicleTransition&&sourceAction.valid&&(sourceAction.movementKind==NativeActionKL::Movement::Vehicle||sourceAction.movementKind==NativeActionKL::Movement::Mounted);
    if(!sourceAction.valid||sourceAction.movementKind!=NativeActionKL::Movement::Vehicle)SuspendPassengerNavigation();
    if(sourceAction.valid&&sourceAction.movementKind!=NativeActionKL::Movement::Vehicle){
        passengerEntryClearance.Reset();passengerEntryPair.Reset();passengerEntryActive=false;
    }
    if(!sourceCarried){carriedAttachment.Reset();carriedReferenceKind=0;}
    if(!sourceAction.FollowNativeCameraTilt()){rappelAttachment.Reset();rappelOrientationLocked=false;rappelAnatomicalUpLocked=false;rappelBodyPhase=0;}
    if(ScenePanelActive()){vehicleCameraTracking.Reset();return SceneDecision(SceneGate::Panel);}
    if(!settings.stereoMode)return SceneDecision(SceneGate::StereoOff);
    if(!VRBridge::Ready())return SceneDecision(SceneGate::HostMissing);
    // This verified main-camera callback runs on the render consumer, after
    // pre-camera driver waits. A 250+ ms wait must not force another discarded
    // image merely because tracking was sampled at the previous Present.
    // Never change tracking or eye identity once this image has begun.
    StereoPairPolicy::LatchAtCamera(pairSequence,frameSnapshot.tracked||projectionAppliedThisFrame,
        activeSample,GetTickCount(),activeRecenterAck,VRBridge::RecenterAck(),RefreshPairTracking);
    currentEye.store(pairSequence.Eye());
    if(!eyeConfigValid)return SceneDecision(SceneGate::EyeConfigMissing);
    const unsigned eye=currentEye.load();
    bool cinematic=NativeCameraKL::CinematicActive();
    frame.nativeCinematic=cinematic;
    if(cinematic) {
        SuspendPassengerNavigation();
        carriedAttachment.Reset();carriedLook.Reset();carriedReferenceKind=0;
        // Keep the author's verified external camera, with tracked look and
        // SDK stereo only. No first-person anchor, body follow or head hide.
        if(!frame.sourceCamera||frame.sourceCamera!=NativeCameraKL::CinematicCamera())return SceneDecision(SceneGate::CinematicCameraMismatch);
        frame.anchorCamera=frame.sourceCamera;
    } else {
        const auto& action=sourceAction;
        auto anchor=NativePlayerAnchor::Current(settings.worldUnitsPerMetre,settings.playerEyeHeightMm,action.FollowAnimatedHead());
        if(action.FollowAnimatedHead()&&anchor.valid&&action.player!=anchor.player)
            anchor=NativePlayerAnchor::Current(settings.worldUnitsPerMetre,settings.playerEyeHeightMm,false);
        const bool animatedHead=anchor.valid&&action.valid&&action.player==anchor.player&&action.FollowAnimatedHead();
        const bool animatedTilt=anchor.valid&&action.valid&&action.player==anchor.player&&action.FollowNativeCameraTilt();
        const bool pickup=anchor.valid&&action.valid&&action.player==anchor.player&&action.action&&action.actionKind==NativeActionKL::Action::Pickup&&
            (action.movementKind==NativeActionKL::Movement::Stand||action.movementKind==NativeActionKL::Movement::BaseMove);
        const bool alignedInteraction=anchor.valid&&action.player==anchor.player&&action.BodyAlignedInteraction();
        const bool drop=anchor.valid&&action.valid&&action.player==anchor.player&&
            action.movementKind==NativeActionKL::Movement::Fall&&!action.action;
        const bool vehicleTransition=anchor.valid&&action.player==anchor.player&&sourceVehicleTransition;
        const bool anatomicalPosture=animatedTilt||pickup||vehicleTransition;
        bool interactionQueuedAnchor=false,dropQueuedAnchor=false,gpuQueuedAnchor=false;
        const bool carried=anchor.valid&&action.player==anchor.player&&sourceCarried&&settings.firstPerson;
        const bool actorCinematic=anchor.valid&&NativeCinematicKL::ActorCameraAction(action,anchor.player);
        if(carried&&!actorCinematic&&action.movementKind==NativeActionKL::Movement::Vehicle&&activeSample.valid&&activeSample.mounted)
            NativePassengerActorKL::SetSeatedContext(anchor.player,anchor.sourceCamera,action.movement,pairSequence.FrameId()/2,
                static_cast<std::uint32_t>(activeRecenterAck),GetTickCount(),settings.worldUnitsPerMetre);
        else SuspendPassengerNavigation();
        NativeCameraMath::Vec3 gpuHead,animatedUp,animatedForward,rappelTowardHead;
        NativeCameraMath::CameraPose gpuActorRoot;
        NativeCameraMath::CameraPose stableRappelHead{{{},{},{}},{}};
        unsigned currentRappelBodyPhase=0;
        if(!actorCinematic&&settings.firstPerson&&anchor.valid&&NativeHandsKL::GpuHeadAnchor(anchor.player,gpuHead,settings.worldUnitsPerMetre,animatedHead,anatomicalPosture?&animatedUp:nullptr,anatomicalPosture?&animatedForward:nullptr,&gpuActorRoot,animatedTilt||carried?&stableRappelHead:nullptr,animatedTilt?&currentRappelBodyPhase:nullptr,animatedTilt?&rappelTowardHead:nullptr,carried?NativeHandsKL::StableHeadMode::Carried:NativeHandsKL::StableHeadMode::Rappel)) {
            gpuQueuedAnchor=true;
            anchor.position=gpuHead;anchor.headBone=true;anchor.animatedHead=animatedHead;
            anchor.root=gpuActorRoot;
            dropQueuedAnchor=drop;
            if(alignedInteraction){
                NativeCameraMath::Vec3 stablePickup;
                interactionQueuedAnchor=NativeCameraKLMath::StablePickupEye(gpuActorRoot,gpuHead,stablePickup);
                if(interactionQueuedAnchor)anchor.position=stablePickup;
            }
            if(anatomicalPosture){frame.nativeActionUpValid=true;frame.nativeActionUp[0]=animatedUp.x;frame.nativeActionUp[1]=animatedUp.y;frame.nativeActionUp[2]=animatedUp.z;}
            if(anatomicalPosture){frame.nativeActionForward[0]=animatedForward.x;frame.nativeActionForward[1]=animatedForward.y;frame.nativeActionForward[2]=animatedForward.z;}
        }
        playerAnchorValid.store(anchor.valid);playerAnchorHeadBone.store(anchor.headBone);
        // A passenger's simulation attachment and queued render palette may
        // describe different instants of the moving van. Never flash the
        // simulation/third-person position when its queued eye is missing.
        if(!anchor.valid|| (animatedHead&&!anchor.animatedHead)||(alignedInteraction&&!interactionQueuedAnchor)||(drop&&!dropQueuedAnchor)||
           (settings.firstPerson&&!actorCinematic&&(carried||vehicleTransition)&&!gpuQueuedAnchor)) {
            // Retaining an upright gameplay heading never retains an old eye.
            // The independent native movement snapshot must still own the
            // current player; scene/UI/tracking gates above do not set this.
            const auto current=NativeActionKL::Current();
            frame.anchorUnavailable=sourceAction.valid&&sourceAction.FollowAnimatedHead()&&current.valid&&
                current.player==sourceAction.player&&current.movement==sourceAction.movement;
            return SceneDecision(SceneGate::PlayerAnchorMissing);
        }
        if(vehicleTransition&&!actorCinematic){
            frame.nativeVehicleTransition=true;frame.vehicleAction=action.action;
            frame.vehicleBase={{animatedForward,animatedUp,NativeCameraMath::Cross(animatedUp,animatedForward)},anchor.position};
            if(!gpuQueuedAnchor||!NativeCameraMath::ValidatePose(frame.vehicleBase,.05f))return SceneDecision(SceneGate::PlayerAnchorMissing);
        }
        if(carried&&!actorCinematic) {
            using namespace NativeCameraMath;
            NativeCarriedCamera::Reference reference{anchor.root,anchor.player,1};
            const auto image=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
            const auto read=[](std::uintptr_t at,void* out,std::size_t size){SIZE_T got=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),out,size,&got)&&got==size;};
            NativeCarriedCamera::Reference mounted;
            if(NativeCarriedCamera::MountedReference(image,action,read,mounted))reference=mounted;
            const bool parentHead=ValidatePose(stableRappelHead,.05f);
            CameraPose anatomical=parentHead?stableRappelHead:CameraPose{anchor.root.basis,anchor.position};
            if(!NativeCarriedCamera::AnatomicalForward(anchor.root,anatomical))return SceneDecision(SceneGate::PlayerAnchorMissing);
            if(!carriedAttachment.valid||carriedAttachment.player!=anchor.player||carriedAttachment.camera!=anchor.sourceCamera||
               carriedAttachment.movement!=action.movement||carriedAttachment.carrier!=reference.identity||carriedAttachment.kind!=reference.kind)carriedLook.Reset();
            if(!carriedAttachment.Select(reference,anatomical,anchor.player,anchor.sourceCamera,action.movement,frame.carriedBase,pairSequence.FrameId()/2,action.action))return SceneDecision(SceneGate::PlayerAnchorMissing);
            if(action.movementKind==NativeActionKL::Movement::Vehicle){
                const auto now=GetTickCount();
                const auto observed=NativePassengerEntryKL::Snapshot(image,anchor.player,action.movement,anchor.sourceCamera,read);
                const auto entry=passengerEntryPair.Select(observed,pairSequence.FrameId()/2,now);
                const auto key=entry.valid?NativeCarriedCamera::EntryKey{entry.player,entry.movement,entry.record,entry.carrier}:passengerEntryClearance.key;
                CameraPose cleared;
                if(!passengerEntryClearance.Select(key,entry.valid,entry.known,entry.blocked,frame.carriedBase,settings.worldUnitsPerMetre,cleared))
                    return SceneDecision(SceneGate::PlayerAnchorMissing);
                frame.carriedBase=cleared;++passengerEntryFrames;
                passengerEntryStage=entry.stage;passengerEntryKnown=entry.known;passengerEntryBlocked=entry.blocked;
                passengerEntryActive=passengerEntryClearance.active;if(passengerEntryClearance.active)++passengerEntryApplied;
            }
            anchor.position=frame.carriedBase.position;
            frame.nativeCarried=true;frame.carriedReference=reference.identity;frame.carriedReferenceKind=reference.kind;
            frame.nativeMounted=action.movementKind==NativeActionKL::Movement::Mounted;
            carriedReferenceKind=reference.kind;++carriedFrames;
            if(parentHead)++carriedParentHeadFrames;else ++carriedCurrentHeadFrames;
            if(reference.kind==2)++carriedMountedBaseFrames;else ++carriedActorBaseFrames;
        }
        if(animatedTilt) {
            NativeCameraMath::CameraPose stable;bool stableBasis=false;
            const bool parentHead=NativeCameraMath::ValidatePose(stableRappelHead,.05f);
            const NativeCameraMath::Basis headBasis{animatedForward,animatedUp,NativeCameraMath::Cross(animatedUp,animatedForward)};
            const auto rawLocal=NativeRigMath::Inverse(anchor.root.basis,NativeCameraMath::Subtract(anchor.position,anchor.root.position));
            if(!rappelAttachment.Select(anchor.root,anchor.position,frame.nativeActionUpValid?&headBasis:nullptr,
                anchor.player,anchor.sourceCamera,sourceAction.movement,activeRecenterAck,stable,stableBasis,
                parentHead?&stableRappelHead:nullptr,currentRappelBodyPhase,&rappelTowardHead))return SceneDecision(SceneGate::PlayerAnchorMissing);
            if(parentHead)++rappelParentHeadFrames;else ++rappelCurrentHeadFrames;
            rappelOrientationLocked=rappelAttachment.orientationLocked;
            rappelAnatomicalUpLocked=rappelAttachment.anatomicalUpLocked;
            rappelBodyPhase=currentRappelBodyPhase;
            rappelEntryTiltDegrees=rappelAttachment.upwardPitch*57.2957795f;
            anchor.position=stable.position;frame.nativeActionUpValid=stableBasis;
            if(stableBasis) {
                frame.nativeActionUp[0]=stable.basis.up.x;frame.nativeActionUp[1]=stable.basis.up.y;frame.nativeActionUp[2]=stable.basis.up.z;
                frame.nativeActionForward[0]=stable.basis.forward.x;frame.nativeActionForward[1]=stable.basis.forward.y;frame.nativeActionForward[2]=stable.basis.forward.z;
            }
            const auto stableLocal=NativeRigMath::Inverse(anchor.root.basis,NativeCameraMath::Subtract(stable.position,anchor.root.position));
            const float raw[]={rawLocal.x,rawLocal.y,rawLocal.z},fixed[]={stableLocal.x,stableLocal.y,stableLocal.z};
            for(unsigned i=0;i<3;++i){rappelNativeHeadLocal[i]=raw[i]/settings.worldUnitsPerMetre;rappelStableHeadLocal[i]=fixed[i]/settings.worldUnitsPerMetre;}
            ++rappelAttachmentFrames;
        }
        if(actorCinematic){
            if(!anchor.sourceCamera||frame.sourceCamera!=anchor.sourceCamera)return SceneDecision(SceneGate::CinematicCameraMismatch);
            cinematic=true;frame.nativeCinematic=true;frame.actorCinematic=true;
        }
        frame.anchorPlayer=anchor.player;
        frame.actorForwardValid=gpuQueuedAnchor&&!actorCinematic&&settings.firstPerson;
        if(frame.actorForwardValid)frame.actorForward=anchor.root.basis.forward;
        frame.nativePickup=pickup&&frame.nativeActionUpValid&&!actorCinematic&&settings.firstPerson;
        if(frame.nativePickup){frame.pickupAction=action.action;frame.pickupActorForward=anchor.root.basis.forward;}
        frame.nativeActionTilt=animatedTilt;
        frame.animatedEye=animatedHead;
        frame.nativeMovementAxes=action.valid&&action.player==anchor.player&&action.NativeMovementAxes();
        frame.stableFirstPersonYaw=!actorCinematic&&settings.firstPerson&&settings.stableFirstPersonYaw;
        frame.authoredHeading=frame.stableFirstPersonYaw&&!carried&&!animatedTilt&&!vehicleTransition&&
            NativeCinematicKL::AuthoredActorHeading(NativeCinematicKL::Snapshot(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)),ReadNativeBytes),action,anchor.player);
        if(frame.authoredHeading){frame.authoredHeadingAction=action.action;frame.authoredActorForward=anchor.root.basis.forward;}
        frame.anchorValid=!actorCinematic&&settings.firstPerson!=0;frame.anchorCamera=anchor.sourceCamera;
        frame.anchorPosition[0]=anchor.position.x;frame.anchorPosition[1]=anchor.position.y;frame.anchorPosition[2]=anchor.position.z;
    }
    Mat4 delta;
    const auto scope=SceneScope(true);
    frame.nativeScope=!cinematic&&!frame.nativeActionTilt&&frame.anchorValid&&scope.valid&&scope.active&&scope.player==frame.anchorPlayer&&scope.sourceCamera==frame.sourceCamera;
    if(!BuildCameraDelta(eye,delta,cinematic||frame.anchorValid?0.f:-1.f,
                         frame.anchorPlayer,frame.anchorCamera,cinematic,
                         frame.nativeActionTilt&&sourceAction.player==frame.anchorPlayer?sourceAction.movement:0,frame.nativeScope,frame.nativeCarried,
                         frame.nativeVehicleTransition?frame.vehicleAction:0))return false;
    std::memcpy(frame.delta,delta,sizeof(delta));
    const float renderScale=settings.renderFovPercent/100.f;
    for(unsigned side=0;side<4;++side) {
        frame.fov[side]=eyeFovCache[eye][side]*renderScale;
        frame.cullingFov[side]=std::max(eyeFovCache[0][side],eyeFovCache[1][side])*renderScale*1.15f;
    }
    frame.nearUnits=std::max(1.f,settings.nearPlaneMm/1000.f*settings.worldUnitsPerMetre);
    std::memcpy(frame.rawHeadOrientation,activeSample.orientation,sizeof(frame.rawHeadOrientation));
    std::memcpy(frame.rawHeadPosition,activeSample.position,sizeof(frame.rawHeadPosition));
    std::memcpy(frame.eyeOffset,eyeOffsetCache[eye],sizeof(frame.eyeOffset));
    if(frame.nativeScope)for(auto& offset:frame.eyeOffset)offset=0.f;
    frame.worldUnitsPerMetre=settings.worldUnitsPerMetre;
    frame.trackingTick=activeSample.tick;frame.sensorTime=activeSample.sensorTime;
    frame.recenterAck=activeRecenterAck;
    frame.eye=eye;frame.pairId=pairSequence.FrameId()/2;frame.valid=true;
    return true;
}

void NativeProjectionApplied(const NativeCameraKL::Frame& frame,const NativeCameraMath::CameraPose& actualEye,const float* projection,void*) {
    // Only the consumer that has written the actual inverse VIEW/P/eye-plane
    // can authorize scene metadata. Producer-side visibility never does so.
    if(!frame.valid||frame.eye>1||!projection||!NativeCameraMath::ValidatePose(actualEye)||
       frame.pairId*2+frame.eye!=pairSequence.FrameId())return;
    if(frame.pairDiscontinuity){
        frameSnapshot={};projectionAppliedThisFrame=false;publishedThisFrame=false;
        pairSequence.Restart();currentEye=0;activeSample={};actorCinematicApplied=false;return;
    }
    for(unsigned element=0;element<16;++element)if(!std::isfinite(projection[element]))return;
    postfilterFrameEvidence={};
    if(frame.anchorValid&&!frame.nativeCinematic&&!frame.nativeScope){
        postfilterFrameEvidence.frame.id=frame.pairId*2+frame.eye;
        postfilterFrameEvidence.frame.eye=frame.eye;postfilterFrameEvidence.frame.tick=GetTickCount();
        postfilterFrameEvidence.epoch=renderEpoch.load(std::memory_order_acquire);
    }
    handHudCamera={};
    if(!frame.nativeCinematic&&frame.anchorPlayer){
        handHudCamera.player=frame.anchorPlayer;handHudCamera.pairId=frame.pairId;
        handHudCamera.tick=GetTickCount();handHudCamera.recenterAck=static_cast<std::uint32_t>(frame.recenterAck);
        handHudCamera.unitsPerMetre=frame.worldUnitsPerMetre;handHudCamera.actualEye=actualEye;handHudCamera.centre=actualEye;
        if(frame.anchorValid&&!frame.nativeScope&&!frame.actorCinematic)
            NativeMirrorBumpKL::WarmTowerEnvironment(actualEye.position);
        using namespace NativeCameraMath;
        handHudCamera.centre.position=Subtract(actualEye.position,Scale(MapTracking(actualEye.basis,{frame.eyeOffset[0],frame.eyeOffset[1],frame.eyeOffset[2]}),frame.worldUnitsPerMetre));
        std::memcpy(handHudCamera.projection,projection,sizeof(handHudCamera.projection));
        if(frame.anchorValid&&!frame.nativeScope&&!frame.actorCinematic)
            NativeGameplayHudKL::PublishMapView(frame.anchorPlayer,actualEye.basis.forward.x,actualEye.basis.forward.z);
    }
    std::memcpy(vrProjection,projection,sizeof(vrProjection));
    projectionAppliedThisFrame=true;
    frameSnapshot.tracked=true;frameSnapshot.nativeGpuApplied=true;
    std::memcpy(frameSnapshot.orientation,frame.rawHeadOrientation,sizeof(frameSnapshot.orientation));
    std::memcpy(frameSnapshot.position,frame.rawHeadPosition,sizeof(frameSnapshot.position));
    std::memcpy(frameSnapshot.fov,frame.fov,sizeof(frameSnapshot.fov));
    frameSnapshot.eye=frame.eye;frameSnapshot.frameId=frame.pairId*2+frame.eye;
    frameSnapshot.sensorTime=frame.sensorTime;frameSnapshot.tick=frame.trackingTick;
    frameSnapshot.recenterAck=frame.recenterAck;
    frameSnapshot.nativePlayer=frame.anchorPlayer;frameSnapshot.nativeSourceCamera=frame.sourceCamera;
    frameSnapshot.actorCinematic=frame.actorCinematic;frameSnapshot.nativeCinematic=frame.nativeCinematic;
    frameSnapshot.worldEye=actualEye.position;
    NativeEffectsKL::SetCinematicViewport(frame.nativeCinematic,backbufferWidth,backbufferHeight,projection);
    frameSnapshot.rappel=frame.nativeActionTilt;
    frameSnapshot.nativeMovementAxes=frame.nativeMovementAxes;
    frameSnapshot.nativeCarried=frame.nativeCarried;
    frameSnapshot.nativeMounted=frame.nativeMounted;
    frameSnapshot.nativeScope=frame.nativeScope;
    NativeRopeKL::SetFrame(frame.anchorPlayer,frameSnapshot.frameId,frame.anchorValid&&!frame.nativeCinematic);
    NativeCarriedPersonVisibilityKL::SetFrame(frame.anchorPlayer,frame.worldUnitsPerMetre,
        settings.firstPerson&&frame.anchorValid&&!frame.nativeCinematic&&!frame.actorCinematic);
    NativeWeaponKL::SetScopedDrawFrame(frame.anchorPlayer,frame.pairId,frame.anchorValid&&!frame.nativeCinematic&&frame.nativeScope);
    NativeGrenadeVisualKL::SetFrame(frame.anchorPlayer,frame.pairId,frame.anchorValid&&!frame.nativeCinematic&&GrenadeVisualsHidden());
    if(frame.nativeScope)++scopeFrames;
    if(!frame.nativeCinematic&&frame.anchorValid&&frame.anchorPlayer&&activeSample.valid&&activeSample.mounted) {
        NativeWeaponKL::HeadGaze gaze;
        gaze.player=frame.anchorPlayer;gaze.sourceCamera=frame.sourceCamera;gaze.pairId=frame.pairId;
        gaze.recenterAck=static_cast<std::uint32_t>(frame.recenterAck);gaze.tick=frame.trackingTick;
        gaze.unitsPerMetre=frame.worldUnitsPerMetre;gaze.origin=actualEye.position;gaze.direction=actualEye.basis.forward;gaze.mounted=true;
        NativeWeaponKL::PublishHeadGaze(gaze);
        gaze.origin=handHudCamera.centre.position;NativeTeamCommandsKL::PublishHeadRay(gaze);
    } else {aimPairTarget.Reset();laserPairBeam.Reset();NativeGrenadeVisualKL::ClearFrame();NativeWeaponKL::ClearScopedDrawFrame();NativeWeaponKL::InvalidateHeadGaze();NativeTeamCommandsKL::InvalidateHeadRay();}
    actorCinematicApplied=frame.actorCinematic;
    const bool movementValid=!frame.nativeCinematic&&(frame.nativeMovementAxes||frame.nativeCarried||(frame.movementYawValid&&std::isfinite(frame.movementYawRadians)));
    nativeMovementYawValid.store(movementValid);
    lastMovementYaw.store(movementValid&&!frame.nativeMovementAxes&&!frame.nativeCarried?frame.movementYawRadians:0.f);
    ++projectionModified;
}

bool StereoSceneActive() {
    return !ScenePanelActive()&&!frameSnapshot.nativeScope&&settings.stereoMode&&frameSnapshot.tracked&&
           projectionAppliedThisFrame&&VRBridge::Ready();
}

void FillHudGeometry(NativeHudKL::Frame& frame,const float (&fov)[2][4]) {
    const float renderScale=settings.renderFovPercent/100.f;
    for(unsigned side=0;side<2;++side)for(unsigned edge=0;edge<4;++edge)
        frame.fov[side][edge]=fov[side][edge]*renderScale;
    frame.width=backbufferWidth;frame.height=backbufferHeight;
    frame.aspect=float(frame.width)/frame.height;
    frame.distanceMetres=settings.hudDistanceMm/1000.f;frame.coverage=settings.hudScalePercent/100.f;
}

bool ProvideHudFrame(NativeHudKL::Frame& frame) {
    if(NativeLoadingActive()||MovieActive()||frameSnapshot.nativeScope||!settings.stereoHud||!StereoSceneActive()||!eyeConfigValid||
       frameSnapshot.eye>1||frameSnapshot.frameId!=pairSequence.FrameId()||
       VRBridge::RecenterAck()!=frameSnapshot.recenterAck||
       GetTickCount()-frameSnapshot.tick>250||!backbufferWidth||!backbufferHeight)return false;
    const auto eye=frameSnapshot.eye;
    FillHudGeometry(frame,eyeFovCache);
    std::memcpy(frame.eyeOffset,eyeOffsetCache[eye],sizeof(frame.eyeOffset));
    frame.eye=eye;frame.pairId=frameSnapshot.frameId/2;
    return true;
}

bool ProvideCinemaTextFrame(NativeHudKL::CinemaTextFrame& text) {
    if(!settings.stereoHud||!backbufferWidth||!backbufferHeight||
       nativeMenuActive||NativeLoadingActive()||!CinemaPanelActive())return false;
    // A film can be the first image after connecting. Its mono path has
    // no stereo camera pair from which to borrow an eye configuration.
    float offsets[2][3]{},fov[2][4]{};
    if(!VRBridge::EyeConfig(0,offsets[0],fov[0])||!VRBridge::EyeConfig(1,offsets[1],fov[1]))return false;
    NativeHudKL::Frame frame;
    FillHudGeometry(frame,fov);
    NativeHudMath::Plane hud;
    MonoFrame::Extent panel;
    if(!NativeHudMath::Build(frame.fov,frame.eyeOffset,0,frame.aspect,frame.distanceMetres,frame.coverage,hud)||
       !MonoFrame::PanelExtent(frame.width,frame.height,settings.panelWidthMm/1000.f,settings.panelDistanceMm/1000.f,panel)||
       !NativeHudMath::CinemaTextScale(hud.tangentY,panel.height,panel.distance,text.scale))return false;
    text.width=frame.width;text.height=frame.height;
    return true;
}

bool HideNativeLetterbox() {
    // The native letterbox producer can run outside the GPU camera scope.
    // Read current tracking plus atomically published scene state; never
    // mutate the stereo pair or inspect render-thread-only FrameSnapshot.
    if(!settings.nativeCamera||!settings.stereoMode||!NativeCameraKL::Installed()||
       !VRBridge::Ready()||GetTickCount()-lastSceneTick.load()>250)return false;
    if(ScenePanelActive()||SceneScope().active)return false;
    if(NativeCameraKL::CinematicActive()) {
        if(!NativeCameraKL::CinematicCamera()||NativeCameraKL::CinematicPanelActive())return false;
    } else if(!settings.firstPerson||ReadNativeMenu()||!playerAnchorValid.load())return false;
    const auto tracking=VRBridge::HeadPose();
    return tracking.valid&&GetTickCount()-tracking.tick<=250;
}

bool TrackedGameplayScene(bool allowWeaponMenu=false){
    if(SceneScope().active)return false;
    static thread_local DWORD lastCheck[2]{};static thread_local bool lastEligible[2]{};
    auto& checked=lastCheck[allowWeaponMenu];auto& eligible=lastEligible[allowWeaponMenu];
    const auto now=GetTickCount();if(checked&&now-checked<8)return eligible;checked=now;
    eligible=false;
    unsigned char nativeMode=0xff;
    if(!ReadNativeBytes(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr))+0x8883A4,&nativeMode,sizeof(nativeMode))||
       (nativeMode!=0&&!(allowWeaponMenu&&nativeMode==3&&VRBridge::TeamHudVisible()&&VRBridge::WeaponMenuVisible())))return false;
    if(!settings.firstPerson||!settings.stereoMode||!VRBridge::Ready()||
       now-lastSceneTick.load()>500||ReadNativeMenu()||movieActive.load()||NativeCameraKL::CinematicActive())return false;
    const auto action=NativeActionKL::Current();
    if(NativeCinematicKL::ActorCameraAction(action,action.player))return false;
    const auto head=VRBridge::HeadPose();eligible=head.valid&&head.mounted&&now-head.tick<=250;return eligible;
}
bool WeaponGameplayScene(){return settings.nativeArms&&TrackedGameplayScene();}
bool LegacyHudScene(){
    unsigned char mode=0xff;
    return settings.firstPerson&&settings.stereoMode&&VRBridge::Ready()&&!ReadNativeMenu()&&!MovieActive()&&
        ReadNativeBytes(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr))+0x8883A4,&mode,1)&&mode==0;
}
bool NativeScopeHudScene(){return frameSnapshot.nativeScope&&frameSnapshot.nativeGpuApplied&&NativeWeaponKL::ScopeOverlayActive();}
bool LegacyRosterScene(){
    if(LegacyHudScene())return true;
    unsigned char mode=0xff;
    return settings.firstPerson&&settings.stereoMode&&VRBridge::Ready()&&!MovieActive()&&
        ReadNativeBytes(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr))+0x8883A4,&mode,1)&&mode==3;
}
void NativeSimulationTick(std::uintptr_t player) {
    NativeMountedAimKL::SimulationTick(player);
    NativeInvulnerabilityKL::SimulationTick(player);
    NativeWeaponKL::ScopeSimulationTick(player);
    NativeWeaponKL::ReloadSimulationTick(player);
    NativeAimKL::SimulationTick(player);
    NativeTeamCommandsKL::SimulationTick(player);
    NativeTutorialKL::SimulationTick(player);
    NativeLedgeHintKL::SimulationTick(player);
}
void NativeWorldObserved(void* state,const NativeCameraMath::CameraPose* source,std::uint32_t caller) {
    // Effects must restore tracked VIEW before any other observer or binder.
    NativeEffectsKL::ObserveWorldBind(state,source,caller);
    NativeHandsKL::ObserveWorldBind(state,source,caller);
    NativeRopeKL::ObserveWorldBind(state,source,caller);
    NativeGrenadeVisualKL::ObserveWorldBind(state,source);
}
bool HideLocalRopeDraw(IDirect3DDevice9* device,bool indexed){
    const bool hidden=NativeBufferFlushKL::ShouldBlockDraw(device,indexed)||NativeGrenadeVisualKL::SuppressDraw()||NativeWeaponKL::SuppressScopedDraw()||NativeWeaponKL::SuppressStowedDraw()||NativeRopeKL::SuppressDraw();
    if(!hidden&&diagnosticLogging.load(std::memory_order_relaxed)){NativeHandsKL::ObserveNativeDraw(device);NativeWeaponKL::ObserveNativeDraw(device);
        if(materialSequenceEnabled.load(std::memory_order_relaxed))NativeWeaponKL::ObserveMaterialSequence(device);
        if(NativeReflectionProbeKL::Enabled())NativeReflectionProbeKL::Observe(device,frameSnapshot.frameId,StereoSceneActive);}
    return hidden;
}
thread_local NativeDrawScopes nativeDrawScopes;
void* BeginNativeDraw(IDirect3DDevice9* device,const D3D9Capture::DrawCall& call){
    return nativeDrawScopes.Begin(device,NativeEffectsKL::BeginDraw,NativeSkinLightingKL::BeginDraw,
        [&](IDirect3DDevice9* drawDevice){return NativeMirrorBumpKL::BeginDraw(drawDevice,&call);},
        NativeCarriedPersonVisibilityKL::BeginDraw);
}
HRESULT FinishNativeDraw(void* token,HRESULT result){
    return nativeDrawScopes.Finish(token,result,NativeEffectsKL::FinishDraw,NativeSkinLightingKL::FinishDraw,NativeMirrorBumpKL::FinishDraw,NativeCarriedPersonVisibilityKL::FinishDraw);
}
void DrawNativeGeometryCopies(IDirect3DDevice9* device,const D3D9Capture::DrawCall& call){
    NativeWeaponKL::DrawDroppedMagazines(device,call);
    if(!NativeWeaponKL::ReloadPropsPending()||frameSnapshot.nativeScope||frameSnapshot.nativeCinematic||
       !frameSnapshot.nativeGpuApplied||!handHudCamera.player)return;

    static thread_local std::uint64_t attempted=UINT64_MAX;
    const auto epoch=renderEpoch.load(std::memory_order_relaxed);
    if(attempted==epoch||!NativeHandsKL::ReloadPropDrawActive(handHudCamera.player,handHudCamera.pairId))return;
    attempted=epoch;

    NativeWeaponKL::ReloadProps props;
    const unsigned count=NativeWeaponKL::SnapshotReloadProps(handHudCamera.player,props);
    if(!count)return;
    NativeAimRenderKL::Frame frames[std::tuple_size<NativeWeaponKL::ReloadProps>::value]{};
    for(unsigned i=0;i<count;++i){
        auto& frame=frames[i];
        const auto& prop=props[i];
        frame.eye=handHudCamera.actualEye;
        std::memcpy(frame.projection,handHudCamera.projection,sizeof(frame.projection));
        frame.propPose=prop.pose;frame.propHalf=prop.half;
        frame.prop=prop.shell?NativeAimRenderKL::Prop::ShotgunShell:NativeAimRenderKL::Prop::Round;
    }
    NativeWeaponKL::RecordReloadPropDraw(HandsRenderer::DrawReloadProps(device,frames,count));
}
void RecordVisualDrawContext(){
    NativeDrawContextKL::Context owner;
    NativeHandsKL::SnapshotNativeDrawContext(owner);NativeWeaponKL::AppendNativeDrawContext(owner);
    NativeVisualTraceKL::Evidence(10,&owner,sizeof(owner));
    NativeCameraKL::DrawCameraContext camera;NativeCameraKL::SnapshotDrawCamera(owner.state,camera);
    NativeVisualTraceKL::Evidence(11,&camera,sizeof(camera));
    NativeEffectsKL::VisualSourceDiagnostic source;NativeEffectsKL::SnapshotVisualSource(owner.entry,owner.state,source);
    NativeVisualTraceKL::Evidence(12,&source,sizeof(source));
    if((owner.roles&(NativeDrawContextKL::LocalBody|NativeDrawContextKL::ActiveLeftHand|NativeDrawContextKL::ActiveRightHand|NativeDrawContextKL::AttachedToLocalPlayer))||source.valid)
        NativeVisualTraceKL::Detail((std::uint64_t(owner.entry)<<16)^owner.pass^(std::uint64_t(source.kind)<<60));
}
void RecordVisualProducer(const NativeCameraDiagnosticsKL::ProducerBatchContext& source){NativeVisualTraceKL::Evidence(13,&source,sizeof(source),source.sequence);}
void RecordVisualProducerBytes(std::uint64_t sequence,const void* bytes,std::size_t size){NativeVisualTraceKL::Evidence(14,bytes,size,sequence);}

bool CurrentAimTarget(NativeWeaponKL::AimFrame& aim,NativeCameraMath::Vec3& target,bool& hit){
    if(GrenadeVisualsHidden())return false;
    const auto action=NativeActionKL::Current();if(action.TrackedReload())return false;
    if(!frameSnapshot.nativeGpuApplied||frameSnapshot.nativeCinematic||!WeaponGameplayScene()||
       !handHudCamera.player||handHudCamera.pairId!=frameSnapshot.frameId/2||
       VRBridge::WeaponMenuVisible()||!NativeWeaponKL::SnapshotAim(aim)||
       aim.player!=handHudCamera.player||aim.pairId!=handHudCamera.pairId||aim.recenterAck!=handHudCamera.recenterAck)return false;
    NativeAimKL::Endpoint endpoint;const bool available=NativeAimKL::ReadEndpoint(aim,endpoint);
    NativeAimTargetPolicy::Selection selected;
    if(!aimPairTarget.Select(aim,available?&endpoint:nullptr,GetTickCount(),selected))return false;
    hit=selected.hit;target=selected.target;
    return true;
}
bool CurrentLaserFrame(NativeAimRenderKL::Frame& frame,bool& surfaceContact){
    ProfileTiming::Scope timing(ProfileTiming::LaserPose);
    const auto eye=std::min(frameSnapshot.eye,1u);
    if(GrenadeVisualsHidden()||NativeActionKL::Current().TrackedReload()){++laserContextRejected[eye];return false;}
    NativeWeaponKL::AimFrame aim;
    if(!frameSnapshot.nativeGpuApplied||frameSnapshot.nativeCinematic||!WeaponGameplayScene()||
       !handHudCamera.player||handHudCamera.pairId!=frameSnapshot.frameId/2||
       VRBridge::WeaponMenuVisible()){++laserContextRejected[eye];return false;}
    if(!NativeWeaponKL::SnapshotCaptureAim(aim)){++laserCaptureRejected[eye];return false;}
    if(aim.player!=handHudCamera.player||aim.pairId!=handHudCamera.pairId||aim.recenterAck!=handHudCamera.recenterAck){++laserIdentityRejected[eye];return false;}
    NativeAimKL::Endpoint endpoint;
    const bool available=NativeAimKL::ReadEndpoint(aim,endpoint);
    NativeLaserPoseKL::Beam beam;
    if(!laserPairBeam.Select(aim,available?&endpoint:nullptr,GetTickCount(),beam)){++laserBeamRejected[eye];return false;}
    NativeAimRenderKL::Frame next;next.eye=handHudCamera.actualEye;next.origin=aim.origin;
    next.contact=beam.end;next.contactValid=beam.surfaceContact;next.units=aim.unitsPerMetre;
    std::memcpy(next.projection,handHudCamera.projection,sizeof(next.projection));
    frame=next;surfaceContact=beam.surfaceContact;return true;
}
bool ProvideReticleFrame(NativeHudKL::ReticleFrame& output){
    if(frameSnapshot.nativeMounted){
        if(!settings.mountedReticle||!frameSnapshot.nativeGpuApplied||frameSnapshot.nativeCinematic||
           !TrackedGameplayScene()||!handHudCamera.player||handHudCamera.pairId!=frameSnapshot.frameId/2||
           VRBridge::WeaponMenuVisible())return false;
        NativeMountedAimKL::ReticleAim target;
        if(!NativeMountedAimKL::SnapshotReticle(handHudCamera.player,handHudCamera.unitsPerMetre,target)||
           target.recenterAck!=handHudCamera.recenterAck)return false;
        NativeHudKL::ReticleFrame result;result.player=handHudCamera.player;result.pairId=handHudCamera.pairId;
        result.tick=GetTickCount();result.eye=frameSnapshot.eye;result.actualEye=handHudCamera.actualEye;
        result.target=target.target;result.width=backbufferWidth;result.height=backbufferHeight;
        std::memcpy(result.projection,handHudCamera.projection,sizeof(result.projection));output=result;return true;
    }
    NativeWeaponKL::AimFrame aim;NativeCameraMath::Vec3 target;bool hit=false;
    if(!settings.nativeReticle||!CurrentAimTarget(aim,target,hit)||!hit)return false;
    NativeHudKL::ReticleFrame result;result.player=aim.player;result.pairId=aim.pairId;result.tick=GetTickCount();
    result.actualEye=handHudCamera.actualEye;result.target=target;
    std::memcpy(result.projection,handHudCamera.projection,sizeof(result.projection));
    result.width=backbufferWidth;result.height=backbufferHeight;output=result;return true;
}
bool ProvideNativeMarkerFrame(NativeHudKL::ReticleFrame& output){
    // The original marker's WORLD point is supplied by its owned native
    // producer. Its eye projection must not depend on a weapon, wrist or
    // the optional aiming reticle being available.
    if(!frameSnapshot.nativeGpuApplied||frameSnapshot.nativeCinematic||!TrackedGameplayScene(true)||
       !handHudCamera.player||handHudCamera.pairId!=frameSnapshot.frameId/2||
       backbufferWidth<16||backbufferHeight<16||
       !NativeCameraMath::ValidatePose(handHudCamera.actualEye,.05f))return false;
    NativeHudKL::ReticleFrame result;result.player=handHudCamera.player;result.pairId=handHudCamera.pairId;
    result.tick=GetTickCount();result.eye=frameSnapshot.eye;result.actualEye=handHudCamera.actualEye;
    result.centre=handHudCamera.centre;
    const float renderScale=settings.renderFovPercent/100.f;
    const float horizontal=(eyeFovCache[0][0]+eyeFovCache[0][1]+eyeFovCache[1][0]+eyeFovCache[1][1])*renderScale*.25f;
    const float vertical=(eyeFovCache[0][2]+eyeFovCache[0][3]+eyeFovCache[1][2]+eyeFovCache[1][3])*renderScale*.25f;
    const float commonFov[4]={horizontal,horizontal,vertical,vertical};
    if(!NativeCameraMath::ApplyEyeProjection(result.referenceProjection,commonFov,1.f,40000.f,true))return false;
    std::memcpy(result.projection,handHudCamera.projection,sizeof(result.projection));
    result.width=backbufferWidth;result.height=backbufferHeight;output=result;return true;
}

bool ProvideWristHud(NativeHudKL::WristHudElement element,std::uintptr_t player,NativeHudKL::WristHudEye& output){
    const bool climb=element==NativeHudKL::WristHudElement::ClimbAction;
    const bool action=climb||element==NativeHudKL::WristHudElement::Action;
    // Panel sprites already passed the held-input check when their wheel was submitted.
    if((action&&(!settings.gameHints||VRBridge::TeamHudVisible()||VRBridge::WeaponMenuVisible()))||
       !frameSnapshot.nativeGpuApplied||frameSnapshot.nativeCinematic||
       frameSnapshot.nativeScope||player!=handHudCamera.player||frameSnapshot.eye>1||
       handHudCamera.pairId!=frameSnapshot.frameId/2||!backbufferWidth||!backbufferHeight)return false;
    struct CachedEye {
        std::uintptr_t player=0;
        std::uint64_t frame=0,epoch=0;
        NativeHudKL::WristHudEye eye;
    };
    static thread_local CachedEye cachedEyes[static_cast<unsigned>(NativeHudKL::WristHudElement::Count)];
    auto& cached=cachedEyes[static_cast<unsigned>(element)];
    if(cached.player==player&&cached.frame==frameSnapshot.frameId&&cached.epoch==renderEpoch){
        output=cached.eye;return true;
    }
    const bool wheel=element==NativeHudKL::WristHudElement::WeaponWheel;
    const bool weapon=wheel||element==NativeHudKL::WristHudElement::Weapon;
    const bool leftPanel=element==NativeHudKL::WristHudElement::Minimap||element==NativeHudKL::WristHudElement::Team;
    const unsigned hand=(weapon||climb)?1:0;
    auto hud=handHudCamera;
    NativeHandsKL::VisualFrame visual;
    if((NativeHandsKL::SnapshotDrawVisual(visual)||NativeHandsKL::SnapshotVisual(visual))&&
       visual.player==player&&visual.pairId==hud.pairId&&visual.recenterAck==hud.recenterAck&&
       GetTickCount()-visual.tick<=150&&visual.tracked[hand]){
        hud.wrist[hand]=visual.finalWrist[hand];
    }else{
        // HUD quads may be consumed before the skin has published its wrist
        // pose. The current eye already owns a sample of the same controller.
        NativeHandsKL::DrawController controller;
        if(!leftPanel||!NativeHandsKL::SnapshotDrawController(player,hud.pairId,hand,controller)||
           !controller.tracked||controller.recenter!=hud.recenterAck)return false;
        hud.wrist[hand]=controller.wrist;
    }
    hud.tracked[hand]=true;
    const float eyeAspect=(float(backbufferWidth)/backbufferHeight)*(hud.projection[0]/hud.projection[5]);
    if(weapon){
        NativeHandHudKL::RightWristLayout layout;
        if(!NativeHandHudKL::PlaceRightWrist(hud,backbufferWidth,backbufferHeight,layout))return false;
        const auto place=[&](NativeHudKL::WristHudElement part,float y,float scale){
            cachedEyes[static_cast<unsigned>(part)]={player,frameSnapshot.frameId,renderEpoch,
                {layout.x,y,scale,backbufferWidth,backbufferHeight,frameSnapshot.eye,hud.pairId,eyeAspect}};
        };
        place(NativeHudKL::WristHudElement::WeaponWheel,layout.wheelY,layout.wheelScale);
        place(NativeHudKL::WristHudElement::Weapon,layout.weaponY,layout.weaponScale);
        output=cached.eye;return true;
    }
    const auto place=[&](NativeHudKL::WristHudElement part,float across,float above,float size){
        NativeHandHudKL::Panel panel;
        if(!NativeHandHudKL::NearHand(hud,hand,.28f,.06f,across,above,panel))return false;
        NativeHandHudKL::Vertex centre;
        if(!NativeHandHudKL::Project(hud.actualEye,hud.projection,backbufferWidth,backbufferHeight,panel.centre,.5f,.5f,centre))return false;
        const float depth=Dot(Subtract(panel.centre,hud.centre.position),hud.centre.basis.forward)/hud.unitsPerMetre;
        if(!std::isfinite(depth)||depth<.12f)return false;
        cachedEyes[static_cast<unsigned>(part)]={player,frameSnapshot.frameId,renderEpoch,
            {(centre.x+.5f)*2.f/backbufferWidth-1.f,1.f-(centre.y+.5f)*2.f/backbufferHeight,
             std::clamp(size/depth,.45f,1.35f),backbufferWidth,backbufferHeight,frameSnapshot.eye,hud.pairId,eyeAspect}};
        return true;
    };
    if(leftPanel){
        const bool map=place(NativeHudKL::WristHudElement::Minimap,-.010f,.22f,.42f);
        const bool team=place(NativeHudKL::WristHudElement::Team,.035f,.14f,.41f);
        if(!(element==NativeHudKL::WristHudElement::Minimap?map:team))return false;
    }else if(!place(element,.035f,.14f,action?.42f:.41f))return false;
    output=cached.eye;
    return true;
}

bool OnTransform(int kind,float matrix[16],void*) {
    // One-shot diagnostic of what K&L's Glacier actually feeds the pipeline:
    // whether the camera lives in D3DTS_VIEW or in per-object WORLD matrices.
    static std::atomic<unsigned> loggedViews{0},loggedProjections{0},loggedWorlds{0};
    if(kind==0) {
        ++totalViews;
        const bool identity=IsIdentityView(matrix);
        viewCarriesCamera=!identity;
        if(identity)++identityViews;
        if(loggedViews.load()<4) {
            char message[420];std::snprintf(message,sizeof(message),
                "VIEW#%u %s: %.3f %.3f %.3f %.3f / %.3f %.3f %.3f %.3f / %.3f %.3f %.3f %.3f / %.3f %.3f %.3f %.3f",
                totalViews.load(),identity?"(identity)":"",matrix[0],matrix[1],matrix[2],matrix[3],
                matrix[4],matrix[5],matrix[6],matrix[7],matrix[8],matrix[9],matrix[10],matrix[11],
                matrix[12],matrix[13],matrix[14],matrix[15]);
            Log(message);++loggedViews;
        }
    } else if(kind==2) {
        ++totalWorlds;
        // Log only rotated matrices: menu/UI quads are axis-aligned and would
        // consume the whole budget before gameplay reveals the real camera.
        bool rotated=false;
        for(int r=0;r<3&&!rotated;++r)for(int c=0;c<3;++c)
            if(r!=c&&std::fabs(matrix[r*4+c])>=0.01f){rotated=true;break;}
        if(rotated)++rotatedWorlds;
        if(rotated&&loggedWorlds.load()<16) {
            char message[420];std::snprintf(message,sizeof(message),
                "WORLD#%u: %.3f %.3f %.3f %.3f / %.3f %.3f %.3f %.3f / %.3f %.3f %.3f %.3f / %.3f %.3f %.3f %.3f",
                totalWorlds.load(),matrix[0],matrix[1],matrix[2],matrix[3],
                matrix[4],matrix[5],matrix[6],matrix[7],matrix[8],matrix[9],matrix[10],matrix[11],
                matrix[12],matrix[13],matrix[14],matrix[15]);
            Log(message);++loggedWorlds;
        }
    } else if(kind==1) {
        // Menu/UI passes feed identity projections; only non-identity
        // frusta reveal the 3D render path, so do not spend the budget there.
        ++totalProjections;
        const bool identity=IsIdentityView(matrix);
        if(identity)++identityProjections;
        if(!identity&&loggedProjections.load()<24) {
            char message[420];std::snprintf(message,sizeof(message),
                "PROJ: %.3f %.3f %.3f %.3f / %.3f %.3f %.3f %.3f / %.3f %.3f %.3f %.3f / %.3f %.3f %.3f %.3f",
                matrix[0],matrix[1],matrix[2],matrix[3],matrix[4],matrix[5],matrix[6],matrix[7],
                matrix[8],matrix[9],matrix[10],matrix[11],matrix[12],matrix[13],matrix[14],matrix[15]);
            Log(message);++loggedProjections;
        }
    }
    if(nativeMenuActive||NativeCameraKL::Installed()||MovieActive()||!settings.stereoMode||!VRBridge::Ready()||!eyeConfigValid)return false;
    const unsigned eye=currentEye.load();
    if(kind==1) { // projection
        const float* m=matrix;
        lastProjectionPerspective=std::fabs(m[11]-1.f)<1e-3f; // _34 == 1
        // Parse the engine's own depth range (game units). Gameplay frusta
        // reach thousands of units or use an infinite far plane (_33 == 1);
        // menus sit near ten.
        float parsedNear=0.f,parsedFar=0.f;
        if(lastProjectionPerspective) {
            if(std::fabs(m[10]-1.f)<=1e-3f&&m[14]<-0.001f) {
                parsedNear=-m[14];parsedFar=1e9f; // infinite far
            } else if(m[10]>1.0001f&&m[10]<1000.f&&m[14]!=0.f) {
                parsedNear=-m[14]/m[10];
                parsedFar=parsedNear*m[10]/(m[10]-1.f);
                if(!(parsedNear>0.005f&&parsedNear<50.f))parsedNear=0.f;
                if(!(parsedFar>parsedNear+1.f&&parsedFar<2000000.f))parsedFar=0.f;
            }
        }
        currentProjectionFar=parsedFar;
        projectionInfiniteFar=lastProjectionPerspective&&
            std::fabs(m[10]-1.f)<=1e-3f&&m[14]<-0.001f;
        // Remember the engine frustum: shader WVP constants are composed with
        // it and get the same delta via P^-1 * delta * PVR later in the frame.
        if(lastProjectionPerspective&&parsedFar>100.f) {
            std::memcpy(rawProjection,matrix,sizeof(Mat4));
            rawProjectionValid=true;
        } else rawProjectionValid=false;
        if(!lastProjectionPerspective||!UsableTangents(eyeFovCache[eye]))return false;
        // Only steer gameplay-scale frames; menus keep their own projection.
        if(parsedFar<=100.f)return false;
        const float scale=settings.worldUnitsPerMetre;
        const float nearUnits=std::max(1.f,settings.nearPlaneMm/1000.f*scale);
        float farUnits=600.f*scale;
        if(parsedFar>0.f)farUnits=std::max(parsedFar,farUnits);
        // The compositor rotates the submitted layer with live head pose;
        // render slightly wider than the headset frustum so edges survive.
        const float fovScale=settings.renderFovPercent/100.f;
        float fov[4];
        for(int side=0;side<4;++side)fov[side]=eyeFovCache[eye][side]*fovScale;
        if(!NativeCameraMath::ApplyEyeProjection(matrix,fov,nearUnits,farUnits,
                                                 projectionInfiniteFar))return false;
        std::memcpy(vrProjection,matrix,sizeof(Mat4));
        projectionAppliedThisFrame=true;
        ++projectionModified;
        return true;
    }
    // view/world: Glacier may leave D3DTS_VIEW identity and concatenate the
    // camera into per-object WORLD matrices (Contracts behaviour) or drive a
    // real VIEW (classic behaviour); both get the same camera-space delta.
    // Identity matrices and menu-scale passes (far<=100) pass through.
    if(kind!=0&&kind!=2)return false;
    if(IsIdentityView(matrix))return false;
    if(kind==2&&viewCarriesCamera)return false;
    if(!lastProjectionPerspective||currentProjectionFar<=100.f)return false;
    if(!ApplyCameraDelta(matrix,eye))return false;
    if(kind==0)++viewModified;else ++worldModified;
    return true;
}
// Shader constants may contain full, transposed WVP matrices. Identify them
// by factoring the known engine projection out, then insert the eye delta.
bool MainSceneViewport(IDirect3DDevice9* device) {
    if(!device)return false;
    if(!backbufferWidth||!backbufferHeight) {
        IDirect3DSurface9* backbuffer=nullptr;
        if(FAILED(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&backbuffer))||!backbuffer)return false;
        D3DSURFACE_DESC description{};
        const HRESULT result=backbuffer->GetDesc(&description);backbuffer->Release();
        if(FAILED(result))return false;
        backbufferWidth=description.Width;backbufferHeight=description.Height;
    }
    D3DVIEWPORT9 viewport{};
    if(FAILED(device->GetViewport(&viewport))||viewport.X||viewport.Y||
       viewport.Width!=backbufferWidth||viewport.Height!=backbufferHeight)return false;
    IDirect3DSurface9* target=nullptr;
    if(FAILED(device->GetRenderTarget(0,&target))||!target)return false;
    D3DSURFACE_DESC description{};const HRESULT result=target->GetDesc(&description);target->Release();
    // HDR scene targets may differ in format from the final backbuffer, but
    // shadow maps and auxiliary views must not receive headset steering.
    return SUCCEEDED(result)&&description.Width==backbufferWidth&&description.Height==backbufferHeight;
}

void ObserveShaderConstants(unsigned startRegister,const float* rows,unsigned registerCount) {
    NativeHandsKL::ObserveShaderConstants(rows,registerCount,startRegister);
    static unsigned dumpCount=0;
    if(registerCount>=4&&dumpCount<16) {
        char message[460];std::snprintf(message,sizeof(message),
            "VSConst#%u c%u count=%u: %.6g %.6g %.6g %.6g / %.6g %.6g %.6g %.6g / %.6g %.6g %.6g %.6g / %.6g %.6g %.6g %.6g",
            dumpCount,startRegister,registerCount,rows[0],rows[1],rows[2],rows[3],rows[4],rows[5],rows[6],rows[7],
            rows[8],rows[9],rows[10],rows[11],rows[12],rows[13],rows[14],rows[15]);
        Log(message);++dumpCount;
    }
}
bool InspectShaderConstants(IDirect3DDevice9*,unsigned start,const float* rows,unsigned count,void*) {
    if(!NativeCameraKL::Installed()||NativeCameraKL::MayRepairProjectionConstants())return true;
    ProfileTiming::Scope profile(ProfileTiming::ShaderObserve);
    ++totalConstants;NativeCameraKL::ObserveShaderConstants();
    ObserveShaderConstants(start,rows,count);return false;
}
bool OnShaderConstants(IDirect3DDevice9* device,unsigned startRegister,float* rows,unsigned registerCount,void*) {
    ProfileTiming::Scope profile(ProfileTiming::ShaderObserve);
    // Keep cinematic repair before the hand observer, as in the mutable path.
    ++totalConstants;NativeCameraKL::ObserveShaderConstants();
    if(NativeCameraKL::RepairProjectionConstants(startRegister,rows,registerCount))return true;
    ObserveShaderConstants(startRegister,rows,registerCount);
    if(nativeMenuActive||NativeCameraKL::Installed()||MovieActive()||!settings.stereoMode||!VRBridge::Ready()||!eyeConfigValid)return false;
    const unsigned eye=currentEye.load();
    Mat4 delta;
    if(!BuildCameraDelta(eye,delta))return false;
    Mat4 inverseProjection{};
    const bool knownProjection=rawProjectionValid&&currentProjectionFar>100.f&&
        StereoRenderMath::Invert(rawProjection,inverseProjection);
    bool any=false;
    bool viewportKnown=false,mainViewport=false;
    for(unsigned base=0;base+4<=registerCount;base+=1) {
        float* m=rows+base*4;
        float patched[16];
        bool matched=knownProjection&&StereoRenderMath::PatchProjectedAffine(m,inverseProjection,delta,vrProjection,patched);
        if(!matched) {
            StereoRenderMath::Perspective recovered;
            if(!StereoRenderMath::RecoverPerspective(m,recovered))continue;
            // Live K&L menu cameras use far=3000; mission cameras far=40000.
            // Until a native menu probe exists, keep short-range menu frusta
            // on the panel. Recover only full-scene, main-resolution passes.
            if(recovered.farUnits<10000.f||!recovered.hasWorldView)continue;
            if(!viewportKnown){mainViewport=MainSceneViewport(device);viewportKnown=true;}
            if(!mainViewport)continue;
            float fov[4];for(unsigned side=0;side<4;++side)
                fov[side]=eyeFovCache[eye][side]*settings.renderFovPercent/100.f;
            Mat4 eyeProjection;
            const float nearUnits=std::max(1.f,settings.nearPlaneMm/1000.f*settings.worldUnitsPerMetre);
            const float farUnits=std::max(recovered.farUnits,600.f*settings.worldUnitsPerMetre);
            if(!NativeCameraMath::ApplyEyeProjection(&eyeProjection[0][0],fov,nearUnits,farUnits,recovered.infiniteFar)||
               !StereoRenderMath::PatchProjectedAffine(m,recovered.inverse,delta,eyeProjection,patched))continue;
            std::memcpy(vrProjection,eyeProjection,sizeof(Mat4));
            projectionAppliedThisFrame=true;
            ++projectionModified;
            if(shaderProjectionsRecovered.fetch_add(1)<12) {
                char message[240];std::snprintf(message,sizeof(message),
                    "Shader scene projection c%u (%s): sx=%.5f sy=%.5f near=%.3f far=%.1f viewport=%ux%u.",
                    startRegister+base,recovered.transposed?"transposed":"row-major",
                    recovered.projection[0][0],recovered.projection[1][1],recovered.nearUnits,recovered.farUnits,
                    backbufferWidth,backbufferHeight);Log(message);
            }
        }
        std::memcpy(m,patched,sizeof(patched));
        base+=3; // the next window must not overlap the already patched matrix
        ++vsMatrixPatched;any=true;
    }
    if(any)SnapshotEye(eye);
    return any;
}

bool PrepareEyeMetadata(VRIPC::EyeMetadata& metadata) {
    if(ScenePanelActive())return SceneDecision(SceneGate::Panel);
    if(!settings.stereoMode)return SceneDecision(SceneGate::StereoOff);
    if(!VRBridge::Ready())return SceneDecision(SceneGate::HostMissing);
    if(!frameSnapshot.tracked)return SceneDecision(SceneGate::FrameNotTracked);
    if(NativeCameraKL::Installed()&&!frameSnapshot.nativeGpuApplied)return SceneDecision(SceneGate::NativeViewMissing);
    if(NativeCameraKL::Installed()&&!NativeCameraKL::CaptureReady(frameSnapshot.frameId/2,frameSnapshot.eye))return SceneDecision(SceneGate::NativeViewMissing);
    if(frameSnapshot.eye>1)return SceneDecision(SceneGate::EyeInvalid);
    if(frameSnapshot.frameId!=pairSequence.FrameId())return SceneDecision(SceneGate::PairMismatch);
    if(!projectionAppliedThisFrame)return SceneDecision(SceneGate::ProjectionMissing);
    if(VRBridge::RecenterAck()!=frameSnapshot.recenterAck)return SceneDecision(SceneGate::RecenterMismatch);
    if(GetTickCount()-frameSnapshot.tick>StereoPairPolicy::CompletedImageMaxAgeMs)return SceneDecision(SceneGate::TrackingStale);
    metadata.frameId=frameSnapshot.frameId;
    metadata.sensorTime=frameSnapshot.sensorTime;
    std::memcpy(metadata.headOrientation,frameSnapshot.orientation,sizeof(metadata.headOrientation));
    std::memcpy(metadata.headPosition,frameSnapshot.position,sizeof(metadata.headPosition));
    std::memcpy(metadata.fov,frameSnapshot.fov,sizeof(metadata.fov));
    metadata.poseValid=frameSnapshot.nativeScope?VRIPC::kScopeFrame:VRIPC::kSceneFrame;
    return SceneDecision(SceneGate::Ready);
}

void PublishInputContext() {
    // Both GPU and CPU capture paths pass through BeforeCapture. Publishing
    // only from the CPU callback left GPU-only sessions in unknown context,
    // allowing gameplay bindings to leak into native menus and cinematics.
    const bool ready=VRBridge::Ready();
    const auto scope=SceneScope();
    const auto nativeAction=NativeActionKL::Current();
    const auto nativeCinema=NativeCinematicKL::Snapshot(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)),ReadNativeBytes);
    const bool firstPersonSkip=ready&&NativeCinematicKL::FirstPersonSkip(nativeCinema,nativeAction,nativeAction.player);
    VRBridge::SetGameplayState(false,false,false,false,ready&&scope.valid&&scope.active,false,false,0,firstPersonSkip,
        ready&&nativeSimulationReady.load()&&NativeWeaponKL::ReloadInstalled());
    const bool steering=ready&&GetTickCount()-lastSceneTick.load()<500;
    const auto action=SceneAction();
    const bool cinematic=MovieActive()||frameSnapshot.nativeCinematic||NativeCameraKL::CinematicActive()||
        NativeCinematicKL::ActorCameraAction(action,action.player);
    const LONG context=MonoFrame::InputContext(ready,nativeMenuActive,NativeLoadingActive(),cinematic,steering);
    NativeWeaponKL::SetWeaponCalibrationContext(context==VRIPC::kInputGame?NativeWeaponKL::CalibrationContext::Gameplay:
        (context==VRIPC::kInputLoading||context==VRIPC::kInputCinematic)?NativeWeaponKL::CalibrationContext::Transition:
        NativeWeaponKL::CalibrationContext::Unknown);
    if(context!=VRIPC::kInputGame) {
        // A missing fresh eye after a renderer stall suspends the pose; it is
        // not a deliberate UI transition that releases an owned physical grip.
        if(context!=VRIPC::kInputUnknown)NativeHandsKL::InvalidateSupportGrip();
        aimPairTarget.Reset();laserPairBeam.Reset();NativeGrenadeVisualKL::ClearFrame();NativeWeaponKL::ClearScopedDrawFrame();NativeWeaponKL::InvalidateHeadGaze();NativeTeamCommandsKL::InvalidateHeadRay();
        NativeWeaponKL::InvalidateScopeGesture();
    }
    NativeWeaponKL::SetScopeGestureContext(context==VRIPC::kInputGame&&!GrenadeVisualsHidden());
    NativeWeaponKL::SetTrackedReloadContext(context==VRIPC::kInputGame);
    NativeWeaponKL::SetThrowbackContext(context==VRIPC::kInputGame);
    NativeCoverKL::SetActionClimbContext(context==VRIPC::kInputGame);
    DWORD foregroundPid=0;GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);
    const bool focused=BackgroundDiagnostic::Focused(foregroundPid==GetCurrentProcessId());
    const bool activeGame=context==VRIPC::kInputGame&&focused&&activeSample.valid&&activeSample.mounted;
    NativeTutorialKL::SetGameplayContext(activeGame&&!VRBridge::TeamHudVisible()&&!VRBridge::WeaponMenuVisible());
    NativeLedgeHintKL::SetGameplayContext(activeGame&&settings.gameHints&&!VRBridge::TeamHudVisible()&&!VRBridge::WeaponMenuVisible());
    NativeObjectiveVisibilityKL::SetContext(activeGame,VRBridge::TeamHudVisible());
    if(context!=VRIPC::kInputGame)VRBridge::SetTeamReceiver({});
    VRBridge::SetInputContext(context);
}
NativeVisualTraceKL::Scene VisualScene(){
    NativeVisualTraceKL::Scene s;s.frame=frameSnapshot.frameId;s.epoch=renderEpoch;s.eye=frameSnapshot.eye;s.tick=GetTickCount();
    s.tracked=frameSnapshot.tracked;s.applied=frameSnapshot.nativeGpuApplied&&projectionAppliedThisFrame;
    s.cinematic=frameSnapshot.nativeCinematic||frameSnapshot.actorCinematic;s.scope=frameSnapshot.nativeScope;
    s.gameplay=!nativeMenuActive&&!NativeLoadingActive();
    if(NativeVisualTraceKL::Enabled()){const auto rig=NativeHandsKL::Snapshot();s.support=rig.supportGripSampleValid&&rig.supportRawGrip>=0.5f;}
    return s;
}
void BeforeCapture(IDirect3DDevice9* device,IDirect3DSurface9* backbuffer,void*) {
    NativeMirrorBumpKL::BeforeCapture(device);
    NativeVisualTraceKL::BeforeCapture(device,backbuffer,VisualScene());
    if(bufferFlushDevice!=device){NativeBufferFlushKL::SetDevice(device);bufferFlushDevice=device;}
    NativeGrenadeVisualKL::ClearFrame();
    NativeWeaponKL::ClearScopedDrawFrame(true);
    // Defend destinations use their original native sprite/atlas, through
    // the same actual-eye route as accepted recipient icons. No custom glyph.
    NativeAimRenderKL::Frame laser;bool drawLaser=false;
    const unsigned laserEye=std::min(frameSnapshot.eye,1u);
    if(settings.laserSight&&(visualBenchMask.load(std::memory_order_relaxed)&BackgroundDiagnostic::BenchLaser)){
        if(frameSnapshot.nativeGpuApplied&&!frameSnapshot.nativeCinematic)++laserEyeAttempts[laserEye];
        bool contact=false;
        if(CurrentLaserFrame(laser,contact)){drawLaser=true;++laserEyeAimReady[laserEye];
            if(contact)++laserEyeContacts[laserEye];}
    }
    // The consumer finished its deferred draws; retire its native arm context.
    NativeRopeKL::ClearFrame();NativeCarriedPersonVisibilityKL::ClearFrame();NativeCameraKL::FinishScene();NativeEffectsKL::SetCinematicViewport(false);
    nativeMenuActive=ReadNativeMenu();
    MovieActive(true); // classify a movie started after this frame's camera packet before publishing metadata
    PublishInputContext();
    D3DSURFACE_DESC description{};
    if(SUCCEEDED(backbuffer->GetDesc(&description))) {
        backbufferWidth=description.Width;backbufferHeight=description.Height;
    }
    VRIPC::EyeMetadata metadata{};
    const auto shownTeam=NativeHudKL::PresentedTeamRoster(handHudCamera.player,handHudCamera.pairId,frameSnapshot.eye);
    TeamCommandIPC::ReceiverCertificate commandReceiver;
    if(PrepareEyeMetadata(metadata)){
        if(drawLaser){
            if(HandsRenderer::DrawAim(device,backbuffer,laser))++laserEyeDrawn[laserEye];
            else ++laserEyeDrawRejected[laserEye];
        }
        HandsRenderer::Draw(device,backbuffer,metadata,eyeOffsetCache[currentEye.load()]);
        if(shownTeam.valid&&VRBridge::TeamHudVisible()) {
            const auto now=GetTickCount();
            const auto receiver=NativeTeamCommandsKL::ReceiverSnapshot(now);
            bool matched=receiver.valid&&receiver.player==shownTeam.player;
            if(receiver.all)matched=matched&&shownTeam.allSelected;
            else {
                bool selected=false;
                for(unsigned i=0;i<shownTeam.count;++i)
                    if(shownTeam.members[i].selected&&shownTeam.members[i].nativeId==receiver.nativeId)selected=true;
                matched=matched&&selected;
            }
            if(matched)commandReceiver=receiver;
        }
    } else if(drawLaser)++laserEyeMetadataRejected[laserEye];
    VRBridge::SetTeamReceiver(commandReceiver);
}

CaptureFramePolicy::Action CaptureDisposition(bool prepared) {
    // Intent comes from this image's already latched native camera, never a
    // guessed startup/menu state. Keep the first scene's GPU generation alive
    // across capture backpressure or a rejected render-pair retry.
    const bool nativeScene=NativeCameraKL::Installed()&&
        (frameSnapshot.tracked||frameSnapshot.nativeGpuApplied||projectionAppliedThisFrame);
    const bool panel=ScenePanelActive();
    const bool nativeSceneIntent=captureSceneIntent.Observe(nativeScene,settings.stereoMode,panel,VRBridge::Ready());
    return CaptureFramePolicy::Select(prepared,settings.stereoMode,lastSceneTick.load()!=0,panel,nativeSceneIntent);
}

// KL2 groups both eyes before the monitor swap. KL1 builds a fresh queue
// for each eye through its complete view wrapper and joins the native worker
// before reusing the queue slot. Simulation remains outside this wrapper.
namespace NativeStereo {
using Render=std::uint32_t(__thiscall*)(void*);
using Join=void(__thiscall*)(void*);
Render original=nullptr;
Join join=nullptr;
std::uintptr_t image=0;
unsigned limit=0;
std::atomic<bool> installed{false};
struct State {
    std::uint32_t renderer=0,viewOwner=0,system=0,simulation=0,queue=0;
    bool separate=false,pending=false;
};
struct Eye {
    VRIPC::EyeMetadata meta{};
    NativeCameraMath::Vec3 position{};
    std::uintptr_t player=0,camera=0;
    long recenter=0;
};
struct Attempt {
    State before{},between{},after{};
    Eye eyes[2]{};
    std::atomic<unsigned> phase{0},published{0};
    std::atomic<bool> suppressed{false};
    std::uint64_t expected=0;
};
struct Result {
    State before{},between{},after{};
    Eye eyes[2]{};
    bool second=false,complete=false,suppressed=false;
    unsigned published=0;
    std::uint32_t leftGate=0,rightGate=0;
    std::uint64_t leftUs=0,rightUs=0;
};
std::atomic<Attempt*> active{nullptr};
SRWLOCK resultsLock=SRWLOCK_INIT;
Result last;
unsigned attempts=0,completed=0,misses=0,skippedPresents=0,singlePresentPairs=0,consecutiveMisses=0;
bool faulted=false;
std::uint64_t startedUs=0,completedUs=0,totalWorkUs=0,firstFrameId=0;

bool ReadState(void* view,State& out) {
    const auto v=reinterpret_cast<std::uintptr_t>(view);
    const auto read=[](std::uintptr_t at,auto& value){return ReadNativeBytes(at,&value,sizeof(value));};
    std::uint32_t vtable=0,scratch=0;
    constexpr unsigned first=0xA7F74,pendingOffset=0xA7F94-first,queueOffset=0xA7FA0-first;
    unsigned char renderFlags[queueOffset+sizeof(out.queue)]{};
    if(!v||!read(v+0x2FC,out.renderer)||!out.renderer||!read(out.renderer,vtable)||vtable!=image+0x540DC4||
       !ReadNativeBytes(out.renderer+first,renderFlags,sizeof(renderFlags))||renderFlags[0]>1||renderFlags[pendingOffset]>1||
       !read(out.renderer+0x7F594,scratch)||scratch||
       !read(v+0x134C,out.viewOwner)||!out.viewOwner||
       !read(image+0x6875F0,out.system)||!out.system||!read(out.system+0x44,out.simulation))return false;
    std::memcpy(&out.queue,renderFlags+queueOffset,sizeof(out.queue));
    out.separate=renderFlags[0];out.pending=renderFlags[pendingOffset];
    return out.queue<=1&&(!out.pending||out.separate);
}
bool SameWorld(const State& before,const State& after) {
    return before.renderer==after.renderer&&before.viewOwner==after.viewOwner&&before.system==after.system&&
        before.simulation==after.simulation&&before.separate==after.separate&&!after.pending;
}
bool WaitForView(void* view,const State& expected,State& result) {
    if(expected.separate)join(reinterpret_cast<void*>(expected.renderer));
    return ReadState(view,result)&&SameWorld(expected,result);
}
bool RenderingRight(){
    const auto* work=active.load(std::memory_order_acquire);
    return work&&work->phase.load(std::memory_order_acquire)==1;
}
bool AcceptRight(const VRIPC::EyeMetadata& meta) {
    const auto* work=active.load(std::memory_order_acquire);
    if(!work||work->phase.load(std::memory_order_acquire)!=1)return true;
    if(work->published.load(std::memory_order_acquire)!=1)return false;
    const auto& left=work->eyes[0];
    const auto delta=NativeCameraMath::Subtract(left.position,frameSnapshot.worldEye);
    return frameSnapshot.eye==1&&StereoPairPolicy::Compatible(left.meta,meta)&&
        left.player==frameSnapshot.nativePlayer&&left.camera==frameSnapshot.nativeSourceCamera&&
        left.recenter==frameSnapshot.recenterAck&&NativeCameraMath::Dot(delta,delta)>.01f;
}
void Observe(const VRIPC::EyeMetadata& meta,unsigned eye) {
    auto* work=active.load(std::memory_order_acquire);
    if(!work||eye>1||work->phase.load(std::memory_order_acquire)!=eye||meta.frameId!=work->expected+eye||
       meta.poseValid!=VRIPC::kSceneFrame||!frameSnapshot.nativePlayer||!frameSnapshot.nativeSourceCamera||
       frameSnapshot.nativeCinematic||frameSnapshot.actorCinematic||frameSnapshot.nativeCarried||frameSnapshot.nativeMounted||
       (work->published.load(std::memory_order_relaxed)&(1u<<eye)))return;
    work->eyes[eye]={meta,frameSnapshot.worldEye,frameSnapshot.nativePlayer,frameSnapshot.nativeSourceCamera,frameSnapshot.recenterAck};
    work->published.fetch_or(1u<<eye,std::memory_order_release);
}
bool SkipMonitorSwap(std::uintptr_t caller) {
    auto* work=active.load(std::memory_order_acquire);
    return work&&caller==image+0xB61D5&&work->phase.load(std::memory_order_acquire)==0&&
        work->published.load(std::memory_order_acquire)==1&&!work->suppressed.exchange(true,std::memory_order_acq_rel);
}
std::uint32_t __fastcall Hook(void* view,void*) {
    if(!installed.load(std::memory_order_acquire)||active.load(std::memory_order_acquire)||!VRBridge::Ready()||
       sceneFrames.load(std::memory_order_relaxed)<80||
       reinterpret_cast<std::uintptr_t>(__builtin_return_address(0))!=image+0x63F4D||!VRBridge::GPUSceneReady())
        return original(view);

    AcquireSRWLockShared(&resultsLock);
    const bool armed=!faulted&&(!limit||completed<limit);
    ReleaseSRWLockShared(&resultsLock);
    Attempt work;
    if(!armed||!ReadState(view,work.before))return original(view);
    if(work.before.pending){
        if(!WaitForView(view,work.before,work.between))return original(view);
        work.before=work.between;
    }
    nativeMenuActive=ReadNativeMenu();
    if(pairSequence.Eye()!=0||ScenePanelActive()||NativeCameraKL::CinematicActive()||SceneScope().active)
        return original(view);

    work.expected=pairSequence.FrameId();
    const auto epoch=renderEpoch.load();
    const auto startUs=FrameMetrics::Microseconds(0);
    const auto start=FrameMetrics::Counter();
    active.store(&work,std::memory_order_release);
    const auto originalResult=original(view);
    Result result;
    const bool firstJoined=WaitForView(view,work.before,work.between);
    result.leftUs=FrameMetrics::Microseconds(start);
    result.leftGate=lastSceneGate.load();

    bool intact=firstJoined;
    if(firstJoined&&work.published.load(std::memory_order_acquire)==1&&
       pairSequence.FrameId()==work.expected+1&&renderEpoch.load()==epoch+1&&
       work.between.queue!=work.before.queue&&!ScenePanelActive()){
        result.second=true;
        work.phase.store(1,std::memory_order_release);
        const auto rightStart=FrameMetrics::Counter();
        original(view);
        intact=WaitForView(view,work.between,work.after);
        result.rightUs=FrameMetrics::Microseconds(rightStart);
        result.rightGate=lastSceneGate.load();
        result.complete=intact&&work.published.load(std::memory_order_acquire)==3&&
            pairSequence.FrameId()==work.expected+2&&renderEpoch.load()==epoch+2&&work.after.queue==work.before.queue;
    }
    // Never complete a successful first image in a different world update.
    if(firstJoined&&work.published.load(std::memory_order_acquire)&&!result.complete&&pairSequence.Eye()==1){
        pairSequence.Restart();currentEye=0;
    }
    active.store(nullptr,std::memory_order_release);
    result.published=work.published.load(std::memory_order_acquire);
    result.suppressed=work.suppressed.load(std::memory_order_acquire);
    result.before=work.before;result.between=work.between;result.after=work.after;
    for(unsigned i=0;i<2;++i)if(result.published&(1u<<i))result.eyes[i]=work.eyes[i];

    AcquireSRWLockExclusive(&resultsLock);
    last=result;++attempts;
    if(result.suppressed)++skippedPresents;
    if(result.complete){
        if(!completed){startedUs=startUs;firstFrameId=work.expected;}
        ++completed;consecutiveMisses=0;
        if(result.suppressed)++singlePresentPairs;
        completedUs=FrameMetrics::Microseconds(0);
        totalWorkUs+=result.leftUs+result.rightUs;
    }else {++misses;if(result.second||result.suppressed)++consecutiveMisses;}
    if(!intact||consecutiveMisses>=4)faulted=true;
    ReleaseSRWLockExclusive(&resultsLock);
    return originalResult;
}
void Install(const wchar_t* ini) {
    if(BackgroundDiagnostic::Active()){
        wchar_t pairs[8]{};
        if(GetEnvironmentVariableW(L"KL1_BACKGROUND_SAME_UPDATE",pairs,8)){
            wchar_t* end=nullptr;const auto count=std::wcstoul(pairs,&end,10);
            if(end&&!*end&&count>=1&&count<=8192)limit=count;
        }
    }
    if(!limit&&!settings.nativeStereoViews)return;
    image=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    constexpr unsigned char wrapper[]{
        0x56,0x8b,0xf1,0x8b,0x8e,0xfc,2,0,0,0x8b,1,0x8b,0x50,0x44,0xff,0xd2,
        0x8b,0x8e,0xfc,2,0,0,0x8b,1,0x8b,0x50,0x40,0x56,0xff,0xd2,
        0x8b,0x8e,0xfc,2,0,0,0x8b,1,0x8b,0x50,0x48,0x5e,0xff,0xe2
    };
    constexpr unsigned char workerJoin[]{
        0x56,0x8b,0xf1,0x80,0xbe,0x74,0x7f,0x0a,0,0,0x74,0x1f,0x80,0xbe,0x94,0x7f,
        0x0a,0,0,0x74,0x16,0x8b,0x86,0x80,0x7f,0x0a,0,0x6a,0xff,0x50,0xff,0x15
    };
    constexpr unsigned char swapCall[]{0x8b,0x0b,0x8b,0x51,0x0c,0x6a,0,0x6a,0,0x6a,0,0x6a,0,0x6a,0,0x53,0xff,0xd2};
    constexpr unsigned char viewCall[]{0x8b,0x16,0x8b,0x82,0xac,0,0,0,0x57,0x8b,0xce,0xff,0xd0};
    const auto match=[](std::uintptr_t address,const unsigned char* expected,std::size_t count){
        unsigned char bytes[64]{};
        return count<=sizeof(bytes)&&ReadNativeBytes(image+address,bytes,count)&&!std::memcmp(bytes,expected,count);
    };
    std::uint32_t slots[3]{};
    if(!ReadNativeBytes(image+0x540E04,slots,sizeof(slots))||
       slots[0]!=image+0xB7140||slots[1]!=image+0xB7530||slots[2]!=image+0xB4870||
       !match(0x63E70,wrapper,sizeof(wrapper))||!match(0xB4790,workerJoin,sizeof(workerJoin))||
       !match(0xB61C3,swapCall,sizeof(swapCall))||!match(0x63F40,viewCall,sizeof(viewCall))){
        Log("Native stereo views unavailable for this render layout.");return;
    }
    join=reinterpret_cast<Join>(image+0xB4790);
    void* target=reinterpret_cast<void*>(image+0x63E70);
    if(MH_CreateHook(target,reinterpret_cast<void*>(&Hook),reinterpret_cast<void**>(&original))!=MH_OK)return;
    if(MH_EnableHook(target)!=MH_OK){MH_RemoveHook(target);return;}
    // A/B can retain the physical swaps while testing paired stereo.
    if(GetPrivateProfileIntW(L"VR",L"NativeStereoSinglePresent",1,ini))D3D9Capture::SetSkipSwapPresent(SkipMonitorSwap);
    installed=true;
    Log("Native stereo views armed for verified gameplay view and GPU transport.");
}
void WriteStatus(FILE* f) {
    if(!installed.load())return;
    AcquireSRWLockShared(&resultsLock);
    const auto r=last;const unsigned total=attempts,passed=completed,failed=misses,skipped=skippedPresents,single=singlePresentPairs;
    const bool stopped=faulted;
    const auto elapsed=completedUs-startedUs,work=totalWorkUs,first=firstFrameId;
    ReleaseSRWLockShared(&resultsLock);
    std::fprintf(f,",\"sameUpdateProbe\":{\"limit\":%u,\"attempts\":%u,\"completePairs\":%u,\"stoppedOnMismatch\":%s,"
        "\"secondRendered\":%s,\"lastComplete\":%s,\"singlePresentPairs\":%u,\"skippedPresents\":%u,\"misses\":%u,\"lastSkippedPresent\":%s,"
        "\"threaded\":%s,\"simulationTicks\":[%u,%u,%u],\"queueIndices\":[%u,%u,%u],"
        "\"callerRva\":%u,\"sceneGates\":[%u,%u],\"elapsedUs\":%llu,\"renderWorkUs\":%llu,\"firstFrameId\":%llu,"
        "\"leftUs\":%llu,\"rightUs\":%llu,\"eyes\":[",
        limit,total,passed,stopped?"true":"false",r.second?"true":"false",r.complete?"true":"false",single,skipped,failed,
        r.suppressed?"true":"false",r.before.separate?"true":"false",
        r.before.simulation,r.between.simulation,r.after.simulation,r.before.queue,r.between.queue,r.after.queue,
        0x63F4Du,r.leftGate,r.rightGate,static_cast<unsigned long long>(elapsed),static_cast<unsigned long long>(work),
        static_cast<unsigned long long>(first),static_cast<unsigned long long>(r.leftUs),static_cast<unsigned long long>(r.rightUs));
    for(unsigned i=0;i<2;++i){
        const auto& e=r.eyes[i];
        std::fprintf(f,"%s{\"published\":%s,\"frameId\":%llu,\"sensorTime\":%.9f,\"worldEye\":[%.4f,%.4f,%.4f]}",
            i?",":"",r.published&(1u<<i)?"true":"false",static_cast<unsigned long long>(e.meta.frameId),
            e.meta.sensorTime,e.position.x,e.position.y,e.position.z);
    }
    std::fprintf(f,"]}");
}
}

int OnGPUFrame(IDirect3DDevice9* device,IDirect3DSurface9* back,unsigned width,unsigned height,
               std::uint64_t frameId,std::uint32_t tick,void*) {
    if(!VRBridge::Ready()){
        if(settings.captureWithoutHost)return -1;
        ++absentConsumerReadbacksAvoided;return 0;
    }
    VRIPC::EyeMetadata metadata{};metadata.width=width;metadata.height=height;metadata.stride=width*4;metadata.tick=tick;
    const bool prepared=PrepareEyeMetadata(metadata);
    const auto disposition=CaptureDisposition(prepared);
    D3D9Capture::SetFrameTraceSource(frameSnapshot.frameId,frameSnapshot.eye,
        disposition==CaptureFramePolicy::Action::Scene?metadata.poseValid:disposition==CaptureFramePolicy::Action::Panel?(NativeLoadingActive()?VRIPC::kContextFrame:VRIPC::kUiFrame):0,lastSceneGate.load());
    if(disposition==CaptureFramePolicy::Action::Scene) {
        if(!NativeStereo::AcceptRight(metadata)){++incompleteFrames;return 0;}
        const int result=VRBridge::PublishGPU(device,back,frameSnapshot.nativeScope?3u:1u<<frameSnapshot.eye,metadata);
        if(result==1) {
            publishedThisFrame.store(true);lastSceneTick.store(GetTickCount());++sceneFrames;
            NativeStereo::Observe(metadata,frameSnapshot.eye);
            if(frameSnapshot.nativeScope)++scopePublished;
        }
        return result<0&&NativeStereo::RenderingRight()?0:result;
    }
    if(disposition==CaptureFramePolicy::Action::Hold){
        ++incompleteFrames;++incompleteReadbacksAvoided;return CaptureFramePolicy::HeldGPUResult();
    }
    metadata.frameId=frameId;metadata.poseValid=NativeLoadingActive()?VRIPC::kContextFrame:VRIPC::kUiFrame;
    const int result=VRBridge::PublishGPU(device,back,3,metadata);
    if(result==1)++panelFrames;
    return result;
}
void BeforeCaptureReset(IDirect3DDevice9*) {
    captureSceneIntent.Reset();
    reflectionContinuity.Reset();
    NativeVisualTraceKL::Reset();
    NativeBufferFlushKL::SetDevice(nullptr);bufferFlushDevice=nullptr;
    NativeEffectsKL::InvalidateDevice();
    NativeSkinLightingKL::InvalidateDevice();
    NativeMirrorBumpKL::InvalidateDevice();
    NativeCarriedPersonVisibilityKL::InvalidateDevice();
    NativeReflectionProbeKL::Reset();
    NativeWeaponKL::InvalidateMaterialProbe();
    NativeHandsKL::InvalidateMaterialProbe();
    CrashDiagnostics::Breadcrumb("device_reset",pairSequence.FrameId());
    // Settings can Reset before another Present. Retire the cached GPU eye
    // now, so the first new-resolution draw cannot reuse the old projection.
    NativeMirrorBumpKL::RetireFrameTargets();
    NativeCameraKL::FrameBoundary();
    NativeHudKL::InvalidateDevice();
    HandsRenderer::InvalidateDevice();handHudCamera={};
    VRBridge::InvalidateGPU();pairSequence.Restart();currentEye=0;frameSnapshot={};activeSample={};
    NativeRopeKL::ClearFrame();NativeCarriedPersonVisibilityKL::ClearFrame();
    eyeConfigValid=false;publishedThisFrame=false;projectionAppliedThisFrame=false;lastSceneTick=0;
    actorCinematicApplied=false;++renderEpoch;
    headPositionReference.Reset();cinematicLook.Reset();vehicleCameraTracking.Reset();vehicleCameraScope.Reset();rappelLook.Reset();rappelAttachment.Reset();rappelOrientationLocked=false;rappelAnatomicalUpLocked=false;rappelBodyPhase=0;
    backbufferWidth=backbufferHeight=0;nativeMovementYawValid=false;VRBridge::SetMovementYaw(0.f,false);aimPairTarget.Reset();laserPairBeam.Reset();NativeGrenadeVisualKL::ClearFrame();NativeWeaponKL::ClearScopedDrawFrame();NativeWeaponKL::InvalidateHeadGaze();NativeTeamCommandsKL::InvalidateHeadRay();NativeWeaponKL::SetScopeGestureContext(false);NativeWeaponKL::SetThrowbackContext(false);
}

bool OnFrame(const std::uint8_t* bgra,bool forceOpaque,std::uint32_t width,std::uint32_t height,
             std::uint32_t stride,std::uint64_t frameId,std::uint32_t tick,void*) {
    static std::atomic<bool> sizeWarned{false};
    if(!VRBridge::Ready())return false;
    if(width>VRIPC::kMaxWidth||height>VRIPC::kMaxHeight) {
        if(!sizeWarned.exchange(true)) {
            char message[160];std::snprintf(message,sizeof(message),
                "Frame %ux%u exceeds the VR transport limit %ux%u; lower the game resolution.",
                width,height,VRIPC::kMaxWidth,VRIPC::kMaxHeight);
            Log(message);
        }
        return false;
    }
    VRIPC::EyeMetadata metadata{};
    metadata.width=width;metadata.height=height;metadata.stride=width*4;
    metadata.tick=tick;
    const unsigned eye=frameSnapshot.tracked?frameSnapshot.eye:currentEye.load();
    const bool prepared=PrepareEyeMetadata(metadata);
    const auto disposition=CaptureDisposition(prepared);
    D3D9Capture::SetFrameTraceSource(frameSnapshot.frameId,eye,prepared?metadata.poseValid:
        disposition==CaptureFramePolicy::Action::Hold?0:NativeLoadingActive()?VRIPC::kContextFrame:VRIPC::kUiFrame,lastSceneGate.load());
    if(prepared) {
        // A GPU stereo attempt never mixes a previously queued GPU eye with
        // a CPU half when the second transfer temporarily falls back.
        if(NativeStereo::RenderingRight()) {++incompleteFrames;return false;}
        if(frameSnapshot.nativeScope) {
            if(!VRBridge::Publish(0,metadata,bgra,stride,forceOpaque)||!VRBridge::Publish(1,metadata,bgra,stride,forceOpaque))return false;
        } else if(!VRBridge::Publish(eye,metadata,bgra,stride,forceOpaque))return false;
        publishedThisFrame.store(true);
        lastSceneTick.store(GetTickCount());
        ++sceneFrames;
        if(frameSnapshot.nativeScope)++scopePublished;
        return true;
    }
    // A tracked scene needs both the camera delta and the projection; while
    // steering is expected, hold broken frames instead of flashing a panel.
    if(disposition==CaptureFramePolicy::Action::Hold) {
        ++incompleteFrames;
        return false;
    }
    // Flat panel fallback publishes the same frame to both eyes.
    metadata.frameId=frameId;metadata.poseValid=NativeLoadingActive()?VRIPC::kContextFrame:VRIPC::kUiFrame;
    if(!VRBridge::Publish(0,metadata,bgra,stride,forceOpaque)||
       !VRBridge::Publish(1,metadata,bgra,stride,forceOpaque))return false;
    ++panelFrames;
    return true;
}

void OnFrameBoundary(void*) {
    if(BackgroundDiagnostic::VisualBench()){
        const auto mask=BackgroundDiagnostic::BenchFeatures(BackgroundDiagnostic::Age());
        if(visualBenchMask.exchange(mask,std::memory_order_relaxed)!=mask)
            NativeHudKL::SetReticleEnabled((mask&BackgroundDiagnostic::BenchReticle)!=0);
    }
    NativeVisualTraceKL::FrameBoundary(VisualScene());
    NativeHandsKL::SetDrawContextDiagnostics(NativeVisualTraceKL::Active());
    CrashDiagnostics::Breadcrumb("frame_boundary",pairSequence.FrameId());
    // D3D Present/EndScene calls this on the GPU consumer. Pair/sample state
    // therefore advances with the executed image, independently of CPU queues.
    // Advance to the other eye only when the finished frame was a complete,
    // published stereo view; otherwise retry the same eye.
    const bool complete=publishedThisFrame.exchange(false)&&frameSnapshot.tracked&&
                        projectionAppliedThisFrame&&frameSnapshot.frameId==pairSequence.FrameId();
    const bool rejectedHandsPair=NativeCameraKL::Installed()&&!NativeHandsKL::RenderPairCaptureReady();
    const bool movementViewApplied=frameSnapshot.tracked&&projectionAppliedThisFrame&&
        frameSnapshot.frameId==pairSequence.FrameId();
    const auto movementRecenterAck=frameSnapshot.recenterAck;
    const bool movementLookLocked=frameSnapshot.rappel;
    const bool nativeAxes=frameSnapshot.nativeMovementAxes;
    const bool nativeCarried=frameSnapshot.nativeCarried;
    const bool nativeMounted=frameSnapshot.nativeMounted;
    const bool completedScope=frameSnapshot.nativeScope;
    const bool reflectionSpecial=completedScope||frameSnapshot.nativeCinematic||frameSnapshot.actorCinematic;
    const bool reflectionScene=movementViewApplied&&!reflectionSpecial;
    const auto reflectionPlayer=frameSnapshot.nativePlayer,reflectionCamera=frameSnapshot.nativeSourceCamera;
    frameSnapshot=EyeSnapshot{};
    NativeRopeKL::ClearFrame();NativeCarriedPersonVisibilityKL::ClearFrame();
    handHudCamera={};
    actorCinematicApplied=false;++renderEpoch;
    projectionAppliedThisFrame=false;
    NativeCameraKL::FrameBoundary();
    CaptureFramePolicy::ReflectionWorld reflectionWorld;
    nativeMenuActive=ReadNativeMenu(reflectionHistoryEnabled?&reflectionWorld:nullptr);
    const bool ready=VRBridge::Ready();
    const bool panel=ready&&ScenePanelActive();
    bool keepReflection=reflectionScene&&ready&&!panel;
    if(reflectionHistoryEnabled){
        const auto now=GetTickCount();
        reflectionWorld.recenter=VRBridge::RecenterAck();
        if(keepReflection&&reflectionWorld.player==reflectionPlayer&&reflectionWorld.recenter==movementRecenterAck){
            reflectionWorld.camera=reflectionCamera;
            keepReflection=reflectionContinuity.Qualified(reflectionWorld,now);
        }else if(ready&&!panel&&!reflectionSpecial&&!movementViewApplied&&
                 captureSceneIntent.observed&&activeSample.valid&&activeSample.mounted&&
                 reflectionWorld.recenter==activeRecenterAck&&
                 lastSceneGate.load()==static_cast<unsigned>(SceneGate::FrameNotTracked)&&
                 reflectionContinuity.CanSkip(now)){
            const auto action=SceneAction();
            std::uint32_t camera=0;
            // Local player's native camera, as checked by NativeCameraKL.
            if(reflectionWorld.player&&!SceneScope().active&&!NativeCameraKL::CinematicActive()&&
               !NativeCinematicKL::ActorCameraAction(action,action.player)&&
               ReadNativeBytes(reflectionWorld.player+0x9A0,&camera,sizeof(camera)))reflectionWorld.camera=camera;
            keepReflection=reflectionContinuity.Skip(reflectionWorld,now);
            if(keepReflection)++reflectionHistoryKeptAfterMiss;
        }else{
            reflectionContinuity.Reset();
            keepReflection=false;
        }
    }
    const bool keepFloor=ready&&!nativeMenuActive&&!NativeLoadingActive();
    NativeMirrorBumpKL::RetireFrameTargets(keepReflection,keepFloor&&!panel,keepFloor);
    if(!ready) {
        captureSceneIntent.Reset();
        aimPairTarget.Reset();laserPairBeam.Reset();NativeGrenadeVisualKL::ClearFrame();NativeWeaponKL::ClearScopedDrawFrame();NativeWeaponKL::InvalidateHeadGaze();NativeTeamCommandsKL::InvalidateHeadRay();
        NativeWeaponKL::SetScopeGestureContext(false);
        NativeWeaponKL::SetThrowbackContext(false);
        pairSequence.Restart();
        referenceValid=false;
        headPositionReference.Reset();
        cinematicLook.Reset();vehicleCameraTracking.Reset();vehicleCameraScope.Reset();
        rappelAttachment.Reset();rappelOrientationLocked=false;rappelAnatomicalUpLocked=false;rappelBodyPhase=0;
        activeSample={};eyeConfigValid=false;
    } else if(panel) {
        aimPairTarget.Reset();laserPairBeam.Reset();NativeGrenadeVisualKL::ClearFrame();NativeWeaponKL::ClearScopedDrawFrame();NativeWeaponKL::InvalidateHeadGaze();NativeTeamCommandsKL::InvalidateHeadRay();
        NativeWeaponKL::SetScopeGestureContext(false);
        NativeWeaponKL::SetThrowbackContext(false);
        pairSequence.Restart();activeSample={};headPositionReference.Reset();cinematicLook.Reset();vehicleCameraTracking.Reset();
    } else {
        StereoPairPolicy::FinishImage(pairSequence,complete,completedScope,rejectedHandsPair);
        // A recenter or long stall invalidates a half-pair. Never publish its
        // right eye using a different reference, eye configuration or sample.
        if(pairSequence.Eye()==1&&(!activeSample.valid||
           GetTickCount()-activeSample.tick>StereoPairPolicy::TrackingStartMaxAgeMs||VRBridge::RecenterAck()!=activeRecenterAck))
            pairSequence.Restart();
        // The shader fallback still needs a boundary sample. Native camera
        // rendering refreshes it at its actual first camera use as well.
        if(pairSequence.Eye()==0)RefreshPairTracking();
    }
    currentEye.store(pairSequence.Eye());
    if(!activeSample.valid||!activeSample.mounted) {aimPairTarget.Reset();laserPairBeam.Reset();NativeGrenadeVisualKL::ClearFrame();NativeWeaponKL::ClearScopedDrawFrame();NativeWeaponKL::InvalidateHeadGaze();NativeTeamCommandsKL::InvalidateHeadRay();NativeWeaponKL::SetScopeGestureContext(false);NativeWeaponKL::SetThrowbackContext(false);}
    if(activeSample.valid&&!activeSample.mounted) {referenceValid=false;headPositionReference.Reset();rappelLook.Reset();rappelAttachment.Reset();rappelOrientationLocked=false;rappelAnatomicalUpLocked=false;rappelBodyPhase=0;}
    trackingValidForStatus.store(activeSample.valid&&activeSample.mounted);
    const float yaw=lastMovementYaw.load();
    const bool movementValid=activeSample.valid&&activeSample.mounted&&movementViewApplied&&
        movementRecenterAck==activeRecenterAck&&
        (!NativeCameraKL::Installed()||nativeMovementYawValid.load())&&std::isfinite(yaw);
    VRBridge::SetMovementYaw(nativeAxes||nativeCarried?0.f:yaw,movementValid,movementLookLocked,nativeAxes,nativeCarried,nativeMounted);
    if(movementValid)++movementYawPublished;
}

const char* PhaseName(bool installed,bool ready,bool running) {
    if(!installed)return "probing_d3d9";
    if(ready)return "submitting";
    if(running)return "host_starting";
    return "waiting_for_f9";
}

struct PendingStatusTrace {
    std::uint64_t objective=0,postfilter=0,reflection=0,frame=0,wait=0,flush=0;
};
bool WriteStatus(bool installed,PendingStatusTrace* pending=nullptr) {
    wchar_t path[MAX_PATH],temporary[MAX_PATH];
    swprintf(path,MAX_PATH,L"%lsKaneLynchVR-status.json",folder);
    swprintf(temporary,MAX_PATH,L"%lsKaneLynchVR-status.tmp",folder);
    FILE* f=_wfopen(temporary,L"w");if(!f)return false;
    auto objectiveWritten=objectiveTraceAfter,postfilterWritten=postfilterTraceAfter,reflectionWritten=reflectionTraceAfter;
    const auto vr=VRBridge::Snapshot();
    const auto capture=D3D9Capture::Snapshot(true);
    const auto hands=HandsRenderer::Snapshot();
    const auto gameplayHud=NativeGameplayHudKL::Snapshot();
    const auto teamHud=NativeTeamHudKL::SnapshotStats();
    const auto nativeWait=NativeFrameWaitKL::Snapshot();
    const auto bufferFlush=NativeBufferFlushKL::Snapshot();
    const auto effects=NativeEffectsKL::Snapshot();
    const auto pipRender=NativePipRenderKL::Snapshot();
    const auto pipCamera=NativeCameraKL::Snapshot();
    const auto native=NativeCameraKL::Snapshot();
    const auto rig=NativeHandsKL::Snapshot();
    const auto skinLighting=NativeSkinLightingKL::Snapshot();
    const auto mirrorBump=NativeMirrorBumpKL::Snapshot();
    const auto carriedPerson=NativeCarriedPersonVisibilityKL::Snapshot();
    const auto weapon=NativeWeaponKL::Snapshot();
    const auto nativeScope=NativeWeaponKL::ReadScope();
    const auto body=NativeBodyKL::Snapshot();
    const auto postprocess=NativePostprocessKL::Snapshot();
    const auto hud=NativeHudKL::Snapshot();
    DWORD activePid=0;GetWindowThreadProcessId(GetForegroundWindow(),&activePid);
    std::fprintf(f,"{\"version\":\"" KL1_RELEASE_VERSION "-kane-lynch-vr\",\"buildRevision\":\"stable-" KL1_RELEASE_VERSION "\",\"phase\":\"%s\",\"pid\":%lu,\"updatedTick\":%lu,\"gameFocused\":%s,"
      "\"hookInstalled\":%s,\"captureEnabled\":%s,\"vrRequested\":%s,\"hostRunning\":%s,\"vrReady\":%s,\"vrError\":%ld,"
      "\"framesSubmitted\":%ld,\"hmdVisible\":%ld,\"hmdPresent\":%ld,"
      "\"stereoMode\":%s,\"currentEye\":%u,\"trackingValid\":%s,"
      "\"sceneFrames\":%llu,\"panelFrames\":%llu,\"incompleteFrames\":%llu,"
      "\"viewModified\":%llu,\"worldModified\":%llu,\"projectionModified\":%llu,\"vsMatrixPatched\":%llu,"
      "\"movementYawPublished\":%llu,\"lastMovementYawDeg\":%.2f,"
      "\"totalViews\":%u,\"identityViews\":%u,\"totalWorlds\":%u,\"rotatedWorlds\":%u,"
      "\"totalProjections\":%u,\"identityProjections\":%u,\"totalConstants\":%u,"
      "\"presents\":%llu,\"swapPresents\":%llu,\"devicePresents\":%llu,\"endScenes\":%llu,\"captured\":%llu,\"callbackDrops\":%llu,\"captureFailures\":%llu,"
      "\"captureTrigger\":\"%s\",\"halCreateResult\":%d,\"nullrefCreateResult\":%d,\"swapchainResult\":%d,\"presentPromotionResult\":%d,"
      "\"presentTargetProbe\":{\"samples\":%u,\"swapChanges\":%u,\"deviceChanges\":%u,\"result\":%d},"
      "\"sourceWidth\":%u,\"sourceHeight\":%u,\"sourceFormat\":%d,"
      "\"lastCaptureResult\":%d,\"lastPresentResult\":%d,\"installResult\":%d,"
      "\"shaderProjectionsRecovered\":%llu,"
      "\"nativeCameraInstalled\":%s,\"nativeCameraCopies\":%llu,\"nativeCameraOwnedCopies\":%llu,\"nativeCameraRejected\":%llu,\"nativeCameraProjections\":%llu,\"nativeCameraRenderPoses\":%llu,\"nativeCameraGeometryPoses\":%llu,\"nativeCameraSource\":%llu,\"nativeCameraFarUnits\":%.2f,"
      "\"shaderCallsInsideNativeRender\":%llu,\"shaderCallsOutsideNativeRender\":%llu,"
      "\"nativeCameraRestoredPoses\":%llu,\"nativeCameraRestoreFailures\":%llu,"
      "\"cameraRenderThreadId\":%u,\"shaderThreadId\":%u,\"cameraCleanupThreadId\":%u,"
      "\"gpuCameraCalls\":%llu,\"gpuCameraViews\":%llu,\"gpuCameraRejected\":%llu,\"gpuCameraEye\":%u,\"gpuCameraPair\":%llu,\"gpuCameraPosition\":[%.3f,%.3f,%.3f],\"gpuCameraThreadId\":%u,"
      "\"handsEnabled\":%s,\"handsConnected\":%s,\"handsValid\":%u,\"handsAgeMs\":%u,\"handsFrames\":%llu,\"handsDrawn\":%llu,\"handsFailures\":%llu,"
      "\"nativeMenuActive\":%s,\"nativeArmsInstalled\":%s,\"nativeArmsCalls\":%llu,\"nativeArmsPlayerCalls\":%llu,"
      "\"nativeArmsApplied\":[%llu,%llu],\"nativeArmsClamped\":[%llu,%llu],\"nativeArmsRejected\":%llu,\"nativeArmsAgeMs\":%u,"
      "\"nativeArmsGpuPalettes\":%llu,\"nativeArmsGpuRejected\":%llu,\"nativeHeadHidden\":%llu,"
      "\"nativeArmsGpuUploadInvalidated\":%llu,\"nativeArmsGpuCacheRejected\":%llu,"
      "\"nativeArmsOwnPaletteUploads\":%llu,\"nativeArmsCollapsedHeadUploads\":%llu,\"nativeArmsShaderRowsInspected\":%llu,"
      "\"nativeArmsInsideRender\":%llu,\"nativeArmsOutsideRender\":%llu,"
      "\"nativeArmsGpuBinderCalls\":%llu,\"nativeArmsGpuPlayerBinds\":%llu,\"nativeArmsGpuNoContext\":%llu,\"nativeArmsFrameThread\":%u,\"nativeArmsBindThread\":%u,"
      "\"nativeArmsRequestedMetres\":[%.4f,%.4f],\"nativeArmsReachMetres\":[%.4f,%.4f],\"nativeArmsTargetErrorMetres\":[%.4f,%.4f],"
      "\"nativeArmHelpersApplied\":[%llu,%llu],\"nativeArmResidualTwistDegrees\":[%.2f,%.2f],\"nativeArmLazyPrepared\":%llu,\"nativeArmLazyCacheHits\":%llu,\"nativeArmLazyRejected\":%llu,"
      "\"weaponAimInstalled\":%s,\"weaponAimCalls\":%llu,\"weaponAimTracked\":%llu,\"weaponAimPassthrough\":%llu,\"weaponAimHand\":%d,"
      "\"weaponWorldCalls\":%llu,\"weaponMeshCandidates\":%llu,\"weaponMeshOwned\":%llu,\"weaponMeshTracked\":%llu,\"weaponMeshLagged\":%llu,\"weaponMeshOwner\":%llu,"
      "\"weaponRigidCalls\":%llu,\"weaponRigidTracked\":%llu,\"weaponMeshFreshRejected\":%llu,"
      "\"bodyFrames\":%llu,\"bodyAdjusted\":%llu,\"bodyRotated\":%llu,\"bodyRejected\":%llu,\"bodyPair\":%llu,\"bodyThread\":%u,\"bodyYawDegrees\":%.2f,\"bodyCorrectionDegrees\":%.2f,"
      "\"bodyShoulderMeasured\":%llu,\"bodyShoulderFallback\":%llu,\"bodyRawTorsoYawDegrees\":%.2f,\"bodyAnimationYawDegrees\":%.2f,\"bodyBindYawOffsetDegrees\":%.2f,"
      "\"temporalFilterHookInstalled\":%s,\"temporalFilterCalls\":%llu,\"temporalHistorySkipped\":%llu,"
      "\"firstPerson\":%u,\"cinematicVR\":%s,\"playerAnchorValid\":%s,\"playerAnchorHeadBone\":%s,\"headPositionMm\":%u,"
      "\"cameraForwardMm\":%u,\"renderFovPercent\":%u,\"panelWidthMm\":%u,\"panelDistanceMm\":%u,\"autoStart\":%u,\"captureWithoutHost\":%u",
      PhaseName(installed,vr.ready,vr.running),GetCurrentProcessId(),GetTickCount(),
      activePid==GetCurrentProcessId()?"true":"false",
      installed?"true":"false",capture.enabled?"true":"false",
      vr.requested?"true":"false",vr.running?"true":"false",vr.ready?"true":"false",vr.error,
      vr.submitted,vr.visible,vr.hmdPresent,
      settings.stereoMode?"true":"false",currentEye.load(),trackingValidForStatus.load()?"true":"false",
      static_cast<unsigned long long>(sceneFrames.load()),
      static_cast<unsigned long long>(panelFrames.load()),
      static_cast<unsigned long long>(incompleteFrames.load()),
      static_cast<unsigned long long>(viewModified.load()),
      static_cast<unsigned long long>(worldModified.load()),
      static_cast<unsigned long long>(projectionModified.load()),
      static_cast<unsigned long long>(vsMatrixPatched.load()),
      static_cast<unsigned long long>(movementYawPublished.load()),
      lastMovementYaw.load()*57.2957795f,
      totalViews.load(),identityViews.load(),totalWorlds.load(),rotatedWorlds.load(),
      totalProjections.load(),identityProjections.load(),totalConstants.load(),
      static_cast<unsigned long long>(capture.presents),
      static_cast<unsigned long long>(capture.swapPresents),
      static_cast<unsigned long long>(capture.devicePresents),
      static_cast<unsigned long long>(capture.endScenes),
      static_cast<unsigned long long>(capture.captured),
      static_cast<unsigned long long>(capture.callbackDrops),
      static_cast<unsigned long long>(capture.failures),
      capture.endSceneTrigger?"endscene":"present",
      capture.halCreateResult,capture.nullrefCreateResult,capture.swapchainResult,capture.promotionResult,
      capture.presentProbeSamples,capture.presentProbeSwapChanges,capture.presentProbeDeviceChanges,capture.presentProbeResult,
      capture.width,capture.height,capture.formatCode,
      capture.lastCaptureResult,capture.lastPresentResult,capture.installResult,
      static_cast<unsigned long long>(shaderProjectionsRecovered.load()),
      native.installed?"true":"false",static_cast<unsigned long long>(native.copies),
      static_cast<unsigned long long>(native.ownedCopies),static_cast<unsigned long long>(native.rejected),
      static_cast<unsigned long long>(native.projections),static_cast<unsigned long long>(native.renderPoses),static_cast<unsigned long long>(native.geometryPoses),
      static_cast<unsigned long long>(native.lastSource),native.lastFarUnits,
      static_cast<unsigned long long>(native.shaderCallsInsideRender),static_cast<unsigned long long>(native.shaderCallsOutsideRender),
      static_cast<unsigned long long>(native.restoredPoses),static_cast<unsigned long long>(native.restoreFailures),
      native.cameraRenderThreadId,native.shaderThreadId,native.cleanupThreadId,
      static_cast<unsigned long long>(native.gpuCameraCalls),static_cast<unsigned long long>(native.gpuCameraViews),
      static_cast<unsigned long long>(native.gpuCameraRejected),native.lastGpuEye,static_cast<unsigned long long>(native.lastGpuPair),
      native.lastGpuPosition[0],native.lastGpuPosition[1],native.lastGpuPosition[2],
      native.gpuCameraThreadId,
      hands.enabled?"true":"false",hands.connected?"true":"false",hands.validHands,hands.ageMs,
      static_cast<unsigned long long>(hands.frames),static_cast<unsigned long long>(hands.hands),
      static_cast<unsigned long long>(hands.failures),
      ReadNativeMenu()?"true":"false",rig.installed?"true":"false",
      static_cast<unsigned long long>(rig.calls),static_cast<unsigned long long>(rig.playerCalls),
      static_cast<unsigned long long>(rig.applied[0]),static_cast<unsigned long long>(rig.applied[1]),
      static_cast<unsigned long long>(rig.clamped[0]),static_cast<unsigned long long>(rig.clamped[1]),
      static_cast<unsigned long long>(rig.rejected),rig.ageMs,
      static_cast<unsigned long long>(rig.gpuPalettes),static_cast<unsigned long long>(rig.gpuRejected),
      static_cast<unsigned long long>(rig.headHidden),
      static_cast<unsigned long long>(rig.gpuUploadInvalidated),static_cast<unsigned long long>(rig.gpuCacheRejected),
      static_cast<unsigned long long>(rig.ownPaletteUploads),static_cast<unsigned long long>(rig.collapsedHeadUploads),
      static_cast<unsigned long long>(rig.shaderRowsInspected),
      static_cast<unsigned long long>(rig.insideRenderCalls),
      static_cast<unsigned long long>(rig.outsideRenderCalls),
      static_cast<unsigned long long>(rig.gpuBinderCalls),static_cast<unsigned long long>(rig.gpuPlayerBinds),
      static_cast<unsigned long long>(rig.gpuNoContext),rig.frameThread,rig.bindThread,
      rig.requestedDistanceMetres[0],rig.requestedDistanceMetres[1],rig.armReachMetres[0],rig.armReachMetres[1],
      rig.targetErrorMetres[0],rig.targetErrorMetres[1],
      static_cast<unsigned long long>(rig.helperApplied[0]),static_cast<unsigned long long>(rig.helperApplied[1]),
      rig.residualTwistDegrees[0],rig.residualTwistDegrees[1],
      static_cast<unsigned long long>(rig.lazyVisualPrepared),static_cast<unsigned long long>(rig.lazyVisualCacheHits),
      static_cast<unsigned long long>(rig.lazyVisualRejected),
      weapon.installed?"true":"false",static_cast<unsigned long long>(weapon.calls),
      static_cast<unsigned long long>(weapon.tracked),static_cast<unsigned long long>(weapon.passthrough),weapon.hand,
      static_cast<unsigned long long>(weapon.worldCalls),static_cast<unsigned long long>(weapon.meshCandidates),
      static_cast<unsigned long long>(weapon.meshOwned),static_cast<unsigned long long>(weapon.meshTracked),
      static_cast<unsigned long long>(weapon.meshLagged),static_cast<unsigned long long>(weapon.meshOwner),
      static_cast<unsigned long long>(weapon.rigidCalls),static_cast<unsigned long long>(weapon.rigidTracked),
      static_cast<unsigned long long>(weapon.meshFreshRejected),
      static_cast<unsigned long long>(body.frames),static_cast<unsigned long long>(body.adjusted),
      static_cast<unsigned long long>(body.rotated),static_cast<unsigned long long>(body.rejected),
      static_cast<unsigned long long>(body.pairId),body.threadId,body.yawDegrees,body.correctionDegrees,
      static_cast<unsigned long long>(body.shoulderMeasured),static_cast<unsigned long long>(body.shoulderFallback),
      body.rawTorsoYawDegrees,body.animationYawDegrees,body.bindYawOffsetDegrees,
      postprocess.installed?"true":"false",static_cast<unsigned long long>(postprocess.calls),
      static_cast<unsigned long long>(postprocess.historySkipped),
      settings.firstPerson,settings.cinematicVr?"true":"false",playerAnchorValid.load()?"true":"false",playerAnchorHeadBone.load()?"true":"false",settings.maxLeanMm,
      settings.cameraForwardMm,settings.renderFovPercent,settings.panelWidthMm,settings.panelDistanceMm,
      settings.autoStart,settings.captureWithoutHost);
    std::fprintf(f,",\"nativeArmsBindPoseCacheHits\":%llu,\"nativeArmsBindPoseCacheMisses\":%llu",
        static_cast<unsigned long long>(rig.bindPoseCacheHits),static_cast<unsigned long long>(rig.bindPoseCacheMisses));
    std::fprintf(f,",\"vehicleCamera\":{\"transitionViews\":%llu,\"compromisedFrames\":%llu,\"unclassifiedPrimaryStatePasses\":%llu}",
        static_cast<unsigned long long>(native.vehicleTransitionViews),static_cast<unsigned long long>(native.captureCompromisedFrames),
        static_cast<unsigned long long>(native.unclassifiedPrimaryStatePasses));
    std::fprintf(f,",\"weaponMeshPoseRecovered\":%llu",static_cast<unsigned long long>(weapon.meshPoseRecovered));
    const auto visualTrace=NativeVisualTraceKL::Snapshot();
    std::fprintf(f,",\"visualTrace\":{\"enabled\":%s,\"active\":%s,\"budgetReached\":%s,\"frames\":%llu,\"written\":%llu,\"draws\":%llu,\"droppedDraws\":%llu,\"images\":%llu,\"copyFailures\":%llu,\"readbackFailures\":%llu,\"writeFailures\":%llu,\"queueDrops\":%llu,\"bytes\":%llu,\"recordingSuspended\":%llu,\"memorySuspended\":%llu,\"previewSkipped\":%llu,\"directory\":%s}",
        visualTrace.enabled?"true":"false",visualTrace.active?"true":"false",visualTrace.budgetReached?"true":"false",
        static_cast<unsigned long long>(visualTrace.frames),static_cast<unsigned long long>(visualTrace.written),static_cast<unsigned long long>(visualTrace.draws),
        static_cast<unsigned long long>(visualTrace.droppedDraws),static_cast<unsigned long long>(visualTrace.images),static_cast<unsigned long long>(visualTrace.copyFailures),
        static_cast<unsigned long long>(visualTrace.readbackFailures),static_cast<unsigned long long>(visualTrace.writeFailures),static_cast<unsigned long long>(visualTrace.queueDrops),
        static_cast<unsigned long long>(visualTrace.bytes),static_cast<unsigned long long>(visualTrace.recordingSuspended),
        static_cast<unsigned long long>(visualTrace.memorySuspended),static_cast<unsigned long long>(visualTrace.previewSkipped),
        PerformanceLog::Quote(PerformanceLog::Utf8(visualTrace.directory)).c_str());
    std::fprintf(f,",\"weaponStowedOwned\":%llu,\"weaponStowedTracked\":%llu,\"weaponStowedRejected\":%llu,\"weaponStowedHidden\":%llu,"
        "\"stereoHudEnabled\":%s,\"hudDistanceMm\":%u,\"hudScalePercent\":%u,"
        "\"nativeHudInstalled\":%s,\"nativeHudCalls\":%llu,\"nativeHudScopes\":%llu,\"nativeHudUploads\":%llu,"
        "\"nativeHudRejected\":%llu,\"nativeHudFullScreenPreserved\":%llu,\"nativeHudThread\":%u,"
        "\"nativeHudProjection\":[%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g],",
        static_cast<unsigned long long>(weapon.stowedOwned),static_cast<unsigned long long>(weapon.stowedTracked),
        static_cast<unsigned long long>(weapon.stowedRejected),static_cast<unsigned long long>(weapon.stowedHidden),
        settings.stereoHud?"true":"false",settings.hudDistanceMm,settings.hudScalePercent,hud.installed?"true":"false",
        static_cast<unsigned long long>(hud.calls),static_cast<unsigned long long>(hud.scopes),
        static_cast<unsigned long long>(hud.uploads),static_cast<unsigned long long>(hud.rejected),
        static_cast<unsigned long long>(hud.fullScreenPreserved),hud.threadId,
        hud.lastProjection[0],hud.lastProjection[1],hud.lastProjection[2],hud.lastProjection[3],
        hud.lastProjection[4],hud.lastProjection[5],hud.lastProjection[6],hud.lastProjection[7],
        hud.lastProjection[8],hud.lastProjection[9],hud.lastProjection[10],hud.lastProjection[11],
        hud.lastProjection[12],hud.lastProjection[13],hud.lastProjection[14],hud.lastProjection[15]);
    std::fprintf(f,"\"cinematicActive\":%s,\"nativeHudIndexedCalls\":%llu,\"nativeHudGenericCandidates\":%llu,"
        "\"nativeHudGenericUploads\":%llu,\"nativeHudCameraRejected\":%llu,\"nativeHudViewportRejected\":%llu,"
        "\"nativeHudConstantsRejected\":%llu,\"nativeHudBufferRejected\":%llu,\"nativeHudLastCaller\":%u,"
        "\"nativeHudLastStride\":%u,\"nativeHudLastVertexUsage\":%u,\"nativeHudLastIndexUsage\":%u,\"nativeHudLastReadStage\":%u,"
        "\"nativeHudLastConstants\":[%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g],",
        NativeCameraKL::CinematicActive()?"true":"false",static_cast<unsigned long long>(hud.indexedCalls),
        static_cast<unsigned long long>(hud.genericCandidates),static_cast<unsigned long long>(hud.genericUploads),
        static_cast<unsigned long long>(hud.cameraRejected),static_cast<unsigned long long>(hud.viewportRejected),
        static_cast<unsigned long long>(hud.constantsRejected),static_cast<unsigned long long>(hud.bufferRejected),
        hud.lastCaller,hud.lastStride,hud.lastVertexUsage,hud.lastIndexUsage,hud.lastReadStage,
        hud.lastConstants[0],hud.lastConstants[1],hud.lastConstants[2],hud.lastConstants[3],
        hud.lastConstants[4],hud.lastConstants[5],hud.lastConstants[6],hud.lastConstants[7],
        hud.lastConstants[8],hud.lastConstants[9],hud.lastConstants[10],hud.lastConstants[11],
        hud.lastConstants[12],hud.lastConstants[13],hud.lastConstants[14],hud.lastConstants[15],
        hud.lastConstants[16],hud.lastConstants[17],hud.lastConstants[18],hud.lastConstants[19]);
    const auto action=NativeActionKL::Current();
    std::fprintf(f,"\"nativeActionValid\":%s,\"nativeActionPreserveRig\":%s,\"nativeActionMovement\":%u,"
        "\"nativeActionKind\":%u,\"nativeMovementAxes\":%s,\"weaponFireCalls\":%llu,\"weaponFireTracked\":%llu,"
        "\"weaponFireRejected\":%llu,\"weaponMuzzleCalls\":%llu,\"weaponMuzzleTracked\":%llu,"
        "\"weaponAimRestored\":%llu,\"weaponAimRestoreRejected\":%llu,\"weaponShotThread\":%u,"
        "\"weaponShotOrigin\":[%.4f,%.4f,%.4f],\"weaponShotDirection\":[%.5f,%.5f,%.5f]",
        action.valid?"true":"false",action.PreserveRig()?"true":"false",static_cast<unsigned>(action.movementKind),
        static_cast<unsigned>(action.actionKind),action.NativeMovementAxes()?"true":"false",
        static_cast<unsigned long long>(weapon.fireCalls),static_cast<unsigned long long>(weapon.fireTracked),
        static_cast<unsigned long long>(weapon.fireRejected),static_cast<unsigned long long>(weapon.muzzleCalls),
        static_cast<unsigned long long>(weapon.muzzleTracked),static_cast<unsigned long long>(weapon.aimRestored),
        static_cast<unsigned long long>(weapon.aimRestoreRejected),weapon.shotThread,
        weapon.shotOrigin[0],weapon.shotOrigin[1],weapon.shotOrigin[2],
        weapon.shotDirection[0],weapon.shotDirection[1],weapon.shotDirection[2]);
    const struct {const char* name;std::uint64_t value;} extraMetrics[]={
        {"nativeHudConstantReadRejected",hud.constantReadRejected},{"nativeHudBackendRejected",hud.backendRejected},
        {"nativeHudProvenanceRejected",hud.provenanceRejected},{"nativeHudDepthRejected",hud.depthRejected},
        {"nativeHudGeometryRejected",hud.geometryRejected},{"nativeHudNativeLocks",hud.nativeLocks},
        {"nativeHudNativeUnlocks",hud.nativeUnlocks},{"nativeHudShadowCaptured",hud.shadowCaptured},
        {"nativeHudShadowRejected",hud.shadowRejected},{"nativeHudShadowReads",hud.shadowReads},
        {"nativeHudShadowMisses",hud.shadowMisses},{"nativeHudScissorAdjusted",hud.scissorAdjusted},
        {"nativeHudScissorRejected",hud.scissorRejected},{"accessoryOwned",weapon.accessoryOwned},
        {"accessoryTracked",weapon.accessoryTracked},{"accessoryRejected",weapon.accessoryRejected},
        {"nativeLetterboxCalls",hud.letterboxCalls},{"nativeLetterboxHidden",hud.letterboxHidden},
        {"nativeLetterboxRejected",hud.letterboxRejected},
        {"weaponCachedMuzzleTracked",weapon.cachedMuzzleTracked},{"weaponCachedMuzzleConsumed",weapon.cachedMuzzleConsumed},
        {"weaponCachedMuzzleRestored",weapon.cachedMuzzleRestored},{"weaponCachedMuzzleChanged",weapon.cachedMuzzleChanged},
        {"weaponOtherViewPreserved",weapon.viewRejected},{"nativeHandsOtherViewPreserved",rig.otherViewPreserved}
    };
    for(const auto& metric:extraMetrics)
        std::fprintf(f,",\"%s\":%llu",metric.name,static_cast<unsigned long long>(metric.value));
    std::fprintf(f,",\"nativeHudLastWriter\":%u,\"nativeHudLastZWrite\":%u,\"nativeHudLastZFunc\":%u,"
        "\"nativeHudLastRawCaller\":%u,\"nativeHudLastConstantsResult\":%d,"
        "\"nativeHudLastQuadMinZ\":%.6g,\"nativeHudLastQuadMaxZ\":%.6g,\"nativeHudLastRawConstants\":[",
        hud.lastWriter,hud.lastZWrite,hud.lastZFunc,hud.lastRawCaller,hud.lastConstantsResult,
        hud.lastQuadMinZ,hud.lastQuadMaxZ);
    for(unsigned i=0;i<20;++i)std::fprintf(f,i?",%.7g":"%.7g",hud.lastRawConstants[i]);
    std::fprintf(f,"],\"nativeLetterboxObject\":%u,\"nativeLetterboxMini\":%u,\"nativeLetterboxFull\":%u,"
        "\"nativeLetterboxFirstPerson\":%s,\"nativeLetterboxAmount\":%.6g",
        hud.lastLetterbox,hud.lastLetterboxMini,hud.lastLetterboxFull,
        hud.lastLetterboxFirstPerson?"true":"false",hud.lastLetterboxAmount);
    const auto cover=NativeCoverKL::Current();
    const auto coverScript=NativeCoverKL::ScriptSnapshot();
    std::fprintf(f,",\"scriptCoverInstalled\":%s,\"scriptCoverCandidates\":%llu,\"scriptCoverEvents\":%llu,\"scriptCoverBlockedEntries\":%llu",
        coverScript.installed?"true":"false",static_cast<unsigned long long>(coverScript.candidates),
        static_cast<unsigned long long>(coverScript.events),static_cast<unsigned long long>(coverScript.blockedEntries));
    std::fprintf(f,",\"scriptCoverDetectorCalls\":%llu,\"scriptCoverRawCandidates\":%llu,\"scriptCoverInvalidStates\":%llu,"
        "\"scriptCoverIneligible\":%llu,\"scriptCoverReadFailures\":%llu,\"scriptCoverActiveCredits\":%llu,\"scriptCoverInactiveUpdates\":%llu",
        static_cast<unsigned long long>(coverScript.detectorCalls),static_cast<unsigned long long>(coverScript.rawFreshCandidates),
        static_cast<unsigned long long>(coverScript.invalidStates),static_cast<unsigned long long>(coverScript.ineligible),
        static_cast<unsigned long long>(coverScript.readFailures),static_cast<unsigned long long>(coverScript.activeCredits),
        static_cast<unsigned long long>(coverScript.inactiveUpdates));
    std::fprintf(f,",\"coverHold\":{\"held\":%s,\"requests\":%llu,\"allowed\":%llu,\"rejected\":%llu}",
        VRBridge::CoverHeld()?"true":"false",static_cast<unsigned long long>(coverScript.heldEntryRequests),
        static_cast<unsigned long long>(coverScript.nativeEntryAllowed),static_cast<unsigned long long>(coverScript.nativeEntryRejected));
    std::fprintf(f,",\"cinematicCamera\":%lu,\"cinematicPanel\":%s,\"stableFirstPersonYaw\":%s,"
        "\"nativeMovementYawValid\":%s,\"nativeCoverValid\":%s,\"nativeCoverEnabled\":%s,\"nativeCoverActive\":%s,"
        "\"nativeCoverCandidateSide\":%d,\"nativeCoverPhase\":%d,\"nativeCoverExposure\":%d,"
        "\"nativeCoverInputBlocked\":%s,\"nativeCoverGuideBase\":%s,\"nativeCoverInputYaw\":%.6g,"
        "\"nativeCoverMove\":[%.5f,%.5f]",
        static_cast<unsigned long>(NativeCameraKL::CinematicCamera()),NativeCameraKL::CinematicPanelActive()?"true":"false",
        settings.stableFirstPersonYaw?"true":"false",nativeMovementYawValid.load()?"true":"false",
        cover.valid?"true":"false",cover.enabled?"true":"false",cover.active?"true":"false",
        int(cover.candidateSide),cover.phase,cover.exposure,cover.controllerBlocked?"true":"false",
        cover.hasGuideBase?"true":"false",cover.inputYawRadians,cover.moveForward,cover.moveRight);
    const auto& pacing=capture.pacing;
    std::fprintf(f,",\"immersiveHud\":%s,\"handHudAmmoProof\":%s,\"handHudContextProof\":%s,\"nativeGameplayHudInstalled\":%s,"
        "\"handHudReady\":%s,\"handHudFrames\":%llu,\"handHudCards\":%llu,\"handHudRejected\":%llu,\"handHudFailures\":%llu,\"handHudTextureBuilds\":%llu,\"nativeGameplayHudHidden\":{",
        settings.immersiveHud?"true":"false",ammoProof.load()?"true":"false",contextProof.load()?"true":"false",gameplayHud.installed?"true":"false",hands.hudReady?"true":"false",
        static_cast<unsigned long long>(hands.hudFrames),static_cast<unsigned long long>(hands.hudCards),static_cast<unsigned long long>(hands.hudRejected),
        static_cast<unsigned long long>(hands.hudFailures),static_cast<unsigned long long>(hands.hudTextureBuilds));
    const char* const hudNames[]={"crosshair","direction","actionMarker","inventoryCounters","mirroredUseAction","minimap","tutorialText","briefingAdvice"};
    static_assert(std::size(hudNames)==static_cast<unsigned>(NativeGameplayHudKL::Producer::Count));
    for(unsigned index=0;index<std::size(hudNames);++index)std::fprintf(f,"%s\"%s\":%llu",index?",":"",hudNames[index],static_cast<unsigned long long>(gameplayHud.hidden[index]));
    std::fprintf(f,"},\"pipGuardVerified\":%s,\"gpuPipPassesSkipped\":%llu,\"gpuPipCompositeFrames\":%llu,\"nativePipCamera\":%llu,\"nativePipGeometry\":%llu",
        pipCamera.pipGuardVerified?"true":"false",static_cast<unsigned long long>(pipCamera.gpuPipPassesSkipped),static_cast<unsigned long long>(pipCamera.gpuPipCompositeFrames),
        static_cast<unsigned long long>(pipCamera.lastPipCamera),static_cast<unsigned long long>(pipCamera.lastPipGeometry));
    std::fprintf(f,",\"auxiliaryPipRender\":{\"requested\":%s,\"installed\":%s,\"enabled\":%s,\"verified\":%s,\"calls\":%llu,\"suppressed\":%llu,\"rejected\":%llu}",
        settings.hideAuxiliaryPip?"true":"false",pipRender.installed?"true":"false",pipRender.enabled?"true":"false",pipRender.verified?"true":"false",
        static_cast<unsigned long long>(pipRender.calls),static_cast<unsigned long long>(pipRender.suppressed),static_cast<unsigned long long>(pipRender.rejected));
    std::fprintf(f,",\"auxiliaryPipDetails\":{\"activeSeen\":%llu,\"parentRejected\":%llu,\"viewRejected\":%llu,\"entryRejected\":%llu,\"fullRejected\":%llu,\"localRejected\":%llu,\"insetRejected\":%llu,\"compositeSuppressed\":%llu,\"mode\":%u,\"rectangle\":[%ld,%ld,%ld,%ld],\"parent\":%llu,\"view\":%llu,\"ownerView\":%llu,\"camera\":%llu}",
        static_cast<unsigned long long>(pipRender.activePipSeen),static_cast<unsigned long long>(pipRender.parentRejected),static_cast<unsigned long long>(pipRender.viewRejected),
        static_cast<unsigned long long>(pipRender.entryRejected),static_cast<unsigned long long>(pipRender.fullRejected),static_cast<unsigned long long>(pipRender.localRejected),
        static_cast<unsigned long long>(pipRender.insetRejected),static_cast<unsigned long long>(pipRender.compositeSuppressed),pipRender.mode,
        static_cast<long>(pipRender.rectangle[0]),static_cast<long>(pipRender.rectangle[1]),static_cast<long>(pipRender.rectangle[2]),static_cast<long>(pipRender.rectangle[3]),
        static_cast<unsigned long long>(pipRender.parent),static_cast<unsigned long long>(pipRender.view),static_cast<unsigned long long>(pipRender.ownerView),static_cast<unsigned long long>(pipRender.camera));
    const auto teamPanel=NativeHudKL::SnapshotTeamPanel();
    std::fprintf(f,",\"teamHud\":{\"installed\":%s,\"held\":%s,\"calls\":%llu,\"hidden\":%llu,\"captured\":%llu,\"rejected\":%llu,\"members\":%u,\"left\":%llu,\"right\":%llu,\"drawDropped\":%llu,\"lastRejection\":%u}",
        teamHud.installed?"true":"false",VRBridge::TeamHudVisible()?"true":"false",static_cast<unsigned long long>(teamHud.calls),
        static_cast<unsigned long long>(teamHud.hidden),static_cast<unsigned long long>(teamHud.captured),static_cast<unsigned long long>(teamHud.rejected),teamHud.members,
        static_cast<unsigned long long>(teamPanel.left),static_cast<unsigned long long>(teamPanel.right),static_cast<unsigned long long>(teamPanel.dropped),teamPanel.lastRejection);
    using NativeHudKL::WristHudElement;
    std::fprintf(f,",\"nativeTutorialBindings\":%llu,\"nativeSubtitleLayouts\":%llu,\"nativeInventoryLayouts\":{\"personal\":%llu,\"exchange\":%llu}",
        static_cast<unsigned long long>(gameplayHud.tutorialBindings),static_cast<unsigned long long>(gameplayHud.subtitleLayouts),
        static_cast<unsigned long long>(gameplayHud.personalWheel),static_cast<unsigned long long>(gameplayHud.exchangeWheel));
    std::fprintf(f,",\"nativeReadableText\":{\"instruction\":%llu,\"speech\":%llu,\"cinemaPanel\":%llu}",
        static_cast<unsigned long long>(NativeHudKL::WristHudDraws(WristHudElement::Instruction,0)+NativeHudKL::WristHudDraws(WristHudElement::Instruction,1)),
        static_cast<unsigned long long>(NativeHudKL::WristHudDraws(WristHudElement::Speech,0)+NativeHudKL::WristHudDraws(WristHudElement::Speech,1)),
        static_cast<unsigned long long>(NativeHudKL::CinemaTextDraws()));
    std::fprintf(f,",\"nativeWristHud\":{\"mapLeft\":%llu,\"mapRight\":%llu,\"wheelLeft\":%llu,\"wheelRight\":%llu,\"weaponLeft\":%llu,\"weaponRight\":%llu,\"ammoLeft\":%llu,\"ammoRight\":%llu,\"captionLeft\":%llu,\"captionRight\":%llu,\"selectionLeft\":%llu,\"selectionRight\":%llu,\"actionLeft\":%llu,\"actionRight\":%llu}",
        static_cast<unsigned long long>(NativeHudKL::WristHudDraws(WristHudElement::Minimap,0)),
        static_cast<unsigned long long>(NativeHudKL::WristHudDraws(WristHudElement::Minimap,1)),
        static_cast<unsigned long long>(NativeHudKL::WristHudDraws(WristHudElement::WeaponWheel,0)),
        static_cast<unsigned long long>(NativeHudKL::WristHudDraws(WristHudElement::WeaponWheel,1)),
        static_cast<unsigned long long>(NativeHudKL::WristHudDraws(WristHudElement::Weapon,0)),
        static_cast<unsigned long long>(NativeHudKL::WristHudDraws(WristHudElement::Weapon,1)),
        static_cast<unsigned long long>(NativeHudKL::WristAmmoDraws(0)),
        static_cast<unsigned long long>(NativeHudKL::WristAmmoDraws(1)),
        static_cast<unsigned long long>(NativeHudKL::WristCaptionDraws(0)),
        static_cast<unsigned long long>(NativeHudKL::WristCaptionDraws(1)),
        static_cast<unsigned long long>(NativeHudKL::SelectionMarkerDraws(0)),
        static_cast<unsigned long long>(NativeHudKL::SelectionMarkerDraws(1)),
        static_cast<unsigned long long>(NativeHudKL::WristHudDraws(WristHudElement::Action,0)),
        static_cast<unsigned long long>(NativeHudKL::WristHudDraws(WristHudElement::Action,1)));
    std::fprintf(f,",\"alternateBodyPalette\":{\"verified\":%s,\"candidates\":%llu,\"applied\":%llu,\"rejected\":%llu}",
        rig.alternatePaletteProof?"true":"false",static_cast<unsigned long long>(rig.alternatePaletteCandidates),
        static_cast<unsigned long long>(rig.alternatePaletteApplied),static_cast<unsigned long long>(rig.paletteSourceRejected));
    const auto aimPreview=NativeAimKL::Snapshot();
    std::fprintf(f,",\"weaponGameplay\":{\"supportEnabled\":%s,\"supportCandidates\":%llu,\"supportHeld\":%llu,\"supportReachRejected\":%llu,\"supportReachAdjusted\":%llu,\"supportAcquired\":%llu,\"supportReleased\":%llu,\"supportAimRecovered\":%llu,\"supportAimRejected\":%llu,\"ownershipNegativeHits\":%llu,\"ownershipNegativeStored\":%llu,\"menuHeld\":%s,\"reticleEnabled\":%s,\"reticleCaptured\":%llu,\"reticleDrawn\":%llu,\"reticleRejected\":%llu,\"laserEnabled\":%s,\"collisionInstalled\":%s,\"queries\":%llu,\"hits\":%llu,\"misses\":%llu,\"rejected\":%llu,\"selfRejected\":%llu}",
        settings.supportGrip?"true":"false",static_cast<unsigned long long>(rig.supportCandidates),static_cast<unsigned long long>(rig.supportHeld),
        static_cast<unsigned long long>(rig.supportReachRejected),static_cast<unsigned long long>(rig.supportReachAdjusted),
        static_cast<unsigned long long>(rig.supportAcquired),static_cast<unsigned long long>(rig.supportReleased),
        static_cast<unsigned long long>(rig.supportAimRecovered),static_cast<unsigned long long>(rig.supportAimRejected),
        static_cast<unsigned long long>(weapon.ownershipNegativeHits),static_cast<unsigned long long>(weapon.ownershipNegativeStored),VRBridge::WeaponMenuVisible()?"true":"false",settings.nativeReticle?"true":"false",
        static_cast<unsigned long long>(hud.reticleCaptured),static_cast<unsigned long long>(hud.reticleDrawn),static_cast<unsigned long long>(hud.reticleRejected),
        settings.laserSight?"true":"false",aimPreview.installed?"true":"false",static_cast<unsigned long long>(aimPreview.queries),
        static_cast<unsigned long long>(aimPreview.hits),static_cast<unsigned long long>(aimPreview.misses),static_cast<unsigned long long>(aimPreview.rejected),static_cast<unsigned long long>(aimPreview.selfRejected));
    std::fprintf(f,",\"worldEffects\":{\"installed\":%s,\"shaderSeen\":%llu,\"shaderRecognized\":%llu,\"shaderDraws\":%llu,\"shaderRestored\":%llu,\"shaderFailed\":%llu,\"restoreFailed\":%llu,\"viewRejected\":%llu,\"projectionRejected\":%llu,\"spriteWrites\":%llu,\"spriteQuads\":%llu,\"spriteRejected\":%llu,\"viewportProbeSamples\":%llu,\"lastViewport\":[%u,%u],\"lastTarget\":[%u,%u],\"maxViewport\":[%u,%u],\"maxTarget\":[%u,%u],\"lastProbeFamily\":%u,\"lastTargetFormat\":%u,\"lastColourWriteMask\":%u",
        effects.installed?"true":"false",static_cast<unsigned long long>(effects.shaderSeen),static_cast<unsigned long long>(effects.shaderRecognized),
        static_cast<unsigned long long>(effects.shaderDraws),static_cast<unsigned long long>(effects.shaderRestored),static_cast<unsigned long long>(effects.shaderFailed),
        static_cast<unsigned long long>(effects.restoreFailed),static_cast<unsigned long long>(effects.viewRejected),static_cast<unsigned long long>(effects.projectionRejected),static_cast<unsigned long long>(effects.spriteWrites),
        static_cast<unsigned long long>(effects.spriteQuads),static_cast<unsigned long long>(effects.spriteRejected),static_cast<unsigned long long>(effects.viewportProbeSamples),
        effects.lastViewportWidth,effects.lastViewportHeight,effects.lastTargetWidth,effects.lastTargetHeight,effects.maxViewportWidth,effects.maxViewportHeight,
        effects.maxTargetWidth,effects.maxTargetHeight,effects.lastProbeFamily,effects.lastTargetFormat,effects.lastColourWriteMask);
    std::fprintf(f,",\"manualWrites\":%llu,\"manualQuads\":%llu,\"manualRejected\":%llu,\"particleWrites\":%llu,\"particleQuadsObserved\":%llu,\"particleQuads\":%llu,\"particleRejected\":%llu,\"particleAlignedPreserved\":%llu,\"particlePrefixRejected\":%llu,\"sourceStateRejected\":%llu",
        static_cast<unsigned long long>(effects.manualWrites),static_cast<unsigned long long>(effects.manualQuads),static_cast<unsigned long long>(effects.manualRejected),
        static_cast<unsigned long long>(effects.particleWrites),static_cast<unsigned long long>(effects.particleQuadsObserved),static_cast<unsigned long long>(effects.particleQuads),
        static_cast<unsigned long long>(effects.particleRejected),static_cast<unsigned long long>(effects.particleAlignedPreserved),static_cast<unsigned long long>(effects.particlePrefixRejected),static_cast<unsigned long long>(effects.sourceStateRejected));
    std::fprintf(f,",\"geometryInstalled\":%s,\"billboardCalls\":%llu,\"billboardEntered\":%llu,\"billboardBound\":%llu,\"billboardRejected\":%llu,\"billboardUnexpected\":%llu,\"lastBillboardMode\":%u,\"lastBillboardDescriptor\":%llu,\"radialFadeDraws\":%llu,\"radialFogDraws\":%llu,\"fogSourceRejected\":%llu,\"modelViewSamples\":%llu,\"modelViewMatches\":%llu,\"modelViewTransposeMatches\":%llu,\"modelViewOther\":%llu,\"modelViewMaxError\":%.6g",
        effects.geometryInstalled?"true":"false",static_cast<unsigned long long>(effects.billboardCalls),static_cast<unsigned long long>(effects.billboardEntered),
        static_cast<unsigned long long>(effects.billboardBound),static_cast<unsigned long long>(effects.billboardRejected),static_cast<unsigned long long>(effects.billboardUnexpected),
        effects.lastBillboardMode,static_cast<unsigned long long>(effects.lastBillboardDescriptor),static_cast<unsigned long long>(effects.radialFadeDraws),
        static_cast<unsigned long long>(effects.radialFogDraws),static_cast<unsigned long long>(effects.fogSourceRejected),static_cast<unsigned long long>(effects.modelViewSamples),
        static_cast<unsigned long long>(effects.modelViewMatches),static_cast<unsigned long long>(effects.modelViewTransposeMatches),static_cast<unsigned long long>(effects.modelViewOther),effects.modelViewMaxError);
    std::fprintf(f,",\"reflectedBillboardBound\":%llu,\"reflectedScatterDraws\":%llu",
        static_cast<unsigned long long>(effects.reflectedBillboardBound),static_cast<unsigned long long>(effects.reflectedScatterDraws));
    std::fprintf(f,",\"lastActualModelView\":[");for(unsigned i=0;i<16;++i)std::fprintf(f,"%s%.6g",i?",":"",effects.lastActualModelView[i]);
    std::fprintf(f,"],\"lastExpectedModelView\":[");for(unsigned i=0;i<16;++i)std::fprintf(f,"%s%.6g",i?",":"",effects.lastExpectedModelView[i]);std::fprintf(f,"]");
    const std::array<std::uint64_t,10>* effectFamilies[]={&effects.familySeen,&effects.familyCalls,&effects.familyDraws,&effects.familyViewRejected,&effects.familyProjectionRejected};
    const char* effectFamilyNames[]={"familySeen","familyCalls","familyDraws","familyViewRejected","familyProjectionRejected"};
    for(unsigned family=0;family<5;++family){std::fprintf(f,",\"%s\":[",effectFamilyNames[family]);for(unsigned slot=0;slot<10;++slot)std::fprintf(f,"%s%llu",slot?",":"",static_cast<unsigned long long>((*effectFamilies[family])[slot]));std::fprintf(f,"]");}
    std::fprintf(f,"}");
    std::fprintf(f,",\"nativeLoadingActive\":%s,\"nativeLoadingEntries\":%llu,\"nativeLoadingResumes\":%llu,\"rappelLookFrames\":%llu,\"rappelYawDeg\":%.3f,\"rappelPitchDeg\":%.3f,\"nativeScopeActive\":%s,\"nativeScopeFrames\":%llu,\"nativeScopePublished\":%llu",
        nativeLoading.Active(ReadNativeMenu(),movieActive.load()||NativeCameraKL::CinematicActive())?"true":"false",
        static_cast<unsigned long long>(nativeLoading.Entries()),static_cast<unsigned long long>(nativeLoading.Resumes()),
        static_cast<unsigned long long>(rappelLookFrames.load()),rappelYawDegrees.load(),rappelPitchDegrees.load(),
        nativeScope.valid&&nativeScope.active?"true":"false",static_cast<unsigned long long>(scopeFrames.load()),static_cast<unsigned long long>(scopePublished.load()));
    std::fprintf(f,",\"nativeScope\":{\"valid\":%s,\"precision\":%s,\"category\":%u,\"mode\":%u,\"hand\":%d,\"moverVtable\":%u,\"proofStage\":%u}",
        nativeScope.valid?"true":"false",nativeScope.precision?"true":"false",nativeScope.category,nativeScope.mode,
        nativeScope.hand,nativeScope.moverVtable,nativeScope.proofStage);
    std::fprintf(f,",\"nativeScopeReticlePreserved\":%llu",static_cast<unsigned long long>(hud.nativeScopeReticlePreserved));
    std::fprintf(f,",\"scopeGesture\":{\"enabled\":%s,\"requested\":%s,\"ticks\":%llu,\"entered\":%llu,\"exited\":%llu,\"precisionForced\":%llu,\"rejected\":%llu,\"distanceMetres\":%.4f,\"angleDegrees\":%.3f}",
        weapon.scopeGestureEnabled?"true":"false",weapon.scopeGestureRequested?"true":"false",static_cast<unsigned long long>(weapon.scopeGestureTicks),
        static_cast<unsigned long long>(weapon.scopeGestureEntered),static_cast<unsigned long long>(weapon.scopeGestureExited),
        static_cast<unsigned long long>(weapon.scopePrecisionForced),static_cast<unsigned long long>(weapon.scopeGestureRejected),
        weapon.scopeGestureDistanceMetres,weapon.scopeGestureAngleDegrees);
    std::fprintf(f,",\"weaponAnimationAim\":{\"supportSuspended\":%llu,\"barrelAxisAccepted\":%llu,\"barrelAxisRejected\":%llu,\"barrelAxisCategory\":%u,\"barrelAxisDeviationDegrees\":%.4f}",
        static_cast<unsigned long long>(rig.supportSuspended),static_cast<unsigned long long>(weapon.barrelAxisAccepted),
        static_cast<unsigned long long>(weapon.barrelAxisRejected),weapon.barrelAxisCategory,weapon.barrelAxisDeviationDegrees);
    std::fprintf(f,",\"supportGripRelease\":{\"input\":%llu,\"context\":%llu,\"identity\":%llu,\"invalidInput\":%llu,\"epoch\":%llu}",
        static_cast<unsigned long long>(rig.supportReleaseInput),static_cast<unsigned long long>(rig.supportReleaseContext),
        static_cast<unsigned long long>(rig.supportReleaseIdentity),static_cast<unsigned long long>(rig.supportReleaseInvalidInput),
        static_cast<unsigned long long>(rig.supportReleaseEpoch));
    std::fprintf(f,",\"beltAccessory\":{\"verified\":%s,\"slots\":%llu,\"owned\":%llu,\"tracked\":%llu,\"rejected\":%llu}",
        weapon.hipAttachmentVerified?"true":"false",static_cast<unsigned long long>(weapon.hipAccessorySlots),static_cast<unsigned long long>(weapon.hipAccessoryOwned),static_cast<unsigned long long>(weapon.hipAccessoryTracked),
        static_cast<unsigned long long>(weapon.hipAccessoryRejected));
    std::fprintf(f,",\"weaponBarrelProof\":{\"stage\":%u,\"localAxis\":[%.5f,%.5f,%.5f]}",
        weapon.barrelAxisProofStage,weapon.barrelAxisLocal[0],weapon.barrelAxisLocal[1],weapon.barrelAxisLocal[2]);
    std::fprintf(f,",\"supportGripSample\":{\"valid\":%s,\"rawGrip\":%.5f,\"releaseGrip\":%.5f,\"tick\":%u,\"releaseTick\":%u,\"lowMs\":%u,\"samples\":%u}",
        rig.supportGripSampleValid?"true":"false",rig.supportRawGrip,rig.supportReleaseGrip,rig.supportGripSampleTick,
        rig.supportReleaseSampleTick,rig.supportReleaseLowMs,rig.supportReleaseSamples);
    std::fprintf(f,",\"beltRigid\":{\"verified\":%s,\"bone\":%u,\"standard\":%u,\"candidates\":%llu,\"owned\":%llu,\"tracked\":%llu,\"rejected\":%llu}",
        weapon.hipRigidVerified?"true":"false",weapon.hipRigidBone,weapon.hipRigidStandard,static_cast<unsigned long long>(weapon.hipRigidCandidates),
        static_cast<unsigned long long>(weapon.hipRigidOwned),static_cast<unsigned long long>(weapon.hipRigidTracked),static_cast<unsigned long long>(weapon.hipRigidRejected));
    std::fprintf(f,",\"laserEyes\":[");
    for(unsigned eye=0;eye<2;++eye)std::fprintf(f,"%s{\"attempts\":%llu,\"aimReady\":%llu,\"contacts\":%llu,\"drawn\":%llu,\"drawRejected\":%llu,\"metadataRejected\":%llu,\"contextRejected\":%llu,\"captureRejected\":%llu,\"identityRejected\":%llu,\"beamRejected\":%llu}",
        eye?",":"",static_cast<unsigned long long>(laserEyeAttempts[eye].load()),static_cast<unsigned long long>(laserEyeAimReady[eye].load()),
        static_cast<unsigned long long>(laserEyeContacts[eye].load()),static_cast<unsigned long long>(laserEyeDrawn[eye].load()),
        static_cast<unsigned long long>(laserEyeDrawRejected[eye].load()),static_cast<unsigned long long>(laserEyeMetadataRejected[eye].load()),
        static_cast<unsigned long long>(laserContextRejected[eye].load()),static_cast<unsigned long long>(laserCaptureRejected[eye].load()),
        static_cast<unsigned long long>(laserIdentityRejected[eye].load()),static_cast<unsigned long long>(laserBeamRejected[eye].load()));
    std::fprintf(f,"]");
    const auto ropes=NativeRopeKL::Snapshot();
    std::fprintf(f,",\"localRopes\":{\"enabled\":%s,\"installed\":%s,\"frames\":%llu,\"owned\":%llu,\"candidates\":%llu,\"suppressed\":%llu,\"rejected\":%llu,\"cacheHits\":%llu,\"multiPassSuppressed\":%llu,\"bindRetired\":%llu,\"scopeBudgetRetired\":%llu,\"rigidBoundaries\":%llu,\"rigidCandidates\":%llu}",
        settings.hideLocalRopes?"true":"false",ropes.installed?"true":"false",static_cast<unsigned long long>(ropes.frames),
        static_cast<unsigned long long>(ropes.owned),static_cast<unsigned long long>(ropes.candidates),static_cast<unsigned long long>(ropes.suppressed),
        static_cast<unsigned long long>(ropes.rejected),static_cast<unsigned long long>(ropes.cacheHits),
        static_cast<unsigned long long>(ropes.multiPassSuppressed),static_cast<unsigned long long>(ropes.bindRetired),
        static_cast<unsigned long long>(ropes.scopeBudgetRetired),static_cast<unsigned long long>(ropes.rigidBoundaries),static_cast<unsigned long long>(ropes.rigidCandidates));
    std::fprintf(f,",\"commandMarkers\":{\"calls\":%llu,\"snapshots\":%llu,\"observed\":%llu,\"fixedObserved\":%llu,\"rejected\":%llu,\"drawFrames\":%llu,\"drawn\":%llu,\"drawRejected\":%llu,\"nativeEmitted\":%llu,\"nativeCaptured\":%llu,\"nativeRouted\":%llu,\"nativeDrawn\":%llu,\"nativeReprojected\":%llu,\"nativeRejected\":%llu,\"nativeRejectStage\":%u}",
        static_cast<unsigned long long>(teamHud.markerCalls),static_cast<unsigned long long>(teamHud.markerSnapshots),static_cast<unsigned long long>(teamHud.markerObserved),
        static_cast<unsigned long long>(teamHud.markerFixedObserved),static_cast<unsigned long long>(teamHud.markerRejected),static_cast<unsigned long long>(hands.commandMarkerFrames),static_cast<unsigned long long>(hands.commandMarkers),static_cast<unsigned long long>(hands.commandMarkerRejected),
        static_cast<unsigned long long>(hud.nativeMarkerEmitted),static_cast<unsigned long long>(hud.nativeMarkerCaptured),static_cast<unsigned long long>(hud.nativeMarkerRouted),
        static_cast<unsigned long long>(hud.nativeMarkerDrawn),static_cast<unsigned long long>(hud.nativeMarkerReprojected),static_cast<unsigned long long>(hud.nativeMarkerRejected),hud.nativeMarkerRejectStage);
    const auto teamCommands=NativeTeamCommandsKL::Snapshot();
    std::fprintf(f,",\"nativeDestinationShield\":{\"captured\":%llu,\"drawn\":%llu},\"gameHints\":%s",
        static_cast<unsigned long long>(hud.nativeDestinationCaptured),static_cast<unsigned long long>(hud.nativeDestinationDrawn),settings.gameHints?"true":"false");
    std::fprintf(f,",\"teamCommands\":{\"installed\":%s,\"requests\":%llu,\"accepted\":%llu,\"rejected\":%llu,\"noTarget\":%llu,\"selectionChanged\":%llu,\"stale\":%llu,\"queries\":%llu,\"targetOverrides\":%llu,\"lastSerial\":%u,\"lastCommand\":%u,\"lastResult\":%u,\"lastReceiver\":%u,\"lastTarget\":%u,\"lastThread\":%u,\"lastPoint\":[%.5f,%.5f,%.5f]}",
        teamCommands.installed?"true":"false",static_cast<unsigned long long>(teamCommands.requests),static_cast<unsigned long long>(teamCommands.accepted),
        static_cast<unsigned long long>(teamCommands.rejected),static_cast<unsigned long long>(teamCommands.noTarget),static_cast<unsigned long long>(teamCommands.selectionChanged),
        static_cast<unsigned long long>(teamCommands.stale),static_cast<unsigned long long>(teamCommands.queries),static_cast<unsigned long long>(teamCommands.targetOverrides),
        teamCommands.lastSerial,teamCommands.lastCommand,teamCommands.lastResult,teamCommands.lastReceiver,teamCommands.lastTarget,teamCommands.lastThread,
        teamCommands.lastPoint[0],teamCommands.lastPoint[1],teamCommands.lastPoint[2]);
    std::fprintf(f,",\"rappelAttachment\":{\"frames\":%llu,\"parentHeadFrames\":%llu,\"currentHeadFallbackFrames\":%llu,\"orientationLocked\":%s,\"bodyPhase\":%u,\"entryTiltDegrees\":%.3f,\"nativeHeadLocalMetres\":[%.5f,%.5f,%.5f],\"stableHeadLocalMetres\":[%.5f,%.5f,%.5f]}",
        static_cast<unsigned long long>(rappelAttachmentFrames.load()),static_cast<unsigned long long>(rappelParentHeadFrames.load()),static_cast<unsigned long long>(rappelCurrentHeadFrames.load()),rappelOrientationLocked.load()?"true":"false",rappelBodyPhase.load(),rappelEntryTiltDegrees.load(),rappelNativeHeadLocal[0].load(),rappelNativeHeadLocal[1].load(),rappelNativeHeadLocal[2].load(),
        rappelStableHeadLocal[0].load(),rappelStableHeadLocal[1].load(),rappelStableHeadLocal[2].load());
    std::fprintf(f,",\"rappelAnatomicalUpLocked\":%s",rappelAnatomicalUpLocked.load()?"true":"false");
    std::fprintf(f,",\"teamCommandRays\":{\"weapon\":%llu,\"head\":%llu,\"heldUnavailable\":%llu,\"headUnavailable\":%llu}",
        static_cast<unsigned long long>(teamCommands.weaponRayRequests),static_cast<unsigned long long>(teamCommands.headRayRequests),
        static_cast<unsigned long long>(teamCommands.heldRayUnavailable),static_cast<unsigned long long>(teamCommands.headRayUnavailable));
    std::fprintf(f,",\"nativeMarkerGeometry\":{\"ambiguous\":%llu,\"negativeW\":%llu,\"vertices\":%u,\"z\":[%.5f,%.5f],\"w\":[%.5f,%.5f]}",
        static_cast<unsigned long long>(hud.nativeMarkerAmbiguous),static_cast<unsigned long long>(hud.nativeMarkerNegativeW),
        hud.nativeMarkerVertexCount,hud.nativeMarkerMinZ,hud.nativeMarkerMaxZ,hud.nativeMarkerMinW,hud.nativeMarkerMaxW);
    const auto visualAim=aimPairTarget.Snapshot();
    std::fprintf(f,",\"aimPairTarget\":{\"resolved\":%llu,\"reused\":%llu,\"hits\":%llu,\"misses\":%llu,\"retired\":%llu}",
        static_cast<unsigned long long>(visualAim.resolved),static_cast<unsigned long long>(visualAim.reused),static_cast<unsigned long long>(visualAim.hits),static_cast<unsigned long long>(visualAim.misses),static_cast<unsigned long long>(visualAim.retired));
    std::fprintf(f,",\"scopeOverlay\":{\"verified\":%s,\"activated\":%llu,\"retained\":%llu,\"cleared\":%llu,\"rejected\":%llu,\"stage\":%u,\"nativeMode\":%u,\"drawFrames\":%llu,\"drawCandidates\":%llu,\"drawSuppressed\":%llu,\"drawRejected\":%llu}",
        weapon.scopeOverlayVerified?"true":"false",static_cast<unsigned long long>(weapon.scopeOverlayActivated),static_cast<unsigned long long>(weapon.scopeOverlayRetained),static_cast<unsigned long long>(weapon.scopeOverlayCleared),static_cast<unsigned long long>(weapon.scopeOverlayRejected),weapon.scopeOverlayProofStage,weapon.scopeOverlayNativeMode,
        static_cast<unsigned long long>(weapon.scopeDrawFrames),static_cast<unsigned long long>(weapon.scopeDrawCandidates),static_cast<unsigned long long>(weapon.scopeDrawSuppressed),static_cast<unsigned long long>(weapon.scopeDrawRejected));
    std::fprintf(f,",\"cinematicProjectionRecovery\":{\"state\":%llu,\"shaderRows\":%llu}",static_cast<unsigned long long>(native.queuedProjectionRestored),static_cast<unsigned long long>(native.queuedProjectionRowsRestored));
    std::fprintf(f,",\"nativeReflectionCamera\":{\"scopes\":%llu,\"certified\":%llu,\"rejected\":%llu,\"views\":%llu,\"restores\":%llu,\"lensRejected\":%llu,\"emptyScopes\":%llu,\"tokyoCameraPreparations\":%llu,\"retomotoCameraPreparations\":%llu,",
        static_cast<unsigned long long>(native.reflectionScopes),static_cast<unsigned long long>(native.reflectionCertified),
        static_cast<unsigned long long>(native.reflectionRejected),static_cast<unsigned long long>(native.reflectionViews),
        static_cast<unsigned long long>(native.reflectionRestores),static_cast<unsigned long long>(native.reflectionLensRejected),
        static_cast<unsigned long long>(native.reflectionEmptyScopes),static_cast<unsigned long long>(native.tokyoReflectionCameraPreparations),
        static_cast<unsigned long long>(native.retomotoReflectionCameraPreparations));
    NativeReflectionCameraJSON::WriteEvidence(f,"lastAccepted",native.lastReflectionAccepted);std::fputc(',',f);
    NativeReflectionCameraJSON::WriteEvidence(f,"lastRejected",native.lastReflectionRejected);std::fputc(',',f);
    NativeReflectionCameraJSON::WriteProducerEvidence(f,"lastProducerAccepted",native.lastReflectionProducerAccepted);std::fputc(',',f);
    NativeReflectionCameraJSON::WriteProducerEvidence(f,"lastProducerRejected",native.lastReflectionProducerRejected);std::fputc('}',f);
    std::fprintf(f,",\"secondaryMaskProjection\":{\"producerScopes\":%llu,\"certified\":%llu,\"applied\":%llu,\"popped\":%llu,\"rejected\":%llu,\"bodyBinds\":%llu,\"weaponBinds\":%llu",
        static_cast<unsigned long long>(native.secondaryProducerScopes),static_cast<unsigned long long>(native.secondaryCertified),
        static_cast<unsigned long long>(native.secondaryApplied),static_cast<unsigned long long>(native.secondaryPopped),static_cast<unsigned long long>(native.secondaryRejected),
        static_cast<unsigned long long>(native.secondaryBodyBinds),static_cast<unsigned long long>(native.secondaryWeaponBinds));
    const auto secondaryEvidence=[&](const char* key,const NativeCameraKL::SecondaryProjectionEvidence& value){
        std::fprintf(f,",\"%s\":",key);if(!value.valid){std::fputs("null",f);return;}
        std::fprintf(f,"{\"reason\":%u,\"eye\":%u,\"tick\":%u,\"sequence\":%llu,\"pair\":%llu,\"state\":%llu,\"packet\":%llu,\"queue\":%llu,\"sourceEntry\":%llu,\"sourceCamera\":%llu",
            value.reason,value.eye,value.tick,static_cast<unsigned long long>(value.sequence),static_cast<unsigned long long>(value.pair),
            static_cast<unsigned long long>(value.state),static_cast<unsigned long long>(value.packet),static_cast<unsigned long long>(value.queue),
            static_cast<unsigned long long>(value.sourceEntry),static_cast<unsigned long long>(value.sourceCamera));
        NativeReflectionCameraJSON::WriteRows(f,"nativeProjection",value.nativeProjection,16);
        NativeReflectionCameraJSON::WriteRows(f,"vrProjection",value.vrProjection,16);std::fputc('}',f);
    };
    secondaryEvidence("lastAccepted",native.lastSecondaryAccepted);secondaryEvidence("lastRejected",native.lastSecondaryRejected);
    const auto& secondaryProducer=native.lastSecondaryProducerRejected;
    std::fputs(",\"lastProducerRejected\":",f);
    if(!secondaryProducer.valid)std::fputs("null",f);
    else {
        std::fprintf(f,"{\"reason\":%u,\"tick\":%u,\"sequence\":%llu,\"queue\":%llu,\"sourceEntry\":%llu,\"sourceCamera\":%llu,\"cameraPacket\":%llu,\"projectionPacket\":%llu,\"drawPacket\":%llu,\"popPacket\":%llu",
            secondaryProducer.reason,secondaryProducer.tick,static_cast<unsigned long long>(secondaryProducer.sequence),static_cast<unsigned long long>(secondaryProducer.queue),
            static_cast<unsigned long long>(secondaryProducer.sourceEntry),static_cast<unsigned long long>(secondaryProducer.sourceCamera),static_cast<unsigned long long>(secondaryProducer.cameraPacket),
            static_cast<unsigned long long>(secondaryProducer.projectionPacket),static_cast<unsigned long long>(secondaryProducer.drawPacket),static_cast<unsigned long long>(secondaryProducer.popPacket));
        const auto writeSecondaryHeader=[&](const char* name,const std::uint32_t* values){std::fprintf(f,",\"%s\":[",name);for(unsigned n=0;n<6;++n)std::fprintf(f,"%s%u",n?",":"",values[n]);std::fputc(']',f);};
        writeSecondaryHeader("cameraHeader",secondaryProducer.cameraHeader);writeSecondaryHeader("projectionHeader",secondaryProducer.projectionHeader);
        writeSecondaryHeader("drawHeader",secondaryProducer.drawHeader);writeSecondaryHeader("popHeader",secondaryProducer.popHeader);
        NativeReflectionCameraJSON::WriteRows(f,"mainProjection",secondaryProducer.mainProjection,16);NativeReflectionCameraJSON::WriteRows(f,"alternateProjection",secondaryProducer.alternateProjection,16);std::fputc('}',f);
    }
    std::fputc('}',f);
    std::fputs(",\"postfilterForeground\":{\"policy\":\"retired_native_mask\",\"installed\":false,\"candidates\":0,\"draws\":0,\"restored\":0,\"failed\":0,\"rejected\":0,\"cacheHits\":0,\"cacheMisses\":0}",f);
    std::fprintf(f,",\"worldSpriteEffects\":{\"type5Sources\":%llu,\"type6Sources\":%llu,\"relativePreserved\":%llu,\"hudPreserved\":%llu,\"type5Writes\":%llu,\"type6Writes\":%llu,\"quads\":%llu,\"rejected\":%llu,\"sourceTypes\":[",
        static_cast<unsigned long long>(hud.worldSprite5Sources),static_cast<unsigned long long>(hud.worldSprite6Sources),
        static_cast<unsigned long long>(hud.worldSpriteRelativePreserved),static_cast<unsigned long long>(hud.worldSpriteHudPreserved),
        static_cast<unsigned long long>(effects.worldSprite5Writes),static_cast<unsigned long long>(effects.worldSprite6Writes),
        static_cast<unsigned long long>(effects.worldSpriteQuads),static_cast<unsigned long long>(effects.worldSpriteRejected));
    for(unsigned n=0;n<hud.spriteSourceTypes.size();++n)std::fprintf(f,"%s%llu",n?",":"",static_cast<unsigned long long>(hud.spriteSourceTypes[n]));
    std::fputc(']',f);
    const auto spriteSourceEvidence=[&](const char* key,const NativeHudKL::Stats::WorldSpriteSourceSnapshot& source){
        std::fprintf(f,",\"%s\":",key);if(!source.valid){std::fputs("null",f);return;}
        std::fprintf(f,"{\"sequence\":%llu,\"drawScopeId\":%llu,\"tick\":%u,\"type\":%u,\"flags\":%u,\"slot\":%u,\"count\":%u,\"reason\":%u,\"entry\":%llu,\"geometry\":%llu,\"records\":%llu,\"sourceState\":%llu,\"anchorKnown\":%s,\"anchor\":[",
            static_cast<unsigned long long>(source.sequence),static_cast<unsigned long long>(source.drawScopeId),source.tick,source.type,source.flags,source.slot,source.count,source.reason,
            static_cast<unsigned long long>(source.entry),static_cast<unsigned long long>(source.geometry),static_cast<unsigned long long>(source.records),static_cast<unsigned long long>(source.sourceState),source.anchorKnown?"true":"false");
        for(unsigned n=0;n<3;++n){if(n)std::fputc(',',f);if(source.anchorKnown&&std::isfinite(source.anchor[n]))std::fprintf(f,"%.9g",static_cast<double>(source.anchor[n]));else std::fputs("null",f);}
        std::fputs("]}",f);
    };
    spriteSourceEvidence("lastEligible",hud.lastWorldSpriteEligible);spriteSourceEvidence("lastPreserved",hud.lastWorldSpritePreserved);std::fputc('}',f);
    std::fprintf(f,",\"particleNormal\":{\"installed\":%s,\"calls\":%llu,\"applied\":%llu,\"alignedPreserved\":%llu,\"rejected\":%llu",
        effects.particleNormalInstalled?"true":"false",static_cast<unsigned long long>(effects.particleNormalCalls),static_cast<unsigned long long>(effects.particleNormalApplied),
        static_cast<unsigned long long>(effects.particleNormalAlignedPreserved),static_cast<unsigned long long>(effects.particleNormalRejected));
    const auto particleNormalEvidence=[&](const char* key,const NativeEffectsKL::ParticleNormalEvidence& value){
        std::fprintf(f,",\"%s\":",key);if(!value.valid){std::fputs("null",f);return;}
        std::fprintf(f,"{\"sequence\":%llu,\"drawScopeId\":%llu,\"tick\":%u,\"mode\":%u,\"reason\":%u,\"eye\":%u,\"sourceState\":%llu,\"sourceEntry\":%llu,\"sourceGeometry\":%llu,\"normalData\":%llu",
            static_cast<unsigned long long>(value.sequence),static_cast<unsigned long long>(value.drawScopeId),value.tick,value.mode,value.reason,value.eye,
            static_cast<unsigned long long>(value.sourceState),static_cast<unsigned long long>(value.sourceEntry),static_cast<unsigned long long>(value.sourceGeometry),static_cast<unsigned long long>(value.normalData));
        NativeReflectionCameraJSON::WriteRows(f,"authored",value.authored,3);NativeReflectionCameraJSON::WriteRows(f,"tracked",value.tracked,3);
        NativeReflectionCameraJSON::WriteRows(f,"reference",value.reference,3);NativeReflectionCameraJSON::WriteRows(f,"eyeBasis",value.eyeBasis,9);
        NativeReflectionCameraJSON::WriteRows(f,"referenceBasis",value.referenceBasis,9);std::fputc('}',f);
    };
    particleNormalEvidence("lastApplied",effects.lastParticleNormalApplied);particleNormalEvidence("lastRejected",effects.lastParticleNormalRejected);std::fputc('}',f);
    std::fprintf(f,",\"particleDeferred\":{\"installed\":%s,\"calls\":%llu,\"quads\":%llu,\"alignedPreserved\":%llu,\"rejected\":%llu}",
        effects.particleDeferredInstalled?"true":"false",static_cast<unsigned long long>(effects.particleDeferredCalls),
        static_cast<unsigned long long>(effects.particleDeferredQuads),static_cast<unsigned long long>(effects.particleDeferredAlignedPreserved),
        static_cast<unsigned long long>(effects.particleDeferredRejected));
    std::fprintf(f,",\"rappelTorso\":{\"candidates\":%llu,\"applied\":%llu,\"rejected\":%llu}",static_cast<unsigned long long>(rig.rappelTorsoCandidates),static_cast<unsigned long long>(rig.rappelTorsoApplied),static_cast<unsigned long long>(rig.rappelTorsoRejected));
    std::fprintf(f,",\"bodyAccessories\":{\"primitive\":%u,\"childCandidates\":%llu,\"childOwned\":%llu,\"childTracked\":%llu,\"childRejected\":%llu,\"groupSlots\":%llu,\"groupOwned\":%llu,\"groupTracked\":%llu,\"groupRejected\":%llu}",
        weapon.hipAccessoryPrimitive,static_cast<unsigned long long>(weapon.bodyChildCandidates),static_cast<unsigned long long>(weapon.bodyChildOwned),static_cast<unsigned long long>(weapon.bodyChildTracked),static_cast<unsigned long long>(weapon.bodyChildRejected),static_cast<unsigned long long>(weapon.bodyGroupSlots),static_cast<unsigned long long>(weapon.bodyGroupOwned),static_cast<unsigned long long>(weapon.bodyGroupTracked),static_cast<unsigned long long>(weapon.bodyGroupRejected));
    std::fprintf(f,",\"markerCalibration\":{\"calibrated\":%llu,\"rejected\":%llu,\"widthNdc\":%.6f,\"heightNdc\":%.6f,\"sourceDepth\":%.5f}",
        static_cast<unsigned long long>(hud.nativeMarkerCalibrated),static_cast<unsigned long long>(hud.nativeMarkerCalibrationRejected),hud.nativeMarkerWidthNdc,hud.nativeMarkerHeightNdc,hud.nativeMarkerSourceDepth);
    const auto& markerSource=hud.markerSource;
    std::fprintf(f,",\"markerCanonical\":{\"applied\":%llu,\"rejected\":%llu,\"nonfinite\":%llu,\"lastMagnitude\":%.6g,\"maxMagnitude\":%.6g,\"spanX\":%.6g,\"spanY\":%.6g}",
        static_cast<unsigned long long>(hud.markerCanonicalized),static_cast<unsigned long long>(hud.markerCanonicalRejected),static_cast<unsigned long long>(hud.markerCanonicalNonfinite),
        std::isfinite(hud.markerCanonicalLastMagnitude)?hud.markerCanonicalLastMagnitude:0.f,std::isfinite(hud.markerCanonicalMaxMagnitude)?hud.markerCanonicalMaxMagnitude:0.f,
        std::isfinite(hud.markerCanonicalSpanX)?hud.markerCanonicalSpanX:0.f,std::isfinite(hud.markerCanonicalSpanY)?hud.markerCanonicalSpanY:0.f);
    std::fprintf(f,",\"spriteLifetime\":{\"markerRetired\":%llu,\"reticleRetired\":%llu}",
        static_cast<unsigned long long>(hud.spriteMarkerRetired),static_cast<unsigned long long>(hud.spriteReticleRetired));
    std::fprintf(f,",\"markerSource\":{\"captures\":%llu,\"tick\":%u,\"worldTick\":%u,\"type\":%u,\"texture\":%u,\"caller\":%u,\"record\":%u,\"entity\":%u,\"entityVtable\":%u,\"geometry\":%u,\"parent\":%u,\"worldValid\":%s,\"fixed\":%u,\"bob\":%u,\"timed\":%u,\"target\":[%.5f,%.5f,%.5f],\"world\":[%.5f,%.5f,%.5f],\"width\":%.6f,\"height\":%.6f,\"offsetX\":%.6f,\"offsetY\":%.6f,\"sourceDepth\":%.5f,\"maxPointStep\":%.5f,\"sourceExtentValid\":%s,\"sourceWidth\":%.5f,\"sourceHeight\":%.5f,\"sourceOffsetX\":%.5f,\"sourceOffsetY\":%.5f}",
        static_cast<unsigned long long>(markerSource.captures),markerSource.tick,markerSource.worldTick,markerSource.type,markerSource.texture,markerSource.caller,
        static_cast<unsigned>(markerSource.record),static_cast<unsigned>(markerSource.entity),static_cast<unsigned>(markerSource.entityVtable),static_cast<unsigned>(markerSource.geometry),static_cast<unsigned>(markerSource.parent),
        markerSource.worldValid?"true":"false",unsigned(markerSource.fixed),unsigned(markerSource.bob),unsigned(markerSource.timed),markerSource.target.x,markerSource.target.y,markerSource.target.z,
        markerSource.world.x,markerSource.world.y,markerSource.world.z,markerSource.width,markerSource.height,markerSource.offsetX,markerSource.offsetY,markerSource.sourceDepth,markerSource.maxPointStep,
        markerSource.sourceExtentValid?"true":"false",markerSource.sourceWidth,markerSource.sourceHeight,markerSource.sourceOffsetX,markerSource.sourceOffsetY);
    const auto tracerVisual=NativeTracerVisualKL::Snapshot();
    std::fprintf(f,",\"tracerVisual\":{\"installed\":%s,\"drawScopes\":%llu,\"emits\":%llu,\"captured\":%llu,\"referenceMatched\":%llu,\"rejected\":%llu,\"producerReads\":%llu,\"producerRejected\":%llu}",
        tracerVisual.installed?"true":"false",static_cast<unsigned long long>(tracerVisual.drawScopes),static_cast<unsigned long long>(tracerVisual.emits),
        static_cast<unsigned long long>(tracerVisual.captured),static_cast<unsigned long long>(tracerVisual.referenceMatched),static_cast<unsigned long long>(tracerVisual.rejected),
        static_cast<unsigned long long>(tracerVisual.producerReads),static_cast<unsigned long long>(tracerVisual.producerRejected));
    std::fprintf(f,",\"nativeCommandAssist\":{\"requests\":%llu,\"selected\":%llu,\"rejected\":%llu,\"occluded\":%llu,\"angle\":%.6f}",
        static_cast<unsigned long long>(teamCommands.assistRequests),static_cast<unsigned long long>(teamCommands.assistSelected),static_cast<unsigned long long>(teamCommands.assistRejected),static_cast<unsigned long long>(teamCommands.assistOccluded),teamCommands.assistLastAngle);
    std::fprintf(f,",\"actionHeadingRetention\":{\"gaps\":%llu,\"retained\":%llu,\"rejected\":%llu}",static_cast<unsigned long long>(native.actionHeadingGaps),static_cast<unsigned long long>(native.actionHeadingRetained),static_cast<unsigned long long>(native.actionHeadingRejected));
    const auto passengerActor=NativePassengerActorKL::Snapshot();
    const auto& cargoMotion=passengerActor.motion;
    std::fprintf(f,",\"cargoMotionAuthority\":{\"poseOnly\":%s,\"revision\":%llu,\"held\":%llu}",cargoMotion.poseOnly?"true":"false",static_cast<unsigned long long>(cargoMotion.revision),static_cast<unsigned long long>(passengerActor.held));
    std::fprintf(f,",\"cargoLocomotion\":{\"active\":%s,\"tick\":%u,\"requestTick\":%u,\"epoch\":%u,\"pair\":%llu,\"speed\":%.5f,\"phase\":%.5f,\"crouchMetres\":%.5f,\"offsetMetres\":[%.5f,%.5f,%.5f],\"velocityMetres\":[%.5f,%.5f,%.5f],\"poseCandidates\":%llu,\"poseApplied\":%llu,\"eyeApplied\":%llu,\"poseRejected\":%llu,\"stage\":%u,\"pelvis\":%u,\"head\":%u,\"feet\":[%u,%u]}",
        cargoMotion.active?"true":"false",cargoMotion.tick,cargoMotion.requestTick,cargoMotion.epoch,static_cast<unsigned long long>(cargoMotion.pair),cargoMotion.speed,cargoMotion.phase,cargoMotion.crouchMetres,
        cargoMotion.offsetMetres.x,cargoMotion.offsetMetres.y,cargoMotion.offsetMetres.z,cargoMotion.velocityMetres.x,cargoMotion.velocityMetres.y,cargoMotion.velocityMetres.z,
        static_cast<unsigned long long>(rig.cargoPoseCandidates),static_cast<unsigned long long>(rig.cargoPoseApplied),static_cast<unsigned long long>(rig.cargoEyeApplied),static_cast<unsigned long long>(rig.cargoPoseRejected),rig.cargoRejectStage,rig.cargoPelvis,rig.cargoHead,rig.cargoLeftFoot,rig.cargoRightFoot);
    const auto& passengerIdentity=passengerActor.identity;
    std::fprintf(f,",\"passengerIdentity\":{\"rejectionStage\":%u,\"stage\":%u,\"requestCamera\":%u,\"currentCamera\":%u,\"carrier\":%u,\"entry\":%u,\"child\":%u",
        passengerActor.rejectionStage,passengerIdentity.stage,passengerActor.requestCamera,passengerActor.currentCamera,static_cast<unsigned>(passengerActor.carrier),static_cast<unsigned>(passengerActor.entry),static_cast<unsigned>(passengerActor.child));
    struct PassengerIdentityField {const char* name;std::uint32_t value;};
    const PassengerIdentityField passengerIdentityFields[]={
        {"player",passengerIdentity.player},{"movement",passengerIdentity.movement},{"movementVtable",passengerIdentity.movementVtable},{"movementId",passengerIdentity.movementId},{"action",passengerIdentity.action},{"actionVtable",passengerIdentity.actionVtable},{"actorAlias",passengerIdentity.actorAlias},
        {"geometry",passengerIdentity.geometry},{"geometryOwner",passengerIdentity.geometryOwner},{"parent",passengerIdentity.parent},{"record",passengerIdentity.record},{"recordCarrier",passengerIdentity.recordCarrier},{"playerCarrier",passengerIdentity.playerCarrier},{"recordSlot",passengerIdentity.recordSlot},
        {"carrierGeometry",passengerIdentity.carrierGeometry},{"carrierOwner",passengerIdentity.carrierOwner},{"count",passengerIdentity.count},{"queueChild",passengerIdentity.child},{"queueSlot",passengerIdentity.queueSlot},{"queueEntry",passengerIdentity.entry},{"matches",passengerIdentity.matches}};
    for(const auto& value:passengerIdentityFields)std::fprintf(f,",\"%s\":%u",value.name,value.value);
    std::fprintf(f,"}");
    const auto cargoCollision=NativePassengerCollisionKL::Snapshot();
    std::fprintf(f,",\"cargoCollision\":{\"installed\":%s,\"accepted\":%s,\"calls\":%llu,\"acceptedSteps\":%llu,\"rejected\":%llu,\"rays\":%llu,\"floorHits\":%llu,\"floorMisses\":%llu,\"carrierContacts\":%llu,\"selfContacts\":%llu,\"otherContacts\":%llu,\"unknownContacts\":%llu,\"wallHits\":%llu,\"threadRejected\":%llu,\"stage\":%u,\"tick\":%u,\"thread\":%u,\"reference\":%u,\"geometry\":%u,\"owner\":%u,\"room\":%u,\"carrier\":%u,\"previous\":[%.5f,%.5f,%.5f],\"desired\":[%.5f,%.5f,%.5f],\"contact\":[%.5f,%.5f,%.5f],\"lastUs\":%llu,\"totalUs\":%llu,\"maxUs\":%llu}",
        cargoCollision.installed?"true":"false",cargoCollision.accepted?"true":"false",static_cast<unsigned long long>(cargoCollision.calls),static_cast<unsigned long long>(cargoCollision.acceptedSteps),static_cast<unsigned long long>(cargoCollision.rejected),static_cast<unsigned long long>(cargoCollision.rays),static_cast<unsigned long long>(cargoCollision.floorHits),static_cast<unsigned long long>(cargoCollision.floorMisses),static_cast<unsigned long long>(cargoCollision.carrierContacts),static_cast<unsigned long long>(cargoCollision.selfContacts),static_cast<unsigned long long>(cargoCollision.otherContacts),static_cast<unsigned long long>(cargoCollision.unknownContacts),static_cast<unsigned long long>(cargoCollision.wallHits),static_cast<unsigned long long>(cargoCollision.threadRejected),
        cargoCollision.stage,cargoCollision.tick,cargoCollision.thread,cargoCollision.reference,static_cast<unsigned>(cargoCollision.geometry),static_cast<unsigned>(cargoCollision.owner),static_cast<unsigned>(cargoCollision.room),static_cast<unsigned>(cargoCollision.carrier),cargoCollision.previous.x,cargoCollision.previous.y,cargoCollision.previous.z,cargoCollision.desired.x,cargoCollision.desired.y,cargoCollision.desired.z,cargoCollision.contact.x,cargoCollision.contact.y,cargoCollision.contact.z,
        static_cast<unsigned long long>(cargoCollision.lastUs),static_cast<unsigned long long>(cargoCollision.totalUs),static_cast<unsigned long long>(cargoCollision.maxUs));
    std::fprintf(f,",\"passengerActor\":{\"installed\":%s,\"applied\":%s,\"published\":%llu,\"updates\":%llu,\"translated\":%llu,\"restored\":%llu,\"rejected\":%llu,\"diverged\":%llu,\"aborted\":%llu,\"player\":%u,\"geometry\":%u,\"parent\":%u,\"thread\":%u,\"pair\":%llu,\"localMetres\":[%.5f,%.5f,%.5f],\"nativePosition\":[%.5f,%.5f,%.5f],\"shiftedPosition\":[%.5f,%.5f,%.5f]}",
        passengerActor.installed?"true":"false",passengerActor.applied?"true":"false",static_cast<unsigned long long>(passengerActor.published),static_cast<unsigned long long>(passengerActor.updates),static_cast<unsigned long long>(passengerActor.translated),static_cast<unsigned long long>(passengerActor.restored),static_cast<unsigned long long>(passengerActor.rejected),static_cast<unsigned long long>(passengerActor.diverged),static_cast<unsigned long long>(passengerActor.aborted),static_cast<unsigned>(passengerActor.player),static_cast<unsigned>(passengerActor.geometry),static_cast<unsigned>(passengerActor.parent),passengerActor.simulationThread,static_cast<unsigned long long>(passengerActor.pair),
        passengerActor.localMetres.x,passengerActor.localMetres.y,passengerActor.localMetres.z,passengerActor.nativePosition.x,passengerActor.nativePosition.y,passengerActor.nativePosition.z,passengerActor.shiftedPosition.x,passengerActor.shiftedPosition.y,passengerActor.shiftedPosition.z);
    const auto tutorial=NativeTutorialKL::Snapshot();
    const auto ledgeHint=NativeLedgeHintKL::Snapshot();
    std::fprintf(f,",\"ledgeHint\":{\"installed\":%s,\"available\":%s,\"probes\":%llu,\"hits\":%llu,\"rejected\":%llu}",
        ledgeHint.installed?"true":"false",ledgeHint.available?"true":"false",static_cast<unsigned long long>(ledgeHint.probes),static_cast<unsigned long long>(ledgeHint.hits),static_cast<unsigned long long>(ledgeHint.rejected));
    std::fprintf(f,",\"coverTutorial\":{\"installed\":%s,\"ticks\":%llu,\"shotTokens\":%llu,\"shotCredits\":%llu,\"timeCredits\":%llu,\"rejected\":%llu,\"ambiguous\":%llu,\"stage\":%u,\"event\":%u,\"elapsedMs\":%u,\"listed\":%u,\"named\":%u,\"active\":%u,\"player\":%u,\"monitor\":%u,\"root\":%u}",
        tutorial.installed?"true":"false",static_cast<unsigned long long>(tutorial.ticks),static_cast<unsigned long long>(tutorial.shotTokens),static_cast<unsigned long long>(tutorial.shotCredits),static_cast<unsigned long long>(tutorial.timeCredits),static_cast<unsigned long long>(tutorial.rejected),static_cast<unsigned long long>(tutorial.ambiguous),tutorial.stage,tutorial.event,tutorial.elapsedMs,tutorial.listed,tutorial.named,tutorial.active,static_cast<unsigned>(tutorial.player),static_cast<unsigned>(tutorial.monitor),static_cast<unsigned>(tutorial.root));
    const auto objectives=NativeObjectiveVisibilityKL::Snapshot();
    std::fprintf(f,",\"objectiveHeldPresentation\":{\"available\":%u,\"added\":%llu,\"views\":%llu,\"rejected\":%llu,\"filteredLive\":%llu,\"copiedLive\":%llu,\"recoveredStyle\":%llu,\"selectionPrepared\":%llu}",
        objectives.available,static_cast<unsigned long long>(objectives.heldAdded),static_cast<unsigned long long>(objectives.heldViews),static_cast<unsigned long long>(objectives.viewRejected),
        static_cast<unsigned long long>(objectives.filteredLive),static_cast<unsigned long long>(objectives.copiedLive),static_cast<unsigned long long>(objectives.recoveredStyle),
        static_cast<unsigned long long>(objectives.selectionPrepared));
    std::fprintf(f,",\"carriedPersonVisibility\":{\"mode\":\"whole-carried-actor-main-eye\",\"installed\":%s,\"frames\":%llu,\"carrying\":%llu,\"candidates\":%llu,\"clipped\":%llu,\"restored\":%llu,\"foreign\":%llu,\"identityRejected\":%llu,\"shaderRejected\":%llu,\"unsupported\":%llu,\"failed\":%llu}",
        carriedPerson.installed?"true":"false",static_cast<unsigned long long>(carriedPerson.frames),
        static_cast<unsigned long long>(carriedPerson.carrying),static_cast<unsigned long long>(carriedPerson.candidates),
        static_cast<unsigned long long>(carriedPerson.clipped),static_cast<unsigned long long>(carriedPerson.restored),
        static_cast<unsigned long long>(carriedPerson.foreign),static_cast<unsigned long long>(carriedPerson.identityRejected),
        static_cast<unsigned long long>(carriedPerson.shaderRejected),static_cast<unsigned long long>(carriedPerson.unsupported),
        static_cast<unsigned long long>(carriedPerson.failed));
    std::fprintf(f,",\"handsRenderPair\":{\"admitted\":%llu,\"reused\":%llu,\"secondEye\":%llu,\"rejected\":%llu,\"expired\":%llu}",
        static_cast<unsigned long long>(rig.renderPairAdmitted),static_cast<unsigned long long>(rig.renderPairReused),
        static_cast<unsigned long long>(rig.renderPairSecondEye),static_cast<unsigned long long>(rig.renderPairRejected),
        static_cast<unsigned long long>(rig.renderPairExpired));
    NativeReflectionDiagnosticsJSON::Mirror(f,mirrorBump);
    std::fprintf(f,",\"reflectionHistoryKeptAfterMiss\":%llu",static_cast<unsigned long long>(reflectionHistoryKeptAfterMiss.load()));
    // The status worker owns this observer. Never scan VA from a draw hook.
    const bool loadingMemoryMenu=ReadNativeMenu();
    loadingMemoryHeadroom.Observe(nativeLoading.Entries(),nativeLoading.AwaitingWorld()||loadingMemoryMenu,GetTickCount());
    const auto& loadingMemory=loadingMemoryHeadroom.Snapshot();
    std::fprintf(f,",\"loadingMemoryHeadroom\":{\"observed\":%s,\"valid\":%s,\"tick\":%u,\"entryCount\":%llu,\"scans\":%llu,\"queries\":%llu,\"freeBytes\":%llu,\"largestFreeRegion\":%llu,\"committedBytes\":%llu,\"reservedBytes\":%llu,\"freeRegions\":%llu,\"committedRegions\":%llu,\"reservedRegions\":%llu,\"availableVirtualKnown\":%s,\"availableVirtualBytes\":%llu,\"failure\":%u,\"lastError\":%u}",
        loadingMemory.observed?"true":"false",loadingMemory.valid?"true":"false",loadingMemory.tick,
        static_cast<unsigned long long>(loadingMemory.entryCount),static_cast<unsigned long long>(loadingMemory.scanCount),
        static_cast<unsigned long long>(loadingMemory.queryCount),static_cast<unsigned long long>(loadingMemory.freeBytes),
        static_cast<unsigned long long>(loadingMemory.largestFreeRegion),static_cast<unsigned long long>(loadingMemory.committedBytes),
        static_cast<unsigned long long>(loadingMemory.reservedBytes),static_cast<unsigned long long>(loadingMemory.freeRegions),
        static_cast<unsigned long long>(loadingMemory.committedRegions),static_cast<unsigned long long>(loadingMemory.reservedRegions),
        loadingMemory.availableVirtualKnown?"true":"false",static_cast<unsigned long long>(loadingMemory.availableVirtualBytes),
        static_cast<unsigned>(loadingMemory.failure),loadingMemory.lastError);
    std::fprintf(f,",\"trackedMaterialBinding\":{\"worldSourceVerified\":%s,\"bonesLightCacheVerified\":%s,\"instanceWorld\":%llu,\"viewAlignedWorld\":%llu,\"weaponLightCacheInvalidated\":%llu,\"bodyLightCacheInvalidated\":%llu}",
        weapon.worldSourceVerified?"true":"false",weapon.bonesLightCacheVerified?"true":"false",static_cast<unsigned long long>(weapon.instanceWorldTracked),
        static_cast<unsigned long long>(weapon.viewAlignedWorldTracked),static_cast<unsigned long long>(weapon.bonesLightCacheInvalidated),static_cast<unsigned long long>(rig.bonesLightCacheInvalidated));
    std::fprintf(f,",\"skinLighting\":{\"candidates\":%llu,\"matched\":%llu,\"draws\":%llu,\"restored\":%llu,\"failed\":%llu,\"cacheHits\":%llu,\"cacheMisses\":%llu}",
        static_cast<unsigned long long>(skinLighting.candidates),static_cast<unsigned long long>(skinLighting.matched),static_cast<unsigned long long>(skinLighting.draws),
        static_cast<unsigned long long>(skinLighting.restored),static_cast<unsigned long long>(skinLighting.failed),static_cast<unsigned long long>(skinLighting.cacheHits),static_cast<unsigned long long>(skinLighting.cacheMisses));
    {
        const auto evidence=NativeHandsKL::SnapshotSkinPassPair();
        const auto& sample=evidence.lastDifferent.valid?evidence.lastDifferent:evidence.last;
        std::fprintf(f,",\"skinPassPair\":{\"colours\":%llu,\"compared\":%llu,\"different\":%llu,\"sourceChanged\":%llu,\"rejected\":%llu,\"retainedMismatch\":%s,\"valid\":%s,\"tick\":%u,\"pair\":%llu,\"epoch\":%llu,\"entry\":%llu,\"state\":%llu,\"sourceEqual\":%s,\"paletteEqual\":%s,\"worldEqual\":%s,\"maxPaletteDelta\":%.9g,\"maxWorldDelta\":%.9g}",
            static_cast<unsigned long long>(evidence.colours),static_cast<unsigned long long>(evidence.compared),
            static_cast<unsigned long long>(evidence.different),static_cast<unsigned long long>(evidence.sourceChanged),
            static_cast<unsigned long long>(evidence.rejected),evidence.lastDifferent.valid?"true":"false",sample.valid?"true":"false",sample.tick,
            static_cast<unsigned long long>(sample.pair),static_cast<unsigned long long>(sample.epoch),
            static_cast<unsigned long long>(sample.entry),static_cast<unsigned long long>(sample.state),sample.sourceEqual?"true":"false",
            sample.paletteEqual?"true":"false",sample.worldEqual?"true":"false",sample.maxPaletteDelta,sample.maxWorldDelta);
    }
    std::fprintf(f,",\"heldGrenadeVisual\":{\"meshTracked\":%llu,\"armFrames\":%llu}",
        static_cast<unsigned long long>(weapon.heldGrenadeMeshTracked),static_cast<unsigned long long>(rig.heldGrenadeArmFrames));
    std::fprintf(f,",\"weaponMaterialEvidence\":{\"samples\":%llu,\"worldMismatches\":%llu,\"shaderLoads\":%llu,\"shaderCacheHits\":%llu,\"last\":[",
        static_cast<unsigned long long>(weapon.materialSamples),static_cast<unsigned long long>(weapon.materialWorldMismatches),
        static_cast<unsigned long long>(weapon.materialShaderLoads),static_cast<unsigned long long>(weapon.materialShaderCacheHits));
    const auto writeMaterialFloats=[&](const float* values,unsigned count){
        std::fputc('[',f);for(unsigned i=0;i<count;++i){if(i)std::fputc(',',f);if(std::isfinite(values[i]))std::fprintf(f,"%.8g",values[i]);else std::fputs("null",f);}std::fputc(']',f);
    };
    const auto writeMaterialPose=[&](const NativeCameraMath::CameraPose& pose){float values[12];static_assert(sizeof(values)==sizeof(pose),"native pose serialization");std::memcpy(values,&pose,sizeof(values));writeMaterialFloats(values,12);};
    for(unsigned slot=0;slot<2;++slot){NativeWeaponKL::MaterialSample sample;const bool valid=NativeWeaponKL::SnapshotMaterial(slot!=0,sample);
        std::fprintf(f,"%s{\"valid\":%s,\"supportHeld\":%s,\"worldMatch\":%s,\"tick\":%u,\"pair\":%llu,\"pass\":%u,\"vsHash\":%u,\"psHash\":%u,\"reference\":%u,\"entry\":%u,\"state\":%u,\"weapon\":%u,\"model\":%u,\"lightSource\":%u,\"sourceWorld\":",
            slot?",":"",valid&&sample.valid?"true":"false",sample.supportHeld?"true":"false",sample.worldMatch?"true":"false",sample.tick,
            static_cast<unsigned long long>(sample.pair),sample.pass,sample.vsHash,sample.psHash,sample.reference,static_cast<unsigned>(sample.entry),static_cast<unsigned>(sample.state),
            static_cast<unsigned>(sample.weapon),static_cast<unsigned>(sample.model),static_cast<unsigned>(sample.lightSource));
        writeMaterialPose(sample.sourceWorld);std::fputs(",\"trackedWorld\":",f);writeMaterialPose(sample.trackedWorld);std::fputs(",\"actualWorld\":",f);writeMaterialPose(sample.actualWorld);
        std::fputs(",\"view\":",f);writeMaterialPose(sample.view);std::fputs(",\"projection\":",f);writeMaterialFloats(sample.projection,16);
        std::fprintf(f,",\"mvpRegister\":%u,\"mvpCount\":%u,\"mvp\":",sample.mvpRegister,sample.mvpCount);writeMaterialFloats(sample.mvp,16);
        std::fprintf(f,",\"eyeRegister\":%u,\"eyeCount\":%u,\"eye\":",sample.eyeRegister,sample.eyeCount);writeMaterialFloats(sample.eye,4);
        std::fprintf(f,",\"vsLightRegister\":%u,\"vsLightCount\":%u,\"vsLowLights\":%s,\"vsLights\":",sample.vsLightRegister,sample.vsLightCount,sample.vsLowLights?"true":"false");writeMaterialFloats(sample.vsLights,28);
        std::fprintf(f,",\"psLightRegister\":%u,\"psLightCount\":%u,\"psLowLights\":%s,\"psLights\":",sample.psLightRegister,sample.psLightCount,sample.psLowLights?"true":"false");writeMaterialFloats(sample.psLights,28);
        std::fputs(",\"nativeLights\":",f);writeMaterialFloats(sample.nativeLights,28);std::fputs(",\"renderState\":[",f);
        for(unsigned i=0;i<9;++i)std::fprintf(f,"%s%u",i?",":"",sample.renderState[i]);std::fputs("]}",f);
    }
    std::fputs("]}",f);
    NativeWeaponKL::MaterialSequence materialSequence;
    const bool sequenceValid=NativeWeaponKL::SnapshotMaterialSequence(materialSequence);
    std::fprintf(f,",\"weaponMaterialSequence\":{\"enabled\":%s,\"valid\":%s,\"supportHeld\":%s,\"tick\":%u,\"pair\":%llu,\"reference\":%u,\"weapon\":%u,\"draws\":[",
        materialSequenceEnabled.load()?"true":"false",sequenceValid?"true":"false",materialSequence.supportHeld?"true":"false",
        materialSequence.tick,static_cast<unsigned long long>(materialSequence.pair),materialSequence.reference,static_cast<unsigned>(materialSequence.weapon));
    for(unsigned i=0;i<materialSequence.count&&i<NativeWeaponKL::MaterialSequenceLimit;++i){const auto& draw=materialSequence.draws[i];
        std::fprintf(f,"%s{\"pass\":%u,\"vsHash\":%u,\"psHash\":%u,\"entry\":%u,\"geometry\":%u,\"renderTarget\":%u,\"depthTarget\":%u,\"worldMatch\":%s,\"renderState\":[",
            i?",":"",draw.pass,draw.vsHash,draw.psHash,static_cast<unsigned>(draw.entry),static_cast<unsigned>(draw.geometry),
            static_cast<unsigned>(draw.renderTarget),static_cast<unsigned>(draw.depthTarget),draw.worldMatch?"true":"false");
        for(unsigned j=0;j<5;++j)std::fprintf(f,"%s%u",j?",":"",draw.renderState[j]);std::fputs("],\"textures\":[",f);
        for(unsigned j=0;j<4;++j)std::fprintf(f,"%s%u",j?",":"",static_cast<unsigned>(draw.textures[j]));std::fputs("]}",f);
    }
    std::fputs("]}",f);
    std::fprintf(f,",\"objectiveVisibility\":{\"installed\":%s,\"forceHeld\":%s,\"colorCalls\":%llu,\"forced\":%llu,\"rejected\":%llu,\"mapCalls\":%llu,\"mapSuppressed\":%llu,\"goalsObserved\":%llu,\"bobSuppressed\":%llu,\"pairReused\":%llu,\"aspectCorrected\":%llu}",
        objectives.installed?"true":"false",objectives.forceHeld?"true":"false",static_cast<unsigned long long>(objectives.colorCalls),static_cast<unsigned long long>(objectives.forced),static_cast<unsigned long long>(objectives.rejected),static_cast<unsigned long long>(objectives.mapCalls),static_cast<unsigned long long>(objectives.mapSuppressed),static_cast<unsigned long long>(objectives.goalsObserved),static_cast<unsigned long long>(hud.nativeObjectiveBobSuppressed),static_cast<unsigned long long>(hud.nativeObjectivePairsReused),static_cast<unsigned long long>(hud.nativeMarkerAspectCorrected));
    if(NativeHudKL::ObjectiveTraceEnabled()){
        const auto trace=NativeHudKL::SnapshotObjectiveTrace();
        std::fprintf(f,",\"objectivePairTrace\":{\"written\":%llu,\"overwritten\":%llu,\"events\":[",
            static_cast<unsigned long long>(trace.written),static_cast<unsigned long long>(trace.overwritten));
        if(trace.written<objectiveWritten)objectiveWritten=0;
        bool firstObjective=true;
        for(unsigned i=0;i<trace.count;++i){const auto& event=trace.events[i];if(event.sequence<=objectiveWritten)continue;
            std::fprintf(f,"%s{\"seq\":%llu,\"pair\":%llu,\"producer\":%llu,\"tick\":%u,\"phase\":%u,\"stage\":%u,\"eye\":%u,\"slot\":%u,\"stamp\":%u,\"record\":%u,\"entity\":%u,\"geometry\":%u,\"entry\":%u,\"clipValid\":%s,\"target\":",
                firstObjective?"":",",static_cast<unsigned long long>(event.sequence),static_cast<unsigned long long>(event.pair),static_cast<unsigned long long>(event.producer),event.tick,event.phase,event.stage,event.eye,event.slot,event.stamp,
                static_cast<unsigned>(event.record),static_cast<unsigned>(event.entity),static_cast<unsigned>(event.geometry),static_cast<unsigned>(event.entry),event.clipValid?"true":"false");
            writeMaterialFloats(event.target,3);std::fputs(",\"clip\":",f);writeMaterialFloats(event.clip,3);std::fputc('}',f);firstObjective=false;objectiveWritten=event.sequence;
        }
        std::fputs("]}",f);
    }
    {
        const auto probe=NativePostfilterProbeKL::Snapshot();
        if(probe.installed){
            std::fprintf(f,",\"postfilterStageProbe\":{\"installed\":true,\"calls\":%llu,\"sampled\":%llu,\"rejected\":%llu,\"frameUnavailable\":%llu,\"written\":%llu,\"overwritten\":%llu,\"events\":[",
                static_cast<unsigned long long>(probe.calls),static_cast<unsigned long long>(probe.sampled),static_cast<unsigned long long>(probe.rejected),static_cast<unsigned long long>(probe.frameUnavailable),static_cast<unsigned long long>(probe.written),static_cast<unsigned long long>(probe.overwritten));
            const auto writeStage=[&](const NativePostfilterProbeKL::StageState& state){
                const float values[]={state.lod,state.amount};
                std::fprintf(f,"{\"postFilter\":%u,\"renderer\":%u,\"lodAmount\":",static_cast<unsigned>(state.postFilter),static_cast<unsigned>(state.renderer));
                writeMaterialFloats(values,2);std::fprintf(f,",\"sequence\":%u,\"viewport\":[%u,%u,%u,%u],\"resources\":[",state.sequence,state.viewport[0],state.viewport[1],state.viewport[2],state.viewport[3]);
                for(unsigned i=0;i<13;++i)std::fprintf(f,"%s%u",i?",":"",state.resources[i]);
                std::fprintf(f,"],\"shader\":[%u,%u],\"maskVariant\":%u,\"resolveFlag\":%u,\"auxRenderer\":%u,\"auxValid\":%s,\"maskTexture\":%u,\"maskSurface\":%u,\"resolveSurface\":%u}",
                    state.shader[0],state.shader[1],state.maskVariant,state.resolveFlag,state.auxRenderer,state.auxValid?"true":"false",state.maskTexture,state.maskSurface,state.resolveSurface);
            };
            if(probe.written<postfilterWritten)postfilterWritten=0;
            bool firstPostfilter=true;
            for(unsigned i=0;i<probe.count&&i<NativePostfilterProbeKL::Capacity;++i){const auto& event=probe.events[i];if(event.sequence<=postfilterWritten)continue;
                std::fprintf(f,"%s{\"sequence\":%llu,\"frameId\":%llu,\"eye\":%u,\"tick\":%u,\"thread\":%u,\"frameKnown\":%s,\"beforeValid\":%s,\"afterValid\":%s,\"before\":",
                    firstPostfilter?"":",",static_cast<unsigned long long>(event.sequence),static_cast<unsigned long long>(event.frame.id),event.frame.eye,event.frame.tick,event.frame.thread,event.frameKnown?"true":"false",event.beforeValid?"true":"false",event.afterValid?"true":"false");
                writeStage(event.before);std::fputs(",\"after\":",f);writeStage(event.after);std::fputc('}',f);firstPostfilter=false;postfilterWritten=event.sequence;
            }
            std::fputs("]}",f);
        }
    }
    if(NativeReflectionProbeKL::Enabled()){
        const auto probe=NativeReflectionProbeKL::Snapshot();
        std::fprintf(f,",\"reflectionMaterialProbe\":{\"enabled\":true,\"samples\":%llu,\"shaderReads\":%llu,\"ctab\":%llu,\"named\":%llu,\"recorded\":%llu,\"latestSequence\":%llu,\"shaderUnavailable\":%llu,\"historyOverwritten\":%llu,\"history\":[",
            static_cast<unsigned long long>(probe.samples),static_cast<unsigned long long>(probe.shaderReads),static_cast<unsigned long long>(probe.ctab),static_cast<unsigned long long>(probe.named),
            static_cast<unsigned long long>(probe.recorded),static_cast<unsigned long long>(probe.latestSequence),static_cast<unsigned long long>(probe.shaderUnavailable),static_cast<unsigned long long>(probe.historyOverwritten));
        if(probe.latestSequence<reflectionWritten)reflectionWritten=0;
        bool firstReflection=true;
        for(unsigned n=0;n<probe.historyCount&&n<NativeReflectionProbeKL::HistoryLimit;++n){const auto& sample=probe.history[n];if(sample.sequence<=reflectionWritten)continue;
            std::fprintf(f,"%s{\"sequence\":%llu,\"tick\":%u,\"frameId\":%llu,\"shaderHash\":%u,\"valid\":%s,\"ctabKnown\":%s,\"reflectionMask\":%u,\"refractionMask\":%u,\"environmentMask\":%u,\"maskSampler\":%u,\"falloffSampler\":%u,\"target\":%u,\"targetWidth\":%u,\"targetHeight\":%u,\"targetFormat\":%u,\"viewport\":[%u,%u,%u,%u],\"states\":[%u,%u,%u],\"textures\":[",
                firstReflection?"":",",static_cast<unsigned long long>(sample.sequence),sample.tick,static_cast<unsigned long long>(sample.frameId),sample.shaderHash,sample.valid?"true":"false",sample.ctabKnown?"true":"false",
                static_cast<unsigned>(sample.samplers.reflection),static_cast<unsigned>(sample.samplers.refraction),static_cast<unsigned>(sample.samplers.environment),static_cast<unsigned>(sample.samplers.mask),static_cast<unsigned>(sample.samplers.falloff),static_cast<unsigned>(sample.target),
                sample.targetWidth,sample.targetHeight,sample.targetFormat,sample.viewportX,sample.viewportY,sample.viewportWidth,sample.viewportHeight,sample.zEnabled,sample.zWrite,sample.alphaBlend);
            const unsigned mask=sample.valid?(sample.samplers.reflection|sample.samplers.refraction|sample.samplers.environment|sample.samplers.mask|sample.samplers.falloff):15u;bool first=true;
            for(unsigned i=0;i<16;++i)if(mask&(1u<<i)){const auto& t=sample.textures[i];std::fprintf(f,"%s[%u,%u,%u,%u,%u,%u]",first?"":",",i,static_cast<unsigned>(t.identity),t.kind,t.width,t.height,t.format);first=false;}
            std::fprintf(f,"],\"vertexHash\":%u,\"vertexCtab\":%s,\"eyeRegister\":[%u,%u],\"cubeRegister\":[%u,%u],\"eyeRead\":%s,\"cubeRead\":%s,\"eye\":",
                sample.vertexHash,sample.vertexCtab?"true":"false",static_cast<unsigned>(sample.vertex.eye.start),static_cast<unsigned>(sample.vertex.eye.count),
                static_cast<unsigned>(sample.vertex.cube.start),static_cast<unsigned>(sample.vertex.cube.count),sample.eyeRead?"true":"false",sample.cubeRead?"true":"false");
            writeMaterialFloats(sample.eye,4);std::fputs(",\"cube\":",f);writeMaterialFloats(sample.cube,16);
            NativeReflectionDiagnosticsJSON::Probe(f,sample);std::fputc('}',f);firstReflection=false;reflectionWritten=sample.sequence;
        }
        std::fputs("]}",f);
    }
    std::fprintf(f,",\"pickupHeadViews\":%llu,\"shadowHead\":{\"installed\":%s,\"calls\":%llu,\"scopes\":%llu,\"palettes\":%llu},\"weaponCalibration\":{\"rebuilds\":%llu,\"rejected\":%llu,\"maxScaleError\":%.8g}",
        static_cast<unsigned long long>(native.pickupHeadViews),rig.shadowPassInstalled?"true":"false",static_cast<unsigned long long>(rig.shadowPassCalls),static_cast<unsigned long long>(rig.shadowPassScopes),static_cast<unsigned long long>(rig.shadowHeadPalettes),static_cast<unsigned long long>(weapon.calibrationRebuilds),static_cast<unsigned long long>(weapon.calibrationRejected),weapon.calibrationMaxScaleError);
    std::fprintf(f,",\"weaponChildOwnership\":{\"nonRoot\":%llu,\"toRoot\":%llu}",
        static_cast<unsigned long long>(weapon.heldOwnerNonRoot),static_cast<unsigned long long>(weapon.heldOwnerNonRootToRoot));
    std::fprintf(f,",\"paletteLifetime\":{\"restored\":%llu,\"replaced\":%llu,\"rejected\":%llu,\"resumed\":%llu,\"fallback\":%llu}",
        static_cast<unsigned long long>(rig.paletteLeaseRestored),static_cast<unsigned long long>(rig.paletteLeaseReplaced),static_cast<unsigned long long>(rig.paletteLeaseRejected),static_cast<unsigned long long>(rig.paletteLeaseResumed),static_cast<unsigned long long>(rig.paletteLeaseFallback));
    std::fprintf(f,",\"skinDrawEvidence\":{\"samples\":%llu,\"rejected\":%llu,\"constantUploads\":%llu,\"constantMismatches\":%llu,\"last\":[",
        static_cast<unsigned long long>(rig.skinDrawSamples),static_cast<unsigned long long>(rig.skinDrawRejected),static_cast<unsigned long long>(rig.skinConstantUploads),static_cast<unsigned long long>(rig.skinConstantMismatches));
    for(unsigned slot=0;slot<4;++slot){
        const bool held=(slot&1u)!=0;
        NativeHandsKL::SkinMaterialSample sample;const bool valid=NativeHandsKL::SnapshotSkinMaterial(held,sample,slot>=2);
        std::fprintf(f,"%s{\"valid\":%s,\"support\":%s,\"gestureHeld\":%s,\"coupled\":%s,\"rawGrip\":%.5f,\"shadow\":%s,\"tick\":%u,\"pair\":%llu,\"pass\":%u,\"bones\":%u,\"entry\":%u,\"state\":%u,\"package\":%u,\"technique\":%u,\"vsHash\":%u,\"psHash\":%u,\"declarationHash\":%u,\"renderStateHash\":%u,\"sourcePalette\":%u,\"renderPalette\":%u,\"sourceHash\":%u,\"paletteHash\":%u,\"constantHash\":%u,\"startRegister\":%u,\"registerCount\":%u,\"constantsCompared\":%s,\"constantsMatch\":%s,\"textures\":[%u,%u,%u,%u],\"textureShapes\":[%u,%u,%u,%u]}",
            slot?",":"",valid&&sample.valid?"true":"false",sample.support?"true":"false",sample.gestureHeld?"true":"false",sample.coupled?"true":"false",sample.rawGrip,sample.shadow?"true":"false",sample.tick,static_cast<unsigned long long>(sample.pair),sample.pass,sample.bones,
            static_cast<unsigned>(sample.entry),static_cast<unsigned>(sample.state),static_cast<unsigned>(sample.package),static_cast<unsigned>(sample.technique),sample.vsHash,sample.psHash,sample.declarationHash,sample.renderStateHash,
            static_cast<unsigned>(sample.sourcePalette),static_cast<unsigned>(sample.renderPalette),sample.sourceHash,sample.paletteHash,sample.constantHash,sample.startRegister,sample.registerCount,sample.constantsCompared?"true":"false",sample.constantsMatch?"true":"false",
            static_cast<unsigned>(sample.textures[0]),static_cast<unsigned>(sample.textures[1]),static_cast<unsigned>(sample.textures[2]),static_cast<unsigned>(sample.textures[3]),sample.textureShapes[0],sample.textureShapes[1],sample.textureShapes[2],sample.textureShapes[3]);
    }
    std::fprintf(f,"]}");
    std::fprintf(f,",\"authoredHeading\":{\"views\":%llu,\"turns\":%llu}",static_cast<unsigned long long>(native.authoredHeadingViews),static_cast<unsigned long long>(native.authoredHeadingTurns));
    std::fprintf(f,",\"cinematicReadPolicy\":{\"committedSceneReused\":%llu,\"freshNativeReads\":%llu,\"cameraDrawReused\":%llu,\"cameraNativeReads\":%llu,\"panelDrawReused\":%llu,\"panelNativeReads\":%llu}",
        static_cast<unsigned long long>(native.cinematicDrawReused),static_cast<unsigned long long>(native.cinematicNativeReads),
        static_cast<unsigned long long>(native.cinematicCameraDrawReused),static_cast<unsigned long long>(native.cinematicCameraNativeReads),
        static_cast<unsigned long long>(native.cinematicPanelDrawReused),static_cast<unsigned long long>(native.cinematicPanelNativeReads));
    std::fprintf(f,",\"passengerRoomscale\":{\"enabled\":false,\"frames\":0,\"intentValid\":false,\"localMetres\":[0,0,0]}");
    std::fprintf(f,",\"passengerSeated\":{\"only\":%s,\"frames\":%llu,\"rejected\":%llu}",passengerActor.seatedOnly?"true":"false",
        static_cast<unsigned long long>(passengerActor.seatedFrames),static_cast<unsigned long long>(passengerActor.seatedRejected));
    std::fprintf(f,",\"passengerEntry\":{\"frames\":%llu,\"applied\":%llu,\"stage\":%u,\"controlsKnown\":%s,\"controlsBlocked\":%s,\"active\":%s,\"clearanceMetres\":%.3f}",
        static_cast<unsigned long long>(passengerEntryFrames.load()),static_cast<unsigned long long>(passengerEntryApplied.load()),passengerEntryStage.load(),
        passengerEntryKnown.load()?"true":"false",passengerEntryBlocked.load()?"true":"false",passengerEntryActive.load()?"true":"false",NativeCarriedCamera::EntryClearance::Metres);
    std::fprintf(f,",\"carriedCamera\":{\"frames\":%llu,\"parentHead\":%llu,\"currentHeadFallback\":%llu,\"mountedBase\":%llu,\"actorBase\":%llu,\"referenceKind\":%u}",
        static_cast<unsigned long long>(carriedFrames.load()),static_cast<unsigned long long>(carriedParentHeadFrames.load()),static_cast<unsigned long long>(carriedCurrentHeadFrames.load()),static_cast<unsigned long long>(carriedMountedBaseFrames.load()),static_cast<unsigned long long>(carriedActorBaseFrames.load()),carriedReferenceKind.load());
    const auto mountedAim=NativeMountedAimKL::Snapshot();
    std::fprintf(f,",\"mountedOptions\":{\"reticleEnabled\":%s,\"mouseAim\":%s,\"nativeMouseTargets\":%llu,\"reticleSamples\":%llu,\"reticleReads\":%llu,\"reticleRejected\":%llu,\"style\":%u,\"styledProducers\":%llu,\"styleRejected\":%llu}",
        settings.mountedReticle?"true":"false",mountedAim.mouseAim?"true":"false",
        static_cast<unsigned long long>(mountedAim.mouseNativeTargets),static_cast<unsigned long long>(mountedAim.reticleSamples),
        static_cast<unsigned long long>(mountedAim.reticleReads),static_cast<unsigned long long>(mountedAim.reticleRejected),
        settings.reticleStyle.load(),static_cast<unsigned long long>(gameplayHud.reticleStyled),static_cast<unsigned long long>(gameplayHud.reticleStyleRejected));
    std::fprintf(f,",\"mountedAim\":{\"installed\":%s,\"updates\":%llu,\"seeds\":%llu,\"seedRejected\":%llu,\"setterCalls\":%llu,\"applied\":%llu,\"rejected\":%llu,\"foreignCalls\":%llu,\"thread\":%u,\"ageMs\":%u,\"player\":%u,\"controller\":%u,\"target\":[%.5f,%.5f,%.5f],\"identityStage\":%u,\"seedStage\":%u,\"identityRecord\":%u,\"identityController\":%u,\"identityEntity\":%u,\"identityVtable\":%u,\"identityRecordsOffset\":%u}",
        mountedAim.installed?"true":"false",static_cast<unsigned long long>(mountedAim.updates),static_cast<unsigned long long>(mountedAim.seeds),static_cast<unsigned long long>(mountedAim.seedRejected),static_cast<unsigned long long>(mountedAim.setterCalls),static_cast<unsigned long long>(mountedAim.applied),static_cast<unsigned long long>(mountedAim.rejected),static_cast<unsigned long long>(mountedAim.foreignCalls),mountedAim.thread,mountedAim.ageMs,static_cast<unsigned>(mountedAim.player),static_cast<unsigned>(mountedAim.controller),mountedAim.target.x,mountedAim.target.y,mountedAim.target.z,mountedAim.identityStage,mountedAim.seedStage,
        mountedAim.identityProbe.record,mountedAim.identityProbe.controller,mountedAim.identityProbe.entity,mountedAim.identityProbe.vtable,mountedAim.identityProbe.recordsOffset);
    const auto grenadeVisual=NativeGrenadeVisualKL::Snapshot();
    const auto invulnerability=NativeInvulnerabilityKL::Snapshot();
    std::fprintf(f,",\"nativeStableTorsoFrames\":%llu",static_cast<unsigned long long>(rig.stableTorsoFrames));
    std::fprintf(f,",\"actionBodyFollow\":{\"adjusted\":%llu,\"rejected\":%llu}",static_cast<unsigned long long>(body.actionAdjusted),static_cast<unsigned long long>(body.actionRejected));
    std::fprintf(f,",\"mountedControl\":{\"epoch\":%u,\"physicalTick\":%u,\"stickTick\":%u,\"localYawRadians\":%.5f,\"localPitchRadians\":%.5f,\"stick\":[%.5f,%.5f],\"carrierForward\":[%.5f,%.5f,%.5f]}",
        mountedAim.controlEpoch,mountedAim.physicalTick,mountedAim.stickTick,mountedAim.localYaw,mountedAim.localPitch,mountedAim.stickX,mountedAim.stickY,
        mountedAim.carrierForward.x,mountedAim.carrierForward.y,mountedAim.carrierForward.z);
    std::fprintf(f,",\"nativeNeutralStandingFrames\":%llu",static_cast<unsigned long long>(rig.neutralStandingFrames));
    std::fprintf(f,",\"nativeNeutralStandingEyeFrames\":%llu",static_cast<unsigned long long>(rig.neutralStandingEyeFrames));
    std::fprintf(f,",\"passengerHands\":{\"pinned\":[%llu,%llu],\"nativeReachCorrected\":[%llu,%llu],\"nativeReachErrorMetres\":[%.5f,%.5f],\"headsHidden\":%llu}",
        static_cast<unsigned long long>(rig.passengerHandsPinned[0]),static_cast<unsigned long long>(rig.passengerHandsPinned[1]),
        static_cast<unsigned long long>(rig.passengerNativeReachCorrected[0]),static_cast<unsigned long long>(rig.passengerNativeReachCorrected[1]),
        rig.passengerNativeReachErrorMetres[0],rig.passengerNativeReachErrorMetres[1],static_cast<unsigned long long>(rig.passengerHeadsHidden));
    std::fprintf(f,",\"testingInvulnerability\":{\"enabled\":%s,\"installed\":%s,\"calls\":%llu,\"blocked\":%llu,\"passed\":%llu,\"healthBlocked\":%llu,\"healthRestored\":%llu}",
        invulnerability.enabled?"true":"false",invulnerability.installed?"true":"false",
        static_cast<unsigned long long>(invulnerability.calls),static_cast<unsigned long long>(invulnerability.blocked),static_cast<unsigned long long>(invulnerability.passed),static_cast<unsigned long long>(invulnerability.healthBlocked),static_cast<unsigned long long>(invulnerability.healthRestored));
    std::fprintf(f,",\"cinemaViewport\":{\"expanded\":%llu,\"restoreFailed\":%llu,\"attempts\":%llu,\"stage\":%u,\"viewport\":[%u,%u,%u,%u]}",
        static_cast<unsigned long long>(effects.cinematicViewportExpanded),static_cast<unsigned long long>(effects.cinematicViewportRestoreFailed),
        static_cast<unsigned long long>(effects.cinematicViewportAttempts),effects.cinematicViewportStage,
        effects.cinematicLastViewport[0],effects.cinematicLastViewport[1],effects.cinematicLastViewport[2],effects.cinematicLastViewport[3]);
    std::fprintf(f,",\"grenadeVisual\":{\"verified\":%s,\"frames\":%llu,\"candidates\":%llu,\"owned\":%llu,\"suppressed\":%llu,\"rejected\":%llu}",
        grenadeVisual.verified?"true":"false",static_cast<unsigned long long>(grenadeVisual.frames),static_cast<unsigned long long>(grenadeVisual.candidates),static_cast<unsigned long long>(grenadeVisual.owned),static_cast<unsigned long long>(grenadeVisual.suppressed),static_cast<unsigned long long>(grenadeVisual.rejected));
    std::fprintf(f,",\"physicalReloadPresentation\":{\"worldDraws\":%u,\"worldRejected\":%u,\"impacts\":%u,\"sounds\":%u,\"nativeCues\":%u,\"worldActive\":%u,\"spareDraws\":%u,\"dryClicks\":%u}",weapon.physicalWorldDraws,weapon.physicalWorldRejected,weapon.physicalImpacts,weapon.physicalSounds,weapon.physicalNativeCues,weapon.physicalWorldActive,weapon.physicalSpareDraws,weapon.physicalDryClicks);
    std::fprintf(f,",\"physicalReloadDiagnostic\":{\"enabled\":%s,\"loaderReady\":%s,\"loader\":%u,\"phase\":%u,\"ejected\":%u,\"taken\":%u,\"inserted\":%u,\"rejected\":%u,\"meshRemapped\":%u,\"meshHidden\":%u}",weapon.physicalDiagnostic?"true":"false",weapon.physicalLoaderReady?"true":"false",weapon.physicalLoader,weapon.physicalPhase,weapon.physicalEjected,weapon.physicalTaken,weapon.physicalInserted,weapon.physicalRejected,weapon.physicalRemapped,weapon.physicalHidden);
    std::fprintf(f,",\"physicalReloadAmmo\":{\"profile\":%s,\"observed\":%llu,\"rejected\":%llu,\"count\":%d,\"reserve\":%d,\"capacity\":%d,\"category\":%u,\"manualActive\":false}",
        weapon.reloadAmmoProfile?"true":"false",static_cast<unsigned long long>(weapon.reloadAmmoObserved),static_cast<unsigned long long>(weapon.reloadAmmoRejected),weapon.reloadAmmoCount,weapon.reloadAmmoReserve,weapon.reloadAmmoCapacity,weapon.reloadAmmoCategory);
    std::fprintf(f,",\"trackedReload\":{\"installed\":%s,\"requests\":%llu,\"blocked\":%llu,\"manual\":%llu,\"empty\":%llu,\"unproved\":%llu}",
        weapon.reloadInstalled?"true":"false",static_cast<unsigned long long>(weapon.reloadRequests),static_cast<unsigned long long>(weapon.reloadBlocked),static_cast<unsigned long long>(weapon.reloadManual),static_cast<unsigned long long>(weapon.reloadEmpty),static_cast<unsigned long long>(weapon.reloadUnproved));
    const auto actionClimb=NativeCoverKL::ActionClimbSnapshot();
    std::fprintf(f,",\"actionClimb\":{\"installed\":%s,\"calls\":%llu,\"requests\":%llu,\"assisted\":%llu,\"rejected\":%llu}",
        actionClimb.installed?"true":"false",static_cast<unsigned long long>(actionClimb.calls),
        static_cast<unsigned long long>(actionClimb.requests),static_cast<unsigned long long>(actionClimb.assisted),static_cast<unsigned long long>(actionClimb.rejected));
    std::fprintf(f,",\"grenadeAim\":{\"installed\":%s,\"updates\":%llu,\"localUpdates\":%llu,\"targets\":%llu,\"applied\":%llu,\"gazeRejected\":%llu,\"contextRejected\":%llu,\"thread\":%u,\"proofStage\":%u,\"moverVtable\":%u,\"direction\":[%.5f,%.5f,%.5f]},\"scopeFire\":{\"calls\":%llu,\"applied\":%llu,\"rejected\":%llu,\"rayHits\":%llu,\"rayMisses\":%llu,\"rayUnavailable\":%llu}",
        weapon.grenadeInstalled?"true":"false",static_cast<unsigned long long>(weapon.grenadeUpdates),static_cast<unsigned long long>(weapon.grenadeLocalUpdates),
        static_cast<unsigned long long>(weapon.grenadeTargets),static_cast<unsigned long long>(weapon.grenadeGazeApplied),static_cast<unsigned long long>(weapon.grenadeGazeRejected),
        static_cast<unsigned long long>(weapon.grenadeContextRejected),weapon.grenadeThread,weapon.grenadeProofStage,weapon.grenadeMoverVtable,
        weapon.grenadeDirection[0],weapon.grenadeDirection[1],weapon.grenadeDirection[2],
        static_cast<unsigned long long>(weapon.scopeFireCalls),static_cast<unsigned long long>(weapon.scopeFireApplied),static_cast<unsigned long long>(weapon.scopeFireRejected),
        static_cast<unsigned long long>(weapon.scopeRayHits),static_cast<unsigned long long>(weapon.scopeRayMisses),static_cast<unsigned long long>(weapon.scopeRayUnavailable));
    std::fprintf(f,",\"grenadeFireAim\":{\"calls\":%llu,\"applied\":%llu,\"rejected\":%llu}",
        static_cast<unsigned long long>(weapon.grenadeFireCalls),static_cast<unsigned long long>(weapon.grenadeFireApplied),
        static_cast<unsigned long long>(weapon.grenadeFireRejected));
    std::fprintf(f,",\"grenadeThrowback\":{\"installed\":%s,\"updates\":%llu,\"owned\":%llu,\"launches\":%llu,\"applied\":%llu,\"rejected\":%llu,\"nativeVelocity\":[%.5f,%.5f,%.5f],\"mappedVelocity\":[%.5f,%.5f,%.5f]}",
        weapon.throwbackInstalled?"true":"false",static_cast<unsigned long long>(weapon.throwbackUpdates),static_cast<unsigned long long>(weapon.throwbackOwnedUpdates),static_cast<unsigned long long>(weapon.throwbackLaunches),static_cast<unsigned long long>(weapon.throwbackApplied),static_cast<unsigned long long>(weapon.throwbackRejected),
        weapon.throwbackNativeVelocity[0],weapon.throwbackNativeVelocity[1],weapon.throwbackNativeVelocity[2],weapon.throwbackMappedVelocity[0],weapon.throwbackMappedVelocity[1],weapon.throwbackMappedVelocity[2]);
    std::fprintf(f,",\"smokeLoopLifecycle\":{\"installed\":%s,\"replacements\":%llu,\"retired\":%llu,\"rejected\":%llu}",
        weapon.smokeLoopInstalled?"true":"false",static_cast<unsigned long long>(weapon.smokeLoopReplacements),
        static_cast<unsigned long long>(weapon.smokeLoopRetired),static_cast<unsigned long long>(weapon.smokeLoopRejected));
    std::fprintf(f,",\"grenadeFireVectors\":{\"barrelAim\":%llu,\"controllerAim\":%llu,\"requested\":[%.5f,%.5f,%.5f],\"nativeAim\":[%.5f,%.5f,%.5f],\"nativeDelta\":[%.5f,%.5f,%.5f],\"mappedDelta\":[%.5f,%.5f,%.5f]}",
        static_cast<unsigned long long>(weapon.grenadeFireBarrelAim),static_cast<unsigned long long>(weapon.grenadeFireControllerAim),
        weapon.grenadeRequestedDirection[0],weapon.grenadeRequestedDirection[1],weapon.grenadeRequestedDirection[2],
        weapon.grenadeNativeAim[0],weapon.grenadeNativeAim[1],weapon.grenadeNativeAim[2],
        weapon.grenadeNativeDelta[0],weapon.grenadeNativeDelta[1],weapon.grenadeNativeDelta[2],
        weapon.grenadeMappedDelta[0],weapon.grenadeMappedDelta[1],weapon.grenadeMappedDelta[2]);

    std::fprintf(f,",\"nativeActionTiltViews\":%llu,\"nativeAnimatedUpViews\":%llu,\"cpuCaptureStages\":{",
        static_cast<unsigned long long>(native.nativeActionTiltViews),static_cast<unsigned long long>(native.nativeAnimatedUpViews));
    for(unsigned stage=0;stage<D3D9Capture::CPUStageCount;++stage){const auto& cost=capture.cpuStages[stage];std::fprintf(f,"%s\"%s\":{\"calls\":%llu,\"totalUs\":%llu,\"maxUs\":%llu}",stage?",":"",D3D9Capture::CPUStageNames[stage],static_cast<unsigned long long>(cost.calls),static_cast<unsigned long long>(cost.totalUs),static_cast<unsigned long long>(cost.maxUs));}
    std::fprintf(f,"}");
    std::fprintf(f,",\"nativeMovieVerified\":%s,\"nativeMovieActive\":%s,\"nativeMovieWidth\":%u,\"nativeMovieHeight\":%u,\"actorCinematicApplied\":%s,"
        "\"nativeHudShadowSkippedWrites\":%llu,\"nativeHudShadowCopiedBytes\":%llu,\"nativeHudShadowTrackedInvalidations\":%llu",
        movieVerified.load()?"true":"false",movieActive.load()?"true":"false",movieWidth.load(),movieHeight.load(),actorCinematicApplied.load()?"true":"false",
        static_cast<unsigned long long>(hud.shadowSkippedWrites),static_cast<unsigned long long>(hud.shadowCopiedBytes),static_cast<unsigned long long>(hud.shadowTrackedInvalidations));
    std::fprintf(f,",\"sessionMonitorLaunchSucceeded\":%s,\"sessionMonitorLaunchError\":%s,\"lastSceneGate\":\"%s\",\"sceneGateAttempts\":{",
        monitorLaunchSucceeded?"true":"false",PerformanceLog::Quote(PerformanceLog::Utf8(monitorLaunchError)).c_str(),sceneGateNames[lastSceneGate.load()]);
    for(unsigned index=0;index<static_cast<unsigned>(SceneGate::Count);++index)
        std::fprintf(f,"%s\"%s\":%llu",index?",":"",sceneGateNames[index],static_cast<unsigned long long>(sceneGates[index].load()));
    std::fprintf(f,"}");
    std::fprintf(f,",\"performanceLogging\":%s,\"performanceLog\":%s,\"performanceLogFailed\":%s,"
        "\"crashLogging\":%s,\"sessionDirectory\":%s,\"crashLog\":%s,\"cpuTimingsEnabled\":%s,\"profileRegions\":[",
        performanceRecorder.Active()?"true":"false",PerformanceLog::Quote(PerformanceLog::Utf8(performanceRecorder.Path())).c_str(),
        performanceRecorder.Failed()?"true":"false",CrashDiagnostics::Active()?"true":"false",
        PerformanceLog::Quote(PerformanceLog::Utf8(CrashDiagnostics::Directory())).c_str(),
        PerformanceLog::Quote(PerformanceLog::Utf8(CrashDiagnostics::Path())).c_str(),
        ProfileTiming::enabled.load()?"true":"false");
    for(unsigned region=0;region<ProfileTiming::Count;++region){
        const auto key=static_cast<ProfileTiming::Region>(region);
        std::fprintf(f,"%s{\"name\":\"%s\",\"estimatedTotalMs\":%.6f,\"samples\":%llu,\"stride\":%u,\"kind\":\"%s\"}",
            region?",":"",ProfileTiming::Names[region],ProfileTiming::EstimatedMs(key),
            static_cast<unsigned long long>(ProfileTiming::counters[region].samples.load()),ProfileTiming::Stride(key),
            key==ProfileTiming::PostprocessDraw||key==ProfileTiming::DualControl||key==ProfileTiming::NativeVertexLock||key==ProfileTiming::NativeIndexLock||key==ProfileTiming::NativeBufferUnlock||key==ProfileTiming::On12QueuePacing||
            key==ProfileTiming::AimOverlay||key==ProfileTiming::ReticleProjection||key==ProfileTiming::WristProjection||key==ProfileTiming::ReloadOverlay?"inclusive_wall_ms":"sampled_mod_wall_ms_nested");
    }
    const auto gpuTiming=D3D9GPUTiming::Snapshot();
    std::fprintf(f,"],\"gpuRenderSpan\":{\"enabled\":%s,\"supported\":%s,\"pending\":%u,\"windowSamples\":%u,\"attempted\":%llu,"
        "\"validSamples\":%llu,\"skippedSamples\":%llu,\"disjointSamples\":%llu,\"failures\":%llu,\"resets\":%llu,"
        "\"lastMs\":%.4f,\"medianMs\":%.4f,\"p95Ms\":%.4f,\"maxMs\":%.4f,\"ownerThread\":%lu,\"lastResult\":%ld,\"sampleAgeMs\":%lu,\"threadHandoffs\":%llu}",
        gpuTiming.enabled?"true":"false",gpuTiming.supported?"true":"false",gpuTiming.pending,gpuTiming.windowSamples,
        static_cast<unsigned long long>(gpuTiming.attempted),static_cast<unsigned long long>(gpuTiming.validSamples),
        static_cast<unsigned long long>(gpuTiming.skippedSamples),static_cast<unsigned long long>(gpuTiming.disjointSamples),
        static_cast<unsigned long long>(gpuTiming.failures),static_cast<unsigned long long>(gpuTiming.resets),
        gpuTiming.lastMs,gpuTiming.medianMs,gpuTiming.p95Ms,gpuTiming.maxMs,
        static_cast<unsigned long>(gpuTiming.ownerThread),static_cast<long>(gpuTiming.lastResult),static_cast<unsigned long>(gpuTiming.sampleAgeMs),
        static_cast<unsigned long long>(gpuTiming.threadHandoffs));
    const auto& on12Pacing=capture.queuePacing;
    std::fprintf(f,",\"on12QueuePacing\":{\"enabled\":%s,\"supported\":%s,\"pending\":%u,\"thread\":%u,\"lastResult\":%d,\"issued\":%llu,\"completed\":%llu,\"waits\":%llu,\"polls\":%llu,\"waitUs\":%llu,\"maxWaitUs\":%llu,\"budgetExhausted\":%llu,\"skipped\":%llu,\"failures\":%llu,\"resets\":%llu}",
        on12Pacing.enabled?"true":"false",on12Pacing.supported?"true":"false",on12Pacing.pending,on12Pacing.thread,on12Pacing.lastResult,
        static_cast<unsigned long long>(on12Pacing.issued),static_cast<unsigned long long>(on12Pacing.completed),static_cast<unsigned long long>(on12Pacing.waits),
        static_cast<unsigned long long>(on12Pacing.polls),static_cast<unsigned long long>(on12Pacing.waitUs),static_cast<unsigned long long>(on12Pacing.maxWaitUs),
        static_cast<unsigned long long>(on12Pacing.budgetExhausted),static_cast<unsigned long long>(on12Pacing.skipped),static_cast<unsigned long long>(on12Pacing.failures),static_cast<unsigned long long>(on12Pacing.resets));
    std::fprintf(f,",\"gpuTransportActive\":%s,\"gpuTransportReason\":\"%s\",\"gpuCopies\":%llu,\"gpuBusy\":%llu,\"gpuPanelCopiesPaced\":%llu,"
        "\"gpuFailures\":%llu,\"gpuRetiredGenerations\":%u,\"gpuSharedTextureBytes\":%llu,\"gpuRetiredTextureBytes\":%llu,\"gpuPhysicalTextures\":%u,\"gpuCompactPanel\":%s,\"cpuPixelMappingBytes\":%llu,"
        "\"resetCalls\":%llu,\"resetFailures\":%llu,\"lastResetResult\":%d,\"resetPending\":%s",
        vr.gpuActive?"true":"false",vr.gpuReason,static_cast<unsigned long long>(vr.gpuCopies),
        static_cast<unsigned long long>(vr.gpuBusy),static_cast<unsigned long long>(vr.gpuPanelCopiesPaced),
        static_cast<unsigned long long>(vr.gpuFailures),vr.gpuRetiredGenerations,
        static_cast<unsigned long long>(vr.gpuSharedTextureBytes),static_cast<unsigned long long>(vr.gpuRetiredTextureBytes),vr.gpuPhysicalTextures,
        vr.gpuCompactPanel?"true":"false",static_cast<unsigned long long>(vr.cpuPixelMappingBytes),
        static_cast<unsigned long long>(capture.resetCalls),static_cast<unsigned long long>(capture.resetFailures),
        capture.lastResetResult,capture.resetPending?"true":"false");
    std::fprintf(f,",\"gpuIntermediateTextures\":%u,\"gpuIntermediateTextureBytes\":%llu,\"gpuRetiredIntermediateBytes\":%llu",
        vr.gpuIntermediateTextures,static_cast<unsigned long long>(vr.gpuIntermediateTextureBytes),static_cast<unsigned long long>(vr.gpuRetiredIntermediateBytes));
    std::fprintf(f,",\"readbackCalls\":%llu,\"frameIntervalMedianMs\":%.3f,\"frameIntervalP95Ms\":%.3f,"
        "\"captureMedianMs\":%.3f,\"captureP95Ms\":%.3f,\"nativePresentMedianMs\":%.3f,\"nativePresentP95Ms\":%.3f,"
        "\"frameStalls50Ms\":%llu,\"captureStalls50Ms\":%llu,\"presentStalls50Ms\":%llu,"
        "\"renderThreadCpuPercent\":%.2f,\"presentationKnown\":%s,\"presentationInterval\":%u,"
        "\"forceImmediate\":%s,\"immediateApplied\":%llu,\"immediateFallbacks\":%llu",
        static_cast<unsigned long long>(capture.readbackCalls),pacing.interval.medianUs/1000.,pacing.interval.p95Us/1000.,
        pacing.capture.medianUs/1000.,pacing.capture.p95Us/1000.,pacing.nativeWait.medianUs/1000.,pacing.nativeWait.p95Us/1000.,
        static_cast<unsigned long long>(pacing.stalls50),static_cast<unsigned long long>(pacing.captureStalls50),
        static_cast<unsigned long long>(pacing.nativeWaitStalls50),pacing.threadCpuPercent,
        capture.presentationKnown?"true":"false",capture.presentationInterval,capture.forceImmediate?"true":"false",
        static_cast<unsigned long long>(capture.immediateApplied),static_cast<unsigned long long>(capture.immediateFallbacks));
    FrameEventTrace::Event frameEvents[512];std::uint64_t overwritten=0;
    const auto eventCount=D3D9Capture::ReadFrameTrace(frameTraceAfter,frameEvents,std::size(frameEvents),overwritten);
    std::fprintf(f,",\"incompleteReadbacksAvoided\":%llu",static_cast<unsigned long long>(incompleteReadbacksAvoided.load()));
    std::fprintf(f,",\"absentConsumerReadbacksAvoided\":%llu",static_cast<unsigned long long>(absentConsumerReadbacksAvoided.load()));
    std::fprintf(f,",\"nativeFrameTrace\":{\"overwritten\":%llu,\"columns\":[\"seq\",\"tick\",\"thread\",\"present\",\"startUs\",\"intervalUs\",\"cpuUs\",\"cpuKnown\",\"captureUs\",\"presentUs\",\"previousCaptureUs\",\"previousPresentUs\",\"captured\",\"drops\",\"failures\",\"sourceFrameId\",\"eye\",\"kind\",\"gate\",\"captureResult\",\"presentResult\",\"firstShaderUs\",\"shaderThread\",\"shaderSpanUs\",\"shaderCpuUs\",\"shaderCpuKnown\",\"preShaderUs\",\"preShaderKnown\",\"shaderKnown\",\"firstCameraUs\",\"cameraThread\",\"cameraKnown\"],\"events\":[",
        static_cast<unsigned long long>(overwritten));
    for(std::size_t i=0;i<eventCount;++i){const auto& e=frameEvents[i];
        std::fprintf(f,"%s[%llu,%u,%u,%llu,%llu,%llu,%llu,%u,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%u,%u,%u,%ld,%ld,%llu,%u,%llu,%llu,%u,%llu,%u,%u,%llu,%u,%u]",i?",":"",
            static_cast<unsigned long long>(e.sequence),e.tick,e.thread,static_cast<unsigned long long>(e.present),
            static_cast<unsigned long long>(e.startUs),static_cast<unsigned long long>(e.intervalUs),static_cast<unsigned long long>(e.cpuUs),e.cpuKnown?1u:0u,
            static_cast<unsigned long long>(e.captureUs),static_cast<unsigned long long>(e.nativePresentUs),
            static_cast<unsigned long long>(e.previousCaptureUs),static_cast<unsigned long long>(e.previousPresentUs),
            static_cast<unsigned long long>(e.captured),static_cast<unsigned long long>(e.drops),static_cast<unsigned long long>(e.failures),
            static_cast<unsigned long long>(e.sourceFrameId),e.eye,e.kind,e.gate,static_cast<long>(e.captureResult),static_cast<long>(e.presentResult),
            static_cast<unsigned long long>(e.firstShaderUs),e.shaderThread,static_cast<unsigned long long>(e.shaderSpanUs),static_cast<unsigned long long>(e.shaderCpuUs),e.shaderCpuKnown?1u:0u,
            static_cast<unsigned long long>(e.preShaderUs),e.preShaderKnown?1u:0u,e.shaderKnown?1u:0u,
            static_cast<unsigned long long>(e.firstCameraUs),e.cameraThread,e.cameraKnown?1u:0u);
    }
    std::fprintf(f,"]}");
    NativeBufferFlushKL::Event flushEvents[NativeBufferFlushKL::EventCapacity];std::uint64_t flushOverwritten=0;
    const auto flushCount=NativeBufferFlushKL::ReadEvents(bufferFlushTraceAfter,flushEvents,std::size(flushEvents),flushOverwritten);
    std::fprintf(f,",\"nativeBufferUploadRecovery\":{\"deferred\":%llu,\"recovered\":%llu,\"attempts\":%llu,\"timeouts\":%llu,\"blockedDraws\":%llu,\"dynamicVertexCreated\":%llu,\"dynamicVertexFallback\":%llu,\"pending\":%u,\"firstFailureTick\":%u,\"lastFailureTick\":%u,\"lastResult\":%d}",
        static_cast<unsigned long long>(bufferFlush.uploadDeferred),static_cast<unsigned long long>(bufferFlush.uploadRecovered),
        static_cast<unsigned long long>(bufferFlush.recoveryAttempts),static_cast<unsigned long long>(bufferFlush.recoveryTimeouts),
        static_cast<unsigned long long>(bufferFlush.blockedDraws),static_cast<unsigned long long>(bufferFlush.dynamicVertexCreated),
        static_cast<unsigned long long>(bufferFlush.dynamicVertexFallback),bufferFlush.pendingUploads,
        bufferFlush.firstFailureTick,bufferFlush.lastFailureTick,bufferFlush.lastUploadResult);
    std::fprintf(f,",\"nativeBufferFlushTrace\":{\"installed\":%s,\"enabled\":%s,\"verified\":%s,\"batching\":%s,\"on12\":%s,\"backend\":\"%s\",\"batchingSupported\":%s,\"batchingFaulted\":%s,\"batchedCalls\":%llu,\"savedLocks\":%llu,\"singleMapCalls\":%llu,\"lockFailures\":%llu,\"unlockFailures\":%llu,\"invalidMappings\":%llu,\"droppedEvents\":%llu,\"overwritten\":%llu,\"phases\":[",
        bufferFlush.installed?"true":"false",bufferFlush.enabled?"true":"false",bufferFlush.verified?"true":"false",bufferFlush.batching?"true":"false",bufferFlush.on12?"true":"false",
        NativeBufferFlushKL::BackendName(bufferFlush.backend),bufferFlush.batchingSupported?"true":"false",
        bufferFlush.batchingFaulted?"true":"false",
        static_cast<unsigned long long>(bufferFlush.batchedCalls),static_cast<unsigned long long>(bufferFlush.savedLocks),
        static_cast<unsigned long long>(bufferFlush.singleMapCalls),static_cast<unsigned long long>(bufferFlush.lockFailures),
        static_cast<unsigned long long>(bufferFlush.unlockFailures),static_cast<unsigned long long>(bufferFlush.invalidMappings),
        static_cast<unsigned long long>(bufferFlush.dropped),static_cast<unsigned long long>(flushOverwritten));
    for(unsigned i=0;i<NativeBufferFlushKL::PhaseCount;++i){const auto& t=bufferFlush.phases[i];
        std::fprintf(f,"%s{\"calls\":%llu,\"active\":%llu,\"totalUs\":%llu,\"maxUs\":%llu,\"bytes\":%llu,\"ranges\":%llu}",i?",":"",
            static_cast<unsigned long long>(t.calls),static_cast<unsigned long long>(t.active),static_cast<unsigned long long>(t.totalUs),
            static_cast<unsigned long long>(t.maxUs),static_cast<unsigned long long>(t.bytes),static_cast<unsigned long long>(t.ranges));}
    std::fprintf(f,"],\"columns\":[\"seq\",\"tick\",\"thread\",\"phase\",\"resource\",\"startUs\",\"endUs\",\"wallUs\",\"pending\",\"valid\",\"shadowReady\",\"capacity\",\"ranges\",\"submittedRanges\",\"bytes\",\"spanBytes\",\"result\"],\"events\":[");
    for(std::size_t i=0;i<flushCount;++i){const auto& e=flushEvents[i];const auto& m=e.metadata;
        std::fprintf(f,"%s[%llu,%u,%u,%u,%u,%llu,%llu,%llu,%u,%u,%u,%u,%u,%u,%llu,%u,%d]",i?",":"",
            static_cast<unsigned long long>(e.sequence),e.tick,e.thread,e.phase,m.resource,static_cast<unsigned long long>(e.startUs),
            static_cast<unsigned long long>(e.endUs),static_cast<unsigned long long>(e.wallUs),m.pending?1u:0u,m.valid?1u:0u,m.shadowReady?1u:0u,
            m.capacity,m.ranges,m.submittedRanges,static_cast<unsigned long long>(m.bytes),m.spanBytes,e.result);}
    std::fprintf(f,"],\"inProgress\":");
    if(bufferFlush.active){const auto& e=bufferFlush.inProgress;const auto& m=e.metadata;const auto now=FrameMetrics::Microseconds(0);
        std::fprintf(f,"{\"thread\":%u,\"phase\":%u,\"resource\":%u,\"startUs\":%llu,\"ageUs\":%llu,\"pending\":%s,\"valid\":%s,\"capacity\":%u,\"ranges\":%u,\"submittedRanges\":%u,\"bytes\":%llu}",
            e.thread,e.phase,m.resource,static_cast<unsigned long long>(e.startUs),static_cast<unsigned long long>(now>=e.startUs?now-e.startUs:0),
            m.pending?"true":"false",m.valid?"true":"false",m.capacity,m.ranges,m.submittedRanges,static_cast<unsigned long long>(m.bytes));
    }else std::fprintf(f,"null");
    std::fprintf(f,"}");
    const auto& hid=NativeHidRefreshKL::refresh;
    std::fprintf(f,",\"nativeHidRefresh\":{\"supported\":%s,\"installed\":%s,\"scans\":%u,\"skipped\":%u,\"changes\":%u,\"failures\":%u,\"passthrough\":%u}",
        NativeHidRefreshKL::imageBase?"true":"false",NativeHidRefreshKL::enabled.load()?"true":"false",
        hid.scans.load(),hid.skipped.load(),hid.changes.load(),hid.failures.load(),NativeHidRefreshKL::passthrough.load());
    NativeFrameWaitKL::Event waitEvents[256];std::uint64_t waitOverwritten=0;
    const auto waitCount=NativeFrameWaitKL::ReadEvents(waitTraceAfter,waitEvents,std::size(waitEvents),waitOverwritten);
    std::fprintf(f,",\"nativeWaitTrace\":{\"installed\":%s,\"enabled\":%s,\"verified\":%s,\"droppedEvents\":%llu,\"overwritten\":%llu,\"retentionVersion\":2,\"filterThresholdUs\":2000,\"filteredFastEvents\":%llu,\"activeUntracked\":%llu,\"sites\":{",
        nativeWait.installed?"true":"false",nativeWait.enabled?"true":"false",nativeWait.verified?"true":"false",
        static_cast<unsigned long long>(nativeWait.droppedEvents),static_cast<unsigned long long>(waitOverwritten),
        static_cast<unsigned long long>(nativeWait.filteredFastEvents),static_cast<unsigned long long>(nativeWait.activeUntracked));
    for(unsigned i=0;i<NativeFrameWaitKL::SiteCount;++i)std::fprintf(f,"%s\"%s\":{\"calls\":%llu,\"totalUs\":%llu,\"maxUs\":%llu}",i?",":"",NativeFrameWaitKL::SiteNames[i],
        static_cast<unsigned long long>(nativeWait.sites[i].calls),static_cast<unsigned long long>(nativeWait.sites[i].totalUs),static_cast<unsigned long long>(nativeWait.sites[i].maxUs));
    std::fprintf(f,"},\"columns\":[\"seq\",\"tick\",\"thread\",\"callerRva\",\"api\",\"site\",\"startUs\",\"endUs\",\"wallUs\",\"timeout\",\"objectCount\",\"waitAll\",\"result\",\"lastError\",\"drawStartUs\",\"drawStartThread\"],\"events\":[");
    for(std::size_t i=0;i<waitCount;++i){const auto& e=waitEvents[i];
        std::fprintf(f,"%s[%llu,%u,%u,%u,%u,%u,%llu,%llu,%llu,%u,%u,%u,%u,%u,%llu,%u]",i?",":"",static_cast<unsigned long long>(e.sequence),
            e.tick,e.thread,e.callerRva,e.api,e.site,static_cast<unsigned long long>(e.startUs),static_cast<unsigned long long>(e.endUs),static_cast<unsigned long long>(e.wallUs),
            e.timeout,e.objectCount,e.waitAll,e.result,e.lastError,static_cast<unsigned long long>(e.drawStartUs),e.drawStartThread);
    }
    std::fprintf(f,"],\"activeWaits\":[");
    NativeFrameWaitKL::ActiveWait activeWaits[NativeFrameWaitKL::ActiveWaitCapacity];
    const auto activeWaitCount=NativeFrameWaitKL::ReadActiveWaits(activeWaits,std::size(activeWaits));
    for(std::size_t i=0;i<activeWaitCount;++i){const auto& item=activeWaits[i];const auto& e=item.event;
        std::fprintf(f,"%s{\"thread\":%u,\"callerRva\":%u,\"api\":%u,\"site\":%u,\"startUs\":%llu,\"ageUs\":%llu,\"timeout\":%u,\"objectCount\":%u,\"waitAll\":%u,\"handles\":[%u,%u]}",i?",":"",
            e.thread,e.callerRva,e.api,e.site,static_cast<unsigned long long>(e.startUs),static_cast<unsigned long long>(item.ageUs),e.timeout,e.objectCount,e.waitAll,e.handle0,e.handle1);
    }
    std::fprintf(f,"]}");
    NativeFrameWaitKL::RenderSignal signalTrace[NativeFrameWaitKL::SignalCount]{};
    if(NativeFrameWaitKL::ReadSignals(signalTrace)){
        std::fprintf(f,",\"nativeRenderSignals\":{");
        for(unsigned i=0;i<NativeFrameWaitKL::SignalCount;++i){
            const auto& e=signalTrace[i];
            std::fprintf(f,"%s\"%s\":{\"samples\":%llu,\"sampledFailures\":%llu,\"startUs\":%llu,\"endUs\":%llu,\"thread\":%u,\"handle\":%u,\"result\":%u,\"lastError\":%u}",
                i?",":"",NativeFrameWaitKL::SignalNames[i],static_cast<unsigned long long>(e.calls),static_cast<unsigned long long>(e.failures),
                static_cast<unsigned long long>(e.startUs),static_cast<unsigned long long>(e.endUs),e.thread,e.handle,e.result,e.lastError);
        }
        std::fprintf(f,"}");
    }
    NativeStereo::WriteStatus(f);
    const auto steamCompatibility=SdkOwnCache12::Snapshot();
    std::fprintf(f,",\"steamCompatibility\":{\"phase\":%u,\"ready\":%s,\"certifiedPages\":%u,\"enabledHooks\":%u,\"reason\":\"%s\"}",
        static_cast<unsigned>(steamCompatibility.phase),SdkOwnCache12::Ready()?"true":"false",
        steamCompatibility.certifiedPages,steamCompatibility.enabledHooks,steamCompatibility.reason);
    std::fprintf(f,",\"renderWidth\":%u,\"renderHeight\":%u,\"renderMultisampleType\":%u,"
        "\"renderMultisampleQuality\":%u,\"readbackMeanMs\":%.3f,\"playerHeadOffsetMetres\":[%.5f,%.5f,%.5f],"
        "\"playerHeadDistanceMetres\":%.5f}\n",
        capture.renderWidth,capture.renderHeight,capture.multisampleType,capture.multisampleQuality,capture.readbackMeanMs,
        rig.playerHeadOffsetMetres[0],rig.playerHeadOffsetMetres[1],rig.playerHeadOffsetMetres[2],rig.playerHeadDistanceMetres);
    const bool statusWritten=std::ferror(f)==0;
    const bool statusClosed=std::fclose(f)==0;
    if(statusWritten&&statusClosed&&MoveFileExW(temporary,path,MOVEFILE_REPLACE_EXISTING)){
        if(pending){pending->objective=objectiveWritten;pending->postfilter=postfilterWritten;pending->reflection=reflectionWritten;
            pending->frame=eventCount?frameEvents[eventCount-1].sequence:frameTraceAfter;
            pending->wait=waitCount?waitEvents[waitCount-1].sequence:waitTraceAfter;
            pending->flush=flushCount?flushEvents[flushCount-1].sequence:bufferFlushTraceAfter;}
        return true;
    }
    return false;
}

// Ring cursors are acknowledged only after the recorder has persisted the
// corresponding valid game snapshot. A failed read/write or skipped interval
// keeps them unchanged, allowing a subsequent status to retry the events.
void PublishAndRecordStatus(bool installed,bool finalSample=false){
    if(!diagnosticLogging)return;
    PendingStatusTrace pending;const bool statusReady=WriteStatus(installed,&pending);
    const auto previous=performanceRecorder.AcceptedGameSamples();
    if(finalSample)performanceRecorder.FlushFinal(folder,gamePid,KL1_RELEASE_VERSION);
    else performanceRecorder.Tick(folder,gamePid,KL1_RELEASE_VERSION);
    if(statusReady&&performanceRecorder.AcceptedGameSamples()>previous){
        objectiveTraceAfter=pending.objective;postfilterTraceAfter=pending.postfilter;reflectionTraceAfter=pending.reflection;
        frameTraceAfter=pending.frame;waitTraceAfter=pending.wait;bufferFlushTraceAfter=pending.flush;
    }
}

void Worker() {
    const auto start=GetTickCount();
    FILETIME clock{};GetSystemTimeAsFileTime(&clock);
    const std::uint64_t now100ns=(std::uint64_t(clock.dwHighDateTime)<<32)|clock.dwLowDateTime;
    const auto created=DiagnosticsSession::Creation(GetCurrentProcess());
    const auto ageMs=created&&now100ns>=created?(now100ns-created)/10000:2000;
    const auto startupWait=static_cast<DWORD>(ageMs<2000?2000-ageMs:0);
    gamePid=GetCurrentProcessId();
    const auto diagnosticIni=std::wstring(folder)+L"vr.ini";
    const bool diagnosticsEnabled=DiagnosticsSession::Enabled(folder);
    diagnosticLogging=diagnosticsEnabled;
    if(diagnosticsEnabled&&GetPrivateProfileIntW(L"Diagnostics",L"CrashLogging",1,diagnosticIni.c_str()))CrashDiagnostics::Install(folder,KL1_RELEASE_VERSION "-game",true);
    CrashDiagnostics::Breadcrumb("worker_start");
    const auto configureDiagnostics=[&]{
        const bool logging=diagnosticsEnabled&&GetPrivateProfileIntW(L"Diagnostics",L"PerformanceLogging",1,diagnosticIni.c_str())!=0;
        ProfileTiming::enabled=logging&&GetPrivateProfileIntW(L"Diagnostics",L"CpuTimings",1,diagnosticIni.c_str())!=0;
        D3D9GPUTiming::Configure(logging&&GetPrivateProfileIntW(L"Diagnostics",L"GpuTimings",1,diagnosticIni.c_str())!=0);
        D3D9Capture::ConfigureFrameTrace(logging&&GetPrivateProfileIntW(L"Diagnostics",L"FrameEvents",1,diagnosticIni.c_str())!=0);
        NativeFrameWaitKL::SetEnabled(logging&&GetPrivateProfileIntW(L"Diagnostics",L"NativeWaitTimings",1,diagnosticIni.c_str())!=0);
        NativeBufferFlushKL::SetEnabled(logging&&GetPrivateProfileIntW(L"Diagnostics",L"NativeBufferFlushTimings",1,diagnosticIni.c_str())!=0);
        NativeHandsKL::SetSkinPassPairDiagnostics(logging);
        materialSequenceEnabled=logging&&GetPrivateProfileIntW(L"Diagnostics",L"WeaponMaterialSequence",0,diagnosticIni.c_str())!=0;
        NativeHudKL::SetObjectiveTraceEnabled(logging&&GetPrivateProfileIntW(L"Diagnostics",L"ObjectivePairTrace",0,diagnosticIni.c_str())!=0);
        NativeReflectionProbeKL::Configure(logging&&GetPrivateProfileIntW(L"Diagnostics",L"ReflectionMaterialProbe",0,diagnosticIni.c_str())!=0);
        const bool visual=logging&&GetPrivateProfileIntW(L"Diagnostics",L"VisualDrawCapture",0,diagnosticIni.c_str())!=0;
        NativeVisualTraceKL::Configure(folder,visual,RecordVisualDrawContext);
        NativeHandsKL::SetDrawContextDiagnostics(visual&&NativeVisualTraceKL::Active());
        NativeCameraKL::ConfigureDiagnostics(visual?NativeVisualTraceKL::Active:nullptr,RecordVisualProducer,RecordVisualProducerBytes);
        NativeEffectsKL::ConfigureVisualSourceDiagnostics(visual?NativeVisualTraceKL::Active:nullptr);
    };
    configureDiagnostics();
    monitorLaunchSucceeded=DiagnosticsLaunch::Start(folder,GetCurrentProcess(),&monitorLaunchError);
    const std::wstring logPath=std::wstring(folder)+L"KaneLynchVR.log";
    if(diagnosticsEnabled)logFile=_wfopen(logPath.c_str(),L"ab");
    if(!monitorLaunchSucceeded)Log(PerformanceLog::Utf8(monitorLaunchError).c_str());
    Log("KaneLynchVR " KL1_RELEASE_VERSION " (x86): certified executable layouts, original startup routes and scoped render corrections.");
    LoadSettings();
    HandsRenderer::Initialize(folder);
    VRBridge::Initialize(folder,Log);
    if(MH_Initialize()!=MH_OK){Log("MinHook initialization failed; capture disabled.");PublishAndRecordStatus(false);
        performanceRecorder.Close("hook_init_failed");
        CrashDiagnostics::Close("hook_init_failed");if(logFile)std::fclose(logFile);return;}
    bool verifiedImage=false,startupChannelRequired=false;
    {
        wchar_t executable[MAX_PATH]{};char digest[65]{};
        const auto length=GetModuleFileNameW(nullptr,executable,MAX_PATH);
        verifiedImage=length&&length<MAX_PATH&&HashSupportedGameFile(executable,digest);
        startupChannelRequired=verifiedImage&&std::strcmp(digest,kSupportedSha256)!=0;
    }
    LauncherNativeStartup::Channel nativeStartup;
    const auto rejectBeforeNativeHooks=[&](const char* reason){
        nativeStartup.SignalFailed();Log(reason);PublishAndRecordStatus(false);
        MH_Uninitialize();performanceRecorder.Close("native_startup_rejected");
        CrashDiagnostics::Close("native_startup_rejected");
        if(logFile&&logFile!=stdout)std::fclose(logFile);logFile=nullptr;
    };
    // A newly qualified retail-layout launch must be owned by this package's launcher. Unsupported
    // runtimes and missing startup channels are rejected before code changes.
    if(startupChannelRequired&&!nativeStartup.OpenCurrent()){
        rejectBeforeNativeHooks("Retail-layout VR startup requires this package's LaunchVR.exe; no native game hooks installed.");return;
    }
    bool nativeReady=SdkOwnCache12::Preflight();
    while(!nativeReady&&GetTickCount()-start<startupWait){
        Sleep(25);
        nativeReady=SdkOwnCache12::Preflight();
    }
    if(!nativeReady){
        rejectBeforeNativeHooks(SdkOwnCache12::Snapshot().reason);return;
    }
    const bool testingModifiers=GetPrivateProfileIntW(L"Debug",L"TestingModifiers",0,diagnosticIni.c_str())!=0;
    const bool invulnerable=GetPrivateProfileIntW(L"Debug",L"Invulnerable",0,diagnosticIni.c_str())!=0;
    const bool invulnerabilityReady=NativeInvulnerabilityKL::Install(invulnerable,Log,verifiedImage&&testingModifiers);
    if(!invulnerabilityReady)
        Log("Testing invulnerability unavailable; native damage retained.");
    if(verifiedImage&&!NativeVRMenuKL::Install(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)),
        diagnosticIni,testingModifiers&&invulnerabilityReady,ApplyVRModifier,Log))
        Log("Native VR settings unavailable; the Options menu was left intact.");
    // Menu resources are parsed during startup. Install their extension first
    // and give the original engine time to initialize before the gameplay hooks.
    const auto elapsed=GetTickCount()-start;
    if(elapsed<startupWait)Sleep(startupWait-elapsed);
    if(verifiedImage)NativeHidRefreshKL::Prepare(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)),
        GetPrivateProfileIntW(L"Diagnostics",L"NativeHidRefresh",1,diagnosticIni.c_str())!=0);
    // Both camera and PIP use this one native producer. Authenticate its
    // original bytes before the camera owns its detour; PIP later registers
    // its filter with that owner instead of trying to hook it a second time.
    if(verifiedImage&&settings.nativeCamera&&settings.stereoMode&&settings.hideAuxiliaryPip&&
       !NativePipRenderKL::InitializeVerifiedImage(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)),Log))
        Log("Native auxiliary PIP original-image proof unavailable.");
    if(settings.nativeCamera&&!NativeCameraKL::Install(ProvideNativeFrame,NativeProjectionApplied,nullptr,Log))
        Log("Native camera unavailable; using the guarded D3D9 stereo fallback.");
    verifiedImage=NativeCameraKL::Installed()||verifiedImage;
    if(diagnosticsEnabled&&verifiedImage&&!NativeFrameWaitKL::Install(Log))Log("Native wait diagnostics unavailable; native waits unchanged.");
    if(verifiedImage&&!NativeBufferFlushKL::Install(Log))Log("Static buffer upload hooks unavailable; native uploads unchanged.");
    configureDiagnostics();
    movieVerified=verifiedImage&&NativeMovieKL::VerifyImage(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)),ReadNativeBytes);
    if(movieVerified.load())Log("Verified native Bink playback uses the complete mono panel; movie frames bypass scene steering and HUD remapping.");
    ammoProof=verifiedImage&&NativeHandHudKL::VerifyAmmoImage(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)),ReadNativeBytes);
    contextProof=verifiedImage&&NativeContextHudKL::VerifyImage(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)),ReadNativeBytes);
    if(NativeCameraKL::Installed()&&
       !NativePlayerAnchor::InitializeVerifiedImage(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr))))
        Log("Player anchor signatures differ; retaining the original camera centre.");
    if(settings.hideLocalRopes&&NativeCameraKL::Installed()&&!NativeRopeKL::Install(true,Log))
        Log("Local rope visibility filter unavailable; original ropes retained.");
    if(verifiedImage&&settings.firstPerson&&NativeCameraKL::Installed()&&!NativeCarriedPersonVisibilityKL::Install(true,Log))
        Log("Carried person main-eye visibility filter unavailable; see native graph verification.");
    if(settings.nativeArms&&!NativeHandsKL::Install(true,Log))
        Log("Native player arms unavailable; see the signature diagnostics.");
    if(verifiedImage&&!NativeWeaponKL::InitializeScope(Log))
        Log("Native sniper precision proof unavailable; original optic handling retained.");
    if(settings.nativeArms&&settings.weaponAim){
        nativeWeaponReady=NativeWeaponKL::Install(true,Log);
        if(!nativeWeaponReady.load())Log("Native weapon targeting unavailable; see the signature diagnostics.");
    }
    if(verifiedImage&&nativeWeaponReady.load()&&NativeCameraKL::Installed()&&
       !NativeTracerVisualKL::Install(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)),Log))
        Log("Native visual bullet trail correction unavailable; projectile targeting retained.");
    NativeWeaponKL::SetTrackedReloadEnabled(settings.firstPerson&&settings.nativeArms);
    // Verify the detector's original bytes before our own proximity detour
    // replaces its prologue. The simulation observer can be registered first.
    if(!verifiedImage||!NativeAimKL::Install(Log)) Log("Native collision queries unavailable; see the signature diagnostics.");
    if(verifiedImage&&!NativeMountedAimKL::Install(Log))Log("Native mounted controller aiming unavailable; native stick/mouse controls retained.");
    if(verifiedImage&&settings.firstPerson&&!NativePassengerActorKL::InstallSeated(Log))
        Log("Native passenger seated pose unavailable; native first-person view retained.");
    NativeHandsKL::SetPassengerMotionProvider(NativePassengerActorKL::ReadMotion);
    NativeHandsKL::SetProjectionBindObserver(NativeCameraKL::ObserveSecondaryMaskBind);
    NativeHandsKL::ConfigureRenderContext(NativeCameraKL::ReadRenderContext);
    NativeWeaponKL::SetProjectionBindObserver(NativeCameraKL::ObserveSecondaryMaskBind);
    if(verifiedImage&&settings.firstPerson&&!NativeTutorialKL::Install(Log))Log("VR second-mission tutorial adapter unavailable; native tutorial retained.");
    if(verifiedImage&&settings.firstPerson&&!NativeLedgeHintKL::Install(Log))Log("VR passive climb hint unavailable; native action behavior retained.");
    NativeCoverKL::SetSimulationObserver(NativeSimulationTick);
    if(settings.firstPerson&&NativeCameraKL::Installed()){
        nativeSimulationReady=NativeCoverKL::InstallScriptProximity(true,settings.worldUnitsPerMetre,Log);
        if(!nativeSimulationReady.load())Log("VR script proximity cover unavailable; see the signature diagnostics.");
    }
    if(settings.firstPerson&&NativeCameraKL::Installed()&&!NativeCoverKL::InstallActionClimb(true,Log))
        Log("VR action-only climb unavailable; see the signature diagnostics.");
    if(settings.nativeCamera&&!NativePostprocessKL::Install(StereoSceneActive,Log))
        Log("Temporal filter hook unavailable; see the signature diagnostics.");
    if(diagnosticsEnabled&&GetPrivateProfileIntW(L"Diagnostics",L"PostfilterStageProbe",0,diagnosticIni.c_str())&&
       !NativePostfilterProbeKL::Install(ProvidePostfilterFrame,Log))Log("Opt-in postfilter stage probe unavailable.");
    NativeHudKL::SetLetterboxProvider(HideNativeLetterbox);
    NativeHudKL::SetReticleProvider(ProvideReticleFrame);
    NativeHudKL::SetNativeMarkerProvider(ProvideNativeMarkerFrame);
    if(verifiedImage&&!NativeGrenadeVisualKL::InitializeVerifiedImage(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr))))
        Log("Grenade weapon visibility signatures unavailable; native firearm rendering retained.");
    NativeHudKL::SetNativeScopeProvider(NativeScopeHudScene);
    NativeHudKL::SetWristHudProvider(ProvideWristHud);
    NativeHudKL::SetCinemaTextProvider(ProvideCinemaTextFrame);
    if(NativeCameraKL::Installed()&&(settings.stereoHud||settings.stereoMode)&&!NativeHudKL::Install(ProvideHudFrame,Log))
        Log("Native HUD stereo correction unavailable; original UI retained.");
    NativeGameplayHudKL::SetImmersiveEnabled(settings.immersiveHud);
    NativeGameplayHudKL::SetReadableSubtitlesActive([](){return settings.stereoHud&&VRBridge::Ready();});
    NativeGameplayHudKL::SetLedgeProvider(NativeLedgeHintKL::Available);
    NativeGameplayHudKL::SetReloadActionProvider(NativeWeaponKL::ReloadActionText);
    if(settings.stereoMode&&!NativeGameplayHudKL::Install(LegacyRosterScene,Log))
        Log("Native gameplay HUD suppression unavailable; original indicators retained.");
    const bool teamPanelControls=GetPrivateProfileIntW(L"Controls",L"Enabled",1,diagnosticIni.c_str())!=0&&
        GetPrivateProfileIntW(L"Controls",L"ImmersiveLayout",1,diagnosticIni.c_str())!=0;
    if(teamPanelControls){
        NativeGameplayHudKL::SetTutorialBindingsActive(VRBridge::Ready);
        NativeGameplayHudKL::SetWristHudHeld([](){return (visualBenchMask.load(std::memory_order_relaxed)&BackgroundDiagnostic::BenchWrists)&&VRBridge::TeamHudVisible();});
    }
    NativeWeaponKL::SetScopeGestureEnabled(teamPanelControls);
    if(settings.immersiveHud&&teamPanelControls&&!NativeTeamHudKL::Install(LegacyRosterScene,Log))
        Log("Native team HUD unavailable; original roster retained.");
    if(settings.immersiveHud&&teamPanelControls&&!NativeTeamCommandsKL::Install(Log))
        Log("Native VR team commands unavailable; see the signature diagnostics.");
    // Team/HUD verification reads the original Count/Index marker routes.
    // The objective adapter detours those routes, so install it only after
    // every native roster/command proof has consumed the original image.
    if(verifiedImage&&settings.firstPerson&&!NativeObjectiveVisibilityKL::Install(Log))
        Log("VR objective hold/map adapter unavailable; native objective visibility retained.");
    if(settings.stereoMode&&NativeCameraKL::Installed()&&
       !NativePipRenderKL::Install(settings.hideAuxiliaryPip,Log))
        Log("Native auxiliary PIP suppression unavailable; original camera rendering retained.");
    if(settings.stereoMode&&NativeCameraKL::Installed()){
        const auto tokyoFloorMode=GetPrivateProfileIntW(L"VR",L"TokyoFloorTexture",2,diagnosticIni.c_str());
        NativeMirrorBumpKL::Configure(NativeCameraKL::ReadEffectsView,NativeCameraKL::ReadReflectionTargetView,
            tokyoFloorMode,NativeCameraKL::QualifyFloorReflector);
        reflectionHistoryEnabled=GetPrivateProfileIntW(L"VR",L"ReflectionTargetHistory",0,diagnosticIni.c_str())!=0;
        if(reflectionHistoryEnabled||tokyoFloorMode==2){
            D3D9Capture::SetTargetObserver(NativeMirrorBumpKL::ObserveTargetChange);
            NativeMirrorBumpKL::ConfigureTargetHistory(D3D9Capture::TargetWritesCovered,NativeCameraKL::ActiveReflectionTargetSerial);
        }
        NativeReflectionProbeKL::ConfigureContext([](NativeReflectionProbeKL::DrawContext& output){
            NativeCameraKL::EffectsView view;if(!NativeCameraKL::ReadEffectsView(view)||!view.worldKnown)return false;
            NativeReflectionProbeKL::DrawContext next;next.state=view.state;next.generation=view.frame;next.eyeIndex=view.eyeIndex;
            std::memcpy(next.world,view.world,sizeof(next.world));std::memcpy(next.projection,view.projection,sizeof(next.projection));
            const float eye[12]={view.eye.basis.right.x,view.eye.basis.right.y,view.eye.basis.right.z,
                view.eye.basis.up.x,view.eye.basis.up.y,view.eye.basis.up.z,
                view.eye.basis.forward.x,view.eye.basis.forward.y,view.eye.basis.forward.z,
                view.eye.position.x,view.eye.position.y,view.eye.position.z};
            std::memcpy(next.eye,eye,sizeof(eye));output=next;return true;
        });
        const bool worldObserver=NativeWeaponKL::SetWorldBindObserver(NativeWorldObserved);
        if(!NativeEffectsKL::Install(Log,worldObserver))Log("Native world effects correction unavailable; original effect shaders retained.");
    }
    if(verifiedImage&&NativeCameraKL::Installed())NativeStereo::Install(diagnosticIni.c_str());
    // All native game-code installers are complete. Certify their own exact
    // changes before updating only those entries in the original SDK cache.
    SdkOwnCache12::Run(folder);
    if(!SdkOwnCache12::Ready()){
        nativeStartup.SignalFailed();Log(SdkOwnCache12::Snapshot().reason);
        PublishAndRecordStatus(false);return;
    }
    if(startupChannelRequired&&!nativeStartup.SignalReady()){
        nativeStartup.SignalFailed();Log("Original Steam native startup acknowledgement failed.");
        PublishAndRecordStatus(false);return;
    }
    nativeStartup.Close();
    D3D9Capture::SetDrawCallbacks(BeginNativeDraw,FinishNativeDraw);
    D3D9Capture::SetDrawTraceCallback(NativeVisualTraceKL::TraceDraw);
    D3D9Capture::SetDrawCopyCallback(DrawNativeGeometryCopies);
    // D3D9 is unusable for a while after early injection, and fails outright
    // while an exclusive-fullscreen device holds the adapter (the game's own
    // when injection lands late, or another session's VR compositor). Keep
    // retrying in the background: hooks land as soon as a device can be
    // created, so a live game session self-heals without a restart.
    VRBridge::SetGPUTransport(true);
    D3D9Capture::SetDrawFilter(HideLocalRopeDraw);
    D3D9Capture::SetGPUCallback(OnGPUFrame);
    if(VRBridge::UseShared9Polling())D3D9Capture::SetRenderProgressCallback(VRBridge::RenderProgress);
    const auto installHooks=[&]() {
        return D3D9Capture::Install(OnFrame,nullptr,OnFrameBoundary,OnTransform,nullptr,
                                    OnShaderConstants,nullptr,BeforeCapture,nullptr,BeforeCaptureReset,settings.readOnlyShaderConstants?InspectShaderConstants:nullptr);
    };
    bool installed=installHooks();
    unsigned installAttempts=1;
    DWORD lastInstallTick=GetTickCount();
    const auto logInstall=[&](bool ok) {
        char message[220];std::snprintf(message,sizeof(message),
            "D3D9 hook attempt %u %s (HRESULT 0x%08lx, hal=0x%08lx nullref=0x%08lx swapchain=0x%08lx).",
            installAttempts,
            ok?"succeeded":"failed; will keep retrying",
            static_cast<unsigned long>(D3D9Capture::Snapshot().installResult),
            static_cast<unsigned long>(D3D9Capture::Snapshot().halCreateResult),
            static_cast<unsigned long>(D3D9Capture::Snapshot().nullrefCreateResult),
            static_cast<unsigned long>(D3D9Capture::Snapshot().swapchainResult));
        Log(message);
    };
    logInstall(installed);
    if(installed) {
        char message[120];
        std::snprintf(message,sizeof(message),"D3D9 hooks installed; capture trigger=%s.",
            D3D9Capture::Snapshot().endSceneTrigger?"endscene":"present");
        Log(message);
    }
    if(installed&&settings.autoStart){VRBridge::Request(true);Log("Automatic VR startup requested (AutoStart=1).");}
    bool f9Down=false,f7Down=false;DWORD lastStatus=0;
    long recenterAckSeen=VRBridge::RecenterAck();
    while(workerRunning.load()) {
        NativeHidRefreshKL::Service(Log);
        if(!installed&&GetTickCount()-lastInstallTick>=2000) {
            lastInstallTick=GetTickCount();++installAttempts;
            installed=installHooks();
            if(installed||installAttempts%10==0) {
                logInstall(installed);
                if(installed) {
                    char message[120];
                    std::snprintf(message,sizeof(message),"D3D9 hooks installed; capture trigger=%s.",
                        D3D9Capture::Snapshot().endSceneTrigger?"endscene":"present");
                    Log(message);
                    if(settings.autoStart&&!VRBridge::Requested())
                        {VRBridge::Request(true);Log("Automatic VR startup requested (AutoStart=1).");}
                }
            }
        }
        D3D9Capture::SetEnabled(VRBridge::Requested()||VRBridge::Ready()||settings.captureWithoutHost);
        for(int i=0;i<5&&workerRunning.load();++i) {
            const bool f9=(BackgroundDiagnostic::Key(VK_F9)&0x8000)!=0;
            if(f9&&!f9Down) {
                const bool start=!VRBridge::Requested();
                VRBridge::Request(start);
                Log(start?"F9: starting headset output.":"F9: stopping headset output.");
            }
            f9Down=f9;
            const bool f7=(BackgroundDiagnostic::Key(VK_F7)&0x8000)!=0;
            if(f7&&!f7Down&&VRBridge::Ready()) {
                recenterAckSeen=VRBridge::RecenterAck();
                VRBridge::RequestRecenter();
                Log("F7: recenter requested.");
            }
            f7Down=f7;
            if(workerRunning.load())Sleep(10);
        }
        VRBridge::Tick();
        if(installed)D3D9Capture::Poll();
        VRBridge::SetPanelSize(settings.panelWidthMm,settings.panelDistanceMm);
        if(VRBridge::RecenterAck()!=recenterAckSeen){recenterAckSeen=VRBridge::RecenterAck();Log("Recenter completed by the host.");}
        if(diagnosticsEnabled&&GetTickCount()-lastStatus>1000){configureDiagnostics();PublishAndRecordStatus(installed);lastStatus=GetTickCount();}
    }
    nativeSimulationReady=false;NativeCoverKL::SetSimulationObserver(nullptr);NativeAimKL::SetEnabled(false);
    NativeHudKL::SetReticleEnabled(false);NativeHudKL::SetNativeMarkerProvider(nullptr);NativeHandsKL::InvalidateSupportGrip();
    VRBridge::Request(false);VRBridge::Tick();
    NativePipRenderKL::Shutdown();
    NativeHidRefreshKL::Shutdown();
    NativeFrameWaitKL::SetEnabled(false);
    NativeBufferFlushKL::SetEnabled(false);NativeBufferFlushKL::ConfigureBatching(false);
    PublishAndRecordStatus(installed,true);
    performanceRecorder.Close("worker_exit");
    CrashDiagnostics::Close("worker_exit");
    if(logFile&&logFile!=stdout){std::fclose(logFile);logFile=nullptr;}
}
DWORD WINAPI WorkerThread(LPVOID) {Worker();return 0;}
}

extern "C" __declspec(dllexport) unsigned KL1BackgroundDiagnosticABI(){return 1;}
extern "C" __declspec(dllexport) unsigned KL1BackgroundDiagnosticStage(){return BackgroundDiagnostic::Active()?NativeWeaponKL::Snapshot().physicalPhase:0;}
BOOL APIENTRY DllMain(HMODULE module,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        wchar_t path[MAX_PATH]{};GetModuleFileNameW(module,path,MAX_PATH);
        wchar_t* tail=wcsrchr(path,L'\\');
        if(tail){*tail=0;wcscpy(folder,path);wcscat(folder,L"\\");}
        workerRunning=true;
        HANDLE thread=CreateThread(nullptr,0,WorkerThread,nullptr,0,nullptr);
        if(thread)CloseHandle(thread);
    } else if(reason==DLL_PROCESS_DETACH)workerRunning=false;
    return TRUE;
}
