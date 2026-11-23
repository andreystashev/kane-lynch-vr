// Real CPU anatomical-anchor adapter + production camera-heading/scene
// policies over private native-layout memory. No game, GPU or VR is opened.
#define main UnusedScopeImageReaderMain
#include "native_scope_kl_test.cpp"
#undef main
#include "native_player_anchor.cpp"
#include "native_camera_kl_math.hpp"
#include "native_cinematic_kl.hpp"
#include "native_skin_math.hpp"
namespace ActionCameraFixture {
using namespace NativeCameraMath;
unsigned count=0,failed=0;
void Test(bool okay,const char* why){++count;if(!okay){++failed;std::printf("FAIL %s\n",why);}}
template<class T>void Put(std::uintptr_t at,const T& value){std::memcpy(reinterpret_cast<void*>(at),&value,sizeof(value));}
bool Near(Vec3 a,Vec3 b){return NativeArmMath::Length(Subtract(a,b))<.001f;}
CameraPose Pose(float yaw,Vec3 p={}){return {{{std::sin(yaw),0,std::cos(yaw)},{0,1,0},{std::cos(yaw),0,-std::sin(yaw)}},p};}
}
int main(int argc,char** argv) {
    using namespace ActionCameraFixture;using namespace NativeCameraKLMath;
    static_assert(sizeof(void*)==4,"native player anchor adapter fixture is x86");
    if(argc<2)return 2;
    FileImage executable;Test(executable.Load(argv[1]),"actual supported executable opens read-only");
    auto* memory=VirtualAlloc(nullptr,0x900000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    Test(memory!=nullptr,"private native camera/actor layout allocated");if(!memory)return 1;
    const auto base=reinterpret_cast<std::uintptr_t>(memory);
    const auto at=[&](std::uintptr_t offset){return static_cast<std::uint32_t>(base+offset);};
    unsigned char getter[24]{};
    Test(executable.Read(0x400000+0x26FBD0,getter,sizeof(getter)),"native player getter signature comes from the actual PE");
    std::memcpy(reinterpret_cast<void*>(base+0x26FBD0),getter,sizeof(getter));
    Test(NativePlayerAnchor::InitializeVerifiedImage(base),"production player-anchor verifier accepts the pinned native getter");
    const auto data=at(0x10000),actor=at(0x20000),camera=at(0x30000),geometry=at(0x40000),model=at(0x50000),lookup=at(0x60000),poses=at(0x70000);
    Put(base+0x68760C,data);Put(data+0xA2C,actor);Put(actor,at(0x56582C));Put(actor+4,geometry);Put(actor+0x9A0,camera);Put(camera,at(0x545A3C));
    Put(actor+0x70,model);Put(model+0xC,lookup);Put(model+0x4A0,std::uint32_t(2));Put(model+0x4D0,poses);Put(lookup+64,std::uint8_t(1));
    Put(geometry+0x50,std::uint32_t(0));
    const auto system=at(0x80000),manager=at(0x81000);Put(base+0x6875F0,system);Put(system+0x96C,manager);Put(manager+0x300,std::uint32_t(0));Put(base+0x88E8B0,std::uint32_t(0));
    const auto reader=[](std::uintptr_t p,void* out,std::size_t n){SIZE_T got=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(p),out,n,&got)&&got==n;};
    FirstPersonHeading heading;CameraPose fpBase,accepted;
    const auto entry=Pose(0,{0,170,0});Test(FirstPersonBase(heading,actor,camera,true,entry,entry.position,fpBase,7),"production heading captures entry once");
    Quat relative;Vec3 displacement;const Quat physical{0,std::sin(.17f),0,std::cos(.17f)};
    Test(StereoRenderMath::ApplyVirtualYaw(physical,{},1.2f,relative,displacement)&&Compose(fpBase,relative,{}, {},100,accepted),"accepted world view contains existing physical and virtual turns");
    Vec3 unused;heading.worldGaze.Select(accepted,unused);heading.sourceState=at(0x82000);
    struct Example {NativeActionKL::Movement movement;NativeActionKL::Action action;Vec3 eye;};
    const Example examples[]={
        {NativeActionKL::Movement::Climb,NativeActionKL::Action::MiniAnim,{25,100,-12}},
        {NativeActionKL::Movement::Fall,NativeActionKL::Action::MiniAct,{45,15,-80}},
        {NativeActionKL::Movement::BaseMove,NativeActionKL::Action::Pickup,{-30,40,60}}};
    for(float yaw:{-1.5707963268f,1.5707963268f,3.1415926536f})for(const auto& example:examples) {
        const auto root=Pose(yaw,{1000,20,-300});Put(geometry,root);
        const auto eye=Pose(0,example.eye);Put(poses+48,eye);
        NativeActionKL::State action;action.valid=true;action.player=actor;action.movementKind=example.movement;action.action=at(0x83000);action.actionKind=example.action;
        const auto anchor=NativePlayerAnchor::Current(100,1700,action.FollowAnimatedHead());
        const auto expected=Add(root.position,NativePlayerPose::Transform(root.basis,example.eye));Vec3 gpuMath;
        Test(anchor.valid&&anchor.headBone&&anchor.animatedHead&&anchor.player==actor&&anchor.sourceCamera==camera&&Near(anchor.position,expected),
             "actual CPU anchor follows complete current low/offset anatomical attachment in rotated actor root");
        Test(NativeSkinMath::EyeAnchor(root,example.eye,true,gpuMath)&&Near(gpuMath,expected),
             "production GPU anatomical transform agrees with current full root/head coordinates without restoring standing feet origin");
        Test(!NativeCinematicKL::ActorCameraAction(action,actor)&&!NativeCinematicKL::ActorScenePanel(action,actor,false),
             "physical climb/fall/pickup cannot detach the eye as MiniAct/MiniAnim cinema");
        const auto remote=Pose(yaw+3.1415926536f,{9999,5,9999});CameraPose rendered;
        Test(FirstPersonBase(heading,actor,camera,true,remote,anchor.position,fpBase,7)&&
             Compose(fpBase,relative,{}, {},100,rendered)&&Near(rendered.position,expected)&&Near(rendered.basis.forward,accepted.basis.forward),
             "production native anchor and heading compose at current anatomy while rejecting native180-degree camera orbit");
    }
    {
        FirstPersonHeading authoredHeading;CameraPose scriptedBase;
        Test(FirstPersonBase(authoredHeading,actor,camera,true,entry,entry.position,scriptedBase,7),"authored source adapter starts from the accepted ordinary heading");
        Put(base+0x88E8B0,std::uint32_t(1));
        NativeActionKL::State action;action.valid=true;action.player=actor;action.action=at(0x83000);
        action.actionKind=NativeActionKL::Action::MiniAct;action.movementKind=NativeActionKL::Movement::BaseMove;
        for(unsigned step=0;step<=9;++step){
            Put(geometry,Pose(float(step)*.1745329252f,{float(step)*20,0,-300}));
            // The authored neck/head turns independently and much farther.
            // Current().root is the owner geometry, never that head basis.
            Put(poses+48,Pose(-float(step)*.43f,{0,170,0}));
            const auto anchor=NativePlayerAnchor::Current(100,1700,true);
            const auto scene=NativeCinematicKL::Snapshot(base,reader);
            const AuthoredHeadingInput sample{action.action,100+step,anchor.root.basis.forward};
            Test(anchor.valid&&anchor.animatedHead&&NativeCinematicKL::AuthoredActorHeading(scene,action,actor)&&
                 FirstPersonBase(authoredHeading,actor,camera,true,Pose(-2.f),anchor.position,scriptedBase,7,false,nullptr,&sample),
                 "actual anatomical adapter supplies the owned actor ROOT for deliberate authored heading, independently of moving neck and native camera");
            Test(Near(scriptedBase.position,anchor.position)&&
                 Dot(scriptedBase.basis.forward,Pose(step?float(step)*.1745329252f-.0034906585f:0).basis.forward)>.99999f,
                 "actual source follows each body turn and anatomical position while ignoring unrelated head-bone sway");
        }
        const auto final=scriptedBase;Put(base+0x88E8B0,std::uint32_t(0));
        Test(!NativeCinematicKL::AuthoredActorHeading(NativeCinematicKL::Snapshot(base,reader),action,actor)&&
             FirstPersonBase(authoredHeading,actor,camera,true,Pose(-2.7f),{5,170,6},scriptedBase,7)&&
             Near(scriptedBase.basis.forward,final.basis.forward),"native mini counter exit preserves the final authored heading in ordinary gameplay");
    }
    const auto old=heading;Put(poses+48,Pose(0,{0,std::numeric_limits<float>::quiet_NaN(),0}));
    const auto unavailable=NativePlayerAnchor::Current(100,1700,true);
    Test(unavailable.valid&&!unavailable.animatedHead&&!unavailable.headBone,
         "missing animation returns explicitly unproved fallback; provider must reject it as anatomical action eye");
    HeadingRetentionContext context{actor,camera,heading.sourceState,7,1020,1000,true,false,true,true,true};
    RetireFirstPersonHeading(heading,RetainPhysicalHeading(heading,context));
    Test(heading.valid&&Near(heading.basis.forward,old.basis.forward),"temporary rejected anatomical eye keeps only the separate stable yaw history");
    Put(manager+0x300,std::uint32_t(1));const auto full=NativeCinematicKL::Snapshot(base,reader);
    context.uiOrCinema=full.PreserveNative();RetireFirstPersonHeading(heading,RetainPhysicalHeading(heading,context));
    Test(full.valid&&full.full==1&&!heading.valid,"actual native full-cut counter retires retained physical-action yaw");
    Put(base+0x26FBD0,std::uint8_t(0));
    Test(!NativePlayerAnchor::InitializeVerifiedImage(base)&&!NativePlayerAnchor::Current(100,1700,true).valid,"changed native getter cannot retain a stale anatomical source");
    VirtualFree(memory,0,MEM_RELEASE);
    std::printf("KL1 production anatomical action camera: %u checks, %u failures.\n",count,failed);return failed?1:0;
}
