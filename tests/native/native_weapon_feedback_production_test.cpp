// Actual owned weapon hooks in private CPU memory; no game, runtime or GPU.
#define main UnusedWeaponFeedbackScopeMain
#include "native_scope_fire_test.cpp"
#undef main
namespace {
void __thiscall StowedRigid(void* entry,void* state,std::uintptr_t) {
    Fixture::ScopeWorld(state,reinterpret_cast<const Fixture::CameraPose*>(reinterpret_cast<std::uintptr_t>(entry)+0x10));
}
}
int main() {
    using namespace Fixture;using namespace NativeWeaponKL;
    auto* allocation=VirtualAlloc(nullptr,0x900000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    Check(allocation!=nullptr,"private weapon feedback fixture allocated");if(!allocation)return 1;
    const auto base=reinterpret_cast<std::uintptr_t>(allocation);
    const auto at=[&](std::uintptr_t off){return static_cast<std::uint32_t>(base+off);};
    const auto data=at(0x10000),actor=at(0x20000),weapon=at(0x30000),model=at(0x40000),entry=at(0x50000),
        state=at(0x60000),component=at(0x70000),manager=at(0x80000),pool=at(0x90000),
        root=at(0xA0000),generations=at(0xB0000),marker=at(0xC0000),markerGeometry=at(0xD0000);
    image=base;installed=true;fixtureTick=1000;renderedFixture=vrDraw=ready=firstPersonDraw=true;player=actor;
    Put(base+kGameData,data);Put(data+0xA2C,actor);Put(actor,at(0x56582C));Put(actor+0x88,component);
    Put(base+0x6C99B8,manager);Put(manager+0xC,pool);Put(pool+4,std::uint32_t(0x70));Put(pool+8,std::uint32_t(1));
    Put(pool+0xC,root);Put(pool+0x10,generations);Put(generations,std::uint16_t(0));
    Put(weapon,at(0x577B74));Put(base+0x577B74+0x2D4,at(kSetTarget));Put(weapon+4,root);Put(root+0x60,weapon);
    Put(weapon+0x58,std::uint32_t(37));Put(weapon+0xC0,model);Put(model,at(0x577864));Put(model+0xA4,std::uint32_t(4));
    Put(model+0x150,marker);Put(marker+4,markerGeometry);Put(markerGeometry+0x60,marker);
    CameraPose muzzle;muzzle.basis={{0,0,1},{0,1,0},{1,0,0}};muzzle.position={50,0,0};Put(markerGeometry,muzzle);
    Put(entry,at(0x541070));Put(entry+0xF4,root);Put(base+kGpuIndex,std::uint32_t(0));
    NativeWeaponOwnership::HandState hand;hand.item=weapon;hand.enabled=1;hand.attacher=31;
    Put(actor+0x358,at(0x5508B4));Put(actor+0x358+0x60,hand);
    Put(actor+0x2D8,at(0x5508B4));NativeWeaponOwnership::HandState left;left.attacher=32;Put(actor+0x2D8+0x60,left);
    CameraPose native;native.basis={{0,0,1},{0,1,0},{1,0,0}};native.position={80,120,30};Put(entry+0x10,native);
    renderedHands={};renderedHands.player=actor;renderedHands.pairId=100;renderedHands.tick=fixtureTick;
    renderedHands.unitsPerMetre=100;renderedHands.recenterAck=7;renderedHands.availableForMesh[1]=true;
    renderedHands.rawWrist[1]=native;renderedHands.finalWrist[1]=native;renderedHands.finalWrist[1].position={100,140,-40};
    actionState={};actionState.valid=true;actionState.player=actor;actionState.movementKind=NativeActionKL::Movement::Stand;
    calibrationEpoch=2;calibrationCandidateEpoch=2;calibrationGameplay=true;calibrationTransition=false;
    heldCache={};negativeDrawMemo={};renderedGun[1]={};settledGrip[1].Reset();settledCandidateGeneration[1]=0;
    CameraPose output;
    const auto mesh=[&]{return MeshFromEntry(entry,reinterpret_cast<const CameraPose*>(entry+0x10),output);};
    Check(mesh()&&!renderedGun[1].shot.weapon,"first rifle pair does not freeze a transition grip");
    Check(mesh()&&!renderedGun[1].shot.weapon,"second eye cannot stand in for another settled pair");
    ++renderedHands.pairId;Check(mesh()&&!renderedGun[1].shot.weapon,"second stable rifle pair still waits");
    ++renderedHands.pairId;Check(mesh()&&renderedGun[1].aligned,"third stable rifle pair captures its own grip");
    const auto rifleCalibration=renderedGun[1].calibration;
    // Switching reference/model category in one scene is a new shotgun binding.
    // The old per-hand settledEpoch bypassed this gate completely.
    Put(weapon+0x58,std::uint32_t(38));Put(model+0xA4,std::uint32_t(5));++renderedHands.pairId;
    Check(mesh()&&renderedGun[1].reference==37,"new shotgun on the same hand cannot inherit rifle settlement");
    ++renderedHands.pairId;auto unsettled=native;unsettled.position.y+=8;Put(entry+0x10,unsettled);
    Check(mesh()&&renderedGun[1].reference==37,"moving shotgun animation restarts calibration settling");
    for(unsigned i=0;i<2;++i){++renderedHands.pairId;Check(mesh(),"new shotgun stable pairs retain visual wrist remapping");}
    Check(renderedGun[1].reference==38&&renderedGun[1].category==5&&renderedGun[1].aligned,
        "shotgun gets an independent settled grip without a loading transition");
    Check(std::memcmp(&rifleCalibration,&renderedGun[1].calibration,sizeof(rifleCalibration))!=0,
        "new binding captures current authored placement rather than the rifle certificate");
    // Body attachment: first-person hiding is proved from the same actual
    // Mesh -> WORLD hook -> Suppress consumer chain used by the game.
    hand.item=0;hand.enabled=0;Put(actor+0x358+0x60,hand);
    NativeWeaponOwnership::Record attachment;attachment.geometryRef=1;attachment.bone=34;
    Put(component+0x14,std::uint32_t(1));Put(component+0x18,attachment);Put(model+0xA4,std::uint32_t(4));
    renderedHands.bodyAvailableForMesh=true;renderedHands.rawRoot=native;renderedHands.renderRoot=native;
    renderedHands.renderRoot.position.y-=45;heldCache={};negativeDrawMemo={};originalWorld=&ScopeWorld;
    const auto bind=[&]{WorldBindHook(reinterpret_cast<void*>(state),nullptr,reinterpret_cast<const CameraPose*>(entry+0x10));};
    bind();Check(SuppressStowedDraw(),"exact back rifle is hidden in crouched first-person view");
    Check(SuppressStowedDraw(),"multiple material passes retain the same stowed suppression");
    Check(!std::memcmp(reinterpret_cast<void*>(entry+0x10),&unsettled,sizeof(unsettled)),"native source mesh pose stays byte-exact");
    firstPersonDraw=false;Check(!SuppressStowedDraw(),"native third-person/cutscene views retain stowed weapons");firstPersonDraw=true;
    bind();++drawGeneration;Check(!SuppressStowedDraw(),"another draw generation cannot inherit hidden geometry");
    bind();Put(weapon+0x58,std::uint32_t(39));Check(!SuppressStowedDraw(),"recycled weapon reference retires hiding");Put(weapon+0x58,std::uint32_t(38));
    bind();hand.item=weapon;hand.enabled=1;Put(actor+0x358+0x60,hand);
    Check(!SuppressStowedDraw(),"equipping the back rifle immediately restores its draw");hand.item=0;hand.enabled=0;Put(actor+0x358+0x60,hand);
    bind();Put(component+0x14,std::uint32_t(0));Check(!SuppressStowedDraw(),"detached body slot cannot retain hidden ownership");Put(component+0x14,std::uint32_t(1));
    bind();auto moved=*reinterpret_cast<const CameraPose*>(state+0x8C);moved.position.x+=1;Put(state+0x8C,moved);
    Check(!SuppressStowedDraw(),"a different WORLD binding cannot borrow the stowed tag");
    bind();ClearScopedDrawFrame();Check(!SuppressStowedDraw(),"capture/recenter reset clears stowed suppression");
    // The renderer can still be drawing this eye when the controller/IK
    // snapshot is unavailable. Neither native binder may leak the attachment.
    drawVisualAvailable=false;originalRigid=&StowedRigid;
    const auto before=Snapshot();
    for(const std::uint32_t category:{4u,0u}){
        Put(model+0xA4,category);
        bind();
        Check(!std::memcmp(reinterpret_cast<const void*>(state+0x8C),&unsettled,sizeof(unsettled)),
              "missing draw pose preserves the native WORLD for the regular binder");
        Check(SuppressStowedDraw(),"body firearm stays hidden through regular bind without draw pose");
        RigidBindHook(reinterpret_cast<void*>(entry),nullptr,reinterpret_cast<void*>(state),0);
        Check(!std::memcmp(reinterpret_cast<const void*>(state+0x8C),&unsettled,sizeof(unsettled)),
              "missing draw pose preserves the native WORLD for the rigid binder");
        Check(SuppressStowedDraw(),"body firearm stays hidden through rigid bind without draw pose");
    }
    const auto after=Snapshot();
    Check(after.meshFreshRejected==before.meshFreshRejected+4&&after.stowedHidden==before.stowedHidden+4,
          "snapshot misses and actual suppression are counted independently");
    drawVisualAvailable=true;renderedHands.bodyAvailableForMesh=false;
    bind();Check(SuppressStowedDraw(),"stowed firearm remains hidden when the body transform is unavailable");
    liveDrawAvailable=false;bind();Check(!SuppressStowedDraw(),"inactive render frame cannot hide a firearm");
    liveDrawAvailable=true;vrDraw=false;bind();Check(!SuppressStowedDraw(),"foreign camera keeps native attachment rendering");
    // Reproduce the garden's colour/mask split with a late change in
    // wrist availability while the same eye is still being rendered.
    vrDraw=true;calibrationGameplay=false;drawVisualAvailable=false;
    Put(component+0x14,std::uint32_t(0));
    hand.item=weapon;hand.enabled=1;Put(actor+0x358+0x60,hand);
    Put(model+0xA4,std::uint32_t(4));heldCache={};negativeDrawMemo={};
    const auto worldEquals=[&](const CameraPose& pose){
        return !std::memcmp(reinterpret_cast<const void*>(state+0x8C),&pose,sizeof(pose));
    };
    ++captureImageEpoch;bind();
    Check(worldEquals(unsettled),"first material can use the game's weapon pose");
    drawVisualAvailable=true;++drawGeneration;
    RigidBindHook(reinterpret_cast<void*>(entry),nullptr,reinterpret_cast<void*>(state),0);
    Check(worldEquals(unsettled),"later mask keeps the first material's native pose");
    ++captureImageEpoch;++renderedHands.pairId;bind();
    const auto tracked=*reinterpret_cast<const CameraPose*>(state+0x8C);
    Check(!worldEquals(unsettled),"next image can use the tracked wrist");
    drawVisualAvailable=false;++drawGeneration;
    RigidBindHook(reinterpret_cast<void*>(entry),nullptr,reinterpret_cast<void*>(state),0);
    Check(worldEquals(tracked),"later mask retains the first tracked wrist after a snapshot miss");
    captureImageEye=1;++captureImageEpoch;ClearScopedDrawFrame(true);bind();
    Check(worldEquals(tracked),"the other eye keeps the first eye's tracked pose after a snapshot miss");
    ++renderedHands.pairId;bind();
    Check(worldEquals(unsettled),"the next stereo pair can fall back to the native pose");
    captureImageEye=0;++captureImageEpoch;drawVisualAvailable=true;ClearScopedDrawFrame(true);bind();
    Check(worldEquals(unsettled),"late tracking cannot move a native weapon in the other eye");
    drawVisualAvailable=true;ClearScopedDrawFrame();bind();
    Check(worldEquals(tracked),"closing the draw context retires the previous pose decision");
    calibrationGameplay=true;
    renderedHands.tracked[1]=true;renderedHands.controllerWrist[1]=renderedHands.finalWrist[1];
    for(unsigned i=0;i<4;++i){++renderedHands.pairId;++captureImageEpoch;bind();}
    const auto lastGun=renderedGun[1];
    const auto goodWorld=*reinterpret_cast<const CameraPose*>(state+0x8C);
    Check(lastGun.shot.hands.pairId==renderedHands.pairId&&lastGun.aligned,"normal draw establishes a carry transform for the active gun");
    drawControllerAvailable=true;drawController={};
    drawController.centre=renderedHands.centre;drawController.wrist=renderedHands.controllerWrist[1];
    drawController.recenter=7;drawController.units=100.f;drawController.tracked=true;
    drawController.wrist.position.x+=13;
    drawVisualAvailable=false;++renderedHands.pairId;++captureImageEpoch;bind();
    auto recoveredWorld=goodWorld;recoveredWorld.position.x+=13;
    const auto recoveryMatches=[&]{
        const auto& world=*reinterpret_cast<const CameraPose*>(state+0x8C);
        return NativeArmMath::Length(Subtract(world.position,recoveredWorld.position))<.001f&&
            Dot(world.basis.forward,recoveredWorld.basis.forward)>.9999f;
    };
    Check(recoveryMatches(),"missing skin draw remaps the carried gun onto the present physical controller");
    ++captureImageEpoch;captureImageEye=1;ClearScopedDrawFrame(true);drawController.wrist.position.x+=21;
    RigidBindHook(reinterpret_cast<void*>(entry),nullptr,reinterpret_cast<void*>(state),0);
    Check(recoveryMatches(),"a late controller update cannot change the recovered pose in the other eye or mask");
    Check(renderedGun[1].tick==lastGun.tick&&renderedGun[1].shot.hands.pairId==lastGun.shot.hands.pairId,
        "recovered rendering does not refresh the certificate used by Fire");
    renderedGun[1].shot.hands.supportHeld=true;
    drawController.wrist=lastGun.shot.hands.controllerWrist[1];
    drawController.wrist.position.x+=13;
    const auto basis=drawController.wrist.basis;
    drawController.wrist.basis={basis.right,basis.up,Scale(basis.forward,-1.f)};
    ++renderedHands.pairId;++captureImageEpoch;bind();
    Check(recoveryMatches(),"a primary-only rotation cannot swing the last confirmed two-hand WORLD away from the fore-end");
    drawController.coupledWrist=lastGun.shot.hands.finalWrist[1];
    drawController.coupledWrist.position.x+=19;
    drawController.coupledItem=weapon;drawController.coupledRoot=root;drawController.coupledModel=model;drawController.coupledReference=38;
    ++renderedHands.pairId;++captureImageEpoch;bind();
    recoveredWorld.position.x+=6;
    Check(recoveryMatches(),"current matched two-hand solve takes priority over the fallback primary controller");
    drawController.coupledItem=0;
    ++renderedHands.pairId;++captureImageEpoch;drawController.tracked=false;fixtureTick+=151;bind();
    Check(worldEquals(unsettled),"lost controllers age out continuity without extending the last true sample");
    ++renderedHands.pairId;++captureImageEpoch;drawController.tracked=true;
    Put(weapon+0x58,std::uint32_t(99));bind();
    Check(worldEquals(unsettled),"recycled gun cannot reuse the previous rifle's carry transform");
    Put(weapon+0x58,std::uint32_t(38));
    ++renderedHands.pairId;++captureImageEpoch;actionState.action=1;actionState.actionKind=NativeActionKL::Action::MiniAct;
    bind();Check(worldEquals(unsettled),"an authored actor action cannot use retained carry placement");
    VirtualFree(allocation,0,MEM_RELEASE);
    std::printf("Weapon feedback production: %u checks, %u failures; CPU only.\n",checks,failures);return failures?1:0;
}
