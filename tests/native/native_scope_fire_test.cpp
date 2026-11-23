// Actual production callbacks; only native-game/runtime entry points are
// substituted. Private in-process data, no detours, game, headset or input.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
namespace Fixture {inline DWORD fixtureTick=1000;inline DWORD Clock(){return fixtureTick;}}
#define GetTickCount Fixture::Clock
#include "native_weapon_kl.cpp"
#include "native_gameplay_hud_kl.cpp"
#include "native_laser_pose_kl.hpp"
#include "native_aim_render_math.hpp"
#include <cstdio>
#include <cstdlib>

namespace D3D9Capture {HRESULT ReplayNativeDraw(IDirect3DDevice9*,const DrawCall&){std::abort();}}
namespace Fixture {
using namespace NativeCameraMath;
unsigned checks=0,failures=0,handsReads=0,nativeFires=0,traceCalls=0,credits=0,hookCreates=0,observerCalls=0,worldCalls=0,order=0;
unsigned rigidObservers=0,rigidNativeCalls=0;
unsigned overlayActivates=0,overlayClears=0;
unsigned ordinaryCrosshairs=0;
unsigned nativeReloads=0;
unsigned tutorialShots=0,tutorialLastNativeFire=0;
bool opticView=true;
DWORD manualReloadTick=0;
std::uint64_t drawGeneration=3;
std::uint64_t captureImageEpoch=11;
unsigned captureImageEye=0;
bool liveDrawAvailable=true,drawVisualAvailable=true,captureImageValid=true;
bool changeCaptureDuringRead=false;
unsigned captureReadCount=0;
bool firstPersonDraw=true;
bool ready=true,consumeOrigin=true,spread=false,vrDraw=false,teamMenu=false;
bool simulationVisualAvailable=true;
std::uintptr_t expectedRigidEntry=0x123400,expectedRigidState=0x234500;
bool renderedFixture=false;
std::uintptr_t nativeProvider=0,nativeProviderState=0;unsigned nativeProviderPass=0;
NativeHandsKL::VisualFrame renderedHands;
NativeHandsKL::DrawController drawController;
bool drawControllerAvailable=false;
long recenter=7;
NativeAimKL::GazeTrace traceResult=NativeAimKL::GazeTrace::Hit;
Vec3 hit{},capturedTarget{},capturedOrigin{},expectedOrigin{};
NativeActionKL::State actionState{};
std::uintptr_t item=0,player=0;
void* trampoline=nullptr;
void Check(bool condition,const char* text){++checks;if(!condition){++failures;std::printf("FAIL: %s\n",text);}}
bool Equal(Vec3 a,Vec3 b){return !std::memcmp(&a,&b,sizeof(a));}
template<class T>void Put(std::uintptr_t at,const T& value){std::memcpy(reinterpret_cast<void*>(at),&value,sizeof(value));}
void __thiscall Target(void* object,const Vec3* value){Put(reinterpret_cast<std::uintptr_t>(object)+0xC4,*value);}
void __thiscall Attachment(void*,Basis* basis,Vec3* position){*basis=Basis{};*position={17,23,41};}
void __thiscall CachedMuzzle(void* object,const Vec3* value){Put(reinterpret_cast<std::uintptr_t>(object)+0x134,*value);}
void __thiscall PumpFire(void* object) {
    ++nativeFires;const auto at=reinterpret_cast<std::uintptr_t>(object);
    Check(NativeWeaponKL::shot.active&&!NativeWeaponKL::shot.headScope,"actual pump Fire executes in its normal native tracked scope");
    capturedTarget=*reinterpret_cast<const Vec3*>(at+0xC4);capturedOrigin=*reinterpret_cast<const Vec3*>(at+0x134);
    Basis basis;Vec3 position;NativeWeaponKL::AttachmentHook(object,nullptr,&basis,&position);
    Check(Equal(position,NativeWeaponKL::shot.attachment.position),"pump Fire receives the exact rendered attachment, not a newer animation sample");
    const auto randomized=Add(capturedTarget,{.125f,.25f,-.125f});
    NativeWeaponKL::SetTargetHook(object,nullptr,&randomized);
    Check(Equal(*reinterpret_cast<const Vec3*>(at+0xC4),randomized),"pump pellet spread remains native inside tracked Fire");
    Put(at+0x134,Vec3{});
}
void __thiscall Fire(void* object) {
    ++nativeFires;const auto at=reinterpret_cast<std::uintptr_t>(object);
    capturedTarget=*reinterpret_cast<const Vec3*>(at+0xC4);capturedOrigin=*reinterpret_cast<const Vec3*>(at+0x134);
    Check(NativeWeaponKL::shot.active&&NativeWeaponKL::shot.headScope,"actual original FF Fire runs inside the head-scope target-only context");
    Check(Equal(capturedOrigin,expectedOrigin),"native origin +134 reaches Fire byte-exact without a head-origin replacement");
    Basis basis;Vec3 position;NativeWeaponKL::AttachmentHook(object,nullptr,&basis,&position);
    Check(Equal(position,{17,23,41})&&ValidateBasis(basis),"actual AttachmentHook preserves the native muzzle/attachment in a scope shot");
    if(spread) {
        const auto randomized=Add(capturedTarget,{.125f,.25f,-.125f});
        NativeWeaponKL::SetTargetHook(object,nullptr,&randomized);
        Check(Equal(*reinterpret_cast<const Vec3*>(at+0xC4),randomized),"native internal spread target is not overwritten with another HMD/controller point");
    }
    if(consumeOrigin)Put(at+0x134,Vec3{});
}
void __thiscall NativeGrenade(void* object) {
    ++nativeFires;const auto at=reinterpret_cast<std::uintptr_t>(object);
    capturedTarget=*reinterpret_cast<const Vec3*>(at+0xC4);capturedOrigin=*reinterpret_cast<const Vec3*>(at+0x134);
    Put(at+0x134,Vec3{});
}
void __thiscall ActivateOverlay(void* object,unsigned mode) {
    ++overlayActivates;const auto logic=reinterpret_cast<std::uintptr_t>(object);
    const auto overlay=*reinterpret_cast<const std::uint32_t*>(logic+0x678);
    Put(logic+0x674,std::uint8_t(1));Put(overlay+0x220,std::uint32_t(mode));
    Put(overlay+0x1B0,*reinterpret_cast<const std::uint32_t*>(overlay+0x1C4));
}
void __thiscall ClearOverlay(void* object) {
    ++overlayClears;const auto logic=reinterpret_cast<std::uintptr_t>(object);
    const auto overlay=*reinterpret_cast<const std::uint32_t*>(logic+0x678);
    Put(logic+0x674,std::uint8_t(0));Put(overlay+0x220,std::uint32_t(0));Put(overlay+0x1B0,std::uint32_t(0));
}
void __thiscall Crosshair(void*,void*,void*){++ordinaryCrosshairs;}
void __thiscall Reload(void*,void*){++nativeReloads;}
void __thiscall ScopeWorld(void* object,const CameraPose* pose){Put(reinterpret_cast<std::uintptr_t>(object)+0x8C,*pose);}
void __thiscall World(void*,const CameraPose* source) {
    ++worldCalls;Check(order==1,"world observer runs before the native world binder");order=2;
    Check(source&&source->position.x==123,"Aim-off common world hook preserves the exact native source pose");
}
void ObserveWorld(void*,const CameraPose*,std::uint32_t){++observerCalls;order=1;}
void ObserveRigid(void*,const CameraPose* source,std::uint32_t caller){++rigidObservers;
    if(!source){Check(order==0,"rigid PRE begins before native WORLD math");order=1;}
    else {Check(order==2&&caller==0x739E0&&source==reinterpret_cast<const CameraPose*>(expectedRigidEntry+0x10),
                "rigid POST identifies the exact GPU entry only after native WORLD has completed");order=3;}}
void __thiscall Rigid(void* entry,void* state,std::uintptr_t extra) {
    ++rigidNativeCalls;Check(order==1,"world observer runs before the inline native rigid binder");order=2;
    Check(entry==reinterpret_cast<void*>(expectedRigidEntry)&&state==reinterpret_cast<void*>(expectedRigidState)&&extra==0x47,
          "clearing a previous rope/effects tag preserves every native rigid argument");
}
}
namespace NativeHandsKL {
bool SnapshotArmDiagnostic(unsigned,ArmDiagnostic&){return false;}

bool SnapshotControllerAim(unsigned side,ControllerAim& out){if(side!=1||!Fixture::ready)return false;out.player=Fixture::player;out.pairId=Fixture::renderedFixture?Fixture::renderedHands.pairId:55;
    out.tick=GetTickCount();out.recenterAck=Fixture::recenter;out.unitsPerMetre=100;out.origin=Fixture::expectedOrigin;out.direction={1,0,0};return true;}
bool DrawFrameActive(std::uint64_t& pair,std::uint64_t* generation){++Fixture::handsReads;if(!Fixture::renderedFixture||!Fixture::liveDrawAvailable)return false;pair=Fixture::renderedHands.pairId;if(generation)*generation=Fixture::drawGeneration;return true;}
bool SnapshotVisual(VisualFrame& out){++Fixture::handsReads;if(!Fixture::renderedFixture||!Fixture::simulationVisualAvailable)return false;out=Fixture::renderedHands;return true;}
bool SnapshotReloadControllers(std::uintptr_t,std::uint32_t,VisualFrame&){return false;}
bool SnapshotDrawVisual(VisualFrame& out){++Fixture::handsReads;if(!Fixture::renderedFixture||!Fixture::liveDrawAvailable||!Fixture::drawVisualAvailable)return false;out=Fixture::renderedHands;return true;}
bool SnapshotDrawController(std::uintptr_t actor,std::uint64_t pair,unsigned,DrawController& output){
    if(!Fixture::drawControllerAvailable||actor!=Fixture::player||pair!=Fixture::renderedHands.pairId)return false;
    output=Fixture::drawController;return true;
}
bool ReadCurrentNativeEntry(std::uintptr_t state,std::uintptr_t& entry,unsigned* pass){
    if(!Fixture::nativeProvider||state!=Fixture::nativeProviderState)return false;entry=Fixture::nativeProvider;if(pass)*pass=Fixture::nativeProviderPass;return true;}
bool FrameBodyRoot(std::uintptr_t player,std::uint64_t pair,NativeCameraMath::CameraPose& raw,NativeCameraMath::CameraPose& rendered) {
    const auto& h=Fixture::renderedHands;if(!Fixture::renderedFixture||h.player!=player||h.pairId!=pair||!h.bodyAvailableForMesh)return false;
    raw=h.rawRoot;rendered=h.renderRoot;return true;
}
}
namespace NativeCameraKL {
bool CinematicActive(){return false;}
bool ReadRenderContext(std::uint64_t& epoch,unsigned& eye){
    if(!Fixture::renderedFixture||!Fixture::liveDrawAvailable)return false;
    epoch=Fixture::captureImageEpoch;eye=Fixture::captureImageEye;return true;
}
bool ReadCaptureContext(std::uint64_t& epoch,unsigned& eye,std::uint64_t& pair){
    if(!Fixture::renderedFixture||!Fixture::captureImageValid)return false;
    epoch=Fixture::captureImageEpoch+((Fixture::changeCaptureDuringRead&&++Fixture::captureReadCount>1)?1:0);
    eye=Fixture::captureImageEye;pair=Fixture::renderedHands.pairId;return true;
}
bool DrawStateActive(std::uintptr_t,bool* firstPerson,bool){if(firstPerson)*firstPerson=Fixture::vrDraw&&Fixture::firstPersonDraw;return Fixture::vrDraw;}
}
namespace NativeActionKL {
State Current(){return Fixture::actionState;}
bool PreserveRig(){return false;}
bool InitializeVerifiedImage(std::uintptr_t){return true;}
}
namespace VRBridge {bool Ready(){return Fixture::ready;}long RecenterAck(){return Fixture::recenter;}bool TeamHudVisible(){return Fixture::teamMenu;}bool WeaponMenuVisible(){return false;}
SecondaryFireInput ReadSecondaryFire(){return {Fixture::ready,false,Fixture::fixtureTick,Fixture::manualReloadTick};}}
namespace NativeCoverKL {void OnTrackedFire(std::uintptr_t){++Fixture::credits;}bool SimulationCallbackActive(std::uintptr_t p){return p==Fixture::player;}}
namespace NativeTutorialKL {void NotifyShot(std::uintptr_t player){Fixture::Check(player==Fixture::player&&Fixture::nativeFires>Fixture::tutorialLastNativeFire,
    "tutorial notification follows one completed issued native Fire");Fixture::tutorialLastNativeFire=Fixture::nativeFires;++Fixture::tutorialShots;}}
