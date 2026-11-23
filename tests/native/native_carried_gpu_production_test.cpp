// Real camera consumer on private native-layout memory. The only substituted
// compiler intrinsic is the authenticated call-site address: no game/HMD.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
namespace CarriedGpuFixture {inline void* caller=nullptr;}
#define __builtin_return_address(level) CarriedGpuFixture::caller
#include "native_camera_kl.cpp"
#include "native_passenger_roomscale_math.hpp"
#include "native_carried_camera_math.hpp"
#undef __builtin_return_address
// The production installer normally takes this relocation target's address.
// This fixture never installs hooks; keep its inline storage for the emitted
// (unexecuted) exact decoder trampoline.
__attribute__((used)) static void** const keepProjectionTailStorage=&KL1ProjectionTailOriginal;
#include <cstdlib>
#include <cstdio>
namespace {
using namespace NativeCameraMath;
unsigned checks=0,appliedCount=0,bodyCount=0,handsCount=0;
NativeActionKL::State currentAction;
NativeCameraKL::Frame requested;
CameraPose observed;
void Check(bool ok,const char* why){++checks;if(!ok){std::printf("FAIL %s\n",why);std::exit(1);}}
[[noreturn]]void Unexpected(){std::puts("FAIL unrelated native/runtime entry");std::abort();}
template<class T>void Put(std::uintptr_t at,const T& value){std::memcpy(reinterpret_cast<void*>(at),&value,sizeof(value));}
void U32(std::uintptr_t at,std::uint32_t value){Put(at,value);}
bool Near(Vec3 a,Vec3 b){return Dot(Subtract(a,b),Subtract(a,b))<.0001f;}
bool Provide(NativeCameraKL::Frame& out,void*){const auto source=out.sourceCamera;out=requested;return out.sourceCamera==source;}
void Applied(const NativeCameraKL::Frame& frame,const CameraPose& eye,const float*,void*){Check(frame.nativeCarried,"real callback retains nativeCarried classification");observed=eye;++appliedCount;}
void __thiscall NativeSetter(void*,const float*){}
}
namespace NativeActionKL {State Current(){return currentAction;}bool InitializeVerifiedImage(std::uintptr_t){Unexpected();}}
namespace NativeCoverKL {bool InitializeVerifiedImage(std::uintptr_t){Unexpected();}bool InputBasis(std::uintptr_t,NativeCameraMath::Basis&){Unexpected();}}
namespace NativeHandsKL {
bool RenderPairCaptureReady(){return true;}
void RetireRenderPairImage(){}
void ClearFrame(){}
void SetFrame(const CameraPose&,const float*,const float*,const float*,float,std::uint64_t,bool,std::uint32_t,bool){++handsCount;}
}
namespace NativeBodyKL {void ClearFrame(){}void SetFrame(const CameraPose&,std::uint64_t,float,bool,std::uint32_t){++bodyCount;}}
namespace VRBridge {bool Ready(){return true;}Tracking HeadPose(){Unexpected();}}
namespace D3D9Capture {void ObserveNativeCamera(){}}
extern "C" {
MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*){Unexpected();}MH_STATUS WINAPI MH_QueueEnableHook(LPVOID){Unexpected();}
MH_STATUS WINAPI MH_ApplyQueued(){Unexpected();}MH_STATUS WINAPI MH_DisableHook(LPVOID){Unexpected();}MH_STATUS WINAPI MH_RemoveHook(LPVOID){Unexpected();}
}
int main(){
    using namespace NativeCameraKL;constexpr std::uintptr_t base=0x30000000;
    auto* mem=VirtualAlloc(reinterpret_cast<void*>(base),0x900000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    Check(mem==reinterpret_cast<void*>(base),"private native image allocation");image=base;installed=true;
    constexpr std::uintptr_t data=base+0x10000,player=base+0x20000,camera=base+0x30000,system=base+0x40000,
        manager=base+0x41000,packet=base+0x50000,queue=base+0x51000,entry=base+0x60000,
        geometry=base+0x61000,state=base+0x70000,dispatcher=base+0x80000,movement=base+0x81000;
    U32(base+0x68760C,data);U32(data+0xA2C,player);U32(player+0x9A0,camera);U32(player+0x194,dispatcher);U32(dispatcher+4,0);U32(player+0x67C,movement);
    U32(base+0x6875F0,system);U32(system+0x96C,manager);U32(manager+0x300,0);
    U32(camera,base+kCameraVtable);U32(entry,base+kCameraEntryVtable);U32(entry+0xF4,geometry);U32(geometry+0x60,camera);
    U32(packet,1);U32(packet+4,entry);U32(packet+8,queue);U32(packet+0xC,1);U32(packet+0x14,0x26);U32(queue+0x14,packet);U32(queue+0xC,packet+0xB0);
    CameraPose native;native.basis={{0,0,1},{0,1,0},{1,0,0}};native.position={9000,8000,7000};CameraPose nativeView;
    Check(NativeCameraKLMath::ViewFromWorld(native,nativeView),"native queued camera fixture");Put(packet+0x18,nativeView);Put(entry+0x10,native);U32(base+0x7678A4,0);
    float projection[16]{};const float tangents[]={1,1,1,1};Check(ApplyEyeProjection(projection,tangents,1,40000,false),"native projection fixture");std::memcpy(reinterpret_cast<void*>(packet+0x48),projection,sizeof(projection));
    currentAction.valid=true;currentAction.player=player;currentAction.movement=movement;currentAction.movementKind=NativeActionKL::Movement::Mounted;
    requested.valid=requested.anchorValid=requested.stableFirstPersonYaw=requested.nativeCarried=true;
    requested.sourceCamera=requested.anchorCamera=camera;requested.anchorPlayer=player;requested.carriedReference=geometry+0x100;
    requested.carriedReferenceKind=2;requested.carriedBase=native;requested.carriedBase.position={100,200,300};
    requested.anchorPosition[0]=100;requested.anchorPosition[1]=200;requested.anchorPosition[2]=300;
    requested.nearUnits=1;requested.pairId=17;requested.eye=0;requested.worldUnitsPerMetre=100;
    for(unsigned i=0;i<4;++i)requested.fov[i]=requested.cullingFov[i]=1;
    StereoRenderMath::Mat4 identity;StereoRenderMath::Identity(identity);std::memcpy(requested.delta,identity,sizeof(identity));
    provide=Provide;applied=Applied;originalGpu=NativeSetter;CarriedGpuFixture::caller=reinterpret_cast<void*>(base+kGpuCallerRva);
    firstPersonHeading.valid=true;firstPersonHeading.player=player;firstPersonHeading.camera=camera;firstPersonHeading.basis=native.basis;
    firstPersonHeading.sourceState=state+0x1000;
    Vec3 unused;Check(firstPersonHeading.worldGaze.Select(native,unused),"established ordinary gameplay heading fixture");
    GpuHook(reinterpret_cast<void*>(state),nullptr,reinterpret_cast<const float*>(packet+0xA0));
    Check(appliedCount==1&&Near(observed.position,{100,200,300})&&pair.valid,"actual GPU first eye uses current anatomical seat instead of external native camera");
    Check(!requested.movementYawValid&&bodyCount==1&&handsCount==1,"actual carried path keeps body/hands rendering and avoids walking/yaw callback");
    const auto firstCentre=pair.centre;FrameBoundary();requested.eye=1;requested.carriedBase.position={150,205,320};
    requested.anchorPosition[0]=150;requested.anchorPosition[1]=205;requested.anchorPosition[2]=320;
    GpuHook(reinterpret_cast<void*>(state),nullptr,reinterpret_cast<const float*>(packet+0xA0));
    Check(appliedCount==2&&!gpuFrame.pairDiscontinuity&&Near(observed.position,{150,205,320})&&!Near(firstCentre.position,observed.position),
          "actual same-pair right eye follows new carrier position instead of cached first-eye centre");
    Check(firstPersonHeading.valid&&Near(firstPersonHeading.basis.forward,native.basis.forward)&&
          Near(firstPersonHeading.worldGaze.forward,native.basis.forward),"actual carried eye preserves ordinary gameplay heading and gaze state");
    Check(firstPersonHeading.sourceState==state+0x1000,"carried renderer cannot replace the retained foot-heading source-state identity");
    FrameBoundary();requested.eye=0;++requested.pairId;requested.carriedReference+=4;
    requested.carriedBase.position={500,500,500};GpuHook(reinterpret_cast<void*>(state),nullptr,reinterpret_cast<const float*>(packet+0xA0));
    Check(appliedCount==3&&Near(observed.position,{500,500,500}),"new native carrier starts next pair with current reference");
    FrameBoundary();requested.eye=0;++requested.pairId;const Quat physical{0,std::sin(.25f),0,std::cos(.25f)};
    StereoRenderMath::Mat4 delta;Check(StereoRenderMath::CameraViewDelta(physical,{},{},0,100,delta),"physical carried free-look delta fixture");
    std::memcpy(requested.delta,delta,sizeof(delta));CameraPose expected;
    Check(NativeCameraKLMath::ApplyPose(requested.carriedBase,requested.delta,expected),"physical carried expected eye");
    GpuHook(reinterpret_cast<void*>(state),nullptr,reinterpret_cast<const float*>(packet+0xA0));
    Check(appliedCount==4&&Near(observed.basis.forward,expected.basis.forward)&&Near(observed.position,requested.carriedBase.position),"actual carried GPU consumer composes physical head yaw once without moving the seat origin");
    FrameBoundary();requested.eye=1;requested.carriedBase.basis.forward={};
    GpuHook(reinterpret_cast<void*>(state),nullptr,reinterpret_cast<const float*>(packet+0xA0));
    Check(appliedCount==4,"invalid current carried pose cannot render a stale first-eye position");
    // Exercise the seat producer and actual GPU consumer together, including
    // an intro standing pose followed by the lower authored seated pose.
    currentAction.movementKind=NativeActionKL::Movement::Vehicle;
    NativeCarriedCamera::State passenger;NativeCarriedCamera::Reference actor{native,player,1};
    CameraPose anatomy=native;anatomy.position={200,470,300};requested.carriedReference=player;requested.carriedReferenceKind=1;
    StereoRenderMath::Identity(identity);std::memcpy(requested.delta,identity,sizeof(identity));
    ++requested.pairId;
    Check(passenger.Select(actor,anatomy,player,camera,movement,requested.carriedBase,requested.pairId),"passenger producer accepts authored entry anatomy");
    FrameBoundary();requested.eye=0;GpuHook(reinterpret_cast<void*>(state),nullptr,reinterpret_cast<const float*>(packet+0xA0));
    Check(appliedCount==5&&Near(observed.position,anatomy.position),"actual first passenger eye uses current entry anatomy");
    const auto firstPassenger=observed.position;
    anatomy.position={220,335,350};
    actor.root.position=Add(actor.root.position,{30,0,50});
    Check(passenger.Select(actor,anatomy,player,camera,movement,requested.carriedBase,requested.pairId),"same passenger identity keeps one pair-local head source");
    FrameBoundary();requested.eye=1;GpuHook(reinterpret_cast<void*>(state),nullptr,reinterpret_cast<const float*>(packet+0xA0));
    Check(appliedCount==6&&Near(observed.position,Add(firstPassenger,{30,0,50}))&&!gpuFrame.pairDiscontinuity,
        "actual second eye retains pair-local head while the actor root moves");
    FrameBoundary();requested.eye=0;++requested.pairId;
    Check(passenger.Select(actor,anatomy,player,camera,movement,requested.carriedBase,requested.pairId),"next pair samples current lowered passenger anatomy");
    GpuHook(reinterpret_cast<void*>(state),nullptr,reinterpret_cast<const float*>(packet+0xA0));
    Check(appliedCount==7&&Near(observed.position,anatomy.position),"next actual pair reaches the new authored seat without locking roof height");
    // Real bounded passenger producer -> real tracked GPU camera consumer.
    NativePassengerRoomscale::State room;NativePassengerRoomscale::Sample roomSample;
    roomSample.key={player,camera,movement,0};roomSample.tracked=true;roomSample.tick=1000;roomSample.pair=++requested.pairId;
    roomSample.headPosition={0,1.6f,0};roomSample.trackingOrigin={};roomSample.relativeHead={};Vec3 local;
    Check(room.Select(roomSample,local)&&Near(local,{}),"passenger room producer enters without changing the accepted current seat");
    roomSample.tick=1050;++roomSample.pair;roomSample.headPosition={.2f,1.25f,-.1f};roomSample.intent={true,true,1050,.7f,.5f};
    Check(room.Select(roomSample,local)&&local.y<-.35f&&local.y>=-.65f&&local.x>0&&local.z<0,
          "tracked crouch and left-stick navigation produce bounded local camera motion inside the van");
    requested.pairId=roomSample.pair;requested.eye=0;
    Check(StereoRenderMath::CameraViewDelta({},local,{},0,100,delta),"passenger local movement constructs the production delta");
    std::memcpy(requested.delta,delta,sizeof(delta));requested.carriedBase=anatomy;
    Check(NativeCameraKLMath::ApplyPose(requested.carriedBase,requested.delta,expected),"passenger movement expected actual eye");
    const auto source=firstPersonHeading;const auto beforeHands=handsCount,beforeBody=bodyCount;
    FrameBoundary();GpuHook(reinterpret_cast<void*>(state),nullptr,reinterpret_cast<const float*>(packet+0xA0));
    Check(Near(observed.position,expected.position)&&!Near(observed.position,anatomy.position)&&handsCount==beforeHands+1&&bodyCount==beforeBody+1,
          "actual GPU first eye follows physical crouch and bounded virtual movement while keeping tracked hands and body callbacks");
    roomSample.headPosition={4,-8,9};Vec3 otherEye;
    Check(room.Select(roomSample,otherEye)&&Near(otherEye,local),"the second eye cannot integrate another movement step or use a different physical crouch sample");
    requested.eye=1;requested.carriedBase.position=Add(anatomy.position,{50,0,0});
    Check(NativeCameraKLMath::ApplyPose(requested.carriedBase,requested.delta,expected),"moving van transports the same local crouch offset");
    FrameBoundary();GpuHook(reinterpret_cast<void*>(state),nullptr,reinterpret_cast<const float*>(packet+0xA0));
    Check(Near(observed.position,expected.position)&&!gpuFrame.pairDiscontinuity,
          "actual GPU second eye composes the current moving carrier and the same room offset");
    Check(firstPersonHeading.player==source.player&&firstPersonHeading.camera==source.camera&&
          Near(firstPersonHeading.basis.forward,source.basis.forward)&&Near(firstPersonHeading.worldGaze.forward,source.worldGaze.forward),
          "passenger navigation leaves the accepted ordinary heading and gaze state unchanged");
    VirtualFree(mem,0,MEM_RELEASE);std::printf("Production carried GPU consumer: %u checks PASS.\n",checks);
}
