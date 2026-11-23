#include "native_effects_kl.cpp"
#include "native_reflection_camera_math.hpp"
#include <fstream>
#include <iterator>
#include <cstdio>
#include <vector>
#include <cstdlib>
namespace {
using namespace NativeCameraMath;
unsigned checks=0;
NativeCameraKL::EffectsView fixtureView;
bool viewValid=true,unexpectedCaller=false;
std::uintptr_t reflectedState=0;
CameraPose reflectedReference;
bool diagnosticActive=false;
bool DiagnosticActive(){return diagnosticActive;}
CameraPose capturedRoot;
Basis viewAtNativeBind;
using NativeWorld=void(__thiscall*)(void*,const CameraPose*);
NativeWorld nativeWorld=nullptr;
void Check(bool value,const char* message){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
bool ReadView(NativeCameraKL::EffectsView& output){output=fixtureView;return viewValid;}
void __fastcall WorldBridge(void* state,void*,const CameraPose* root){
    const auto caller=std::uint32_t(reinterpret_cast<std::uintptr_t>(__builtin_return_address(0))-NativeEffectsKL::nativeImage);
    NativeEffectsKL::ObserveWorldBind(state,root,unexpectedCaller?0:caller);
    capturedRoot=*root;std::memcpy(&viewAtNativeBind,static_cast<unsigned char*>(state)+0xBC,sizeof(viewAtNativeBind));
    nativeWorld(state,root);
}
CameraPose Eye(float yaw,float pitch,float roll){
    const Vec3 f{std::sin(yaw)*std::cos(pitch),std::sin(pitch),std::cos(yaw)*std::cos(pitch)};
    const Vec3 right{std::cos(yaw),0,-std::sin(yaw)},up=Cross(f,right);
    return {{f,Subtract(Scale(up,std::cos(roll)),Scale(right,std::sin(roll))),Add(Scale(right,std::cos(roll)),Scale(up,std::sin(roll)))},{80,140,-30}};
}
bool Near(const Basis& a,const Basis& b){const float* x=reinterpret_cast<const float*>(&a),*y=reinterpret_cast<const float*>(&b);
    for(unsigned i=0;i<9;++i)if(!std::isfinite(x[i])||!std::isfinite(y[i])||std::fabs(x[i]-y[i])>.0005f)return false;return true;}
}
namespace NativeCameraKL {
bool ReadEffectsView(EffectsView& output){return ::ReadView(output);}
bool ReadReflectionBillboardReference(std::uintptr_t state,CameraPose& output){
    if(!state||state!=reflectedState)return false;
    output=reflectedReference;return true;
}
bool ReadReflectionScatter(ReflectionTargetView&,float (&)[8]){return false;}
}
namespace NativeHudKL {
void SetMappedVertexFilter(MappedVertexFilter){}
bool ReadMappedVertexContext(MappedVertexContext&){return false;}
bool ReadParticleDrawContext(MappedVertexContext&){return false;}
void SetVisualSourceTrace(NativeEffectsKL::VisualTraceActive){}
bool ReadSpriteVisualSource(std::uintptr_t,std::uintptr_t,NativeEffectsKL::VisualSourceDiagnostic&){return false;}
}
int main(int argc,char** argv){
    if (argc < 2) {
        std::puts("Pass a supported game executable for the native fixture.");
        return 2;
    }
    const char* path = argv[1];
    wchar_t widePath[32768]{};
    Check(MultiByteToWideChar(CP_UTF8,0,path,-1,widePath,32768)>0,"native EXE path decodes as UTF-8");
    char digest[65]{};if(!HashFile(widePath,digest)){std::puts("SKIP: pinned EXE unavailable for native CPU billboard fixture");return 0;}
    Check(IsSupportedExecutableHash(digest),"native fixture uses exact supported EXE hash");
    std::ifstream file(path,std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)),{});
    auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(bytes.data()+dos->e_lfanew);
    Check(nt->Signature==IMAGE_NT_SIGNATURE&&nt->OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR32_MAGIC,"native PE32 image");
    const auto size=nt->OptionalHeader.SizeOfImage;
    auto* mapped=static_cast<unsigned char*>(VirtualAlloc(nullptr,size+4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    Check(mapped&&reinterpret_cast<std::uintptr_t>(mapped)<0x80000000u,"private low-address CPU image, not a launched game");
    std::memcpy(mapped,bytes.data(),nt->OptionalHeader.SizeOfHeaders);
    auto* sections=IMAGE_FIRST_SECTION(nt);for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i){const auto& s=sections[i];
        Check(s.VirtualAddress+s.SizeOfRawData<=size&&s.PointerToRawData+s.SizeOfRawData<=bytes.size(),"native section bounds");
        std::memcpy(mapped+s.VirtualAddress,bytes.data()+s.PointerToRawData,s.SizeOfRawData);}
    const auto readOriginal=[&](std::uintptr_t at,void* out,std::size_t count){if(at<0x400000u||at-0x400000u+count>size)return false;
        std::memcpy(out,mapped+at-0x400000u,count);return true;};
    Check(NativeEffectsKLProducer::VerifyBillboard(0x400000u,readOriginal),"actual pinned geometry mode/view/centre/WorldBind/ret8 signatures");
    mapped[0x72E42]^=1;Check(!NativeEffectsKLProducer::VerifyBillboard(0x400000u,readOriginal),"changed native mode-selector instruction rejects correction");mapped[0x72E42]^=1;
    const auto delta=std::uint32_t(reinterpret_cast<std::uintptr_t>(mapped)-nt->OptionalHeader.ImageBase);
    const auto reloc=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    for(unsigned at=reloc.VirtualAddress;at<reloc.VirtualAddress+reloc.Size;){auto* block=reinterpret_cast<IMAGE_BASE_RELOCATION*>(mapped+at);
        Check(block->SizeOfBlock>=8&&at+block->SizeOfBlock<=reloc.VirtualAddress+reloc.Size,"private relocation bounds");
        auto* entries=reinterpret_cast<WORD*>(block+1);for(unsigned i=0;i<(block->SizeOfBlock-8)/2;++i)if((entries[i]>>12)==IMAGE_REL_BASED_HIGHLOW)
            *reinterpret_cast<DWORD*>(mapped+block->VirtualAddress+(entries[i]&0xFFF))+=delta;
        at+=block->SizeOfBlock;
    }
    // The supported fixed-base EXE has no relocation directory. These are all
    // absolute data operands executed by this one leaf routine; native CALLs
    // stay image-relative. Relocate only this explicit CPU fixture's copies.
    if(!reloc.Size)for(unsigned operand:{0x72E01u,0x72E2Fu,0x72F82u,0x72F95u,0x72FA3u,0x72FA9u}){
        auto* address=reinterpret_cast<DWORD*>(mapped+operand);
        Check(*address>=0x400000u&&*address<0x400000u+size,"pinned billboard absolute data operand");*address+=delta;
    }
    // Pure original 71770 copied with only its three relative math CALLs
    // relocated. No game entry point/import, D3D device, window or GPU runs.
    auto* clone=mapped+size;std::memcpy(clone,mapped+0x71770,0x51);
    for(unsigned off:{0x25u,0x39u,0x46u}){Check(clone[off]==0xE8,"original native WV math CALL");
        const auto target=std::uintptr_t(mapped)+0x71770+off+5+*reinterpret_cast<std::int32_t*>(clone+off+1);
        *reinterpret_cast<std::int32_t*>(clone+off+1)=std::int32_t(target-(std::uintptr_t(clone)+off+5));}
    nativeWorld=reinterpret_cast<NativeWorld>(clone);
    mapped[0x71770]=0xE9;*reinterpret_cast<std::int32_t*>(mapped+0x71771)=std::int32_t(reinterpret_cast<std::uintptr_t>(&WorldBridge)-(reinterpret_cast<std::uintptr_t>(mapped)+0x71775));
    DWORD previous=0;Check(VirtualProtect(mapped,size+4096,PAGE_EXECUTE_READWRITE,&previous)!=FALSE,"execute only explicit private native math path");
    FlushInstructionCache(GetCurrentProcess(),mapped,size+4096);
    NativeEffectsKL::nativeImage=reinterpret_cast<std::uintptr_t>(mapped);
    NativeEffectsKL::originalBillboardBind=reinterpret_cast<NativeEffectsKL::BillboardBind>(mapped+0x72E00);
    NativeEffectsKL::Configure(ReadView);
    alignas(16) unsigned char state[0x500]{},entry[0x150]{},instance[16]{},object[32]{},descriptor[32]{};
    const auto ptr=[](const void* p){return std::uint32_t(reinterpret_cast<std::uintptr_t>(p));};
    *reinterpret_cast<DWORD*>(instance+4)=ptr(object);*reinterpret_cast<DWORD*>(object+0x14)=ptr(descriptor)|0x80000000u;
    *reinterpret_cast<WORD*>(descriptor+2)=8;*reinterpret_cast<DWORD*>(entry)=ptr(mapped+0x540EE0);
    CameraPose source;source.position={300,210,900};std::memcpy(entry+0x10,&source,sizeof(source));std::memcpy(entry+0x40,&source,sizeof(source));
    const auto reference=Eye(.42f,.13f,-.17f);fixtureView.reference=reference;fixtureView.state=reinterpret_cast<std::uintptr_t>(state);
    const auto run=[&](const CameraPose& eye,unsigned mode,bool active=true,bool badCaller=false,bool expectScope=true){
        descriptor[0xD]=static_cast<unsigned char>(mode);viewValid=active;unexpectedCaller=badCaller;fixtureView.eye=eye;
        CameraPose view;Check(NativeCameraKLMath::ViewFromWorld(eye,view),"native actual view");std::memcpy(state+0xBC,&view,sizeof(view));
        const auto before=NativeEffectsKL::Snapshot();NativeEffectsKL::BillboardBindHook(entry,nullptr,state,instance);
        const auto after=NativeEffectsKL::Snapshot();
        Check(Near(viewAtNativeBind,view.basis),"native 71770 receives actual tracked VIEW, never temporary reference");
        Check(!std::memcmp(state+0xBC,&view,sizeof(view)),"actual full VIEW restored before native binder and after scope exit");
        Check(!std::memcmp(&capturedRoot.position,&source.position,sizeof(source.position)),"original authored billboard centre is bit-identical");
        if(active&&mode&&expectScope)Check(after.billboardEntered==before.billboardEntered+1,"exact geometry descriptor entered reference basis scope");
        else Check(after.billboardEntered==before.billboardEntered,"mode0 and inactive/GUI views preserve native path");
        return capturedRoot;
    };
    for(unsigned slot:{0u,1u}){
    *reinterpret_cast<DWORD*>(mapped+0x7678A4)=slot;source.position={300.f+slot*70,210.f+slot*9,900.f-slot*40};
    std::memcpy(entry+0x10+slot*0x30,&source,sizeof(source));
    for(unsigned mode:{1u,2u,3u,4u,5u,6u,7u}){
        const auto baseline=run(reference,mode);
        for(float yaw:{-.8f,.2f,.9f})for(float pitch:{-.4f,.5f})for(float roll:{-.3f,.6f}){
            const auto eye=Eye(yaw,pitch,roll);const auto root=run(eye,mode);
            Check(Near(root.basis,baseline.basis),"actual native mesh billboard WORLD basis remains fixed under HMD yaw pitch roll");
            CameraPose wv;std::memcpy(&wv,state+0xEC,sizeof(wv));
            using NativePoint=void(__fastcall*)(Vec3*,const Vec3*,const Basis*);
            const auto point=reinterpret_cast<NativePoint>(mapped+0x2E780);
            for(Vec3 local: {Vec3{-.7f,-1.3f,.2f},Vec3{.7f,1.3f,-.2f}}){
                const auto fixedWorld=Add(baseline.position,Add(Add(Scale(baseline.basis.right,local.x),Scale(baseline.basis.up,local.y)),Scale(baseline.basis.forward,local.z)));
                const Vec3 expected{Dot(eye.basis.right,Subtract(fixedWorld,eye.position)),Dot(eye.basis.up,Subtract(fixedWorld,eye.position)),Dot(eye.basis.forward,Subtract(fixedWorld,eye.position))};
                Vec3 actual;point(&actual,&local,&wv.basis);actual=Add(actual,wv.position);
                Check(Dot(Subtract(actual,expected),Subtract(actual,expected))<.0001f,"original native cached WV transforms fixed authored corners into actual tracked view, independently of HMD orientation");
            }
        }
    }
    }
    const auto ordinary=run(Eye(.2f,.3f,.4f),0);Check(!std::memcmp(&ordinary,&source,sizeof(source)),"mode0 retains complete original root and scale");
    const auto inactive=run(Eye(.2f,.3f,.4f),7,false);
    Check(!Near(inactive.basis,run(Eye(-.4f,-.1f,-.3f),7,false).basis),"inactive and external views retain native camera-facing behavior");
    fixtureView.state=reinterpret_cast<std::uintptr_t>(state)+4;run(Eye(.2f,.3f,.4f),7,true,false,false);fixtureView.state=reinterpret_cast<std::uintptr_t>(state);
    *reinterpret_cast<WORD*>(descriptor+2)=7;run(Eye(.2f,.3f,.4f),7,true,false,false);*reinterpret_cast<WORD*>(descriptor+2)=8;
    const auto beforeUnexpected=NativeEffectsKL::Snapshot();run(Eye(.2f,.3f,.4f),7,true,true);
    Check(NativeEffectsKL::Snapshot().billboardUnexpected==beforeUnexpected.billboardUnexpected+1,"unexpected WorldBind caller restores tracked view before any native math");
    using namespace NativeReflectionCameraMath::Detail;
    auto mirror=Identity();mirror.a[5]=-1;
    const auto reflectedView=[&](const CameraPose& eye){
        CameraPose mainView;Check(NativeCameraKLMath::ViewFromWorld(eye,mainView),"fixture source view is rigid");
        return Packed(Multiply(mirror,View(mainView)));
    };
    reflectedReference=reflectedView(reference);
    reflectedState=reinterpret_cast<std::uintptr_t>(state);
    unexpectedCaller=false;viewValid=false;descriptor[0xD]=3;
    const auto runReflected=[&](const CameraPose& view){
        std::memcpy(state+0xBC,&view,sizeof(view));
        NativeEffectsKL::BillboardBindHook(entry,nullptr,state,instance);
        Check(Near(viewAtNativeBind,view.basis)&&!std::memcmp(state+0xBC,&view,sizeof(view)),
            "reflected geometry scope restores exact reflected VIEW before native cached WV");
        Check(!std::memcmp(&capturedRoot.position,&source.position,sizeof(source.position)),
            "reflected geometry scope preserves the original authored centre");
        return capturedRoot;
    };
    const auto reflectedCount=NativeEffectsKL::Snapshot().reflectedBillboardBound;
    const auto reflectedA=reflectedView(Eye(.2f,.3f,.4f));
    const auto reflectedB=reflectedView(Eye(-.4f,-.1f,-.3f));
    const auto firstReflectedRoot=runReflected(reflectedA);
    Check(Near(firstReflectedRoot.basis,runReflected(reflectedB).basis)&&
        NativeEffectsKL::Snapshot().reflectedBillboardBound==reflectedCount+2,
        "actual native geometry shares a stable reflected world basis across different mirrored eye poses");
    reflectedState+=4;
    const auto unqualifiedRoot=runReflected(reflectedA);
    Check(!Near(unqualifiedRoot.basis,runReflected(reflectedB).basis)&&
        NativeEffectsKL::Snapshot().reflectedBillboardBound==reflectedCount+2,
        "foreign reflected state leaves the native camera-facing geometry untouched");
    reflectedState=0;
    NativeEffectsKL::ConfigureVisualSourceDiagnostics(DiagnosticActive);diagnosticActive=true;fixtureView.frame=77;
    run(Eye(.6f,-.2f,.3f),3);
    NativeEffectsKL::VisualSourceDiagnostic diagnostic;
    Check(NativeEffectsKL::SnapshotVisualSource(reinterpret_cast<std::uintptr_t>(entry),reinterpret_cast<std::uintptr_t>(state),diagnostic)&&
        diagnostic.kind==2&&diagnostic.mode==3&&diagnostic.frame==77&&diagnostic.rootKnown&&diagnostic.descriptor==reinterpret_cast<std::uintptr_t>(descriptor)&&
        diagnostic.instance==reinterpret_cast<std::uintptr_t>(instance)&&diagnostic.object==reinterpret_cast<std::uintptr_t>(object),
        "actual native geometry bind supplies descriptor instance source root bound root and exact camera frame");
    Check(!std::memcmp(diagnostic.rootOriginal+9,&source.position,sizeof(source.position))&&
        !std::memcmp(diagnostic.rootBound,state+0x8C,sizeof(diagnostic.rootBound)),"geometry diagnostics retain native centre and current bound WORLD rather than previous object WORLD");
    ++fixtureView.frame;Check(!NativeEffectsKL::SnapshotVisualSource(reinterpret_cast<std::uintptr_t>(entry),reinterpret_cast<std::uintptr_t>(state),diagnostic),
        "a later frame cannot borrow the old geometry bind diagnostic");--fixtureView.frame;
    state[0x8C]^=1;Check(!NativeEffectsKL::SnapshotVisualSource(reinterpret_cast<std::uintptr_t>(entry),reinterpret_cast<std::uintptr_t>(state),diagnostic),
        "changed actual WORLD cannot borrow the previous matching entry's descriptor");state[0x8C]^=1;
    diagnosticActive=false;NativeEffectsKL::ConfigureVisualSourceDiagnostics(nullptr);
    NativeEffectsKL::Configure(nullptr);NativeEffectsKL::originalBillboardBind=nullptr;NativeEffectsKL::nativeImage=0;
    VirtualFree(mapped,0,MEM_RELEASE);
    std::printf("Native geometry billboard actual CPU: %u checks PASS; original native modes 1..7, tracked WV and authored centres.\n",checks);
}
