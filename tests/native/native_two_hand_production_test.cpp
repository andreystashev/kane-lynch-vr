// Exercise the actual GPU rig path. Only the native held-item read and VR UI
// flags are fixtures; controller mapping, acquisition, steering, reach fitting
// and the complete production arm solve are unchanged.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
namespace Fixture {inline DWORD tick=1000;inline DWORD Clock(){return tick;}}
#define GetTickCount Fixture::Clock
#include "native_hands_kl.cpp"
#undef GetTickCount
#include <cstdio>
#include <cstdlib>

namespace {
NativeWeaponKL::HeldSupport heldFixture;
bool teamOpen=false,weaponOpen=false,reloadBusy=false;
NativeActionKL::State actionFixture{};
unsigned checks=0,failures=0;
void Check(bool ok,const char* text){++checks;if(!ok){++failures;std::printf("FAIL: %s\n",text);}}
NativeCameraMath::CameraPose Frame(){NativeCameraMath::CameraPose p;p.basis={{0,0,1},{0,1,0},{1,0,0}};return p;}
[[noreturn]] void Unexpected(){std::puts("FAIL: fixture attempted an unrelated game/runtime/hook entry");std::abort();}
}
namespace NativeWeaponKL {
bool ReloadHandTarget(std::uintptr_t,unsigned,const NativeCameraMath::CameraPose&,NativeCameraMath::CameraPose&){return false;}
bool ReloadHandBusy(std::uintptr_t){return reloadBusy;}
bool PreferReloadSlide(const HeldSupport&,const NativeCameraMath::CameraPose&,const NativeCameraMath::CameraPose&,const NativeCameraMath::CameraPose*,std::uint32_t){return false;}
bool CaptureHeldSupport(std::uintptr_t player,float units,HeldSupport& output,bool allowInactive) {
    if(player!=heldFixture.player||units!=100.f)return false;
    if(!heldFixture.active&&!allowInactive)return false;
    output=heldFixture;return true;
}
}
namespace VRBridge {
bool TeamHudVisible(){return teamOpen;}
bool WeaponMenuVisible(){return weaponOpen;}
bool Ready(){return true;}
}
namespace NativeActionKL {
bool InitializeVerifiedImage(std::uintptr_t){Unexpected();}
State Current(){return actionFixture;}
bool PreserveRig(){Unexpected();}
}
namespace NativeCameraKL {
bool CinematicActive(){return false;}
bool DrawStateActive(std::uintptr_t,bool*,bool){Unexpected();}
}
namespace NativeBodyKL {
void ResetFollow(){Unexpected();}
bool AdjustAction(std::uintptr_t,const NativeActionKL::State&,const NativeCameraMath::CameraPose&,NativeCameraMath::Vec3,
                  NativeCameraMath::Vec3,NativeCameraMath::Vec3,NativeCameraMath::CameraPose&,std::uint64_t){Unexpected();}
bool Adjust(std::uintptr_t,const NativeCameraMath::CameraPose&,NativeCameraMath::Vec3,
            NativeCameraMath::Vec3,NativeCameraMath::Vec3,NativeCameraMath::CameraPose&,std::uint64_t){Unexpected();}
}
extern "C" {
MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*){Unexpected();}
MH_STATUS WINAPI MH_QueueEnableHook(LPVOID){Unexpected();}
MH_STATUS WINAPI MH_ApplyQueued(){Unexpected();}
MH_STATUS WINAPI MH_DisableHook(LPVOID){Unexpected();}
MH_STATUS WINAPI MH_RemoveHook(LPVOID){Unexpected();}
}
int main() {
    using namespace NativeCameraMath;
    using namespace NativeHandsKL;
    CameraFrame camera;camera.centre=Frame();camera.centre.position={0,150,0};camera.units=100;
    camera.firstPerson=true;camera.rig.player=1;camera.rig.model=2;camera.rig.valid=true;camera.rig.count=7;
    camera.rig.lookup[22]=1;camera.rig.lookup[23]=2;camera.rig.lookup[24]=3;
    camera.rig.lookup[16]=4;camera.rig.lookup[17]=5;camera.rig.lookup[18]=6;
    camera.rig.parents[1]=0;camera.rig.parents[2]=1;camera.rig.parents[3]=2;
    camera.rig.parents[4]=0;camera.rig.parents[5]=4;camera.rig.parents[6]=5;
    camera.rawHeadOrientation[3]=1;
    camera.hands.present=1;camera.hands.mounted=1;camera.hands.head.orientation[3]=1;
    camera.hands.inputValid[0]=camera.hands.inputValid[1]=HandsIPC::kGripInput|HandsIPC::kTriggerInput;
    camera.hands.head.flags=HandsIPC::kOrientation|HandsIPC::kPosition;
    for(auto& h:camera.hands.hands){h.orientation[3]=1;h.flags=HandsIPC::kOrientation|HandsIPC::kPosition;}
    camera.hands.hands[1].position[0]=.15f;camera.hands.hands[1].position[1]=-.25f;camera.hands.hands[1].position[2]=-.60f;
    camera.hands.hands[0].position[0]=.10f;camera.hands.hands[0].position[1]=-.25f;camera.hands.hands[0].position[2]=-1.f;
    std::array<Bone,NativeRigMath::MaximumBones> original{};
    for(auto& b:original)b.basis=Frame().basis;
    original[1].position={18,125,0};original[2].position={18,125,35};original[3].position={18,125,70};
    original[4].position={-18,125,0};original[5].position={-18,125,35};original[6].position={-18,125,70};
    std::array<std::uint8_t,NativeRigMath::MaximumBones> recovered{};for(unsigned i=0;i<7;++i)recovered[i]=1;
    heldFixture.player=1;heldFixture.weapon=3;heldFixture.root=4;heldFixture.model=5;
    heldFixture.marker=6;heldFixture.markerGeometry=7;heldFixture.reference=8;heldFixture.category=4;
    heldFixture.active=true;
    heldFixture.socket=Frame();heldFixture.socket.position={20,0,0};heldFixture.candidate=true;
    auto solve=[&](float grip,bool stalePose=false,bool missingInput=false) {
        Fixture::tick+=16;++camera.pairId;camera.hands.grip[0]=grip;camera.hands.tick=Fixture::Clock();camera.input=camera.hands;
        if(stalePose)camera.hands.tick-=250;
        if(missingInput)camera.input={};
        auto pose=original;std::array<std::uint8_t,NativeRigMath::MaximumBones> changed{};
        PreparedDraw draw;draw.count=7;draw.rawRoot=Frame();draw.renderRoot=Frame();
        const bool solved=SolveGpuRig(camera,original,pose.data(),recovered,changed,draw);
        Check((camera.nativeAction||stalePose)?!solved:solved,"production rig tracks controllers or preserves native reload animation");
        return draw;
    };
    const auto savedRight=camera.hands.hands[1];
    camera.hands.hands[1].position[1]=.35f;
    camera.hands.hands[1].position[2]=-.75f;
    const auto clamped=solve(0);
    actionFixture.valid=true;actionFixture.player=camera.rig.player;
    for(const auto movement:{NativeActionKL::Movement::Stand,NativeActionKL::Movement::BaseMove}){
        actionFixture.movementKind=movement;
        const auto raised=solve(0);
        const Vec3 correction=Subtract(raised.visual.finalWrist[1].position,clamped.visual.finalWrist[1].position);
        Check(raised.visual.tracked[1]&&raised.armResult[1].clamped&&correction.y>0.f&&
              NativeArmMath::Length(correction)<=10.01f,
              "a raised trigger hand stays tracked and gets a bounded reach correction in ordinary stance");
    }
    actionFixture={};
    camera.hands.hands[1]=savedRight;
    supportState={};auto draw=solve(0);Check(!draw.visual.supportHeld,"released button never creates a coupled gun");
    const auto beforeGrip=draw.visual.finalWrist[1];
    draw=solve(.60f);Check(draw.visual.supportHeld,"rendered fore-end contact acquires with a moderate squeeze");
    Check(draw.visual.tracked[0]&&draw.visual.tracked[1],"both complete arm solves publish the same held visual frame");
    Vec3 line,expected;
    NativeArmMath::Unit(Subtract(draw.visual.finalWrist[0].position,draw.visual.finalWrist[1].position),line);
    NativeArmMath::Unit(NativeRigMath::Transform(beforeGrip.basis,heldFixture.socket.position),expected);
    Check(Dot(line,expected)>.9999f,"acquisition keeps the rifle pointing in its original direction");
    Check(Dot(beforeGrip.basis.forward,draw.visual.finalWrist[1].basis.forward)>.9999f,
          "the primary wrist keeps its orientation on the grip edge");
    const float supportX=camera.hands.hands[0].position[0];
    camera.hands.hands[0].position[0]+= .1f;
    const auto steered=solve(.60f);
    Vec3 movedLine;
    Check(steered.visual.supportHeld&&NativeArmMath::Unit(Subtract(steered.visual.finalWrist[0].position,
          steered.visual.finalWrist[1].position),movedLine)&&Dot(line,movedLine)<.99f,
          "subsequent support controller motion steers the coupled rifle");
    camera.hands.hands[0].position[0]=supportX;
    Check(std::fabs(NativeArmMath::Length(Subtract(draw.visual.finalWrist[0].position,draw.visual.finalWrist[1].position))-20)<.001f,
          "fixed authored foregrip length survives reach fitting and both production solves");
    Check(!draw.armResult[0].clamped&&!draw.armResult[1].clamped,"coupled weapon fits actual arm reach without stretching");
    reloadBusy=true;draw=solve(.8f);Check(!draw.visual.supportHeld,"manual magazine ownership releases the actual support-hand solve");
    reloadBusy=false;draw=solve(0);draw=solve(.8f);Check(draw.visual.supportHeld,"support can be acquired again after the manual mechanism releases the hand");
    draw=solve(.25f);Check(draw.visual.supportHeld,"relaxed but held grip retains support in the actual rig path");
    draw=solve(.09f);Check(draw.visual.supportHeld&&supportState.held,"a lightly squeezed physical button does not drop below the former .20 threshold");
    draw=solve(0);Check(draw.visual.supportHeld&&supportState.held,"one fresh zero-valued sample does not detach the production foregrip");
    draw=solve(.8f);Check(draw.visual.supportHeld&&supportState.lowGripSamples==0,"fresh held analog cancels transient-release confirmation");
    camera.hands.inputValid[0]=0;
    for(unsigned i=0;i<16;++i)draw=solve(0);
    Check(!draw.visual.supportHeld&&supportState.held&&supportState.lowGripSamples==0,
          "inactive OpenXR grip action with valid controller pose and default zero never releases or authorizes coupled IK");
    camera.hands.inputValid[0]=HandsIPC::kGripInput;
    draw=solve(.8f);Check(draw.visual.supportHeld,"restored valid grip action resumes the existing socket without a re-grab");
    supportState.tick-=1000;
    draw=solve(.8f);Check(draw.visual.supportHeld,"a one-second renderer gap does not release a fresh physical grip");
    draw=solve(.8f,true);Check(!draw.visual.supportHeld&&supportState.held&&supportState.suspended,
        "stale paired eye pose with fresh held physical input suspends output without dropping the latch");
    draw=solve(.8f,false,true);Check(!draw.visual.supportHeld&&supportState.held,
        "a missing physical sample cannot invent a grip release or authorize coupled IK");
    draw=solve(.8f);Check(draw.visual.supportHeld,"a current pose and input resume the same socket after the stall");
    for(unsigned i=0;i<9;++i)draw=solve(0,true);
    Check(!supportState.held&&supportState.releaseReason==NativeTwoHandMath::ReleaseReason::Input,
        "a real new physical release retires the latch even while paired pose is stale");
    solve(0);draw=solve(.8f);Check(draw.visual.supportHeld,"fresh re-grab follows a true physical release");
    heldFixture.active=false;draw=solve(.8f);Check(!draw.visual.supportHeld&&supportState.held,
        "temporary inactive native IKHAND preserves only logical grip outside recognized action IDs");
    heldFixture.active=true;draw=solve(.8f);Check(draw.visual.supportHeld,"native hand reactivation restores coupled output");
    camera.nativeAction=true;heldFixture.active=false;
    draw=solve(.8f);Check(!draw.visual.supportHeld&&supportState.held&&supportState.suspended,"native reload suspends visual IK while retaining the exact owned grip latch");
    camera.nativeAction=false;heldFixture.active=true;
    draw=solve(.8f);Check(draw.visual.supportHeld,"finishing reload resumes the same foregrip without a release and re-grab");
    camera.nativeAction=true;camera.nativeBodyAction=true;
    draw=solve(.8f);Check(!draw.visual.supportHeld&&supportState.held,"native body action such as Climb keeps only the logical same-gun grip, with no forced IK");
    camera.nativeAction=false;camera.nativeBodyAction=false;
    draw=solve(.8f);Check(draw.visual.supportHeld,"returning from the same-gun body action resumes support without another squeeze");
    actionFixture.valid=true;actionFixture.player=1;actionFixture.movementKind=NativeActionKL::Movement::Vehicle;
    const auto seated=NativeVisualPolicy::ForAction(actionFixture,1);
    Check(actionFixture.PassengerWeapon()&&!seated.nativeArms&&seated.nativeBody,
          "actual passenger action policy preserves the authored seated body and allows only tracked held-gun arms");
    camera.nativeAction=seated.nativeArms;camera.nativeBodyAction=seated.nativeBody;draw=solve(.8f);
    VisualFrame seatedVisual;visual=draw.visual;
    Check(draw.visual.active[1]&&draw.visual.supportHeld&&SnapshotVisual(seatedVisual),
          "actual GPU rig and SnapshotVisual both retain tracked passenger aiming without detaching the seated body");
    actionFixture.movementKind=NativeActionKL::Movement::Mounted;
    const auto mounted=NativeVisualPolicy::ForAction(actionFixture,1);
    Check(mounted.nativeArms&&mounted.nativeBody&&!SnapshotVisual(seatedVisual),
          "anchored mounted gun keeps its original native arm/body rig and rejects ordinary handheld snapshots");
    actionFixture.movementKind=NativeActionKL::Movement::Vehicle;actionFixture.action=1;actionFixture.actionKind=NativeActionKL::Action::Reload;
    const auto seatedReload=NativeVisualPolicy::ForAction(actionFixture,1);
    Check(seatedReload.nativeArms&&seatedReload.nativeBody&&SnapshotVisual(seatedVisual),
          "passenger reload retains original seated native animation and same-item ownership");
    for(auto movement:{NativeActionKL::Movement::Stand,NativeActionKL::Movement::BaseMove,
                       NativeActionKL::Movement::Cover,NativeActionKL::Movement::Vehicle}) {
        actionFixture.movementKind=movement;
        const auto reload=NativeVisualPolicy::ForAction(actionFixture,1);
        camera.nativeAction=reload.nativeArms;camera.nativeBodyAction=reload.nativeBody;heldFixture.active=false;
        draw=solve(.8f);visual=draw.visual;
        Check(!draw.visual.active[0]&&!draw.visual.active[1]&&!draw.visual.supportHeld&&supportState.held&&SnapshotVisual(seatedVisual),
              "manual/empty reload displays original native hands and retains logical foregrip through temporary inactive IKHAND");
        heldFixture.active=true;
        actionFixture.action=0;actionFixture.actionKind=NativeActionKL::Action::None;
        const auto finished=NativeVisualPolicy::ForAction(actionFixture,1);
        camera.nativeAction=finished.nativeArms;camera.nativeBodyAction=finished.nativeBody;draw=solve(.8f);
        Check(draw.visual.supportHeld&&supportState.held,"reload/stance completion needs no physical release or re-grab");
        actionFixture.action=1;actionFixture.actionKind=NativeActionKL::Action::Reload;
    }
    actionFixture.movementKind=NativeActionKL::Movement::Climb;
    const auto traversalReload=NativeVisualPolicy::ForAction(actionFixture,1);
    Check(traversalReload.nativeArms&&traversalReload.nativeBody&&!SnapshotVisual(seatedVisual),
          "reload diagnostic class never reopens tracked weapon IK during real climbing");
    for(auto kind:{NativeActionKL::Action::PutBack,NativeActionKL::Action::GetBack,NativeActionKL::Action::Command}) {
        actionFixture.movementKind=NativeActionKL::Movement::Stand;actionFixture.actionKind=kind;
        const auto gesture=NativeVisualPolicy::ForAction(actionFixture,1);
        camera.nativeAction=gesture.nativeArms;camera.nativeBodyAction=gesture.nativeBody;draw=solve(.8f);visual=draw.visual;
        Check(draw.visual.tracked[0]&&draw.visual.tracked[1]&&draw.visual.supportHeld&&SnapshotVisual(seatedVisual),
              "production rig keeps both tracked arms and owned foregrip during exact weapon-ready/put-away or command gesture");
        heldFixture.active=false;draw=solve(.8f);
        Check(!draw.visual.supportHeld&&supportState.held,"native transition changing active held ownership suspends support; no generic inactive-hand exception");
        heldFixture.active=true;draw=solve(.8f);
        Check(draw.visual.supportHeld,"same item reactivation after cosmetic transition resumes persistent grip");
    }
    actionFixture={};camera.nativeAction=false;camera.nativeBodyAction=false;
    camera.hands.hands[0].flags=0;
    draw=solve(.8f);Check(draw.visual.supportHeld&&supportState.held&&draw.visual.active[0],"support optical loss keeps the visual hand at the latched fore-end while primary pose and physical grip remain fresh");
    camera.hands.hands[0].flags=HandsIPC::kOrientation|HandsIPC::kPosition;
    draw=solve(.8f);Check(draw.visual.supportHeld,"valid support pose restores the existing coupled gun automatically");
    teamOpen=true;draw=solve(.8f);Check(!draw.visual.supportHeld,"team UI immediately cancels support in production");
    teamOpen=false;draw=solve(.8f);Check(!draw.visual.supportHeld,"UI return cannot recapture a still-held button");
    solve(0);draw=solve(.8f);Check(draw.visual.supportHeld,"a real release permits reacquisition after UI");
    for(unsigned i=0;i<9;++i)draw=solve(0);
    Check(!draw.visual.supportHeld,"confirmed actual release restores independent controller hands");
    {
        auto* memory=VirtualAlloc(nullptr,0x900000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
        Check(memory!=nullptr,"private controller-aim native layout allocated");
        if(memory) {
            const auto base=reinterpret_cast<std::uintptr_t>(memory);
            const auto pointer=[&](std::uintptr_t offset){return static_cast<std::uint32_t>(base+offset);};
            const auto put=[&](std::uintptr_t at,std::uint32_t value){std::memcpy(reinterpret_cast<void*>(at),&value,4);};
            image=base;installed=true;const auto data=pointer(0x10000),player=pointer(0x20000),model=pointer(0x30000);
            put(base+kGameData,data);put(data+0xA2C,player);put(player,pointer(kPlayerVtable));put(player+0x70,model);
            camera.rig.player=player;camera.rig.model=model;camera.tick=Fixture::Clock();camera.nativeAction=true;
            camera.centre.basis={{1,0,0},{0,1,0},{0,0,-1}};publishedFrame=camera;
            HandsIPC::Producer input;Check(input.Open(GetCurrentProcessId()),"private controller input channel opens");
            auto physical=camera.hands;physical.present=physical.mounted=1;physical.tick=Fixture::Clock();
            physical.head.orientation[0]=physical.head.orientation[1]=physical.head.orientation[2]=0;physical.head.orientation[3]=1;
            physical.head.flags=HandsIPC::kOrientation|HandsIPC::kPosition;
            physical.hands[1].orientation[0]=physical.hands[1].orientation[1]=physical.hands[1].orientation[2]=0;physical.hands[1].orientation[3]=1;
            input.Publish(physical);ControllerAim aim;
            Check(SnapshotControllerAim(1,aim)&&Dot(aim.direction,{1,0,0})>.99999f,
                "actual controller aim uses the rendered VR world basis even while native throw owns visual IK");
            physical.hands[1].orientation[1]=std::sin(.785398163f);physical.hands[1].orientation[3]=std::cos(.785398163f);
            physical.tick=Fixture::Clock();input.Publish(physical);
            Check(SnapshotControllerAim(1,aim)&&Dot(aim.direction,{0,0,1})>.99999f,
                "right controller rotation maps through VR yaw once, independently of native actor heading");
            const auto replacement=pointer(0x40000);put(replacement,pointer(kPlayerVtable));put(replacement+0x70,model);put(data+0xA2C,replacement);
            Check(!SnapshotControllerAim(1,aim),"actor replacement reusing the same model cannot inherit the old mapped camera");put(data+0xA2C,player);
            frameGeneration=1;
            Check(!SnapshotControllerAim(1,aim),"changed source generation rejects the retained simulation mapping");frameGeneration=0;
            physical.mounted=0;physical.tick=Fixture::Clock();input.Publish(physical);
            Check(!SnapshotControllerAim(1,aim),"unmounted tracking never supplies a grenade pointing certificate");
            const auto component=pointer(0x50000),manager=pointer(0x60000),pool=pointer(0x70000),root=pointer(0x80000),
                generations=pointer(0x90000),grenade=pointer(0xA0000),grenadeModel=pointer(0xB0000),action=pointer(0xC0000);
            put(player+0x88,component);put(component+0x14,1);NativeWeaponOwnership::Record attachment;
            attachment.geometryRef=1;attachment.bone=31;std::memcpy(reinterpret_cast<void*>(component+0x18),&attachment,sizeof(attachment));
            put(base+0x6C99B8,manager);put(manager+0xC,pool);put(pool+4,0x70);put(pool+8,2);put(pool+0xC,root);put(pool+0x10,generations);
            put(grenade,pointer(0x577B74));put(grenade+4,root);put(root+0x60,grenade);put(grenade+0xC0,grenadeModel);put(grenade+0x58,91);
            put(grenadeModel,pointer(0x577864));put(grenadeModel+0xA4,11);put(player+0x358,pointer(0x5508B4));
            NativeWeaponOwnership::HandState hand;hand.enabled=0;hand.attacher=31;std::memcpy(reinterpret_cast<void*>(player+0x358+0x60),&hand,sizeof(hand));
            actionFixture={};actionFixture.valid=true;actionFixture.player=player;actionFixture.action=action;
            actionFixture.actionKind=NativeActionKL::Action::Throw;actionFixture.actionVtable=pointer(0x56665C);actionFixture.movementKind=NativeActionKL::Movement::Stand;
            put(action,actionFixture.actionVtable);put(action+8,player);
            const auto dispatcher=pointer(0xD0000);put(player+0x194,dispatcher);put(dispatcher,pointer(0x54DE4C));
            put(dispatcher+8,player);put(dispatcher+4,action);
            NativeHeldGrenadeVisualKL::Owner grenadeOwner;
            Check(NativeHeldGrenadeVisualKL::Capture(image,actionFixture,player,ReadBytes,grenadeOwner)&&grenadeOwner.item==grenade&&grenadeOwner.root==root,
                  "private grenade fixture includes exact current native dispatcher ownership");
            camera.nativeAction=camera.nativeBodyAction=true;camera.hands.tick=Fixture::Clock();camera.input=camera.hands;
            const auto grenadeSolve=[&](){auto pose=original;PreparedDraw next;next.count=7;next.rawRoot=next.renderRoot=Frame();
                std::array<std::uint8_t,NativeRigMath::MaximumBones> changed{};
                return SolveGpuRig(camera,original,pose.data(),recovered,changed,next)&&next.visual.tracked[0]&&next.visual.tracked[1];};
            Check(grenadeSolve(),"actual GPU rig tracks unreleased owned grenade hands while native Throw keeps gameplay/charge/launch authority");
            put(dispatcher+4,action+4);Check(!grenadeSolve(),"late native action replacement retires held grenade arm authority");put(dispatcher+4,action);
            put(action+0x24,1);Check(!grenadeSolve(),"native launch latch immediately retires grenade arm exception");put(action+0x24,0);
            actionFixture.actionVtable=pointer(0x5668DC);put(action,actionFixture.actionVtable);
            Check(!grenadeSolve(),"incoming ThrowBack does not inherit ordinary held-grenade visual tracking");
            input.Close();installed=false;VirtualFree(memory,0,MEM_RELEASE);
        }
    }
    std::printf("KL1 production two-hand rig: %u checks %s.\n",checks,failures?"FAILED":"PASS");
    return failures?1:0;
}
