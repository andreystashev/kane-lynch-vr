// Actual owned held-item/rigid callbacks. Each native part has its own bone
// WORLD beneath the same MODEL root; preserving that difference is essential
// for a suppressor, scope or other rigid authored firearm part.
#define main UnusedScopePartFixtureMain
#include "native_scope_fire_test.cpp"
#undef main
#include "native_two_hand_math.hpp"
namespace AccessoryPartFixture {
using namespace NativeCameraMath;
CameraPose nativePart,carriedPart;
unsigned rigidNative=0,worldNative=0;
void __thiscall Rigid(void*,void* state,std::uintptr_t){++rigidNative;Fixture::Put(reinterpret_cast<std::uintptr_t>(state)+0x8C,nativePart);}
void __thiscall World(void* state,const CameraPose* pose){++worldNative;carriedPart=*pose;Fixture::Put(reinterpret_cast<std::uintptr_t>(state)+0x8C,*pose);}
bool Near(Vec3 a,Vec3 b){return NativeArmMath::Length(Subtract(a,b))<.001f;}
bool Near(const CameraPose& a,const CameraPose& b){return Near(a.position,b.position)&&Near(a.basis.right,b.basis.right)&&Near(a.basis.up,b.basis.up)&&Near(a.basis.forward,b.basis.forward);}
}
int main() {
    using namespace Fixture;using namespace NativeWeaponKL;using namespace AccessoryPartFixture;
    auto* allocation=VirtualAlloc(nullptr,0x900000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    Check(allocation!=nullptr,"private two-part firearm fixture");if(!allocation)return 1;
    const auto base=reinterpret_cast<std::uintptr_t>(allocation);
    const auto at=[&](std::uintptr_t offset){return static_cast<std::uint32_t>(base+offset);};
    const auto data=at(0x10000),actor=at(0x20000),weapon=at(0x30000),root=at(0x40000),child=at(0x50000),
        childControl=at(0x51000),model=at(0x60000),marker=at(0x70000),markerGeometry=at(0x71000),entry=at(0x80000),state=at(0x90000);
    image=base;installed=true;fixtureTick=1000;renderedFixture=vrDraw=ready=true;player=actor;
    actionState.valid=true;actionState.player=actor;actionState.movementKind=NativeActionKL::Movement::Stand;
    Put(base+kGameData,data);Put(data+0xA2C,actor);Put(actor,at(0x56582C));
    Put(actor+0x358,at(0x5508B4));NativeWeaponOwnership::HandState right;right.item=weapon;right.enabled=1;right.attacher=31;Put(actor+0x358+0x60,right);
    Put(weapon,at(0x577B74));Put(base+0x577B74+0x2D4,at(kSetTarget));Put(weapon+4,root);Put(root+0x60,weapon);Put(weapon+0x58,std::uint32_t(37));Put(weapon+0xC0,model);
    Put(model,at(0x577864));Put(model+0xA4,std::uint32_t(15));Put(model+0x150,marker);Put(marker+4,markerGeometry);Put(markerGeometry+0x60,marker);
    Put(child+0x50,root);Put(child+0x60,childControl);Put(childControl+4,child);Put(entry,at(0x541070));Put(entry+0xF4,child);Put(base+kGpuIndex,std::uint32_t(0));
    const Basis identity{{0,0,1},{0,1,0},{1,0,0}};CameraPose itemFrame{identity,{300,160,-900}};
    const Quat quarter{0,-std::sin(3.14159265358979323846f*.25f),0,std::cos(3.14159265358979323846f*.25f)};
    const CameraPose childLocal{{Rotate(quarter,identity.forward),Rotate(quarter,identity.up),Rotate(quarter,identity.right)},{4,7,2}};
    Put(child,childLocal);Put(entry+0x10,NativeRigMath::ToWorld(itemFrame,{childLocal.basis,childLocal.position}));
    const CameraPose muzzle{{{-1,0,0},{0,1,0},{0,0,1}},{0,0,90}};Put(markerGeometry,muzzle);
    renderedHands={};renderedHands.player=actor;renderedHands.pairId=55;renderedHands.unitsPerMetre=100;renderedHands.tick=fixtureTick;renderedHands.availableForMesh[1]=true;
    renderedHands.rawWrist[1]=itemFrame;renderedHands.finalWrist[1]={identity,{350,170,-860}};renderedHands.wrist[1]=renderedHands.finalWrist[1];
    heldCache={};negativeDrawMemo={};renderedGun[0]={};renderedGun[1]={};originalRigid=AccessoryPartFixture::Rigid;originalWorld=AccessoryPartFixture::World;
    CameraPose canonical;Check(NativeWeaponMath::HeldAttachment(renderedHands.rawWrist[1],renderedHands.finalWrist[1],itemFrame,muzzle,1,100,canonical),"actual item/muzzle marker establishes the held canonical frame");
    const CameraPose mainPart{identity,{0,0,0}};
    const Quat small{0,std::sin(.15f),0,std::cos(.15f)};
    const CameraPose silencer{{Rotate(small,identity.forward),Rotate(small,identity.up),Rotate(small,identity.right)},{45,3,-7}};
    CameraPose expectedMain,expectedSilencer;
    for(unsigned part=0;part<2;++part) {
        const auto& local=part?silencer:mainPart;
        const auto modelRoot=NativeRigMath::ToWorld(itemFrame,{childLocal.basis,childLocal.position});
        nativePart=NativeRigMath::ToWorld(modelRoot,{local.basis,local.position});
        const auto expectedModel=NativeRigMath::ToWorld(canonical,{childLocal.basis,childLocal.position});
        const auto expected=NativeRigMath::ToWorld(expectedModel,{local.basis,local.position});
        RigidBindHook(reinterpret_cast<void*>(entry),nullptr,reinterpret_cast<void*>(state),0);
        Check(worldNative==part+1&&Near(carriedPart,expected),part?"actual cached rigid hook preserves suppressor bone translation/rotation beneath the same weapon model root":"actual first rigid hook maps the native rifle model to the canonical held frame");
        if(part)expectedSilencer=expected;else expectedMain=expected;
    }
    Check(!Near(expectedMain.position,expectedSilencer.position)&&Near(NativeRigMath::ToModel(expectedMain,carriedPart).position,silencer.position),"suppressor stays at its authored nonzero offset rather than collapsing onto the rifle model root");
    // A child draw may use the SAME item owner as its own parent. The old
    // early stop treated that child as the item root and applied wrist remap
    // only, producing a different transform from canonical main geometry.
    Put(child+0x60,weapon);CameraPose childResult;
    const auto expectedChild=NativeRigMath::ToWorld(canonical,{childLocal.basis,childLocal.position});
    const auto beforeChildren=heldOwnerNonRoot.load(),beforeRoots=heldOwnerNonRootToRoot.load();
    Check(MeshFromEntry(entry,reinterpret_cast<const CameraPose*>(entry+0x10),childResult)&&Near(childResult,expectedChild)&&
          heldOwnerNonRoot==beforeChildren+1&&heldOwnerNonRootToRoot==beforeRoots+1,
          "same-owner child ascends to the actual reciprocal item root and uses the same canonical frame");
    Put(child+0x50,std::uint32_t(0));
    Check(!MeshFromEntry(entry,reinterpret_cast<const CameraPose*>(entry+0x10),childResult)&&heldOwnerNonRootToRoot==beforeRoots+1,
          "same-owner child whose parent does not reach item root is not remapped or granted ownership");
    const auto otherWeapon=at(0xA0000),otherRoot=at(0xA1000);
    Put(otherWeapon,at(0x577B74));Put(otherWeapon+4,otherRoot);Put(otherWeapon+0x58,std::uint32_t(38));Put(otherRoot+0x60,otherWeapon);
    Put(actor+0x2D8,at(0x5508B4));NativeWeaponOwnership::HandState otherHand;otherHand.item=otherWeapon;otherHand.enabled=1;otherHand.attacher=32;
    Put(actor+0x2D8+0x60,otherHand);Put(child+0x50,otherRoot);heldCache={};
    Check(!MeshFromEntry(entry,reinterpret_cast<const CameraPose*>(entry+0x10),childResult)&&heldOwnerNonRootToRoot==beforeRoots+1,
          "a child owned by one held weapon cannot be carried through another held weapon's root");
    Put(actor+0x2D8+0x64,std::uint8_t(0));heldCache={};
    Put(child+0x50,root);Put(child+0x60,childControl);
    const auto sourceBytes=*reinterpret_cast<const CameraPose*>(entry+0x10);
    Check(Near(sourceBytes,NativeRigMath::ToWorld(itemFrame,{childLocal.basis,childLocal.position}))&&*reinterpret_cast<const std::uint32_t*>(child+0x50)==root,"native model root/geometry links remain untouched");
    // Native model animation changes between two parts in the SAME stereo
    // pair. Canonical physical placement is fixed; part-local offset remains.
    const Quat turn{0,std::sin(.4f),0,std::cos(.4f)};itemFrame.basis={Rotate(turn,identity.forward),Rotate(turn,identity.up),Rotate(turn,identity.right)};
    const auto animatedModel=NativeRigMath::ToWorld(itemFrame,{childLocal.basis,childLocal.position});Put(entry+0x10,animatedModel);
    nativePart=NativeRigMath::ToWorld(animatedModel,{silencer.basis,silencer.position});
    RigidBindHook(reinterpret_cast<void*>(entry),nullptr,reinterpret_cast<void*>(state),0);
    Check(worldNative==3&&Near(carriedPart,expectedSilencer),"current native model yaw changes between parts cannot cancel the cached suppressor offset");
    renderedGun[1]={};RigidBindHook(reinterpret_cast<void*>(entry),nullptr,reinterpret_cast<void*>(state),0);
    Check(worldNative==4&&Near(carriedPart,expectedSilencer),"a suppressor with nonzero bone offset may be the FIRST draw that establishes the canonical item root");
    const auto oldOrigin=renderedGun[1].shot.origin;const auto oldHand=renderedHands.finalWrist[1];
    const Vec3 wristShift{50,0,0};
    renderedHands.finalWrist[1].position=Add(oldHand.position,wristShift);
    captureImageEye=1;++captureImageEpoch;++drawGeneration;
    RigidBindHook(reinterpret_cast<void*>(entry),nullptr,reinterpret_cast<void*>(state),0);
    Check(worldNative==5&&Near(carriedPart,expectedSilencer),"second-eye suppressor keeps the wrist pose of its stereo pair");
    Check(Near(renderedGun[1].shot.origin,oldOrigin)&&Near(renderedGun[1].shot.hands.finalWrist[1],oldHand),
        "second-eye muzzle and Fire certificate keep the same wrist pose as the visible suppressor");
    captureImageEye=0;++captureImageEpoch;++drawGeneration;++renderedHands.pairId;
    RigidBindHook(reinterpret_cast<void*>(entry),nullptr,reinterpret_cast<void*>(state),0);
    auto movedSilencer=expectedSilencer;movedSilencer.position=Add(movedSilencer.position,wristShift);
    Check(worldNative==6&&Near(carriedPart,movedSilencer),"the next stereo pair moves the suppressor with the newly sampled wrist");
    Check(renderedGun[1].shot.hands.pairId==renderedHands.pairId&&Near(renderedGun[1].shot.origin,Add(oldOrigin,wristShift))&&
        Near(renderedGun[1].shot.hands.finalWrist[1],renderedHands.finalWrist[1]),
        "the next stereo pair rebases the muzzle and Fire certificate with the visible suppressor");
    // Long held foregrip: accepted tiny native/tracking basis roundoff must
    // remain a one-frame error, never accumulate into scaled weapon parts.
    const auto nativeModel=NativeRigMath::ToWorld(itemFrame,{childLocal.basis,childLocal.position});Put(entry+0x10,nativeModel);
    bool everyPart=true;float maximumScaleError=0,maximumShearError=0,maximumSocketError=0;renderedHands.supportHeld=true;
    unsigned expectedWorld=worldNative;
    for(unsigned frame=0;frame<4000;++frame) {
        CameraPose primary{identity,{400,170,-860}},socket{identity,{20,0,0}},lever=primary,right,left;
        lever.position=Add(primary.position,{20,7*std::sin(frame*.017f),15*std::sin(frame*.013f)});
        if(!NativeTwoHandMath::Aim(primary,socket,lever,100,right,left)){everyPart=false;break;}
        constexpr float roundoff=1.000008f;
        right.basis={Scale(right.basis.forward,roundoff),Scale(right.basis.up,roundoff),Scale(right.basis.right,roundoff)};
        right.basis.up=Add(right.basis.up,Scale(right.basis.forward,.000004f));
        renderedHands.finalWrist[1]=right;renderedHands.finalWrist[0]=left;
        ++renderedHands.pairId;++drawGeneration;fixtureTick+=16;renderedHands.tick=fixtureTick;
        nativePart=nativeModel;RigidBindHook(reinterpret_cast<void*>(entry),nullptr,reinterpret_cast<void*>(state),0);const auto main=carriedPart;
        nativePart=NativeRigMath::ToWorld(nativeModel,{silencer.basis,silencer.position});
        RigidBindHook(reinterpret_cast<void*>(entry),nullptr,reinterpret_cast<void*>(state),0);expectedWorld+=2;
        everyPart=everyPart&&worldNative==expectedWorld;
        for(const auto axis:{main.basis.forward,main.basis.up,main.basis.right,carriedPart.basis.forward,carriedPart.basis.up,carriedPart.basis.right})
            maximumScaleError=std::max(maximumScaleError,std::fabs(Dot(axis,axis)-1.f));
        for(const auto basis:{main.basis,carriedPart.basis})maximumShearError=std::max({maximumShearError,
            std::fabs(Dot(basis.forward,basis.up)),std::fabs(Dot(basis.forward,basis.right)),std::fabs(Dot(basis.up,basis.right))});
        const auto local=NativeRigMath::ToModel(main,carriedPart);
        maximumSocketError=std::max(maximumSocketError,NativeArmMath::Length(Subtract(local.position,silencer.position)));
    }
    Check(everyPart,"every rigid part keeps the actual canonical binding through4000 held foregrip swings");
    Check(maximumScaleError<.0002f&&maximumShearError<.00005f&&maximumSocketError<.01f,
        "held foregrip never accumulates scale/shear or separates the authored suppressor from its model");
    // The same native item and marker can survive a movie/loading transition.
    // Old certificates must not return, nor may an authored transitional pose
    // become the first new long-lived certificate.
    const auto oldEpoch=calibrationEpoch.load();
    SetWeaponCalibrationContext(CalibrationContext::Unknown);
    Check(calibrationEpoch==oldEpoch&&!calibrationTransition,"unknown/stall leaves an accepted grip generation intact");
    SetWeaponCalibrationContext(CalibrationContext::Transition);
    SetWeaponCalibrationContext(CalibrationContext::Transition);
    Check(calibrationEpoch==oldEpoch+1&&calibrationTransition,"one explicit transition retires the old generation exactly once");
    SetWeaponCalibrationContext(CalibrationContext::Unknown);
    itemFrame={identity,{300,160,-900}};Put(entry+0x10,NativeRigMath::ToWorld(itemFrame,{childLocal.basis,childLocal.position}));
    renderedHands.rawWrist[1]=itemFrame;renderedHands.finalWrist[1]={identity,{350,170,-860}};
    CameraPose remapped;const auto oldCaptureEpoch=renderedGun[1].calibrationEpoch;
    Check(MeshFromEntry(entry,reinterpret_cast<const CameraPose*>(entry+0x10),remapped)&&renderedGun[1].calibrationEpoch==oldCaptureEpoch,
        "unknown following a transition cannot reopen calibration and preserves native wrist fallback");
    SetWeaponCalibrationContext(CalibrationContext::Gameplay);actionState.action=1;actionState.actionKind=NativeActionKL::Action::MiniAct;
    Check(MeshFromEntry(entry,reinterpret_cast<const CameraPose*>(entry+0x10),remapped)&&renderedGun[1].calibrationEpoch==oldCaptureEpoch,
        "authored action cannot establish a grip after transition");
    actionState.action=0;actionState.actionKind=NativeActionKL::Action::None;
    ++renderedHands.pairId;++drawGeneration;MeshFromEntry(entry,reinterpret_cast<const CameraPose*>(entry+0x10),remapped);
    actionState.action=1;actionState.actionKind=NativeActionKL::Action::Reload;
    ++renderedHands.pairId;++drawGeneration;MeshFromEntry(entry,reinterpret_cast<const CameraPose*>(entry+0x10),remapped);
    actionState.action=0;actionState.actionKind=NativeActionKL::Action::None;
    ++renderedHands.pairId;++drawGeneration;MeshFromEntry(entry,reinterpret_cast<const CameraPose*>(entry+0x10),remapped);
    // No draw occurs at all during this stall: the next gameplay pair must
    // not inherit the one incomplete pre-stall observation.
    SetWeaponCalibrationContext(CalibrationContext::Unknown);SetWeaponCalibrationContext(CalibrationContext::Gameplay);
    for(unsigned frame=0;frame<2;++frame){++renderedHands.pairId;++drawGeneration;
        Check(MeshFromEntry(entry,reinterpret_cast<const CameraPose*>(entry+0x10),remapped)&&renderedGun[1].calibrationEpoch==oldCaptureEpoch,
            "a fresh transition pose uses wrist fallback until three distinct stable pairs");}
    ++renderedHands.pairId;++drawGeneration;
    Check(MeshFromEntry(entry,reinterpret_cast<const CameraPose*>(entry+0x10),remapped)&&renderedGun[1].calibrationEpoch==calibrationEpoch.load(),
        "same item/marker receives a new certificate only after three stable pairs");
    SetWeaponCalibrationContext(CalibrationContext::Unknown);const auto acceptedEpoch=renderedGun[1].calibrationEpoch;
    ++renderedHands.pairId;++drawGeneration;
    Check(MeshFromEntry(entry,reinterpret_cast<const CameraPose*>(entry+0x10),remapped)&&renderedGun[1].calibrationEpoch==acceptedEpoch,
        "temporary unknown keeps the existing approved grip");
    renderedGun[1]={};++renderedHands.pairId;++drawGeneration;
    Check(MeshFromEntry(entry,reinterpret_cast<const CameraPose*>(entry+0x10),remapped)&&!renderedGun[1].calibrationEpoch,
        "unknown cannot capture a new grip when no approved certificate exists");
    SetWeaponCalibrationContext(CalibrationContext::Gameplay);
    SetWeaponCalibrationContext(CalibrationContext::Transition);SetWeaponCalibrationContext(CalibrationContext::Gameplay);
    actionState.movementKind=NativeActionKL::Movement::Vehicle;
    for(unsigned frame=0;frame<3;++frame){itemFrame.position.x+=10;renderedHands.rawWrist[1]=itemFrame;
        renderedHands.rawWrist[1].position.y+=.25f*frame;renderedHands.finalWrist[1].position.x+=10;
        Put(entry+0x10,NativeRigMath::ToWorld(itemFrame,{childLocal.basis,childLocal.position}));
        ++renderedHands.pairId;++drawGeneration;MeshFromEntry(entry,reinterpret_cast<const CameraPose*>(entry+0x10),remapped);}
    Check(renderedGun[1].calibrationEpoch==calibrationEpoch.load(),"seated moving vehicle plus small wrist animation can establish the new certificate after transition");
    // A legitimate but unproved barrel-axis asset uses the existing physical
    // ray fallback. Three stable pairs should settle the lifecycle even when
    // no canonical-axis certificate is possible for that asset.
    SetWeaponCalibrationContext(CalibrationContext::Transition);SetWeaponCalibrationContext(CalibrationContext::Gameplay);
    actionState.movementKind=NativeActionKL::Movement::Stand;Put(model+0xA4,std::uint32_t(99));
    for(unsigned frame=0;frame<3;++frame){++renderedHands.pairId;++drawGeneration;
        MeshFromEntry(entry,reinterpret_cast<const CameraPose*>(entry+0x10),remapped);}
    Check(renderedGun[1].calibrationEpoch==calibrationEpoch.load()&&!renderedGun[1].aligned&&settledBinding[1].epoch==calibrationEpoch.load()&&settledBinding[1].item==weapon,
        "stable physical-ray fallback finishes lifecycle settling without fabricating barrel proof");
    renderedHands.rawWrist[1].position.x+=5;++renderedHands.pairId;++drawGeneration;
    MeshFromEntry(entry,reinterpret_cast<const CameraPose*>(entry+0x10),remapped);
    Check(renderedGun[1].shot.hands.pairId==renderedHands.pairId&&!renderedGun[1].aligned,
        "later unproved-axis updates are not repeatedly delayed by the three-pair transition filter");
    // A second independently owned firearm and a return to the first must
    // settle their own current grip, even within this side's same scene epoch.
    Put(model+0xA4,std::uint32_t(15));Put(otherWeapon+0xC0,model);Put(child+0x50,otherRoot);
    renderedHands.rawWrist[1]=itemFrame;right.item=otherWeapon;Put(actor+0x358+0x60,right);heldCache={};
    for(unsigned frame=0;frame<3;++frame){++renderedHands.pairId;++drawGeneration;
        MeshFromEntry(entry,reinterpret_cast<const CameraPose*>(entry+0x10),remapped);
        if(frame<2)Check(renderedGun[1].shot.weapon!=otherWeapon,"new firearm cannot inherit the previous weapon's settlement");}
    Check(renderedGun[1].shot.weapon==otherWeapon&&renderedGun[1].shot.hands.pairId==renderedHands.pairId&&renderedGun[1].aligned,
        "switch to another proved held firearm calibrates only after its own stable pairs");
    Put(child+0x50,root);right.item=weapon;Put(actor+0x358+0x60,right);heldCache={};
    for(unsigned frame=0;frame<3;++frame){++renderedHands.pairId;++drawGeneration;
        MeshFromEntry(entry,reinterpret_cast<const CameraPose*>(entry+0x10),remapped);}
    Check(renderedGun[1].shot.weapon==weapon&&renderedGun[1].shot.hands.pairId==renderedHands.pairId&&renderedGun[1].aligned,
        "return to the first firearm settles its current grip instead of borrowing another item's capture");
    // This part's intermediate pose is missing, but its assembly and the
    // same frame's accepted item root are still valid.
    const auto& bound=renderedGun[1];
    const auto attachedSilencer=NativeRigMath::ToWorld(bound.attachment,{silencer.basis,silencer.position});
    nativePart=NativeRigMath::ToWorld(bound.nativeAttachment,{silencer.basis,silencer.position});
    auto incomplete=childLocal;incomplete.basis.forward={};Put(child,incomplete);
    Check(MeshFromEntry(entry,&nativePart,remapped)&&Near(remapped,attachedSilencer),
          "a partially updated rigid sniper part retains the current rifle transform");
    const auto nativeShift=Vec3{38,-6,17};
    const auto nextHeldShift=Vec3{-12,16,9};
    auto nextItem=bound.nativeAttachment;
    nextItem.position=Add(nextItem.position,nativeShift);
    renderedHands.rawWrist[1].position=Add(renderedHands.rawWrist[1].position,nativeShift);
    renderedHands.finalWrist[1].position=Add(renderedHands.finalWrist[1].position,nextHeldShift);
    const auto nextModel=NativeRigMath::ToWorld(nextItem,{childLocal.basis,childLocal.position});
    Put(entry+0x10,nextModel);
    nativePart=NativeRigMath::ToWorld(nextItem,{silencer.basis,silencer.position});
    auto nextSilencer=attachedSilencer;nextSilencer.position=Add(nextSilencer.position,nextHeldShift);
    renderedHands.tracked[1]=true;
    ++renderedHands.pairId;++drawGeneration;
    Check(MeshFromEntry(entry,&nativePart,remapped)&&Near(remapped,nextSilencer),
          "suppressor drawn first in a new pair uses the new model WORLD when its owned parent link is temporarily incomplete");
    const auto lastProvedPair=bound.shot.hands.pairId;
    auto missingWorld=nextModel;missingWorld.basis.forward={};
    Put(entry+0x10,missingWorld);
    ++renderedHands.pairId;++drawGeneration;
    Check(MeshFromEntry(entry,&nativePart,remapped)&&Near(remapped,nextSilencer)&&bound.shot.hands.pairId==lastProvedPair,
          "when the entry WORLD is also unavailable the owned suppressor follows the prior grip without publishing a new aim proof");
    Put(entry+0x10,nextModel);
    const auto priorRecenter=renderedHands.recenterAck;
    renderedHands.recenterAck=priorRecenter+1;
    ++renderedHands.pairId;++drawGeneration;
    CameraPose wristOnly;
    Check(NativeWeaponMath::Remap(renderedHands.rawWrist[1],renderedHands.finalWrist[1],nativePart,wristOnly)&&
          MeshFromEntry(entry,&nativePart,remapped)&&Near(remapped,wristOnly)&&!Near(remapped,nextSilencer),
          "a recenter retires the previous child chain and the exact assembly's retained grip");
    renderedHands.recenterAck=priorRecenter;

    const long restoredRecenter=Fixture::recenter;Fixture::recenter=priorRecenter;
    drawControllerAvailable=true;drawVisualAvailable=false;
    drawController.tracked=true;drawController.recenter=priorRecenter;drawController.units=100;
    drawController.coupledItem=weapon;drawController.coupledRoot=root;drawController.coupledModel=model;
    drawController.coupledReference=37;drawController.coupledWrist=renderedHands.finalWrist[1];
    ++renderedHands.pairId;++drawGeneration;
    const auto recoveredCount=meshPoseRecovered.load();
    Check(MeshFromEntry(entry,&nativePart,remapped,0,nullptr,state)&&Near(remapped,nextSilencer)&&
          meshPoseRecovered==recoveredCount+1&&bound.shot.hands.pairId==lastProvedPair,
          "draw-only controller recovery carries the same sniper part when visual hands and a parent link are both unavailable");
    Fixture::recenter=restoredRecenter;drawControllerAvailable=false;drawVisualAvailable=true;
    Put(child,childLocal);
    renderedFixture=false;RigidBindHook(reinterpret_cast<void*>(entry),nullptr,reinterpret_cast<void*>(state),0);
    Check(rigidNative==8007&&worldNative==8006,"retired GPU eye still executes exactly one native rigid bind without accessory remapping");
    const auto extra=at(0xB0000),primitive=at(0xB1000),resource=at(0xB2000),asset=at(0xB3000),
        parts=at(0xB4000),header=at(0xB5000),definitions=at(0xB6000);
    Put(entry+0xF4,root);Put(extra+4,primitive);Put(primitive+0x14,resource|0x80000000u);
    Put(resource+0x3C,std::uint32_t(0));Put(root+0x68,asset|0x80000000u);
    Put(asset+2,std::uint16_t(7));Put(asset+0xC,std::uint8_t(1));Put(asset+0x10,parts|0x80000000u);
    Put(parts+4,header|0x80000000u);Put(header,std::uint32_t(1));Put(header+4,definitions|0x80000000u);
    char clipName[36]="w_bone_clip";Put(definitions+0x1C,clipName);
    NativeWeaponKL::SetPhysicalReloadEnabled(true);
    ReloadDiagnostic::View reload;reload.loader=PhysicalReloadKL::Loader::MagazineAndSlide;
    reload.player=actor;reload.item=weapon;reload.model=model;reload.reference=37;reload.inserted=false;reload.magazineAsset=asset;
    ReloadDiagnostic::Publish(reload);ReloadDiagnostic::Invalidate();
    Fixture::nativeProvider=entry;Fixture::nativeProviderState=state;
    RigidBindHook(reinterpret_cast<void*>(entry),nullptr,reinterpret_cast<void*>(state),extra);
    Check(ReloadDiagnostic::Suppress(),"a detached owned magazine cannot reappear on the rifle when the current hand pose is lost");
    Put(entry+0xF4,child);Put(child+0x50,root);Put(child+0x68,asset|0x80000000u);
    Fixture::nativeProvider=childControl;
    Put(state+0x14,std::uint32_t(childControl));
    RigidBindHook(reinterpret_cast<void*>(entry),nullptr,reinterpret_cast<void*>(state),extra);
    Check(ReloadDiagnostic::Suppress(),"detached magazine on a child binder is hidden in the wrapping native material");
    Put(state+0x14,std::uint32_t(entry));
    Check(!ReloadDiagnostic::Suppress(),"another native material sharing the provider cannot borrow the hide tag");
    Put(child+0x50,otherRoot);
    Check(!ReloadDiagnostic::DetachedClip(entry,extra),"same clip asset under a different gun is preserved");
    Put(entry+0xF4,root);Fixture::nativeProvider=entry;
    Put(weapon+0x58,std::uint32_t(42));
    Check(!ReloadDiagnostic::DetachedClip(entry,extra),"a changed native weapon reference retires the detached-magazine proof");
    Put(weapon+0x58,std::uint32_t(37));reload.inserted=true;ReloadDiagnostic::Publish(reload);
    RigidBindHook(reinterpret_cast<void*>(entry),nullptr,reinterpret_cast<void*>(state),extra);
    Check(!ReloadDiagnostic::Suppress(),"seating the magazine retires the draw suppression");
    SetTrackedReloadContext(true);renderedFixture=true;liveDrawAvailable=true;drawVisualAvailable=true;
    Put(base+0x577B74+0x308,at(0x11DDB0));Put(weapon+0xD8,20u^0xBEEBBEEBu);
    reload.ready=true;reload.ak74=true;reload.tick=fixtureTick;reload.recenter=renderedHands.recenterAck;
    reload.slideTravel=.06f;ReloadDiagnostic::Publish(reload);
    ReloadProps props;
    Check(ReloadDiagnostic::Owns(reload.item)&&!ReloadPropsPending()&&!SnapshotReloadProps(actor,props),
          "AK slider state alone never submits an extra bolt mesh");
    VirtualFree(allocation,0,MEM_RELEASE);
    std::printf("Production distinct rigid firearm parts: %u checks, %u failures; max scale=%g shear=%g socket=%g.\n",checks,failures,maximumScaleError,maximumShearError,maximumSocketError);return failures?1:0;
}
