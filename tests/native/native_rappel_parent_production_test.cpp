// Actual GPU anatomical-head adapter over private low-address native-layout
// memory. No game, graphics device, input, runtime or hooks are opened.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstring>
namespace RappelReadFixture {
inline std::uintptr_t trigger=0,target=0;inline std::uint32_t replacement=0;
inline BOOL WINAPI Read(HANDLE process,LPCVOID source,LPVOID output,SIZE_T size,SIZE_T* copied) {
    const auto result=::ReadProcessMemory(process,source,output,size,copied);
    if(result&&trigger==reinterpret_cast<std::uintptr_t>(source)&&size==48) {
        std::memcpy(reinterpret_cast<void*>(target),&replacement,4);trigger=0;
    }
    return result;
}
}
#define ReadProcessMemory RappelReadFixture::Read
#include "native_hands_kl.cpp"
#include "native_carried_camera_math.hpp"
#undef ReadProcessMemory
#include <cstdio>
#include <cstdlib>
#include <limits>
namespace {
unsigned checks=0;
void Check(bool okay,const char* why){++checks;if(!okay){std::printf("FAIL %s\n",why);std::exit(1);}}
[[noreturn]]void Unexpected(){std::puts("FAIL unrelated game/runtime/hook entry");std::abort();}
template<class T>void Put(std::uintptr_t at,const T& value){std::memcpy(reinterpret_cast<void*>(at),&value,sizeof(value));}
bool Near(NativeCameraMath::Vec3 a,NativeCameraMath::Vec3 b){const auto d=NativeCameraMath::Subtract(a,b);return NativeCameraMath::Dot(d,d)<.0001f;}
}
namespace NativeWeaponKL {
bool CaptureHeldSupport(std::uintptr_t,float,HeldSupport&,bool){Unexpected();}
bool PreferReloadSlide(const HeldSupport&,const NativeCameraMath::CameraPose&,const NativeCameraMath::CameraPose&,const NativeCameraMath::CameraPose*,std::uint32_t){Unexpected();}
bool ReloadHandBusy(std::uintptr_t){Unexpected();}
bool ReloadHandTarget(std::uintptr_t,unsigned,const NativeCameraMath::CameraPose&,NativeCameraMath::CameraPose&){Unexpected();}
}
namespace VRBridge {bool TeamHudVisible(){Unexpected();}bool WeaponMenuVisible(){Unexpected();}bool Ready(){Unexpected();}}
namespace NativeActionKL {
bool InitializeVerifiedImage(std::uintptr_t){Unexpected();}State Current(){Unexpected();}bool PreserveRig(){Unexpected();}
}
namespace NativeCameraKL {bool CinematicActive(){Unexpected();}bool DrawStateActive(std::uintptr_t,bool*,bool){Unexpected();}}
namespace NativeBodyKL {
void ResetFollow(){Unexpected();}
bool AdjustAction(std::uintptr_t,const NativeActionKL::State&,const NativeCameraMath::CameraPose&,NativeCameraMath::Vec3,
                  NativeCameraMath::Vec3,NativeCameraMath::Vec3,NativeCameraMath::CameraPose&,std::uint64_t){Unexpected();}
bool Adjust(std::uintptr_t,const NativeCameraMath::CameraPose&,NativeCameraMath::Vec3,NativeCameraMath::Vec3,
            NativeCameraMath::Vec3,NativeCameraMath::CameraPose&,std::uint64_t){Unexpected();}
}
extern "C" {
MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*){Unexpected();}
MH_STATUS WINAPI MH_QueueEnableHook(LPVOID){Unexpected();}MH_STATUS WINAPI MH_ApplyQueued(){Unexpected();}
MH_STATUS WINAPI MH_DisableHook(LPVOID){Unexpected();}MH_STATUS WINAPI MH_RemoveHook(LPVOID){Unexpected();}
}
int main() {
    using namespace NativeHandsKL;using namespace NativeCameraMath;
    constexpr std::uintptr_t base=0x28000000;
    auto* memory=VirtualAlloc(reinterpret_cast<void*>(base),0x900000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    Check(memory==reinterpret_cast<void*>(base),"private native-layout allocation");
    image=base;installed=true;
    constexpr std::uintptr_t data=base+0x10000,player=base+0x20000,geometry=base+0x30000,model=base+0x40000,
        cache=base+0x50000,entry=base+0x60000,lookup=base+0x70000,parents=base+0x71000,rest=base+0x72000;
    Put(base+kGameData,data);Put(data+0xA2C,player);Put(player,base+kPlayerVtable);Put(player+0x70,model);
    Put(entry,base+kBonesVtable);Put(entry+0xF4,geometry);Put(geometry+0x60,player);Put(entry+0x108,cache);
    Put(entry+0x114,std::uint32_t(8));Put(model+0x4A0,std::uint32_t(8));Put(model+0xC,lookup);Put(model+4,rest);Put(model+0x10,parents);
    const std::uint8_t parentBytes[]={0,0,1,2,3,1,1,1};std::memcpy(reinterpret_cast<void*>(parents),parentBytes,sizeof(parentBytes));
    Put(lookup+0,std::uint8_t(1));Put(lookup+6,std::uint8_t(3));Put(lookup+64,std::uint8_t(4));
    Put(lookup+16,std::uint8_t(6));Put(lookup+22,std::uint8_t(7));
    NativeSkinMath::BindLocal local[8];local[1].position={0,90,0};local[2].position={0,50,0};local[3].position={0,15,0};local[4].position={0,4,-8};local[6].position={20,50,0};local[7].position={-20,50,0};
    std::memcpy(reinterpret_cast<void*>(rest),local,sizeof(local));
    std::uint32_t parentIds[]={0,0,1,2,3,1,1,1};CameraPose bind[8];
    Check(NativeSkinMath::BindPose(local,parentIds,8,bind),"private costume builds the actual anatomical bind chain");
    CameraPose root;root.basis={{0,0,1},{0,1,0},{1,0,0}};root.position={300,1200,-900};
    Put(entry+0x10,root);Put(entry+0x40,root);Put(base+kGpuPoseIndex,std::uint32_t(0));
    lastOwnedEntry=entry;lastOwnedModel=model;lastOwnedEntryTick=GetTickCount();
    const float a=54.f*3.14159265358979323846f/360.f;Quat tilt{std::sin(a),0,0,std::cos(a)};
    NativeRigMath::Bone parent{{Rotate(tilt,root.basis.forward),Rotate(tilt,root.basis.up),Rotate(tilt,root.basis.right)},{5,45,200}};
    NativeRigMath::Bone head{parent.basis,Add(parent.position,{12,30,40})};
    const auto upload=[&](unsigned index,const NativeRigMath::Bone& pose){float rows[12];Check(NativeSkinMath::Palette(bind[index],pose,rows),"actual skin-palette preparation");std::memcpy(reinterpret_cast<void*>(cache+0x90+index*sizeof(rows)),rows,sizeof(rows));};
    NativeRigMath::Bone pelvis{root.basis,{5,45,70}},leftShoulder{root.basis,{25,45,130}},rightShoulder{root.basis,{-15,45,130}};
    upload(1,pelvis);upload(2,parent);upload(3,head);upload(6,leftShoulder);upload(7,rightShoulder);
    Vec3 raw,up,forward,towardHead;CameraPose observedRoot,stable;unsigned phase=0;
    Check(GpuHeadAnchor(player,raw,100.f,true,&up,&forward,&observedRoot,&stable,&phase,&towardHead)&&ValidatePose(stable)&&phase==2,"actual GpuHeadAnchor returns independent proven parent-based world eye and horizontal native torso phase");
    Check(Near(towardHead,{0,0,1}),"actual parent/pelvis GPU palettes prove WORLD direction toward the character's head");
    Check(Snapshot().rappelTorsoApplied==1&&Dot(stable.basis.forward,{0,1,0})>.999f,"actual original torso/shoulder palettes replace the54degree neck gaze with the body's straight face-up plane");
    RappelLookMath::Attachment orientation;CameraPose oriented;bool haveBasis=false;
    Check(orientation.Select(observedRoot,raw,&stable.basis,player,123,456,0,oriented,haveBasis,&stable,phase,&towardHead)&&orientation.anatomicalUpLocked&&
          Dot(oriented.basis.up,towardHead)>.999f&&Dot(oriented.basis.forward,{0,1,0})>.999f,"production parent-eye feeds the settled view: screen top toward head/city, bottom toward feet, forward remains upward");
    CameraPose expected;
    Check(RappelLookMath::ParentHead(root,bind[2],bind[3],bind[4],parent,100.f,expected)&&Near(expected.position,stable.position),"adapter selects the current GPU parent rather than the actor-root entry offset");
    Check(!Near(raw,stable.position)&&Dot(stable.basis.forward,{0,1,0})>.999f,"head-local animated displacement is removed while horizontal authored face looks upward");
    const auto first=stable;head.position=Add(head.position,{30,-25,80});upload(3,head);
    Check(GpuHeadAnchor(player,raw,100.f,true,&up,&forward,&observedRoot,&stable,&phase)&&Near(first.position,stable.position)&&phase==2,"same-parent animated head sway does not move the stable camera or change torso phase");
    parent.position.y-=150;pelvis.position.y-=150;upload(1,pelvis);upload(2,parent);
    Check(GpuHeadAnchor(player,raw,100.f,true,&up,&forward,&observedRoot,&stable,&phase)&&Near(stable.position,Add(first.position,{0,-150,0}))&&phase==2,"current parent descent moves the stable eye immediately without a stale latch");
    const auto beforeCarried=Snapshot().rappelTorsoApplied;
    CameraPose carried;unsigned carriedPhase=99;
    Check(GpuHeadAnchor(player,raw,100.f,true,nullptr,nullptr,&observedRoot,&carried,&carriedPhase,nullptr,StableHeadMode::Carried)&&
          ValidatePose(carried)&&carriedPhase==0&&Snapshot().rappelTorsoApplied==beforeCarried,
          "actual carried GPU adapter follows current parent but grants no rappel phase/torso certificate");
    Check(Near(carried.position,stable.position)&&Dot(carried.basis.forward,{0,1,0})<.95f,
          "carried adapter preserves the accepted anatomical eye without applying rappel face-up orientation");
    const auto firstSeat=carried;
    NativeCarriedCamera::State passenger;NativeCarriedCamera::Reference actor{observedRoot,player,1};CameraPose passengerEye;
    Check(passenger.Select(actor,carried,player,player+4,player+8,passengerEye),"actual carried palette source seeds the passenger producer");
    auto lowerParent=parent;lowerParent.position.y-=120;upload(2,lowerParent);
    Check(GpuHeadAnchor(player,raw,100.f,true,nullptr,nullptr,&observedRoot,&carried,nullptr,nullptr,StableHeadMode::Carried)&&
          Near(carried.position,Add(firstSeat.position,{0,-120,0})),"actual carried GPU source follows current authored seated parent translation rather than standing bind height");
    Check(passenger.Select(actor,carried,player,player+4,player+8,passengerEye)&&Near(passengerEye.position,carried.position),
          "actual current parent palette reaches passenger eye without the old entry-height position latch");upload(2,parent);
    auto movingRoot=root;movingRoot.position.x+=50;Put(entry+0x10,movingRoot);
    Check(GpuHeadAnchor(player,raw,100.f,true,nullptr,nullptr,&observedRoot,&carried,nullptr,nullptr,StableHeadMode::Carried)&&
          Near(carried.position,Add(firstSeat.position,{50,0,0})),"next GPU eye follows current moving carrier without retaining the first-eye centre");Put(entry+0x10,root);
    RappelReadFixture::trigger=cache+0x90+sizeof(float)*12*2;RappelReadFixture::target=data+0xA2C;RappelReadFixture::replacement=player+0x1000;
    const auto beforeForeignHead=raw;const auto beforeForeignRoot=observedRoot;
    Check(!GpuHeadAnchor(player,raw,100.f,true,nullptr,nullptr,&observedRoot,&carried,nullptr,nullptr,StableHeadMode::Carried)&&
          Near(raw,beforeForeignHead)&&!std::memcmp(&observedRoot,&beforeForeignRoot,sizeof(observedRoot))&&
          !ValidatePose(carried)&&!RappelReadFixture::trigger,"late actor replacement rejects the whole queued eye without publishing an old current-head fallback");Put(data+0xA2C,player);
    RappelReadFixture::trigger=cache+0x90+sizeof(float)*12*2;RappelReadFixture::target=entry+0x10+offsetof(CameraPose,position);RappelReadFixture::replacement=0;
    Check(!GpuHeadAnchor(player,raw,100.f,true,nullptr,nullptr,&observedRoot,&carried,nullptr,nullptr,StableHeadMode::Carried)&&
          Near(raw,beforeForeignHead)&&!ValidatePose(carried)&&!RappelReadFixture::trigger,
          "queued actor root advancing during head reads cannot publish a mixed-time passenger eye");Put(entry+0x10,root);
    RappelReadFixture::trigger=cache+0x90+sizeof(float)*12*2;RappelReadFixture::target=cache+0x90+sizeof(float)*12*3;RappelReadFixture::replacement=0x7FC00000;
    Check(!GpuHeadAnchor(player,raw,100.f,true,nullptr,nullptr,&observedRoot,&carried,nullptr,nullptr,StableHeadMode::Carried)&&
          Near(raw,beforeForeignHead)&&!ValidatePose(carried)&&!RappelReadFixture::trigger,
          "head palette replacement retires a mixed queued sample even when entry/cache pointers match");upload(3,head);
    auto standingPelvis=pelvis;standingPelvis.position={parent.position.x,parent.position.y-90,parent.position.z};upload(1,standingPelvis);
    Check(GpuHeadAnchor(player,raw,100.f,true,&up,&forward,&observedRoot,&stable,&phase)&&phase==1,"actual vertical pelvis-to-parent palette proves entrance independently of gaze");
    Put(cache+0x90+sizeof(float)*12,std::numeric_limits<float>::quiet_NaN());
    Check(GpuHeadAnchor(player,raw,100.f,true,&up,&forward,&observedRoot,&stable,&phase)&&ValidatePose(stable)&&phase==0,"bad pelvis palette grants no ongoing entrance authority while preserving the accepted anatomical eye");
    upload(1,pelvis);
    RappelReadFixture::trigger=cache+0x90+sizeof(float)*12;RappelReadFixture::target=entry+0x108;RappelReadFixture::replacement=cache+0x1000;
    Check(!GpuHeadAnchor(player,raw,100.f,true,&up,&forward,&observedRoot,&stable,&phase,&towardHead)&&!ValidatePose(stable)&&phase==0&&Near(towardHead,{})&&!RappelReadFixture::trigger,"late native cache replacement rejects the complete queued eye as well as parent/phase authority");Put(entry+0x108,cache);
    Put(cache+0x90+sizeof(float)*12*2,std::numeric_limits<float>::quiet_NaN());
    Check(GpuHeadAnchor(player,raw,100.f,true,&up,&forward,&observedRoot,&stable,&phase)&&!ValidatePose(stable)&&phase==0,"bad parent palette retains the valid current anatomical eye but retires optional stable proof");
    upload(2,parent);Put(geometry+0x60,player+4);
    Check(!GpuHeadAnchor(player,raw,100.f,true,&up,&forward,&observedRoot,&stable)&&!ValidatePose(stable),"foreign geometry cannot authorize a parent-based eye");
    Put(geometry+0x60,player);lastOwnedEntryTick=GetTickCount()-1000;
    Check(!GpuHeadAnchor(player,raw,100.f,true,&up,&forward,&observedRoot,&stable)&&!ValidatePose(stable),"stale entry fails without retaining last frame's stable eye");
    VirtualFree(memory,0,MEM_RELEASE);
    std::printf("Production rappel parent-eye adapter: %u checks PASS; private native-layout memory only.\n",checks);return 0;
}
