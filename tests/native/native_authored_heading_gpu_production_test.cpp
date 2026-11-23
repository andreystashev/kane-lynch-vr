// Actual authenticated camera consumer + current native actor-root source.
// Private memory only; no hooks are installed and no game/VR process opens.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
namespace CinematicReadFixture {
inline unsigned reads=0;
inline BOOL WINAPI Read(HANDLE p,LPCVOID at,LPVOID out,SIZE_T count,SIZE_T* got){
    ++reads;return ::ReadProcessMemory(p,at,out,count,got);
}
}
namespace AuthoredGpuFixture {inline void* caller=nullptr;}
#define __builtin_return_address(level) AuthoredGpuFixture::caller
#define ReadProcessMemory CinematicReadFixture::Read
#include "native_camera_kl.cpp"
#include "native_player_anchor.cpp"
#include "native_skin_math.hpp"
#undef ReadProcessMemory
#undef __builtin_return_address
__attribute__((used)) static void** const keepProjectionTailStorage=&KL1ProjectionTailOriginal;
#include <cstdio>
#include <cstdlib>
namespace {
using namespace NativeCameraMath;
unsigned checks=0,appliedCount=0,handSetCalls=0;
bool handsCaptureReady=true;
bool observedRenderContext=false;std::uint64_t observedRenderEpoch=0;unsigned observedRenderEye=99;
NativeActionKL::State currentAction;
NativeCameraKL::Frame requested;
CameraPose observed;
std::uintptr_t actorRootAddress=0;
bool deriveOwner=true;
void Check(bool ok,const char* why){++checks;if(!ok){std::printf("FAIL %s\n",why);std::exit(1);}}
[[noreturn]]void Unexpected(){std::puts("FAIL unrelated native/runtime entry");std::abort();}
template<class T>void Put(std::uintptr_t at,const T& value){std::memcpy(reinterpret_cast<void*>(at),&value,sizeof(value));}
void U32(std::uintptr_t at,std::uint32_t value){Put(at,value);}
bool Near(Vec3 a,Vec3 b){return Dot(Subtract(a,b),Subtract(a,b))<.0001f;}
CameraPose Pose(float yaw,Vec3 p={}){return {{{std::sin(yaw),0,std::cos(yaw)},{0,1,0},{std::cos(yaw),0,-std::sin(yaw)}},p};}
bool Provide(NativeCameraKL::Frame& out,void*){
    const auto source=out.sourceCamera;out=requested;
    if(deriveOwner){
        out.authoredHeading=NativeCinematicKL::AuthoredActorHeading(NativeCameraKL::CinematicState(),currentAction,out.anchorPlayer);
        out.authoredHeadingAction=currentAction.action;CameraPose actor;
        if(NativeCameraKL::Read(actorRootAddress,actor))out.authoredActorForward=actor.basis.forward;
    }
    return out.sourceCamera==source;
}
void Applied(const NativeCameraKL::Frame&,const CameraPose& eye,const float*,void*){observed=eye;++appliedCount;}
void __thiscall NativeSetter(void*,const float*){}
}
namespace NativeActionKL {State Current(){return currentAction;}bool InitializeVerifiedImage(std::uintptr_t){Unexpected();}}
namespace NativeCoverKL {
bool InitializeVerifiedImage(std::uintptr_t){Unexpected();}
bool InputBasis(std::uintptr_t,NativeCameraMath::Basis& out){out=Pose(0).basis;return true;}
}
namespace NativeHandsKL {
bool RenderPairCaptureReady(){return handsCaptureReady;}
void RetireRenderPairImage(){handsCaptureReady=true;}
void ClearFrame(){}
void SetFrame(const CameraPose&,const float*,const float*,const float*,float,std::uint64_t,bool,std::uint32_t,bool){
    ++handSetCalls;observedRenderEpoch=0;observedRenderEye=99;
    observedRenderContext=NativeCameraKL::ReadRenderContext(observedRenderEpoch,observedRenderEye);
}
}
namespace NativeBodyKL {void ClearFrame(){}void SetFrame(const CameraPose&,std::uint64_t,float,bool,std::uint32_t){}}
namespace VRBridge {bool Ready(){return true;}Tracking HeadPose(){Unexpected();}}
namespace D3D9Capture {void ObserveNativeCamera(){}}
extern "C" {
MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*){Unexpected();}MH_STATUS WINAPI MH_QueueEnableHook(LPVOID){Unexpected();}
MH_STATUS WINAPI MH_ApplyQueued(){Unexpected();}MH_STATUS WINAPI MH_DisableHook(LPVOID){Unexpected();}MH_STATUS WINAPI MH_RemoveHook(LPVOID){Unexpected();}
}
int main(){
    using namespace NativeCameraKL;constexpr std::uintptr_t base=0x30000000;constexpr float rad=.01745329252f;
    auto* mem=VirtualAlloc(reinterpret_cast<void*>(base),0x900000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    Check(mem==reinterpret_cast<void*>(base),"private native actor/camera image allocation");image=base;installed=true;
    constexpr std::uintptr_t data=base+0x10000,player=base+0x20000,camera=base+0x30000,system=base+0x40000,
        manager=base+0x41000,packet=base+0x50000,queue=base+0x51000,entry=base+0x60000,
        geometry=base+0x61000,actorGeometry=base+0x62000,state=base+0x70000,dispatcher=base+0x80000,
        movement=base+0x81000,action=base+0x82000;
    U32(base+0x68760C,data);U32(data+0xA2C,player);U32(player+4,actorGeometry);U32(player+0x9A0,camera);
    U32(player+0x194,dispatcher);U32(dispatcher+4,0);U32(player+0x67C,movement);U32(movement,base+0x566AEC);U32(action,base+0x5667F4);
    U32(base+0x6875F0,system);U32(system+0x96C,manager);U32(manager+0x300,0);U32(base+0x88E8B0,0);
    U32(camera,base+kCameraVtable);U32(entry,base+kCameraEntryVtable);U32(entry+0xF4,geometry);U32(geometry+0x60,camera);
    U32(packet,1);U32(packet+4,entry);U32(packet+8,queue);U32(packet+0xC,1);U32(packet+0x14,0x26);U32(queue+0x14,packet);U32(queue+0xC,packet+0xB0);
    U32(base+0x7678A4,0);actorRootAddress=actorGeometry;Put(actorGeometry,Pose(40*rad,{1,2,3}));
    const auto native=Pose(0,{9000,8000,7000});CameraPose nativeView;
    Check(NativeCameraKLMath::ViewFromWorld(native,nativeView),"native inverse camera packet fixture");Put(packet+0x18,nativeView);Put(entry+0x10,native);
    float projection[16]{};const float tangents[]={1,1,1,1};Check(ApplyEyeProjection(projection,tangents,1,40000,false),"native projection fixture");
    std::memcpy(reinterpret_cast<void*>(packet+0x48),projection,sizeof(projection));
    currentAction.valid=true;currentAction.player=player;currentAction.movement=movement;currentAction.movementKind=NativeActionKL::Movement::BaseMove;
    requested.valid=requested.anchorValid=requested.stableFirstPersonYaw=requested.animatedEye=true;
    requested.sourceCamera=requested.anchorCamera=camera;requested.anchorPlayer=player;requested.anchorPosition[1]=170;
    requested.nearUnits=1;requested.pairId=17;requested.eye=0;requested.recenterAck=7;requested.worldUnitsPerMetre=100;
    for(unsigned i=0;i<4;++i)requested.fov[i]=requested.cullingFov[i]=1;
    const Quat physical{0,std::sin(15*rad),0,std::cos(15*rad)};Quat relative;Vec3 displacement;
    Check(StereoRenderMath::ApplyVirtualYaw(physical,{},35*rad,relative,displacement),"physical and virtual yaw fixture stays relative to the gameplay base");
    StereoRenderMath::Mat4 delta;Check(StereoRenderMath::CameraViewDelta(relative,{},{},0,100,delta),"same HMD delta feeds ordinary authored and exit frames");
    std::memcpy(requested.delta,delta,sizeof(delta));provide=Provide;applied=Applied;originalGpu=NativeSetter;
    AuthoredGpuFixture::caller=reinterpret_cast<void*>(base+kGpuCallerRva);
    const auto render=[&]{GpuHook(reinterpret_cast<void*>(state),nullptr,reinterpret_cast<const float*>(packet+0xA0));};
    render();const auto initial=observed;
    Check(appliedCount==1&&!gpuFrame.authoredHeading&&Near(pair.centre.basis.forward,native.basis.forward),"ordinary entry establishes accepted stable heading");
    Check(observedRenderContext&&observedRenderEpoch==frameGeneration.load()&&observedRenderEye==0,
        "actual first-person SetFrame can obtain current main epoch/eye before applied callback returns");
    std::uint64_t epochToken=99;unsigned eyeToken=99;
    Check(ReadRenderContext(epochToken,eyeToken)&&epochToken==observedRenderEpoch&&eyeToken==0,
        "current main renderer state exposes its exact image epoch and eye");
    auto beforeCinematicReads=CinematicReadFixture::reads;bool ordinaryGuard=true;
    LARGE_INTEGER guardStart{},guardFast{},guardNative{},guardFrequency{};
    QueryPerformanceFrequency(&guardFrequency);QueryPerformanceCounter(&guardStart);
    for(unsigned n=0;n<10000;++n)ordinaryGuard=ordinaryGuard&&!CinematicActive();
    QueryPerformanceCounter(&guardFast);
    Check(ordinaryGuard&&CinematicReadFixture::reads==beforeCinematicReads,
        "10000 actual committed-scene cinematic guards avoid native memory syscalls");
    renderContextOpen=false;bool nativeGuard=true;
    for(unsigned n=0;n<10000;++n)nativeGuard=nativeGuard&&!CinematicActive();
    QueryPerformanceCounter(&guardNative);renderContextOpen=true;
    Check(nativeGuard&&CinematicReadFixture::reads-beforeCinematicReads==40000,
        "outside the committed scene the same 10000 guards still perform all 40000 native reads");
    std::printf("Cinematic guard fixture 10000 calls: committed %.3f ms / native %.3f ms (microbenchmark, not gameplay FPS).\n",
        1000.*double(guardFast.QuadPart-guardStart.QuadPart)/guardFrequency.QuadPart,
        1000.*double(guardNative.QuadPart-guardFast.QuadPart)/guardFrequency.QuadPart);
    beforeCinematicReads=CinematicReadFixture::reads;
    bool panelGuards=true;
    for(unsigned n=0;n<10000;++n)panelGuards=panelGuards&&!CinematicCamera()&&!CinematicPanelActive();
    Check(panelGuards&&CinematicReadFixture::reads==beforeCinematicReads,
        "10000 actual Camera/Panel guards share the committed classification without native pointer walks");
    constexpr std::uintptr_t cinematicController=base+0x88000;
    U32(base+0x87750C,cinematicController);U32(cinematicController+0x24,camera);U32(camera+4,geometry);
    U32(manager+0x300,1);
    Check(!CinematicActive()&&CinematicReadFixture::reads==beforeCinematicReads,
        "native producer transition cannot reclassify a previously committed render packet");
    Check(!CinematicCamera()&&!CinematicPanelActive()&&CinematicReadFixture::reads==beforeCinematicReads,
        "Camera/Panel also retain only the same committed packet's classification during a producer cut transition");
    const auto savedCinematicState=currentCameraState;currentCameraState=state+4;
    Check(CinematicActive()&&CinematicReadFixture::reads>beforeCinematicReads,
        "foreign camera reads current native cinematic state instead of borrowing main classification");
    Check(CinematicCamera()==camera&&CinematicPanelActive(),
        "foreign camera sees the exact new native cut camera and panel immediately");
    currentCameraState=savedCinematicState;beforeCinematicReads=CinematicReadFixture::reads;
    const auto cinematicThread=gpuThread.load();gpuThread=GetCurrentThreadId()+1;
    Check(CinematicActive()&&CinematicReadFixture::reads>beforeCinematicReads,
        "non-GPU thread cannot borrow committed cinematic classification");gpuThread=cinematicThread;
    beforeCinematicReads=CinematicReadFixture::reads;cinematicVR=true;
    Check(CinematicCamera()==camera&&CinematicReadFixture::reads>beforeCinematicReads,
        "cinematic VR setting change revokes committed classification until another native packet");cinematicVR=false;
    beforeCinematicReads=CinematicReadFixture::reads;++frameGeneration;
    Check(CinematicActive()&&CinematicReadFixture::reads>beforeCinematicReads,
        "retired image generation reads native cinematic transition immediately");--frameGeneration;U32(manager+0x300,0);
    const auto savedState=currentCameraState;currentCameraState=state+4;epochToken=77;eyeToken=88;
    Check(!ReadRenderContext(epochToken,eyeToken)&&epochToken==77&&eyeToken==88,
        "foreign current camera state cannot borrow the retained primary token");currentCameraState=savedState;
    const auto savedGpuThread=gpuThread.load();gpuThread=GetCurrentThreadId()+1;
    Check(!ReadRenderContext(epochToken,eyeToken),"another thread cannot obtain a live GPU token");gpuThread=savedGpuThread;
    const auto savedView=*reinterpret_cast<const CameraPose*>(state+0xBC);auto badView=savedView;badView.position.x+=10;Put(state+0xBC,badView);
    Check(!ReadRenderContext(epochToken,eyeToken),"foreign actual VIEW cannot borrow main token");Put(state+0xBC,savedView);
    const auto savedEye=gpuFrame.eye;gpuFrame.eye=2;Check(!ReadRenderContext(epochToken,eyeToken),"invalid eye is rejected");gpuFrame.eye=savedEye;
    gpuFrame.nativeCinematic=true;Check(!ReadRenderContext(epochToken,eyeToken),"cinematic cannot issue a first-person lease");gpuFrame.nativeCinematic=false;
    captureCompromised=true;Check(!ReadRenderContext(epochToken,eyeToken),"failed main pass cannot authorize a later lease in the same image");captureCompromised=false;
    const auto firstImageEpoch=observedRenderEpoch;render();
    Check(observedRenderContext&&observedRenderEpoch==firstImageEpoch&&observedRenderEye==0,
        "repeated actual main camera packet retains the same image epoch and eye");--appliedCount;
    FinishScene();Check(!ReadRenderContext(epochToken,eyeToken),"finished scene cannot issue retained image authority");
    std::uint64_t captureEpoch=0,capturePair=0;unsigned captureEye=99;
    Check(ReadCaptureContext(captureEpoch,captureEye,capturePair)&&captureEpoch==firstImageEpoch&&captureEye==0&&capturePair==requested.pairId,
        "completed-image capture token survives draw cleanup without reopening native draw authority");
    const auto rejectCapture=[&]{captureEpoch=77;captureEye=88;capturePair=99;
        return !ReadCaptureContext(captureEpoch,captureEye,capturePair)&&captureEpoch==77&&captureEye==88&&capturePair==99;};
    captureCompromised=true;Check(rejectCapture(),"compromised image cannot issue a capture token");captureCompromised=false;
    handsCaptureReady=false;Check(rejectCapture(),"rejected hands image cannot issue a capture token");handsCaptureReady=true;
    gpuThread=GetCurrentThreadId()+1;Check(rejectCapture(),"another thread cannot obtain completed-image authority");gpuThread=savedGpuThread;
    const auto captureAnchor=gpuFrame.anchorValid;gpuFrame.anchorValid=false;Check(rejectCapture(),"unanchored frame cannot issue first-person capture token");gpuFrame.anchorValid=captureAnchor;
    gpuFrame.nativeCinematic=true;Check(rejectCapture(),"cinematic image cannot issue first-person capture token");gpuFrame.nativeCinematic=false;
    const auto captureNativeEye=gpuFrame.eye;gpuFrame.eye=2;Check(rejectCapture(),"invalid eye cannot issue completed-image authority");gpuFrame.eye=captureNativeEye;
    const auto capturePlayer=pair.player;pair.player+=4;Check(rejectCapture(),"changed local owner cannot borrow completed-image authority");pair.player=capturePlayer;
    ++frameGeneration;Check(rejectCapture(),"next image generation revokes capture token even before another camera packet");--frameGeneration;
    U32(base+0x8883A4,1u);beforeCinematicReads=CinematicReadFixture::reads;
    Check(CinematicPanelActive()&&CinematicReadFixture::reads>beforeCinematicReads,
        "scene cleanup reads a newly paused native menu immediately");U32(base+0x8883A4,0u);
    U32(manager+0x300,1);beforeCinematicReads=CinematicReadFixture::reads;
    Check(CinematicActive()&&CinematicReadFixture::reads>beforeCinematicReads,
        "scene cleanup revokes cinematic read optimization before the next camera packet");U32(manager+0x300,0);
    FrameBoundary();++requested.pairId;currentAction.action=action;currentAction.actionKind=NativeActionKL::Action::MiniAct;
    U32(dispatcher+4,action);U32(base+0x88E8B0,1);Put(entry+0x10,Pose(-150*rad));render();
    Check(appliedCount==2&&gpuFrame.authoredHeading&&Near(observed.basis.forward,initial.basis.forward),"actual MiniAct entry captures root reference without inheriting external camera yaw");
    for(unsigned step=1;step<=9;++step){
        FrameBoundary();++requested.pairId;requested.eye=0;
        Put(actorGeometry,Pose((40+step*10)*rad,{float(step),0,3}));Put(entry+0x10,Pose(-float(step)));
        requested.anchorPosition[0]=float(step);requested.anchorPosition[1]=170-float(step);render();
        Check(Near(pair.centre.basis.forward,Pose((step*10-.2f)*rad).basis.forward)&&
              observed.position.x==float(step)&&observed.position.y==170-float(step),
              "every actual authored camera step follows deliberate current root yaw and current anatomical eye position");
    }
    const auto finalBase=pair.centre;CameraPose expected;
    Check(Near(finalBase.basis.forward,Pose(89.8f*rad).basis.forward)&&
          NativeCameraKLMath::ApplyPose(finalBase,requested.delta,expected)&&Near(observed.basis.forward,expected.basis.forward),
          "actual GPU consumer follows deliberate90-degree actor turn and composes physical/virtual look exactly once");
    CameraPose actualView,actualWorld;Read(state+0xBC,actualView);
    Check(NativeCameraKLMath::WorldFromView(actualView,actualWorld)&&Near(actualWorld.basis.forward,observed.basis.forward),"native renderer receives the actual authored eye orientation, not just changed provider metadata");
    const auto once=observed;const auto turns=Snapshot().authoredHeadingTurns;
    FrameBoundary();requested.eye=1;Put(actorGeometry,Pose(170*rad));requested.anchorPosition[0]=10;render();
    Check(!gpuFrame.pairDiscontinuity&&Near(observed.basis.forward,once.basis.forward)&&observed.position.x==10&&Snapshot().authoredHeadingTurns==turns,
          "same-pair second eye updates current anatomy but cannot apply root turn twice");
    FrameBoundary();++requested.pairId;requested.eye=0;U32(base+0x88E8B0,0);U32(dispatcher+4,0);currentAction.action=0;currentAction.actionKind=NativeActionKL::Action::None;
    Put(actorGeometry,Pose(-90*rad));Put(entry+0x10,Pose(-170*rad));render();
    Check(!gpuFrame.authoredHeading&&!firstPersonHeading.authored.valid&&Near(pair.centre.basis.forward,finalBase.basis.forward)&&Near(observed.basis.forward,once.basis.forward),
          "actual skipped/normal mini exit preserves final view despite native third-person camera and body turn");
    const Vec3 walked{std::sin(gpuFrame.movementYawRadians),0,std::cos(gpuFrame.movementYawRadians)};
    Check(gpuFrame.movementYawValid&&Near(walked,observed.basis.forward),"actual exit movement-yaw certificate follows the turned rendered gaze instead of sending walking along the old heading");
    FrameBoundary();++requested.pairId;Put(entry+0x10,Pose(120*rad));render();
    Check(Near(observed.basis.forward,once.basis.forward),"ordinary source reuse after authored scene cannot reset final heading");
    // New action and a same-pair owner transition must rebase, never consume
    // the previous owner's angular difference or publish a mixed stereo pair.
    FrameBoundary();++requested.pairId;U32(base+0x88E8B0,1);U32(dispatcher+4,action);currentAction.action=action;currentAction.actionKind=NativeActionKL::Action::MiniAct;
    render();const auto beforeChanged=observed;
    FrameBoundary();requested.eye=1;U32(dispatcher+4,action+0x100);U32(action+0x100,base+0x566834);currentAction.action=action+0x100;currentAction.actionKind=NativeActionKL::Action::MiniAnim;Put(actorGeometry,Pose(70*rad));render();
    Check(gpuFrame.pairDiscontinuity&&Near(observed.basis.forward,beforeChanged.basis.forward),"changed MiniAnim owner forces pair restart and seeds reference instead of jumping the view");
    // Consumer must reject a forged provider's actor-turn certificate.
    FrameBoundary();++requested.pairId;requested.eye=0;deriveOwner=false;requested.authoredHeading=true;requested.authoredHeadingAction=action;
    requested.authoredActorForward=Pose(150*rad).basis.forward;render();
    Check(!gpuFrame.authoredHeading&&Near(observed.basis.forward,beforeChanged.basis.forward),"foreign action pointer cannot drive root yaw even when a provider sets the flag");
    FrameBoundary();++requested.pairId;requested.authoredHeadingAction=currentAction.action;U32(currentAction.action,base+0x566944);render();
    Check(!gpuFrame.authoredHeading,"native reload action vtable cannot acquire MiniAnim heading ownership");
    FrameBoundary();++requested.pairId;U32(currentAction.action,base+0x566834);U32(movement,base+0x567144);render();
    Check(!gpuFrame.authoredHeading,"actual rappel movement cannot acquire authored root yaw even with a MiniAnim action and forged provider flag");
    // Real pickup getter -> actor hierarchy/head pose -> actual GpuHook.
    // This deliberately uses a third-person native camera orbit unrelated
    // to the pickup anatomy; it must never determine the eye posture.
    FrameBoundary();pair={};firstPersonHeading={};deriveOwner=false;requested={};
    requested.valid=requested.anchorValid=requested.stableFirstPersonYaw=requested.animatedEye=true;
    requested.sourceCamera=requested.anchorCamera=camera;requested.anchorPlayer=player;requested.nearUnits=1;
    requested.pairId=100;requested.recenterAck=7;requested.worldUnitsPerMetre=100;
    for(unsigned i=0;i<4;++i)requested.fov[i]=requested.cullingFov[i]=1;
    std::memcpy(requested.delta,delta,sizeof(delta));U32(base+0x88E8B0,0);U32(movement,base+0x566AEC);
    U32(dispatcher+4,0);currentAction.action=0;currentAction.actionKind=NativeActionKL::Action::None;
    U32(player,base+0x56582C);U32(actorGeometry+0x50,0);U32(actorGeometry+0x60,player);
    constexpr std::uintptr_t model=base+0x83000,lookup=base+0x84000,poses=base+0x85000;
    U32(player+0x70,model);U32(model+0xC,lookup);U32(model+0x4A0,1);U32(model+0x4D0,poses);Put(lookup+64,std::uint8_t(0));
    const unsigned char getter[]={0x8b,0x44,0x24,0x04,0x85,0xc0,0x7c,0x05,0x83,0xf8,0x10,0x7c,0x01,0xcc,0x8b,0x84,0x81,0x2c,0x0a,0,0,0xc2,4,0};
    std::memcpy(reinterpret_cast<void*>(base+0x26FBD0),getter,sizeof(getter));
    Check(NativePlayerAnchor::InitializeVerifiedImage(base),"pickup fixture uses the production authenticated player/head getter");
    Put(actorGeometry,Pose(40*rad,{100,200,300}));Put(poses,Pose(0,{0,170,0}));Put(entry+0x10,Pose(0));
    auto anchor=NativePlayerAnchor::Current(100,1700,true);Check(anchor.valid&&anchor.headBone&&anchor.animatedHead,"pickup production anatomy source is owned and animated");
    Vec3 selectedPickupEye{};
    const auto setAnchor=[&]{auto position=anchor.position;
        if(requested.nativePickup)Check(NativeCameraKLMath::StablePickupEye(anchor.root,anchor.position,position),"pickup provider uses same-root stable eye without animated neck X/Z travel");
        selectedPickupEye=position;requested.anchorPosition[0]=position.x;requested.anchorPosition[1]=position.y;requested.anchorPosition[2]=position.z;};
    setAnchor();render();const auto pickupEntry=pair.centre;
    FrameBoundary();++requested.pairId;currentAction.action=action;currentAction.actionKind=NativeActionKL::Action::Pickup;
    U32(action,base+0x566628);U32(dispatcher+4,action);requested.nativePickup=requested.nativeActionUpValid=true;
    requested.pickupAction=action;requested.pickupActorForward=anchor.root.basis.forward;
    const auto setHead=[&](const Basis& b){requested.nativeActionForward[0]=b.forward.x;requested.nativeActionForward[1]=b.forward.y;requested.nativeActionForward[2]=b.forward.z;
        requested.nativeActionUp[0]=b.up.x;requested.nativeActionUp[1]=b.up.y;requested.nativeActionUp[2]=b.up.z;};
    // The real GPU producer faces model -Z, NOT +entity.forward. Exercise
    // those production helpers, including a neutral entry before bending.
    const auto bindHead=Pose(.43f);NativeRigMath::Bone animatedHead{bindHead.basis,{0,170,0}};Vec3 up,forward;
    const auto produceHead=[&]{const bool okay=NativeSkinMath::AnimatedHeadUp(anchor.root,bindHead,animatedHead,up)&&
        NativeSkinMath::AnimatedHeadForward(anchor.root,bindHead,animatedHead,forward);
        Check(okay,"actual GPU anatomical head-axis producer accepts pickup root/bind/animated bone");return Basis{forward,up,Cross(up,forward)};};
    auto head=produceHead();setHead(head);render();
    Check(gpuFrame.nativePickup&&Near(pair.centre.basis.forward,pickupEntry.basis.forward)&&Near(pair.centre.basis.up,pickupEntry.basis.up),
        "real minus-Z anatomical neutral pickup retains gameplay heading instead of reversing180 degrees");
    FrameBoundary();++requested.pairId;
    const Quat bend{std::sin(-30*rad),0,0,std::cos(30*rad)};
    animatedHead.basis={Rotate(bend,bindHead.basis.forward),Rotate(bend,bindHead.basis.up),Rotate(bend,bindHead.basis.right)};
    head=produceHead();
    setHead(head);Put(poses,Pose(0,{25,90,35}));anchor=NativePlayerAnchor::Current(100,1700,true);setAnchor();Put(entry+0x10,Pose(-150*rad));render();
    Check(gpuFrame.nativePickup&&Near(pair.centre.basis.forward,pickupEntry.basis.forward)&&Near(pair.centre.basis.up,{0,1,0})&&
          Near(pair.centre.position,selectedPickupEye)&&!Near(selectedPickupEye,anchor.position),
          "owned pickup keeps stable VR heading and actor-centred eye instead of chasing third-person head bend/translation");
    CameraPose expectedPickup;Check(NativeCameraKLMath::ApplyPose(pair.centre,requested.delta,expectedPickup)&&Near(observed.basis.forward,expectedPickup.basis.forward),
        "pickup posture composes the physical plus virtual HMD delta exactly once");
    Read(state+0xBC,actualView);Check(NativeCameraKLMath::WorldFromView(actualView,actualWorld)&&Near(actualWorld.position,selectedPickupEye)&&Near(actualWorld.basis.forward,observed.basis.forward),
        "native renderer itself receives stable VR pickup eye rather than following the animated third-person neck");
    FrameBoundary();++requested.pairId;Put(actorGeometry,Pose(70*rad,{110,200,310}));anchor=NativePlayerAnchor::Current(100,1700,true);setAnchor();
    head=produceHead();
    requested.pickupActorForward=anchor.root.basis.forward;setHead(head);render();const auto pickupTurn=pair.centre;
    Check(Near(firstPersonHeading.basis.forward,pickupEntry.basis.forward)&&Near(pickupTurn.position,selectedPickupEye),"pickup authored actor turn cannot rotate the VR camera away from its gameplay heading");
    FrameBoundary();requested.eye=1;render();Check(Near(pair.centre.basis.forward,pickupTurn.basis.forward),"pickup second eye cannot apply actor root turn twice");
    FrameBoundary();++requested.pairId;requested.eye=0;requested.nativePickup=false;U32(dispatcher+4,0);currentAction.action=0;
    currentAction.actionKind=NativeActionKL::Action::None;Put(entry+0x10,Pose(-2.9f));render();
    Check(!gpuFrame.nativePickup&&Near(pair.centre.basis.up,{0,1,0})&&Near(pair.centre.basis.forward,pickupEntry.basis.forward),"pickup exit retains accepted upright VR gameplay heading without native body-turn catch-up");
    FrameBoundary();++requested.pairId;requested.nativePickup=true;requested.pickupAction=action+4;render();
    Check(!gpuFrame.nativePickup,"forged or exited pickup owner cannot enable anatomical-posture camera");
    FrameBoundary();++requested.pairId;U32(dispatcher+4,action);currentAction.action=action;requested.pickupAction=action;U32(action,base+0x566944);render();
    Check(!gpuFrame.nativePickup,"reload cannot borrow pickup head-posture authorization");
    FrameBoundary();++requested.pairId;requested.nativePickup=false;requested.nativeActionTilt=true;
    requested.nativeActionUpValid=true;U32(movement,base+0x567144);U32(dispatcher+4,0);currentAction.action=0;
    const Basis faceUp{{0,1,0},{0,0,-1},{1,0,0}};setHead(faceUp);render();
    Check(firstPersonHeading.traversalHeadingPending,"actual rappel consumer remembers its later gameplay heading needs synchronization");
    FrameBoundary();++requested.pairId;requested.nativeActionTilt=false;requested.actorForwardValid=true;
    requested.actorForward=Pose(75*rad).basis.forward;U32(movement,base+0x566AEC);render();
    Check(!firstPersonHeading.traversalHeadingPending&&Near(observed.basis.forward,Scale(requested.actorForward,-1.f)),
        "actual first upright post-rappel eye faces current actor instead of resurrecting pre-rope camera yaw");
    const auto exitEye=observed;FrameBoundary();requested.eye=1;render();
    Check(Near(observed.basis.forward,exitEye.basis.forward),"actual right eye does not align post-rappel heading twice");
    // Native scripted van intro is MiniAct + Vehicle movement, not the two
    // Action::Vehicle classes. Its animated head must supply the entire view.
    FrameBoundary();++requested.pairId;requested.eye=0;requested.nativeVehicleTransition=true;requested.vehicleAction=action;
    requested.actorForwardValid=false;requested.vehicleBase=Pose(-80*rad,{200,145,300});
    currentAction.action=action;currentAction.actionKind=NativeActionKL::Action::MiniAct;currentAction.movementKind=NativeActionKL::Movement::Vehicle;
    U32(movement,base+0x566D14);U32(dispatcher+4,action);U32(action,base+0x5667F4);
    StereoRenderMath::Identity(delta);std::memcpy(requested.delta,delta,sizeof(delta));render();
    Check(gpuFrame.nativeVehicleTransition&&Near(observed.position,requested.vehicleBase.position)&&Near(observed.basis.forward,requested.vehicleBase.basis.forward)&&
          CaptureReady(requested.pairId,requested.eye),"actual van MiniAct renders from current head pose instead of frozen gameplay or seated root");
    FrameBoundary();++requested.pairId;requested.vehicleBase=Pose(35*rad,{240,115,320});render();
    Check(Near(observed.position,requested.vehicleBase.position)&&Near(observed.basis.forward,requested.vehicleBase.basis.forward),
          "actual vehicle entry follows current animated head translation and gaze without looking back at the hero");
    FrameBoundary();requested.eye=1;requested.vehicleBase=Pose(45*rad,{243,112,321});render();
    Check(Near(observed.position,requested.vehicleBase.position)&&Near(observed.basis.forward,requested.vehicleBase.basis.forward),
          "second vehicle eye follows its current queued anatomy instead of reusing the first eye's seat");
    Check(observedRenderContext&&observedRenderEpoch==frameGeneration.load()&&observedRenderEye==1&&observedRenderEpoch!=firstImageEpoch,
          "a later actual eye has a distinct scene-image epoch and its correct eye identity");
    FrameBoundary();++requested.pairId;requested.eye=0;currentAction.movementKind=NativeActionKL::Movement::BaseMove;U32(movement,base+0x566AEC);render();
    Check(gpuFrame.nativeVehicleTransition,"same exact exit MiniAct retains animated eyes when movement switches to walking");
    const auto successful=appliedCount;
    // A later pass in the SAME image loses current anatomy/ownership. Main
    // frameSnapshot was already filled; capture readiness must still reject.
    U32(dispatcher+4,action+4);currentAction.action=action+4;requested.vehicleAction=action;render();
    Check(appliedCount==successful&&!CaptureReady(requested.pairId,requested.eye),
          "later failed owned camera pass invalidates a previously tracked image before capture instead of publishing native thirdperson pixels");
    requested.nativeVehicleTransition=false;requested.actorForwardValid=false;render();
    Check(appliedCount==successful+1&&!CaptureReady(requested.pairId,requested.eye),
          "later recovery within the same partial image cannot undo its capture rejection");
    FrameBoundary();++requested.pairId;render();Check(CaptureReady(requested.pairId,requested.eye),"next fully tracked image recovers capture without retaining the compromised image");
    FrameBoundary();++requested.pairId;requested.nativeVehicleTransition=true;requested.vehicleAction=action;render();
    requested.nativeVehicleTransition=false;render();
    Check(!CaptureReady(requested.pairId,requested.eye),"failed first main pass followed by recovery cannot publish a partially native image either");
    FrameBoundary();++requested.pairId;render();Check(CaptureReady(requested.pairId,requested.eye),"fresh image recovers after an initial-pass rejection");
    handsCaptureReady=false;Check(!CaptureReady(requested.pairId,requested.eye),"failed admitted hands pair prevents publication of this image");
    FinishScene();Check(!CaptureReady(requested.pairId,requested.eye),"scene cleanup keeps rejection sticky until capture is checked");
    FrameBoundary();Check(handsCaptureReady,"boundary retires the failed image even if the next scene has no hands callback");
    ++requested.pairId;requested.nativeCinematic=true;requested.actorCinematic=false;const auto handCallsBeforeCinematic=handSetCalls;render();
    Check(handSetCalls==handCallsBeforeCinematic&&CaptureReady(requested.pairId,requested.eye),"full cinematic without hands SetFrame can publish after previous image rejection");
    FrameBoundary();++requested.pairId;requested.nativeCinematic=false;render();
    Check(CaptureReady(requested.pairId,requested.eye),"next authenticated scene recovers after a hands rejection");
    constexpr std::uintptr_t foreignCamera=base+0x89000;
    U32(foreignCamera,base+kCameraVtable);U32(geometry+0x60,foreignCamera);const auto unclassified=Snapshot().unclassifiedPrimaryStatePasses;render();
    Check(Snapshot().unclassifiedPrimaryStatePasses==unclassified+1&&CaptureReady(requested.pairId,requested.eye),
          "unclassified source sharing primary state is counted without falsely declaring an offscreen auxiliary pass to be the presentation target");
    Check(!ReadRenderContext(epochToken,eyeToken),"foreign or auxiliary camera packet cannot borrow a live render-context token");
    U32(geometry+0x60,camera);
    VirtualFree(mem,0,MEM_RELEASE);std::printf("Production authored actor/pickup heading GPU consumer: %u checks PASS.\n",checks);
}
