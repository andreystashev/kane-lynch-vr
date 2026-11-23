// Actual hands staging + body follow + weapon rigid hook over isolated
// native-layout memory. No game, runtime, detour, GPU or external input.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstring>
namespace HipRigidReadFixture {
inline std::uintptr_t trigger=0,target=0;inline std::size_t triggerSize=0;inline std::uint32_t replacement=0;
inline BOOL WINAPI Read(HANDLE process,LPCVOID source,LPVOID output,SIZE_T size,SIZE_T* copied) {
    const auto result=::ReadProcessMemory(process,source,output,size,copied);
    if(result&&trigger==reinterpret_cast<std::uintptr_t>(source)&&size==triggerSize) {
        std::memcpy(reinterpret_cast<void*>(target),&replacement,4);trigger=0;
    }
    return result;
}
}
#define ReadProcessMemory HipRigidReadFixture::Read
#include "native_hands_kl.cpp"
#include "native_body_kl.cpp"
#include "native_camera_kl_math.hpp"
#include "native_weapon_kl.cpp"
#undef ReadProcessMemory
#include <cstdio>
#include <cstdlib>
namespace HipRigidFixture {
using namespace NativeCameraMath;
unsigned checks=0,failures=0,nativeRigidCalls=0,nativeWorldCalls=0;
CameraPose nativeWorld,renderedWorld;
bool ready=true,activeView=true;
NativeActionKL::State action;
NativePassengerActorKL::Motion cargoMotion;
bool CargoMotion(std::uintptr_t player,std::uint32_t ack,NativePassengerActorKL::Motion& output){
    if(!cargoMotion.active||cargoMotion.player!=player||(ack!=0xFFFFFFFFu&&ack!=cargoMotion.recenter))return false;
    output=cargoMotion;return true;
}
void Check(bool condition,const char* why){++checks;if(!condition){++failures;std::printf("FAIL %s\n",why);}}
template<class T>void Put(std::uintptr_t at,const T& value){std::memcpy(reinterpret_cast<void*>(at),&value,sizeof(value));}
bool Near(Vec3 a,Vec3 b){return NativeArmMath::Length(Subtract(a,b))<.001f;}
bool Near(const CameraPose& a,const CameraPose& b){return Near(a.position,b.position)&&Near(a.basis.up,b.basis.up)&&Near(a.basis.right,b.basis.right)&&Near(a.basis.forward,b.basis.forward);}
void __thiscall Rigid(void*,void* state,std::uintptr_t){++nativeRigidCalls;Put(reinterpret_cast<std::uintptr_t>(state)+0x8C,nativeWorld);}
void __thiscall World(void* state,const CameraPose* pose){++nativeWorldCalls;renderedWorld=*pose;Put(reinterpret_cast<std::uintptr_t>(state)+0x8C,*pose);}
}
namespace VRBridge {
bool Ready(){return HipRigidFixture::ready;}long RecenterAck(){return 7;}
bool TeamHudVisible(){return false;}bool WeaponMenuVisible(){return false;}
}
namespace NativeCameraKL {
bool CinematicActive(){return false;}
bool ReadRenderContext(std::uint64_t&,unsigned&){return false;}
bool ReadCaptureContext(std::uint64_t&,unsigned&,std::uint64_t&){return false;}
bool DrawStateActive(std::uintptr_t,bool*,bool){return HipRigidFixture::activeView;}
}
namespace NativeActionKL {
bool InitializeVerifiedImage(std::uintptr_t){return true;}State Current(){return HipRigidFixture::action;}
bool PreserveRig(){return false;}
}
namespace NativeCoverKL {
void OnTrackedFire(std::uintptr_t){}bool SimulationCallbackActive(std::uintptr_t){return false;}
}
namespace NativeAimKL {
GazeTrace TraceGaze(const NativeWeaponKL::HeadGaze&,const NativeWeaponKL::AimFrame&,NativeCameraMath::Vec3&){return GazeTrace::Miss;}
}
extern "C" {
MH_STATUS WINAPI MH_Initialize(){return MH_OK;}
MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*){std::abort();}
MH_STATUS WINAPI MH_EnableHook(LPVOID){std::abort();}MH_STATUS WINAPI MH_DisableHook(LPVOID){std::abort();}
MH_STATUS WINAPI MH_RemoveHook(LPVOID){std::abort();}MH_STATUS WINAPI MH_QueueEnableHook(LPVOID){std::abort();}
MH_STATUS WINAPI MH_ApplyQueued(){std::abort();}
}
int main() {
    using namespace HipRigidFixture;using namespace NativeCameraMath;
    static_assert(sizeof(void*)==4,"actual native rigid ABI fixture is x86");
    auto* allocation=VirtualAlloc(nullptr,0x900000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    Check(allocation!=nullptr,"private body/rigid fixture allocation");if(!allocation)return 1;
    const auto base=reinterpret_cast<std::uintptr_t>(allocation);
    const auto at=[&](std::uintptr_t offset){return static_cast<std::uint32_t>(base+offset);};
    const auto data=at(0x10000),actor=at(0x20000),model=at(0x30000),rootGeometry=at(0x40000),
        skinEntry=at(0x50000),rigidEntry=at(0x60000),cache=at(0x70000),lookup=at(0x80000),
        parents=at(0x81000),rest=at(0x82000),extra=at(0x90000),part=at(0x91000),state=at(0xA0000),
        modelResource=at(0xB0000),parts=at(0xB1000),header=at(0xB2000),definitions=at(0xB3000),partResource=at(0xB4000);
    Put(base+0x68760C,data);Put(data+0xA2C,actor);Put(actor,at(0x56582C));Put(actor+4,rootGeometry);Put(actor+0x70,model);Put(rootGeometry+0x60,actor);
    Put(skinEntry,at(0x541034));Put(skinEntry+0xF4,rootGeometry);Put(skinEntry+0x108,cache);Put(skinEntry+0x114,std::uint32_t(8));
    Put(rigidEntry,at(0x541070));Put(rigidEntry+0xF4,rootGeometry);Put(rigidEntry+0x108,cache);
    Put(model+0x4A0,std::uint32_t(8));Put(model+0xC,lookup);Put(model+0x10,parents);Put(model+4,rest);
    std::memset(reinterpret_cast<void*>(lookup),255,128);Put(lookup,std::uint8_t(1));Put(lookup+6,std::uint8_t(3));Put(lookup+63,std::uint8_t(5));Put(lookup+64,std::uint8_t(4));
    Put(lookup+16,std::uint8_t(6));Put(lookup+22,std::uint8_t(7));
    const std::uint8_t hierarchy[]={0,0,1,2,3,1,2,2};std::memcpy(reinterpret_cast<void*>(parents),hierarchy,sizeof(hierarchy));
    NativeSkinMath::BindLocal local[8];local[1].position={0,90,0};local[2].position={0,50,0};local[3].position={0,15,0};local[4].position={0,4,-8};local[5].position={14,3,-7};local[6].position={20,0,0};local[7].position={-20,0,0};
    std::memcpy(reinterpret_cast<void*>(rest),local,sizeof(local));std::uint32_t ids[]={0,0,1,2,3,1,2,2};CameraPose bind[8];
    Check(NativeSkinMath::BindPose(local,ids,8,bind),"actual costume anatomical bind");
    for(unsigned i=0;i<8;++i){float rows[12];Check(NativeSkinMath::Palette(bind[i],{bind[i].basis,bind[i].position},rows),"actual original skin palette");std::memcpy(reinterpret_cast<void*>(cache+0x90+i*sizeof(rows)),rows,sizeof(rows));}
    Put(rootGeometry+0x68,std::uint32_t(10));
    const std::uint32_t resources[]={modelResource,parts,header,definitions,partResource};for(unsigned i=0;i<5;++i)Put(base+0x71F308+(10+i)*4,resources[i]);
    Put(modelResource+2,std::uint16_t(7));Put(modelResource+0xC,std::uint8_t(1));Put(modelResource+0x10,std::uint32_t(11));Put(parts+4,std::uint32_t(12));Put(header,std::uint32_t(8));Put(header+4,std::uint32_t(13));
    const char hipName[16]="hip attacher";Put(definitions+5*64+0x1C,hipName);Put(extra+4,part);Put(part+0x14,std::uint32_t(14));Put(partResource+0x3C,std::uint32_t(5));
    CameraPose raw;raw.basis={{0,0,1},{0,1,0},{1,0,0}};raw.position={300,160,-900};Put(skinEntry+0x10,raw);Put(rigidEntry+0x10,raw);Put(base+0x7678A4,std::uint32_t(0));
    NativeHandsKL::image=base;NativeHandsKL::installed=true;NativeHandsKL::hideHead=false;
    NativeHandsKL::lastOwnedEntry=skinEntry;NativeHandsKL::lastOwnedModel=model;NativeHandsKL::lastOwnedEntryTick=GetTickCount();
    NativeHandsKL::CameraFrame camera;camera.valid=true;camera.pairId=55;camera.generation=77;camera.units=100;camera.firstPerson=true;camera.recenterAck=7;
    camera.centre={{{1,0,0},{0,1,0},{0,0,-1}},Add(raw.position,{0,159,-8})};
    Check(NativeHandsKL::CaptureRigMetadata(model,actor,camera.rig),"actual current owned body rig metadata");
    NativeHandsKL::publishedFrame=camera;NativeHandsKL::frameGeneration=77;NativeHandsKL::frameThread=GetCurrentThreadId();
    NativeBodyKL::SetFrame(camera.centre,camera.pairId,100,true,7);
    NativeWeaponKL::image=base;NativeWeaponKL::installed=true;NativeWeaponKL::hipRigidProof=true;NativeWeaponKL::originalRigid=Rigid;NativeWeaponKL::originalWorld=World;
    CameraPose actualRaw,actualRender;
    Check(NativeHandsKL::FrameBodyRoot(actor,55,actualRaw,actualRender)&&Near(actualRaw,raw),"real FrameBodyRoot reconstructs the current exact body source");
    Check(!Near(actualRender,actualRaw),"real body-follow staging supplies a nonidentity VR body transform");
    // .43: the actual first-person palette producer must align the authored
    // pickup BODY to the VR view, rather than make the camera chase it.
    const auto ordinaryCamera=camera;
    action.valid=true;action.player=actor;action.movement=actor+0x1000;action.movementKind=NativeActionKL::Movement::BaseMove;
    action.action=actor+0x2000;action.actionKind=NativeActionKL::Action::Pickup;
    for(unsigned bone:{3u,4u}){const auto shifted=Add(bind[bone].position,{35,-65,45});float rows[12];
        Check(NativeSkinMath::Palette(bind[bone],{bind[bone].basis,shifted},rows),"native pickup source palette retains displaced animated anatomy");
        std::memcpy(reinterpret_cast<void*>(cache+0x90+bone*sizeof(rows)),rows,sizeof(rows));}
    Vec3 animatedEye,stableEye;CameraPose actorRoot;
    Check(NativeHandsKL::GpuHeadAnchor(actor,animatedEye,100,true,nullptr,nullptr,&actorRoot)&&
          NativeCameraKLMath::StablePickupEye(actorRoot,animatedEye,stableEye),"actual queued native head supplies filtered actor-centred pickup eye");
    const auto headLocal=NativeRigMath::Inverse(actorRoot.basis,Subtract(animatedEye,actorRoot.position));
    std::array<unsigned char,8*48> sourceBefore{};std::memcpy(sourceBefore.data(),reinterpret_cast<void*>(cache+0x90),sourceBefore.size());
    camera.nativeBodyAction=camera.nativeAction=true;camera.pairId=56;camera.generation=78;camera.centre.position=stableEye;
    NativeHandsKL::publishedFrame=camera;NativeHandsKL::frameGeneration=camera.generation;NativeBodyKL::SetFrame(camera.centre,camera.pairId,100,true,7);
    Check(NativeHandsKL::FrameBodyRoot(actor,56,actualRaw,actualRender)&&Near(actualRaw,raw),"actual PrepareDraw accepts owned Pickup body-follow while retaining its native source root");
    const auto alignedHead=Add(actualRender.position,NativeRigMath::Transform(actualRender.basis,headLocal));
    const auto facing=Cross(NativeRigMath::Transform(actualRender.basis,{-40,0,0}),{0,1,0});
    Vec3 facingUnit;Check(NativeArmMath::Unit(facing,facingUnit)&&Dot(facingUnit,camera.centre.basis.forward)>.999f&&
        std::fabs(alignedHead.x-stableEye.x)<.001f&&std::fabs(alignedHead.z-stableEye.z)<.001f&&actualRender.position.y==raw.position.y,
        "pickup authored body faces the VR gaze, head stays over the camera and native feet keep their original height");
    Check(!std::memcmp(sourceBefore.data(),reinterpret_cast<void*>(cache+0x90),sourceBefore.size()),"pickup body anchoring never changes original native palette/bones");
    // The same real draw-local adapter handles the two separately verified
    // native actions reported by the player, without freezing their bones.
    for(unsigned kind=0;kind<3;++kind){
        action.actionKind=kind==0?NativeActionKL::Action::Throw:(kind==1?NativeActionKL::Action::Heal:NativeActionKL::Action::MiniAct);
        action.throwBack=kind==0;action.explosionCover=kind==2;camera.pairId=60+kind;camera.generation=82+kind;
        NativeHandsKL::publishedFrame=camera;NativeHandsKL::frameGeneration=camera.generation;
        NativeBodyKL::SetFrame(camera.centre,camera.pairId,100,true,7);
        Check(NativeHandsKL::FrameBodyRoot(actor,camera.pairId,actualRaw,actualRender),
            "real owned throw-back/revive draw accepts its native queued body");
        const auto aligned=Add(actualRender.position,NativeRigMath::Transform(actualRender.basis,headLocal));
        const auto torso=Cross(NativeRigMath::Transform(actualRender.basis,{-40,0,0}),{0,1,0});Vec3 unit;
        Check(std::fabs(aligned.x-stableEye.x)<.001f&&std::fabs(aligned.z-stableEye.z)<.001f&&
            actualRender.position.y==raw.position.y&&NativeArmMath::Unit(torso,unit)&&Dot(unit,camera.centre.basis.forward)>.999f,
            "native interaction keeps its body under the eye and facing the HMD with original height");
        Check(!std::memcmp(sourceBefore.data(),reinterpret_cast<void*>(cache+0x90),sourceBefore.size()),
            "native interaction alignment does not mutate source animation palette");
    }
    action.actionKind=NativeActionKL::Action::Throw;action.throwBack=false;action.explosionCover=false;
    Check(!NativeBodyKL::ActionFollowEligible(action,actor),"ordinary grenade throw does not inherit defensive throw-back body alignment");
    action.throwBack=false;
    // .44: a backward step off a ledge uses the actual owned Fall movement,
    // not Pickup or an external movie. Preserve its complete queued eye Y,
    // while the visible body faces and remains attached to the VR view.
    action.movementKind=NativeActionKL::Movement::Fall;action.action=0;action.actionKind=NativeActionKL::Action::None;
    unsigned dropStage=0;
    for(float turn:{1.5707963268f,3.1415926536f,-1.5707963268f}){
        const Quat rotation{0,std::sin(turn*.5f),0,std::cos(turn*.5f)};auto dropRoot=raw;
        dropRoot.basis={Rotate(rotation,raw.basis.forward),Rotate(rotation,raw.basis.up),Rotate(rotation,raw.basis.right)};
        dropRoot.position.y-=40.f*dropStage;Put(skinEntry+0x10,dropRoot);
        NativeHandsKL::lastOwnedEntryTick=GetTickCount();Vec3 dropEye;CameraPose queuedDropRoot;
        Check(NativeHandsKL::GpuHeadAnchor(actor,dropEye,100,true,nullptr,nullptr,&queuedDropRoot)&&Near(queuedDropRoot,dropRoot),
            "owned descent eye comes from the actual current native queued root/head, never CPU or third-person orbit");
        camera.pairId=58+dropStage;camera.generation=80+dropStage;camera.centre.position=dropEye;
        NativeHandsKL::publishedFrame=camera;NativeHandsKL::frameGeneration=camera.generation;NativeBodyKL::SetFrame(camera.centre,camera.pairId,100,true,7);
        Check(NativeHandsKL::FrameBodyRoot(actor,camera.pairId,actualRaw,actualRender)&&Near(actualRaw,dropRoot),
            "actual PrepareDraw gives owned Fall/None the same first-person body authority as accepted Pickup");
        const auto attached=Add(actualRender.position,NativeRigMath::Transform(actualRender.basis,headLocal));
        const auto torso=Cross(NativeRigMath::Transform(actualRender.basis,{-40,0,0}),{0,1,0});Vec3 unit;
        Check(Near(attached,dropEye)&&actualRender.position.y==dropRoot.position.y&&NativeArmMath::Unit(torso,unit)&&Dot(unit,camera.centre.basis.forward)>.999f,
            "90/180-degree backward-drop animation keeps visible head at full current eye and torso facing HMD, preserving actual descending rootY");
        ++dropStage;
    }
    Check(!std::memcmp(sourceBefore.data(),reinterpret_cast<void*>(cache+0x90),sourceBefore.size()),"descent body authority leaves accepted native source bones/palettes byte-identical");
    action.action=actor+0x2000;action.actionKind=NativeActionKL::Action::MiniAct;
    Check(!NativeBodyKL::ActionFollowEligible(action,actor),"authored action during Fall cannot inherit the physical no-action descent scope");
    action.action=0;action.actionKind=NativeActionKL::Action::None;action.movementKind=NativeActionKL::Movement::Die;
    Check(!NativeBodyKL::ActionFollowEligible(action,actor),"wounded Die is a separate native movement and keeps its accepted camera/body behavior");
    action.movementKind=NativeActionKL::Movement::Fall;
    Check(!NativeBodyKL::ActionFollowEligible(action,actor+4),"foreign player cannot borrow local descent body authority");
    Put(skinEntry+0x10,raw);
    action.movementKind=NativeActionKL::Movement::Rappel;camera.pairId=57;camera.generation=79;
    NativeHandsKL::publishedFrame=camera;NativeHandsKL::frameGeneration=camera.generation;NativeBodyKL::SetFrame(camera.centre,camera.pairId,100,true,7);
    Check(NativeHandsKL::FrameBodyRoot(actor,57,actualRaw,actualRender)&&Near(actualRaw,actualRender),"accepted Rappel keeps authored root instead of acquiring pickup body-follow authority");
    for(unsigned bone:{3u,4u}){float rows[12];NativeSkinMath::Palette(bind[bone],{bind[bone].basis,bind[bone].position},rows);
        std::memcpy(reinterpret_cast<void*>(cache+0x90+bone*sizeof(rows)),rows,sizeof(rows));}
    action={};camera=ordinaryCamera;NativeHandsKL::publishedFrame=camera;NativeHandsKL::frameGeneration=camera.generation;
    NativeBodyKL::ResetFollow();NativeBodyKL::SetFrame(camera.centre,camera.pairId,100,true,7);
    Check(NativeHandsKL::FrameBodyRoot(actor,55,actualRaw,actualRender),"ordinary staging restored after isolated physical-action regression");
    // Deliberately poison the ordinary same-eye visual cache. This fixture
    // must still carry gear with freshly staged actual body roots.
    NativeHandsKL::visual.player=actor;NativeHandsKL::visual.pairId=55;NativeHandsKL::visual.bodyAvailableForMesh=true;
    NativeHandsKL::visual.rawRoot=raw;NativeHandsKL::visual.renderRoot=raw;NativeHandsKL::visual.renderRoot.position.x+=999;NativeHandsKL::visualGeneration=77;
    nativeWorld=NativeRigMath::ToWorld(raw,{bind[5].basis,bind[5].position});CameraPose expected;
    Check(NativeWeaponMath::Remap(actualRaw,actualRender,nativeWorld,expected),"expected actual-body rigid WORLD");
    NativeWeaponKL::RigidBindHook(reinterpret_cast<void*>(rigidEntry),nullptr,reinterpret_cast<void*>(state),extra);
    Check(nativeRigidCalls==1&&nativeWorldCalls==1&&Near(renderedWorld,expected),"actual RigidBindHook carries named model hip part with current FrameBodyRoot instead of poisoned visual cache");
    Check(NativeWeaponKL::Snapshot().hipRigidTracked==1&&NativeWeaponKL::Snapshot().hipAccessorySlots==0,"runtime counters distinguish applied rigid model hip from absent GROUP slots");
    Check(Near(*reinterpret_cast<CameraPose*>(rigidEntry+0x10),raw)&&*reinterpret_cast<std::uint32_t*>(rootGeometry+0x60)==actor,"rigid source root and actor ownership stay native/read-only");
    // Native actor yaw changes inside one eye. The old visual frame remains
    // a valid-generation cache hit, while actual current roots must update.
    const Quat yaw{0,std::sin(.65f),0,std::cos(.65f)};raw.basis={Rotate(yaw,raw.basis.forward),Rotate(yaw,raw.basis.up),Rotate(yaw,raw.basis.right)};raw.position.x+=17;
    Put(skinEntry+0x10,raw);Put(rigidEntry+0x10,raw);
    Check(NativeHandsKL::FrameBodyRoot(actor,55,actualRaw,actualRender)&&Near(actualRaw,raw),"same-generation native root change cannot reuse the old cached source");
    nativeWorld=NativeRigMath::ToWorld(raw,{bind[5].basis,bind[5].position});Check(NativeWeaponMath::Remap(actualRaw,actualRender,nativeWorld,expected),"expected current-turn body remap");
    NativeWeaponKL::RigidBindHook(reinterpret_cast<void*>(rigidEntry),nullptr,reinterpret_cast<void*>(state),extra);
    Check(nativeWorldCalls==2&&Near(renderedWorld,expected),"actual rigid hook follows left/right-stick native yaw with the exact newly staged body delta");
    const char pelvisName[16]="PELVIS";Put(definitions+64+0x1C,pelvisName);Put(partResource+0x3C,std::uint32_t(1));
    nativeWorld=NativeRigMath::ToWorld(raw,{bind[1].basis,bind[1].position});Check(NativeWeaponMath::Remap(actualRaw,actualRender,nativeWorld,expected),"expected exact PELVIS rigid remap");
    NativeWeaponKL::RigidBindHook(reinterpret_cast<void*>(rigidEntry),nullptr,reinterpret_cast<void*>(state),extra);
    Check(nativeWorldCalls==3&&Near(renderedWorld,expected)&&NativeWeaponKL::Snapshot().hipRigidStandard==0&&NativeWeaponKL::Snapshot().hipRigidBone==1,"actual rigid hook carries exact named pelvis equipment and records standard0/model bone separately");
    Put(definitions+64+0x1C,hipName);NativeWeaponKL::RigidBindHook(reinterpret_cast<void*>(rigidEntry),nullptr,reinterpret_cast<void*>(state),extra);
    Check(nativeWorldCalls==3,"actual pelvis path rejects another bone name even at standard0");Put(definitions+64+0x1C,pelvisName);Put(partResource+0x3C,std::uint32_t(5));
    auto mismatch=raw;mismatch.position.z+=100;Put(rigidEntry+0x10,mismatch);
    NativeWeaponKL::RigidBindHook(reinterpret_cast<void*>(rigidEntry),nullptr,reinterpret_cast<void*>(state),extra);
    Check(nativeWorldCalls==3,"another instance root cannot borrow the primary body's transform");Put(rigidEntry+0x10,raw);
    Put(partResource+0x3C,std::uint32_t(3));NativeWeaponKL::RigidBindHook(reinterpret_cast<void*>(rigidEntry),nullptr,reinterpret_cast<void*>(state),extra);
    Check(nativeWorldCalls==3,"non-hip head/hand rigid parts remain native");Put(partResource+0x3C,std::uint32_t(5));
    Put(rootGeometry+0x60,at(0x12345));NativeWeaponKL::RigidBindHook(reinterpret_cast<void*>(rigidEntry),nullptr,reinterpret_cast<void*>(state),extra);
    Check(nativeWorldCalls==3,"foreign actor/cable geometry cannot receive local body correction");Put(rootGeometry+0x60,actor);
    NativeHandsKL::publishedFrame.valid=false;NativeWeaponKL::RigidBindHook(reinterpret_cast<void*>(rigidEntry),nullptr,reinterpret_cast<void*>(state),extra);
    Check(nativeWorldCalls==3,"retired eye generation cannot move costume equipment");NativeHandsKL::publishedFrame.valid=true;
    const CameraPose beforeRaw=actualRaw,beforeRender=actualRender;
    NativeHandsKL::preparedValid=false;HipRigidReadFixture::trigger=cache+0x90;HipRigidReadFixture::triggerSize=8*48;
    HipRigidReadFixture::target=skinEntry+0x108;HipRigidReadFixture::replacement=cache+0x1000;
    Check(!NativeHandsKL::FrameBodyRoot(actor,55,actualRaw,actualRender)&&!HipRigidReadFixture::trigger&&Near(actualRaw,beforeRaw)&&Near(actualRender,beforeRender),"actual FrameBodyRoot rejects late native cache replacement without publishing partial roots");Put(skinEntry+0x108,cache);
    NativeHandsKL::preparedValid=false;NativeHandsKL::lastOwnedEntryTick=GetTickCount()-1000;
    Check(!NativeHandsKL::FrameBodyRoot(actor,55,actualRaw,actualRender),"a stale owned body entry cannot authorize rigid costume movement");NativeHandsKL::lastOwnedEntryTick=GetTickCount();
    activeView=false;NativeWeaponKL::RigidBindHook(reinterpret_cast<void*>(rigidEntry),nullptr,reinterpret_cast<void*>(state),extra);
    Check(nativeWorldCalls==3,"non-VR views retain native rigid WORLD");
    activeView=true;
    const auto accessoryControl=at(0xC5000),accessoryGeometry=at(0xC6000),accessoryEntry=at(0xC7000);
    Put(accessoryControl,at(0x5462CC));Put(accessoryControl+4,accessoryGeometry);Put(accessoryGeometry+0x60,accessoryControl);Put(accessoryGeometry+0x50,rootGeometry);
    Put(accessoryEntry,at(0x540EE0));Put(accessoryEntry+0xF4,accessoryGeometry);
    const CameraPose beltLocal{raw.basis,{18,96,-9}};const auto beltWorld=NativeRigMath::ToWorld(raw,{beltLocal.basis,beltLocal.position});Put(accessoryEntry+0x10,beltWorld);
    Check(NativeHandsKL::FrameBodyRoot(actor,55,actualRaw,actualRender)&&NativeWeaponMath::Remap(actualRaw,actualRender,beltWorld,expected),"fresh actual body roots prepare an ordinary model-child accessory");
    NativeWeaponKL::heldCache={};NativeWeaponKL::negativeDrawMemo={};
    NativeWeaponKL::WorldBindHook(reinterpret_cast<void*>(state),nullptr,reinterpret_cast<const CameraPose*>(accessoryEntry+0x10));
    Check(NativeWeaponKL::Snapshot().bodyChildTracked==1&&Near(renderedWorld,expected),"actual generic WORLD binder carries exact local actor descendant without any rigid binder or attachment slot");
    Check(Near(*reinterpret_cast<const CameraPose*>(accessoryEntry+0x10),beltWorld),"ordinary native accessory source WORLD stays read-only");
    Put(accessoryControl,at(0x57A79C));NativeWeaponKL::negativeDrawMemo={};
    NativeWeaponKL::WorldBindHook(reinterpret_cast<void*>(state),nullptr,reinterpret_cast<const CameraPose*>(accessoryEntry+0x10));
    Check(NativeWeaponKL::Snapshot().bodyChildTracked==1&&Near(renderedWorld,beltWorld),"actual cable class explicitly keeps native WORLD even with a hero scene parent");Put(accessoryControl,at(0x5462CC));
    Put(accessoryGeometry+0x50,std::uint32_t(0));NativeWeaponKL::negativeDrawMemo={};
    NativeWeaponKL::WorldBindHook(reinterpret_cast<void*>(state),nullptr,reinterpret_cast<const CameraPose*>(accessoryEntry+0x10));
    Check(NativeWeaponKL::Snapshot().bodyChildTracked==1&&Near(renderedWorld,beltWorld),"detached/foreign scene model keeps native WORLD instead of following nearby player");
    // A native body GROUP can be a SCENE SIBLING of the actor. Its exact
    // generation-checked +88 record, not scene proximity, authorizes carry.
    const auto component=at(0xC9000),primitives=at(0xC8000),manager=at(0xD0000),pool=at(0xD1000),poolData=at(0xD2000),generations=at(0xD3000),bodyGroup=at(0xD4000),groupGeometry=poolData+4*0x70;
    Put(actor+0x88,component);Put(component+0x14,std::uint32_t(1));NativeWeaponOwnership::Record record;record.geometryRef=3*8+4+1;record.bone=3;Put(component+0x18,record);
    Put(base+0x6C99B8,manager);Put(manager+0xC,pool);Put(pool+4,std::uint32_t(0x70));Put(pool+8,std::uint32_t(8));Put(pool+0xC,poolData);Put(pool+0x10,generations);Put(generations+4*2,std::uint16_t(3));
    Put(groupGeometry+0x60,bodyGroup);Put(bodyGroup,at(0x5462CC));Put(bodyGroup+4,groupGeometry);Put(accessoryGeometry+0x50,groupGeometry);
    Put(header+0x14,std::uint32_t(15));Put(base+0x71F308+15*4,primitives);Put(primitives+3,std::uint8_t(2));
    const char spineName[32]="SPINE";Put(definitions+2*64+0x1C,spineName);Put(base+0x6318B0+3*8,std::uint32_t(3));Put(base+0x6318B4+3*8,at(0x538704));Put(base+0x538704,spineName);
    NativeWeaponKL::heldCache={};NativeWeaponKL::negativeDrawMemo={};
    Check(NativeWeaponKL::CurrentHeld().bodyGroup[0]==bodyGroup,"actual ownership cache discovers non-hip torso GROUP via native primitive/model name/topology");
    NativeWeaponKL::WorldBindHook(reinterpret_cast<void*>(state),nullptr,reinterpret_cast<const CameraPose*>(accessoryEntry+0x10));
    Check(NativeWeaponKL::Snapshot().bodyGroupTracked==1&&Near(renderedWorld,expected),"actual WORLD adapter carries an animated torso GROUP that is not in actor scene ancestry");
    Put(component+0x14,std::uint32_t(0));
    NativeWeaponKL::WorldBindHook(reinterpret_cast<void*>(state),nullptr,reinterpret_cast<const CameraPose*>(accessoryEntry+0x10));
    Check(NativeWeaponKL::Snapshot().bodyGroupTracked==1&&Near(renderedWorld,beltWorld),"native GROUP detachment retires cached draw authorization immediately");
    // Actual PrepareDraw + anatomical GPU source share one independently
    // named, current cargo pose. No renderer/HMD/native simulation is faked.
    constexpr unsigned cargoCount=15;
    const std::uint8_t cargoParents[cargoCount]={0,0,1,2,3,1,5,6,7,1,9,10,11,2,2};
    const Vec3 cargoPositions[cargoCount]={{},{0,95,0},{0,140,0},{0,160,0},{0,164,-8},{10,90,0},{10,48,-3},{10,7,0},{10,3,-15},
        {-10,90,0},{-10,48,-3},{-10,7,0},{-10,3,-15},{20,140,0},{-20,140,0}};
    NativeSkinMath::BindLocal cargoLocal[cargoCount];std::uint32_t cargoIds[cargoCount];CameraPose cargoBind[cargoCount];
    for(unsigned i=0;i<cargoCount;++i){cargoIds[i]=cargoParents[i];cargoLocal[i].position=i?Subtract(cargoPositions[i],cargoPositions[cargoParents[i]]):Vec3{};}
    Check(NativeSkinMath::BindPose(cargoLocal,cargoIds,cargoCount,cargoBind),"cargo test constructs actual current15-bone costume rest hierarchy");
    std::memcpy(reinterpret_cast<void*>(rest),cargoLocal,sizeof(cargoLocal));std::memcpy(reinterpret_cast<void*>(parents),cargoParents,sizeof(cargoParents));
    Put(model+0x4A0,std::uint32_t(cargoCount));Put(skinEntry+0x114,std::uint32_t(cargoCount));Put(header,std::uint32_t(cargoCount));
    Put(lookup+16,std::uint8_t(13));Put(lookup+22,std::uint8_t(14));
    const char* cargoNames[cargoCount]={"","PELVIS","SPINE","HEAD","EYE","LEFT_THIGH","LEFT_CALF","LEFT_FOOT","LEFT_TOE",
        "RIGHT_THIGH","RIGHT_CALF","RIGHT_FOOT","RIGHT_TOE","LEFT_SHOULDER","RIGHT_SHOULDER"};
    std::memset(reinterpret_cast<void*>(definitions),0,cargoCount*64);
    for(unsigned i=0;i<cargoCount;++i){std::memcpy(reinterpret_cast<void*>(definitions+i*64+0x1C),cargoNames[i],std::strlen(cargoNames[i])+1);
        float rows[12];Check(NativeSkinMath::Palette(cargoBind[i],{cargoBind[i].basis,cargoBind[i].position},rows),"cargo native palette is independent source");
        std::memcpy(reinterpret_cast<void*>(cache+0x90+i*sizeof(rows)),rows,sizeof(rows));}
    std::array<unsigned char,cargoCount*48> cargoSource{};std::memcpy(cargoSource.data(),reinterpret_cast<void*>(cache+0x90),cargoSource.size());
    action={};action.valid=true;action.player=actor;action.movement=actor+0x1000;action.movementKind=NativeActionKL::Movement::Vehicle;
    cargoMotion.active=true;cargoMotion.player=actor;cargoMotion.movement=action.movement;cargoMotion.carrier=actor+0x4000;
    cargoMotion.epoch=1;cargoMotion.recenter=7;cargoMotion.tick=GetTickCount();cargoMotion.crouchMetres=.45f;
    NativeHandsKL::SetPassengerMotionProvider(CargoMotion);NativeHandsKL::lastOwnedEntryTick=GetTickCount();
    camera=ordinaryCamera;camera.nativeBodyAction=true;camera.pairId=90;camera.generation=100;
    Check(NativeHandsKL::CaptureRigMetadata(model,actor,camera.rig),"cargo actual producer captures replacement current costume metadata");
    CameraPose cargoStable,cargoRoot;Vec3 cargoEye;
    Check(NativeHandsKL::GpuHeadAnchor(actor,cargoEye,100,true,nullptr,nullptr,&cargoRoot,&cargoStable,nullptr,nullptr,NativeHandsKL::StableHeadMode::Carried),"actual carried GPU anchor accepts certified cargo motion and named bones");
    Vec3 cargoExpectedEye;const NativePassengerPoseMath::Sample neutralSample{};
    Check(NativePassengerPoseMath::Eye(cargoBind,cargoCount,3,4,neutralSample,100,cargoExpectedEye)&&
        Near(cargoEye,Add(raw.position,NativeRigMath::Transform(raw.basis,cargoExpectedEye)))&&Near(cargoStable.position,cargoEye),
        "actual carried eye and stable source use new crouched body rather than old seated parent pose");
    camera.centre={cargoStable.basis,cargoEye};NativeHandsKL::publishedFrame=camera;NativeHandsKL::frameGeneration=camera.generation;
    NativeHandsKL::preparedValid=false;NativeHandsKL::PreparedDraw cargoDraw;
    Check(NativeHandsKL::PrepareDraw(camera,skinEntry,cargoDraw)&&cargoDraw.modified&&!cargoDraw.rootModified&&!cargoDraw.hidden,
        "actual PrepareDraw uploads crouched wholebody even without tracked wrists or head mask, leaving actual actor root intact");
    NativeRigMath::Bone cargoHead,cargoFoot;Vec3 preparedEye;
    Check(NativeSkinMath::PoseFromPalette(cargoBind[3],cargoDraw.palette[3].data(),cargoHead)&&
        NativeSkinMath::AnatomicalEye(cargoBind[3],cargoBind[4],cargoHead,100,preparedEye)&&Near(preparedEye,cargoExpectedEye)&&
        NativeSkinMath::PoseFromPalette(cargoBind[7],cargoDraw.palette[7].data(),cargoFoot)&&Near(cargoFoot.position,cargoBind[7].position),
        "actual rendered head exactly matches camera eye while crouched feet retain native floor height");
    const auto floorFoot=cargoFoot.position;const auto stillHead=cargoHead.position;
    cargoMotion.speed=.65f;cargoMotion.velocityMetres={0,0,-.65f};cargoMotion.phase=4.5f;cargoMotion.tick=GetTickCount();
    ++camera.generation;++camera.pairId;NativeHandsKL::publishedFrame=camera;NativeHandsKL::frameGeneration=camera.generation;NativeHandsKL::preparedValid=false;
    Check(NativeHandsKL::PrepareDraw(camera,skinEntry,cargoDraw)&&NativeSkinMath::PoseFromPalette(cargoBind[7],cargoDraw.palette[7].data(),cargoFoot)&&
        !Near(cargoFoot.position,floorFoot)&&NativeSkinMath::PoseFromPalette(cargoBind[3],cargoDraw.palette[3].data(),cargoHead)&&Near(cargoHead.position,stillHead),
        "actual walking staging moves lowerbody feet with distance phase and never bobs the VR eye");
    Check(!std::memcmp(cargoSource.data(),reinterpret_cast<void*>(cache+0x90),cargoSource.size()),"cargo draw and camera leave original native source palettes byte-identical");
    for(auto gesture:{NativeActionKL::Action::PutBack,NativeActionKL::Action::GetBack,NativeActionKL::Action::Command}){
        action.action=actor+0x2000;action.actionKind=gesture;++camera.generation;++camera.pairId;cargoMotion.tick=GetTickCount();
        NativeHandsKL::publishedFrame=camera;NativeHandsKL::frameGeneration=camera.generation;NativeHandsKL::preparedValid=false;
        Check(NativeHandsKL::PrepareDraw(camera,skinEntry,cargoDraw)&&cargoDraw.modified&&
            NativeHandsKL::GpuHeadAnchor(actor,cargoEye,100,true,nullptr,nullptr,nullptr,&cargoStable,nullptr,nullptr,NativeHandsKL::StableHeadMode::Carried)&&
            Near(cargoEye,Add(raw.position,NativeRigMath::Transform(raw.basis,cargoExpectedEye))),
            "native tracked item/command gesture preserves the cargo body and exact crouched eye instead of returning to seated pose");
    }
    action.actionKind=NativeActionKL::Action::MiniAct;++camera.generation;NativeHandsKL::publishedFrame=camera;NativeHandsKL::frameGeneration=camera.generation;NativeHandsKL::preparedValid=false;
    Check(NativeHandsKL::PrepareDraw(camera,skinEntry,cargoDraw)&&!cargoDraw.modified,
        "authored Vehicle MiniAct remains native and cannot inherit tracked item-gesture cargo pose authority");
    action.action=0;action.actionKind=NativeActionKL::Action::None;
    cargoMotion.active=false;++camera.generation;NativeHandsKL::publishedFrame=camera;NativeHandsKL::frameGeneration=camera.generation;NativeHandsKL::preparedValid=false;
    Check(NativeHandsKL::PrepareDraw(camera,skinEntry,cargoDraw)&&!cargoDraw.modified&&
        !std::memcmp(cargoDraw.palette.data(),cargoSource.data(),cargoSource.size()),"retired cargo certificate immediately restores native Vehicle pose without leaking crouch into entry/exit");
    NativeHandsKL::SetPassengerMotionProvider(nullptr);
    VirtualFree(allocation,0,MEM_RELEASE);
    std::printf("Production owned rigid hip + actual body staging: %u checks, %u failures.\n",checks,failures);return failures?1:0;
}
