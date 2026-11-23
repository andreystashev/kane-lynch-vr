// Actual GPU body policy over private native-layout input memory. No game,
// hooks, GPU, headset, saves or input events are opened.
#define main UnusedTwoHandProductionMain
#include "native_two_hand_production_test.cpp"
#undef main
#include <limits>

int main() {
    using namespace NativeCameraMath;
    using namespace NativeHandsKL;
    auto* memory=VirtualAlloc(nullptr,0x3000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    Check(memory!=nullptr,"private native stance/input layout allocated");if(!memory)return 1;
    const auto player=reinterpret_cast<std::uintptr_t>(memory),controller=player+0x2000;
    const auto put=[&](std::uintptr_t at,const auto& value){std::memcpy(reinterpret_cast<void*>(at),&value,sizeof(value));};
    put(player+0x9B8,static_cast<std::uint32_t>(controller));put(controller+0x18,std::uint8_t(0));
    CameraFrame camera;camera.firstPerson=true;camera.rig.player=player;camera.rig.count=8;
    camera.rig.lookup[0]=1;camera.rig.head=4;camera.centre=Frame();camera.centre.position={10,170,20};
    const std::uint32_t parents[]={0,0,1,2,3,2,1,6};
    std::memcpy(camera.rig.parents.data(),parents,sizeof(parents));
    camera.hands.present=camera.hands.mounted=1;camera.hands.tick=Fixture::Clock();
    camera.hands.head.flags=HandsIPC::kOrientation|HandsIPC::kPosition;camera.hands.head.orientation[3]=1;
    std::array<CameraPose,8> bind{};std::array<Bone,8> source{};
    std::array<std::uint8_t,8> recovered{};
    for(unsigned i=0;i<8;++i){bind[i].position={0,float(i)*20,0};source[i]={bind[i].basis,bind[i].position};recovered[i]=1;}
    // Native first-shot pose: pelvis drops, spine leans, one leg steps.
    source[1].position.y-=12;source[3].position.x=22;source[4].position={26,70,8};source[6].position.x=35;
    const auto nativeCopy=source;const auto cameraCopy=camera;
    NativeActionKL::State action;action.valid=true;action.player=player;action.movementKind=NativeActionKL::Movement::Stand;
    auto apply=[&](float forward,float right,std::uint32_t input) {
        put(player+0x96C,forward);put(player+0x970,right);put(player+0x400,input);
        actionFixture=action;
        auto pose=source;std::array<std::uint8_t,8> changed{};
        const bool okay=StabilizeGameplayPose(camera,action,bind.data(),source.data(),pose.data(),8,changed.data(),recovered.data());
        return std::make_pair(okay,pose);
    };
    const auto neutral=apply(0,0,0);
    Check(neutral.first&&neutral.second[1].position.y==bind[1].position.y&&neutral.second[6].position.x==0&&
          neutral.second[4].position.y==bind[4].position.y,
          "actual stationary GPU body path removes first-shot pelvis drop, stepped leg and spine lean together");
    Check(!std::memcmp(&camera,&cameraCopy,sizeof(camera))&&!std::memcmp(source.data(),nativeCopy.data(),sizeof(source)),
          "accepted ordinary camera and all native gameplay bones remain byte-identical");
    for(auto movement:{NativeActionKL::Movement::Stand,NativeActionKL::Movement::BaseMove}) {
        action.movementKind=movement;const auto walking=apply(.6f,0,0),strafing=apply(0,-.5f,0),crouching=apply(0,0,0x400);
        for(const auto& result:{walking,strafing,crouching})Check(result.first&&result.second[1].position.y==source[1].position.y&&
            result.second[6].position.x==source[6].position.x,
            "walking, strafing and deliberate crouch preserve authored pelvis/leg animation in the production policy");
    }
    action.movementKind=NativeActionKL::Movement::Cover;
    Check(apply(0,0,0).second[1].position.y==source[1].position.y,"cover body is never replaced by standing legs");
    action.movementKind=NativeActionKL::Movement::Vehicle;
    Check(apply(0,0,0).second[1].position.y==source[1].position.y,"seated vehicle body is never raised into standing bind pose");
    action.movementKind=NativeActionKL::Movement::Stand;
    for(auto kind:{NativeActionKL::Action::Reload,NativeActionKL::Action::CloseCombat,NativeActionKL::Action::Pickup}) {
        action.action=1;action.actionKind=kind;const auto result=apply(0,0,0);
        Check(!result.first&&!std::memcmp(result.second.data(),source.data(),sizeof(source)),
              "manual reload, melee and pickup keep their complete native authored pose");
    }
    action.action=0;
    for(auto movement:{NativeActionKL::Movement::Rappel,NativeActionKL::Movement::Climb,NativeActionKL::Movement::Mounted}) {
        action.movementKind=movement;const auto result=apply(0,0,0);
        Check(!result.first&&!std::memcmp(result.second.data(),source.data(),sizeof(source)),
              "rappel, climbing and mounted gameplay remain outside the ordinary stance filter");
    }
    action.movementKind=NativeActionKL::Movement::Stand;
    Check(apply(std::numeric_limits<float>::quiet_NaN(),0,0).second[1].position.y==source[1].position.y,
          "invalid native movement input cannot authorize a stationary pose");
    put(controller+0x18,std::uint8_t(1));
    Check(apply(0,0,0).second[1].position.y==source[1].position.y,"blocked native controller cannot authorize standing legs");
    Check(neutralStandingFrames==1,"only the exact proved stationary state reaches the new diagnostic counter");
    VirtualFree(memory,0,MEM_RELEASE);
    std::printf("KL1 production stance: %u checks %s.\n",checks,failures?"FAILED":"PASS");return failures?1:0;
}