namespace NativeHudKL {
bool OriginalScopeOverlayActive(){return Fixture::opticView&&NativeWeaponKL::ScopeOverlayActive();}
bool ReticleEnabled(){return true;}
void BeginReticle(std::uintptr_t,void*){}
void EndReticle(){}
void BeginNativeMarkers(std::uintptr_t,void*,void*){}
void EndNativeMarkers(){}
}
namespace NativeAimKL {
bool TraceReloadFloor(std::uintptr_t,NativeCameraMath::Vec3,float,float&){return false;}
GazeTrace TraceGaze(const NativeWeaponKL::HeadGaze& gaze,const NativeWeaponKL::AimFrame& weapon,NativeCameraMath::Vec3& point) {
    ++Fixture::traceCalls;
    Fixture::Check(NativeWeaponKL::ScopeQueryActive(weapon.player,weapon.weapon),"native ROOM query has a capability only on the active owned Fire thread");
    Fixture::Check(gaze.player==Fixture::player&&weapon.weapon==Fixture::item,"query certificate binds the same current local sniper");
    if(Fixture::traceResult==GazeTrace::Hit)point=Fixture::hit;
    return Fixture::traceResult;
}
}
extern "C" {
MH_STATUS WINAPI MH_Initialize(){return MH_OK;}
MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID* original){++Fixture::hookCreates;*original=Fixture::trampoline;return MH_OK;}
MH_STATUS WINAPI MH_EnableHook(LPVOID){return MH_OK;}
MH_STATUS WINAPI MH_DisableHook(LPVOID){return MH_OK;}
MH_STATUS WINAPI MH_RemoveHook(LPVOID){return MH_OK;}
MH_STATUS WINAPI MH_QueueEnableHook(LPVOID){return MH_OK;}
MH_STATUS WINAPI MH_QueueDisableHook(LPVOID){return MH_OK;}
MH_STATUS WINAPI MH_ApplyQueued(){return MH_OK;}
}
int main() {
    using namespace Fixture;
    static_assert(sizeof(void*)==4,"production native ABI fixture is x86");
    auto* allocation=VirtualAlloc(nullptr,0x900000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    Check(allocation!=nullptr,"private native-layout fixture allocation");if(!allocation)return 1;
    const auto base=reinterpret_cast<std::uintptr_t>(allocation);
    const auto ptr=[&](std::uintptr_t offset){return static_cast<std::uint32_t>(base+offset);};
    const std::uint32_t data=ptr(0x10000),actor=ptr(0x20000),camera=ptr(0x30000),aim=ptr(0x40000),weapon=ptr(0x50000),root=ptr(0x60000),model=ptr(0x70000);
    item=weapon;player=actor;
    Put(base+0x8883A4,std::uint8_t(0));Put(base+0x68760C,data);Put(data+0xA2C,actor);
    Put(actor,ptr(0x56582C));Put(actor+0x9A0,camera);Put(camera,ptr(0x545A3C));
    Put(actor+0x9AC,aim);Put(aim,ptr(NativeCameraMoverKL::MouseVtable));Put(aim+8,actor);Put(aim+0x9C,std::uint8_t(1));Put(aim+0x68,std::uint32_t(4));
    Put(actor+0x358,ptr(0x5508B4));Put(actor+0x2D8,ptr(0x5508B4));
    NativeWeaponOwnership::HandState hand;hand.item=weapon;hand.enabled=1;hand.attacher=31;
    Put(actor+0x358+0x60,hand);Put(actor+0x2D8+0x60,NativeWeaponOwnership::HandState{});
    Put(weapon,ptr(0x577B74));Put(weapon+4,root);Put(root+0x60,weapon);Put(weapon+0xC0,model);
    Put(model,ptr(0x577864));Put(model+0xA0,std::uint32_t(1));Put(model+0xA4,std::uint32_t(15));Put(weapon+0x58,std::uint32_t(37));
    Put(base+0x577B74+0x308,ptr(0x11DDB0));Put(base+0x577B74+0x2D4,ptr(0x11DF40));Put(weapon+0xD8,1u^0xBEEBBEEBu);
    NativeWeaponKL::image=base;NativeWeaponKL::scopeImage=base;
    NativeWeaponKL::original=&Target;NativeWeaponKL::originalFireFF=&Fire;NativeWeaponKL::originalAttachment=&Attachment;
    NativeWeaponKL::HeadGaze gaze;gaze.player=actor;gaze.sourceCamera=camera;gaze.pairId=0;gaze.recenterAck=recenter;
    gaze.tick=GetTickCount();gaze.origin={100,170,500};gaze.direction={1,0,0};gaze.mounted=true;
    NativeWeaponKL::PublishHeadGaze(gaze);
    const Vec3 savedTarget{-123,45,678};expectedOrigin={140,150,510};Put(weapon+0x134,expectedOrigin);Put(weapon+0xC4,savedTarget);
    hit=Add(gaze.origin,Scale(gaze.direction,3700));spread=true;
    Check(!NativeWeaponKL::ScopeQueryActive(actor,weapon),"rendering/outside callbacks cannot authorize a scope query");
    Check(NativeWeaponKL::ScopedHeadFire(reinterpret_cast<void*>(item),&Fire),"production scope path handles a current sniper with an actual contact");
    Check(tutorialShots==1,"a proved last-round scoped shot emits one tutorial token after native Fire, before native caller consumes its round");
    Check(Equal(capturedTarget,hit)&&!Equal(capturedTarget,savedTarget),"actual FF Fire receives the HMD ray contact instead of the actor/controller target");
    Check(Equal(*reinterpret_cast<const Vec3*>(weapon+0xC4),savedTarget),"target restore returns the exact prior owned item value after native Fire");
    Check(Equal(*reinterpret_cast<const Vec3*>(weapon+0x134),{}),"native consumption of the original one-shot muzzle is not reversed");
    Check(nativeFires==1&&traceCalls==1&&credits==1&&handsReads==0,"one native shot/query is sufficient without a visible gun/body feedback loop");
    NativeWeaponKL::originalPrecision=[](void* object,bool value) __attribute__((thiscall)){Put(reinterpret_cast<std::uintptr_t>(object)+0x9C,std::uint8_t(value));};
    NativeWeaponKL::scopeGestureEnabled=true;NativeWeaponKL::scopeGestureContext=true;
    NativeWeaponKL::scopeGestureOwner={NativeWeaponKL::ReadScope(),static_cast<std::uint32_t>(recenter),GetTickCount(),NativeWeaponKL::scopeGestureEpoch.load(),true};
    NativeWeaponKL::PrecisionHook(reinterpret_cast<void*>(aim),nullptr,false);
    Check(*reinterpret_cast<std::uint8_t*>(aim+0x9C)==1,"native input cannot undo the current owned near-face sniper request");
    NativeWeaponKL::scopeGestureOwner.requested=false;
    NativeWeaponKL::PrecisionHook(reinterpret_cast<void*>(aim),nullptr,true);
    Check(*reinterpret_cast<std::uint8_t*>(aim+0x9C)==0,"lowered right controller rejects stale native precision on the owned sniper");
    NativeWeaponKL::InvalidateScopeGesture();
    NativeWeaponKL::PrecisionHook(reinterpret_cast<void*>(aim),nullptr,true);
    Check(*reinterpret_cast<std::uint8_t*>(aim+0x9C)==1,"invalidated gesture cannot override unrelated native precision writes");
    NativeWeaponKL::scopeGestureEnabled=false;
    Put(aim,ptr(0x56AB44));
    Check(!NativeWeaponKL::ScopedHeadFire(reinterpret_cast<void*>(item),&Fire)&&nativeFires==1,
          "the old rendering-camera vtable never authorizes a scope shot from an impossible +9AC layout");
    Put(aim,ptr(NativeCameraMoverKL::GamepadVtable));
    Check(!NativeWeaponKL::ScopeQueryActive(actor,weapon),"scope query capability retires after the callback");
    traceResult=NativeAimKL::GazeTrace::Miss;spread=false;consumeOrigin=false;expectedOrigin={};Put(weapon+0x134,expectedOrigin);
    const auto priorTutorialShots=tutorialShots;Put(weapon+0xD8,0u^0xBEEBBEEBu);
    Check(NativeWeaponKL::ScopedHeadFire(reinterpret_cast<void*>(item),&Fire),"a native gamepad mover and proved ROOM miss still fire with an unchanged native muzzle path");
    Check(tutorialShots==priorTutorialShots,"even a direct empty-magazine Fire invocation cannot grant tutorial shot credit");
    Put(weapon+0xD8,7u^0xBEEBBEEBu);
    Check(Equal(capturedTarget,Add(gaze.origin,Scale(gaze.direction,NativeScopeKL::MaximumAimRangeMetres*gaze.unitsPerMetre))),
          "miss convergence is beyond the native visible far plane, not an artificial 100 m reticle point");
    Check(Equal(*reinterpret_cast<const Vec3*>(weapon+0x134),{}),"zero native origin remains zero so native attachment logic owns the spawn");
    const auto shotsBefore=nativeFires;traceResult=NativeAimKL::GazeTrace::Unavailable;
    Check(!NativeWeaponKL::ScopedHeadFire(reinterpret_cast<void*>(item),&Fire)&&nativeFires==shotsBefore&&tutorialShots==priorTutorialShots&&
          Equal(*reinterpret_cast<const Vec3*>(weapon+0xC4),savedTarget),"unavailable collision proof preserves the previous native path without a fabricated contact");
    ++recenter;Check(!NativeWeaponKL::ScopedHeadFire(reinterpret_cast<void*>(item),&Fire),"changed tracking origin retires a previously published gaze");--recenter;
    NativeWeaponKL::InvalidateHeadGaze();Check(!NativeWeaponKL::ScopedHeadFire(reinterpret_cast<void*>(item),&Fire),"UI/runtime invalidation cannot reuse held scope gaze");
    NativeWeaponKL::PublishHeadGaze(gaze);Put(model+0xA4,std::uint32_t(4));
    Check(!NativeWeaponKL::ScopedHeadFire(reinterpret_cast<void*>(item),&Fire),"ordinary rifle precision never enters the sniper-only firing path");
    Put(model+0xA4,std::uint32_t(15));Put(aim+0x9C,std::uint8_t(0));
    Check(!NativeWeaponKL::ScopedHeadFire(reinterpret_cast<void*>(item),&Fire),"unscoped sniper fire retains its normal controller path");
    // Real scope simulation entry + actual private hands IPC. Time alone is
    // deterministic; native precision setter is the same exact-layout stub.
    HandsIPC::Producer handsInput;Check(handsInput.Open(GetCurrentProcessId()),"private hands IPC fixture opens without a game/host");
    HandsIPC::Frame raised;raised.present=raised.mounted=1;raised.recenterAck=recenter;
    raised.head.orientation[3]=raised.hands[1].orientation[3]=1;
    raised.head.flags=raised.hands[1].flags=HandsIPC::kOrientation|HandsIPC::kPosition;
    raised.head.position[1]=1.6f;raised.hands[1].position[0]=.1f;raised.hands[1].position[1]=1.55f;raised.hands[1].position[2]=-.2f;
    raised.grip[0]=1; // A real left foregrip cannot block right near-face entry.
    NativeWeaponKL::SetScopeGestureEnabled(true);NativeWeaponKL::SetScopeGestureContext(true);
    const auto sim=[&](DWORD now){fixtureTick=now;raised.tick=now;handsInput.Publish(raised);NativeWeaponKL::ScopeSimulationTick(actor);};
    sim(2000);Check(!*reinterpret_cast<std::uint8_t*>(aim+0x9C),"production simulation does not enter on its first raised sample");
    sim(2120);Check(*reinterpret_cast<std::uint8_t*>(aim+0x9C)&&NativeWeaponKL::Snapshot().scopeGestureEntered==1,
        "right controller120ms near face enters actual native precision while the left grip stays held");
    NativeWeaponKL::PrecisionHook(reinterpret_cast<void*>(aim),nullptr,false);
    Check(*reinterpret_cast<std::uint8_t*>(aim+0x9C),"next native zero input cannot cancel a valid near-face scope");
    raised.hands[1].position[2]=-.7f;sim(2140);sim(2320);
    Check(!*reinterpret_cast<std::uint8_t*>(aim+0x9C),"lowering the physical right controller exits native precision");
    raised.hands[1].position[2]=-.2f;sim(2340);sim(2460);teamMenu=true;sim(2470);
    Check(!*reinterpret_cast<std::uint8_t*>(aim+0x9C),"team UI immediately retires the owned optical precision state");teamMenu=false;
    sim(2500);sim(2620);Put(weapon+0x58,std::uint32_t(38));sim(2640);
    Check(!*reinterpret_cast<std::uint8_t*>(aim+0x9C),"same mover cleanup retires a previous request after exact weapon generation change");
    Put(weapon+0x58,std::uint32_t(37));sim(2700);sim(2820);
    NativeWeaponKL::SetScopeGestureContext(false);sim(2840);sim(2960);
    Check(!*reinterpret_cast<std::uint8_t*>(aim+0x9C)&&!NativeWeaponKL::Snapshot().scopeGestureRequested,
        "repeated simulation ticks in native UI cannot issue a new precision request between invalidations");
    NativeWeaponKL::SetScopeGestureEnabled(false);fixtureTick=3000;
    handsInput.Close();
    const unsigned char worldPrefix[]={0x53,0x8B,0xD9,0x56,0x8B,0x74,0x24,0x0C,0x57,0x8D,0x93,0x8C,0,0,0,0x8B,0xFA,0xB9,0x0C,0,0,0,0xF3,0xA5};
    std::memcpy(reinterpret_cast<void*>(base+0x71770),worldPrefix,sizeof(worldPrefix));trampoline=reinterpret_cast<void*>(&World);
    NativeWeaponKL::installed=false;
    Check(NativeWeaponKL::SetWorldBindObserver(&ObserveWorld)&&hookCreates==1,"Aim-off effects can acquire the single proved common world hook");
    Check(NativeWeaponKL::SetWorldBindObserver(&ObserveWorld)&&hookCreates==1,"reconnecting the observer never creates a competing detour");
    CameraPose pose;pose.position.x=123;NativeWeaponKL::WorldBindHook(nullptr,nullptr,&pose);
    Check(observerCalls==1&&worldCalls==1&&order==2&&handsReads==0,"common hook calls observer then native binder without weapon/hand tracking when Aim is off");
    Check(NativeWeaponKL::SetWorldBindObserver(nullptr)&&hookCreates==1,"retiring the observer preserves the shared hook lifetime");
    Check(NativeWeaponKL::SetWorldBindObserver(&ObserveRigid)&&hookCreates==1,"inline rigid path reuses the same common observer without another detour");
    NativeWeaponKL::originalRigid=&Rigid;order=0;const auto beforeHands=handsReads,beforeWorlds=worldCalls;
    NativeWeaponKL::RigidBindHook(reinterpret_cast<void*>(0x123400),nullptr,reinterpret_cast<void*>(0x234500),0x47);
    Check(rigidObservers==1&&rigidNativeCalls==1&&order==2&&handsReads==beforeHands&&worldCalls==beforeWorlds,
          "unrelated non-VR rigid entry is observed then preserved without wrist remap or replacement world call");
    NativeWeaponKL::SetWorldBindObserver(nullptr);
    // Exercise the actual grenade ownership/mover gate as well as its math.
    NativeWeaponKL::scopeGestureContext=true;
    const auto action=ptr(0x80000);Put(action+8,actor);
    actionState.valid=true;actionState.player=actor;actionState.action=action;
    actionState.actionVtable=ptr(NativeGrenadeKLImage::ThrowVtable);
    NativeWeaponKL::grenadeScope={};NativeWeaponKL::grenadeScope.active=true;NativeWeaponKL::grenadeScope.action=action;
    NativeWeaponKL::grenadeScope.player=actor;
    Put(model+0xA4,std::uint32_t(11));Put(aim+0x34,Vec3{0,0,1});
    gaze.direction={0,0,-1};gaze.tick=GetTickCount();NativeWeaponKL::PublishHeadGaze(gaze);
    expectedOrigin={140,150,510};Put(weapon+0x134,expectedOrigin);
    gaze.tick=GetTickCount();NativeWeaponKL::PublishHeadGaze(gaze);
    const auto nativeThrowTarget=Add(expectedOrigin,{0,90,300});Vec3 throwTarget;
    for(const auto mover:{NativeCameraMoverKL::MouseVtable,NativeCameraMoverKL::GamepadVtable}) {
        Put(aim,ptr(mover));
        Check(NativeWeaponKL::GrenadeTarget(weapon,&nativeThrowTarget,throwTarget)&&
              Equal(throwTarget,Add(expectedOrigin,{300,90,0}))&&NativeWeaponKL::grenadeProofStage==7,
              "actual grenade callback accepts both native mover classes and remaps target to HMD gaze");
        Check(Equal(*reinterpret_cast<const Vec3*>(weapon+0x134),expectedOrigin),"mover correction preserves the actual native throw origin");
    }
    Put(aim,ptr(0x56AB44));
    Check(!NativeWeaponKL::GrenadeTarget(weapon,&nativeThrowTarget,throwTarget)&&NativeWeaponKL::grenadeProofStage==4,
          "the old renderer-camera vtable rejects at the diagnostic mover gate, never impersonating a player mover");
    Put(aim,ptr(NativeCameraMoverKL::MouseVtable));Put(actor+0x9A0,camera+4);
    Check(!NativeWeaponKL::GrenadeTarget(weapon,&nativeThrowTarget,throwTarget)&&NativeWeaponKL::grenadeProofStage==3,
          "grenade gaze binds the native render camera at +9A0 independently of its input mover");
    Put(actor+0x9A0,camera);actionState={};
    Check(!NativeWeaponKL::GrenadeTarget(weapon,&nativeThrowTarget,throwTarget)&&NativeWeaponKL::grenadeProofStage==1,
          "missing owned Throw action retires the grenade callback before camera or aim reads");
    // The rigid post boundary cannot reinterpret the native entry before its
    // WORLD is finished, nor leave a tag across the following unrelated bind.
    expectedRigidEntry=ptr(0xA8000);expectedRigidState=ptr(0xA9000);vrDraw=true;order=0;renderedFixture=false;
    NativeWeaponKL::worldBindObserver=&ObserveRigid;NativeWeaponKL::originalRigid=&Rigid;
    Put(base+0x7678A4,std::uint32_t(0));
    NativeWeaponKL::RigidBindHook(reinterpret_cast<void*>(expectedRigidEntry),nullptr,reinterpret_cast<void*>(expectedRigidState),0x47);
    Check(order==3,"VR rigid draw publishes PRE/native/POST in that order");
    vrDraw=false;NativeWeaponKL::worldBindObserver=nullptr;
    // A complete held pump mesh -> cached ray -> actual tracked Fire path.
    NativeWeaponKL::grenadeScope={};actionState={};NativeWeaponKL::installed=true;NativeWeaponKL::supportTypeProof=true;
    NativeWeaponKL::setCachedMuzzle=&CachedMuzzle;NativeWeaponKL::originalFireFF=&PumpFire;
    Put(base+0x577B74+0x2D4,ptr(0x11DF40));Put(base+0x577B74+0x290,ptr(0x11D230));
    unsigned char ownerCode[]={0xB8,0,0,0,0,0xC3};std::memcpy(ownerCode+1,&actor,4);
    std::memcpy(reinterpret_cast<void*>(base+0x11D230),ownerCode,sizeof(ownerCode));DWORD oldProtect=0;
    Check(VirtualProtect(reinterpret_cast<void*>(base+0x11D230),64,PAGE_EXECUTE_READWRITE,&oldProtect)!=FALSE,"private native-owner ABI stub executable");
    Put(model+0xA4,std::uint32_t(5));const auto marker=ptr(0xA0000),markerGeometry=ptr(0xA1000),entry=ptr(0xA2000);
    Put(model+0x150,marker);Put(marker+4,markerGeometry);Put(markerGeometry+0x60,marker);
    CameraPose localMuzzle;localMuzzle.basis={{0,0,1},{0,1,0},{1,0,0}};localMuzzle.position={50,2,0};Put(markerGeometry,localMuzzle);
    Put(root+0x50,std::uint32_t(0));Put(entry,ptr(5508832));Put(entry+0xF4,root);Put(base+0x7678A4,std::uint32_t(0));
    renderedHands={};renderedHands.player=actor;renderedHands.unitsPerMetre=100;renderedHands.pairId=55;renderedHands.recenterAck=recenter;
    renderedHands.active[1]=true;renderedHands.availableForMesh[0]=renderedHands.availableForMesh[1]=true;
    CameraPose raw;raw.basis={{0,0,1},{0,1,0},{1,0,0}};raw.position={100,150,400};
    const Quat pumpYaw{0,std::sin(.06f),0,std::cos(.06f)},controllerYaw{0,std::sin(.25f),0,std::cos(.25f)};
    CameraPose mesh=raw,final=raw;
    mesh.basis={Rotate(pumpYaw,raw.basis.forward),Rotate(pumpYaw,raw.basis.up),Rotate(pumpYaw,raw.basis.right)};
    final.basis={Rotate(controllerYaw,raw.basis.forward),Rotate(controllerYaw,raw.basis.up),Rotate(controllerYaw,raw.basis.right)};
    renderedHands.rawWrist[1]=raw;renderedHands.finalWrist[1]=final;renderedFixture=true;Put(entry+0x10,mesh);
    actionState.valid=true;actionState.player=actor;actionState.action=0;actionState.movementKind=NativeActionKL::Movement::Stand;
    CameraPose drawn;
    Check(NativeWeaponKL::MeshFromEntry(entry,reinterpret_cast<const CameraPose*>(entry+0x10),drawn),"actual owned pump GPU mesh path produces the tracked visible firearm");
    NativeWeaponKL::AimFrame pumpAim;
    Check(NativeWeaponKL::SnapshotAim(pumpAim)&&Dot(pumpAim.direction,drawn.basis.right)>.99999f&&Dot(pumpAim.direction,final.basis.right)>.99999f,
          "production pump removes native idle yaw from the actual visible barrel and shares its exact ray");
    NativeWeaponKL::AimFrame renderAim;
    Check(NativeWeaponKL::SnapshotRenderAim(renderAim)&&Equal(renderAim.origin,pumpAim.origin)&&Dot(renderAim.direction,drawn.basis.right)>.99999f,
          "overlay snapshot is attached to the exact gun bound in the current GPU image");
    NativeWeaponKL::AimFrame captureAim;
    Check(NativeWeaponKL::SnapshotCaptureAim(captureAim)&&Equal(captureAim.origin,pumpAim.origin)&&Dot(captureAim.direction,drawn.basis.right)>.99999f,
          "completed-image overlay reads the muzzle actually bound by the production firearm mesh");
    liveDrawAvailable=false;
    Check(!NativeWeaponKL::SnapshotRenderAim(renderAim),"HUD cleanup revokes the old live-mesh snapshot as in the real zero-aimReady run");
    auto beforeCaptureHands=handsReads;
    Check(NativeWeaponKL::SnapshotCaptureAim(captureAim)&&Equal(captureAim.origin,pumpAim.origin)&&handsReads==beforeCaptureHands,
          "completed-image laser survives HUD/body lease cleanup without reading or rebuilding a live hand pose");
    const auto savedCaptureTick=fixtureTick;fixtureTick+=401;
    Check(!NativeWeaponKL::SnapshotAim(renderAim)&&NativeWeaponKL::SnapshotCaptureAim(captureAim)&&captureAim.tick==fixtureTick,
          "slow image completion keeps its actual visible muzzle while simulation aim still expires independently");
    fixtureTick=savedCaptureTick;liveDrawAvailable=true;
    ++captureImageEpoch;
    Check(!NativeWeaponKL::SnapshotCaptureAim(captureAim),"a new native image cannot borrow the previous image's bound muzzle");--captureImageEpoch;
    ++captureImageEye;
    Check(!NativeWeaponKL::SnapshotCaptureAim(captureAim),"another eye cannot borrow the previous image's bound muzzle");--captureImageEye;
    captureImageValid=false;
    Check(!NativeWeaponKL::SnapshotCaptureAim(captureAim),"incomplete or retired image cannot publish a laser even if the old mesh snapshot remains");captureImageValid=true;
    ++recenter;
    Check(!NativeWeaponKL::SnapshotCaptureAim(captureAim),"recenter immediately revokes completed-image gun presentation");--recenter;
    captureReadCount=0;changeCaptureDuringRead=true;
    Check(!NativeWeaponKL::SnapshotCaptureAim(captureAim),"image change during native identity reads rejects the captured muzzle atomically");changeCaptureDuringRead=false;
    NativeWeaponKL::calibrationTransition=true;
    Check(!NativeWeaponKL::SnapshotCaptureAim(captureAim),"load/cutscene calibration transition revokes completed-image presentation");NativeWeaponKL::calibrationTransition=false;
    ++drawGeneration;
    Check(!NativeWeaponKL::SnapshotRenderAim(renderAim),"a new GPU generation cannot borrow the preceding image's visible muzzle");
    --drawGeneration;simulationVisualAvailable=false;
    Check(!NativeWeaponKL::SnapshotAim(pumpAim)&&NativeWeaponKL::SnapshotRenderAim(renderAim),
          "admitted rendering can finish without extending an expired simulation or Fire sample");
    simulationVisualAvailable=true;
    const auto savedRenderPair=renderedHands.pairId;++renderedHands.pairId;
    Check(!NativeWeaponKL::SnapshotCaptureAim(captureAim),"capture reader also rejects a changed physical eye pair");
    Check(!NativeWeaponKL::SnapshotRenderAim(renderAim),"another physical pair cannot reuse a previous rendered gun certificate");renderedHands.pairId=savedRenderPair;
    const auto savedRenderAck=renderedHands.recenterAck;++renderedHands.recenterAck;
    Check(!NativeWeaponKL::SnapshotRenderAim(renderAim),"recenter retires the current-image gun mapping");renderedHands.recenterAck=savedRenderAck;
    const auto savedRenderReference=*reinterpret_cast<std::uint32_t*>(weapon+0x58);Put(weapon+0x58,savedRenderReference+1);
    Check(!NativeWeaponKL::SnapshotCaptureAim(captureAim),"replacement of native weapon reference revokes completed-image presentation");
    Check(!NativeWeaponKL::SnapshotRenderAim(renderAim),"native weapon reference replacement retires render-only aim immediately");Put(weapon+0x58,savedRenderReference);
    Check(NativeWeaponKL::SnapshotRenderAim(renderAim)&&NativeWeaponKL::SnapshotAim(pumpAim),"unchanged current image restores render and independently fresh simulation readers");
    NativeAimKL::Endpoint previousContact;previousContact.valid=true;previousContact.pose=renderAim;previousContact.tick=fixtureTick;
    previousContact.point=Add(renderAim.origin,Scale(renderAim.direction,1000.f));
    const auto originalRenderWrist=renderedHands.finalWrist[1];const Quat rapidTurn{0,std::sin(.075f),0,std::cos(.075f)};
    renderedHands.finalWrist[1].basis={Rotate(rapidTurn,originalRenderWrist.basis.forward),Rotate(rapidTurn,originalRenderWrist.basis.up),Rotate(rapidTurn,originalRenderWrist.basis.right)};
    renderedHands.finalWrist[1].position.x+=10;++drawGeneration;
    Check(NativeWeaponKL::MeshFromEntry(entry,reinterpret_cast<const CameraPose*>(entry+0x10),drawn)&&NativeWeaponKL::SnapshotRenderAim(renderAim),
          "a rapid hand change reaches both the actual bound firearm and its same-image overlay snapshot");
    NativeLaserPoseKL::Beam currentBeam;
    liveDrawAvailable=false;
    Check(NativeWeaponKL::SnapshotCaptureAim(renderAim),"rapidly moved firearm remains available after its completed mesh lease ends");
    liveDrawAvailable=true;
    Check(NativeLaserPoseKL::Build(renderAim,&previousContact,fixtureTick,currentBeam)&&!currentBeam.surfaceContact&&
          currentBeam.distance>700.f&&currentBeam.distance<=800.f,
          "real moved-firearm rendering retains a visible rigid guide without inventing a surface dot");
    Vec3 rigidDirection;NativeAimKLMath::Unit(Subtract(currentBeam.end,renderAim.origin),rigidDirection);
    Check(Dot(rigidDirection,drawn.basis.right)>.99999f&&Dot(rigidDirection,renderAim.direction)>.999999f,
          "production mesh-to-muzzle-to-overlay beam remains collinear while the controller moves quickly");
    for(unsigned eye=0;eye<2;++eye){
        NativeAimRenderKL::Frame laser;laser.eye.basis.forward=renderAim.direction;laser.eye.basis.up={0,1,0};
        laser.eye.basis.right=Cross(laser.eye.basis.up,laser.eye.basis.forward);
        laser.eye.position=Add(Subtract(renderAim.origin,Scale(renderAim.direction,100.f)),{eye?3.f:-3.f,10.f,0});
        laser.origin=renderAim.origin;laser.contact=currentBeam.end;laser.contactValid=false;laser.units=renderAim.unitsPerMetre;
        const float fov[4]={1,1,1,1};
        Check(ApplyEyeProjection(laser.projection,fov,1.f,40000.f,true),"actual laser overlay eye projection is generated");
        NativeAimRenderKL::Geometry vertices;
        Check(NativeAimRenderKL::Build(laser,1920,1080,vertices)&&vertices.count==6,
              "actual renderer emits the rigid pending beam in both eyes without a false endpoint dot");
        NativeAimKL::Endpoint fresh;fresh.valid=true;fresh.pose=renderAim;fresh.tick=fixtureTick;
        fresh.point=Add(renderAim.origin,Scale(renderAim.direction,1000.f));NativeLaserPoseKL::Beam contactBeam;
        Check(NativeLaserPoseKL::Build(renderAim,&fresh,fixtureTick,contactBeam)&&contactBeam.surfaceContact&&
              std::fabs(contactBeam.distance-1000.f)<.001f,"a real aligned collision beyond8m retains its exact full distance");
        laser.contact=contactBeam.end;laser.contactValid=true;
        Check(NativeAimRenderKL::Build(laser,1920,1080,vertices)&&vertices.count==54,
              "actual renderer emits exactly one beam ending at the real hit and its contact dot in each eye");
    }
    renderedHands.finalWrist[1]=originalRenderWrist;++drawGeneration;
    Check(NativeWeaponKL::MeshFromEntry(entry,reinterpret_cast<const CameraPose*>(entry+0x10),drawn)&&NativeWeaponKL::SnapshotAim(pumpAim),
          "native firearm pose and physical aiming resume after the rapid-motion render fixture");
    // Eligibility is decided before the original request can retire the
    // dispatcher action, allocate Reload or touch native ammunition.
    Put(actor,ptr(0x56582C));Put(weapon+0xD8,7u^0xBEEBBEEBu);
    Put(base+0x577B74+0x308,ptr(0x11DDB0));Put(base+0x873680,std::uint32_t(0));
    NativeWeaponKL::originalReload=&Reload;NativeWeaponKL::SetTrackedReloadEnabled(true);NativeWeaponKL::SetTrackedReloadContext(true);
    const auto reloadRequest=[&](std::uint32_t caller,bool manual){NativeWeaponKL::DispatchReload(reinterpret_cast<void*>(actor),reinterpret_cast<void*>(weapon),caller,manual);};
    unsigned priorReloads=nativeReloads;
    for(auto caller:{0x1F5C12u,0x1F5C42u,0x1F6136u})reloadRequest(caller,false);
    Check(nativeReloads==priorReloads&&NativeWeaponKL::reloadBlocked==3&&
          *reinterpret_cast<const std::uint32_t*>(weapon+0xD8)==(7u^0xBEEBBEEBu),
          "actual request adapter suppresses all verified autonomous nonempty reload callers without invoking native action or editing ammo");
    reloadRequest(0x1F6136,true);reloadRequest(0x12B335,false);reloadRequest(0x38270F,false);
    Check(nativeReloads==priorReloads+3,"native R, native message and network callers each invoke the original request exactly once");
    Put(weapon+0xD8,0u^0xBEEBBEEBu);reloadRequest(0x1F6136,false);
    Check(nativeReloads==priorReloads+3&&NativeWeaponKL::reloadBlocked==4,"the local player must press reload even with an empty magazine");
    Put(weapon+0xD8,7u^0xBEEBBEEBu);NativeWeaponKL::SetTrackedReloadContext(false);reloadRequest(0x1F6136,false);
    Check(nativeReloads==priorReloads+4,"gameplay context retirement immediately restores native reload eligibility");NativeWeaponKL::SetTrackedReloadContext(true);
    Put(actor+0x358+0x64,std::uint8_t(0));reloadRequest(0x1F6136,false);
    Check(nativeReloads==priorReloads+4,"the local player's inactive hand during a swap cannot bypass manual reload");
    Put(actor+0x358+0x64,std::uint8_t(1));fixtureTick=GetTickCount();manualReloadTick=fixtureTick-4;
    reloadRequest(0x1F6136,false);reloadRequest(0x1F6136,false);
    Check(nativeReloads==priorReloads+4,"pending A never authorizes an unrelated autonomous reload");
    actionState.valid=true;actionState.player=actor;actionState.action=0;actionState.movementKind=NativeActionKL::Movement::Stand;
    NativeWeaponKL::reloadInstalled=true;NativeWeaponKL::ReloadSimulationTick(actor);NativeWeaponKL::ReloadSimulationTick(actor);
    Check(nativeReloads==priorReloads+5,"short A dispatches the verified native request exactly once in simulation");
    manualReloadTick=fixtureTick-3;Put(weapon+0xD8,0u^0xBEEBBEEBu);
    reloadRequest(0x1F6136,false);NativeWeaponKL::ReloadSimulationTick(actor);
    Check(nativeReloads==priorReloads+6,"an automatic empty request leaves the pending A for simulation");
    manualReloadTick=fixtureTick-2;Put(weapon+0xD8,7u^0xBEEBBEEBu);NativeWeaponKL::SetTrackedReloadContext(false);
    NativeWeaponKL::ReloadSimulationTick(actor);NativeWeaponKL::SetTrackedReloadContext(true);NativeWeaponKL::ReloadSimulationTick(actor);
    Check(nativeReloads==priorReloads+6,"an A intent observed outside gameplay is retired rather than deferred");
    manualReloadTick=fixtureTick-1;actionState.action=1;NativeWeaponKL::ReloadSimulationTick(actor);actionState.action=0;NativeWeaponKL::ReloadSimulationTick(actor);
    Check(nativeReloads==priorReloads+6,"an A intent observed during a native action is not replayed afterwards");
    manualReloadTick=fixtureTick;Put(weapon+0xD8,0u^0xBEEBBEEBu);NativeWeaponKL::ReloadSimulationTick(actor);reloadRequest(0x1F6136,false);
    Check(nativeReloads==priorReloads+7,"an automatic empty request cannot duplicate a completed A dispatch");
    manualReloadTick=0;reloadRequest(0x1F6136,false);
    Check(nativeReloads==priorReloads+7,"the empty weapon stays empty with no reload input");
    Put(weapon+0xD8,7u^0xBEEBBEEBu);
    manualReloadTick=0;Put(actor+0x358+0x64,std::uint8_t(0));
    actionState.valid=true;actionState.player=actor;actionState.action=1;actionState.actionKind=NativeActionKL::Action::Reload;
    actionState.movementKind=NativeActionKL::Movement::Stand;
    ++renderedHands.pairId;NativeWeaponKL::heldCache={};Put(entry+0x10,mesh);
    CameraPose reloadPose;NativeWeaponMath::Remap(renderedHands.rawWrist[1],renderedHands.finalWrist[1],mesh,reloadPose);
    Check(NativeWeaponKL::MeshFromEntry(entry,reinterpret_cast<const CameraPose*>(entry+0x10),drawn)&&
          Equal(drawn.position,reloadPose.position)&&!NativeWeaponKL::SnapshotAim(pumpAim),
          "manual reload carries the native animated weapon and does not publish a fabricated controller ray");
    Check(!NativeWeaponKL::HeldBy(actor,weapon,1),"visual-only reload exception never grants native Fire an active hand");
    actionState.actionKind=NativeActionKL::Action::Throw;
    Check(!NativeWeaponKL::SnapshotAim(pumpAim)&&!NativeWeaponKL::HeldForVisual(actor,weapon,1),"another native action cannot inherit inactive-reload visual ownership");
    Put(actor+0x358+0x64,std::uint8_t(1));actionState={};NativeWeaponKL::SetTrackedReloadContext(false);
    renderedHands.pairId=55;NativeWeaponKL::heldCache={};Put(entry+0x10,mesh);
    Check(NativeWeaponKL::MeshFromEntry(entry,reinterpret_cast<const CameraPose*>(entry+0x10),drawn)&&NativeWeaponKL::SnapshotAim(pumpAim),
          "original accepted canonical firearm direction resumes unchanged after reload-specific test context");
    for(auto kind:{NativeActionKL::Action::PutBack,NativeActionKL::Action::GetBack,NativeActionKL::Action::Command}) {
        actionState.valid=true;actionState.player=actor;actionState.action=1;actionState.actionKind=kind;
        actionState.movementKind=NativeActionKL::Movement::Stand;
        Check(NativeWeaponKL::SnapshotAim(pumpAim)&&Dot(pumpAim.direction,final.basis.right)>.99999f,
              "actual held firearm snapshot preserves accepted controller direction through exact weapon/command gesture");
        Put(actor+0x358+0x64,std::uint8_t(0));
        Check(!NativeWeaponKL::HeldForVisual(actor,weapon,1)&&!NativeWeaponKL::SnapshotAim(pumpAim),
              "weapon switching deactivating the native hand still retires gun visuals; gesture grants no inactive held-gun ownership");
        Put(actor+0x358+0x64,std::uint8_t(1));
    }
    actionState={};
    actionState.valid=true;actionState.player=actor;actionState.movementKind=NativeActionKL::Movement::Vehicle;
    Check(NativeWeaponKL::SnapshotAim(pumpAim)&&Dot(pumpAim.direction,final.basis.right)>.99999f,
          "exact seated passenger without an authored action retains the accepted visible-firearm canonical ray");
    actionState.movementKind=NativeActionKL::Movement::Mounted;
    Check(!NativeWeaponKL::SnapshotAim(pumpAim),"mounted native gun never acquires an ordinary handheld firearm aim override");
    actionState.movementKind=NativeActionKL::Movement::Vehicle;actionState.action=1;actionState.actionKind=NativeActionKL::Action::Throw;
    Check(!NativeWeaponKL::SnapshotAim(pumpAim),"passenger authored throw/reload action still owns the complete native arm rig");actionState={};
    actionState.valid=true;actionState.player=actor;actionState.movementKind=NativeActionKL::Movement::Stand;
    Put(entry+0x10,raw);
    Check(NativeWeaponKL::MeshFromEntry(entry,reinterpret_cast<const CameraPose*>(entry+0x10),drawn)&&
          Dot(pumpAim.direction,drawn.basis.right)>.99999f,
          "native idle-to-fire animation between eyes cannot turn the cached same-pair barrel away from its ray");
    renderedHands.pairId=56;Put(entry+0x10,mesh);const auto rigidSource=ptr(0xA3000);Put(rigidSource,mesh);
    Check(NativeWeaponKL::MeshFromEntry(entry,reinterpret_cast<const CameraPose*>(rigidSource),drawn)&&
          NativeWeaponKL::SnapshotAim(pumpAim)&&Dot(pumpAim.direction,drawn.basis.right)>.99999f,
          "rigid pump normalizes its actual derived draw pose, keeping barrel and ray identical");
    const auto acceptedAxes=NativeWeaponKL::barrelAxisAccepted.load();renderedHands.pairId=57;Put(model+0xA4,std::uint32_t(4));
    Check(NativeWeaponKL::MeshFromEntry(entry,reinterpret_cast<const CameraPose*>(rigidSource),drawn)&&NativeWeaponKL::SnapshotAim(pumpAim)&&
          Dot(pumpAim.direction,final.basis.right)>.99999f&&Dot(pumpAim.direction,drawn.basis.right)>.99999f&&NativeWeaponKL::barrelAxisAccepted>acceptedAxes,
          "ordinary rifle removes the same native idle animation offset while retaining its physical pointing ray");
    renderedHands.pairId=58;Put(model+0xA4,std::uint32_t(5));auto unproved=mesh;
    const Quat largeYaw{0,std::sin(.5f),0,std::cos(.5f)};
    unproved.basis={Rotate(largeYaw,raw.basis.forward),Rotate(largeYaw,raw.basis.up),Rotate(largeYaw,raw.basis.right)};Put(rigidSource,unproved);Put(entry+0x10,unproved);
    Check(NativeWeaponKL::MeshFromEntry(entry,reinterpret_cast<const CameraPose*>(rigidSource),drawn)&&NativeWeaponKL::SnapshotAim(pumpAim)&&
          Dot(pumpAim.direction,final.basis.right)>.99999f&&Dot(pumpAim.direction,drawn.basis.right)>.99999f&&NativeWeaponKL::barrelAxisRejected==0,
          "large idle arm animation no longer turns a proven model barrel sideways from the controller ray");
    renderedHands.pairId=59;Put(rigidSource,mesh);Put(entry+0x10,mesh);
    Check(NativeWeaponKL::MeshFromEntry(entry,reinterpret_cast<const CameraPose*>(rigidSource),drawn)&&NativeWeaponKL::SnapshotAim(pumpAim),"proved pump draw resumes after conservative axis rejection");
    NativeWeaponKL::ShotContext rejectedShot;Put(weapon+0x58,std::uint32_t(38));
    Check(!NativeWeaponKL::PrepareShot(weapon,rejectedShot),"weapon generation change cannot fire from an old visible pump certificate");Put(weapon+0x58,std::uint32_t(37));
    Put(weapon+0x134,Vec3{1,2,3});Put(weapon+0xC4,savedTarget);
    NativeWeaponKL::ScopedFire(reinterpret_cast<void*>(weapon),&PumpFire);
    Check(tutorialShots==priorTutorialShots+1,"normal tracked firearm emits the same single post-native-shot tutorial credit");
    Vec3 fired;NativeArmMath::Unit(Subtract(capturedTarget,capturedOrigin),fired);
    Check(Dot(fired,pumpAim.direction)>.99999f&&Equal(capturedOrigin,pumpAim.origin),"actual tracked pump Fire keeps physical muzzle and corrected visible-barrel direction");
    Check(Equal(*reinterpret_cast<const Vec3*>(weapon+0xC4),savedTarget)&&Equal(*reinterpret_cast<const Vec3*>(weapon+0x134),{}),"pump native target and one-shot origin consumption restore normally");
    NativeHandsKL::ControllerAim visibleThrowAim;
    Check(NativeWeaponKL::CurrentThrowAim(actor,visibleThrowAim)&&Dot(visibleThrowAim.direction,pumpAim.direction)>.99999f,
          "grenade source prefers the current visible firearm/reticle ray over a different bare controller rotation");
    // Historical read-only native capture barrel-axis-14616: the DRAWN
    // model is +X, but its child matrix carries that barrel to ITEM +Z.
    // The actual pistol marker points -Z; its physical muzzle is +Z from
    // the real grip. The .34 synthetic +X root fixture missed this distinction.
    localMuzzle.basis={{1,4.37114e-8f,0},{-4.37114e-8f,1,2.23305e-8f},{0,2.23305e-8f,-1}};
    localMuzzle.position={-.0526923f,13.281f,17.2654f};Put(markerGeometry,localMuzzle);
    CameraPose child;child.basis={{-.999717f,.0236923f,-.00193811f},{.0236861f,.999714f,.00319936f},{-.00201336f,-.00315255f,.999993f}};
    child.position={.661154f,-7.48558f,-5.15315f};
    const auto childGeometry=ptr(0xA4000),childOwner=ptr(0xA5000);Put(childGeometry,child);
    Put(childGeometry+0x50,root);Put(childGeometry+0x60,childOwner);Put(entry+0xF4,childGeometry);
    CameraPose nativeItem=raw;renderedHands.rawWrist[1]=raw;
    renderedHands.rawWrist[1].basis={Scale(nativeItem.basis.right,-1),nativeItem.basis.up,nativeItem.basis.forward};
    renderedHands.rawWrist[1].position=NativeRigMath::ToWorld(nativeItem,{raw.basis,{2.06871f,5.49869f,-12.37699f}}).position;
    const auto modelWorld=NativeRigMath::ToWorld(nativeItem,{child.basis,child.position});Put(entry+0x10,modelWorld);
    Put(model+0xA4,std::uint32_t(0));++renderedHands.pairId;
    Check(NativeWeaponKL::MeshFromEntry(entry,reinterpret_cast<const CameraPose*>(entry+0x10),drawn)&&NativeWeaponKL::SnapshotAim(pumpAim)&&
          Dot(pumpAim.direction,final.basis.right)>.99999f&&Dot(pumpAim.direction,drawn.basis.right)>.9999f,
          "actual native item+Z and child mesh+X both align with the physical controller instead of the .34 ninety-degree ray");
    const auto markerWorld=NativeRigMath::ToWorld(NativeWeaponKL::renderedGun[1].attachment,{localMuzzle.basis,localMuzzle.position});
    Check(Dot(Scale(markerWorld.basis.right,-1),pumpAim.direction)>.99999f&&Equal(markerWorld.position,pumpAim.origin),
          "reversed native pistol marker resolves its sign without changing the actual muzzle offset");
    Check(NativeWeaponKL::CurrentThrowAim(actor,visibleThrowAim)&&Dot(visibleThrowAim.direction,final.basis.right)>.99999f,
          "current grenade barrel preference cannot inherit the old item-X ninety-degree error");
    // A loading/render hitch must expire a shot, not the already-proved
    // geometry of this exact held pistol. Reproduce a native idle pose in
    // which the current wrist->muzzle displacement cannot prove its axis.
    const auto acceptedPistol=drawn;const auto acceptedPistolOrigin=pumpAim.origin;
    auto idleItem=nativeItem;
    idleItem.position=Subtract(renderedHands.rawWrist[1].position,
        NativeRigMath::Transform(idleItem.basis,{30,localMuzzle.position.y,localMuzzle.position.z}));
    CameraPose unprovedPistol;unsigned idleStage=0;
    Check(!NativeWeaponMath::HeldAttachment(renderedHands.rawWrist[1],renderedHands.finalWrist[1],idleItem,
          localMuzzle,1,100,unprovedPistol,&idleStage)&&idleStage==4,
          "actual pistol fixture reproduces unprovable native idle grip rather than merely a different yaw");
    fixtureTick+=500;renderedHands.tick=fixtureTick;++renderedHands.pairId;
    Check(!NativeWeaponKL::SnapshotAim(pumpAim),"a render hitch still expires the physical shot certificate");
    const auto idleModel=NativeRigMath::ToWorld(idleItem,{child.basis,child.position});Put(entry+0x10,idleModel);
    Check(NativeWeaponKL::MeshFromEntry(entry,reinterpret_cast<const CameraPose*>(entry+0x10),drawn)&&
          NativeWeaponKL::SnapshotAim(pumpAim)&&Equal(pumpAim.origin,acceptedPistolOrigin)&&
          Dot(drawn.basis.right,acceptedPistol.basis.right)>.9999f&&
          Dot(pumpAim.direction,drawn.basis.right)>.9999f,
          "fresh draw reuses exact held pistol geometry after 500 ms without recapturing sideways native idle pose");
    Put(entry+0x10,modelWorld);
    localMuzzle.basis.right={0,0,1};localMuzzle.basis.forward={-1,0,0};Put(markerGeometry,localMuzzle);++renderedHands.pairId;
    Check(NativeWeaponKL::MeshFromEntry(entry,reinterpret_cast<const CameraPose*>(entry+0x10),drawn)&&NativeWeaponKL::SnapshotAim(pumpAim)&&
          Dot(pumpAim.direction,final.basis.right)>.99999f&&Dot(pumpAim.direction,drawn.basis.right)>.9999f,
          "positive native shotgun marker uses the same physical barrel without a per-weapon yaw offset");
    localMuzzle.basis={{0,0,1},{0,1,0},{1,0,0}};Put(markerGeometry,localMuzzle);++renderedHands.pairId;
    Check(NativeWeaponKL::MeshFromEntry(entry,reinterpret_cast<const CameraPose*>(entry+0x10),drawn)&&NativeWeaponKL::SnapshotAim(pumpAim)&&
          Dot(pumpAim.direction,final.basis.right)>.99999f,
          "unproved marker axis retains the pre-regression physical pointing ray instead of substituting item-X");
    NativeWeaponKL::HeldSupport support;Put(model+0xA4,std::uint32_t(5));hand.enabled=0;Put(actor+0x358+0x60,hand);
    Check(!NativeWeaponKL::CaptureHeldSupport(actor,100,support)&&NativeWeaponKL::CaptureHeldSupport(actor,100,support,true)&&!support.active,
          "only explicit reload suspension accepts the same inactive native hand without inventing a held item");
    hand.item=weapon+4;Put(actor+0x358+0x60,hand);
    Check(!NativeWeaponKL::CaptureHeldSupport(actor,100,support,true),"reload suspension still rejects replacement or invalid owned weapon");
    // The live .33 log never called ZFFActionThrow. Exercise the actual owned
    // FF grenade Fire adapter without that action and without IKHAND holding
    // the grenade; the pinned native owner getter still proves the player.
    NativeWeaponKL::grenadeScope={};NativeWeaponKL::originalFireFF=&NativeGrenade;NativeWeaponKL::scopeGestureContext=true;
    Put(model+0xA4,std::uint32_t(11));Put(aim+0x34,Vec3{0,0,1});
    expectedOrigin={140,150,510};const Vec3 charged=Add(expectedOrigin,{10,25,400});
    Put(weapon+0x134,expectedOrigin);Put(weapon+0xC4,charged);
    NativeWeaponKL::ScopedFire(reinterpret_cast<void*>(weapon),&NativeGrenade);
    Check(Equal(capturedOrigin,expectedOrigin)&&Equal(capturedTarget,Add(expectedOrigin,{std::hypot(400.f,10.f),25,0})),
          "actual FF grenade keeps origin/charge/loft and follows the controller barrel ray even while HMD looks away");
    Check(Equal(*reinterpret_cast<Vec3*>(weapon+0xC4),charged)&&Equal(*reinterpret_cast<Vec3*>(weapon+0x134),{}),
          "grenade target restores and consumed native origin remains consumed");
    Check(NativeWeaponKL::Snapshot().grenadeFireApplied==1&&NativeWeaponKL::Snapshot().grenadeUpdates==0,
          "owned grenade Fire works independently of the unexecuted legacy Throw action hook");
    NativeWeaponKL::SetTargetHook(reinterpret_cast<void*>(weapon),nullptr,&charged);
    Check(Equal(*reinterpret_cast<Vec3*>(weapon+0xC4),charged),"ordinary target updates retain grenade charge instead of firearm100m aiming");
    ready=false;Put(weapon+0x134,expectedOrigin);
    NativeWeaponKL::ScopedFire(reinterpret_cast<void*>(weapon),&NativeGrenade);ready=true;
    Check(Equal(capturedTarget,charged)&&NativeWeaponKL::Snapshot().grenadeFireRejected==1,
          "unavailable controller proof runs exactly the original grenade path without falling through firearm targeting");
    // Native mode13 (the retained .35 run) does not choose the original
    // camera overlay in630070. Exercise the actual production adapter with
    // the authored geometry, keeping the accepted firearm/throw basis above.
    Put(model+0xA4,std::uint32_t(15));hand.item=weapon;hand.enabled=1;Put(actor+0x358+0x60,hand);
    Put(aim+0x68,std::uint32_t(13));Put(aim+0x9C,std::uint8_t(1));Put(entry+0xF4,root);
    const auto logic=ptr(0xB0000),overlay=ptr(0xB1000),shape=ptr(0xB2000),shapeGeometry=ptr(0xB3000),shapeName=ptr(0xB4000);
    Put(actor+0x9B0,logic);Put(aim+0xC,logic);Put(logic,ptr(0x56AB44));Put(logic+0x490,actor);Put(logic+0x24,camera);
    Put(logic+0x678,overlay);Put(logic+0x674,std::uint8_t(0));Put(overlay,ptr(0x56AF04));Put(overlay+0x220,std::uint32_t(0));
    Put(overlay+0x1C4,shape);Put(shape+4,shapeGeometry);Put(shapeGeometry+0x60,shape);Put(shapeGeometry+0x6C,shapeName);
    std::memcpy(reinterpret_cast<void*>(shapeName),"Scope_Dragunov",sizeof("Scope_Dragunov"));
    NativeWeaponKL::activateScopeOverlay=&ActivateOverlay;NativeWeaponKL::originalOverlayClear=&ClearOverlay;NativeWeaponKL::scopeOverlayVerified=true;
    NativeWeaponKL::scopeGestureEnabled=true;NativeWeaponKL::scopeGestureContext=true;
    NativeWeaponKL::ScopeGestureOwner request{NativeWeaponKL::ReadScope(),static_cast<std::uint32_t>(recenter),GetTickCount(),NativeWeaponKL::scopeGestureEpoch.load(),true};
    NativeWeaponKL::ScopeOverlayTick(request,true);
    Check(overlayActivates==1&&*reinterpret_cast<std::uint32_t*>(overlay+0x220)==1&&*reinterpret_cast<std::uint32_t*>(overlay+0x1B0)==shape,
          "actual native mode13 near-face request activates existing Dragunov camera geometry instead of a synthetic tint");
    const auto crosshair=ptr(0xB6000);Put(crosshair,ptr(0x573F2C));Put(crosshair+0x10,actor);
    NativeGameplayHudKL::image=base;NativeGameplayHudKL::originals[0]=&Crosshair;
    NativeGameplayHudKL::DrawHook<0>(reinterpret_cast<void*>(crosshair),nullptr,nullptr,nullptr);
    Check(!ordinaryCrosshairs&&NativeGameplayHudKL::Snapshot().hidden[0]==1,
          "actual owned ordinary CrossHair producer cannot draw a second sprite over the original camera optic");
    opticView=false;NativeGameplayHudKL::DrawHook<0>(reinterpret_cast<void*>(crosshair),nullptr,nullptr,nullptr);
    Check(ordinaryCrosshairs==1,"ordinary crosshair passes unchanged outside an applied optic frame");opticView=true;
    Put(crosshair+0x10,actor+4);NativeGameplayHudKL::DrawHook<0>(reinterpret_cast<void*>(crosshair),nullptr,nullptr,nullptr);
    Check(ordinaryCrosshairs==2,"other owner CrossHair producer remains native even during local optics");Put(crosshair+0x10,actor);
    NativeWeaponKL::OverlayClearHook(reinterpret_cast<void*>(logic),nullptr);
    Check(overlayClears==0&&NativeWeaponKL::Snapshot().scopeOverlayRetained==1,
          "native default-mode update cannot immediately clear this exact fresh scoped overlay");
    NativeWeaponKL::ScopeOverlayTick(request,true);
    Check(overlayActivates==1,"same active original overlay is never activated twice");
    NativeWeaponKL::ScopeOverlayTick(request,false);
    Check(overlayClears==1&&*reinterpret_cast<std::uint32_t*>(overlay+0x220)==0,
          "lowering the right controller clears only the original overlay opened by this request");
    NativeWeaponKL::ScopeOverlayTick(request,true);NativeWeaponKL::scopeGestureContext=false;
    NativeWeaponKL::OverlayClearHook(reinterpret_cast<void*>(logic),nullptr);
    Check(overlayClears==2,"explicit UI/context retirement permits original native overlay cleanup immediately");
    NativeWeaponKL::scopeGestureContext=true;NativeWeaponKL::ScopeOverlayTick(request,false);
    Put(logic+0x674,std::uint8_t(1));Put(overlay+0x220,std::uint32_t(4));
    NativeWeaponKL::ScopeOverlayTick(request,true);
    Check(overlayActivates==2&&!NativeWeaponKL::scopeOverlayOwner.ours,
          "another authored native overlay is neither overwritten nor claimed by the sniper adapter");
    NativeWeaponKL::OverlayClearHook(reinterpret_cast<void*>(logic),nullptr);
    Check(overlayClears==3,"unrelated native overlay cleanup executes its original function exactly once");
    Put(logic+0x490,actor+4);NativeWeaponKL::ScopeOverlayTick(request,true);
    Check(overlayActivates==2&&NativeWeaponKL::Snapshot().scopeOverlayProofStage==2,
          "foreign logic-camera owner cannot activate another player's green optic");Put(logic+0x490,actor);
    Put(shapeName,char('X'));NativeWeaponKL::ScopeOverlayTick(request,true);
    Check(overlayActivates==2&&NativeWeaponKL::Snapshot().scopeOverlayProofStage==4,
          "a substituted camera overlay resource cannot impersonate the authored Dragunov scope");Put(shapeName,char('S'));
    NativeWeaponKL::ScopeOverlayTick(request,true);NativeWeaponKL::InvalidateScopeGesture();
    NativeWeaponKL::OverlayClearHook(reinterpret_cast<void*>(logic),nullptr);
    Check(overlayClears==4,"recenter/epoch retirement never blocks the original camera clear");
    NativeWeaponKL::ScopeOverlayTick(request,false);
    // Late draw suppression tags the exact active right-hand rifle entry,
    // not the world, body, original overlay geometry or other inventory.
    const auto scopedState=ptr(0xB5000);vrDraw=true;Put(entry+0xF4,root);Put(root+0x50,std::uint32_t(0));Put(entry+0x10,raw);
    Put(scopedState+0x8C,raw);Put(aim+0x9C,std::uint8_t(1));
    NativeWeaponKL::originalWorld=&ScopeWorld;NativeWeaponKL::worldBindObserver=nullptr;
    const auto scopeBind=[&]{NativeWeaponKL::WorldBindHook(reinterpret_cast<void*>(scopedState),nullptr,reinterpret_cast<const CameraPose*>(entry+0x10));};
    NativeWeaponKL::SetScopedDrawFrame(actor,renderedHands.pairId,true);
    scopeBind();
    Check(NativeWeaponKL::SuppressScopedDraw()&&NativeWeaponKL::SuppressScopedDraw(),
          "actual exact owned sniper WORLD certificate suppresses every native material pass during its optic view");
    auto moved=*reinterpret_cast<const CameraPose*>(scopedState+0x8C);moved.position.x+=1;Put(scopedState+0x8C,moved);
    Check(!NativeWeaponKL::SuppressScopedDraw(),"changed WORLD cannot inherit the hidden rifle tag");
    scopeBind();++drawGeneration;
    Check(!NativeWeaponKL::SuppressScopedDraw(),"another draw generation cannot hide geometry using an old scoped certificate");
    NativeWeaponKL::SetScopedDrawFrame(actor,renderedHands.pairId,true);
    scopeBind();
    Put(weapon+0x58,std::uint32_t(38));Check(!NativeWeaponKL::SuppressScopedDraw(),"weapon reference replacement invalidates scoped-rifle hiding");Put(weapon+0x58,std::uint32_t(37));
    NativeWeaponKL::SetScopedDrawFrame(actor,renderedHands.pairId,true);
    scopeBind();
    firstPersonDraw=false;Check(!NativeWeaponKL::SuppressScopedDraw(),"native non-first-person VIEW never hides the rifle");firstPersonDraw=true;
    NativeWeaponKL::SetScopedDrawFrame(actor,renderedHands.pairId,true);Put(entry+0xF4,shapeGeometry);Put(shapeGeometry+0x50,std::uint32_t(0));
    scopeBind();
    Check(!NativeWeaponKL::SuppressScopedDraw(),"original green optic geometry and unrelated objects remain visible");Put(entry+0xF4,root);
    NativeWeaponKL::SetScopedDrawFrame(actor,renderedHands.pairId,true);
    scopeBind();NativeWeaponKL::ClearScopedDrawFrame();
    Check(!NativeWeaponKL::SuppressScopedDraw(),"BeforeCapture/reset retirement clears all scoped-draw authorization");
    NativeWeaponKL::SetScopedDrawFrame(actor,renderedHands.pairId,true);
    scopeBind();Put(aim+0x9C,std::uint8_t(0));
    Check(!NativeWeaponKL::SuppressScopedDraw(),"lowering the sniper immediately restores its render passes");
    VirtualFree(allocation,0,MEM_RELEASE);
    std::printf("Production scoped Fire/grenade target/world observer: %u checks, %u failures; private native callbacks only.\n",checks,failures);
    return failures?1:0;
}
