// Reuse the existing isolated native-weapon callback fixtures and their
// runtime substitutes; run only the real hip ownership/cache/draw adapter.
// The renamed scope fixture entry is deliberately never executed here.
#define main UnusedScopeFireFixtureMain
#include "native_scope_fire_test.cpp"
#undef main
int main() {
    using namespace Fixture;using namespace NativeWeaponKL;
    auto* allocation=VirtualAlloc(nullptr,0x900000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    Check(allocation!=nullptr,"private hip production fixture allocation");if(!allocation)return 1;
    const auto base=reinterpret_cast<std::uintptr_t>(allocation);
    const auto ptr=[&](std::uintptr_t off){return static_cast<std::uint32_t>(base+off);};
    const auto data=ptr(0x10000),actor=ptr(0x20000),model=ptr(0x30000),component=ptr(0x40000),lookup=ptr(0x50000),playerRoot=ptr(0x60000),group=ptr(0x70000),entry=ptr(0x80000),child=ptr(0x90000);
    const auto manager=ptr(0xA0000),pool=ptr(0xA1000),geometryData=ptr(0xA2000),generations=ptr(0xA3000),geometry=geometryData+6*0x70;
    const auto modelResource=ptr(0xC0000),parts=ptr(0xC1000),header=ptr(0xC2000),definitions=ptr(0xC3000),primitives=ptr(0xC4000),state=ptr(0xE0000);
    image=base;installed=true;player=actor;fixtureTick=1000;renderedFixture=true;
    Put(base+kGameData,data);Put(data+0xA2C,actor);Put(actor,ptr(0x56582C));Put(actor+4,playerRoot);Put(actor+0x70,model);Put(actor+0x88,component);
    Put(base+0x6C99B8,manager);Put(manager+0xC,pool);Put(pool+4,std::uint32_t(0x70));Put(pool+8,std::uint32_t(8));Put(pool+0xC,geometryData);Put(pool+0x10,generations);
    for(unsigned i=0;i<8;++i)Put(generations+i*2,std::uint16_t(3));
    Put(model+0xC,lookup);Put(model+0x4A0,std::uint32_t(20));Put(lookup+63,std::uint8_t(6));Put(playerRoot+0x68,std::uint32_t(10));
    const std::uint32_t resources[]={modelResource,parts,header,definitions,primitives};for(unsigned i=0;i<5;++i)Put(base+0x71F308+(10+i)*4,resources[i]);
    Put(modelResource+2,std::uint16_t(7));Put(modelResource+0xC,std::uint8_t(1));Put(modelResource+0x10,std::uint32_t(11));Put(parts+4,std::uint32_t(12));
    Put(header,std::uint32_t(20));Put(header+4,std::uint32_t(13));Put(header+0x14,std::uint32_t(14));Put(primitives+35,std::uint8_t(6));
    const char name[16]="hip attacher";Put(definitions+6*64+0x1C,name);Put(base+0x6319A0,std::uint32_t(35));Put(base+0x6319A4,ptr(0x5385B8));Put(base+0x5385B8,name);
    const char pelvisName[16]="PELVIS";Put(base+0x6318C0,std::uint32_t(2));Put(base+0x6318C4,ptr(0x53870C));Put(base+0x53870C,pelvisName);
    NativeWeaponOwnership::Record record;record.geometryRef=3*8+6+1;record.bone=35;Put(component+0x14,std::uint32_t(1));Put(component+0x18,record);
    Put(geometry+0x60,group);Put(group,ptr(0x5462CC));Put(group+4,geometry);Put(child+0x50,geometry);
    Put(entry,ptr(0x541070));Put(entry+0xF4,child);
    hipAttachmentProof=NativeWeaponOwnership::VerifyHipImage(base,ReadBytes);Check(hipAttachmentProof,"production hip gate uses the exact pinned primitive metadata");
    renderedHands={};renderedHands.player=actor;renderedHands.pairId=55;renderedHands.unitsPerMetre=100;renderedHands.bodyAvailableForMesh=true;
    renderedHands.rawRoot.basis={{0,0,1},{0,1,0},{1,0,0}};renderedHands.rawRoot.position={300,160,-900};renderedHands.renderRoot=renderedHands.rawRoot;
    const Quat q{0,std::sin(.7f),0,std::cos(.7f)};const auto raw=renderedHands.rawRoot;
    renderedHands.renderRoot.basis={Rotate(q,raw.basis.forward),Rotate(q,raw.basis.up),Rotate(q,raw.basis.right)};renderedHands.renderRoot.position=Add(raw.position,{12,-8,5});
    const auto source=NativeRigMath::ToWorld(raw,{raw.basis,{20,90,-10}});Put(state+0x8C,source);Put(entry+0x10,CameraPose{});
    heldCache={};negativeDrawMemo={};
    Check(CurrentHeld().hipGroup[0]==group&&!CurrentHeld().stowedWeapon[0]&&!CurrentHeld().handGroup[0],"actual CurrentHeld discovers a named hip GROUP without falsely classifying it as a gun or wrist accessory");
    CameraPose output;
    Check(MeshFromEntry(entry,reinterpret_cast<const CameraPose*>(state+0x8C),output),"actual MeshFromEntry follows a rigid descendant of the attached hip GROUP");
    const auto localBefore=NativeRigMath::ToModel(raw,source),localAfter=NativeRigMath::ToModel(renderedHands.renderRoot,output);
    Check(Equal(localBefore.position,localAfter.position)||NativeArmMath::Length(Subtract(localBefore.position,localAfter.position))<.001f,"actual draw preserves the native belt offset while carrying the VR body yaw/translation");
    Check(!std::memcmp(reinterpret_cast<void*>(state+0x8C),&source,sizeof(source))&&
          *reinterpret_cast<const std::uint32_t*>(geometry+0x60)==group,"native WORLD and attachment source are unchanged by the draw remap");
    Check(Snapshot().hipAccessoryOwned==1&&Snapshot().hipAccessoryTracked==1,"new hip counters identify an actually applied draw rather than a guessed nearby object");
    renderedHands.bodyAvailableForMesh=false;Check(!MeshFromEntry(entry,reinterpret_cast<const CameraPose*>(state+0x8C),output)&&Snapshot().hipAccessoryRejected==1,"missing current body authorization retains the native equipment");renderedHands.bodyAvailableForMesh=true;
    renderedHands.player=actor+4;Check(!MeshFromEntry(entry,reinterpret_cast<const CameraPose*>(state+0x8C),output),"another player's visual frame cannot move the local equipment");renderedHands.player=actor;
    Put(generations+6*2,std::uint16_t(4));Check(!MeshFromEntry(entry,reinterpret_cast<const CameraPose*>(state+0x8C),output),"geometry generation recycling is rechecked immediately before remap");Put(generations+6*2,std::uint16_t(3));
    hipAttachmentProof=false;Check(!MeshFromEntry(entry,reinterpret_cast<const CameraPose*>(state+0x8C),output),"disabling the verified hip capability cannot reuse a cached positive GROUP");hipAttachmentProof=true;
    renderedFixture=false;Check(!MeshFromEntry(entry,reinterpret_cast<const CameraPose*>(state+0x8C),output),"UI or missing rendered eye authorization retains the native equipment");renderedFixture=true;
    auto detached=record;detached.bone=25;Put(component+0x18,detached);Check(!MeshFromEntry(entry,reinterpret_cast<const CameraPose*>(state+0x8C),output),"a detached or reassigned hip slot cannot borrow its old draw identity");Put(component+0x18,record);
    Put(group,ptr(0x57A79C));Check(!MeshFromEntry(entry,reinterpret_cast<const CameraPose*>(state+0x8C),output),"the actual rappelling cable class never receives the hip attachment delta");
    Put(group,ptr(0x5462CC));Put(lookup,std::uint8_t(1));Put(primitives+2,std::uint8_t(1));Put(definitions+64+0x1C,pelvisName);
    record.bone=2;Put(component+0x18,record);heldCache={};negativeDrawMemo={};
    Check(CurrentHeld().hipGroup[0]==group&&Snapshot().hipAccessoryPrimitive==2,"actual native primitive2 PELVIS GROUP is discovered separately from primitive35");
    Check(MeshFromEntry(entry,reinterpret_cast<const CameraPose*>(state+0x8C),output),"exact PELVIS group follows current body even with no hip-attacher record");
    VirtualFree(allocation,0,MEM_RELEASE);
    std::printf("Production named hip attachment: %u checks, %u failures; no game/runtime.\n",checks,failures);return failures?1:0;
}
