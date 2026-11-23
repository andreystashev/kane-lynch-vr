// Real shared BF600 dispatcher and PIP install/lifecycle on private native
// layouts. Reads the pinned Steam PE as data; starts no game, window or GPU.
// The reused fixture aborts if any MinHook API is called: BF600 has ONE owner.
#define main UnusedReflectionProductionMain
#include "native_reflection_camera_production_test.cpp"
#undef main
#include "native_pip_render_kl.cpp"
#include "native_hud_test_fixture.hpp"
namespace PipProducerFixture {
namespace C=NativeCameraKL;namespace P=NativePipRenderKL;
std::uintptr_t base=0,q=0,start=0,owner=0;
unsigned nativeCalls=0,tick=0;
std::uintptr_t expectedSelf=0,expectedArgs[6]{};
template<class T>void Set(std::uintptr_t at,const T& value){std::memcpy(reinterpret_cast<void*>(at),&value,sizeof(value));}
void __thiscall NativeProducer(void* self,void* a,void* b,void* c,void* d,void* e,void* f){
    ++nativeCalls;const std::uintptr_t args[]={reinterpret_cast<std::uintptr_t>(a),reinterpret_cast<std::uintptr_t>(b),
        reinterpret_cast<std::uintptr_t>(c),reinterpret_cast<std::uintptr_t>(d),reinterpret_cast<std::uintptr_t>(e),reinterpret_cast<std::uintptr_t>(f)};
    Check(reinterpret_cast<std::uintptr_t>(self)==expectedSelf&&!std::memcmp(args,expectedArgs,sizeof(args)),
          "shared detour preserves original thiscall self and all six arguments exactly");
    const auto cursor=*reinterpret_cast<std::uint32_t*>(q+0xC);Set(cursor,std::uint32_t(0xDEADBEEF));Set(q+0xC,cursor+4);
}
bool ForeignFilter(void*,std::uintptr_t,std::uintptr_t,std::uintptr_t,std::uintptr_t,std::uintptr_t,std::uintptr_t){return false;}
void Branch(std::uintptr_t target){
    const auto at=base+P::ProducerRva;Set(at,std::uint8_t(0xE9));Set(at+1,std::uint32_t(target-at-5));
}
void Invoke(std::uintptr_t entry,std::uintptr_t parent=0,std::uintptr_t view=0,bool first=false){
    expectedSelf=entry;expectedArgs[0]=q;expectedArgs[1]=parent;expectedArgs[2]=0x3333;
    expectedArgs[3]=view?view:owner;expectedArgs[4]=0x5555;expectedArgs[5]=first?1:0;
    C::MainRenderHook(reinterpret_cast<void*>(entry),nullptr,reinterpret_cast<void*>(q),reinterpret_cast<void*>(parent),
        reinterpret_cast<void*>(0x3333),reinterpret_cast<void*>(expectedArgs[3]),reinterpret_cast<void*>(0x5555),reinterpret_cast<void*>(expectedArgs[5]));
}
void Advance(std::uintptr_t system){Set(system+0x44,++tick);Set(q+0xC,std::uint32_t(start));}
}
int main(int argc,char** argv){
    using namespace PipProducerFixture;
    static_assert(sizeof(void*)==4,"native producer ABI fixture is x86");
    if (argc < 2) {
        std::puts("Pass a supported game executable for the native fixture.");
        return 2;
    }
    const char* path = argv[1];
    std::wstring wide(std::strlen(path)+1,L'\0');MultiByteToWideChar(CP_UTF8,0,path,-1,wide.data(),static_cast<int>(wide.size()));
    char digest[65]{};const bool exact=HashFile(wide.c_str(),digest)&&IsSupportedExecutableHash(digest);
    Check(exact,"read-only source PE has an exact supported executable SHA256");if(!exact)return 1;
    NativeHudFixture::Image source;Check(source.Load(path),"original Steam PE loaded as nonexecutable data");if(source.mapped.empty())return 1;
    Check(P::VerifyImage(NativeHudFixture::Base,source.Reader()),"all pristine original producer/PIP/overlay signatures match pinned PE");
    auto* privateImage=VirtualAlloc(reinterpret_cast<void*>(0x25000000),source.mapped.size(),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    auto* arena=VirtualAlloc(reinterpret_cast<void*>(0x30000000),0x40000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    Check(privateImage&&arena,"private nonexecutable image and native layouts allocated");if(!privateImage||!arena)return 1;
    base=reinterpret_cast<std::uintptr_t>(privateImage);const auto mem=reinterpret_cast<std::uintptr_t>(arena);
    std::memcpy(privateImage,source.mapped.data(),source.mapped.size());
    // This original PE has no relocation table. Relocate only typed operands
    // consumed by the existing proof; assert their original values first.
    const std::uint32_t pointers[][2]={{0x23641C,0x563CA4},{0x563CA4,0x1DAAF0},{0x563D84,0x236D70},
        {0xBF603,0x4F103B},{0x540F50,0xBF600},{0x56B124,0xF6220},{0x56B12C,0xF5770}};
    for(const auto& p:pointers){Check(*reinterpret_cast<std::uint32_t*>(base+p[0])==NativeHudFixture::Base+p[1],
        "private relocation names the exact original typed pointer");Set(base+p[0],std::uint32_t(base+p[1]));}
    Check(P::VerifyImage(base,P::Read),"relocated private pristine image retains complete producer proof");
    Check(!P::Install(true,nullptr),"PIP cannot install without pristine proof");
    Set(base+P::ProducerRva,std::uint8_t(0x90));
    Check(!P::InitializeVerifiedImage(base,nullptr),"unknown producer code cannot pass preflight");
    Set(base+P::ProducerRva,std::uint8_t(0x6A));
    Check(P::InitializeVerifiedImage(base,nullptr),"pristine proof is retained before camera installation");
    C::image=base;C::originalMainRender=NativeProducer;
    Check(!C::RegisterCameraProducerFilter(base,P::SuppressProducer),"camera owner must be installed before registration");
    C::installed=true;
    Check(!P::InitializeVerifiedImage(base,nullptr),"preflight cannot be forged after camera installation");
    Check(!C::RegisterCameraProducerFilter(base,P::SuppressProducer),"native unhooked bytes do not authorize a shared callback");
    Branch(reinterpret_cast<std::uintptr_t>(&ForeignFilter));
    Check(!C::RegisterCameraProducerFilter(base,P::SuppressProducer),"foreign detour cannot be chained as camera owner");
    Branch(reinterpret_cast<std::uintptr_t>(&C::MainRenderHook));
    Check(!P::VerifyImage(base,P::Read),"old post-camera original-signature verification reproduces the reported failure");
    Check(!C::RegisterCameraProducerFilter(base+4,P::SuppressProducer),"proof cannot cross executable image identity");
    Check(!C::RegisterCameraProducerFilter(base,nullptr),"null callback cannot register");
    C::originalMainRender=nullptr;
    Check(!C::RegisterCameraProducerFilter(base,P::SuppressProducer),"missing native trampoline rejects registration");C::originalMainRender=NativeProducer;
    unsigned char owned[5];std::memcpy(owned,reinterpret_cast<void*>(base+P::ProducerRva),5);
    Check(P::Install(true,nullptr),"PIP installs through the existing verified camera producer with no second hook");
    Check(P::Install(true,nullptr)&&!std::memcmp(owned,reinterpret_cast<void*>(base+P::ProducerRva),5),
          "repeated install preserves exactly the existing owned detour bytes");
    Check(!C::RegisterCameraProducerFilter(base,ForeignFilter),"different callback cannot replace a registered owner");
    Check(P::Snapshot().installed&&P::Snapshot().verified&&P::Snapshot().enabled,"shared installation publishes full retained proof status");

    const auto system=mem,data=mem+0x2000,manager=mem+0x4000,records=mem+0x5000,frontend=mem+0x6000;
    const auto controller=mem+0x8000,cam=mem+0xA000,geo=mem+0xB000,player=mem+0xC000;
    const auto mainCam=mem+0xE000,mainGeo=mem+0xF000,pipEntry=mem+0x10000,mainEntry=mem+0x11000;
    const auto overlayCam=mem+0x12000,overlayGeo=mem+0x13000,overlayEntry=mem+0x14000;
    q=mem+0x18000;start=mem+0x20000;owner=mem+0x16000;
    Set(base+0x6875F0,std::uint32_t(system));Set(system+0x96C,std::uint32_t(manager));Set(manager+0x300,std::uint32_t(0));
    Set(base+0x88E8B0,std::uint32_t(0));Set(base+0x8883A4,std::uint8_t(0));
    Set(base+0x68760C,std::uint32_t(data));Set(data+0x154C,std::uint32_t(records));Set(records,std::uint32_t(frontend));
    Set(frontend,std::uint32_t(base+NativePipKL::FrontendVtableRva));Set(frontend+0x914,std::uint32_t(controller));
    Set(controller,std::uint32_t(base+NativePipKL::ControllerVtableRva));Set(controller+0x544,std::uint8_t(1));
    Set(controller+0x4B8,std::uint32_t(6));Set(controller+0x24,std::uint32_t(cam));
    const std::int32_t rect[]={48,34,752,546};std::memcpy(reinterpret_cast<void*>(controller+0x4A8),rect,sizeof(rect));
    Set(controller+0x51C,std::uint32_t(owner));Set(owner,std::uint32_t(base+0x53DAC4));
    Set(data+0xA2C,std::uint32_t(player));Set(player+0x9A0,std::uint32_t(mainCam));
    const auto link=[&](std::uintptr_t entry,std::uintptr_t camera,std::uintptr_t geometry,std::uint32_t vt){
        Set(entry,std::uint32_t(base+P::CameraEntryVtableRva));Set(entry+0xF4,std::uint32_t(geometry));
        Set(camera,std::uint32_t(base+vt));Set(camera+4,std::uint32_t(geometry));Set(geometry+0x60,std::uint32_t(camera));};
    link(pipEntry,cam,geo,NativePipKL::CameraVtableRva);link(mainEntry,mainCam,mainGeo,NativePipKL::CameraVtableRva);
    link(overlayEntry,overlayCam,overlayGeo,P::OverlayCameraVtableRva);
    Set(q+0x14,std::uint32_t(start));Set(q+0x10,std::uint32_t(start+0x10000));Advance(system);
    Check(NativePipKL::Snapshot(base,P::Read).active,"private registered native inset resolves through real ownership walk");
    // A skip still retires producer receipts from a rewound/reused native queue.
    C::secondaryPackets[0].queue=q;C::secondaryPackets[0].queueBegin=start;C::secondaryPackets[0].end=start+16;
    C::reflectionPackets[0].certificate.queue=q;C::reflectionPackets[0].queueBegin=start;C::reflectionPackets[0].certificate.end=start+16;
    Invoke(pipEntry);
    Check(nativeCalls==0&&*reinterpret_cast<std::uint32_t*>(q+0xC)==start&&P::Snapshot().suppressed==1,
          "actual shared dispatcher suppresses only auxiliary producer without reserving any native queue bytes");
    Check(!C::secondaryPackets[0].queue&&!C::reflectionPackets[0].certificate.queue,"suppressed producer retires stale secondary and reflection queue receipts");
    Invoke(pipEntry,0,0,true);Check(nativeCalls==0&&P::Snapshot().suppressed==2,"first-camera ordering cannot turn an auxiliary inset into main view");
    const auto passes=[&](const char* reason,std::uintptr_t entry,std::uintptr_t parent=0,std::uintptr_t view=0){
        const auto before=nativeCalls;Advance(system);Invoke(entry,parent,view);Check(nativeCalls==before+1&&
            *reinterpret_cast<std::uint32_t*>(q+0xC)==start+4,reason);};
    passes("main local camera preserves original producer",mainEntry);
    passes("recursive parent-geometry producer remains native",pipEntry,0x2222);
    passes("foreign native view cannot borrow inset ownership",pipEntry,0,owner+4);
    Set(controller+0x4B8,std::uint32_t(0));passes("inactive controller remains native",pipEntry);Set(controller+0x4B8,std::uint32_t(6));
    Set(controller+0x4A8,std::int32_t(0));passes("fullscreen composition remains native",pipEntry);Set(controller+0x4A8,rect[0]);
    Set(manager+0x300,std::uint32_t(1));passes("full authored cutscene remains native",pipEntry);Set(manager+0x300,std::uint32_t(0));
    Set(base+0x8883A4,std::uint8_t(3));passes("native inventory mode remains native",pipEntry);Set(base+0x8883A4,std::uint8_t(0));
    Set(player+0x9A0,std::uint32_t(cam));passes("inset identity reused as local camera is never suppressed",pipEntry);Set(player+0x9A0,std::uint32_t(mainCam));
    Set(cam+4,std::uint32_t(mainGeo));passes("changed camera geometry backlink cannot borrow retained proof",pipEntry);Set(cam+4,std::uint32_t(geo));
    Set(pipEntry,std::uint32_t(0));passes("unknown render entry retains native producer",pipEntry);Set(pipEntry,std::uint32_t(base+P::CameraEntryVtableRva));
    Set(controller+0x538,std::uint8_t(1));Set(controller+0x628,std::uint32_t(overlayCam));Advance(system);
    const auto beforeOverlay=nativeCalls;Invoke(overlayEntry);
    Check(nativeCalls==beforeOverlay&&P::Snapshot().compositeSuppressed==1,"owned derived circular overlay camera shares the same suppression dispatcher");
    P::Shutdown();passes("shutdown retains native rendering without removing the shared camera hook",pipEntry);
    Check(!handFrames&&!bodyFrames&&!hostApplied&&!providerCalls,"producer-only path never publishes tracking, body, hands or host eyes");
    C::installed=false;C::cameraProducerFilter=nullptr;C::originalMainRender=nullptr;C::image=0;
    VirtualFree(arena,0,MEM_RELEASE);VirtualFree(privateImage,0,MEM_RELEASE);
    std::printf("Shared native PIP/camera producer: %u checks, %u failures; private CPU only.\n",checks,failed);return failed?1:0;
}
