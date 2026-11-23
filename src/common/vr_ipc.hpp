#pragma once
#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <cwchar>

// Fixed-width, pointer-free ABI shared by the x86 game and x64 Meta host.
// Sequence is odd during a write; reader accepts matching even values only.
// Layout follows the proven Hitman Blood Money / Contracts VR transport; only
// the names, magic and frame-kind set differ. Keep sizeof/offsetof asserts in
// sync.
namespace VRIPC {
constexpr std::uint32_t kMagic=0x4B4C5652;
// v6 realigns the eye array after the v5 tracking block broke 16-byte
// alignment (aligned SSE stores in Publish faulted on misaligned metadata).
// v7 adds the actual LibOVR sampling clock and preserves 64-byte alignment
// of both eyes. A v6 host must not attach to the extended mapping.
// v8 adds virtual VR turning inside the tracking seqlock; eye layout stays fixed.
// v9 adds an acknowledged host->game native menu command mailbox.
// v10 selects CPU pixels or a fenced GPU side-channel token per eye.
// v12 accepts larger GPU-only images; the fixed CPU pixel arena/layout stays
// unchanged. Older hosts must not interpret a 4K GPU token as CPU pixels.
// v13 consumes reserved header words for fresh host->game HUD intent; image
// offsets and the GPU/CPU pixel contracts stay unchanged. Both binaries match.
// v14 extends the CPU arena to true3840x2160 as well. Both host and game
// must match: a v13 consumer has a different right-eye offset/mapping size.
// v15 moves optional CPU pixels into an epoch-qualified side-channel. Both
// eye headers now fit in512bytes; a v14 host must refuse this compact layout.
// v16 uses exact-size CPU arenas with immutable dimensions and epoch-qualified
// resizing, and permits native image dimensions through the D3D11 16K bound.
constexpr std::uint32_t kVersion=16;
constexpr std::uint32_t kCPU=1,kGPU=2;
// Scene frames carry a tracked stereo view (game renders alternating eyes);
// kUiFrame stays the monoscopic flat-panel fallback (StereoMode=0).
constexpr std::uint32_t kSceneFrame=1,kUiFrame=2,kScopeFrame=3,kContextFrame=4;
// Game->host input context so the Touch mapping can switch between mission
// and menu bindings; kInputUnknown lets the host infer it from frame kinds.
constexpr LONG kInputUnknown=0,kInputMenu=1,kInputGame=2,kInputSuspended=3,kInputInventory=4,kInputMap=5,kInputCinematic=6,kInputLoading=7;
// CPU compatibility arena. GPU images live in shared textures, not this array.
constexpr std::uint32_t kMaxWidth=16384,kMaxHeight=16384;
constexpr std::size_t kMaxImageBytes=kMaxWidth*kMaxHeight*4;
constexpr std::uint32_t kMaxGPUWidth=16384,kMaxGPUHeight=16384;
inline bool ValidImageDimensions(std::uint32_t width,std::uint32_t height,std::uint32_t transport){
    return width>=16&&height>=16&&
        (transport==kGPU?(width<=kMaxGPUWidth&&height<=kMaxGPUHeight):
         transport==kCPU&&width<=kMaxWidth&&height<=kMaxHeight);
}

#pragma pack(push,8)
struct EyeMetadata {
    std::uint32_t width,height,stride,tick;
    std::uint64_t frameId;
    double sensorTime;
    // Head pose in tracking space at source rendering time: x,y,z,w then xyz.
    float headOrientation[4];
    float headPosition[3];
    // Actual rendered projection tangents: left,right,up,down. All positive.
    float fov[4];
    // 1=stereo scene, 2=mono UI, 3=full-view mono scope, 4=native context list.
    std::uint32_t poseValid;
};
struct alignas(64) EyeFrame {
    volatile LONG sequence;
    std::uint32_t transport,transportEpoch,gpuSlot;
    EyeMetadata metadata;
};
struct alignas(64) SharedFrames {
    std::uint32_t magic,version,bytes,gamePid;
    volatile LONG captureEnabled,shutdown,hostReady,hostError;
    volatile LONG hostSubmittedFrames,hostVisible,hostHmdPresent,hostHeartbeatTick;
    // Published before hostReady=1; immutable for a given host session.
    float eyeOffset[2][3];
    float eyeFov[2][4];
    std::uint32_t recommendedWidth[2],recommendedHeight[2];
    // Panel placement in millimetres; zero keeps the host defaults.
    volatile LONG panelWidthMm,panelDistanceMm;
    // Version 6: host->game live tracking for in-process stereo steering.
    // trackingTick uses GetTickCount; a reader younger than 250 ms may steer.
    volatile LONG trackingTick;
    float trackingOrientation[4]; // x,y,z,w in LibOVR space
    float trackingPosition[3];    // metres, floor-level origin
    volatile LONG recenterRequest,recenterAck;
    volatile LONG inputContext;
    // Odd during a tracking publish; readers re-check after copying.
    volatile LONG trackingSequence;
    // Game->host heading relative to the game camera axes, radians toward right.
    volatile LONG movementSequence,movementTick;
    float movementYaw;
    // movementSequence also protects bit0: native action owns look/turn;
    // bit1: verified Rappel/Climb requires full native movement axes.
    // Its padding role/offset is retained for the existing control ABI.
    std::uint32_t reservedV6[1];
    // Host sampling time, protected by the same trackingSequence as the pose.
    double trackingSensorTime;
    // pack(8) caps member alignment on x86; reserve this padding explicitly.
    float trackingTurnYaw;
    volatile LONG nativeMenuReadyTick,menuCommandSequence,menuCommandAck,menuCommandTick,menuCommand;
    // Flags: cover-known1,covered2,left-only4,shield8,optic16,
    // native-stance-known32,native-crouched64,pickup128/256,
    // first-person-skip512,native-reload-adapter-ready1024. Layout unchanged.
    volatile LONG gameplaySequence,gameplayTick,gameplayFlags;
    // Host->game filtered intent: bit1 HUD, bit2 secondary trigger. The
    // trigger has passed the same mapper calibration/menu/focus consumption.
    // Bits4/8 select pickup hand; bit16 holds the team roster VR panel,
    // bit32 the weapon menu and bit64 deliberate native cover entry.
    volatile LONG hudSequence,hudTick,hudFlags;
    std::uint32_t trackingMounted=0,hudEventTick=0;
    EyeFrame eyes[2];
};
#pragma pack(pop)
static_assert(sizeof(LONG)==4,"IPC requires 32-bit interlocked integers");
static_assert(sizeof(EyeMetadata)==80,"IPC metadata ABI changed");
static_assert(offsetof(EyeFrame,metadata)==16,"IPC metadata alignment changed");
static_assert(offsetof(SharedFrames,trackingSensorTime)==192,"IPC tracking clock alignment changed");
static_assert(offsetof(SharedFrames,trackingTurnYaw)==200,"IPC virtual turn alignment changed");
static_assert(offsetof(SharedFrames,nativeMenuReadyTick)==204,"IPC native menu readiness alignment changed");
static_assert(offsetof(SharedFrames,gameplaySequence)==224&&offsetof(SharedFrames,gameplayFlags)==232,"IPC gameplay state alignment changed");
static_assert(offsetof(SharedFrames,hudSequence)==236&&offsetof(SharedFrames,hudFlags)==244,"IPC HUD intent alignment changed");
static_assert(offsetof(SharedFrames,trackingMounted)==248,"IPC wear-state alignment changed");
static_assert(offsetof(SharedFrames,eyes)==256,"IPC header ABI changed");
static_assert(sizeof(EyeFrame)==128&&sizeof(SharedFrames)==512,"IPC compact metadata layout changed");
static_assert(sizeof(EyeFrame)%64==0&&sizeof(SharedFrames)%64==0,"IPC cache-line alignment changed");
inline constexpr DWORD ManualReloadLifetimeMs=2000;
inline void PublishHudIntent(SharedFrames& frames,bool visible,DWORD now,bool secondaryFire=false,unsigned pickupHand=2,bool teamHeld=false,bool weaponMenuHeld=false,bool coverHeld=false,bool grenadeVisual=false,bool mountedStick=false,DWORD reloadTick=0,bool climbHeld=false,DWORD teamNextTick=0){
    InterlockedIncrement(&frames.hudSequence);
    InterlockedExchange(&frames.hudFlags,(visible?1:0)|(secondaryFire?2:0)|(pickupHand<2?(4<<pickupHand):0)|(teamHeld?16:0)|(weaponMenuHeld?32:0)|(coverHeld?64:0)|(grenadeVisual?128:0)|(mountedStick?256:0)|(reloadTick?512:0)|(climbHeld?1024:0)|(teamNextTick?2048:0));
    // Reload belongs to gameplay and receiver selection belongs to the held panel.
    frames.hudEventTick=teamNextTick?teamNextTick:reloadTick;
    InterlockedExchange(&frames.hudTick,static_cast<LONG>(now));
    MemoryBarrier();InterlockedIncrement(&frames.hudSequence);
}
namespace Detail {
struct HostIntent {bool valid=false;DWORD tick=0;LONG flags=0,context=0;DWORD reloadTick=0,teamNextTick=0;};
inline HostIntent ReadHostIntent(const SharedFrames& frames,DWORD now){
    const auto before=frames.hudSequence;MemoryBarrier();
    const auto pid=frames.gamePid;
    if(!before||(before&1)||frames.magic!=kMagic||frames.version!=kVersion||frames.bytes!=sizeof(SharedFrames)||
       !pid||!frames.captureEnabled||!frames.hostReady||frames.shutdown)return {};
    const auto tick=static_cast<DWORD>(frames.hudTick);const auto flags=frames.hudFlags,context=frames.inputContext;
    const auto eventTick=frames.hudEventTick;
    MemoryBarrier();
    if(before!=frames.hudSequence||frames.magic!=kMagic||frames.version!=kVersion||frames.bytes!=sizeof(SharedFrames)||
       frames.gamePid!=pid||!frames.captureEnabled||!frames.hostReady||frames.shutdown||context!=frames.inputContext||
         !tick||static_cast<LONG>(now-tick)<0||now-tick>100||(flags&~LONG(4095))||(flags&12)==12)return {};
    return {true,tick,flags,context,(flags&512)?eventTick:0u,(flags&2048)?eventTick:0u};
}
}
inline bool ReadHudIntent(const SharedFrames& frames,DWORD now){
    const auto intent=Detail::ReadHostIntent(frames,now);return intent.valid&&(intent.flags&1);
}
inline bool ReadTeamHudIntent(const SharedFrames& frames,DWORD now){
    const auto intent=Detail::ReadHostIntent(frames,now);
    return intent.valid&&intent.context==kInputGame&&(intent.flags&16);
}
inline bool ReadWeaponMenuIntent(const SharedFrames& frames,DWORD now){
    const auto intent=Detail::ReadHostIntent(frames,now);
    return intent.valid&&intent.context==kInputGame&&(intent.flags&32);
}
inline bool ReadCoverIntent(const SharedFrames& frames,DWORD now){
    const auto intent=Detail::ReadHostIntent(frames,now);
    return intent.valid&&intent.context==kInputGame&&(intent.flags&64)&&!(intent.flags&48);
}
inline bool ReadClimbIntent(const SharedFrames& frames,DWORD now){
    const auto intent=Detail::ReadHostIntent(frames,now);
    return intent.valid&&intent.context==kInputGame&&(intent.flags&1024)&&!(intent.flags&48);
}
inline DWORD ReadTeamNextIntent(const SharedFrames& frames,DWORD now){
    const auto intent=Detail::ReadHostIntent(frames,now);
    if(!intent.valid||intent.context!=kInputGame||(intent.flags&(16|32|2048))!=(16|32|2048)||
       !intent.teamNextTick||now-intent.teamNextTick>300)return 0;
    return intent.teamNextTick;
}
inline bool ReadGrenadeVisualIntent(const SharedFrames& frames,DWORD now){
    const auto intent=Detail::ReadHostIntent(frames,now);
    return intent.valid&&intent.context==kInputGame&&(intent.flags&128)&&!(intent.flags&48);
}
inline bool ReadMountedStickIntent(const SharedFrames& frames,DWORD now){
    const auto intent=Detail::ReadHostIntent(frames,now);
    return intent.valid&&intent.context==kInputGame&&(intent.flags&256)&&!(intent.flags&48);
}
struct SecondaryFireIntent {bool valid=false,pressed=false;DWORD tick=0,reloadTick=0;};
inline SecondaryFireIntent ReadSecondaryFireIntent(const SharedFrames& frames,DWORD now){
    const auto intent=Detail::ReadHostIntent(frames,now);
    if(!intent.valid||intent.context!=kInputGame)return {};
    const auto reload=(intent.flags&512)&&!(intent.flags&48)&&intent.reloadTick&&
        static_cast<LONG>(now-intent.reloadTick)>=0&&now-intent.reloadTick<=ManualReloadLifetimeMs?intent.reloadTick:0u;
    return {true,(intent.flags&2)!=0,intent.tick,reload};
}
  struct PickupIntent {bool valid=false;unsigned hand=2;DWORD tick=0;};
  inline PickupIntent ReadPickupIntent(const SharedFrames& frames,DWORD now){
      const auto intent=Detail::ReadHostIntent(frames,now);
      if(!intent.valid||intent.context!=kInputGame)return {};
      return {true,(intent.flags&4)?0u:(intent.flags&8)?1u:2u,intent.tick};
  }
inline void MappingName(wchar_t* out,std::size_t count,DWORD pid) {
    swprintf(out,count,L"Local\\KaneLynchVR-%lu",pid);
}
}
