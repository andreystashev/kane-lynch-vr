// Real producer hooks, retention lock and consumer certificate matcher on
// private process memory. No game, tracking runtime, window or GPU is opened.
#include "native_camera_kl.cpp"
#include <vector>
#include <cstdlib>
#include <fstream>
[[gnu::used]] static void** keepProjectionTailSlot=&KL1ProjectionTailOriginal;
namespace {[[noreturn]]void Unlisted(){std::puts("FAIL unlisted camera subsystem call");std::abort();}
unsigned handFrames=0,bodyFrames=0,handClears=0,bodyClears=0,hostApplied=0,providerCalls=0;
}
namespace NativeActionKL {bool InitializeVerifiedImage(std::uintptr_t){Unlisted();} State Current(){return {};}}
namespace NativeCoverKL {bool InitializeVerifiedImage(std::uintptr_t){Unlisted();} bool InputBasis(std::uintptr_t,Basis&){Unlisted();}}
namespace D3D9Capture {void ObserveNativeCamera(){}}
namespace NativeHandsKL {
bool RenderPairCaptureReady(){return true;}
void RetireRenderPairImage(){}
void ClearFrame(){++handClears;}
void SetFrame(const NativeCameraMath::CameraPose&,const float*,const float*,const float*,float,std::uint64_t,bool,std::uint32_t,bool){++handFrames;}
}
namespace NativeBodyKL {
void ClearFrame(){++bodyClears;}
void SetFrame(const NativeCameraMath::CameraPose&,std::uint64_t,float,bool,std::uint32_t){++bodyFrames;}
}
namespace VRBridge {Tracking HeadPose(){Unlisted();}bool Ready(){Unlisted();}}
extern "C" {
std::uintptr_t NativeReflectionTestSavedStack=0;
__attribute__((naked,noinline)) void __cdecl NativeReflectionTestWriter(void*,void*,void*,void*,void*,unsigned){
    __asm__ __volatile__(
        "pushal\n\tmovl %esp,%ebp\n\tmovl %esp,_NativeReflectionTestSavedStack\n\tsubl $256,%esp\n\t"
        "movl 48(%ebp),%eax\n\tmovl %eax,44(%esp)\n\t"
        "movl 44(%ebp),%eax\n\tmovl %eax,40(%esp)\n\t"
        "movl 40(%ebp),%esi\n\tmovl 52(%ebp),%edi\n\tmovl 56(%ebp),%ecx\n\t"
        "movl 36(%ebp),%eax\n\txorl %ebx,%ebx\n\tsubl %ecx,%esp\n\tjmp *%eax\n\t");
}
MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*){Unlisted();}
MH_STATUS WINAPI MH_QueueEnableHook(LPVOID){Unlisted();}
MH_STATUS WINAPI MH_ApplyQueued(){Unlisted();}
MH_STATUS WINAPI MH_DisableHook(LPVOID){Unlisted();}
MH_STATUS WINAPI MH_RemoveHook(LPVOID){Unlisted();}
}
namespace {
using namespace NativeCameraKL;
unsigned checks=0,failed=0;
void Check(bool ok,const char* why){++checks;if(!ok){++failed;std::printf("FAIL %s\n",why);}}
template<class T>void Put(std::uintptr_t at,T value){std::memcpy(reinterpret_cast<void*>(at),&value,sizeof(value));}
std::uintptr_t queueAddress=0,buffer=0,sourceEntry=0,viewAddress=0,nestedSelf=0;
bool emitNested=false;
std::vector<std::uintptr_t> emitted;
void Emit(std::uintptr_t self){
    auto at=static_cast<std::uintptr_t>(*reinterpret_cast<std::uint32_t*>(queueAddress+0xC));
    for(unsigned n=0;n<4;++n){
        const std::uint32_t h[6]={1,static_cast<std::uint32_t>(self),
            static_cast<std::uint32_t>(queueAddress),static_cast<std::uint32_t>(viewAddress),0,0x26};
        std::memcpy(reinterpret_cast<void*>(at),h,sizeof(h));
        std::memset(reinterpret_cast<void*>(at+0x18),0,0x98);
        Put(at+0x18,std::uint32_t((n&1)?700:500));
        if(n==3)Put(at+0x48,std::uint32_t(300));
        emitted.push_back(at);at+=0xB0;
    }
    Put(queueAddress+0xC,static_cast<std::uint32_t>(at));
}
void __thiscall Producer(void* self,void* q,void* entry,void* a3,void* view,void* a5,void* a6){
    if(emitNested){emitNested=false;ReflectorHook(reinterpret_cast<void*>(nestedSelf),nullptr,q,entry,a3,view,a5,a6);}
    Emit(reinterpret_cast<std::uintptr_t>(self));
}
void __thiscall NoopProducer(void*,void*,void*,void*,void*,void*,void*){}
void __thiscall NoopCameraWorld(const void*,Basis*,Vec3*){}
void __thiscall Setter(void* state,const float* plane){std::memcpy(static_cast<unsigned char*>(state)+0x3AC,plane,16);}
void Applied(const Frame&,const NativeCameraMath::CameraPose&,const float*,void*){++hostApplied;}
bool Provider(Frame& requested,void*){
    ++providerCalls;requested.valid=true;requested.anchorCamera=requested.sourceCamera;
    requested.nearUnits=5;requested.fov[0]=requested.fov[1]=requested.fov[2]=requested.fov[3]=1.2f;
    for(unsigned i=0;i<4;++i)requested.delta[i*4+i]=1;return true;
}
void ActualNativeWriters(const char* path,std::uintptr_t self){
    std::wstring wide(std::strlen(path)+1,L'\0');MultiByteToWideChar(CP_UTF8,0,path,-1,wide.data(),static_cast<int>(wide.size()));
    char digest[65]{};const bool identity=HashFile(wide.c_str(),digest)&&IsSupportedExecutableHash(digest);
    Check(identity,"actual native producer executable SHA matches supported image");if(!identity)return;
    std::ifstream file(path,std::ios::binary);file.seekg(0,std::ios::end);const auto length=file.tellg();
    Check(length>0&&length<64000000,"actual native producer PE opens read-only");if(length<=0||length>64000000)return;
    std::vector<unsigned char> disk(static_cast<std::size_t>(length));file.seekg(0);file.read(reinterpret_cast<char*>(disk.data()),length);
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(disk.data());
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(disk.data()+dos->e_lfanew);
    const auto* sections=IMAGE_FIRST_SECTION(nt);
    const auto read=[&](std::uint32_t rva,std::size_t bytes){std::vector<unsigned char> out;
        for(unsigned n=0;n<nt->FileHeader.NumberOfSections;++n)if(rva>=sections[n].VirtualAddress&&
            rva-sections[n].VirtualAddress<sections[n].SizeOfRawData){
            const auto offset=sections[n].PointerToRawData+rva-sections[n].VirtualAddress;
            if(offset<=disk.size()&&bytes<=disk.size()-offset)out.assign(disk.begin()+offset,disk.begin()+offset+bytes);
            break;
        }
        return out;
    };
    struct Site {unsigned begin,end,skew,cameraOffset;};
    const Site sites[]={{0xBECC0,0xBECEA,20,0},{0xBEE75,0xBEEBB,52,24},{0xBF027,0xBF051,60,0},{0xBF1EF,0xBF21C,56,0}};
    Put(queueAddress+0xC,static_cast<std::uint32_t>(buffer));emitted.clear();
    MainRenderHook(nullptr,nullptr,reinterpret_cast<void*>(queueAddress),nullptr,nullptr,nullptr,nullptr,nullptr);
    for(const auto& site:sites){
        auto code=read(site.begin,site.end-site.begin);Check(code.size()==site.end-site.begin,"actual native camera writer bytes read from pinned callsite");
        if(code.empty())return;
        // Return from the real writer's variable outstanding cdecl stack.
        code.push_back(0x8B);code.push_back(0x25);
        const auto slot=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&NativeReflectionTestSavedStack));
        const auto* slotBytes=reinterpret_cast<const unsigned char*>(&slot);code.insert(code.end(),slotBytes,slotBytes+4);
        code.push_back(0x61);code.push_back(0xC3);
        void* executable=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);Check(executable!=nullptr,"private native writer clone allocated");if(!executable)return;
        std::memcpy(executable,code.data(),code.size());DWORD old=0;
        Check(VirtualProtect(executable,4096,PAGE_EXECUTE_READ,&old)!=0,"private native writer clone made executable");
        FlushInstructionCache(GetCurrentProcess(),executable,code.size());
        const auto before=*reinterpret_cast<std::uint32_t*>(queueAddress+0xC);
        NativeReflectionTestWriter(executable,reinterpret_cast<void*>(queueAddress),reinterpret_cast<void*>(sourceEntry),reinterpret_cast<void*>(self),reinterpret_cast<void*>(viewAddress),site.skew);
        const auto packet=before+site.cameraOffset;
        Check(*reinterpret_cast<const std::uint32_t*>(packet)==1&&*reinterpret_cast<const std::uint32_t*>(packet+4)==self&&self!=sourceEntry,
            "actual native writer derives queued reflector entry from outstanding cdecl stack, not source-camera argument");
        const auto end=*reinterpret_cast<std::uint32_t*>(queueAddress+0xC);
        std::memset(reinterpret_cast<void*>(end),0,0x98);Put(queueAddress+0xC,end+0x98);Put(packet+0x14,std::uint32_t(0x26));
        emitted.push_back(packet);VirtualFree(executable,0,MEM_RELEASE);
    }
    NativeReflectionPacketsKL::Certificate native;
    Check(NativeReflectionPacketsKL::Capture(queueAddress,buffer,*reinterpret_cast<std::uint32_t*>(queueAddress+0xC),sourceEntry,self,viewAddress,
        [](std::uintptr_t at,void* out,std::size_t bytes){SIZE_T got=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),out,bytes,&got)&&got==bytes;},native),
        "production parser accepts complete sequence emitted by all four real native callsite byte clones");
}
}
int main(int argc,char** argv){
    static_assert(sizeof(void*)==4,"native producer hook ABI is x86");
    auto* memory=VirtualAlloc(nullptr,0x900000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    Check(memory!=nullptr,"private native layout allocated");if(!memory)return 1;
    image=reinterpret_cast<std::uintptr_t>(memory);
    const auto self=image+0x10000;sourceEntry=image+0x20000;queueAddress=image+0x30000;
    buffer=image+0x40000;viewAddress=image+0x50000;nestedSelf=image+0x60000;
    Put(self,static_cast<std::uint32_t>(image+kReflectorVtable));Put(nestedSelf,static_cast<std::uint32_t>(image+kReflectorVtable));
    Put(sourceEntry,static_cast<std::uint32_t>(image+kCameraEntryVtable));
    Put(queueAddress+0x14,static_cast<std::uint32_t>(buffer));Put(queueAddress+0x10,static_cast<std::uint32_t>(buffer+0x10000));
    Put(queueAddress+0xC,static_cast<std::uint32_t>(buffer));originalReflector=Producer;originalMainRender=NoopProducer;
    ReflectorHook(reinterpret_cast<void*>(self),nullptr,reinterpret_cast<void*>(queueAddress),reinterpret_cast<void*>(sourceEntry),nullptr,reinterpret_cast<void*>(viewAddress),nullptr,nullptr);
    NativeReflectionPacketsKL::Certificate certificate;unsigned phase=99;
    for(unsigned n=0;n<4;++n)Check(ReflectionPacket(emitted[n],certificate,phase)==1&&phase==n,"production consumer matches exact certified native phase");
    const auto last=emitted.back();
    MainRenderHook(nullptr,nullptr,reinterpret_cast<void*>(queueAddress),nullptr,nullptr,nullptr,nullptr,nullptr);
    Check(ReflectionPacket(last,certificate,phase)==1,"appending another camera to same queue preserves earlier certificates");
    Put(emitted[1]+0x48,std::uint32_t(911));
    Check(ReflectionPacket(emitted[1],certificate,phase)==-1,"changed queued payload inside a producer scope cannot fall through as main eye");
    Put(queueAddress+0xC,static_cast<std::uint32_t>(buffer));
    MainRenderHook(nullptr,nullptr,reinterpret_cast<void*>(queueAddress),nullptr,nullptr,nullptr,nullptr,nullptr);
    Check(ReflectionPacket(last,certificate,phase)==0,"queue reuse retires even byte-identical old certificates and rejection ranges");
    emitted.clear();emitNested=true;
    ReflectorHook(reinterpret_cast<void*>(self),nullptr,reinterpret_cast<void*>(queueAddress),reinterpret_cast<void*>(sourceEntry),nullptr,reinterpret_cast<void*>(viewAddress),nullptr,nullptr);
    Check(emitted.size()==8,"private nested native producer appended both scopes");
    Check(ReflectionPacket(emitted[1],certificate,phase)==1&&phase==1&&certificate.reflector==nestedSelf,"nested exact inner certificate takes precedence over unknown outer scope");
    Check(ReflectionPacket(emitted[5],certificate,phase)==-1,"unknown outer sequence retains native rendering instead of borrowing inner camera identity");
    Check(reflectionCertified.load()==2&&reflectionRejected.load()==1,"production counters distinguish exact and rejected producers");
    auto producerEvidence=Snapshot();
    Check(producerEvidence.lastReflectionProducerAccepted.valid&&producerEvidence.lastReflectionProducerRejected.valid&&
        producerEvidence.lastReflectionProducerRejected.trace.reason==NativeReflectionPacketsKL::Failure::Entry&&
        producerEvidence.lastReflectionProducerRejected.trace.observed.packets[0].Word(4)==nestedSelf,
        "rejected producer scope preserves actual offending packet before any mathematical certificate exists");
    const auto rejectsBefore=reflectionRejected.load();originalReflector=NoopProducer;
    ReflectorHook(reinterpret_cast<void*>(self),nullptr,reinterpret_cast<void*>(queueAddress),reinterpret_cast<void*>(sourceEntry),nullptr,reinterpret_cast<void*>(viewAddress),nullptr,nullptr);
    Check(reflectionEmptyScopes.load()==1&&reflectionRejected.load()==rejectsBefore,
        "inactive reflector with no appended commands is counted as empty rather than a failed certificate");originalReflector=Producer;
    // Execute the ACTUAL GPU hook through a private i386 callsite carrying its
    // exact expected return RVA. Native decoder view/P copies are reproduced
    // before entry, while all external rendering/VR callbacks are private.
    Put(queueAddress+0xC,static_cast<std::uint32_t>(buffer));
    MainRenderHook(nullptr,nullptr,reinterpret_cast<void*>(queueAddress),nullptr,nullptr,nullptr,nullptr,nullptr);
    emitted.clear();ReflectorHook(reinterpret_cast<void*>(self),nullptr,reinterpret_cast<void*>(queueAddress),reinterpret_cast<void*>(sourceEntry),nullptr,reinterpret_cast<void*>(viewAddress),nullptr,nullptr);
    using namespace NativeCameraMath;using namespace NativeReflectionCameraMath::Detail;
    const CameraPose original{{{0,0,1},{0,1,0},{1,0,0}},{0,-170,0}};
    auto mirror=Identity();mirror.a[5]=-1;
    const auto mirrored=Packed(Multiply(mirror,View(original)));
    float nativeP[16]{};nativeP[0]=1.4f;nativeP[5]=2.4f;nativeP[10]=1;nativeP[11]=1;nativeP[14]=-5;
    for(unsigned n=0;n<4;++n){Put(emitted[n]+0x18,(n&1)?mirrored:original);std::memcpy(reinterpret_cast<void*>(emitted[n]+0x48),nativeP,sizeof(nativeP));}
    // Re-certify the private native payloads exactly as the producer would.
    NativeReflectionPacketsKL::Certificate current;
    Check(NativeReflectionPacketsKL::Capture(queueAddress,buffer,buffer+4*0xB0,sourceEntry,self,viewAddress,
        [](std::uintptr_t p,void* out,std::size_t bytes){SIZE_T got=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(p),out,bytes,&got)&&got==bytes;},current),"native-shaped matrices retain complete producer certificate");
    AcquireSRWLockExclusive(&reflectionLock);reflectionPackets={};reflectionPackets[0]={current,GetTickCount(),buffer};ReleaseSRWLockExclusive(&reflectionLock);
    const auto data=image+0x100000,player=image+0x110000,camera=image+0x120000,state=image+0x130000,geometry=image+0x140000;
    const auto system=image+0x150000,manager=image+0x160000;
    Put(image+0x6875F0,static_cast<std::uint32_t>(system));Put(system+0x96C,static_cast<std::uint32_t>(manager));
    Put(image+0x68760C,static_cast<std::uint32_t>(data));Put(data+0xA2C,static_cast<std::uint32_t>(player));
    Put(player+0x9A0,static_cast<std::uint32_t>(camera));Put(camera,static_cast<std::uint32_t>(image+kCameraVtable));
    Put(sourceEntry+0xF4,static_cast<std::uint32_t>(geometry));Put(geometry+0x60,static_cast<std::uint32_t>(camera));
    CameraPose world;Check(NativeCameraKLMath::WorldFromView(original,world),"private original view has valid native camera world");
    Put(sourceEntry+0x10,world);Put(image+0x7678A4,std::uint32_t(0));
    const auto caller=image+kGpuCallerRva-9;const unsigned char pushArg[]={0xFF,0x74,0x24,0x04};
    std::memcpy(reinterpret_cast<void*>(caller),pushArg,sizeof(pushArg));Put(caller+4,std::uint8_t(0xE8));
    Put(caller+5,static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(&GpuHook)-(caller+9)));
    const unsigned char returnArg[]={0xC2,0x04,0};std::memcpy(reinterpret_cast<void*>(caller+9),returnArg,sizeof(returnArg));
    DWORD protection=0;Check(VirtualProtect(reinterpret_cast<void*>(caller&~std::uintptr_t(4095)),4096,PAGE_EXECUTE_READ,&protection)!=0,"private exact native caller stub executable");
    FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(caller),12);
    const auto invoke=[&](std::uintptr_t packet){
        std::memcpy(reinterpret_cast<void*>(state+0xBC),reinterpret_cast<void*>(packet+0x18),sizeof(CameraPose));
        std::memcpy(reinterpret_cast<void*>(state+0x3F8),reinterpret_cast<void*>(packet+0x48),sizeof(nativeP));
        reinterpret_cast<void(__fastcall*)(void*,void*,const float*)>(caller)(reinterpret_cast<void*>(state),nullptr,reinterpret_cast<float*>(packet+0xA0));
    };
    originalGpu=Setter;provide=Provider;applied=Applied;installed=true;
    frameGeneration=1;gpuGeneration=1;frameValid=true;gpuEye=world;gpuEye.position.x=7;
    gpuFrame={};gpuFrame.sourceCamera=camera;gpuFrame.pairId=78;gpuFrame.worldUnitsPerMetre=100;
    gpuFrame.eyeOffset[0]=.032f;gpuFrame.eyeOffset[1]=.002f;gpuFrame.eyeOffset[2]=-.004f;
    gpuContext={camera,player,0,false,false};gpuContext.movementKnown=true;
    pair={};pair.valid=true;pair.source=camera;pair.pairId=78;pair.centre=world;
    std::memcpy(gpuProjection,nativeP,sizeof(nativeP));gpuProjection[0]=.8f;gpuProjection[5]=.7f;gpuProjection[8]=.06f;
    const auto cachedEye=gpuEye;const auto cachedPair=pair;
    CameraPose expectedMain;NativeCameraKLMath::ViewFromWorld(gpuEye,expectedMain);
    invoke(emitted[0]);
    Check(hostApplied==0&&handFrames==0&&bodyFrames==0&&gpuState==0&&
        !std::memcmp(reinterpret_cast<const void*>(state+0xBC),&expectedMain,sizeof(expectedMain))&&
        !std::memcmp(reinterpret_cast<const void*>(state+0x3F8),gpuProjection,sizeof(gpuProjection)),
        "actual first auxiliary reflector packet uses VR view/lens without publishing a main eye");
    ReflectionTargetView targetReceipt;
    Check(!ReadReflectionTargetView(targetReceipt),"original-view reflector phase cannot certify RT rendering");
    CameraPose billboardReference;
    Check(!ReadReflectionBillboardReference(state,billboardReference),
        "original-view reflector phase grants no billboard reference");
    float scatterColumns[8]{};
    Check(!ReadReflectionScatter(targetReceipt,scatterColumns),"original-view packet grants no reflected Scatter columns");
    invoke(emitted[1]);CameraPose actualView;std::memcpy(&actualView,reinterpret_cast<void*>(state+0xBC),sizeof(actualView));
    CameraPose expectedReflection;Check(NativeReflectionCameraMath::BuildView(original,mirrored,expectedMain,expectedReflection)&&
        !std::memcmp(&actualView,&expectedReflection,sizeof(actualView)),"actual reflected GPU packet receives reflected current VR eye");
    Check(!std::memcmp(&gpuEye,&cachedEye,sizeof(gpuEye))&&!std::memcmp(&pair,&cachedPair,sizeof(pair))&&hostApplied==0&&handFrames==0&&bodyFrames==0&&handClears>0&&bodyClears>0&&gpuState==0,
        "actual reflection preserves main cached eye/pair and never publishes host eye or player overrides");
    Check(ReadReflectionTargetView(targetReceipt)&&targetReceipt.serial&&targetReceipt.frame==frameGeneration.load()&&
        targetReceipt.pair==78&&targetReceipt.eyeIndex==gpuFrame.eye&&
        !std::memcmp(&targetReceipt.view,&expectedReflection,sizeof(expectedReflection)),
        "reflected RT snapshot is the exact certified current reflected state without main-eye authority");
    const auto reflectedWorld=Packed(RigidInverse(View(expectedReflection)));
    const Vec3 expectedUp{reflectedWorld.basis.right.y,reflectedWorld.basis.up.y,reflectedWorld.basis.forward.y};
    Check(ReadReflectionScatter(targetReceipt,scatterColumns)&&
        scatterColumns[0]==expectedUp.x&&scatterColumns[1]==expectedUp.y&&scatterColumns[2]==expectedUp.z&&
        scatterColumns[4]==gpuFrame.eyeOffset[0]*100&&scatterColumns[5]==gpuFrame.eyeOffset[1]*100&&
        scatterColumns[6]==-gpuFrame.eyeOffset[2]*100,
        "reflected Scatter uses the actual negative-parity VIEW for world up and eye-local stereo offset");
    CameraPose centredView,expectedBillboard;
    Check(NativeCameraKLMath::ViewFromWorld(pair.centre,centredView)&&
        NativeReflectionCameraMath::BuildView(original,mirrored,centredView,expectedBillboard)&&
        ReadReflectionBillboardReference(state,billboardReference)&&
        !std::memcmp(&billboardReference,&expectedBillboard,sizeof(billboardReference)),
        "certified pre-draw billboard reference uses the native reflector and the steady stereo reference");
    CameraPose rotatedBillboard;
    EffectsView reflectedEffects;
    Check(!ReadReflectionBillboardReference(state+4,rotatedBillboard)&&!ReadEffectsView(reflectedEffects),
        "billboard proof accepts only the exact reflected state and does not authorize main-view effects");
    gpuEye.basis={{0,.6f,.8f},{0,.8f,-.6f},{1,0,0}};
    invoke(emitted[1]);
    Check(ReadReflectionTargetView(targetReceipt)&&
        std::memcmp(&targetReceipt.view,&expectedReflection,sizeof(expectedReflection))&&
        ReadReflectionBillboardReference(state,rotatedBillboard)&&
        !std::memcmp(&rotatedBillboard,&billboardReference,sizeof(billboardReference)),
        "tracking pitch rotates the reflected render view but leaves the reflected billboard reference steady");
    float rotatedColumns[8]{};
    Check(ReadReflectionScatter(targetReceipt,rotatedColumns)&&
        std::memcmp(scatterColumns,rotatedColumns,3*sizeof(float))&&
        !std::memcmp(scatterColumns+4,rotatedColumns+4,4*sizeof(float)),
        "reflected Scatter world-up columns follow view rotation while the eye-local offset stays invariant");
    gpuEye=cachedEye;
    invoke(emitted[1]);
    Put(state+0xBC,999.f);Check(!ReadReflectionTargetView(targetReceipt),"changed VIEW retires target permission before any target getter");
    Check(!ReadReflectionBillboardReference(state,billboardReference),"changed reflected VIEW grants no billboard reference");
    Put(state+0xBC,expectedReflection.basis.forward.x);
    Put(state+0x3F8,999.f);Check(!ReadReflectionTargetView(targetReceipt),"changed projection cannot borrow a reflected RT certificate");
    Check(!ReadReflectionBillboardReference(state,billboardReference),"changed reflected projection grants no billboard reference");
    Put(state+0x3F8,gpuProjection[0]);
    Check(ReadReflectionTargetView(targetReceipt)&&gpuState==0,"restored exact reflected state retains only its RT receipt");
    invoke(emitted[2]);Check(!ReadReflectionTargetView(targetReceipt),"restore camera packet immediately retires reflected RT snapshot");std::memcpy(&actualView,reinterpret_cast<void*>(state+0xBC),sizeof(actualView));
    Check(!ReadReflectionBillboardReference(state,billboardReference),"restore packet retires reflected billboard reference");
    Check(!ReadReflectionScatter(targetReceipt,rotatedColumns),"restore packet retires reflected Scatter columns");
    Check(!std::memcmp(&actualView,&expectedMain,sizeof(actualView))&&
        !std::memcmp(reinterpret_cast<const void*>(state+0x3F8),gpuProjection,sizeof(gpuProjection))&&
        hostApplied==0&&handFrames==0&&bodyFrames==0&&gpuState==0,
        "actual third auxiliary packet restores exact cached VR view without main-eye or player authorization");
    invoke(emitted[3]);std::memcpy(&actualView,reinterpret_cast<void*>(state+0xBC),sizeof(actualView));
    Check(hostApplied==0&&gpuState==0&&!std::memcmp(&actualView,&expectedReflection,sizeof(actualView)),
        "actual final reflector-entry projection shares reflected VR eye without publishing a main eye");
    Check(!ReadReflectionBillboardReference(state,billboardReference),"final sampling camera grants no RT billboard reference");
    Check(!ReadReflectionScatter(targetReceipt,rotatedColumns),"final sampling camera grants no reflected Scatter columns");
    frameValid=false;pair={};hostApplied=0;providerCalls=0;
    const auto viewsBeforeFirstPacket=reflectionViews.load();
    invoke(emitted[1]);Check(frameValid&&providerCalls==1&&hostApplied==0&&reflectionViews.load()==viewsBeforeFirstPacket+1,
        "first observed mirrored packet derives main base from certified original view and publishes no host eye");
    Put(emitted[1]+0x48,1.1f);const auto stableEye=gpuEye;
    invoke(emitted[1]);Check(!std::memcmp(&stableEye,&gpuEye,sizeof(stableEye))&&hostApplied==0&&gpuState==0,
        "changed certified payload retains native state and leaves cached main eye intact");
    invoke(emitted[2]);Check(hostApplied==0&&gpuState==0,"valid auxiliary original-view restore survives a rejected reflected payload");
    const auto ordinary=buffer+0x1000;
    std::memcpy(reinterpret_cast<void*>(ordinary),current.packets[0].bytes.data(),NativeReflectionPacketsKL::PacketBytes);
    Put(ordinary+4,static_cast<std::uint32_t>(sourceEntry));
    Put(queueAddress+0xC,static_cast<std::uint32_t>(ordinary+NativeReflectionPacketsKL::PacketBytes));
    invoke(ordinary);Check(hostApplied==1&&gpuState==state&&DrawStateActive(state)&&handFrames==1&&bodyFrames==1,
        "subsequent ordinary main packet restores normal first-person authorization after final reflector state");
    Check(!ReadReflectionBillboardReference(state,billboardReference),"ordinary main view grants no reflected billboard reference");
    Put(state+0x3D4,.998431384f);Put(state+0x3D8,2.f);
    EffectsView materialView;
    Check(ReadEffectsView(materialView)&&materialView.modelViewScale==.998431384f&&materialView.modelViewDepthBias==2.f,
        "owned effects snapshot reads exact native semantic1 scale/depth fields from current draw state");
    // The synthetic provider above does not need an actor anchor for its
    // reflection checks. Supply that already-owned local identity only for
    // the read-only render-lifetime token and exercise actual nested packets.
    gpuFrame.anchorValid=true;gpuFrame.anchorPlayer=player;pair.player=player;
    std::uint64_t leaseEpoch=0;unsigned leaseEye=99;
    Check(ReadRenderContext(leaseEpoch,leaseEye),"current main image exposes a render token before a nested reflector");
    const auto beforeReflectionEpoch=leaseEpoch;
    invoke(emitted[0]);
    Check(!ReadRenderContext(leaseEpoch,leaseEye),"actual certified reflector packet closes main render authority");
    invoke(ordinary);
    Check(ReadRenderContext(leaseEpoch,leaseEye)&&leaseEpoch==beforeReflectionEpoch,
        "actual main camera reentry after auxiliary ClearFrame resumes the same image epoch without borrowing the reflector");
    auto evidence=Snapshot();
    Check(evidence.lastReflectionAccepted.valid&&evidence.lastReflectionAccepted.reason==0&&
        evidence.lastReflectionAccepted.sourceCamera==camera&&evidence.lastReflectionAccepted.packet==emitted[1]&&
        evidence.lastReflectionAccepted.originalProjection[0]==nativeP[0],
        "accepted reflected pass retains coherent bounded native and VR camera evidence automatically");
    auto unusualLens=current;const float cropped=1.1f;
    std::memcpy(unusualLens.packets[1].bytes.data()+0x48,&cropped,sizeof(cropped));
    AcquireSRWLockExclusive(&reflectionLock);reflectionPackets[0]={unusualLens,GetTickCount(),buffer};ReleaseSRWLockExclusive(&reflectionLock);
    invoke(emitted[1]);evidence=Snapshot();
    Check(evidence.reflectionLensRejected==1&&evidence.lastReflectionRejected.valid&&evidence.lastReflectionRejected.reason==1&&
        evidence.lastReflectionRejected.originalProjection[0]==nativeP[0]&&evidence.lastReflectionRejected.obliqueProjection[0]==cropped&&
        evidence.lastReflectionAccepted.valid&&gpuState==0,
        "unusual native lens fails closed and retains its full matrices and reason without erasing accepted evidence");
    invoke(emitted[2]);invoke(emitted[3]);std::memcpy(&actualView,reinterpret_cast<void*>(state+0xBC),sizeof(actualView));
    Check(!std::memcmp(&actualView,&mirrored,sizeof(actualView))&&gpuState==0&&Snapshot().lastReflectionRejected.reason==1,
        "final sampling remains native after matching RT rejection even across a successful main restore");
    Check(!ReadReflectionTargetView(targetReceipt),"rejected reflected lens grants no target receipt");
    Check(!ReadReflectionBillboardReference(state,billboardReference),"rejected reflected lens grants no billboard reference");
    originalCameraWorld=NoopCameraWorld;
    Put(self+0xF4,static_cast<std::uint32_t>(image+0x12000));
    Put(nestedSelf+0xF4,static_cast<std::uint32_t>(image+0x62000));
    visibility={true,gpuFrame,world,world.position,GetTickCount()};
    visibility.eye={{},{5600,21175,210}};
    visibility.frame.eyeOffset[0]=.032f;
    visibility.frame.eyeOffset[1]=visibility.frame.eyeOffset[2]=0.f;
    Check(!QualifiedFloorCamera(self,sourceEntry).source,"unqualified Retomoto producer keeps its native camera");
    QualifyFloorReflector(self,true);
    const auto floorCamera=QualifiedFloorCamera(self,sourceEntry);
    Check(floorCamera.source==camera&&floorCamera.retomoto&&
          std::fabs(floorCamera.pose.position.z-(visibility.eye.position.z+3.2f))<0.01f,
          "qualified Retomoto producer uses the centre of the tracked pair");
    QualifyFloorReflector(nestedSelf,true);
    Check(QualifiedFloorCamera(self,sourceEntry).source==camera&&QualifiedFloorCamera(nestedSelf,sourceEntry).source==camera,
          "a second Retomoto producer preserves the first certificate");
    visibility.eye.position.y=25500;
    Check(!QualifiedFloorCamera(self,sourceEntry).source,"Retomoto roof keeps native reflection preparation");
    visibility.eye.position.y=19750;
    Check(QualifiedFloorCamera(self,sourceEntry).source==camera,"a qualified producer can continue on the lower floors");
    Put(self+0xF4,static_cast<std::uint32_t>(image+0x13000));
    Check(!QualifiedFloorCamera(self,sourceEntry).source,"a changed reflector geometry retires its old certificate");
    if(argc>1)ActualNativeWriters(argv[1],self);
    VirtualFree(memory,0,MEM_RELEASE);
    std::printf("reflection producer/consumer lifecycle: %u/%u\n",checks-failed,checks);return failed?1:0;
}
