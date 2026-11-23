// Actual GpuHeadAnchor over private native-layout memory. Only unrelated
// native/runtime entry points and the clock are fixtures; no game/HMD/input.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
namespace StanceEyeFixture {
inline std::uintptr_t trigger=0;inline void(*afterRead)()=nullptr;
inline BOOL WINAPI Read(HANDLE process,LPCVOID source,LPVOID output,SIZE_T size,SIZE_T* copied) {
    const auto result=::ReadProcessMemory(process,source,output,size,copied);
    if(result&&trigger==reinterpret_cast<std::uintptr_t>(source)&&afterRead){trigger=0;afterRead();}
    return result;
}
}
#define ReadProcessMemory StanceEyeFixture::Read
#define main UnusedTwoHandStanceEyeMain
#include "native_two_hand_production_test.cpp"
#undef main
#undef ReadProcessMemory

int main() {
    using namespace NativeCameraMath;
    using namespace NativeHandsKL;
    constexpr std::uintptr_t base=0x2A000000;
    auto* memory=VirtualAlloc(reinterpret_cast<void*>(base),0x900000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    Check(memory==reinterpret_cast<void*>(base),"private native stance-eye layout allocated");if(!memory)return 1;
    constexpr std::uintptr_t data=base+0x10000,player=base+0x20000,geometry=base+0x30000,model=base+0x40000,
        cache=base+0x50000,entry=base+0x60000,lookup=base+0x70000,parents=base+0x71000,rest=base+0x72000,controller=base+0x73000;
    const auto put=[](std::uintptr_t at,const auto& value){std::memcpy(reinterpret_cast<void*>(at),&value,sizeof(value));};
    const auto nearEye=[](Vec3 a,Vec3 b){return NativeArmMath::Length(Subtract(a,b))<.0001f;};
    image=base;installed=true;
    put(base+kGameData,data);put(data+0xA2C,player);put(player,base+kPlayerVtable);put(player+0x70,model);
    put(entry,base+kBonesVtable);put(entry+0xF4,geometry);put(geometry+0x60,player);put(entry+0x108,cache);
    put(entry+0x114,8u);put(model+0x4A0,8u);put(model+0xC,lookup);put(model+4,rest);put(model+0x10,parents);
    const std::uint8_t parentBytes[]={0,0,1,2,3,1,1,1};std::memcpy(reinterpret_cast<void*>(parents),parentBytes,sizeof(parentBytes));
    put(lookup+0,std::uint8_t(1));put(lookup+6,std::uint8_t(3));put(lookup+64,std::uint8_t(4));
    put(player+0x9B8,controller);put(controller+0x18,std::uint8_t(0));
    NativeSkinMath::BindLocal local[8];local[1].position={0,90,0};local[2].position={0,50,0};
    local[3].position={0,15,0};local[4].position={0,4,-8};
    std::memcpy(reinterpret_cast<void*>(rest),local,sizeof(local));
    const std::uint32_t parentIds[]={0,0,1,2,3,1,1,1};CameraPose bind[8];
    Check(NativeSkinMath::BindPose(local,parentIds,8,bind),"actual costume bind hierarchy established");
    auto root=Frame();root.position={321,1200,-765};
    put(entry+0x10,root);put(entry+0x40,root);put(base+kGpuPoseIndex,0u);
    lastOwnedEntry=entry;lastOwnedModel=model;lastOwnedEntryTick=Fixture::Clock();
    const auto upload=[&](unsigned index,Vec3 position){float rows[12];
        Check(NativeSkinMath::Palette(bind[index],{bind[index].basis,position},rows),"native palette fixture generated");
        std::memcpy(reinterpret_cast<void*>(cache+0x90+index*sizeof(rows)),rows,sizeof(rows));};
    actionFixture={};actionFixture.valid=true;actionFixture.player=player;actionFixture.movement=base+0x74000;
    actionFixture.movementId=2;actionFixture.movementKind=NativeActionKL::Movement::Stand;
    const auto inputs=[&](float forward,float right,std::uint32_t flags){put(player+0x96C,forward);put(player+0x970,right);put(player+0x400,flags);};
    inputs(0,0,0);upload(1,bind[1].position);upload(3,bind[3].position);
    publishedFrame={};publishedFrame.valid=true;publishedFrame.centre=root;publishedFrame.pairId=49;
    publishedFrame.rawHeadPosition[0]=.15f;publishedFrame.rawHeadOrientation[3]=1;
    const auto frameCopy=publishedFrame;const auto generation=frameGeneration.load();
    Vec3 idle,shot,eye;CameraPose actualRoot;
    const Vec3 expectedIdle{root.position.x,root.position.y+bind[4].position.y,root.position.z};
    Check(GpuHeadAnchor(player,idle,100,false,nullptr,nullptr,&actualRoot)&&nearEye(idle,expectedIdle)&&
          !std::memcmp(&actualRoot,&root,sizeof(root)),"stationary bind idle retains accepted ordinary eye baseline and exact native root");
    upload(1,Add(bind[1].position,{0,-12,0}));upload(3,Add(bind[3].position,{7,-12,11}));
    std::array<unsigned char,0x90+8*48> paletteCopy{};std::memcpy(paletteCopy.data(),reinterpret_cast<void*>(cache),paletteCopy.size());
    Check(GpuHeadAnchor(player,shot,100,false)&&nearEye(shot,idle)&&shot.x==idle.x&&shot.z==idle.z,
          "actual first-shot pelvis drop is omitted only from stationary standing eye Y, retaining accepted X/Z");
    const auto expectedNative=Add(expectedIdle,{0,-12,0});
    for(auto movement:{NativeActionKL::Movement::Stand,NativeActionKL::Movement::BaseMove}) {
        actionFixture.movementKind=movement;
        inputs(.6f,0,0);Check(GpuHeadAnchor(player,eye,100,false)&&nearEye(eye,expectedNative),"walking retains exact existing animated pelvis height");
        inputs(0,-.6f,0);Check(GpuHeadAnchor(player,eye,100,false)&&nearEye(eye,expectedNative),"strafing retains exact existing animated pelvis height");
        inputs(0,0,0x400);Check(GpuHeadAnchor(player,eye,100,false)&&nearEye(eye,expectedNative),"deliberate stationary crouch retains native lowered eye height");
    }
    inputs(0,0,0);put(controller+0x18,std::uint8_t(1));
    Check(GpuHeadAnchor(player,eye,100,false)&&nearEye(eye,expectedNative),"blocked controller cannot authorize the neutral standing eye");
    put(controller+0x18,std::uint8_t(0));
    Check(GpuHeadAnchor(player,eye,100,false)&&nearEye(eye,idle),"stationary BaseMove uses the same proved predicate as stationary Stand");
    Check(!std::memcmp(paletteCopy.data(),reinterpret_cast<void*>(cache),paletteCopy.size()),
          "stationary/walking/crouch eye consumers leave native source palette untouched");
    // Authored action/cover policies use AnatomicalEye as before; this pose
    // is deliberately different from the pelvis-only ordinary height.
    upload(3,Add(bind[3].position,{7,-23,11}));const auto authored=Add(expectedIdle,{0,-23,0});
    std::memcpy(paletteCopy.data(),reinterpret_cast<void*>(cache),paletteCopy.size());
    for(auto kind:{NativeActionKL::Action::Reload,NativeActionKL::Action::Pickup,NativeActionKL::Action::CloseCombat}) {
        actionFixture.action=base+0x75000;actionFixture.actionKind=kind;
        Check(GpuHeadAnchor(player,eye,100,false)&&nearEye(eye,authored),"native reload/pickup/melee action eye is unchanged");
    }
    actionFixture.action=0;actionFixture.actionKind=NativeActionKL::Action::None;actionFixture.movementKind=NativeActionKL::Movement::Cover;
    Check(GpuHeadAnchor(player,eye,100,false)&&nearEye(eye,authored),"cover retains complete native anatomical eye height");
    actionFixture.movementKind=NativeActionKL::Movement::Vehicle;
    Vec3 anatomical;Check(NativeSkinMath::AnatomicalEye(bind[3],bind[4],{bind[3].basis,Add(bind[3].position,{7,-23,11})},100,anatomical),"authored anatomical expected attachment");
    const auto carriedExpected=NativeRigMath::ToWorld(root,{root.basis,anatomical}).position;
    Check(GpuHeadAnchor(player,eye,100,true)&&nearEye(eye,carriedExpected),"animated carried eye retains complete existing native X/Y/Z attachment");
    actionFixture.movementKind=NativeActionKL::Movement::Stand;
    StanceEyeFixture::trigger=player+0x96C;
    StanceEyeFixture::afterRead=[](){actionFixture.action=1;actionFixture.actionKind=NativeActionKL::Action::Reload;};
    Check(GpuHeadAnchor(player,eye,100,false)&&nearEye(eye,expectedNative),
          "native action replacement during input read cannot authorize stationary eye neutralization from an earlier snapshot");
    actionFixture.action=0;actionFixture.actionKind=NativeActionKL::Action::None;
    actionFixture.movementKind=NativeActionKL::Movement::Stand;actionFixture.player=player+4;
    Check(GpuHeadAnchor(player,eye,100,false)&&nearEye(eye,authored),"foreign action player cannot borrow stationary eye policy");
    actionFixture.player=player;
    Check(!std::memcmp(paletteCopy.data(),reinterpret_cast<void*>(cache),paletteCopy.size())&&
          !std::memcmp(&publishedFrame,&frameCopy,sizeof(frameCopy))&&frameGeneration.load()==generation,
          "actual eye adapter never writes native palette or cameraFrame/generation");
    Check(neutralStandingEyeFrames==3,"only bind idle, stationary shot and stationary BaseMove reached the new eye counter");
    installed=false;VirtualFree(memory,0,MEM_RELEASE);
    std::printf("KL1 production stationary eye: %u checks %s.\n",checks,failures?"FAILED":"PASS");return failures?1:0;
}
