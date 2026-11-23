// Actual Mesh/world-source production path, native memory read-only; no GPU.
#define main UnusedWorldSourceFixtureMain
#include "native_scope_fire_test.cpp"
#undef main
#include <fstream>
#include <vector>
namespace WorldSourceFixture {
using namespace NativeCameraMath;
bool Near(const CameraPose& a,const CameraPose& b){return NativeArmMath::Length(Subtract(a.position,b.position))<.001f&&
    NativeArmMath::Length(Subtract(a.basis.forward,b.basis.forward))<.001f&&
    NativeArmMath::Length(Subtract(a.basis.up,b.basis.up))<.001f&&NativeArmMath::Length(Subtract(a.basis.right,b.basis.right))<.001f;}
}
int main(int argc,char** argv){
    using namespace Fixture;using namespace NativeWeaponKL;using namespace WorldSourceFixture;
    Check(argc==2,"supported EXE primary input supplied");if(argc!=2)return 1;
    char digest[65]{};std::wstring path;for(const char* p=argv[1];*p;++p)path.push_back(wchar_t(static_cast<unsigned char>(*p)));
    Check(HashFile(path.c_str(),digest)&&IsSupportedExecutableHash(digest),"pinned EXE digest matches before reading signatures");
    std::ifstream input(argv[1],std::ios::binary);std::vector<unsigned char> file((std::istreambuf_iterator<char>(input)),{});
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(file.data()+dos->e_lfanew);
    const auto* sections=IMAGE_FIRST_SECTION(nt);
    const auto readPE=[&](std::uintptr_t address,void* out,std::size_t bytes){
        if(address<0x400000)return false;const auto rva=address-0x400000;
        for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i){if(rva>=sections[i].VirtualAddress&&
            rva+bytes<=sections[i].VirtualAddress+sections[i].SizeOfRawData){
            std::memcpy(out,file.data()+sections[i].PointerToRawData+rva-sections[i].VirtualAddress,bytes);return true;}}return false;};
    Check(NativeWeaponWorldSource::Verify(0x400000,readPE),"actual native source callers/provider dispatch signatures match");
    auto* allocation=VirtualAlloc(nullptr,0x900000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    Check(allocation!=nullptr,"private material/native source layout allocated");if(!allocation)return 1;
    const auto base=reinterpret_cast<std::uintptr_t>(allocation);const auto at=[&](std::uintptr_t n){return std::uint32_t(base+n);};
    const auto data=at(0x10000),actor=at(0x20000),weapon=at(0x30000),root=at(0x40000),model=at(0x50000),entry=at(0x60000),
        state=at(0x70000),list=at(0x80000),rows=at(0x90000),record=at(0xA0000),material=at(0xB0000),descriptor=at(0xC0000);
    image=base;installed=worldSourceProof=true;fixtureTick=1000;renderedFixture=vrDraw=ready=true;player=actor;
    Put(base+kGameData,data);Put(data+0xA2C,actor);Put(actor,at(0x56582C));Put(actor+0x358,at(0x5508B4));
    NativeWeaponOwnership::HandState right;right.item=weapon;right.enabled=1;right.attacher=31;Put(actor+0x358+0x60,right);
    Put(weapon,at(0x577B74));Put(base+0x577B74+0x2D4,at(kSetTarget));Put(weapon+4,root);Put(root+0x60,weapon);
    Put(weapon+0x58,std::uint32_t(37));Put(weapon+0xC0,model);Put(model,at(0x577864));Put(model+0xA4,std::uint32_t(4));
    Put(entry,at(0x541034));Put(base+0x541034+0x18,at(0x71F80));Put(entry+0xF4,root);Put(base+kGpuIndex,std::uint32_t(0));
    CameraPose raw,final;raw.position={70,120,30};final.position={135,155,-80};final.basis={{1,0,0},{0,1,0},{0,0,-1}};
    Put(entry+0x10,raw);Put(entry+0x90,list);Put(list,std::uint32_t(2));Put(list+4,rows);Put(state+0x3C4,std::uint32_t(1));
    CameraPose selected=raw;selected.position.x+=3;
    const Quat rowTurn{0,std::sin(.3f),0,std::cos(.3f)};
    selected.basis={Rotate(rowTurn,raw.basis.forward),Rotate(rowTurn,raw.basis.up),Rotate(rowTurn,raw.basis.right)};Put(rows+0x38,selected);
    renderedHands={};renderedHands.player=actor;renderedHands.pairId=21;renderedHands.recenterAck=7;renderedHands.tick=fixtureTick;
    renderedHands.availableForMesh[1]=renderedHands.active[1]=true;renderedHands.rawWrist[1]=raw;renderedHands.finalWrist[1]=final;
    heldCache={};negativeDrawMemo={};renderedGun[0]={};renderedGun[1]={};nativeProvider=entry;nativeProviderState=state;
    CameraPose output,expected;NativeWeaponMath::Remap(raw,final,selected,expected);
    const auto* rowSource=reinterpret_cast<const CameraPose*>(std::uintptr_t(rows)+0x38);
    Check(Mesh(rowSource,output,state,base+0x7205F)&&Near(output,expected),"actual owned instance WORLD uses current provider instead of subtracting entry offset from row");
    Check(instanceWorldTracked==1,"qualified alternate draw is counted separately from inline source");
    Check(Near(*rowSource,selected),"native selected source pose remains byte-equivalent");
    const auto muzzle=at(0x150000),muzzleGeometry=at(0x151000);CameraPose localMuzzle;localMuzzle.basis={{0,0,1},{0,1,0},{1,0,0}};localMuzzle.position={50,0,0};
    Put(model+0x150,muzzle);Put(muzzle+4,muzzleGeometry);Put(muzzleGeometry+0x60,muzzle);Put(muzzleGeometry,localMuzzle);
    Check(Mesh(rowSource,output,state,base+0x7205F)&&Near(output,expected)&&!renderedGun[1].shot.weapon,
          "invalid actor action keeps selected-instance wrist remap without capturing a calibration");
    actionState.valid=true;actionState.player=actor;actionState.movementKind=NativeActionKL::Movement::Stand;
    CameraPose canonical;
    Check(NativeWeaponMath::HeldAttachment(raw,final,selected,localMuzzle,1,100,canonical),"selected instance has a proved native barrel/grip frame");
    Check(Mesh(rowSource,output,state,base+0x7205F)&&Near(output,canonical),
          "canonical firearm attachment is derived from the selected instance model root, not unrelated inline entry root");
    Check(Mesh(rowSource,output,state,base+0x7205F)&&Near(output,canonical),"later lighting material reuses the same canonical instance frame");
    Put(model+0x150,std::uint32_t(0));renderedGun[1]={};
    Put(state+0x3C4,std::uint32_t(0));Check(!Mesh(rowSource,output,state,base+0x7205F),"wrong selected native instance cannot inherit provider");Put(state+0x3C4,std::uint32_t(1));
    nativeProvider=0;Check(!Mesh(rowSource,output,state,base+0x7205F),"closed material scope is required");nativeProvider=entry;
    Check(!Mesh(rowSource,output,state,base+0x72060),"unproved native caller remains untouched");
    Put(root+0x60,actor);Check(!Mesh(rowSource,output,state,base+0x7205F),"foreign geometry cannot borrow an owned scoped source");Put(root+0x60,weapon);
    Put(entry,at(0x540EE0));Put(base+0x540EE0+0x18,at(0x72E00));Put(state+0x14,record);Put(record+0xC,entry);Put(record+4,material);
    Put(material+0x14,std::uint32_t(descriptor|0x80000000));Put(descriptor+2,std::uint16_t(8));Put(descriptor+0xD,std::uint8_t(1));
    CameraPose aligned=raw;aligned.basis={{1,0,0},{0,1,0},{0,0,-1}};NativeWeaponMath::Remap(raw,final,aligned,expected);
    NativeWeaponWorldSource::Source derived;
    Check(NativeWeaponWorldSource::Resolve(base,state,entry,&aligned,base+0x72F6A,true,ReadBytes,derived)&&
          MeshFromEntry(entry,&aligned,output)&&Near(output,expected),"read-only research helper proves owned derived stack WORLD without enabling global billboard fallback");
    Check(!Mesh(&aligned,output,state,base+0x72F6A)&&viewAlignedWorldTracked==0,"unproved gun use keeps the view-aligned runtime path disabled");
    Put(descriptor+0xD,std::uint8_t(3));Check(!NativeWeaponWorldSource::Resolve(base,state,entry,&aligned,base+0x72F6A,true,ReadBytes,derived),"unknown descriptor mode remains native");Put(descriptor+0xD,std::uint8_t(1));
    aligned.position.x+=1;Check(!NativeWeaponWorldSource::Resolve(base,state,entry,&aligned,base+0x72F6A,true,ReadBytes,derived),"derived pose cannot replace original translation");aligned.position.x-=1;
    Put(rows,aligned);Check(!Mesh(reinterpret_cast<const CameraPose*>(rows),output,state,base+0x72F6A),"heap impostor cannot inherit native stack WORLD source");
    nativeProviderState=state+4;Check(!Mesh(&aligned,output,state,base+0x72F6A),"another material state cannot inherit current dispatch");
    // The real FF charge path keeps IKHAND naming the firearm, while its
    // separate grenade geometry occupies the native wrist attachment.
    const auto grenade=at(0xD0000),grenadeRoot=at(0xE0000),grenadeModel=at(0xF0000),component=at(0x100000),
        manager=at(0x110000),pool=at(0x120000),generations=at(0x130000),dispatcher=at(0x1E0000);
    Put(actor+0x194,dispatcher);Put(dispatcher,at(0x54DE4C));Put(dispatcher+4,std::uint32_t(0));Put(dispatcher+8,actor);
    Put(actor+0x88,component);Put(component+0x14,std::uint32_t(1));NativeWeaponOwnership::Record attachment;
    attachment.geometryRef=1;attachment.bone=31;Put(component+0x18,attachment);
    Put(base+0x6C99B8,manager);Put(manager+0xC,pool);Put(pool+4,std::uint32_t(0x70));Put(pool+8,std::uint32_t(2));
    Put(pool+0xC,grenadeRoot);Put(pool+0x10,generations);Put(generations,std::uint16_t(0));
    Put(grenade,at(0x577B74));Put(grenade+4,grenadeRoot);Put(grenade+0xC0,grenadeModel);Put(grenade+0x58,std::uint32_t(91));
    Put(grenadeRoot+0x60,grenade);Put(grenadeModel,at(0x577864));Put(grenadeModel+0xA4,std::uint32_t(11));
    Put(entry,at(0x541070));Put(entry+0xF4,grenadeRoot);CameraPose nativeGrenade=raw;nativeGrenade.position=Add(raw.position,{0,6,2});Put(entry+0x10,nativeGrenade);
    actionState={};actionState.valid=true;actionState.player=actor;actionState.movementKind=NativeActionKL::Movement::BaseMove;
    heldCache={};negativeDrawMemo={};NativeHeldGrenadeVisualKL::Owner grenadeProof;
    Check(NativeHeldGrenadeVisualKL::Capture(base,actionState,actor,ReadBytes,grenadeProof)&&grenadeProof.item==grenade&&grenadeProof.side==1,
          "actual FF grenade wrist attachment is proved even while IKHAND still names the firearm");
    NativeWeaponMath::Remap(raw,final,nativeGrenade,expected);
    Check(MeshFromEntry(entry,reinterpret_cast<const CameraPose*>(entry+0x10),output)&&Near(output,expected)&&heldGrenadeMeshTracked==1,
          "actual grenade mesh follows tracked wrist with authored local grip offset, without touching grenade target or physics");
    Check(*reinterpret_cast<const std::uint32_t*>(actor+0x358+0x60)==weapon&&Near(*reinterpret_cast<const CameraPose*>(entry+0x10),nativeGrenade),
          "native firearm hand ownership and grenade source pose stay unchanged");
    attachment.bone=34;Put(component+0x18,attachment);
    Check(!NativeHeldGrenadeVisualKL::Capture(base,actionState,actor,ReadBytes,grenadeProof)&&
          !MeshFromEntry(entry,reinterpret_cast<const CameraPose*>(entry+0x10),output),"launched/detached/stowed grenade cannot inherit tracked wrist rendering");
    attachment.bone=31;Put(component+0x18,attachment);actionState.action=at(0x140000);actionState.actionKind=NativeActionKL::Action::Throw;
    actionState.actionVtable=at(0x5668DC);
    Check(!NativeHeldGrenadeVisualKL::Capture(base,actionState,actor,ReadBytes,grenadeProof),"accepted incoming ThrowBack keeps its original distinct visual/physics path");

    // Held .45 live sample PID 25948: two attached objects (rifle on 34,
    // grenade FF category 11 on primitive 25), action=None, movement=BaseMove,
    // and *both* IKHAND item/enabled fields empty. 25 is only a candidate:
    // the current costume's resource lookup must also name RIGHT_HAND.
    const auto bodyRoot=at(0x160000),bodyModel=at(0x170000),lookup=at(0x180000),resource=at(0x190000),
        parts=at(0x1A0000),skeleton=at(0x1B0000),definitions=at(0x1C0000),primitiveTable=at(0x1D0000),rifleRoot=grenadeRoot-0x70;
    const auto tagged=[](std::uint32_t pointer){return pointer|0x80000000u;};
    Check((bodyRoot&0x80000000u)==0,"private x86 fixture is representable as a native tagged resource");
    Put(actor+4,bodyRoot);Put(actor+0x70,bodyModel);Put(bodyRoot+0x68,tagged(resource));
    Put(bodyModel+0xC,lookup);Put(bodyModel+0x4A0,std::uint32_t(64));Put(lookup+24,std::uint8_t(12));
    Put(resource+2,std::uint16_t(7));Put(resource+0xC,std::uint8_t(1));Put(resource+0x10,tagged(parts));
    Put(parts+4,tagged(skeleton));Put(skeleton,std::uint32_t(64));Put(skeleton+4,tagged(definitions));
    Put(skeleton+0x14,tagged(primitiveTable));Put(primitiveTable+25,std::uint8_t(12));
    std::memcpy(reinterpret_cast<void*>(definitions+12*64+0x1C),"RIGHT_HAND",sizeof("RIGHT_HAND"));
    Put(actor+0x2D8,at(0x5508B4));NativeWeaponOwnership::HandState emptyLeft;emptyLeft.attacher=32;
    NativeWeaponOwnership::HandState emptyRight;emptyRight.attacher=31;
    Put(actor+0x2D8+0x60,emptyLeft);Put(actor+0x358+0x60,emptyRight);
    NativeWeaponOwnership::Record rifleAttachment,heldAttachment;
    rifleAttachment.geometryRef=1;rifleAttachment.bone=34;
    heldAttachment.geometryRef=2;heldAttachment.bone=25;
    Put(component+0x14,std::uint32_t(2));Put(component+0x18,rifleAttachment);Put(component+0x18+sizeof(rifleAttachment),heldAttachment);
    Put(pool+0xC,rifleRoot);Put(generations,std::uint16_t(0));Put(generations+2,std::uint16_t(0));
    Put(rifleRoot+0x60,weapon);Put(weapon+4,rifleRoot);
    actionState={};actionState.valid=true;actionState.player=actor;actionState.movementKind=NativeActionKL::Movement::BaseMove;
    heldCache={};negativeDrawMemo={};grenadeProof={};
    Check(NativeHeldGrenadeVisualKL::Capture(base,actionState,actor,ReadBytes,grenadeProof)&&grenadeProof.item==grenade&&grenadeProof.side==1,
          "live-shaped empty IKHAND grenade is proved through primitive 25 and current RIGHT_HAND skeleton");
    NativeWeaponMath::Remap(raw,final,nativeGrenade,expected);
    Check(MeshFromEntry(entry,reinterpret_cast<const CameraPose*>(entry+0x10),output)&&Near(output,expected),
          "real FF grenade draw follows tracked right wrist with two empty IKHANDs and rifle stowed");
    Put(dispatcher+4,at(0x140000));heldCache={};negativeDrawMemo={};
    Check(!NativeHeldGrenadeVisualKL::Capture(base,actionState,actor,ReadBytes,grenadeProof),
          "dispatcher action transition invalidates the stale action=None certificate");
    Put(dispatcher+4,std::uint32_t(0));Put(dispatcher+8,at(0x20004));heldCache={};negativeDrawMemo={};
    Check(!NativeHeldGrenadeVisualKL::Capture(base,actionState,actor,ReadBytes,grenadeProof),
          "foreign dispatcher owner cannot lend grenade attachment to local player");
    Put(dispatcher+8,actor);heldCache={};negativeDrawMemo={};
    NativeWeaponOwnership::HandState activeGrenade=emptyRight;activeGrenade.item=grenade;activeGrenade.enabled=1;
    Put(actor+0x358+0x60,activeGrenade);heldCache={};negativeDrawMemo={};
    const auto priorGrenadeDraws=heldGrenadeMeshTracked.load();
    Check(MeshFromEntry(entry,reinterpret_cast<const CameraPose*>(entry+0x10),output)&&Near(output,expected)&&
          heldGrenadeMeshTracked==priorGrenadeDraws+1,"active IKHAND grenade retains the same wrist mapping and dedicated grenade draw path");
    Put(actor+0x358+0x60,emptyRight);heldCache={};negativeDrawMemo={};
    Put(generations+2,std::uint16_t(1));heldCache={};negativeDrawMemo={};
    Check(!NativeHeldGrenadeVisualKL::Capture(base,actionState,actor,ReadBytes,grenadeProof)&&
          !MeshFromEntry(entry,reinterpret_cast<const CameraPose*>(entry+0x10),output),"recycled grenade ZREF never obtains the tracked wrist");
    Put(generations+2,std::uint16_t(0));Put(grenadeRoot+0x60,weapon);heldCache={};negativeDrawMemo={};
    Check(!NativeHeldGrenadeVisualKL::Capture(base,actionState,actor,ReadBytes,grenadeProof)&&
          !MeshFromEntry(entry,reinterpret_cast<const CameraPose*>(entry+0x10),output),"another item's owner cannot borrow grenade attachment");
    Put(grenadeRoot+0x60,grenade);Put(grenade+4,rifleRoot);heldCache={};negativeDrawMemo={};
    Check(!NativeHeldGrenadeVisualKL::Capture(base,actionState,actor,ReadBytes,grenadeProof),"grenade item must own the exact attached root");
    Put(grenade+4,grenadeRoot);std::memcpy(reinterpret_cast<void*>(definitions+12*64+0x1C),"LEFT_HAND",sizeof("LEFT_HAND"));
    heldCache={};negativeDrawMemo={};
    Check(!NativeHeldGrenadeVisualKL::Capture(base,actionState,actor,ReadBytes,grenadeProof)&&
          !MeshFromEntry(entry,reinterpret_cast<const CameraPose*>(entry+0x10),output),"primitive 25 must resolve to the current named RIGHT_HAND");
    std::memcpy(reinterpret_cast<void*>(definitions+12*64+0x1C),"RIGHT_HAND",sizeof("RIGHT_HAND"));
    Put(grenade,at(0x54EA4C));heldCache={};negativeDrawMemo={};
    Check(!NativeHeldGrenadeVisualKL::Capture(base,actionState,actor,ReadBytes,grenadeProof),"unproved item vtable is not accepted as FF grenade");
    Put(grenade,at(0x577B74));actionState.action=at(0x140000);actionState.actionKind=NativeActionKL::Action::Throw;
    actionState.actionVtable=at(0x5668DC);
    Check(!NativeHeldGrenadeVisualKL::Capture(base,actionState,actor,ReadBytes,grenadeProof),"incoming ThrowBack remains separate even with a named wrist attachment");
    VirtualFree(allocation,0,MEM_RELEASE);
    std::printf("Native weapon indirect WORLD production: %u checks, %u failures; CPU only.\n",checks,failures);return failures?1:0;
}
