// Production MainRender capture/consumer/TLS lifetime with private native
// memory. The actual native writer/decoder ABI is tested independently by
// native_projection_packet_tail_kl_test. No game, D3D or GPU is started.
#define main UnusedReflectionProductionMain
#include "native_reflection_camera_production_test.cpp"
#undef main
namespace SecondaryFixture {
using namespace NativeCameraKL;using namespace NativeCameraMath;
std::uintptr_t q=0,first=0,entry=0,camera=0,projectionPacket=0,popPacket=0;
float baseP[16]{},alternateP[16]{};CameraPose nativeView;
unsigned producerCalls=0;
std::uintptr_t Add(unsigned kind,unsigned words=0,unsigned source=0,unsigned pass=0){
    const auto at=std::uintptr_t(*reinterpret_cast<const std::uint32_t*>(q+0xC));
    const std::uint32_t h[6]={kind,source,std::uint32_t(q),kind==1?123u:0u,pass,words};std::memcpy(reinterpret_cast<void*>(at),h,24);
    std::memset(reinterpret_cast<void*>(at+24),0,words*4);Put(q+0xC,std::uint32_t(at+24+words*4));return at;
}
void __thiscall Producer(void* self,void* queue,void* a2,void*,void*,void*,void*){
    ++producerCalls;Check(reinterpret_cast<std::uintptr_t>(self)==entry&&reinterpret_cast<std::uintptr_t>(queue)==q&&a2==reinterpret_cast<void*>(0x1234),
        "actual MainRender hook forwards original thiscall self/queue/arguments without guessing source from arg2");
    const auto c=Add(1,0x26,std::uint32_t(entry));std::memcpy(reinterpret_cast<void*>(c+24),&nativeView,sizeof(nativeView));std::memcpy(reinterpret_cast<void*>(c+0x48),baseP,64);
    Add(0x2C);projectionPacket=Add(0x2B,16);std::memcpy(reinterpret_cast<void*>(projectionPacket+24),alternateP,64);
    const auto draw=Add(0x12,2,0,9);Put(draw+24,std::uint32_t(0x66660000));Put(draw+28,std::uint32_t(0x77770000));popPacket=Add(0x2D);
}
}
int main(){
    using namespace SecondaryFixture;using namespace NativeCameraKL;using namespace NativeCameraMath;
    static_assert(sizeof(void*)==4,"native consumer ABI fixture is x86");
    auto* memory=VirtualAlloc(nullptr,0x900000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);Check(memory!=nullptr,"private camera/queue/renderer state allocated");if(!memory)return 1;
    image=reinterpret_cast<std::uintptr_t>(memory);entry=image+0x10000;camera=image+0x20000;q=image+0x30000;first=image+0x40000;
    const auto geometry=image+0x50000,state=image+0x60000,data=image+0x70000,player=image+0x80000;
    Put(entry,std::uint32_t(image+kCameraEntryVtable));Put(entry+0xF4,std::uint32_t(geometry));Put(geometry+0x60,std::uint32_t(camera));Put(camera,std::uint32_t(image+kCameraVtable));
    Put(image+0x68760C,std::uint32_t(data));Put(data+0xA2C,std::uint32_t(player));Put(player+0x9A0,std::uint32_t(camera));
    Put(q+0x14,std::uint32_t(first));Put(q+0xC,std::uint32_t(first));Put(q+0x10,std::uint32_t(first+0x10000));
    baseP[0]=1.2f;baseP[5]=1.6f;baseP[10]=baseP[11]=1;baseP[14]=-5;std::memcpy(alternateP,baseP,64);alternateP[0]=2.1f;alternateP[5]=2.8f;
    CameraPose nativeEye;NativeCameraKLMath::ViewFromWorld(nativeEye,nativeView);originalMainRender=SecondaryFixture::Producer;
    MainRenderHook(reinterpret_cast<void*>(entry),nullptr,reinterpret_cast<void*>(q),reinterpret_cast<void*>(0x1234),nullptr,nullptr,nullptr,nullptr);
    Check(producerCalls==1&&secondaryCertified.load()==1,"production producer retains one exact source-owned alternate mask scope");
    installed=true;frameValid=true;frameGeneration=gpuGeneration=5;gpuThread=GetCurrentThreadId();gpuState=currentCameraState=state;
    gpuFrame={};gpuFrame.sourceCamera=camera;gpuFrame.anchorPlayer=player;gpuFrame.anchorValid=true;gpuFrame.pairId=42;gpuFrame.eye=1;
    pair={};pair.valid=true;pair.source=camera;pair.player=player;pair.pairId=42;
    gpuEye=nativeEye;gpuEye.position={7,180,3};CameraPose view;NativeCameraKLMath::ViewFromWorld(gpuEye,view);
    std::memcpy(gpuProjection,baseP,64);gpuProjection[0]=.8f;gpuProjection[5]=.7f;gpuProjection[8]=.06f;
    const auto copy=[&](){std::memcpy(reinterpret_cast<void*>(state+0xBC),&view,sizeof(view));std::memcpy(reinterpret_cast<void*>(state+0x3F8),alternateP,64);SecondaryProjectionPacket(state,projectionPacket,0);};
    const auto pop=[&](){std::memcpy(reinterpret_cast<void*>(state+0x3F8),gpuProjection,64);SecondaryProjectionPacket(state,popPacket,1);};
    copy();Check(IsSecondaryProjectionDraw(state)&&DrawStateActive(state)&&!std::memcmp(reinterpret_cast<void*>(state+0x3F8),gpuProjection,64),
        "exact certified secondary P becomes the same physical VR lens as ordinary main geometry");
    Check(!std::memcmp(reinterpret_cast<void*>(projectionPacket+24),alternateP,64)&&!std::memcmp(reinterpret_cast<void*>(state+0xBC),&view,sizeof(view)),
        "queued native projection/source payload and actual VIEW remain untouched");
    ObserveSecondaryMaskBind(state,0x88880000,0);ObserveSecondaryMaskBind(state,0x99990000,1);
    Check(secondaryBodyBinds.load()==1&&secondaryWeaponBinds.load()==1,"actual active TLS mask receipt counts separate successful owned bind classes");
    ObserveSecondaryMaskBind(state+4,0x88880000,0);ObserveSecondaryMaskBind(state,0,1);ObserveSecondaryMaskBind(state,1,2);
    Check(secondaryBodyBinds.load()==1&&secondaryWeaponBinds.load()==1,"foreign state/missing entry/unknown bind class cannot add receipt");
    pop();Check(!IsSecondaryProjectionDraw(state)&&secondaryPopped.load()==1&&DrawStateActive(state),"exact native pop closes only secondary receipt and restores main VR draw guard");
    ObserveSecondaryMaskBind(state,1,0);Check(secondaryBodyBinds.load()==1,"ordinary later colour binds do not inherit secondary scope");
    copy();SecondaryProjectionPacket(state,projectionPacket+4,0);Check(!IsSecondaryProjectionDraw(state),"unknown projection command retires secondary scope");
    copy();SecondaryProjectionPacket(state,popPacket+4,1);Check(!IsSecondaryProjectionDraw(state),"foreign projection pop cannot retain secondary authorization");
    std::memcpy(reinterpret_cast<void*>(state+0xBC),&nativeView,sizeof(nativeView));std::memcpy(reinterpret_cast<void*>(state+0x3F8),alternateP,64);
    SecondaryProjectionPacket(state,projectionPacket,0);Check(!IsSecondaryProjectionDraw(state)&&!std::memcmp(reinterpret_cast<void*>(state+0x3F8),alternateP,64),"native/foreign VIEW remains native even with exact source packet");
    gpuFrame.nativeCinematic=true;copy();Check(!IsSecondaryProjectionDraw(state)&&!std::memcmp(reinterpret_cast<void*>(state+0x3F8),alternateP,64),"cinematic source is never normalized by gameplay mask certificate");gpuFrame.nativeCinematic=false;
    gpuFrame.anchorValid=false;copy();Check(!IsSecondaryProjectionDraw(state),"non-first-person source remains native");gpuFrame.anchorValid=true;
    Put(geometry+0x60,std::uint32_t(camera+4));copy();Check(!IsSecondaryProjectionDraw(state),"reparented source camera entry fails live ownership");Put(geometry+0x60,std::uint32_t(camera));
    const auto changed=alternateP[0]+1;Put(projectionPacket+24,changed);copy();Check(!IsSecondaryProjectionDraw(state),"changed native queue payload cannot borrow old source certificate");Put(projectionPacket+24,alternateP[0]);
    copy();++frameGeneration;Check(!IsSecondaryProjectionDraw(state),"eye-generation boundary invalidates receipt before a stale bind can use it");SecondaryProjectionPacket(state,popPacket,1);frameGeneration=5;
    copy();FinishScene();Check(!IsSecondaryProjectionDraw(state),"finish scene retires active scope without inventing a pop");
    copy();pop();const auto snapshot=Snapshot();Check(snapshot.lastSecondaryAccepted.valid&&snapshot.lastSecondaryRejected.valid&&
        snapshot.secondaryApplied>=5&&snapshot.secondaryBodyBinds==1&&snapshot.secondaryWeaponBinds==1,"automatic evidence preserves accepted/rejected native and VR lens provenance");
    Put(q+0xC,std::uint32_t(first));originalMainRender=NoopProducer;
    MainRenderHook(reinterpret_cast<void*>(entry),nullptr,reinterpret_cast<void*>(q),nullptr,nullptr,nullptr,nullptr,nullptr);
    copy();Check(!IsSecondaryProjectionDraw(state),"rewound/reused queue retires previous scope even if later bytes remain identical");
    originalMainRender=SecondaryFixture::Producer;alternateP[14]=-7;Put(q+0xC,std::uint32_t(first));
    MainRenderHook(reinterpret_cast<void*>(entry),nullptr,reinterpret_cast<void*>(q),reinterpret_cast<void*>(0x1234),nullptr,nullptr,nullptr,nullptr);
    const auto denied=Snapshot().lastSecondaryProducerRejected;
    Check(denied.valid&&denied.reason==5&&denied.sourceEntry==entry&&denied.drawHeader[4]==9&&
        denied.mainProjection[14]==-5&&denied.alternateProjection[14]==-7,"actual producer rejection snapshots incompatible raw lenses and native mask header before authorization");
    Check(!handFrames&&!bodyFrames&&!hostApplied&&!providerCalls,"projection-only path never publishes a camera/host eye or rotates body/hand payload");
    installed=false;VirtualFree(memory,0,MEM_RELEASE);
    std::printf("Secondary native mask actual production capture/consumer/TLS: %u/%u PASS; private CPU only.\n",checks-failed,checks);return failed?1:0;
}
