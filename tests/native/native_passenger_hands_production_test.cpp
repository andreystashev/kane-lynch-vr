// Actual passenger GPU hand solve over private poses; native/runtime entry
// points are fixtures. No game, input events, hooks, GPU or headset opens.
#define main UnusedTwoHandPassengerMain
#include "native_two_hand_production_test.cpp"
#undef main

int main() {
    using namespace NativeHandsKL;using namespace NativeCameraMath;
    const auto close=[](Vec3 a,Vec3 b){return NativeArmMath::Length(Subtract(a,b))<.001f;};
    CameraFrame camera;camera.firstPerson=true;camera.nativeBodyAction=true;camera.units=100;
    camera.centre=Frame();camera.centre.position={0,150,0};camera.rawHeadOrientation[3]=1;
    camera.rig.player=1;camera.rig.model=2;camera.rig.valid=true;camera.rig.count=8;
    camera.rig.lookup[22]=1;camera.rig.lookup[23]=2;camera.rig.lookup[24]=3;
    camera.rig.lookup[16]=4;camera.rig.lookup[17]=5;camera.rig.lookup[18]=6;
    const std::uint32_t parents[]={0,0,1,2,0,4,5,3};std::memcpy(camera.rig.parents.data(),parents,sizeof(parents));
    camera.hands.present=camera.hands.mounted=1;camera.hands.head.orientation[3]=1;
    camera.hands.head.flags=HandsIPC::kOrientation|HandsIPC::kPosition;
    for(auto& hand:camera.hands.hands){hand.orientation[3]=1;hand.flags=HandsIPC::kOrientation|HandsIPC::kPosition;}
    for(auto& mask:camera.hands.inputValid)mask=HandsIPC::kGripInput|HandsIPC::kTriggerInput;
    camera.hands.hands[1].position[0]=.15f;camera.hands.hands[1].position[1]=-.25f;camera.hands.hands[1].position[2]=-.90f;
    camera.hands.hands[0].position[0]=.15f;camera.hands.hands[0].position[1]=-.25f;camera.hands.hands[0].position[2]=-1.10f;
    std::array<Bone,NativeRigMath::MaximumBones> idle{};
    for(auto& bone:idle)bone.basis=Frame().basis;
    idle[1].position={18,125,0};idle[2].position={18,125,25};idle[3].position={18,125,50};
    idle[4].position={-18,125,0};idle[5].position={-18,125,25};idle[6].position={-18,125,50};
    idle[7].position=Add(idle[3].position,{2,1,3});const auto idleCopy=idle;
    auto firing=idle;for(unsigned i=1;i<8;++i)firing[i].position=Add(firing[i].position,{35,-10,0});
    std::array<std::uint8_t,NativeRigMath::MaximumBones> recovered{};for(unsigned i=0;i<8;++i)recovered[i]=1;
    heldFixture={};heldFixture.player=1;heldFixture.weapon=3;heldFixture.root=4;heldFixture.model=5;heldFixture.marker=6;
    heldFixture.markerGeometry=7;heldFixture.reference=8;heldFixture.category=4;heldFixture.active=true;
    heldFixture.socket=Frame();heldFixture.socket.position={20,0,0};heldFixture.candidate=true;
    actionFixture={};actionFixture.valid=true;actionFixture.player=1;actionFixture.movementKind=NativeActionKL::Movement::Vehicle;
    struct Result {PreparedDraw draw;std::array<Bone,NativeRigMath::MaximumBones> pose;bool solved;};
    auto solve=[&](const auto& source,float grip,bool stale=false,bool nextPair=true) {
        Fixture::tick+=16;if(nextPair)++camera.pairId;camera.hands.tick=Fixture::Clock();camera.hands.grip[0]=grip;
        camera.input=camera.hands;if(stale)camera.hands.tick-=250;
        Result out;out.pose=source;out.draw.count=8;out.draw.rawRoot=out.draw.renderRoot=Frame();
        std::array<std::uint8_t,NativeRigMath::MaximumBones> changed{};
        out.solved=SolveGpuRig(camera,source,out.pose.data(),recovered,changed,out.draw);return out;
    };
    supportGripEnabled=false;supportState={};const auto before=solve(idle,0),shot=solve(firing,0);
    Check(before.solved&&shot.solved&&before.draw.visual.tracked[0]&&before.draw.visual.tracked[1],"exact Vehicle/no-action tracks both complete passenger arms");
    for(unsigned side=0;side<2;++side) {
        Check(before.draw.armResult[side].clamped&&shot.draw.armResult[side].clamped,
              "fixture actually crosses native seated reach, reproducing the old clamp-driven displacement");
        Check(close(before.draw.visual.finalWrist[side].position,before.draw.diagnostic[side].target.position)&&
              close(shot.draw.visual.finalWrist[side].position,before.draw.visual.finalWrist[side].position)&&shot.draw.error[side]<.00001f,
              "native firing shoulder/pelvis translation cannot move the absolute passenger wrist away from its controller");
    }
    const auto wrist=NativeRigMath::ToModel({shot.pose[3].basis,shot.pose[3].position},{shot.pose[7].basis,shot.pose[7].position});
    const auto originalFinger=NativeRigMath::ToModel({idle[3].basis,idle[3].position},{idle[7].basis,idle[7].position});
    Check(close(wrist.position,originalFinger.position),"finger subtree preserves native grip offsets while the complete wrist is pinned");
    Check(!std::memcmp(idle.data(),idleCopy.data(),sizeof(idle)),"passenger solve leaves native input rig read-only");
    supportGripEnabled=true;supportState={};solve(idle,0);
    const auto coupledIdle=solve(idle,.8f),coupledFire=solve(firing,.8f,false,false);
    Check(coupledIdle.draw.visual.supportHeld&&coupledFire.draw.visual.supportHeld,"passenger foregrip acquires and remains held across native firing pose in both eyes");
    for(unsigned side=0;side<2;++side)Check(close(coupledIdle.draw.visual.finalWrist[side].position,coupledFire.draw.visual.finalWrist[side].position),
          "coupled passenger firearm retains one coherent physical hand frame instead of separate native reach corrections");
    const auto socket=NativeRigMath::ToWorld(coupledFire.draw.visual.finalWrist[1],{heldFixture.socket.basis,heldFixture.socket.position});
    Check(close(socket.position,coupledFire.draw.visual.finalWrist[0].position),"actual authored foregrip socket and both passenger wrist endpoints remain rigidly attached");
    supportGripEnabled=false;supportState={};actionFixture.movementKind=NativeActionKL::Movement::Stand;
    const auto ordinary=solve(firing,0);
    Check(ordinary.solved&&ordinary.draw.error[1]>.01f,"ordinary gameplay keeps its original anatomical reach policy");
    actionFixture.movementKind=NativeActionKL::Movement::Vehicle;actionFixture.action=1;actionFixture.actionKind=NativeActionKL::Action::Reload;
    const auto reload=solve(firing,0);
    Check(!reload.solved&&!std::memcmp(reload.pose.data(),firing.data(),sizeof(firing)),
          "fresh native Reload retains original full hand animation even if the camera publication still says tracked arms");
    actionFixture.action=0;actionFixture.actionKind=NativeActionKL::Action::None;
    const auto stale=solve(firing,0,true);
    Check(!stale.solved&&!std::memcmp(stale.pose.data(),firing.data(),sizeof(firing)),"stale passenger tracking cannot pin a fabricated wrist");
    actionFixture.player=2;const auto foreign=solve(firing,0);
    Check(foreign.draw.error[1]>.01f,"another native player's passenger state cannot authorize detached endpoints");
    Check(passengerHandsPinned[0]==5&&passengerHandsPinned[1]==5&&passengerNativeReachCorrected[1]>0,
          "new counters distinguish passenger tracked endpoints from actual native reach corrections");
    std::printf("KL1 passenger tracked hands: %u checks %s.\n",checks,failures?"FAILED":"PASS");return failures?1:0;
}
