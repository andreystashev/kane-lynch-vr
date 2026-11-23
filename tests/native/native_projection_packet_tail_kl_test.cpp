// Actual pinned projection-copy/pop instruction slices and real MinHook,
// over private CPU state/queue memory. No game, window, D3D or VR is opened.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "MinHook.h"
#include "native_projection_tail_kl.hpp"
#include "native_hud_test_fixture.hpp"
#include "native_secondary_mask_projection_kl.hpp"
#include "profile.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
extern "C" {
std::uintptr_t KL1ProjectionSourceSavedStack=0,KL1ProjectionSourceBase=0,KL1ProjectionSourceSystem=0;
__attribute__((naked,noinline)) void __cdecl KL1ProjectionSourceInvoke(void*,void*,void*){
    __asm__ __volatile__(
        "pushal\n\tmovl %esp,_KL1ProjectionSourceSavedStack\n\tmovl %esp,%edx\n\tsubl $22784,%esp\n\t"
        "movl %esp,_KL1ProjectionSourceBase\n\tmovl $0x22223333,140(%esp)\n\t"
        "movl $0x33334444,96(%esp)\n\tmovl $1,68(%esp)\n\tmovl 44(%edx),%ecx\n\tmovl %ecx,22620(%esp)\n\t"
        "movl 36(%edx),%eax\n\tmovl 40(%edx),%ebp\n\t"
        "movl $1,%edi\n\txorl %ebx,%ebx\n\tjmp *%eax\n\t");
}
}
namespace {
unsigned checks=0,legacyCalls=0,packetCalls=0;std::uintptr_t expectedState=0,expectedPacket=0;
unsigned expectedPhase=0;float expectedNative=0;
void Check(bool ok,const char* why){++checks;if(!ok){std::printf("FAIL: %s\n",why);std::exit(1);}}
void Legacy(std::uintptr_t state){
    Check(state==expectedState&&expectedPhase==0,"historical callback receives exact copied state only");++legacyCalls;
    const float oldRepair=7.25f;std::memcpy(reinterpret_cast<void*>(state+0x3F8),&oldRepair,4);
    __asm__ __volatile__("fninit\n\tpxor %xmm0,%xmm0\n\tclc\n\t");
}
void Packet(std::uintptr_t state,std::uintptr_t packet,unsigned phase){
    Check(state==expectedState&&packet==expectedPacket&&phase==expectedPhase,"optional observer receives original queued EBP and copy/pop phase");
    float copied=0;std::memcpy(&copied,reinterpret_cast<void*>(state+0x3F8),4);
    Check(copied==(phase?expectedNative:7.25f),"copy preserves legacy-first callback order; pop observes native restored projection");
    ++packetCalls;const float result=phase?9.5f:8.5f;std::memcpy(reinterpret_cast<void*>(state+0x3F8),&result,4);
    __asm__ __volatile__("fninit\n\tpxor %xmm0,%xmm0\n\tstc\n\t");
}
void* __cdecl PrivateCopy(void* target,const void* source,std::size_t bytes){return std::memcpy(target,source,bytes);}
float SourceProjection(unsigned i){return i==0?1.5f:i==5?2.7f:(i==10||i==11)?1.f:i==14?-5.f:0.f;}
void __thiscall PrivateProjection(void* renderer,float* output,void* space,void* rectangle,unsigned infinite,unsigned alternate){
    Check(reinterpret_cast<std::uintptr_t>(renderer)==0x33334444&&reinterpret_cast<std::uintptr_t>(rectangle)==0x22223333,
        "actual secondary producer argument loads select its native renderer and rectangle");
    Check(reinterpret_cast<std::uintptr_t>(space)==KL1ProjectionSourceBase+0x124&&reinterpret_cast<std::uintptr_t>(output)==KL1ProjectionSourceBase+0x314,
        "actual secondary producer native stack offsets select CameraSpace and output P");
    Check(infinite==1&&alternate==1,"actual secondary producer passes infinite=1 and alternate-FOV=1 to native projection builder");
    for(unsigned i=0;i<16;++i)output[i]=SourceProjection(i);
}
void NativeSourceProof(const NativeHudFixture::Image& image){
    constexpr unsigned begin=0x4C1658,end=0x4C174F;
    std::vector<unsigned char> code(end-begin);
    Check(image.Read(begin,code.data(),code.size()),"actual secondary projection push/build/copy native bytes loaded");
    auto* executable=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    Check(executable!=nullptr,"private source writer allocated");
    std::vector<unsigned char> emitter(0x4B7BB7-0x4B7B20);
    Check(image.Read(0x4B7B20,emitter.data(),emitter.size()),"actual native six-argument geometry emitter loaded");
    auto* emitterCode=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    Check(emitterCode!=nullptr,"private native geometry emitter allocated");
    unsigned globalRelocations=0;for(unsigned i=0;i+4<=emitter.size();++i){std::uint32_t address=0;std::memcpy(&address,emitter.data()+i,4);
        if(address==0xA875F0){const auto replacement=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&KL1ProjectionSourceSystem));
            std::memcpy(emitter.data()+i,&replacement,4);++globalRelocations;}}
    Check(globalRelocations==1,"only native frame-counter global is relocated in actual geometry emitter");
    std::memcpy(emitterCode,emitter.data(),emitter.size());FlushInstructionCache(GetCurrentProcess(),emitterCode,emitter.size());
    const auto redirect=[&](unsigned address,unsigned target,std::uintptr_t replacement){
        const auto offset=address-begin;Check(code[offset]==0xE8,"only native external call is redirected to private ABI stub");
        std::int32_t original=0;std::memcpy(&original,code.data()+offset+1,4);
        Check(address+5+original==target,"actual external projection/memcpy call target authenticated");
        const auto displacement=static_cast<std::int32_t>(replacement-reinterpret_cast<std::uintptr_t>(executable+offset+5));
        std::memcpy(code.data()+offset+1,&displacement,4);
    };
    redirect(0x4C1697,0x486060,reinterpret_cast<std::uintptr_t>(&PrivateProjection));
    redirect(0x4C16D6,0x8BA9CE,reinterpret_cast<std::uintptr_t>(&PrivateCopy));
    redirect(0x4C1707,0x4B7B20,reinterpret_cast<std::uintptr_t>(emitterCode));
    const unsigned char restore[]={0x8B,0x25};code.insert(code.end(),restore,restore+2);
    const auto slot=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&KL1ProjectionSourceSavedStack));
    const auto* bytes=reinterpret_cast<const unsigned char*>(&slot);code.insert(code.end(),bytes,bytes+4);code.push_back(0x61);code.push_back(0xC3);
    std::memcpy(executable,code.data(),code.size());FlushInstructionCache(GetCurrentProcess(),executable,code.size());
    std::array<std::uint32_t,8> queue{};std::array<unsigned char,512> buffer{};
    const auto start=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(buffer.data()));
    const auto queueAt=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(queue.data()));
    constexpr std::uint32_t cameraEntry=0x11110000,camera=0x22220000;
    const std::uint32_t cameraHeader[]={1,cameraEntry,queueAt,0x44445555,0,0x26};
    std::memcpy(buffer.data(),cameraHeader,sizeof(cameraHeader));
    float mainP[16]{};for(unsigned i=0;i<16;++i)mainP[i]=SourceProjection(i);mainP[0]=2.4f;mainP[5]=4.3f;
    std::memcpy(buffer.data()+0x48,mainP,sizeof(mainP));
    queue[3]=start+0xB0;queue[5]=start;queue[4]=start+buffer.size();queue[2]=1;
    std::array<std::uint32_t,24> system{};system[0x44/4]=777;KL1ProjectionSourceSystem=reinterpret_cast<std::uintptr_t>(system.data());
    std::array<std::uint32_t,8> entry{},descriptor{};descriptor[0]=reinterpret_cast<std::uintptr_t>(entry.data());descriptor[2]=1u<<9;
    reinterpret_cast<unsigned char*>(descriptor.data())[0x11]=0x10;reinterpret_cast<unsigned char*>(descriptor.data())[0x12]=7;
    KL1ProjectionSourceInvoke(executable,queue.data(),descriptor.data());
    std::uint32_t first[6]{},second[6]{};std::memcpy(first,buffer.data()+0xB0,sizeof(first));std::memcpy(second,buffer.data()+0xB0+24,sizeof(second));
    Check(first[0]==0x2C&&first[2]==reinterpret_cast<std::uintptr_t>(queue.data())&&first[5]==0,
        "actual source emits native projection-stack push with exact queue ownership");
    Check(second[0]==0x2B&&second[2]==reinterpret_cast<std::uintptr_t>(queue.data())&&second[5]==16,
        "actual source emits projection-only kind2B payload of16DWORDs");
    bool copied=true;for(unsigned i=0;i<16;++i){float value=0;std::memcpy(&value,buffer.data()+0xB0+48+4*i,4);copied&=value==SourceProjection(i);}
    Check(copied,"actual source REP copies full builder P into closed queue payload");
    const unsigned drawOffset=0xB0+24+24+64,popOffset=drawOffset+28;
    std::uint32_t draw[7]{},popHeader[6]{};std::memcpy(draw,buffer.data()+drawOffset,sizeof(draw));std::memcpy(popHeader,buffer.data()+popOffset,sizeof(popHeader));
    Check(draw[0]==0x12&&draw[2]==queueAt&&draw[4]==9&&draw[5]==1&&draw[6]==reinterpret_cast<std::uintptr_t>(entry.data()),
        "actual source call4C1707 passes q,array,count,9,flag16,7 to actual geometry writer yielding exact kind12 payload");
    Check(entry[0x1C/4]==777,"actual geometry writer updates only the emitted entry native frame stamp");
    Check(popHeader[0]==0x2D&&popHeader[2]==queueAt&&popHeader[5]==0&&queue[2]==6&&queue[3]==start+popOffset+48,
        "actual source emits matching projection pop and following native state header with exact cursor/count");
    NativeSecondaryMaskProjectionKL::Batch batch;
    const auto read=[&](std::uintptr_t at,void* out,std::size_t bytes){
        if(at<start||at-start>buffer.size()||bytes>buffer.size()-(at-start))return false;
        std::memcpy(out,buffer.data()+at-start,bytes);return true;};
    Check(NativeSecondaryMaskProjectionKL::Capture(queueAt,start,start,start+popOffset+24,cameraEntry,camera,read,batch)&&
        batch.count==1&&batch.scopes[0].projectionPacket==start+0xB0+24&&batch.scopes[0].drawPacket==start+drawOffset&&batch.scopes[0].popPacket==start+popOffset,
        "production secondary certificate accepts exact camera-context plus actual push/projection/geometry/pop native writer output");
    VirtualFree(emitterCode,0,MEM_RELEASE);VirtualFree(executable,0,MEM_RELEASE);
}
struct Decoder {unsigned char* memory=nullptr;unsigned char* hook=nullptr;unsigned char* resume=nullptr;
    ~Decoder(){if(memory)VirtualFree(memory,0,MEM_RELEASE);}};
