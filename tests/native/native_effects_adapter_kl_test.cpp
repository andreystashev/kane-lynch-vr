#include "native_hud_kl.cpp"
#include "native_effects_kl.cpp"
#include "native_effects_math_kl.hpp"
#include <cstdio>
#include <array>
namespace NativeCameraKL {
std::uintptr_t CinematicCamera(){return 0;}
bool CinematicPanelActive(){return true;}
EffectsView fixtureView;
bool ReadEffectsView(EffectsView& output){output=fixtureView;return true;}
bool ReadReflectionBillboardReference(std::uintptr_t,NativeCameraMath::CameraPose&){return false;}
bool ReadReflectionScatter(ReflectionTargetView&,float (&)[8]){return false;}
}
namespace {
using NativeEffectsKLMath::Vertex;
unsigned checks=0,failures=0,filters=0,unlocks=0,resourceSize=0,lastFiltersAtUnlock=0;
NativeHudKL::MappedVertexContext lastContext{};
bool contextKnown=false;
bool beforeUnlock=false;
float columns[8]={0,1,0,1e-6f,0,0,0,0};
void Check(bool value,const char* message){++checks;if(!value){++failures;std::printf("FAIL: %s\n",message);}}
void Filter(std::uint32_t writer,void* data,std::size_t size){
    contextKnown=NativeHudKL::ReadMappedVertexContext(lastContext);
    Check(writer==0x99360||writer==0xA37D3||writer==0x9997E,"only exact native effect allocation callers are offered to adapter");
    beforeUnlock=true;++filters;
    if(writer==0x99360)Check(NativeEffectsKLMath::MappedQuads(data,size,columns),"leased native mapped span supports exact world-quad conversion");
    else if(writer==0xA37D3){
        NativeCameraMath::CameraPose eye{},reference{};
        Check(NativeEffectsKLMath::MappedManualQuads(data,size,eye,reference),"leased ManualScatter stride28 span preserves its native private data");
    }
}
void __thiscall Unlock(void*){lastFiltersAtUnlock=filters;++unlocks;}
struct Device {void** table;};
struct Resource {void** table;};
std::array<void*,16> deviceTable{},resourceTable{};
Device device{deviceTable.data()};Resource resource{resourceTable.data()};
ULONG STDMETHODCALLTYPE ReleaseDevice(void*){return 1;}
HRESULT STDMETHODCALLTYPE GetDevice(void*,IDirect3DDevice9** out){*out=reinterpret_cast<IDirect3DDevice9*>(&device);return S_OK;}
HRESULT STDMETHODCALLTYPE GetDesc(void*,D3DVERTEXBUFFER_DESC* out){*out={};out->Type=D3DRTYPE_VERTEXBUFFER;out->Size=resourceSize;return S_OK;}
}
extern "C" char EffectsAdapterParticleReturn,EffectsAdapterManualReturn;
extern "C" __attribute__((naked)) void EffectsAdapterParticle(void*,void*,unsigned,unsigned){
    __asm__ volatile("pushl %ebp\nmovl %esp,%ebp\nsubl $0x98,%esp\n"
        "movl 16(%ebp),%eax\nmovl %eax,0x74(%esp)\nmovl 20(%ebp),%eax\nmovl %eax,0x94(%esp)\n"
        "movl 8(%ebp),%ecx\nxorl %edx,%edx\ncall *12(%ebp)\n"
        ".globl _EffectsAdapterParticleReturn\n_EffectsAdapterParticleReturn:\nleave\nret\n");
}
extern "C" __attribute__((naked)) void EffectsAdapterManual(void*,void*,unsigned){
    __asm__ volatile("pushl %ebp\nsubl $0x3f30,%esp\nmovl %esp,%ebp\n"
        "movl 0x3f40(%ebp),%eax\nmovl %eax,0x3f10(%ebp)\nmovl 0x3f38(%ebp),%ecx\n"
        "movl 0x3f3c(%ebp),%eax\nxorl %edx,%edx\ncall *%eax\n"
        ".globl _EffectsAdapterManualReturn\n_EffectsAdapterManualReturn:\naddl $0x3f30,%esp\npopl %ebp\nret\n");
}
int main(){
    using namespace NativeHudKL;
    image=0x400000;originalBufferUnlock=Unlock;SetMappedVertexFilter(Filter);
    deviceTable[2]=reinterpret_cast<void*>(&ReleaseDevice);resourceTable[3]=reinterpret_cast<void*>(&GetDevice);resourceTable[13]=reinterpret_cast<void*>(&GetDesc);
    Vertex input[4]={{90,-20,500,1,.1f,.2f},{90,20,500,2,.3f,.4f},{110,20,500,3,.5f,.6f},{110,-20,500,4,.7f,.8f}};
    Vertex vertices[4];resourceSize=sizeof(vertices);
    NativeBufferControl control{std::uint32_t(reinterpret_cast<std::uintptr_t>(&resource)),std::uint32_t(reinterpret_cast<std::uintptr_t>(vertices)),resourceSize,resourceSize};
    unsigned first=0;
    const auto reset=[&]{InvalidateDevice();std::memcpy(vertices,input,sizeof(input));control.end=resourceSize;beforeUnlock=false;};
    const auto lock=[&](std::uint32_t writer,unsigned stride=24){RegisterWrite(&control,vertices,4,stride,&first,NativeHudShadow::Kind::Vertex,image+writer);};
    reset();const auto original=control;lock(0x99360);Check(pendingWrites.load(),"exact native world writer gets bounded pre-Unlock pending lease");
    BufferUnlockHook(&control,nullptr);
    Check(filters==1&&lastFiltersAtUnlock==1&&unlocks==1&&beforeUnlock,"filter runs once before the original native Unlock");
    Check(!std::memcmp(&control,&original,sizeof(control)),"native resource/control identity is never modified");
    Check(!pendingWrites.load(),"world mapped lease retired before native Unlock");
    BufferUnlockHook(&control,nullptr);Check(filters==1&&unlocks==2,"repeated Unlock cannot reuse old mapped span");
    reset();lock(0x99361);BufferUnlockHook(&control,nullptr);
    Check(filters==1&&!std::memcmp(vertices,input,sizeof(input)),"unrelated world writer retains exact vertices");
    reset();control.end=4*12;lock(0x99360,12);BufferUnlockHook(&control,nullptr);
    Check(filters==1&&!std::memcmp(vertices,input,sizeof(input)),"unknown vertex layout cannot qualify by caller alone");
    reset();lock(0x99551);BufferUnlockHook(&control,nullptr);
    Check(filters==1&&!std::memcmp(vertices,input,sizeof(input)),"real stride24 interface writer remains HUD data and never receives world correction");
    reset();lock(0x99360);--control.end;BufferUnlockHook(&control,nullptr);
    Check(filters==1&&!std::memcmp(vertices,input,sizeof(input)),"changed mapped-span bounds invalidate effect authorization");
    reset();lock(0x99360);auto different=control;BufferUnlockHook(&different,nullptr);
    Check(filters==1&&pendingWrites.load(),"another native control cannot consume the pending effect lease");
    BufferUnlockHook(&control,nullptr);Check(filters==2,"original control can still consume its own lease");
    reset();lock(0x99360);InvalidateDevice();BufferUnlockHook(&control,nullptr);
    Check(filters==2&&!std::memcmp(vertices,input,sizeof(input)),"reset clears pending native world span without stale writes");
    reset();SetMappedVertexFilter(nullptr);lock(0x99360);BufferUnlockHook(&control,nullptr);
    Check(filters==2&&!std::memcmp(vertices,input,sizeof(input)),"disabled mapped adapter retains original world writer");
    SetMappedVertexFilter(Filter);reset();const auto oldFilters=filters;lock(0x9997E);BufferUnlockHook(&control,nullptr);
    Check(filters==oldFilters+1&&lastFiltersAtUnlock==filters&&!std::memcmp(vertices,input,sizeof(input)),"type7 particles are observed before Unlock without applying the type1 correction");
    using NativeEffectsKLMath::ManualVertex;
    ManualVertex manual[4]={{-10,-20,500,1,0x7F00FFFF,.1f,.2f},{-10,20,500,2,0x7F00FFFE,.3f,.4f},
        {10,20,500,3,0x7F00FFFD,.5f,.6f},{10,-20,500,4,0x7F00FFFC,.7f,.8f}},saved[4];
    std::memcpy(saved,manual,sizeof(manual));InvalidateDevice();resourceSize=sizeof(manual);
    NativeBufferControl manualControl{std::uint32_t(reinterpret_cast<std::uintptr_t>(&resource)),std::uint32_t(reinterpret_cast<std::uintptr_t>(manual)),resourceSize,resourceSize};
    const auto oldManual=manualControl;
    RegisterWrite(&manualControl,manual,4,28,&first,NativeHudShadow::Kind::Vertex,image+0xA37D3);
    Check(pendingWrites.load(),"exact ManualScatter stride28 native allocation receives its own pending lease");
    BufferUnlockHook(&manualControl,nullptr);
    Check(filters==oldFilters+2&&lastFiltersAtUnlock==filters,"ManualScatter callback precedes original Unlock");
    Check(!std::memcmp(&manualControl,&oldManual,sizeof(manualControl))&&!std::memcmp(manual,saved,sizeof(manual)),"neutral native ManualScatter private index and all original vertices survive adapter");
    InvalidateDevice();manualControl.end=4*24;
    RegisterWrite(&manualControl,manual,4,24,&first,NativeHudShadow::Kind::Vertex,image+0xA37D3);BufferUnlockHook(&manualControl,nullptr);
    Check(filters==oldFilters+2,"ManualScatter address with wrong stride cannot authorize a write");
    InvalidateDevice();manualControl.end=resourceSize;
    RegisterWrite(&manualControl,manual,4,28,&first,NativeHudShadow::Kind::Vertex,image+0xA37D4);BufferUnlockHook(&manualControl,nullptr);
    Check(filters==oldFilters+2,"adjacent ManualScatter return address remains unknown");
    // Real x86 caller stack fixture: actual production hook obtains the
    // return PC/CFA and saved native EBP itself, through its compiled prologue.
    image=reinterpret_cast<std::uintptr_t>(&EffectsAdapterParticleReturn)-0x9A19B;
    InvalidateDevice();resourceSize=sizeof(vertices);control.end=control.capacity=resourceSize;
    particleDrawContext.sourceState=0x12340000;particleDrawContext.particleMode=0;
    lock(0x9997E);EffectsAdapterParticle(&control,reinterpret_cast<void*>(&BufferUnlockHook),1,1);
    Check(contextKnown&&lastContext.liveKnown&&lastContext.liveQuads==1&&lastContext.particleMode==0&&lastContext.sourceState==0x12340000,
        "compiled native no-stack-argument Unlock ABI reads exact particle live prefix and mode0 context");
    Check(!ReadMappedVertexContext(lastContext),"mapped source context cannot escape callback lifetime");
    InvalidateDevice();lock(0x9997E);EffectsAdapterParticle(&control,reinterpret_cast<void*>(&BufferUnlockHook),2,1);
    Check(contextKnown&&!lastContext.liveKnown,"native count larger than allocation fails closed before tail access");
    InvalidateDevice();lock(0x9997E);EffectsAdapterParticle(&control,reinterpret_cast<void*>(&BufferUnlockHook),1,2);
    Check(contextKnown&&!lastContext.liveKnown,"foreign native batch count cannot authorize live prefix");
    image=reinterpret_cast<std::uintptr_t>(&EffectsAdapterManualReturn)-0xA3EA4;
    InvalidateDevice();resourceSize=sizeof(manual);manualControl.end=manualControl.capacity=resourceSize;
    RegisterWrite(&manualControl,manual,4,28,&first,NativeHudShadow::Kind::Vertex,image+0xA37D3);
    EffectsAdapterManual(&manualControl,reinterpret_cast<void*>(&BufferUnlockHook),0x23450000);
    Check(contextKnown&&lastContext.sourceState==0x23450000,"compiled native ManualScatter unusual EBP frame reads exact original state parameter");
    SetMappedVertexFilter(NativeEffectsKL::FilterMapped);NativeEffectsKL::Configure(NativeCameraKL::ReadEffectsView);
    auto& view=NativeCameraKL::fixtureView;view.state=0x12340000;
    view.reference.basis={{0,0,1},{0,1,0},{1,0,0}};
    view.eye.basis={{.6f,0,.8f},{0,1,0},{.8f,0,-.6f}};
    Vertex particles[8];for(unsigned q=0;q<2;++q)for(unsigned i=0;i<4;++i)particles[q*4+i]=input[i];
    // This tail is deliberately invalid. The production filter must neither
    // validate nor write it because the native producer emitted just one quad.
    for(unsigned i=4;i<8;++i)particles[i].x=std::numeric_limits<float>::quiet_NaN();
    Vertex originalParticles[8];std::memcpy(originalParticles,particles,sizeof(particles));
    resourceSize=sizeof(particles);NativeBufferControl particleControl{std::uint32_t(reinterpret_cast<std::uintptr_t>(&resource)),
        std::uint32_t(reinterpret_cast<std::uintptr_t>(particles)),resourceSize,resourceSize};
    image=reinterpret_cast<std::uintptr_t>(&EffectsAdapterParticleReturn)-0x9A19B;
    const auto particleCall=[&](unsigned mode,unsigned live,unsigned allocated){
        InvalidateDevice();particleDrawContext.sourceState=0x12340000;particleDrawContext.particleMode=mode;
        RegisterWrite(&particleControl,particles,8,24,&first,NativeHudShadow::Kind::Vertex,image+0x9997E);
        EffectsAdapterParticle(&particleControl,reinterpret_cast<void*>(&BufferUnlockHook),live,allocated);
    };
    auto effectBefore=NativeEffectsKL::Snapshot();particleCall(0,1,2);
    Check(NativeEffectsKL::Snapshot().particleQuads==effectBefore.particleQuads+1&&std::memcmp(particles,originalParticles,sizeof(input)),
        "actual production mode0 particle filter corrects exact native live prefix");
    Check(!std::memcmp(particles+4,originalParticles+4,sizeof(input)),"actual production filter never inspects or modifies allocated dead-particle tail");
    std::memcpy(particles,originalParticles,sizeof(particles));effectBefore=NativeEffectsKL::Snapshot();particleCall(1,1,2);
    Check(NativeEffectsKL::Snapshot().particleAlignedPreserved==effectBefore.particleAlignedPreserved+1&&!std::memcmp(particles,originalParticles,sizeof(particles)),
        "velocity-aligned native particle mode keeps bit-exact original geometry");
    effectBefore=NativeEffectsKL::Snapshot();particleCall(0,3,2);
    Check(NativeEffectsKL::Snapshot().particlePrefixRejected==effectBefore.particlePrefixRejected+1&&!std::memcmp(particles,originalParticles,sizeof(particles)),
        "unproven or excessive live count retains entire original allocation");
    view.state=0x12340004;effectBefore=NativeEffectsKL::Snapshot();particleCall(0,1,2);
    Check(NativeEffectsKL::Snapshot().sourceStateRejected==effectBefore.sourceStateRejected+1&&!std::memcmp(particles,originalParticles,sizeof(particles)),
        "same-thread foreign particle state cannot borrow accepted main camera");
    view.state=0x23450000;image=reinterpret_cast<std::uintptr_t>(&EffectsAdapterManualReturn)-0xA3EA4;
    InvalidateDevice();resourceSize=sizeof(manual);std::memcpy(manual,saved,sizeof(manual));effectBefore=NativeEffectsKL::Snapshot();
    RegisterWrite(&manualControl,manual,4,28,&first,NativeHudShadow::Kind::Vertex,image+0xA37D3);
    EffectsAdapterManual(&manualControl,reinterpret_cast<void*>(&BufferUnlockHook),0x23450000);
    Check(NativeEffectsKL::Snapshot().manualQuads==effectBefore.manualQuads+1&&std::memcmp(manual,saved,sizeof(manual)),
        "actual production ManualScatter applies only under exact native source state");
    for(unsigned i=0;i<4;++i)Check(!std::memcmp(reinterpret_cast<unsigned char*>(manual+i)+12,reinterpret_cast<unsigned char*>(saved+i)+12,16),
        "actual production ManualScatter retains original colour private index and UV bits");
    std::printf("Native effect mapped-writer adapter: %u checks, %u failures.\n",checks,failures);return failures?1:0;
}
