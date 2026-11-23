// Executes the actual pinned native shader-package dispatch bytes in private
// memory, with only external renderer/native callbacks substituted. No game,
// GPU, headset, hooks, inputs, saves or configs are opened.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
namespace ShadowFixture {inline DWORD tick=1000;inline DWORD Clock(){return tick;}}
#define GetTickCount ShadowFixture::Clock
#include "native_hands_kl.cpp"
#undef GetTickCount
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>
namespace ShadowFixture {
using namespace NativeCameraMath;using namespace NativeHandsKL;
unsigned checks=0,failed=0,draws=0,guards=0;std::uintptr_t entryAt=0,acceptedState=0;
std::uintptr_t lastBound=0;float lastHeadScale=0;NativeActionKL::State action;
bool nestedRequested=false;
std::uintptr_t nestedPackage=0,nestedContexts=0,nestedEntries=0;
std::uintptr_t expectedProvider=0,expectedPackage=0;
void Check(bool value,const char* why){++checks;if(!value){++failed;std::printf("FAIL %s\n",why);}}
[[noreturn]]void Unexpected(){std::puts("FAIL unrelated game/runtime entry");std::abort();}
template<class T>void Put(std::uintptr_t at,const T& value){std::memcpy(reinterpret_cast<void*>(at),&value,sizeof(value));}
struct FileImage {
    std::vector<unsigned char> bytes;struct Section{std::uint32_t virtualSize,rva,rawSize,raw;};std::vector<Section> sections;
    template<class T>bool Get(std::size_t at,T& out)const{if(at>bytes.size()||sizeof(T)>bytes.size()-at)return false;std::memcpy(&out,bytes.data()+at,sizeof(T));return true;}
    bool Load(const char* path){std::ifstream f(path,std::ios::binary);if(!f)return false;f.seekg(0,std::ios::end);auto size=f.tellg();
        if(size<=0||size>64*1024*1024)return false;bytes.resize(static_cast<std::size_t>(size));f.seekg(0);f.read(reinterpret_cast<char*>(bytes.data()),size);
        std::uint32_t pe=0,sig=0;std::uint16_t count=0,optional=0,magic=0;
        if(!f||!Get(0x3c,pe)||!Get(pe,sig)||sig!=0x4550||!Get(pe+6,count)||count>96||!Get(pe+20,optional)||!Get(pe+24,magic)||magic!=0x10b)return false;
        for(unsigned i=0;i<count;++i){Section s{};if(!Get(pe+24+optional+40*i+8,s))return false;sections.push_back(s);}return true;}
    bool Read(std::uintptr_t at,void* out,std::size_t count)const{if(at<0x400000)return false;const auto rva=at-0x400000;
        for(const auto& s:sections)if(rva>=s.rva&&rva-s.rva<s.rawSize){const auto offset=rva-s.rva;
            if(count>s.rawSize-offset||s.raw+offset>bytes.size()||count>bytes.size()-s.raw-offset)return false;
            std::memcpy(out,bytes.data()+s.raw+offset,count);return true;}
        return false;}
};
void __thiscall NativeBind(void* entry,void* state,std::uintptr_t){
    const auto at=reinterpret_cast<std::uintptr_t>(entry),target=reinterpret_cast<std::uintptr_t>(state);
    const auto cache=*reinterpret_cast<const std::uint32_t*>(at+0x108);Put(target+0x3BC,cache+0x90u);
    Put(target+0x8C,*reinterpret_cast<const CameraPose*>(at+0x10));
}
unsigned __thiscall PassCount(void*,void*){return 1;}
void __thiscall PassBegin(void*,void*,void*){}
void __thiscall Prepare(void*,void*,void*,void*,void* state){BindPaletteHook(reinterpret_cast<void*>(entryAt),nullptr,state,0);}
void __thiscall PassEnd(void*){}
void __thiscall Draw(void*,void* state){++draws;const auto at=reinterpret_cast<std::uintptr_t>(state);
    std::uintptr_t currentProvider=0;unsigned currentPass=99;
    Check(ReadCurrentNativeEntry(at,currentProvider,&currentPass)&&currentProvider==expectedProvider&&
          currentPass==*reinterpret_cast<const std::uint8_t*>(expectedPackage+0x62),
          "actual48F0A0 callback exposes only current record provider and exact package pass");
    lastBound=*reinterpret_cast<const std::uint32_t*>(at+0x3BC);lastHeadScale=*reinterpret_cast<const float*>(lastBound+2*48);
    if(currentPass==9){
        Check(!PostfilterForegroundActive(),"unrelated current provider cannot borrow body mask authorization");
        const auto record=*reinterpret_cast<const std::uint32_t*>(at+0x14);
        Put(record+0xC,static_cast<std::uint32_t>(paletteLease.entry));
        Check(PostfilterForegroundActive(),"actual binder lease permits exact player opaque mask without a support grip");
        boundShadow=true;Check(!PostfilterForegroundActive(),"shadow staging cannot authorize foreground mask");boundShadow=false;
        ++boundGeneration;Check(!PostfilterForegroundActive(),"old body draw generation rejects mask");--boundGeneration;
        const auto previousAccepted=acceptedState;acceptedState=0;Check(!PostfilterForegroundActive(),"another camera state cannot borrow a body mask");acceptedState=previousAccepted;
        techniqueObservation.pass=10;Check(!PostfilterForegroundActive(),"alpha body mask remains native");techniqueObservation.pass=9;
        Put(record+0xC,static_cast<std::uint32_t>(expectedProvider));
    }
    if(nestedRequested) {
        nestedRequested=false;Put(nestedPackage+0x62,std::uint8_t(2));
        TechniqueDrawHook(reinterpret_cast<void*>(nestedPackage),nullptr,reinterpret_cast<void*>(nestedContexts),reinterpret_cast<void*>(nestedEntries),1,state);
        Put(nestedPackage+0x62,std::uint8_t(0));
        const auto resumed=*reinterpret_cast<const std::uint32_t*>(at+0x3BC);
        Check(paletteLease.active&&paletteLease.depth==1&&resumed==reinterpret_cast<std::uintptr_t>(gpuPalette.data())&&*reinterpret_cast<const float*>(resumed+2*48)<.001f,
              "nested shadow return resumes the outer masked normal pose for remaining materials");
    }
}
}
namespace NativeWeaponKL {
bool CaptureHeldSupport(std::uintptr_t,float,HeldSupport&,bool){return false;}
bool PreferReloadSlide(const HeldSupport&,const NativeCameraMath::CameraPose&,const NativeCameraMath::CameraPose&,const NativeCameraMath::CameraPose*,std::uint32_t){return false;}
bool ReloadHandBusy(std::uintptr_t){return false;}
bool ReloadHandTarget(std::uintptr_t,unsigned,const NativeCameraMath::CameraPose&,NativeCameraMath::CameraPose&){return false;}
}
namespace VRBridge {bool TeamHudVisible(){return false;}bool WeaponMenuVisible(){return false;}bool Ready(){return true;}}
namespace NativeActionKL {bool InitializeVerifiedImage(std::uintptr_t){ShadowFixture::Unexpected();}State Current(){return ShadowFixture::action;}bool PreserveRig(){ShadowFixture::Unexpected();}}
namespace NativeCameraKL {bool CinematicActive(){return false;}bool DrawStateActive(std::uintptr_t state,bool*,bool){++ShadowFixture::guards;return state==ShadowFixture::acceptedState;}}
namespace NativeBodyKL {void ResetFollow(){ShadowFixture::Unexpected();}bool Adjust(std::uintptr_t,const NativeCameraMath::CameraPose&,NativeCameraMath::Vec3,NativeCameraMath::Vec3,NativeCameraMath::Vec3,NativeCameraMath::CameraPose&,std::uint64_t){ShadowFixture::Unexpected();}
bool AdjustAction(std::uintptr_t,const NativeActionKL::State&,const NativeCameraMath::CameraPose&,NativeCameraMath::Vec3,NativeCameraMath::Vec3,NativeCameraMath::Vec3,NativeCameraMath::CameraPose&,std::uint64_t){ShadowFixture::Unexpected();}}
extern "C" {
MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*){ShadowFixture::Unexpected();}MH_STATUS WINAPI MH_QueueEnableHook(LPVOID){ShadowFixture::Unexpected();}
MH_STATUS WINAPI MH_ApplyQueued(){ShadowFixture::Unexpected();}MH_STATUS WINAPI MH_DisableHook(LPVOID){ShadowFixture::Unexpected();}MH_STATUS WINAPI MH_RemoveHook(LPVOID){ShadowFixture::Unexpected();}
}
int main(int argc,char** argv) {
    using namespace ShadowFixture;
    static_assert(sizeof(void*)==4,"pinned native dispatch is x86");if(argc!=2)return 2;
    FileImage executable;Check(executable.Load(argv[1]),"supported EXE opened read-only");
    const auto read=[&](std::uintptr_t at,void* out,std::size_t bytes){return executable.Read(at,out,bytes);};
    Check(NativeShadowPassKL::Verify(0x400000,read),"actual EXE proves named ShadowMap2/ShadowDrop3, selected table+C, selector+62 and native ABI");
    constexpr std::uintptr_t base=0x2C000000;
    auto* memory=VirtualAlloc(reinterpret_cast<void*>(base),0x900000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    Check(memory==reinterpret_cast<void*>(base),"private native image/renderer-state allocated");if(!memory||failed)return 1;
    image=base;installed=true;
    constexpr std::uintptr_t data=base+0x10000,player=base+0x20000,geometry=base+0x30000,model=base+0x40000,cache=base+0x50000,
        entry=base+0x60000,lookup=base+0x70000,parentsAt=base+0x71000,rest=base+0x72000,package=base+0x80000,
        packageVtable=base+0x81000,state=base+0x90000,renderer=base+0xA1000,rendererVtable=base+0xA2000,
        item=base+0xA3000,provider=base+0xA4000,providerVtable=base+0xA5000,contexts=base+0xA6000,entries=base+0xA7000,itemVtable=base+0xA8000;
    Put(base+kGameData,data);Put(data+0xA2C,player);Put(player,base+kPlayerVtable);Put(player+0x70,model);
    Put(entry,base+kBonesVtable);Put(entry+0xF4,geometry);Put(geometry+0x60,player);Put(entry+0x108,cache);Put(entry+0x114,4u);
    Put(model+0x4A0,4u);Put(model+0xC,lookup);Put(model+4,rest);Put(model+0x10,parentsAt);
    const std::uint8_t parents[]={0,0,1,2};std::memcpy(reinterpret_cast<void*>(parentsAt),parents,sizeof(parents));
    Put(lookup,std::uint8_t(1));Put(lookup+6,std::uint8_t(2));Put(lookup+64,std::uint8_t(3));
    NativeSkinMath::BindLocal local[4];local[1].position={0,90,0};local[2].position={0,65,0};local[3].position={0,4,-8};
    std::memcpy(reinterpret_cast<void*>(rest),local,sizeof(local));const std::uint32_t parentIds[]={0,0,1,2};CameraPose bind[4];
    Check(NativeSkinMath::BindPose(local,parentIds,4,bind),"native bind tree generated");
    for(unsigned i=0;i<4;++i){float rows[12];Check(NativeSkinMath::Palette(bind[i],{bind[i].basis,bind[i].position},rows),"complete native head palette generated");
        std::memcpy(reinterpret_cast<void*>(cache+0x90+i*sizeof(rows)),rows,sizeof(rows));}
    std::array<unsigned char,4*48> original{};std::memcpy(original.data(),reinterpret_cast<void*>(cache+0x90),original.size());
    CameraPose root;root.position={300,1200,-900};Put(entry+0x10,root);Put(entry+0x40,root);Put(base+kGpuPoseIndex,0u);
    CameraFrame camera;camera.valid=camera.firstPerson=camera.nativeBodyAction=true;camera.units=100;camera.tick=Clock();camera.pairId=1;
    camera.generation=frameGeneration.fetch_add(1)+1;camera.centre=root;camera.centre.position=Add(root.position,{0,159,0});camera.rawHeadOrientation[3]=1;
    camera.hands.present=camera.hands.mounted=1;camera.hands.tick=Clock();camera.hands.head.flags=HandsIPC::kOrientation|HandsIPC::kPosition;camera.hands.head.orientation[3]=1;
    Check(CaptureRigMetadata(model,player,camera.rig),"owned local rig metadata captured");publishedFrame=camera;frameThread=GetCurrentThreadId();
    action.valid=true;action.player=player;action.movementKind=NativeActionKL::Movement::Vehicle;
    entryAt=entry;acceptedState=state;originalBind=NativeBind;expectedProvider=provider;expectedPackage=package;
    Put(package,packageVtable);Put(packageVtable+0x20,base+NativeShadowPassKL::DrawRva);
    for(unsigned pass=0;pass<11;++pass){Put(package+0xC+pass*4,renderer);Put(contexts+0x10+pass*4,std::uint32_t(0));}
    Put(renderer,rendererVtable);Put(rendererVtable+0x1C,reinterpret_cast<std::uintptr_t>(&Prepare));Put(rendererVtable+0x20,reinterpret_cast<std::uintptr_t>(&PassEnd));
    Put(item,itemVtable);Put(item+0xC,provider);Put(itemVtable+4,reinterpret_cast<std::uintptr_t>(&Draw));
    Put(provider,providerVtable);Put(providerVtable+0x14,reinterpret_cast<std::uintptr_t>(&PassCount));Put(providerVtable+0x18,reinterpret_cast<std::uintptr_t>(&PassBegin));Put(entries,item);
    constexpr auto native=base+NativeShadowPassKL::DrawRva;Check(executable.Read(0x400000+NativeShadowPassKL::DrawRva,reinterpret_cast<void*>(native),0xC3),"actual native shader dispatch bytes copied without modifying the EXE");
    DWORD protection=0;Check(VirtualProtect(reinterpret_cast<void*>(native),0xC3,PAGE_EXECUTE_READWRITE,&protection)!=FALSE,"private authenticated dispatch executable");
    originalTechniqueDraw=reinterpret_cast<NativeTechniqueDraw>(native);
    const auto draw=[&](std::uint8_t pass){Put(package+0x62,pass);TechniqueDrawHook(reinterpret_cast<void*>(package),nullptr,
        reinterpret_cast<void*>(contexts),reinterpret_cast<void*>(entries),1,reinterpret_cast<void*>(state));};
    draw(0);Check(draws==1&&lastBound==reinterpret_cast<std::uintptr_t>(gpuPalette.data())&&lastHeadScale<.001f,"actual native normal dispatch consumes the hidden first-person head palette");
    std::array<std::array<float,12>,4> visible{};std::memcpy(visible.data(),gpuPalette.data(),sizeof(visible));
    const auto beforeGuards=guards;const auto anchorTick=lastOwnedEntryTick.load();tick+=16;draw(2);
    Check(draws==2&&lastBound==reinterpret_cast<std::uintptr_t>(gpuShadowPalette.data())&&lastHeadScale>.99f&&guards==beforeGuards+1,"actual ShadowMap dispatch restores complete owned head without a GPU colour/depth heuristic");
    Check(!std::memcmp(visible.data(),gpuShadowPalette.data(),2*48)&&
          std::memcmp(visible[2].data(),gpuShadowPalette[2].data(),48)&&std::memcmp(visible[3].data(),gpuShadowPalette[3].data(),48),
          "shadow staging changes only head/child rows while preserving prepared body palette exactly");
    Check(gpuPalette[2][0]<.001f&&lastOwnedEntryTick.load()==anchorTick,"head-visible shadow palette never replaces normal staging or camera anchor authority");
    draw(3);Check(draws==3&&lastBound==reinterpret_cast<std::uintptr_t>(gpuShadowPalette.data())&&lastHeadScale>.99f,"actual ShadowDrop dispatch retains head even with the same perspective/view state");
    Check(!shadowPass.valid&&shadowHeadPalettes==2,"native shadow call-stack authorization is retired immediately after each dispatch");
    acceptedState=0;draw(2);Check(draws==4&&lastBound==cache+0x90&&lastHeadScale>.99f&&shadowHeadPalettes==2,
          "separate shadow camera keeps the existing complete native palette without borrowing a VR body solve");acceptedState=state;
    draw(0);Check(draws==5&&lastBound==reinterpret_cast<std::uintptr_t>(gpuPalette.data())&&lastHeadScale<.001f,"normal draw after shadow restores first-person head invisibility");
    Put(geometry+0x60,player+4);draw(2);
    Check(draws==6&&lastBound==cache+0x90&&lastHeadScale>.99f&&shadowHeadPalettes==2,"foreign actor palette stays native and cannot obtain local shadow ownership");Put(geometry+0x60,player);
    NativeShadowPassKL::Context proof;Put(package+0x62,std::uint8_t(2));Check(NativeShadowPassKL::Snapshot(image,package,state,ReadBytes,proof),"exact current package/state/pass snapshot is positive");
    Check(!NativeShadowPassKL::Matches(image,proof,state+4,ReadBytes),"another renderer state cannot borrow shadow authorization");
    Put(package+0x62,std::uint8_t(0));Check(!NativeShadowPassKL::Matches(image,proof,state,ReadBytes),"normal pass cannot borrow a former shadow snapshot");
    Check(!std::memcmp(original.data(),reinterpret_cast<void*>(cache+0x90),original.size()),"normal/shadow/foreign dispatches keep native source palette byte-identical");
    nestedPackage=package;nestedContexts=contexts;nestedEntries=entries;nestedRequested=true;const auto resumeBefore=paletteLeaseResumed.load();draw(0);
    Check(paletteLeaseResumed==resumeBefore+1&&techniqueDepth==0&&!paletteLease.active,"nested native shadow scope resumes once and final outer exit retires its lease");
    Check(*reinterpret_cast<const std::uint32_t*>(state+0x3BC)==cache+0x90,"shader-package exit restores the original native palette after every actual draw");
    std::uintptr_t retiredProvider=0;unsigned retiredPass=99;
    Check(!ReadCurrentNativeEntry(state,retiredProvider,&retiredPass),"current native provider authority is unavailable after actual48F0A0 returns");
    draw(9);Check(!PostfilterForegroundActive(),"opaque mask authorization retires when actual dispatch returns");
    installed=false;VirtualFree(memory,0,MEM_RELEASE);
    std::printf("KL1 native shadow head dispatch: %u checks, %u failures.\n",checks,failed);return failed?1:0;
}