void Build(Decoder& decoder,const NativeHudFixture::Image& image,bool pop){
    // Preserve caller's callee-saved registers; allocate the native decoder's
    // projection stack offsets, set its live state pointer at[ESP+10], and
    // queued packet in EBP. Outstanding native x87 value is explicit.
    std::vector<unsigned char> code={0x53,0x56,0x57,0x55,0x81,0xEC,0x40,0x03,0,0,
        0x8B,0x84,0x24,0x54,0x03,0,0,0x89,0x44,0x24,0x10,
        0x8B,0xAC,0x24,0x58,0x03,0,0,
        0x8B,0xB4,0x24,0x5C,0x03,0,0,
        0x8D,0xBC,0x24,0xE0,0x02,0,0,0xB9,0x10,0,0,0,0xF3,0xA5,
        0xC7,0x84,0x24,0xDC,0x02,0,0,0x01,0,0,0,0xD9,0xE8,0xF9};
    const std::uint32_t begin=pop?0x4B5537u:0x4B54C3u,end=pop?0x4B5574u:0x4B54DDu;
    std::vector<unsigned char> native(end-begin);
    Check(image.Read(begin,native.data(),native.size()),"actual native decoder instruction slice read from pinned PE");
    const auto start=code.size();code.insert(code.end(),native.begin(),native.end());
    // Native EDI is restored to state. Observe native flags, EBP, ECX and
    // projection stack depth BEFORE changing them in the private epilogue.
    const unsigned char suffix[]={0x9C,0x58,0x89,0x87,0x80,0x04,0,0,
        0x89,0xAF,0x84,0x04,0,0,0x89,0x8F,0x88,0x04,0,0,
        0x8B,0x84,0x24,0xDC,0x02,0,0,0x89,0x87,0x8C,0x04,0,0,
        0x81,0xC4,0x40,0x03,0,0,0x5D,0x5F,0x5E,0x5B,0xC3};
    code.insert(code.end(),suffix,suffix+sizeof(suffix));
    decoder.memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    Check(decoder.memory!=nullptr,"private decoder executable allocated");std::memcpy(decoder.memory,code.data(),code.size());
    decoder.hook=decoder.memory+start+(pop?0x4B5569u:0x4B54D0u)-begin;
    decoder.resume=decoder.memory+start+(pop?0x4B5570u:0x4B54D7u)-begin;
    const unsigned char count[]={0xB9,0x10,0,0,0};
    Check(!std::memcmp(decoder.hook,count,sizeof(count)),"actual decoder hook owns exactly five-byte MOV ECX,16");
    Check(decoder.hook[5]==0xF3&&decoder.hook[6]==0xA5,"original REP remains outside overwritten count instruction");
    FlushInstructionCache(GetCurrentProcess(),decoder.memory,code.size());
}
std::uint32_t Word(const std::array<unsigned char,0x500>& state,unsigned offset){std::uint32_t value=0;std::memcpy(&value,state.data()+offset,4);return value;}
}
int main(int argc,char** argv){
    static_assert(sizeof(void*)==4,"native packet-tail ABI is x86");
    if(argc!=2)return 2;
    wchar_t path[MAX_PATH]{};char digest[65]{};
    Check(MultiByteToWideChar(CP_UTF8,0,argv[1],-1,path,MAX_PATH)>0&&HashFile(path,digest)&&IsSupportedExecutableHash(digest),"actual source executable SHA matches supported native decoder");
    NativeHudFixture::Image image;Check(image.Load(argv[1]),"actual native PE loaded read-only");
    NativeSourceProof(image);
    Decoder copy,pop;Build(copy,image,false);Build(pop,image,true);
    using Decode=void(__cdecl*)(void*,void*,const float*);
    alignas(16)std::array<unsigned char,0x500> state{};
    std::array<unsigned char,0xB0> packet{};std::array<float,16> saved{};saved[0]=2.5f;
    const float native=3.5f;std::memcpy(packet.data()+0x18,&native,4);
    expectedState=reinterpret_cast<std::uintptr_t>(state.data());expectedPacket=reinterpret_cast<std::uintptr_t>(packet.data());
    std::uint32_t baselineFlags[2]{};
    for(unsigned phase=0;phase<2;++phase){reinterpret_cast<Decode>(phase?pop.memory:copy.memory)(state.data(),packet.data(),saved.data());
        float result=0;std::memcpy(&result,state.data()+0x3F8,4);Check(result==(phase?saved[0]:native),"unhooked native copy/pop payload is baseline");
        baselineFlags[phase]=Word(state,0x480);Check(Word(state,0x48C)==(phase?0u:1u),"actual native projection stack pop decrements depth exactly once");}
    Check(MH_Initialize()==MH_OK,"real MinHook initialized for private decoder");
    KL1ProjectionTailOriginal=copy.resume;KL1ProjectionPopOriginal=pop.resume;
    Check(MH_CreateHook(copy.hook,reinterpret_cast<void*>(&KL1ProjectionTailHook),nullptr)==MH_OK&&MH_EnableHook(copy.hook)==MH_OK,"actual copy-count instruction hooked without shared FSTP overwrite");
    Check(MH_CreateHook(pop.hook,reinterpret_cast<void*>(&KL1ProjectionPopHook),nullptr)==MH_OK&&MH_EnableHook(pop.hook)==MH_OK,"actual pop-count instruction hooked without decoder stack arithmetic overwrite");
    NativeProjectionTailKL::observer=Legacy;NativeProjectionTailKL::packetObserver=Packet;
    for(unsigned phase=0;phase<2;++phase)for(unsigned i=0;i<10;++i){
        expectedPhase=phase;expectedNative=saved[0];const auto beforeLegacy=legacyCalls;
        alignas(16)const std::uint32_t sentinel[4]={0x11223344,0x55667788,0x99AABBCC,0xDDEEFF00};
        alignas(16)std::uint32_t xmm[4]{};const double previous=123.25;double after=0;
        __asm__ __volatile__("fldl %0\n\tmovdqu %1,%%xmm0"::"m"(previous),"m"(sentinel):"xmm0");
        reinterpret_cast<Decode>(phase?pop.memory:copy.memory)(state.data(),packet.data(),saved.data());
        __asm__ __volatile__("fstpl %0\n\tmovdqu %%xmm0,%1":"=m"(after),"=m"(xmm));
        Check(after==previous,"actual copy/pop x87 stack survives both callback clobbers and native FSTP");
        Check(!std::memcmp(xmm,sentinel,sizeof(xmm)),"actual copy/pop XMM state survives callbacks");
        Check(Word(state,0x480)==baselineFlags[phase],"actual decoder EFLAGS match unhooked copy/pop baseline");
        Check(Word(state,0x484)==expectedPacket&&Word(state,0x488)==0,"original queued EBP and native REP ECX survive callbacks");
        Check(legacyCalls==beforeLegacy+(phase?0u:1u),"legacy cinematic observer is copy-only and never runs on pop");
        float value=0;std::memcpy(&value,state.data()+0x3F8,4);Check(value==(phase?9.5f:8.5f),"optional observer executes after native projection copy/pop before later draw");
    }
    Check(legacyCalls==10&&packetCalls==20,"optional callback receives both lifetime phases exactly once");
    NativeProjectionTailKL::packetObserver=nullptr;expectedPhase=0;
    reinterpret_cast<Decode>(copy.memory)(state.data(),packet.data(),saved.data());
    Check(legacyCalls==11&&packetCalls==20,"optional observer disabled preserves historical callback ABI");
    expectedPhase=1;reinterpret_cast<Decode>(pop.memory)(state.data(),packet.data(),saved.data());
    float value=0;std::memcpy(&value,state.data()+0x3F8,4);Check(value==saved[0]&&legacyCalls==11&&packetCalls==20,"disabled optional observer leaves native pop untouched");
    Check(MH_DisableHook(copy.hook)==MH_OK&&MH_DisableHook(pop.hook)==MH_OK&&MH_RemoveHook(copy.hook)==MH_OK&&MH_RemoveHook(pop.hook)==MH_OK,"both private hooks removed cleanly");
    NativeProjectionTailKL::observer=nullptr;MH_Uninitialize();
    std::printf("Actual native projection copy/pop packet ABI: %u checks PASS; CPU/private native decoder only.\n",checks);return 0;
}
