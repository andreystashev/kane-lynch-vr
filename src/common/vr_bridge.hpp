#pragma once
#include "vr_ipc.hpp"
#include "team_command_types.hpp"
#include "vehicle_control_ipc.hpp"
struct IDirect3DDevice9;
struct IDirect3DSurface9;

namespace VRBridge {
using LogCallback=void(*)(const char*);
struct Stats {
    bool requested,running,ready;
    LONG error,submitted,visible,hmdPresent;
    std::uint32_t leftSequence,rightSequence;
    float ipdMeters;
    bool gpuRequested=false,gpuActive=false;
    std::uint64_t gpuCopies=0,gpuBusy=0,gpuFailures=0,gpuPanelCopiesPaced=0;
    unsigned gpuRetiredGenerations=0;
    unsigned gpuPhysicalTextures=0;
    unsigned gpuIntermediateTextures=0;
    std::uint64_t gpuIntermediateTextureBytes=0,gpuRetiredIntermediateBytes=0;
    std::uint64_t gpuSharedTextureBytes=0;
    std::uint64_t gpuRetiredTextureBytes=0;
    bool gpuCompactPanel=false;
    std::uint64_t cpuPixelMappingBytes=0;
    const char* gpuReason="disabled";
    long gpuCreateResult=0;
};
// Live tracking sample published by the host (LibOVR space: x right, y up,
// z backward; forward is -z). Valid for 250 ms after trackingTick.
struct Tracking {
    bool valid;
    bool mounted;
    float orientation[4]; // x,y,z,w
    float position[3];    // metres from the recentered floor origin
    DWORD tick;
    double sensorTime;   // LibOVR sampling clock, coherent with orientation/position
    LONG recenterAck;    // tracking origin ID, read in the same seqlock as the pose
    float turnYaw;
};
void Initialize(const wchar_t* folder,LogCallback log);
bool Requested();
void Request(bool enable);
void Tick();
void SetPanelSize(unsigned widthMm,unsigned distanceMm);
void SetInputContext(LONG context);
// Invalid camera/input bases clear freshness immediately instead of publishing
// a plausible zero heading that could redirect a held walking stick.
void SetMovementYaw(float radians,bool valid=true,bool lookLocked=false,bool nativeMovementAxes=false,bool nativeCarried=false,bool nativeMounted=false);
bool Ready();
// Copies the host-published per-eye offset (metres) and FOV tangents.
bool EyeConfig(int eye,float* offset,float* fov);
// Latest host tracking sample; valid=false while the host is silent.
Tracking HeadPose();
// Asks the host to ovr_RecenterTrackingOrigin; ack increments on completion.
void RequestRecenter();
long RecenterAck();
void SetNativeMenuReady(bool ready);
void SetGameplayState(bool coverKnown,bool covered,bool weaponLeft,bool humanShield=false,bool sniperAim=false,
    bool stanceKnown=false,bool crouched=false,unsigned pickupMask=0,bool firstPersonSkip=false,bool nativeReloadReady=false);
bool GameplayHudVisible();
bool TeamHudVisible();
bool WeaponMenuVisible();
bool CoverHeld();
bool ClimbHeld();
DWORD TeamNextTick();
bool GrenadeVisualActive();
bool MountedStickActive();
void SetMountedReceiver(const VehicleControlIPC::Certificate& receiver);
void SetPassengerReceiver(const VehicleControlIPC::Certificate& receiver);
struct MountedInput {bool valid=false;DWORD tick=0;float x=0,y=0;};
MountedInput ReadMountedInput(const VehicleControlIPC::Certificate& receiver);
struct PassengerNavigation {bool valid=false;DWORD tick=0;float leftX=0,leftY=0;bool crouched=false;std::uint32_t epoch=0,recenter=0;};
PassengerNavigation ReadPassengerNavigation();
// Separate small mailbox preserves the image IPC ABI. Receiver certificates
// are published only after the matching roster really appears in VR.
void SetTeamReceiver(const TeamCommandIPC::ReceiverCertificate& receiver);
bool ReadTeamCommand(TeamCommandIPC::Request& request);
void PublishTeamCommandResult(const TeamCommandIPC::Result& result);
bool ReadTeamCommandResult(TeamCommandIPC::Result& result);
struct SecondaryFireInput {bool valid=false,pressed=false;DWORD tick=0,reloadTick=0;};
// Filtered Touch input only. Raw tracking/controller triggers must never
// bypass the host mapper's calibration, focus or menu consumption.
SecondaryFireInput ReadSecondaryFire();
struct PickupInput {bool valid=false;unsigned hand=2;DWORD tick=0;};
PickupInput ReadPickup();
unsigned ConsumeMenuCommand();
void SetGPUTransport(bool enable);
bool GPUSceneReady();
bool UseShared9Polling();
void RenderProgress(IDirect3DDevice9* device);
int PublishGPU(IDirect3DDevice9* device,IDirect3DSurface9* back,unsigned mask,const VRIPC::EyeMetadata& metadata);
void InvalidateGPU();
bool Publish(unsigned eye,const VRIPC::EyeMetadata& metadata,const std::uint8_t* pixels,
             std::uint32_t sourceStride=0,bool forceOpaque=false);
Stats Snapshot();
}
